// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/session.hpp
//
// fixpp::session::Session — the MINIMAL REAL skeleton (Clarifications
// 2026-05-19 Q1 / D-4 / E10). 2d-OWNED surface ONLY — NO FIX FSM. The full
// FIX establishment FSM (Logon / gap-fill / ResendRequest / sequence-reset /
// concrete heartbeat values / send()) is owned by the deferred 005, which
// EXTENDS this same type. 007 ships only what the threading contract needs:
// the executor→session_executor binding, two-phase close, the engine-internal
// session_arena() accessor, and the session_local<trace_context> slot.
// [2d §4.5]/§4.6/§4.7. Realizes specs/007-threading-clock/contracts/session.hpp.
//
// Phase 2 ships the SHAPE (this header + the out-of-line skeleton in
// src/session/session.cpp). The 2d-owned BEHAVIOUR is wired per user story:
//   open()  executor-resolution/rejections  → T020 (US1)
//   open()  effective_clock resolution       → T030 (US2)
//   close() two-phase / idempotency          → T037/T038/T039 (US3)
//   trace slot population/teardown           → T045 (US4)
//   open()  config-validation rejections     → T050 (US5)
#pragma once

#include <array>
#include <asio/awaitable.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/post.hpp>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/clock.hpp>  // steady_time_point (T041 US3 liveness)
#include <fixpp/core/error.hpp>  // expected_t
#include <fixpp/core/session_executor.hpp>
#include <fixpp/core/session_local.hpp>
#include <fixpp/core/trace_context.hpp>
#include <fixpp/session/message_store.hpp>  // 008-message-store — MessageStore complete type
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
// (the unique_ptr<MessageStore> member's
// nested type alias flush_hook_fn requires it).
#include <fixpp/session/reconnect_fsm.hpp>   // 013 T023 — ReconnectFsm driver (FR-009 state owner)
#include <fixpp/session/seqnum.hpp>          // 005 US2 — seqnum_t / seqnum_min
#include <fixpp/session/seqnum_manager.hpp>  // 005 US2 — SeqnumManager (T031)
#include <fixpp/session/session_config.hpp>  // FR-001 / D-1 — by-value cfg_ member requires complete type (W-5 lifetime fix, 010)
#include <fixpp/session/session_event.hpp>  // 013 T013a — SessionEvent + kSessionEventRingCapacity
#include <fixpp/session/session_fsm.hpp>    // 005-session-establishment-fsm — fsm_state enum

namespace fixpp::core {
struct EngineConfig;
class Clock;
}  // namespace fixpp::core

namespace fixpp::transport {
class Transport;
struct handshake_result;
}  // namespace fixpp::transport

namespace fixpp::session {

// graceful: phase 1 (engine-internal FileStore::flush_for_session_close()
// hook once → Logout exchange under a CHILD asio::cancellation_state below
// the root) → phase 2 (root cancellation_type::total). terminal: skip phase 1
// (hook NOT invoked). partial is NOT in the v1.0 surface (N-P1-3).
enum class close_mode : std::uint8_t { graceful = 0, terminal = 1 };

// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding) — Session is heap-allocated once per
// session; the 39-byte padding doesn't fit on a hot path. Field order tracks the
// FSM/handshake/seqnum/store/clock/exec/state groupings; reordering for tight packing would obscure
// that structure. Defer to a dedicated perf pass if profiling shows it matters.
class Session {
public:
    // The ctor pre-conditions a non-null session_arena via the [2d §4.5]
    // resolution chain so session_arena()'s never-null contract holds for
    // the whole session lifetime (I-18). engine is borrowed and MUST
    // outlive the Session (engine-owned lifetime — [arch §4.4]). cfg is
    // COPIED into cfg_ by value (FR-001 / D-1 / W-5 lifetime fix); the
    // caller may freely drop or mutate the config after the ctor returns.
    //
    // NOT noexcept (W3.1 / /simplify B-1, 010-session-cfg-lifetime): the
    // SessionConfig copy-ctor invoked in the initializer list can throw
    // — std::string (sender_comp_id / target_comp_id / begin_string) may
    // allocate when above the SSO threshold; std::function<void(span)>
    // transport_send may allocate for non-SBO captures; std::shared_ptr
    // copies bump a refcount whose atomic op is noexcept but whose enclosing
    // copy ctor is not annotated noexcept by the standard. Declaring this
    // ctor `noexcept` while constructing `SessionConfig cfg_;` from a
    // potentially-throwing copy would call std::terminate on any thrown
    // copy — UB-class hazard. Matches the close() precedent below (NOT
    // noexcept because std::make_shared allocates).
    Session(const fixpp::core::EngineConfig& engine, const SessionConfig& cfg);

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;
    ~Session();

    // Minimal 2d-OWNED open() shape (D-4). The FIX establishment FSM is
    // 005's; the 2d-owned obligations bound here: (1) resolve the session
    // executor as SessionConfig::executor_override.value_or(
    // EngineConfig::executor) and feed it to make_session_executor (the
    // SINGLE error::executor_not_serialised point — FR-009/I-06); (2)
    // resolve effective_clock = clock_override ?: EngineConfig::clock ONCE,
    // bound to session lifetime (FR-005/I-03); (3) populate the
    // session_local<trace_context> slot from initial_trace_context (FR-014);
    // (4) reject null dictionary / null EngineConfig::executor / sentinel
    // security_profile / incompatible combo → invalid_session_config
    // (FR-018); (5) reject a second open() → session_already_open (slot 51).
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> open() noexcept;

    // Two-phase close ([2d §4.7]:833-834 frozen shape). Idempotent
    // THREE-STATE model (I-10): already-closing → SAME in-flight awaitable,
    // no error, no side effects; never-opened / already-closed(drained) →
    // error::session_already_closed; no side effects in any case. graceful
    // runs the engine-internal FileStore::flush_for_session_close() hook
    // once in phase 1 (after the last in-flight store(...) resumes, before
    // the Logout async_write); terminal skips phase 1 entirely (hook NOT
    // invoked). partial excluded (N-P1-3).
    //
    // PRECONDITION — v1.0 caller model (gate-b/r1 RC#2, P1.3→P2):
    //   close() is called from within the session's serialisation domain
    //   (strand under per_session_strand mode; attested executor under
    //   direct_executor). Concurrent foreign-thread invocation is UNDEFINED
    //   in v1.0; the C-ABI thunk (2i) is responsible for serialising user-
    //   thread invocations onto the session domain before calling close().
    //   The `closing` re-entry polling loop is a future-proof barrier; under
    //   the v1.0 caller model it is effectively empty (the first close runs
    //   to completion within the serialisation domain before any second
    //   close() call can reach the `closing` branch).
    //   TODO(005): when the C-ABI thunk (2i) wires real concurrent callers,
    //   harden with atomic<lifecycle> + atomic_shared_ptr/mutex to make
    //   foreign-thread concurrent close safe.
    //
    // NOTE on noexcept (gate-b/r1 RC#2, P2.1): `close()` is NOT declared
    // noexcept because the first-close path allocates via std::make_shared
    // and may invoke the user-supplied close_flush_hook_ (std::function),
    // both of which can throw. Under the project-wide D-9 terminate-on-OOM
    // policy a bad_alloc from make_shared on the cold path would terminate;
    // the hook's exception guarantee is the hook author's responsibility. The
    // C-ABI thunk (2i) wraps the call in try/catch as its projection.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> close(
        close_mode mode = close_mode::graceful);

