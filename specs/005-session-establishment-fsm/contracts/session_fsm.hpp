// contracts/session_fsm.hpp — shape oracle (NOT the build header)
// The 7-state FIX session FSM. [FIX-SL §4.10] matrix; RecoveryPending per
// Clarification Q1 / D-2. SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstdint>

namespace fixpp::session {

enum class fsm_state : std::uint8_t {
    NotConnected = 0,
    LogonSent,        // initiator path
    LogonReceived,    // acceptor path ([FIX-SL §4.3.10])
    Active,
    RecoveryPending,  // gap detected; held; surfaced; NOT disconnected (Q1).
                      // EXIT transitions are a DEFERRED seam (recovery feature).
    LogoutSent,
    Disconnected,
};

// Events the FSM consumes (every state×event cell is defined — I-1; no UB):
//   open_initiator | inbound_logon_valid | inbound_logon_refused
//   | seqnum_in_seq | seqnum_too_low | seqnum_too_high
//   | inbound_test_request | timer_tick | inbound_logout
//   | initiate_logout | close_terminal_or_fatal
//
// Contract highlights (see data-model.md transition matrix):
//  - Active + seqnum_too_high  → RecoveryPending: hold msg (not processed,
//    not counted), surface session_recovery_pending, NO ResendRequest/
//    SequenceReset emitted by 005, session stays connected.
//  - Active + seqnum_too_low(no PossDup) → session-fatal Logout(text)+
//    disconnect ([FIX-SL §4.5.3]).
//  - Receipt of deferred admin types (ResendRequest/SequenceReset) is a
//    DEFINED bounded transition consistent with the RecoveryPending seam
//    (FR-017) — never undefined.
//  - close(graceful): two-phase ([2d §6.5]) — Logout async_write + close-
//    timeout under a CHILD cancellation_state; phase-2 root total only
//    after phase-1; close(terminal) skips phase-1; close() idempotent (I-9).
struct session_fsm;  // single-writer on the per-session strand.

}  // namespace fixpp::session
