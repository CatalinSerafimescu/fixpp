// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/core/test_sleep_cancel_race.cpp — [2d §9.10] Seam 10 (TSan+ASan)
//
// sleep_until + cancel_sleeps race (FR-003 / I-17 / [2d §6.6]): N in-flight
// waiters, cancel_sleeps() fired concurrently from another thread; EVERY
// awaiter completes EXACTLY once (deadline-reached OR operation_aborted),
// with no leak and no double-completion. TSan+ASan-mandatory. Exercised on
// both mock_clock and system_clock_source.
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/thread_pool.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <system_error>
#include <thread>

#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/core/test/mock_clock.hpp>

namespace {

using namespace std::chrono_literals;

// `drain` is invoked every poll iteration while waiting for completions to
// reach N. For mock_clock the awaiter deadline (real steady_clock::now()+50ms)
// is unreachable by frozen mock time, so the ONLY completion path is abort:
// any waiter that registers AFTER the concurrent one-shot cancel_sleeps()
// must be drained by a continued cancel, else its co_spawn coroutine never
// finishes and pool.join() blocks forever (manifests only under TSan, whose
// ~20x slowdown makes the fixed 5ms park window too short for all N to
// register pre-cancel). cancel_sleeps() is idempotent + erase-under-lock, so
// repeating it completes each awaiter exactly once. system_clock_source has
// real, reachable 50ms deadlines, so its stragglers self-complete: no-op.
template <class MakeClock, class Drain>
void race(MakeClock make_clock, Drain drain) {
    asio::thread_pool pool{4};
    auto clk = make_clock(pool.get_executor());

    constexpr int N = 500;
    std::atomic<int> completions{0};
    std::atomic<int> aborted{0};
    std::atomic<int> deadline{0};

    for (int i = 0; i < N; ++i) {
        asio::co_spawn(
            pool,
            [&]() -> asio::awaitable<void> {
                try {
                    co_await clk->sleep_until(
                        std::chrono::steady_clock::now() + 50ms);
                    deadline.fetch_add(1, std::memory_order_relaxed);
                } catch (const std::system_error&) {
                    aborted.fetch_add(1, std::memory_order_relaxed);
                }
                completions.fetch_add(1, std::memory_order_relaxed);
                co_return;
            },
            asio::detached);
    }

    std::this_thread::sleep_for(5ms);            // let waiters park
    std::thread canceller{[&] { clk->cancel_sleeps(); }};
    std::thread canceller2{[&] { clk->cancel_sleeps(); }};  // idempotent/concurrent
    canceller.join();
    canceller2.join();

    // Allow deadline-reached stragglers + posted completions to drain.
    const auto t0 = std::chrono::steady_clock::now();
    while (completions.load(std::memory_order_relaxed) < N &&
           std::chrono::steady_clock::now() - t0 < 5s) {
        drain(clk);
        std::this_thread::sleep_for(5ms);
    }
    pool.join();

    // EXACTLY once per awaiter, no leak, no double-completion.
    EXPECT_EQ(completions.load(), N);
    EXPECT_EQ(aborted.load() + deadline.load(), N);
}

TEST(SeamSleepCancelRace, MockClockEveryAwaiterCompletesExactlyOnce) {
    race(
        [](asio::any_io_executor ex) {
            return std::make_shared<fixpp::core::mock_clock>(
                fixpp::core::utc_time_point{}, fixpp::core::steady_time_point{},
                ex);
        },
        [](auto& clk) { clk->cancel_sleeps(); });
}

TEST(SeamSleepCancelRace, SystemClockSourceEveryAwaiterCompletesExactlyOnce) {
    race(
        [](asio::any_io_executor ex) {
            return std::make_shared<fixpp::core::system_clock_source>(ex);
        },
        [](auto&) {});
}

}  // namespace
