// src/core/decimal.cpp
// decimal_traits<pod_decimal> implementation — US1 parse/format/compare + pod conversion.
// Also emits decimal_alias_sentinel<FIXPP_DECIMAL_T>::tag definition (US2).

#include "fixpp/core/decimal.hpp"

#include <algorithm>
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory_resource>
#include <span>

#include "fixpp/core/decimal_alias.hpp"
#include "fixpp/core/error.hpp"

namespace fixpp::core {

// ── T019a: from_chars ────────────────────────────────────────────────────────
// Single-pass FIX FLOAT parser.  Grammar:
//   [sign] integer_digits ['.' fractional_digits]
// where bare '.' (no integer or no fractional digits) is invalid.
// No exponent notation ('e'/'E') allowed.

expected_t<pod_decimal> decimal_traits<pod_decimal>::from_chars(
    std::span<const std::byte> src, std::pmr::memory_resource* /*mr*/) noexcept {
    if (src.empty()) {
        return std::unexpected{error::decimal_invalid_input};
    }

    const auto* p = reinterpret_cast<const char*>(src.data());
    const auto* end = p + src.size();

    // Sign
    bool negative = false;
    if (*p == '+') {
        ++p;
    } else if (*p == '-') {
        negative = true;
        ++p;
    }

    if (p == end) {
        return std::unexpected{error::decimal_invalid_input};
    }

    // Must start with a digit (reject bare '.5' etc.)
    if (*p < '0' || *p > '9') {
        return std::unexpected{error::decimal_invalid_input};
    }

    // Parse integer digits
    std::int64_t mantissa = 0;
    std::int8_t exponent = 0;
    bool overflow = false;

    while (p != end && *p >= '0' && *p <= '9') {
        const int digit = *p - '0';
        // Check for multiplication overflow before doing it
        if (!overflow) {
            if (mantissa > (INT64_MAX - digit) / 10) {
                overflow = true;
            } else {
                mantissa = (mantissa * 10) + digit;
            }
        }
        ++p;
    }

    // Fractional part
    if (p != end && *p == '.') {
        ++p;
        // Require at least one digit after '.' (reject '5.')
        if (p == end || *p < '0' || *p > '9') {
            return std::unexpected{error::decimal_invalid_input};
        }

        while (p != end && *p >= '0' && *p <= '9') {
            const int digit = *p - '0';
            if (!overflow) {
                if (exponent <= -38 || mantissa > (INT64_MAX - digit) / 10) {
                    overflow = true;
                } else {
                    mantissa = (mantissa * 10) + digit;
                    --exponent;
                }
            }
            ++p;
        }
    }

    // Any remaining character is invalid (includes SOH, letters, etc.)
    if (p != end) {
        return std::unexpected{error::decimal_invalid_input};
    }

    if (overflow) {
        return std::unexpected{error::decimal_overflow};
    }

    // Apply sign
    if (negative) {
        if (mantissa > static_cast<std::int64_t>(INT64_MAX)) {
            return std::unexpected{error::decimal_overflow};
        }
        mantissa = -mantissa;
    }

    // Sentinel collision: INT64_MIN is reserved for the invalid sentinel
    if (mantissa == INT64_MIN) {
        return std::unexpected{error::decimal_overflow};
    }

    return pod_decimal{.mantissa = mantissa, .exponent = exponent};
}

// ── T019b: to_chars ──────────────────────────────────────────────────────────
// Serializes a pod_decimal into FIX FLOAT ASCII bytes.

expected_t<std::size_t> decimal_traits<pod_decimal>::to_chars(pod_decimal const& v,
                                                              std::span<std::byte> dst) noexcept {
    // AC-S1: sentinel check
    if (v.mantissa == INT64_MIN) {
        return std::unexpected{error::decimal_invalid_input};
    }

    // AC-S3: exponent domain check (must be in [-38, 0])
    if (v.exponent > 0 || v.exponent < -38) {
        return std::unexpected{error::decimal_invalid_input};
    }

    // AC-S2: zero shortcut
    if (v.mantissa == 0) {
        if (dst.empty()) {
            return std::unexpected{error::decimal_buffer_too_small};
        }
        dst[0] = std::byte{'0'};
        return std::size_t{1};
    }

    // Work in a local buffer (worst case 41 bytes: sign + "0." + 19+19 digits)
    char buf[44]{};
    char* w = buf;

    std::int64_t m = v.mantissa;
    std::int8_t e = v.exponent;

    // Handle negative
    const bool neg = m < 0;
    if (neg) {
        m = -m;
    }  // safe: we excluded INT64_MIN above

    // Produce digits of |m| into a temporary digit buffer (up to 19 digits)
    char digits[20]{};
    int ndigits = 0;
    {
        std::int64_t tmp = m;
        if (tmp == 0) {
            digits[ndigits++] = '0';
        } else {
            while (tmp > 0) {
                digits[ndigits++] = static_cast<char>('0' + (tmp % 10));
                tmp /= 10;
            }
            // digits is reversed; reverse it
            for (int i = 0, j = ndigits - 1; i < j; ++i, --j) {
                std::swap(digits[i], digits[j]);
            }
        }
    }

    // AC-S4: strip trailing fractional zeros
    int frac_digits = -static_cast<int>(e);  // number of fractional digits in m
    while (frac_digits > 0 && ndigits > 0 && digits[ndigits - 1] == '0') {
        --ndigits;
        --frac_digits;
        // e effectively becomes one less negative
    }
    // (If all fractional digits were zeros and mantissa is now zero,
    //  the zero-shortcut above already handled it.)

    // Write sign
    if (neg) {
        *w++ = '-';
    }

    if (frac_digits == 0) {
        // Integer: just the digits
        for (int i = 0; i < ndigits; ++i) {
            *w++ = digits[i];
        }
    } else if (frac_digits >= ndigits) {
        // Pure fraction: "0.000...digits"
        *w++ = '0';
        *w++ = '.';
        const int leading_zeros = frac_digits - ndigits;
        for (int i = 0; i < leading_zeros; ++i) {
            *w++ = '0';
        }
        for (int i = 0; i < ndigits; ++i) {
            *w++ = digits[i];
        }
    } else {
        // Mixed: integer part + '.' + fractional part
        const int int_digits = ndigits - frac_digits;
        for (int i = 0; i < int_digits; ++i) {
            *w++ = digits[i];
        }
        *w++ = '.';
        for (int i = int_digits; i < ndigits; ++i) {
            *w++ = digits[i];
        }
    }

    auto len = static_cast<std::size_t>(w - buf);
    if (dst.size() < len) {
        return std::unexpected{error::decimal_buffer_too_small};
    }
    std::memcpy(dst.data(), buf, len);
    return len;
}

// ── T020: compare ────────────────────────────────────────────────────────────
// Digit-string comparison per 2a §6.3 / research.md D-5.
// No __int128. O(≤19 iterations). noexcept.

std::strong_ordering decimal_traits<pod_decimal>::compare(pod_decimal const& a,
                                                          pod_decimal const& b) noexcept {
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

// ── T021: from_pod, to_pod, predicates ──────────────────────────────────────

expected_t<pod_decimal> decimal_traits<pod_decimal>::from_pod(pod_decimal pd) noexcept {
    // Enforce canonical domain per 2a §4.2: exponent ∈ [-38, 0].
    // INT64_MIN is the invalid sentinel — reject it too.
    if (pd.mantissa == INT64_MIN) {
        return std::unexpected{error::decimal_overflow};
    }
    if (pd.exponent < -38 || pd.exponent > 0) {
        return std::unexpected{error::decimal_overflow};
    }
    return pd;
}

expected_t<pod_decimal> decimal_traits<pod_decimal>::to_pod(pod_decimal const& v) noexcept {
    // Enforce canonical domain per 2a §4.2: exponent ∈ [-38, 0].
    // INT64_MIN is the invalid sentinel.
    if (v.mantissa == INT64_MIN) {
        return std::unexpected{error::decimal_overflow};
    }
    if (v.exponent < -38 || v.exponent > 0) {
        return std::unexpected{error::decimal_overflow};
    }
    return v;
}

bool decimal_traits<pod_decimal>::is_finite(pod_decimal const& v) noexcept {
    return v.mantissa != INT64_MIN;
}

bool decimal_traits<pod_decimal>::is_zero(pod_decimal const& v) noexcept { return v.mantissa == 0; }

bool decimal_traits<pod_decimal>::is_negative(pod_decimal const& v) noexcept {
    return v.mantissa < 0;
}

}  // namespace fixpp::core

// ── US2 (T027): decimal_alias_sentinel<FIXPP_DECIMAL_T> definition ──────────
// Exactly one specialization per library build. Any consumer built with a different
// FIXPP_DECIMAL_T references a specialization the library never defined → link error.

template <>
char const fixpp::detail::decimal_alias_sentinel<FIXPP_DECIMAL_T>::tag = 0;
