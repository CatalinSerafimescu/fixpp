// tests/core/decimal_mul_u64_wide_test.cpp
// 060-int128-decimal-compare — Phase 4 (US2) T008.
//
// Direct unit test of `mul_u64_wide` (data-model.md "Internal primitive"
// contract): unsigned 64x64->128 widening multiply, returns low 64 bits,
// writes high 64 bits to *hi.
//
// `mul_u64_wide` in src/core/decimal.cpp is `static inline` — TU-local, not
// linkable from a test binary, and the surface must stay invariant (T019:
// no new public/C-ABI symbol). So this file carries a FLAG-IDENTICAL COPY of
// the function (identical #if ladder, identical bare-macro guards, #else
// body copied VERBATIM from src/core/decimal.cpp:264-286) compiled under the
// same compile-time flags as the shipped TU. Because mul_u64_wide is pure,
// stateless, compile-time-selected arithmetic with no external dependency,
// a copy built under the same flags is bit-identical machine code to the
// shipped symbol on that branch.
//
// The SHIPPED symbol is additionally covered end-to-end through compare():
// the default (__int128) branch via the T003 differential-oracle corpus
// (decimal_compare_diff_oracle_test.cpp) exercising production compare();
// the #else portable branch via T013's FIXPP_DECIMAL_FORCE_PORTABLE_MUL=ON
// build of fixpp_core running that same oracle. This test does not replace
// either — it adds dense (hi,lo)-level assertions directly on the widening
// multiply, including hi!=0 products, that the compare()-level corpus can't
// guarantee are actually exercised at the primitive's boundary.
//
// This test binary is compiled TWICE (see tests/core/CMakeLists.txt):
//   - decimal_mul_u64_wide_test           (default: __int128 branch here)
//   - decimal_mul_u64_wide_test_portable  (FIXPP_DECIMAL_FORCE_PORTABLE_MUL
//     defined for this TU: forces the #else schoolbook branch here)
// A compile-time branch marker (kBuiltPortable) is asserted against the
// build's intent so a misconfiguration (both targets silently compiling the
// same branch) is caught rather than passing vacuously.

#include <gtest/gtest.h>

#include <cstdint>
#include <random>

