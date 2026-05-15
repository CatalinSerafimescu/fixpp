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
                                                  // — 003-OWNED, NOT 002-shipped (002 ships no
                                                  // field_traits.hpp). Gate A r1 Opus N-P2-2 /
                                                  // RC#1; header/contract/ACs added at re-/plan.
                                                  // spec §8 "Upstream dependency audit".
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

    // ⚠ INHERITED DESIGN-DOC DEFECT — un-fixable by a 003 bundle edit.
    // The body below (`fixpp::decimal_t::from_chars(fv->bytes())`) is copied
    // VERBATIM from signed-off 2c v1.3 (.specify/2c-codegen.md:1040 / :270-271).
    // That symbol DOES NOT EXIST on the merged 001/2a surface: the only parse
    // entry points are decimal_traits<T>::from_chars(span, std::pmr::
    // memory_resource*) (001 decimal_traits.hpp:98-100) and decimal<T>::parse(
    // span, mr) (:162-163) — PMR MANDATORY; 2a's own Gate A removed the no-PMR
    // form (:123-128). It is wrong on three axes (type carrier / name / missing
    // mr) and the flyweight holds no `mr` (sizeof == one pointer, AC-G7).
    // This is a defect in INHERITED 2c v1.3, not in this bundle — a bundle edit
    // cannot fix it. Resolution: reopen 2c §4.1.3/§4.7 to the real PMR-taking
    // entry point, then re-derive AC-G4/AC-G4a/NFR-003-4. The shape is shown
    // AS-INHERITED (not patched into a fake correct form) per the Gate A rule
    // for inherited-design defects. See spec AC-G4 + plan.md `## Gate A`.
    [[nodiscard]] inline expected_t<fixpp::decimal_t>
    price() const noexcept   // BLOCKED on 2c §4.1.3/§4.7 reopen (Opus RC#2)
    { auto fv = view_.template get<44>();
      if (!fv) return std::unexpected{fv.error()};
      return fixpp::decimal_t::from_chars(fv->bytes()); }  // ⚠ inherited; symbol absent on 001/2a

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
