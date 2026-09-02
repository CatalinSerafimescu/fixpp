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
// Neither announces itself: nothing in the API reports that the wait was
// rounded up. The oversleep surfaces later and elsewhere — as a weakened
// interleaving, or as an enclosing budget exhausted by work that is behaving
// correctly — with the timer never named as the cause.
//
// A spin waits on the requested clock rather than on the timer, so it is not
// rounded up to a tick. It bounds the wait from BELOW, not above: preemption
// or a coarse clock can still make the delivered window longer than asked. It
// also BURNS A CORE for the duration, so this is for SHORT windows only. A site
// that needs to yield the CPU across a long deadline must keep sleeping and
// accept the granularity; spinning one of those would starve the very thread it
// is waiting for.
//
// The delivered duration is returned so a caller that must PROVE its window was
// honoured can assert on it rather than assume it. Callers that only need the
// delay may ignore it. A non-positive `d` returns zero without sampling.
//
// Re-derive rather than trust a number: compile a loop of
// `sleep_for(microseconds{5})` and divide the elapsed time by the iteration
// count, on the platform in question.
#pragma once

#include <chrono>

namespace fixpp::test_support {

inline std::chrono::steady_clock::duration spin_for(std::chrono::steady_clock::duration d) {
    if (d <= std::chrono::steady_clock::duration::zero()) {
        return std::chrono::steady_clock::duration::zero();
    }
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
