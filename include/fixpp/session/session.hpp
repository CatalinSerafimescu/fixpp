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
#include <fixpp/core/detail/asio_recycler_config.hpp>
#include <fixpp/core/error.hpp>  // expected_t
#include <fixpp/core/session_executor.hpp>
#include <fixpp/core/session_local.hpp>
#include <fixpp/core/trace_context.hpp>
// 066-dict-backed-inbound-parse T002: inbound_tv_ is a
// std::optional<table_view> Session MEMBER (not a unique_ptr to an
// incomplete type like validator_), so table_view must be complete here.
// [const §XV.9] guard confirmed clean — table_view.hpp has a deliberately
// minimal include graph (no mutex, no heavy asio; see its own file header
// and validator.hpp's own identical §XV.9 confirmation) — the check_no_std_mutex
// _corpus Tier-1 gate (tests/sync/CMakeLists.txt) covers this file.
#include <fixpp/dict/table_view.hpp>
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
#include <fixpp/wire/framer.hpp>            // 093 E-2: pmr_carry_buffer, the carry open() allocates

namespace fixpp::core {
struct EngineConfig;
class Clock;
}  // namespace fixpp::core

namespace fixpp::transport {
class Transport;
struct handshake_result;
}  // namespace fixpp::transport

namespace fixpp::dict {
// 033 T006: non-owning handle for the engine-built application-version registry.
// Forward-declared to keep session.hpp free of version_registry.hpp's includes
// ([const §XV.9] — full def in session.cpp via version_registry.hpp include).
class version_registry;
}  // namespace fixpp::dict

namespace fixpp::wire {
// 041-validation-gate-wiring T014: forward-declare the validator so session.hpp
// can hold a unique_ptr<dictionary_driven_validator> without including
// validator.hpp here. Including validator.hpp in session.hpp would pull
// dict/table_view.hpp and potentially std::vector into the awaitable closure,
// risking a [const §XV.9] violation. Full definition in session.cpp via
// #include <fixpp/wire/validator.hpp>.
class dictionary_driven_validator;
}  // namespace fixpp::wire

