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
// during the guard's own drain (the session liveness loop's `sleep_until`,
// src/session/session.cpp:4816, is the concrete production case) can arm a
// sleep the one-shot cancel has already missed.
//
// (#322) THAT GAP IS NOW EXERCISED, and the paragraph above used to end "this
// test does not exercise that gap; it is a documented residual of the guard".
// `~quiesce_on_exit` delegates to `cancel_and_drain_or_report`, whose
// alternating cancel-then-drain loop releases a sleep armed by a previous
// slice, and both directions of that are pinned below —
// CancelAndDrainOrReportWitness.OneShotCancelThenDrainCannotReleaseTheSleep
// (the gap, on the one-shot drain) and .ReleasesASleepArmedDuringItsOwnDrain
// (the release). What is still NOT proven here is a residual neither lever
// reaches: a coroutine parked on something that is not a clock sleep and not
// the guard's transport.
//
// Anchors: tests/support/pump_until_ready.hpp (quiesce_on_exit); #289 B2
// design review c-Q1..c-Q4.

#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include <array>
#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/transport/transport.hpp>
#include <memory>
#include <span>
#include <stdexcept>
#include <system_error>

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

// Minimal Transport double for ClosesTransportBeforeDraining below: async_write
// blocks on a steady_timer until close() cancels it, mirroring the "blocked
// write" shape every live-transport hang in this suite takes.
class BlockedWriteTransport final : public fixpp::transport::Transport {
public:
    explicit BlockedWriteTransport(asio::any_io_executor exec) : timer_{std::move(exec)} {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::ConnectInfo>>
    async_connect(fixpp::transport::Endpoint const&) override {
        co_return fixpp::transport::ConnectInfo{};
    }
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_read_some(
        std::span<std::byte>) override {
        co_return std::unexpected{fixpp::core::error::transport_read_eof};
    }
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_write(
        std::span<const std::byte> buf) override {
        timer_.expires_after(std::chrono::seconds{30});
        asio::error_code ec;
        co_await timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        if (closed_) {
            co_return std::unexpected{fixpp::core::error::transport_already_closed};
        }
        co_return buf.size();
    }
    [[nodiscard]] fixpp::core::expected_t<void> cancel() noexcept override { return {}; }
    [[nodiscard]] fixpp::core::expected_t<void> close() noexcept override {
        closed_ = true;
        timer_.cancel();
        return {};
    }

private:
    asio::steady_timer timer_;
    bool closed_{false};
};

}  // namespace

// Branch fires: an explicit outstanding-work guard keeps `ioc.stopped()`
// false for the whole (short, 1ms) quiesce budget, so quiesce_on_exit
// reports the residual via ADD_FAILURE. The work guard, and quiesce_on_exit
// itself, must be scoped INSIDE the macro's captured statement — declaring
// quiesce_on_exit before EXPECT_NONFATAL_FAILURE would run its destructor
// after the macro stopped intercepting failures, silently passing.
//
// (#322) THE MATCHER MOVED, AND WHERE IT MOVED TO IS THE ASSERTION. The guard no
// longer composes its own residual text; it delegates to
// `cancel_and_drain_or_report`, so the message is that drain's, ending in the
// `site` the guard passes. `"warning above. Site: quiesce_on_exit"` is chosen to
// span BOTH halves of what must hold, because `EXPECT_NONFATAL_FAILURE` matches a
// substring and either half alone is weak: the tail of the residual sentence
// discriminates the RESIDUAL report from the `#308` THROW report (which also
// carries `Site: quiesce_on_exit`), and the site discriminates THIS guard's
// delegation from a bare `cancel_and_drain_or_report` call. A matcher bound only
// to the shared `kDrainResidual` stem would pass whichever drain reported — see
// that constant's own comment in pump_until_ready.hpp.
TEST(QuiesceOnExitResidualWitness, ReportsWhenIocNeverDrains) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);

    EXPECT_NONFATAL_FAILURE(([&] {
                                auto keep_alive = asio::make_work_guard(ioc);
                                quiesce_on_exit quiesce{ioc, *clock, 1ms};
                            }()),
                            "warning above. Site: quiesce_on_exit");
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

// gate-b/r1 P1-4 (Art. VII §4): the two-argument {ioc, clock} aggregate
// init's DEFAULT budget (5s) had no falsifiable witness — every existing
// two-arg caller drains promptly regardless of what the default is, so
// changing the initializer 5s->0ms left the whole suite green. Assert the
// default directly. Costs no wall-clock: `quiesce` is never exercised past
// construction here (the io_context is never run), only its stored `budget`
// member is read, and the destructor's own drain runs against an empty,
// never-started context, which stops immediately regardless of the budget.
TEST(QuiesceOnExitResidualWitness, DefaultBudgetIsFiveSeconds) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);

    quiesce_on_exit quiesce{ioc, *clock};
    EXPECT_EQ(quiesce.budget, 5s);
}

