// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/reconnect_fsm.cpp
//
// fixpp::session::ReconnectFsm — driver layer on top of the 6-state fsm_state.
// Anchors: FR-001..FR-016, D-1..D-5; [FIX-SL §4.3]/§4.5/§4.6;
// data-model.md §E-1; contracts/reconnect_fsm.hpp.
//
// Phase 3 (013): FR-009 state entry/exit wired (T023/T026). State is owned here;
// the ResendRequest emit logic lives in session.cpp (which calls into these
// methods) because it requires Session's seqnum_mgr_ + store_then_emit
// infrastructure — injecting those would require a 5th ctor parameter
// that breaks test fixtures constructing ReconnectFsm directly with 4 args.
//
// Phase 3 (014) T009: drive_reconnect_attempt realized — bounded retry loop
// per data-model E-1 steps 1,3–6,8 + C1 contract:
//   connect → handshake → success handoff (session_->install_reconnected_transport).
//   Cancellation: enable_total_cancellation + RAII release of in-flight transport.
//   [[feedback_asio_cospawn_total_cancellation_default]].
//
// ABI NOTE: awaiting_resend_ is a TRANSIENT BOOL on Active — NOT a new
// fsm_state value per D-1 / [arch §5.6]. Do NOT extend fsm_state.

#include "fixpp/session/reconnect_fsm.hpp"

#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
// Full TransportFactory definition (forward-declared in the header to keep
// tls/pinset.hpp's std::shared_mutex out of the asio::awaitable closure per
// [const §XV.9]); needed here for factory_->make() + fixpp::tls::SslCtxConfig.
#include "fixpp/transport/transport_factory.hpp"
// cert_source's full definition — needed here to call snap->load_credentials()
// for the leaf SHA-256 fingerprint (T017 rotation-detect step 2) — is obtained
// TRANSITIVELY via transport_factory.hpp above, which includes the tls
// cert_source header for TransportFactory::reload_credentials /
// cert_source_snapshot. Do NOT add a direct include of the tls cert_source
// header here: session->tls is not an allowed module edge
// ([arch §2.3] / tools/check_layers.py); session reaches tls ONLY through the
// transport interface (session->transport->tls). cert_source is still
// forward-declared in reconnect_fsm.hpp per [const §XV.9].
// [data-model §E-3; FR-010]
// TlsTransport — needed for the dynamic_cast<TlsTransport*> in E-1 step 6.
// [data-model §E-1; tls_transport.hpp:61-67]
#include "fixpp/transport/tls_transport.hpp"
// Session — full definition needed to call Session::install_reconnected_transport.
// [data-model §E-1 step 8; const §XV.9: kept out of reconnect_fsm.hpp]
#include "fixpp/session/session.hpp"