namespace fixpp::session::detail {
// Forward-declare FrameHeader so session.hpp can reference it in the
// validate_inbound_or_reject_ private helper declaration without including
// scan_frame_header.hpp (an internal non-public header) here.
// Full definition in src/session/scan_frame_header.hpp, included by session.cpp.
// [const §XV.9] — forward-decl keeps the awaitable closure mutex-free.
struct FrameHeader;
}  // namespace fixpp::session::detail

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
    // 033 T006: app_version_registry is a non-owning nullable handle to the
    // engine-built dict::version_registry (data-model E2 serviceability check at
    // inbound FIXT Logon). Default null = no registry (test construction path;
    // FIX.4.x sessions that never consult the registry). The Engine passes
    // &engine.app_version_registry_ — its lifetime spans all Sessions.
    // [033 data-model.md E2; research R2 threading; T006]
    Session(const fixpp::core::EngineConfig& engine, const SessionConfig& cfg,
            const fixpp::dict::version_registry* app_version_registry = nullptr);

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
    // security_profile / incompatible combo / a SenderCompID, TargetCompID,
    // BeginString or configured RefMsgType(372) holding a byte < 0x20 (incl.
    // SOH \x01) or '=' (0x3D) (fixpp#452, FR-012/FR-013) →
    // invalid_session_config (FR-018); (5) reject a second open() →
    // session_already_open (slot 51).
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> open() noexcept;

    // Two-phase close ([2d §4.7] close() declaration frozen shape). Idempotent
    // THREE-STATE model (I-10): already-closing → SAME in-flight awaitable,
    // no error; never-opened / already-closed(drained) →
    // error::session_already_closed, with no side effects.
    // OD-29 (093 — terminal close escalates a graceful one): the already-closing
    // join has no side effects unless a terminal close arrives while a graceful
    // one is in flight. Then the terminal close ends the graceful close's grace
    // wait, or keeps its phase 1 (the Logout exchange) from starting, so the
    // transport closes now; both callers get the in-flight result. A graceful
    // close on a closing session, and close()'s wait for a reset unit, are
    // unchanged. graceful
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

    // 070-fix44-closeout S-030 (FR-007): the peer's advertised MaxMessageSize(383)
    // read from its inbound Logon (nullopt ⇒ peer advertised none). Observability
    // only in this feature — captured to inform the deferred outbound-respect
    // behavior; there is no hard outbound guard here (see spec Assumptions).
    [[nodiscard]] std::optional<std::uint32_t> peer_max_message_size() const noexcept {
        return peer_advertised_max_message_size_;
    }

    // 093-inbound-frame-dispositions (data-model E-4; FR-003): the number of garbled
    // inbound regions this session has disregarded, Framer garbles and frames whose
    // third field is not MsgType(35) alike. Monotonic. A relaxed load, so any thread
    // may call it. It is not ordered with the rest of the session's state: a caller
    // that needs the value to reflect a given frame synchronises by other means.
    [[nodiscard]] std::uint64_t garbled_frame_count() const noexcept {
        return garbled_frames_.load(std::memory_order_relaxed);
    }

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

    // 033 T008 / data-model E2 — negotiated FIXT application version profile.
    // Strand-confined accessor; valid (non-Unknown) only after a FIXT session has
    // reached Active and set negotiated_appl_version_ at inbound-Logon parse.
    // Returns {session=vt11, default_appl=negotiated_appl_version_, ...} for FIXT
    // sessions; returns {session=Unknown, default_appl=Unknown, ...} for FIX.4.x
    // sessions (negotiated_appl_version_ stays Unknown — no FIXT negotiation).
    // Return by value (dict::version_profile is 4 bytes, trivially copyable) —
    // no [[clang::lifetimebound]] needed.
    // Reachable from fromApp via Engine::lookup(SessionId)→shared_ptr<Session>.
    // [033 data-model.md E2; FR-005; SC-006/W5; INV-FIXT-2]
    [[nodiscard]] fixpp::dict::version_profile negotiated_version_profile() const noexcept;

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
            (void)this;  // read only by the debug-only guard below
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

    // fixpp#511: test-only access to private state goes through ONE named friend,
    // defined in tests/support/session_test_access.hpp (never installed). The
    // friend is unconditional on purpose: a member gated behind a test macro would
    // make a test TU's Session a different class from the library's, an ODR
    // violation (ill-formed, no diagnostic required).
    friend struct session_test_access;

    // 093-inbound-frame-dispositions (data-model E-11, plan OD-12): the engine's one
    // named seam into a Session's private state, for run_read_pump and the accept
    // loop. Defined in src/session/session_engine_access.hpp, which is never
    // installed. Production code, not test access, and unconditional for the same
    // ODR reason as the friend above. Engine hooks go there, never into this
    // class's public surface.
    friend struct session_engine_access;

    // 015 T011 — Engine-internal acceptor attach primitive.
    // Called by the engine's run_accept_loop STRICTLY-BEFORE the first
    // on_inbound_frame (happens-before invariant Gate A New-1 / E-4).
    // Actions: set live_peer_id_, rebind transport_send_, take ownership.
    // Does NOT transition the FSM. NOT install_reconnected_transport (which
    // re-enters LogonSent — initiator-only; Gate A Codex-2).
    // Declared public for accessibility from engine.cpp's run_accept_loop.
    // By convention only the engine's accept loop should call this method.
    // §XV.9: handshake_result forward-declared above; full def in session.cpp.
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
    //            success (matching session.cpp's existing store_->reset() logged-then-proceed
    //            pattern; C2.6 zero-regression clause; data-model §"Durable reset helper").
    // Logic lives in session.cpp to keep this enum include-free ([const §XV.9]).
    // [024 data-model §"Durable reset helper"; C2.6; research D2]
    enum class reset_disposition : std::uint8_t { fatal = 0, logged = 1 };

    // reset_seqnums_to_one_durable — shared durable-reset helper (T003).
    // Body: co_await seqnum_mgr_.reset_to_one() then co_await store_->reset().
    // store_ null-checked (matching session.cpp's existing `if (store_)` null-guard pattern).
    // Disposition controls store-failure handling (see reset_disposition above).
    // NOT wired to any trigger in this slice (T002/T003 foundational only);
    // wired in T007 (initiator Logon), T008 (acceptor Logon), T014 (teardown).
    // [024 data-model §"Durable reset helper"; C2.6; research D2]
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset_seqnums_to_one_durable(
        reset_disposition disposition) noexcept;  // wired in T007/T008/T014

    // 070-fix44-closeout S-029: refuse an inbound Logon on a TestMessageIndicator(464)
    // posture mismatch (or malformed 464). Emits a Logout(35=5) carrying reason_text
    // (fire toAdmin → assign_outbound → store_then_emit), then transitions to
    // Disconnected — the session never reaches Active. Mirrors the Logon-time
    // Logout+disconnect disposition (session.cpp's 070-fix44-closeout S-029 posture-mismatch call
    // site). Called from both the acceptor inbound-Logon and the initiator inbound-Logon-ack paths.
    // [FR-002; D-F]
    // arm: the calling Logon arm's expected state (093 plan OD-26), passed to the
    // Logout's store_then_emit and to the Disconnected write after it.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> refuse_logon_with_logout_(
        std::string_view reason_text, fsm_state arm) noexcept;

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

    // 070-fix44-closeout S-030 (FR-007): peer's advertised MaxMessageSize(383),
    // parsed from its inbound Logon. Observability only (no outbound guard here).
    std::optional<std::uint32_t> peer_advertised_max_message_size_;

    // 093-inbound-frame-dispositions (data-model E-2): the session's one inbound
    // limit L, set by open() from inbound_limit_for(cfg_) (src/session/inbound_limit.hpp).
    // 0 until open() runs.
    std::uint32_t inbound_limit_ = 0;

    // 093 (contract C-3 I-2): the spill witness, the upstream of the parse buffer's and
    // the carry's monotonic resources. It counts every request that reaches it (every
    // allocation past the buffer or the carry block), then forwards it to
    // fixpp::detail::arena_upstream(): null on every lane except MSVC's debug STL, where
    // the request is served from the heap. mutable: validate_inbound_ is const.
    class spill_witness final : public std::pmr::memory_resource {
    public:
        [[nodiscard]] std::uint64_t spills() const noexcept {
            return spills_.load(std::memory_order_relaxed);
        }

    private:
        void* do_allocate(std::size_t bytes, std::size_t alignment) override;
        void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override;
        [[nodiscard]] bool do_is_equal(
            std::pmr::memory_resource const& other) const noexcept override;
        std::atomic<std::uint64_t> spills_{0};
    };
    mutable spill_witness parse_spill_witness_;

    // 093 (data-model E-2): the per-session parse buffer B(L), allocated once by open()
    // from session_arena_ (inbound_parse_block_) and released at destruction. Every
    // inbound parse builds a fresh monotonic_buffer_resource over inbound_parse_buf_,
    // upstream parse_spill_witness_, with inbound_entry_cap_ = N(L) as its entry cap.
    // The span is the whole block except under session_test_access's shrink.
    std::span<std::byte> inbound_parse_block_;
    std::span<std::byte> inbound_parse_buf_;
    std::size_t inbound_entry_cap_ = 0;

    // 093 (data-model E-2, plan OD-13): the read pump's carry, allocated once by open()
    // and borrowed by run_read_pump through session_engine_access. One block from
    // cfg_.framer_carry_arena (carry_arena_), a monotonic resource over exactly that
    // block (upstream parse_spill_witness_), and the carry over that resource, whose
    // noexcept constructor's reserve the block serves. ~Session releases them in
    // reverse order. 093 supersedes the pump-local carry of 015 T015.
    std::pmr::memory_resource* carry_arena_ = nullptr;
    std::span<std::byte> carry_block_;
    std::optional<std::pmr::monotonic_buffer_resource> carry_resource_;
    std::optional<fixpp::wire::pmr_carry_buffer> carry_;

    // 093 (data-model E-4): the count garbled_frame_count() reads. Written only by
    // note_garbles_, on the session strand, with relaxed ordering.
    // Placement condition: the count is monotonic per SessionId for the engine's life
    // only while the engine builds one Session per SessionEntry per Engine::start().
    // Re-derive before relying on it: read what follows the run_read_pump call in
    // run_accept_loop and in run_connect_loop (src/session/engine.cpp), and the
    // lifecycle note above `class Engine` (include/fixpp/session/engine.hpp). If a
    // Session ever serves a second connection, this placement must be re-derived.
    std::atomic<std::uint64_t> garbled_frames_{0};

    // 093 (data-model E-12): the logger, resolved once at open() from
    // cfg_.logger_override, else the engine's; null when neither is set. And the
    // garble log's rate state: whether a garble record has been written, the earliest
    // time of the next one on effective_clock_, and the regions of rate-suppressed
    // summaries since the last record (plan OD-21).
    std::shared_ptr<fixpp::log::Logger> logger_;
    bool garble_logged_ = false;
    fixpp::core::steady_time_point garble_log_next_;
    std::uint64_t garbles_unlogged_ = 0;

    // note_garbles_ — 093 (data-model E-4, E-12): accounts one summary of disregarded
    // inbound bytes. Reached from the engine through session_engine_access (the read
    // pump after a feed, the accept loop once after open()) and from contract C-2 step
    // 1. Adds `regions` to the count; when regions > 0 it emits one
    // session_event_garbled_frame and writes a garble log record, at most one per
    // max(HeartBtInt, 1 s). Session strand only.
    void note_garbles_(fixpp::wire::garble_summary const& g) noexcept;

    // 093-inbound-frame-dispositions (FR-013, data-model E-12): a frame over L was
    // refused at framing once this Session exists. Writes one FIXPP_SLOG record with
    // the failure kind and L; the read pump then closes the session terminally.
    // Reached through session_engine_access.
    void note_frame_too_large_() noexcept;

    // 093 (data-model E-6): true once the session has first entered Active. Set by
    // record_state_transition_ on that entry, whether or not an application is attached
    // (onLogon_fired_ latches only with one), and never cleared. The read pump stops
    // testing the establishment deadline once it is set. Written and read on the
    // session strand.
    // Placement condition: as garbled_frames_ above; a Session reused for a second
    // connection would need this reset when the transport is installed.
    bool reached_active_ = false;

    // note_establishment_timeout_ — 093 (data-model E-5, E-12; contract C-4): the
    // establishment deadline passed before the first Active. Emits
    // session_event_establishment_timeout and writes one log record carrying
    // logon_timeout_ms. Reached from the read pump through session_engine_access, just
    // before it closes the session. Session strand only.
    void note_establishment_timeout_() noexcept;

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

    // ── 029-persistent-seqnum-hydrate strand-confined flags (T005) ───────────
    //
    // hydrated_: true once ensure_hydrated_() has completed successfully (one-shot
    //   latch; set only after both reads + SeqnumManager::hydrate() succeed — D-9 /
    //   INV-H6 / C2.1). A transient read failure leaves it false so the next
    //   reconnect retries.
    // hydrating_: re-entrancy guard for ensure_hydrated_(); cleared on failure so a
    //   failed hydrate is not sticky (C2.1 / D-9).
    // store_is_persistent_: captured ONCE at open() inside the if(cfg_.store_factory)
    //   branch from cfg_.store_factory->yields_persistent_store() (C2.2 / D-10).
    //   Null-store path leaves it false (default). When false, ensure_hydrated_()
    //   skips the read and persist_inbound_advance_() skips the write (INV-H4).
    // All three are strand-confined (single-writer on the per-session strand).
    // Additive POD bools; no new include — [const §XV.9] closure unaffected.
    bool hydrated_ = false;
    bool hydrating_ = false;
    bool store_is_persistent_ = false;

    // 032-initiator-reset-outbound-advance: latched emit-time fact that fixpp
    // actually emitted 141=Y in its initiator Logon (= initr_reset_seqnum at
    // session.cpp's initr_reset_seqnum). Unconditionally assigned on every initiator-Logon emit
    // (overwrites false when no 141=Y → stale-across-reconnect is structurally
    // impossible). Consumed one-shot on the peer_ack_sent_reset_flag arm.
    // Strand-confined; additive POD bool; no new include. [contract C4, data-model]
    bool own_logon_sent_reset_flag_ = false;

    // ── 033 T008 / data-model E2 — negotiated FIXT application version ────────
    // Set once at inbound FIXT Logon parse (after 1137 resolves and is verified
    // serviceable — FR-004/FR-004a). Strand-confined (single-writer on the session
    // strand, [const §XI.4]). Stays Unknown for FIX.4.x sessions throughout their
    // lifetime. Exposed via negotiated_version_profile() (public).
    // [033 data-model.md E2; INV-FIXT-2]
    fixpp::dict::application_version negotiated_appl_version_ =
        fixpp::dict::application_version::Unknown;

    // ── 033 T006 / data-model E2 — engine-built application version registry ──
    // Non-owning nullable handle to the Engine's engine-lifetime version_registry.
    // Null when Session is constructed without an engine (test paths, FIX.4.x
    // sessions that never need serviceability checks).
    // Captured in the Session ctor; never swapped mid-session.
    // The full type is forward-declared above; version_registry.hpp is included
    // only in session.cpp ([const §XV.9] — avoids dragging any dict internals
    // into the awaitable closure). [033 data-model.md E2; research R2 threading]
    const fixpp::dict::version_registry* app_version_registry_ = nullptr;

    // ── 041-validation-gate-wiring T014 — opt-in inbound validator ───────────
    //
    // Built once at open() when cfg_.validate_inbound_messages == true and
    // cfg_.dictionary is non-null (the fail-closed config gate T004 in
    // Engine::register_session guarantees this invariant for engine-managed
    // sessions; open() also guards directly so raw-Session construction paths
    // are covered). Null when validation is disabled (default path) — no
    // validator construction/invocation on the default inbound path (FR-002 /
    // SC-005 / [const §XV.1]).
    //
    // Held as unique_ptr to keep validator.hpp (which includes dict/table_view.hpp
    // and owns std::vector-backed tables) OUT of the awaitable closure via the
    // forward-declaration above ([const §XV.9]). The out-of-line dtor in
    // session.cpp provides the delete expression where the full type is visible.
    // [041 T014; data-model E-2; research R-2; [const §VIII.5]/§XV.1]
    std::unique_ptr<fixpp::wire::dictionary_driven_validator> validator_;

    // ── 066-dict-backed-inbound-parse T002 — inbound dict-membership table ──
    //
    // Resolved ONCE in open(), immediately after the non-null-dictionary guard
    // (session.cpp's inbound_tv_ assignment). Mirrors the validator's owned table_view (above /
    // session.cpp's validator_ assignment).
    // Stable-address referent (data-model.md "Session inbound table_view"): the
    // dict-backed Parser ctor stores std::addressof of the POINTEE (parser.hpp),
    // so the pointee's address must not move for the session lifetime — a
    // heap-owned object held by shared_ptr and never reseated per message
    // satisfies this at least as strongly as the former std::optional member did
    // (the address is now independent of the Session object itself).
    //
    // fixpp#215 item 1 (Option C) — shared_ptr, not std::optional-by-value: the
    // view is built by whoever gets there FIRST and then SHARED, instead of
    // every consumer walking the same Dictionary again. open() adopts
    // cfg_.dict_snapshot's table (via fixpp::dict::shared_dictionary_view, which
    // shares the table's own owner, not the snapshot — fixpp#495 D-4) when
    // the config supplies one (the C-ABI path, which needs the same view for
    // its outbound commit path) and otherwise builds one itself. Either way the
    // Session owns a strong reference for its whole lifetime, so the pointee
    // outlives every Parser built over it.
    //
    // fixpp#495: the owner object of every view parse_and_dispatch_ hands an
    // application — the OWNER-OBJECT RULE at Parser's owned-route constructor
    // (parser.hpp; note §3.1) applies. This site's fact: written only in open(),
    // before `state_ = lifecycle::open`, after which open()'s first check makes
    // further writes unreachable. Re-check with
    // `grep -n "inbound_tv_ *=" src/session/session.cpp`.
    //
    // Invariant: open() hard-fails (invalid_session_config) when
    // cfg_.dictionary is null, BEFORE this member is built — so
    // inbound_tv_ is GUARANTEED non-null whenever parse_and_dispatch_
    // runs (both callers, fire_to_admin_ and the receive loop, only run on a
    // successfully-opened session). open() is single-invocation per session
    // (state_ != never_opened rejects a second call) and reconnection is
    // driven by ReconnectFsm::drive_reconnect_attempt(), which does NOT
    // re-invoke open() — so no rebuild-during-parse hazard exists.
    std::shared_ptr<const fixpp::dict::table_view> inbound_tv_;

    // ── 029-persistent-seqnum-hydrate awaitable declarations ─────────────────
    //
    // ensure_hydrated_(): one-shot cold-open hydration gate (C2.1–C2.7).
    //   apply_inbound_seed: true → apply *in_r from the store; false → keep next_inbound
    //   at construction value seqnum_min (withheld on reset-Logon paths — RC-1 / C2.4).
    //   force: when true, bypass the one-shot hydrated_ latch and re-read the store.
    //   Used by refresh_on_logon (025) at each reconnect to adopt the store's counters
    //   unconditionally (store-wins, up or down — INV-RoL-4).  Default false preserves
    //   the pre-025 byte-identical cold-open behaviour.
    // persist_inbound_advance_(): site-keyed durable inbound +1, invoked after each
    //   delivering callback at every check_inbound-success site (C3).
    // 093 plan OD-26: `arm` is a Logon arm caller's expected state, for the Disconnected
    // write that follows a failed store operation (disconnect_unless_superseded_).
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> ensure_hydrated_(
        bool apply_inbound_seed, bool force = false,
        std::optional<fsm_state> arm = std::nullopt) noexcept;
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> persist_inbound_advance_(
        std::optional<fsm_state> arm = std::nullopt) noexcept;
    // consume_rejected_seqnum_(): fixpp#423 — an in-sequence message answered by a
    //   Reject before the seqnum gate consumes its MsgSeqNum (advance + persist).
    // close_filled_resend_gap_(): exit AwaitingResend once next_inbound passes the gap end.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> consume_rejected_seqnum_(
        seqnum_t seq, std::string_view msg_type) noexcept;
    void close_filled_resend_gap_() noexcept;

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

    // parse_and_dispatch_'s value: whether the callback ran. Private; it never
    // leaves the session. [092 data-model E-3]
    enum class dispatch_outcome : std::uint8_t {
        dispatched,    // the callback ran and returned normally
        parse_failed,  // the re-frame or the parse failed; the callback did not run
    };

    // parse_and_dispatch_ — dedup helper: build a stack parse arena, re-frame +
    // parse `frame` into a MessageView<Index>, build the SessionId, open a
    // callback_dispatch_scope, and return invoke_callback_safe(cb(view, sid)).
    // On parse failure (Framer or Parser): returns dispatch_outcome::parse_failed and
    // the callback does not run. A call over bytes received from the peer (a late
    // inbound site) closes the session on it through close_on_late_parse_failure_; a
    // call over a frame fixpp built ignores it. Superseded decision: a parse failure
    // used to return success ("skip, not fatal"); 092-garbled-frame-reject contract
    // C-6 (owner ruling O-2) replaced it.
    // `arena_bytes` is explicit so each call site documents its sizing choice.
    // [const §VIII.5] (stack-only, no heap); [019-app-callbacks T014/T011/T013/T016;
    // 092 data-model E-3, contract C-6]
    template <class CB>
    [[nodiscard]] fixpp::core::expected_t<dispatch_outcome> parse_and_dispatch_(
        std::span<const std::byte> frame, std::size_t arena_bytes, CB&& cb) noexcept;

    // 093-inbound-frame-dispositions (data-model E-2, contract C-3): the overload every
    // late inbound site calls. It parses over the session's parse buffer B(L) instead
    // of a stack arena, under the entry cap N(L), reserving the frame's entries up
    // front; the admin and outbound sites keep the overload above (C-3 I-6). The tag
    // keeps the two overloads apart at each call site.
    struct inbound_parse_t {
        explicit inbound_parse_t() = default;
    };
    static constexpr inbound_parse_t inbound_parse_buffer{};
    template <class CB>
    [[nodiscard]] fixpp::core::expected_t<dispatch_outcome> parse_and_dispatch_(
        std::span<const std::byte> frame, inbound_parse_t /*tag*/, CB&& cb) noexcept;

    // close_on_late_parse_failure_ — 092-garbled-frame-reject contract C-6: the one
    // action every late inbound parse site takes when its parse fails (a
    // parse_and_dispatch_ parse_failed outcome, or a validate_inbound_ parse_failed
    // outcome). Closes the session terminally and sends nothing; the caller returns
    // its result without invoking the receive callback. It does not undo what ran
    // at the call site before the parse.
    // [092 data-model E-3; contract C-6; research R-4; spec FR-016]
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    close_on_late_parse_failure_() noexcept;

    // emit_session_reject_ — dedup helper: build + emit a session Reject(35=3) with
    // RefTagID=0, SessionRejectReason=3, and Disconnected-on-failure error handling
    // for both assign_outbound and store_then_emit.  ref_seq is the MsgSeqNum of the
    // offending inbound message; ref_msg_type is its MsgType tag.
    // Returns expected_t<void>{} on success (or build failure); returns unexpected on
    // assign_outbound / store_then_emit failure (caller should propagate).
    // [019-app-callbacks T011/T016; FR-005; D4; INV-4]
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> emit_session_reject_(
        seqnum_t ref_seq, std::string_view ref_msg_type) noexcept;

    // 041-validation-gate-wiring T010 — overload that carries a mapped
    // SessionRejectReason(373) and an optional RefTagID(371) through to the
    // Reject builder. validate() returns a
    // wire_* error; the caller maps it via wire_error_to_session_reject_reason()
    // and passes the result here. ref_tag_id == 0 → 371 omitted.
    // 092-garbled-frame-reject (research R-5): builds via build_reject_with_text and
    // carries `text` as Text(58); an empty text (the default) omits 58, so the frame
    // is byte-identical to build_reject's.
    // [041 T010; data-model E-4; RC-C; 092 R-5]
    // arm: a Logon arm caller's expected state (093 plan OD-26), passed to
    // store_then_emit and to the Disconnected write after a failed emit.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> emit_session_reject_(
        seqnum_t ref_seq, std::string_view ref_msg_type, int reason, int ref_tag_id = 0,
        std::string_view text = {}, std::optional<fsm_state> arm = std::nullopt) noexcept;

    // dispose_unparseable_ — 092-garbled-frame-reject (fixpp#507) contract C-2: the
    // disposition of a frame the header scan could not read (hdr.fault is set). Each
    // on_inbound_frame state arm calls it only on that branch, right after its scan
    // and before any guard, then returns. It reads only hdr.fault,
    // hdr.fault_length_tag, hdr.msg_type_is_third, hdr.fault_ref_seq_num and
    // hdr.fault_ref_msg_type (contract C-3 I-1). `state` is the arm's FSM state.
    // [092 contract C-1 step 3, C-2; data-model E-2; research R-3]
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> dispose_unparseable_(
        fixpp::session::detail::FrameHeader const& hdr, fsm_state state) noexcept;

    // validate_inbound_ — synchronous dedup helper (041 simplify-triage FIX-1/FIX-2 +
    // per-message coroutine-frame alloc fix):
    // Parse `frame` over the session's parse buffer B(L) (093, data-model E-2), run
    // validator_->validate(), and return the rejection decision WITHOUT emitting.
    //
    // Returns validate_outcome::pass when validation passes — caller continues
    // normally.
    // Returns validate_outcome::reject, with the RejectDecision, when a violation is
    // found; caller must co_await emit_session_reject_(parse_seqnum(hdr.msg_seq_num),
    //   hdr.msg_type, v.reject.reason, v.reject.ref_tag_id) inline.
    // Returns validate_outcome::parse_failed when the re-frame or the parse fails;
    // the caller closes the session through close_on_late_parse_failure_. Superseded
    // decision: that case used to return nullopt, read as "validation passes";
    // 092-garbled-frame-reject contract C-6 (owner ruling O-2) replaced it.
    //
    // SYNCHRONOUS — no co_await, no sub-coroutine frame allocated on the pass path.
    // PRECONDITION: cfg_.validate_inbound_messages && validator_ must hold
    // (callers guard this — do NOT call without the guard).
    // PRECONDITION: hdr.msg_type != "3" && hdr.msg_type != "5" (3/5 exemption
    // must be checked by the caller before calling).
    // [041 T014; data-model E-4; SC-005 zero-cost default-path; const §VIII.5;
    // 092 data-model E-3]
    struct RejectDecision {
        int reason = 0;
        int ref_tag_id = 0;
    };
    enum class validate_outcome : std::uint8_t { pass, reject, parse_failed };
    struct InboundValidation {
        validate_outcome outcome = validate_outcome::pass;
        RejectDecision reject{};  // meaningful only when outcome == reject
    };
    [[nodiscard]] InboundValidation validate_inbound_(
        std::span<const std::byte> frame,
        fixpp::session::detail::FrameHeader const& hdr) const noexcept;

    // fixpp#544 (B35 Phase 4, `.specify/544-hot-path-zero-alloc.md` §2.2): on_inbound_frame's
    // split. on_inbound_frame and on_inbound_cold_ are non-coroutines returning an arm's
    // awaitable; every other member below is a coroutine whose first statement is
    // FIXPP_INBOUND_SPLIT_ENTRY(tic), the callee half of the b3 boundary defined in
    // src/session/session.cpp. `tic` is the caller's saved throw_if_cancelled value.
    // Members taking `hdr` or a view are awaited directly by the arm that owns that storage.
    using inbound_result_t = asio::awaitable<fixpp::core::expected_t<void>>;
    using inbound_continue_t = asio::awaitable<std::optional<fixpp::core::expected_t<void>>>;
    [[nodiscard]] inbound_result_t on_inbound_active_(std::span<const std::byte> frame) noexcept;
    [[nodiscard]] inbound_result_t on_inbound_cold_(bool tic,
                                                    std::span<const std::byte> frame) noexcept;
    [[nodiscard]] inbound_result_t on_inbound_drained_(bool tic) noexcept;
    [[nodiscard]] inbound_result_t on_inbound_not_connected_(
        bool tic, std::span<const std::byte> frame) noexcept;
    [[nodiscard]] inbound_result_t on_inbound_logout_sent_(
        bool tic, std::span<const std::byte> frame) noexcept;
    [[nodiscard]] inbound_result_t on_inbound_logon_sent_(
        bool tic, std::span<const std::byte> frame) noexcept;
    // The Active arm's synchronous SendingTime(52) MaxLatency check.
    [[nodiscard]] bool inbound_sending_time_ok_(
        fixpp::session::detail::FrameHeader const& hdr) const noexcept;
    // The sub-arm a rarely-taken Active branch selects, with the arm locals it takes by value.
    struct inbound_slow_t {
        enum class kind : std::uint8_t {
            none,
            validate_failed,
            sending_time_reject,
            sequence_reset,
            too_high,
            out_of_sequence,
            gap_fill,
            logout,
            from_admin_failed,
            test_request,
            resend_request,
            unsupported,
            from_app_failed,
        };
        kind k = kind::none;
        InboundValidation v{};
        seqnum_t next_expected = 0;
        seqnum_t seq = 0;
        fixpp::core::error chk_error{};
        fixpp::core::expected_t<dispatch_outcome> cb_r{};
    };
    // A non-coroutine: returns the awaitable of the sub-arm `slow` names.
    [[nodiscard]] inbound_result_t on_inbound_active_slow_(
        bool tic, std::span<const std::byte> frame, fixpp::session::detail::FrameHeader const& hdr,
        inbound_slow_t const& slow) noexcept;
    // The Active arm's sub-arms. An inbound_continue_t returns nullopt when the arm continues.
    [[nodiscard]] inbound_result_t on_inbound_active_validate_failed_(
        bool tic, fixpp::session::detail::FrameHeader const& hdr, InboundValidation v) noexcept;
    [[nodiscard]] inbound_result_t on_inbound_active_sending_time_reject_(
        bool tic, fixpp::session::detail::FrameHeader const& hdr) noexcept;
    [[nodiscard]] inbound_result_t on_inbound_active_sequence_reset_(
        bool tic, std::span<const std::byte> frame,
        fixpp::session::detail::FrameHeader const& hdr) noexcept;
    [[nodiscard]] inbound_result_t on_inbound_active_too_high_(bool tic, seqnum_t next_expected,
                                                               seqnum_t seq) noexcept;
    [[nodiscard]] inbound_continue_t on_inbound_active_poss_dup_(
        bool tic, fixpp::session::detail::FrameHeader const& hdr) noexcept;
    [[nodiscard]] inbound_result_t on_inbound_active_out_of_sequence_(
        bool tic, std::span<const std::byte> frame, fixpp::session::detail::FrameHeader const& hdr,
        fixpp::core::error chk_error) noexcept;
    [[nodiscard]] inbound_result_t on_inbound_active_gap_fill_(
        bool tic, std::span<const std::byte> frame,
        fixpp::session::detail::FrameHeader const& hdr) noexcept;
    [[nodiscard]] inbound_result_t on_inbound_active_logout_(
        bool tic, std::span<const std::byte> frame,
        fixpp::session::detail::FrameHeader const& hdr) noexcept;
    [[nodiscard]] inbound_result_t on_inbound_active_from_admin_failed_(
        bool tic, fixpp::session::detail::FrameHeader const& hdr,
        fixpp::core::expected_t<dispatch_outcome> cb_r) noexcept;
    [[nodiscard]] inbound_result_t on_inbound_active_test_request_(
        bool tic, fixpp::session::detail::FrameHeader const& hdr) noexcept;
    [[nodiscard]] inbound_result_t on_inbound_active_resend_request_(
        bool tic, fixpp::session::detail::FrameHeader const& hdr) noexcept;
    [[nodiscard]] inbound_result_t on_inbound_active_unsupported_(
        bool tic, fixpp::session::detail::FrameHeader const& hdr) noexcept;
    [[nodiscard]] inbound_result_t on_inbound_active_from_app_failed_(
        bool tic, fixpp::session::detail::FrameHeader const& hdr,
        fixpp::core::expected_t<dispatch_outcome> cb_r) noexcept;
    // Reply leaves (§2.2 class 2): each owns the buffer its reply frame is built in.
    [[nodiscard]] inbound_result_t inbound_reject_leaf_(
        bool tic, seqnum_t ref_seq, int ref_tag_id, std::string_view ref_msg_type, int reason,
        std::string_view sending_time, std::optional<fsm_state> arm = std::nullopt) noexcept;
    [[nodiscard]] inbound_result_t inbound_reject_best_effort_leaf_(
        bool tic, seqnum_t ref_seq, std::string_view ref_msg_type,
        std::string_view sending_time) noexcept;
    [[nodiscard]] inbound_result_t inbound_logout_leaf_(bool tic,
                                                        std::string_view sending_time) noexcept;
    [[nodiscard]] inbound_result_t inbound_logon_sent_logout_leaf_(
        bool tic, std::string_view text, std::string_view sending_time) noexcept;
    [[nodiscard]] inbound_result_t inbound_resend_request_leaf_(
        bool tic, seqnum_t begin_seqno, std::string_view sending_time) noexcept;
    [[nodiscard]] inbound_result_t inbound_heartbeat_reply_leaf_(
        bool tic, std::string_view test_req_id, std::string_view sending_time) noexcept;
    [[nodiscard]] inbound_result_t inbound_business_reject_leaf_(
        bool tic, seqnum_t ref_seq, std::string_view ref_msg_type,
        std::string_view sending_time) noexcept;
    [[nodiscard]] inbound_result_t inbound_acceptor_reply_logon_leaf_(
        bool tic, bool peer_sent_reset, int heartbt_sec, std::string_view reply_sending_time_view,
        seqnum_t& n_pre_outbound) noexcept;

    // 014 T010/T015 — PRIVATE handoff from ReconnectFsm on a successful attempt.
    // Called by ReconnectFsm::drive_reconnect_attempt() (step 8) via the
    // session_ back-pointer. ReconnectFsm is a value member of Session
    // (reconnect_fsm_, this file's member declaration) so the call is always in-domain.
    //
    // Responsibilities:
    //   - Take ownership of the live transport via reconnected_transport_.
    //   - Store handshake_result.peer_id as live_peer_id_ for the authorize
    //     site (E-2 / T015): the LogonSent→Active Logon-ack guard reads
    //     live_peer_id_ as arm (1-live) ahead of the override seam.
    //   - Re-enter LogonSent so the session re-drives Logon to Active.
    //
    // §XV.9 discipline: handshake_result is forward-declared above in this header
    // (namespace fixpp::transport { struct handshake_result; }).
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
    // recent inbound frame that was neither garbled nor faulty reached the
    // LogonReceived/Active arm (093 contract C-5). Used by the liveness timer loop
    // to compute the inbound-silence elapsed time. Seeded at open() and on entering
    // Active. Single-writer on the per-session strand.
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
    // guard (session.cpp's arm (1-live) authorize block), which reset()s it after authorizing.
    // Nullopt until the first successful reconnect; each successful reconnect overwrites it and the
    // guard reset()s it on consume, so a stale identity from a prior session is never
    // re-authorized. [data-model §E-2; contracts C2; FR-006] peer_identity is transitively
    // available via session_config.hpp → compid_authorization_policy.hpp → peer_identity.hpp.
    std::optional<fixpp::tls::peer_identity> live_peer_id_;

    // Outbound seqnum is managed exclusively by seqnum_mgr_ (RC#A gate-b/r1-green).
    // Use seqnum_mgr_.peek_outbound() to read; seqnum_mgr_.assign_outbound() to advance.
    // The bare next_outbound_seq_ field was removed to prevent split-brain divergence
    // between admin paths and Session::send. [005 data-model.md E3; 009 spec.md FR-001(a)]

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
    // 093-inbound-frame-dispositions (contract C-6) supersedes fixpp#518's in-unit
    // teardown_reset_done_ stops inside the 141=Y reset units: the unit sets the
    // manager before its one store operation, and close() waits for that operation, so
    // the flag is only close()'s single-fire latch.
    bool teardown_reset_done_ = false;

    // 093-inbound-frame-dispositions (data-model E-10; contract C-6) — the 141=Y reset
    // unit. run_reset_unit_ is its steps 3 and 4: the manager set (reset_to_one, then
    // both counters to the targets), stopping at the first error without returning
    // from the arm, then, only if that succeeded, the store's one reset_to. Plan OD-25
    // (superseding OD-14's in-place shield): the reset_to runs on an empty cancellation
    // slot, and the arm restores its own state after the unit (step 5).
    struct reset_unit_result {
        fixpp::core::expected_t<void> manager;
        fixpp::core::expected_t<void> store;
    };
    [[nodiscard]] asio::awaitable<reset_unit_result> run_reset_unit_(seqnum_t next_in,
                                                                     seqnum_t next_out) noexcept;
    // Set across the unit's reset_to await and cleared after it, error paths included.
    // Session strand only, as teardown_reset_done_.
    bool reset_unit_in_flight_ = false;
    // The unit's completion signal: set by close() while it waits for an in-flight
    // unit, called by the unit when it clears reset_unit_in_flight_. Session strand only.
    std::function<void()> reset_unit_wake_;

    // OD-29 (093 — terminal close escalates a graceful one). A close(terminal) made
    // while a close(graceful) is in flight sets the latch, which close(graceful) reads
    // before its phase 1 starts, and calls the wake, which is non-null only while phase
    // 1 races its grace timer and ends that wait. Session strand only.
    bool close_escalated_ = false;
    std::function<void()> close_grace_wake_;

    // 093-inbound-frame-dispositions (data-model E-13; contract C-6) — the engine-stop
    // flag: Engine::stop()'s step 1 sets it on the session strand, through
    // session_engine_access, before it emits its cancellation. logon_arm_superseded
    // reads it, so every later predicate check on this strand stops a Logon arm.
    // Written and read only on the session strand.
    // Placement condition: as garbled_frames_; a Session reused for a second connection
    // would need it reset when the transport is installed.
    bool engine_stop_requested_ = false;
    void note_engine_stop_() noexcept { engine_stop_requested_ = true; }

    // ── 013 Phase 3 T023/T026 — ReconnectFsm driver ─────────────────────────
    // 043 T012 (D-4/E-6) — Session-owned resolved transport factory.
    // Populated ONCE at open() to the effective factory:
    //   - insecure_plain_tcp + no override → built-in asio_plain_transport_factory
    //     (auto-derive, D-4).
    //   - Otherwise → transport_factory_override ? transport_factory_override
    //                                            : engine_.default_transport_factory
    //     (existing TLS resolution, preserved).
    // The SAME object used for BOTH the FR-008 kind()-check (US3/T023) AND the FSM
    // reconnect-mint (via set_transport_factory, E-5). Declared BEFORE reconnect_fsm_
    // so the owning shared_ptr outlives the FSM's non-owning factory_ raw pointer
    // (destruction order = reverse of declaration; honours reconnect_fsm.hpp's
    // "factory outlives this FSM" contract). Null until open(). [data-model §E-6; D-6]
    std::shared_ptr<fixpp::transport::TransportFactory> effective_transport_factory_;

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
    // the live Transport, satisfying transport.hpp's In-flight exclusivity contract.
    // Every live emit site (store_then_emit direct path, ResendRequest replay,
    // liveness HB/TR) acquires this gate before submitting to the transport.
    // Must be drained (cancel_and_drain()) in Session::close() AFTER root
    // cancellation fires, before seqnum_mgr_ drain, to satisfy the async_mutex
    // destructor's not_locked precondition. [transport.hpp In-flight exclusivity; FQ-A D-6]
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

    // ── 034-credential-store-redaction T004 — masker buffer bound ────────────
    // Upper bound for the coroutine-frame copy used in the masked-Logon persist
    // path (store_then_emit, T006). Bound to the build_logon builder's actual
    // maximum output capacity (session.cpp's `logon_buf` / `reply_buf` arrays
    // — both 256 bytes), so the over-bound branch in T006 is provably
    // dead for any frame that survives the MsgType=A gate (build_logon already
    // fails-closed with wire_frame_too_large above this size, and T007's
    // open()-time credential-length guard adds config-time defense for both roles).
    // NOT a public SessionConfig/ctor/template param (Art. X / non-template class).
    //
    // The dead over-bound branch earns its BRDA via session_test_access::store_then_emit
    // (tests/support/session_test_access.hpp): a test feeds a hand-crafted >256-byte
    // 35=A frame directly into store_then_emit, exercising the real branch against
    // this real bound. (An earlier design proposed a FIXPP_TEST_LOGON_MASK_BOUND
    // compile override; that could not reach this constant — store_then_emit is
    // compiled into libfixpp_session WITHOUT the test define — so frame-injection
    // through the test-access friend is the working seam for the same BRDA outcome.)
    // [034 data-model.md E1 / research R3 / contracts/store-redaction.md C2; plan ## Gate A]
    static constexpr std::size_t kMaxMaskableLogonBytes = 256;

    // store_then_emit: store(outbound) BEFORE transport_send (I-3).
    // stamped_seq: the MsgSeqNum already written into `frame` by the builder — passed
    //   explicitly since next_outbound_seq_ is removed (RC#A gate-b/r1-green unification).
    // Returns ok on success; propagates store errors per I-07; propagates transport
    //   throws as dispatch_aborted (RC#B gate-b/r1-green transport error surface).
    // arm: a Logon arm's expected state (093 plan OD-25). When set and
    //   logon_arm_superseded_(*arm) holds after the store, the frame is not transmitted
    //   and the call returns success; the caller's own predicate check follows.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store_then_emit(
        seqnum_t stamped_seq, std::span<const std::byte> frame,
        std::optional<fsm_state> arm = std::nullopt) noexcept;

    // fixpp#518, 093 (data-model E-13; contract C-6): true when a Logon arm must stop
    // after a resume, because the session left the state the arm expects while the arm
    // was suspended, or Engine::stop()'s step 1 has run on this strand. The writer that
    // matters is close(), which an application can post from a callback the arm fires
    // (Engine::lookup() already returns the session) or from another thread, and which
    // owns the teardown once it begins. The arm then returns success, as in the
    // Disconnected row. `closing` is the signal because close() sets it before it can
    // yield the strand, while a graceful close() leaves the FSM in the arm's state until
    // its phase 1 writes. The FSM term covers `closed_drained` too: close() writes
    // Disconnected before it gets there. `never_opened` is not a close.
    [[nodiscard]] bool logon_arm_superseded_(fsm_state expected) const noexcept;

    // 093 plan OD-26: the Disconnected write that follows a store suspension on a Logon
    // arm (an error path, or a refusal's fail-closed write). With `arm` set and
    // logon_arm_superseded_(*arm) true, close() or Engine::stop() owns the teardown and
    // nothing is written; otherwise it records Disconnected.
    void disconnect_unless_superseded_(std::optional<fsm_state> arm) noexcept;

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
    // disconnect_required (gate-b/r2 FQ-1): out-param set true ONLY at the two
    // commit-region producer sites (assign_outbound overflow, store_then_emit
    // fatal) — carries disconnect PROVENANCE out of send_impl so Session::send
    // can decide the transition by origin, not by re-classifying the returned
    // error value (a toApp passthrough can carry a store-block error value
    // without having reached the commit region). Default-false; the caller
    // must initialize it. [contracts/store-then-emit-disposition.md item 3]
    // fixpp#544 (B35, `.specify/544-hot-path-zero-alloc.md` §2.5; rulings R-9, R-10):
    // send_impl is the send tail. It builds into `frame_buf` through build_send_frame_,
    // then runs toApp, assign_outbound and store_then_emit over it, so `frame_buf` must
    // outlive the call. Its callers must be Session::send (over the session's frame slot) or
    // send_fallback_leaf_ (over the leaf's own buffer); re-check with
    // `git grep -n "send_impl(" -- src include`.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> send_impl(
        std::span<const std::byte> app_payload, std::span<std::byte> frame_buf,
        bool& disconnect_required);

    // fixpp#544 §2.5: the frame build of Session::send, a non-coroutine. It runs the 020
    // payload checks, the 022 scanner and excision pass, the SendingTime(52) stamp, the
    // MsgSeqNum peek and the framing, in that order, and writes the frame into `out`. It
    // uses send_strip_scratch_ and send_body_scratch_ without the slot flag. That is sound
    // while it neither suspends nor calls an Application callback, because the strand runs
    // one handler at a time. The clock's now() is the injected dependency it calls, at the
    // position the stamp has always had.
    struct send_frame_built {
        std::size_t size = 0;
        seqnum_t seq = 0;
    };
    [[nodiscard]] fixpp::core::expected_t<send_frame_built> build_send_frame_(
        std::span<const std::byte> app_payload, std::span<std::byte> out);

    // fixpp#544 §2.5 (R-10): the fallback for a send that finds the frame slot held (a send
    // nested in toApp, or overlapping one suspended in its store or its write). Its frame
    // owns a frame buffer, so it is over asio's recycler limit and allocates; it never
    // writes the slot flag. Its first statement is FIXPP_INBOUND_SPLIT_ENTRY(tic), the
    // callee half of the b3 boundary Session::send awaits it through.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> send_fallback_leaf_(
        bool tic, std::span<const std::byte> app_payload, bool& disconnect_required);

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
    // [transport.hpp In-flight exclusivity; FQ-A D-6;
    // feedback_async_mutex_us3_asio_cancel_and_subagent_seams]
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> live_write_serialized_(
        std::span<const std::byte> frame) noexcept;

    // run_logout_phase1: emit Logout frame, then wait for peer Logout-confirm
    // OR clock-bound 2 s timeout (session_logout_timeout, slot 73) under a
    // CHILD cancellation_state. Called from close(graceful) phase 1.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> run_logout_phase1() noexcept;

    // 027 T005 — replay_outbound_range_: extracted from the inline
    // ResendRequest-reply walk (session.cpp's `replay_outbound_range_` body). Replays stored
    // outbound app messages in [begin, requested_end] (or through current when
    // end_is_through_current=true) with PossDupFlag(43)=Y+OrigSendingTime(122)
    // at their original MsgSeqNum and SendingTime(52) restamped (fixpp#420); collapses
    // admin/absent runs — and any stored app message whose replay frame cannot be
    // built, which also emits session_event_resend_slot_gap_filled (fixpp#424) —
    // into SequenceReset-GapFill(123=Y). Transmit-only (does NOT advance the live
    // outbound counter, not re-stored). [const §VIII.5]: fixed stack buffers.
    //
    // TWO-VALUE END MODEL (data-model I-NEX-3, research D-5, contracts C3):
    //   eff_end = (end_is_through_current || requested_end > our_last)
    //               ? our_last : requested_end   (store-walk upper bound)
    //   empty/short-store GapFill NewSeqNo =
    //               end_is_through_current ? peek_outbound() : (requested_end+1)
    // A single (begin, end_inclusive) signature would lose requested_end and
    // regress the shipped 013 ResendRequest GapFill NewSeqNo for an explicit-end
    // beyond-store request. The EndSeqNo=0 => through-current resolution is
    // conveyed via end_is_through_current (NOT re-derived inside the helper).
    //
    // Returns replay_outcome::completed on success (resend complete, remain in Active).
    // Returns replay_outcome::superseded when `arm` is set and logon_arm_superseded_(*arm)
    //   held before one of the walk's effects (093 plan OD-25): the walk stopped there.
    // Returns std::unexpected(app_callback_threw) when a GapFill toAdmin threw.
    // Returns std::unexpected(dispatch_aborted) on transport write error.
    //   In BOTH unexpected cases the CALLER owns record_state_transition_(Disconnected).
    //   The helper NEVER calls record_state_transition_ directly (D-5).
    //
    // Callers:
    //   (1) ResendRequest handler (Active): begin=rr_begin, requested_end=rr_end
    //       (raw parsed EndSeqNo), end_is_through_current=(rr_end==0); no arm.
    //   (2) 789 honor path (T014/T015, US1): begin=X, requested_end=N-1,
    //       end_is_through_current=true; the Logon arm's expected state.
    enum class replay_outcome : std::uint8_t { completed, superseded };
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<replay_outcome>> replay_outbound_range_(
        fixpp::session::seqnum_t begin, fixpp::session::seqnum_t requested_end,
        bool end_is_through_current, std::optional<fsm_state> arm = std::nullopt) noexcept;

    // 027 — honor_peer_next_expected_: shared body for the 789-honor dispatch.
    // Extracted from the acceptor (NotConnected) and initiator (LogonSent)
    // handlers; both are byte-for-byte identical EXCEPT the input expressions.
    //
    // Inputs:
    //   raw_789    — the raw string_view of tag 789's value from the inbound Logon
    //                (may be empty if tag was present but had no value).
    //   present_789 — true iff tag 789 appeared in the inbound Logon frame.
    //   next_outbound_ref — the next-outbound seq the peer's 789 is compared against
    //                (031): acceptor passes the PRE-reply outbound (captured before the
    //                reply Logon consumes a seq); initiator passes current peek_outbound()
    //                (byte-identical). The three comparisons use this; the resend RANGE
    //                still reads the live peek_outbound() (INV-NEX-RANGE).
    //   arm        — the calling Logon arm's expected state (093 plan OD-25).
    //
    // Outcomes (D-10 ordering preserved: invalid-X FIRST, then X>N, then X<N):
    //   in_sync_continue     — X==N (in sync), or X<N resend succeeded; caller continues.
    //   ended_disconnected   — X==0 invalid OR X>N violation: helper emitted Logout,
    //                          called record_state_transition_(Disconnected), caller MUST
    //                          co_return expected_t<void>{} (terminal, already handled).
    //   superseded           — logon_arm_superseded_(arm) held after a suspension, before
    //                          the helper's next effect: it stopped there and wrote no
    //                          state; caller MUST co_return expected_t<void>{}.
    //   unexpected(err)      — X<N resend failed; helper called
    //                          record_state_transition_(Disconnected); caller MUST
    //                          co_return std::unexpected(err).
    //
    // The presence guard (cfg_.enable_next_expected_msg_seq_num && present_789)
    // remains at each call site so the knob-off / tag-absent no-op stays visible.
    enum class logon_789_outcome : std::uint8_t {
        in_sync_continue,
        ended_disconnected,
        superseded
    };
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<logon_789_outcome>>
    honor_peer_next_expected_(std::string_view raw_789, bool present_789,
                              fixpp::session::seqnum_t next_outbound_ref, fsm_state arm) noexcept;

    // fixpp#544 (B35, `.specify/544-hot-path-zero-alloc.md` §2.5; rulings R-9, R-10): the
    // send path's session-owned storage, allocated with the Session, so once per session
    // and never per message ([const §XV.1]).
    //   - send_strip_scratch_ and send_body_scratch_: build_send_frame_'s scratch, used
    //     only inside that synchronous helper;
    //   - send_frame_slot_: the frame Session::send's primary path builds into; it is read
    //     until store_then_emit returns;
    //   - send_slot_in_use_: set and cleared only by Session::send's RAII holder, on the
    //     session strand. A send that finds it set takes send_fallback_leaf_.
    static constexpr std::size_t kSendFrameBytes = 4096;
    std::array<std::byte, kSendFrameBytes> send_strip_scratch_{};
    std::array<std::byte, kSendFrameBytes> send_body_scratch_{};
    std::array<std::byte, kSendFrameBytes> send_frame_slot_{};
    bool send_slot_in_use_ = false;
};

}  // namespace fixpp::session