// gate-b/r1 P1-1's mechanism, isolated: quiesce_on_exit.transport, when set,
// must be closed BEFORE the residual-work drain runs. Without it, a
// coroutine parked in async_write on a still-open transport never wakes, and
// this test's 50ms drain window would time out (ADD_FAILURE) even though
// nothing is leaked forever — it simply never quiesces within budget.
// Mutation: deleting the `quiesce.transport = &transport;` line below turns
// this RED (ADD_FAILURE fires, caught by gtest as a normal test failure —
// there is no SPI wrapper here because the passing path must be silent).
//
// (#322) The close now happens in `cancel_and_drain_or_report`, before its first
// slice and inside `pump_or_report_throw`, with the guard forwarding its
// `transport` field. This cell therefore also pins the FORWARDING: dropping the
// argument from the destructor's delegating call reds it exactly as deleting the
// assignment does.
TEST(QuiesceOnExitResidualWitness, ClosesTransportBeforeDraining) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);
    BlockedWriteTransport transport{ioc.get_executor()};

    std::array<std::byte, 1> buf{};
    asio::co_spawn(ioc, transport.async_write(std::span<const std::byte>{buf}), asio::detached);
    ioc.run_for(10ms);  // let the write actually start blocking on the timer
    ioc.restart();

    quiesce_on_exit quiesce{ioc, *clock, 50ms};
    quiesce.transport = &transport;
}

// gate-b/r1 F1b (opus_pr301_1_triage.md fix queue item 2): `drain_or_report`'s
// residual `ADD_FAILURE` branch has shipped since #289 with no witness that
// has ever seen it fire — every existing
// caller in this file's `Fixture` fixtures leaves `EngineConfig::clock` null,
// so `run_liveness_loop()` co_returns immediately and nothing is ever
// outstanding at the drain. The residual branch IS proven RED above
// (QuiesceOnExitResidualWitness.ReportsWhenIocNeverDrains). (#322) That used to
// read "`quiesce_on_exit`'s destructor body is structurally identical ... but a
// copied body is not a tested body"; the copy is gone, the destructor is one
// delegating call. The conclusion is unchanged and now rests on a different
// fact — `drain_or_report` is a separate free function with
// its own text and its own callers (Fixture::feed, Fixture::~Fixture in
// tests/session/test_next_expected_msgseqnum.cpp). Prove both directions
// directly.
TEST(DrainOrReportWitness, ReportsWhenIocNeverDrains) {
    asio::io_context ioc;
    auto keep_alive =
        asio::make_work_guard(ioc);  // keeps ioc.stopped()==false for the whole budget

    EXPECT_NONFATAL_FAILURE(fixpp::test_support::drain_or_report(ioc, "probe", 1ms),
                            "teardown drain, so a coroutine frame");
}

// Negative control: an otherwise-empty io_context drains and stops within the
// budget, so the residual branch must stay silent. Any nonfatal failure here
// (an unexpected ADD_FAILURE) fails this test outright — that IS the
// assertion, proving the instrument in the direction the flagship failure
// class (an instrument that reports clean whether or not it can fail) would
// otherwise hide.
TEST(DrainOrReportWitness, SilentWhenIocDrainsNormally) {
    asio::io_context ioc;
    fixpp::test_support::drain_or_report(ioc, "probe", 1ms);
}

// ═══════════════════════════════════════════════════════════════════════════════
// (#305) The deadline artefact, and the capability the fix introduces
//
// `io_context::run_for` is `run_until`, which loops on `run_one_until`, and
// `run_one_until` is `while (now < abs_time) { ... }  return 0;`
// (asio impl/io_context.hpp:112-131). When the deadline has ALREADY arrived at
// entry, the loop body never runs, `impl_.wait_one` is never called, and nothing
// consults `outstanding_work_` or sets `stopped_` — while the `restart()` just
// above has cleared it. `stopped()` then reads false on a context holding NO work.
//
// Both `quiesce_on_exit` and `drain_or_report` had that shape when these cells
// were written, so both are pinned here. `drain_or_report` is the newer copy
// (#301) and inherited the defect by being structurally identical; a fix landing
// on only the older one is how this repo's "fixed some sites of a claim, missed
// another" class recurs, so the two cases below are deliberately twins rather
// than one test. (#322) `quiesce_on_exit` no longer HAS that shape -- it
// delegates -- so its cell now exercises `cancel_and_drain_or_report`'s probe
// through the guard. Both cells are kept: they pin different propositions now,
// the probe itself and the delegation reaching it.
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

// A coroutine that records having been RESUMED. A free function, not an
// immediately-invoked lambda: `co_spawn(ioc, lambda(), tok)` leaves the closure
// dangling for the coroutine's whole life, and this repo has that trap on file.
asio::awaitable<void> record_resumption(bool* resumed) {
    *resumed = true;
    co_return;
}

}  // namespace

// A zero budget on an EMPTY context must be silent. Pre-fix this reported a
// residual that did not exist — the deadline had passed, so the work count was
// never consulted. Emptiness is asserted rather than assumed: without that, a
// context that quietly held work would make this pass for the wrong reason.
TEST(QuiesceOnExitResidualWitness, ZeroBudgetOnEmptyContextIsNotResidual) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);

    ASSERT_EQ(ioc.poll(), 0u) << "context is not empty at entry, so this test would no "
                                 "longer isolate the deadline artefact";
    ioc.restart();

    quiesce_on_exit quiesce{ioc, *clock, 0ms};
    // ~quiesce runs here and must add NO failure.
}

