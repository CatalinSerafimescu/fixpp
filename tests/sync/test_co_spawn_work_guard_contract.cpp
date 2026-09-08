// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_co_spawn_work_guard_contract.cpp — #289 batch 21
//
// WHAT THIS PINS, AND WHY IT IS A TEST RATHER THAN A PARAGRAPH
// ─────────────────────────────────────────────────────────────────────────────
// The #289 triage keeps reaching one question: after a `run()` that returned,
// may a `.get()` on a `co_spawn`'d future still block?
//
// `tests/support/pump_until_ready.hpp` answered it with two hazards, and its
// hazard (a) — "the frame is suspended on something this context does not drive,
// so the work count reaches zero with the frame still parked" — is FALSE for a
// future spawned on the context being driven. `asio::co_spawn` holds
// `execution::outstanding_work.tracked` on the SPAWN executor for the frame's
// whole lifetime (`asio/impl/co_spawn.hpp`, `co_spawn_work_guard` /
// `co_spawn_state`), so a live frame is outstanding work whatever it is parked
// on. That converts a per-file question about suspension points into a
// STRUCTURAL property — but only under two clauses, and the arms below establish
// both, so the argument is measured rather than read off a header.
//
//   clause 1  the spawn executor's context IS the driven context   (arm 4)
//   clause 2  the run is not a post-exhaustion no-op               (arm 3)
//
// ⚠️ ARM 2 IS THE POSITIVE CONTROL AND IT IS NOT OPTIONAL. Every other arm asserts
// about `stopped()`; an instrument wired so that `stopped()` could only read one
// way would pass all of them while measuring nothing. Arm 2 is the case that must
// read the OTHER way.
//
// THE INSTRUMENT: `ioc.poll()` then `ioc.stopped()`. `poll()` runs whatever is
// ready and returns; it does not stop a context, so `stopped()` afterwards means
// exactly "work was exhausted", which is the quantity every arm is about.
//
// ⚠️ THERE IS DELIBERATELY NO TIME BUDGET HERE, so there is no band a slow
// sanitiser lane can blow. An earlier draft used `run_for(200ms)` and paid that
// 200 ms ON THE PASSING PATH ONLY: an arm expecting `!stopped()` can never exhaust
// at any budget, so the wait bought nothing, while a real regression would have
// returned EARLY. `poll()` measures the same quantity at zero cost. [#394 is the
// adjacent lesson — a threshold derived as a ratio to another timeout is wrong in
// both directions; the cheapest way not to have that problem is not to introduce a
// threshold.]
//
// NO `GTEST_SKIP()` PATH EXISTS HERE, deliberately: a skipped cell also exits 0
// and ctest reports it as `Passed`, so a green tier is what a skip looks like.
// These arms exercise `asio::co_spawn` only, so every platform runs all of them —
// which is the point, since the guard lives in `co_spawn` and not in the
// backend, and Tier 2 (IOCP) is what turns that from a claim into a reading.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <future>

