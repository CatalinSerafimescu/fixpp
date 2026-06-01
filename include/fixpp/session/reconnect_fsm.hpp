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
// cert_source snapshot discipline (014 T009): the FSM reads
// factory_->cert_source_snapshot() at each drive_reconnect_attempt attempt entry
// (now a pure-virtual on the abstract TransportFactory base per C4 T003/T009).
// Returns a strong-ref std::shared_ptr<cert_source> BY VALUE COPY.
// NEVER captures a raw cert_source* or a weak_ptr<cert_source>
// — per [[feedback_weak_ptr_cache_needs_owning_context]].
//
// Live-transport handoff (014 T009/T010): on a successful attempt the FSM calls
// Session::install_reconnected_transport(unique_ptr<Transport>, handshake_result,
// bound_principal) via the session_ back-pointer (set by Session::open() via
// set_session_owner()). Session is forward-declared here; the call happens in
// reconnect_fsm.cpp which includes the full session.hpp. The heavy
// handshake_result / bound_principal types never appear in this header's
// public signatures, preserving [const §XV.9].
#pragma once

#include <array>
#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/steady_timer.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "fixpp/core/error.hpp"  // error enum + expected_t<T>
#include "fixpp/session/resend_state.hpp"
#include "fixpp/session/session_fsm.hpp"
#include "fixpp/transport/reconnect_policy.hpp"

// fixpp::tls::SecurityProfile — forward-declare as a uint8_t-backed scoped enum
// so the FSM can store the TLS profile without including tls/security_profile.hpp
// → tls/pinset.hpp → shared_mutex (§XV.9 guard). C++ allows forward-declaring
// scoped enums with explicit underlying type. The full definition is available
// in reconnect_fsm.cpp via transport_factory.hpp (which includes security_profile.hpp).
//
// fixpp::tls::cert_source — forward-declare so the FSM can hold a
// std::shared_ptr<cert_source> member (last_active_source_) without pulling in
// cert_source.hpp → (transitively) into the asio::awaitable closure.
// A shared_ptr<T> data member requires only T to be forward-declared; the full
// definition is available in reconnect_fsm.cpp via cert_source.hpp (included
// alongside transport_factory.hpp). [data-model §E-3; I-7;
// [[feedback_weak_ptr_cache_needs_owning_context]]; §XV.9]
namespace fixpp::tls {
enum class SecurityProfile : std::uint8_t;
class cert_source;
}  // namespace fixpp::tls

// TransportFactory is used only as a non-owning raw pointer in this header (ctor
// arg + factory_ member); the full definition is needed only in reconnect_fsm.cpp
// (factory_->make() + SslCtxConfig). Forward-declare here rather than #include
// transport_factory.hpp: that header transitively pulls tls/pinset.hpp's
// std::shared_mutex, and this header includes <asio/awaitable.hpp> — so the
// include would drag a std::mutex type into the asio::awaitable closure of every
// consumer (session.hpp), violating [const §XV.9] / [2f §6.6] (caught by the
// check_no_std_mutex_corpus Tier-1 gate).
namespace fixpp::transport {
class TransportFactory;
}
// Endpoint is a lightweight value type (std::string + uint16_t + uint32_t);
// include the header directly (it has no heavy transitive includes).
#include "fixpp/transport/endpoint.hpp"
// Session* back-pointer — forward-declared to avoid circular include
// (session.hpp includes reconnect_fsm.hpp). The actual call to
// Session::install_reconnected_transport() is in reconnect_fsm.cpp which
// includes the full session.hpp definition. [data-model §E-1 step 8]
namespace fixpp::session {
class Session;
}

