#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/validator.hpp
// [2b §4.6]/[2b §6.5] Validator interface + the dictionary_driven_validator
// default. Shape oracle: specs/004-wire-codec/contracts/validator.hpp.
// Authority: .specify/2b-wire.md v0.2.
//
// EXACTLY 5 pure-virtual ([const §XIV.2] cap satisfied directly). The default
// dictionary_driven_validator holds a fixpp::dict::table_view BY VALUE — there
// is NO virtual wire/->dict/ runtime edge (SC-007): the validator calls the
// value type's *non-virtual* members.
//
// table_view-seam note (041-validation-gate-wiring, 2026-06-16): T009 promotes
// fixpp::dict::table_view from test-mock-only to a production header
// (include/fixpp/dict/table_view.hpp). The forward-declaration seam is CLOSED;
// this header now includes the complete production types directly. Test TUs
// that previously included the mock BEFORE this header for the single-
// definition-rule are updated: the mock is no longer the sole definition.
// dictionary_driven_validator remains HEADER-ONLY; all 5 overrides are inline
// below and are instantiated wherever a complete table_view exists (now in
// both production TUs via Dictionary::as_table_view() and test TUs).
//
// §XV.9 guard: table_view.hpp and field_type.hpp have deliberately minimal
// include graphs (no mutex, no heavy asio) — see their file headers.

#include <cstddef>
#include <cstdint>
#include <fixpp/core/decimal_alias.hpp>    // fixpp::decimal_t
#include <fixpp/core/decimal_helpers.hpp>  // core::detail::trap_throw (C1)
#include <fixpp/core/error.hpp>
#include <memory_resource>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "parser.hpp"    // MessageView<Mode>, access_mode
#include "tag_scan.hpp"  // accumulate_bounded (W-P3-1 declared_count bound)

// T009 (041-validation-gate-wiring): complete-type includes replacing the
// forward-declaration seam. §XV.9 guard confirmed: table_view.hpp and
// field_type.hpp include only cstdint/span/string_view/unordered_map/vector
// + field_ref.hpp — no mutex, no shared_mutex, no asio heavy headers.
#include <fixpp/dict/field_type.hpp>
#include <fixpp/dict/table_view.hpp>

namespace fixpp::wire {

// [2b §4.6] runtime-virtual validation plugin. EXACTLY 5 pure-virtual.
class Validator {
public:
    Validator() noexcept = default;
    Validator(Validator const&) = default;
    Validator(Validator&&) noexcept = default;
    Validator& operator=(Validator const&) = default;
    Validator& operator=(Validator&&) noexcept = default;
    virtual ~Validator() noexcept = default;

    // Unconditional validate over every dictionary-known field present in
    // `msg` (NOT per-accessor). Working set is drawn from `scratch_mr`.
    [[nodiscard]] virtual core::expected_t<void> validate(
        MessageView<access_mode::Index> const& msg,
        std::pmr::memory_resource* scratch_mr) const noexcept = 0;

    [[nodiscard]] virtual core::expected_t<void> validate_field(
        std::uint16_t tag, std::span<const std::byte> value) const noexcept = 0;

    [[nodiscard]] virtual std::span<std::uint16_t const> required_fields(
        std::string_view msg_type) const noexcept = 0;

    [[nodiscard]] virtual bool field_valid_for(std::string_view msg_type,
                                               std::uint16_t tag) const noexcept = 0;

    [[nodiscard]] virtual std::uint16_t group_first_field(std::uint16_t no_tag) const noexcept = 0;
};

// [const §XIV.2] cap = 5. The five pure-virtual above are the only pure
// virtuals; asserted structurally by tests/wire/validator_domain_test.cpp.
static_assert(std::is_abstract_v<Validator>, "[const §XIV.2] cap = 5");

// [2b §6.5] full per-version default. Holds dict::table_view BY VALUE; the
// per-version behaviour is data-driven by the held table_view (v42/v44/
// v50sp2/vt11). final ⇒ no further virtual extension.
//
// HEADER-ONLY (see table_view-seam note at top): the ctor + all 5 overrides
// are defined inline so the class is materialised only where a complete
// table_view exists (test TUs). Bodies land in T046 (Phase C).
class dictionary_driven_validator final : public Validator {
public:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    explicit dictionary_driven_validator(fixpp::dict::table_view dict) noexcept
        : dict_{std::move(dict)} {}

