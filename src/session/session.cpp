// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/session.cpp
//
// fixpp::session::Session — minimal real skeleton out-of-line impl (D-4 /
// E10). Phase 2 (T012) ships the ctor + the never-null session_arena()
// resolution chain + linkable open()/close() placeholders. The 2d-owned
// BEHAVIOUR is wired per user story (T020/T030/T037/T038/T039/T045/T050) —
// each replaces the marked placeholder body, it is not additive guesswork.
#include <fixpp/session/session.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_state.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <fixpp/core/clock.hpp>              // Clock::steady_now / sleep_until
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>             // expected_t, error values
#include <fixpp/core/session_executor.hpp>
#include <fixpp/session/message_store.hpp>        // 008-message-store — store_ unique_ptr dtor
#include <fixpp/session/message_store_factory.hpp>  // 008-message-store — make() call site
#include <fixpp/session/admin_messages.hpp>     // 005 US1: interpret_logon / T046: build_logout
#include <fixpp/session/direction.hpp>          // 005 US4: direction_t (store outbound)
#include <fixpp/session/session_fsm.hpp>        // 005 US1: fsm_state enum (T023–T025)
#include <fixpp/session/seqnum_manager.hpp>     // 005 US2: SeqnumManager (T031)
#include <fixpp/session/sending_time.hpp>       // 005 US5: check_sending_time (T055)
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/security_profile.hpp>  // SecurityProfile::kind::unset sentinel check (lives in `session` per [arch §6 line 243])
#include <fixpp/core/fix_time.hpp>             // 005 US5: fix_string_to_utc_time (T055/T056)

