// SPDX-License-Identifier: AGPL-3.0-or-later
// Phase 1 contract — literal extract of [2c §4.8]. The shipped header is
// include/fixpp/dict/reify.hpp. Header-mostly; out-of-line .cpp split locked
// at /tasks (research.md D-5). No method is added to wire::MessageView (AC-R1).
#pragma once
#include <fixpp/core/error.hpp>                  // fixpp::core::expected_t, error
#include <fixpp/dict/version_profile.hpp>        // version_profile + dict::resolve_application_version
                                                 // — 003-OWNED, NOT 002-shipped: 002 deferred both.
                                                 // RC#1 RESOLVED at re-/plan 2026-05-15 —
                                                 // contracts/version_profile.hpp pins the 003-owned
                                                 // additive edit (struct + free fn + ApplVerID
                                                 // wire→C++ map); new ACs AC-VP1..AC-VP6.
#include <fixpp/wire/message_view_contract.hpp>  // vendored frozen stub (R6 / D-2). RC#3 RESOLVED at
                                                 // re-/plan 2026-05-15: the arch §2.4 carve-out is
                                                 // broadened (v0.2→v0.3, [const §XX] arch amendment)
                                                 // to cover the dict↔wire BRIDGE surface — the
                                                 // generated fixpp::vXX::* tree PLUS the hand-written
                                                 // reify.hpp + field_traits.hpp + the vendored
                                                 // wire-contract stub — as a dual-compile bridge
                                                 // (NOT a cyclic dictionary→wire module edge, which
                                                 // arch §2.2/§2.3 forbid). check_layers.py is taught
                                                 // an explicit bridge file-list. See spec NFR-003-8 /
                                                 // research.md D-12 / plan.md "Re-/plan (RC#3)".
#include <cstdint>
#include <memory_resource>
#include <string_view>

namespace fixpp::dict {

// Per [2c §4.8]. resolved_message_version carries (kind, session, application)
// per RC#1; FIXT admin → {session_admin, vt11, Unknown}; application → the
// resolved value (v42/v44/v50sp2). Its ONE authoritative definition (the full
// struct + the sizeof==4 / alignof==1 / trivially-copyable static_asserts that
// AC-VP1 pins) lives in contracts/version_profile.hpp, included above
// (#include <fixpp/dict/version_profile.hpp> at line 7). No forward-declare /
// "pinned at /tasks" deferral here — that would be a false re-opening of a type
// AC-VP1 already closes (Opus N-P2-4 / RC#3). This contract consumes that one
// shape; owning_message_handle::version() returns it by value.

// owning_message_t<Msg> — the return-type alias of reify_as<Msg> / the
// owning_message_handle::as<Msg>() return type (Opus N-P2-1: used in reify_as /
// as<> / AC-R1 / data-model Entity 6; the name-mangling/ADL surface every
// downstream consumer binds to is pinned in the contract here, not deferred).
//
// CANONICAL 2c v1.4 §4.8 FORM — INHERITED 2c TEXT (replan loop round 2 —
// Opus round-2 / Codex round-2 P1 / Root cause #1, Option A). 2c-codegen.md
// v1.4 §4.8 (L1456–1464) DEFINES `owning_message_t<Msg>` as
//   `typename owning_message_traits<Msg>::type`
// via a per-message EXTERNAL TRAIT specialisation: codegen emits one
// `owning_<Msg>` class plus one `owning_message_traits<Msg>` specialisation
// per typed message (alongside it, in the per-version Reify.hpp). 003 inherits
// this verbatim — it is NOT a 003-derived/2c-underspecified resolvent and NOT
// a `Msg::owning_type` member alias; the round-1 rewrite's attribution was
// false against the signed-off design doc and is corrected here. 2c's prose
// name `owning_<Msg>` (used in Reify.hpp / as<>) is the per-message concrete
// owner the trait resolves to (`owning_NewOrderSingle`, …); under the trait,
// `owning_<Msg>` and `owning_message_t<Msg>` denote the SAME type.
//
// The primary template is declared so the alias is well-formed in this
// contract; the per-message specialisation (e.g. owning_message_traits<
// NewOrderSingle>) is emitted by codegen into the per-version Reify.hpp and is
// part of the codegen SHAPE ORACLE — contracts/generated_message.hpp pins the
// NewOrderSingle specialisation and the codegen-shape golden seam #18
// (tests/codegen/flyweight_shape_test.cpp, AC-G7a) asserts it is present and
// bound to the correct owner; omission is caught as a COMPILE-TIME shape-oracle
// failure (the alias becomes ill-formed), not a runtime GoogleTest assertion.
template <class Msg> struct owning_message_traits;          // 2c v1.4 §4.8 L1459
template <class Msg>
using owning_message_t = typename owning_message_traits<Msg>::type;  // 2c v1.4
                                                     // §4.8 L1463–1464; AC-R1 /
                                                     // AC-G7a (inherited 2c text)

// Type-erased owning message (runtime-dispatch return). Move-only. SBO variant
// may elide the heap allocation below a published size threshold.
class owning_message_handle {
public:
    owning_message_handle(owning_message_handle const&)            = delete;
    owning_message_handle& operator=(owning_message_handle const&) = delete;
    owning_message_handle(owning_message_handle&&) noexcept;
    owning_message_handle& operator=(owning_message_handle&&) noexcept;
    ~owning_message_handle();

