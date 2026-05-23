// SPDX-License-Identifier: AGPL-3.0-or-later
// Shape oracle for 009-session-fsm-finalize FR-004: session_role enum + the
// SessionConfig.role field.
//
// This is a CONTRACT SHAPE ORACLE, not an implementation header. The actual
// declaration lands in include/fixpp/session/session_config.hpp during
// /speckit-implement; this file documents the binding contract that
// implementation must satisfy.
//
// Source: spec.md FR-004; research.md D-1; Opus pr81 round-1 triage RC#2.

#pragma once

#include <cstdint>

namespace fixpp::session {

// Selects the role for a Session at construction time. Drives Session::open()
// initial-state choice.
//
// - initiator: Session::open() sets fsm_state_ = LogonSent and emits an
//   initial Logon (the existing 005 path; preserved as the default to avoid
//   breaking inherited tests).
// - acceptor:  Session::open() sets fsm_state_ = NotConnected and emits NO
//   outbound Logon. The session waits for a peer Logon via on_inbound_frame;
//   when a valid peer Logon arrives, the session traverses
//   NotConnected -> LogonReceived -> Active per data-model.md:19 of 005.
//
// Default: initiator. SessionConfig.role defaults to session_role::initiator
// so existing 005 tests + integrations that built SessionConfig without an
// explicit role retain the prior behavior.
enum class session_role : std::uint8_t {
    initiator = 0,
    acceptor  = 1,
};

// SessionConfig field (added to the 005-owned SessionConfig in
// include/fixpp/session/session_config.hpp):
//
//   session_role role = session_role::initiator;
//
// Mood: storage-only POD field; no validation; no defaulted-yet-overridable
// semantics. Read once by Session::open() to branch the initial state.
//
// Reentrancy: the field is read on the per-session strand during open(); no
// other code path mutates or reads it concurrently. No synchronization needed.

}  // namespace fixpp::session
