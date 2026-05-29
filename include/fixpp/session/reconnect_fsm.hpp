// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/reconnect_fsm.hpp
//
// fixpp::session::ReconnectFsm — driver layer on top of the 6-state fsm_state
// machine. Anchors: FR-001..FR-008, FR-009..FR-016, D-1..D-5; [FIX-SL §4.3],
// [FIX-SL §4.5], [FIX-SL §4.6]; data-model.md §E-1; contracts/reconnect_fsm.hpp.
//
// ABI PRESERVATION NOTE: The 6-state fsm_state enum (NotConnected / LogonSent /
// LogonReceived / Active / LogoutSent / Disconnected) has a FROZEN ABI per
// [arch §5.6] / 010 F-04. awaiting_resend_ is a TRANSIENT BOOL on Active
// (NOT a new fsm_state value per D-1). Do NOT add a 7th fsm_state variant;
// doing so would break every 010 F-04 consumer.
//
// Factory ownership: factory_ is a NON-OWNING raw pointer to the factory
// owned by SessionConfig::transport_factory_override (shared_ptr; 010 FR-001a
// precedent; 013 T011). The engine guarantees the factory outlives the FSM
// per [arch §5.6] frozen-at-open rule.
//
// cert_source snapshot discipline: the FSM reads factory_->cert_source_snapshot()
// at drive_reconnect_attempt entry (returns a strong-ref std::shared_ptr<cert_source>
// BY VALUE COPY). NEVER captures a raw cert_source* or a weak_ptr<cert_source>
// — per [[feedback_weak_ptr_cache_needs_owning_context]].
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include <asio/awaitable.hpp>
#include <asio/steady_timer.hpp>
#include <asio/any_io_executor.hpp>

#include "fixpp/core/error.hpp"   // error enum + expected_t<T>
#include "fixpp/session/resend_state.hpp"
#include "fixpp/session/session_fsm.hpp"
#include "fixpp/transport/reconnect_policy.hpp"

// TransportFactory is used only as a non-owning raw pointer in this header (ctor
// arg + factory_ member); the full definition is needed only in reconnect_fsm.cpp
// (factory_->make() + SslCtxConfig). Forward-declare here rather than #include
// transport_factory.hpp: that header transitively pulls tls/pinset.hpp's
// std::shared_mutex, and this header includes <asio/awaitable.hpp> — so the
// include would drag a std::mutex type into the asio::awaitable closure of every
// consumer (session.hpp), violating [const §XV.9] / [2f §6.6] (caught by the
// check_no_std_mutex_corpus Tier-1 gate).
namespace fixpp::transport { class TransportFactory; }

namespace fixpp::session {

using core::expected_t;

// ReconnectFsm — driver layer on top of session_fsm. Owns the per-attempt
// Transport mint via TransportFactory, the AwaitingResend transient flag on
// Active (NOT a new fsm_state value per D-1), the Heartbeat / TestRequest
// cadence timers, and the Logout / force-disconnect timeout.
// [data-model §E-1]
class ReconnectFsm {
public:
    // Constructed by Session at SessionConfig-build time. Holds a non-owning
    // raw pointer to TransportFactory; the factory itself is owned by
    // SessionConfig::transport_factory_override per 2h Appendix D §D.1+§D.2
    // sign-off. The engine guarantees the factory outlives the FSM per
    // [arch §5.6] frozen-at-open rule. [data-model §E-1]
    ReconnectFsm(fixpp::transport::TransportFactory* factory,
                 fixpp::transport::ReconnectPolicy   policy,
                 std::chrono::seconds                heartbeat_interval,
                 std::chrono::milliseconds           logout_disconnect_timeout) noexcept;

    // FR-001 / FR-002 — drive a reconnect attempt: walk policy_, mint fresh
    // Transport via factory_->make(...), re-Logon. Returns transport_*
    // variants on connect / handshake failure, session_* variants on Logon
    // protocol failure.
    //
    // cert_source consumption: reads factory_->cert_source_snapshot() at
    // attempt entry (strong-ref shared_ptr BY VALUE COPY). The captured
    // strong-ref keeps the OLD cert_source alive for the handshake duration
    // even if an operator-driven reload_credentials lands during the handshake;
    // the NEXT drive_reconnect_attempt reads the NEW snapshot. NEVER captures
    // raw cert_source* or weak_ptr<cert_source> per
    // [[feedback_weak_ptr_cache_needs_owning_context]]. [FR-033 / data-model §E-1]
    [[nodiscard]] asio::awaitable<expected_t<void>>
    drive_reconnect_attempt() noexcept;

    // FR-003 / FR-005 — emit Heartbeat(0) outbound when no outbound traffic has
    // been sent for HeartBtInt seconds. Armed on Active entry; rearmed on every
    // outbound; cancelled on Disconnected entry.
    [[nodiscard]] asio::awaitable<expected_t<void>>
    run_heartbeat_cadence() noexcept;