    [[nodiscard]] resolved_message_version version() const noexcept;          // AC-R6
    [[nodiscard]] std::string_view msg_type() const noexcept [[clang::lifetimebound]];
    [[nodiscard]] wire::MessageView<wire::access_mode::Index> const&
        view() const noexcept [[clang::lifetimebound]];
    [[nodiscard]] expected_t<wire::field_view>
        field_value(std::uint16_t tag) const noexcept [[clang::lifetimebound]];

    // nullptr on resolved-version / MsgType mismatch (no UB, no throw) — AC-R6.
    // Return type is the canonical 2c v1.4 §4.8 owning_message_t<Msg> alias
    // (`typename owning_message_traits<Msg>::type`; equivalently 2c's prose
    // owner name `owning_<Msg>` — the trait resolves them to the same type;
    // Root cause #1, Option A).
    template <class Msg>
    [[nodiscard]] auto as() const noexcept [[clang::lifetimebound]]
        -> owning_message_t<Msg> const*;

private:
    struct impl; /* small-variant OR heap polymorphic owner */
};

// Typed entry point — caller names Msg at compile time; no dispatch overhead.
// Failures: dict_reify_oom (PMR fail trapped via [2a §4.2] trap_throw),
// dict_reify_msg_type_mismatch (view MsgType != Msg::msg_type_v). AC-R1/R3/R8.
// dict_reify_version_mismatch is NOT a failure mode here (dropped per RC#1).
template <class Msg>
[[nodiscard]] expected_t<owning_message_t<Msg>>
reify_as(wire::MessageView<wire::access_mode::Index> const& view,
         std::pmr::memory_resource* mr) noexcept;

// Runtime-dispatch entry point. Resolution (AC-D2/D3/D6/D7):
//  1. peek MsgType(35).
//  2. FIXT-admin hit → {session_admin, profile.session, Unknown} via
//     _dispatch/reify_dispatch_fixt.hpp → vt11::owning_<Msg>.
//  3. miss → read ApplVerID(1128); the wire-owned field-absent error from
//     get<1128>() (NOT a 003 slot — cross-feature note in
//     contracts/version_profile.hpp) maps to empty sv →
//     dict::resolve_application_version(profile, value) [003-OWNED free fn,
//     RC#1 RESOLVED at re-/plan 2026-05-15 — contracts/version_profile.hpp;
//     AC-VP1..AC-VP6] → {application, profile.session, resolved} via
//     _dispatch/reify_dispatch_application.hpp.
// Failures: dict_reify_oom, dict_reify_msg_type_mismatch,
//   dict_unknown_appl_ver_id, dict_unresolved_application_version (NOT a
//   sentinel fall-through — RC#1), dict_reify_unknown_msg_type (resolved
//   version+MsgType has no codegen owner, e.g. runtime-XML-only version).
[[nodiscard]] expected_t<owning_message_handle>
reify(wire::MessageView<wire::access_mode::Index> const& view,
      version_profile profile,
      std::pmr::memory_resource* mr) noexcept;

}  // namespace fixpp::dict
