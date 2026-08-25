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

#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <atomic>
#include <chrono>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <asio/awaitable.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <memory>
#include <stdexcept>

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
            // 0 ms teardown bound: stop_within's `while (now < t0 + bound)` body
            // never executes, so stop() is co_spawned but never pumped and cannot
            // complete. No hanging session and no wall-clock cost is needed.
            fixpp::interop::InteropEngineFixture fx{{}, std::chrono::milliseconds{0}};
            fx.start();
        }()),
        "Engine::stop() did not complete successfully");
}

// Counter-direction: an engine that stops cleanly must leave the branch silent.
// Without this, the test above is satisfied by a destructor that fails
// unconditionally. Any non-fatal failure here fails this test outright, which is
// exactly the assertion.
TEST(InteropEngineFixtureTeardown, CleanStopReportsNothing) {
    // Non-vacuity control for MissPathActuallyReleasesTheEngine below: the SAME
    // weak_ptr probe must read the OPPOSITE way here. Without this, a probe that
    // could never expire (e.g. some other strong reference kept the clock alive)
    // would satisfy that test while proving nothing. A zero is only meaningful
    // once the instrument has been seen non-zero.
    std::weak_ptr<fixpp::core::Clock> weak_clock;
    {
        asio::io_context probe_ioc;
        auto clock =
            std::make_shared<fixpp::core::system_clock_source>(probe_ioc.get_executor());
        weak_clock = clock;
        fixpp::core::EngineConfig cfg;
        cfg.clock = clock;
        clock.reset();

        fixpp::interop::InteropEngineFixture probe_fx{std::move(cfg)};
        probe_fx.start();
        const auto probe_elapsed = probe_fx.stop_within(std::chrono::seconds{5});
        EXPECT_LT(probe_elapsed, std::chrono::seconds{5});
        // ~probe_fx destroys the Engine normally here (stop completed), so the
        // EngineConfig-owned clock copy dies with it.
    }
    EXPECT_TRUE(weak_clock.expired())
        << "the weak_ptr probe cannot detect a DESTROYED Engine, so its "
           "non-expiry on the miss path would prove nothing";

    fixpp::interop::InteropEngineFixture fx;
    fx.start();
    const auto elapsed = fx.stop_within(std::chrono::seconds{5});
    EXPECT_LT(elapsed, std::chrono::seconds{5})
        << "an idle engine must stop well inside the bound";
    // stop_completed(), NOT stopped(). stopped() is the predicate this whole
    // change exists to discredit — it is true from step 1 of teardown onward, so
    // asserting it here would leave this counter-direction test green even if
    // stop_completed_ were wired wrong, which is precisely the mutant it is
    // supposed to catch.
    EXPECT_TRUE(fx.stop_completed()) << "the spawned stop() operation must have resolved";
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
// flag flips. The one-handler granularity is REQUIRED, not merely tidy: if
// stop() were allowed to run to completion, the destructor's stop_within(0 ms)
// would still evaluate its post-loop readiness check, set stop_completed_, take
// the clean path and emit NO failure — and EXPECT_NONFATAL_FAILURE would then
// fail. A slice-based pump could not reliably stop short of completion.
TEST(InteropEngineFixtureTeardown, StoppedFlagIsNotTreatedAsCompletion) {
    bool reached_window = false;

    EXPECT_NONFATAL_FAILURE(
        ([&reached_window] {
            fixpp::interop::InteropEngineFixture fx{{}, std::chrono::milliseconds{0}};
            fx.start();

            // Spawn stop() without letting it run to completion (0 ms bound
            // pumps nothing), then step the context one handler at a time.
            (void)fx.stop_within(std::chrono::milliseconds{0});
            for (int i = 0; i < 1000 && !fx.stopped() && fx.ioc().poll_one() != 0; ++i) {
            }

            // The window: the Engine says "stopped", the operation says "not
            // finished". If this does not hold the test below proves nothing,
            // so assert it rather than let the run pass quietly.
            //
            // Honest note on the second conjunct: stop_completed_ is written
            // only inside stop_within, and nothing between the 0 ms call above
            // and this line calls it, so !stop_completed() cannot currently be
            // false. It is retained as a pin against a future edit that re-enters
            // stop_within here — NOT because it discriminates today.
            reached_window = fx.stopped() && !fx.stop_completed();
        }()),
        "Engine::stop() did not complete successfully");

    EXPECT_TRUE(reached_window)
        << "could not reach the stopped()==true / stop_completed()==false window, "
           "so this test did not exercise the distinction it exists to pin";
}

// ── #292 — the release() is pinned BEHAVIOURALLY, not just by its message ────
//
// Gap this closes: the two tests above check the ADD_FAILURE text, so a mutant
// that drops `engine_.release()` while keeping the message passes both of them.
// Neither instrument that would otherwise notice is reliable — ~Engine's
// `assert(stopped_)` is a no-op under NDEBUG, and __lsan_ignore_object makes the
// leak invisible to LeakSanitizer by design. So "the Engine outlived ioc_" was
// COVERED but UNASSERTED.
//
// The pin observes a value that cannot be terminal: the Engine holds its
// EngineConfig BY VALUE, so it owns a shared_ptr copy of the configured clock.
// If the destructor released the Engine, that copy is never destroyed and the
// weak_ptr stays live; if the Engine was destroyed instead, every copy dies with
// it and the weak_ptr expires. No spelling of the failure message can fake it.
//
// The clock is supplied by the caller here so the test holds the only other
// strong reference and can drop it deliberately — with_clock() honours an
// explicit clock rather than injecting its own.
TEST(InteropEngineFixtureTeardown, MissPathActuallyReleasesTheEngine) {
    std::weak_ptr<fixpp::core::Clock> weak_clock;

    EXPECT_NONFATAL_FAILURE(
        ([&weak_clock] {
            asio::io_context probe_ioc;
            auto clock = std::make_shared<fixpp::core::system_clock_source>(
                probe_ioc.get_executor());
            weak_clock = clock;

            fixpp::core::EngineConfig cfg;
            cfg.clock = clock;

            fixpp::interop::InteropEngineFixture fx{std::move(cfg),
                                                    std::chrono::milliseconds{0}};
            fx.start();

            // Drop the test's own strong reference, so after ~fx the ONLY thing
            // that can still be holding the clock alive is a leaked Engine.
            // Sampled here rather than before construction: the fixture's ctor
            // copies the config, so a count taken earlier would be inflated by
            // copies that are about to die anyway and would prove nothing.
            clock.reset();
        }()),
        "Engine::stop() did not complete successfully");

    EXPECT_FALSE(weak_clock.expired())
        << "the Engine was DESTROYED on the bounded-stop miss path instead of "
           "being released: its EngineConfig-owned clock reference died with it. "
           "A destroyed Engine means ~Engine ran with teardown frames still "
           "suspended in ioc_, which is the silent failure #292 exists to prevent.";
}

// ── #292 — a throwing stop() is REPORTED and RELEASED (gate-b/r2 P1-1) ──────
//
// Withdraws two round-1 waivers that both rested on "no test seam exists to make
// Engine::stop() throw". One does: every interop target compiles with
// FIXPP_TEST_HOOKS (tests/interop/CMakeLists.txt:93), Engine exposes
// set_post_send_drain_hook() (engine.hpp:340), and stop() co_awaits it
// (engine.cpp:1304) BEFORE step 4 (session close) and step 5 (registry clear).
// A throwing hook therefore aborts teardown midway — exactly the state to test.
//
// The defect this pins was introduced by the round-1 fix for P1-3. Wrapping the
// whole destructor in ONE try/catch meant a throw out of stop_within() jumped
// straight to the handler, skipping the completion check, the release() and the
// report: the Engine was destroyed SILENTLY with teardown incomplete, and
// because stop() sets stopped_ at step 1, ~Engine's assert passed on the way out.
// That is the same silent class the whole PR exists to remove.
//
// Two properties, one test: the failure is REPORTED (the SPI matcher), and the
// Engine is RELEASED rather than destroyed (the weak_ptr, live afterwards).
//
// SCOPE, stated so the test is not read as proving more (gate-b/r3 P2-1, P2-2):
//   - This fixture registers NO sessions, so every per-session cancel / close /
//     join loop inside stop() is empty. The test proves the destructor handles an
//     exceptional future, reports, and retains the Engine — it does NOT prove the
//     hook fired before a NON-EMPTY registry was cleared. Mutation that stays
//     green: move the hook after registry_.clear(). Closing that needs a
//     registered session and a probe on registry-owned state.
//   - The weak_ptr observes CLOCK RETENTION, not Engine identity. Mutation that
//     stays green: copy the clock elsewhere on the failure branch, then destroy
//     the Engine. Closing that needs an Engine-destruction counter, which is a
//     src/ seam this PR cannot add.
// Both are recorded as gaps rather than waived silently.
TEST(InteropEngineFixtureTeardown, ThrowingStopIsReportedAndReleasesTheEngine) {
    std::weak_ptr<fixpp::core::Clock> weak_clock;

    EXPECT_NONFATAL_FAILURE(
        ([&weak_clock] {
            asio::io_context probe_ioc;
            auto clock = std::make_shared<fixpp::core::system_clock_source>(
                probe_ioc.get_executor());
            weak_clock = clock;
            fixpp::core::EngineConfig cfg;
            cfg.clock = clock;

            fixpp::interop::InteropEngineFixture fx{std::move(cfg)};
            fx.start();
            fx.engine().set_post_send_drain_hook([]() -> asio::awaitable<void> {
                throw std::runtime_error("post-send-drain hook throws (gate-b/r2 P1-1)");
                co_return;
            });
            clock.reset();
        }()),
        "Engine::stop() did not complete successfully");

    EXPECT_FALSE(weak_clock.expired())
        << "a stop() that threw partway through teardown left the Engine DESTROYED "
           "rather than released — its EngineConfig-owned clock died with it. "
           "(Precisely (gate-b/r3 P3-1): on THIS path the stop operation completed "
           "exceptionally, so its frames are no longer suspended in ioc_. What is "
           "unsafe is that teardown stopped before session close and registry "
           "clear, so Engine-owned state may still be referenced.)";
}

// ── #292 — exactly one teardown body runs, and its failure is not masked ─────
//
// NAMED FOR THE PROPERTY THAT IS LOAD-BEARING, which is not literally "co_spawn
// was called once" (gate-b/r3 P1-2). Codex proposed a mutant that keeps the
// tracked future and co_spawns an EXTRA detached stop() beside it. Measured: the
// test stays green — and so does the system, because Engine::stop() opens with
// its own idempotency guard, `if (stopped_.load(acquire)) co_return;`
// (engine.cpp:1163). A second operation spawned after the first has set the flag
// returns immediately and never reaches the hook. An extra spawn is INERT.
//
// So the literal spawn count is neither observable test-side nor the thing that
// can hurt. The two properties that CAN are both asserted here:
//   1. exactly ONE teardown body runs   — the hook-entry counter
//   2. that operation's failure is not masked — the rethrow
// Deleting the `if (!stop_fut_.valid())` guard breaks (2): the second call
// co_spawns a fresh stop(), which returns normally because stopped_ is already
// true, silently masking the first operation's exception and abandoning it.
//
// Round 1 waived all of this as unwitnessable, on an argument that only ever
// covered two NORMALLY-completing idle stops. It never covered a first operation
// that completed by THROWING, which the hook makes reachable deterministically.
TEST(InteropEngineFixtureTeardown, ExactlyOneTeardownBodyRunsAndItsFailureIsNotMasked) {
    // The observation is a COUNT, not just the rethrow (gate-b/r3 P1-2). An
    // earlier version asserted only that the second call rethrows the original
    // exception. That reddens when the guard is deleted in the obvious way (the
    // tracked future is replaced, masking the exception) but stays GREEN under a
    // different violation of the same named property: keep assigning the tracked
    // future exactly as now AND co_spawn an extra detached stop() beside it. Both
    // bodies can pass the outer stopped_ check before either sets the flag, the
    // tracked future still rethrows, and nothing notices two teardowns ran.
    //
    // Counting hook entries observes the operations themselves rather than the
    // handle to one of them, so an untracked spawn cannot hide behind it.
    std::atomic<int> hook_entries{0};

    EXPECT_NONFATAL_FAILURE(
        ([&hook_entries] {
            fixpp::interop::InteropEngineFixture fx;
            fx.start();
            fx.engine().set_post_send_drain_hook(
                [&hook_entries]() -> asio::awaitable<void> {
                    hook_entries.fetch_add(1, std::memory_order_relaxed);
                    throw std::runtime_error("post-send-drain hook throws (gate-b/r3 P1-2)");
                    co_return;
                });

            // Spawn operation #1 and drive it far enough to reach the throwing
            // hook, so its future is ready-with-exception rather than pending.
            (void)fx.stop_within(std::chrono::milliseconds{0});
            fx.ioc().restart();
            fx.ioc().run_for(std::chrono::seconds{2});

            // The second call must observe THAT operation and rethrow. A freshly
            // spawned stop() would return normally (stopped_ is already true) and
            // silently mask the first one's failure.
            bool rethrew = false;
            try {
                (void)fx.stop_within(std::chrono::seconds{5});
            } catch (const std::runtime_error&) {
                rethrew = true;
            }
            EXPECT_TRUE(rethrew)
                << "the second stop_within() did not rethrow the in-flight "
                   "operation's exception, which means it spawned a SECOND stop() "
                   "and abandoned the first — the frame is now unowned in ioc_";
        }()),
        "Engine::stop() did not complete successfully");

    EXPECT_EQ(hook_entries.load(std::memory_order_relaxed), 1)
        << "Engine::stop()'s teardown body ran " << hook_entries.load(std::memory_order_relaxed)
        << " times, not once. The rethrow assertion above cannot see this — it "
           "observes the TRACKED future, so an extra spawn leaves that handle "
           "untouched. This counter observes the operations themselves.";
}
