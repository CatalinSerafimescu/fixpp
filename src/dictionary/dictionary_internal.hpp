// SPDX-License-Identifier: AGPL-3.0-or-later
// src/dictionary/dictionary_internal.hpp
//
// Private to `src/dictionary/`. Defines `detail::dict_metadata_handle` — the
// heap-pinned metadata block holding every PMR-allocated table the public
// `Dictionary` accessors look into. Allocated by `XmlLoader::load*` via
// `std::allocate_shared` over `std::pmr::polymorphic_allocator` so the
// control-block deallocator returns memory to the originating `mr` per
// `[2c §4.3]` C-R2-P1-1.
//
// Not exposed from `include/fixpp/dict/`; included only by
// `src/dictionary/dictionary.cpp` and `src/dictionary/xml_loader.cpp`.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <fixpp/dict/component_ref.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/group_ref.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace fixpp::dict::detail {

// Bytewise (locale-independent) `unsigned char` comparator over `string_view`
// per research.md D-6 / Gate A round 1 P2.4. Shared by xml_loader.cpp (build
// ordering) and dictionary.cpp (name lookups) — the two TUs that include this
// private header — so the sort key is byte-identical across build and query.
[[nodiscard]] inline int bytewise_compare(std::string_view a, std::string_view b) noexcept {
    auto const n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        auto const lhs = static_cast<unsigned char>(a[i]);
        auto const rhs = static_cast<unsigned char>(b[i]);
        if (lhs != rhs) {
            return lhs < rhs ? -1 : 1;
        }
    }
    if (a.size() == b.size()) {
        return 0;
    }
    return a.size() < b.size() ? -1 : 1;
}

// (offset, length) into the metadata-handle's name string pool. Indices are
// stable for the handle's lifetime because the pool is reserved to its final
// size before any string_view is bound (see xml_loader.cpp build sequence).
struct NameSlice {
    std::uint32_t offset{0};
    std::uint32_t length{0};
};

// Per-MsgType run inside the flat `fields_` array.
struct MsgFieldsRun {
    std::uint32_t start{0};
    std::uint32_t count{0};
};

// Lookup helper: a name (slice into the pool) bound to a payload index.
struct NamedIndex {
    // cppcheck-suppress unusedStructMember  // read via lambda in dictionary.cpp component_impl
    // (cppcheck lambda-member-access limitation)
    NameSlice name{};
    std::uint32_t index{0};
};

// 074-orchestra-native-reader (FR-002): per-tag run into the flat enum-value
// store. Sorted by `tag` ascending; binary-searched by `enum_values_impl`.
struct EnumRun {
    std::uint16_t tag{0};
    std::uint32_t start{0};
    std::uint32_t count{0};
};

// Field-name → tag entry (sorted by name bytewise).
struct FieldNameEntry {
    // cppcheck-suppress unusedStructMember  // read via lambda in dictionary.cpp field_by_name_impl
    // (cppcheck lambda-member-access limitation)
    NameSlice name{};
    std::uint16_t tag{0};
};

// 083 T025 — Entity 2, the feature's ONE new persistent structure
// (data-model.md Entity 2; contracts/group_ctx_delims.md C-2.1/C-2.2/C-2.3).
// One record = one group context and the delimiter its own declaration gives
// it, replacing the global first-seen `GroupDef.first_field_tag` as the
// SOURCE OF TRUTH for delimiter resolution.
//
// `msg_type` is deliberately NOT a member: records live in a per-message run
// (`per_msg_group_ctx_delim_offsets_`, parallel to `messages_`), exactly as
// `msg_group_required_pool_` does, so the message scoping IS the run and there
// is no second copy of the msg_type to keep in sync.
struct GroupCtxDelim {
    // Ancestor count tags, OUTERMOST FIRST, EXCLUDING this group's own
    // `no_tag` (C-1.3 / Entity 1's invariant). Same convention as
    // `table_view::group_ctx_` and the test oracle's `GroupContextKey`, so the
    // three cannot disagree about what a context is.
    std::array<std::uint16_t, kMaxGroupContextDepth> parent_path{};
    std::uint16_t no_tag{0};
    // Never 0 for a STORED record: recording 0 is the FR-006 fail-closed
    // condition, not a storable state (data-model.md Entity 2, validation
    // rules). 0 is therefore usable as the accessor's "no record" answer
    // without colliding with a legitimate value.
    std::uint16_t delimiter{0};
    std::uint8_t depth{0};
};

