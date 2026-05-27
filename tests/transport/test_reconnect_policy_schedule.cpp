// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/transport/test_reconnect_policy_schedule.cpp
// T030 — [2h §9 seam #13] — ReconnectPolicy schedule + delay_for_attempt contract.
//
// Property-style tests with deterministic RNG seeds: schedule formula produces
// the documented exponential-with-jitter envelope; replay against fuzz
// determinism per [const §VII.7] (same seed → same schedule).
//
// Pins ReconnectPolicy::defaults() numerics per Clarifications Q2=C —
// schedule [100ms, 200ms, 400ms, 800ms, 1.6s, 3.2s, 6.4s, 12.8s, 25.6s, 30s]
// + jitter=0.10 + max_attempts=10; asserts cumulative wall-clock at
// max_attempts=10 lands in 73-89 s band before+after jitter.
//
// Parallel cells:
//   - defaults_quickfix_compat() returns {[30s], 0.0, 0} — 30s per attempt,
//     NO jitter, NO cap (industry-canonical QFC/Fix8 path per D-6).
//   - Plateau-at-last per QuickFIX/J
//     IoSessionInitiator::computeNextRetryConnectDelay():318-319 — delay_for_attempt(n)
//     with n >= schedule.size() clamps to last entry.
//   - max_attempts=0 (explicit opt-in to unbounded) verified as a property of
//     defaults_quickfix_compat (FSM-side exceed-surface is session-Phase-4's
//     responsibility; here we just verify the policy value).
//   - Deterministic-per-attempt seed (same session_id_seed + attempt_n → same
//     delay) per FR-021.

#include <gtest/gtest.h>

#include <fixpp/transport/reconnect_policy.hpp>

#include <chrono>
#include <cstdint>
#include <memory_resource>

