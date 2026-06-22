// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_drain_onstrand_cancel_during_reap.cpp — 048 T012 (FR-004/P1-N2)
//
// Witness: a parked waiter is cancelled on the strand DURING the reap.
// The per-waiter phase_ CAS (queued→cancelled) guarantees single-winner:
// either the reap wins (schedule_record_resume with sync_lock_aborted) OR
// on_cancel wins — but NOT both. The waiter is resolved EXACTLY once.
//
// On-strand interleaving (research.md D-2 Gate-A watch-point):
// on_cancel() and the reap CAS both run on the owning strand — they are
// serialised by the strand; the first to CAS wins; the loser no-ops.
//
// Witnesses the CAS single-winner under ASan+TSan (T012).
//
// Design: research.md D-4; INV-B; contracts/async_mutex-contract.md FR-004.
// Task: T012.

#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
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
// N waiters park; a cancel fires on the strand for the FIRST waiter at the
// same time cancel_and_drain() runs. The waiter must be resolved exactly once
// (either sync_lock_aborted from the reap OR sync_lock_aborted from on_cancel
// — both outcomes are sync_lock_aborted; what matters is exactly-once).
// ─────────────────────────────────────────────────────────────────────────────

TEST(DrainOnStrandCancelDuringReap, SingleWinnerCAS) {
    constexpr int N = 4;
    constexpr int REPS = 100;

    for (int rep = 0; rep < REPS; ++rep) {
        std::atomic<int> total_completed{0};
        std::atomic<int> total_aborted{0};
        std::atomic<int> total_granted{0};
        bool drain_ok = false;

        asio::io_context ioc;

        std::vector<asio::cancellation_signal> sigs(N);

        auto main_coro = [&]() -> asio::awaitable<void> {
            async_mutex mtx;
            auto ex = co_await asio::this_coro::executor;

            // Holder keeps the mutex.
            auto holder = co_await mtx.async_lock();
            EXPECT_TRUE(holder.has_value());

            // Park N waiters with their own cancellation slots.
            std::vector<std::future<void>> futs;
            for (int i = 0; i < N; ++i) {
                futs.push_back(asio::co_spawn(
                    ex,
                    asio::bind_cancellation_slot(
                        sigs[i].slot(),
                        [&, i]() -> asio::awaitable<void> {
                            auto r = co_await mtx.async_lock();
                            if (r.has_value()) {
                                total_granted.fetch_add(1, std::memory_order_relaxed);
                                r->release()->unlock();
                            } else if (r.error() == error::sync_lock_aborted) {
                                total_aborted.fetch_add(1, std::memory_order_relaxed);
                            }
                            total_completed.fetch_add(1, std::memory_order_relaxed);
                        }),
                    asio::use_future));
            }

            // Yield to let waiters queue.
            co_await yield_n(N * 2 + 4);

            // Fire cancel on waiter[0] — races the reap CAS.
            // Both are on the same strand, so one will win the CAS.
            asio::post(ex, [&]() { sigs[0].emit(asio::cancellation_type::total); });

            // Release holder and drain concurrently with the cancel.
            holder = expected_t<async_lock_guard>{};
            auto d = co_await mtx.cancel_and_drain();
            drain_ok = d.has_value();

            co_await yield_n(N * 4 + 4);
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
        ASSERT_FALSE(timed_out) << "Rep " << rep << " hung";
        f.get();

        EXPECT_TRUE(drain_ok) << "Rep " << rep << " drain failed";
        // Every waiter must complete exactly once.
        EXPECT_EQ(total_completed.load(), N)
            << "Rep " << rep << " not all waiters completed (double-resume or lost)";
        // No waiter can be both granted AND aborted.
        EXPECT_EQ(total_granted.load() + total_aborted.load(), total_completed.load())
            << "Rep " << rep << " granted+aborted != total_completed";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Stress: fire cancellations on ALL waiters concurrently with the drain.
// ─────────────────────────────────────────────────────────────────────────────

TEST(DrainOnStrandCancelDuringReap, AllWaitersCancelledConcurrentlyWithDrain) {
    constexpr int N = 8;
    constexpr int REPS = 50;

    for (int rep = 0; rep < REPS; ++rep) {
        std::atomic<int> total_completed{0};
        bool drain_ok = false;

        asio::io_context ioc;
        std::vector<asio::cancellation_signal> sigs(N);

        auto coro = [&]() -> asio::awaitable<void> {
            async_mutex mtx;
            auto ex = co_await asio::this_coro::executor;
            auto holder = co_await mtx.async_lock();

            std::vector<std::future<void>> futs;
            for (int i = 0; i < N; ++i) {
                futs.push_back(asio::co_spawn(
                    ex,
                    asio::bind_cancellation_slot(
                        sigs[i].slot(),
                        [&]() -> asio::awaitable<void> {
                            auto r = co_await mtx.async_lock();
                            (void)r;
                            total_completed.fetch_add(1, std::memory_order_relaxed);
                        }),
                    asio::use_future));
            }
            co_await yield_n(N + 2);

            // Post all cancellations on the strand.
            for (int i = 0; i < N; ++i) {
                asio::post(ex, [&, i]() { sigs[i].emit(asio::cancellation_type::total); });
            }

            holder = expected_t<async_lock_guard>{};
            auto d = co_await mtx.cancel_and_drain();
            drain_ok = d.has_value();
            co_await yield_n(N * 4 + 4);
            for (auto& f : futs) f.get();
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
        ASSERT_FALSE(timed_out) << "Rep " << rep << " hung";
        f.get();
        EXPECT_TRUE(drain_ok) << "Rep " << rep << " drain failed";
        EXPECT_EQ(total_completed.load(), N)
            << "Rep " << rep << " not all waiters completed exactly once";
    }
}

}  // namespace
