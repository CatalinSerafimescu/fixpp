// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/heartbeat.cpp
//
// fixpp::session heartbeat/test-request/graceful-close timer out-of-line
// bodies (005, T018 stub).
//
// Phase 2 ships STUBS that link cleanly (T018). Full implementations land
// per user story:
//   T039 (Phase 5 / US3): arm_heartbeat_timer + arm_test_request_timer
//         (via Clock::sleep_until, child cancellation_state; HeartBtInt=0
//         disables; D-8 defaults).
//   T040 (Phase 5 / US3): on_peer_test_request (echo Heartbeat(TestReqID)).
//   T047 (Phase 6 / US4): start_graceful_close_timer (two-phase close bound,
//         [2d §6.5]; 2 s QuickFIX LogoutTimeout default, D-8; session_logout_timeout).
//
// Anchors: data-model.md E7/E9; research D-5/D-6/D-8; contracts/heartbeat.hpp;
// spec FR-005/FR-006; [FIX-SL §4.5.5]/§4.6.2.
#include <fixpp/session/heartbeat.hpp>

namespace fixpp::session {

[[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    arm_heartbeat_timer(std::chrono::seconds /*heartbt_int*/,
                        asio::cancellation_slot /*slot*/) noexcept {
    // PLACEHOLDER — body lands T039 (Phase 5 / US3).
    co_return fixpp::core::expected_t<void>{};
}

[[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    arm_test_request_timer(std::chrono::milliseconds /*threshold*/,
                           asio::cancellation_slot /*slot*/) noexcept {
    // PLACEHOLDER — body lands T039 (Phase 5 / US3).
    co_return fixpp::core::expected_t<void>{};
}

[[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    on_peer_test_request(std::string_view /*test_req_id*/) noexcept {
    // PLACEHOLDER — body lands T040 (Phase 5 / US3).
    co_return fixpp::core::expected_t<void>{};
}

[[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    start_graceful_close_timer(std::chrono::seconds /*timeout*/,
                               asio::cancellation_slot /*child_slot*/) noexcept {
    // PLACEHOLDER — body lands T047 (Phase 6 / US4).
    co_return fixpp::core::expected_t<void>{};
}

}  // namespace fixpp::session
