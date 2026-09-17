// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/wait_until_clamp_test.cpp
//
// Gate B round 1, #326: `wait_until_observed` did not honor its advertised
// budget when `slice` outlives the remaining time to the deadline -- it slept
// the FULL slice regardless.
// `pump_until` in pump_until_ready.hpp already clamps its own sleep to
// `min(slice, deadline - now)`; `wait_until_observed` did not.

#include <gtest/gtest.h>

#include <chrono>

#include "support/wait_until.hpp"

using fixpp::test_support::wait_until_observed;

TEST(WaitUntilObservedClamp, TimeoutReturnsPromptlyWhenSliceOutlivesBudget) {
    // Predicate never becomes true, and the slice dwarfs the budget: an
    // unclamped sleep blocks for at least `slice`, the clamp bounds the wait to
    // about `budget`.
    //
    // The bound is derived from the MUTANT's side, which cannot move: sleep_for
    // blocks for at least its duration, so an unclamped wait is never shorter
    // than `slice`, however fast the runner is. A scheduling stall is ADDITIVE
    // and only lengthens the clamped path, so the false red needs one stall of
    // about `bound`. Size `slice` against the largest stall a runner can
    // produce, never against the clamped path's latency (#470).
    const auto budget = std::chrono::milliseconds{2};
    const auto slice = std::chrono::seconds{10};
    const auto bound = slice - std::chrono::seconds{1};

    const auto start = std::chrono::steady_clock::now();
    const bool observed = wait_until_observed([] { return false; }, budget, slice);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(observed);
    EXPECT_LT(elapsed, bound);
}
