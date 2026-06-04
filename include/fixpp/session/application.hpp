// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/application.hpp
//
// fixpp::session::Application — the public user-implemented callback interface
// for the FIX application layer (019-app-callbacks, Phase-5 slice 1).
//
// Engine-driven callbacks (onCreate/onLogon/onLogout, fromAdmin/fromApp,
// toAdmin/toApp) run on the engine's `exec_` under single-thread executor
// confinement (015 E-5); serialization derives from that confinement, NOT from
// a per-session strand (INV-2, L-019-3). Return-value contract — NEVER throw
// to signal a normal outcome (FR-015). A thrown exception ⇒ the engine
// terminal-closes the session (FR-011).
//
// Anchor: specs/019-app-callbacks/{research.md D2, data-model.md, contracts/
//         application-interface.md}; [const §XIV.2] (7 methods, 0 pure-virtual);
//         [arch §4.4] (placement in session/ module, no check_layers.py change).
// No std::mutex. No exception control-flow. [const §XV.9].
#pragma once

#include <fixpp/core/error.hpp>   // expected_t<void>
#include <fixpp/wire/parser.hpp>  // wire::MessageView, wire::access_mode

// SessionId is declared in engine.hpp — forward-declare to keep this header
// lightweight (it is included by EngineConfig which is in the awaitable corpus).
// The full definition is transitively available to all callers via engine.hpp.
namespace fixpp::session {
struct SessionId;
}  // namespace fixpp::session

namespace fixpp::session {

// Application — user-implemented observer/interceptor for FIX message flow.
//
// A user subclasses Application and overrides only the callbacks they need.
// All methods have default no-op / default-accept bodies (0 pure-virtual) per
// [const §XIV.2]. The all-default design means a user who does not override a
// callback gets zero behavioural change vs. a session without an Application.
//
// [const §XIV.2] justification: 7 methods, but the irreducible FIX-engine
// callback set mirrored by QuickFIX-C++/J and Fix8 (3 lifecycle hooks, 2
// inbound delivery hooks split by admin/app with distinct reject semantics, 2
// outbound interception hooks split by admin/app with distinct vetoability).
// None is collapsible without losing a distinct, standard FIX semantic.
// Pure-virtual count = 0, satisfying §XIV.2 cap.
class Application {
public:
    virtual ~Application() = default;

    // ── Lifecycle callbacks (notifications; no reject) ────────────────────────
    // All three run on the engine's exec_ (single-thread confinement); a throw ⇒ terminal close.

    // onCreate: session object constructed and exec_ initialized by open(),
    //   BEFORE the first Logon is sent or received (FR-009). Use for per-session
    //   resource initialization. The executor() is valid at this point.
    virtual void onCreate(const SessionId& /*id*/) {}  // FR-009

    // onLogon: session reached established (Active) state (FR-009).
    //   The user may begin originating sends.
    virtual void onLogon(const SessionId& /*id*/) {}  // FR-009

    // onLogout: session leaving established state — fires on graceful close,
    //   terminal close, OR callback-threw exit (exactly once per session via
    //   the single Active→!Active edge; INV-7). Use for per-session cleanup.
    virtual void onLogout(const SessionId& /*id*/) {}  // FR-009

    // ── Inbound delivery callbacks (after engine FSM processing) ─────────────
    // Both run on the engine's exec_ after the FSM accepts the frame (INV-6).

    // fromAdmin: inbound admin message (MsgType 0/1/2/3/4/5), after FSM
    //   processing (FR-004). Default: accept.
    //   return {} (value) ⇒ accepted; return unexpected(e) ⇒ engine emits
    //   session Reject(35=3) to the peer (FR-005).
    virtual fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const SessionId& /*id*/) {
        return {};  // default accept (FR-002)
    }  // FR-003/FR-004/FR-005

    // fromApp: inbound application message (any non-session MsgType), after
    //   FSM processing (FR-003). Default: accept.
    //   return {} (value) ⇒ accepted; return unexpected(e) ⇒ engine emits
    //   BusinessMessageReject(35=j) to the peer (FR-005).
    virtual fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const SessionId& /*id*/) {
        return {};  // default accept (FR-002)
    }  // FR-003/FR-005

    // ── Outbound interception callbacks (before transmit) ─────────────────────

    // toAdmin: engine about to emit an admin message; inspect only — the
    //   message is ALWAYS sent regardless of the return (admin not vetoable,
    //   FR-008). Default: no-op.
    virtual void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
                         const SessionId& /*id*/) {}  // FR-008

    // toApp: engine or user about to emit an application message (FR-007).
    //   Default: send.
    //   return {} (value) ⇒ send; return unexpected(error::app_do_not_send) ⇒
    //   veto (DoNotSend — message not transmitted, Engine::send returns
    //   unexpected(app_do_not_send)); return unexpected(other_error) ⇒ abort.
    virtual fixpp::core::expected_t<void> toApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const SessionId& /*id*/) {
        return {};  // default send (FR-002)
    }  // FR-007
};

}  // namespace fixpp::session
