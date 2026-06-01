// tests/core/decimal_predicates_test.cpp
// Gate B P1 #3 — direct coverage for decimal_traits<pod_decimal> predicates
// and detail::trap_throw. Previously only exercised transitively.

#include <gtest/gtest.h>

#include <climits>
#include <stdexcept>

#include "fixpp/core/decimal.hpp"
#include "fixpp/core/decimal_helpers.hpp"
#include "fixpp/core/error.hpp"

using fixpp::core::decimal_traits;
using fixpp::core::error;
using fixpp::core::pod_decimal;
using fixpp::core::pod_decimal_invalid;
using fixpp::core::detail::trap_throw;

// ── is_finite ───────────────────────────────────────────────────────────────

TEST(DecimalPredicates, IsFiniteRejectsSentinel) {
    EXPECT_FALSE(decimal_traits<pod_decimal>::is_finite(pod_decimal_invalid));
    EXPECT_FALSE(decimal_traits<pod_decimal>::is_finite(pod_decimal{INT64_MIN, 0}));
    EXPECT_FALSE(decimal_traits<pod_decimal>::is_finite(pod_decimal{INT64_MIN, -38}));
}

TEST(DecimalPredicates, IsFiniteAcceptsAllFiniteMantissas) {
    EXPECT_TRUE(decimal_traits<pod_decimal>::is_finite(pod_decimal{0, 0}));
    EXPECT_TRUE(decimal_traits<pod_decimal>::is_finite(pod_decimal{1, 0}));
    EXPECT_TRUE(decimal_traits<pod_decimal>::is_finite(pod_decimal{-1, -38}));
    EXPECT_TRUE(decimal_traits<pod_decimal>::is_finite(pod_decimal{INT64_MAX, 0}));
    EXPECT_TRUE(decimal_traits<pod_decimal>::is_finite(pod_decimal{-INT64_MAX, -38}));
}

// ── is_zero ─────────────────────────────────────────────────────────────────

TEST(DecimalPredicates, IsZeroOnlyForZeroMantissa) {
    EXPECT_TRUE(decimal_traits<pod_decimal>::is_zero(pod_decimal{0, 0}));
    EXPECT_TRUE(decimal_traits<pod_decimal>::is_zero(pod_decimal{0, -38}));
    EXPECT_TRUE(decimal_traits<pod_decimal>::is_zero(pod_decimal{0, -10}));

    EXPECT_FALSE(decimal_traits<pod_decimal>::is_zero(pod_decimal{1, 0}));
    EXPECT_FALSE(decimal_traits<pod_decimal>::is_zero(pod_decimal{-1, -1}));
    // Sentinel mantissa is not zero — predicate reflects raw mantissa equality.
    EXPECT_FALSE(decimal_traits<pod_decimal>::is_zero(pod_decimal_invalid));
}

// ── is_negative ─────────────────────────────────────────────────────────────

TEST(DecimalPredicates, IsNegativeFollowsSignedMantissa) {
    EXPECT_FALSE(decimal_traits<pod_decimal>::is_negative(pod_decimal{0, 0}));
    EXPECT_FALSE(decimal_traits<pod_decimal>::is_negative(pod_decimal{1, 0}));
    EXPECT_FALSE(decimal_traits<pod_decimal>::is_negative(pod_decimal{INT64_MAX, -38}));

    EXPECT_TRUE(decimal_traits<pod_decimal>::is_negative(pod_decimal{-1, 0}));
    EXPECT_TRUE(decimal_traits<pod_decimal>::is_negative(pod_decimal{-INT64_MAX, -38}));
    // The invalid sentinel uses INT64_MIN, which is < 0 — predicate is purely
    // sign-of-mantissa; callers gate validity with is_finite.
    EXPECT_TRUE(decimal_traits<pod_decimal>::is_negative(pod_decimal_invalid));
}

// ── trap_throw ──────────────────────────────────────────────────────────────

TEST(DecimalTrapThrow, ForwardsReturnValueWhenNoThrow) {
    auto r = trap_throw([] { return 42; });
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 42);
}

TEST(DecimalTrapThrow, MapsBadAllocToOutOfMemory) {
    auto r = trap_throw([]() -> int { throw std::bad_alloc{}; });
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::out_of_memory);
}

TEST(DecimalTrapThrow, MapsArbitraryExceptionToInvalidInput) {
    auto r = trap_throw([]() -> int { throw std::runtime_error{"boom"}; });
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_invalid_input);
}

TEST(DecimalTrapThrow, MapsNonStdExceptionToInvalidInput) {
    struct AlienError {};
    auto r = trap_throw([]() -> int { throw AlienError{}; });
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_invalid_input);
}

// trap_throw is declared noexcept — exceptions never propagate.
static_assert(noexcept(trap_throw([] { return 0; })));

// ── from_pod / to_pod canonical-domain enforcement (P1-2 / 2a §4.2) ─────────
// Both from_pod and to_pod must enforce exponent ∈ [-38, 0].
// Out-of-domain values → decimal_overflow; in-domain values → identity.

TEST(DecimalToPod, InDomainRoundTrip) {
    // Representative in-domain values
    const pod_decimal cases[] = {
        {1, 0}, {12345, -2}, {-999, -3}, {9223372036854775806LL, -38}, {0, 0}, {0, -10},
    };
    for (auto const& v : cases) {
        auto r = decimal_traits<pod_decimal>::to_pod(v);
        ASSERT_TRUE(r.has_value()) << "mantissa=" << v.mantissa << " exp=" << (int)v.exponent;
        EXPECT_EQ(r->mantissa, v.mantissa);
        EXPECT_EQ(r->exponent, v.exponent);
    }
}

TEST(DecimalToPod, OutOfDomainPositiveExponentReturnsOverflow) {
    auto r = decimal_traits<pod_decimal>::to_pod(pod_decimal{1, 1});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_overflow);
}

TEST(DecimalToPod, OutOfDomainBelowMinus38ReturnsOverflow) {
    auto r = decimal_traits<pod_decimal>::to_pod(pod_decimal{1, -39});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_overflow);
}

TEST(DecimalToPod, SentinelMantissaReturnsOverflow) {
    auto r = decimal_traits<pod_decimal>::to_pod(pod_decimal{INT64_MIN, 0});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_overflow);
}

TEST(DecimalFromPod, InDomainRoundTrip) {
    const pod_decimal cases[] = {
        {1, 0}, {12345, -2}, {-999, -3}, {9223372036854775806LL, -38}, {0, 0},
    };
    for (auto const& v : cases) {
        auto r = decimal_traits<pod_decimal>::from_pod(v);
        ASSERT_TRUE(r.has_value()) << "mantissa=" << v.mantissa << " exp=" << (int)v.exponent;
        EXPECT_EQ(r->mantissa, v.mantissa);
        EXPECT_EQ(r->exponent, v.exponent);
    }
}

TEST(DecimalFromPod, OutOfDomainPositiveExponentReturnsOverflow) {
    auto r = decimal_traits<pod_decimal>::from_pod(pod_decimal{1, 1});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_overflow);
}

TEST(DecimalFromPod, OutOfDomainBelowMinus38ReturnsOverflow) {
    auto r = decimal_traits<pod_decimal>::from_pod(pod_decimal{1, -39});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_overflow);
}

TEST(DecimalFromPod, SentinelMantissaReturnsOverflow) {
    auto r = decimal_traits<pod_decimal>::from_pod(pod_decimal{INT64_MIN, 0});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_overflow);
}
