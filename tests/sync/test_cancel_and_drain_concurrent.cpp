// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_cancel_and_drain_concurrent.cpp — Seam #23
//
// Concurrent cancel_and_drain() callers (RC-B).
//
// Multiple coroutines call cancel_and_drain() concurrently on the same mutex
// while waiters are queued. They must be serialised into ONE drain epoch:
//   - every queued waiter reaped exactly once (total aborted == N, no double);
//   - every concurrent cancel_and_drain() caller returns a consistent success;
//   - idempotent: a subsequent cancel_and_drain() after the epoch completes
//     also returns success.
//
// Oracle: [2f §9 #23] — "concurrent cancel_and_drain() serialisation" (RC-B).
//         [2f §4.7.3] — drain_in_progress_ atomic_flag serialises concurrent drains.
// SC-003: idempotent drain; concurrent callers all succeed.
//
// HARNESS NOTE: concurrent drain futures are top-level co_spawn (not nested in
// another coroutine on the same ioc) to avoid deadlock from .get() inside ioc.run().

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <atomic>
#include <future>
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
// Test 1: D=4 concurrent drainers, N=8 queued waiters.
// All drainers must return success; total aborted == N; no double-resume.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamCancelAndDrainConcurrent, MultipleDrainersSerialised) {
    constexpr int N = 8;   // number of waiting acquirers
    constexpr int D = 4;   // number of concurrent drain callers

    async_mutex mtx;

    std::atomic<int> aborted_count{0};
    std::atomic<int> granted_count{0};
    std::atomic<int> completed_count{0};
    std::atomic<int> drain_success_count{0};
    std::atomic<int> drain_fail_count{0};

    asio::io_context ioc;

    // Canonical §4.7.4 sequencing: the D drainers run CONCURRENTLY while the
    // holder is still in its critical section (single ioc.run(), no two-phase
    // split). One drainer wins drain_in_progress_ and becomes the reaper; the
    // others subscribe to the same epoch. The holder's later unlock() observes
    // draining_ == true and short-circuits. (The prior two-phase shape released
    // the holder before any drainer ran, so unlock() legitimately granted the
    // waiters per US1 — not what this RC-B serialisation seam tests.)
    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        co_await yield_n(N * 4 + D + 8);  // hold past drainer start + reap
        // Guard dtor → unlock() (draining_ == true → short-circuit).
    };

    auto make_waiter = [&]() -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx.async_lock();
        if (r.has_value()) {
            granted_count.fetch_add(1, std::memory_order_acq_rel);
        } else if (r.error() == error::sync_lock_aborted ||
                   r.error() == error::sync_lock_drained) {
            aborted_count.fetch_add(1, std::memory_order_acq_rel);
        }
        completed_count.fetch_add(1, std::memory_order_acq_rel);
    };

    auto make_drainer = [&](int i) -> asio::awaitable<void> {
        co_await yield_n(N * 2 + i);  // let holder + waiters settle; stagger
        auto d = co_await mtx.cancel_and_drain();
        if (d.has_value())
            drain_success_count.fetch_add(1, std::memory_order_acq_rel);
        else
            drain_fail_count.fetch_add(1, std::memory_order_acq_rel);
    };

    auto fh = asio::co_spawn(ioc, holder_coro(), asio::use_future);
    std::vector<std::future<void>> futs;
    for (int i = 0; i < N; ++i)
        futs.push_back(asio::co_spawn(ioc, make_waiter(), asio::use_future));
    std::vector<std::future<void>> drain_futs;
    drain_futs.reserve(D);
    for (int i = 0; i < D; ++i)
        drain_futs.push_back(
            asio::co_spawn(ioc, make_drainer(i), asio::use_future));

    ioc.run();
    fh.get();
    for (auto& f : futs) f.get();
    for (auto& df : drain_futs) df.get();

    EXPECT_EQ(completed_count.load(), N)
        << "All N waiters must complete exactly once";
    EXPECT_EQ(granted_count.load(), 0)
        << "No waiter must be granted (drain aborts all)";
    EXPECT_EQ(aborted_count.load(), N)
        << "Every waiter must be aborted exactly once (total aborted == N)";
    EXPECT_EQ(drain_success_count.load(), D)
        << "All D concurrent drainers must return success";
    EXPECT_EQ(drain_fail_count.load(), 0)
        << "No drainer must fail";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: Idempotent drain — a subsequent cancel_and_drain() after completion
// also returns success (no error, no hang).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamCancelAndDrainConcurrent, IdempotentDrainAfterEpochCompletes) {
    async_mutex mtx;
    bool first_drain_ok  = false;
    bool second_drain_ok = false;

    asio::io_context ioc;

    auto run = [&]() -> asio::awaitable<void> {
        // First drain.
        auto d1 = co_await mtx.cancel_and_drain();
        first_drain_ok = d1.has_value();

        // Second drain after completion — must also succeed.
        auto d2 = co_await mtx.cancel_and_drain();
        second_drain_ok = d2.has_value();
    };

    auto f = asio::co_spawn(ioc, run(), asio::use_future);
    ioc.run();
    f.get();

    EXPECT_TRUE(first_drain_ok)  << "First cancel_and_drain() must succeed";
    EXPECT_TRUE(second_drain_ok) << "Second (idempotent) cancel_and_drain() must also succeed";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: No waiter is double-resumed under concurrent drainers.
// D=3 drainers, N=12 waiters.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamCancelAndDrainConcurrent, NoDoubleResumeUnderConcurrentDrain) {
    constexpr int N = 12;
    constexpr int D = 3;

    async_mutex mtx;
    std::atomic<int> total_completions{0};
    std::atomic<int> drain_success_count{0};

    asio::io_context ioc;

    // Phase 1: hold + queue N waiters, then release.
    auto phase1 = [&]() -> asio::awaitable<void> {
        auto ex = co_await asio::this_coro::executor;

        auto holder = co_await mtx.async_lock();
        EXPECT_TRUE(holder.has_value());

        for (int i = 0; i < N; ++i) {
            asio::co_spawn(ex, [&]() -> asio::awaitable<void> {
                co_await yield_n(1);
                auto r = co_await mtx.async_lock();
                (void)r;
                total_completions.fetch_add(1, std::memory_order_acq_rel);
            }, asio::detached);
        }

        co_await yield_n(N * 4);
        holder = expected_t<async_lock_guard>{};  // unlock
    };

    auto fp1 = asio::co_spawn(ioc, phase1(), asio::use_future);
    ioc.run();
    fp1.get();

    // Phase 2: D concurrent drainers.
    ioc.restart();
    std::vector<std::future<void>> dfuts;
    dfuts.reserve(D);
    for (int i = 0; i < D; ++i) {
        dfuts.push_back(
            asio::co_spawn(ioc, [&, i]() -> asio::awaitable<void> {
                co_await yield_n(i);
                auto d = co_await mtx.cancel_and_drain();
                if (d.has_value())
                    drain_success_count.fetch_add(1, std::memory_order_acq_rel);
            }, asio::use_future));
    }

    ioc.run();
    for (auto& df : dfuts) df.get();

    EXPECT_EQ(total_completions.load(), N)
        << "Each of the N waiters must complete exactly once";
    EXPECT_EQ(drain_success_count.load(), D)
        << "All D concurrent drainers must succeed";
}

}  // namespace
