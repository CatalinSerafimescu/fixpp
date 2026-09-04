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
#include <unordered_map>
#include <unordered_set>
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
[[nodiscard]] inline GroupCtxDelim make_group_ctx_delim(std::span<std::uint16_t const> parent_path,
                                                        std::uint16_t no_tag,
                                                        std::uint16_t delimiter) noexcept {
    GroupCtxDelim rec;
    rec.no_tag = no_tag;
    rec.delimiter = delimiter;
    rec.depth =
        static_cast<std::uint8_t>(std::min<std::size_t>(parent_path.size(), kMaxGroupContextDepth));
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

// fixpp#264 — the ONE walk that builds a group context's ancestor chain: count
// tags from `start` outward to the root, reversed to OUTERMOST FIRST, matching
// `GroupCtxDelim::parent_path` and `group_ctx_key::parent_path`. Both EXCLUDE
// the group's own `no_tag` (C-1.3 / Entity 1), so callers pass the group count
// tag's OWN `group_no_tag`, not its `no_tag`.
//
// This walk is deliberately UNCLAMPED. The K = `kMaxGroupContextDepth` clamp
// belongs to `make_group_ctx_delim` / `make_group_ctx_key` alone, which keep
// the first — i.e. the OUTERMOST — K entries. Applying a clamp here instead
// stops the walk early and therefore keeps the INNERMOST K, which is a
// DIFFERENT key for the same context whenever the chain is longer than K. That
// is what fixpp#264 was: the FR-023 completeness probe clamped during its walk
// while `as_table_view()` and the loaders' capture path clamped after theirs,
// so a chain of K+1 or more produced two keys, the probe's `lower_bound` missed
// a record that was present, and a well-formed dictionary was rejected at load.
// One walk here plus one clamp in the key builders is what keeps the two sides
// from drifting again; do not reintroduce a bound in this function.
//
// A tag absent from `immediate_parent` is a root. A CYCLE yields `nullopt`, and
// that distinction is the whole point: it must NOT yield a shortened path.
//
// Truncating at the repeat looks fail-closed and is not. The truncated array is
// a WELL-FORMED key, so it can COLLIDE with a context the loaders legitimately
// registered rather than miss every one of them. A self-parented group
// (`immediate_parent[G] == G`) truncates to `[G]` — exactly the key of that
// group's own inner occurrence — so the completeness probe MATCHES a record
// instead of reporting a violation, and the dictionary loads with the outer
// context absent and the count tag injected into the inner one's member set.
// That is an acceptance change at ANY depth, not just past the clamp, and it
// turns a rejection into a silently wrong answer.
//
// This is reachable, not theoretical. Each loader reduces a message's field run
// to one `FieldRef` per tag with an UNSTABLE sort (`std::ranges::sort` +
// `unique`, `xml_loader.cpp` / `orchestra_loader.cpp`), so which of two
// equal-tag occurrences survives is unspecified — and when the inner occurrence
// of a self-nested group wins, `immediate_parent[G] == G` is what the relation
// holds. Do not re-derive how often that happens: it depends on the standard
// library's sort, so any count measured here would be a fact about one
// toolchain. The CONDITION is that the relation is not guaranteed acyclic; the
// walk owes termination and a non-colliding answer regardless.
//
// Both callers treat `nullopt` as a violation: the FR-023 probe reports the
// offending tag (rejecting the load, which is what this did before #264 shared
// the walk) and `as_table_view()` skips the context rather than registering a
// colliding one.
//
// The bound is PIGEONHOLE, not a scan. `immediate_parent` maps each tag to one
// parent, so a walk that has pushed more entries than the relation has keys must
// have revisited one. That is exact, costs O(depth), and — unlike a
// `std::ranges::find` over the accumulated path — does not make a deep chain
// quadratic per walk and cubic across a dictionary.
//
// The walk is otherwise UNCLAMPED by design. The K = `kMaxGroupContextDepth`
// clamp belongs to `make_group_ctx_delim` / `make_group_ctx_key` alone, which
// keep the first — i.e. the OUTERMOST — K entries. Clamping here instead stops
// the walk early and so keeps the INNERMOST K, a different key for the same
// context once the chain is longer than K. That was #264. One walk plus one
// clamp, in that order, is what keeps the two sides from drifting again — do not
// reintroduce a depth bound here, INCLUDING a "we discard past K anyway" bound,
// which reproduces the original defect exactly.
[[nodiscard]] inline std::optional<std::vector<std::uint16_t>> group_parent_path(
    std::uint16_t start, std::unordered_map<std::uint16_t, std::uint16_t> const& immediate_parent) {
    std::vector<std::uint16_t> path;
    std::uint16_t cur = start;
    while (cur != 0) {
        if (path.size() > immediate_parent.size()) {
            return std::nullopt;  // pigeonhole: a tag was revisited ⇒ cycle
        }
        path.push_back(cur);
        auto const it = immediate_parent.find(cur);
        cur = (it != immediate_parent.end()) ? it->second : std::uint16_t{0};
    }
    std::ranges::reverse(path);
    return path;
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
// One captured record PLUS the UNCLAMPED chain it was built from. The record's
// own `parent_path` is already clamped to `kMaxGroupContextDepth`, which is
// exactly what makes the extra copy necessary: past K the clamp is lossy, so two
// genuinely different contexts can produce byte-identical keys and the key alone
// can no longer tell a benign duplicate from a collision. `full_path` is what
// distinguishes them, and it is loader-local — nothing in `GroupCtxDelim`,
// `group_ctx_key`, the store, or any query path changes.
struct CapturedDelim {
    GroupCtxDelim rec;
    std::vector<std::uint16_t> full_path;  // ancestors, outermost first, UNCLAMPED
};

struct DelimCapture {
    std::vector<std::uint16_t> path;     // ancestors, outermost first
    std::vector<std::uint16_t> pending;  // parallel to `path`; 0 = not yet captured
    std::vector<CapturedDelim> out;      // this message's records
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
// Gate B r1 F1 (fixpp#261 PR review, 2026-08-13) — the predicate below is
// STRUCTURAL, matching `as_table_view()` exactly. History: 082 made
// `as_table_view()` decide group-ness structurally (`group_first_field(t) !=
// 0`, `dictionary.cpp:489/528/533`) while this sweep still tested
// `fr.type == NumInGroup`, so on FIX 4.0/4.1/4.2 — whose `<group>` count
// fields are legacy `INT`-typed — `as_table_view()` registered 4/7/18
// contexts this sweep never examined. Closed by widening both
// `find_context_without_delim_record()` and `find_incomplete_group_context()`
// to take a caller-supplied, sorted `std::span<std::uint16_t const>` of
// structural group tags, and testing SET MEMBERSHIP instead of `fr.type`.
//
// The set is `{tag : group_index_by_no_tag_.contains(tag) AND
// groups_[idx].first_field_tag != 0}`, built by each loader from its OWN
// build-time `groups_` / `group_index_by_no_tag_` — NOT from
// `dict_metadata_handle::groups_` / `group_first_field_impl()` (the
// handle-side table), which is not filled until `xml_loader.cpp:1157-1207` /
// `orchestra_loader.cpp:980-1019`. That re-point was tried and reverted: at
// sweep time the handle-side table is EMPTY, so every tag reads "not a
// group", the sweep reports no violation, and FR-023 silently stops
// enforcing — three `LoaderDisposition` tests went RED on that change. The
// loader-side `groups_` table, by contrast, IS final at sweep time: both
// loaders finish their first-seen `first_field_tag` projection immediately
// before calling this sweep (`xml_loader.cpp:1038-1047`,
// `orchestra_loader.cpp:881-890`), so there was never an ordering problem to
// solve — only the wrong table being read.
//
// The `first_field_tag != 0` filter is load-bearing: dropping it would widen
// the sweep past its consumer and manufacture false load rejections on the
// declared-but-message-unreachable groups `as_table_view()` also declines to
// register (tags 384/627 on FIX50/SP1/SP2).
//
// The registration predicate otherwise still mirrors `Dictionary::
// as_table_view()`: a context exists iff, on this message's deduped field
// run, the count tag is a structural group tag AND at least one `FieldRef`
// has `group_no_tag == no_tag` — C-3.4a's `!members.empty()` leg, which is
// what keeps a message that merely REUSES a group tag as a plain scalar from
// reading as a violation.
//
// Enforced at `finalize()` and deliberately NOT at `as_table_view()`, which is
// contractually non-throwing (established by 072, L-063-4) and must stay so —
// which is what makes a consumer-side lookup miss unreachable by construction
// rather than merely unobserved.
[[nodiscard]] inline std::uint16_t find_context_without_delim_record(
    std::span<FieldRef const> fields, std::span<GroupCtxDelim const> delims,
    // Gate B r1 F1: sorted, unique structural group tags — see the doc
    // comment above for the exact set definition and why it is loader-side.
    std::span<std::uint16_t const> structural_group_tags) noexcept {
    // /simplify: the three scans below were each linear, making this
    // O(G²)/O(depth·G²) in groups-per-message on the largest dictionaries
    // (FIX50SP2, Orchestra). Each is replaced with the lookup structure the
    // sibling code already uses for the same job — `as_table_view()` builds
    // its immediate-parent chain with an unordered_map (dictionary.cpp), and
    // `group_ctx_delimiter_impl` resolves the very same `delims` data with
    // `lower_bound` rather than a scan. Load-time only, but the correct
    // pattern was a few lines away in both cases.
    //
    // Immediate-parent chain over count tags only, as dictionary.cpp builds it.
    std::unordered_map<std::uint16_t, std::uint16_t> immediate_parent;
    // Every tag that is some group's container — the C-3.4a "has members" test.
    std::unordered_set<std::uint16_t> has_members;
    for (auto const& fr : fields) {
        if (std::ranges::binary_search(structural_group_tags, fr.tag)) {
            immediate_parent.emplace(fr.tag, fr.group_no_tag);
        }
        if (fr.group_no_tag != 0) {
            has_members.insert(fr.group_no_tag);
        }
    }
    for (auto const& fr : fields) {
        if (!std::ranges::binary_search(structural_group_tags, fr.tag)) {
            continue;
        }
        if (!has_members.contains(fr.tag)) {
            continue;  // C-3.4a: scalar reuse contributes no context
        }
        // fixpp#264: the chain is walked UNCLAMPED and `make_group_ctx_delim`
        // applies the K clamp, exactly as the registration side does. Clamping
        // here instead kept the innermost K and built a different key.
        auto const path = group_parent_path(fr.group_no_tag, immediate_parent);
        if (!path) {
            // A cyclic ancestor relation has no context key, so no record can
            // satisfy it. Report the violation — the disposition this check had
            // before #264 made the walk unbounded, restored deliberately.
            return fr.tag;
        }
        GroupCtxDelim const probe = make_group_ctx_delim(*path, fr.tag, /*delimiter=*/0);
        // PRECONDITION: `delims` is sorted by `group_ctx_delim_less`. Both call
        // sites (each loader's `finalize()`) sort the per-message records with
        // exactly that comparator immediately before flushing them into the
        // pool this span is a contiguous run of, and `group_ctx_delimiter_impl`
        // already relies on the same ordering to `lower_bound` this data at
        // runtime. Stated rather than assumed, because a scan tolerated an
        // unsorted span and this does not — an unsorted span would yield a
        // spurious "no record" and a false load-time rejection.
        auto const it = std::ranges::lower_bound(delims, probe, group_ctx_delim_less);
        if (it == delims.end() || group_ctx_delim_less(probe, *it)) {
            return fr.tag;  // no record for a context as_table_view() will register
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
    [[nodiscard]] std::uint16_t group_ctx_delimiter_impl(std::string_view msg_type,
                                                         std::span<std::uint16_t const> parent_path,
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

// Gate B r1 F1 (fixpp#216 P1-1) — TEST SEAM. The FR-023 / C-3.4 completeness
// throws in both loaders' finalize() (xml_loader.cpp, orchestra_loader.cpp)
// hold on every shipped dictionary (C-7.3's zero-population measurement), so
// no witness reaches them by loading an ordinary dialect. When enabled,
// `maybe_drop_first_group_ctx_delim_run_for_testing` truncates the FIRST
// message's per-context delimiter run to zero records, called immediately
// before the completeness check both loaders run (after Entity 2's own
// projection loop, which reads the pool, not the offsets, so nothing else
// observes the truncation). `find_incomplete_group_context` then GENUINELY
// misses at the `lower_bound` above (not a synthetic stand-in) and returns a
// REAL offending `(message, tag)` pair, so the throw fires for the actual
// reason it exists to catch. RAII-reset by the test.
//
#ifdef FIXPP_TEST_HOOKS
// Gate B r2: declaration gated to the repo's house pattern (src/capi/capi_internal.hpp:45-59,
// src/session/file_store.cpp:123-125). The DEFINITION stays unconditional in dictionary.cpp —
// that is what lets a test TU defining this macro link against the library, which is compiled
// WITHOUT it. Gating the declaration is what makes a production caller unable to NAME it.
void set_force_incomplete_group_context_for_testing(bool enable) noexcept;
#endif  // FIXPP_TEST_HOOKS

// Consulted unconditionally by BOTH loaders' finalize() and therefore NOT gated — it is inert
// (one relaxed atomic load) unless the gated setter above has been called. Same shape as
// src/session/engine.cpp:919's unconditional consult of a hook production can never set.
void maybe_drop_first_group_ctx_delim_run_for_testing(dict_metadata_handle& h) noexcept;

// ── 083 /simplify (C-1.5): the last two loader-symmetric blocks, shared ──────
//
// C-1.5's rule is that a one-loader fix is a half-restructure, which is why
// `make_group_ctx_delim`, `group_ctx_delim_less`, `capture_first_emission` and
// `find_context_without_delim_record` already live here. Two more blocks were
// left inline in BOTH `xml_loader.cpp` and `orchestra_loader.cpp` — one of them
// byte-identical, the other differing only in which exception type is thrown —
// with the Orchestra copy's own comment reading "same sort, same key-dedup and
// same run shape as xml_loader.cpp". Documenting a duplication is not the same
// as not having one; both are lifted here so the two walks cannot drift.

// Flush ONE message's captured delimiter records into the handle's pool:
// sort → reject clamp collisions → dedup by key → append → record the run.
//
// Deduping by key is deliberate — a group declared in both the header and the
// body yields two captures that agree by construction, and the first is kept.
//
// #264 review: that dedup is SAFE ONLY WHILE THE KEY IS LOSSLESS. Up to
// `kMaxGroupContextDepth` it is: a record's clamped `parent_path` IS its full
// chain, so equal keys imply equal chains and the two captures really are the
// same context. Past K the clamp drops the innermost ancestors, so two DISTINCT
// contexts — say a group reached under two different level-K+1 parents — produce
// byte-identical keys, and this dedup would silently DISCARD the second. The
// store cannot represent them separately (`group_ctx_key::parent_path` is a
// fixed K-element array), so the surviving record would answer for both and a
// message nested under the dropped parent would resolve the WRONG delimiter.
//
// Fixing that in the store is out of reach here; refusing the input is not. A
// collision is reported as a violation, which is strictly better than answering
// wrongly and is the same disposition FR-023 already takes. Returns the
// offending `no_tag`, or `nullopt` when the message flushed cleanly — it returns
// rather than throws so each loader keeps its OWN exception type (FR-006c),
// exactly as `find_incomplete_group_context` does.
[[nodiscard]] inline std::optional<std::uint16_t> flush_group_ctx_delims(dict_metadata_handle& h,
                                                                         DelimCapture& cap) {
    auto const same_key = [](CapturedDelim const& a, CapturedDelim const& b) noexcept {
        return !group_ctx_delim_less(a.rec, b.rec) && !group_ctx_delim_less(b.rec, a.rec);
    };
    std::ranges::sort(cap.out, [](CapturedDelim const& a, CapturedDelim const& b) noexcept {
        return group_ctx_delim_less(a.rec, b.rec);
    });
    // Equal keys are adjacent after the sort. Within a run of them, if any two
    // chains differ then some ADJACENT pair differs — equality is transitive —
    // so the pairwise scan is complete, not a sampling.
    for (std::size_t i = 1; i < cap.out.size(); ++i) {
        if (same_key(cap.out[i - 1], cap.out[i]) &&
            cap.out[i - 1].full_path != cap.out[i].full_path) {
            return cap.out[i].rec.no_tag;
        }
    }
    auto const last = std::ranges::unique(cap.out, same_key).begin();
    cap.out.erase(last, cap.out.end());
    MsgFieldsRun const run{.start = static_cast<std::uint32_t>(h.group_ctx_delim_pool_.size()),
                           .count = static_cast<std::uint32_t>(cap.out.size())};
    for (auto const& captured : cap.out) {
        h.group_ctx_delim_pool_.push_back(captured.rec);
    }
    h.per_msg_group_ctx_delim_offsets_.push_back(run);
    return std::nullopt;
}

// FR-023 / C-3.4 completeness sweep over every message. Returns the FIRST
// violation as `(message index, offending NumInGroup tag)`, or `nullopt` when
// every context `as_table_view()` will register has a delimiter record.
//
// Returns rather than throws so each loader keeps its OWN exception type
// (FR-006c: `xml_parse_error` vs `orchestra_parse_error`, discriminated by
// catch type) — that difference is the one thing that legitimately varies
// between the two, and it stays at the call site.
//
// `structural_group_tags`: sorted, unique group tags, supplied by the caller
// (each loader's own build-time `groups_` table — see the doc comment on
// `find_context_without_delim_record` above).
[[nodiscard]] inline std::optional<std::pair<std::size_t, std::uint16_t>>
find_incomplete_group_context(dict_metadata_handle const& h,
                              std::span<std::uint16_t const> structural_group_tags) {
    for (std::size_t i = 0; i < h.per_msg_field_offsets_.size(); ++i) {
        auto const frun = h.per_msg_field_offsets_[i];
        auto const drun = (i < h.per_msg_group_ctx_delim_offsets_.size())
                              ? h.per_msg_group_ctx_delim_offsets_[i]
                              : MsgFieldsRun{};
        auto const offender = find_context_without_delim_record(
            std::span<FieldRef const>{h.fields_.data() + frun.start, frun.count},
            std::span<GroupCtxDelim const>{h.group_ctx_delim_pool_.data() + drun.start,
                                           drun.count},
            structural_group_tags);
        if (offender != 0) {
            return std::pair{i, offender};
        }
    }
    return std::nullopt;
}

}  // namespace fixpp::dict::detail
