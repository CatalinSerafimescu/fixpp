// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/session_config.hpp
//
// fixpp::session::SessionConfig — value-typed, FROZEN at Session::open
// ([arch §5.6] — close-and-reopen only; I-13). The executor / clock /
// dictionary axes follow ONE uniform pattern:
//     resolved = override.value_or(engine_anchor)
// Namespace-scoped threading_mode / lock_policy enums + the CLOSED, 2-value
// nested SessionConfig::backpressure_mode (drop_oldest UNREPRESENTABLE on the
// app/session message path — [const §XV.15] / [2d §6.4] / I-14). The frozen
// field implementers switch on is SessionConfig::app_backpressure.
// [2d §4.5]. Realizes specs/007-threading-clock/contracts/session_config.hpp.
//
// NO close_timeout field (D-9): the close-timeout VALUE lives in the
// session-module Phase-4 spec (005), not 2d's frozen config shape; 2d wires
// only the timeout mechanism ([2d §4.7]:864 / [2d §6.7]:1207).
#pragma once

#include <asio/any_io_executor.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/trace_context.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>  // value-typed member ⇒ complete type (013 T011)
#include <fixpp/session/message_store_factory.hpp>  // shared_ptr member ⇒ complete type (FR-001a)
#include <fixpp/session/security_profile.hpp>       // value-typed member ⇒ complete type
#include <fixpp/tap/tap_consumer.hpp>               // value-typed member ⇒ complete type
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <type_traits>

namespace fixpp::core {
class Clock;
}
namespace fixpp::dict {
class Dictionary;
class DialectOverlay;
}  // namespace fixpp::dict
namespace fixpp::tls {
class cert_source;
}
namespace fixpp::log {
class Logger;  // 017 T012: logger_override field ([2d §4.5]; replaces log_sink_override)
}
namespace fixpp::otel {
class TracerProvider;  // 017 T012: tracer_override field ([2d §4.5])
}
// Endpoint is a value type used by reconnect_endpoint field; needs full def.
// [const §XV.9] check: endpoint.hpp only includes <cstdint>/<string>/<utility>
// — no shared_mutex or awaitable chains. Safe to include here.
#include <fixpp/transport/endpoint.hpp>
// ReconnectPolicy is an optional value-typed member (016 T008) ⇒ needs the
// complete type. [const §XV.9] check: reconnect_policy.hpp includes only
// <chrono>/<cstdint>/<memory_resource>/<vector> — no shared_mutex / awaitable.
#include <fixpp/transport/reconnect_policy.hpp>
// 026 T003 — fix_time_precision for sending_time_precision field below.
// [const §XV.9] verified safe in Gate A (New-5): fix_time.hpp is mutex-free
// chrono; it pulls no std::mutex into the session.hpp awaitable closure.
#include <fixpp/core/fix_time.hpp>

namespace fixpp::transport {
class TransportFactory;  // [2d §4.5] forward decl per [2h App D §D.2] sign-off; the actual
                         // SessionConfig::transport_factory_override field wiring lands in
                         // the post-012 session-Phase-4 spec (this is reachability only).
}

