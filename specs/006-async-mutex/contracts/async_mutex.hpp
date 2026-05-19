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
// Source of truth: .specify/2f-async-mutex.md v1.5 §4.1 + §6.2 + §1.1,
//                  AS AMENDED BY v1.6 errata E-2 / E-3 / E-4 (recorded
//                  post-sign-off at /implement; re-touch 006 Gate A scope):
//   - E-2 (§4.2): the frame-local awaiter is NOT the intrusive node; the
//       stable detail::waiter_record is. state_ / next_drain_head_ hold
//       detail::waiter_record*; the contended mr==nullptr zero-global-new
//       node is drawn from the per-mutex waiter_pool_ (NOT an embedded
//       waiter node). See data-model.md E2/E2a + invariant I-32.
//   - E-3 (§4.6.2): 2f waiter resumption is ALWAYS post-ed to the bound
//       executor — never inline-dispatched — regardless of completion_policy
//       or running_in_this_thread() (re-entrant inline resume = heap-UAF,
//       TSan-diagnosed). completion_policy survives only as a semantic knob.
//   - E-4 (§4.3.4): asio 1.36.0's cancellation_slot has NO allocator-binding
//       hook (source-verified). detail::slot_allocator is NOT bound to the
//       slot; the cancellation closure lives in asio's per-thread recycler
//       (zero global new/delete only in steady state, post per-thread
//       warm-up). slot_allocator is retained as the typed storage-policy
//       wrapper for the waiter_record fallback.
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
#include <expected>
#include <memory>
#include <memory_resource>
// Build header includes these properly; the oracle forward-declares the
// minimum needed to state the EXACT signatures literally (RC#2 oracle-
// fidelity policy: literal declarations, forward-declared dependencies —
// the build header forward-declares expected_t / asio::awaitable the same
// way, so no full ASIO / error.hpp include chain is needed here):
// #include <asio/awaitable.hpp>             (build header only)
// #include "fixpp/core/error.hpp"           (build header only)
// #include "fixpp/core/sync/async_lock_guard.hpp"   (or same header)

// Forward declarations for the literal signature oracle. The build header
// gets these from <asio/awaitable.hpp> + "fixpp/core/error.hpp"; the second
// awaitable template parameter (the executor) is defaulted by ASIO exactly
// as written here, so `asio::awaitable<T>` resolves identically.
namespace asio {
class any_io_executor;
template <class T, class Executor = any_io_executor> class awaitable;
}  // namespace asio
namespace fixpp::core { enum class error : int; }