// session_event.hpp defines session_event_credentials_rotated (the payload of
// the emit callback).  It does NOT transitively include any mutex type, so
// including it here is safe for the §XV.9 awaitable-closure check.
// Verified: session_event.hpp → compid_authorization_policy.hpp → peer_identity.hpp
// (no shared_mutex / mutex anywhere in that chain).
// [data-model §E-3; §XV.9 safety-verified]
#include "fixpp/session/session_event.hpp"

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
                 fixpp::transport::ReconnectPolicy policy, std::chrono::seconds heartbeat_interval,
                 std::chrono::milliseconds logout_disconnect_timeout) noexcept;

    // 014 T009 — Set the peer endpoint for reconnect attempts.
    // Called by Session::open() before the first drive_reconnect_attempt.
    // [data-model §E-1 step 5 — async_connect(ep)]
    void set_reconnect_endpoint(fixpp::transport::Endpoint ep) noexcept {
        endpoint_ = std::move(ep);
    }

    // 014 T009/T010 — Set the owning Session back-pointer.
    // Called by Session::open() so drive_reconnect_attempt can call
    // Session::install_reconnected_transport() on success.
    // Session is forward-declared; the call happens in reconnect_fsm.cpp
    // which includes the full session.hpp. [data-model §E-1 step 8]
    void set_session_owner(Session* session) noexcept { session_ = session; }

    // 014 T009 — Set the TLS security profile for per-attempt SslCtxConfig
    // construction. Called by Session::open() from cfg_.security_profile's TLS
    // profile (mapped from the session-layer SecurityProfile to the TLS-layer
    // SecurityProfile). Without this the FSM would build ssl_cfg with
    // SecurityProfile::unset, causing async_handshake to reject the attempt.
    // The type is forward-declared (uint8_t-backed enum) to avoid pulling
    // tls/security_profile.hpp → tls/pinset.hpp → shared_mutex into this
    // awaitable-corpus header. [data-model §E-1 step 3; §XV.9]
    void set_tls_profile(fixpp::tls::SecurityProfile profile) noexcept { tls_profile_ = profile; }

    // 014 T017/T018 — Inject the strand-bound credential-rotation emit callback.
    // Called by Session::open() (T018) to wire the on-strand emit of
    // SessionEvent::credentials_rotated.  The FSM invokes this callback at step 2
    // of drive_reconnect_attempt when a rotation is detected, BEFORE make().
    // [data-model §E-3; contracts C3; FR-009; §XI.4]
    //
    // When no callback is set (nullptr or default-constructed function):
    //   the emit is silently skipped (test-only path where FSM runs standalone
    //   without a Session owner and the caller will inject the callback manually).
    void set_emit_credentials_rotated(
        std::function<void(session_event_credentials_rotated)> cb) noexcept {
        emit_credentials_rotated_ = std::move(cb);
    }

    // FR-001 / FR-002 / FR-003 / FR-004 — bounded reconnect loop (014 T009).
    // Per attempt (0..max_attempts-1):
    //   (1) await delay_for_attempt(n) (skip for n==0), honouring total-cancel;
    //   (2) read cert_source snapshot for rotation detection (E-3, US3);
    //   (3) build per-attempt SslCtxConfig ssl_cfg from the snapshot — held in
    //       attempt scope across both make() and async_handshake() (the arg is
    //       const& [[clang::lifetimebound]] at tls_transport.hpp:116-118);
    //   (4) factory_->make(exec, ssl_cfg, mr) → on failure count attempt, continue;
    //   (5) t->async_connect(endpoint_) → on failure release t, count, continue;
    //   (6) dynamic_cast<TlsTransport*>(t.get()) null-check → on null count, continue;
    //       tls->async_handshake(ssl_cfg) → on failure release t, count, continue;
    //   (7) (US2 T014) authorize via handshake_result.peer_id;
    //   (8) on success call session_->install_reconnected_transport(…) (T010)
    //       and co_return expected_t<void>{}.
    // At loop exhaustion → co_return error (transport_reconnect_limit_exceeded).
    // Cancellation: enable_total_cancellation() resets the co_await state;
    //   total-cancel → abort in-flight attempt + RAII-release t.
    //   [[feedback_asio_cospawn_total_cancellation_default]]
    [[nodiscard]] asio::awaitable<expected_t<void>> drive_reconnect_attempt() noexcept;

    // FR-003 / FR-005 — emit Heartbeat(0) outbound when no outbound traffic has
    // been sent for HeartBtInt seconds. Armed on Active entry; rearmed on every
    // outbound; cancelled on Disconnected entry.
    [[nodiscard]] asio::awaitable<expected_t<void>> run_heartbeat_cadence() noexcept;

    // FR-004 / FR-007 — emit TestRequest(1) when no inbound traffic for
    // 1.2×HeartBtInt; if no inbound within 2×HeartBtInt total, Logout(5) and
    // disconnect with session_test_request_unanswered (slot 74, 005-era reused
    // per F1/D1 2026-05-28).
    [[nodiscard]] asio::awaitable<expected_t<void>> run_inbound_liveness_watch() noexcept;

    // FR-006 — validate inbound Heartbeat's TestReqID(112) matches the most
    // recent outbound TestRequest's TestReqID. Mismatch → session_testreqid_mismatch
    // (slot 118). [data-model §E-1]
    [[nodiscard]] expected_t<void> validate_inbound_heartbeat_testreqid(
        std::string_view inbound_testreqid) const noexcept;

    // FR-009 — enter AwaitingResend transient: set awaiting_resend_=true and
    // populate resend_state_ with [begin_seqno, end_seqno].
    // The caller (Session::on_inbound_frame) emits ResendRequest(2) inline
    // (it has access to seqnum_mgr_ + store_then_emit); this method owns the
    // STATE transition only. [data-model §E-1; spec.md FR-009; plan.md T023]
    [[nodiscard]] asio::awaitable<expected_t<void>> enter_awaiting_resend(
        std::uint32_t begin_seqno, std::uint32_t end_seqno) noexcept;

    // FR-013 / FR-014 — process inbound SequenceReset(4). GapFillFlag=Y → advance
    // next_expected_inbound to NewSeqNo without storing; GapFillFlag=N (forced reset)
    // → advance + emit warning event.
    [[nodiscard]] expected_t<void> process_inbound_sequence_reset(std::uint32_t new_seqno,
                                                                  bool gap_fill_flag) noexcept;

    // FR-010 / FR-011 / FR-012 — reply to inbound ResendRequest(2): walk
    // MessageStore [BeginSeqNo, EndSeqNo] (EndSeqNo=0 → our_last_outbound per D-2);
    // for each application message, emit with PossDupFlag(43)=Y + OrigSendingTime(122);
    // collapse admin spans to SequenceReset-GapFill(4) per D-3; emit GapFill on
    // store-horizon per D-4.
    [[nodiscard]] asio::awaitable<expected_t<void>> reply_to_inbound_resend_request(
        std::uint32_t begin_seqno, std::uint32_t end_seqno) noexcept;

    // FR-008 / US1 AC5 — initiator-graceful Logout. Emits Logout(5), awaits peer
    // reply for logout_disconnect_timeout_, closes Transport, transitions to
    // Disconnected. Surfaces session_logout_timeout (slot 73, 005-era reused per
    // F1/D1 2026-05-28) if elapsed before peer reply. Symmetric on acceptor side
    // per [[feedback_half_restructure_symmetric_api]].
    [[nodiscard]] asio::awaitable<expected_t<void>> drive_logout(
        std::chrono::milliseconds timeout) noexcept;

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
    fixpp::transport::ReconnectPolicy policy_;
    std::uint32_t attempt_index_ = 0;
    std::chrono::seconds heartbeat_interval_;
    std::chrono::milliseconds logout_disconnect_timeout_;

    // 014 T009 — peer endpoint for async_connect (set by Session::open via
    // set_reconnect_endpoint before drive_reconnect_attempt is first called).
    fixpp::transport::Endpoint endpoint_{};

    // 014 T009/T010 — NON-OWNING back-pointer to the owning Session (set by
    // Session::open via set_session_owner). Used to call the private
    // Session::install_reconnected_transport() on success. Forward-declared
    // above; full definition in reconnect_fsm.cpp (which includes session.hpp).
    Session* session_ = nullptr;

    // 014 T009 — TLS security profile for per-attempt SslCtxConfig construction.
    // Set by Session::open() via set_tls_profile(). Default = unset (enum value 0);
    // must be set before drive_reconnect_attempt is called on a live TLS path.
    // Forward-declared as uint8_t-backed enum above; full definition via
    // transport_factory.hpp in reconnect_fsm.cpp. [data-model §E-1 step 3; §XV.9]
    fixpp::tls::SecurityProfile tls_profile_{};
    // Timers are optional so ReconnectFsm is constructible without an executor;
    // populated at first use in Phase 3 (drive_reconnect_attempt binds the
    // session executor at call time). [data-model §E-1 Phase 2 shape]
    std::optional<asio::steady_timer> heartbeat_timer_;
    std::optional<asio::steady_timer> test_request_timer_;
    std::optional<asio::steady_timer> logout_timer_;

    // Transient bool on Active state — NOT a new fsm_state value per D-1.
    bool awaiting_resend_ = false;

    // Populated while awaiting_resend_ == true.
    ResendState resend_state_;

    // TestReqID of the most recently emitted outbound TestRequest; nullopt
    // when no TestRequest is outstanding.
    std::optional<std::string> last_outbound_testreqid_;

    // 014 T017 — Credential-rotation detect state (data-model §E-3 / contracts C3).
    //
    // last_active_source_ — STRONG-REF owning the previously-active cert_source.
    // nullptr on construction (no prior active source).  Set to the snapshot at
    // step 2 on every drive_reconnect_attempt entry.
    //
    // INVARIANT (I-7 / [[feedback_weak_ptr_cache_needs_owning_context]]): this is a
    // std::shared_ptr (strong ref), NOT a weak_ptr.  A weak_ptr with no other owner
    // would expire after every attempt, re-firing the rotation event on every
    // reconnect and making a no-cache.  The strong ref keeps the previous cert_source
    // alive until the NEXT rotation so old_sha256 remains computable.
    //
    // cert_source is forward-declared above; the full definition is in
    // reconnect_fsm.cpp (via cert_source.hpp).  A shared_ptr data member only
    // needs the forward declaration for the pointer itself; ~shared_ptr needs
    // the complete type — ensured by the explicit destructor in .cpp. [§XV.9]
    std::shared_ptr<fixpp::tls::cert_source> last_active_source_{};

    // last_active_fp_ — SHA-256 of last_active_source_'s leaf DER.
    // All-zero while last_active_source_ == nullptr (first-load state).
    // Updated in lockstep with last_active_source_ at each rotation detection.
    // [data-model §E-3; FR-010]
    std::array<std::byte, 32> last_active_fp_{};

    // 014 T017/T018 — Strand-bound emit callback injected by Session::open().
    // Invoked at drive_reconnect_attempt step 2 (before make()) when a rotation
    // is detected (snap != last_active_source_).
    // The Session lambda calls Session::emit_event() to land the event on the
    // session strand per §XI.4.
    // Default-constructed (empty function): emit is silently skipped (standalone
    // FSM test path where the caller injects the callback via set_emit_credentials_rotated).
    // [data-model §E-3; contracts C3; FR-009; §XI.4]
    std::function<void(session_event_credentials_rotated)> emit_credentials_rotated_{};
};

}  // namespace fixpp::session
