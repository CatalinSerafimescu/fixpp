// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/reconnect_fsm.cpp
//
// fixpp::session::ReconnectFsm — driver layer on top of the 6-state fsm_state.
// Anchors: FR-001..FR-016, D-1..D-5; [FIX-SL §4.3]/§4.5/§4.6;
// data-model.md §E-1; contracts/reconnect_fsm.hpp.
//
// Phase 3: FR-009 state entry/exit wired (T023/T026). State is owned here;
// the ResendRequest emit logic lives in session.cpp (which calls into these
// methods) because it requires Session's seqnum_mgr_ + store_then_emit
// infrastructure — injecting those would require a 5th ctor parameter
// that breaks test fixtures constructing ReconnectFsm directly with 4 args.
// Escalation: session.cpp inline emit + ReconnectFsm state ownership is the
// correct partition given the test-fixture constraint.
//
// ABI NOTE: awaiting_resend_ is a TRANSIENT BOOL on Active — NOT a new
// fsm_state value per D-1 / [arch §5.6]. Do NOT extend fsm_state.

#include <asio/this_coro.hpp>

#include "fixpp/session/reconnect_fsm.hpp"

namespace fixpp::session {

// ── Constructor ───────────────────────────────────────────────────────────────
//
// Initializes all scalar fields. Timer optionals start empty; populated on
// first async use in Phase 3. [data-model §E-1]
ReconnectFsm::ReconnectFsm(fixpp::transport::TransportFactory* factory,
                            fixpp::transport::ReconnectPolicy   policy,
                            std::chrono::seconds                heartbeat_interval,
                            std::chrono::milliseconds           logout_disconnect_timeout) noexcept
    // Remaining members (attempt_index_, the three timer optionals,
    // awaiting_resend_, resend_state_, last_outbound_testreqid_) take their
    // header NSDMI / default-constructed empty state. [data-model §E-1]
    : factory_{factory},
      policy_{std::move(policy)},
      heartbeat_interval_{heartbeat_interval},
      logout_disconnect_timeout_{logout_disconnect_timeout}
{}

// ── drive_reconnect_attempt ───────────────────────────────────────────────────
//
// FR-001 / FR-002: mint fresh Transport via factory_->make() per attempt.
// Phase 3 stub: calls make() once. Full retry loop (policy walk + limit) is
// deferred to Phase 4 US2 per plan.md task ordering.
[[nodiscard]] asio::awaitable<expected_t<void>>
ReconnectFsm::drive_reconnect_attempt() noexcept {
    if (factory_ != nullptr) {
        auto exec = co_await asio::this_coro::executor;
        fixpp::tls::SslCtxConfig ssl_cfg{};
        auto t = factory_->make(exec, std::move(ssl_cfg), nullptr);
        (void)t;
    }
    co_return expected_t<void>{};
}

// ── run_heartbeat_cadence ─────────────────────────────────────────────────────
//
// FR-003 / FR-005: deferred to Phase 4 (heartbeat timer arm + outbound idle).
// Liveness logic lives in session.cpp run_liveness_loop() for Phase 3.
[[nodiscard]] asio::awaitable<expected_t<void>>
ReconnectFsm::run_heartbeat_cadence() noexcept {
    co_return expected_t<void>{};
}

// ── run_inbound_liveness_watch ────────────────────────────────────────────────
//
// FR-004 / FR-007: deferred to Phase 4. Session.cpp run_liveness_loop() covers
// the inbound-idle TestRequest emit for Phase 3.
[[nodiscard]] asio::awaitable<expected_t<void>>
ReconnectFsm::run_inbound_liveness_watch() noexcept {
    co_return expected_t<void>{};
}

// ── validate_inbound_heartbeat_testreqid ─────────────────────────────────────
//
// FR-006: mismatch → session_testreqid_mismatch (slot 118). Phase 3 check
// is inline in session.cpp on_inbound_frame; this method stub is Phase 4.
[[nodiscard]] expected_t<void>
ReconnectFsm::validate_inbound_heartbeat_testreqid(
    std::string_view /*inbound_testreqid*/) const noexcept
{
    return expected_t<void>{};
}

// ── enter_awaiting_resend ─────────────────────────────────────────────────────
//
// FR-009: set awaiting_resend_=true and populate resend_state_ with the
// [begin, end] range from the inbound too-high seqnum detection.
// The ResendRequest(2) emit is performed by session.cpp::on_inbound_frame
// (which has access to seqnum_mgr_ and store_then_emit); this method owns
// the STATE transition only per data-model.md §E-1.
// [spec.md FR-009; data-model.md §E-1; plan.md T023/T026]
[[nodiscard]] asio::awaitable<expected_t<void>>
ReconnectFsm::enter_awaiting_resend(std::uint32_t begin_seqno,
                                    std::uint32_t end_seqno) noexcept {
    awaiting_resend_ = true;
    resend_state_.outstanding_begin = begin_seqno;
    resend_state_.outstanding_end   = end_seqno;
    co_return expected_t<void>{};
}

// ── exit_awaiting_resend ──────────────────────────────────────────────────────
//
// Clear the AwaitingResend transient flag and reset resend_state_. Called by
// session.cpp when the gap closes. [data-model §E-1]
void ReconnectFsm::exit_awaiting_resend() noexcept {
    awaiting_resend_ = false;
    resend_state_.reset();
}

// ── process_inbound_sequence_reset ───────────────────────────────────────────
//
// FR-013 / FR-014: advance next_expected_inbound to NewSeqNo (GapFillFlag=Y)
// or forced reset (GapFillFlag=N). Phase 3 stub — seqnum advance is handled
// inline in session.cpp; this method is the Phase 4 hook.
[[nodiscard]] expected_t<void>
ReconnectFsm::process_inbound_sequence_reset(std::uint32_t /*new_seqno*/,
                                              bool /*gap_fill_flag*/) noexcept {
    return expected_t<void>{};
}

// ── reply_to_inbound_resend_request ──────────────────────────────────────────
//
// FR-010 / FR-011 / FR-012: walk MessageStore [begin, end] and emit replays.
// The reply logic lives INLINE in session.cpp on_inbound_frame (which has the
// MessageStore + transport_send + scan_frame_header it needs): per-slot store
// walk, replay stored application messages with PossDupFlag(43)=Y +
// OrigSendingTime(122), and collapse absent/admin runs into SequenceReset-
// GapFill. This method remains a thin no-op hook for symmetry with the other
// ReconnectFsm driver entry points (state ownership only, no emit). The T016
// store-horizon witness drives the inline path via a seeded MessageStore.
[[nodiscard]] asio::awaitable<expected_t<void>>
ReconnectFsm::reply_to_inbound_resend_request(std::uint32_t /*begin_seqno*/,
                                               std::uint32_t /*end_seqno*/) noexcept {
    co_return expected_t<void>{};
}

// ── drive_logout ──────────────────────────────────────────────────────────────
//
// FR-008: emit Logout(5), arm timer, await peer reply. Phase 3 stub — Logout
// logic is inline in session.cpp run_logout_phase1 / on_inbound_frame.
[[nodiscard]] asio::awaitable<expected_t<void>>
ReconnectFsm::drive_logout(std::chrono::milliseconds /*timeout*/) noexcept {
    co_return expected_t<void>{};
}

// ── Accessors ─────────────────────────────────────────────────────────────────

[[nodiscard]] bool ReconnectFsm::is_awaiting_resend() const noexcept {
    return awaiting_resend_;
}

[[nodiscard]] ResendState const& ReconnectFsm::current_resend_state() const noexcept {
    return resend_state_;
}

}  // namespace fixpp::session