namespace fixpp::sync {

class async_lock_guard;

// expected_t alias mirrors core::expected_t<T> = std::expected<T, core::error>;
// declared (not erased) so the async_lock / cancel_and_drain return types are
// stated EXACTLY as in design-doc §4.1.
template <class T>
using expected_t = /* fixpp::core::expected_t<T> = */
    std::expected<T, fixpp::core::error>;

namespace detail {
enum class waiter_phase : std::uint8_t;
class async_mutex_awaiter;
struct waiter_record;   // E-2: the STABLE intrusive node (state_ /
                        // next_drain_head_ point here, NOT at the awaiter).
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
    // PMR-aware fallback (RC#2 fix; E-2-amended):
    //   mr == nullptr (default) — frame-local awaiter embedded in caller's
    //     coroutine frame (HALO-eligible per [const §XI.6]: awaiter ≤ 96 B,
    //     v1.1 layout). E-2: the contended stable node (detail::waiter_record)
    //     is drawn from the per-mutex waiter_pool_ bounded arena — zero global
    //     new/delete on the v1.0 hot path (NOT an embedded waiter node).
    //   mr != nullptr — awaiter AND waiter_record allocated from / reclaimed
    //     to mr. Type-erased completion handlers (asio::any_completion_handler)
    //     and composed operations pass mr explicitly. The session-side helper
    //     (E7, declared separately in session/) recovers mr from the bound
    //     session_executor.
    //
    // E-4 (source-verified, asio 1.36.0): the cancellation slot's handler-
    // closure storage is NOT asio::bind_allocator-wrapped — cancellation_slot
    // exposes no allocator-binding hook. detail::slot_allocator is NOT bound
    // to the slot; the closure is owned by asio's per-thread recycling cache
    // (zero global new/delete only in STEADY STATE, after a documented
    // per-thread warm-up; the one-time per-thread first-touch is §6.4
    // bench-soft). slot_allocator is retained as the typed storage-policy
    // wrapper for the waiter_record fallback (the allocation 2f controls),
    // unit-verified by §9 seam #21.
    //
    // Cancellation: honours total (waiter unlinked, completes with
    // unexpected{sync_lock_aborted}); partial is treated as total; terminal
    // is treated as total.
    //
    // Completion: runs on the awaiter's bound executor per [2d §7.4]. E-3:
    // 2f waiter resumption is ALWAYS post-ed — never inline-dispatched —
    // regardless of policy_ or running_in_this_thread() (re-entrant inline
    // resume = TSan-diagnosed heap-UAF). policy_ is a semantic knob only.
    //
    // [[nodiscard]]: co_await result MUST be consumed.
    // EXACT signature per design-doc §4.1 (lines 505–506) — literal, NOT a
    // placeholder (RC#2 / N-P1-1 oracle-fidelity close).
    [[nodiscard]] asio::awaitable<expected_t<async_lock_guard>>
        async_lock(std::pmr::memory_resource* mr = nullptr) noexcept;

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
    //   (f) reap_chain: for each queued waiter_record: phase CAS
    //       queued→cancelled (acq_rel) on record->phase_ (E-2); CAS winner
    //       writes record->result_ = unexpected{sync_lock_aborted} and
    //       post-s bound-executor resumption (winner-only — v1.4
    //       CAS-then-publish; E-3 always-post); captures latch_state
    //       shared_ptr. Node reclaimed via record->refcount_ (I-32).
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
    // EXACT signature per design-doc §4.1 (lines 579–580) — literal, NOT a
    // placeholder (RC#2 / N-P1-1 oracle-fidelity close).
    [[nodiscard]] asio::awaitable<expected_t<void>>
        cancel_and_drain() noexcept;

    // ─────────────────────────────────────────────────────────────────────────
    // Release

    // unlock — release the mutex. Walks next_drain_head_ first (RC-A), then
    // the LIFO from state_ (both are detail::waiter_record* chains — E-2).
    // The first non-cancelled queued waiter_record is granted ownership
    // (record->phase_ CAS queued→granted; winner writes record->result_);
    // remaining FIFO tail is spliced into next_drain_head_. The granted
    // waiter is resumed via one post hop (E-3 always-post — never inline).
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
    // E-2: a non-sentinel value is a detail::waiter_record* (the stable node),
    // NOT the frame-local awaiter.
    std::atomic<uintptr_t>                              state_             {not_locked};

    // RC-A v1.1 — mutex-owned residual FIFO chain.
    // E-2: retyped from detail::async_mutex_awaiter* to detail::waiter_record*
    // (the stable intrusive node; see data-model.md E2a + invariant I-32).
    std::atomic<detail::waiter_record*>                 next_drain_head_   {nullptr};

    // E-2 (new) — per-mutex bounded, pre-reserved freelist/slab of
    // detail::waiter_record. Stable-node storage for the contended
    // mr==nullptr path: zero global operator new/delete (an arena per
    // [const §VIII.5] default). Exhaustion ⇒ unexpected{sync_lock_alloc_failed}
    // (slot 44). The exact storage shape (inline slab vs. intrusive freelist)
    // is an implementation choice; the zero-global-new contract is normative.
    // detail::waiter_pool waiter_pool_;   // (build header: concrete type)

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
//       std::atomic<fixpp::sync::detail::waiter_record*>::is_always_lock_free,
//       "fixpp::sync: state_/next_drain_head_ atomic exchange requires "
//       "lock-free std::atomic<detail::waiter_record*> (E-2).");
//
// Placed AFTER the detail::waiter_record struct definition (in the detail
// section of the build header) — E-2: the sentinel must be distinguishable
// from any real waiter_record*, and alignof on an incomplete class is
// ill-formed:
//
//   static_assert(alignof(fixpp::sync::detail::waiter_record) >= 8,
//       "fixpp::sync: detail::waiter_record must be 8-byte-aligned so the "
//       "low-bit `not_locked` sentinel is distinguishable from a real node.");
// ─────────────────────────────────────────────────────────────────────────────
