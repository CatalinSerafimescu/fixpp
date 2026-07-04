# Feature Specification: Exact wide-integer cross-exponent decimal compare (C1 / int128)

**Feature Branch**: `060-int128-decimal-compare`
**Created**: 2026-07-04
**Status**: Draft
**Input**: User description: "C1 — exact int128 wide-multiply cross-exponent decimal compare (default replacement, semantics-preserving speed swap). REPLACE the different-exponent slow path of `decimal_traits<pod_decimal>::compare` with a branch-free exact wide-integer compare … bit-identical `strong_ordering` … contract amendment reversing the Gate-A no-`__int128` decision … differential-oracle-verified … MSVC portability lane."

## Overview

`decimal_traits<pod_decimal>::compare` is on the decimal hot path: profiling (phase-9,
`01-hotpath-diagnosis.md §3`) measured it at ~23.5% of `decimal_bench` (~154 Ir/compare), with the
**different-exponent same-magnitude-bucket** regime the single most expensive shape (12.48 ns) because
it runs a full canonicalize (`strip_zeros`) + `digit_count`/bucket + per-digit extraction +
lexicographic walk. The already-merged **R3** optimization hoisted only the *same*-exponent case; this
feature (**C1**) attacks the *cross*-exponent slow path that R3 does not touch, replacing the entire
divide-and-walk pipeline with a branch-free exact wide-integer compare that emits **bit-identical
`std::strong_ordering`** for every input.

Because the result is identical to today's comparator, this is a pure **speed swap on the default
path** — there is no runtime mode, no flag, and nothing for a caller to opt into (confirmed with user
2026-07-04). The observable contract is unchanged; only the internal algorithm and its cost change.

This deliberately reverses the original Gate-A "**no `__int128`**" decision from `2a-decimal.md` v0.1.
That rejection was of an **unguarded** common-exponent scale that overflows even 128 bits at full domain
delta (a 38-exponent normalization needs ~189 bits). The new algorithm is sound **by construction**: a
magnitude-dominance guard decides all large-delta pairs with *no* arithmetic, and the remaining pairs
are proved to fit an unsigned 128-bit product (`< 2^123`). The reversal therefore requires a contract
amendment (see FR-009) leading with that bound proof.

## Clarifications

### Session 2026-07-04

- Q: What test vehicle carries the differential oracle (new comparator vs. digit-string reference)?
  → A: **Both** — a fixed-seed **deterministic corpus** as the hard always-on Tier-1 gate (reproducible,
  makes SC-001 provable on a normal CI run) **plus** a **differential libFuzzer target** that asserts
  `new == reference` for continuous randomized coverage (also remediates the module's known
  "fuzz asserts nothing" anti-pattern for the compare path). See FR-010 / FR-010a / SC-001 / SC-006.
- Resolved factually (not a user decision): FR-012's "`windows-msvc` verify label" maps to the existing
  **Tier-2 Windows CI** (`tier2.yml`, `windows-2022`, presets `windows-msvc-{debug,release,asan}`,
  triggered by the `run-tier2` label) — the MSVC lane already exists; no new CI infrastructure needed.

## Normative References

Per `[const §VI.5]`. This feature introduces **no new OFFICIAL FIX spec rows** and touches no FIX
wire/session semantics — so there are no new `[DocAbbrev §X.Y.Z]` coverage-index entries. The governing
internal contracts that inform this spec (both **amended** by this feature per FR-009) are:

- `.specify/2a-decimal.md §6.3` — decimal comparison algorithm & the `no-__int128` clause (being
  superseded by the guarded wide-multiply).
- `specs/001-core-decimal/research.md D-5` — the original rejection of the unguarded common-exponent
  scale (supersession note added).
- Design note: `research/G19-fix-fpml-iso20022/phases/phase-9/perf-investigation/findings/msvc-int128-decimal.md`
  (overflow bound proof §2, algorithm §3, MSVC primitive options §4, witness matrix §6, amendment §8).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Faster cross-exponent decimal ordering with identical results (Priority: P1)

