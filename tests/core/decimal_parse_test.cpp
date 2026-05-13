// tests/core/decimal_parse_test.cpp
// US1 — decimal_traits<pod_decimal>::from_chars — AC-P1..P10

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory_resource>
#include <span>
#include <string_view>

#include "fixpp/core/decimal.hpp"

using fixpp::core::decimal_traits;
using fixpp::core::error;
using fixpp::core::pod_decimal;

namespace {

// Helper: parse ASCII string through from_chars
auto parse(std::string_view s) {
    std::array<std::byte, 64> buf{};
    for (std::size_t i = 0; i < s.size() && i < buf.size(); ++i)
        buf[i] = static_cast<std::byte>(s[i]);
    return decimal_traits<pod_decimal>::from_chars(std::span<const std::byte>{buf.data(), s.size()},
                                                   std::pmr::null_memory_resource());
}

}  // namespace

// AC-P1: empty input → decimal_invalid_input
TEST(DecimalParse, EmptyInput) {
    auto r = parse("");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_invalid_input);
}

// AC-P2: +0 and -0 are accepted and produce {0, 0}
TEST(DecimalParse, PlusZero) {
    auto r = parse("+0");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->mantissa, 0);
    EXPECT_EQ(r->exponent, 0);
}

TEST(DecimalParse, MinusZero) {
    auto r = parse("-0");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->mantissa, 0);
    EXPECT_EQ(r->exponent, 0);
}

TEST(DecimalParse, PlainZero) {
    auto r = parse("0");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->mantissa, 0);
    EXPECT_EQ(r->exponent, 0);
}

// AC-P3: bare .5 or 5. → decimal_invalid_input (leading/trailing dot not allowed)
TEST(DecimalParse, BareLeadingDot) {
    auto r = parse(".5");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_invalid_input);
}

TEST(DecimalParse, BareTrailingDot) {
    auto r = parse("5.");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_invalid_input);
}

// AC-P4: embedded SOH (FIX field delimiter \x01) → error
TEST(DecimalParse, EmbeddedSOH) {
    auto r = parse("1\x012");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_invalid_input);
}

// AC-P5: leading zeros stripped — "00005" → {5, 0}
TEST(DecimalParse, LeadingZerosStripped) {
    auto r = parse("00005");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->mantissa, 5);
    EXPECT_EQ(r->exponent, 0);
}

// AC-P6: trailing fractional zeros preserved — "1.50" → {150, -2}
TEST(DecimalParse, TrailingFractionalZeroPreserved) {
    auto r = parse("1.50");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->mantissa, 150);
    EXPECT_EQ(r->exponent, -2);
}

// AC-P7: mantissa overflow → decimal_overflow
TEST(DecimalParse, MantissaOverflow) {
    // 20 digits — exceeds int64 capacity
    auto r = parse("99999999999999999999");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_overflow);
}

// AC-P8: fractional digits > 38 → exponent < -38 → decimal_overflow
TEST(DecimalParse, ExponentUnderflow) {
    // 39 fractional digits
    auto r = parse("0.000000000000000000000000000000000000001");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_overflow);
}

// AC-P9: sentinel collision — parsed mantissa == INT64_MIN → decimal_overflow
TEST(DecimalParse, SentinelCollision) {
    // INT64_MIN = -9223372036854775808
    auto r = parse("-9223372036854775808");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_overflow);
}

// AC-P10: on failure, out is unmodified (expected_t carries no value on error)
TEST(DecimalParse, FailureNoSideEffect) {
    auto r = parse("");
    EXPECT_FALSE(r.has_value());
    // The expected_t<pod_decimal> simply has no value — the caller's pre-existing
    // storage is untouched because from_chars returns by value, not by out-param.
}

// Additional positive cases
TEST(DecimalParse, SimpleInteger) {
    auto r = parse("42");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->mantissa, 42);
    EXPECT_EQ(r->exponent, 0);
}

TEST(DecimalParse, NegativeDecimal) {
    auto r = parse("-1.25");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->mantissa, -125);
    EXPECT_EQ(r->exponent, -2);
}

TEST(DecimalParse, PositiveDecimal) {
    auto r = parse("3.14159");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->mantissa, 314159);
    EXPECT_EQ(r->exponent, -5);
}

// FIX50SP2 §3.3 example entries
TEST(DecimalParse, FIX50SP2_Example_1) {
    // "1.5" → mantissa=15, exponent=-1
    auto r = parse("1.5");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->mantissa, 15);
    EXPECT_EQ(r->exponent, -1);
}

TEST(DecimalParse, UnknownChar) {
    auto r = parse("1e5");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_invalid_input);
}
