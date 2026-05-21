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

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <utility>

#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>             // expected_t, error values
#include <fixpp/core/session_executor.hpp>
#include <fixpp/session/message_store.hpp>        // 008-message-store — store_ unique_ptr dtor
#include <fixpp/session/message_store_factory.hpp>  // 008-message-store — make() call site
#include <fixpp/session/admin_messages.hpp>     // 005 US1: interpret_logon (T024/T025)
#include <fixpp/session/session_fsm.hpp>        // 005 US1: fsm_state enum (T023–T025)
#include <fixpp/session/seqnum_manager.hpp>     // 005 US2: SeqnumManager (T031)
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/security_profile.hpp>  // SecurityProfile::kind::unset sentinel check (lives in `session` per [arch §6 line 243])

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
    }

    // T037/T039 phase 2 — fire root cancellation_type::total ONLY after
    // phase 1 has resolved (peer ACK | child timeout | child cancelled —
    // collapsed to "phase 1 done" in the scripted scope). This is the single
    // propagation point: every strand of in-flight session work bound to
    // root_cancellation_slot() (transport r/w, heartbeat sleep, mutex
    // acquire, cancellable_dispatch, parser→fromApp — all 005-owned) unwinds
    // here. terminal reaches phase 2 immediately (phase 1 skipped).
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
// Reads tag 8 (BeginString), 34 (MsgSeqNum), 49 (SenderCompID),
// 56 (TargetCompID) from a raw FIX frame.
// Used by the inbound dispatch path (scenarios 2i/2k/seqnum check).
struct FrameHeader {
    std::string_view begin_string;
    std::string_view sender_comp_id;
    std::string_view target_comp_id;
    std::string_view msg_seq_num;   // tag 34 raw string value
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
            case 8:  h.begin_string    = val; break;
            case 34: h.msg_seq_num     = val; break;
            case 49: h.sender_comp_id  = val; break;
            case 56: h.target_comp_id  = val; break;
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

// T024/T025 (US1, Phase 3) + T032/T034/T035 (US2, Phase 4): Inbound FSM dispatch.
//
// Guard precedence per data-model.md matrix preamble:
//   (1) CompID/BeginString gate (NotConnected / post-logon states)
//   (2) seqnum class (too-low / too-high / in-seq) — T035
//   (3) message-type-for-state (US5, Phase 7)
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
            // Post-logon inbound: screen BeginString and CompID (scenarios 2i/2k).
            // Any mismatch → session-fatal → Disconnected.
            auto hdr = scan_frame_header(frame);
            if (hdr.begin_string != cfg_.begin_string ||
                hdr.sender_comp_id != cfg_.target_comp_id ||
                hdr.target_comp_id != cfg_.sender_comp_id) {
                fsm_state_ = fsm_state::Disconnected;
                co_return fixpp::core::expected_t<void>{};
            }

            // T035: seqnum check for post-logon inbound messages.
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
                    // Phase 4 (US2): transition to Disconnected.
                    // Phase 6 (US4) wires the Logout emission before Disconnected.
                    fsm_state_ = fsm_state::Disconnected;
                    co_return fixpp::core::expected_t<void>{};
                }
            }

            // In-sequence: counter advanced. Remain in current state.
            // fromAdmin/fromApp dispatch is wired in Phase 6 (US4).
            co_return fixpp::core::expected_t<void>{};
        }

        case fsm_state::LogonSent: {
            // Initiator path: Logon emitted, awaiting peer Logon ack.
            // Per data-model.md matrix LogonSent row:
            //   inbound Logon (valid)            → Active (validate HeartBtInt/CompID/BeginString)
            //   inbound Logon (refused)          → Disconnected
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

            // Valid Logon-ack shape: now check seqnum (T035 LogonSent row).
            auto hdr = scan_frame_header(frame);
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
            co_return fixpp::core::expected_t<void>{};
        }

        case fsm_state::LogoutSent:
        case fsm_state::Disconnected:
            // Defined cells: drain without FSM change.
            // LogoutSent row: all inbound cells are `(drained)` per matrix
            // (counter NOT advanced, no dispatch, no emit); the non-drained
            // cells (inbound Logout → Disconnected; graceful-close timeout →
            // Disconnected) are wired in US4/Phase 6.
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

}  // namespace fixpp::session
