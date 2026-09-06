// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_drain_latch_holder_lifecycle.cpp — Seam #24
//
// Lazy drain_latch_state + pre-drain holder lifecycle (RC-β).
//
// Scenario: a holder acquires BEFORE cancel_and_drain() is called. While the
// holder holds, cancel_and_drain() is invoked with waiters queued behind the
// holder. cancel_and_drain() must NOT complete until:
//   (1) the pre-drain holder releases its guard (mutex unlocks); AND
//   (2) all waiters are reaped with sync_lock_aborted.
//
// Lazy latch: a default-constructed, never-drained async_mutex works normally
// (no drain_latch_state allocated until cancel_and_drain() is called).
//
// Oracle: [2f §9 #24] — "lazy drain_latch_state + pre-drain holder lifecycle" (RC-β).
//         [2f §4.7.2] — drain waits on active_holders_count_ to reach zero.
//         [2f §4.7.3] — drain_latch_ptr_ is null until first drain invocation.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <fixpp/core/sync/async_mutex.hpp>
#include <vector>

#include "support/pump_until_ready.hpp"
#include "sync/sync_test_support.hpp"

namespace {

using fixpp::core::error;
using fixpp::sync::async_lock_guard;
using fixpp::sync::async_mutex;
using fixpp::sync::expected_t;

using fixpp::sync::test::yield_n;

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: cancel_and_drain() waits for pre-drain holder to release.
//
// Timeline:
//   t0: holder acquires
//   t1: N waiters park
//   t2: cancel_and_drain() called (concurrent with holder still holding)
//   t3: holder releases (guard drops)
//   t4: drain must complete; all waiters must see sync_lock_aborted
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDrainLatchHolderLifecycle, DrainWaitsForPreDrainHolderToRelease) {
    constexpr int N = 6;

    std::atomic<int> aborted_count{0};
    std::atomic<int> completed_count{0};
    bool drain_completed = false;
    bool drain_ok = false;
    std::atomic<bool> holder_released{false};

    asio::io_context ioc;
    async_mutex mtx;

    // Holder coroutine: acquires, holds for a while, then releases.
    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());

        // Yield to let waiters park and drain to be called.
        co_await yield_n(N * 4 + 8);

        // Now release. drain must notice active_holders_count_ == 0.
        holder_released.store(true, std::memory_order_release);
        // Guard destructor calls unlock().
    };

    // Drainer coroutine: waits for waiters to park, then calls cancel_and_drain().
    // Returns only after holder releases AND all waiters are reaped.
    auto drainer_coro = [&]() -> asio::awaitable<void> {
        // Wait for waiters to park behind the holder.
        co_await yield_n(N * 2);

        auto d = co_await mtx.cancel_and_drain();
        drain_ok = d.has_value();
        drain_completed = true;

        // At this point holder_released must have fired.
        EXPECT_TRUE(holder_released.load(std::memory_order_acquire))
            << "Drain must not complete before the pre-drain holder releases";
    };

    // Waiter coroutines.
    auto make_waiter = [&]() -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx.async_lock();
        if (!r.has_value() &&
            (r.error() == error::sync_lock_aborted || r.error() == error::sync_lock_drained))
            aborted_count.fetch_add(1, std::memory_order_acq_rel);
        completed_count.fetch_add(1, std::memory_order_acq_rel);
    };

    auto fh = asio::co_spawn(ioc, holder_coro(), asio::use_future);
    auto fd = asio::co_spawn(ioc, drainer_coro(), asio::use_future);
    std::vector<std::future<void>> futs;
    futs.reserve(N);
    for (int i = 0; i < N; ++i)
        futs.push_back(asio::co_spawn(ioc, make_waiter(), asio::use_future));

    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, fh, "SeamDrainLatchHolderLifecycle::DrainWaitsForPreDrainHolderToRelease/fh")) {
        return;
    }
    fh.get();
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, fd, "SeamDrainLatchHolderLifecycle::DrainWaitsForPreDrainHolderToRelease/fd")) {
        return;
    }
    fd.get();
    for (auto& f : futs) f.get();

    EXPECT_TRUE(drain_completed) << "cancel_and_drain() must complete";
    EXPECT_TRUE(drain_ok) << "cancel_and_drain() must return success";
    EXPECT_EQ(completed_count.load(), N) << "All N waiters must complete";
    EXPECT_EQ(aborted_count.load(), N)
        << "All N waiters must receive sync_lock_aborted (not granted)";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: Lazy latch — a default-constructed, never-drained async_mutex works
// normally (no drain_latch_state constructed until cancel_and_drain() is called).
// This verifies the normal acquire/release cycle is unaffected by the lazy latch.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDrainLatchHolderLifecycle, NeverDrainedMutexWorksNormally) {
    constexpr int N = 8;

    std::atomic<int> granted{0};
    std::atomic<int> in_critical{0};
    int overlap = 0;

    asio::io_context ioc;
    async_mutex mtx;  // default-constructed; drain_latch_ptr_ is null

    auto make_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value()) << "Undraned mutex must grant normally";
        int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (v > 1) overlap++;
        granted.fetch_add(1, std::memory_order_acq_rel);
        in_critical.fetch_sub(1, std::memory_order_acq_rel);
    };

    std::vector<std::future<void>> futs;
    futs.reserve(N);
    for (int i = 0; i < N; ++i) futs.push_back(asio::co_spawn(ioc, make_coro(), asio::use_future));

    ioc.run();
    for (auto& f : futs) f.get();

    EXPECT_EQ(overlap, 0) << "Mutual exclusion must hold on undraned mutex";
    EXPECT_EQ(granted.load(), N) << "All N acquirers must succeed on undraned mutex";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: Holder acquires AFTER cancel_and_drain() is called should not be