// The twin, on the copy that inherited the defect. Deleting `drain_or_report`'s
// probe reds this and not the one above. (#322) The converse no longer names
// anything deletable -- `quiesce_on_exit` has no probe of its own now -- but the
// separation survives in the form that matters: this cell is blind to
// `cancel_and_drain_or_report`, and the one above reaches it through the guard,
// so a defect in either drain still reds exactly one of them.
TEST(DrainOrReportWitness, ZeroBudgetOnEmptyContextIsNotResidual) {
    asio::io_context ioc;

    ASSERT_EQ(ioc.poll(), 0u) << "context is not empty at entry, so this test would no "
                                 "longer isolate the deadline artefact";
    ioc.restart();

    fixpp::test_support::drain_or_report(ioc, "probe", 0ms);
}

// ── The capability the fix ADDS, witnessed rather than described ──────────────
//
// This is the half an earlier draft of the comment got wrong by calling it an
// inherited obligation. At a normal budget `run_for` already resumes coroutines,
// so "a drain resumes frames" is nothing new there. But `run_for(0)` resumes
// NOTHING — it returns before entering the scheduler — whereas `poll_one()` WILL
// dispatch. So the fix newly gives the expired-deadline path the ability to
// resume a frame, in precisely the case where a caller chose a zero budget
// because it wanted nothing run.
//
// That is a real behavioural change and it deserves an observable, not a
// sentence: this asserts the coroutine actually ran during the guard's
// destructor. Pre-fix `resumed` stays false. Anyone who later removes the probe
// to "avoid dispatching during teardown" will find this test, and the two above,
// stating both directions of the trade.
TEST(QuiesceOnExitResidualWitness, ZeroBudgetProbeCanNowResumeACoroutine) {
    bool resumed = false;
    {
        asio::io_context ioc;
        auto clock = make_mock_clock(ioc);
        asio::co_spawn(ioc, record_resumption(&resumed), asio::detached);

        EXPECT_FALSE(resumed) << "nothing may have run before the guard";
        quiesce_on_exit quiesce{ioc, *clock, 0ms};
        // ~quiesce: run_for(0ms) resumes nothing, then poll_one() dispatches the
        // co_spawn's initial handler and the coroutine runs to completion — which
        // also drains the work count, so this path stays silent.
    }
    EXPECT_TRUE(resumed)
        << "the zero-budget probe did not resume the suspended coroutine. If the probe was "
           "removed, ZeroBudgetOnEmptyContextIsNotResidual should also be red; if it is not, "
           "the probe is present but no longer dispatching.";
}

// ── (gate-b/r1 F3) The bound the header states but nothing enforced ───────────
//
// `drain_or_report`'s PROBE comment in `pump_until_ready.hpp` ("The probe can
// then dispatch at most ONE handler") states that the zero/expired-budget
// probe dispatches at most one handler — `poll_one()`, not `poll()`. Nothing above pins
// that cardinality: every existing witness uses at most one outstanding handler,
// so `poll_one() -> poll()` (an unbounded drain during teardown) leaves the whole
// binary green. Two independently observable handlers, and both the dispatch
// count and the still-outstanding second handler's residual report are asserted.
//
// The EXPECT_NONFATAL_FAILURE wrapper is mandatory, not stylistic: with two
// handlers queued, poll_one() runs exactly one and leaves outstanding_work_ != 0,
// so stopped() is false and the guard's ADD_FAILURE correctly fires on CORRECT
// code. Without the wrapper this test reds on the correct code. Under the
// poll_one -> poll mutant both handlers run, the work count reaches zero, no
// report is emitted — EXPECT_NONFATAL_FAILURE fails (no failure was intercepted)
// and `ran == 2`: the cell kills the mutant on both axes.
//
// (#322) WHAT THIS CELL PINS CHANGED, and saying so is the point of keeping it.
// The guard now delegates, so the probe it exercises is the one
// CancelAndDrainOrReportWitness.ZeroBudgetProbeDispatchesAtMostOneHandler already
// covers — the `poll_one -> poll` mutant reds both. The independent proposition
// left here is the DELEGATION: that `~quiesce_on_exit` routes to the primitive
// with this guard's own budget and site. Mutate `budget` to `kQuiesceBudget` in
// the destructor's call and this reds (both handlers run within 5 s, no residual)
// while the primitive's own twin stays green.
//
// ⚠️ THIS CELL IS THE ONLY ONE THAT PINS BUDGET FORWARDING -- measured, and an
// earlier draft of this comment guessed otherwise. Under that mutant exactly ONE
// of the ten QuiesceOnExitResidualWitness cells fails: this one. The three the
// draft also named (ZeroBudgetProbeCanNowResumeACoroutine,
// ThrowingHandlerReportsOnceNotTwice, OuterCatchSwallowsAThrowingAddFailure) all
// stay GREEN, because a longer budget changes nothing for a coroutine that
// resumes anyway, a handler that throws in the first slice, or a work guard that
// is unreleasable at any budget. So deleting this cell as "redundant with the
// primitive's twin" removes the only check that the guard's own budget reaches
// the primitive at all.
TEST(QuiesceOnExitResidualWitness, ZeroBudgetProbeDispatchesAtMostOneHandler) {
    int ran = 0;
    {
        asio::io_context ioc;
        auto clock = make_mock_clock(ioc);
        asio::post(ioc, [&ran] { ++ran; });
        asio::post(ioc, [&ran] { ++ran; });
        EXPECT_NONFATAL_FAILURE(([&] { quiesce_on_exit quiesce{ioc, *clock, 0ms}; }()),
                                "warning above. Site: quiesce_on_exit");
    }
    EXPECT_EQ(ran, 1) << "the probe dispatched more than one handler; poll_one() may have "
                         "become poll(), which drains an unbounded queue during teardown";
}

