// tests/core/decimal_cross_traits_test.cpp
// US4 — Seam #3: cross-traits conversion AC-X1..X3.
// Defines test_decimal_wide inline; uses mock_decimal_traits.hpp for negative paths.

#include <gtest/gtest.h>

#include <array>
#include <compare>
#include <cstdint>
#include <type_traits>

#include "fixpp/core/decimal.hpp"
#include "fixpp/core/decimal_helpers.hpp"
#include "mock_decimal_traits.hpp"

using fixpp::core::decimal;
using fixpp::core::decimal_traits;
using fixpp::core::error;
using fixpp::core::pod_decimal;

// ── Inline wider-T for AC-X1/X2 tests ───────────────────────────────────────
//
// decimal_wide stores its mantissa as __int128, which MSVC x64 does NOT support
// (no native 128-bit integer type). The wider-mantissa cross-traits machinery is
// therefore exercised only on toolchains with __int128 (Linux/Clang/GCC); the X1,
// X2 and X4 tests below are guarded out under MSVC. pod_decimal — the PRODUCTION
// type — is fully covered on every platform by X3/X5/X6/X7 + the parse/same-type
// tests, which do not depend on __int128.
#ifndef _MSC_VER

namespace fixpp::core::test {

// A minimal wider representation (mantissa stored as __int128 for wider range).
struct decimal_wide {
    __int128 mantissa{};
    std::int8_t exponent{};
};

}  // namespace fixpp::core::test

// Inject specialization for decimal_wide into fixpp::core
namespace fixpp::core {

template <>
struct decimal_traits<test::decimal_wide> {
    using value_type = test::decimal_wide;
    static constexpr bool is_lossless_for_fix_float = true;
    static constexpr std::size_t max_serialized_bytes = 60;

    static expected_t<test::decimal_wide> from_chars(std::span<const std::byte>,
                                                     std::pmr::memory_resource*) noexcept {
        return test::decimal_wide{1, 0};  // stub for AC-X tests
    }

    static expected_t<std::size_t> to_chars(test::decimal_wide const&,
                                            std::span<std::byte> dst) noexcept {
        if (dst.empty()) return std::unexpected{error::decimal_buffer_too_small};
        dst[0] = std::byte{'1'};
        return std::size_t{1};
    }

    static expected_t<test::decimal_wide> from_pod(pod_decimal pd) noexcept {
        // Widen: always succeeds for in-domain pod_decimal values
        return test::decimal_wide{static_cast<__int128>(pd.mantissa), pd.exponent};
    }

    static expected_t<pod_decimal> to_pod(test::decimal_wide const& v) noexcept {
        // Narrow: fails if out of int64 range (not in pod_decimal mantissa domain).
        // Returns decimal_overflow — the contract-correct error for an out-of-domain pod.
        // The cross-traits wrapper (decimal::from/to) is responsible for remapping
        // this to decimal_precision_loss per 2a §6.4.
        static constexpr __int128 INT64_MIN_128 = static_cast<__int128>(INT64_MIN) + 1;
        static constexpr __int128 INT64_MAX_128 = static_cast<__int128>(INT64_MAX);
        if (v.mantissa < INT64_MIN_128 || v.mantissa > INT64_MAX_128)
            return std::unexpected{error::decimal_overflow};
        // Also enforce exponent domain per the canonical pod_decimal contract.
        if (v.exponent < -38 || v.exponent > 0) return std::unexpected{error::decimal_overflow};
        return pod_decimal{static_cast<std::int64_t>(v.mantissa), v.exponent};
    }

    static std::strong_ordering compare(test::decimal_wide const& a,
                                        test::decimal_wide const& b) noexcept {
        if (a.mantissa < b.mantissa) return std::strong_ordering::less;
        if (a.mantissa > b.mantissa) return std::strong_ordering::greater;
        return std::strong_ordering::equal;
    }

    static bool is_finite(test::decimal_wide const& v) noexcept { return v.mantissa != INT64_MIN; }
    static bool is_zero(test::decimal_wide const& v) noexcept { return v.mantissa == 0; }
    static bool is_negative(test::decimal_wide const& v) noexcept { return v.mantissa < 0; }
};

}  // namespace fixpp::core

#endif  // !_MSC_VER (decimal_wide / __int128)

