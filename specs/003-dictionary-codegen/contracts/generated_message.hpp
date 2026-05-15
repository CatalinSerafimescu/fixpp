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
                                                  // field_traits.hpp). RC#1 RESOLVED at re-/plan
                                                  // 2026-05-15: contracts/field_traits.hpp + the
                                                  // NET-NEW include/fixpp/dict/field_traits.hpp
                                                  // are 003-owned (Project-Structure row added).
#include <fixpp/dict/version_profile.hpp>         // application_version (002-shipped enum) +
                                                  // version_profile/resolve_application_version
                                                  // (003-OWNED additive edit; RC#1 RESOLVED).
#include <fixpp/wire/message_view_contract.hpp>   // vendored frozen stub (R6); RC#3 RESOLVED via
                                                  // the arch §2.4 v0.2→v0.3 dual-compile bridge carve-out.
#include <memory_resource>                        // std::pmr::memory_resource (decimal route, v1.4)
#include <type_traits>                            // std::is_same_v (AC-G7a owning_message_traits pin)
#include <fixpp/dict/reify.hpp>                    // dict::owning_message_traits primary template
                                                  // ([2c §4.8] L1459) — specialised below

namespace fixpp::v50sp2 {  // one namespace per codegen version; vt11 = 7 admin

// Forward-declared so the owning_message_traits<NewOrderSingle> specialisation
// below is well-formed in the shape oracle. The owning_<Msg> class DEFINITION
// is emitted into the per-version Reify.hpp (Entity 4, [2c §4.8]); this
// contract pins only the per-message owning_message_traits specialisation.
class owning_NewOrderSingle;

class NewOrderSingle {                                            // AC-G1
public:
    static constexpr std::string_view  msg_type_v = "D";          // AC-G2
    static constexpr application_version version_v =
        application_version::v50sp2;                               // AC-G2

    // AC-G7a / seam #18 — the owning_message_t<Msg> resolvent is the canonical
    // 2c v1.4 §4.8 (L1456–1464) external-trait form: codegen emits one
    // `owning_message_traits<Msg>` specialisation per typed message (at
    // namespace scope, alongside `owning_<Msg>` in the per-version Reify.hpp;
    // pinned below). This is INHERITED 2c v1.4 text — not a 003-derived /
    // 2c-underspecified resolvent and not a `Msg::owning_type` member alias.
    // See the namespace-scope owning_message_traits<NewOrderSingle>
    // specialisation + AC-G7a static_assert after this class.

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

    // RC#2 RESOLVED — re-derived from corrected 2c v1.4 §4.1.3/§4.7
    // (.specify/2c-codegen.md:263,313,1051,1084,1153; [const §XX] amendment,
    // commit 41dd8c1). v1.3's `decimal_t::from_chars(fv->bytes())` was a
    // phantom (no-mr member named from_chars on decimal_t — absent on merged
    // 001/2a). The corrected decimal route:
    //   * is NOT field_traits-routed (decimal_t excluded — [2c §4.1.3:269-277]);
    //   * calls the real PMR-mandatory entry point
    //     fixpp::decimal_t::parse(span, mr) ([2a §4.3]; a shell over
    //     decimal_traits<FIXPP_DECIMAL_T>::from_chars(span, mr), [2a §4.2]);
    //   * takes an EXPLICIT std::pmr::memory_resource* mr parameter (the
    //     flyweight still holds NO arena — AC-G7 sizeof == one pointer is
    //     PRESERVED; mr is caller-threaded into the accessor signature, not
    //     a member). zero-alloc for the default pod_decimal trait; an
    //     allocating substituted FIXPP_DECIMAL_T (e.g. cpp_dec_float) may draw
    //     from mr per call — AC-G4a. Latency ceiling is the separate ≤75 ns
    //     row (§6.2), not the ≤20 ns string/int/char row. See AC-G4 / AC-G4a /
    //     NFR-003-4 (decimal arm) / data-model Entity 1.
    [[nodiscard]] inline expected_t<fixpp::decimal_t>
    price(std::pmr::memory_resource* mr) const noexcept   // v1.4 PMR-mandatory
    { auto fv = view_.template get<44>();
      if (!fv) return std::unexpected{fv.error()};
      return fixpp::decimal_t::parse(fv->bytes(), mr); }   // [2a §4.3]; v1.4

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

// AC-G7a / seam #18 — codegen emits one owning_message_traits<Msg>
// specialisation per typed message (2c v1.4 §4.8 L1460–1462, verbatim form),
// at namespace scope alongside owning_<Msg> in the per-version Reify.hpp; the
// specialisation of the dict-scope primary template appears outside
// fixpp::v50sp2 (a specialisation must be declared in a namespace enclosing
// fixpp::dict). The shape oracle pins the NewOrderSingle specialisation; the
// static_assert below catches a codegen template that omits the specialisation
// (the alias becomes ill-formed — a COMPILE-TIME shape-oracle failure, like
// AC-G7's sizeof static_assert, not a runtime golden assertion) or wires it to
// the wrong owner. dict::owning_message_t<NewOrderSingle> ≡
// dict::owning_message_traits<NewOrderSingle>::type ≡
// fixpp::v50sp2::owning_NewOrderSingle (see contracts/reify.hpp:
// `template<class Msg> using owning_message_t =
// typename owning_message_traits<Msg>::type;`, 2c v1.4 §4.8 L1463–1464).
template <>
struct fixpp::dict::owning_message_traits<fixpp::v50sp2::NewOrderSingle> {
    using type = fixpp::v50sp2::owning_NewOrderSingle;            // 2c v1.4 §4.8 L1461
};
static_assert(std::is_same_v<
    fixpp::dict::owning_message_t<fixpp::v50sp2::NewOrderSingle>,
    fixpp::v50sp2::owning_NewOrderSingle>);

// Filtered at emit, NOT partially emitted (spec §A3):
//  - FIX-Latest A-035..A-065  → build warning, not emitted, no v1.0 flag (AC-G9)
//  - A-014..A-034             → not emitted as typed classes in v1.0 (AC-G10)
// Both remain reachable only via 002 runtime view.get(uint16_t).
