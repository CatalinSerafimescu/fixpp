// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/sync/test_async_mutex_drain_latch_publish_acquire.cpp
// W-orig (047-async-mutex-drain-reap, FR-006 / SC-006) — the original
// orphan-a-late-waiter witness, moved from the 046-atomic-shared-ptr branch and
// HARDENED for 047.
//
// Bug under test (B-orig): cancel_and_drain()'s reaper re-walks the waiter lists
// (step g) BEFORE the acquirer-count quiesce (step h) and never re-scans after
// quiescing, so an acquirer that pushes onto state_ concurrently with the drain
// (and is still counted in active_acquirers_count_ when the reaper observes the
// lists empty) is ORPHANED: never reaped, never resumed → its async_lock() never
// completes → a lost wake. The converging reap+quiesce loop (047) re-scans after
// every feeder quiesces, catching the late push (edge #1).
//
// Discrimination: every begun acquirer reaches EXACTLY ONE terminal outcome
// (granted / drained / aborted). If one is orphaned, the round's completion
// counter never reaches the expected total → the internal self-deadline trips
// → ADD_FAILURE() with diagnostics (a FAST attributable FAIL, never a lane hang).
//
// Hardening (FR-006): real asio::thread_pool with >=4 workers, >=32 racing
// acquirers/round, >=100 rounds/invocation; an internal steady-clock
// self-deadline; a FRESH mutex per round; and orphan-at-teardown safety — on the
// timeout/failure path we LEAK the heap-allocated mutex + pool (the async_mutex
// dtor rejects a non-empty state and would abort the process, converting a clean
// gtest FAILURE into an abort). We poll an atomic completion counter instead of
// blocking on use_future joins (no lane hang; also removes the libc++ TSan
// use_future/promise teardown noise of finding 1).
//
// TSan (libstdc++ Tier-1) is the primary race-detection oracle; the completion
// counters + the self-deadline are the functional discriminators. The bug is
// timing-dependent: contention widens the window, so the mutation-revert RED
// proof (T016) runs the release build under `taskset -c 0,1`.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_awaitable.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <fixpp/core/sync/async_mutex.hpp>
#include <thread>
#include <vector>

#include "sync/sync_test_support.hpp"

namespace {

using fixpp::core::error;
using fixpp::sync::async_mutex;
using fixpp::sync::expected_t;
using fixpp::sync::test::yield_n;

// ── DrainLatchPublishAcquire ──────────────────────────────────────────────────
//
// Per round:
//   1. A holder acquires the mutex and holds it long enough for waiters to park.
//   2. N waiters race to async_lock() (some park on state_, some may fast-fail
//      on draining_, at most one may win a grant before the drain fires).
//   3. A drainer calls cancel_and_drain() concurrently with the waiter pushes —
//      the genuine late-waiter race.
//
// Each waiter increments `completed` exactly once on reaching its terminal
// outcome (granted / drained / aborted). The round succeeds when
// completed == N (every begun acquirer settled) AND the drain reported success.
// An orphaned waiter never increments `completed` → the deadline trips.

TEST(AsyncMutexDrainLatchPublishAcquire, ConcurrentDrainNeverOrphansLateWaiter) {
    constexpr int kPoolSize = 4;
    constexpr int kRounds = 100;
    constexpr int kWaitersPerRound = 32;
    constexpr auto kRoundDeadline = std::chrono::seconds(5);

    for (int round = 0; round < kRounds; ++round) {
        // Heap-allocate so we can LEAK them on the failure path: the async_mutex
        // dtor aborts on a non-empty state (a parked orphan), which would turn a
        // clean gtest FAILURE into a process abort. On success we delete both.
        auto* pool = new asio::thread_pool{kPoolSize};
        auto* mtx = new async_mutex;

        std::atomic<int> completed{0};
        std::atomic<int> granted{0};
        std::atomic<int> aborted{0};
        std::atomic<int> drain_ok{0};
        std::atomic<int> drain_done{0};
        std::atomic<bool> holder_acquired{false};

        auto holder = [mtx, &holder_acquired]() -> asio::awaitable<void> {
            auto g = co_await mtx->async_lock();
            EXPECT_TRUE(g.has_value()) << "holder must acquire successfully";
            holder_acquired.store(true, std::memory_order_release);
            co_await yield_n(kWaitersPerRound * 2 + 4);
            // guard dtor unlocks here; draining_ may already be true.
        };

        auto drainer = [mtx, &drain_ok, &drain_done, &holder_acquired]() -> asio::awaitable<void> {
            while (!holder_acquired.load(std::memory_order_acquire)) co_await yield_n(1);
            co_await yield_n(kWaitersPerRound + 2);
            auto d = co_await mtx->cancel_and_drain();
            if (d.has_value()) drain_ok.fetch_add(1, std::memory_order_acq_rel);
            drain_done.fetch_add(1, std::memory_order_acq_rel);
        };

        auto waiter = [mtx, &completed, &granted, &aborted]() -> asio::awaitable<void> {
            co_await yield_n(1);
            auto r = co_await mtx->async_lock();
            if (r.has_value()) {
                granted.fetch_add(1, std::memory_order_acq_rel);
            } else if (r.error() == error::sync_lock_aborted ||
                       r.error() == error::sync_lock_drained) {
                aborted.fetch_add(1, std::memory_order_acq_rel);
            }
            // EXACTLY-ONCE terminal increment, whatever the outcome.
            completed.fetch_add(1, std::memory_order_acq_rel);
        };

        asio::co_spawn(*pool, holder(), asio::detached);
        asio::co_spawn(*pool, drainer(), asio::detached);
        for (int i = 0; i < kWaitersPerRound; ++i)
            asio::co_spawn(*pool, waiter(), asio::detached);

        // Bounded wait on the completion counters — NEVER a blocking join (an
        // orphaned waiter would hang the lane forever). On timeout, FAIL FAST.
        const auto deadline = std::chrono::steady_clock::now() + kRoundDeadline;
        bool ok = false;
        while (std::chrono::steady_clock::now() < deadline) {
            if (completed.load(std::memory_order_acquire) == kWaitersPerRound &&
                drain_done.load(std::memory_order_acquire) == 1) {
                ok = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        if (!ok) {
            ADD_FAILURE() << "round=" << round
                          << ": TIMEOUT — a begun acquirer was ORPHANED (lost wake). "
                          << "completed=" << completed.load() << "/" << kWaitersPerRound
                          << " granted=" << granted.load() << " aborted=" << aborted.load()
                          << " drain_done=" << drain_done.load()
                          << " drain_ok=" << drain_ok.load()
                          << ". LEAKING mutex+pool (dtor would abort on the parked orphan).";
            // Intentional leak of *pool and *mtx — do NOT join/delete (would hang
            // or abort). Stop the test now with a clean FAILURE.
            return;
        }

        EXPECT_EQ(granted.load() + aborted.load(), kWaitersPerRound)
            << "round=" << round << ": every waiter must reach exactly one terminal outcome";
        EXPECT_EQ(drain_ok.load(), 1)
            << "round=" << round << ": cancel_and_drain() must succeed exactly once";

        pool->join();
        delete mtx;
        delete pool;
    }
}

}  // namespace