    // [2b §6.5] Unconditional validation over every field present in `msg`.
    // Algorithm (per spec):
    //   1. Iterate each field via begin/end; for each:
    //      a. Unexpected tag  → wire_unexpected_tag (42)
    //      b. Enum violation  → wire_field_value_out_of_range (40)
    //      c. Type structural → wire_field_value_out_of_range (40) /
    //                           wire_field_value_truncated (41) [Float path]
    //   2. Required-fields scan: framing tags 8/9/10 treated as implicitly
    //      satisfied; remaining required tags probed via msg.get().
    //      Missing → wire_required_field_missing (38).
    // Zero-heap working set: O(1) stack only — required presence is probed via
    // the OffsetTable (msg.get(), O(log n)), so no seen[] bitmap is allocated;
    // scratch_mr is threaded only to the Float/decimal parse path. Well within
    // the ≤ ~600 B [2b §6.5] working-set bound (no new/delete on any path).
    [[nodiscard]] core::expected_t<void> validate(
        MessageView<access_mode::Index> const& msg,
        std::pmr::memory_resource* scratch_mr) const noexcept override {
        std::string_view const msg_type = msg.msg_type();

        // ── Step 0: header-order check ([2b §6.5.1], W-002) ─────────────
        // FIX standard-header order: 8(BeginString), 9(BodyLength), 35(MsgType)
        // must appear as the first three fields in document order. Tags 8 and 9
        // are already verified positionally by the Framer; here we check that
        // the first non-framing entry in the offset table is tag 35.
        {
            constexpr std::uint16_t kBeginStringH = 8;
            constexpr std::uint16_t kBodyLengthH = 9;
            constexpr std::uint16_t kCheckSumH = 10;
            constexpr std::uint16_t kMsgType = 35;
            auto const ents = msg.offsets().entries();
            // Find the first entry that is not a framing tag.
            for (auto const& e : ents) {
                if (e.tag == kBeginStringH || e.tag == kBodyLengthH || e.tag == kCheckSumH) {
                    continue;  // skip framing — they are order-guaranteed by Framer
                }
                // First non-framing field must be MsgType (35).
                if (e.tag != kMsgType) {
                    return core::expected_t<void>{std::unexpect,
                                                  core::error::wire_header_out_of_order};
                }
                break;
            }
        }

        // ── Step 1: iterate every present field ──────────────────────────
        for (auto it = msg.begin(); !(it == msg.end()); ++it) {
            auto const& fld = *it;

            // (a) Unexpected tag check
            if (!dict_.field_valid_for(msg_type, fld.tag)) {
                return core::expected_t<void>{std::unexpect, core::error::wire_unexpected_tag};
            }

            // (b) Enum validity check
            if (!dict_.enum_valid(fld.tag, fld.value)) {
                return core::expected_t<void>{std::unexpect,
                                              core::error::wire_field_value_out_of_range};
            }

            // (c) Type structural check ([2b §6.5 rule 3])
            auto const check = check_field_type(fld.tag, fld.value, scratch_mr);
            if (!check) {
                return check;
            }
        }

        // ── Step 2: required-fields scan ─────────────────────────────────
        // Tags 8/9/10 are guaranteed present by the framer; skip them.
        constexpr std::uint16_t kBeginString = 8;
        constexpr std::uint16_t kBodyLength = 9;
        constexpr std::uint16_t kCheckSum = 10;

        auto const req = dict_.required_fields(msg_type);
        for (auto const req_tag : req) {
            if (req_tag == kBeginString || req_tag == kBodyLength || req_tag == kCheckSum) {
                continue;  // framing-guaranteed — always present
            }
            if (!msg.get(req_tag).has_value()) {
                return core::expected_t<void>{std::unexpect,
                                              core::error::wire_required_field_missing};
            }
        }

        // ── Step 3: repeating-group structure check (/clarify Q2) ────────
        // For each group declared in the dictionary (identified by the count
        // field no_tag and its first-delimiter delim_tag), verify:
        //   (a) the declared count matches the actual first-delimiter occurrences
        //   (b) the first field after the count field is the delimiter (not a
        //       different field injected before the first instance)
        // Walk the offset table entries in document order.
        //
        // 072 FR-010 (L-063-3): nesting-aware, query-before-push recursive group
        // validation. The pre-072 walk was FLAT — it iterated every entry and
        // queried each group at the ROOT context (`{msg_type, path=[]}`), so a
        // genuinely NESTED reused group missed the context store and degraded to
        // the bare-`no_tag` (first-seen) resolution, and its hand-rolled
        // `seen_in_instance` boundary heuristic had no notion of nesting depth
        // (could not span a multi-entry nested group). `validate_group_level`
        // (the level scanner) + `consume_group` (the single-occurrence consumer,
        // Gate B r2 split) below resolve each candidate group under its REAL
        // parent path first, then push the group's own no_tag only to recurse
        // into a nested child — so typed read and strict validation now agree
        // on depth-≥2 membership.
        auto const ents = msg.offsets().entries();
        group_context const root_ctx{.msg_type = msg_type};  // depth 0, empty parent path
        auto const grp = validate_group_level(ents, root_ctx, msg.bytes().data(), 0, ents.size());
        if (!grp) {
            return core::expected_t<void>{std::unexpect, grp.error()};
        }

        return {};
    }