namespace fixpp::session {

// ── Constructor ───────────────────────────────────────────────────────────────
//
// Initializes all scalar fields. Timer optionals start empty; populated on
// first async use in Phase 3. [data-model §E-1]
ReconnectFsm::ReconnectFsm(fixpp::transport::TransportFactory* factory,
                           fixpp::transport::ReconnectPolicy policy,
                           std::chrono::seconds heartbeat_interval,
                           std::chrono::milliseconds logout_disconnect_timeout) noexcept
    // Remaining members (attempt_index_, the three timer optionals,
    // awaiting_resend_, resend_state_, last_outbound_testreqid_) take their
    // header NSDMI / default-constructed empty state. [data-model §E-1]
    : factory_{factory},
      policy_{std::move(policy)},
      heartbeat_interval_{heartbeat_interval},
      logout_disconnect_timeout_{logout_disconnect_timeout} {}

// ── drive_reconnect_attempt ───────────────────────────────────────────────────
//
// 014 T009: bounded reconnect loop per data-model E-1 steps 1,3–6,8 + C1.
//
// For each attempt n in [0, max_attempts):
//   (1) await delay_for_attempt(n) — skip for n==0 (first attempt, no delay);
//       honour total-cancel (release and abort).
//   (2) Rotation check (E-3 / US3 T017): snapshot = factory_->cert_source_snapshot().
//       [US3 wiring deferred to T017 — for now just capture snap for ssl_cfg build]
//   (3) Build per-attempt SslCtxConfig ssl_cfg from the snapshot.
//       HOLD ssl_cfg in attempt scope: async_handshake takes it by const&
//       [[clang::lifetimebound]] on TlsTransport::async_handshake's cfg
//       parameter (tls_transport.hpp) — never a temporary.
//   (4) t = factory_->make(exec, ssl_cfg, nullptr); on failure count + continue.
//   (5) t->async_connect(endpoint_); on failure release t, count + continue.
//   (6) dynamic_cast<TlsTransport*>(t.get()) null-check.
//       On null (non-TLS transport) release t, count + continue.
//       tls->async_handshake(ssl_cfg); on failure release t, count + continue.
//       Capture hr : handshake_result.
//   (7) Authorization (E-2 / US2 T014): deferred; permissive for US1.
//   (8) Success: call session_->install_reconnected_transport(move(t), hr)
//       and co_return expected_t<void>{}.
//
// Loop exhaustion → co_return error (session_seqnum_too_high reused as
//   transport_reconnect_limit_exceeded — exact error slot resolved by T026;
//   FR-003 / C1 contract). [data-model §E-1; contracts C1]
//
// Cancellation: co_await asio::this_coro::reset_cancellation_state(
//   asio::enable_total_cancellation()) at the top of the coroutine so that
//   cancellation_type::total (from root_cancel_ at Session::close) propagates
//   through the nested awaits and releases the partially-built transport via
//   RAII (SC-004). [[feedback_asio_cospawn_total_cancellation_default]].
//
// factory_ null guard: if no factory is configured the coroutine co_returns
// an error immediately (non-retried; test fixtures may construct a ReconnectFsm
// without a factory when testing FSM state transitions only).
[[nodiscard]] asio::awaitable<expected_t<void>> ReconnectFsm::drive_reconnect_attempt() noexcept {
    using fixpp::core::error;

    // Enable total-cancellation so cancellation_type::total from the root
    // cancellation signal propagates through nested co_awaits.
    // [[feedback_asio_cospawn_total_cancellation_default]]: co_spawn defaults
    // to terminal-only; we must explicitly reset here. [const §XI.2]
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

    if (factory_ == nullptr) {
        co_return std::unexpected{error::transport_factory_failed};
    }

    auto exec = co_await asio::this_coro::executor;

    const std::uint32_t max_attempts = policy_.max_attempts;
    // max_attempts == 0 means unbounded (QuickFIX-cpp / Fix8 compat mode).
    // [reconnect_policy.hpp: "0 = UNBOUNDED (opt-in only)"]
    for (std::uint32_t n = 0; max_attempts == 0 || n < max_attempts; ++n) {
        // ── Step 1: inter-attempt backoff delay (skip for n==0) ──────────────
        if (n > 0) {
            auto delay = policy_.delay_for_attempt(n);
            if (delay.count() > 0) {
                asio::steady_timer timer{exec};
                timer.expires_after(delay);
                asio::error_code wait_ec;
                co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, wait_ec));

                // #349: reap the state THE SLEEP LEFT BEHIND, before any reset.
                //
                // The reset used to stand above this read, and that ordering
                // discarded a real emission. `wait_ec` only covers the case
                // where the cancellation aborted the wait itself. It does NOT
                // cover a `total` emitted after the timer fired NATURALLY but
                // before this coroutine resumed — a live possibility, since the
                // emitting handler and this resumption are two handlers on the
                // same strand. On that path wait_ec is clear, the reset wiped
                // the emission (it re-constructs cancellation_state from the
                // parent slot with `cancelled_` value-initialised, so an
                // emission that already happened is not replayed — #341), and
                // BOTH this reap and the one below the loop head then observed
                // `none`. The attempt proceeded to connect with the caller's
                // cancellation silently discarded.
                if (auto cs = co_await asio::this_coro::cancellation_state;
                    cs.cancelled() != asio::cancellation_type::none) {
                    co_return std::unexpected{error::transport_connect_cancelled};
                }
                if (wait_ec == asio::error::operation_aborted) {
                    co_return std::unexpected{error::transport_connect_cancelled};
                }

                // Re-enable total cancellation for the attempt below. AFTER the
                // reap, never before it (#349).
                co_await asio::this_coro::reset_cancellation_state(
                    asio::enable_total_cancellation());
            }
        }

