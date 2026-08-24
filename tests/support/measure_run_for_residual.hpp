// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/measure_run_for_residual.hpp
//
// Measurement instrument for issue #289's per-site migration decision:
// decide each `ioc.run_for(W); ioc.restart(); fut.get();` site by counting
// how many handlers dispatch AFTER the awaited operation became ready but
// BEFORE the original window W would have closed. Zero residual is evidence
// that the window is a pure wait and the site is safe to convert to a bounded
// pump (see pump_until_ready.hpp); non-zero means the window is doing double
// duty (draining other work) and must be preserved.
//
// Observationally reproduces one:
//
//     ioc.run_for(window);
//     ioc.restart();
//
// while recording how many handlers begin after `ready()` has first been
// observed true.
//
// The implementation uses one steady-clock absolute deadline and repeatedly
// calls ioc.run_one_until(deadline), exactly matching this pinned Asio
// implementation's run_for/run_until dispatch loop (asio/impl/io_context.hpp:
// run_for -> run_until -> `while (run_one_until(abs_time)) ++n;`). It does
// NOT slice the deadline into smaller relative windows, add a work guard, or
// wait after run_one_until returns zero — each would change the original
// run_for semantics (a work guard in particular would defeat the original
// early-stop-on-drained-work behaviour; see pump_until_ready.hpp's own
// rejection of that shape for the same reason). It restarts `ioc` once on
// exit, matching the trailing `ioc.restart()` at every real call site.
//
// `ready_observed == false` is NOT a zero-residual result: the original
// window ended (or the context ran out of work) before the operation became
// ready, so this measurement provides no evidence for conversion — that site
// is dangerous, not convertible. Callers must inspect `ready_observed` before
// trusting `residual_handlers`.
//
// The readiness-transition handler itself is not residual. A handler is
// residual only when readiness was ALREADY observed true before that handler
// was dispatched — this matches the substitutability question: would the
// awaited get() have unblocked before that handler ran anyway?
//
// Preconditions for an actionable result (undocumented violation produces a
// FALSE ZERO, not a loud failure):
//   * no other thread is running or polling `ioc` concurrently with this call;
//   * the observed operation (the `ready` predicate / future) completes
//     through THIS `ioc`, not through another thread or executor;
//   * downstream behaviour does not depend on elapsed real time or on work
//     serviced by another executor while this window is open.
// Issue #289 records at least 28 sites that are pool-serviced or otherwise
// externally driven; a zero from one of those is meaningless — this function
// counts only handlers dispatched by ITS OWN drive of `ioc`, and cannot see
// completions serviced elsewhere.
//
// Exact residual counts are empirical evidence for one run, not a proof over
// all possible schedules — confirm on the relevant test configurations/
// sanitizer lanes before relying on a zero. Only a synthetic, bare-asio
// positive cell (see the self-test) should assert an exact residual count;
// production-scheduling-derived measurements should assert zero-vs-non-zero.
//
// `location`/`phase` support per-call-site attribution (#289's biggest sites
// put the idiom inside a fixture helper called from many places with
// different downstream state — a helper-DEFINITION-level aggregate residual
// can hide "one call site non-zero, the rest zero" and gives no safe
// per-invocation migration decision). `location` defaults to the CALLER via
// std::source_location::current() as a default argument, so a call routed
// through a fixture helper that itself forwards the default still reports
// its own caller, not the helper. `phase` is an optional caller-supplied
// label (e.g. "Fixture::feed/inbound") disambiguating multiple measurements
// that share one source line, such as a loop or a parameterised test.

#pragma once

#include <asio/io_context.hpp>

#include <chrono>
#include <cstddef>
#include <future>
#include <source_location>
#include <string>
#include <string_view>

namespace fixpp::test_support {

struct run_for_residual_measurement {
    bool ready_at_entry{};
    bool ready_observed{};
    bool stopped_before_deadline{};
    std::size_t handlers_dispatched{};
    std::size_t residual_handlers{};
    std::chrono::steady_clock::duration elapsed{};
    std::source_location location{};
    std::string phase{};
};

// Predicate form. `ready` is polled after every dispatched handler, exactly
// where the original code would next check the awaited condition.
template <class Ready>
[[nodiscard]] run_for_residual_measurement measure_run_for_residual(
    asio::io_context& ioc, Ready&& ready, std::chrono::steady_clock::duration window,
    std::string_view phase = {},
    std::source_location location = std::source_location::current()) {
    run_for_residual_measurement result;
    result.location = location;
    result.phase = std::string{phase};

    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + window;

    result.ready_at_entry = static_cast<bool>(ready());
    bool observed = result.ready_at_entry;

    for (;;) {
        const bool observed_before_step = observed;
        const std::size_t s = ioc.run_one_until(deadline);
        if (s == 0) {
            result.stopped_before_deadline = std::chrono::steady_clock::now() < deadline;
            break;
        }
        ++result.handlers_dispatched;
        if (observed_before_step) {
            ++result.residual_handlers;
        }
        if (!observed && ready()) {
            observed = true;
        }
    }

    result.ready_observed = observed;
    result.elapsed = std::chrono::steady_clock::now() - start;
    ioc.restart();
    return result;
}

// Future-shaped wrapper. The predicate is `fut.wait_for(0s) ==
// std::future_status::ready`, matching pump_until_ready.hpp's
// `pump_until_ready`.
template <class Fut>
[[nodiscard]] run_for_residual_measurement measure_run_for_residual_ready(
    asio::io_context& ioc, Fut& fut, std::chrono::steady_clock::duration window,
    std::string_view phase = {},
    std::source_location location = std::source_location::current()) {
    return measure_run_for_residual(
        ioc, [&fut] { return fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready; },
        window, phase, location);
}

}  // namespace fixpp::test_support