// The twin, on `drain_or_report`. Same shape, same mandatory-wrapper reasoning;
// see the comment above.
TEST(DrainOrReportWitness, ZeroBudgetProbeDispatchesAtMostOneHandler) {
    int ran = 0;
    {
        asio::io_context ioc;
        asio::post(ioc, [&ran] { ++ran; });
        asio::post(ioc, [&ran] { ++ran; });
        EXPECT_NONFATAL_FAILURE(
            ([&] { fixpp::test_support::drain_or_report(ioc, "probe", 0ms); }()),
            "teardown drain, so a coroutine frame");
    }
    EXPECT_EQ(ran, 1) << "the probe dispatched more than one handler; poll_one() may have "
                         "become poll(), which drains an unbounded queue during teardown";
}

// ── (gate-b/r1 F5) drain_or_report's missing resumption twin ──────────────────
//
// ZeroBudgetProbeCanNowResumeACoroutine above proves the new zero-budget
// dispatch capability for `quiesce_on_exit`; the PR twinned the empty-context
// witness across both helpers (ZeroBudgetOnEmptyContextIsNotResidual) but not
// this one — the exact "fixed one of two identical shapes" class
// `drain_or_report`'s PROBE comment in `pump_until_ready.hpp` names.
// `drain_or_report` gained the identical capability and nothing observed it.
TEST(DrainOrReportWitness, ZeroBudgetProbeCanNowResumeACoroutine) {
    bool resumed = false;
    {
        asio::io_context ioc;
        asio::co_spawn(ioc, record_resumption(&resumed), asio::detached);

        EXPECT_FALSE(resumed) << "nothing may have run before the guard";
        fixpp::test_support::drain_or_report(ioc, "probe", 0ms);
        // run_for(0ms) resumes nothing, then poll_one() dispatches the co_spawn's
        // initial handler and the coroutine runs to completion — which also
        // drains the work count, so this call stays silent.
    }
    EXPECT_TRUE(resumed)
        << "the zero-budget probe did not resume the suspended coroutine. If the probe was "
           "removed, ZeroBudgetOnEmptyContextIsNotResidual should also be red; if it is not, "
           "the probe is present but no longer dispatching.";
}

// ── (gate-b/r2 N1) cancel_and_drain_or_report's missing cardinality twin ──────
//
// Both siblings above pin "the probe dispatches at most one handler"; this
// third copy did not, and `poll_one() -> poll()` in `cancel_and_drain_or_report`'s
// zero-budget probe left the whole binary green. Modelled on
// QuiesceOnExitResidualWitness.ZeroBudgetProbeDispatchesAtMostOneHandler
// (the QuiesceOnExitResidualWitness cell of that name above -- by name, since the
// old pin :316-328 now lands on a sibling zero-budget witness), not the
// DrainOrReportWitness copy, because this drain also needs
// a mock clock. The matcher is the substring unique to this drain's residual
// message (the `cancel_and_drain_or_report` branch of the shared residual
// report, immediately after its `kDrainResidual` stem) -- the shared stem
// alone would pass whichever drain reported (pump_until_ready.hpp,
// `kDrainResidual`'s own comment -- by symbol, since that comment has grown and
// any line range cuts off the clause about binding the `site`).
TEST(CancelAndDrainOrReportWitness, ZeroBudgetProbeDispatchesAtMostOneHandler) {
    int ran = 0;
    {
        asio::io_context ioc;
        auto clock = make_mock_clock(ioc);
        asio::post(ioc, [&ran] { ++ran; });
        asio::post(ioc, [&ran] { ++ran; });
        EXPECT_NONFATAL_FAILURE(
            ([&] { fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "probe", 0ms); }()),
            "even with clock sleeps released on every slice");
    }
    EXPECT_EQ(ran, 1) << "the probe dispatched more than one handler; poll_one() may have "
                         "become poll(), which drains an unbounded queue during teardown";
}

// ── (gate-b/r2 N2) cancel_and_drain_or_report's missing resumption twin ──────
//
// Both siblings above also carry ZeroBudgetProbeCanNowResumeACoroutine; this
// one did not. Same shape and reuse of `record_resumption`, with the 4-arg
// signature and a mock clock this drain requires.
TEST(CancelAndDrainOrReportWitness, ZeroBudgetProbeCanNowResumeACoroutine) {
    bool resumed = false;
    {
        asio::io_context ioc;
        auto clock = make_mock_clock(ioc);
        asio::co_spawn(ioc, record_resumption(&resumed), asio::detached);

        EXPECT_FALSE(resumed) << "nothing may have run before the guard";
        fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "probe", 0ms);
        // run_for(0ms) resumes nothing, then poll_one() dispatches the co_spawn's
        // initial handler and the coroutine runs to completion — which also
        // drains the work count, so this call stays silent.
    }
    EXPECT_TRUE(resumed)
        << "the zero-budget probe did not resume the suspended coroutine. If the probe was "
           "removed, ZeroBudgetOnEmptyContextIsNotResidual should also be red; if it is not, "
           "the probe is present but no longer dispatching.";
}