// C-2.2 ("keyed IDENTICALLY to `make_group_ctx_key`") made structural rather
// than asserted: ONE function builds the key, and both the store side
// (xml_loader) and the lookup side (`group_ctx_delimiter_impl`) go through it,
// so the depth clamp cannot drift between them. The clamp mirrors
// `make_group_ctx_key`'s `std::min(parent_path.size(), kMaxGroupContextDepth)`
// (include/fixpp/dict/table_view.hpp:229-233) — a path deeper than K=16 clamps
// rather than growing unbounded.
[[nodiscard]] inline GroupCtxDelim make_group_ctx_delim(
    std::span<std::uint16_t const> parent_path, std::uint16_t no_tag,
    std::uint16_t delimiter) noexcept {
    GroupCtxDelim rec;
    rec.no_tag = no_tag;
    rec.delimiter = delimiter;
    rec.depth = static_cast<std::uint8_t>(
        std::min<std::size_t>(parent_path.size(), kMaxGroupContextDepth));
    for (std::uint8_t i = 0; i < rec.depth; ++i) {
        rec.parent_path[i] = parent_path[i];
    }
    return rec;
}

// Ordering within a message's run: by `no_tag`, then by the path, so the
// lookup can binary-search on `no_tag` and walk the (normally one-element)
// equal range. Also the dedup key at the emission site.
[[nodiscard]] inline bool group_ctx_delim_less(GroupCtxDelim const& a,
                                               GroupCtxDelim const& b) noexcept {
    if (a.no_tag != b.no_tag) {
        return a.no_tag < b.no_tag;
    }
    if (a.depth != b.depth) {
        return a.depth < b.depth;
    }
    for (std::uint8_t i = 0; i < a.depth; ++i) {
        if (a.parent_path[i] != b.parent_path[i]) {
            return a.parent_path[i] < b.parent_path[i];
        }
    }
    return false;
}

// 083 T026/T027 — the loaders' shared per-message capture state for Entity 2
// (research.md D-1 "first-emission capture"; contracts/group_ctx_delims.md
// C-1.1/C-1.2/C-1.3). Defined here rather than in either loader so the XML and
// Orchestra walks cannot drift apart: FR-005 / C-1.5 records that a one-loader
// fix is a half-restructure.
//
// `path` and `pending` are pushed and popped TOGETHER around each nested-group
// recursion and are parallel: `path.back()` is the innermost open group's own
// count tag, `pending.back()` is that group's delimiter slot — 0 until the
// FIRST FieldRef emitted at its level sets it. Because component members
// expand inline at the enclosing level and a nested group's count tag is
// pushed at the OUTER level before descent, "first emitted at this level" is
// already document order: FR-003 and FR-004 fall out and NO second traversal
// is written. Adding one would recreate the drift that caused the defect.
//
// A NULL `DelimCapture*` is the C-1.1 gate: the component-cache and
// group-cache expansions are not message-scoped, so they must contribute
// NOTHING. A null pointer says that structurally — a discard vector would
// still manufacture records keyed to no message.
struct DelimCapture {
    std::vector<std::uint16_t> path;     // ancestors, outermost first
    std::vector<std::uint16_t> pending;  // parallel to `path`; 0 = not yet captured
    std::vector<GroupCtxDelim> out;      // this message's records
};

// The FIRST emission at the currently-open group's level wins. Called at every
// site that pushes a FieldRef, so `no-op unless a group is open and its slot is
// still empty` is the whole rule. Deliberately NOT an unconditional assignment:
// an `=` here would record the LAST member instead of the first, invert the
// delimiter, and still compile and pass every test that does not pin order —
// the same hazard the census oracle's `try_emplace` guards against.
inline void capture_first_emission(DelimCapture* cap, std::uint16_t tag) noexcept {
    if (cap != nullptr && !cap->pending.empty() && cap->pending.back() == 0) {
        cap->pending.back() = tag;
    }
}