namespace fixpp::session {

enum class RejectPolicy : std::uint8_t;  // owned by 005; declared for the field

// FR-017 / D-6 / Clarifications Q1=A — per-session ResetSeqNumFlag(141)=Y
// policy. Default = bilateral_strict per [const §XII.5] no-implicit-default +
// 2-of-3 industry convergence (QFC + QFJ favour bilateral; Fix8 unilateral is
// the outlier). Three modes:
//   - bilateral_strict  — refuse Logon if peer's response lacks 141=Y (when we
//                         sent 141=Y); QFJ-style.
//   - bilateral_lenient — auto-mirror 141=Y in our response Logon when peer
//                         sends 141=Y; QFC-style.
//   - unilateral        — honour any received 141=Y regardless of our outbound
//                         flag; Fix8-style.
// Per-session granularity (not engine-wide) so multi-tenant acceptors can pair
// counterparties running different engines. [data-model §E-4]
enum class reset_seqnum_policy : std::uint8_t {
    bilateral_strict = 0,
    bilateral_lenient = 1,
    unilateral = 2,
};

enum class threading_mode : std::uint8_t {
    per_session_strand = 0,  // default — make_strand wrap; callbacks serialised
    direct_executor = 1,     // expert opt-out — needs already_serialized_executor
};

enum class lock_policy : std::uint8_t {
    mutex = 0,  // default
    spin = 1,   // opt-in; store-write path always mutex ([const §XI.5])
};

// Selects the role for a Session at construction time; drives Session::open()
// initial-state choice (specs/009-session-fsm-finalize/contracts/session_role.hpp).
// - initiator: open() sets fsm_state_ = LogonSent + emits initial Logon.
// - acceptor:  open() sets fsm_state_ = NotConnected + waits for peer Logon.
enum class session_role : std::uint8_t {
    initiator = 0,
    acceptor = 1,
};

// Portable "closed enum" attribute (no-op where unsupported). Placed after
// the enum name per the Clang spelling; a static_assert at every switch site
// (T048) enumerates exactly the 2 values and a runtime out-of-range cast is
// rejected with error::invalid_session_config (seam 13).
#if defined(__clang__) && defined(__has_attribute)
#if __has_attribute(enum_extensibility)
#define FIXPP_ENUM_CLOSED __attribute__((enum_extensibility(closed)))
#endif
#endif
#ifndef FIXPP_ENUM_CLOSED
#define FIXPP_ENUM_CLOSED
#endif

// Compile-time exhaustiveness guard for switch sites over backpressure_mode.
// Place FIXPP_ASSERT_BACKPRESSURE_SWITCH_EXHAUSTIVE(T) immediately before any
// switch(backpressure_mode), enumeration BLOCK_VAL and DISCONNECT_VAL below.
// The static_assert fires if the underlying integer range of T ever grows
// beyond the 2 legal values (block=0, disconnect_and_recover=1). Dropping
// drop_oldest from the enum is intentional per [const §XV.15] / [2d §6.4] /
// I-14; this macro is the compile-time enforcement companion.
//
// Usage pattern (at every switch site):
//   FIXPP_ASSERT_BACKPRESSURE_SWITCH_EXHAUSTIVE(SessionConfig::backpressure_mode);
//   switch (cfg.app_backpressure) {
//       case SessionConfig::backpressure_mode::block:              ...
//       case SessionConfig::backpressure_mode::disconnect_and_recover: ...
//   }
#define FIXPP_ASSERT_BACKPRESSURE_SWITCH_EXHAUSTIVE(T)                                   \
    static_assert(static_cast<std::uint8_t>(T::block) == 0 &&                            \
                      static_cast<std::uint8_t>(T::disconnect_and_recover) == 1,         \
                  "backpressure_mode must be closed: exactly {block=0, "                 \
                  "disconnect_and_recover=1}. drop_oldest is BANNED on the app/session " \
                  "path ([const §XV.15] / [2d §6.4] / I-14). Extend this list if "       \
                  "the enum changes and update ALL switch sites.")

// Value-typed; FROZEN at Session::open ([arch §5.6] — close-and-reopen only).
struct SessionConfig {
    enum class FIXPP_ENUM_CLOSED backpressure_mode : std::uint8_t {
        block = 0,                   // push back to producer (default)
        disconnect_and_recover = 1,  // terminate session; ResendRequest on reconnect
    };

    std::optional<asio::any_io_executor> executor_override;
    threading_mode mode = threading_mode::per_session_strand;
    lock_policy locks = lock_policy::mutex;
    bool already_serialized_executor = false;            // MUST be true when mode==direct_executor
    std::shared_ptr<fixpp::core::Clock> clock_override;  // null → EngineConfig::clock

    std::string sender_comp_id;  // identity owned by 005
    std::string target_comp_id;
    std::string begin_string;

    session_role role = session_role::initiator;  // FR-004; default preserves 005 behavior

    std::shared_ptr<MessageStoreFactory>
        store_factory;  // FR-001a — shared ownership (was unique_ptr pre-010); stateless factory
                        // may be shared across Sessions, each calling make() to mint its own
                        // MessageStore (per-Session uniqueness invariant preserved)
    std::shared_ptr<fixpp::tls::cert_source> cert_source;
    fixpp::session::SecurityProfile
        security_profile;  // no-implicit-default (N-P2-3); kind::unset → Session::open() rejects
                           // (FR-018; lives in `session` per [arch §6 line 243])