#ifndef _MSC_VER
// ── AC-X1: T≠U round-trip funnels through pod_decimal ───────────────────────
// decimal<Wide>::to<pod_decimal>() then decimal<pod_decimal>::from<Wide>()
// restores value for in-domain values. Exercises the wrapper methods end-to-end.
TEST(DecimalCrossTraits, X1_RoundTripThroughPod) {
    using Wide = fixpp::core::test::decimal_wide;
    // Create a wide decimal with a value that fits in pod_decimal
    decimal<Wide> src{Wide{12345LL, -2}};

    // Wide → pod via decimal<Wide>::to<pod_decimal>() wrapper
    auto to_pod_r = src.to<pod_decimal>();
    ASSERT_TRUE(to_pod_r.has_value());
    EXPECT_EQ(to_pod_r->value().mantissa, 12345);
    EXPECT_EQ(to_pod_r->value().exponent, -2);

    // pod → wide via decimal<pod_decimal>::from<Wide>() wrapper (symmetry)
    auto from_pod_r = decimal<pod_decimal>::from(src);
    ASSERT_TRUE(from_pod_r.has_value());
    EXPECT_EQ(from_pod_r->value().mantissa, 12345);
    EXPECT_EQ(from_pod_r->value().exponent, -2);
}

// AC-X2: out-of-domain narrowing returns decimal_precision_loss (not decimal_overflow)
// The mock's to_pod returns decimal_overflow for out-of-int64-range values;
// the wrapper (decimal<Wide>::to<pod_decimal>()) must remap to decimal_precision_loss
// per 2a §6.4. This test verifies the remap inside the wrapper method.
TEST(DecimalCrossTraits, X2_NarrowingPrecisionLoss) {
    using Wide = fixpp::core::test::decimal_wide;
    // A value that overflows int64 — mock to_pod returns decimal_overflow
    static constexpr __int128 TOO_BIG = static_cast<__int128>(INT64_MAX) + 1;
    decimal<Wide> wide_val{Wide{TOO_BIG, 0}};

    auto r = wide_val.to<pod_decimal>();
    ASSERT_FALSE(r.has_value());
    // Wrapper must remap decimal_overflow → decimal_precision_loss (2a §6.4)
    EXPECT_EQ(r.error(), error::decimal_precision_loss);
}
#endif  // !_MSC_VER (X1/X2 use decimal_wide)

// AC-X3: T==U if constexpr short-circuit — no funnel, no error
// decimal<pod_decimal>::to<pod_decimal>() hits the is_same_v<T,U> branch and
// returns the value directly without going through the pod funnel.
TEST(DecimalCrossTraits, X3_SameTypeSameValue) {
    pod_decimal v{100, -2};
    decimal<pod_decimal> src{v};

    // Wrapper short-circuit: to<pod_decimal>() on a decimal<pod_decimal>
    auto r = src.to<pod_decimal>();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->value().mantissa, v.mantissa);
    EXPECT_EQ(r->value().exponent, v.exponent);
}

// Negative path using mock_decimal_traits (seam #1)
TEST(DecimalCrossTraits, MockTraitsFromPodFail) {
    using Mock = fixpp::core::test::mock_pod;
    fixpp::core::decimal_traits<Mock>::fail_mask = fixpp::core::test::mock_fail::from_pod;

    auto r = decimal_traits<Mock>::from_pod(pod_decimal{1, 0});
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_precision_loss);

    // Reset
    decimal_traits<Mock>::fail_mask = fixpp::core::test::mock_fail::none;
}

// AC-X4: from<U>() overflow remap — symmetric to X2 on the from<U>() wrapper.
// decimal<pod_decimal>::from(decimal<Wide>) where Wide's to_pod returns
// decimal_overflow must remap to decimal_precision_loss per 2a §6.4.
#ifndef _MSC_VER  // decimal_wide / __int128 — see guard above
TEST(DecimalCrossTraits, X4_FromOverflowRemap) {
    using Wide = fixpp::core::test::decimal_wide;
    static constexpr __int128 TOO_BIG = static_cast<__int128>(INT64_MAX) + 1;
    decimal<Wide> wide_val{Wide{TOO_BIG, 0}};

    // from<U>() where U=Wide — Wide's to_pod returns decimal_overflow
    auto r = decimal<pod_decimal>::from(wide_val);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_precision_loss);
}
#endif  // !_MSC_VER (X4 uses decimal_wide)

