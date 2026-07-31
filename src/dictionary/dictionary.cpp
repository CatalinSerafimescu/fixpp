// SPDX-License-Identifier: AGPL-3.0-or-later
// src/dictionary/dictionary.cpp
//
// `fixpp::dict::Dictionary` accessor bodies + `detail::dict_metadata_handle`
// lookup implementations. Every public accessor is `const` and `noexcept` per
// `[2c §4.3]` / AC-D8. The heap-pinned metadata-handle is allocated by
// `XmlLoader::load*` via `std::allocate_shared` over
// `std::pmr::polymorphic_allocator` so the control-block deallocator routes
// memory back to the originating `mr` (`[2c §4.3]` / C-R2-P1-1).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fixpp/dict/dictionary.hpp>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dictionary_internal.hpp"
#include "fixpp/dict/component_ref.hpp"
#include "fixpp/dict/field_ref.hpp"
#include "fixpp/dict/group_ref.hpp"
#include "fixpp/dict/version_profile.hpp"
#include "fixt_framing_table.hpp"  // 081 Concern A: kFixtFramingTable (D-2)

namespace fixpp::dict {

// `detail::bytewise_compare` (the locale-independent name sort key shared with
// xml_loader.cpp) lives in dictionary_internal.hpp per research.md D-6.

// ============================================================================
// detail::dict_metadata_handle — sorted-array lookup helpers
// ============================================================================

namespace detail {

namespace {

// Shared message-row binary search for the two per-MsgType run lookups below.
// Returns SIZE_MAX when `msg_type` is absent (keeps the hot path allocation-
// and exception-free; the callers map the sentinel to an empty run).
[[nodiscard]] std::size_t find_msg_index(std::pmr::vector<MessageEntry> const& messages,
                                         std::string_view msg_type) noexcept {
    auto const it = std::ranges::lower_bound(
        messages, msg_type, {}, [](MessageEntry const& m) noexcept { return m.msg_type; });
    if (it == messages.end() || it->msg_type != msg_type) {
        return SIZE_MAX;
    }
    return static_cast<std::size_t>(it - messages.begin());
}

}  // namespace

MsgFieldsRun dict_metadata_handle::find_msg_fields(std::string_view msg_type) const noexcept {
    auto const idx = find_msg_index(messages_, msg_type);
    return idx == SIZE_MAX ? MsgFieldsRun{} : per_msg_field_offsets_[idx];
}

MsgFieldsRun dict_metadata_handle::find_msg_required(std::string_view msg_type) const noexcept {
    auto const idx = find_msg_index(messages_, msg_type);
    return idx == SIZE_MAX ? MsgFieldsRun{} : per_msg_required_offsets_[idx];
}

FieldRef dict_metadata_handle::field_ref_impl(std::string_view msg_type,
                                              std::uint16_t tag) const noexcept {
    auto const run = find_msg_fields(msg_type);
    if (run.count == 0) {
        return FieldRef{};
    }
    const auto* const begin = fields_.data() + run.start;
    const auto* const end = begin + run.count;
    const auto* const it = std::lower_bound(
        begin, end, tag, [](FieldRef const& a, std::uint16_t t) noexcept { return a.tag < t; });
    if (it == end || it->tag != tag) {
        return FieldRef{};
    }
    return *it;
}

std::span<std::uint16_t const> dict_metadata_handle::required_fields_impl(
    std::string_view msg_type) const noexcept {
    auto const run = find_msg_required(msg_type);
    if (run.count == 0) {
        return {};
    }
    return std::span<std::uint16_t const>{required_fields_pool_.data() + run.start, run.count};
}

std::uint16_t dict_metadata_handle::group_first_field_impl(std::uint16_t no_tag) const noexcept {
    auto const it = std::ranges::lower_bound(groups_, no_tag, {},
                                             [](GroupRef const& g) noexcept { return g.no_tag; });
    if (it == groups_.end() || it->no_tag != no_tag) {
        return 0;
    }
    return it->first_field_tag;
}

std::uint16_t dict_metadata_handle::length_pair_data_tag_impl(
    std::uint16_t length_tag) const noexcept {
    // Walk the global fields table (one row per (MsgType, tag); we only need
    // the first hit because length_pair_data_tag is a per-tag property not a
    // per-(MsgType,tag) property in v1.0).
    for (auto const& f : fields_) {
        // cppcheck-suppress useStlAlgorithm  // raw loop chosen for early-exit + readability
        if (f.tag == length_tag && f.length_pair_data_tag != 0) {
            return f.length_pair_data_tag;
        }
    }
    return 0;
}

std::optional<std::uint16_t> dict_metadata_handle::field_by_name_impl(
    std::string_view name) const noexcept {
    // NOLINTNEXTLINE(modernize-use-ranges)
    auto const it = std::lower_bound(field_by_name_.begin(), field_by_name_.end(), name,
                                     [this](FieldNameEntry const& e, std::string_view n) noexcept {
                                         return bytewise_compare(name_at(e.name), n) < 0;
                                     });
    if (it == field_by_name_.end() || name_at(it->name) != name) {
        return std::nullopt;
    }
    return it->tag;
}

std::optional<ComponentRef> dict_metadata_handle::component_impl(
    std::string_view name) const noexcept {
    // NOLINTNEXTLINE(modernize-use-ranges) — asymmetric comparator.
    auto const it = std::lower_bound(components_by_name_.begin(), components_by_name_.end(), name,
                                     [this](NamedIndex const& e, std::string_view n) noexcept {
                                         return bytewise_compare(name_at(e.name), n) < 0;
                                     });
    if (it == components_by_name_.end() || name_at(it->name) != name) {
        return std::nullopt;
    }
    return components_[it->index];
}

std::optional<GroupRef> dict_metadata_handle::group_impl(std::uint16_t no_tag) const noexcept {
    auto const it = std::ranges::lower_bound(groups_, no_tag, {},
                                             [](GroupRef const& g) noexcept { return g.no_tag; });
    if (it == groups_.end() || it->no_tag != no_tag) {
        return std::nullopt;
    }
    return *it;
}

std::span<FieldRef const> dict_metadata_handle::component_fields_impl(
    std::uint16_t component_id) const noexcept {
    if (component_id >= components_.size()) {
        return {};
    }
    auto const& cr = components_[component_id];
    if (cr.field_count == 0 || cr.first_field_index + cr.field_count > component_fields_.size()) {
        return {};
    }
    return std::span<FieldRef const>{component_fields_.data() + cr.first_field_index,
                                     cr.field_count};
}

std::span<FieldRef const> dict_metadata_handle::group_fields_impl(
    std::uint16_t no_tag) const noexcept {
    auto const it = std::ranges::lower_bound(groups_, no_tag, {},
                                             [](GroupRef const& g) noexcept { return g.no_tag; });
    if (it == groups_.end() || it->no_tag != no_tag) {
        return {};
    }
    auto const& gr = *it;
    if (gr.field_count == 0 || gr.first_field_index + gr.field_count > group_fields_.size()) {
        return {};
    }
    return std::span<FieldRef const>{group_fields_.data() + gr.first_field_index, gr.field_count};
}

// Gate B r1 F1 (fixpp#201): `group_required_offsets_[i]` is parallel to
// `groups_[i]` (same finalize() loop populates both in lockstep) — locate the
// index via the SAME `no_tag` lower_bound as `group_fields_impl` above, then
// index the parallel run table.
std::span<std::uint16_t const> dict_metadata_handle::group_required_members_impl(
    std::uint16_t no_tag) const noexcept {
    auto const it = std::ranges::lower_bound(groups_, no_tag, {},
                                             [](GroupRef const& g) noexcept { return g.no_tag; });
    if (it == groups_.end() || it->no_tag != no_tag) {
        return {};
    }
    auto const idx = static_cast<std::size_t>(it - groups_.begin());
    if (idx >= group_required_offsets_.size()) {
        return {};
    }
    auto const run = group_required_offsets_[idx];
    if (run.count == 0 || run.start + run.count > group_required_pool_.size()) {
        return {};
    }
    return std::span<std::uint16_t const>{group_required_pool_.data() + run.start, run.count};
}

// Gate B r1 F1 (fixpp#201): reuses `find_msg_index`'s message-row lookup (SAME
// index as `per_msg_field_offsets_`) but indexes the parallel
// `per_msg_group_required_offsets_`/`msg_group_required_pool_` pair store.
std::span<std::pair<std::uint16_t, std::uint16_t> const>
dict_metadata_handle::msg_group_required_pairs_impl(std::string_view msg_type) const noexcept {
    auto const idx = find_msg_index(messages_, msg_type);
    if (idx == SIZE_MAX || idx >= per_msg_group_required_offsets_.size()) {
        return {};
    }
    auto const run = per_msg_group_required_offsets_[idx];
    if (run.count == 0 || run.start + run.count > msg_group_required_pool_.size()) {
        return {};
    }
    return std::span<std::pair<std::uint16_t, std::uint16_t> const>{
        msg_group_required_pool_.data() + run.start, run.count};
}

// 083 T025 (Entity 2). Same run-lookup shape as msg_group_required_pairs_impl
// above — the message scoping IS the run.
std::span<GroupCtxDelim const> dict_metadata_handle::msg_group_ctx_delims_impl(
    std::string_view msg_type) const noexcept {
    auto const idx = find_msg_index(messages_, msg_type);
    if (idx == SIZE_MAX || idx >= per_msg_group_ctx_delim_offsets_.size()) {
        return {};
    }
    auto const run = per_msg_group_ctx_delim_offsets_[idx];
    if (run.count == 0 || run.start + run.count > group_ctx_delim_pool_.size()) {
        return {};
    }
    return std::span<GroupCtxDelim const>{group_ctx_delim_pool_.data() + run.start, run.count};
}

std::uint16_t dict_metadata_handle::group_ctx_delimiter_impl(
    std::string_view msg_type, std::span<std::uint16_t const> parent_path,
    std::uint16_t no_tag) const noexcept {
    auto const run = msg_group_ctx_delims_impl(msg_type);
    // Build the probe through the SAME helper the store side uses, so the
    // depth clamp is one rule rather than two that can drift (C-2.2).
    GroupCtxDelim const probe = make_group_ctx_delim(parent_path, no_tag, /*delimiter=*/0);
    auto const* const begin = run.data();
    auto const* const end = begin + run.size();
    auto const* it = std::lower_bound(begin, end, probe, group_ctx_delim_less);
    // `group_ctx_delim_less` orders by (no_tag, depth, path) and the probe
    // carries all three, so an exact hit lands here or nowhere. Confirm the
    // key rather than trusting the position: lower_bound returns the insertion
    // point on a miss.
    if (it == end || group_ctx_delim_less(probe, *it)) {
        return 0;  // no record — FR-006 fail-closed is the caller's decision
    }
    return it->delimiter;
}

// 074-orchestra-native-reader (FR-002): enum {value, description} pairs for a
// codeset-backed field, keyed by tag. Binary-search the per-tag runs (sorted by
// tag) → a span into the flat enum_values_ store. Empty for tags with no
// codeset and for dictionaries that populate no enum store (all XmlLoader ones).
std::span<EnumValueRef const> dict_metadata_handle::enum_values_impl(
    std::uint16_t tag) const noexcept {
    auto const it = std::ranges::lower_bound(enum_runs_, tag, {},
                                             [](EnumRun const& r) noexcept { return r.tag; });
    if (it == enum_runs_.end() || it->tag != tag) {
        return {};
    }
    auto const& run = *it;
    if (run.count == 0 || run.start + run.count > enum_values_.size()) {
        return {};
    }
    return std::span<EnumValueRef const>{enum_values_.data() + run.start, run.count};
}

// 003-dictionary-codegen (RC#5 — F1 IR data path). Build-time codegen-
// enumeration; not on any runtime hot path.
std::span<FieldRef const> dict_metadata_handle::message_fields_impl(
    std::string_view msg_type) const noexcept {
    MsgFieldsRun const run = find_msg_fields(msg_type);
    if (run.count == 0 || run.start + run.count > fields_.size()) {
        return {};
    }
    return std::span<FieldRef const>{fields_.data() + run.start, run.count};
}

std::string_view dict_metadata_handle::field_name_impl(std::uint16_t tag) const noexcept {
    // field_by_name_ is sorted by name (not tag); a linear scan is fine —
    // this runs once per tag at codegen time, never on a runtime path.
    for (auto const& e : field_by_name_) {
        if (e.tag == tag) {
            return name_at(e.name);
        }
    }
    return {};
}

}  // namespace detail

// ============================================================================
// Dictionary — public accessor bodies (every method const + noexcept)
// ============================================================================

Dictionary::Dictionary(detail::dict_metadata_handle_ptr handle) noexcept
    : handle_(std::move(handle)) {}

Dictionary::~Dictionary() = default;

session_version Dictionary::which_session_version() const noexcept {
    return handle_ ? handle_->version_impl() : session_version::Unknown;
}

FieldRef Dictionary::field_ref(std::string_view msg_type, std::uint16_t tag) const noexcept {
    return handle_ ? handle_->field_ref_impl(msg_type, tag) : FieldRef{};
}

std::span<std::uint16_t const> Dictionary::required_fields(
    std::string_view msg_type) const noexcept {
    return handle_ ? handle_->required_fields_impl(msg_type) : std::span<std::uint16_t const>{};
}

bool Dictionary::field_valid_for(std::string_view msg_type, std::uint16_t tag) const noexcept {
    return field_ref(msg_type, tag).rule != field_presence::NotDeclared;
}

std::uint16_t Dictionary::group_first_field(std::uint16_t no_tag) const noexcept {
    return handle_ ? handle_->group_first_field_impl(no_tag) : 0;
}

std::uint16_t Dictionary::length_pair_data_tag(std::uint16_t length_tag) const noexcept {
    return handle_ ? handle_->length_pair_data_tag_impl(length_tag) : 0;
}

std::optional<FieldRef> Dictionary::field(std::string_view msg_type,
                                          std::uint16_t tag) const noexcept {
    auto const fr = field_ref(msg_type, tag);
    if (fr.rule == field_presence::NotDeclared) {
        return std::nullopt;
    }
    return fr;
}

std::optional<std::uint16_t> Dictionary::field_by_name(std::string_view name) const noexcept {
    return handle_ ? handle_->field_by_name_impl(name) : std::nullopt;
}

std::optional<ComponentRef> Dictionary::component(std::string_view name) const noexcept {
    return handle_ ? handle_->component_impl(name) : std::nullopt;
}

std::optional<GroupRef> Dictionary::group(std::uint16_t no_tag) const noexcept {
    return handle_ ? handle_->group_impl(no_tag) : std::nullopt;
}

std::span<MessageEntry const> Dictionary::messages() const noexcept {
    return handle_ ? handle_->messages_impl() : std::span<MessageEntry const>{};
}

std::span<FieldRef const> Dictionary::component_fields(std::string_view name) const noexcept {
    if (!handle_) {
        return {};
    }
    auto const cr = handle_->component_impl(name);
    if (!cr) {
        return {};
    }
    return handle_->component_fields_impl(cr->component_id);
}

std::span<FieldRef const> Dictionary::group_fields(std::uint16_t no_tag) const noexcept {
    return handle_ ? handle_->group_fields_impl(no_tag) : std::span<FieldRef const>{};
}

// 003-dictionary-codegen (RC#5 — F1 IR data path; additive, source-compatible
// read accessors — [arch §9.3] stable-from-v1.0, [const §X.4]-style).
std::span<FieldRef const> Dictionary::message_fields(std::string_view msg_type) const noexcept {
    return handle_ ? handle_->message_fields_impl(msg_type) : std::span<FieldRef const>{};
}

std::string_view Dictionary::field_name(std::uint16_t tag) const noexcept {
    return handle_ ? handle_->field_name_impl(tag) : std::string_view{};
}

// 074-orchestra-native-reader (FR-002): additive read-only codeset enum surface.
std::span<EnumValueRef const> Dictionary::enum_values(std::uint16_t tag) const noexcept {
    return handle_ ? handle_->enum_values_impl(tag) : std::span<EnumValueRef const>{};
}

// 041-validation-gate-wiring T008: build a `dict::table_view` from this
// Dictionary.
//
// Algorithm:
//  1. For every message, populate valid-tag set + required-tag list.
//  2. For every group (via messages), populate group_first + group_members.
//  3. Build global tag→field_type map by walking ALL message_fields() runs
//     across all messages (a tag's field_data_type is invariant across
//     msg_types in FIX — R-1a confirms union-across-messages is correct).
//
// [const §XV.1]: called once at session/validator setup; must not be called
// on the per-message hot path.
namespace detail {
// 083 T049 (W-11a) test seam — see dictionary_internal.hpp. A plain
// non-atomic counter: the witness opens one session on one thread, and this
// must not add synchronisation to a hot construction path.
namespace {
std::uint64_t g_as_table_view_calls = 0;
}  // namespace
void bump_as_table_view_call_count() noexcept { ++g_as_table_view_calls; }
std::uint64_t as_table_view_call_count() noexcept { return g_as_table_view_calls; }
void reset_as_table_view_call_count() noexcept { g_as_table_view_calls = 0; }
}  // namespace detail

table_view Dictionary::as_table_view() const {
    detail::bump_as_table_view_call_count();  // 083 T049 test seam (W-11a)
    table_view tv;

    if (!handle_) {
        return tv;  // null handle (moved-from Dictionary) → empty table_view
    }

    auto const msgs = messages();

    for (auto const& msg_entry : msgs) {
        auto const mt = msg_entry.msg_type;

        // ── required-fields list ─────────────────────────────────────────
        auto const req_span = required_fields(mt);
        for (auto const tag : req_span) {
            tv.add_required_tag(mt, tag);
        }

        // ── valid-tag set (all fields for this msg_type) ─────────────────
        auto const all_fields = message_fields(mt);
        for (auto const& fr : all_fields) {
            if (fr.rule != field_presence::NotDeclared) {
                tv.add_valid_tag(mt, fr.tag);
            }
        }

        // ── group structure (legacy bare-no_tag store — PRE-063 UNCHANGED) ──
        // Dual-store amendment #1 (orchestrator-approved, commit 0caafd23):
        // as_table_view() populates BOTH this legacy bare store AND the
        // context-scoped store below. The bare store is REQUIRED — the frozen
        // `Validator::group_first_field(no_tag)` pure-virtual ([const §XIV.2]
        // 5-virtual cap) is production-wired here and must answer from a
        // context-free store; see the DUAL-STORE INVARIANT on table_view.hpp's
        // context accessors. This loop is byte-identical to pre-063 `main`; it
        // feeds only the bare fallback that context-aware call sites use on a
        // context-miss (which degrades to exactly `main`'s pre-063 resolution,
        // not worse — L-063-3), never overriding the CONTEXT store that the
        // parser lambda / validator query first.
        for (auto const& fr : all_fields) {
            if (fr.type != field_data_type::NumInGroup) {
                continue;
            }
            std::uint16_t const legacy_no_tag = fr.tag;
            std::uint16_t const legacy_first = group_first_field(legacy_no_tag);
            if (legacy_first == 0) {
                continue;
            }
            tv.set_group_first(legacy_no_tag, legacy_first);
            for (auto const& gfr : group_fields(legacy_no_tag)) {
                tv.add_group_member(legacy_no_tag, gfr.tag);
            }
            // Gate B r1 F1 (fixpp#201): the DIRECT required-member set is now
            // sourced from the GROUP-RELATIVE store (own_req gated by a
            // component-AND accumulator reset at this group's own boundary —
            // NOT the blind `gfr.rule==Required && gfr.group_no_tag==
            // legacy_no_tag` filter, which over-included a member required
            // only inside an OPTIONAL component nested within the group; see
            // `dict_metadata_handle::group_required_members_impl`).
            for (auto const req_tag : handle_->group_required_members_impl(legacy_no_tag)) {
                tv.add_group_required_member(legacy_no_tag, req_tag);
            }
        }

        // ── group structure (063 Defect-A: context-scoped, per-message) ───
        // Bare no_tag-keyed `group_first_field(no_tag)`/`group_fields(no_tag)`
        // (the Dictionary handle's OWN global, first-seen-wins accessors) are
        // deliberately NOT used here: a NumInGroup tag reused with different
        // membership across contexts (e.g. FIX44 295 — MassQuote's
        // QuotEntryGrp vs QuotCxlEntriesGrp) would silently resolve to
        // whichever variant the loader saw FIRST across the whole dictionary
        // (Defect A). Instead, derive membership from THIS message's own
        // per-message expansion (`all_fields = message_fields(mt)`, already
        // fetched above): `FieldRef.group_no_tag` (field_ref.hpp:88) persists
        // each field's IMMEDIATE enclosing group's no_tag within this
        // specific message's expansion — a pre-dedup context that survives
        // finalize() (data-model.md "GroupMembership", Option A).
        //
        // 1) immediate_parent[G] = the enclosing group of G's OWN count-tag
        //    entry (0 = top level) — this is exactly `fr.group_no_tag` on the
        //    FieldRef whose `.tag == G` and `.type == NumInGroup`.
        std::unordered_map<std::uint16_t, std::uint16_t> immediate_parent;
        for (auto const& fr : all_fields) {
            if (fr.type == field_data_type::NumInGroup) {
                immediate_parent[fr.tag] = fr.group_no_tag;
            }
        }
        for (auto const& fr : all_fields) {
            if (fr.type != field_data_type::NumInGroup) {
                continue;
            }
            std::uint16_t const no_tag = fr.tag;

            // 2) Members of `no_tag` IN THIS MESSAGE = every FieldRef whose
            //    immediate enclosing group is `no_tag`. This per-message set is
            //    context-exact (the Defect-A fix) — NOT the global, deduped
            //    `group_fields(no_tag)`. `all_fields` (message_fields) is
            //    tag-SORTED (xml_loader.cpp), so `members` is a SET, not
            //    declaration order; the DELIMITER is taken separately in (4).
            std::vector<std::uint16_t> members;
            for (auto const& m : all_fields) {
                if (m.group_no_tag == no_tag) {
                    members.push_back(m.tag);
                }
            }
            if (members.empty()) {
                continue;  // not a real group in this message (plain scalar reuse of the tag)
            }

            // Gate B r1 F1 (fixpp#201): DIRECT required members, sourced from
            // the context-exact GROUP-RELATIVE pairs store (own_req gated by
            // a component-AND accumulator reset at this group's own boundary
            // — NOT the blind `m.rule==Required` filter, which over-included
            // a member required only inside an OPTIONAL component nested
            // within the group; see `dict_metadata_handle::
            // msg_group_required_pairs_impl`). Filtered to THIS no_tag from
            // the whole-message pairs list (small counts — RC5 pin: max
            // observed direct required-member count is 6).
            std::vector<std::uint16_t> required_members;
            for (auto const& [pair_no_tag, pair_tag] : handle_->msg_group_required_pairs_impl(mt)) {
                if (pair_no_tag == no_tag) {
                    required_members.push_back(pair_tag);
                }
            }

            // 3) Full parent-no_tag path (outermost first, EXCLUDING no_tag
            //    itself): walk the immediate_parent chain up from no_tag's
            //    OWN enclosing group to the root (0). For FIX44 295 in
            //    MassQuote: fr.group_no_tag == 296 (295's count-field's own
            //    enclosing group), immediate_parent[296] == 0 (296 is
            //    top-level) → path == [296].
            std::vector<std::uint16_t> path;
            std::uint16_t cur = fr.group_no_tag;
            while (cur != 0) {
                path.push_back(cur);
                auto const pit = immediate_parent.find(cur);
                cur = (pit != immediate_parent.end()) ? pit->second : std::uint16_t{0};
            }
            std::reverse(path.begin(), path.end());

            // 4) Delimiter = the group's DECLARATION first field, NOT
            //    members.front(): `all_fields` is tag-sorted, so members.front()
            //    is the lowest-tag member, not the delimiter (e.g. FIX44
            //    NoPartyIDs(453): lowest member PartyIDSource(447), real
            //    delimiter PartyID(448)). Using members.front() made the
            //    validator reject valid real-dict groups. [This paragraph is
            //    unchanged and still TRUE.]
            //
            //    083 T031 (C-3.1): the SOURCE is now Entity 2 — this context's
            //    OWN declaration — not the global `group_first_field(no_tag)`.
            //
            //    083 T032 (FR-011 / C-3.2) — CORRECTION of the sentence that
            //    used to close this comment. It read: "a reused tag with
            //    divergent per-context delimiters is an L-063-3 residual; the
            //    per-context MEMBER SET above stays exact regardless." The
            //    second clause is FALSE, and believing it is why the defect
            //    survived inspection. The member set does NOT stay exact: it is
            //    `set_group_first_ctx`'s own unconditional
            //    `add_group_member_ctx(msg_type, parent_path, no_tag, first)`
            //    (include/fixpp/dict/table_view.hpp:645) that INJECTS the wrong
            //    global delimiter into this context's member set — measured on
            //    48 contexts. A wrong delimiter therefore corrupts the member
            //    set too, which is fixpp#210 Consequence 1/2. With the source
            //    corrected the injected tag is already a declared member, so
            //    that injection becomes a no-op (D-5 / C-3.3) and the pollution
            //    disappears by construction rather than by a second fix.
            //
            //    There is deliberately NO fallback here: falling back to the
            //    global would reinstate the defect, and to members.front() a
            //    worse one already fixed above. A 0 means the context has no
            //    Entity-2 record, which is FR-006's fail-closed condition and is
            //    rejected at LOAD time in the loaders' finalize() (FR-023 /
            //    C-3.4), never silently absorbed here — `as_table_view()` is
            //    contractually non-throwing (072, L-063-4) and stays so.
            std::uint16_t const delim = handle_->group_ctx_delimiter_impl(mt, path, no_tag);

            // Register into the CONTEXT-KEYED store (in ADDITION to the
            // legacy bare store populated above — see the escalation note
            // there). Every context-aware consumer (parser lambda,
            // validator.hpp) queries THIS store first, so Defect A stays
            // fixed on those call sites regardless of the legacy loop above.
            tv.set_group_first_ctx(mt, path, no_tag, delim);
            for (auto const member_tag : members) {
                tv.add_group_member_ctx(mt, path, no_tag, member_tag);
            }
            for (auto const req_tag : required_members) {  // fixpp#201
                tv.add_group_required_member_ctx(mt, path, no_tag, req_tag);
            }
        }
    }

    // ── global tag→field_type map (+ T016 multi-value bit) ─────────────────
    // Walk every FieldRef across all message field runs; the first
    // non-NotDeclared occurrence of a tag wins (type is invariant in FIX).
    // T016: a store-only tag (populated below, never seen by this loop)
    // defaults to `multi_value = false` — `enum_domain`'s default member
    // init (table_view.hpp) — since `set_multi_value` is only ever called
    // from inside this loop, on tags this dictionary's message expansion
    // actually reaches.
    for (auto const& msg_entry : msgs) {
        auto const all_fields = message_fields(msg_entry.msg_type);
        for (auto const& fr : all_fields) {
            if (fr.rule == field_presence::NotDeclared) {
                continue;
            }
            // Only record if not already mapped (first-seen wins; invariant).
            tv.set_field_type(fr.tag, field_type_from_data_type(fr.type));
            if (fr.type == field_data_type::MultiCharValue ||
                fr.type == field_data_type::MultiStringValue) {
                tv.set_multi_value(fr.tag, true);
            }
        }
    }

    // ── enum-domain table (T015 — STORE-DRIVEN, not message_fields()-driven)
    // Iterate the dictionary's OWN enum store (`enum_runs_` / `enum_values_`
    // on the metadata handle — populated by XmlLoader/OrchestraLoader) so
    // EVERY enum-backed tag gets a domain, including tags unreachable from
    // message expansion (e.g. MsgType(35), EncryptMethod(98),
    // MsgDirection(385), ApplVerID(1128), SessionStatus(1409) — 35 such tags
    // across FIX50/SP1/SP2, the FIXT split). A message_fields()-only
    // projection would leave those 35 with NO domain, and
    // `table_view::validate_field()`'s first statement is `enum_valid()`
    // with NO `field_valid_for` precheck (validator.hpp:323-330) — silently
    // ACCEPTING out-of-domain values through a frozen public API, an FR-003
    // (normative MUST) violation. This is the C3-1 fix.
    //
    // Built once here, never per message ([const §XV.1], [const §VIII.5]).
    // `add_enum` copies the code bytes; see `enum_domain` (table_view.hpp) for
    // why the table must own them rather than alias `handle_->name_pool_`.
    for (auto const& run : handle_->enum_runs_) {
        for (auto const& ev : enum_values(run.tag)) {
            tv.add_enum(run.tag, ev.value);
        }
    }

    // ── 081 Concern A (D-1/D-2, T009): validator-private FIXT.1.1 framing
    // surface — v50/v50sp1/v50sp2 ONLY (empty `<header/>`). Baked constant,
    // census-pinned to dictionaries/FIXT11.xml (fixt_framing_table.hpp).
    // Does NOT touch valid_/required_/types_ above (D-1 load-bearing
    // invariant: field_valid_for/valid_tags_for/field_type_of stay
    // byte-identical whether strict validation is on or off).
    switch (which_session_version()) {
        case session_version::v50:
        case session_version::v50sp1:
        case session_version::v50sp2:
            for (auto const& entry : detail::kFixtFramingTable) {
                tv.add_fixt_framing_tag(entry.tag, entry.type);
            }
            break;
        default:
            break;  // all other versions: framing surface stays empty
    }

    return tv;
}

}  // namespace fixpp::dict
