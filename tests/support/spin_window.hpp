// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/spin_window.hpp
//
// Occupy a precise SHORT window — and report what it actually cost.
//
// NOT `sleep_for`. A sleep shorter than the system timer granularity does not
// sleep for the requested duration: it sleeps for the granularity. Two distinct
// consequences follow, and a site usually cares about one of them:
//
//   - a LOOP of N such sleeps costs N x granularity rather than N x the window,
//     so work serialized behind it can miss a budget while the code under test
//     is behaving correctly (PR #326);
//   - N sleeps of DIFFERENT sub-granularity lengths all wake at the same tick
//     boundary, so windows that exist to separate events stop separating them
//     (issue #327).
//
// Both are silent: the sleep returns success, and the test still passes — just
// weaker, or slower, than it reads.
//
// A spin costs the requested duration on every platform. It also BURNS A CORE
// for that duration, so this is for SHORT windows only. A site that needs to
// yield the CPU across a long deadline must keep sleeping and accept the
// granularity; spinning one of those would starve the very thread it waits for.
//
// The delivered duration is returned so a caller that must PROVE its window was
// honoured can assert on it rather than assume it. Callers that only need the
// delay may ignore it.
//
// Re-derive rather than trust a number: compile a loop of
// `sleep_for(microseconds{5})` and divide the elapsed time by the iteration
// count, on the platform in question.
#pragma once

#include <chrono>

namespace fixpp::test_support {

inline std::chrono::steady_clock::duration spin_for(std::chrono::steady_clock::duration d) {
    const auto started = std::chrono::steady_clock::now();
    const auto until = started + d;
    // The sample that ends the spin IS the measurement: reading the clock again
    // afterwards would add an unbounded preemption gap to the reported figure.
    auto now = started;
    while ((now = std::chrono::steady_clock::now()) < until) {
        // spin
    }
    return now - started;
}

}  // namespace fixpp::test_support
