// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/sync/test_async_mutex_drain_latch_publish_acquire.cpp
// T008 (046-atomic-shared-ptr, NFR-017) — consumer publish/acquire witness for
// async_mutex::drain_latch_ptr_
// (fixpp::sync::atomic_shared_ptr<detail::drain_latch_state>).
//
// Design anchor: .specify/046-atomic-shared-ptr.md (plan row 6-consumers)
// Consumer: async_mutex (include/fixpp/core/sync/async_mutex.hpp)
//
// drain_latch_ptr_ publish/acquire race scenario:
//   Writer:  cancel_and_drain() atomically stores (release-store) a freshly
//            constructed drain_latch_state into drain_latch_ptr_.
//   Readers: unlock() on any waiter acquires-loads drain_latch_ptr_ to detect
//            whether a drain is in progress.  Concurrent cancel_and_drain() vs
//            async_lock() creates the live race where the store and the load
//            happen on SEPARATE threads (the io_context thread vs a coroutine
//            resumption, both via asio::thread_pool{N}).
//
// Discrimination: the test is not merely a stress test that "doesn't crash" —
// the assertions verify that EVERY waiter completes exactly once (no resume
// lost, no double-resume) and that the drain outcome is always reported before
// the pool drains.  A torn drain_latch_ptr_ pointer would cause the waiter's
// drain_latch->signal_*() call to corrupt the latch → either a missed wake
// (test hangs) or a double-wake (ASSERT fires).
//
// TSan is the primary race-detection oracle; the bounded iteration count and
// the completion counters are the functional discriminators.
//
// Note: the existing test_cancel_and_drain.cpp tests the algorithm on a
// SINGLE-THREADED io_context (no cross-thread publish/acquire edge).  This test
// uses asio::thread_pool{4} so that the drain_latch_ptr_ store and the
// concurrent unlock() / wait() loads run on DIFFERENT OS threads — the
// condition the NFR-017 primitive was introduced to solve.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <cstddef>
#include <fixpp/core/sync/async_mutex.hpp>
#include <future>
#include <vector>

#include "sync/sync_test_support.hpp"

namespace {

using fixpp::core::error;
using fixpp::sync::async_mutex;
using fixpp::sync::expected_t;
using fixpp::sync::test::yield_n;

// ── DrainLatchPublishAcquire ──────────────────────────────────────────────────
//
// Pattern: repeat R rounds of the following scenario:
//   1. Spawn a holder coroutine that acquires the mutex and holds for a while.
//   2. Spawn N waiter coroutines that all try to async_lock().
//   3. Spawn a drainer coroutine that calls cancel_and_drain() concurrently
//      with the waiter lock attempts.
//
// The multi-thread pool ensures the drain_latch_ptr_ release-store (in the
// drainer) and the acquire-load (in the waiters' unlock paths) are on distinct
// OS threads — the genuine publish/acquire cross-thread edge under test.
//
// Assertion:
//   - sum(aborted + granted + drained) == N (every waiter completes once)
//   - drain result is success (has_value)
// A torn drain_latch_ptr_ would either hang (lost wake) or fire EXPECT_EQ.

TEST(AsyncMutexDrainLatchPublishAcquire, ConcurrentDrainAndLockNeverSeesTornPointer) {
    // Use thread_pool{4} for genuine multi-thread execution.  The drain_latch_ptr_
    // release-store (drainer) and acquire-loads (waiters' unlock) run on different
    // pool workers — the cross-thread edge the NFR-017 primitive was introduced for.
    constexpr int kPoolSize = 4;
    constexpr int kRounds = 8;
    constexpr int kWaitersPerRound = 8;

    for (int round = 0; round < kRounds; ++round) {
        asio::thread_pool pool{kPoolSize};
        async_mutex mtx;

        std::atomic<int> aborted{0};
        std::atomic<int> granted{0};
        std::atomic<int> drain_ok_count{0};

        // Holder: acquires and holds long enough for waiters to park.
        auto holder = [&]() -> asio::awaitable<void> {
            auto g = co_await mtx.async_lock();
            EXPECT_TRUE(g.has_value()) << "holder must acquire successfully";
            co_await yield_n(kWaitersPerRound * 2 + 4);
            // Guard dtor calls unlock(); at this point draining_ may already be true.
        };

        // Drainer: calls cancel_and_drain() while the holder is still active.
        auto drainer = [&]() -> asio::awaitable<void> {
            co_await yield_n(kWaitersPerRound + 2);
            auto d = co_await mtx.cancel_and_drain();
            if (d.has_value()) {
                drain_ok_count.fetch_add(1, std::memory_order_acq_rel);
            }
        };

        // Waiter: tries to lock; will receive either sync_lock_aborted (drain wins)
        // or (very rarely) a grant if the drain has not yet fired.
        auto make_waiter = [&]() -> asio::awaitable<void> {
            co_await yield_n(1);
            auto r = co_await mtx.async_lock();
            if (r.has_value()) {
                granted.fetch_add(1, std::memory_order_acq_rel);
                // unlock will acquire-load drain_latch_ptr_ on the drain path.
            } else {
                if (r.error() == error::sync_lock_aborted ||
                    r.error() == error::sync_lock_drained) {
                    aborted.fetch_add(1, std::memory_order_acq_rel);
                }
            }
        };

        auto fh = asio::co_spawn(pool, holder(), asio::use_future);
        auto fd = asio::co_spawn(pool, drainer(), asio::use_future);

        std::vector<std::future<void>> wfuts;
        wfuts.reserve(kWaitersPerRound);
        for (int i = 0; i < kWaitersPerRound; ++i) {
            wfuts.push_back(asio::co_spawn(pool, make_waiter(), asio::use_future));
        }

        fh.get();
        fd.get();
        for (auto& f : wfuts) f.get();

        pool.join();

        int total = aborted.load() + granted.load();
        EXPECT_EQ(total, kWaitersPerRound)
            << "round=" << round
            << ": sum(aborted+granted) must equal N=" << kWaitersPerRound
            << "; a torn drain_latch_ptr_ would cause a lost wake (hanging) or "
               "extra resume (double-count). "
               "aborted=" << aborted.load() << " granted=" << granted.load();

        EXPECT_EQ(drain_ok_count.load(), 1)
            << "round=" << round << ": cancel_and_drain() must succeed exactly once";
    }
}

}  // namespace