    // 072 FR-010 (L-063-3), Gate B r2 fix: consumes EXACTLY ONE group occurrence
    // — the group whose count field sits at `ents[pos]` — validating its
    // declared instances and recursing into nested children via `consume_group`
    // itself (one nested-group call per nested occurrence, never a level scan),
    // and returns the entry index one-PAST this group's last instance. `ctx` is
    // the group's PARENT context (query-before-push: resolved under `ctx`
    // first; `ctx.pushed(no_tag)` is formed only to recurse into a nested
    // child, mirroring `consume_group_extent` / the typed accessor's
    // `group_ctx.pushed(no_tag)`). Pre-fix, this "consume ONE nested group"
    // job was done by `validate_group_level` itself, which was ALSO a level
    // scanner (`while (i < end)` over the whole remaining range), so a
    // recursive "consume this nested group" call kept scanning past its own
    // group's extent into sibling fields / later parent instances (Gate B r2
    // P2: a multi-instance parent containing a nested group, e.g. 296=2 each
    // with its own nested 295, had the first instance's 295-recursion swallow
    // BOTH instances and return i=end, under-counting 296's actual_count against
    // declared_count=2 -> false-reject). Recursion depth is bounded by the
    // group_context K=16 clamp (nested descent stops past depth 16, the same
    // level beyond which membership context cannot be represented), so stack
    // use is O(min(depth,16)) with no heap.
    [[nodiscard]] core::expected_t<std::size_t> consume_group(
        std::span<OffsetTable::entry const> ents, group_context const& ctx,
        std::byte const* frame_base, std::size_t pos, std::size_t end) const noexcept {
        std::span<std::uint16_t const> const parent_path{ctx.parent_path.data(), ctx.depth};
        std::uint16_t const no_tag = ents[pos].tag;
        std::uint16_t const delim_tag = dict_.group_first_field(ctx.msg_type, parent_path, no_tag);
        // Saturate at UINT32_MAX (W-P3-1) so an over-declared count cannot
        // wrap down to the real instance count and spuriously pass.
        std::span<std::byte const> const count_bytes{frame_base + ents[pos].offset,
                                                     ents[pos].length};
        std::uint32_t const declared_count = fixpp::wire::parse_bounded_u32(count_bytes);
        std::size_t i = pos + 1;  // past the count field
        if (declared_count == 0) {
            return i;  // zero-count group: nothing to verify, nothing consumed
        }
        // First instance must open with the delimiter.
        if (i >= end || ents[i].tag != delim_tag) {
            return core::expected_t<std::size_t>{std::unexpect,
                                                 core::error::wire_required_field_missing};
        }
        auto const member_tags = dict_.group_member_tags(ctx.msg_type, parent_path, no_tag);
        auto const is_member = [&](std::uint16_t tag) noexcept {
            for (auto const m : member_tags) {
                if (m == tag) {
                    return true;
                }
            }
            return false;
        };
        // Child context = this level + this group's own no_tag, used ONLY to
        // recurse into nested children. `pushed` clamps at K=16 (child.depth
        // == ctx.depth means the cap was hit -> stop descending).
        group_context const child = ctx.pushed(no_tag);
        bool const can_descend = child.depth > ctx.depth;
        std::span<std::uint16_t const> const child_path{child.parent_path.data(), child.depth};

        std::uint32_t actual_count = 0;
        while (i < end && ents[i].tag == delim_tag) {
            ++i;  // consume this instance's delimiter
            while (i < end && ents[i].tag != delim_tag) {
                std::uint16_t const t = ents[i].tag;
                if (!is_member(t)) {
                    break;  // end of this group's extent
                }
                // Query-before-push: is `t` itself a nested group under the
                // pushed child path? (depth-bounded by K=16.)
                if (can_descend && dict_.group_first_field(ctx.msg_type, child_path, t) != 0) {
                    // Consume EXACTLY this ONE nested-group occurrence (never a
                    // level scan) so the return index lands one-past ITS
                    // extent, not the whole remaining range.
                    auto const nested = consume_group(ents, child, frame_base, i, end);
                    if (!nested) {
                        return nested;  // propagate the nested failure slot
                    }
                    i = *nested;  // advance past the consumed nested group
                } else {
                    ++i;  // ordinary scalar member (or depth cap reached)
                }
            }
            ++actual_count;
        }
        if (actual_count != declared_count) {
            return core::expected_t<std::size_t>{std::unexpect,
                                                 core::error::wire_required_field_missing};
        }
        return i;  // one-past this group's last instance
    }