A consumer of the FIX engine (or any caller of `decimal` ordering — sorting, dedup, range checks,
`operator<=>`) compares decimal values whose stored exponents differ. Today this pays the divide-loop
+ digit-walk pipeline; after this feature the same comparison returns the **same ordering** via a
constant-time wide-integer path.

**Why this priority**: This is the entire feature. The value (lower latency on the hottest decimal
regime) and the hard constraint (results must not change) are both realised here; every other story is
a guardrail around it.

**Independent Test**: Run the decimal comparator against the retained digit-string reference
implementation (differential oracle) over a full-domain randomized corpus plus the extended
Python-`Decimal` oracle corpus; assert bit-for-bit `strong_ordering` agreement on every pair. Separately
run `BM_decimal_compare_diff_exp` before/after and confirm the instruction count drops with no regression
on `BM_decimal_compare` / `BM_decimal_compare_diff_bucket`.

**Acceptance Scenarios**:

1. **Given** two finite same-sign decimals with different stored exponents whose magnitude delta
   `k = |ae − be| ≤ 18`, **When** `compare` runs, **Then** the result equals the digit-string
   reference's `strong_ordering` for that pair, computed via a single widening multiply (no divide
   loops, no digit walk).
2. **Given** two finite same-sign decimals with `k ≥ 19`, **When** `compare` runs, **Then** the
   higher-exponent operand is ordered strictly by magnitude dominance (its sign) with **no**
   multiplication, and the result equals the reference.
3. **Given** trailing-zero / canonicalization-equivalent pairs such as `{100,−2}` vs `{1,0}`,
   **When** `compare` runs, **Then** the result is `equal` (exact integer identity, no stripping).
4. **Given** any pair on which the digit-string reference returns X, **When** `compare` runs on the
   same pair, **Then** it also returns X — for the entire non-sentinel input domain, including
   out-of-canonical-domain `int8` exponents.

---

### User Story 2 - No overflow, no narrowing, no undefined behavior on any input (Priority: P1)

The wide-multiply path must be provably overflow-free and must never silently narrow the 128-bit
product down to 64 bits (the failure mode that passes every small-mantissa test yet misorders large
scaled values).

**Why this priority**: This is the soundness leg that killed the v0.1 design. Without it the feature is
a correctness regression, not an optimization. Equal priority to Story 1.

**Independent Test**: Directed witnesses that force the scaled product across `2^64` — the **unsigned
hi-limb threshold** above which `mul_u64_wide` writes a non-zero `hi` (note `2^63` is the *signed*
overflow point and is NOT what the unsigned helper cares about) — e.g. `{99,0}` vs
`{9223372036854775807,−18}` (scaled `99×10^18 = 9.9e19 ≥ 2^64`, so `hi ≠ 0`), in both operand orders and
both signs; assert correct ordering. Mutation test: dropping the high-limb (`hi`) consultation, or
narrowing the guard constant, must be caught by at least one failing witness.

**Acceptance Scenarios**:

1. **Given** a `k ≤ 18` pair whose scaled magnitude exceeds `2^64` (so the product's high limb is
   non-zero), **When** `compare` runs, **Then** the high limb of the 128-bit product is consulted and the
   ordering is correct.
2. **Given** the guard boundary (`k = 18` multiply arm, `k = 20` guard arm near `INT64_MAX`, `k = 38`
   full domain), **When** `compare` runs in both scale directions and both signs, **Then** every result
   matches the reference.
3. **Given** the invalid sentinel (`mantissa == INT64_MIN`) on either or both sides, **When** `compare`
   runs, **Then** the sentinel path is unchanged (sentinel orders strictly greatest) and magnitude
   code is never reached, so negation of `INT64_MIN` never occurs.

---

### User Story 3 - Builds and behaves identically across all supported toolchains including MSVC (Priority: P2)

The one 128-bit primitive must compile and produce identical ordering on GCC, Clang, the libc++ lane,
clang-cl, and MSVC (x64 and ARM64), with a portable fallback for any other compiler.