// ═══════════════════════════════════════════════════════════════════════════════
// (#308) A throwing handler must FAIL the test, not kill the process
//
// Pumping dispatches arbitrary ready handlers and a handler may throw.
// `~quiesce_on_exit` is implicitly `noexcept`, so pre-fix an escaping exception was
// `std::terminate`: no gtest failure, no test name, no indication of which guard
// died. `drain_or_report` and `cancel_and_drain_or_report` are not themselves
// `noexcept`, but both are called from destructor BODIES, so the exception meets an
// implicitly-noexcept frame one level up and terminates just the same.
//
// ⚠️ HOW THESE WERE PROVEN NON-VACUOUS, and it is not by a mutation these tests can
// perform on themselves. Reverting the `pump_or_report_throw` guard in
// `pump_until_ready.hpp` does not turn these RED -- it makes the BINARY DIE, taking
// every later test with it, which is precisely the undiagnosable outcome #308 is
// about. So the RED arm is a manual revert-and-run recorded in the gate record, not
// a checked-in mutant, and each of the three was forced INDIVIDUALLY: a process
// death on the first one masks the other two entirely, exactly the way an early
// return masked 8 of 15 forced arms in #316.
//
// (gate-b/r1 F2b) All three std-exception witnesses below match the FULL composed
// message -- `kDrainThrew` + that witness's own `site` + " -- what(): " + `what()`
// -- not merely the bare `what()` text. A bare-`what()` matcher cannot discriminate:
// `pump_or_report_throw` composes ONE shared stem for every drain, varying only by
// `site`, and two of the three call sites used to pass the identical literal
// `"probe"`, making `drain_or_report`'s and `cancel_and_drain_or_report`'s failure
// messages byte-identical. Fixed with BOTH halves, per this header's own
// prescription for the residual messages (pump_until_ready.hpp,
// `kDrainResidual`'s own comment -- by symbol, not a line range): distinct
// `site` strings per witness AND full-message matching -- either half alone is a
// no-op (a bare-`what()` matcher still passes with distinct sites; distinct sites
// with a bare-`what()` matcher still collide on the shared stem+what() alone since
// nothing forces the matcher to consult `site`).
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

// A handler that throws a std::exception carrying identifiable text.
void post_throwing_handler(asio::io_context& ioc) {
    asio::post(ioc, [] { throw std::runtime_error("witness-boom"); });
}

// (gate-b/r1 F2a) A handler that throws a NON-std value. `pump_or_report_throw`'s
// `catch (...)` (pump_until_ready.hpp, `pump_or_report_throw`'s non-std arm --
// cited by SYMBOL because the old line pin 190-191 drifted onto `kDrainThrew`'s
// literal, which is also about a throw and so read as correct) has been new code
// with no witness
// since this PR added it -- every checked-in throwing handler threw
// std::runtime_error, so only the `catch (const std::exception&)` arm ever ran.
// asio's scheduler rethrows a handler's exception unchanged out of
// `io_context::run_for` (it does not require std::exception), so `throw 42;`
// reaches the non-std arm exactly as a std::exception would reach the other one.
void post_non_std_throwing_handler(asio::io_context& ioc) {
    asio::post(ioc, [] { throw 42; });
}

// ⚠️ THE DRAIN MUST BE CALLED FROM A DESTRUCTOR BODY, and an earlier draft of these
// witnesses got this wrong in a way that read as a pass. `drain_or_report` and
// `cancel_and_drain_or_report` are not themselves `noexcept`, so calling either
// DIRECTLY from a test body lets the exception reach GoogleTest, which catches it
// and reports a normal test failure -- the mutant run then shows "1 FAILED TEST"
// and looks like proof, while the mechanism #308 is actually about (an
// implicitly-noexcept frame one level up) was never exercised at all.
// `~quiesce_on_exit` is the only one of the three that is a destructor itself, and
// it was the only one that died. These two guards restore the real caller shape:
// `~Fixture` at test_next_expected_msgseqnum.cpp:374 is exactly this.
template <class F>
struct CallOnDestruct {
    F f;
    ~CallOnDestruct() { f(); }
};

}  // namespace

// `~quiesce_on_exit`'s site is hardcoded "quiesce_on_exit" -- already distinct
// from the two free functions' sites below, so no parameter is introduced for it.
// (#322) It is the site argument of the destructor's `cancel_and_drain_or_report`
// call, which forwards it to `pump_or_report_throw`. This used to say the
// destructor passed it to `pump_or_report_throw` directly; that stopped being
// true when the guard began delegating, and the string is unchanged either way.
TEST(QuiesceOnExitResidualWitness, ReportsWhenAHandlerThrows) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);
    post_throwing_handler(ioc);

    EXPECT_NONFATAL_FAILURE(([&] { quiesce_on_exit quiesce{ioc, *clock, 50ms}; }()),
                            "#308: a handler threw during the teardown drain, so the drain did not "
                            "complete. Site: quiesce_on_exit -- what(): witness-boom");
}

