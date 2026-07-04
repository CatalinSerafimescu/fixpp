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

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace fixpp::core {

// Canonical pod_decimal exponent domain per 2a §4.2: exponent ∈ [kCanonicalExpMin,
// kCanonicalExpMax].
inline constexpr int kCanonicalExpMin = -38;
inline constexpr int kCanonicalExpMax = 0;

// Locale-independent ASCII digit test. std::isdigit is locale-dependent and is
// UB for negative char values — neither acceptable on the FIX-FLOAT parse path.
static constexpr bool is_ascii_digit(char c) noexcept { return c >= '0' && c <= '9'; }

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
    if (!is_ascii_digit(*p)) {
        return std::unexpected{error::decimal_invalid_input};
    }

    // Parse integer digits
    std::int64_t mantissa = 0;
    std::int8_t exponent = 0;
    bool overflow = false;

    while (p != end && is_ascii_digit(*p)) {
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
        if (p == end || !is_ascii_digit(*p)) {
            return std::unexpected{error::decimal_invalid_input};
        }

        while (p != end && is_ascii_digit(*p)) {
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

    // Apply sign. No overflow guard is needed here: the accumulation guard above
    // caps |mantissa| at INT64_MAX, so mantissa ∈ [0, INT64_MAX] before negation.
    if (negative) {
        mantissa = -mantissa;
    }

    // Sentinel collision (INT64_MIN reserved) is unreachable by construction: the
    // load-bearing guard is the accumulation check above — the string
    // "9223372036854775808" (INT64_MAX+1) overflows during accumulation before sign
    // application, so mantissa is always in [-INT64_MAX, INT64_MAX] here (AC-P9,
    // DecimalParse.SentinelCollision). Kept as a fail-closed net (not an assert, which
    // would fail OPEN under NDEBUG and leak the sentinel as a valid value) in case a
    // future refactor weakens the accumulation guard.
    if (mantissa == INT64_MIN) {
        return std::unexpected{error::decimal_overflow};  // LCOV_EXCL_LINE — unreachable, see above
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

    // AC-S3: exponent domain check (must be in [kCanonicalExpMin, kCanonicalExpMax])
    if (v.exponent > kCanonicalExpMax || v.exponent < kCanonicalExpMin) {
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

// ── 060-int128-decimal-compare T004: wide-multiply compare primitives ──────
// Internal, TU-local. Unsigned 64x64->128 widening multiply: returns the low
// 64 bits, writes the high 64 bits to *hi. Total, noexcept, no allocation on
// any #if branch (data-model.md "Internal primitive" mul_u64_wide contract).
// Selection order puts clang-cl (_MSC_VER **and** __SIZEOF_INT128__) on the
// __int128 branch (research.md R2). FIXPP_DECIMAL_FORCE_PORTABLE_MUL is
// defined BARE (no value) by CMakeLists.txt T002 — guard with defined(...),
// never `#if FIXPP_DECIMAL_FORCE_PORTABLE_MUL` (that expands to `#if ` and
// breaks the build when the option is ON).
static inline std::uint64_t mul_u64_wide(std::uint64_t a, std::uint64_t b,
                                          std::uint64_t* hi) noexcept {
#if defined(__SIZEOF_INT128__) && !defined(FIXPP_DECIMAL_FORCE_PORTABLE_MUL)
    unsigned __int128 p = static_cast<unsigned __int128>(a) * b;
    *hi = static_cast<std::uint64_t>(p >> 64);
    return static_cast<std::uint64_t>(p);
#elif defined(_MSC_VER) && defined(_M_X64) && !defined(FIXPP_DECIMAL_FORCE_PORTABLE_MUL)
    return _umul128(a, b, hi);
#elif defined(_MSC_VER) && defined(_M_ARM64) && !defined(FIXPP_DECIMAL_FORCE_PORTABLE_MUL)
    *hi = __umulh(a, b);
    return a * b;
#else
    // Portable 32-bit-limb schoolbook (Hacker's Delight mulhilo): split a, b
    // into hi/lo 32-bit halves, four partial products, combine carries into
    // the full 128-bit product (hi, lo). No 64-bit intermediate overflows.
    const std::uint64_t a_lo = a & 0xFFFFFFFFULL;
    const std::uint64_t a_hi = a >> 32;
    const std::uint64_t b_lo = b & 0xFFFFFFFFULL;
    const std::uint64_t b_hi = b >> 32;

    std::uint64_t t = a_lo * b_lo;
    const std::uint64_t w0 = t & 0xFFFFFFFFULL;
    std::uint64_t k = t >> 32;

    t = a_hi * b_lo + k;
    const std::uint64_t w1 = t & 0xFFFFFFFFULL;
    const std::uint64_t w2 = t >> 32;

    t = a_lo * b_hi + w1;
    k = t >> 32;

    *hi = a_hi * b_hi + w2 + k;
    return (t << 32) | w0;
#endif
}

// kPow10[k] = 10^k for k in [0, 18]; 10^18 < 2^63 fits uint64_t. Only indexed
// for k <= 18 — the k >= 19 dominance guard in compare() caps the index
// before any access (data-model.md kPow10 contract; no 10^19..10^38 entries
// needed).
static constexpr std::uint64_t kPow10[19] = {
    1ULL,
    10ULL,
    100ULL,
    1000ULL,
    10000ULL,
    100000ULL,
    1000000ULL,
    10000000ULL,
    100000000ULL,
    1000000000ULL,
    10000000000ULL,
    100000000000ULL,
    1000000000000ULL,
    10000000000000ULL,
    100000000000000ULL,
    1000000000000000ULL,
    10000000000000000ULL,
    100000000000000000ULL,
    1000000000000000000ULL,
};

// ── T020: compare ────────────────────────────────────────────────────────────
// Same-exponent: direct signed-mantissa compare (R3 hoist, unchanged). Different-
// exponent: exact wide-integer compare per amended 2a §6.3 / research.md R1 —
// k >= 19 magnitude dominance (no multiply) else one mul_u64_wide 64x64->128
// widening multiply + two-limb (hi,lo) compare, sign flip applied at the end.
// noexcept. O(1) — no loops, no divisions.

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

    // Step 2: raw-mantissa zero filter — 0 x 10^e == 0 for any e, so a zero
    // operand compares purely on the other operand's sign, with no scaling
    // needed (data-model.md "Compare control flow (target state)" zero
    // filter). Reads RAW mantissa (no canonicalization).
    if (a.mantissa == 0 && b.mantissa == 0) {
        return std::strong_ordering::equal;
    }
    if (a.mantissa == 0) {
        return b_neg ? std::strong_ordering::greater : std::strong_ordering::less;
    }
    if (b.mantissa == 0) {
        return a_neg ? std::strong_ordering::less : std::strong_ordering::greater;
    }

    // Step 3: exact wide-integer magnitude compare (research.md R1; T004/T005).
    // Both mantissas are nonzero and non-sentinel (INT64_MIN excluded at
    // Step 0) and same-signed (Step 1), so negation to an unsigned magnitude
    // is safe.
    const std::uint64_t mag_a =
        static_cast<std::uint64_t>(a.mantissa < 0 ? -a.mantissa : a.mantissa);
    const std::uint64_t mag_b =
        static_cast<std::uint64_t>(b.mantissa < 0 ? -b.mantissa : b.mantissa);

    // k computed in `int` (not int8_t) so an out-of-canonical-domain exponent
    // difference cannot overflow — the comparator stays total beyond the
    // canonical [-38, 0] domain (data-model.md pod_decimal note).
    const int ae = a.exponent;
    const int be = b.exponent;
    const bool a_scales = ae > be;
    const int k = a_scales ? (ae - be) : (be - ae);

    // mag_scaled is the operand at the LARGER raw exponent — it must be
    // scaled up by 10^k to compare against `other` at equal effective scale.
    const std::uint64_t mag_scaled = a_scales ? mag_a : mag_b;
    const std::uint64_t other = a_scales ? mag_b : mag_a;

    auto invert = [](std::strong_ordering o) {
        if (o == std::strong_ordering::less) {
            return std::strong_ordering::greater;
        }
        if (o == std::strong_ordering::greater) {
            return std::strong_ordering::less;
        }
        return std::strong_ordering::equal;
    };

    std::strong_ordering scaled_vs_other = std::strong_ordering::equal;
    if (k >= 19) {
        // Dominance, no multiply: mag_scaled >= 1 (zero filtered above) so
        // mag_scaled * 10^19 >= 10^19 > INT64_MAX >= other (research.md R1).
        scaled_vs_other = std::strong_ordering::greater;
    } else {
        // mag_scaled <= INT64_MAX < 2^63, kPow10[k] <= 10^18 < 2^60, so the
        // product is < 2^123 and fits the 128-bit (hi, lo) pair exactly
        // (research.md R1 bound proof). `hi` MUST be consulted — dropping it
        // silently narrows to 64 bits (data-model.md mul_u64_wide invariant).
        std::uint64_t hi;
        const std::uint64_t lo = mul_u64_wide(mag_scaled, kPow10[k], &hi);
        scaled_vs_other = (hi != 0 || lo > other)
                              ? std::strong_ordering::greater
                              : (lo == other ? std::strong_ordering::equal
                                             : std::strong_ordering::less);
    }

    // scaled_vs_other compares (mag_scaled*10^k) vs other; if `a` is the
    // scaled side that's already a vs b, else it's b vs a and must be
    // inverted. Sign flip mirrors the original :371-374 disposition.
    const std::strong_ordering mag_cmp = a_scales ? scaled_vs_other : invert(scaled_vs_other);
    return a_neg ? invert(mag_cmp) : mag_cmp;
}

// ── T021: from_pod, to_pod, predicates ──────────────────────────────────────

expected_t<pod_decimal> decimal_traits<pod_decimal>::from_pod(pod_decimal pd) noexcept {
    // Enforce canonical domain per 2a §4.2: exponent ∈ [kCanonicalExpMin, kCanonicalExpMax].
    // INT64_MIN is the invalid sentinel — reject it too.
    if (pd.mantissa == INT64_MIN) {
        return std::unexpected{error::decimal_overflow};
    }
    if (pd.exponent < kCanonicalExpMin || pd.exponent > kCanonicalExpMax) {
        return std::unexpected{error::decimal_overflow};
    }
    return pd;
}

expected_t<pod_decimal> decimal_traits<pod_decimal>::to_pod(pod_decimal const& v) noexcept {
    // Same canonical-domain enforcement as from_pod (identical predicate).
    return from_pod(v);
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