**Why this priority**: The MSVC intrinsics branch is compiled by **no Linux CI** (the L-049-3
Linux-byte-identical-breakage shape). It is a guardrail on portability rather than the core value, hence
P2 — but it is a hard release condition, not optional.

**Independent Test**: Compile and run the full comparator test suite on the `windows-msvc` verify label;
confirm the differential oracle passes there too. Exercise the portable `#else` fallback path (e.g. via
a forced-fallback build define in a unit test) so it is covered, not just the native intrinsic branch.

**Acceptance Scenarios**:

1. **Given** a GCC/Clang/clang-cl build, **When** the comparator is compiled, **Then** the native
   `unsigned __int128` primitive is selected and the oracle passes.
2. **Given** an MSVC x64 / ARM64 build, **When** the comparator is compiled, **Then** the platform
   intrinsic primitive is selected (signatures confirmed against current Microsoft `<intrin.h>` docs)
   and the oracle passes on the `windows-msvc` label.
3. **Given** a compiler matching none of the above, **When** the comparator is compiled, **Then** the
   portable limb-based fallback is selected and the oracle passes.

### Edge Cases

- **Zero operands at any exponent**: `{0,e}` is value 0 for every `e`; zero-filter before scaling so the
  `k ≥ 19` dominance premise (`|m| ≥ 1`) holds. `{0,−38}` vs `{0,0}` → equal; `{0,−5}` vs `{−1,0}` →
  greater.
- **Out-of-canonical-domain exponents**: the C++ `decimal` explicit constructor does not validate, so
  the comparator must stay **total** over any `int8` exponent (delta > 38 via int8 extremes), not just
  `[−38,0]`. `k` is computed in `int` so there is no `int8` overflow.
- **Guard-constant one-step margin**: `k = 19` and `k = 20` both agree at the boundary (a `19→20`
  mutant is semantically equivalent); the real breaking points are `k = 20` near `INT64_MAX` (guard
  arm) and the `hi`-limb consultation — those are the witnesses to pin.
