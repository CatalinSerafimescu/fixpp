// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/006-async-mutex/contracts/async_mutex.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// SHAPE ORACLE (NOT THE BUILD HEADER)
// This file is a planning-phase contract artifact.
// It specifies the exact class surface that the implementation MUST match.
// It is NOT compiled into the library.
//
// Build header target: include/fixpp/core/sync/async_mutex.hpp
//   Namespace: fixpp::sync
// ─────────────────────────────────────────────────────────────────────────────
//
// Source of truth: .specify/2f-async-mutex.md v1.5 §4.1 + §6.2 + §1.1
//
// async_mutex — the awaitable mutex value type.
//   - The only legal mutex shape in coroutine context per [const §XI.3].
//   - CI-enforced via tools/check_no_std_mutex_in_awaitable_headers.sh
//     (post-preprocessing scope per [const §XV.9]).
//   - Algorithm: own implementation; BSL-1.0 algorithm attribution to
//     avast/asio-mutex (Lewis-Baker / cppcoro std::atomic<uintptr_t> state
//     encoding with not_locked/locked_no_waiters/pointer-to-LIFO sentinel).
//   - NOT a plugin: zero pure-virtual, not a base class, not extensible.
//   - Non-copyable, non-movable. Consumer holds by value at a stable address.
//   - constexpr-default-constructible; no executor dependency.
//
// State encoding (std::atomic<uintptr_t> state_):
//   state_ == not_locked        (= 1)  — free; low-bit set, distinguishable
//                                        from any 8-byte-aligned waiter ptr.
//   state_ == locked_no_waiters (= 0)  — held, LIFO list empty.
//   state_ == <pointer-to-waiter>      — held, head of LIFO list.
//
// Key version milestones:
//   v1.1 RC-A: mutex-owned residual FIFO via next_drain_head_ (replaces
//              awaiter-owned residual_, solves v1.0 UAF defect).
//   v1.1 RC-B: draining_ + drain_in_progress_ + cancel_and_drain() primitive.
//   v1.2 RC-α: active_holders_count_ (winner-only post-CAS).
//   v1.3 RC-α: active_acquirers_count_ (in-flight acquirer epoch counter).
//   v1.3 RC-β: lazy drain_latch_ptr_ (shared_ptr<drain_latch_state>) replaces
//              non-implementable v1.2 by-value detail::drain_latch.
//   v1.4:      CAS-then-publish protocol on result_ writes; drain_latch_ptr_
//              release-stored before draining_ (ordering guarantee).
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
// #include <asio/awaitable.hpp>             (build header only)
// #include <memory_resource>                (build header only)
// #include "fixpp/core/error.hpp"           (build header only)
// #include "fixpp/core/sync/async_lock_guard.hpp"   (or same header)

namespace fixpp::sync {

class async_lock_guard;

namespace detail {
enum class waiter_phase : std::uint8_t;
class async_mutex_awaiter;
class slot_allocator;
class drain_latch_state;
}  // namespace detail

enum class completion_policy : std::uint8_t {
    dispatch = 0,
    post     = 1,
};

class async_mutex {
public:
    // ─────────────────────────────────────────────────────────────────────────
    // Constructors

    // Default: unlocked mutex with dispatch completion policy.
    // constexpr, noexcept, no executor dependency.
    constexpr async_mutex() noexcept = default;

    // Explicit completion policy.
    explicit constexpr async_mutex(completion_policy cp) noexcept
        : policy_(cp) {}

    // Non-copyable, non-movable — a movable mutex would have to patch every
    // in-flight awaiter's mutex_ back-pointer atomically with the move,
    // which is fragile under cancellation.
    async_mutex(async_mutex const&)            = delete;
    async_mutex(async_mutex&&)                 = delete;
    async_mutex& operator=(async_mutex const&) = delete;
    async_mutex& operator=(async_mutex&&)      = delete;

    // ─────────────────────────────────────────────────────────────────────────
    // Destructor (RC#3 fix)
    //
    // std::terminate() precondition (fires in BOTH debug AND release via
    // fixpp::core::abort_invariant) if the mutex is held OR waiters are present
    // in state_ or next_drain_head_. Hard precondition; no release-mode UB.
    //
    // Callers MUST drain via cancel_and_drain() before destruction. The
    // canonical pattern:
    //   co_await mutex.cancel_and_drain();  // waits for all waiters + holders
    //   // ... mutex is now safe to destroy ...
    ~async_mutex();