TEST(DrainOrReportWitness, ReportsWhenAHandlerThrows) {
    asio::io_context ioc;
    post_throwing_handler(ioc);

    EXPECT_NONFATAL_FAILURE(([&] {
                                CallOnDestruct d{[&] {
                                    fixpp::test_support::drain_or_report(
                                        ioc, "drain_or_report/witness", 50ms);
                                }};
                            }()),
                            "#308: a handler threw during the teardown drain, so the drain did not "
                            "complete. Site: drain_or_report/witness -- what(): witness-boom");
}

// (gate-b/r1 F2a) The catch(...) arm, witnessed for the first time. Same
// destructor-body shape as the std-exception witness above -- see the comment
// on CallOnDestruct.
TEST(DrainOrReportWitness, ReportsWhenANonStdHandlerThrows) {
    asio::io_context ioc;
    post_non_std_throwing_handler(ioc);

    EXPECT_NONFATAL_FAILURE(
        ([&] {
            CallOnDestruct d{
                [&] { fixpp::test_support::drain_or_report(ioc, "drain_or_report/nonstd", 50ms); }};
        }()),
        "#308: a handler threw during the teardown drain, so the drain did not "
        "complete. Site: drain_or_report/nonstd -- a non-std exception, so no text "
        "is available.");
}

TEST(CancelAndDrainOrReportWitness, ReportsWhenAHandlerThrows) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);
    post_throwing_handler(ioc);

    EXPECT_NONFATAL_FAILURE(([&] {
                                CallOnDestruct d{[&] {
                                    fixpp::test_support::cancel_and_drain_or_report(
                                        ioc, *clock, "cancel_and_drain/witness", 50ms);
                                }};
                            }()),
                            "#308: a handler threw during the teardown drain, so the drain did not "
                            "complete. Site: cancel_and_drain/witness -- what(): witness-boom");
}

// The throwing path must SKIP the residual report rather than emit it as well.
// EXPECT_NONFATAL_FAILURE fails when it intercepts more than one failure, so this
// asserts the cardinality directly: pre-fix-of-this-detail the guard would report
// "a handler threw" AND "work is still outstanding", describing a consequence as if
// it were an independent finding. The context genuinely IS non-quiescent here (the
// work guard keeps it so), which is what makes the cell non-vacuous -- without the
// guard variable the residual branch would stay silent for the wrong reason.
//
// (gate-b/r1 F2c) Twinned below for `cancel_and_drain_or_report` and
// `~quiesce_on_exit` -- their `return`s after `pump_or_report_throw` were
// REACHED by the pre-existing ReportsWhenAHandlerThrows witnesses above (no work
// guard there, so the context quiesces after the throw), but nothing pinned that
// the residual report is SKIPPED rather than silent for the wrong reason. Each
// twin is proven non-vacuous by deleting its corresponding `return` and confirming
// it reports 2 failures instead of 1 -- see the gate record for the individual
// mutation runs.
TEST(DrainOrReportWitness, ThrowingHandlerReportsOnceNotTwice) {
    asio::io_context ioc;
    auto keep_alive = asio::make_work_guard(ioc);
    post_throwing_handler(ioc);

    EXPECT_NONFATAL_FAILURE(([&] {
                                CallOnDestruct d{[&] {
                                    fixpp::test_support::drain_or_report(
                                        ioc, "drain_or_report/witness", 50ms);
                                }};
                            }()),
                            "#308: a handler threw during the teardown drain, so the drain did not "
                            "complete. Site: drain_or_report/witness -- what(): witness-boom");
}

TEST(CancelAndDrainOrReportWitness, ThrowingHandlerReportsOnceNotTwice) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);
    auto keep_alive = asio::make_work_guard(ioc);
    post_throwing_handler(ioc);

    EXPECT_NONFATAL_FAILURE(([&] {
                                CallOnDestruct d{[&] {
                                    fixpp::test_support::cancel_and_drain_or_report(
                                        ioc, *clock, "cancel_and_drain/witness", 50ms);
                                }};
                            }()),
                            "#308: a handler threw during the teardown drain, so the drain did not "
                            "complete. Site: cancel_and_drain/witness -- what(): witness-boom");
}

TEST(QuiesceOnExitResidualWitness, ThrowingHandlerReportsOnceNotTwice) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);
    auto keep_alive = asio::make_work_guard(ioc);
    post_throwing_handler(ioc);

    EXPECT_NONFATAL_FAILURE(([&] { quiesce_on_exit quiesce{ioc, *clock, 50ms}; }()),
                            "#308: a handler threw during the teardown drain, so the drain did not "
                            "complete. Site: quiesce_on_exit -- what(): witness-boom");
}

