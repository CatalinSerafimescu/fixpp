// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/dict/reify.hpp
//
// 003-OWNED, NEW bridge header. Literal materialisation of [2c §4.8]. The
// dict::reify_as / dict::reify free-function-template bridge +
// owning_message_handle + the canonical 2c v1.4 §4.8 owning_message_traits<Msg>
// primary template and owning_message_t<Msg> alias. No method is added to
// wire::MessageView (AC-R1). Header-mostly; out-of-line .cpp split locked at
// /tasks (research.md D-5). Oracle:
// specs/003-dictionary-codegen/contracts/reify.hpp; data-model Entities 5/6;
// spec §4.3/§4.4 AC-R1..AC-R8 / AC-D1..AC-D7 / AC-G7a.
//
// Bridge header (arch §2.4 v0.3 dual-compile carve-out, RC#3): #includes the
// vendored frozen wire stub + the 003-owned version_profile additive surface;
// NOT a cyclic dictionary→wire module link edge (check_layers.py
// BRIDGE_SOURCE_FILES / BRIDGE_EXEMPT_INCLUDES). The literal dictionary→wire
// whitelist edge was rejected (it would form the forbidden wire↔dictionary
// cycle); see spec NFR-003-8 / plan "Re-/plan (RC#3)".
#pragma once
#include <fixpp/core/error.hpp>            // core::expected_t, core::error
#include <fixpp/dict/version_profile.hpp>  // version_profile +
                                           // resolved_message_version +
                                           // dict::resolve_application_version
                                           // (003-OWNED, RC#1; the ONE
                                           // authoritative shape AC-VP1
                                           // pins — consumed here, NOT
                                           // re-declared/deferred).
#include <cstdint>
#include <fixpp/wire/message_view_contract.hpp>  // vendored frozen R6 stub
#include <memory_resource>
#include <string_view>

namespace fixpp::dict {

// owning_message_t<Msg> — the return-type alias of reify_as<Msg> / the
// owning_message_handle::as<Msg>() return type (Opus N-P2-1). CANONICAL
// 2c v1.4 §4.8 FORM — INHERITED 2c TEXT (Root cause #1, Option A): 2c v1.4
// §4.8 (L1456-1464) DEFINES owning_message_t<Msg> as
//   `typename owning_message_traits<Msg>::type`
// via a per-message EXTERNAL TRAIT specialisation: codegen emits one
// owning_<Msg> class plus one owning_message_traits<Msg> specialisation per
// typed message (alongside it, in the per-version Reify.hpp). It is NOT a
// `Msg::owning_type` member alias. The primary template is declared here so
// the alias is well-formed; the per-message specialisation is emitted by
// codegen into <vXX>/Reify.hpp and pinned in the shape oracle
// (contracts/generated_message.hpp + seam #18 flyweight_shape_test.cpp,
// AC-G7a) — a template that omits it is caught as a COMPILE-TIME shape-oracle
// failure (the alias becomes ill-formed), not a runtime assertion.
template <class Msg>
struct owning_message_traits;  // 2c v1.4 §4.8 L1459
template <class Msg>
// verbatim 2c v1.4 §4.8 inherited text — contract-pinned, see contracts/reify.hpp / AC-G7a
// NOLINTNEXTLINE(readability-redundant-typename)
using owning_message_t = typename owning_message_traits<Msg>::type;  // 2c v1.4
                                                                     // §4.8 L1463-1464; AC-R1 /
                                                                     // AC-G7a (inherited 2c text)

// Type-erased owning message (runtime-dispatch return). Move-only. SBO variant
// may elide the heap allocation below a published size threshold (Entity 5).
class owning_message_handle {
public:
    owning_message_handle(owning_message_handle const&) = delete;
    owning_message_handle& operator=(owning_message_handle const&) = delete;
    // verbatim 2c v1.4 §4.8 inherited text — contract-pinned, see contracts/reify.hpp / AC-G7a
    // NOLINTNEXTLINE(readability-named-parameter,hicpp-named-parameter)
    owning_message_handle(owning_message_handle&&) noexcept;
    // verbatim 2c v1.4 §4.8 inherited text — contract-pinned, see contracts/reify.hpp / AC-G7a
    // NOLINTNEXTLINE(readability-named-parameter,hicpp-named-parameter)
    owning_message_handle& operator=(owning_message_handle&&) noexcept;
    ~owning_message_handle();

    [[nodiscard]] resolved_message_version version() const noexcept;  // AC-R6
    [[nodiscard]] std::string_view msg_type() const noexcept [[clang::lifetimebound]];
    [[nodiscard]] wire::MessageView<wire::access_mode::Index> const& view() const noexcept
        [[clang::lifetimebound]];
    [[nodiscard]] core::expected_t<wire::field_view> field_value(std::uint16_t tag) const noexcept
        [[clang::lifetimebound]];

    // nullptr on resolved-version / MsgType mismatch (no UB, no throw) — AC-R6.
    // Return type is the canonical 2c v1.4 §4.8 owning_message_t<Msg> alias.
    template <class Msg>
    [[nodiscard]] auto as() const noexcept [[clang::lifetimebound]] -> owning_message_t<Msg> const*;

private:
    struct impl;             /* small-variant OR heap polymorphic owner */
    impl* pimpl_ = nullptr;  // R6 stub: heap pimpl; full SBO/polymorphic owner in 2b.
};

// Typed entry point — caller names Msg at compile time; no dispatch overhead.
// Failures: dict_reify_oom (PMR fail trapped via [2a §4.2] trap_throw),
// dict_reify_msg_type_mismatch (view MsgType != Msg::msg_type_v). AC-R1/R3/R8.
// dict_reify_version_mismatch is NOT a failure mode here (dropped per RC#1).
template <class Msg>
[[nodiscard]] core::expected_t<owning_message_t<Msg>> reify_as(
    wire::MessageView<wire::access_mode::Index> const& view,
    std::pmr::memory_resource* mr) noexcept;

// Runtime-dispatch entry point. Resolution (AC-D2/D3/D6/D7):
//  1. peek MsgType(35).
//  2. FIXT-admin hit → {session_admin, profile.session, Unknown} via
//     _dispatch/reify_dispatch_fixt.hpp → vt11::owning_<Msg>.
//  3. miss → read ApplVerID(1128); the wire-owned field-absent error from
//     get<1128>() (NOT a 003 slot — cross-feature note in version_profile.hpp)
//     maps to empty sv → dict::resolve_application_version(profile, value)
//     [003-OWNED, RC#1; AC-VP1..AC-VP6] → {application, profile.session,
//     resolved} via _dispatch/reify_dispatch_application.hpp.
// Failures: dict_reify_oom, dict_reify_msg_type_mismatch,
//   dict_unknown_appl_ver_id, dict_unresolved_application_version (NOT a
//   sentinel fall-through — RC#1), dict_reify_unknown_msg_type (resolved
//   version+MsgType has no codegen owner, e.g. runtime-XML-only version).
[[nodiscard]] core::expected_t<owning_message_handle> reify(
    wire::MessageView<wire::access_mode::Index> const& view, version_profile profile,
    std::pmr::memory_resource* mr) noexcept;

}  // namespace fixpp::dict
