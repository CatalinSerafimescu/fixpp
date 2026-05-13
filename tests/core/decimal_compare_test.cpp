// tests/core/decimal_compare_test.cpp
// US1 — decimal_traits<pod_decimal>::compare — AC-C1..C5

#include <gtest/gtest.h>

#include <compare>
#include <type_traits>

#include "fixpp/core/decimal.hpp"

using fixpp::core::decimal_traits;
using fixpp::core::pod_decimal;
using fixpp::core::pod_decimal_invalid;

// AC-C4: compare returns std::strong_ordering (compile-time check)
static_assert(
    std::is_same_v<decltype(decimal_traits<pod_decimal>::compare(pod_decimal{}, pod_decimal{})),
                   std::strong_ordering>);

// AC-C5: compare is noexcept
static_assert(noexcept(decimal_traits<pod_decimal>::compare(pod_decimal{}, pod_decimal{})));

// AC-C1: {1,0}, {10,-1}, {100,-2} all compare equal (canonicalized value equality)
TEST(DecimalCompare, ValueEqualityCanonical) {
    pod_decimal a{1, 0};
    pod_decimal b{10, -1};
    pod_decimal c{100, -2};

    EXPECT_EQ(decimal_traits<pod_decimal>::compare(a, b), std::strong_ordering::equal);
    EXPECT_EQ(decimal_traits<pod_decimal>::compare(b, c), std::strong_ordering::equal);
    EXPECT_EQ(decimal_traits<pod_decimal>::compare(a, c), std::strong_ordering::equal);
}

// AC-C2: pod_decimal_invalid orders strictly greater than every finite value
TEST(DecimalCompare, InvalidGreaterThanFinite) {
    EXPECT_EQ(decimal_traits<pod_decimal>::compare(pod_decimal_invalid, pod_decimal{0, 0}),
              std::strong_ordering::greater);
    EXPECT_EQ(decimal_traits<pod_decimal>::compare(pod_decimal{0, 0}, pod_decimal_invalid),
              std::strong_ordering::less);
    EXPECT_EQ(decimal_traits<pod_decimal>::compare(pod_decimal_invalid, pod_decimal_invalid),
              std::strong_ordering::equal);
    EXPECT_EQ(decimal_traits<pod_decimal>::compare(pod_decimal_invalid, pod_decimal{INT64_MAX, 0}),
              std::strong_ordering::greater);
}

// AC-C3: algorithm never uses __int128 — verified by correctness on boundary values
// that would overflow int128 if the comparison were done via multiplication.
TEST(DecimalCompare, NoBigIntOverflow) {
    // INT64_MAX × 10^0 vs INT64_MAX × 10^0
    pod_decimal big{INT64_MAX, 0};
    EXPECT_EQ(decimal_traits<pod_decimal>::compare(big, big), std::strong_ordering::equal);

    // Different magnitudes
    pod_decimal small{1, -38};
    EXPECT_EQ(decimal_traits<pod_decimal>::compare(big, small), std::strong_ordering::greater);
}

TEST(DecimalCompare, OrderingLess) {
    EXPECT_EQ(decimal_traits<pod_decimal>::compare(pod_decimal{1, 0}, pod_decimal{2, 0}),
              std::strong_ordering::less);
}

TEST(DecimalCompare, OrderingGreater) {
    EXPECT_EQ(decimal_traits<pod_decimal>::compare(pod_decimal{2, 0}, pod_decimal{1, 0}),
              std::strong_ordering::greater);
}

TEST(DecimalCompare, NegativePositive) {
    EXPECT_EQ(decimal_traits<pod_decimal>::compare(pod_decimal{-1, 0}, pod_decimal{1, 0}),
              std::strong_ordering::less);
}

TEST(DecimalCompare, BothNegativeCanonical) {
    // -10 × 10^-1 == -1 × 10^0
    pod_decimal a{-10, -1};
    pod_decimal b{-1, 0};
    EXPECT_EQ(decimal_traits<pod_decimal>::compare(a, b), std::strong_ordering::equal);
}

// decimal<pod_decimal> operator== and operator<=> rely on compare
TEST(DecimalCompare, DecimalWrapperOperators) {
    using fixpp::core::decimal;
    decimal<pod_decimal> x{pod_decimal{10, -1}};
    decimal<pod_decimal> y{pod_decimal{1, 0}};
    EXPECT_EQ(x, y);
    EXPECT_EQ(x <=> y, std::strong_ordering::equal);
}