// granted — the mutex is drained.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDrainLatchHolderLifecycle, AcquireAfterDrainIsRejected) {
    bool drain_ok = false;
    bool post_drain_acquire_rejected = false;

    asio::io_context ioc;
    async_mutex mtx;

    auto run = [&]() -> asio::awaitable<void> {
        auto d = co_await mtx.cancel_and_drain();
        drain_ok = d.has_value();

        // Any subsequent acquire must fail with sync_lock_drained.
        auto r = co_await mtx.async_lock();
        post_drain_acquire_rejected = !r.has_value() && r.error() == error::sync_lock_drained;
    };

    auto f = asio::co_spawn(ioc, run(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, f, "SeamDrainLatchHolderLifecycle::AcquireAfterDrainIsRejected")) {
        return;
    }
    f.get();

    EXPECT_TRUE(drain_ok);
    EXPECT_TRUE(post_drain_acquire_rejected)
        << "Post-drain acquire must be rejected with sync_lock_drained";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4 (RC-β latch subscriber): Two concurrent callers of cancel_and_drain()
// while a pre-drain holder holds.
//
// The SECOND drainer must subscribe to the first (reaper) via the drain_latch_state
// and receive the same outcome. Both must succeed. Specifically, the second
// drainer must NOT return until the first has actually drained (i.e., the latch
// signals completion). This tests the drain_latch_state subscriber wake mechanism.
//
// With the current stub (drain_in_progress_ never set, notify() no-op), the
// second drainer independently re-drains which may cause waiters to complete
// twice or granted_count to be non-zero. The oracle fires if any waiter is
// granted (none should be after drain).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDrainLatchHolderLifecycle, TwoConcurrentDrainersWithPreDrainHolder) {
    constexpr int N = 4;

    std::atomic<int> aborted_count{0};
    std::atomic<int> granted_count{0};
    std::atomic<int> completed_count{0};
    std::atomic<int> drain_success{0};

    asio::io_context ioc;
    async_mutex mtx;

    // Canonical §4.7.4 sequencing: two drainers run CONCURRENTLY while the
    // pre-drain holder still holds (single ioc.run()). One wins
    // drain_in_progress_ (reaper); the other subscribes to the same epoch.
    // The holder's later unlock() observes draining_ == true → short-circuit.
    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        co_await yield_n(N * 4 + 8);  // hold past drainer start + reap
        // Guard dtor → unlock() (draining_ == true → short-circuit).
    };

    auto make_waiter = [&]() -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx.async_lock();
        if (r.has_value())
            granted_count.fetch_add(1, std::memory_order_acq_rel);
        else
            aborted_count.fetch_add(1, std::memory_order_acq_rel);
        completed_count.fetch_add(1, std::memory_order_acq_rel);
    };

    auto make_drainer = [&](int stagger) -> asio::awaitable<void> {
        co_await yield_n(N * 2 + stagger);
        auto d = co_await mtx.cancel_and_drain();
        if (d.has_value()) drain_success.fetch_add(1, std::memory_order_acq_rel);
    };

    auto fh = asio::co_spawn(ioc, holder_coro(), asio::use_future);
    std::vector<std::future<void>> futs;
    for (int i = 0; i < N; ++i)
        futs.push_back(asio::co_spawn(ioc, make_waiter(), asio::use_future));
    auto f1 = asio::co_spawn(ioc, make_drainer(0), asio::use_future);
    auto f2 = asio::co_spawn(ioc, make_drainer(1), asio::use_future);

    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, fh, "SeamDrainLatchHolderLifecycle::TwoConcurrentDrainersWithPreDrainHolder/fh")) {
        return;
    }
    fh.get();
    for (auto& f : futs) f.get();
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, f1, "SeamDrainLatchHolderLifecycle::TwoConcurrentDrainersWithPreDrainHolder/f1")) {
        return;
    }
    f1.get();
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, f2, "SeamDrainLatchHolderLifecycle::TwoConcurrentDrainersWithPreDrainHolder/f2")) {
        return;
    }
    f2.get();

    EXPECT_EQ(completed_count.load(), N) << "All N waiters must complete exactly once";
    // If the latch/subscriber mechanism works: no waiter is granted.
    // If it does not: the second drainer may race and grant one.
    EXPECT_EQ(granted_count.load(), 0) << "No waiter must be granted";
    EXPECT_EQ(aborted_count.load(), N) << "All N waiters must be aborted";
    EXPECT_EQ(drain_success.load(), 2) << "Both drainers must succeed";
}

}  // namespace
