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

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include <asio/awaitable.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/post.hpp>

#include <fixpp/core/clock.hpp>           // steady_time_point (T041 US3 liveness)
#include <fixpp/core/error.hpp>          // expected_t
#include <fixpp/core/session_executor.hpp>
#include <fixpp/core/session_local.hpp>
#include <fixpp/core/trace_context.hpp>
#include <fixpp/session/message_store.hpp>  // 008-message-store — MessageStore complete type
                                            // (the unique_ptr<MessageStore> member's
                                            // nested type alias flush_hook_fn requires it).
#include <fixpp/session/session_fsm.hpp>    // 005-session-establishment-fsm — fsm_state enum
#include <fixpp/session/seqnum_manager.hpp> // 005 US2 — SeqnumManager (T031)

namespace fixpp::core { struct EngineConfig; class Clock; }

namespace fixpp::session {

struct SessionConfig;

// graceful: phase 1 (engine-internal FileStore::flush_for_session_close()
// hook once → Logout exchange under a CHILD asio::cancellation_state below
// the root) → phase 2 (root cancellation_type::total). terminal: skip phase 1
// (hook NOT invoked). partial is NOT in the v1.0 surface (N-P1-3).
enum class close_mode : std::uint8_t { graceful = 0, terminal = 1 };

class Session {
public:
    // The ctor pre-conditions a non-null session_arena via the [2d §4.5]
    // resolution chain so session_arena()'s never-null contract holds for
    // the whole session lifetime (I-18). engine/cfg are borrowed and MUST
    // outlive the Session (engine-owned lifetime — [arch §4.4]).
    Session(const fixpp::core::EngineConfig& engine,
            const SessionConfig& cfg) noexcept;

    Session(const Session&)            = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&)                 = delete;
    Session& operator=(Session&&)      = delete;
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
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
        open() noexcept;

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
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
        close(close_mode mode = close_mode::graceful);

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
    [[nodiscard]] const fixpp::core::session_executor& executor() const noexcept {
        return exec_;
    }
    [[nodiscard]] bool is_open() const noexcept { return state_ == lifecycle::open; }

    // ENGINE-INTERNAL (fixpp::session/ + the session_trace_context_of
    // bridge). The session-domain trace context (FR-014/FR-015/I-11): the
    // session_local<trace_context> slot, populated at open() from
    // SessionConfig::initial_trace_context and cleared at close completion
    // (T045). Read through the borrowed stable Session* by
    // current_trace_context — survives cross-thread coroutine resume because
    // it is plain value ownership, NOT thread_local (E7/E8).
    [[nodiscard]] const fixpp::otel::trace_context&
    trace_context_value() const noexcept {
        return trace_slot_.load();
    }

    // The single effective_clock resolved ONCE at open() (FR-005 / I-03):
    // SessionConfig::clock_override ?: EngineConfig::clock, bound to the
    // session lifetime. Every session-scoped consumer reads this; valid only
    // after a successful open().
    [[nodiscard]] const std::shared_ptr<fixpp::core::Clock>&
    effective_clock() const noexcept {
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
    using close_flush_hook =
        std::function<fixpp::core::expected_t<void>()>;
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
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
        on_inbound_frame(std::span<const std::byte> frame) noexcept;

    // User output. Stamps SendingTime(52) from effective_clock, assigns
    // MsgSeqNum(34), Writer::commit, store(seq, committed, outbound) BEFORE
    // transport::async_write (durable-before-transmit, I-3).
    // PLACEHOLDER — body wired per US1/T023 (Phase 3).
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
        send(std::span<const std::byte> app_payload) noexcept;

    // Current FSM state (read-only; single-writer on the per-session strand).
    // PLACEHOLDER — returns NotConnected until Phase 3 wires the FSM field.
    [[nodiscard]] fsm_state state() const noexcept;

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
    template <class F>
    void dispatch_app_callback(F&& f) const {
        asio::post(exec_,
                   [this, g = std::forward<F>(f)]() mutable {
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
                           std::atomic<bool>& flag;
                           ~dispatch_guard() noexcept {
                               flag.store(false, std::memory_order_release);
                           }
                       };
                       const bool prev =
                           in_dispatch_.exchange(true, std::memory_order_acq_rel);
                       assert(!prev &&
                              "concurrent session callback entry: strand "
                              "invariant violated (direct_executor attested "
                              "over a non-serialised executor?)");
                       [[maybe_unused]] dispatch_guard guard{in_dispatch_};
#endif
                       g();
                   });
    }

private:
    const fixpp::core::EngineConfig& engine_;
    const SessionConfig&             cfg_;
    std::pmr::memory_resource*       session_arena_;   // resolved in ctor, never null

    fixpp::core::session_executor    exec_;            // bound at open() (T020)
    std::shared_ptr<fixpp::core::Clock> effective_clock_;  // resolved at open() (T030)
    mutable std::atomic<bool>        in_dispatch_{false};  // debug strand-invariant guard (seam 16)
    fixpp::core::session_local<fixpp::otel::trace_context> trace_slot_;  // [2d §4.6]

    // Lifecycle state for the idempotent three-state close model (I-10);
    // never-opened vs open vs closing vs closed(drained).
    enum class lifecycle : std::uint8_t {
        never_opened = 0, open = 1, closing = 2, closed_drained = 3,
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
    std::unique_ptr<MessageStore>       store_;

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
    std::shared_ptr<std::optional<fixpp::core::expected_t<void>>>
        close_result_;

    // ── 005-session-establishment-fsm FSM state field (T016) ─────────────────
    // Current FSM state; single-writer on the per-session strand (data-model E2).
    // Initialised to NotConnected; transitions wired per user story (Phase 3–6).
    // Separate from the lifecycle state_ above (that tracks open/close lifecycle;
    // this tracks the FIX protocol state).
    fsm_state fsm_state_ = fsm_state::NotConnected;

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
    fixpp::core::steady_time_point last_inbound_steady_{};

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
};

}  // namespace fixpp::session
