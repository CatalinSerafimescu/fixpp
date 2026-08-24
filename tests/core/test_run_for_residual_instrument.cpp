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
#include <gtest/gtest.h>

#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>

#include <chrono>
#include <cstddef>
#include <future>

#include "support/measure_run_for_residual.hpp"

namespace {

using namespace std::chrono_literals;
using fixpp::test_support::measure_run_for_residual_ready;
using fixpp::test_support::run_for_residual_measurement;

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

    const run_for_residual_measurement m = measure_run_for_residual_ready(ioc, fut, 200ms);

    EXPECT_FALSE(m.ready_at_entry);
    EXPECT_TRUE(m.ready_observed);
    EXPECT_EQ(m.residual_handlers, kFollowOnHandlers);
    // Transition handler + the kFollowOnHandlers posted handlers.
    EXPECT_EQ(m.handlers_dispatched, kFollowOnHandlers + 1);
}

// Zero cell: the future becomes ready and nothing follows it. Residual must
// be exactly zero, and readiness must still be observed (distinguishing this
// from the never-ready cell below).
TEST(RunForResidualInstrument, ZeroCellNoFollowOnWork) {
    asio::io_context ioc;
    std::promise<void> prom;
    std::future<void> fut = prom.get_future();

    asio::post(ioc, [&] { prom.set_value(); });

    const run_for_residual_measurement m = measure_run_for_residual_ready(ioc, fut, 200ms);

    EXPECT_FALSE(m.ready_at_entry);
    EXPECT_TRUE(m.ready_observed);
    EXPECT_EQ(m.residual_handlers, 0u);
    EXPECT_EQ(m.handlers_dispatched, 1u);
}

// Never-ready cell, empty-context exit: nothing is ever posted to make the
// future ready within the window, and the context has NO outstanding work,
// so the loop exits via the "ran out of work" branch (stopped_before_deadline
// == true), promptly rather than at the deadline. This proves ready_observed
// distinguishes "never became ready" from "became ready with zero residual"
// — a struct that could not tell these apart would let a dangerous site
// masquerade as a convertible one. It does NOT, by itself, cover the exit
// that matters at a real #289 site — see the deadline-exit cell below.
TEST(RunForResidualInstrument, NeverReadyCellIsDistinguishableFromZeroResidual) {
    asio::io_context ioc;
    std::promise<void> prom;
    std::future<void> fut = prom.get_future();
    // Deliberately nothing posted: the context has no work, so this returns
    // promptly once it observes it has drained (asio's stopped()
    // "ran out of work" case), well before the 50ms window would elapse.

    const run_for_residual_measurement m = measure_run_for_residual_ready(ioc, fut, 50ms);

    EXPECT_FALSE(m.ready_at_entry);
    EXPECT_FALSE(m.ready_observed);
    EXPECT_EQ(m.handlers_dispatched, 0u);
    EXPECT_TRUE(m.stopped_before_deadline);
}

// Never-ready cell, deadline exit: the context has OUTSTANDING WORK (a
// steady_timer whose expiry is well beyond the window, mirroring a
// co_spawn'd coroutine's outstanding-work guard) and the future never
// becomes ready, so run_one_until never sees the context drain and the loop
// exits only when the absolute deadline is reached. This is the #284/#289
// mechanism verbatim: a window that expires with the awaited work still
// pending. stopped_before_deadline is the field that distinguishes this
// exit from the empty-context exit above, so it gets the discriminating
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

    const run_for_residual_measurement m = measure_run_for_residual_ready(ioc, fut, kWindow);

    EXPECT_FALSE(m.ready_at_entry);
    EXPECT_FALSE(m.ready_observed);
    EXPECT_FALSE(m.stopped_before_deadline);
    EXPECT_GE(m.elapsed, kWindow);
}

}  // namespace
