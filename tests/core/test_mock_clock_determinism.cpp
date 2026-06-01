// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/core/test_mock_clock_determinism.cpp — [2d §9.1] Seam 1
//
// mock_clock determinism (E3 / D-10 / FR-002): two identically-seeded
// instances driven by the SAME advance sequence produce identical
// now/steady_now/wake-up order. Covers step_to (fast-forward) and
// set_utc_skew (wall-only NTP skew that does NOT move steady_now — US2 AC-3).
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <chrono>
#include <fixpp/core/test/mock_clock.hpp>
#include <vector>

namespace {

using fixpp::core::mock_clock;
using fixpp::core::steady_time_point;
using fixpp::core::utc_time_point;
using namespace std::chrono_literals;

struct run_result {
    std::vector<int> wake_order;
    fixpp::core::utc_time_point utc_after;
    fixpp::core::steady_time_point steady_after;
};

// Drive 4 sleepers with staggered deadlines, then advance in 3 steps; record
// the order awaiters wake. Identical seeds + advance ⇒ identical result.
run_result drive(std::chrono::nanoseconds skew_at_step2) {
    asio::io_context ioc;
    const utc_time_point u0{1'700'000'000s};
    const steady_time_point s0{};
    mock_clock clk{u0, s0, ioc.get_executor()};

    run_result r;
    auto sleeper = [&](int id, std::chrono::nanoseconds d) -> asio::awaitable<void> {
        co_await clk.sleep_until(s0 + d);
        r.wake_order.push_back(id);
        co_return;
    };
    asio::co_spawn(ioc, sleeper(1, 10ms), asio::detached);
    asio::co_spawn(ioc, sleeper(2, 30ms), asio::detached);
    asio::co_spawn(ioc, sleeper(3, 20ms), asio::detached);
    asio::co_spawn(ioc, sleeper(4, 30ms), asio::detached);
    ioc.poll();  // register all waiters

    clk.advance(15ms);
    ioc.poll();                       // wakes #1
    clk.set_utc_skew(skew_at_step2);  // wall-only; steady unaffected
    clk.advance(10ms);
    ioc.poll();  // steady=25ms wakes #3
    clk.step_to(s0 + 40ms);
    ioc.poll();  // wakes #2,#4 (insertion order)

    r.utc_after = clk.now();
    r.steady_after = clk.steady_now();
    return r;
}

TEST(SeamMockClockDeterminism, IdenticalSeedAndAdvanceYieldIdenticalOrder) {
    auto a = drive(5ms);
    auto b = drive(5ms);
    EXPECT_EQ(a.wake_order, b.wake_order);
    EXPECT_EQ(a.steady_after, b.steady_after);
    EXPECT_EQ(a.utc_after, b.utc_after);
    // #1(10) then #3(20) then #2(30),#4(30) in registration order.
    ASSERT_EQ(a.wake_order.size(), 4u);
    EXPECT_EQ(a.wake_order.front(), 1);
    EXPECT_EQ(a.wake_order[1], 3);
}

TEST(SeamMockClockDeterminism, UtcSkewDoesNotMoveSteady) {
    auto no_skew = drive(0ms);
    auto with_skew = drive(500ms);
    // steady_now unaffected by wall-clock skew (US2 AC-3 / I-02) ...
    EXPECT_EQ(no_skew.steady_after, with_skew.steady_after);
    // ... but now() carries the skew.
    EXPECT_EQ(with_skew.utc_after - no_skew.utc_after, std::chrono::nanoseconds{500ms});
}

}  // namespace