// ═══════════════════════════════════════════════════════════════════════════════
// (gate-b/r1 F1) The report ITSELF is not a no-throw operation
//
// ADD_FAILURE() cannot be made non-throwing: under --gtest_throw_on_failure,
// gtest's AddTestPartResult records the failure and THEN throws
// GoogleTestFailureException -- downstream of the record, so
// EXPECT_NONFATAL_FAILURE's ScopedFakeTestPartResultReporter does not absorb it.
// Pre-fix that escaped every one of the three teardown drains' implicitly-noexcept
// frames and reached std::terminate; each drain now wraps its whole body in one
// outer `catch (...)`, matching this header's own argument that a single catch is
// correct HERE (see `pump_or_report_throw`'s comment: "this helper has no release
// branch to lose"). The no-throw contract is unconditional BY DESIGN -- including
// when a free drain is called directly from a test body, where propagation would
// otherwise be legal -- because the exception policy for every drain in this
// header is decided ONCE, not per call site.
//
// Pinned IN-PROCESS with a scoped throw_on_failure flag, not a subprocess: the
// residual path's own ADD_FAILURE() is enough to drive the throw, and driving it
// is exactly what exercises the swallow. Modelled on
// test_test_request_id_cross_session_race.cpp's
// CrossSessionTeardown.OuterCatchSwallowsAThrowingAddFailure (:1925-2020).
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

// RAII-scoped `GTEST_FLAG_SET(throw_on_failure, true)`. Copied verbatim from
// test_test_request_id_cross_session_race.cpp:1919-1924 -- restoring the previous
// value is mandatory, or every later EXPECT_* in this test would throw instead
// of merely failing. gtest constructs a flag saver per `Test` object, so a flag
// set inside a test body cannot leak past that test; the restore below is
// required within the test, not across the binary. (The pre-existing copy at
// test_test_request_id_cross_session_race.cpp:1919-1924 carries the same
// overstated "in this binary" wording -- out of scope here, left as-is.)
struct throw_on_failure_scope {
    bool previous = GTEST_FLAG_GET(throw_on_failure);
    throw_on_failure_scope() { GTEST_FLAG_SET(throw_on_failure, true); }
    ~throw_on_failure_scope() { GTEST_FLAG_SET(throw_on_failure, previous); }
};

}  // namespace

// `throw_scope` is declared FIRST in each witness below, so it is destroyed
// LAST -- the flag must still be true when the drain's own ADD_FAILURE() runs,
// or the swallow this witness exists to pin is never exercised. If the fix
// regresses (the outer catch removed from pump_until_ready.hpp), this test does
// not merely go RED: the process TERMINATES, taking the rest of the binary with
// it -- exactly like the #308 throw witnesses above, which is why this is proven
// by running under the flag rather than by a checked-in mutant.
TEST(QuiesceOnExitResidualWitness, OuterCatchSwallowsAThrowingAddFailure) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);

    EXPECT_NONFATAL_FAILURE(([&] {
                                throw_on_failure_scope throw_scope;
                                auto keep_alive = asio::make_work_guard(ioc);
                                quiesce_on_exit quiesce{ioc, *clock, 1ms};
                                // ~quiesce runs here, throw_on_failure still true: its residual
                                // ADD_FAILURE() records the failure then throws
                                // GoogleTestFailureException, caught by the outer catch(...)
                                // in `cancel_and_drain_or_report`, which since #322 IS
                                // ~quiesce_on_exit's body. The destructor adds no handler of
                                // its own; that the delegated one covers it is what this
                                // pins, since a throw escaping here terminates the process.
                            }()),
                            "warning above. Site: quiesce_on_exit");

    SUCCEED() << "control reached past ~quiesce_on_exit without std::terminate, "
                 "so the outer catch(...) swallowed the throwing ADD_FAILURE";
}

TEST(DrainOrReportWitness, OuterCatchSwallowsAThrowingAddFailure) {
    asio::io_context ioc;
    auto keep_alive = asio::make_work_guard(ioc);

    EXPECT_NONFATAL_FAILURE(([&] {
                                throw_on_failure_scope throw_scope;
                                fixpp::test_support::drain_or_report(ioc, "probe", 1ms);
                            }()),
                            "teardown drain, so a coroutine frame");

    SUCCEED() << "control reached past drain_or_report without std::terminate, so "
                 "the outer catch(...) swallowed the throwing ADD_FAILURE";
}

TEST(CancelAndDrainOrReportWitness, OuterCatchSwallowsAThrowingAddFailure) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);
    auto keep_alive = asio::make_work_guard(ioc);

    EXPECT_NONFATAL_FAILURE(([&] {
                                throw_on_failure_scope throw_scope;
                                fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                                                "probe", 50ms);
                            }()),
                            "even with clock sleeps released on every slice");

    SUCCEED() << "control reached past cancel_and_drain_or_report without "
                 "std::terminate, so the outer catch(...) swallowed the throwing "
                 "ADD_FAILURE";
}

// ═══════════════════════════════════════════════════════════════════════════════
// (#289 tail) cancel_and_drain_or_report — the sleep armed during the drain itself
//
// The defect is one of ORDER, not of power. A single `cancel_sleeps()` DOES
// terminate a sleeping liveness loop: it completes the waiter with
// operation_aborted, `mock_clock::sleep_until`'s `void(std::error_code)` initiation
// under `use_awaitable` throws `std::system_error`, and `run_liveness_loop`'s catch
// sits OUTSIDE its `while (fsm_state_ == Active)` loop, so the throw crosses the
// loop boundary into a clean `co_return`. What the one-shot pair misses is only a
// sleep armed AFTER it ran -- and a miss-branch drain that itself completes a state
// transition arms exactly that.
//
// `arm_sleep_when_first_resumed` reproduces that shape with no Session at all:
// `co_spawn` POSTS the initial resumption, so the sleep is armed by the DRAIN,
// never before it. The catch mirrors `run_liveness_loop`'s conversion of the
// cancellation throw into a clean return.
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