        // #349: KEPT, and the condition under which it can fire is narrower
        // than the old comment ("re-check after the backoff sleep, or at
        // attempt 0") claimed — both of those are precisely the cases in which
        // it CANNOT fire. Walk the paths to the nearest preceding SUSPENSION
        // (this_coro awaiters are await_ready()==true and never break the
        // chain):
        //   n == 0                : nothing has suspended since the reset at
        //                           the top of the coroutine  -> dead.
        //   n > 0, delay >  0     : the backoff block above ends in a reset
        //                           -> dead.
        //   n > 0, delay == 0     : the backoff block is SKIPPED entirely, so
        //                           the nearest suspension is the PREVIOUS
        //                           iteration's async_connect/async_handshake
        //                           and no reset intervenes -> LIVE.
        // A zero-length backoff is reachable, not hypothetical:
        // ReconnectPolicy::delay_for_attempt returns 0 for an empty schedule,
        // and session_config.hpp records a shipped configuration that had one.
        // That single live path is why this reap is not deleted alongside the
        // dead ones -- do not "tidy" it away by analogy with #341.
        if (auto cs = co_await asio::this_coro::cancellation_state;
            cs.cancelled() != asio::cancellation_type::none) {
            co_return std::unexpected{error::transport_connect_cancelled};
        }

        // ── Step 2: cert_source snapshot + rotation detection (E-3 / T017) ────
        // Read the current cert_source through the abstract factory pointer.
        // cert_source_snapshot() is now a pure-virtual on the abstract base (C4).
        //
        // Rotation detection logic (data-model §E-3; contracts C3; FR-009/010/011):
        //   - If last_active_source_ == nullptr (first-ever load): set baseline
        //     (load fingerprint + store source), NO emit.  FR-009 SPEC-FIXED rule.
        //   - If snap != last_active_source_ (rotation staged): compute new_fp
        //     from snap->load_credentials() leaf SHA-256; invoke the strand-bound
        //     emit callback with {old=last_active_fp_, new=new_fp} BEFORE make();
        //     update both members.  No-op rotation (new_fp == old_fp) still emits
        //     (FR-011 — not fingerprint-gated).
        //
        // Computing the fingerprint: co_await snap->load_credentials() to get
        // the local_credentials bundle; leaf.sha256() is the pre-computed SHA-256
        // of the raw DER bytes.  On load failure the fingerprint stays all-zero
        // (degenerate but non-fatal: the attempt will fail at async_handshake
        // regardless since the cert_source is broken).
        //
        // §XV.9 safety: cert_source.hpp is included only in this .cpp, not the
        // header. The full type is needed here for the virtual load_credentials().
        auto snap = factory_->cert_source_snapshot();

        // FR-013a: load credentials here ONLY when a fingerprint is actually
        // needed — the first-ever load (baseline) or a detected rotation. A
        // no-rotation reconnect (snap == last_active_source_) must NOT load,
        // else load_credentials() would run twice per handshake (this
        // rotation-detect + the handshake itself at transport_factory.cpp:378),
        // breaking the FR-013a "load_credentials() == 1 per handshake" witness.
        const bool first_load = (last_active_source_ == nullptr);
        const bool rotated = !first_load && (snap != last_active_source_);

        if (first_load || rotated) {
            // Compute the SHA-256 fingerprint of snap's leaf via load_credentials()
            // — an awaitable call on the coroutine stack.
            std::array<std::byte, 32> new_fp{};
            if (snap) {
                auto creds_r = co_await snap->load_credentials();
                if (creds_r.has_value()) {
                    new_fp = creds_r->leaf.sha256();
                }
            }

            if (rotated && emit_credentials_rotated_) {
                // ── Rotation detected: emit BEFORE make() (FR-009) ───────────
                // Even if new_fp == last_active_fp_ (no-op rotation), emit
                // (FR-011). The callback is strand-bound and set by
                // Session::open() (T018); the standalone-FSM test path injects
                // it directly. old=last_active_fp_ is read before the update.
                //
                // 038 T011 (G2): wrap in try/catch — best-effort notification.
                // Production path: Session::open() injects a noexcept lambda
                // (→ noexcept emit_event ring-buffer write), so throw is
                // unreachable in production. This hardens the standalone-FSM
                // injection seam so a throwing test callback does not propagate
                // out of the noexcept coroutine body.
                // Mirror: CompIdAuthorizationPolicy::authorize_logon try/catch
                // shape (compid_authorization_policy.cpp:355-361).
                // The catch MUST fall through: baseline update and make() still
                // run (the attempt proceeds to its policy outcome; INV-7).
                // [038 G2; FR-006/FR-007; INV-7/8/9; 038 contracts C-2]
                try {
                    emit_credentials_rotated_(session_event_credentials_rotated{
                        .old_sha256 = last_active_fp_,
                        .new_sha256 = new_fp,
                    });
                } catch (...) {
                    // Contain — best-effort rotation notification.
                    // Fall through to baseline update + make() step.
                }
            }
            // First load sets the baseline (NO event, FR-009 SPEC-FIXED);
            // rotation updates AFTER the emit above.
            last_active_source_ = snap;
            last_active_fp_ = new_fp;
        }
        // else: snap == last_active_source_ → no rotation, no load, no emit.