    std::shared_ptr<const fixpp::dict::Dictionary> dictionary;           // required
    std::shared_ptr<const fixpp::dict::DialectOverlay> dialect_overlay;  // optional

    std::optional<std::chrono::seconds> heartbeat_interval;           // value owned by 005
    std::optional<std::chrono::milliseconds> test_request_threshold;  // value owned by 005
    std::optional<std::chrono::milliseconds> sending_time_threshold;  // value owned by 005
    RejectPolicy reject_policy{};                                     // owned by 005

    std::pmr::memory_resource* message_arena = nullptr;       // null → engine default
    std::pmr::memory_resource* framer_carry_arena = nullptr;  // owned by 2b; recorded here
    std::pmr::memory_resource* session_arena = nullptr;

    fixpp::otel::trace_context initial_trace_context{};  // value-typed (C-P2-4)

    // 017 T012 — per-session logger override (engine-anchor + session-override
    // per [2d §4.5]). null → Engine resolves its own EngineConfig::logger.
    // Replaces the former log_sink_override (a single Sink); logger_override
    // supplies a whole Logger instance so the session can enqueue records to
    // a custom ring. [2k §4.5 / contracts/adjacent-amendments.md item 3/4]
    std::shared_ptr<fixpp::log::Logger> logger_override;  // null → engine default

    // 017 T012 — per-session tracer override. null → Engine resolves its own
    // EngineConfig::tracer. meter_override intentionally omitted — metrics are
    // engine-scoped (anchor §4.8). [2k / contracts/adjacent-amendments.md item 3]
    std::shared_ptr<fixpp::otel::TracerProvider> tracer_override;  // null → engine default

    fixpp::tap::TapConsumer tap_consumer;  // default = no tap

    backpressure_mode app_backpressure = backpressure_mode::block;

    // Out-of-band test/transport sink (US4 / T046 / seam #11).
    // Called with the committed outbound frame span AFTER store(outbound)
    // completes (durable-before-transmit, I-3). If null, outbound frames are
    // silently dropped (mirrors today's "no transport" state for earlier phases).
    // The 2d::TransportFactory replaces this in the transport/ feature (deferred).
    // NO std::mutex — must only be called from the session executor strand.
    // ([const §XV.9] grep gate: this field is a std::function, not std::mutex.)
    std::function<void(std::span<const std::byte>)> transport_send;

    // ── 013-session-reconnect-binding extensions (4 new fields) [data-model §E-4] ──

    // FR-017 / Clarifications Q1=A — per-session ResetSeqNumFlag policy.
    // Default bilateral_strict per spec FR-017 + [const §XII.5] no-implicit-default.
    // bilateral_strict: we send 141=Y in our outbound Logon AND require the peer
    //   to also send 141=Y; a peer Logon without 141=Y disconnects with
    //   session_seqnum_reset_mismatch(116). [FIX-SL §4.1.1: mutual seqnum reset]
    // bilateral_lenient: honour peer 141=Y if received; accept without refusing.
    // unilateral: honour any peer 141=Y regardless of our own flag (Fix8-style).
    // RC#C (gate-b/r1): restored from bilateral_lenient → bilateral_strict per FR-017.
    // Pre-013 tests updated to send 141=Y in their test Logon frames to comply.
    // [FR-017; Clarifications Q1=A; triage RC#C(a)]
    reset_seqnum_policy reset_seqnum_policy_field{reset_seqnum_policy::bilateral_strict};

    // ── 024-reset-refresh-on-logon (S-017) — lifecycle reset knobs ──────────
    //
    // Three additive bool knobs that trigger a durable seqnum reset to {1,1}
    // at the corresponding session lifecycle event.  All default false =
    // QuickFIX-compatible no-op; default-off is an EXPLICIT per-field default,
    // not an implicit-zero ([const §XII.5]).  The QuickFIX cfg-key spellings
    // (ResetOnLogon / ResetOnLogout / ResetOnDisconnect) inform naming so a
    // future cfg_loader mapping is 1:1, but the loader is NOT extended in this
    // slice (C1.2).  ABI note: adding three bool members changes SessionConfig
    // struct layout → a normal source rebuild is required (no C-ABI surface;
    // SessionConfig is a C++-only value type).
    //
    // reset_on_logon  (QuickFIX: ResetOnLogon)
    //   Initiator: reset seqnums to {1,1} before building the outbound Logon;
    //   the Logon then carries ResetSeqNumFlag(141)=Y (OR-of-three predicate).
    //   Acceptor: reset before check_inbound so a fresh peer 34=1 is admitted
    //   even when local next-expected > 1.
    // [data-model §"Additive config fields"; C1.1; research D5]
    bool reset_on_logon = false;

