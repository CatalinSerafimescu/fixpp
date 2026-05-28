// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/reconnect_fsm.cpp
//
// fixpp::session::ReconnectFsm — driver layer on top of the 6-state fsm_state.
// Anchors: FR-001..FR-016, D-1..D-5; [FIX-SL §4.3]/§4.5/§4.6;
// data-model.md §E-1; contracts/reconnect_fsm.hpp.
//
// Phase 2 SHAPE: link-green stubs for all declared methods. Behavioral bodies
// land in Phase 3 (T023–T031) per [const §VII.1] TDD ordering.
// ABI NOTE: awaiting_resend_ is a TRANSIENT BOOL on Active — NOT a new
// fsm_state value per D-1 / [arch §5.6]. Do NOT extend fsm_state.

#include <stdexcept>

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
    : factory_{factory},
      policy_{std::move(policy)},
      attempt_index_{0},
      heartbeat_interval_{heartbeat_interval},
      logout_disconnect_timeout_{logout_disconnect_timeout},
      heartbeat_timer_{},
      test_request_timer_{},
      logout_timer_{},
      awaiting_resend_{false},
      resend_state_{},
      last_outbound_testreqid_{}
{}

// ── Phase 2 link-green stubs ──────────────────────────────────────────────────
// All bodies return a stub awaitable/result. Behavioral wiring: Phase 3 T023+.

[[nodiscard]] asio::awaitable<expected_t<void>>
ReconnectFsm::drive_reconnect_attempt() noexcept {
    co_return expected_t<void>{};
}

[[nodiscard]] asio::awaitable<expected_t<void>>
ReconnectFsm::run_heartbeat_cadence() noexcept {
    co_return expected_t<void>{};
}

[[nodiscard]] asio::awaitable<expected_t<void>>
ReconnectFsm::run_inbound_liveness_watch() noexcept {
    co_return expected_t<void>{};
}

[[nodiscard]] expected_t<void>
ReconnectFsm::validate_inbound_heartbeat_testreqid(
    std::string_view /*inbound_testreqid*/) const noexcept
{
    return expected_t<void>{};
}

[[nodiscard]] asio::awaitable<expected_t<void>>
ReconnectFsm::enter_awaiting_resend(std::uint32_t /*inbound_msgseqnum*/) noexcept {
    co_return expected_t<void>{};
}

[[nodiscard]] expected_t<void>
ReconnectFsm::process_inbound_sequence_reset(std::uint32_t /*new_seqno*/,
                                              bool /*gap_fill_flag*/) noexcept {
    return expected_t<void>{};
}

[[nodiscard]] asio::awaitable<expected_t<void>>
ReconnectFsm::reply_to_inbound_resend_request(std::uint32_t /*begin_seqno*/,
                                               std::uint32_t /*end_seqno*/) noexcept {
    co_return expected_t<void>{};
}

[[nodiscard]] asio::awaitable<expected_t<void>>
ReconnectFsm::drive_logout(std::chrono::milliseconds /*timeout*/) noexcept {
    co_return expected_t<void>{};
}

[[nodiscard]] bool ReconnectFsm::is_awaiting_resend() const noexcept {
    return awaiting_resend_;
}

[[nodiscard]] ResendState const& ReconnectFsm::current_resend_state() const noexcept {
    return resend_state_;
}

}  // namespace fixpp::session