namespace {

// ─────────────────────────────────────────────────────────────────────────
// Flag-identical copy of src/core/decimal.cpp mul_u64_wide (see file header
// comment). Selection order and bare-macro guards match the shipped copy
// exactly. The #else body below is copied VERBATIM from
// src/core/decimal.cpp:264-286.
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

// Branch marker: true iff THIS TU actually selected the #else schoolbook
// path (i.e. FIXPP_DECIMAL_FORCE_PORTABLE_MUL was defined so the __int128
// and MSVC intrinsic arms were suppressed). Used to positively confirm which
// branch each of the two ctest targets compiled, per the requirement to
// prove the portable path is actually exercised rather than silently
// skipped (mirrors the "prove RED" discipline for canary tests).
#if defined(__SIZEOF_INT128__) && !defined(FIXPP_DECIMAL_FORCE_PORTABLE_MUL)
constexpr bool kBuiltPortableBranch = false;
#elif defined(_MSC_VER) && defined(_M_X64) && !defined(FIXPP_DECIMAL_FORCE_PORTABLE_MUL)
constexpr bool kBuiltPortableBranch = false;
#elif defined(_MSC_VER) && defined(_M_ARM64) && !defined(FIXPP_DECIMAL_FORCE_PORTABLE_MUL)
constexpr bool kBuiltPortableBranch = false;
#else
constexpr bool kBuiltPortableBranch = true;
#endif

#if defined(FIXPP_DECIMAL_FORCE_PORTABLE_MUL)
constexpr bool kForcePortableDefined = true;
#else
constexpr bool kForcePortableDefined = false;
#endif

// Golden reference: on Linux/Clang unsigned __int128 is always available
// regardless of which branch mul_u64_wide itself takes in this TU — so the
// golden computation is independent of the branch under test and validates
// both the __int128 copy AND the #else schoolbook copy against the same
// ground truth.
struct WideProduct {
    std::uint64_t hi;
    std::uint64_t lo;
};

WideProduct golden_wide_mul(std::uint64_t a, std::uint64_t b) {
    unsigned __int128 g = static_cast<unsigned __int128>(a) * b;
    WideProduct r;
    r.hi = static_cast<std::uint64_t>(g >> 64);
    r.lo = static_cast<std::uint64_t>(g);
    return r;
}

void check(std::uint64_t a, std::uint64_t b) {
    const WideProduct golden = golden_wide_mul(a, b);
    std::uint64_t hi = 0xFEEDFACEDEADBEEFULL;  // poison, must be overwritten
    const std::uint64_t lo = mul_u64_wide(a, b, &hi);
    ASSERT_EQ(hi, golden.hi) << "a=" << a << " b=" << b;
    ASSERT_EQ(lo, golden.lo) << "a=" << a << " b=" << b;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────
// Branch-marker test: proves which #if arm THIS compiled binary took, so a
// build misconfiguration where both ctest targets compile the same branch
// is caught rather than passing vacuously.
TEST(MulU64WideBranchMarker, MatchesForcePortableDefine) {
    EXPECT_EQ(kBuiltPortableBranch, kForcePortableDefined)
        << "kBuiltPortableBranch=" << kBuiltPortableBranch
        << " kForcePortableDefined=" << kForcePortableDefined
        << " — this TU's selected #if arm must match whether "
           "FIXPP_DECIMAL_FORCE_PORTABLE_MUL was defined for it.";
}

// ─────────────────────────────────────────────────────────────────────────
// Boundary inputs.
TEST(MulU64WideTest, BoundaryValues) {
    const std::uint64_t kU64Max = 0xFFFFFFFFFFFFFFFFULL;
    const std::uint64_t kI64Max = 0x7FFFFFFFFFFFFFFFULL;
    const std::uint64_t kPow2_32 = 0x100000000ULL;
    const std::uint64_t k1e18 = 1000000000000000000ULL;

    const std::uint64_t values[] = {
        0ULL, 1ULL, 2ULL, 3ULL,
        kPow2_32 - 1, kPow2_32, kPow2_32 + 1,
        kI64Max, kI64Max + 1,  // 2^63
        kU64Max - 1, kU64Max,
        k1e18, k1e18 - 1, k1e18 + 1,
        10ULL, 100ULL, 1000ULL, 99ULL,
    };
    for (std::uint64_t a : values) {
        for (std::uint64_t b : values) {
            check(a, b);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────
// Deterministic random pairs (seed=42, matching the repo's oracle
// convention — decimal_compare_diff_oracle_test.cpp).
TEST(MulU64WideTest, DeterministicRandom) {
    std::mt19937_64 rng(42);
    std::uniform_int_distribution<std::uint64_t> dist(
        0ULL, 0xFFFFFFFFFFFFFFFFULL);
    for (int i = 0; i < 5000; ++i) {
        const std::uint64_t a = dist(rng);
        const std::uint64_t b = dist(rng);
        check(a, b);
    }
}

// ─────────────────────────────────────────────────────────────────────────
// Products that cross 2^64 — explicitly assert hi != 0 to prove the high
// limb is real and actually consulted, not incidentally zero.
TEST(MulU64WideTest, ProductsCross2Pow64_HiNonZero) {
    struct Case {
        std::uint64_t a;
        std::uint64_t b;
    };
    const Case cases[] = {
        {99ULL, 9223372036854775807ULL},              // R5 witness product (~9.9e19)
        {0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL},  // UINT64_MAX^2
        {1ULL << 40, 1ULL << 40},                       // 2^80
        {1000000000000000000ULL, 100ULL},               // 1e20
        {0x100000000ULL, 0x100000001ULL},                // just over 2^64
    };
    for (Case const& c : cases) {
        const WideProduct golden = golden_wide_mul(c.a, c.b);
        ASSERT_NE(golden.hi, 0ULL)
            << "test-setup error: golden hi is zero for a=" << c.a
            << " b=" << c.b << " — case does not actually cross 2^64";

        std::uint64_t hi = 0;
        const std::uint64_t lo = mul_u64_wide(c.a, c.b, &hi);
        EXPECT_NE(hi, 0ULL) << "a=" << c.a << " b=" << c.b;
        EXPECT_EQ(hi, golden.hi) << "a=" << c.a << " b=" << c.b;
        EXPECT_EQ(lo, golden.lo) << "a=" << c.a << " b=" << c.b;
    }
}
