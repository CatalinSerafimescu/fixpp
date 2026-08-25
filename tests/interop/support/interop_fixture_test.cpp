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

#include <gtest/gtest-spi.h>
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

// ── #292 — the bounded-stop miss is REPORTED, not an abort ───────────────────
//
// ~InteropEngineFixture used to call stop_within(30s) and DISCARD the result, so
// a stop() that never completed was invisible; ~Engine's `assert(stopped_)`
// (src/session/engine.cpp:145) then turned a teardown regression into a SIGABRT
// instead of a named failure. This is the direct witness for the repaired
// branch: the miss is taken deterministically (teardown bound 0 ms, so
// stop_within's pump loop body never runs and stop() cannot have completed) and
// the destructor must produce a named non-fatal failure.
//
// The fixture MUST be scoped INSIDE the macro's statement. Declaring it before
// EXPECT_NONFATAL_FAILURE would run its destructor after the macro stopped
// intercepting failures, and this test would pass while asserting nothing — the
// same trap called out in tests/session/test_quiesce_on_exit_residual.cpp:110.
//
// Polarity, both directions:
//   MISS  -> exactly one non-fatal failure naming the bound   (this test)
//   CLEAN -> no failure at all                                (the test below)
TEST(InteropEngineFixtureTeardown, BoundedStopMissReportsNamedFailure) {
    EXPECT_NONFATAL_FAILURE(
        ([] {
            fixpp::interop::InteropEngineFixture fx;
            fx.start();
            // 0 ms: stop_within's `while (now < t0 + bound)` body never executes,
            // so stop() is co_spawned but never pumped and cannot complete. No
            // hanging session and no wall-clock cost is needed to reach the branch.
            fx.set_teardown_bound_for_test(std::chrono::milliseconds{0});
        }()),
        "Engine::stop() did not complete within the");
}

// Counter-direction: an engine that stops cleanly must leave the branch silent.
// Without this, the test above is satisfied by a destructor that fails
// unconditionally. Any non-fatal failure here fails this test outright, which is
// exactly the assertion.
TEST(InteropEngineFixtureTeardown, CleanStopReportsNothing) {
    fixpp::interop::InteropEngineFixture fx;
    fx.start();
    const auto elapsed = fx.stop_within(std::chrono::seconds{5});
    EXPECT_LT(elapsed, std::chrono::seconds{5})
        << "an idle engine must stop well inside the bound";
    EXPECT_TRUE(fx.stopped()) << "stop() must have completed";
    // ~fx runs here and must add no failure.
}

// ── #292 — the case the FIRST fix attempt got wrong ──────────────────────────
//
// Engine::stop() stores stopped_=true at STEP 1 of its teardown
// (src/session/engine.cpp:1196) and only then cancels loops, joins them, closes
// sessions and clears the registry (through engine.cpp:1343). So there is a real
// window in which Engine::stopped() reports true while teardown frames are still
// suspended in the io_context.
//
// A teardown check written against stopped() takes its "safe, nothing to do"
// branch in exactly that window: ~Engine then runs, its stopped_ assert PASSES
// (so there is no abort to notice), and the still-suspended frames are left
// holding a destroyed Engine. That is strictly worse than the abort #292 was
// filed about, because it is silent. The fixture therefore gates on the spawned
// operation's own completion, and this test pins that distinction.
//
// Reaching the window deterministically: poll_one() dispatches at most one ready
// handler, so we step the context one handler at a time and stop the moment the
// flag flips. No sleeping, no timing assumption.
TEST(InteropEngineFixtureTeardown, StoppedFlagIsNotTreatedAsCompletion) {
    bool reached_window = false;

    EXPECT_NONFATAL_FAILURE(
        ([&reached_window] {
            fixpp::interop::InteropEngineFixture fx;
            fx.start();

            // Spawn stop() without letting it run to completion (0 ms bound
            // pumps nothing), then step the context one handler at a time.
            (void)fx.stop_within(std::chrono::milliseconds{0});
            for (int i = 0; i < 1000 && !fx.stopped(); ++i) {
                if (fx.ioc().stopped()) {
                    fx.ioc().restart();
                }
                if (fx.ioc().poll_one() == 0) {
                    break;
                }
            }

            // The window: the Engine says "stopped", the operation says "not
            // finished". If this does not hold the test below proves nothing,
            // so assert it rather than let the run pass quietly.
            reached_window = fx.stopped() && !fx.stop_completed();

            fx.set_teardown_bound_for_test(std::chrono::milliseconds{0});
        }()),
        "Engine::stop() did not complete within the");

    EXPECT_TRUE(reached_window)
        << "could not reach the stopped()==true / stop_completed()==false window, "
           "so this test did not exercise the distinction it exists to pin";
}