    // ENGINE-INTERNAL accessor (callable from fixpp::session/ ONLY —
    // [arch §2.3] leaf rule; consumed by the merged-006 session-side helper
    // async_lock_via_session_executor per [2f App D §D.1]). noexcept; NEVER
    // null — the ctor pre-conditioned the [2d §4.5] resolution chain
    // (SessionConfig::session_arena ?: EngineConfig::default_session_resource
    // ?: std::pmr::get_default_resource()); frozen at open, never swapped
    // mid-session (I-18).
    [[nodiscard]] std::pmr::memory_resource* session_arena() const noexcept;

    // ENGINE-INTERNAL (fixpp::session/ + test seams). The resolved
    // session_executor binding (FR-007/FR-009). Valid only after a
    // successful open(); precondition: state_ == open.
    [[nodiscard]] const fixpp::core::session_executor& executor() const noexcept { return exec_; }
    [[nodiscard]] bool is_open() const noexcept { return state_ == lifecycle::open; }

    // The canonical session trace-context accessor (017 owned amendment #1 /
    // contracts/adjacent-amendments.md §1 / [2k App D §D.1]).
    // Returns the session-domain trace_context stored in trace_slot_ at open()
    // from SessionConfig::initial_trace_context. Used by FIXPP_SLOG callers:
    //   auto const& tc = session.get_trace_context();
    //   FIXPP_SLOG(logger, info, tc, cat::session, "msg {}", ...);
    // Valid only after a successful open(); plain value ownership (NOT thread_local).
    // [[clang::lifetimebound]] on the implicit *this: the returned reference is
    // bound to the Session object lifetime.
    [[nodiscard]]
    const fixpp::otel::trace_context& get_trace_context() const noexcept [[clang::lifetimebound]] {
        return trace_slot_.load();
    }

    // The single effective_clock resolved ONCE at open() (FR-005 / I-03):
    // SessionConfig::clock_override ?: EngineConfig::clock, bound to the
    // session lifetime. Every session-scoped consumer reads this; valid only
    // after a successful open().
    [[nodiscard]] const std::shared_ptr<fixpp::core::Clock>& effective_clock() const noexcept {
        return effective_clock_;
    }

    // ENGINE-INTERNAL (fixpp::session/ + seam tests). The phase-1 close
    // flush seam (D-16 / I-07): 007 ships NO MessageStore/FileStore type —
    // the real non-virtual FileStore::flush_for_session_close() reached via
    // the session's unique_ptr<MessageStore> friend mechanism is 2e/005's.
    // 007 wires only the CALL SITE and asserts the 2d-owned ORDERING
    // property (D-5 scripted-test-double scoping). The seam-5 scripted
    // double installs a hook here; close(graceful) invokes it EXACTLY ONCE
    // in phase 1 (after the last in-flight store(...) resumes, before the
    // Logout step); close(terminal) NEVER invokes it. A hook returning
    // unexpected{store_io_failure} is logged and close proceeds (I-07).
    using close_flush_hook = std::function<fixpp::core::expected_t<void>()>;
    void set_close_flush_hook(close_flush_hook hook) noexcept {
        close_flush_hook_ = std::move(hook);
    }

    // ENGINE-INTERNAL (fixpp::session/ + seam tests). The ROOT cancellation
    // slot (T039 / I-07): in-flight session work (transport read/write,
    // heartbeat sleep, awaitable-mutex acquire, app-callback dispatch via
    // cancellable_dispatch, the parser→fromApp chain — all 005-owned in the
    // real engine) binds to this slot so phase-2's cancellation_type::total
    // tears every strand of in-flight work down. 007 exposes the slot and
    // fires phase-2 total; the scripted double binds a sleep/dispatch to it
    // and asserts the propagation property only.
    [[nodiscard]] asio::cancellation_slot root_cancellation_slot() noexcept {
        return root_cancel_.slot();
    }

    // ── 005-session-establishment-fsm scaffold (T016) ─────────────────────────
    //
    // These methods complete the [FIX-SL §4.10] Session lifecycle surface.
    // Bodies land per user story (Phase 3–6); Phase 2 ships declarations only.
    //
    // Reentrancy contract (documented per entry point, [const §X.5] / FR-016):
    //   on_inbound_frame() — session-strand; engine's transport feeds it after
    //                        parse/frame-validate; inbound ordering: store(inbound)
    //                        completes BEFORE fromAdmin/fromApp is dispatched.
    //   send()             — session-strand; durable-before-transmit (I-3):
    //                        store(seq, committed, outbound) BEFORE transport write.
    //   state()            — session-strand; single-writer on the per-session strand.
    //   fromAdmin/fromApp  — user callbacks dispatched via cancellable_dispatch
    //                        ([2d §6.5]) on the per-session strand (default mode).
    // All public surfaces noexcept across the inbound-process / timer-fire window;
    // a throwing user callback TRAPS (core::detail::trap_throw) — FR-015.

    // Engine feeds a verified inbound FIX frame (post-Framer/Parser, [2e]
    // inbound ordering). Returns the FSM-defined disposition.
    // PLACEHOLDER — body wired per US1/T024 (Phase 3).
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> on_inbound_frame(
        std::span<const std::byte> frame) noexcept;

    // User output. Stamps SendingTime(52) from effective_clock, assigns
    // MsgSeqNum(34), Writer::commit, store(seq, committed, outbound) BEFORE
    // transport::async_write (durable-before-transmit, I-3).
    // PLACEHOLDER — body wired per US1/T023 (Phase 3).
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> send(
        std::span<const std::byte> app_payload) noexcept;

    // Current FSM state (read-only; single-writer on the per-session strand).
    // PLACEHOLDER — returns NotConnected until Phase 3 wires the FSM field.
    [[nodiscard]] fsm_state state() const noexcept;