    // ─────────────────────────────────────────────────────────────────────────
    // Primary acquire surface

    // async_lock — acquire the mutex asynchronously.
    //
    // Returns an awaitable that completes:
    //   - with async_lock_guard         — mutex acquired (RAII; releases on dtor).
    //   - with unexpected{sync_lock_aborted}  (=43) — cancellation won the
    //                                      §4.5 CAS-arbitration race.
    //   - with unexpected{sync_lock_drained}  (=46) — mutex already drained via
    //                                      cancel_and_drain() (RC-B close).
    //   - with unexpected{sync_lock_alloc_failed} (=44) — PMR allocation failure.
    //
    // PMR-aware fallback (RC#2 fix):
    //   mr == nullptr (default) — awaiter embedded in caller's coroutine frame.
    //     HALO-eligible per [const §XI.6]: awaiter ≤ 96 B (v1.1 layout).
    //     Zero allocation on the v1.0 hot path.
    //   mr != nullptr — awaiter allocated from mr. Type-erased completion
    //     handlers (asio::any_completion_handler) and composed operations pass
    //     mr explicitly. The session-side helper (E7, declared separately in
    //     session/) recovers mr from the bound session_executor.
    //
    // The cancellation slot's handler-closure storage is bound via
    // asio::bind_allocator(detail::slot_allocator{this, mr}) at await_suspend
    // time (RC-C close — Codex C-P2-7).
    //
    // Cancellation: honours total (waiter unlinked, completes with
    // unexpected{sync_lock_aborted}); partial is treated as total; terminal
    // is treated as total.
    //
    // Completion: runs on the awaiter's bound executor per [2d §7.4]; inline
    // vs. post governed by policy_ (§4.6).
    //
    // [[nodiscard]]: co_await result MUST be consumed.
    [[nodiscard]]
    /* asio::awaitable<expected_t<async_lock_guard>> */
    void  // placeholder — replace with asio::awaitable<expected_t<async_lock_guard>>
        async_lock(/* std::pmr::memory_resource* mr = nullptr */ int mr = 0)
        noexcept;

    // ─────────────────────────────────────────────────────────────────────────
    // Drain primitive (RC#3 + RC-B + RC-α + RC-β)

    // cancel_and_drain — drain the mutex of all current and future acquisitions.
    //
    // Idempotent (second call returns immediately if drain already published).
    // Concurrent-call-safe (drain_in_progress_ serialises reapers; non-reapers
    // subscribe to the same drain_latch_state).
    //
    // Algorithm summary (v1.3 code shape):
    //   (a) Idempotent fast path — draining_.load(acquire). If true, load
    //       drain_latch_ptr_ (acquire); if non-null, co_await state->wait();
    //       return released_ ? {} : unexpected{sync_lock_aborted}.
    //   (b) Concurrent-call serialiser — drain_in_progress_.test_and_set(acq_rel).
    //       Non-reapers spin-wait until draining_ is visible, then subscribe.
    //   (c) Allocate lazy drain_latch_state via make_shared (inside this coro
    //       frame); drain_latch_ptr_.store(state, release) BEFORE
    //       draining_.store(true, release) — v1.4 ordering guarantee.
    //   (d) Bind reaper's cancellation_state.
    //   (e) Sentinel-discriminated atomic exchange of both state_ and
    //       next_drain_head_.
    //   (f) reap_chain: for each queued waiter: phase CAS queued→cancelled
    //       (acq_rel); CAS winner writes *result_ = unexpected{sync_lock_aborted}
    //       and schedules bound-executor resumption (winner-only — v1.4
    //       CAS-then-publish); captures latch_state shared_ptr.
    //   (g) Stable re-walk loop: re-exchange both lists until both observe
    //       nullptr (closes Opus C-R3-P1-3 unlock-vs-reaper splice race).
    //   (h) co_await latch_state->wait() until:
    //       active_holders_count_ == 0 AND
    //       active_acquirers_count_ == 0 AND
    //       in_flight_resumptions_ == 0.
    //   (i) signal_release() → co_return {}; OR, if reaper is cancelled:
    //       signal_abort() → co_return unexpected{sync_lock_aborted}.
    //
    // Returns:
    //   {}                        — drain complete; mutex safe to destroy.
    //   unexpected{sync_lock_aborted} — the cancel_and_drain awaitable was
    //                               itself cancelled (draining_ stays true;
    //                               subsequent async_lock returns
    //                               unexpected{sync_lock_drained}).
    [[nodiscard]]
    /* asio::awaitable<expected_t<void>> */
    void  // placeholder — replace with asio::awaitable<expected_t<void>>
        cancel_and_drain() noexcept;