    // 072 FR-010 (L-063-3), Gate B r2 fix: LEVEL SCANNER. Validates every group
    // appearing in entries [begin,end) at the nesting level given by
    // `parent_path` (which EXCLUDES each candidate group's own no_tag), by
    // delegating each occurrence found to `consume_group` (which returns the
    // index one-past that ONE occurrence). This is the entry point from
    // `validate()` for the whole message (ctx = root, [0, ents.size())); it is
    // never called recursively to "consume a nested group" — that is
    // `consume_group`'s job — so there is no double-duty and no over-run.
    [[nodiscard]] core::expected_t<std::size_t> validate_group_level(
        std::span<OffsetTable::entry const> ents, group_context const& ctx,
        std::byte const* frame_base, std::size_t begin, std::size_t end) const noexcept {
        std::span<std::uint16_t const> const parent_path{ctx.parent_path.data(), ctx.depth};
        std::size_t i = begin;
        while (i < end) {
            if (dict_.group_first_field(ctx.msg_type, parent_path, ents[i].tag) == 0) {
                ++i;
                continue;  // not a group count field at this nesting level
            }
            auto const consumed = consume_group(ents, ctx, frame_base, i, end);
            if (!consumed) {
                return consumed;
            }
            i = *consumed;
        }
        return i;
    }

    // Single-field check: enum + type, no msg_type context.
    [[nodiscard]] core::expected_t<void> validate_field(
        std::uint16_t tag, std::span<const std::byte> value) const noexcept override {
        if (!dict_.enum_valid(tag, value)) {
            return core::expected_t<void>{std::unexpect,
                                          core::error::wire_field_value_out_of_range};
        }
        return check_field_type(tag, value, nullptr);
    }

