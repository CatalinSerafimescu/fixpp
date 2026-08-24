// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/core/test_run_for_residual_instrument.cpp — issue #289, batch B0
//
// Self-test for tests/support/measure_run_for_residual.hpp — the instrument
// #289's per-site migration decision is built on. Per this repo's standing
// rule, a verification instrument that has never been proven able to return
// non-zero is a false-green generator, not a clean result: this file proves
// the instrument can distinguish all three outcomes it must distinguish
// (residual > 0, residual == 0, never-ready) using bare asio primitives, not
// production scheduling. Exact residual counts are asserted ONLY in the
// synthetic positive cell; the review this batch implements is explicit that
// production-derived measurements should assert zero-vs-non-zero, never an
// exact count.
//
// gate-b/r1: also proves the ready_at_entry==true branch, the location/phase
// attribution parameters, the near_deadline_inconclusive fail-closed flag,
// the exclusive_driver refusal, and that a pre-readiness handler is excluded
// from the residual count (i.e. the transition is NOT "whichever handler
// dispatches first").
#include <gtest/gtest.h>

#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>

#include <chrono>
#include <cstddef>
#include <future>
#include <source_location>
#include <string>

#include "measure_run_for_residual.hpp"

namespace {

using namespace std::chrono_literals;
using fixpp::test_support::measure_run_for_residual_ready;
using fixpp::test_support::run_for_residual_measurement;

// Stand-in for a #289 fixture helper that itself forwards the caller's
// location via a defaulted std::source_location parameter, exactly the
// pattern measure_run_for_residual.hpp documents as the reason `location`
// defaults the way it does. Used by LocationAttributesToHelperCallerNotHelper
// below to prove attribution lands on the TEST's call site, not on the line
// inside this helper that forwards it.
run_for_residual_measurement measure_via_fixture_helper(
    asio::io_context& ioc, std::future<void>& fut, std::chrono::steady_clock::duration window,
    std::source_location location = std::source_location::current()) {
    return measure_run_for_residual_ready(ioc, fut, window, /*exclusive_driver=*/true, {}, location);
}

// Positive/RED cell: one handler makes a promise ready and posts exactly N
// further handlers that run before the window closes. Residual must be
// EXACTLY N — this is the one cell allowed an exact-count oracle, because it
// is constructed synthetically rather than derived from production
// scheduling.
TEST(RunForResidualInstrument, PositiveCellCountsExactResidual) {
    constexpr std::size_t kFollowOnHandlers = 3;

    asio::io_context ioc;
    std::promise<void> prom;
    std::future<void> fut = prom.get_future();

    asio::post(ioc, [&] {
        prom.set_value();
        for (std::size_t i = 0; i < kFollowOnHandlers; ++i) {
            asio::post(ioc, [] {});
        }
    });

    const run_for_residual_measurement m =
        measure_run_for_residual_ready(ioc, fut, 200ms, /*exclusive_driver=*/true);

    EXPECT_FALSE(m.ready_at_entry);
    EXPECT_TRUE(m.ready_observed);
    EXPECT_EQ(m.residual_handlers, kFollowOnHandlers);
    // Transition handler + the kFollowOnHandlers posted handlers.
    EXPECT_EQ(m.handlers_dispatched, kFollowOnHandlers + 1);
    EXPECT_FALSE(m.near_deadline_inconclusive);
    EXPECT_FALSE(m.skipped_non_exclusive_driver);
}

// Zero cell: the future becomes ready and nothing follows it. Residual must
// be exactly zero, and readiness must still be observed (distinguishing this
// from the never-ready cell below).
TEST(RunForResidualInstrument, ZeroCellNoFollowOnWork) {
    asio::io_context ioc;
    std::promise<void> prom;
    std::future<void> fut = prom.get_future();

    asio::post(ioc, [&] { prom.set_value(); });

    const run_for_residual_measurement m =
        measure_run_for_residual_ready(ioc, fut, 200ms, /*exclusive_driver=*/true);

    EXPECT_FALSE(m.ready_at_entry);
    EXPECT_TRUE(m.ready_observed);
    EXPECT_EQ(m.residual_handlers, 0u);
    EXPECT_EQ(m.handlers_dispatched, 1u);
    // The transition happens on the very first dispatched handler, far
    // (~200ms) from the deadline: not inconclusive.
    EXPECT_FALSE(m.near_deadline_inconclusive);
}

// Never-ready cell, empty-context exit: nothing is ever posted to make the
// future ready within the window, and the context has NO outstanding work,
// so the loop exits via the "ran out of work" branch
// (stopped_due_to_exhausted_work == true), promptly rather than at the
// deadline. This proves ready_observed distinguishes "never became ready"
// from "became ready with zero residual" — a struct that could not tell
// these apart would let a dangerous site masquerade as a convertible one. It
// does NOT, by itself, cover the exit that matters at a real #289 site — see
// the deadline-exit cell below.
TEST(RunForResidualInstrument, NeverReadyCellIsDistinguishableFromZeroResidual) {
    asio::io_context ioc;
    std::promise<void> prom;
    std::future<void> fut = prom.get_future();
    // Deliberately nothing posted: the context has no work, so this returns
    // promptly once it observes it has drained (asio's stopped()
    // "ran out of work" case), well before the 50ms window would elapse.

    const run_for_residual_measurement m =
        measure_run_for_residual_ready(ioc, fut, 50ms, /*exclusive_driver=*/true);

    EXPECT_FALSE(m.ready_at_entry);
    EXPECT_FALSE(m.ready_observed);
    EXPECT_EQ(m.handlers_dispatched, 0u);
    EXPECT_TRUE(m.stopped_due_to_exhausted_work);
}

// Never-ready cell, deadline exit: the context has OUTSTANDING WORK (a
// steady_timer whose expiry is well beyond the window, mirroring a
// co_spawn'd coroutine's outstanding-work guard) and the future never
// becomes ready, so run_one_until never sees the context drain and the loop
// exits only when the absolute deadline is reached. This is the #284/#289
// mechanism verbatim: a window that expires with the awaited work still
// pending. stopped_due_to_exhausted_work is the field that distinguishes
// this exit from the empty-context exit above, so it gets the discriminating
// assertion here.
TEST(RunForResidualInstrument, NeverReadyCellExitsOnDeadlineWithOutstandingWork) {
    constexpr auto kWindow = 30ms;

    asio::io_context ioc;
    std::promise<void> prom;
    std::future<void> fut = prom.get_future();

    // Expiry far beyond kWindow: this timer never fires within the
    // measurement, but its pending async_wait keeps the context non-empty,
    // so run_one_until blocks on it rather than returning early.
    asio::steady_timer far_future_timer(ioc, 10 * kWindow);
    far_future_timer.async_wait([](const std::error_code&) {});

    const run_for_residual_measurement m =
        measure_run_for_residual_ready(ioc, fut, kWindow, /*exclusive_driver=*/true);

    EXPECT_FALSE(m.ready_at_entry);
    EXPECT_FALSE(m.ready_observed);
    EXPECT_FALSE(m.stopped_due_to_exhausted_work);
    EXPECT_GE(m.elapsed, kWindow);
}

// gate-b/r1 P1-b: the ready_at_entry==true branch is otherwise untested.
// Satisfy the promise BEFORE measuring, then queue N handlers: every one of
// them dispatches with readiness already observed, so all N must count as
// residual — this exercises the `observed_before_step` seed
// (`bool observed = result.ready_at_entry;`), not just its steady-state
// update inside the loop.
TEST(RunForResidualInstrument, ReadyAtEntryMakesEveryDispatchedHandlerResidual) {
    constexpr std::size_t kHandlers = 3;

    asio::io_context ioc;
    std::promise<void> prom;
    std::future<void> fut = prom.get_future();
    prom.set_value();

    for (std::size_t i = 0; i < kHandlers; ++i) {
        asio::post(ioc, [] {});
    }

    const run_for_residual_measurement m =
        measure_run_for_residual_ready(ioc, fut, 200ms, /*exclusive_driver=*/true);

    EXPECT_TRUE(m.ready_at_entry);
    EXPECT_TRUE(m.ready_observed);
    EXPECT_EQ(m.handlers_dispatched, kHandlers);
    EXPECT_EQ(m.residual_handlers, kHandlers);
}

// gate-b/r1 P1-b: `phase` is assigned but was never asserted anywhere.
TEST(RunForResidualInstrument, PhaseLabelSurvives) {
    asio::io_context ioc;
    std::promise<void> prom;
    std::future<void> fut = prom.get_future();
    prom.set_value();

    const run_for_residual_measurement m = measure_run_for_residual_ready(
        ioc, fut, 50ms, /*exclusive_driver=*/true, "Fixture::feed/inbound");

    EXPECT_EQ(m.phase, "Fixture::feed/inbound");
}

// gate-b/r1 P1-b: `location` defaults to std::source_location::current(),
// which attributes to the IMMEDIATE caller. Routed through
// measure_via_fixture_helper (which forwards its own defaulted location
// rather than letting the inner call re-default), the reported location must
// be THIS test's call site, not the line inside the helper that performs the
// forwarding call — proving the attribution the header's docs claim is the
// entire point of the parameter.
TEST(RunForResidualInstrument, LocationAttributesToHelperCallerNotHelper) {
    asio::io_context ioc;
    std::promise<void> prom;
    std::future<void> fut = prom.get_future();
    prom.set_value();

    const std::source_location before = std::source_location::current();
    const run_for_residual_measurement m = measure_via_fixture_helper(ioc, fut, 50ms);
    const std::source_location after = std::source_location::current();

    EXPECT_STREQ(m.location.file_name(), before.file_name());
    // The call above sits on the single line between `before` and `after`.
    // Attribution to the caller means m.location.line() falls strictly
    // between the two; measure_via_fixture_helper is defined much earlier in
    // this file, so a location attributed to the HELPER (the bug this cell
    // exists to catch) would fail both bounds.
    EXPECT_GT(m.location.line(), before.line());
    EXPECT_LT(m.location.line(), after.line());
}

// gate-b/r1 P2-d: every ready-producing cell above makes the FIRST
// dispatched handler the transition, so an implementation that (wrongly)
// treats "the first dispatched handler" as the transition would pass all of
// them. Insert a handler ahead of the transition that leaves the future
// UNREADY: only the N handlers strictly after the real transition may count
// as residual — N, not N+1.
TEST(RunForResidualInstrument, PreReadinessHandlerIsExcludedFromResidual) {
    constexpr std::size_t kFollowOnHandlers = 3;

    asio::io_context ioc;
    std::promise<void> prom;
    std::future<void> fut = prom.get_future();

    asio::post(ioc, [] {});  // dispatches first; must NOT be treated as the transition
    asio::post(ioc, [&] {
        prom.set_value();
        for (std::size_t i = 0; i < kFollowOnHandlers; ++i) {
            asio::post(ioc, [] {});
        }
    });

    const run_for_residual_measurement m =
        measure_run_for_residual_ready(ioc, fut, 200ms, /*exclusive_driver=*/true);

    EXPECT_FALSE(m.ready_at_entry);
    EXPECT_TRUE(m.ready_observed);
    // pre-readiness handler + transition handler + kFollowOnHandlers.
    EXPECT_EQ(m.handlers_dispatched, kFollowOnHandlers + 2);
    EXPECT_EQ(m.residual_handlers, kFollowOnHandlers);
}

// gate-b/r1 P2-e: a readiness transition observed within 5% of `window` of
// the deadline is flagged inconclusive — the `ready()` probe between
// dispatches is not free, so a transition this close to the deadline can
// consume the remaining window and under-report a genuine residual as zero.
TEST(RunForResidualInstrument, NearDeadlineTransitionIsFlaggedInconclusive) {
    constexpr auto kWindow = 500ms;
    constexpr auto kFireAt = 490ms;  // within the 25ms (5%) margin of kWindow's deadline

    asio::io_context ioc;
    std::promise<void> prom;
    std::future<void> fut = prom.get_future();

    asio::steady_timer near_deadline_timer(ioc, kFireAt);
    near_deadline_timer.async_wait([&](const std::error_code&) { prom.set_value(); });

    const run_for_residual_measurement m =
        measure_run_for_residual_ready(ioc, fut, kWindow, /*exclusive_driver=*/true);

    EXPECT_TRUE(m.ready_observed);
    EXPECT_TRUE(m.near_deadline_inconclusive);
}

// gate-b/r1 P2-f: `exclusive_driver` has no default, and passing `false`
// must refuse to run the measurement at all, even though the future is
// already ready — proving the sound-domain violation cannot silently
// produce the same {ready_observed=true, residual_handlers=0} shape that
// means "convertible".
TEST(RunForResidualInstrument, NonExclusiveDriverSkipsMeasurement) {
    asio::io_context ioc;
    std::promise<void> prom;
    std::future<void> fut = prom.get_future();
    prom.set_value();

    const run_for_residual_measurement m =
        measure_run_for_residual_ready(ioc, fut, 50ms, /*exclusive_driver=*/false);

    EXPECT_TRUE(m.skipped_non_exclusive_driver);
    EXPECT_FALSE(m.ready_observed);
    EXPECT_FALSE(m.ready_at_entry);
    EXPECT_EQ(m.handlers_dispatched, 0u);
}

}  // namespace
