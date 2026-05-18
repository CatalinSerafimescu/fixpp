// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/006-async-mutex/contracts/async_mutex_awaiter.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// SHAPE ORACLE (NOT THE BUILD HEADER)
// This file is a planning-phase contract artifact.
// It specifies the exact layout and protocol that the implementation MUST match.
// It is NOT compiled into the library.
//
// Build header target: include/fixpp/core/sync/async_mutex.hpp
//   (detail::async_mutex_awaiter is defined in the fixpp::sync::detail
//   namespace within the main async_mutex header, after the async_mutex class)
// ─────────────────────────────────────────────────────────────────────────────
//
// Source of truth: .specify/2f-async-mutex.md v1.5 §4.2 / §4.2.1 / §4.2.2 /
//                  §4.2.3
//
// async_mutex_awaiter — the waiter object / suspension unit for
// co_await m.async_lock(). Private to fixpp::sync::detail; not part of the
// user surface beyond async_lock's return-shape contract.
//
// HALO eligibility: ≤ 96 B layout (v1.1 budget — RC-A drops residual_, RC-C
// adds slot_storage_). alignas(8) required so the LIFO state_ encoding's
// low-bit `not_locked` sentinel is distinguishable from any real waiter pointer.
//
// v1.1 layout changes from v1.0:
//   - removes async_mutex_awaiter* residual_ (RC-A — mutex owns the residual
//     list via async_mutex::next_drain_head_).
//   - adds std::array<std::byte, 32> slot_storage_ (RC-C — feeds
//     detail::slot_allocator on the embedded-default path so the cancellation
//     handler closure does not touch the global heap when mr == nullptr).
//
// Three-state waiter_phase enum (v1.1 collapse from v1.0's four states —
// RC-A close):
//   queued    = 0  — pushed onto LIFO or spliced into next_drain_head_;
//                    still cancellable.
//   granted   = 1  — drain CAS-granted ownership; await_resume returns guard.
//                    Terminal.
//   cancelled = 2  — cancellation handler or reaper CAS-acquired; await_resume
//                    returns unexpected{sync_lock_aborted}. Terminal.
//
// v1.4 CAS-then-publish protocol: only the CAS winner writes *result_;
// the loser observes terminal phase and does NOT touch *result_.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <array>
#include <atomic>
#include <coroutine>
#include <cstddef>
#include <cstdint>

// Build header includes these properly; this oracle forward-declares the
// minimum so the EXACT layout + protocol is stated literally, including the
// normative slot_ / result_ fields and the real await_resume() /
// on_cancel() signatures (RC#2 oracle-fidelity policy — this file claims an
// "exact layout and protocol that the implementation MUST match" at the
// banner, so it MUST carry the real fields that drive the §1.1 byte budget):
// #include <asio/awaitable.hpp>
// #include <asio/cancellation_slot.hpp>
// #include <asio/cancellation_type.hpp>
// #include "fixpp/core/error.hpp"
#include <expected>

namespace asio {
class cancellation_slot;
enum class cancellation_type : unsigned int;  // ASIO's underlying type
}  // namespace asio
namespace fixpp::core { enum class error : int; }

namespace fixpp::sync {

class async_mutex;
class async_lock_guard;
// expected_t<T> = fixpp::core::expected_t<T> = std::expected<T, core::error>;
// declared (not erased) so result_ and await_resume() carry their real types.
template <class T>
using expected_t = std::expected<T, fixpp::core::error>;

namespace detail {

// Phase enum DEFINITION (Codex C-P3-7 close — order-valid header sketch).
// Forward-declared in the namespace block above async_mutex (§4.1); the
// complete definition lives here, after the async_mutex class definition.
// v1.1 collapses the v1.0 four-state machine to three states (RC-A close).
enum class waiter_phase : std::uint8_t {
    queued    = 0,  // pushed onto LIFO (state_) or spliced into
                    //   next_drain_head_ (residual FIFO — RC-A); still
                    //   cancellable. The drain skips `cancelled` waiters and
                    //   CASes the first `queued` waiter to `granted`.
    granted   = 1,  // unlock()'s drain (or fast-path acquire) has granted
                    //   ownership; await_resume returns the guard. Terminal.
    cancelled = 2,  // cancellation handler (or cancel_and_drain reaper)
                    //   CAS-acquired this waiter; await_resume returns
                    //   unexpected{sync_lock_aborted}. Terminal.
};

// async_mutex_awaiter — the waiter object.
// alignas(8): LIFO state_ encoding's low-bit `not_locked` sentinel
//             requires waiter pointers to be >= 8-byte-aligned.
//
// Layout (field order is normative; implementation MUST preserve it):
struct alignas(8) async_mutex_awaiter {
    async_mutex*               mutex_;        // back-pointer to the mutex.
    async_mutex_awaiter*       next_;         // intrusive link — reused by
                                              //   both state_'s LIFO chain and
                                              //   next_drain_head_'s FIFO chain
                                              //   (RC-A).
    std::atomic<waiter_phase>  phase_;        // RC-A 3-state machine; the
                                              //   arbitration atom for unlock
                                              //   drain vs. cancellation handler.
    asio::cancellation_slot    slot_;         // NORMATIVE field (§4.2 line
                                              //   743). Bound at await_suspend
                                              //   via asio::bind_allocator(
                                              //     slot_allocator{this, mr}).
    std::coroutine_handle<>    coro_;         // continuation; stored at
                                              //   await_suspend.
    expected_t<async_lock_guard>* result_;    // NORMATIVE field (§4.2 line
                                              //   745). Sink for await_resume;
                                              //   points into caller's frame-
                                              //   local result slot. Valid from
                                              //   await_suspend entry through
                                              //   await_resume return.
                                              //   v1.4 CAS-then-publish: ONLY
                                              //   the CAS winner writes
                                              //   *result_; losers do NOT
                                              //   touch it.
    std::array<std::byte, 32>  slot_storage_; // RC-C inline buffer for
                                              //   detail::slot_allocator when
                                              //   mr == nullptr (HALO path).
                                              //   §9 seam #21 verifies this
                                              //   selection.