    // FR-004 / D-2 — set of recent FSM transitions (capacity ≤16).
    // Returns a std::span view over the underlying std::array<fsm_state, 16>
    // in PHYSICAL-BUFFER ORDER (index 0..15).
    //
    // CONTRACT: containment / membership-witness only. The returned span gives
    // the set of transitions recorded over the most recent ≤16
    // record_state_transition_() calls. Tests assert membership via Contains /
    // std::find / history_contains; callers MUST NOT infer event order from
    // index position (the buffer is NOT chronologically ordered).
    //
    // Always-on, zero-cost-when-unread (~1ns push per transition). Used by
    // FR-004 observability tests and FR-006 matrix witness per-cell checks.
    // Empty before the first record_state_transition_() call.
    [[nodiscard]] std::span<const fsm_state> fsm_visit_history() const noexcept;

    // 013 FR-035 — NEW ring-buffer accessor for SessionEvent observability.
    // DISTINCT from fsm_visit_history() (FSM-state observation; unchanged).
    // Returns a std::span view over the underlying fixed-capacity ring of
    // SessionEvent values (capacity = kSessionEventRingCapacity = 16).
    // Membership-witness semantics per 010 F-04 contract — NOT chronologically
    // ordered. Body wired in Phase 5 T040; declaration here satisfies Phase 2.
    // [data-model §E-6 / contracts/session_ext.hpp]
    [[nodiscard]] std::span<const SessionEvent> recent_events() const noexcept;

    // 013 T044 — FR-030 / D-11 — Operator-facing credential rotation forwarder.
    // Pure forwarder: validates nullptr + factory-present, then delegates to
    // cfg_.transport_factory_override->reload_credentials(new_source).
    // Returns error::session_invalid_argument (slot 119) if new_source is nullptr
    // or no transport_factory_override is configured. NO direct atomic-swap in
    // Session — the factory IS the symmetric authority per
    // [[feedback_half_restructure_symmetric_api]].
    // Active session / Transport state UNAFFECTED — only the NEXT
    // TransportFactory::make() call (next reconnect cycle) observes the rotated
    // source per FR-031.
    //
    // session_event_credentials_rotated WIRED in 014 T017/T018:
    // The event fires BEFORE the first handshake on the rotated cert_source
    // (data-model E-3; contracts C3; FR-009) via the FSM's
    // emit_credentials_rotated_ callback injected by Session::open() at T018.
    // Session::open() wires: reconnect_fsm_.set_emit_credentials_rotated(
    //     [this](session_event_credentials_rotated ev){ emit_event(std::move(ev)); });
    // The real leaf SHA-256 fingerprint is computed inside drive_reconnect_attempt
    // via co_await snap->load_credentials() per FR-010.
    // DEFERRED comment from 013 RESOLVED. [FR-032; data-model §E-3; T017/T018]
    [[nodiscard]] fixpp::core::expected_t<void> reload_credentials(
        std::shared_ptr<fixpp::tls::cert_source> new_source) noexcept;

    // The per-session strand callback-dispatch path (FR-008 / I-05 / T021):
    // every application callback ({onLogon,onLogout,toAdmin,fromAdmin,toApp,
    // fromApp,store op,clock wake,transport completion}) is submitted onto
    // the resolved serialisation domain via the bound session_executor.
    // Under per_session_strand exec_ wraps asio::make_strand so no two run
    // concurrently within a session and fromApp(N+1) never begins before
    // fromApp(N) returns; under direct_executor it is the attested
    // already-serialised executor. The engine NEVER picks a concrete
    // executor — exec_ derives from
    // SessionConfig::executor_override.value_or(EngineConfig::executor).
    //
    // LIFETIME PRECONDITION (engine-internal contract): the caller MUST keep
    // this Session alive until ALL work dispatched here has fully run — the
    // posted handler captures `this`, and in debug builds its re-entrancy
    // dispatch_guard dtor (~below) stores to `in_dispatch_` AFTER the user
    // callback returns. Destroying the Session while a dispatched callback is
    // queued or running (incl. that trailing guard store) is UB / a data race.
    // The Engine satisfies this on teardown via stop() → close(terminal) +
    // join-before-registry-clear; a caller that constructs a raw Session and
    // drives it directly (the seam/unit tests) MUST drain `exec_` itself before
    // the Session goes out of scope (e.g. a guard-less post onto executor() +
    // wait — see tests/session/test_executor_compat.cpp run_combo). Phase-5
    // app-callback wiring inherits this requirement (spec/behaviors-and-
    // limitations.md L-015-4; cf. detached-write keepalive, 014).
    template <class F>
    void dispatch_app_callback(F&& f) const {
        asio::post(exec_, [this, g = std::forward<F>(f)]() mutable {
#ifndef NDEBUG
            // Seam 16 / Edge Case: in DEBUG builds a detected
            // concurrent session-callback entry trips the
            // strand-invariant assert (the symptom of
            // direct_executor attested over a genuinely
            // non-serialised executor). RELEASE builds compile
            // this out — that misuse is documented
            // user-contract-violation UB, not a runtime guard.
            //
            // RC#2 P2.3 (gate-b/r1): RAII guard so the flag is
            // always cleared on scope exit, even if the callback
            // throws. Without this, a throwing callback left
            // in_dispatch_ permanently set and false-positived the
            // next otherwise-serial dispatch assertion.
            struct dispatch_guard {
                // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members) — RAII guard
                // binds to the caller's flag for clear-on-scope-exit.
                std::atomic<bool>& flag;
                explicit dispatch_guard(std::atomic<bool>& f) noexcept : flag(f) {}
                ~dispatch_guard() noexcept { flag.store(false, std::memory_order_release); }
                dispatch_guard(const dispatch_guard&) = delete;
                dispatch_guard(dispatch_guard&&) = delete;
                dispatch_guard& operator=(const dispatch_guard&) = delete;
                dispatch_guard& operator=(dispatch_guard&&) = delete;
            };
            const bool prev = in_dispatch_.exchange(true, std::memory_order_acq_rel);
            assert(!prev &&
                   "concurrent session callback entry: strand "
                   "invariant violated (direct_executor attested "
                   "over a non-serialised executor?)");
            [[maybe_unused]] dispatch_guard guard{in_dispatch_};
#endif
            g();
        });
    }

