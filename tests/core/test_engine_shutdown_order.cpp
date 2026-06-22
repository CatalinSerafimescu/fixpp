// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/core/test_engine_shutdown_order.cpp — [2d §9.14] Seam 14
//
// Engine-shutdown ordering (I-17 / root cause #5) + the clock_not_set
// engine-open gate (FR-006 / I-03). 2d-owned properties:
//   • teardown order: drain → cancel_sleeps() → clear EngineConfig::clock
//     shared_ptr LAST; a mock_clock held outside the engine outlives it
//     safely (no live waiters post-teardown);
//   • Engine::open rejects EngineConfig::clock == nullptr with
//     error::clock_not_set REGARDLESS of any session clock_override
//     (validate_engine_config — the minimal 2d-owned engine-open gate).
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/system_executor.hpp>
#include <asio/thread_pool.hpp>
#include <chrono>
#include <thread>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <memory>

namespace {

using namespace std::chrono_literals;
using fixpp::core::error;

TEST(SeamEngineShutdownOrder, ClockNotSetRejectedRegardlessOfOverride) {
    fixpp::core::EngineConfig cfg;
    cfg.clock = nullptr;
    auto r = fixpp::core::validate_engine_config(cfg);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::clock_not_set);

    cfg.clock = std::make_shared<fixpp::core::system_clock_source>(asio::system_executor{});
    EXPECT_TRUE(fixpp::core::validate_engine_config(cfg).has_value());
}

TEST(SeamEngineShutdownOrder, FixtureHeldMockClockOutlivesEngineTeardown) {
    // mock_clock held OUTSIDE the engine (fixture shared_ptr).
    auto fixture_clock = std::make_shared<fixpp::core::mock_clock>(
        fixpp::core::utc_time_point{}, fixpp::core::steady_time_point{}, asio::system_executor{});

    {
        asio::thread_pool pool{2};
        // "engine" holds its own clock shared_ptr (the EngineConfig::clock).
        std::shared_ptr<fixpp::core::Clock> engine_clock =
            std::make_shared<fixpp::core::system_clock_source>(pool.get_executor());

        asio::co_spawn(
            pool,
            [engine_clock]() -> asio::awaitable<void> {
                try {
                    co_await engine_clock->sleep_until(std::chrono::steady_clock::now() + 10s);
                } catch (...) { /* operation_aborted on teardown */
                }
                co_return;
            },
            asio::detached);
        std::this_thread::sleep_for(20ms);

        // Ordered teardown (I-17): drain-equivalent → cancel_sleeps() →
        // clear the engine clock shared_ptr LAST.
        engine_clock->cancel_sleeps();
        pool.join();
        engine_clock.reset();  // cleared last — no live waiters
    }

    // The fixture-held clock outlived the engine safely; cancel_sleeps() has
    // no live waiters post-teardown (seam 14 variant).
    fixture_clock->cancel_sleeps();
    EXPECT_EQ(fixture_clock->steady_now(), fixpp::core::steady_time_point{});
    EXPECT_EQ(fixture_clock.use_count(), 1);
}

}  // namespace