namespace {

using namespace std::chrono_literals;
using fixpp::transport::ReconnectPolicy;
using ms = std::chrono::milliseconds;

// ─────────────────────────────────────────────────────────────────────────────
// Cell 1 — defaults() materialised schedule + knob values.
// ─────────────────────────────────────────────────────────────────────────────
TEST(ReconnectPolicy, DefaultsMaterialisesV02ExponentialSchedule) {
    const auto p = ReconnectPolicy::defaults();

    ASSERT_EQ(p.schedule.size(), 10u);
    EXPECT_EQ(p.schedule[0], 100ms);
    EXPECT_EQ(p.schedule[1], 200ms);
    EXPECT_EQ(p.schedule[2], 400ms);
    EXPECT_EQ(p.schedule[3], 800ms);
    EXPECT_EQ(p.schedule[4], 1600ms);
    EXPECT_EQ(p.schedule[5], 3200ms);
    EXPECT_EQ(p.schedule[6], 6400ms);
    EXPECT_EQ(p.schedule[7], 12800ms);
    EXPECT_EQ(p.schedule[8], 25600ms);
    EXPECT_EQ(p.schedule[9], 30000ms);

    EXPECT_DOUBLE_EQ(p.jitter, 0.10);
    EXPECT_EQ(p.max_attempts, 10u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 2 — defaults_quickfix_compat() returns single 30s, no jitter, no cap.
// ─────────────────────────────────────────────────────────────────────────────
TEST(ReconnectPolicy, DefaultsQuickFixCompatSingleFixed30sNoJitterNoCap) {
    const auto p = ReconnectPolicy::defaults_quickfix_compat();

    ASSERT_EQ(p.schedule.size(), 1u);
    EXPECT_EQ(p.schedule[0], 30000ms);
    EXPECT_DOUBLE_EQ(p.jitter, 0.0);
    EXPECT_EQ(p.max_attempts, 0u);  // 0 = UNBOUNDED per FR-019.
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 3 — pre-jitter cumulative sum at max_attempts=10 = 81 100 ms.
// ─────────────────────────────────────────────────────────────────────────────
TEST(ReconnectPolicy, DefaultsPreJitterCumulativeMatches81100Ms) {
    const auto p = ReconnectPolicy::defaults();

    ms sum{0};
    for (auto d : p.schedule) {
        sum += d;
    }
    EXPECT_EQ(sum, 81100ms);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 4 — jitter=0 produces exact schedule values (deterministic).
// ─────────────────────────────────────────────────────────────────────────────
TEST(ReconnectPolicy, ZeroJitterReturnsExactScheduleEntries) {
    auto p = ReconnectPolicy::defaults();
    p.jitter = 0.0;  // override for this cell

    for (std::uint32_t n = 0; n < p.schedule.size(); ++n) {
        EXPECT_EQ(p.delay_for_attempt(n), p.schedule[n])
            << "attempt " << n;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 5 — jitter=0.10 keeps delay within ±10% of base schedule entry.
// ─────────────────────────────────────────────────────────────────────────────
TEST(ReconnectPolicy, TenPercentJitterStaysWithinPlusMinusTenPercent) {
    auto p = ReconnectPolicy::defaults();   // jitter = 0.10
    p.session_id_seed = 0xDEADBEEFCAFEBABE;

    for (std::uint32_t n = 0; n < p.schedule.size(); ++n) {
        const auto base = p.schedule[n];
        const auto lo = ms{static_cast<long>(base.count() * 0.90)};
        const auto hi = ms{static_cast<long>(base.count() * 1.10)};
        const auto d = p.delay_for_attempt(n);
        EXPECT_GE(d, lo) << "attempt " << n << " base=" << base.count();
        EXPECT_LE(d, hi) << "attempt " << n << " base=" << base.count();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 6 — plateau-at-last: delay_for_attempt(n >= size) clamps to schedule.back().
// Matches QuickFIX/J IoSessionInitiator::computeNextRetryConnectDelay():318-319.
// ─────────────────────────────────────────────────────────────────────────────
TEST(ReconnectPolicy, PlateauAtLastClampsBeyondScheduleSize) {
    auto p = ReconnectPolicy::defaults();
    p.jitter = 0.0;  // disable jitter so the clamp is exact.

    EXPECT_EQ(p.delay_for_attempt(10),  p.schedule.back());
    EXPECT_EQ(p.delay_for_attempt(100), p.schedule.back());
    EXPECT_EQ(p.delay_for_attempt(std::numeric_limits<std::uint32_t>::max()),
              p.schedule.back());
}

// Plateau also holds for defaults_quickfix_compat (size=1; every attempt → 30s).
TEST(ReconnectPolicy, QuickFixCompatEveryAttemptYields30s) {
    auto p = ReconnectPolicy::defaults_quickfix_compat();  // jitter=0.0
    for (std::uint32_t n : {0u, 1u, 10u, 1000u}) {
        EXPECT_EQ(p.delay_for_attempt(n), 30000ms) << "attempt " << n;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 7 — determinism: same (session_id_seed, attempt_n) → same delay.
// Fuzz-replay contract per [const §VII.7].
// ─────────────────────────────────────────────────────────────────────────────
TEST(ReconnectPolicy, SameSeedAndAttemptReturnsSameDelay) {
    auto a = ReconnectPolicy::defaults();
    a.session_id_seed = 0x1234567890ABCDEF;

    auto b = ReconnectPolicy::defaults();
    b.session_id_seed = 0x1234567890ABCDEF;

    for (std::uint32_t n = 0; n < 12; ++n) {  // include 2 plateau attempts
        EXPECT_EQ(a.delay_for_attempt(n), b.delay_for_attempt(n))
            << "attempt " << n;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 8 — different session_id_seed produces a different jitter trajectory
// at some attempt (sanity check that the seed actually affects the RNG path).
// ─────────────────────────────────────────────────────────────────────────────
TEST(ReconnectPolicy, DifferentSeedsDivergeAtSomeAttempt) {
    auto a = ReconnectPolicy::defaults();
    a.session_id_seed = 0x1111111111111111;

    auto b = ReconnectPolicy::defaults();
    b.session_id_seed = 0x2222222222222222;

    bool any_diff = false;
    for (std::uint32_t n = 0; n < a.schedule.size(); ++n) {
        if (a.delay_for_attempt(n) != b.delay_for_attempt(n)) {
            any_diff = true;
            break;
        }
    }
    EXPECT_TRUE(any_diff)
        << "Two distinct seeds yielded identical schedules across 10 attempts — "
           "the seed is not threading through to the RNG.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 9 — post-jitter cumulative wall-clock at 10 attempts lands in [73, 89] s
// regardless of seed (extremes of ±10% jitter on each entry).
// ─────────────────────────────────────────────────────────────────────────────
TEST(ReconnectPolicy, PostJitterCumulativeWithinSeventyThreeToEightyNineSecondBand) {
    // Use a handful of seeds to sample the jitter band.
    const std::uint64_t seeds[] = {
        0x0000000000000001ULL,
        0xDEADBEEFDEADBEEFULL,
        0xFFFFFFFFFFFFFFFFULL,
        0xA5A5A5A5A5A5A5A5ULL,
    };
    for (auto seed : seeds) {
        auto p = ReconnectPolicy::defaults();
        p.session_id_seed = seed;

        ms cumulative{0};
        for (std::uint32_t n = 0; n < p.schedule.size(); ++n) {
            cumulative += p.delay_for_attempt(n);
        }
        EXPECT_GE(cumulative, 73000ms) << "seed=" << seed;
        EXPECT_LE(cumulative, 89000ms) << "seed=" << seed;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 10 — PMR allocator is honoured: a non-null upstream mr is used to
// allocate the schedule vector when passed in.
// ─────────────────────────────────────────────────────────────────────────────
TEST(ReconnectPolicy, DefaultsAllocatesScheduleAgainstProvidedMr) {
    std::pmr::monotonic_buffer_resource mr{1024};
    auto p = ReconnectPolicy::defaults(&mr);

    ASSERT_EQ(p.schedule.size(), 10u);
    EXPECT_EQ(p.schedule.get_allocator().resource(), &mr);
}

TEST(ReconnectPolicy, QuickFixCompatAllocatesAgainstProvidedMr) {
    std::pmr::monotonic_buffer_resource mr{256};
    auto p = ReconnectPolicy::defaults_quickfix_compat(&mr);

    ASSERT_EQ(p.schedule.size(), 1u);
    EXPECT_EQ(p.schedule.get_allocator().resource(), &mr);
}

}  // namespace