    // ── 019-app-callbacks T007 ────────────────────────────────────────────────
    //
    // callback_dispatch_scope — release-safe RAII guard for direct on-strand
    // Application callback sites (research D3). Complements the existing
    // `dispatch_guard` which is DEBUG-only and local to dispatch_app_callback.
    //
    // In DEBUG builds: asserts that no two callbacks enter concurrently for this
    // session (strand invariant — INV-2), then clears the flag on scope exit
    // (even if the callback throws, so the flag is never left set on unwind).
    // In RELEASE builds: zero overhead (the guard body compiles out entirely).
    //
    // Usage at a direct on-strand call site (T011/T014/T016 sites):
    //   callback_dispatch_scope cs{*this};
    //   app->fromApp(view, id);           // guarded
    //   // cs dtor fires on any exit path
    //
    // Does NOT itself suppress exceptions — the throw-wrapper invoke_callback_safe
    // (below) wraps the call for the terminal-close disposition (FR-011; D5).
    //
    // Anchor: specs/019-app-callbacks/research.md D3/D5; data-model.md INV-2;
    //         tasks.md T007. [const §XV.9] — no std::mutex, no exception paths.
    struct callback_dispatch_scope {
#ifndef NDEBUG
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
        std::atomic<bool>& flag;
        explicit callback_dispatch_scope(const Session& s) noexcept : flag(s.in_dispatch_) {
            const bool prev = flag.exchange(true, std::memory_order_acq_rel);
            assert(!prev &&
                   "concurrent session callback entry: strand "
                   "invariant violated (direct_executor attested "
                   "over a non-serialised executor?)");
        }
        ~callback_dispatch_scope() noexcept { flag.store(false, std::memory_order_release); }
        callback_dispatch_scope(const callback_dispatch_scope&) = delete;
        callback_dispatch_scope(callback_dispatch_scope&&) = delete;
        callback_dispatch_scope& operator=(const callback_dispatch_scope&) = delete;
        callback_dispatch_scope& operator=(callback_dispatch_scope&&) = delete;
#else
        explicit callback_dispatch_scope(const Session& /*s*/) noexcept {}
        // All special members are defaulted — the debug body is a no-op.
        // No fields ⇒ EBO applies; the guard adds zero size/overhead in release.
        callback_dispatch_scope(const callback_dispatch_scope&) = delete;
        callback_dispatch_scope(callback_dispatch_scope&&) = delete;
        callback_dispatch_scope& operator=(const callback_dispatch_scope&) = delete;
        callback_dispatch_scope& operator=(callback_dispatch_scope&&) = delete;
        ~callback_dispatch_scope() = default;
#endif
    };

    // invoke_callback_safe — throw→terminal-close wrapper scaffold (FR-011; D5).
    //
    // Wraps a callable that invokes one Application method in a try/catch block.
    // On catch: stores app_callback_threw as the pending error, then calls
    // close(terminal) via the session's close method (which must be co_awaited
    // by the caller). The US1/US2/US3 implementation tasks wire this at each
    // callback site; this scaffold is the single shared shape they call.
    //
    // Returns: the callable's return value on success (expected_t<void>{} for
    //   void callbacks); unexpected(error::app_callback_threw) on catch.
    //
    // NOTE: This is a scaffold — the caller is responsible for co_awaiting
    // close(terminal) when the result is app_callback_threw. The US1-US3 tasks
    // (T011/T014/T016) handle this; the scaffold itself only catches + records.
    // [research D5; FR-011; data-model.md "Reject/veto value mapping"]
    template <class F>
    [[nodiscard]] static fixpp::core::expected_t<void> invoke_callback_safe(F&& f) noexcept {
        try {
            if constexpr (std::is_void_v<decltype(std::forward<F>(f)())>) {
                std::forward<F>(f)();
                return {};
            } else {
                return std::forward<F>(f)();
            }
        } catch (...) {
            // A throw from a user callback is a fatal contract violation per
            // FR-011. The caller must terminal-close the session. We record
            // app_callback_threw here; the caller co_awaits close(terminal).
            return std::unexpected(fixpp::core::error::app_callback_threw);
        }
    }

#ifdef FIXPP_TEST_HOOKS
    // TEST-ONLY accessor: expose SeqnumManager for drain-contract tests
    // (009 T021 FR-011 — CloseWithHolderDoesNotTerminate). Allows tests to
    // directly acquire the internal async_mutex to manufacture a genuine holder
    // that is in-flight when close() calls seqnum_mgr_.drain(). NOT for
    // production use. Gated by FIXPP_TEST_HOOKS ([const §XV.9]).
    [[nodiscard]] SeqnumManager& seqnum_mgr_test_access() noexcept { return seqnum_mgr_; }
#endif

    // 015 T011 — Engine-internal acceptor attach primitive.
    // Called by the engine's run_accept_loop STRICTLY-BEFORE the first
    // on_inbound_frame (happens-before invariant Gate A New-1 / E-4).
    // Actions: set live_peer_id_, rebind transport_send_, take ownership.
    // Does NOT transition the FSM. NOT install_reconnected_transport (which
    // re-enters LogonSent — initiator-only; Gate A Codex-2).
    // Declared public for accessibility from engine.cpp's run_accept_loop.
    // By convention only the engine's accept loop should call this method.
    // §XV.9: handshake_result forward-declared on line 60; full def in session.cpp.
    // [data-model §E-2; T011; contracts C1 step 5; FR-005/006; T-041]
    void attach_accepted_transport(std::unique_ptr<fixpp::transport::Transport> transport,
                                   fixpp::transport::handshake_result hr) noexcept;

    // 015 T016(b) — Engine connect-loop driver (SC-010 delta (7)).
    // Public awaitable over the private reconnect_fsm_.drive_reconnect_attempt():
    // connects + handshakes + authorizes + install_reconnected_transport (which
    // rebinds transport_send_ to the live sink + re-enters LogonSent), and THEN
    // emits the initial Logon POST-connect over that now-live sink (connect-then-
    // Logon, FR-003 / E-1a — grounded in QuickFIX-cpp setResponder→generateLogon
    // and Fix8 connect→send(generate_logon)). On reconnect exhaustion/cancel or a
    // Logon-emit failure the session is left Disconnected and the error returned.
    // Declared public for accessibility from engine.cpp's run_connect_loop; by
    // convention only the engine's connect loop should call it. The post-connect
    // emit is folded in here (NOT a third public method) to hold the SC-010
    // surface at exactly the two documented additions.
    // [data-model §E-1a; T016(b); FR-003/FR-004; SC-010 (7)]
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> drive_reconnect() noexcept;