    // FR-004 / FR-007 — emit TestRequest(1) when no inbound traffic for
    // 1.2×HeartBtInt; if no inbound within 2×HeartBtInt total, Logout(5) and
    // disconnect with session_test_request_unanswered (slot 74, 005-era reused
    // per F1/D1 2026-05-28).
    [[nodiscard]] asio::awaitable<expected_t<void>>
    run_inbound_liveness_watch() noexcept;

    // FR-006 — validate inbound Heartbeat's TestReqID(112) matches the most
    // recent outbound TestRequest's TestReqID. Mismatch → session_testreqid_mismatch
    // (slot 118). [data-model §E-1]
    [[nodiscard]] expected_t<void>
    validate_inbound_heartbeat_testreqid(std::string_view inbound_testreqid) const noexcept;

    // FR-009 — enter AwaitingResend transient: set awaiting_resend_=true and
    // populate resend_state_ with [begin_seqno, end_seqno].
    // The caller (Session::on_inbound_frame) emits ResendRequest(2) inline
    // (it has access to seqnum_mgr_ + store_then_emit); this method owns the
    // STATE transition only. [data-model §E-1; spec.md FR-009; plan.md T023]
    [[nodiscard]] asio::awaitable<expected_t<void>>
    enter_awaiting_resend(std::uint32_t begin_seqno,
                          std::uint32_t end_seqno) noexcept;

    // FR-013 / FR-014 — process inbound SequenceReset(4). GapFillFlag=Y → advance
    // next_expected_inbound to NewSeqNo without storing; GapFillFlag=N (forced reset)
    // → advance + emit warning event.
    [[nodiscard]] expected_t<void>
    process_inbound_sequence_reset(std::uint32_t new_seqno,
                                   bool gap_fill_flag) noexcept;

    // FR-010 / FR-011 / FR-012 — reply to inbound ResendRequest(2): walk
    // MessageStore [BeginSeqNo, EndSeqNo] (EndSeqNo=0 → our_last_outbound per D-2);
    // for each application message, emit with PossDupFlag(43)=Y + OrigSendingTime(122);
    // collapse admin spans to SequenceReset-GapFill(4) per D-3; emit GapFill on
    // store-horizon per D-4.
    [[nodiscard]] asio::awaitable<expected_t<void>>
    reply_to_inbound_resend_request(std::uint32_t begin_seqno,
                                    std::uint32_t end_seqno) noexcept;

    // FR-008 / US1 AC5 — initiator-graceful Logout. Emits Logout(5), awaits peer
    // reply for logout_disconnect_timeout_, closes Transport, transitions to
    // Disconnected. Surfaces session_logout_timeout (slot 73, 005-era reused per
    // F1/D1 2026-05-28) if elapsed before peer reply. Symmetric on acceptor side
    // per [[feedback_half_restructure_symmetric_api]].
    [[nodiscard]] asio::awaitable<expected_t<void>>
    drive_logout(std::chrono::milliseconds timeout) noexcept;

    // Accessor — TRANSIENT FLAG, NOT a new fsm_state value per D-1 / [arch §5.6].
    [[nodiscard]] bool is_awaiting_resend() const noexcept;

    // Accessor — current resend state (only valid when is_awaiting_resend()).
    [[nodiscard]] ResendState const& current_resend_state() const noexcept;

    // Exit AwaitingResend: clear the transient flag and reset resend_state_.
    // Called by Session when the gap closes (seqnum_mgr_.next_inbound_unsafe()
    // advances past resend_state_.outstanding_end). [data-model §E-1]
    void exit_awaiting_resend() noexcept;

private:
    // NON-OWNING; owned by SessionConfig::transport_factory_override (shared_ptr).
    // The engine guarantees the factory outlives this FSM per [arch §5.6].
    fixpp::transport::TransportFactory* factory_;
    fixpp::transport::ReconnectPolicy   policy_;
    std::uint32_t                       attempt_index_ = 0;
    std::chrono::seconds                heartbeat_interval_;
    std::chrono::milliseconds           logout_disconnect_timeout_;
    // Timers are optional so ReconnectFsm is constructible without an executor;
    // populated at first use in Phase 3 (drive_reconnect_attempt binds the
    // session executor at call time). [data-model §E-1 Phase 2 shape]
    std::optional<asio::steady_timer>   heartbeat_timer_;
    std::optional<asio::steady_timer>   test_request_timer_;
    std::optional<asio::steady_timer>   logout_timer_;

    // Transient bool on Active state — NOT a new fsm_state value per D-1.
    bool awaiting_resend_ = false;

    // Populated while awaiting_resend_ == true.
    ResendState resend_state_;

    // TestReqID of the most recently emitted outbound TestRequest; nullopt
    // when no TestRequest is outstanding.
    std::optional<std::string> last_outbound_testreqid_;
};

}  // namespace fixpp::session
