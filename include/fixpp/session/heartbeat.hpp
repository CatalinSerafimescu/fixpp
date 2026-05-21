// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/heartbeat.hpp
//
// fixpp::session heartbeat/test-request/graceful-close timer declarations (005, T017).
//
// All timing reads effective_clock (E7, [2d §7.9], NFR-015) via the per-session
// Clock pointer — never a direct wall-clock call (FR-011). Timers are armed
// via Clock::sleep_until(steady deadline) under the awaiter's ASIO cancellation
// slot ([2d §6.5], FR-014); no std::stop_token ([const §XI.2]).
//
// Reentrancy contract (documented per entry point, [const §X.5] / FR-016):
//   arm_heartbeat_timer()       — session-strand; (re)armed on outbound activity.
//   arm_test_request_timer()    — session-strand; tracks inbound silence window.
//   on_peer_test_request(id)    — session-strand; echoes Heartbeat(TestReqID).
//   start_graceful_close_timer() — session-strand; child cancellation_state;
//                                  Logout timeout → force disconnect (I-9).
// All entry points noexcept across the timer-fire window ([const §X.5]).
// Zero global new/delete on the timer-fire path ([const §VIII.5], I-7).
//
// Anchors: data-model.md E7/E9; research D-5/D-6/D-8; contracts/heartbeat.hpp;
// spec FR-005/FR-006; [FIX-SL §4.5.5]/§4.6.2. S-003/S-004.
//
// Bodies land per user story: T039 (Phase 5 / US3) arms heartbeat + test-request;
// T047 (Phase 6 / US4) wires the graceful-close timer.
//
// No asio::awaitable in this header beyond the return types required by the
// public interface (returned, not co_awaited here — no std::mutex visible in
// an awaitable context). ([const §XV.9] grep gate: safe.)
#pragma once

#include <chrono>
#include <string_view>

#include <asio/awaitable.hpp>
#include <asio/cancellation_state.hpp>  // asio::cancellation_slot is in this header

#include <fixpp/core/error.hpp>

namespace fixpp::session {

// ── Heartbeat timer ───────────────────────────────────────────────────────────

// (Re)arm the outbound-idle heartbeat timer. HeartBtInt=0 disables timers.
// Fires emit-Heartbeat(35=0) on outbound idle >= negotiated HeartBtInt.
// PLACEHOLDER — body lands T039 (Phase 5 / US3).
[[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    arm_heartbeat_timer(std::chrono::seconds heartbt_int,
                        asio::cancellation_slot slot) noexcept;

// ── TestRequest timer ─────────────────────────────────────────────────────────

// Arm the inbound-silence TestRequest timer. Fires emit-TestRequest(35=1) on
// inbound silence > test_request_threshold (default 1×HeartBtInt, D-8).
// Unanswered past the grace window → session_test_request_unanswered →
// unhealthy → disconnect ([FIX-SL §4.5.5]).
// PLACEHOLDER — body lands T039 (Phase 5 / US3).
[[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    arm_test_request_timer(std::chrono::milliseconds threshold,
                           asio::cancellation_slot slot) noexcept;

// Handle an inbound TestRequest: emit Heartbeat echoing TestReqID(112).
// PLACEHOLDER — body lands T040 (Phase 5 / US3).
[[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    on_peer_test_request(std::string_view test_req_id) noexcept;

// ── Graceful-close timer ──────────────────────────────────────────────────────

// Start the two-phase graceful-close timeout ([2d §6.5] / D-6 / D-8).
// Runs under a CHILD asio::cancellation_state (not the root slot).
// On expiry: session_logout_timeout → force-disconnect → Disconnected (I-9).
// D-8 default: 2 s (QuickFIX LogoutTimeout). FR-005 / [FIX-SL §4.6.2].
// PLACEHOLDER — body lands T047 (Phase 6 / US4).
[[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    start_graceful_close_timer(std::chrono::seconds timeout,
                               asio::cancellation_slot child_slot) noexcept;

}  // namespace fixpp::session