    // 015 T016(b) — live-transport accessor for the read-pump (SC-010 delta (8)).
    // Returns the live transport installed post-connect: reconnected_transport_
    // (initiator, via drive_reconnect → install_reconnected_transport) or
    // accepted_transport_ (acceptor, via attach_accepted_transport). PRECONDITION:
    // a live transport has been installed (post drive_reconnect / attach); the
    // engine only calls this after a successful install. Declared public for
    // accessibility from engine.cpp's run_connect_loop. [T016(b); FR-003; SC-010 (8)]
    [[nodiscard]] fixpp::transport::Transport& live_transport() noexcept;

private:
    // ── 024-reset-refresh-on-logon (S-017) — durable reset helper ────────────
    //
    // Disposition for the store-failure handling in reset_seqnums_to_one_durable.
    // The disposition is keyed on the TRIGGER CAUSE (not the call site):
    //   fatal  — knob-driven Logon reset: a store failure propagates the error
    //            so the caller can block reaching Active (C2.6; research D2).
    //   logged — teardown / 013-only received-141: store failure is swallowed
    //            and logged (I-07 logged-then-proceed) and the method co_returns
    //            success (matching the existing :1589-1592 inline pattern; C2.6
    //            zero-regression clause; data-model §"Durable reset helper").
    // Logic lives in session.cpp to keep this enum include-free ([const §XV.9]).
    // [024 data-model §"Durable reset helper"; C2.6; research D2]
    enum class reset_disposition : std::uint8_t { fatal = 0, logged = 1 };

    // reset_seqnums_to_one_durable — shared durable-reset helper (T003).
    // Body: co_await seqnum_mgr_.reset_to_one() then co_await store_->reset().
    // store_ null-checked (matching the existing :1589-1592 null-guard pattern).
    // Disposition controls store-failure handling (see reset_disposition above).
    // NOT wired to any trigger in this slice (T002/T003 foundational only);
    // wired in T007 (initiator Logon), T008 (acceptor Logon), T014 (teardown).
    // [024 data-model §"Durable reset helper"; C2.6; research D2]
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset_seqnums_to_one_durable(
        reset_disposition disposition) noexcept;  // wired in T007/T008/T014

    const fixpp::core::EngineConfig& engine_;
    SessionConfig cfg_;  // FR-001 / D-1 — by-value copy (W-5 lifetime fix, 010); caller may drop or
                         // mutate the config after the ctor returns without causing UAF
    std::pmr::memory_resource* session_arena_;  // resolved in ctor, never null

    fixpp::core::session_executor exec_;                   // bound at open() (T020)
    std::shared_ptr<fixpp::core::Clock> effective_clock_;  // resolved at open() (T030)
    mutable std::atomic<bool> in_dispatch_{false};         // debug strand-invariant guard (seam 16)
    fixpp::core::session_local<fixpp::otel::trace_context> trace_slot_;  // [2d §4.6]

    // Lifecycle state for the idempotent three-state close model (I-10);
    // never-opened vs open vs closing vs closed(drained).
    enum class lifecycle : std::uint8_t {
        never_opened = 0,
        open = 1,
        closing = 2,
        closed_drained = 3,
    };
    lifecycle state_ = lifecycle::never_opened;

    // Phase-1 flush seam (D-16). Default-empty: a graceful close with no
    // installed hook simply skips phase-1 flush. 007 placeholder retained
    // here for the scripted seam-5 test double; 008's A1 close-path dispatch
    // (via `store_->flush_hook()` returning a typed `flush_hook_fn`) is wired
    // by T032 in Phase 4 alongside this field, NOT in place of it (the
    // typed-thunk pointer captured below is the live mechanism; the
    // `close_flush_hook_` std::function remains for 007's scripted scoping).
    close_flush_hook close_flush_hook_;

    // ── 008-message-store ownership wiring (T011 / FR-005 / FR-025 /
    //    FR-026) ────────────────────────────────────────────────────────────
    //
    // store_arena_resource_ is the engine-provided dedicated PMR resource
    // whose lifetime matches the store instance — peer of session_arena_,
    // NOT a sub-resource (FR-026 "persisted bytes outlive any per-session-
    // arena reset cadence"). Default upstream is std::pmr::get_default_resource().
    // Used as the 3rd `mr` argument to MessageStoreFactory::make() at open()
    // when the caller's MemoryStore::Config::store_resource is null.
    //
    // store_ is bound at open() if cfg_.store_factory is non-null (the
    // 007-baseline smoke path leaves it null; 005's FSM open() will require
    // it). N1 unique ownership: no sharing across sessions, no mid-session
    // swap (FR-025; [arch §5.6]).
    //
    // DESTRUCTION ORDER (CRITICAL): store_ MUST destruct BEFORE
    // store_arena_resource_ — the store frees back to this resource on its
    // own dtor. Member declaration order dictates the destruction order
    // (reverse of declaration); store_ is declared AFTER store_arena_resource_
    // here.
    std::pmr::monotonic_buffer_resource store_arena_resource_;
    std::unique_ptr<MessageStore> store_;

    // A1-pinned graceful-close hook (FR-028 / I-17 / Opus N3-P2-1). Read
    // ONCE from store_->flush_hook() at open() — engine-internal factory-
    // type tag. Null when no store wired OR when the concrete impl does
    // not satisfy detail::has_flush_for_session_close (MemoryStore path).
    // Dispatched at close(graceful) by T032; close(terminal) skips entirely.
    MessageStore::flush_hook_fn close_flush_hook_a1_ = nullptr;

    // Root cancellation signal (T039). Phase 2 fires cancellation_type::total
    // on this; in-flight work bound to root_cancellation_slot() unwinds.
    asio::cancellation_signal root_cancel_;

    // Idempotent THREE-STATE close (I-10): the FIRST close() runs the body
    // and caches its result here; a concurrent/subsequent call while
    // state_==closing observes the SAME in-flight result (no error, no side
    // effects) by awaiting this shared slot rather than re-running phase 1/2.
    std::shared_ptr<std::optional<fixpp::core::expected_t<void>>> close_result_;

    // ── 005-session-establishment-fsm FSM state field (T016) ─────────────────
    // Current FSM state; single-writer on the per-session strand (data-model E2).
    // Initialised to NotConnected; transitions wired per user story (Phase 3–6).
    // Separate from the lifecycle state_ above (that tracks open/close lifecycle;
    // this tracks the FIX protocol state).
    fsm_state fsm_state_ = fsm_state::NotConnected;

    // ── FR-004 / D-2 — FSM transition ring-buffer (capacity 16) ──────────────
    // Stores the last ≤16 fsm_state values recorded via record_state_transition_.
    // Written exclusively via record_state_transition_; ring wraps at index 16.
    // fsm_visit_count_ saturates at UINT8_MAX to avoid overflow; the write index
    // is tracked separately (fsm_visit_write_idx_) so the ring keeps rotating
    // even after fsm_visit_count_ saturates (F-13 fix).
    std::array<fsm_state, 16> fsm_visit_history_{};
    std::uint8_t fsm_visit_count_ = 0;
    std::uint32_t fsm_visit_write_idx_ = 0;

