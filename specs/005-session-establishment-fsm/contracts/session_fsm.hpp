// contracts/session_fsm.hpp — shape oracle (NOT the build header)
// The 6-state FIX session FSM. [FIX-SL §4.10] matrix. No RecoveryPending
// half-state (removed Session-2026-05-18 / D-2 — recovery is deferred and
// the bound oracle requires a ResendRequest(35=2) on a gap).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstdint>

namespace fixpp::session {

enum class fsm_state : std::uint8_t {
    NotConnected = 0,
    LogonSent,        // initiator path
    LogonReceived,    // acceptor path ([FIX-SL §4.3.10])
    Active,
    LogoutSent,
    Disconnected,
};

// Events the FSM consumes (every state×event cell is defined — I-1; no UB;
// full alphabet + guard precedence in data-model.md transition matrix):
//   open_initiator | inbound_logon_valid | inbound_logon_refused
//   | inbound_heartbeat | inbound_test_request | inbound_reject
//   | inbound_logout | inbound_out_of_scope_admin (RR/SeqReset)
//   | seqnum_in_seq | seqnum_too_low | seqnum_too_high
//   | invalid_msgtype_or_type_invalid_for_state
//   | dup_logon_while_active | simultaneous_logon_resolution
//   | timer_tick | initiate_logout | close_terminal_or_fatal
//
// Contract highlights (see data-model.md transition matrix):
//  - seqnum_too_high → SESSION-FATAL: surface
//    session_seqnum_gap_unrecoverable, emit orderly Logout(text),
//    disconnect. NO ResendRequest/SequenceReset emitted by 005 (recovery
//    deferred — Session-2026-05-18); the deferred session-recovery feature
//    replaces this with real ResendRequest-driven recovery on reconnect.
//  - seqnum_too_low(no PossDup) → session-fatal Logout(text)+
//    disconnect ([FIX-SL §4.1] ordered-sequence integrity).
//  - Receipt of deferred admin types (ResendRequest/SequenceReset) is a
//    DEFINED bounded transition (session-level Reject /
//    session_admin_not_supported, FR-017) — never undefined.
//  - close(graceful): two-phase ([2d §6.5]) — Logout async_write + close-
//    timeout under a CHILD cancellation_state; phase-2 root total only
//    after phase-1; close(terminal) skips phase-1; close() idempotent (I-9).
struct session_fsm;  // single-writer on the per-session strand.

}  // namespace fixpp::session