// 083 T041 — the FR-023 / C-3.4 completeness invariant, shared by both loaders
// (C-1.5: a one-loader fix is a half-restructure).
//
// Every group context `as_table_view()` will enumerate and register MUST have
// an Entity-2 record. Returns the offending `no_tag` on the first violation, or
// 0 if the message is complete.
//
// The registration predicate is mirrored EXACTLY from
// `Dictionary::as_table_view()` (`src/dictionary/dictionary.cpp:445-463`):
// a context exists iff, on this message's deduped field run, the count tag's
// `FieldRef.type` is `NumInGroup` AND at least one `FieldRef` has
// `group_no_tag == no_tag` — C-3.4a's `!members.empty()` leg, which is what
// keeps a message that merely REUSES a NumInGroup-typed tag as a plain scalar
// from reading as a violation.
//
// Enforced at `finalize()` and deliberately NOT at `as_table_view()`, which is
// contractually non-throwing (established by 072, L-063-4) and must stay so —
// which is what makes a consumer-side lookup miss unreachable by construction
// rather than merely unobserved.
[[nodiscard]] inline std::uint16_t find_context_without_delim_record(
    std::span<FieldRef const> fields, std::span<GroupCtxDelim const> delims) noexcept {
    // Immediate-parent chain over count tags only, as dictionary.cpp builds it.
    std::vector<std::pair<std::uint16_t, std::uint16_t>> immediate_parent;
    for (auto const& fr : fields) {
        if (fr.type == field_data_type::NumInGroup) {
            immediate_parent.emplace_back(fr.tag, fr.group_no_tag);
        }
    }
    auto parent_of = [&](std::uint16_t tag) noexcept -> std::uint16_t {
        for (auto const& [t, p] : immediate_parent) {
            if (t == tag) {
                return p;
            }
        }
        return 0;
    };
    for (auto const& fr : fields) {
        if (fr.type != field_data_type::NumInGroup) {
            continue;
        }
        bool has_members = false;
        for (auto const& m : fields) {
            if (m.group_no_tag == fr.tag) {
                has_members = true;
                break;
            }
        }
        if (!has_members) {
            continue;  // C-3.4a: scalar reuse contributes no context
        }
        std::vector<std::uint16_t> path;
        std::uint16_t cur = fr.group_no_tag;
        while (cur != 0 && path.size() < kMaxGroupContextDepth) {
            path.push_back(cur);
            cur = parent_of(cur);
        }
        std::reverse(path.begin(), path.end());
        GroupCtxDelim const probe = make_group_ctx_delim(path, fr.tag, /*delimiter=*/0);
        bool found = false;
        for (auto const& rec : delims) {
            if (!group_ctx_delim_less(probe, rec) && !group_ctx_delim_less(rec, probe)) {
                found = true;
                break;
            }
        }
        if (!found) {
            return fr.tag;
        }
    }
    return 0;
}

class dict_metadata_handle {
public:
    explicit dict_metadata_handle(std::pmr::memory_resource* mr) noexcept
        : mr_(mr),
          name_pool_(mr),
          fields_(mr),
          per_msg_field_offsets_(mr),
          required_fields_pool_(mr),
          per_msg_required_offsets_(mr),
          components_(mr),
          components_by_name_(mr),
          component_fields_(mr),
          groups_(mr),
          group_fields_(mr),
          group_required_pool_(mr),
          group_required_offsets_(mr),
          msg_group_required_pool_(mr),
          per_msg_group_required_offsets_(mr),
          group_ctx_delim_pool_(mr),
          per_msg_group_ctx_delim_offsets_(mr),
          enum_values_(mr),
          enum_runs_(mr),
          messages_(mr),
          field_by_name_(mr) {}

    // Resolve a NameSlice against the (now-stable) name_pool_ data pointer.
    [[nodiscard]] std::string_view name_at(NameSlice ns) const noexcept {
        if (ns.length == 0) {
            return std::string_view{};
        }
        return std::string_view{name_pool_.data() + ns.offset, ns.length};
    }

    // Per-MsgType field-array lookup. Returns the run; `count == 0` if the
    // MsgType is not in the dictionary.
    [[nodiscard]] MsgFieldsRun find_msg_fields(std::string_view msg_type) const noexcept;

    // Per-MsgType required-fields run.
    [[nodiscard]] MsgFieldsRun find_msg_required(std::string_view msg_type) const noexcept;

    // cppcheck-suppress unusedFunction  // public-ish accessor for the originating PMR; kept for
    // future copy/share ops
    [[nodiscard]] std::pmr::memory_resource* mr() const noexcept { return mr_; }

    // ---- Public-API-shaped accessors used by Dictionary methods ----

    [[nodiscard]] FieldRef field_ref_impl(std::string_view msg_type,
                                          std::uint16_t tag) const noexcept;

    [[nodiscard]] std::span<std::uint16_t const> required_fields_impl(
        std::string_view msg_type) const noexcept;

    [[nodiscard]] std::uint16_t group_first_field_impl(std::uint16_t no_tag) const noexcept;

    [[nodiscard]] std::uint16_t length_pair_data_tag_impl(std::uint16_t length_tag) const noexcept;

