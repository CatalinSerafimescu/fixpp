// contracts/heartbeat.hpp — shape oracle (NOT the build header)
// Heartbeat / TestRequest / graceful-close TIMING entry points
// (effective_clock-driven). Added Gate A round 1 (F-03 / RC#4): the
// declared public header include/fixpp/session/heartbeat.hpp had no
// contract; [const §X.5]/FR-016 require a per-entry-point reentrancy
// contract for this coroutine+timer+cancellation surface.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// asio::awaitable, fixpp::core::expected_t, error, Clock

namespace fixpp::session {

// All timing reads effective_clock (E7, [2d §7.9], NFR-015) — never a
// direct wall-clock call (FR-011). steady_now() for elapsed deltas;
// sleep_until(steady deadline) for every timer, armed under the awaiter's
// ASIO cancellation slot ([2d §6.5], FR-014); no stop_token.
//
// Reentrancy contract (documented per entry point, [const §X.5] / FR-016):
//   arm_heartbeat_timer(...)   — session-strand; (re)arms on outbound
//       activity; fires emit-Heartbeat on idle == negotiated HeartBtInt(108)
//       (S-003). HeartBtInt=0 ⇒ no timers armed ([FIX-SL §4.3.4]).
//   arm_test_request_timer(...) — session-strand; fires emit-TestRequest
//       on inbound silence > test_request_threshold (1×HeartBtInt, D-8);
//       unanswered past the grace window ⇒ session_test_request_unanswered
//       → unhealthy → disconnect (S-004, [FIX-SL §4.5.5]).
//   on_peer_test_request(TestReqID) — session-strand; emits Heartbeat
//       echoing TestReqID(112) ([FIX-SL §4.5.1]).
//   start_graceful_close_timer(...) — session-strand; the [2d §6.5]
//       two-phase close bound: runs under a CHILD cancellation_state with
//       Logout async_write; phase-2 root total only after phase-1; on
//       expiry ⇒ session_logout_timeout → force-disconnect (I-9).
//
// All entry points noexcept across the timer-fire window; a throwing user
// callback TRAPS (core::detail::trap_throw), never propagates (FR-015).
// Zero global new/delete on the timer-fire path ([const §VIII.5], I-7).
// Not reentrant from within a dispatched callback.

}  // namespace fixpp::session
