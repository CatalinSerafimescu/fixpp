// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/interop_fixture_test.cpp — #289(B1) direct pin.
//
// InteropEngineFixture::run_until forwards to the shared fixpp::test_support::
// pump_until, which does NOT revive a context already stopped at entry (a work
// guard does not clear io_context::stopped()). The fixture's own contract does
// revive it — run_until restarts the context before handing off. Without that
// entry restart, a weak negative predicate (e.g. "session left Active") could
// burn its entire wall-clock budget dispatching nothing on a drained context,
// and a downstream "still Active" assertion would pass having proven nothing.
// This is the direct pin for that entry-restart requirement (b1-review.out Q7).

#include <gtest/gtest.h>

#include <asio/post.hpp>
#include <atomic>
#include <chrono>

#include "support/interop_fixture.hpp"

using namespace std::chrono_literals;

TEST(InteropEngineFixtureRunUntil, RevivesContextStoppedAtEntry) {
    fixpp::interop::InteropEngineFixture fx;
    fx.start();

    // Drain the context directly (bypassing run_until) so it is stopped when
    // run_until is called below.
    fx.ioc().run();
    ASSERT_TRUE(fx.ioc().stopped());

    std::atomic<bool> posted_work_ran{false};
    asio::post(fx.ioc(), [&] { posted_work_ran.store(true, std::memory_order_release); });

    const bool ready = fx.run_until(
        [&] { return posted_work_ran.load(std::memory_order_acquire); }, 1s);

    EXPECT_TRUE(ready) << "run_until must restart a context stopped at entry, "
                          "or posted work is never dispatched within the deadline";
    EXPECT_TRUE(posted_work_ran.load(std::memory_order_acquire))
        << "posted work must have run — a context left stopped never dispatches it";
}