    // ─────────────────────────────────────────────────────────────────────────
    // Release

    // unlock — release the mutex. Walks next_drain_head_ first (RC-A), then
    // the LIFO from state_. The first non-cancelled queued waiter is granted
    // ownership; remaining FIFO tail is spliced into next_drain_head_.
    //
    // Under draining_ == true: does NOT splice; decrements active_holders_count_
    // and notifies the drain latch (Opus C-R3-P1-3 close).
    //
    // Precondition: the mutex is held. UB in release if violated.
    // (Generally invoked via async_lock_guard's destructor, not directly.)
    void unlock() noexcept;

    // ─────────────────────────────────────────────────────────────────────────
    // Accessor

    [[nodiscard]] completion_policy policy() const noexcept { return policy_; }

private:
    // ─────────────────────────────────────────────────────────────────────────
    // State encoding constants

    static constexpr uintptr_t not_locked        = 1;  // free; low bit set.
    static constexpr uintptr_t locked_no_waiters = 0;  // held; LIFO empty.

    // ─────────────────────────────────────────────────────────────────────────
    // Fields (layout order is normative; implementation MUST preserve it for
    // the cache-line locality guarantee stated in §6.3 row 1 footnote)

    // Primary state atom — Lewis-Baker / cppcoro encoding.
    std::atomic<uintptr_t>                              state_             {not_locked};

    // RC-A v1.1 — mutex-owned residual FIFO chain.
    std::atomic<detail::async_mutex_awaiter*>           next_drain_head_   {nullptr};

    // RC-B v1.1 — drain flag; set by cancel_and_drain(), never cleared.
    std::atomic<bool>                                   draining_          {false};

    // RC-B v1.1 (Opus N-P2-1) — concurrent-call serialiser.
    std::atomic_flag                                    drain_in_progress_ = ATOMIC_FLAG_INIT;

    // v1.2 / v1.3 RC-α — winner-only post-CAS holder count.
    std::atomic<std::uint32_t>                          active_holders_count_   {0};

    // NEW v1.3 RC-α — in-flight acquirer epoch counter.
    std::atomic<std::uint32_t>                          active_acquirers_count_ {0};

    // NEW v1.3 RC-β; UPDATED v1.4 — lazy drain latch.
    // NOT lock-free in general; cold path only.
    std::atomic<std::shared_ptr<detail::drain_latch_state>>
                                                        drain_latch_ptr_   {};

    // Per-mutex completion policy (immutable after construction).
    completion_policy const                             policy_            {completion_policy::dispatch};

    friend class detail::async_mutex_awaiter;
};

}  // namespace fixpp::sync

// ─────────────────────────────────────────────────────────────────────────────
// Compile-time invariants (order-valid placement per Codex C-P3-7):
//
// Placed AFTER the async_mutex class definition but BEFORE the awaiter struct
// (for the uintptr_t / is_always_lock_free invariants that do not need the
// awaiter's complete type):
//
//   static_assert(sizeof(uintptr_t) >= sizeof(void*),
//       "fixpp::sync: state encoding requires uintptr_t to fit a pointer.");
//   static_assert(std::atomic<uintptr_t>::is_always_lock_free,
//       "fixpp::sync: async_mutex requires lock-free std::atomic<uintptr_t>.");
//   static_assert(
//       std::atomic<fixpp::sync::detail::async_mutex_awaiter*>::is_always_lock_free,
//       "fixpp::sync: next_drain_head_ atomic exchange requires lock-free "
//       "std::atomic<async_mutex_awaiter*>.");
//
// Placed AFTER the async_mutex_awaiter struct definition (in async_mutex_awaiter.hpp
// / the detail section of the build header):
//
//   static_assert(alignof(async_mutex_awaiter) >= 8,
//       "fixpp::sync: async_mutex_awaiter must be 8-byte-aligned so the "
//       "low-bit `not_locked` sentinel is distinguishable from a real waiter.");
// ─────────────────────────────────────────────────────────────────────────────
