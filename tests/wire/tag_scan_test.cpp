// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/tag_scan_test.cpp — T004 runtime boundary unit test for
// fixpp::wire::accumulate_tag_digit (contracts/tag-scan-helper.md / FR-007).
//
// Witnesses:
//   - 65535       : max valid FIX tag — accepted, value == 65535
//   - 65536       : one beyond the bound — rejected
//   - 429496729634: wrap-and-continue alias (→34) — rejected, value never 34
//   - 429496729649: wrap-and-continue alias (→49) — rejected, value never 49
//   - 000000000034 : zero-padded tag — accepted, value == 34
//   - single digit : e.g. '8' — accepted, value == 8

#include <gtest/gtest.h>

#include <cstdint>
#include <fixpp/wire/tag_scan.hpp>
#include <string_view>

namespace {

using fixpp::wire::accumulate_tag_digit;

// Helper: run every digit of `digits` through accumulate_tag_digit. Returns
// {saw_false, final_tag_value}. Stops as soon as false is returned.
struct DriveResult {
    bool saw_false;
    std::uint32_t final_value;
};

DriveResult drive(std::string_view digits) {
    std::uint32_t t = 0;
    for (char c : digits) {
        if (!accumulate_tag_digit(t, static_cast<unsigned char>(c))) {
            return {.saw_false = true, .final_value = t};
        }
    }
    return {.saw_false = false, .final_value = t};
}

// ---------------------------------------------------------------------------
// Accept cases
// ---------------------------------------------------------------------------

TEST(TagScanHelper, AcceptsMaxValidTag65535) {
    auto [saw_false, val] = drive("65535");
    EXPECT_FALSE(saw_false) << "65535 must be accepted (no false return)";
    EXPECT_EQ(val, 65535U) << "accumulator must be exactly 65535";
}

TEST(TagScanHelper, AcceptsZeroPaddedTag34) {
    auto [saw_false, val] = drive("000000000034");
    EXPECT_FALSE(saw_false) << "zero-padded 000000000034 must be accepted";
    EXPECT_EQ(val, 34U) << "accumulator must be exactly 34";
}

TEST(TagScanHelper, AcceptsSingleDigit) {
    auto [saw_false, val] = drive("8");
    EXPECT_FALSE(saw_false) << "single digit '8' must be accepted";
    EXPECT_EQ(val, 8U);
}

// ---------------------------------------------------------------------------
// Reject cases — must return false AND never transiently alias the small tag
// ---------------------------------------------------------------------------

TEST(TagScanHelper, Rejects65536) {
    // Named post-conditions (feedback_witness_asserts_named_postcondition_not_proxy):
    // 1. accumulate_tag_digit returns false at some digit
    // 2. the accumulator never transitions to 65536 (no in-loop check needed here;
    //    the helper either rejects or accepts — there is no transient 65536 value
    //    because we stop on false, so the end-value check is sufficient)
    auto [saw_false, val] = drive("65536");
    EXPECT_TRUE(saw_false) << "65536 must be rejected (helper returns false)";
    EXPECT_NE(val, 65536U) << "accumulator must not hold 65536 when rejected";
}

TEST(TagScanHelper, RejectsWrapVector429496729634_NeverAlias34) {
    // Both named post-conditions:
    // 1. helper returns false at some digit (wrap is stopped before it occurs)
    // 2. accumulator never equals the aliased small tag 34 at rejection or at any
    //    in-loop step — we check in-loop for the strongest witness.
    std::string_view digits = "429496729634";
    std::uint32_t t = 0;
    bool saw_false = false;
    for (char c : digits) {
        // Assert the accumulator has not yet reached the aliased tag 34 while
        // we are still processing digits (it should reject first).
        EXPECT_NE(t, 34U) << "accumulator aliased tag 34 before rejection";
        if (!accumulate_tag_digit(t, static_cast<unsigned char>(c))) {
            saw_false = true;
            break;
        }
    }
    EXPECT_TRUE(saw_false)
        << "wrap-and-continue 429496729634 must be rejected (helper returns false)";
    EXPECT_NE(t, 34U) << "accumulator must not equal aliased tag 34 at point of rejection";
}

TEST(TagScanHelper, RejectsWrapVector429496729649_NeverAlias49) {
    // Same two named post-conditions, alias target is 49.
    std::string_view digits = "429496729649";
    std::uint32_t t = 0;
    bool saw_false = false;
    for (char c : digits) {
        EXPECT_NE(t, 49U) << "accumulator aliased tag 49 before rejection";
        if (!accumulate_tag_digit(t, static_cast<unsigned char>(c))) {
            saw_false = true;
            break;
        }
    }
    EXPECT_TRUE(saw_false)
        << "wrap-and-continue 429496729649 must be rejected (helper returns false)";
    EXPECT_NE(t, 49U) << "accumulator must not equal aliased tag 49 at point of rejection";
}

}  // namespace
