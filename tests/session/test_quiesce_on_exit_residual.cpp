// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_quiesce_on_exit_residual.cpp
//
// #289(c) — checked-in RED/GREEN witness for the residual-work diagnostic
// branch of `fixpp::test_support::quiesce_on_exit`
// (tests/support/pump_until_ready.hpp). That branch — an `ADD_FAILURE()`
// when the io_context has not run out of work by the end of the quiesce
// window — has shipped since #284/#289(a) with no automated coverage: every
// existing caller's happy path never exercises it.
//
// Scope (restated from the #289 B2 design review, c-Q4): this witness
// proves ONLY the diagnostic branch's polarity — outstanding work at
// destruction time fires ADD_FAILURE, no outstanding work stays silent. It
// does NOT prove that every suspended coroutine is detected, that
// `stopped()==true` proves no dangling coroutine exists, that
// `quiesce_on_exit` successfully cancels all work, or that a reported
// residual was necessarily caused by a session frame.
// `mock_clock::sleep_until()` registers a new waiter unconditionally
// whenever the deadline is still in the future
// (src/core/test/mock_clock.cpp:119), while `cancel_sleeps()` is a
// one-shot drain of only the waiters present at the moment it runs
// (src/core/test/mock_clock.cpp:166) — a coroutine whose first run happens
// during the guard's own `run_for` drain (the session liveness loop's
// `sleep_until`, src/session/session.cpp:4816, is the concrete production
// case) can arm a sleep nothing will ever fire. This test does not exercise
// that gap; it is a documented residual of the guard, not something proven
// absent here.
//
// Anchors: tests/support/pump_until_ready.hpp (quiesce_on_exit); #289 B2
// design review c-Q1..c-Q4.

#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <chrono>
#include <fixpp/core/test/mock_clock.hpp>
#include <memory>

#include "support/pump_until_ready.hpp"

using namespace std::chrono_literals;
using fixpp::test_support::quiesce_on_exit;

namespace {

std::shared_ptr<fixpp::core::mock_clock> make_mock_clock(asio::io_context& ioc) {
    using namespace std::chrono;
    auto utc = system_clock::time_point{} + seconds{1704067200};
    auto stp = fixpp::core::steady_time_point{} + seconds{0};
    return std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
}

}  // namespace

// Branch fires: an explicit outstanding-work guard keeps `ioc.stopped()`
// false for the whole (short, 1ms) quiesce budget, so quiesce_on_exit
// reports the residual via ADD_FAILURE. The work guard, and quiesce_on_exit
// itself, must be scoped INSIDE the macro's captured statement — declaring
// quiesce_on_exit before EXPECT_NONFATAL_FAILURE would run its destructor
// after the macro stopped intercepting failures, silently passing.
TEST(QuiesceOnExitResidualWitness, ReportsWhenIocNeverDrains) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);

    EXPECT_NONFATAL_FAILURE(
        ([&] {
            auto keep_alive = asio::make_work_guard(ioc);
            quiesce_on_exit quiesce{ioc, *clock, 1ms};
        }()),
        "quiesce_on_exit: the io_context did not run out of work");
}

// Branch does not fire: an otherwise-empty io_context (no outstanding work,
// no scheduled sleeps) drains and stops within the budget. Any nonfatal
// failure here (i.e. an unexpected ADD_FAILURE) fails this test outright,
// which is exactly the assertion — the branch must stay silent.
TEST(QuiesceOnExitResidualWitness, SilentWhenIocDrainsNormally) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);

    quiesce_on_exit quiesce{ioc, *clock, 1ms};
}
