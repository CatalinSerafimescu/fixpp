// SPDX-License-Identifier: AGPL-3.0-or-later
// Phase 1 contract — literal extract of [2c §4.8]. The shipped header is
// include/fixpp/dict/reify.hpp. Header-mostly; out-of-line .cpp split locked
// at /tasks (research.md D-5). No method is added to wire::MessageView (AC-R1).
#pragma once
#include <fixpp/core/error.hpp>                  // fixpp::core::expected_t, error
#include <fixpp/dict/version_profile.hpp>        // version_profile + dict::resolve_application_version
                                                 // — 003-OWNED, NOT 002-shipped: 002 deferred both
                                                 // (Gate A r1 Codex P1-1 / Opus RC#1; spec §8
                                                 // "Upstream dependency audit"). Header/contract
                                                 // added at re-/plan, not this convergence pass.
#include <fixpp/wire/message_view_contract.hpp>  // vendored frozen stub (R6 / D-2). NOTE: this is a
                                                 // hand-written dict/ header #include-ing wire/ —
                                                 // an OPEN layer-amendment item (Codex P1-3 / RC#3),
                                                 // NOT covered by the arch §2.4 generated-header
                                                 // carve-out. See spec NFR-003-8 / R6.
#include <cstdint>
#include <memory_resource>
#include <string_view>

namespace fixpp::dict {

// Per [2c §4.8]. resolved_message_version carries (kind, session, application)
// per RC#1; FIXT admin → {session_admin, vt11, Unknown}; application → the
// resolved value (v42/v44/v50sp2).
struct resolved_message_version;  // exact layout pinned at /tasks vs [2c §4.8]

// owning_message_t<Msg> — the return-type alias of reify_as<Msg> (Opus N-P2-1:
// previously used in reify_as / owning_message_handle::as<> / AC-R1 / data-model
// Entity 6 but NEVER defined; the name-mangling/ADL surface every downstream
// consumer binds to must be pinned in the contract, not deferred). Canonical
// 2c §4.8 form: each codegen Reify.hpp emits owning_<Msg> and exposes it as
// Msg::owning_type, so owning_message_t<v44::NewOrderSingle> ≡
// v44::owning_NewOrderSingle.
template <class Msg>
using owning_message_t = typename Msg::owning_type;  // AC-R1; [2c §4.8]

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
    template <class Msg>
    [[nodiscard]] auto as() const noexcept [[clang::lifetimebound]]
        -> owning_<Msg> const*;

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
//  3. miss → read ApplVerID(1128) (dict_field_not_present → empty sv) →
//     dict::resolve_application_version(profile, value) [003-OWNED free fn —
//     NOT 002-shipped; 002 deferred it. Gate A r1 Codex P1-1 / Opus RC#1;
//     spec §8. Contract/header added at re-/plan.] →
//     {application, profile.session, resolved} via
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
