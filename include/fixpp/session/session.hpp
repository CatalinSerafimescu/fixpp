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
#include <cstdint>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <utility>

#include <asio/awaitable.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/post.hpp>

#include <fixpp/core/error.hpp>          // expected_t
#include <fixpp/core/session_executor.hpp>
#include <fixpp/core/session_local.hpp>
#include <fixpp/core/trace_context.hpp>

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
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
        close(close_mode = close_mode::graceful) noexcept;

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
                       const bool prev =
                           in_dispatch_.exchange(true, std::memory_order_acq_rel);
                       assert(!prev &&
                              "concurrent session callback entry: strand "
                              "invariant violated (direct_executor attested "
                              "over a non-serialised executor?)");
#endif
                       g();
#ifndef NDEBUG
                       in_dispatch_.store(false, std::memory_order_release);
#endif
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
    // installed hook simply skips phase-1 flush (no store wired yet — the
    // real unique_ptr<MessageStore> friend call is 005/2e's).
    close_flush_hook close_flush_hook_;

    // Root cancellation signal (T039). Phase 2 fires cancellation_type::total
    // on this; in-flight work bound to root_cancellation_slot() unwinds.
    asio::cancellation_signal root_cancel_;

    // Idempotent THREE-STATE close (I-10): the FIRST close() runs the body
    // and caches its result here; a concurrent/subsequent call while
    // state_==closing observes the SAME in-flight result (no error, no side
    // effects) by awaiting this shared slot rather than re-running phase 1/2.
    std::shared_ptr<std::optional<fixpp::core::expected_t<void>>>
        close_result_;
};

}  // namespace fixpp::session
