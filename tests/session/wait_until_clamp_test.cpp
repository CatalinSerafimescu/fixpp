// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/wait_until_clamp_test.cpp
//
// Gate B round 1, #326: `wait_until_observed` did not honor its advertised
// budget when `slice` outlives the remaining time to the deadline -- it slept
// the FULL slice regardless, so `budget=1ms, slice=1s` returned ~1s late.
// `pump_until` (pump_until_ready.hpp:75) already clamps its own sleep to
// `min(slice, deadline - now)`; `wait_until_observed` did not.
//
// One focused test, not a suite (Gate B waived the seven-case ask: no other
// tests/support/*.hpp in this repo carries a dedicated unit test, and this is
// the one path the clamp fix touches).

#include "support/wait_until.hpp"

#include <gtest/gtest.h>

#include <chrono>

using fixpp::test_support::wait_until_observed;

TEST(WaitUntilObservedClamp, TimeoutReturnsPromptlyWhenSliceOutlivesBudget) {
    // Predicate never becomes true; slice is two orders of magnitude larger
    // than the budget. An unclamped sleep would block for ~slice (200ms);
    // the clamp bounds the wait to ~budget (2ms).
    const auto budget = std::chrono::milliseconds{2};
    const auto slice = std::chrono::milliseconds{200};

    const auto start = std::chrono::steady_clock::now();
    const bool observed = wait_until_observed([] { return false; }, budget, slice);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(observed);
    // Generous upper bound to stay robust under CI scheduling jitter while
    // still discriminating against the unclamped ~200ms behaviour.
    EXPECT_LT(elapsed, std::chrono::milliseconds{100});
}