- **Sign flip**: mixed signs decided by the (unchanged) sign filter; below it comparison is on
  magnitudes with the result flipped for negative — identical flip discipline to today's comparator
  (the Gate-B P1 #1 same-bucket-negatives lesson).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The comparator MUST return, for every non-sentinel input pair, a `std::strong_ordering`
  **bit-for-bit identical** to the current digit-string comparator's result. This is the primary,
  non-negotiable acceptance condition.
- **FR-002**: The different-exponent slow path (today's `strip_zeros` + `digit_count`/bucket +
  digit-extraction + lexicographic walk) MUST be replaced by an exact wide-integer compare that
  contains **no loops and no divisions**.
- **FR-003**: The sentinel filter, the sign filter, and the R3 same-exponent hoist MUST be preserved
  with their current observable behavior.
- **FR-004**: For magnitude delta `k ≥ 19`, the comparator MUST decide the ordering by magnitude
  dominance (higher-exponent operand's sign) with **no multiplication**.
- **FR-005**: For `k ≤ 18`, the comparator MUST compute the ordering via a single unsigned 64×64→128
  widening multiply against a static power-of-ten table (`10^0 … 10^18`) and a two-limb compare, and
  MUST consult the high limb of the product (no silent narrowing to 64 bits).
- **FR-006**: The comparator MUST be **total** over all non-sentinel inputs including out-of-canonical
  `int8` exponents, and MUST remain `noexcept` with no allocation and no exception on any path.
- **FR-007**: The feature MUST make **zero change to public surface**: the `decimal.hpp` signature, the
  C-ABI `_checked` wrappers, and all other headers are untouched. Scope is `src/core/decimal.cpp` only.
- **FR-008**: The 128-bit primitive MUST be a single translation-unit-local helper selecting a native
  widening multiply per compiler (`unsigned __int128` where available; MSVC x64/ARM64 intrinsics;
  portable limb fallback otherwise) with **one algorithm** everywhere — the per-compiler split is at the
  multiply instruction only, introducing **no new dependency and no internal-STL-header dependency**.
- **FR-009**: The Gate-A `no-__int128` contract MUST be formally amended, leading with the overflow
  bound proof (`k ≥ 19` dominance + product `< 2^123`) and recording that observable semantics are
  unchanged. Because this feature also **reclassifies C1 from an opt-in low-latency MODE candidate to a
  default-path, semantics-preserving speed swap** (no mode flag, per Overview `:19-21`), the amendment
  MUST NOT merely add a cross-reference — it MUST land, in the same PR, a **dated supersession note
  (2026-07-04)** at every site that still frames C1 as opt-in-mode, so no document contradicts the
  default-path decision (SC-005). The amendment MUST touch:
  - `.specify/2a-decimal.md §6.3` — guarded algorithm + bound proof (retire "No wide-int dependency").
  - `specs/001-core-decimal/research.md D-5` — supersession note.
  - the `src/core/decimal.cpp` contract comment — drop "No `__int128`", cite amended §6.3.
  - `02-lowlatency-recommendations.md`, at **all three C1 framings**: the **C1 Tier-C entry** (`:365+`),
    the **Tier-C preamble caveat** (`:354-362` — the "None of them is a default-path change … opt-in
    low-latency MODE" set-level framing, which MUST be carved out for C1), and the **"Considered and
    rejected: `__int128`" bullet** (`:357-359`) — a dated supersession note reclassifying C1 as a
    default-path swap, not just a pointer.
  - `remaining-work/perf-and-hardening-findings.md`, at **all three C1 sites**: the Cluster-2 residual
    line (`:62`), the **C1 table row** (`:72`), and the **"Low-latency MODE" list entry** (`:141`). The
    C1 row MUST be **rewritten** to state the default-path reclassification (semantics-preserving speed
    swap), **not merely flipped to DONE**.
- **FR-010**: Verification MUST include a **differential oracle** — a retained verbatim copy of the
  current digit-string comparator as a test-local reference — asserting `strong_ordering` agreement over
  (a) a **fixed-seed deterministic** full-domain corpus biased toward digit-count boundaries with in- and
  out-of-domain exponents, and (b) the existing Python-`Decimal` oracle corpus extended with
  cross-exponent pairs; plus property checks for antisymmetry and transitivity. This deterministic
  corpus is the **hard always-on gate** (runs in Tier-1; reproducible so SC-001 is provable on a normal
  CI run).
- **FR-010a**: Verification MUST ALSO include a **differential libFuzzer target** that decodes two
  `pod_decimal` values from fuzzer input and asserts `new.compare == reference.compare` (mismatch =
  `__builtin_trap`, per the module's fuzz convention), with a committed seed corpus. This provides
  continuous randomized coverage beyond the fixed corpus and closes the "fuzz asserts nothing"
  anti-pattern for the compare path (the existing `fuzz_decimal_parse` gap is a separate, out-of-scope
  residual — see Assumptions).
- **FR-011**: Verification MUST include the directed-witness matrix (zero filters, scaled-crosses-`2^64`
  `hi`-limb, `k`-boundary in both directions/signs, sign-flip extremes, sentinel regressions,
  out-of-domain exponents), each **mutation-tested** to prove it kills the mutant it targets.
- **FR-012**: The MSVC intrinsic signatures and their architecture availability MUST be confirmed
  against current Microsoft `<intrin.h>` documentation at implementation time; the feature verify MUST
  run the `windows-msvc` label; and the portable `#else` fallback path MUST be covered by tests.

### Non-Functional / Performance Requirements

- **NFR-001**: `BM_decimal_compare_diff_exp` (primary) MUST show a reduced instruction count vs baseline
  under single-filter callgrind; `BM_decimal_compare` and `BM_decimal_compare_diff_bucket` MUST NOT
  regress. Absolute nanosecond figures on WSL2 are shape-only, not gate values.

### Key Entities

- **`pod_decimal`**: `{int64_t mantissa, int8_t exponent}`; `INT64_MIN` mantissa = invalid sentinel;
  canonical exponent domain `[−38, 0]` but the comparator is total beyond it.
- **Digit-string reference comparator**: a verbatim, test-local copy of today's `compare` body, retained
  purely as the differential oracle — the source of truth for "identical results".

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The differential oracle reports **100% agreement** (zero mismatches) between the new
  comparator and the digit-string reference across the fixed-seed deterministic full-domain corpus and
  the extended Python-`Decimal` corpus, **reproducibly** on a normal Tier-1 CI run.
- **SC-002**: Every directed witness in the matrix passes, and each is demonstrated to **fail** when its
  targeted mutant is introduced (mutation-tested), with the documented semantically-equivalent mutant
  (`19→20`) recorded as an accepted no-kill.
- **SC-003**: The cross-exponent compare regime's measured instruction count (single-filter callgrind on
  `BM_decimal_compare_diff_exp`) **decreases** relative to the pre-feature baseline, with no regression
  on the same-exponent and diff-bucket benchmarks.
- **SC-004**: The comparator compiles and the oracle passes on **all** supported toolchains, including
  the `windows-msvc` label and the forced portable-fallback path; no public API, C-ABI, wire, error, or
  layout surface changes.
- **SC-005**: The contract amendment is present at every site FR-009 enumerates and leads with the
  overflow bound proof; **after merge no site frames C1 as an opt-in low-latency MODE** — verifiable by
  grep across `02-lowlatency-recommendations.md` (Tier-C entry + preamble caveat + rejected-`__int128`
  bullet) and `remaining-work/perf-and-hardening-findings.md` (`:62`/`:72`/`:141`): no residual "opt-in
  low-latency MODE" / "none … default-path change" text remains bound to C1, and no two project documents
  contradict each other on the `__int128` decision.
- **SC-006**: A differential libFuzzer target exists, builds in the fuzz lane, asserts
  `new == reference` (traps on mismatch), ships a committed seed corpus, and is **demonstrated to trap**
  when a deliberate compare mutant is introduced (proving it asserts, not the `fuzz_decimal_parse`
  no-assert shape).

## Assumptions

- The R3 same-exponent hoist (`a.exponent == b.exponent → a.mantissa <=> b.mantissa`) is already merged
  and remains in place; C1 layers on top of it and does not re-touch the same-exponent path.
- No low-latency MODE / SessionConfig toggle exists or is introduced — this is a default-path swap
  (verified: no such infrastructure in the source, 2026-07-04).
- Only `compare` benefits and is in scope. `from_chars` (mantissa is int64-bounded by ABI), `to_chars`
  (not flagged by profiling), and decimal arithmetic (no add/sub/mul surface exists) are explicitly out
  of scope.
- The existing `fuzz_decimal_parse.cpp` "asserts nothing" gap (the *parse* canonical-domain invariant,
  flagged in the Cluster-2 tracker) is a **separate residual** and stays out of scope; this feature adds
  a differential *compare* fuzz target (FR-010a), not a parse assertion.
- The overflow bound proof and algorithm are taken from the vetted design note
  `phases/phase-9/perf-investigation/findings/msvc-int128-decimal.md`; the plan phase carries the
  concrete algorithm/helper shape (the spec deliberately stays at the observable-contract level).
- Perf figures are treated as shape-only on WSL2; the R3 measurement protocol (single-filter callgrind
  + the named benchmarks) is authoritative for the no-regression checks.

## Dependencies

- Design note: `research/G19-fix-fpml-iso20022/phases/phase-9/perf-investigation/findings/msvc-int128-decimal.md`
  (bound proof §2, algorithm §3, MSVC options §4, witness matrix §6, amendment list §8).
- Anchor contracts to amend: `.specify/2a-decimal.md §6.3`, `specs/001-core-decimal/research.md` D-5.
- Baseline benchmarks: `BM_decimal_compare`, `BM_decimal_compare_diff_exp`,
  `BM_decimal_compare_diff_bucket`.
- CI: the opt-in `windows-msvc` verify label (no Linux lane compiles the MSVC intrinsics branch).