        // ── Step 3: build per-attempt SslCtxConfig (held in attempt scope) ──
        // ssl_cfg is held across both make() and async_handshake() — the arg
        // to async_handshake is const& [[clang::lifetimebound]]; MUST NOT pass
        // a temporary (TlsTransport::async_handshake's cfg parameter, tls_transport.hpp).
        // tls_profile_ is set by Session::open() via set_tls_profile(); without
        // it async_handshake rejects the attempt with transport_psk_unsupported.
        fixpp::tls::SslCtxConfig ssl_cfg{};
        ssl_cfg.profile = tls_profile_;
        if (snap) {
            ssl_cfg.cs = snap;  // bind the cert_source for this attempt
        }

        // ── Step 4: factory_->make ────────────────────────────────────────────
        auto make_result = factory_->make(exec, ssl_cfg, nullptr);
        if (!make_result) {
            // make() failure counts as one attempt; retry per policy.
            continue;
        }
        auto t = std::move(*make_result);

        // ── Step 5: async_connect ─────────────────────────────────────────────
        auto connect_result = co_await t->async_connect(endpoint_);
        if (!connect_result) {
            // Release t (RAII) and count the attempt.
            continue;
        }

        // ── Step 6 (plaintext fast-path): skip handshake entirely (043 T011) ──
        // When is_plaintext_ the FSM proceeds connect → Logon with no handshake
        // and no authorization (D-7/D-10). A default handshake_result{} is
        // passed so install_reconnected_transport receives a well-typed argument;
        // live_peer_id_ is guarded in Session::install_reconnected_transport (T013)
        // and stays nullopt for insecure_plain_tcp (D-10 #2 MUST).
        // This early-return leaves steps 6/7/8 byte-identical for non-plaintext
        // profiles — the fail-closed null-cast path (non-TLS transport on a TLS
        // profile is still a bug) is provably unchanged. [data-model §E-5; D-7]
        if (is_plaintext_) {
            if (session_ != nullptr) {
                session_->install_reconnected_transport(std::move(t),
                                                        fixpp::transport::handshake_result{});
            }
            co_return expected_t<void>{};
        }

        // ── Step 6: dynamic_cast to TlsTransport + async_handshake ────────────
        // TlsTransport inherits virtually from Transport — static_cast down that
        // edge is ill-formed; dynamic_cast is required (E-1 / C1 / tls_transport.hpp:61-67).
        auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(t.get());
        if (tls == nullptr) {
            // Non-TLS transport (or cast fail): release t, count, continue.
            continue;
        }

        // ssl_cfg is still in scope — required by [[clang::lifetimebound]]
        // on TlsTransport::async_handshake's cfg parameter (tls_transport.hpp).
        auto handshake_result_r = co_await tls->async_handshake(ssl_cfg);
        if (!handshake_result_r) {
            // Handshake failure: release t, count, continue.
            continue;
        }
        auto hr = std::move(*handshake_result_r);

