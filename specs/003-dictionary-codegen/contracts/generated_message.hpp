// SPDX-License-Identifier: AGPL-3.0-or-later
// Phase 1 contract — the SHAPE fixpp-codegen emits, literal extract of
// [2c §4.7]. NOT hand-written: build/<preset>/_codegen/include/fixpp/<vXX>/
// Messages.hpp is generated at configure time by tools/codegen/fixpp-codegen
// (F1 Candidate A; research.md D-1). This file pins the invariants the
// codegen template MUST satisfy and the determinism golden is taken from.
#pragma once
#include <fixpp/core/decimal_alias.hpp>          // fixpp::decimal_t (2a)
#include <fixpp/core/error.hpp>                   // expected_t
#include <fixpp/dict/field_traits.hpp>            // dict::field_traits / decode_field (2c §4.1.3)
#include <fixpp/wire/message_view_contract.hpp>   // vendored frozen stub (R6)

namespace fixpp::v50sp2 {  // one namespace per codegen version; vt11 = 7 admin

class NewOrderSingle {                                            // AC-G1
public:
    static constexpr std::string_view  msg_type_v = "D";          // AC-G2
    static constexpr application_version version_v =
        application_version::v50sp2;                               // AC-G2

    explicit NewOrderSingle(                                       // AC-G3
        wire::MessageView<wire::access_mode::Index> const& view
            [[clang::lifetimebound]]) noexcept : view_(view) {}    // no validation

    // AC-G4 / AC-G11: inline noexcept (NOT constexpr); [[nodiscard]];
    // [[clang::lifetimebound]] on view-returning accessors.
    [[nodiscard]] inline expected_t<std::string_view>
    cl_ord_id() const noexcept [[clang::lifetimebound]]
    { return dict::decode_field<std::string_view>(view_.template get<11>()); }

    [[nodiscard]] inline expected_t<char>
    side() const noexcept
    { return dict::decode_field<char>(view_.template get<54>()); }

    [[nodiscard]] inline expected_t<fixpp::decimal_t>
    price() const noexcept                                         // decimal via [2a §4.2]
    { auto fv = view_.template get<44>();
      if (!fv) return std::unexpected{fv.error()};
      return fixpp::decimal_t::from_chars(fv->bytes()); }

    [[nodiscard]] inline wire::group_view<NewOrderSingle::Leg>     // AC-G5
    legs() const noexcept [[clang::lifetimebound]]
    { return view_.template group<555, NewOrderSingle::Leg>(); }

    [[nodiscard]] inline expected_t<wire::field_view>              // AC-G6 / §4.7.1
    field_value(std::uint16_t tag) const noexcept [[clang::lifetimebound]]
    { return view_.get(tag); }

    [[nodiscard]] inline wire::MessageView<wire::access_mode::Index> const&
    view() const noexcept [[clang::lifetimebound]] { return view_; }

    class Leg { /* per-tag accessors + own field_value(uint16_t) — AC-G5 */ };

private:
    wire::MessageView<wire::access_mode::Index> const& view_;      // exactly one ref
};

// AC-G7 / seam #18 — emitted per message; catches a template member-add bug.
static_assert(sizeof(NewOrderSingle)
              == sizeof(wire::MessageView<wire::access_mode::Index> const*));

}  // namespace fixpp::v50sp2

// Filtered at emit, NOT partially emitted (spec §A3):
//  - FIX-Latest A-035..A-065  → build warning, not emitted, no v1.0 flag (AC-G9)
//  - A-014..A-034             → not emitted as typed classes in v1.0 (AC-G10)
// Both remain reachable only via 002 runtime view.get(uint16_t).