    // reset_on_logout  (QuickFIX: ResetOnLogout)
    //   Reset seqnums to {1,1} at Logout teardown in EITHER direction:
    //   Logout sent (locally-initiated, close(graceful)) OR Logout received
    //   (peer-initiated, keyed on a dedicated logout_seen_ flag).  Does NOT
    //   fire on an abnormal disconnect with no Logout (that is reset_on_disconnect).
    // [data-model §"Additive config fields"; C1.1; C3.1; research D5]
    bool reset_on_logout = false;

    // reset_on_disconnect  (QuickFIX: ResetOnDisconnect)
    //   Reset seqnums to {1,1} on ANY transport disconnect/teardown, including
    //   an abnormal connection drop (no Logout exchanged).  Superset of
    //   reset_on_logout: fires on graceful Logout, peer-initiated Logout, AND
    //   any raw read-pump EOF or transport error.
    // [data-model §"Additive config fields"; C1.1; C4.1; research D5]
    bool reset_on_disconnect = false;

    // FR-008 / Clarifications Q5=A — initiator-graceful Logout disconnect
    // timeout in milliseconds. Default 2000 ms (matches QuickFIX/J
    // SessionState.logoutTimeoutMs=2000L). Must be > 0; validated at
    // SessionConfig-build time per [arch §5.3] construction-time carve-out.
    std::uint32_t logout_disconnect_timeout_ms{2000};

    // FR-023 / Clarifications Q3=A — operator-supplied allow-list of
    // {principal → {compid_set}} bindings. Default-constructed = empty
    // allow-list = default-deny (rejects ALL Logons). Operator MUST enumerate
    // bindings before opening any session. COPY-CONSTRUCTIBLE per [data-model §E-3]
    // + 010 W-5 (CompIdAuthorizationPolicy pimpl supports copy). [data-model §E-4]
    CompIdAuthorizationPolicy compid_authorization_policy;

    // FR-030 / 2h Appendix D §D.2 reservation — operator-supplied per-session
    // transport factory override. Default nullptr => engine substitutes
    // EngineConfig::default_transport_factory at Session::open-time per:
    //   resolved_factory = transport_factory_override.value_or(
    //                          EngineConfig::default_transport_factory).
    // OWNERSHIP TYPE: std::shared_ptr<TransportFactory> (NOT unique_ptr) per
    // 010 FR-001a precedent — unique_ptr would break the
    // static_assert(std::is_copy_constructible_v<SessionConfig>) invariant at
    // line 176. The "no factory shared across Sessions" invariant is preserved
    // via a Session::open-time hygiene assertion (Phase 3 T030) checking
    // use_count()==1. The shared_ptr type is for SessionConfig COPY SEMANTICS
    // ONLY; cross-Session sharing is FORBIDDEN. [2h Appendix D §D.1+§D.2]
    std::shared_ptr<fixpp::transport::TransportFactory> transport_factory_override;

    // 014 T009 — peer endpoint for initiator reconnect attempts.
    // Set by the operator at SessionConfig-build time; consumed by
    // Session::open() which calls reconnect_fsm_.set_reconnect_endpoint().
    // Default-constructed Endpoint (empty host, port=0) means "not configured".
    // [data-model §E-1 step 5 — async_connect(ep)]
    fixpp::transport::Endpoint reconnect_endpoint;

    // 016 T008 — per-session initiator reconnect backoff policy. nullopt ⇒ the
    // Session resolves ReconnectPolicy::defaults_quickfix_compat() at construction
    // (single 30 s interval, unbounded), discharging the 015 down-peer L2
    // carry-forward: the prior hard-coded empty ReconnectPolicy{} had a 0-backoff
    // schedule (delay_for_attempt == 0) ⇒ ~100% CPU busy-spin on repeated connect
    // failure. Operators set a finite max_attempts policy to bound reconnect (the
    // FSM then surfaces transport_reconnect_limit_exceeded at the cap). Optional
    // (no-implicit-default, [const §XII.5]); value-typed ⇒ keeps SessionConfig
    // copy-constructible. [FR-004; data-model §E-1 reconnect_policy]
    std::optional<fixpp::transport::ReconnectPolicy> reconnect_policy;