    // FR-004 / D-2 — route every FSM transition through this helper so the
    // ring-buffer is always in sync with fsm_state_.
    void record_state_transition_(fsm_state new_state) noexcept;

    // ── 019 T016 — lifecycle callback fire-once guards ────────────────────────
    //
    // onLogon_fired_ and onLogout_fired_ are set BEFORE the respective callback
    // is invoked, providing the INV-7 once-per-session guarantee across all three
    // converging onLogout exit paths:
    //   graceful close → record_state_transition_(Disconnected)
    //   terminal close → record_state_transition_(Disconnected)
    //   callback-threw → close(terminal) → record_state_transition_(Disconnected)
    // Both flags are checked inside record_state_transition_ so only ONE function
    // must be updated when new paths reach Disconnected.
    //
    // lifecycle_cb_threw_: set to true if a lifecycle callback (onLogon) threw.
    // record_state_transition_ is noexcept so it cannot propagate; callers that
    // transition TO Active check this flag and call close(terminal) if set.
    // (onLogout throws during a Disconnected transition — no further close needed.)
    // [019-app-callbacks T016; FR-009; data-model.md INV-7; research D3]
    bool onLogon_fired_ = false;
    bool onLogout_fired_ = false;
    bool lifecycle_cb_threw_ = false;

    // ── 013 FR-035 — SessionEvent ring-buffer (capacity kSessionEventRingCapacity=16) ──
    // Stores the most recent ≤16 SessionEvent values emitted via emit_event().
    // Written exclusively from the per-session strand ([const §XI.4]).
    // Ring wraps at kSessionEventRingCapacity; membership-witness semantics
    // (NOT chronologically ordered). [data-model §E-6]
    std::array<SessionEvent, kSessionEventRingCapacity> recent_events_{};
    std::size_t events_count_ = 0;
    std::size_t events_write_idx_ = 0;

    // 013 FR-035 — emit a SessionEvent into recent_events_. Called from the
    // per-session strand only ([const §XI.4]). Body wired in Phase 5 T040;
    // declaration here satisfies Phase 2 (link-green via session.cpp stub).
    void emit_event(SessionEvent ev) noexcept;

    // 019 T014 — fire toAdmin for an engine-originated admin emit (FR-008/010).
    // Called BEFORE store_then_emit at every admin message build site (Logon/
    // Logout/Heartbeat/TestRequest/ResendRequest/SequenceReset). Inspect-only —
    // admin is NOT vetoable. On throw: terminal-close + records app_callback_threw
    // (throw→terminal-close is handled by the caller after this returns false).
    // Returns true if toAdmin completed normally (or no Application registered);
    // returns false if the callback threw (caller must terminal-close + return error).
    // Called directly on the session strand ([research D3; FR-008/010]).
    [[nodiscard]] bool fire_to_admin_(std::span<const std::byte> frame) noexcept;

    // parse_and_dispatch_ — dedup helper: build a stack parse arena, re-frame +
    // parse `frame` into a MessageView<Index>, build the SessionId, open a
    // callback_dispatch_scope, and return invoke_callback_safe(cb(view, sid)).
    // On parse failure (Framer or Parser): returns expected_t<void>{} (skip callback,
    // not fatal — same disposition as every call site today).
    // `arena_bytes` is explicit so each call site documents its sizing choice.
    // [const §VIII.5] (stack-only, no heap); [019-app-callbacks T014/T011/T013/T016]
    template <class CB>
    [[nodiscard]] fixpp::core::expected_t<void> parse_and_dispatch_(
        std::span<const std::byte> frame, std::size_t arena_bytes, CB&& cb) noexcept;

    // emit_session_reject_ — dedup helper: build + emit a session Reject(35=3) with
    // RefTagID=0, SessionRejectReason=3, and Disconnected-on-failure error handling
    // for both assign_outbound and store_then_emit.  ref_seq is the MsgSeqNum of the
    // offending inbound message; ref_msg_type is its MsgType tag.
    // Returns expected_t<void>{} on success (or build failure); returns unexpected on
    // assign_outbound / store_then_emit failure (caller should propagate).
    // [019-app-callbacks T011/T016; FR-005; D4; INV-4]
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> emit_session_reject_(
        seqnum_t ref_seq, std::string_view ref_msg_type) noexcept;

    // 014 T010/T015 — PRIVATE handoff from ReconnectFsm on a successful attempt.
    // Called by ReconnectFsm::drive_reconnect_attempt() (step 8) via the
    // session_ back-pointer. ReconnectFsm is a value member of Session
    // (reconnect_fsm_, session.hpp:517) so the call is always in-domain.
    //
    // Responsibilities:
    //   - Take ownership of the live transport via reconnected_transport_.
    //   - Store handshake_result.peer_id as live_peer_id_ for the authorize
    //     site (E-2 / T015): the LogonSent→Active Logon-ack guard reads
    //     live_peer_id_ as arm (1-live) ahead of the override seam.
    //   - Re-enter LogonSent so the session re-drives Logon to Active.
    //
    // §XV.9 discipline: handshake_result is forward-declared in this header
    // (line 60 — namespace fixpp::transport { struct handshake_result; }).
    // A by-value parameter in a non-defining DECLARATION only needs the
    // forward declaration, so session.hpp stays free of tls_transport.hpp
    // (which pulls tls/pinset.hpp → std::shared_mutex into the awaitable
    // closure). The full definition is in session.cpp via #include
    // "fixpp/transport/tls_transport.hpp". [const §XV.9; 8e2d362 guard]
    //
    // [data-model §E-1 step 8; E-2; contracts C1; C2; FR-001; FR-006]
    void install_reconnected_transport(std::unique_ptr<fixpp::transport::Transport> transport,
                                       fixpp::transport::handshake_result hr) noexcept;

    // ReconnectFsm is a value member of Session and needs access to the
    // private install_reconnected_transport() method. The friend declaration
    // enables the call from reconnect_fsm.cpp (which includes session.hpp).
    friend class ReconnectFsm;

    // ── 005 US2 seqnum counter manager (T031) ────────────────────────────────
    // Serialised by the async_mutex inside SeqnumManager (D-7 / [2f §7.3]).
    // Lifetime: bound to Session; drained at close().
    SeqnumManager seqnum_mgr_;

