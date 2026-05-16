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
// table_view-seam note (US4 resume, 2026-05-16): fixpp::dict::table_view is
// the value-typed metadata contract owned by 2c (only FORWARD-declared here,
// same as parser.hpp). dictionary_driven_validator stores it by value, so its
// definition needs the COMPLETE type. Per the seam #1 single-definition rule
// (tests/support/mock_dict_table.hpp) only TESTS supply a complete table_view;
// production wire code never instantiates a validator. Consequently
// dictionary_driven_validator is HEADER-ONLY (all 5 overrides inline below)
// and is only instantiated in test TUs that include the mock BEFORE this
// header. src/wire/validator.cpp therefore carries no table_view-dependent
// code (the design doc's .cpp split assumed a real 2c table_view; with the
// 2c-deferred mock, header-only instantiation is the only correct C++).

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

namespace fixpp::dict {
class table_view;  // value type, owned by 2c; only forward-declared here.
}  // namespace fixpp::dict

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
                    return core::expected_t<void>{std::unexpect, inner_err};
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
