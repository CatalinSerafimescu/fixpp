// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/006-async-mutex/contracts/drain_latch_state.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// SHAPE ORACLE (NOT THE BUILD HEADER)
// This file is a planning-phase contract artifact.
// It specifies the exact class shape that the implementation MUST match.
// It is NOT compiled into the library.
//
// Build header target: include/fixpp/core/sync/async_mutex.hpp
//   (detail::drain_latch_state is defined in the fixpp::sync::detail namespace
//   within the main async_mutex header — or a companion detail header included
//   from it)
// ─────────────────────────────────────────────────────────────────────────────
//
// Source of truth: .specify/2f-async-mutex.md v1.5 §3.1 (RC-β row) + §4.7.2 +
//                  §4.7.3
//
// drain_latch_state — lazy-constructed event-state object. Allocated as
// std::shared_ptr<drain_latch_state> inside cancel_and_drain()'s coroutine
// frame; NOT a by-value mutex member.
//
// History / motivation:
//   v1.2 by-value detail::drain_latch owned an asio::steady_timer — this was
//   NON-IMPLEMENTABLE: asio::steady_timer requires an executor at construction
//   and async_mutex() is constexpr-default-constructible per [arch §5.5].
//   v1.3 RC-β fix (Opus C-R3-P1-4 / C-R3-P2-1 / N-R3-P1-1 / N-R3-P1-2 close):
//   the state is allocated lazily inside cancel_and_drain()'s call frame and
//   stored atomically in async_mutex::drain_latch_ptr_. The mutex constructor
//   remains constexpr and executor-free.
//
// Lifetime:
//   - Created by the reaper via std::make_shared<drain_latch_state>(executor).
//   - The reaper atomic-stores the shared_ptr into async_mutex::drain_latch_ptr_
//     (release) BEFORE setting draining_ = true (v1.4 ordering guarantee).
//   - In-flight resumption-handler lambdas capture the shared_ptr by value,
//     keeping the state alive until each lambda fires.
//   - drain_latch_ptr_ is cleared only after the terminal result is published
//     via signal_release() or signal_abort().
//   - The state object survives the reaper's coroutine frame destruction.
//     (shared_ptr lifetime; closes Opus N-R3-P1-2 UAF where in_flight_
//     resumptions_ previously lived on the reaper's stack.)
//
// Note on atomicity of drain_latch_ptr_:
//   std::atomic<std::shared_ptr<T>> is NOT lock-free in general. The update
//   path (in cancel_and_drain()) is cold — only exercised when cancel_and_drain
//   is invoked. It does not impact the hot acquire/release path.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <atomic>
#include <cstdint>

// Forward declarations (build header includes these properly):
// #include <asio/awaitable.hpp>
// #include <asio/experimental/concurrent_channel.hpp>
// (or equivalent project-internal multi-waiter subscriber-list awaitable per
//  [2f §4.7.3] implementation pick — asio::experimental::concurrent_channel is
//  the cited precedent, present in ASIO 1.30)

namespace fixpp::sync::detail {

// drain_latch_state — the event-state object for one cancel_and_drain() epoch.
//
// All members are accessed under the memory-ordering invariants I-25..I-31
// (data-model.md §Memory-Ordering Invariant Table).
class drain_latch_state {
public:
    // Construct with the reaper's executor (needed by the channel for
    // async I/O event dispatch).
    // explicit drain_latch_state(asio::any_io_executor exec);

    // ─────────────────────────────────────────────────────────────────────────
    // Observer state (checked by subscribers after wait() returns):

    // Set to true by signal_release() at the drain publication edge.
    // Subscribers acquire-load this after wait() to confirm a successful drain.
    // Memory-ordering: see I-25 (store release) / I-27 (load acquire).
    std::atomic<bool> released_ {false};

    // NEW v1.4 — set to true by signal_abort() when the reaper is itself
    // cancelled (cancellation_type::total on the reaper's parent state).
    // Subscribers acquire-load this after wait() to distinguish abort from
    // release.
    // Memory-ordering: see I-26 (store release) / I-27 (load acquire).
    std::atomic<bool> aborted_ {false};

    // Count of scheduled-but-not-yet-completed reaper-resumption handlers.
    // Lives on this state object (NOT on the reaper's stack) — closes Opus
    // N-R3-P1-2 UAF where the previous design placed this on the reaper's
    // coroutine frame, which could be destroyed before all lambdas fired.
    // Memory-ordering: see I-28 (increment acq_rel) / I-29 (decrement acq_rel)
    //                      / I-30 (load acquire in wait loop).
    std::atomic<std::uint32_t> in_flight_resumptions_ {0};

    // ─────────────────────────────────────────────────────────────────────────
    // Channel for multi-waiter latch surface:
    //   asio::experimental::concurrent_channel<void()> channel_;
    //   (or equivalent project-internal awaitable subscriber list)
    //   — omitted here (ASIO type; implementation picks per [2f §4.7.3])

    // ─────────────────────────────────────────────────────────────────────────
    // Public interface:

    // wait() — subscribes to the release/abort edge. Parks the caller until
    // signal_release() or signal_abort() is called. Cancellation-slot
    // integration per [2f §4.7.3].
    // Returns: asio::awaitable<void>
    // asio::awaitable<void> wait();

    // notify() — called by each holder's unlock() (when draining_ == true)
    // and each resumption handler lambda. Non-terminal wake; idempotent
    // (channel try_send). Memory-ordering: see I-31.
    void notify() noexcept;

    // signal_release() — publishes the drain-complete edge.
    //   released_.store(true, release);   // I-25
    //   wakes all parked wait() subscribers.
    // Called by the last reaper-resumption handler (observing
    // in_flight_resumptions_ fetch_sub == 1) AND all drain conditions met.
    void signal_release() noexcept;

    // signal_abort() — NEW v1.4 — publishes the reaper-cancelled edge.
    //   aborted_.store(true, release);    // I-26
    //   wakes all parked wait() subscribers with the abort outcome.
    // Called when the reaper's cancel_and_drain awaitable is itself cancelled
    // (cancellation_type::total from the caller's parent state).
    void signal_abort() noexcept;
};

}  // namespace fixpp::sync::detail

// ─────────────────────────────────────────────────────────────────────────────
// Interaction diagram (cancel_and_drain() reaper):
//
//   reaper:
//     make_shared<drain_latch_state>(exec)    → latch_state
//     drain_latch_ptr_.store(latch_state, release)    // I-23
//     draining_.store(true, release)                  // I-13
//     ... exchange both state_ and next_drain_head_ ...
//     ... reap_chain: for each queued waiter: ...
//       in_flight_resumptions_.fetch_add(1, acq_rel)  // I-28
//       schedule_resume_on_bound_executor(head, [latch_state, this]{
//           if (in_flight_resumptions_.fetch_sub(1,acq_rel)==1  // I-29
//               && active_holders_count_.load(acquire)==0       // I-19
//               && active_acquirers_count_.load(acquire)==0)    // I-22
//               latch_state->signal_release();                  // I-25
//           else
//               latch_state->notify();                         // I-31
//       });
//     co_await latch_state->wait()                             // parks reaper
//     → on wakeup: if released_.load(acquire): co_return {};   // I-27
//                  if aborted_.load(acquire):  co_return unexpected{...}
//
//   holder's unlock() under draining_ == true:
//     active_holders_count_.fetch_sub(1, acq_rel)             // I-18
//     if (auto latch = drain_latch_ptr_.load(acquire))         // I-24
//         latch->notify();                                     // I-31
// ─────────────────────────────────────────────────────────────────────────────