    // ── 005 US3 liveness loop (T039/T041) ────────────────────────────────────
    // run_liveness_loop — co_spawned on the session executor when the session
    // first enters Active state (LogonSent → Active via peer Logon-ack, or
    // NotConnected → LogonReceived → Active via acceptor path). Drives:
    //   1. Outbound idle: after heartbt_int of no outbound data → emit Heartbeat.
    //   2. Inbound silence: after heartbt_int of no inbound data → emit TestRequest.
    //   3. Unanswered TestRequest: after grace window (1×heartbt_int) without
    //      inbound Heartbeat reply → session_test_request_unanswered → Disconnected.
    //   4. HeartBtInt=0: co_returns immediately (no timers armed — FR-006).
    // Runs under the root cancellation slot; Session::close() cancels it.
    // [feedback_asio_cospawn_total_cancellation_default]: the coroutine resets to
    // enable_total_cancellation.
    [[nodiscard]] asio::awaitable<void> run_liveness_loop() noexcept;

    // ── 005 US3 liveness state (T041) ────────────────────────────────────────
    // last_inbound_steady_ — the effective_clock.steady_now() at which the most
    // recent inbound frame was processed in Active state. Used by the liveness
    // timer loop to compute the inbound-silence elapsed time. Initialised to
    // the epoch; updated on every inbound frame in Active. Single-writer on the
    // per-session strand.
    fixpp::core::steady_time_point last_inbound_steady_;

    // pending_test_req_id_ — the TestReqID of the most recently emitted
    // TestRequest that has not yet been acknowledged by an inbound Heartbeat.
    // Empty when no TestRequest is outstanding. When the liveness timer loop
    // fires an unanswered-TR disconnect, this field holds the unacknowledged ID.
    std::string pending_test_req_id_;

    // unanswered_tr_ — set to true when a TestRequest has been emitted and the
    // grace window (1×HeartBtInt) has elapsed without a Heartbeat reply.
    // When true the liveness loop transitions the session to Disconnected with
    // session_test_request_unanswered (error slot 74). Single-writer.
    bool unanswered_tr_ = false;

    // Per-session TestRequest ID counter (FR-010, RC#6). Replaces the prior
    // process-global `static tr_counter` in run_liveness_loop. Single-writer
    // on the session strand; wrap-around at UINT32_MAX is acceptable
    // (within-session uniqueness contract per spec.md §Edge Cases / research.md D-3).
    std::uint32_t next_test_request_id_ = 0;

    // ── US4 / T046 transport surface ─────────────────────────────────────────
    // transport_send_ — two uses:
    //   (1) Pre-live / config-time sync sink: captured from cfg_.transport_send
    //       at open(); used by store_then_emit() when live_transport_ptr_()
    //       returns null (no live transport attached yet).
    //   (2) Gap-fill/resend-replay transmit-only path (on_inbound_frame, Active
    //       state ResendRequest handling): the fire-and-forget bridge is
    //       acceptable there (errors are synchronously detected and disconnect).
    // For normal outbound frames, store_then_emit() uses live_transport_ptr_()
    // for a direct co_await async_write (FQ-1, gate-b/r1).
    // Always called from the session strand (single-writer, no races).
    std::function<void(std::span<const std::byte>)> transport_send_;

    // 014 T010 — live reconnected transport (owned by Session after a successful
    // drive_reconnect_attempt). Null until the first successful reconnect.
    // The type is forward-declared in this header; the destructor is
    // instantiated in session.cpp where the full Transport definition is visible.
    // [data-model §E-1 step 8; contracts C1; FR-001]
    // shared_ptr (015 /simplify Q-1): the rebound transport_send_ detached write
    // captures a keepalive copy so an in-flight write outlives a Session freed by
    // Engine::stop()'s registry clear (the writes are not in the join counter).
    std::shared_ptr<fixpp::transport::Transport> reconnected_transport_;

    // 015 T011 — live accepted transport (owned by Session after a successful
    // attach_accepted_transport call). Null until the engine's accept loop
    // attaches a peer. shared_ptr per 015 /simplify Q-1 (see reconnected_transport_).
    // [data-model §E-2; T011; contracts C1 step 5]
    std::shared_ptr<fixpp::transport::Transport> accepted_transport_;

    // 014 T015 — live peer identity from the most recent successful reconnect
    // handshake. Stored by install_reconnected_transport (step 8) and consumed
    // by arm (1-live) in the LogonSent→Active Logon-ack authorization
    // guard (session.cpp:1864-1906), which reset()s it after authorizing. Nullopt
    // until the first successful reconnect; each successful reconnect overwrites
    // it and the guard reset()s it on consume, so a stale identity from a prior
    // session is never re-authorized. [data-model §E-2; contracts C2; FR-006]
    // peer_identity is transitively available via session_config.hpp →
    // compid_authorization_policy.hpp → peer_identity.hpp.
    std::optional<fixpp::tls::peer_identity> live_peer_id_;

    // Outbound seqnum is managed exclusively by seqnum_mgr_ (RC#A gate-b/r1-green).
    // Use seqnum_mgr_.peek_outbound() to read; seqnum_mgr_.assign_outbound() to advance.
    // The bare next_outbound_seq_ field was removed to prevent split-brain divergence
    // between admin paths and Session::send. [005 data-model.md:30 E3; 009 spec.md FR-001(a)]

    // logout_confirmed_: set to true when an inbound Logout is received while
    // in LogoutSent state (the peer is confirming our Logout). The
    // run_logout_phase1 coroutine polls this to determine whether the
    // graceful-close completes normally or times out. Single-writer on strand.
    bool logout_confirmed_ = false;

    // 024 T013 — logout_seen_: set to true at EXACTLY two Logout-specific sites:
    //   (a) run_logout_phase1(): local graceful-Logout-sent path
    //       (session.cpp, before record_state_transition_(LogoutSent)).
    //   (b) on_inbound_frame() Active branch: inbound peer-Logout 35=5 receipt.
    // Used by the teardown reset predicate in close() (T014).
    //
    // NOT derived from onLogout_fired_ — that flag fires on ANY Active→!Active
    // transition (including abnormal terminal close with no Logout), so it is a
    // "left-Active" predicate, not a "logout-seen" predicate. Keying the teardown
    // reset on onLogout_fired_ would collapse reset_on_logout into reset_on_disconnect,
    // contradicting C4.2. [contracts/reset-knobs.md C3.1; plan.md Gate A note (b)]
    // Additive POD; no new include → no std::mutex in the awaitable closure [const §XV.9].
    bool logout_seen_ = false;

    // 024 T014 — teardown_reset_done_: single-fire guard for the teardown reset in
    // close(). Prevents a logout+disconnect double-trigger (graceful Logout → timeout
    // → terminal close path) from calling store_->reset() twice. FileStore::reset() is
    // non-idempotent I/O (full atomic-rename + fdatasync + dir-fsync per call).
    // [contracts/reset-knobs.md C5.1; plan.md Gate A note (e)]
    // Additive POD; no new include [const §XV.9].
    bool teardown_reset_done_ = false;

