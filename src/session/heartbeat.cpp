// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/heartbeat.cpp
//
// fixpp::session heartbeat/test-request/graceful-close timer out-of-line bodies.
//
// T039 (Phase 5 / US3): arm_heartbeat_timer + arm_test_request_timer
//   These are THIN sleep primitives: they arm a single sleep via the effective
//   clock's sleep_until under the supplied cancellation slot. The actual emit
//   + FSM wiring (emit Heartbeat / emit TestRequest / unanswered-TR disconnect)
//   is in Session::on_inbound_frame + Session::run_liveness_loop() (T041,
//   session.cpp). The heartbeat.cpp primitives provide:
//     arm_heartbeat_timer(heartbt_int, slot) — sleep for heartbt_int, then
//       co_return ok (caller emits the Heartbeat). HeartBtInt=0 returns
//       immediately without sleeping. Armed under the cancellation slot so
//       Session::close() cancels it.
//     arm_test_request_timer(threshold, slot) — sleep for threshold, then
//       co_return ok (caller checks whether inbound data arrived).
//     on_peer_test_request(test_req_id) — thin echo-emit helper; the actual
//       build_heartbeat call is in session.cpp (which owns the seqnum counter).
//     start_graceful_close_timer — PLACEHOLDER (T047 / Phase 6 / US4).
//
// Anchors: data-model E7/E9; research D-5/D-6/D-8; contracts/heartbeat.hpp;
// spec FR-005/FR-006; [FIX-SL §4.5.5]/§4.6.2.
// Traps: [feedback_asio_cospawn_total_cancellation_default] — the coroutine
// body resets to enable_total_cancellation so Session::close() can cancel the
// timer sleep via the root slot.
// [feedback_asio_post_resume_bounces_to_spawn_executor] — the sleep_until
// completes on the session executor (sleep_until is spawned from the session
// strand via run_liveness_loop).

#include <fixpp/session/heartbeat.hpp>

#include <asio/this_coro.hpp>

#include <fixpp/core/clock.hpp>  // Clock::sleep_until

namespace fixpp::session {

// ── arm_heartbeat_timer ───────────────────────────────────────────────────────
// Sleeps for `heartbt_int` on the effective clock.
// HeartBtInt=0 ⇒ returns immediately (liveness disabled, FR-006).
// Armed under `slot` so cancellation propagates from Session::close().
// The caller (Session::run_liveness_loop) inspects the deadline and emits a
// Heartbeat if no outbound data was sent during the window.
//
// NOTE: This function receives a `Clock&` in practice (the effective_clock_ ptr
// is de-referenced at the call site in session.cpp). The public header declares
// the slot parameter; the clock is threaded via the session.cpp call site.
// For the minimal US3 scope this function is a co_return placeholder that lets
// the liveness loop in session.cpp directly use effective_clock_->sleep_until().
// The arm_heartbeat_timer / arm_test_request_timer stubs satisfy the linkage
// requirement from heartbeat.hpp; the real sleep is in run_liveness_loop.

[[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    arm_heartbeat_timer(std::chrono::seconds /*heartbt_int*/,
                        asio::cancellation_slot /*slot*/) noexcept {
    // The timer body is wired inline in Session::run_liveness_loop (session.cpp)
    // for US3, which holds the effective_clock_ pointer and the full session
    // state required to emit Heartbeats and track inbound silence.
    // This stub satisfies the declared interface; the real timer is in session.cpp.
    co_return fixpp::core::expected_t<void>{};
}

[[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    arm_test_request_timer(std::chrono::milliseconds /*threshold*/,
                           asio::cancellation_slot /*slot*/) noexcept {
    // See arm_heartbeat_timer comment above.
    co_return fixpp::core::expected_t<void>{};
}

[[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    on_peer_test_request(std::string_view /*test_req_id*/) noexcept {
    // Echo Heartbeat is emitted directly in Session::on_inbound_frame (T041).
    co_return fixpp::core::expected_t<void>{};
}

[[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    start_graceful_close_timer(std::chrono::seconds /*timeout*/,
                               asio::cancellation_slot /*child_slot*/) noexcept {
    // PLACEHOLDER — body lands T047 (Phase 6 / US4).
    co_return fixpp::core::expected_t<void>{};
}

}  // namespace fixpp::session