    // Awaitable protocol:

    // await_ready — uncontended fast path (v1.2 RC-B + v1.3 RC-α close).
    // Awaitable-factory pre-step (NEW v1.3 RC-α): the factory increments
    //   mutex_->active_acquirers_count_.fetch_add(1, acq_rel) BEFORE this call.
    // Step 1: draining_.load(acquire) — if true, write *result_ = unexpected
    //   {sync_lock_drained}, phase_ = cancelled (release), decrement
    //   active_acquirers_count_ (RC-α decrement-point #1), return true.
    // Step 2: state_.compare_exchange_strong(not_locked → locked_no_waiters,
    //   acquire/relaxed). On success: increment active_holders_count_ (winner-
    //   only, post-CAS — RC-α), decrement active_acquirers_count_ (RC-α
    //   decrement-point #2), return true. On failure: return false.
    bool await_ready() noexcept;

    // await_suspend — enqueue + slot-bind + phase init (v1.3 RC-α/RC-B close).
    // Step 1: draining_.load(acquire) defense-in-depth check (fast-fail if
    //   draining_ landed between await_ready steps 1 and 2); decrement
    //   active_acquirers_count_ (RC-α decrement-point #3a), resume inline.
    // Steps 2–6: store coro_ = h; init phase_ = queued (relaxed); recover
    //   cancellation_state; bind slot via asio::bind_allocator(
    //   slot_allocator{this, mr}); register on_cancel; LIFO push CAS retry
    //   (release/acquire). On CAS-success: decrement active_acquirers_count_
    //   (RC-α decrement-point #3b — tracking transfers to LIFO walk).
    // Step 7: if state_ transitions to not_locked mid-push, CAS to
    //   locked_no_waiters, decrement active_acquirers_count_ (decrement-point
    //   #3c), increment active_holders_count_, resume inline.
    void await_suspend(std::coroutine_handle<> h) noexcept;

    // await_resume — read result, clear slot.
    // phase_.load(acquire): granted → return async_lock_guard{mutex_};
    //   cancelled → return *result_ (unexpected{sync_lock_aborted} or
    //   unexpected{sync_lock_drained}). Clears slot_ via slot_.clear().
    // Cancellation-after-resume is a no-op: slot is cleared, and a stale
    // cancel CAS fails because phase_ is already terminal (Opus N-P1 close).
    // EXACT signature per §4.2 (line 753) — literal, NOT omitted.
    expected_t<async_lock_guard> await_resume() noexcept;

    // on_cancel — invoked from the cancellation_slot handler.
    // CAS phase_: queued → cancelled (acq_rel/acquire). On CAS-success (winner):
    //   write *result_ = unexpected{sync_lock_aborted}, schedule resumption on
    //   bound executor. On CAS-failure (drain won): no-op (waiter already
    //   granted). EXACT parameter type per §4.5 — asio::cancellation_type,
    //   NOT an int placeholder (RC#2 oracle-fidelity close).
    void on_cancel(asio::cancellation_type type) noexcept;
};

// Compile-time alignment invariant — placed AFTER the awaiter definition
// (Codex C-P3-7 close: alignof on an incomplete class is ill-formed).
// Implementation MUST include this assertion after the struct definition.
//
// static_assert(alignof(async_mutex_awaiter) >= 8,
//               "fixpp::sync: async_mutex_awaiter must be 8-byte-aligned so "
//               "the low-bit `not_locked` sentinel is distinguishable from a "
//               "real waiter pointer.");

}  // namespace detail
}  // namespace fixpp::sync

// ─────────────────────────────────────────────────────────────────────────────
// Field-layout sizing reference (v1.1, ≤ 96 B budget — EXACT per design-doc
// §1.1 line 70; matches data-model.md D-2 and the §1.1 inline arithmetic):
//   async_mutex*               mutex_         :  8 B
//   async_mutex_awaiter*       next_          :  8 B
//   std::atomic<waiter_phase>  phase_         :  1 B (+ 3 B padding to 4 B align) → 4 B
//   asio::cancellation_slot    slot_          : ≈ 16 B (ASIO impl-defined)
//   std::coroutine_handle<>    coro_          :  8 B
//   expected_t<...>*           result_        :  8 B
//   std::array<std::byte,32>   slot_storage_  : 32 B
//   ─────────────────────────────────────────────────────────
//   Raw total                                 : 8+8+4+16+8+8+32 = 84 B
//   With alignas(8) trailing padding          : ≈ 88 B
//   Published ceiling                         : ≤ 96 B
//     (≈ 8 B headroom for asio::cancellation_slot's implementation-defined
//      size — design-doc §1.1 names this slack intentionally)
// ─────────────────────────────────────────────────────────────────────────────
