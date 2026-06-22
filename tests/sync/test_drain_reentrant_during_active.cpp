// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_drain_reentrant_during_active.cpp — 048 T011 (P1-2)
//
// Witness: a second cancel_and_drain() on the strand while the first is
// suspended → the second AWAITS draining_complete_, returns the terminal
// result (ok); no false-success, no early-destroy.
//
// The design (research.md D-2 step 1 + data-model.md reentrancy path):
//   if (draining_.load()) { while(!draining_complete_) co_await post(ex); co_return ok; }
//
// The second caller MUST NOT return ok before the first drain finalizes
// (draining_complete_=true). If it returned eagerly, a caller could destroy
// the mutex while the first drain still has in_flight_resumers_ > 0.
//
// Design: research.md D-2; data-model.md state diagram; contracts INV-A.
// Task: T011.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <fixpp/core/sync/async_mutex.hpp>
#include <future>
#include <vector>

#include "sync/sync_test_support.hpp"

namespace {

using fixpp::core::error;
using fixpp::sync::async_lock_guard;
using fixpp::sync::async_mutex;
using fixpp::sync::expected_t;

using fixpp::sync::test::yield_n;

// ─────────────────────────────────────────────────────────────────────────────
// Two concurrent cancel_and_drain() calls on the same strand.
// The holder keeps the first drain suspended; the second drain must AWAIT
// draining_complete_ and return ok (not false-success, not before finalize).
// ─────────────────────────────────────────────────────────────────────────────

TEST(DrainReentrantDuringActive, SecondDrainerAwaitsFirstCompletion) {
    constexpr int N = 4;

    bool first_drain_ok = false;
    bool second_drain_ok = false;
    std::atomic<int> drain_order{0};  // which drain returned first
    std::atomic<int> completed{0};

    asio::io_context ioc;
    async_mutex mtx;

    auto main_coro = [&]() -> asio::awaitable<void> {
        auto ex = co_await asio::this_coro::executor;

        // Acquire holder to delay drain completion.
        auto holder = co_await mtx.async_lock();
        EXPECT_TRUE(holder.has_value());

        // Park N waiters.
        std::vector<std::future<void>> futs;
        for (int i = 0; i < N; ++i) {
            futs.push_back(asio::co_spawn(
                ex,
                [&]() -> asio::awaitable<void> {
                    auto r = co_await mtx.async_lock();
                    (void)r;
                    completed.fetch_add(1, std::memory_order_relaxed);
                },
                asio::use_future));
        }

        co_await yield_n(N + 2);

        // Launch first and second drain concurrently on the same strand.
        auto fd1 = asio::co_spawn(
            ex,
            [&]() -> asio::awaitable<void> {
                auto d = co_await mtx.cancel_and_drain();
                first_drain_ok = d.has_value();
                drain_order.fetch_add(1, std::memory_order_relaxed);
            },
            asio::use_future);

        // Yield slightly so first drain starts.
        co_await yield_n(2);

        auto fd2 = asio::co_spawn(
            ex,
            [&]() -> asio::awaitable<void> {
                auto d = co_await mtx.cancel_and_drain();
                second_drain_ok = d.has_value();
                drain_order.fetch_add(1, std::memory_order_relaxed);
            },
            asio::use_future);

        // Yield so drains proceed; then release the holder.
        co_await yield_n(N * 4 + 8);
        // Release holder — first drain loop can now complete.
        holder = expected_t<async_lock_guard>{};

        // Wait for both drains to finish.
        co_await yield_n(8);

        fd1.get();
        fd2.get();
        for (auto& f : futs) f.get();
    };

    auto f = asio::co_spawn(ioc, main_coro(), asio::use_future);

    bool timed_out = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        ioc.run_for(std::chrono::milliseconds(100));
        if (f.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) break;
    }
    if (f.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        timed_out = true;
        ioc.stop();
    }
    ASSERT_FALSE(timed_out) << "Test hung — reentrant drain did not complete";
    f.get();

    EXPECT_TRUE(first_drain_ok) << "First drain must return ok";
    EXPECT_TRUE(second_drain_ok) << "Reentrant drain must return ok (not eagerly false-success)";
    EXPECT_EQ(completed.load(), N) << "All N waiters must complete";
    // Both drains must have completed (drain_order incremented twice).
    EXPECT_EQ(drain_order.load(), 2) << "Both drains must have returned";
}

// ─────────────────────────────────────────────────────────────────────────────
// Idempotent: calling cancel_and_drain() multiple times after completion
// returns ok immediately (draining_complete_ is set).
// ─────────────────────────────────────────────────────────────────────────────

TEST(DrainReentrantDuringActive, PostCompletionCallsReturnOk) {
    bool all_ok = true;

    asio::io_context ioc;
    async_mutex mtx;

    auto coro = [&]() -> asio::awaitable<void> {
        // First drain on idle mutex.
        auto d1 = co_await mtx.cancel_and_drain();
        if (!d1.has_value()) all_ok = false;

        // Second call after completion — draining_ and draining_complete_ are
        // already set; must return ok promptly without hanging.
        auto d2 = co_await mtx.cancel_and_drain();
        if (!d2.has_value()) all_ok = false;

        auto d3 = co_await mtx.cancel_and_drain();
        if (!d3.has_value()) all_ok = false;
    };

    auto f = asio::co_spawn(ioc, coro(), asio::use_future);

    bool timed_out = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        ioc.run_for(std::chrono::milliseconds(100));
        if (f.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) break;
    }
    if (f.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        timed_out = true;
        ioc.stop();
    }
    ASSERT_FALSE(timed_out) << "Post-completion reentrant calls hung";
    f.get();
    EXPECT_TRUE(all_ok) << "All three drain calls must return ok";
}

}  // namespace