asio::awaitable<void> arm_sleep_when_first_resumed(fixpp::core::Clock* clock) {
    try {
        // Far enough out that mock_clock parks rather than firing immediately
        // (it completes inline when deadline <= its current steady time).
        co_await clock->sleep_until(fixpp::core::steady_time_point{} + 1h);
    } catch (const std::system_error&) {
        // cancel_sleeps() aborts the wait. Mirrors run_liveness_loop's conversion.
    }
    co_return;
}

}  // namespace

// RED ARM — pins the defect on the shape #313/#316 shipped at ~40 sites. The
// one-shot cancel runs while there is nothing to cancel; the drain then resumes the
// coroutine, which arms a sleep nothing will ever release. The context cannot
// quiesce and the caller is handed a residual report it has no lever to clear.
//
// This test PASSES on correct code — the failure it intercepts is the defect being
// present, and it is deliberately kept so that a future change which "fixes"
// drain_or_report instead would show up here rather than silently making the new
// primitive redundant.
TEST(CancelAndDrainOrReportWitness, OneShotCancelThenDrainCannotReleaseTheSleep) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);
    asio::co_spawn(ioc, arm_sleep_when_first_resumed(clock.get()), asio::detached);

    clock->cancel_sleeps();  // one-shot: nothing is registered yet
    EXPECT_NONFATAL_FAILURE(fixpp::test_support::drain_or_report(ioc, "probe", 100ms),
                            "teardown drain, so a coroutine frame");

    // Leave nothing suspended for the fixture teardown to trip over.
    clock->cancel_sleeps();
    ioc.restart();
    ioc.run_for(50ms);
}

// GREEN ARM — the same shape, released. Alternating the cancel with the drain means
// the sleep armed by slice N is cancelled by slice N+1, the coroutine resumes,
// unwinds, and the context runs out of work. Any nonfatal failure here fails this
// test outright, which IS the assertion.
//
// The two arms differ in exactly one call, so the cell isolates the primitive
// rather than the scenario.
TEST(CancelAndDrainOrReportWitness, ReleasesASleepArmedDuringItsOwnDrain) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);
    asio::co_spawn(ioc, arm_sleep_when_first_resumed(clock.get()), asio::detached);

    fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "probe", 5s);
    EXPECT_TRUE(ioc.stopped()) << "the drain reported success but left work outstanding";
}

// The THIRD copy of the #305 zero-budget artefact, and the twin the other two
// helpers already carry (QuiesceOnExitResidualWitness / DrainOrReportWitness
// .ZeroBudgetOnEmptyContextIsNotResidual, above).
//
// This is the cell whose ABSENCE let the defect in. Written the obvious way — test
// the deadline at the top of the loop and `break` — this helper exits before
// `poll_one()` has ever run, so the `restart()` leaves `stopped()` false on a
// context holding NO work and it reports a residual that does not exist. Both
// sibling helpers were fixed for exactly that in #305; the third copy reintroduced
// it, and nothing caught it because the third copy had no zero-budget witness.
// Emptiness is asserted rather than assumed: without that, a context quietly
// holding work would make this pass for the wrong reason.
TEST(CancelAndDrainOrReportWitness, ZeroBudgetOnEmptyContextIsNotResidual) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);

    ASSERT_EQ(ioc.poll(), 0u) << "context is not empty at entry, so this test would no "
                                 "longer isolate the deadline artefact";
    ioc.restart();

    fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "probe", 0ms);
}

// Negative control: an otherwise-empty io_context must drain and stay silent.
// Without this, a primitive that never reports anything would pass the arm above.
TEST(CancelAndDrainOrReportWitness, SilentWhenIocDrainsNormally) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);

    fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "probe", 50ms);
}

// The residual branch must still FIRE for work no clock lever can release — the
// primitive gained a second lever, not immunity — AND it must fire AT MOST ONCE
// across the whole budget rather than once per slice.
//
// Both axes live in this one cell deliberately. An earlier draft had a separate
// `ReportsAtMostOnceAcrossManySlices` passing `1ms` explicitly, which is
// `kPumpSlice`'s own value (pump_until_ready.hpp) — so it was a byte-for-byte
// duplicate of this test that could never go red independently. The cardinality
// assertion is already here for free: `EXPECT_NONFATAL_FAILURE` fails when it
// intercepts MORE than one failure, and at a 50ms budget with the 1ms default
// slice a per-slice mutant emits ~47, so the margin is wide rather than a
// boundary. Measured under exactly that mutant: "Expected: 1 non-fatal failure.
// Actual: 47 failures."
//
// An explicit work guard is
// unreleasable by construction, which is the point: cancelling sleeps forever
// cannot clear it, so this pins that the helper reports rather than spins to the
// budget silently.
TEST(CancelAndDrainOrReportWitness, ReportsWhenIocNeverDrains) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);
    auto keep_alive = asio::make_work_guard(ioc);

    EXPECT_NONFATAL_FAILURE(
        fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "probe", 50ms),
        "even with clock sleeps released on every slice");
}