    [[nodiscard]] std::span<std::uint16_t const> required_fields(
        std::string_view msg_type) const noexcept override {
        return dict_.required_fields(msg_type);
    }

    [[nodiscard]] bool field_valid_for(std::string_view msg_type,
                                       std::uint16_t tag) const noexcept override {
        return dict_.field_valid_for(msg_type, tag);
    }

    [[nodiscard]] std::uint16_t group_first_field(std::uint16_t no_tag) const noexcept override {
        return dict_.group_first_field(no_tag);
    }

private:
    fixpp::dict::table_view dict_;  // held BY VALUE (SC-007: no virtual edge)

    // Structural type check for a single field value ([2b §6.5 rule 3]).
    // mr is only used for the Float/decimal parse path.
    // Returns success or a wire_* error; never throws.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    [[nodiscard]] core::expected_t<void> check_field_type(
        std::uint16_t tag, std::span<const std::byte> value,
        std::pmr::memory_resource* mr) const noexcept {
        using ft = fixpp::dict::field_type;
        switch (dict_.field_type_of(tag)) {
            case ft::Float: {
                // (C1) trap_throw fences the potentially-throwing 2a decode
                // boundary (FR-013, [arch §5.3]). Result is
                // expected<expected<decimal_t, error>, error>; flatten both.
                auto wrapped = core::detail::trap_throw(
                    [value, mr]() { return fixpp::decimal_t::parse(value, mr); });
                if (!wrapped) {
                    // trap_throw outer branch: decimal_t::parse is noexcept, so a
                    // throw → terminate and this branch is dead in practice. If a
                    // future decimal trait ever throws, the raw error escapes here
                    // (bypassing the T009a remap below). That is a pre-existing
                    // constraint: the in-contract (non-throwing) path is the only
                    // live path and guarantees only wire_* slots reach the caller.
                    return core::expected_t<void>{std::unexpect, wrapped.error()};
                }
                if (!(*wrapped)) {
                    auto const inner_err = (*wrapped).error();
                    // Re-map 2a/001's decimal_precision_loss → wire surface slot.
                    if (inner_err == core::error::decimal_precision_loss) {
                        return core::expected_t<void>{std::unexpect,
                                                      core::error::wire_field_value_truncated};
                    }
                    // T009a (041-validation-gate-wiring FR-004 / data-model E-4):
                    // In-contract path (non-throwing decimal_t::parse): any other
                    // decimal parse error (decimal_invalid_input=10, decimal_overflow=11, …)
                    // is NOT a wire_* slot and must not escape validate(). Remap to
                    // wire_field_value_out_of_range (slot 40) → SessionRejectReason=5.
                    // This ensures every error on the in-contract path is a wire_* slot.
                    return core::expected_t<void>{std::unexpect,
                                                  core::error::wire_field_value_out_of_range};
                }
                return {};
            }
            case ft::Int: {
                // An Int field must be non-empty; optional leading '-'; then
                // only ASCII digits [0-9].
                if (value.empty()) {
                    return core::expected_t<void>{std::unexpect,
                                                  core::error::wire_field_value_out_of_range};
                }
                std::size_t start = 0;
                if (static_cast<unsigned char>(value[0]) == '-') {
                    start = 1;
                }
                for (std::size_t idx = start; idx < value.size(); ++idx) {
                    auto const ch = static_cast<unsigned char>(value[idx]);
                    if (ch < '0' || ch > '9') {
                        return core::expected_t<void>{std::unexpect,
                                                      core::error::wire_field_value_out_of_range};
                    }
                }
                return {};
            }
            case ft::Char: {
                // A Char field must be exactly one byte.
                if (value.size() != 1) {
                    return core::expected_t<void>{std::unexpect,
                                                  core::error::wire_field_value_out_of_range};
                }
                return {};
            }
            case ft::String:
            case ft::Boolean:
            case ft::Data:
            case ft::Length:
            default:
                // No structural constraint beyond non-degenerate framing.
                return {};
        }
    }
};

}  // namespace fixpp::wire
