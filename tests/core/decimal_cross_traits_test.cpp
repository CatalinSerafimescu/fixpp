// tests/core/decimal_cross_traits_test.cpp
// US4 — Seam #3: cross-traits conversion AC-X1..X3.
// Defines test_decimal_wide inline; uses mock_decimal_traits.hpp for negative paths.

#include <gtest/gtest.h>

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
        // Narrow: fails if out of int64 range
        static constexpr __int128 INT64_MIN_128 = static_cast<__int128>(INT64_MIN);
        static constexpr __int128 INT64_MAX_128 = static_cast<__int128>(INT64_MAX);
        if (v.mantissa < INT64_MIN_128 + 1 || v.mantissa > INT64_MAX_128)
            return std::unexpected{error::decimal_precision_loss};
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

// ── AC-X1: T≠U round-trip funnels through pod_decimal ───────────────────────
// decimal<pod_decimal>::from<decimal_wide>(src) then .to<pod_decimal>() restores value
// for in-domain values.
TEST(DecimalCrossTraits, X1_RoundTripThroughPod) {
    using Wide = fixpp::core::test::decimal_wide;
    // Create a wide decimal with a value that fits in pod_decimal
    decimal<Wide> src{Wide{12345LL, -2}};

    // Convert wide → pod via from<pod_decimal>
    // Actually, T037 tests decimal<T>::from<U>() — but those bodies are T039/T040 stubs
    // until US4 implementation. For T037 TDD (red phase), we call the traits directly.

    // Direct traits-level round-trip (seam #3)
    auto to_pod_r = decimal_traits<Wide>::to_pod(src.value());
    ASSERT_TRUE(to_pod_r.has_value());

    auto from_pod_r = decimal_traits<pod_decimal>::from_pod(*to_pod_r);
    ASSERT_TRUE(from_pod_r.has_value());

    // Value should round-trip correctly
    EXPECT_EQ(from_pod_r->mantissa, 12345);
    EXPECT_EQ(from_pod_r->exponent, -2);
}

// AC-X2: out-of-domain narrowing returns decimal_precision_loss
TEST(DecimalCrossTraits, X2_NarrowingPrecisionLoss) {
    using Wide = fixpp::core::test::decimal_wide;
    // A value that overflows int64
    static constexpr __int128 TOO_BIG = static_cast<__int128>(INT64_MAX) + 1;
    Wide wide_val{TOO_BIG, 0};

    auto r = decimal_traits<Wide>::to_pod(wide_val);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_precision_loss);
}

// AC-X3: T==U if constexpr short-circuit — no funnel, no error
// This is a compile-time property; verified by instantiating T==U path.
// The actual short-circuit is implemented in T039/T040 replacing the stubs.
// At TDD red phase we verify the traits-level T==U path.
TEST(DecimalCrossTraits, X3_SameTypeSameValue) {
    pod_decimal v{100, -2};
    // to_pod on pod_decimal is identity (T==U path)
    auto r = decimal_traits<pod_decimal>::to_pod(v);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->mantissa, v.mantissa);
    EXPECT_EQ(r->exponent, v.exponent);
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
