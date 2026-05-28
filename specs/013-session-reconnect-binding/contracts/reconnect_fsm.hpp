// SPDX-License-Identifier: AGPL-3.0-or-later
//
// fixpp — 013-session-reconnect-binding
// Contract: ReconnectFsm — driver on top of the 6-state fsm_state machine.
// Anchors: FR-001..FR-008, FR-009..FR-016, D-1..D-5; [FIX-SL §4.3], [FIX-SL §4.5], [FIX-SL §4.6].
//
// NOTE: This is the Phase-1 contract emitted by /speckit-plan. The shipped form
// lands at include/fixpp/session/reconnect_fsm.hpp post-/speckit-implement.

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <asio/awaitable.hpp>
#include <asio/steady_timer.hpp>

#include "fixpp/core/error.hpp"
#include "fixpp/core/expected.hpp"
#include "fixpp/session/resend_state.hpp"
#include "fixpp/session/session_fsm.hpp"
#include "fixpp/transport/reconnect_policy.hpp"
#include "fixpp/transport/transport_factory.hpp"

namespace fixpp::session {

// ReconnectFsm — driver layer on top of session_fsm. Owns the per-attempt
// Transport mint via TransportFactory, the AwaitingResend transient flag on
// Active (NOT a new fsm_state value per D-1), the Heartbeat / TestRequest
// cadence timers, and the Logout / force-disconnect timeout.
class ReconnectFsm {
public:
    // Constructed by Session at SessionConfig-build time. Holds a shared_ptr to
    // TransportFactory so reload_credentials can atomically swap the underlying
    // cert_source slot without invalidating the factory pointer.
    ReconnectFsm(std::shared_ptr<fixpp::transport::TransportFactory> factory,
                 fixpp::transport::ReconnectPolicy policy,
                 std::chrono::seconds heartbeat_interval,
                 std::chrono::milliseconds logout_disconnect_timeout) noexcept;

    // FR-001 / FR-002 — drive a reconnect attempt: walk policy_, mint fresh
    // Transport via factory_->make(...), re-Logon. Returns transport_*
    // variants on connect / handshake failure, session_* variants on Logon
    // protocol failure.
    [[nodiscard]] asio::awaitable<expected_t<void>>
    drive_reconnect_attempt() noexcept;

    // FR-003 / FR-005 — emit Heartbeat(0) outbound when no outbound traffic has
    // been sent for HeartBtInt seconds. Armed on Active entry; rearmed on every
    // outbound; cancelled on Disconnected entry.
    [[nodiscard]] asio::awaitable<expected_t<void>>
    run_heartbeat_cadence() noexcept;

    // FR-004 / FR-007 — emit TestRequest(1) when no inbound traffic for 1.2×
    // HeartBtInt; if no inbound within 2× HeartBtInt total, Logout(5) and
    // disconnect with session_heartbeat_timeout (slot 119).
    [[nodiscard]] asio::awaitable<expected_t<void>>
    run_inbound_liveness_watch() noexcept;

    // FR-006 — validate inbound Heartbeat's TestReqID(112) matches the most
    // recent outbound TestRequest's TestReqID. Mismatch → session_testreqid_mismatch
    // (slot 120).
    [[nodiscard]] expected_t<void>
    validate_inbound_heartbeat_testreqid(std::string_view inbound_testreqid) const noexcept;

    // FR-009 — enter AwaitingResend transient on inbound MsgSeqNum > next_expected_inbound.
    // Emits ResendRequest(2){[next_expected_inbound, inbound_msgseqnum-1]}; populates
    // resend_state_. Active outbound traffic continues during AwaitingResend per spec
    // §FR-009; inbound above next_expected_inbound is HELD until gap closes.
    [[nodiscard]] asio::awaitable<expected_t<void>>
    enter_awaiting_resend(std::uint32_t inbound_msgseqnum) noexcept;

    // FR-013 / FR-014 — process inbound SequenceReset(4). GapFillFlag=Y → advance
    // next_expected_inbound to NewSeqNo without storing; GapFillFlag=N (forced reset)
    // → advance + emit warning event.
    [[nodiscard]] expected_t<void>
    process_inbound_sequence_reset(std::uint32_t new_seqno,
                                   bool gap_fill_flag) noexcept;

    // FR-010 / FR-011 / FR-012 — reply to inbound ResendRequest(2): walk MessageStore
    // [BeginSeqNo, EndSeqNo] (EndSeqNo=0 → our_last_outbound per D-2); for each
    // application message, emit with PossDupFlag(43)=Y + OrigSendingTime(122);
    // collapse admin spans to SequenceReset-GapFill(4) per D-3; emit GapFill on
    // store-horizon per D-4.
    [[nodiscard]] asio::awaitable<expected_t<void>>
    reply_to_inbound_resend_request(std::uint32_t begin_seqno,
                                    std::uint32_t end_seqno) noexcept;

    // FR-008 / US1 AC5 — initiator-graceful Logout. Emits Logout(5), awaits peer
    // reply for logout_disconnect_timeout_, closes Transport, transitions to
    // Disconnected. Surfaces session_logout_disconnect_timeout (slot 118) if
    // elapsed before peer reply. Symmetric on acceptor side per [[feedback_half_restructure_symmetric_api]].
    [[nodiscard]] asio::awaitable<expected_t<void>>
    drive_logout(std::chrono::milliseconds timeout) noexcept;

    // Accessor — TRANSIENT FLAG, NOT a new fsm_state value per D-1.
    [[nodiscard]] bool is_awaiting_resend() const noexcept;

    // Accessor — current resend state (only valid when is_awaiting_resend()).
    [[nodiscard]] ResendState const& current_resend_state() const noexcept;

private:
    std::shared_ptr<fixpp::transport::TransportFactory> factory_;
    fixpp::transport::ReconnectPolicy policy_;
    std::uint32_t attempt_index_;
    std::chrono::seconds heartbeat_interval_;
    std::chrono::milliseconds logout_disconnect_timeout_;
    asio::steady_timer heartbeat_timer_;
    asio::steady_timer test_request_timer_;
    asio::steady_timer logout_timer_;
    bool awaiting_resend_;
    ResendState resend_state_;
    std::optional<std::string> last_outbound_testreqid_;
};

}  // namespace fixpp::session
