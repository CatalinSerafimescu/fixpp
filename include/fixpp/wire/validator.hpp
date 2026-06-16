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

#include "parser.hpp"  // MessageView<Mode>, access_mode

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
        auto const ents = msg.offsets().entries();
        for (std::size_t i = 0; i < ents.size(); ++i) {
            std::uint16_t const no_tag = ents[i].tag;
            std::uint16_t const delim_tag = dict_.group_first_field(no_tag);
            if (delim_tag == 0) {
                continue;  // not a group count field in this dict
            }
            // Parse the declared count.
            std::span<std::byte const> const count_bytes{
                // The offset table's raw pointer into the frame buffer:
                // bytes().data() is the frame start, entry.offset is value offset.
                msg.bytes().data() + ents[i].offset, ents[i].length};
            std::uint32_t declared_count = 0;
            for (auto b : count_bytes) {
                auto c = static_cast<unsigned char>(b);
                if (c < '0' || c > '9') {
                    break;
                }
                declared_count = declared_count * 10U + static_cast<std::uint32_t>(c - '0');
            }
            // Walk entries after the count to count actual delimiter occurrences
            // and verify the first entry after the count is the delimiter.
            if (declared_count == 0) {
                continue;  // zero-count group: nothing to verify
            }
            // Entry i+1 must be the delimiter (first field of first instance).
            if (i + 1 >= ents.size() || ents[i + 1].tag != delim_tag) {
                return core::expected_t<void>{std::unexpect,
                                              core::error::wire_required_field_missing};
            }

            auto const member_tags = dict_.group_member_tags(no_tag);
            auto const is_member = [&](std::uint16_t tag) noexcept {
                for (auto const member_tag : member_tags) {
                    if (member_tag == tag) {
                        return true;
                    }
                }
                return false;
            };

            std::size_t actual_count = 0;
            std::size_t group_body_end = i + 1U;
            std::size_t k = i + 1U;
            while (k < ents.size()) {
                if (ents[k].tag != delim_tag) {
                    break;
                }
                std::size_t const inst_start = k;
                ++k;  // consume delimiter
                while (k < ents.size()) {
                    if (ents[k].tag == delim_tag) {
                        break;  // next instance
                    }
                    if (!is_member(ents[k].tag)) {
                        break;
                    }
                    bool seen_in_instance = false;
                    for (std::size_t m = inst_start + 1U; m < k; ++m) {
                        if (ents[m].tag == ents[k].tag) {
                            seen_in_instance = true;
                            break;
                        }
                    }
                    if (seen_in_instance) {
                        break;
                    }
                    ++k;
                }
                group_body_end = k;
                ++actual_count;
            }

            if (actual_count != declared_count) {
                return core::expected_t<void>{std::unexpect,
                                              core::error::wire_required_field_missing};
            }
        }

        return {};
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
                    // Any other decimal parse error (decimal_invalid_input=10,
                    // decimal_overflow=11, …) is NOT a wire_* slot and must not
                    // escape validate(). Remap to wire_field_value_out_of_range
                    // (slot 40) → SessionRejectReason=5 (type non-conformant).
                    // This ensures every error emitted by validate() is a wire_*
                    // slot, never a raw decimal error.
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
