// tests/core/decimal_reference_compare.hpp
// 060-int128-decimal-compare — T010.
//
// Shared frozen reference, extracted out of decimal_compare_diff_oracle_test.cpp
// so the differential libFuzzer target (tests/fuzz/fuzz_decimal_compare.cpp)
// and the GoogleTest differential oracle use the SAME source of truth instead
// of two copies that could drift apart.
//
// FROZEN — this is a verbatim copy of decimal_traits<pod_decimal>::compare's
// body as of 060 T003 (pre-T005 swap): the pre-T005, digit-string comparator.
// NEVER re-sync this to the new impl — that makes both differential
// consumers (the GoogleTest oracle and the fuzzer) vacuous. See
// specs/060-int128-decimal-compare/tasks.md T003/T010 + Notes. Verbatim
// source: src/core/decimal.cpp:244-393 (only the signature is adapted: free
// function instead of a member of decimal_traits<pod_decimal>, taking `a`/`b`
// by const ref exactly as the original).

#ifndef FIXPP_TESTS_CORE_DECIMAL_REFERENCE_COMPARE_HPP
#define FIXPP_TESTS_CORE_DECIMAL_REFERENCE_COMPARE_HPP

#include <compare>
#include <cstdint>
#include <utility>

#include "fixpp/core/decimal.hpp"

using fixpp::core::pod_decimal;

inline std::strong_ordering reference_compare(pod_decimal const& a, pod_decimal const& b) noexcept {
    bool a_invalid = (a.mantissa == INT64_MIN);
    bool b_invalid = (b.mantissa == INT64_MIN);

    // AC-C2: invalid sentinel orders strictly greater than every finite value
    if (a_invalid && b_invalid) {
        return std::strong_ordering::equal;
    }
    if (a_invalid) {
        return std::strong_ordering::greater;
    }
    if (b_invalid) {
        return std::strong_ordering::less;
    }

    // Both finite
    // Step 1: sign comparison
    const bool a_neg = a.mantissa < 0;
    const bool b_neg = b.mantissa < 0;
    if (a_neg != b_neg) {
        return a_neg ? std::strong_ordering::less : std::strong_ordering::greater;
    }

    // R3 fast path (hoisted above canonicalization): at EQUAL raw exponents,
    // value = mantissa × 10^e with 10^e > 0, so value ordering is exactly the signed
    // ordering of the raw mantissas — including equality, and including trailing-zero
    // mantissas at the same stored exponent ({100,-2} vs {120,-2} → less). Reads RAW
    // mantissa/exponent (before strip_zeros), removing both strip_zeros and both
    // digit_count divide-chains from the dominant same-exponent regime. Sentinel is
    // filtered at Step 0 and sign mismatch at Step 1, so both operands are finite and
    // same-signed here. (Values whose raw exponents differ but canonicalize to equal
    // exponents still fall to the Step-4 paths below.)
    if (a.exponent == b.exponent) {
        return a.mantissa <=> b.mantissa;
    }

    // Step 2: canonicalize — strip trailing base-10 zeros from each mantissa
    // (equivalent to increasing exponent while dividing mantissa by 10)
    std::int64_t am = a.mantissa;
    std::int64_t bm = b.mantissa;
    int ae = a.exponent;
    int be = b.exponent;

    auto strip_zeros = [](std::int64_t m, int& e) {
        while (m != 0 && m % 10 == 0) {
            m /= 10;
            ++e;
        }
        return m;
    };
    am = strip_zeros(am, ae);
    bm = strip_zeros(bm, be);

    if (am == 0 && bm == 0) {
        return std::strong_ordering::equal;
    }
    if (am == 0) {
        return b_neg ? std::strong_ordering::greater : std::strong_ordering::less;
    }
    if (bm == 0) {
        return a_neg ? std::strong_ordering::less : std::strong_ordering::greater;
    }

    // Step 3: magnitude bucket — digit_count + exponent gives the integer-part magnitude
    // digit_count(m) = floor(log10(|m|)) + 1
    auto digit_count = [](std::int64_t m) -> int {
        if (m < 0) {
            m = -m;
        }
        int n = 0;
        while (m > 0) {
            m /= 10;
            ++n;
        }
        return n;
    };
    const int a_mag = digit_count(am) + ae;
    const int b_mag = digit_count(bm) + be;

    if (a_mag != b_mag) {
        bool a_greater = (a_mag > b_mag);
        if (a_neg) {
            a_greater = !a_greater;
        }
        return a_greater ? std::strong_ordering::greater : std::strong_ordering::less;
    }

    // Step 4 fast path: same exponent — direct signed-mantissa compare.
    // At equal exponent, value = mantissa × 10^ae, and 10^ae > 0, so the
    // ordering of values is identical to the signed ordering of mantissas
    // for both signs (and sign mismatch is already filtered at Step 1).
    // This is the hot path hammered by BM_decimal_compare and most
    // production traffic; restored after Gate B P1 #1's fix was scoped
    // too broadly (correctness bug only existed when ae != be).
    if (ae == be) {
        if (am == bm) {
            return std::strong_ordering::equal;
        }
        return (am < bm) ? std::strong_ordering::less : std::strong_ordering::greater;
    }

    // Step 4 slow path: same magnitude bucket but different exponents —
    // lexicographic digit-string compare on absolute mantissas per
    // 2a-decimal.md §6.3, with sign flip applied at the end. Operates on
    // |mantissa| (signed compare would conflate sign with magnitude across
    // different scales — see Gate B P1 #1: same-bucket negatives misordered).
    // INT64_MIN already excluded at step 0, so negation is safe.
    char a_digits[19];
    char b_digits[19];
    auto extract_digits = [](std::int64_t m, char buf[19]) -> int {
        if (m < 0) {
            m = -m;
        }
        int n = 0;
        if (m == 0) {
            return n;
        }
        while (m > 0) {
            buf[n++] = static_cast<char>(m % 10);
            m /= 10;
        }
        for (int i = 0, j = n - 1; i < j; ++i, --j) {
            std::swap(buf[i], buf[j]);
        }
        return n;
    };
    const int a_n = extract_digits(am, a_digits);
    const int b_n = extract_digits(bm, b_digits);

    // Shorter string is right-padded with zeros to the longer's length;
    // this aligns the MSDs (same magnitude bucket invariant) and walks
    // toward less-significant digits. Max 19 iterations.
    const int max_n = (a_n > b_n) ? a_n : b_n;
    std::strong_ordering abs_cmp = std::strong_ordering::equal;
    for (int i = 0; i < max_n; ++i) {
        const char da = (i < a_n) ? a_digits[i] : char{0};
        const char db = (i < b_n) ? b_digits[i] : char{0};
        if (da != db) {
            abs_cmp = (da < db) ? std::strong_ordering::less : std::strong_ordering::greater;
            break;
        }
    }

    if (a_neg && abs_cmp != std::strong_ordering::equal) {
        abs_cmp = (abs_cmp == std::strong_ordering::less) ? std::strong_ordering::greater
                                                          : std::strong_ordering::less;
    }
    return abs_cmp;
}

#endif  // FIXPP_TESTS_CORE_DECIMAL_REFERENCE_COMPARE_HPP
