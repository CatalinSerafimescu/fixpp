// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/session_fsm.hpp
//
// fixpp::session FSM state enum — 6 states, NO RecoveryPending (T012).
//
// Anchors: data-model.md E2; research D-2; contracts/session_fsm.hpp;
// [FIX-SL §4.10]. FR-001 / SC-001 / SC-002.
//
// The 6-state machine matches the [FIX-SL §4.10] standard state set exactly.
// RecoveryPending was removed Session-2026-05-18 / Gate A round 1 (D-2):
// a too-high gap is session-fatal for 005; real ResendRequest-driven recovery
// is the deferred session-recovery feature's ([2e §3.1] / [2e §4 last bullet]).
//
// Every state×event cell in the FR-001 event alphabet is DEFINED (no UB, no
// silent no-op). The full transition matrix is in data-model.md. Seam #1
// (tests/session/fsm_transition_matrix_test.cpp — Phase 8 / T058) asserts
// total coverage.
//
// No asio::awaitable in this header. No std::mutex. ([const §XV.9] grep gate:
// this header is safe to include from awaitable coroutine contexts.)
#pragma once

#include <cstdint>

namespace fixpp::session {

// ── FSM state ─────────────────────────────────────────────────────────────────

enum class fsm_state : std::uint8_t {
    NotConnected = 0,   // Initial / reset state. open() transitions to LogonSent
                        // (initiator) or waits for inbound Logon (acceptor).
    LogonSent = 1,      // Initiator: Logon emitted, awaiting peer Logon (ack).
                        // [FIX-SL §4.3.2].
    LogonReceived = 2,  // Acceptor: peer Logon received, own Logon replied.
                        // Transitions to Active on any normal post-Logon message.
                        // [FIX-SL §4.3.10].
    Active = 3,         // Session established. Heartbeat / TestRequest timers
                        // armed. MsgSeqNum counters advance. fromAdmin/fromApp
                        // dispatched via cancellable_dispatch. [FIX-SL §4.5].
    LogoutSent = 4,     // Logout initiated (either side). Inbound messages are
                        // drained without advancing counters or dispatching
                        // fromAdmin/fromApp. Awaiting peer Logout or
                        // graceful-close timeout. [FIX-SL §4.6].
    Disconnected = 5,   // Terminal. All further open()/send()/inbound ignored
                        // (session_already_closed / defined cell). [FIX-SL §4.7].
};

// ── Event alphabet (documented per data-model.md transition matrix) ───────────
//
// Every event below has a defined transition in EVERY source state (I-1; FR-001):
//   open_initiator          — open() called as initiator role
//   inbound_logon_valid     — Logon received with good CompID/BeginString/HeartBtInt
//   inbound_logon_refused   — Logon received but refused (BeginString/CompID/not-1st)
//   inbound_heartbeat       — Heartbeat(35=0) received in Active
//   inbound_test_request    — TestRequest(35=1) received → echo Heartbeat
//   inbound_reject          — Reject(35=3) received (log, no reject-of-reject)
//   inbound_logout          — Logout(35=5) received
//   inbound_out_of_scope_admin — ResendRequest/SequenceReset: defined bounded
//                                reject (session_admin_not_supported, FR-017)
//   seqnum_in_seq           — MsgSeqNum == next-expected: advance counter
//   seqnum_too_low          — MsgSeqNum < next-expected, no PossDupFlag=Y → session-fatal
//   seqnum_too_high         — MsgSeqNum > next-expected → session-fatal, no ResendRequest
//   invalid_msgtype         — parse/type not recognised for current state → session Reject
//   timer_tick              — heartbeat / test-request / graceful-close timeout expiry
//   initiate_logout         — application calls Session::close(graceful)
//   close_terminal_or_fatal — Session::close(terminal) or any session-fatal error path
//
// Transition bodies land per user story in Phase 3 (US1) through Phase 6 (US4).
// Phase 8 T058 seam #1 asserts total coverage.

// ── Transition-table type scaffold ────────────────────────────────────────────
//
// The compile-time transition table is authored in Phase 3–6 (one row per user
// story). This struct tag is the anchor for the constexpr table type that seam
// #1 (T058) tests against. Defined as a forward declaration here so Phase 2
// headers can forward-declare it; Phase 3 introduces the full table type.
struct session_fsm;  // Single-writer on the per-session strand ([2d §4.5];
                     // dispatch always serialised through session_executor).

}  // namespace fixpp::session
