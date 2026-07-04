// tests/core/decimal_compare_diff_oracle_test.cpp
// 060-int128-decimal-compare — Phase 2 (Foundational) T003.
//
// Differential-oracle harness: `reference_compare` below is a FROZEN verbatim
// copy of today's `decimal_traits<pod_decimal>::compare` body (the pre-T005,
// digit-string comparator). It exists so that after T005 replaces the
// different-exponent slow path in src/core/decimal.cpp with the wide-integer
// compare, this file can assert the new implementation produces bit-identical
// std::strong_ordering results over a deterministic, boundary-biased corpus.
//
// On THIS tree (pre-T005), reference_compare IS the production algorithm, so
// the assertions below pass trivially — that is expected and correct
// (characterization-first, not red-first; see tasks.md T003 + Notes). The
// oracle becomes discriminating only once T005 lands.

#include <gtest/gtest.h>

#include <algorithm>
#include <compare>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

#include "fixpp/core/decimal.hpp"
#include "decimal_reference_compare.hpp"

using fixpp::core::pod_decimal;
using fixpp::core::pod_decimal_invalid;
using fixpp::core::decimal_traits;

namespace {

// ─────────────────────────────────────────────────────────────────────────
// Deterministic, boundary-biased value pool + pairing.
//
// Seed 42 — matches the Python oracle's seed convention
// (tests/oracle/decimal_compare_oracle_test.py, seam-#8, seed=42; research.md
// R4). Fixed seed => the corpus is reproducible across runs/CI.
// ─────────────────────────────────────────────────────────────────────────
constexpr std::uint64_t kSeed = 42;
constexpr std::size_t kPoolFillSize = 3000;   // uniform-random fill beyond the boundary set
constexpr std::size_t kPairCount = 8000;      // random pairs drawn from the pool

// Builds a value pool biased toward digit-count boundaries (10^d - 1 and 10^d
// for d = 0..18, and the ±INT64_MAX / ±(INT64_MIN+1) extremes), crossed with a
// representative exponent set spanning the canonical domain [-38, 0] PLUS
// out-of-domain int8 exponents (totality per data-model.md / R1). The pool is
// then topped up with fully uniform-random (mantissa, exponent) draws over the
// whole domain (mantissa excludes INT64_MIN — the sentinel is tested
// separately; exponent spans the full int8 range).
std::vector<pod_decimal> make_value_pool(std::mt19937_64& rng) {
    std::vector<pod_decimal> pool;

    // Representative exponents: canonical domain samples + out-of-domain int8
    // extremes/mid-values, to exercise totality beyond [-38, 0].
    std::vector<int> const exponents = {
        -38, -19, -18, -10, -5, -1, 0,              // canonical domain
        1, 5, 10, 50, 100, -50, -100, -127, 127, -128  // out-of-domain int8
    };

    // Digit-count boundary mantissas: 10^d and 10^d - 1 for d = 0..18, both
    // signs (10^18 is the largest power of ten representable in int64;
    // 10^19 would overflow).
    std::vector<std::int64_t> mantissas;
    std::int64_t p = 1;
    for (int d = 0; d <= 18; ++d) {
        mantissas.push_back(p);
        mantissas.push_back(-p);
        if (p > 1) {
            mantissas.push_back(p - 1);
            mantissas.push_back(-(p - 1));
        }
        if (p > INT64_MAX / 10) {
            break;  // next *= 10 would overflow; d==18 (10^18) is the last safe step
        }
        p *= 10;
    }
    mantissas.push_back(INT64_MAX);
    mantissas.push_back(INT64_MAX - 1);
    mantissas.push_back(-INT64_MAX);
    mantissas.push_back(-(INT64_MAX - 1));
    mantissas.push_back(INT64_MIN + 1);  // most-negative finite value (NOT the sentinel)
    mantissas.push_back(0);

    // Cartesian cross of boundary mantissas × representative exponents.
    for (auto m : mantissas) {
        for (auto e : exponents) {
            pool.push_back(pod_decimal{m, static_cast<std::int8_t>(e)});
        }
    }

    // Top up with uniform-random (mantissa, exponent) draws over the full
    // domain. mantissa excludes INT64_MIN (the sentinel, tested separately);
    // exponent spans the full int8 range (including out-of-domain values).
    std::uniform_int_distribution<std::int64_t> mantissa_dist(INT64_MIN + 1, INT64_MAX);
    std::uniform_int_distribution<int> exponent_dist(-128, 127);
    while (pool.size() < kPoolFillSize) {
        pool.push_back(pod_decimal{mantissa_dist(rng), static_cast<std::int8_t>(exponent_dist(rng))});
    }

    return pool;
}

}  // namespace

