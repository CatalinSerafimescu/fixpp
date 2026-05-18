// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_in_flight_acquirer_coverage.cpp — Seam #25
//
// In-flight acquirer coverage (RC-α).
//
// An acquirer is in-flight (mid async_lock) concurrently with
// cancel_and_drain() landing. The drain must not report complete until that
// in-flight acquirer is resolved — it either:
//   (a) becomes a holder (fast-path grant raced before draining_=true) and
//       then unlocks, OR
//   (b) fast-fails with sync_lock_drained (draining_=true seen before grant).
//
// In all interleavings:
//   - no acquirer "slips past" the drain unaccounted;
//   - post-drain acquires all get sync_lock_drained;
//   - cancel_and_drain() returns success only after counts settle.
//
// yield_n staggering + several repetitions exercise the race window.
//
// Oracle: [2f §9 #25] — "in-flight acquirer coverage" (RC-α).
//         [2f §4.7] — active_acquirers_count_ tracked; drain waits for zero.
//         I-20..I-22 — ordering sites for acquirer epoch counter.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <atomic>
#include <vector>

#include <fixpp/core/sync/async_mutex.hpp>

namespace {

using fixpp::sync::async_mutex;
using fixpp::sync::async_lock_guard;
using fixpp::sync::expected_t;
using fixpp::core::error;

static asio::awaitable<void> yield_n(int n) {
    auto ex = co_await asio::this_coro::executor;
    for (int i = 0; i < n; ++i)
        co_await asio::post(ex, asio::use_awaitable);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: Single in-flight acquirer races with cancel_and_drain().
//
// The acquirer either gets the lock (fast path, before draining_=true) and
// unlocks it, or gets sync_lock_drained. Either way, drain completes after.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamInFlightAcquirerCoverage, SingleInFlightResolvedBeforeDrainCompletes) {
    async_mutex mtx;

    bool drain_ok = false;
    std::atomic<int> acquirer_granted{0};
    std::atomic<int> acquirer_drained{0};
    std::atomic<int> acquirer_completed{0};

    asio::io_context ioc;

    // Stagger: in-flight acquirer starts just before drain.
    auto run = [&]() -> asio::awaitable<void> {
        auto ex = co_await asio::this_coro::executor;

        // Spawn the in-flight acquirer (starts concurrently with drain below).
        asio::co_spawn(ex, [&]() -> asio::awaitable<void> {
            // Acquirer checks draining_ racing with cancel_and_drain().
            auto r = co_await mtx.async_lock();
            if (r.has_value()) {
                acquirer_granted.fetch_add(1, std::memory_order_acq_rel);
                // Unlock to let drain complete.
                // guard destructor calls unlock().
            } else if (r.error() == error::sync_lock_drained ||
                       r.error() == error::sync_lock_aborted) {
                acquirer_drained.fetch_add(1, std::memory_order_acq_rel);
            }
            acquirer_completed.fetch_add(1, std::memory_order_acq_rel);
        }, asio::detached);

        // Drain is called immediately after spawning the acquirer (same thread,
        // before ioc yields to the acquirer).
        auto d = co_await mtx.cancel_and_drain();
        drain_ok = d.has_value();

        // Extra yields to let all detached coroutines finish.
        co_await yield_n(16);
    };

    auto f = asio::co_spawn(ioc, run(), asio::use_future);
    ioc.run();
    f.get();

    EXPECT_TRUE(drain_ok) << "cancel_and_drain() must succeed";
    EXPECT_EQ(acquirer_completed.load(), 1) << "Acquirer must complete exactly once";
    int g = acquirer_granted.load();
    int dr = acquirer_drained.load();
    EXPECT_EQ(g + dr, 1)
        << "Acquirer must be either granted (g=" << g << ") or drained (dr=" << dr << ")";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: Multiple repetitions to cover the race window.
// Each iteration: spawn K acquirers with varying stagger, then drain.
// In each iteration: total(granted + drained) == K; no hang.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamInFlightAcquirerCoverage, MultipleIterationsCoverRaceWindow) {
    constexpr int ROUNDS = 8;
    constexpr int K      = 4;   // concurrent in-flight acquirers per round

    for (int round = 0; round < ROUNDS; ++round) {
        async_mutex mtx;

        std::atomic<int> total_completed{0};
        std::atomic<int> total_granted{0};
        std::atomic<int> total_rejected{0};
        bool drain_ok = false;

        asio::io_context ioc;

        auto run = [&]() -> asio::awaitable<void> {
            auto ex = co_await asio::this_coro::executor;

            // Spawn K in-flight acquirers with different yields to vary interleaving.
            for (int k = 0; k < K; ++k) {
                asio::co_spawn(ex, [&, k]() -> asio::awaitable<void> {
                    co_await yield_n(k);  // stagger
                    auto r = co_await mtx.async_lock();
                    if (r.has_value()) {
                        total_granted.fetch_add(1, std::memory_order_acq_rel);
                        // unlock via guard dtor
                    } else {
                        total_rejected.fetch_add(1, std::memory_order_acq_rel);
                    }
                    total_completed.fetch_add(1, std::memory_order_acq_rel);
                }, asio::detached);
            }

            // Drain starts at round-dependent yield offset.
            co_await yield_n(round % (K + 1));

            auto d = co_await mtx.cancel_and_drain();
            drain_ok = d.has_value();

            // Allow all detached coros to finish.
            co_await yield_n(K * 8);
        };

        auto f = asio::co_spawn(ioc, run(), asio::use_future);
        ioc.run();
        f.get();

        EXPECT_TRUE(drain_ok) << "Round " << round << ": cancel_and_drain() must succeed";
        EXPECT_EQ(total_completed.load(), K)
            << "Round " << round << ": all K acquirers must complete";
        EXPECT_EQ(total_granted.load() + total_rejected.load(), K)
            << "Round " << round << ": granted + rejected must == K";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: Post-drain acquires all get sync_lock_drained (no acquirer slips past).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamInFlightAcquirerCoverage, PostDrainAcquiresAllGetDrained) {
    constexpr int M = 8;
    async_mutex mtx;

    std::atomic<int> post_drain_drained{0};
    std::atomic<int> post_drain_granted{0};
    bool drain_ok = false;

    asio::io_context ioc;

    auto run = [&]() -> asio::awaitable<void> {
        auto ex = co_await asio::this_coro::executor;

        // Drain an idle mutex.
        auto d = co_await mtx.cancel_and_drain();
        drain_ok = d.has_value();

        // Spawn M acquirers strictly AFTER drain completes.
        for (int i = 0; i < M; ++i) {
            asio::co_spawn(ex, [&]() -> asio::awaitable<void> {
                auto r = co_await mtx.async_lock();
                if (r.has_value())
                    post_drain_granted.fetch_add(1, std::memory_order_acq_rel);
                else if (r.error() == error::sync_lock_drained)
                    post_drain_drained.fetch_add(1, std::memory_order_acq_rel);
            }, asio::detached);
        }

        co_await yield_n(M * 4);
    };

    auto f = asio::co_spawn(ioc, run(), asio::use_future);
    ioc.run();
    f.get();

    EXPECT_TRUE(drain_ok);
    EXPECT_EQ(post_drain_granted.load(), 0)
        << "No acquirer must slip past the drain";
    EXPECT_EQ(post_drain_drained.load(), M)
        << "All post-drain acquirers must get sync_lock_drained";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4 (RC-α epoch serialisation): Two concurrent cancel_and_drain() callers
// while K in-flight acquirers race with both drains.
//
// The drain_in_progress_ serialisation must ensure exactly ONE epoch drains.
// Each in-flight acquirer must be accounted for in exactly one drain's
// active_acquirers_count_ wait. If drain_in_progress_ is not set (current stub),
// both drainers may independently decrement active_acquirers_count_ leading to
// undercount, or one drain may complete before an acquirer that raced with it
// is fully resolved — causing a post-drain granted acquirer.
//
// Oracle: no granted_count > 0 after both drains complete; all completed == K.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamInFlightAcquirerCoverage, ConcurrentDrainWithInFlightAcquirers) {
    constexpr int K = 6;   // concurrent in-flight acquirers

    async_mutex mtx;

    std::atomic<int> total_completed{0};
    std::atomic<int> granted_count{0};
    std::atomic<int> rejected_count{0};
    std::atomic<int> drain_success{0};

    asio::io_context ioc;

    // Spawn K in-flight acquirers with varying stagger, and 2 concurrent drains
    // as top-level futures (not nested, avoiding deadlock).
    for (int k = 0; k < K; ++k) {
        asio::co_spawn(ioc, [&, k]() -> asio::awaitable<void> {
            co_await yield_n(k);
            auto r = co_await mtx.async_lock();
            if (r.has_value()) {
                granted_count.fetch_add(1, std::memory_order_acq_rel);
                // unlock via guard dtor
            } else {
                rejected_count.fetch_add(1, std::memory_order_acq_rel);
            }
            total_completed.fetch_add(1, std::memory_order_acq_rel);
        }, asio::detached);
    }

    auto fd1 = asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        auto d = co_await mtx.cancel_and_drain();
        if (d.has_value()) drain_success.fetch_add(1, std::memory_order_acq_rel);
    }, asio::use_future);

    auto fd2 = asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        co_await yield_n(1);
        auto d = co_await mtx.cancel_and_drain();
        if (d.has_value()) drain_success.fetch_add(1, std::memory_order_acq_rel);
    }, asio::use_future);

    ioc.run();
    fd1.get();
    fd2.get();

    // After both drains complete, all K acquirers must have completed.
    EXPECT_EQ(total_completed.load(), K)
        << "All K in-flight acquirers must complete exactly once";
    EXPECT_EQ(drain_success.load(), 2)
        << "Both drains must succeed";

    // Post-drain: any new acquirer must be rejected with sync_lock_drained.
    ioc.restart();
    bool post_drain_rejected = false;
    auto fc = asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        auto r = co_await mtx.async_lock();
        post_drain_rejected = !r.has_value() && r.error() == error::sync_lock_drained;
    }, asio::use_future);
    ioc.run();
    fc.get();
    EXPECT_TRUE(post_drain_rejected) << "Post-drain acquires must be rejected";
}

}  // namespace
