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
TEST(QuiesceOnExitResidualWitness, ReportsWhenIocNeverDrains) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);

    EXPECT_NONFATAL_FAILURE(([&] {
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
// residual ADD_FAILURE branch (pump_until_ready.hpp:296) has shipped
// since #289 with no witness that has ever seen it fire — every existing
// caller in this file's `Fixture` fixtures leaves `EngineConfig::clock` null,
// so `run_liveness_loop()` co_returns immediately and nothing is ever
// outstanding at the drain. `quiesce_on_exit`'s destructor body is
// structurally identical and IS proven RED above
// (QuiesceOnExitResidualWitness.ReportsWhenIocNeverDrains), but a copied body
// is not a tested body — `drain_or_report` is a separate free function with
// its own text and its own callers (Fixture::feed, Fixture::~Fixture in
// tests/session/test_next_expected_msgseqnum.cpp). Prove both directions
// directly.
TEST(DrainOrReportWitness, ReportsWhenIocNeverDrains) {
    asio::io_context ioc;
    auto keep_alive =
        asio::make_work_guard(ioc);  // keeps ioc.stopped()==false for the whole budget

    EXPECT_NONFATAL_FAILURE(fixpp::test_support::drain_or_report(ioc, "probe", 1ms),
                            "did not run out of work within the teardown drain");
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
// Both `quiesce_on_exit` and `drain_or_report` had that shape, so both are pinned
// here. `drain_or_report` is the newer copy (#301) and inherited the defect by
// being structurally identical; a fix landing on only the older one is how this
// repo's "fixed some sites of a claim, missed another" class recurs, so the two
// cases below are deliberately twins rather than one test.
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
// probe reds this and not the one above; deleting `quiesce_on_exit`'s reds the
// one above and not this. That separation is the point.
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
// pump_until_ready.hpp:238-240 ("The probe can then dispatch at most ONE
// handler") states that the zero/expired-budget probe dispatches at most one
// handler — `poll_one()`, not `poll()`. Nothing above pins
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
TEST(QuiesceOnExitResidualWitness, ZeroBudgetProbeDispatchesAtMostOneHandler) {
    int ran = 0;
    {
        asio::io_context ioc;
        auto clock = make_mock_clock(ioc);
        asio::post(ioc, [&ran] { ++ran; });
        asio::post(ioc, [&ran] { ++ran; });
        EXPECT_NONFATAL_FAILURE(([&] { quiesce_on_exit quiesce{ioc, *clock, 0ms}; }()),
                                "quiesce_on_exit: the io_context did not run out of work");
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
            "did not run out of work within the teardown drain");
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
// pump_until_ready.hpp:231-233 names. `drain_or_report` gained the identical
// capability and nothing observed it.
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