namespace {

using namespace std::chrono_literals;

// A duration no test run reaches, so the op it belongs to never completes on its
// own. `1h` rather than `hours::max()`: the latter overflows some steady_clock
// conversions on Windows.
//
// ⚠️ TEARDOWN USES `expires_after(0s)`, NOT `cancel()`, AND THE REASON IS THIS
// FILE'S OWN CLAIM. A frame co_spawn'd onto a context that is stopped (arm 3) or
// never driven (arm 4) has NOT STARTED — it has not reached `async_wait`, so
// there is no pending wait for `cancel()` to abort, and the subsequent `run()`
// starts the frame, arms the one-hour timer and WEDGES THE BINARY. Both arms did
// exactly that on their first run. Re-arming the timer to fire immediately
// releases the frame whether or not it has started.
constexpr auto kNever = 1h;

bool is_ready(std::future<void>& f) { return f.wait_for(0s) == std::future_status::ready; }

// ── ARM 1 — the claim ────────────────────────────────────────────────────────
// A live frame spawned on `ioc` but parked on an op `ioc` does not drive still
// holds `ioc`'s work count above zero. This is the case `pump_until_ready.hpp`'s
// hazard (a) claims can exhaust; it cannot.
TEST(SyncCoSpawnWorkGuard, LiveFrameParkedOnForeignOpKeepsContextUnexhausted) {
    asio::io_context ioc;
    asio::io_context other;  // never driven, so nothing on it can ever complete
    asio::steady_timer t{other, kNever};

    auto fut = asio::co_spawn(
        ioc, [&t]() -> asio::awaitable<void> { co_await t.async_wait(asio::use_awaitable); },
        asio::use_future);

    ioc.poll();

    EXPECT_FALSE(ioc.stopped())
        << "#289: a live co_spawn'd frame must keep its SPAWN context's work count "
           "above zero even while parked on an op that context does not drive — "
           "asio::co_spawn holds execution::outstanding_work.tracked for the frame's "
           "lifetime. If this reads stopped(), the structural argument in "
           "tests/support/pump_until_ready.hpp no longer holds and every #289 site "
           "dismissed by `an exhaustion run on the spawn context dominates it` needs "
           "re-reading.";
    EXPECT_FALSE(is_ready(fut)) << "the frame must still be parked for this arm to mean anything";

    // Teardown: release the frame rather than destroying it suspended. See
    // `kNever`'s note — `expires_after(0s)`, not `cancel()`, because a frame on a
    // stopped context has not started and so has no pending wait to cancel.
    t.expires_after(0s);
    other.run();
    ioc.run();  // no restart(): `poll()` above left the context running
    EXPECT_TRUE(is_ready(fut));
}

// ── ARM 2 — the positive control ─────────────────────────────────────────────
// The same instrument must be able to report the other answer.
TEST(SyncCoSpawnWorkGuard, CompletedFrameLetsContextExhaust) {
    asio::io_context ioc;

    auto fut = asio::co_spawn(ioc, []() -> asio::awaitable<void> { co_return; }, asio::use_future);

    ioc.poll();

    EXPECT_TRUE(ioc.stopped())
        << "POSITIVE CONTROL: with the frame complete there is no work left, so the "
           "context must exhaust. A failure here means stopped() is not measuring what "
           "the other arms assume, and their passes are worth nothing.";
    EXPECT_TRUE(is_ready(fut));
}

// ── ARM 3 — clause 2, and the one live hazard ────────────────────────────────
// A second `run()` with no `restart()` between returns immediately having
// dispatched nothing. The frame spawned after the first run is untouched, so a
// `.get()` on its future would block forever — the shape
// `run_to_exhaustion_or_report` is one edit away from, since it deliberately
// does not `restart()` on its success path.
TEST(SyncCoSpawnWorkGuard, SecondRunWithoutRestartDispatchesNothing) {
    asio::io_context ioc;
    asio::steady_timer t{ioc, kNever};

    auto first =
        asio::co_spawn(ioc, []() -> asio::awaitable<void> { co_return; }, asio::use_future);
    ioc.run();  // exhausts, and leaves `ioc` STOPPED
    ASSERT_TRUE(is_ready(first));
    ASSERT_TRUE(ioc.stopped());

    auto second = asio::co_spawn(
        ioc, [&t]() -> asio::awaitable<void> { co_await t.async_wait(asio::use_awaitable); },
        asio::use_future);

    ioc.poll();  // NO restart() — this is the defect being modelled

    EXPECT_TRUE(ioc.stopped()) << "a stopped context must stay stopped without restart()";
    EXPECT_FALSE(is_ready(second))
        << "#289 clause 2: a run() on an already-stopped context dispatches NOTHING, so "
           "the frame spawned after the previous run is untouched and a get() on its "
           "future blocks forever. `an exhaustion run dominates this get()` is only true "
           "for a run that actually ran.";

    ioc.restart();  // and this is the fix, shown rather than described
    t.expires_after(0s);
    ioc.run();
    EXPECT_TRUE(is_ready(second));
}

// ── ARM 4 — clause 1, the straddle ───────────────────────────────────────────
// One line from arm 1: the frame is spawned on `other` and `ioc` is driven. The
// guard is on the SPAWN executor, so `ioc` exhausts at once with the frame live.
// This is what makes "the same context" load-bearing rather than decorative.
TEST(SyncCoSpawnWorkGuard, GuardIsOnTheSpawnExecutorNotTheDrivenOne) {
    asio::io_context ioc;
    asio::io_context other;
    asio::steady_timer t{other, kNever};

    auto fut = asio::co_spawn(
        other, [&t]() -> asio::awaitable<void> { co_await t.async_wait(asio::use_awaitable); },
        asio::use_future);

    ioc.poll();

    EXPECT_TRUE(ioc.stopped())
        << "#289 clause 1: co_spawn's work guard is held on the SPAWN executor, so "
           "driving a DIFFERENT context to exhaustion says nothing about this frame. A "
           "site whose futures are spawned elsewhere is not dominated by the run above "
           "it, however unbounded that run is.";
    EXPECT_FALSE(is_ready(fut))
        << "the frame must still be parked, or this arm is not straddling anything";

    t.expires_after(0s);
    other.run();
    EXPECT_TRUE(is_ready(fut));
}

// ── ARM 5 — the spelling the tree actually uses ──────────────────────────────
// `tests/sync/test_cross_strand_acquire.cpp` spawns onto `asio::make_strand(ioc)`, and
// the sweep's DRIVE axis cannot resolve that name back to `ioc` — it escalates the site
// for a human to read. This arm is what that reader would otherwise have to reason
// about: a strand forwards work tracking to its underlying context, so `ioc.run()`
// exhausting still implies the frame completed.
TEST(SyncCoSpawnWorkGuard, StrandOfTheDrivenContextIsTheDrivenContext) {
    asio::io_context ioc;

    auto fut = asio::co_spawn(
        asio::make_strand(ioc), []() -> asio::awaitable<void> { co_return; }, asio::use_future);

    ioc.run();

    EXPECT_TRUE(ioc.stopped());
    EXPECT_TRUE(is_ready(fut))
        << "#289: a frame spawned on a STRAND of `ioc` holds `ioc`'s work count, so an "
           "exhausting run() on `ioc` implies it completed. If this ever fails, the "
           "sites the DRIVE axis escalates for a strand spelling need a different "
           "argument than the one recorded for batch 21.";
}

}  // namespace