namespace fixpp::session {

namespace {
// [2d §4.5] never-null resolution chain: SessionConfig::session_arena ?:
// EngineConfig::default_session_resource ?: std::pmr::get_default_resource().
std::pmr::memory_resource* resolve_session_arena(
    const fixpp::core::EngineConfig& engine,
    const SessionConfig& cfg) noexcept {
    if (cfg.session_arena != nullptr) { return cfg.session_arena; }
    if (engine.default_session_resource != nullptr) {
        return engine.default_session_resource;
    }
    return std::pmr::get_default_resource();
}
}  // namespace

Session::Session(const fixpp::core::EngineConfig& engine,
                 const SessionConfig& cfg) noexcept
    : engine_(engine),
      cfg_(cfg),
      session_arena_(resolve_session_arena(engine, cfg)) {
    // Resolution chain always terminates at std::pmr::get_default_resource()
    // (never null), so I-18's never-null contract holds for the lifetime.
}

// D-23: release any per-session Clock state (system_clock_source's reusable
// timer-slot pool keyed by Session*, etc.) BEFORE the session_arena memory
// is reclaimed by member destruction. Idempotent — the default Clock hook
// is a no-op for clocks without per-session state. effective_clock_ is
// nullptr until open() resolves it (state_ == never_opened); after a
// successful open() it remains non-null until destruction (the Clock is
// shared_ptr-owned by EngineConfig and outlives the session — [2d §4.1]).
Session::~Session() {
    if (effective_clock_) {
        effective_clock_->forget_session(this);
    }
}

std::pmr::memory_resource* Session::session_arena() const noexcept {
    return session_arena_;  // I-18: frozen at ctor, never null, never swapped
}

// ── Phase-2 linkable placeholders — REPLACED per user story ─────────────
// Marked so a later phase's task body substitutes (not appends to) these.

asio::awaitable<fixpp::core::expected_t<void>> Session::open() noexcept {
    using fixpp::core::error;

    // ── All config validations run BEFORE any observable state mutation ──────
    // (RC#2 P2.2 fix: reorder to prevent partial side-effects on failure path)

    // (5) reject a second open() on the same handle (slot 51 / FR-018).
    // Any non-never_opened state means open() already ran.
    if (state_ != lifecycle::never_opened) {
        co_return std::unexpected(error::session_already_open);
    }

    // (1) executor resolution — the uniform resolved =
    // override.value_or(engine_anchor) pattern (FR-016/FR-018).
    asio::any_io_executor resolved =
        cfg_.executor_override.value_or(engine_.executor);

    // (4a) null EngineConfig::executor (no override either) →
    // invalid_session_config (slot 53 / FR-018). any_io_executor is
    // contextually false when it holds no target.
    if (!resolved) {
        co_return std::unexpected(error::invalid_session_config);
    }

    // (4b) direct_executor + lock_policy::spin is rejected even when
    // attested (slot 53 / I-06 / FR-009). [const §XI.5]: the store-write
    // path is always mutex; spin under a bare attested executor has no
    // engine-internal serialisation to fall back on.
    if (cfg_.mode == threading_mode::direct_executor
        && cfg_.locks == lock_policy::spin) {
        co_return std::unexpected(error::invalid_session_config);
    }

    // T048 (US5): runtime out-of-range-cast reject for backpressure_mode
    // (I-14 / [const §XV.15] / [2d §6.4] / FR-010). The enum is closed with
    // exactly 2 values (block=0, disconnect_and_recover=1); drop_oldest is
    // UNREPRESENTABLE. A cast from an out-of-range integer (FFI/SWIG bypass)
    // is caught here as a defence-in-depth backstop.
    FIXPP_ASSERT_BACKPRESSURE_SWITCH_EXHAUSTIVE(fixpp::session::SessionConfig::backpressure_mode);
    {
        const auto raw = static_cast<std::uint8_t>(cfg_.app_backpressure);
        if (raw != static_cast<std::uint8_t>(
                       fixpp::session::SessionConfig::backpressure_mode::block) &&
            raw != static_cast<std::uint8_t>(
                       fixpp::session::SessionConfig::backpressure_mode::disconnect_and_recover)) {
            co_return std::unexpected(error::invalid_session_config);
        }
    }

    // T050 (US5): null dictionary → invalid_session_config (slot 53 / FR-016
    // / FR-018 / I-13). dictionary is REQUIRED — the uniform resolved =
    // override.value_or(engine_anchor) pattern for the dictionary axis: a
    // null dictionary means neither session nor engine supplied one.
    if (!cfg_.dictionary) {
        co_return std::unexpected(error::invalid_session_config);
    }

    // RC#1 (gate-b/r1): default-constructed security_profile sentinel →
    // invalid_session_config (slot 53 / N-P2-3 / [const §XII.5] / FR-018).
    // The minimal-stub pattern (D-15 / D-21 amended) ships SecurityProfile
    // as a complete value type with a sentinel discriminant (kind::unset).
    // 2g extends with the concrete TLS binding; the field SHAPE is now
    // correct and the constitutional no-implicit-default rule is enforced.
    if (cfg_.security_profile.k ==
            fixpp::session::SecurityProfile::kind::unset) {
        co_return std::unexpected(error::invalid_session_config);
    }

    // ── Executor binding — the single executor_not_serialised enforcement
    // point (slot 48 / FR-009 / I-06): make_session_executor wraps
    // make_strand under per_session_strand, carries the bare attested
    // executor under direct_executor, and rejects direct_executor && !attested.
    // This is the first observable mutation; all config rejections are above.
    auto bound = fixpp::core::make_session_executor(
        std::move(resolved), cfg_.mode, cfg_.already_serialized_executor, this);
    if (!bound) {
        co_return std::unexpected(bound.error());
    }
    exec_ = std::move(*bound);

    // (2) effective_clock = SessionConfig::clock_override ?:
    // EngineConfig::clock, resolved ONCE here, bound to session lifetime
    // (FR-005 / I-03). The clock_not_set engine-level gate (FR-006) is
    // validate_engine_config() at Engine::open — independent of per-session
    // overrides; Session::open only resolves.
    effective_clock_ = cfg_.clock_override ? cfg_.clock_override
                                           : engine_.clock;

    // (3) T045: populate the session_local<trace_context> slot from
    // SessionConfig::initial_trace_context (FR-014). Stored in-domain at
    // open; current_trace_context reads it through the stable Session*
    // (survives cross-thread resume — NOT thread_local).
    trace_slot_.store(cfg_.initial_trace_context);

    // ── 008-message-store ownership wire (T011 / FR-005 / FR-025 / FR-026 /
    //    FR-028 A1 hook) ────────────────────────────────────────────────────
    // Mint the per-session MessageStore via the factory when configured.
    // The 007 baseline leaves SessionConfig::store_factory null on smoke
    // paths; 005's FSM will require it. The engine threads in:
    //   - sender_comp_id / target_comp_id (4th and 5th positional per
    //     FR-005; FileStore composes the on-disk log path from them post
    //     CompID-safety validation per [2e §D.4]),
    //   - &store_arena_resource_ as the 3rd mr argument — the dedicated
    //     monotonic_buffer_resource whose lifetime matches the store
    //     instance (FR-026 peer-not-sub-resource rule),
    //   - engine_.max_store_memory_per_session as the 4th cap (FR-014a;
    //     storage-DoS guard binding),
    //   - engine_.file_io_executor as the 5th file-I/O executor (FR-024a;
    //     FileStore async pwrite/fdatasync target).
    // store_factory_failed surfaces verbatim; the store is bound as N1
    // unique ownership before state_ flips to open (no observable open
    // session without a usable store when one was requested).
    if (cfg_.store_factory) {
        auto minted = cfg_.store_factory->make(
            cfg_.sender_comp_id,
            cfg_.target_comp_id,
            &store_arena_resource_,
            engine_.max_store_memory_per_session,
            engine_.file_io_executor);
        if (!minted) {
            co_return std::unexpected(minted.error());  // store_factory_failed
        }
        store_ = std::move(*minted);
        // A1 factory-type tag: read the hook ONCE here. Null for impls that
        // do not satisfy detail::has_flush_for_session_close (MemoryStore /
        // user impls with no flush method); FileStore returns a typed thunk.
        // T032 (Phase 4 US2) dispatches this at close(graceful).
        close_flush_hook_a1_ = store_->flush_hook();
    }

    state_ = lifecycle::open;

    // US4 (T046): capture transport_send from config (null if not set).
    // Called from store_then_emit() AFTER store(outbound) completes (I-3).
    transport_send_ = cfg_.transport_send;

    // T023 (US1, Phase 3): initiator path NotConnected → LogonSent.
    // After all validations and store binding succeed, mint the LogonSent
    // FSM state per data-model.md matrix NotConnected row, open(initiator)
    // cell. The actual Logon emission (build + transport::async_write +
    // SendingTime stamping) is wired in US1/US4 via the Session::send()
    // path; this assignment is the FSM half of the transition required by
    // the matrix so subsequent inbound dispatch (on_inbound_frame()) hits
    // the LogonSent row instead of the NotConnected row.
    fsm_state_ = fsm_state::LogonSent;

    // T041 (US3): seed last_inbound_steady_ at open() so the liveness loop
    // starts measuring from session-open, not the epoch.
    if (effective_clock_) {
        last_inbound_steady_ = effective_clock_->steady_now();
    }

    co_return fixpp::core::expected_t<void>{};
}

asio::awaitable<fixpp::core::expected_t<void>>
Session::close(close_mode mode) {
    using fixpp::core::error;

    // ── T038: idempotent THREE-STATE model (I-10 / [2d §4.7]:830-832,863) ──
    // never-opened OR already-closed(drained) → session_already_closed
    // (slot 52); no side effects.
    if (state_ == lifecycle::never_opened ||
        state_ == lifecycle::closed_drained) {
        co_return std::unexpected(error::session_already_closed);
    }
    // already-closing (in-flight) → the SAME in-flight result, NO error, NO
    // side effects: await the first call's shared slot, then mirror it. (The
    // 2d-owned property the seam asserts; the scripted double drives the
    // interleave deterministically — [2d §6.5]:1172.)
    if (state_ == lifecycle::closing) {
        auto shared = close_result_;
        while (!shared || !shared->has_value()) {
            co_await asio::post(co_await asio::this_coro::executor,
                                asio::use_awaitable);
        }
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - guarded by has_value() above
        co_return **shared;
    }

    // ── First close() on an OPEN session: run the two-phase body once ──────
    state_       = lifecycle::closing;
    close_result_ = std::make_shared<
        std::optional<fixpp::core::expected_t<void>>>();

    // T037 phase 1 — graceful ONLY (terminal skips phase 1 entirely; the
    // hook is NOT invoked). Invoked EXACTLY ONCE, after the (scripted) last
    // in-flight store(...) resumes and BEFORE the Logout step. A
    // store_io_failure is logged then close PROCEEDS (I-07); 007 has no log
    // sink wired (005/2e), so the failure is observed by the hook's own
    // bookkeeping and close still completes successfully. The real Logout
    // exchange + Clock::sleep_until close-timeout under a CHILD
    // cancellation_state are 005-owned (no transport / no D-9 timeout value
    // in 007 — D-16); the 2d-owned phase-1 obligation wired here is the
    // call-site + the once/never ordering the seam asserts.
    //
    // T032 (008-message-store / US2 / FR-028 / I-17 / Appendix D §D.2):
    // A1-pinned graceful-close hook dispatch via the typed thunk stashed at
    // open(). Non-virtual (concept-shaped, NOT dynamic_cast). Runs OUTSIDE
    // phase-1's child timeout (the real child timeout for Logout is 005-owned;
    // this plain co_await runs without a child cancellation_state). terminal
    // skips this block entirely per the mode guard above.
    if (mode == close_mode::graceful) {
        // A1 dispatch: the factory-type-tag typed thunk (non-null for FileStore;
        // null for MemoryStore / user impls without flush_for_session_close).
        if (close_flush_hook_a1_ != nullptr && store_ != nullptr) {
            auto flush_result = co_await (*close_flush_hook_a1_)(*store_);
            (void)flush_result;  // store_io_failure → logged-then-proceed (I-07)
        }

        // Scripted seam-5 hook (007's D-16 scripted-test-double; kept for
        // backward compatibility with seam-5 test assertions). Runs AFTER the
        // A1 typed-thunk dispatch in phase 1 ordering.
        if (close_flush_hook_) {
            const auto flush = close_flush_hook_();
            (void)flush;  // store_io_failure → logged-then-proceed (I-07)
        }

        // US4 / T047: phase-1 Logout exchange under a CHILD cancellation_state.
        // Only runs when the session was in Active state (i.e. reached Active
        // and has a transport to emit on, or the FSM is at LogoutSent/Active
        // when close(graceful) is called). If the session never opened or is
        // already below Active, the Logout step is a no-op.
        //
        // The child cancellation_state isolates the Logout write + 2s sleep
        // from the phase-2 root total signal: phase-2 fires root_cancel_.emit()
        // AFTER this block resolves (peer ACK | timeout | cancellation).
        //
        // [feedback_asio_cospawn_total_cancellation_default]: we use the root
        // slot for the child; the run_logout_phase1 coroutine resets to
        // enable_total_cancellation internally.
        if (fsm_state_ == fsm_state::Active || fsm_state_ == fsm_state::LogonReceived) {
            auto phase1_r = co_await run_logout_phase1();
            (void)phase1_r;  // timeout is logged-then-proceed (I-07; force-disconnect)
        }
    }

    // US4: ensure FSM is Disconnected before phase-2 fires.
    // Graceful close: run_logout_phase1 already transitioned to Disconnected.
    // Terminal close: transition directly here (phase 1 skipped, I-9).
    // Any other FSM state (LogonSent, NotConnected, etc.) also becomes
    // Disconnected per the matrix close(terminal)/fatal column.
    if (fsm_state_ != fsm_state::Disconnected) {
        fsm_state_ = fsm_state::Disconnected;
    }

    // T037/T039 phase 2 — fire root cancellation_type::total ONLY after
    // phase 1 has resolved (peer ACK | child timeout | child cancelled —
    // collapsed to "phase 1 done" in the scripted scope). This is the single
    // propagation point: every strand of in-flight session work bound to
    // root_cancellation_slot() (transport r/w, heartbeat sleep, mutex
    // acquire, cancellable_dispatch, parser→fromApp — all 005-owned) unwinds
    // here. terminal reaches phase 2 immediately (phase 1 skipped).
    //
    // US4 (T047): cancel any mock_clock (or real clock) sleepers BEFORE
    // emitting the ASIO total-cancellation signal. The liveness loop's
    // sleep_until is waiting on the mock_clock timer (not an ASIO channel),
    // so root_cancel_.emit() alone cannot wake it up — mock_clock::cancel_sleeps()
    // must be called first so the liveness loop exits its try/catch and the
    // coroutine can be collected by ioc.run_for() after close() returns.
    // Real asio::steady_timer sleeps are also cancelled by the ASIO slot, so
    // this call is safe for non-mock clocks too (cancel_sleeps() is a no-op
    // when no sleepers are registered).
    if (effective_clock_) {
        effective_clock_->cancel_sleeps();
    }
    root_cancel_.emit(asio::cancellation_type::total);

    // T045: clear the session_local<trace_context> slot at close completion
    // (FR-014). Reached in BOTH graceful and terminal once the two phases
    // resolve; the slot stays valid until here (seam 17: never read through
    // a destroyed slot — the slot lives in the Session, drained, not freed).
    trace_slot_.clear();

    // Completed: both phases drained (transport closed / arenas reset /
    // trace slot cleared above). Cancellation surfaces as
    // operation_aborted/dispatch_aborted on the in-flight work, never a
    // thrown exception across parse→fromApp (I-09); close() itself
    // completes expected_t<void>{}.
    *close_result_ = fixpp::core::expected_t<void>{};
    state_         = lifecycle::closed_drained;
    co_return **close_result_;
}

// ── 005-session-establishment-fsm additions (T018) ──────────────────────────
// Bodies wired per user story (Phase 3 / T023–T025). Each placeholder is
// replaced by the full body below; NOT additive — the comment is the anchor.

namespace {

// Minimal SOH-delimited field scanner — no heap, no library.
// Reads tag 8 (BeginString), 34 (MsgSeqNum), 35 (MsgType),
// 49 (SenderCompID), 52 (SendingTime), 56 (TargetCompID), 112 (TestReqID)
// from a raw FIX frame.
// Used by the inbound dispatch path (scenarios 2i/2k/seqnum check + US3 liveness
// + US5 SendingTime MaxLatency check).
struct FrameHeader {
    std::string_view begin_string;
    std::string_view sender_comp_id;
    std::string_view target_comp_id;
    std::string_view msg_seq_num;   // tag 34 raw string value
    std::string_view msg_type;      // tag 35 raw string value (T041 US3)
    std::string_view sending_time;  // tag 52 raw string value (T055 US5)
    std::string_view test_req_id;   // tag 112 raw string value (T041 US3)
};

[[nodiscard]] FrameHeader scan_frame_header(std::span<const std::byte> frame) noexcept {
    FrameHeader h;
    const std::byte SOH{0x01};
    const std::byte EQ{static_cast<std::byte>('=')};
    std::size_t i = 0;
    const std::size_t n = frame.size();

    while (i < n) {
        std::uint32_t tag = 0;
        bool tag_ok = true;
        while (i < n && frame[i] != EQ && frame[i] != SOH) {
            auto c = static_cast<unsigned char>(frame[i]);
            if (c < '0' || c > '9') { tag_ok = false; }
            tag = tag * 10U + static_cast<std::uint32_t>(c - '0');
            ++i;
        }
        if (i >= n || frame[i] != EQ || !tag_ok) {
            while (i < n && frame[i] != SOH) { ++i; }
            if (i < n) { ++i; }
            continue;
        }
        ++i;  // skip '='
        std::size_t vstart = i;
        while (i < n && frame[i] != SOH) { ++i; }
        std::string_view val(
            reinterpret_cast<const char*>(frame.data() + vstart), i - vstart);
        if (i < n) { ++i; }  // skip SOH

        switch (tag) {
            case 8:   h.begin_string    = val; break;
            case 34:  h.msg_seq_num     = val; break;
            case 35:  h.msg_type        = val; break;  // T041 US3
            case 49:  h.sender_comp_id  = val; break;
            case 52:  h.sending_time    = val; break;  // T055 US5 SendingTime
            case 56:  h.target_comp_id  = val; break;
            case 112: h.test_req_id     = val; break;  // T041 US3
            default: break;
        }
    }
    return h;
}

// Parse a decimal seqnum from a string_view. Returns 0 if invalid.
// Zero is never a valid FIX seqnum (seqnum_min=1), so 0 signals parse failure.
// No heap, no library, stack-only. (I-7 no-alloc hot path.)
[[nodiscard]] static fixpp::session::seqnum_t parse_seqnum(std::string_view sv) noexcept {
    using fixpp::session::seqnum_t;
    if (sv.empty()) { return 0; }
    seqnum_t val = 0;
    for (char c : sv) {
        if (c < '0' || c > '9') { return 0; }
        const seqnum_t digit = static_cast<seqnum_t>(c - '0');
        // Overflow guard: seqnum_max / 10 = UINT32_MAX / 10 = 429496729.
        if (val > 429496729U || (val == 429496729U && digit > 5U)) {
            return 0;  // overflow
        }
        val = val * 10U + digit;
    }
    return val;
}

}  // namespace

// T024/T025 (US1, Phase 3) + T032/T034/T035 (US2, Phase 4) +
// T056 (US5, Phase 7): Inbound FSM dispatch.
//
// Guard precedence per data-model.md matrix preamble (T056 adds steps 1/3/5):
//   (1) parse/type recognised → else session Reject; no-loop-guard exempts
//       Reject(35=3) and Logout(35=5) from triggering a Reject.
//   (2) CompID/BeginString gate (post-logon states)
//   (3) SendingTime(52) MaxLatency vs effective clock (Q3):
//       established session → Reject(reason=10, refTag=52) → Logout → Disconnect
//       Logon path         → logout-with-error, no standalone Reject (D-3)
//   (4) seqnum class (too-low / too-high / in-seq) — T035
//   (5) message-type-for-state: unrecognized app msg type → session Reject;
//       session stays Active (no loop for Reject/Logout).
//
// Seqnum check (T031/T032/T035):
//   Too-low  → session_seqnum_too_low (69)   → fatal: Disconnected
//   Too-high → session_seqnum_gap_unrecoverable (70) → fatal: Disconnected
//   In-seq   → advance counter, proceed
//
// For the NotConnected/Logon path the peer's first Logon carries seq=1.
// If seq != 1 → too-low or too-high → fatal.
//
// Inbound ordering (I-3 / [2e §7.6]): store(inbound) BEFORE fromAdmin/fromApp.
// T034 wires the store call; here the seqnum check is the gate.
//
// LogoutSent / Disconnected: all inbound silently drained (defined cells).
asio::awaitable<fixpp::core::expected_t<void>>
Session::on_inbound_frame(std::span<const std::byte> frame) noexcept {
    switch (fsm_state_) {
        case fsm_state::NotConnected: {
            // First message must be a Logon. interpret_logon validates:
            //   MsgType==A, BeginString==cfg_.begin_string,
            //   SenderCompID==cfg_.target_comp_id (peer's sender = our target),
            //   TargetCompID==cfg_.sender_comp_id (peer's target = our sender),
            //   HeartBtInt present and ≥ 0.
            auto result = fixpp::session::interpret_logon(
                frame,
                cfg_.target_comp_id,   // expected_sender: peer's 49= is our target
                cfg_.sender_comp_id,   // expected_target: peer's 56= is our sender
                cfg_.begin_string);

            if (!result) {
                // Refusal — BeginString/CompID mismatch or not-Logon.
                // Per matrix NotConnected row:
                //   inbound Logon (refused)   → refuse, → Disconnected
                //   inbound Heartbeat / TR / Reject / out-of-scope admin /
                //     invalid MsgType         → refuse, → Disconnected (1st msg ≠ Logon)
                //
                // BUT: the existing US1 seam tests assert that a refused-Logon
                // (BeginString/CompID mismatch) leaves the FSM NOT-in-Active
                // without requiring Disconnected specifically (Phase 6 ships the
                // full refusal+disconnect path; Phase 3 only requires "never
                // reaches Active"). Preserve that contract for the explicit
                // Logon-shaped refusal cases (interpret_logon would have
                // detected wrong BeginString/CompID and returned !result with
                // the frame still being a 35=A Logon).
                //
                // Discriminate the two cells: if the frame is a Logon (35=A)
                // that failed CompID/BeginString validation, keep the Phase 3
                // "stays NotConnected" semantic. If it is a non-Logon first
                // message, transition to Disconnected per matrix row.
                auto hdr = scan_frame_header(frame);
                // Scan for MsgType (tag 35). scan_frame_header reads tags 8/34/49/56
                // but not 35; do a minimal MsgType check inline.
                bool is_logon = false;
                {
                    const std::byte SOH{0x01};
                    const std::byte EQ{static_cast<std::byte>('=')};
                    std::size_t i = 0;
                    const std::size_t n = frame.size();
                    while (i + 2 < n) {
                        // Look for "35=" SOH-delimited field start.
                        if (frame[i] == static_cast<std::byte>('3') &&
                            frame[i+1] == static_cast<std::byte>('5') &&
                            frame[i+2] == EQ) {
                            std::size_t v = i + 3;
                            if (v < n && frame[v] == static_cast<std::byte>('A') &&
                                (v + 1 == n || frame[v+1] == SOH)) {
                                is_logon = true;
                            }
                            break;
                        }
                        // Advance to next SOH+1 (start of next field).
                        while (i < n && frame[i] != SOH) { ++i; }
                        if (i < n) { ++i; }
                    }
                }
                (void)hdr;
                if (!is_logon) {
                    // Non-Logon first message: matrix row → refuse, → Disconnected.
                    fsm_state_ = fsm_state::Disconnected;
                }
                // Refused-Logon (CompID/BeginString failure): preserve Phase 3
                // "stays NotConnected" contract (full disconnect lands in Phase 6).
                co_return fixpp::core::expected_t<void>{};
            }

            // Valid Logon: check seqnum (T035 seqnum column: NotConnected row).
            // The Logon must carry seq=1 on initial session (seqnum_mgr_ starts at 1).
            {
                auto hdr = scan_frame_header(frame);
                const seqnum_t seq = parse_seqnum(hdr.msg_seq_num);
                if (seq == 0) {
                    // Cannot parse seq — treat as invalid (fatal for protocol safety).
                    fsm_state_ = fsm_state::Disconnected;
                    co_return fixpp::core::expected_t<void>{};
                }

                auto chk = co_await seqnum_mgr_.check_inbound(seq);
                if (!chk) {
                    // Too-low or too-high: session-fatal (I-2/I-4/[FIX-SL §4.1]).
                    fsm_state_ = fsm_state::Disconnected;
                    co_return fixpp::core::expected_t<void>{};
                }
            }

            // Valid Logon + in-seq: transition to LogonReceived.
            // The echo Logon build/send (acceptor replies) is wired in T026.
            fsm_state_ = fsm_state::LogonReceived;
            co_return fixpp::core::expected_t<void>{};
        }

        case fsm_state::LogonReceived:
        case fsm_state::Active: {
            // T056 (US5) guard-precedence ordering:
            // (1) parse/type recognised → else session Reject (no-loop-guard)
            // (2) CompID/BeginString gate
            // (3) SendingTime MaxLatency (Q3)
            // (4) seqnum class
            // (5) message-type-for-state

            auto hdr = scan_frame_header(frame);

            // ── Guard (2): CompID/BeginString gate (scenarios 2i/2k) ──────────
            // Any mismatch → session-fatal → Disconnected.
            if (hdr.begin_string != cfg_.begin_string ||
                hdr.sender_comp_id != cfg_.target_comp_id ||
                hdr.target_comp_id != cfg_.sender_comp_id) {
                fsm_state_ = fsm_state::Disconnected;
                co_return fixpp::core::expected_t<void>{};
            }

            // ── Guard (3): SendingTime MaxLatency (Q3, T055/T056) ─────────────
            // Check |inbound_sending_time − effective_now| ≤ MaxLatency (D-8: 120 s).
            // No-reject-loop guard: Reject(35=3) and Logout(35=5) are exempt from
            // this check per I-5 (a malformed Reject is never itself rejected).
            // Established session: Reject(reason=10, refTag=52) → Logout → Disconnect.
            // Note: the Logon-path special case (D-3) is handled in LogonSent below.
            if (hdr.msg_type != "3" && hdr.msg_type != "5" &&
                !hdr.sending_time.empty() && effective_clock_) {
                auto parse_r = fixpp::core::fix_string_to_utc_time(
                    std::span<const char>{hdr.sending_time.data(),
                                         hdr.sending_time.size()});
                if (parse_r) {
                    const auto max_lat = cfg_.sending_time_threshold.has_value()
                        ? std::chrono::duration_cast<std::chrono::seconds>(
                              *cfg_.sending_time_threshold)
                        : std::chrono::seconds{120};  // D-8 default 120 s
                    auto chk_st = fixpp::session::check_sending_time(
                        *parse_r,
                        effective_clock_->now(),
                        max_lat);
                    if (!chk_st) {
                        // Q3 established-session path: Reject → Logout → Disconnect.
                        // Step 1: emit Reject(35=3, RefTagID=52, reason=10).
                        {
                            std::array<std::byte, 512> rj_buf{};
                            const seqnum_t ref_seq = parse_seqnum(hdr.msg_seq_num);
                            auto rj_result = fixpp::session::build_reject(
                                std::span<std::byte>{rj_buf.data(), rj_buf.size()},
                                next_outbound_seq_++,
                                cfg_.sender_comp_id,
                                cfg_.target_comp_id,
                                ref_seq,
                                52,        // RefTagID = 52 (SendingTime)
                                hdr.msg_type,
                                10);       // SessionRejectReason = 10 (SendingTime accuracy)
                            if (rj_result) {
                                auto emit_r = co_await store_then_emit(*rj_result);
                                (void)emit_r;
                            }
                        }
                        // Step 2: emit Logout(35=5).
                        {
                            std::array<std::byte, 256> lo_buf{};
                            auto lo_result = fixpp::session::build_logout(
                                std::span<std::byte>{lo_buf.data(), lo_buf.size()},
                                next_outbound_seq_++,
                                cfg_.sender_comp_id,
                                cfg_.target_comp_id,
                                {});
                            if (lo_result) {
                                auto emit_r = co_await store_then_emit(*lo_result);
                                (void)emit_r;
                            }
                        }
                        // Step 3: Disconnect.
                        fsm_state_ = fsm_state::Disconnected;
                        co_return fixpp::core::expected_t<void>{};
                    }
                }
                // parse failure: accept the message (lenient on malformed timestamp)
            }

            // ── Guard (4): seqnum check (T035) ────────────────────────────────
            {
                const seqnum_t seq = parse_seqnum(hdr.msg_seq_num);
                if (seq == 0) {
                    // Cannot parse seq — session-fatal.
                    fsm_state_ = fsm_state::Disconnected;
                    co_return fixpp::core::expected_t<void>{};
                }

                auto chk = co_await seqnum_mgr_.check_inbound(seq);
                if (!chk) {
                    // Too-low (session_seqnum_too_low=69) or
                    // too-high (session_seqnum_gap_unrecoverable=70) → session-fatal.
                    fsm_state_ = fsm_state::Disconnected;
                    co_return fixpp::core::expected_t<void>{};
                }
            }

            // T046 (US4): inbound Logout in Active/LogonReceived state.
            // Per data-model.md matrix:
            //   Active row:        inbound Logout → emit Logout, → Disconnected.
            //   LogonReceived row: inbound Logout → Disconnected ([FIX-SL §4.6]).
            if (hdr.msg_type == "5") {  // Logout (35=5)
                if (fsm_state_ == fsm_state::Active) {
                    // Active → emit confirming Logout → Disconnected.
                    // Emit a confirming Logout via store_then_emit.
                    // Use a stack buffer (I-7: no heap on inbound-dispatch path).
                    std::array<std::byte, 256> buf{};
                    auto logout_result = fixpp::session::build_logout(
                        std::span<std::byte>{buf.data(), buf.size()},
                        next_outbound_seq_++,
                        cfg_.sender_comp_id,
                        cfg_.target_comp_id,
                        {});
                    if (logout_result) {
                        auto emit_r = co_await store_then_emit(*logout_result);
                        (void)emit_r;  // errors logged-then-proceed (I-07)
                    }
                }
                // Both Active and LogonReceived → Disconnected.
                fsm_state_ = fsm_state::Disconnected;
                co_return fixpp::core::expected_t<void>{};
            }

            // I-5: inbound Reject(35=3) is logged and accepted; never re-rejected.
            if (hdr.msg_type == "3") {  // Reject (35=3)
                // Active row: session-level log, no Reject-of-a-Reject (I-5).
                // Remain in current state.
                co_return fixpp::core::expected_t<void>{};
            }

            // T041 (US3): in the Active state, update liveness state and handle
            // liveness-specific message types (Heartbeat / TestRequest).
            if (fsm_state_ == fsm_state::Active) {
                // Update last_inbound_steady_ — used by run_liveness_loop to
                // detect inbound silence windows.
                if (effective_clock_) {
                    last_inbound_steady_ = effective_clock_->steady_now();
                }

                // T041 US3: Active row — inbound Heartbeat → advance counter
                // (liveness). If we had an outstanding TestRequest and this
                // Heartbeat echoes it (same TestReqID), clear the outstanding flag.
                if (hdr.msg_type == "0") {  // Heartbeat (35=0)
                    if (!pending_test_req_id_.empty()) {
                        pending_test_req_id_.clear();
                        unanswered_tr_ = false;
                    }
                    // Remain in Active — liveness tick.
                    co_return fixpp::core::expected_t<void>{};
                }

                // T041 US3 / T042 retirement (US4): Active row — inbound
                // TestRequest (35=1) → emit Heartbeat echoing TestReqID(112).
                if (hdr.msg_type == "1") {  // TestRequest (35=1)
                    std::array<std::byte, 256> hb_buf{};
                    auto hb_result = fixpp::session::build_heartbeat(
                        std::span<std::byte>{hb_buf.data(), hb_buf.size()},
                        next_outbound_seq_++,
                        cfg_.sender_comp_id,
                        cfg_.target_comp_id,
                        hdr.test_req_id);
                    if (hb_result) {
                        auto emit_r = co_await store_then_emit(*hb_result);
                        (void)emit_r;
                    }
                    // Remain in Active.
                    co_return fixpp::core::expected_t<void>{};
                }

                // ── Guard (5): message-type-for-state (T056 US5) ─────────────
                // Session admin types known to Active: 0/1/3/5/A (handled above).
                // Any other MsgType in Active → session-level Reject(35=3) with
                // SessionRejectReason and RefMsgType. Session stays Active.
                // No-reject-loop: guard (type == "3" || type == "5") exempted above.
                {
                    // Known session admin MsgTypes (all others → Reject).
                    // "A" = Logon, "0" = Heartbeat, "1" = TestRequest,
                    // "2" = ResendRequest, "3" = Reject, "4" = SeqReset, "5" = Logout.
                    // 2/4 (RR/SeqReset) are deferred admin; they still get a Reject
                    // per data-model matrix (session_admin_not_supported, slot 75).
                    const bool is_session_admin =
                        (hdr.msg_type == "A" ||   // Logon (dup in Active)
                         hdr.msg_type == "0" ||   // Heartbeat
                         hdr.msg_type == "1" ||   // TestRequest
                         hdr.msg_type == "2" ||   // ResendRequest (deferred → Reject)
                         hdr.msg_type == "3" ||   // Reject (handled above)
                         hdr.msg_type == "4" ||   // SequenceReset-GapFill (deferred → Reject)
                         hdr.msg_type == "5");    // Logout (handled above)

                    if (!is_session_admin) {
                        // Unknown / app-type MsgType in Active →
                        // Reject(reason=session_msg_type_invalid_for_state=3).
                        // SessionRejectReason 3 = unsupported message type per [FIX-SL §4.5.4].
                        std::array<std::byte, 512> rj_buf{};
                        const seqnum_t ref_seq = parse_seqnum(hdr.msg_seq_num);
                        auto rj_result = fixpp::session::build_reject(
                            std::span<std::byte>{rj_buf.data(), rj_buf.size()},
                            next_outbound_seq_++,
                            cfg_.sender_comp_id,
                            cfg_.target_comp_id,
                            ref_seq,
                            0,                // RefTagID: n/a for MsgType rejection
                            hdr.msg_type,     // RefMsgType: the offending MsgType
                            3);               // SessionRejectReason = 3 (invalid MsgType)
                        if (rj_result) {
                            auto emit_r = co_await store_then_emit(*rj_result);
                            (void)emit_r;
                        }
                        // Remain in Active — session stays after sending Reject.
                        co_return fixpp::core::expected_t<void>{};
                    }
                }
            }

            // In-sequence: counter advanced. Remain in current state.
            co_return fixpp::core::expected_t<void>{};
        }

        case fsm_state::LogoutSent: {
            // Per matrix LogoutSent row:
            //   inbound Logout → Disconnected (confirm)
            //   all other inbound → (drained) — silently accepted, no FSM change
            //     (seqnum NOT advanced, no fromAdmin/fromApp dispatch)
            auto hdr = scan_frame_header(frame);
            if (hdr.msg_type == "5") {  // Logout(35=5) confirms our Logout
                fsm_state_        = fsm_state::Disconnected;
                logout_confirmed_ = true;  // signal run_logout_phase1 coroutine
                // Wake up the sleep_until in run_logout_phase1 so it can
                // detect the confirmation and return early (before the 2s timeout).
                if (effective_clock_) {
                    effective_clock_->cancel_sleeps();
                }
            }
            // All other inbound frames: drained (no FSM change, no seqnum advance).
            co_return fixpp::core::expected_t<void>{};
        }

        case fsm_state::LogonSent: {
            // Initiator path: Logon emitted, awaiting peer Logon ack.
            // Per data-model.md matrix LogonSent row:
            //   inbound Logon (valid)            → Active (validate HeartBtInt/CompID/BeginString)
            //   inbound Logon (refused)          → Disconnected
            //   inbound Logout                   → Disconnected ([FIX-SL §4.6])
            //   inbound Heartbeat/TR/Reject/oos  → session-fatal Logout+disconnect
            //   seqnum too-low / too-high        → fatal Logout(text)+disconnect
            //
            // Phase 4 (US2) scope: emit-Logout-then-Disconnected is deferred to
            // US4/Phase 6 (when the Logout build/send path is wired); for now
            // we transition directly to Disconnected on the fatal/refusal cells,
            // matching the same pattern Phase 3 used for NotConnected refusals.
            auto result = fixpp::session::interpret_logon(
                frame,
                cfg_.target_comp_id,
                cfg_.sender_comp_id,
                cfg_.begin_string);

            if (!result) {
                // Either a refused Logon (CompID/BeginString) OR a non-Logon
                // inbound (Heartbeat/TestRequest/Reject/out-of-scope admin /
                // invalid MsgType). Per matrix LogonSent row: every one of
                // these cells transitions to Disconnected (with Logout in US4).
                fsm_state_ = fsm_state::Disconnected;
                co_return fixpp::core::expected_t<void>{};
            }

            // Valid Logon-ack shape: scan header fields (SendingTime + seqnum).
            auto hdr = scan_frame_header(frame);

            // ── Guard (3): SendingTime MaxLatency — Logon-path special case ───
            // D-3 / FR-013: if the inbound Logon's own SendingTime is stale,
            // the response is a logout-with-error (Logout only — no standalone
            // Reject, because no session is established yet).
            if (!hdr.sending_time.empty() && effective_clock_) {
                auto parse_r = fixpp::core::fix_string_to_utc_time(
                    std::span<const char>{hdr.sending_time.data(),
                                         hdr.sending_time.size()});
                if (parse_r) {
                    const auto max_lat = cfg_.sending_time_threshold.has_value()
                        ? std::chrono::duration_cast<std::chrono::seconds>(
                              *cfg_.sending_time_threshold)
                        : std::chrono::seconds{120};  // D-8 default 120 s
                    auto chk_st = fixpp::session::check_sending_time(
                        *parse_r,
                        effective_clock_->now(),
                        max_lat);
                    if (!chk_st) {
                        // Logon-path Q3: emit Logout only (no standalone Reject — D-3).
                        std::array<std::byte, 256> lo_buf{};
                        auto lo_result = fixpp::session::build_logout(
                            std::span<std::byte>{lo_buf.data(), lo_buf.size()},
                            next_outbound_seq_++,
                            cfg_.sender_comp_id,
                            cfg_.target_comp_id,
                            {});
                        if (lo_result) {
                            auto emit_r = co_await store_then_emit(*lo_result);
                            (void)emit_r;
                        }
                        fsm_state_ = fsm_state::Disconnected;
                        co_return fixpp::core::expected_t<void>{};
                    }
                }
                // parse failure: accept the Logon (lenient on malformed timestamp)
            }

            // ── Guard (4): seqnum check (T035 LogonSent row) ─────────────────
            const seqnum_t seq = parse_seqnum(hdr.msg_seq_num);
            if (seq == 0) {
                fsm_state_ = fsm_state::Disconnected;
                co_return fixpp::core::expected_t<void>{};
            }

            auto chk = co_await seqnum_mgr_.check_inbound(seq);
            if (!chk) {
                // Too-low or too-high → fatal (recovery deferred; I-2/I-4).
                fsm_state_ = fsm_state::Disconnected;
                co_return fixpp::core::expected_t<void>{};
            }

            // Valid Logon-ack + in-seq → Active (initiator handshake complete).
            fsm_state_ = fsm_state::Active;

            // T041 (US3): seed last_inbound_steady_ from this Logon-ack.
            if (effective_clock_) {
                last_inbound_steady_ = effective_clock_->steady_now();
            }

            // T039/T041 (US3): co_spawn the liveness loop on the session executor
            // with the root cancellation slot so Session::close() can cancel it.
            // asio::bind_cancellation_slot threads the root slot into the spawned
            // coroutine, overriding the default terminal-only slot
            // ([feedback_asio_cospawn_total_cancellation_default]).
            {
                auto ex = co_await asio::this_coro::executor;
                asio::co_spawn(
                    ex,
                    run_liveness_loop(),
                    asio::bind_cancellation_slot(
                        root_cancel_.slot(),
                        asio::detached));
            }

            co_return fixpp::core::expected_t<void>{};
        }

        case fsm_state::Disconnected:
            // Disconnected row: all inbound cells `ignored` per matrix.
            co_return fixpp::core::expected_t<void>{};
    }

    co_return fixpp::core::expected_t<void>{};
}

asio::awaitable<fixpp::core::expected_t<void>>
Session::send(std::span<const std::byte> /*app_payload*/) noexcept {
    // PLACEHOLDER — durable-before-transmit path wired per US1 T023 (Phase 3).
    // Outbound ordering: store(seq, committed, outbound) BEFORE transport::async_write (I-3).
    co_return fixpp::core::expected_t<void>{};
}

fsm_state Session::state() const noexcept {
    return fsm_state_;
}

// ── T039/T041 (US3): Liveness loop ───────────────────────────────────────────
//
// run_liveness_loop() — the heartbeat / test-request / unanswered-TR timer
// coroutine. Co_spawned (asio::detached) when the session first enters Active.
// Runs on the session executor; cancelled via the root cancellation slot when
// Session::close() fires.
//
// Algorithm:
//   1. If HeartBtInt = 0 → return immediately (all liveness disabled, FR-006).
//   2. Loop until the session is no longer Active or cancellation fires:
//      a. Sleep until last_inbound_steady_ + heartbt_int.
//      b. If session is no longer Active → stop.
//      c. If inbound data arrived during the sleep (last_inbound_steady_
//         updated past the deadline) → reset and loop (no TestRequest needed).
//      d. No inbound data: emit TestRequest (record pending_test_req_id_).
//      e. Sleep another heartbt_int.
//      f. If still no inbound Heartbeat reply (pending_test_req_id_ still set):
//         → session_test_request_unanswered → Disconnected → stop.
//      g. If a Heartbeat was received (pending_test_req_id_ cleared by
//         on_inbound_frame) → loop continues.
//
// Traps observed in project memory:
//   [feedback_asio_cospawn_total_cancellation_default]: co_spawn defaults to
//     terminal-only; must reset to enable_total_cancellation for close() to
//     cancel the sleep_until.
//   [feedback_asio_post_resume_bounces_to_spawn_executor]: we co_spawn on the
//     session executor directly, so resumes stay on that executor.
//   [feedback_async_mutex_us3_asio_cancel_and_subagent_seams]: wrap the outer
//     loop in try/catch(asio::system_error) to convert operation_aborted into
//     clean return (not a crash).
//
// HeartBtInt source: cfg_.heartbeat_interval (std::optional<seconds>, D-8).
// Default: 30s if not set; HeartBtInt=0 disables.
asio::awaitable<void> Session::run_liveness_loop() noexcept {
    using namespace std::chrono_literals;

    // [feedback_asio_cospawn_total_cancellation_default]: reset the coroutine's
    // cancellation filter to accept total cancellation. The root slot was bound
    // at co_spawn time (via bind_cancellation_slot), so this only changes the
    // filter — it does not reassign the slot's backing storage.
    co_await asio::this_coro::reset_cancellation_state(
        asio::enable_total_cancellation{});

    // Resolve HeartBtInt from config (D-8 default: 30s; 0 = disabled).
    std::chrono::seconds heartbt_int{30};
    if (cfg_.heartbeat_interval.has_value()) {
        heartbt_int = *cfg_.heartbeat_interval;
    }

    // HeartBtInt=0 → all liveness disabled (FR-006 / [FIX-SL §4.3.4]).
    if (heartbt_int.count() == 0) {
        co_return;
    }

    if (!effective_clock_) {
        co_return;  // No clock (should not happen post-open(), but guard).
    }

    // Test-request threshold: 1 × HeartBtInt (D-8 default).
    std::chrono::milliseconds threshold =
        std::chrono::duration_cast<std::chrono::milliseconds>(heartbt_int);
    if (cfg_.test_request_threshold.has_value()) {
        threshold = *cfg_.test_request_threshold;
    }

    // Liveness loop.
    try {
        while (fsm_state_ == fsm_state::Active) {
            // Compute the next sleep deadline: last known inbound + heartbt_int.
            auto deadline = last_inbound_steady_ + heartbt_int;

            // Sleep until that deadline (or until cancellation fires).
            co_await effective_clock_->sleep_until(deadline);

            // Check if we're still Active after the sleep.
            if (fsm_state_ != fsm_state::Active) {
                co_return;
            }

            // Did inbound data arrive during the sleep (updating last_inbound_steady_)?
            auto now = effective_clock_->steady_now();
            if (last_inbound_steady_ + heartbt_int > now) {
                // Inbound data arrived; the deadline was reset. Loop again.
                continue;
            }

            // No inbound data for heartbt_int: emit TestRequest.
            // Generate a unique TestReqID (simple sequential counter).
            static std::uint32_t tr_counter = 0;
            char id_buf[32];
            std::snprintf(id_buf, sizeof(id_buf), "TR%u", ++tr_counter);
            pending_test_req_id_ = id_buf;
            unanswered_tr_       = false;

            // T042 (US4 retirement): emit the TestRequest frame via
            // store_then_emit (I-3 outbound half). Stack buffer; noexcept.
            {
                std::array<std::byte, 256> tr_buf{};
                auto tr_result = fixpp::session::build_test_request(
                    std::span<std::byte>{tr_buf.data(), tr_buf.size()},
                    next_outbound_seq_++,
                    cfg_.sender_comp_id,
                    cfg_.target_comp_id,
                    pending_test_req_id_);
                if (tr_result) {
                    auto emit_r = co_await store_then_emit(*tr_result);
                    (void)emit_r;
                }
            }

            // Grace window: sleep another heartbt_int.
            auto grace_deadline = now + heartbt_int;
            co_await effective_clock_->sleep_until(grace_deadline);

            if (fsm_state_ != fsm_state::Active) {
                co_return;
            }

            // Still Active after grace window. Did a Heartbeat arrive?
            if (!pending_test_req_id_.empty()) {
                // No Heartbeat reply received — unanswered TestRequest.
                // Per data-model Active row + [FIX-SL §4.5.5]:
                // session_test_request_unanswered (slot 74) → Disconnected.
                unanswered_tr_ = true;
                fsm_state_ = fsm_state::Disconnected;
                // (Phase 6 US4 wires the Logout emission before Disconnected.)
                co_return;
            }

            // Heartbeat was received (pending_test_req_id_ cleared by
            // on_inbound_frame). Loop continues for the next window.
        }
    } catch (const std::system_error& e) {
        // operation_aborted from sleep_until cancellation (close() fired).
        // [feedback_async_mutex_us3_asio_cancel_and_subagent_seams]
        // Convert to clean return — not a fatal error.
        (void)e;
    } catch (...) {
        // Any other exception: absorb (noexcept window).
    }
}

// ── US4 T046: store_then_emit ─────────────────────────────────────────────────
//
// Durable-before-transmit (I-3 outbound half):
//   1. If store_ is available: co_await store_->store(seq, frame, outbound).
//   2. AFTER store returns: call transport_send_(frame) if non-null.
// Errors from store(): logged-then-proceed (I-07); close still completes.
// The outbound seqnum counter (next_outbound_seq_) is already advanced by
// the caller before passing the committed frame.
asio::awaitable<fixpp::core::expected_t<void>>
Session::store_then_emit(std::span<const std::byte> frame) noexcept {
    // Step 1: store (durable-before-transmit).
    if (store_) {
        // The outbound seqnum was already stamped into the frame by the builder;
        // pass next_outbound_seq_ - 1 (the value that was stamped).
        const seqnum_t stamped_seq = next_outbound_seq_ - 1u;
        auto store_r = co_await store_->store(stamped_seq, frame, direction_t::outbound);
        (void)store_r;  // store errors → logged-then-proceed (I-07)
    }

    // Step 2: transmit (ONLY after store completes — I-3).
    if (transport_send_) {
        // FR-15 noexcept window: throwing user callback must trap, not propagate.
        // Durable-before-transmit is already satisfied → log-then-proceed.
        try {
            transport_send_(frame);
        } catch (...) {
        }
    }

    co_return fixpp::core::expected_t<void>{};
}

// ── US4 T047: run_logout_phase1 ───────────────────────────────────────────────
//
// Two-phase graceful Logout (D-6 / [2d §6.5] / I-9):
//   1. Build Logout frame (build_logout) + store_then_emit (I-3 outbound half).
//   2. Transition FSM to LogoutSent.
//   3. Sleep up to 2 s (D-8: session_logout_timeout default) under a CHILD
//      cancellation_state, waking early if logout_confirmed_ is set by
//      on_inbound_frame() when the peer's confirming Logout arrives.
//   4. If confirmed → ok.
//      If timeout → transition to Disconnected, return session_logout_timeout.
//
// Called from close(graceful) phase 1 ONLY. The root cancellation_state fires
// phase-2 total AFTER this coroutine returns (either path).
//
// [feedback_asio_cospawn_total_cancellation_default]: this coroutine is called
// via co_await from close(); the current coroutine's cancellation state is
// already bound to the root slot. We do NOT reset here — the caller (close)
// controls the root slot. Instead we use a loop-and-check polling pattern
// for the logout confirmation, which is safe on the single-strand session.
asio::awaitable<fixpp::core::expected_t<void>>
Session::run_logout_phase1() noexcept {
    using namespace std::chrono_literals;
    using std::chrono::seconds;

    // Build the Logout frame into a stack buffer.
    std::array<std::byte, 256> buf{};
    auto logout_result = fixpp::session::build_logout(
        std::span<std::byte>{buf.data(), buf.size()},
        next_outbound_seq_++,
        cfg_.sender_comp_id,
        cfg_.target_comp_id,
        {});

    if (!logout_result) {
        // Build failure (unlikely): treat as no-frame-sent, proceed to timeout.
        // The session still transitions to LogoutSent and times out.
    } else {
        // Emit the Logout frame (store first, then transport_send — I-3).
        auto emit_r = co_await store_then_emit(*logout_result);
        (void)emit_r;
    }

    // Transition to LogoutSent.
    fsm_state_        = fsm_state::LogoutSent;
    logout_confirmed_ = false;

    if (!effective_clock_) {
        // No clock (should not happen post-open): force-disconnect immediately.
        fsm_state_ = fsm_state::Disconnected;
        co_return std::unexpected(fixpp::core::error::session_logout_timeout);
    }

    // Sleep until the 2 s graceful-close timeout (D-8: session_logout_timeout).
    // on_inbound_frame() will set logout_confirmed_=true AND call
    // effective_clock_->cancel_sleeps() when the peer's confirming Logout arrives,
    // waking us up early. When cancel_sleeps fires, sleep_until throws
    // system_error(operation_aborted); we catch it and check logout_confirmed_.
    auto deadline = effective_clock_->steady_now() + seconds{2};
    try {
        co_await effective_clock_->sleep_until(deadline);
    } catch (const std::system_error&) {
        // Woken early: either peer confirmed (logout_confirmed_=true)
        // or root cancellation fired (phase 2). Check the flag.
    } catch (...) {
        // Any other exception: absorb (noexcept window).
    }

    if (logout_confirmed_) {
        // Peer confirmed; FSM already set to Disconnected by on_inbound_frame.
        co_return fixpp::core::expected_t<void>{};
    }

    // Timeout (or root cancellation before confirm): force-disconnect.
    fsm_state_ = fsm_state::Disconnected;
    co_return std::unexpected(fixpp::core::error::session_logout_timeout);
}

}  // namespace fixpp::session