    [[nodiscard]] std::optional<std::uint16_t> field_by_name_impl(
        std::string_view name) const noexcept;

    [[nodiscard]] std::optional<ComponentRef> component_impl(std::string_view name) const noexcept;

    [[nodiscard]] std::optional<GroupRef> group_impl(std::uint16_t no_tag) const noexcept;

    // Returns the flat FieldRef run for a component (by component_id).
    // Returns empty span if component_id is out of range or field_count == 0.
    [[nodiscard]] std::span<FieldRef const> component_fields_impl(
        std::uint16_t component_id) const noexcept;

    // Returns the flat FieldRef run for a group (by no_tag).
    // Returns empty span if the group has no fields recorded.
    [[nodiscard]] std::span<FieldRef const> group_fields_impl(std::uint16_t no_tag) const noexcept;

    // Gate B r1 F1 (fixpp#201): the GROUP-RELATIVE (bare/global, first-seen)
    // direct required-member set for a group — `own_req` gated by a
    // component-AND accumulator RESET at this group's own boundary (NOT the
    // message-root accumulator that feeds `required_fields_impl`). Consumed
    // by `Dictionary::as_table_view()`'s legacy bare-store loop in place of
    // the old blind `gfr.rule==Required && gfr.group_no_tag==no_tag` filter.
    [[nodiscard]] std::span<std::uint16_t const> group_required_members_impl(
        std::uint16_t no_tag) const noexcept;

    // Gate B r1 F1 (fixpp#201): the CONTEXT-exact (per-message) twin — every
    // (enclosing group no_tag, tag) pair this message's own expansion reached
    // with `own_req && group-scope-component-AND-since-nearest-enclosing-
    // group-boundary` true. Consumed by `Dictionary::as_table_view()`'s
    // context-store loop (filtered to the no_tag being populated) in place of
    // the old blind `m.rule==Required` filter.
    [[nodiscard]] std::span<std::pair<std::uint16_t, std::uint16_t> const>
    msg_group_required_pairs_impl(std::string_view msg_type) const noexcept;

    // 083 T025 (Entity 2): every per-context delimiter record this message's
    // own expansion produced, sorted by `group_ctx_delim_less`. Consumed by
    // `Dictionary::as_table_view()`'s context-store loop in place of the
    // global `group_first_field(no_tag)`, and by the loaders' finalize()
    // completeness invariant (FR-023 / C-3.4).
    [[nodiscard]] std::span<GroupCtxDelim const> msg_group_ctx_delims_impl(
        std::string_view msg_type) const noexcept;

    // 083 T025 (Entity 2): the delimiter for ONE context. Returns 0 when no
    // record exists — which is distinguishable from a real answer because a
    // stored record's delimiter is never 0 (FR-006). There is deliberately NO
    // fallback to `group_first_field(no_tag)`: that would reinstate the exact
    // defect this feature removes (data-model.md Entity 2, "no silent
    // fallback exists"). Callers that need a fail-closed disposition act on
    // the 0.
    [[nodiscard]] std::uint16_t group_ctx_delimiter_impl(
        std::string_view msg_type, std::span<std::uint16_t const> parent_path,
        std::uint16_t no_tag) const noexcept;

    // 074 (FR-002): enum {value, description} pairs for `tag`; empty span if the
    // tag has no codeset entry. Binary-searches `enum_runs_` (sorted by tag).
    [[nodiscard]] std::span<EnumValueRef const> enum_values_impl(std::uint16_t tag) const noexcept;

    [[nodiscard]] std::span<MessageEntry const> messages_impl() const noexcept {
        return std::span<MessageEntry const>{messages_};
    }

    // 003-dictionary-codegen (RC#5 — F1 IR data path): the codegen tool needs
    // the FULL per-message field run (required + optional) and tag→FIX-field-
    // name, neither of which the runtime-MVS lookup surface exposed. Build-
    // time codegen-enumeration only; not on any runtime hot path.
    [[nodiscard]] std::span<FieldRef const> message_fields_impl(
        std::string_view msg_type) const noexcept;
    [[nodiscard]] std::string_view field_name_impl(std::uint16_t tag) const noexcept;

    [[nodiscard]] session_version version_impl() const noexcept { return version_; }

    // ---- Build-time state (populated by XmlLoader) ----

    std::pmr::memory_resource* mr_;

    // Name string pool. Appended-to during build. Reserved to its final
    // capacity before any `std::string_view` is bound into it (xml_loader.cpp
    // builds an exact byte budget in a first pass over the DOM).
    std::pmr::vector<char> name_pool_;

