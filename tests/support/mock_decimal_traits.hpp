#pragma once
// tests/support/mock_decimal_traits.hpp
// Seam #1 — header-only parameterizable failing-traits helper.
// Configurable to fail on any of: from_chars, to_chars, from_pod, to_pod, compare.
// Used by decimal_cross_traits_test.cpp (seam #3) and future downstream 2b tests.

#include <compare>
#include <cstddef>
#include <memory_resource>
#include <span>

#include "fixpp/core/decimal.hpp"

namespace fixpp::core::test {

// Bitmask controlling which operations fail.
enum class mock_fail : unsigned {
    none = 0,
    from_chars = 1 << 0,
    to_chars = 1 << 1,
    from_pod = 1 << 2,
    to_pod = 1 << 3,
    compare = 1 << 4,
};
constexpr mock_fail operator|(mock_fail a, mock_fail b) noexcept {
    return static_cast<mock_fail>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
constexpr bool has(mock_fail mask, mock_fail bit) noexcept {
    return (static_cast<unsigned>(mask) & static_cast<unsigned>(bit)) != 0;
}

// A minimal representation type for use with mock_decimal_traits.
struct mock_pod {
    std::int64_t mantissa{};
    std::int8_t exponent{};
};

}  // namespace fixpp::core::test

// Inject specialization into fixpp::core so decimal<mock_pod> compiles.
namespace fixpp::core {

template <>
struct decimal_traits<test::mock_pod> {
    using value_type = test::mock_pod;
    static constexpr bool is_lossless_for_fix_float = false;
    static constexpr std::size_t max_serialized_bytes = 41;

    // The fail mask is set per test via a thread-local (reset after each test).
    static inline test::mock_fail fail_mask{test::mock_fail::none};

    // Configurable error returned by to_pod when its fail bit is set. Default is
    // decimal_overflow (the contract-correct error for an out-of-domain pod). Tests
    // exercising the cross-traits wrapper's "non-overflow passthrough" branch may
    // override this to verify the ternary's false arm.
    static inline fixpp::core::error to_pod_error{fixpp::core::error::decimal_overflow};

    static expected_t<test::mock_pod> from_chars(std::span<const std::byte> /*src*/,
                                                 std::pmr::memory_resource* /*mr*/) noexcept {
        if (test::has(fail_mask, test::mock_fail::from_chars))
            return std::unexpected{error::decimal_invalid_input};
        return test::mock_pod{1, 0};  // constant valid value
    }

    static expected_t<std::size_t> to_chars(test::mock_pod const& /*v*/,
                                            std::span<std::byte> dst) noexcept {
        if (test::has(fail_mask, test::mock_fail::to_chars))
            return std::unexpected{error::decimal_invalid_input};
        if (dst.size() < 1) return std::unexpected{error::decimal_buffer_too_small};
        dst[0] = std::byte{'1'};
        return std::size_t{1};
    }

    static expected_t<test::mock_pod> from_pod(pod_decimal pd) noexcept {
        if (test::has(fail_mask, test::mock_fail::from_pod))
            return std::unexpected{error::decimal_precision_loss};
        return test::mock_pod{pd.mantissa, pd.exponent};
    }

    static expected_t<pod_decimal> to_pod(test::mock_pod const& v) noexcept {
        if (test::has(fail_mask, test::mock_fail::to_pod))
            return std::unexpected{to_pod_error};
        return pod_decimal{v.mantissa, v.exponent};
    }

    static std::strong_ordering compare(test::mock_pod const& a, test::mock_pod const& b) noexcept {
        if (test::has(fail_mask, test::mock_fail::compare))
            return std::strong_ordering::equal;  // degenerate but noexcept
        if (a.mantissa < b.mantissa) return std::strong_ordering::less;
        if (a.mantissa > b.mantissa) return std::strong_ordering::greater;
        return std::strong_ordering::equal;
    }

    static bool is_finite(test::mock_pod const& v) noexcept { return v.mantissa != INT64_MIN; }
    static bool is_zero(test::mock_pod const& v) noexcept { return v.mantissa == 0; }
    static bool is_negative(test::mock_pod const& v) noexcept { return v.mantissa < 0; }
};

}  // namespace fixpp::core