// Corpus gate: for every generated pair, the production comparator must
// agree bit-for-bit with the frozen reference. Trivially true pre-T005
// (reference_compare == production compare); becomes discriminating once
// T005 swaps the different-exponent slow path for the wide-integer compare.
TEST(DecimalCompareDiffOracle, CorpusMatchesReference) {
    std::mt19937_64 rng(kSeed);
    std::vector<pod_decimal> const pool = make_value_pool(rng);
    ASSERT_GE(pool.size(), kPoolFillSize);

    std::uniform_int_distribution<std::size_t> idx_dist(0, pool.size() - 1);
    for (std::size_t i = 0; i < kPairCount; ++i) {
        pod_decimal const a = pool[idx_dist(rng)];
        pod_decimal const b = pool[idx_dist(rng)];

        auto const production = decimal_traits<pod_decimal>::compare(a, b);
        auto const reference = reference_compare(a, b);
        ASSERT_EQ(production, reference)
            << "pair #" << i << " mismatch: a={mantissa=" << a.mantissa
            << ", exponent=" << static_cast<int>(a.exponent) << "} b={mantissa=" << b.mantissa
            << ", exponent=" << static_cast<int>(b.exponent) << "}";
    }
}

// Explicit sentinel-pair gate (kept separate from the random mantissa draws,
// which never generate INT64_MIN, per T003's brief).
TEST(DecimalCompareDiffOracle, SentinelPairsMatchReference) {
    std::vector<pod_decimal> const values = {
        pod_decimal_invalid,
        pod_decimal{0, 0},
        pod_decimal{1, -38},
        pod_decimal{INT64_MAX, 0},
        pod_decimal{-INT64_MAX, -38},
        pod_decimal{INT64_MIN + 1, -38},
    };
    for (auto const& a : values) {
        for (auto const& b : values) {
            auto const production = decimal_traits<pod_decimal>::compare(a, b);
            auto const reference = reference_compare(a, b);
            ASSERT_EQ(production, reference)
                << "sentinel pair mismatch: a={mantissa=" << a.mantissa
                << ", exponent=" << static_cast<int>(a.exponent) << "} b={mantissa=" << b.mantissa
                << ", exponent=" << static_cast<int>(b.exponent) << "}";
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// T007 — Directed witness matrix (research.md R5 / data-model.md, the
// corrected 7-row matrix). Each row asserts the NAMED, hand-derived expected
// std::strong_ordering DIRECTLY (per [feedback_witness_asserts_named_postcondition_not_proxy]
// — a `== reference_compare(...)` check alone is a weaker proxy that would
// stay green even if BOTH the production comparator and this frozen reference
// shared the same latent bug). `reference_compare` is asserted as a SECONDARY
// check on every witness: if a hand-derivation here is wrong, the concrete
// EXPECT_EQ fails while the reference-agreement EXPECT_EQ still passes,
// pointing at a test bug rather than a production regression; if `compare`
// itself regressed, both fail.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// Test-local sign-inversion helper for std::strong_ordering, used by the
// antisymmetry / transitivity property checks below. NOT the production
// `invert` lambda (private to src/core/decimal.cpp `compare`) — a
// from-scratch equivalent written independently for the test.
std::strong_ordering invert_order(std::strong_ordering o) noexcept {
    if (o == std::strong_ordering::less) {
        return std::strong_ordering::greater;
    }
    if (o == std::strong_ordering::greater) {
        return std::strong_ordering::less;
    }
    return std::strong_ordering::equal;
}

// Asserts both the concrete expected ordering (primary) and agreement with
// the frozen reference oracle (secondary diagnostic).
void expect_order(pod_decimal const& a, pod_decimal const& b, std::strong_ordering expected,
                   char const* label) {
    auto const production = decimal_traits<pod_decimal>::compare(a, b);
    auto const reference = reference_compare(a, b);
    EXPECT_EQ(production, expected)
        << label << ": a={mantissa=" << a.mantissa << ", exponent=" << static_cast<int>(a.exponent)
        << "} b={mantissa=" << b.mantissa << ", exponent=" << static_cast<int>(b.exponent) << "}";
    EXPECT_EQ(production, reference) << label << " (production disagrees with frozen reference)";
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────
// T009 mutation-test kill table (tasks.md T009 / research.md R5).
//
// Method: each mutant below was applied one at a time to src/core/decimal.cpp
// (a pristine copy was kept outside the tree and restored + md5-verified
// after every mutant; final restored state re-verified byte-identical and
// GREEN before this comment was written — see the T009 implementer report).
// Each row records whether >=1 witness in this file FAILED (killed) and, for
// mutants that read src/core/decimal.cpp:293 kPow10[19] (size 19, valid
// indices [0,18]) out of bounds, whether the OOB was ALSO confirmed via
// build/linux-clang-asan (global-buffer-overflow) in addition to (or instead
// of) a clean GoogleTest assertion failure under build/linux-clang-debug.
//
// | # | Mutant (edit)                                              | Outcome | Detection mode (debug / asan)                                    |
// |---|--------------------------------------------------------------|---------|-------------------------------------------------------------------|
// | 1 | drop `hi != 0`: `(hi != 0 || lo > other)` -> `(lo > other)`   | KILLED  | clean assert-fail: CorpusMatchesReference, WitnessHiLimbCrosses2Pow64, PropertyTransitivity. No OOB (in-bounds edit). |
// | 2 | guard `k >= 19` -> `k >= 21`                                  | KILLED  | debug: clean assert-fail (CorpusMatchesReference, PropertyTransitivity) — WitnessKBoundary itself did NOT fail (its k=20 cell's `mag_scaled=1` happened to still compare `greater` against the OOB-read `kPow10[20]` garbage value — a "lucky" non-discriminating cell for THIS guard value). asan: CONFIRMED global-buffer-overflow at decimal.cpp:416 (kPow10[19]/[20] OOB read, k in [19,20] now falls to the multiply arm). |
// | 3 | guard `k >= 19` -> `k >= 40`                                  | KILLED  | debug: clean assert-fail (CorpusMatchesReference, PropertyTransitivity). asan: CONFIRMED global-buffer-overflow at decimal.cpp:416 (k in [19,39] falls to the multiply arm, kPow10[k] OOB for k>18). |
// | 4 | `a_scales = ae > be` -> `ae < be`                             | KILLED  | debug: clean assert-fail (CorpusMatchesReference, WitnessCanonicalizationEquality, WitnessHiLimbCrosses2Pow64, WitnessKBoundary, WitnessExtremes, WitnessOutOfDomainExponents, PropertyTransitivity — 7/9 witnesses). asan: CONFIRMED global-buffer-overflow at decimal.cpp:416 (k becomes negative for every diff-exponent pair -> kPow10[negative index], OOB before the array start). |
// | 5 | `kPow10[k]` -> `kPow10[k - 1]` (stays in [0,17], no OOB since k>=1 in the else branch) | KILLED | clean assert-fail: CorpusMatchesReference, WitnessCanonicalizationEquality. No OOB (in-bounds edit). |
// | 6 | end sign-flip: `return a_neg ? invert(mag_cmp) : mag_cmp;` -> `return mag_cmp;` | KILLED | clean assert-fail: CorpusMatchesReference, WitnessHiLimbCrosses2Pow64, WitnessKBoundary, WitnessExtremes (the named target witness), WitnessOutOfDomainExponents, PropertyTransitivity. No OOB (in-bounds edit). |
// | 7 | guard `k >= 19` -> `k >= 20` (tasks.md/R1's "accepted no-kill")| **KILLED — result-equivalent but OOB read at kPow10[19] (memory-safety)** | debug: clean assert-fail (CorpusMatchesReference, PropertyTransitivity) — WitnessKBoundary's new k=19 cell is NOT a killer here (its comparison reads uninitialized/garbage past `kPow10`'s end under mutant 7, a UB coin-flip; see note below). asan: CONFIRMED `global-buffer-overflow` at decimal.cpp:416 (`kPow10[19]`, one past the last valid index 18) — see re-measurement note below. |
//
// **Mutant 7 finding, corrected (follow-up to the original T009 pass — the
// prior version of this note mis-stated the mechanism as "wrong answers";
// it is not):** mutant 7 (`k >= 19` -> `k >= 20`) is RESULT-EQUIVALENT — at
// k=19 the dominance arm returns "scaled side greater", and a hypothetical
// `kPow10[19] = 10^19` (which fits uint64_t: 10^19 < 2^64) would compute the
// same "greater" via the multiply arm. The ordering the mutant would produce
// is therefore not wrong. The ONLY thing wrong with `>= 20` is that `kPow10`
// is sized EXACTLY to the `>= 19` guard (19 entries, valid indices [0,18]),
// so under the loosened guard `k == 19` reaches the multiply arm and reads
// `kPow10[19]` OUT OF BOUNDS — a memory-safety violation, confirmed as a
// `global-buffer-overflow` under AddressSanitizer (see the re-measurement
// note immediately below the table for the exact captured ASan output).
// Guard 19 and `kPow10`'s size are JOINTLY load-bearing for memory safety,
// not merely for a semantic boundary choice. This SUPERSEDES the ex-ante
// "`19 -> 20` is an accepted semantically-equivalent no-kill" prediction in
// research.md R1 — measurement beat prediction: the mutant IS killed, just
// not by a wrong-answer assertion — it is killed by the ASan lane detecting
// the OOB read. (The original T009 pass additionally flagged that the
// directed WitnessKBoundary matrix had no k=19 cell, relying instead on the
// seed=42 corpus/property tests, whose fixed exponent pool at line ~216
// happens to include both `-19` and `0`, to kill this mutant on the debug
// lane. That gap in DIRECTED coverage is now closed: WitnessKBoundary has a
// dedicated k=19 cell above, asserting the CORRECT ordering at the boundary
// directly on TODAY'S CORRECT CODE. That cell is CORRECTNESS coverage, not a
// mutant-7 killer: under mutant 7 its own comparison reads `kPow10[19]` OOB
// garbage instead of the corpus/property tests' pairs, so whether it happens
// to still report "greater" is a UB coin-flip, not a reliable kill. The
// memory-safety kill for mutant 7 is delivered by the ASan lane, not by this
// or any other GoogleTest assertion.)
//
// Re-measurement (this follow-up pass, mutant 7 applied in isolation to a
// pristine tree, /tmp/decimal_pristine_m7.cpp kept outside the tree and
// restored + diffed byte-identical + md5-verified afterward): built and ran
// DecimalCompareDiffOracle.* under build/linux-clang-asan. AddressSanitizer
// reported (captured verbatim, PID/addresses vary run-to-run, mechanism
// does not):
//
//   ==138519==ERROR: AddressSanitizer: global-buffer-overflow on address
//   0x5e457686e018 at pc 0x5e457676fae8 bp 0x7fff06ab6c90 sp 0x7fff06ab6c88
//   READ of size 8 at 0x5e457686e018 thread T0
//     #0 ... fixpp::core::decimal_traits<pod_decimal>::compare(pod_decimal
//        const&, pod_decimal const&) src/core/decimal.cpp:416:59
//   ...
//   0x5e457686e018 is located 0 bytes after global variable
//   'fixpp::core::kPow10' defined in
//   '.../src/core/decimal.cpp:293' (0x5e457686df80) of size 152
//   SUMMARY: AddressSanitizer: global-buffer-overflow
//   src/core/decimal.cpp:416:59 in
//   fixpp::core::decimal_traits<pod_decimal>::compare(...)
//
// 152 bytes = 19 entries * 8 bytes/uint64_t — i.e. the OOB read lands
// exactly 0 bytes after `kPow10`'s last byte, confirming the read is
// `kPow10[19]` (one past the last valid index 18), hit on the first k=19
// pair CorpusMatchesReference's seed=42 draw encounters (the OOB is
// unconditional at k=19, not conditional on a specific pair's values — every
// k=19 pair anywhere in the corpus would trigger it; this run happened to
// hit one before CorpusMatchesReference finished, aborting the whole
// process before any later test — including the new WitnessKBoundary k=19
// cell above — got to run at all). Confirms the mechanism exactly as
// stated: k=19 under the loosened guard reads `kPow10[19]`, one index past
// the last valid entry (18).
// ─────────────────────────────────────────────────────────────────────────

// Row 1 — canonicalization-equality: values that canonicalize to the same
// magnitude at different (mantissa, exponent) representations must compare
// equal, in both argument orders, for both signs.
TEST(DecimalCompareDiffOracle, WitnessCanonicalizationEquality) {
    expect_order(pod_decimal{100, -2}, pod_decimal{1, 0}, std::strong_ordering::equal, "100e-2 vs 1e0");
    expect_order(pod_decimal{1, 0}, pod_decimal{100, -2}, std::strong_ordering::equal, "1e0 vs 100e-2 (reversed)");

    expect_order(pod_decimal{1000, -3}, pod_decimal{10, -1}, std::strong_ordering::equal, "1000e-3 vs 10e-1");
    expect_order(pod_decimal{10, -1}, pod_decimal{1000, -3}, std::strong_ordering::equal,
                 "10e-1 vs 1000e-3 (reversed)");

    expect_order(pod_decimal{-100, -2}, pod_decimal{-1, 0}, std::strong_ordering::equal, "-100e-2 vs -1e0");
    expect_order(pod_decimal{-1, 0}, pod_decimal{-100, -2}, std::strong_ordering::equal,
                 "-1e0 vs -100e-2 (reversed)");
}

// Row 2 — THE soundness-critical witness: the scaled product crosses 2^64,
// so the `hi` limb of mul_u64_wide is non-zero and MUST be consulted.
//
// {99,0} vs {9223372036854775807,-18}: k=18 (multiply arm, not the k>=19
// dominance arm), scaled product 99 * 10^18 = 9.9e19. 2^64 = 18446744073709551616
// (~1.8447e19). 9.9e19 >= 2^64, so `hi = 5` (nonzero) — a caller that drops
// `hi` and looks only at the low 64 bits sees a narrowed/wrapped `lo` and can
// derive the wrong ordering.
//
// Why -18 (product >= 2^64), NOT the superseded design-note -17 (product =
// 9.9e18 < 2^64, `hi = 0`): research.md R5 row 2's correction note explains
// design-note §6 row 2 / §2 / Risk #4 used `2^63` (the *signed* int64
// overflow point) and mantissa `...-17`, whose product leaves `hi = 0` — so
// the drop-`hi` mutant SURVIVES that weaker witness (dropping an
// already-zero `hi` changes nothing). Because the shipped `mul_u64_wide` is
// UNSIGNED, the `hi`-limb consequence only begins at `2^64`, not `2^63`. Do
// NOT "restore" this witness to `-17`/`2^63` — that would silently make it
// non-discriminating again.
TEST(DecimalCompareDiffOracle, WitnessHiLimbCrosses2Pow64) {
    pod_decimal const big99{99, 0};
    pod_decimal const scaled_max{INT64_MAX, -18};

    // 99 > INT64_MAX * 10^-18 (~9.223) once the hi-limb is honored.
    expect_order(big99, scaled_max, std::strong_ordering::greater, "99e0 vs INT64_MAX e-18");
    expect_order(scaled_max, big99, std::strong_ordering::less, "INT64_MAX e-18 vs 99e0 (reversed)");

    // Negated pair: sign flip reverses the ordering.
    pod_decimal const neg_big99{-99, 0};
    pod_decimal const neg_scaled_max{-INT64_MAX, -18};
    expect_order(neg_big99, neg_scaled_max, std::strong_ordering::less, "-99e0 vs -INT64_MAX e-18");
    expect_order(neg_scaled_max, neg_big99, std::strong_ordering::greater,
                 "-INT64_MAX e-18 vs -99e0 (reversed)");
}

// Row 3 — k-boundary: k=18 (last multiply-arm value, no dominance shortcut),
// k=19 (the exact dominance-guard boundary — the first value the `k >= 19`
// check routes to the no-multiply arm; added as a follow-up to the T009
// mutation test, which found the matrix previously jumped straight from
// k=18 to k=20 with no cell discriminating k=19 itself — see the kill-table
// mutant-7 row below), k=20 (just above the k>=19 dominance guard, near
// INT64_MAX so the guard itself — not the actual magnitudes — is the
// deciding factor), k=38 (full canonical-domain span, exponents 0 and -38).
// Both scale directions and both signs for each k.
TEST(DecimalCompareDiffOracle, WitnessKBoundary) {
    // k = 18 — multiply arm (hi == 0 for these small mantissas; distinct
    // from row 2's hi != 0 witness).
    {
        pod_decimal const a{5, 0};
        pod_decimal const b{7, -18};
        expect_order(a, b, std::strong_ordering::greater, "k=18: 5e0 vs 7e-18");
        expect_order(b, a, std::strong_ordering::less, "k=18: 7e-18 vs 5e0 (reversed)");

        pod_decimal const na{-5, 0};
        pod_decimal const nb{-7, -18};
        expect_order(na, nb, std::strong_ordering::less, "k=18: -5e0 vs -7e-18");
        expect_order(nb, na, std::strong_ordering::greater, "k=18: -7e-18 vs -5e0 (reversed)");
    }

    // k = 19 — the first value of `k` that takes the dominance arm (no
    // multiply); on today's correct code this is intentional and safe
    // (dominance is a valid shortcut for k >= 19). It is ALSO the exact
    // boundary a guard-loosening mutant (`k >= 19` -> `k >= 20`) turns into
    // an out-of-bounds `kPow10[19]` read (kPow10 has only 19 entries, valid
    // indices [0,18]) — see the kill-table mutant-7 row below. This cell
    // asserts the CORRECT ordering at k=19 directly, closing the gap the
    // mutation test surfaced: previously only the seed-42 corpus happened to
    // include a k=19 pair (its fixed exponent pool has both -19 and 0), a
    // fragility this directed cell removes. (The mutant-7 memory-safety kill
    // itself is delivered by the ASan lane, not by this assertion.)
    {
        pod_decimal const a{1, 0};
        pod_decimal const b{INT64_MAX, -19};
        // value_a = 1; value_b = INT64_MAX * 10^-19 = 9223372036854775807e-19
        // ~= 0.9223372036854775807 < 1, so a is greater.
        expect_order(a, b, std::strong_ordering::greater, "k=19: 1e0 vs INT64_MAX e-19");
        expect_order(b, a, std::strong_ordering::less, "k=19: INT64_MAX e-19 vs 1e0 (reversed)");

        pod_decimal const na{-1, 0};
        pod_decimal const nb{-INT64_MAX, -19};
        // Negated: -1 < -0.9223... , so na is less.
        expect_order(na, nb, std::strong_ordering::less, "k=19: -1e0 vs -INT64_MAX e-19");
        expect_order(nb, na, std::strong_ordering::greater, "k=19: -INT64_MAX e-19 vs -1e0 (reversed)");
    }

    // k = 20 — dominance arm (k >= 19), the guard alone decides even though
    // `other`'s raw mantissa is INT64_MAX (astronomically larger digit-wise).
    {
        pod_decimal const a{1, 0};
        pod_decimal const b{INT64_MAX, -20};
        expect_order(a, b, std::strong_ordering::greater, "k=20: 1e0 vs INT64_MAX e-20");
        expect_order(b, a, std::strong_ordering::less, "k=20: INT64_MAX e-20 vs 1e0 (reversed)");

        pod_decimal const na{-1, 0};
        pod_decimal const nb{-INT64_MAX, -20};
        expect_order(na, nb, std::strong_ordering::less, "k=20: -1e0 vs -INT64_MAX e-20");
        expect_order(nb, na, std::strong_ordering::greater, "k=20: -INT64_MAX e-20 vs -1e0 (reversed)");
    }

    // k = 38 — full canonical-domain span (exponents 0 and -38).
    {
        pod_decimal const a{1, 0};
        pod_decimal const b{INT64_MAX, -38};
        expect_order(a, b, std::strong_ordering::greater, "k=38: 1e0 vs INT64_MAX e-38");
        expect_order(b, a, std::strong_ordering::less, "k=38: INT64_MAX e-38 vs 1e0 (reversed)");

        pod_decimal const na{-1, 0};
        pod_decimal const nb{-INT64_MAX, -38};
        expect_order(na, nb, std::strong_ordering::less, "k=38: -1e0 vs -INT64_MAX e-38");
        expect_order(nb, na, std::strong_ordering::greater, "k=38: -INT64_MAX e-38 vs -1e0 (reversed)");
    }
}

// Row 4 — zero-filter ordering/sign. Note: {0,-5} vs {-1,0} actually
// resolves at the Step-1 SIGN filter (a is non-negative, b is negative —
// sign mismatch short-circuits before the zero filter is reached); it is
// included here as the task-specified pair, not as zero-filter coverage.
// The other two pairs genuinely reach the raw-mantissa zero filter (equal
// exponents fail first, then both/one-operand-zero is decided there).
TEST(DecimalCompareDiffOracle, WitnessZeroFilterOrderingAndSign) {
    expect_order(pod_decimal{0, -38}, pod_decimal{0, 0}, std::strong_ordering::equal, "0e-38 vs 0e0");
    expect_order(pod_decimal{0, 0}, pod_decimal{0, -38}, std::strong_ordering::equal, "0e0 vs 0e-38 (reversed)");

    // Sign-filter short-circuit (see comment above) — 0 > any negative value.
    expect_order(pod_decimal{0, -5}, pod_decimal{-1, 0}, std::strong_ordering::greater, "0e-5 vs -1e0");
    expect_order(pod_decimal{-1, 0}, pod_decimal{0, -5}, std::strong_ordering::less, "-1e0 vs 0e-5 (reversed)");

    // Genuine zero-filter path — 0 < any positive value.
    expect_order(pod_decimal{0, 3}, pod_decimal{1, -38}, std::strong_ordering::less, "0e3 vs 1e-38");
    expect_order(pod_decimal{1, -38}, pod_decimal{0, 3}, std::strong_ordering::greater, "1e-38 vs 0e3 (reversed)");
}

// Row 5 — extremes: +/-INT64_MAX and +/-(INT64_MIN+1) at exponent 0 and -38,
// cross-paired. INT64_MIN itself is the sentinel (excluded); INT64_MIN+1 is
// the most-negative finite value, and negating it (-(INT64_MIN+1) ==
// INT64_MAX) is safe — no overflow. These kill a magnitude-negation bug or
// an end-of-function sign-flip omission (the Gate-B P1 #1 same-bucket-
// negatives shape), because same-sign extreme-magnitude operands at
// different exponents force the negate-then-scale-then-flip path.
TEST(DecimalCompareDiffOracle, WitnessExtremes) {
    // Sign-mismatch extremes (trivial via Step 1, kept as a regression pin).
    expect_order(pod_decimal{INT64_MAX, 0}, pod_decimal{INT64_MIN + 1, -38}, std::strong_ordering::greater,
                 "INT64_MAX e0 vs (INT64_MIN+1) e-38");
    expect_order(pod_decimal{INT64_MIN + 1, -38}, pod_decimal{INT64_MAX, 0}, std::strong_ordering::less,
                 "(INT64_MIN+1) e-38 vs INT64_MAX e0 (reversed)");

    // Same-sign (both negative), different magnitudes-at-scale: huge negative
    // at exponent 0 vs tiny negative at exponent -38.
    expect_order(pod_decimal{INT64_MIN + 1, 0}, pod_decimal{-1, -38}, std::strong_ordering::less,
                 "(INT64_MIN+1) e0 vs -1e-38");
    expect_order(pod_decimal{-1, -38}, pod_decimal{INT64_MIN + 1, 0}, std::strong_ordering::greater,
                 "-1e-38 vs (INT64_MIN+1) e0 (reversed)");

    // Same-sign (both negative), SAME magnitude, different exponent: proves
    // negation of the extreme value is applied consistently and the guard
    // dominance still yields the mathematically-correct ordering.
    expect_order(pod_decimal{INT64_MIN + 1, 0}, pod_decimal{INT64_MIN + 1, -38}, std::strong_ordering::less,
                 "(INT64_MIN+1) e0 vs (INT64_MIN+1) e-38");
    expect_order(pod_decimal{INT64_MIN + 1, -38}, pod_decimal{INT64_MIN + 1, 0}, std::strong_ordering::greater,
                 "(INT64_MIN+1) e-38 vs (INT64_MIN+1) e0 (reversed)");

    // Same-sign (both positive), SAME magnitude, different exponent.
    expect_order(pod_decimal{INT64_MAX, -38}, pod_decimal{INT64_MAX, 0}, std::strong_ordering::less,
                 "INT64_MAX e-38 vs INT64_MAX e0");
    expect_order(pod_decimal{INT64_MAX, 0}, pod_decimal{INT64_MAX, -38}, std::strong_ordering::greater,
                 "INT64_MAX e0 vs INT64_MAX e-38 (reversed)");
}

// Row 6 — sentinel pairs (regression — the sentinel-handling path is
// UNCHANGED by this feature).
TEST(DecimalCompareDiffOracle, WitnessSentinelPairs) {
    expect_order(pod_decimal_invalid, pod_decimal_invalid, std::strong_ordering::equal,
                 "invalid vs invalid");
    expect_order(pod_decimal_invalid, pod_decimal{INT64_MAX, 0}, std::strong_ordering::greater,
                 "invalid vs INT64_MAX e0");
    expect_order(pod_decimal{INT64_MAX, 0}, pod_decimal_invalid, std::strong_ordering::less,
                 "INT64_MAX e0 vs invalid (reversed)");
    expect_order(pod_decimal_invalid, pod_decimal{INT64_MIN + 1, -38}, std::strong_ordering::greater,
                 "invalid vs (INT64_MIN+1) e-38");
    expect_order(pod_decimal{0, 0}, pod_decimal_invalid, std::strong_ordering::less, "0e0 vs invalid");
}

// Row 7 — out-of-domain int8 exponents: proves totality holds beyond the
// canonical [-38, 0] domain (int8 exponent field spans [-128, 127]).
TEST(DecimalCompareDiffOracle, WitnessOutOfDomainExponents) {
    expect_order(pod_decimal{5, 7}, pod_decimal{5, 0}, std::strong_ordering::greater, "5e7 vs 5e0");
    expect_order(pod_decimal{5, 0}, pod_decimal{5, 7}, std::strong_ordering::less, "5e0 vs 5e7 (reversed)");

    // delta = 127 - (-128) = 255 > 38 (out-of-domain, but `k` computed in
    // `int` cannot overflow at this magnitude — totality preserved).
    expect_order(pod_decimal{1, 127}, pod_decimal{1, -128}, std::strong_ordering::greater,
                 "1e127 vs 1e-128");
    expect_order(pod_decimal{1, -128}, pod_decimal{1, 127}, std::strong_ordering::less,
                 "1e-128 vs 1e127 (reversed)");

    expect_order(pod_decimal{-1, 127}, pod_decimal{-1, -128}, std::strong_ordering::less,
                 "-1e127 vs -1e-128");
    expect_order(pod_decimal{-1, -128}, pod_decimal{-1, 127}, std::strong_ordering::greater,
                 "-1e-128 vs -1e127 (reversed)");
}

// ═══════════════════════════════════════════════════════════════════════════
// Property checks: antisymmetry + transitivity (research.md R4/R5).
// ═══════════════════════════════════════════════════════════════════════════

// Antisymmetry: compare(a,b) == invert(compare(b,a)) for every sampled pair,
// drawn from the same boundary-biased pool as the corpus gate (deterministic,
// seed 42).
TEST(DecimalCompareDiffOracle, PropertyAntisymmetry) {
    constexpr std::size_t kSampleSize = 500;
    std::mt19937_64 rng(kSeed);
    std::vector<pod_decimal> const pool = make_value_pool(rng);
    ASSERT_GE(pool.size(), kSampleSize);

    std::uniform_int_distribution<std::size_t> idx_dist(0, pool.size() - 1);
    for (std::size_t i = 0; i < kSampleSize; ++i) {
        pod_decimal const a = pool[idx_dist(rng)];
        pod_decimal const b = pool[idx_dist(rng)];

        auto const ab = decimal_traits<pod_decimal>::compare(a, b);
        auto const ba = decimal_traits<pod_decimal>::compare(b, a);
        EXPECT_EQ(ab, invert_order(ba))
            << "antisymmetry #" << i << " violated: a={mantissa=" << a.mantissa
            << ", exponent=" << static_cast<int>(a.exponent) << "} b={mantissa=" << b.mantissa
            << ", exponent=" << static_cast<int>(b.exponent) << "}";
    }
}

// Transitivity: sort a deterministic sample by `compare` (via the
// `less`-only projection), then assert compare(sorted[i], sorted[k]) is
// NEVER `greater` for every i < k in the sorted order (i.e. the sort's own
// transitive closure holds under the real 3-way comparator, not just the
// 2-way `less` projection used to sort). Sorting first (rather than sampling
// random triples and hoping a<=b<=c holds) guarantees the transitive
// conclusion is actually exercised on every triple, not vacuously true on a
// rarely-satisfied antecedent.
TEST(DecimalCompareDiffOracle, PropertyTransitivity) {
    constexpr std::size_t kSortedSampleSize = 60;
    std::mt19937_64 rng(kSeed + 1);  // distinct stream from the antisymmetry test
    std::vector<pod_decimal> const pool = make_value_pool(rng);
    ASSERT_GE(pool.size(), kSortedSampleSize);

    std::uniform_int_distribution<std::size_t> idx_dist(0, pool.size() - 1);
    std::vector<pod_decimal> sample;
    sample.reserve(kSortedSampleSize);
    for (std::size_t i = 0; i < kSortedSampleSize; ++i) {
        sample.push_back(pool[idx_dist(rng)]);
    }

    std::sort(sample.begin(), sample.end(), [](pod_decimal const& x, pod_decimal const& y) {
        return decimal_traits<pod_decimal>::compare(x, y) == std::strong_ordering::less;
    });

    for (std::size_t i = 0; i < sample.size(); ++i) {
        for (std::size_t k = i + 1; k < sample.size(); ++k) {
            auto const cmp_ik = decimal_traits<pod_decimal>::compare(sample[i], sample[k]);
            EXPECT_NE(cmp_ik, std::strong_ordering::greater)
                << "transitivity violated: sorted[" << i << "]={mantissa=" << sample[i].mantissa
                << ", exponent=" << static_cast<int>(sample[i].exponent) << "} > sorted[" << k
                << "]={mantissa=" << sample[k].mantissa
                << ", exponent=" << static_cast<int>(sample[k].exponent) << "}";
        }
    }

    // Additionally exercise the conditional a<=b ∧ b<=c => a<=c form on
    // random (unsorted) triples, drawn from the same deterministic stream, as
    // a secondary check that does not depend on `std::sort`'s own internal
    // comparisons.
    constexpr std::size_t kTripleCount = 300;
    for (std::size_t i = 0; i < kTripleCount; ++i) {
        pod_decimal const a = pool[idx_dist(rng)];
        pod_decimal const b = pool[idx_dist(rng)];
        pod_decimal const c = pool[idx_dist(rng)];

        auto const ab = decimal_traits<pod_decimal>::compare(a, b);
        auto const bc = decimal_traits<pod_decimal>::compare(b, c);
        if (ab != std::strong_ordering::greater && bc != std::strong_ordering::greater) {
            auto const ac = decimal_traits<pod_decimal>::compare(a, c);
            EXPECT_NE(ac, std::strong_ordering::greater)
                << "conditional transitivity #" << i << " violated: a={mantissa=" << a.mantissa
                << ", exponent=" << static_cast<int>(a.exponent) << "} b={mantissa=" << b.mantissa
                << ", exponent=" << static_cast<int>(b.exponent) << "} c={mantissa=" << c.mantissa
                << ", exponent=" << static_cast<int>(c.exponent) << "}";
        }
    }
}