        // ── Step 7: Authorization (US2 T014) ─────────────────────────────────
        // Run CompIdAuthorizationPolicy::authorize(hr.peer_id, ...) via the
        // session's config. On fail-closed: emit the inherited 013 event shape
        // (session_event_compid_authorization_failed + session_compid_unauthorized),
        // release t (RAII), count the attempt, and continue (retry-to-cap).
        //
        // Reconnect-path disposition (E-2 / C2 / Q1):
        //   - Auth failure counts as exactly ONE attempt and is retried per the
        //     backoff schedule to the cap (reason-agnostic per Clarifications Q1).
        //   - This is NOT a terminal Disconnected (unlike 013's open-Logon path
        //     at session.cpp:1004-1005 / :1799-1800); only loop-exhaustion is.
        //   - Only when session_ != nullptr (session-bound FSM path).
        //
        // [data-model §E-1 step 7; E-2; contracts C2; FR-006; FR-007; Q1]
        if (session_ != nullptr) {
            const auto& cfg = session_->cfg_;
            const bool is_mtls =
                cfg.security_profile.k == fixpp::session::SecurityProfile::kind::mtls_ca ||
                cfg.security_profile.k == fixpp::session::SecurityProfile::kind::mtls_pinned;

            if (is_mtls) {
                const std::string_view asserted_compid = cfg.target_comp_id;
                auto auth_r =
                    cfg.compid_authorization_policy.authorize(hr.peer_id, asserted_compid);
                if (!auth_r) {
                    // Authorization failed: emit event, release t, count attempt.
                    // cn is left EMPTY: emit_event PERSISTS the event into the
                    // session-lifetime recent_events_ ring (session.cpp:142), so a
                    // view into hr.peer_id (a coroutine-stack temporary destroyed at
                    // the `continue` below) would dangle for later recent_events()
                    // readers. Mirrors the mTLS-no-identity arm (session.cpp:1939).
                    session_->emit_event(fixpp::session::session_event_compid_authorization_failed{
                        .cn = {},
                        .asserted_compid = asserted_compid,
                        .expected_compids = {},
                        .principal_source = fixpp::session::bound_principal::source::CN,
                    });
                    // t is released here (RAII unique_ptr); count attempt,
                    // continue to next iteration (retry-to-cap, NOT terminal).
                    continue;
                }
                // Authorization succeeded: fall through to step 8.
                // (bound_principal is not stored on the reconnect path; T015
                // wires it into the Session's Logon-ack guard via live_peer_id_.)
            }
            // Non-mTLS: gate skipped (permissive), fall through to step 8.
        }

        // ── Step 8: Success handoff → Session::install_reconnected_transport ──
        if (session_ != nullptr) {
            session_->install_reconnected_transport(std::move(t), std::move(hr));
        }
        // (If session_ is null the transport is simply dropped — test-only path
        // where ReconnectFsm is tested standalone without a Session owner.)

        co_return expected_t<void>{};
    }

    // Loop exhausted at max_attempts — surface the terminal limit-exceeded error.
    // The owning Session's driver (015) effects the Disconnected state transition;
    // drive_reconnect_attempt only co_returns the error.
    // [FR-003; C1; data-model §E-1]
    co_return std::unexpected{error::transport_reconnect_limit_exceeded};
}

// ── run_heartbeat_cadence ─────────────────────────────────────────────────────
//
// FR-003 / FR-005: deferred to Phase 4 (heartbeat timer arm + outbound idle).
// Liveness logic lives in session.cpp run_liveness_loop() for Phase 3.
[[nodiscard]] asio::awaitable<expected_t<void>> ReconnectFsm::run_heartbeat_cadence() noexcept {
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
[[nodiscard]] expected_t<void> ReconnectFsm::validate_inbound_heartbeat_testreqid(
    std::string_view /*inbound_testreqid*/) const noexcept {
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
[[nodiscard]] asio::awaitable<expected_t<void>> ReconnectFsm::enter_awaiting_resend(
    std::uint32_t begin_seqno, std::uint32_t end_seqno) noexcept {
    awaiting_resend_ = true;
    resend_state_.outstanding_begin = begin_seqno;
    resend_state_.outstanding_end = end_seqno;
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
[[nodiscard]] expected_t<void> ReconnectFsm::process_inbound_sequence_reset(
    std::uint32_t /*new_seqno*/, bool /*gap_fill_flag*/) noexcept {
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
[[nodiscard]] asio::awaitable<expected_t<void>> ReconnectFsm::reply_to_inbound_resend_request(
    std::uint32_t /*begin_seqno*/, std::uint32_t /*end_seqno*/) noexcept {
    co_return expected_t<void>{};
}

// ── drive_logout ──────────────────────────────────────────────────────────────
//
// FR-008: emit Logout(5), arm timer, await peer reply. Phase 3 stub — Logout
// logic is inline in session.cpp run_logout_phase1 / on_inbound_frame.
[[nodiscard]] asio::awaitable<expected_t<void>> ReconnectFsm::drive_logout(
    std::chrono::milliseconds /*timeout*/) noexcept {
    co_return expected_t<void>{};
}

// ── Accessors ─────────────────────────────────────────────────────────────────

[[nodiscard]] bool ReconnectFsm::is_awaiting_resend() const noexcept { return awaiting_resend_; }

[[nodiscard]] ResendState const& ReconnectFsm::current_resend_state() const noexcept {
    return resend_state_;
}

}  // namespace fixpp::session