    // 015 T016(d) — engine-managed lazy-connect discriminator (connect-then-Logon).
    // Set ONLY by the Engine's run_connect_loop for initiator sessions it drives.
    // When true, Session::open()'s initiator arm is a NO-OP (no LogonSent
    // transition, no Logon emission) — exactly like the acceptor arm — because
    // there is no live transport yet. The connect loop then calls
    // Session::drive_reconnect(), whose install_reconnected_transport rebinds
    // transport_send_ and re-enters LogonSent, after which the initial Logon is
    // emitted POST-connect over the now-live sink (FR-003 / E-1a / Clarifications
    // 2026-05-31; grounded in QuickFIX-cpp setResponder→generateLogon + Fix8
    // connect→send(generate_logon)). DEFAULT false preserves the 013/014
    // per-session-direct model where open() emits the Logon at open.
    bool engine_managed = false;

    // 023 T009 — engine-only strand adoption seam (D3-B / E-3 / INV-3a).
    // When set by Engine::start(), Session::open() calls the adopt_strand_t
    // overload of make_session_executor, storing this executor DIRECTLY with
    // strand_wrapped=true instead of re-wrapping via make_strand (D1 anti-pattern).
    //
    // MUST NOT be set by non-engine callers. The ordinary user per_session_strand
    // path is BYTE-UNCHANGED: open() only uses this field when it is non-empty.
    // Default: nullopt (ordinary path; make_session_executor wraps with make_strand).
    //
    // Type: asio::any_io_executor (type-erased) so session_config.hpp need not
    // include <asio/strand.hpp>; the engine passes asio::any_io_executor{strand}.
    // Copy-constructible: asio::any_io_executor satisfies std::is_copy_constructible.
    std::optional<asio::any_io_executor> engine_adopt_strand;

    // 021 FR-010 / data-model §2 — inbound validated too-low possible-duplicate
    // APPLICATION-message disposition. When false (default, QFJ-parity), a
    // tolerated too-low app duplicate is dropped (no Application::fromApp call);
    // when true (QuickFIX-cpp parity), it is redelivered to fromApp (the replayed
    // frame carries 43=Y, so the callback sees it flagged possible-duplicate).
    // ADMINISTRATIVE duplicates are ALWAYS ignored regardless of this knob.
    // Neither setting advances the inbound seqnum or disconnects. This is a
    // protocol duplicate-discard (the message was already processed once;
    // MsgSeqNum < expected proves it) — NOT a [const §XV.15] backpressure drop.
    // Additive, default-valued public-header field ⇒ no ABI/behavior break.
    bool redeliver_poss_dup = false;

    // 022 FR-006 / D1 — send-path AllowPosDup knob (QuickFIX-J
    // SETTING_ALLOW_POS_DUP_MESSAGES parity, exact key spelling for 008
    // cfg_loader translation). When false (DEFAULT, matches both QFcpp
    // unconditional-strip and QFJ default), a plain Session::send STRIPS any
    // caller-supplied PossDupFlag(43) and OrigSendingTime(122) from the opaque
    // app payload before framing. When true, they are RETAINED verbatim (QFJ
    // sendRaw-as-is) — operator opt-in for callers that manage their own
    // duplicate flags. The automatic resend/retransmission path
    // (build_replay_frame) always re-adds 43=Y+122 regardless of this knob
    // (FR-007 — structurally independent; build_replay_frame never calls
    // send_impl). No C-ABI surface or error-slot change; additive C++ config
    // field ⇒ struct-layout change requiring a normal source rebuild;
    // default-strip is an intentional default wire-behavior change for plain
    // sends containing caller-supplied 43/122 (matching QF defaults).
    bool allow_pos_dup = false;