    // ── 013 Phase 3 T023/T026 — ReconnectFsm driver ─────────────────────────
    // Owns the AwaitingResend transient bool (NOT a new fsm_state per D-1),
    // the ResendState, and FR-001..FR-016 recovery state. Constructed from
    // cfg_.transport_factory_override (non-owning raw ptr), cfg_.reconnect_policy,
    // cfg_.heartbeat_interval, and cfg_.logout_disconnect_timeout_ms.
    // Session::on_inbound_frame delegates state tracking to reconnect_fsm_;
    // the ResendRequest emit remains inline (has access to seqnum_mgr_ + store).
    // [data-model §E-1; contracts/reconnect_fsm.hpp]
    ReconnectFsm reconnect_fsm_;

    // last_outbound_steady_: monotonic time of the last outbound frame sent.
    // Seeded at open() → updated on every store_then_emit() call.
    // Used by the liveness loop to detect outbound idle → Heartbeat (T018 Cell A).
    fixpp::core::steady_time_point last_outbound_steady_;

    // FQ-A (gate-b/r2) — single serialized live-outbound write gate.
    // Held ACROSS each live async_write (acquire before write, release after
    // write completes). Ensures at most one async_write is ever in-flight on
    // the live Transport, satisfying transport.hpp:47-50 ≤1-in-flight contract.
    // Every live emit site (store_then_emit direct path, ResendRequest replay,
    // liveness HB/TR) acquires this gate before submitting to the transport.
    // Must be drained (cancel_and_drain()) in Session::close() AFTER root
    // cancellation fires, before seqnum_mgr_ drain, to satisfy the async_mutex
    // destructor's not_locked precondition. [transport.hpp:47-50; FQ-A D-6]
    fixpp::sync::async_mutex write_gate_;

    // FQ-A (gate-b/r2) — liveness-loop lifetime counter.
    // Initialized to 0 at construction. The spawn sites increment it
    // synchronously BEFORE co_spawn so the counter is > 0 the instant a
    // detached liveness frame exists; the coroutine body owns only the
    // matching decrement via its RAII guard at exit.
    // Session::close() polls until it reaches 0 AFTER firing root_cancel_, so
    // that registry_.clear() cannot destroy the Session while the liveness
    // coroutine is still running and touching Session members / the live transport.
    // Using a shared_ptr avoids async_mutex destructor precondition issues when
    // tests destroy a Session without calling close() (e.g., after verifying
    // FSM state but before explicit teardown).
    // [feedback_detached_cospawn_write_not_in_join_counter; FQ-A D-6 F3/F4]
    std::shared_ptr<std::atomic<int>> liveness_counter_{std::make_shared<std::atomic<int>>(0)};

    // store_then_emit: store(outbound) BEFORE transport_send (I-3).
    // stamped_seq: the MsgSeqNum already written into `frame` by the builder — passed
    //   explicitly since next_outbound_seq_ is removed (RC#A gate-b/r1-green unification).
    // Returns ok on success; propagates store errors per I-07; propagates transport
    //   throws as dispatch_aborted (RC#B gate-b/r1-green transport error surface).
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store_then_emit(
        seqnum_t stamped_seq, std::span<const std::byte> frame) noexcept;

    // apply_inbound_sequence_reset: apply an inbound SequenceReset(35=4)
    // NewSeqNo(36) to the expected-inbound counter (S-023; FIX-SL §4.8 /
    // QuickFIX Session::nextSequenceReset arms):
    //   new_seqno > expected  → set_next_inbound (advance past gap / hard-reset)
    //   new_seqno < expected  → Reject(RefTagID=36, SessionRejectReason=5)
    //   new_seqno == expected → no-op
    //   new_seqno == 0 (absent/invalid) → no-op
    // Shared by both GapFill (123=Y) and Reset (123=N/absent) modes; the caller
    // places each mode relative to the seqnum gate (Reset bypasses it).
    // ref_seq is the rejected message's MsgSeqNum (for the Reject RefSeqNum).
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> apply_inbound_sequence_reset(
        seqnum_t new_seqno, seqnum_t ref_seq) noexcept;

    // send_impl: the actual send pipeline (frame building + store_then_emit).
    // Called from the noexcept send() wrapper which catches asio::system_error
    // thrown on async cancellation. May throw on store awaitable cancellation.
    // [F5 Round-A drift fix: noexcept-throw trap separation]
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> send_impl(
        std::span<const std::byte> app_payload);

    // 015 T016(d) — emit the initial initiator Logon via the admin-builder path
    // (build_logon + assign_outbound + store_then_emit). Extracted from open()'s
    // initiator arm so it can be driven from TWO call sites:
    //   • open() (per-session-direct model, 013/014) — emitted AT open;
    //   • drive_reconnect() (engine lazy-connect model) — emitted POST-connect
    //     once install_reconnected_transport has rebound transport_send_ to the
    //     live sink (connect-then-Logon, FR-003 / E-1a).
    // On any failure (build overflow / seqnum overflow / store-or-transport
    // throw) it transitions the session to Disconnected and returns the error —
    // the caller propagates it. Does NOT perform the LogonSent transition (the
    // caller owns that: open() before the call, install_reconnected_transport
    // before drive_reconnect's call). [data-model §E-1a; T016(d); FR-003/FR-004]
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> emit_initiator_logon_() noexcept;

    // FQ-A (gate-b/r2): returns the live transport as a shared_ptr<Transport>.
    // The keepalive across the async_write co_await ensures the Transport is not
    // freed by registry_.clear() while the write is in-flight (Q-1 UAF fix).
    // Returns nullptr if no live transport is attached yet.
    [[nodiscard]] std::shared_ptr<fixpp::transport::Transport> live_transport_shared_()
        const noexcept;

    // FQ-A (gate-b/r2): one serialized live write. Acquires write_gate_, holds a
    // shared_ptr<Transport> keepalive across the async_write, releases the gate
    // on completion (success or error). Returns dispatch_aborted on:
    //   - write_gate_ acquire cancelled (operation_aborted from cancel_and_drain)
    //   - async_write returns !has_value() (any transport error)
    // NEVER holds the gate across any read — guards write-submit→complete only.
    // [transport.hpp:47-50; FQ-A D-6; feedback_async_mutex_us3_asio_cancel_and_subagent_seams]
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> live_write_serialized_(
        std::span<const std::byte> frame) noexcept;

    // run_logout_phase1: emit Logout frame, then wait for peer Logout-confirm
    // OR clock-bound 2 s timeout (session_logout_timeout, slot 73) under a
    // CHILD cancellation_state. Called from close(graceful) phase 1.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> run_logout_phase1() noexcept;
};

}  // namespace fixpp::session
