// tests/core/decimal_format_test.cpp
// US1 — decimal_traits<pod_decimal>::to_chars — AC-S1..S6

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include "fixpp/core/decimal.hpp"

using fixpp::core::decimal_traits;
using fixpp::core::error;
using fixpp::core::pod_decimal;
using fixpp::core::pod_decimal_invalid;

namespace {

// Format a pod_decimal into a char buffer; returns the string on success.
auto fmt(pod_decimal v) {
    std::array<std::byte, 64> buf{};
    auto r = decimal_traits<pod_decimal>::to_chars(v, std::span<std::byte>{buf});
    struct result_t {
        bool ok;
        error err{};
        std::string str;
    };
    if (!r.has_value()) return result_t{false, r.error(), {}};
    return result_t{true, {}, std::string(reinterpret_cast<const char*>(buf.data()), *r)};
}

}  // namespace

// AC-S1: sentinel mantissa == INT64_MIN → decimal_invalid_input
TEST(DecimalFormat, SentinelInvalid) {
    auto r = fmt(pod_decimal_invalid);
    ASSERT_FALSE(r.ok);
    EXPECT_EQ(r.err, error::decimal_invalid_input);
}

// AC-S2: mantissa == 0 → "0" regardless of exponent
TEST(DecimalFormat, ZeroMantissaAnyExponent) {
    for (std::int8_t exp = 0; exp >= -10; --exp) {
        auto r = fmt(pod_decimal{0, exp});
        ASSERT_TRUE(r.ok) << "exp=" << static_cast<int>(exp);
        EXPECT_EQ(r.str, "0") << "exp=" << static_cast<int>(exp);
    }
}

// AC-S3: exponent outside [-38, 0] → decimal_invalid_input before formatting
TEST(DecimalFormat, ExponentPositive) {
    auto r = fmt(pod_decimal{1, 1});
    ASSERT_FALSE(r.ok);
    EXPECT_EQ(r.err, error::decimal_invalid_input);
}

TEST(DecimalFormat, ExponentBelowMinus38) {
    auto r = fmt(pod_decimal{1, -39});
    ASSERT_FALSE(r.ok);
    EXPECT_EQ(r.err, error::decimal_invalid_input);
}

// AC-S4: trailing zero stripping — {150, -2} → "1.5" not "1.50"
TEST(DecimalFormat, TrailingZeroStripped) {
    auto r = fmt(pod_decimal{150, -2});
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.str, "1.5");
}

// AC-S5: worst-case 41-byte bound — max_serialized_bytes == 41
TEST(DecimalFormat, WorstCaseBound) {
    static_assert(decimal_traits<pod_decimal>::max_serialized_bytes == 41);
    // A value that produces many digits: -9999999999999999999 × 10^-38
    // sign(1) + "0." + 19 zeros (38 - 19 = 19 leading zeros) + 19 mantissa digits
    // Actually worst case is sign + "0." + 19 fractional zeros + 19 mantissa digits
    // = 1 + 2 + 19 + 19 = 41 bytes
    auto r = fmt(pod_decimal{-9223372036854775807LL, -38});
    ASSERT_TRUE(r.ok);
    EXPECT_LE(r.str.size(), 41u);
}

// AC-S6: buffer too small → decimal_buffer_too_small
TEST(DecimalFormat, BufferTooSmall) {
    std::array<std::byte, 1> tiny{};
    auto r =
        decimal_traits<pod_decimal>::to_chars(pod_decimal{12345, -2}, std::span<std::byte>{tiny});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_buffer_too_small);
}

TEST(DecimalFormat, ZeroMantissaEmptyBufferTooSmall) {
    std::span<std::byte> empty{};
    auto r = decimal_traits<pod_decimal>::to_chars(pod_decimal{0, 0}, empty);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_buffer_too_small);
}

// Additional positive cases
TEST(DecimalFormat, SimpleInteger) {
    auto r = fmt(pod_decimal{42, 0});
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.str, "42");
}

TEST(DecimalFormat, NegativeDecimal) {
    auto r = fmt(pod_decimal{-125, -2});
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.str, "-1.25");
}

TEST(DecimalFormat, FractionalOnly) {
    auto r = fmt(pod_decimal{5, -1});
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.str, "0.5");
}

TEST(DecimalFormat, FIX50SP2_1_5) {
    auto r = fmt(pod_decimal{15, -1});
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.str, "1.5");
}