    // Per-message field runs concatenated; per-msg run pointed to by
    // `per_msg_field_offsets_[msg_index]`.
    std::pmr::vector<FieldRef> fields_;

    // Parallel to `messages_`. `per_msg_field_offsets_[i]` is the run for the
    // i-th message in `messages_`.
    std::pmr::vector<MsgFieldsRun> per_msg_field_offsets_;

    // Per-message required-fields tag list (flat, with runs).
    std::pmr::vector<std::uint16_t> required_fields_pool_;
    std::pmr::vector<MsgFieldsRun> per_msg_required_offsets_;

    // Components (sorted by component_id == declaration order).
    std::pmr::vector<ComponentRef> components_;
    // Component-name → index lookup (sorted bytewise by name).
    std::pmr::vector<NamedIndex> components_by_name_;
    // Flat FieldRef table for per-component field runs.
    // ComponentRef::first_field_index / field_count index into this vector.
    std::pmr::vector<FieldRef> component_fields_;

    // Groups (sorted by `no_tag` ascending).
    std::pmr::vector<GroupRef> groups_;
    // Flat FieldRef table for per-group field runs.
    // GroupRef::first_field_index / field_count index into this vector.
    std::pmr::vector<FieldRef> group_fields_;

    // Gate B r1 F1 (fixpp#201): bare/global group-relative required-member
    // store. `group_required_offsets_[i]` is the run for `groups_[i]` (SAME
    // index — populated in lockstep in the same finalize() loop that builds
    // `groups_`/`group_fields_`, so no separate sort/lookup key is needed).
    std::pmr::vector<std::uint16_t> group_required_pool_;
    std::pmr::vector<MsgFieldsRun> group_required_offsets_;

    // Gate B r1 F1 (fixpp#201): per-message context-exact group-required
    // (no_tag,tag) pairs. `per_msg_group_required_offsets_[i]` is the run for
    // the i-th message in `messages_` (SAME index as `per_msg_field_offsets_`).
    std::pmr::vector<std::pair<std::uint16_t, std::uint16_t>> msg_group_required_pool_;
    std::pmr::vector<MsgFieldsRun> per_msg_group_required_offsets_;

    // 083 T025 (Entity 2 — C-2.1/C-2.3): per-message per-context delimiter
    // records. `per_msg_group_ctx_delim_offsets_[i]` is the run for the i-th
    // message in `messages_` (SAME index as `per_msg_field_offsets_`).
    // PMR-allocated on the caller's `memory_resource` alongside the handle's
    // other tables; written ONCE during load and never mutated afterwards.
    // C-2.1's "no mutation path exposed" holds at the surface that matters:
    // this handle is private to `src/dictionary/` (see the file header), and
    // the public `Dictionary` exposes only const accessors over it — there is
    // no setter anywhere in `include/fixpp/dict/`.
    std::pmr::vector<GroupCtxDelim> group_ctx_delim_pool_;
    std::pmr::vector<MsgFieldsRun> per_msg_group_ctx_delim_offsets_;

    // 074 (FR-002): flat enum-value store (value+description string_views into
    // the FROZEN name_pool_ — bound in finalize() after the pool is stable) and
    // the per-tag runs index (sorted by tag). Empty for XmlLoader dictionaries.
    std::pmr::vector<EnumValueRef> enum_values_;
    std::pmr::vector<EnumRun> enum_runs_;

    // Messages (sorted bytewise by `msg_type`).
    std::pmr::vector<MessageEntry> messages_;

    // Field-name → tag (sorted by name bytewise).
    std::pmr::vector<FieldNameEntry> field_by_name_;

    session_version version_{session_version::Unknown};
};

// 083 T049 (W-11a) — TEST SEAM. Counts entries into
// `Dictionary::as_table_view()`, so the C-ABI witness can assert the view is
// built ONCE per opened session and ZERO times per message (C-9.2a / D-13;
// `dict->as_table_view()` inside the commit path is barred by [const §XV.1]).
//
// Declared in this INTERNAL header, deliberately — not in the public
// `include/fixpp/dict/dictionary.hpp` — mirroring the `fixpp_capi::detail`
// live-state-counter precedent (`src/capi/capi_internal.hpp:496-503`). No
// installed public surface is touched. `tests/capi/` already includes internal
// headers directly.
void bump_as_table_view_call_count() noexcept;
[[nodiscard]] std::uint64_t as_table_view_call_count() noexcept;
void reset_as_table_view_call_count() noexcept;

}  // namespace fixpp::dict::detail