    // 026 T003 / data-model E4 — precision used to stamp newly-stamped outbound
    // SendingTime(52) only.  OrigSendingTime(122) on resend is the stored
    // original 52 preserved verbatim — never re-stamped at this precision
    // (I-NST-4; build_replay_frame byte-copies the stored 52 into 122).
    // Default millis ⇒ FIX 4.x parity = byte-identical no-op (FR-003/SC-002,
    // I-NST-1); nanos/micros/seconds are opt-in.
    // No-implicit-default: millis is an EXPLICIT per-field default
    // ([const §XII.5]).  ABI note: adding this field changes SessionConfig
    // struct layout → a normal source rebuild is required (no C-ABI surface;
    // SessionConfig is a C++-only value type).
    fixpp::core::fix_time_precision sending_time_precision =
        fixpp::core::fix_time_precision::millis;

    // 027 T003 / data-model E1 — enable NextExpectedMsgSeqNum(789) fast session
    // resume. Single knob controlling BOTH emitting our 789 in the outbound Logon
    // AND honoring a peer's inbound 789 (research D-2):
    //   emit: build_logon appends 789=<next_inbound_unsafe()> after the 141 block;
    //   honor: on inbound Logon carrying 789=X, proactively resend [X, N-1] via
    //          replay_outbound_range_() (eliminates the ResendRequest round-trip).
    // Default false = QuickFIX-compatible no-op; default-off is an EXPLICIT
    // per-field default ([const §XII.5]; parity with QFcpp m_sendNextExpectedMsgSeqNum
    // default false and QFJ enableNextExpectedMsgSeqNum default false).
    // ABI note: additive bool field changes SessionConfig struct layout → a normal
    // source rebuild is required (no C-ABI surface; SessionConfig is C++-only).
    // No new include: primitive bool, §XV.9 N/A (data-model E1).
    bool enable_next_expected_msg_seq_num = false;

    // 028 T003 / data-model E1 / contract C1 — steady-state inbound CompID
    // validation knob (QuickFIX: CheckCompID, default Y). Default true = strict
    // = current behaviour: SenderCompID(49)/TargetCompID(56) of every post-Logon
    // inbound message must match cfg.target_comp_id/cfg.sender_comp_id; mismatch
    // disconnects. false = QuickFIX-compat relaxation: steady-state CompID check
    // is skipped (Logon-establishment CompID check and BeginString(8) check remain
    // strict; 013 compid_authorization_policy allow-list also remains strict).
    // Note inverted polarity vs 021/022/024/026/027 knobs (those default false;
    // this defaults true because strict is the safe baseline and the relaxation
    // is the opt-in). No new include: primitive bool, §XV.9 N/A (data-model E1).
    bool check_comp_id = true;

    // 028 T003 / data-model E1 / contract C2 — inbound sequence-number
    // validation knob (QuickFIX: ValidateSequenceNumbers, default Y). Default
    // true = strict = current behaviour: too-high triggers ResendRequest +
    // AwaitingResend; too-low disconnects. false = QuickFIX-compat relaxation:
    // out-of-order frames (too-high or too-low) are delivered without advancing
    // the counter; no ResendRequest emitted; session stays Active. The seq==0
    // fatal, too-low-Heartbeat(35=0) silent-drop, PossDup(43) path, and
    // Logon-time sequence checks remain strict. Note inverted polarity vs
    // 021/022/024/026/027 knobs (those default false; this defaults true).
    // No new include: primitive bool, §XV.9 N/A (data-model E1).
    bool validate_sequence_numbers = true;
};

// FR-001 / D-1 — hygiene gate: SessionConfig must be copy-constructible so
// Session can hold it by value (W-5 lifetime fix, 010-session-cfg-lifetime).
// This static_assert fires at compile time if any future field addition breaks
// copyability (e.g., a unique_ptr<T> member). The std::shared_ptr<T> pattern
// (as used for store_factory post FR-001a amendment) is copy-constructible;
// unique_ptr<T> is not. Amendment: if a field must be non-copyable, convert
// it to shared_ptr per the FR-001a amendment pattern and update this comment.
static_assert(std::is_copy_constructible_v<SessionConfig>,
              "SessionConfig must be copy-constructible per 010 W-5 by-value "
              "Session::cfg_ membership (FR-001 / D-1). If a new field is not "
              "copy-constructible (e.g. unique_ptr<T>), convert to shared_ptr<T> "
              "following the FR-001a amendment pattern for store_factory.");

}  // namespace fixpp::session