// AC-X5: from<U>() target from_pod failure passes through raw error
// (the second error branch in decimal<T>::from<U>()).
TEST(DecimalCrossTraits, X5_FromTargetFromPodFailure) {
    using Mock = fixpp::core::test::mock_pod;
    decimal_traits<Mock>::fail_mask = fixpp::core::test::mock_fail::from_pod;

    decimal<pod_decimal> src{pod_decimal{1, 0}};
    auto r = decimal<Mock>::from(src);  // T=Mock, U=pod_decimal
    ASSERT_FALSE(r.has_value());
    // Mock's from_pod fails with decimal_precision_loss; passed through raw
    // (not via the decimal_overflow ternary remap).
    EXPECT_EQ(r.error(), error::decimal_precision_loss);

    decimal_traits<Mock>::fail_mask = fixpp::core::test::mock_fail::none;
}

// AC-X6: to<U>() target from_pod failure passes through raw error
// (the second error branch in decimal<T>::to<U>()).
TEST(DecimalCrossTraits, X6_ToTargetFromPodFailure) {
    using Mock = fixpp::core::test::mock_pod;
    decimal_traits<Mock>::fail_mask = fixpp::core::test::mock_fail::from_pod;

    decimal<pod_decimal> src{pod_decimal{1, 0}};
    auto r = src.to<Mock>();  // T=pod_decimal, U=Mock
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_precision_loss);

    decimal_traits<Mock>::fail_mask = fixpp::core::test::mock_fail::none;
}

// AC-X7: from<U>() and to<U>() pass through non-overflow errors verbatim
// (the ternary "false" branch — pod.error() != decimal_overflow).
// This is the documented behavior: only decimal_overflow gets remapped to
// decimal_precision_loss; other errors propagate unchanged.
TEST(DecimalCrossTraits, X7_FromNonOverflowErrorPassthrough) {
    using Mock = fixpp::core::test::mock_pod;
    decimal_traits<Mock>::fail_mask = fixpp::core::test::mock_fail::to_pod;
    decimal_traits<Mock>::to_pod_error = error::decimal_invalid_input;  // non-overflow

    decimal<Mock> src{fixpp::core::test::mock_pod{1, 0}};
    auto r_from = decimal<pod_decimal>::from(src);  // hits from<U>() ternary false arm
    ASSERT_FALSE(r_from.has_value());
    EXPECT_EQ(r_from.error(), error::decimal_invalid_input);  // NOT remapped

    auto r_to = src.to<pod_decimal>();  // hits to<U>() ternary false arm
    ASSERT_FALSE(r_to.has_value());
    EXPECT_EQ(r_to.error(), error::decimal_invalid_input);  // NOT remapped

    // Reset
    decimal_traits<Mock>::fail_mask = fixpp::core::test::mock_fail::none;
    decimal_traits<Mock>::to_pod_error = error::decimal_overflow;
}

TEST(DecimalCrossTraits, ParseInvalidInputPropagatesError) {
    std::array<std::byte, 1> src{std::byte{'+'}};
    auto r = decimal<pod_decimal>::parse(std::span<const std::byte>{src.data(), src.size()},
                                         std::pmr::null_memory_resource());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_invalid_input);
}

TEST(DecimalCrossTraits, ParseValidInputReturnsWrappedValue) {
    std::array<std::byte, 3> src{std::byte{'1'}, std::byte{'.'}, std::byte{'5'}};
    auto r = decimal<pod_decimal>::parse(std::span<const std::byte>{src.data(), src.size()},
                                         std::pmr::null_memory_resource());
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->value().mantissa, 15);
    EXPECT_EQ(r->value().exponent, -1);
}

TEST(DecimalCrossTraits, FromSameTypeShortCircuitsWithoutTraitHooks) {
    using Mock = fixpp::core::test::mock_pod;
    decimal_traits<Mock>::fail_mask =
        fixpp::core::test::mock_fail::from_pod | fixpp::core::test::mock_fail::to_pod;

    decimal<Mock> src{Mock{7, -1}};
    auto r = decimal<Mock>::from(src);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->value().mantissa, 7);
    EXPECT_EQ(r->value().exponent, -1);

    decimal_traits<Mock>::fail_mask = fixpp::core::test::mock_fail::none;
}

TEST(DecimalCrossTraits, ToSameTypeShortCircuitsWithoutTraitHooks) {
    using Mock = fixpp::core::test::mock_pod;
    decimal_traits<Mock>::fail_mask =
        fixpp::core::test::mock_fail::from_pod | fixpp::core::test::mock_fail::to_pod;

    decimal<Mock> src{Mock{9, -2}};
    auto r = src.to<Mock>();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->value().mantissa, 9);
    EXPECT_EQ(r->value().exponent, -2);

    decimal_traits<Mock>::fail_mask = fixpp::core::test::mock_fail::none;
}
