# Implementation Plan: Exact wide-integer cross-exponent decimal compare (C1 / int128)

**Branch**: `060-int128-decimal-compare` | **Date**: 2026-07-04 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/060-int128-decimal-compare/spec.md`

## Summary

Replace the different-exponent slow path of `decimal_traits<pod_decimal>::compare`
(`src/core/decimal.cpp` — `strip_zeros` + `digit_count`/bucket + digit-extraction + lexicographic
walk) with a branch-free exact wide-integer compare that returns **bit-identical**
`std::strong_ordering`. After a zero-filter, compute `k = |ae − be|`: `k ≥ 19` decides by magnitude
dominance with no arithmetic; `k ≤ 18` decides via one unsigned 64×64→128 widening multiply
(`|m_hi-exp| × kPow10[k]`) and a two-limb compare (product proved `< 2^123`). The 128-bit primitive is
one TU-local `mul_u64_wide` helper, `#if`-selected per compiler (`unsigned __int128` / MSVC intrinsics /
portable limb fallback) — one algorithm everywhere, per-compiler only at the multiply. Sentinel filter,
sign filter, and the merged R3 same-exponent hoist are preserved verbatim.

This is a **default-path, semantics-preserving speed swap** (no mode flag). It knowingly reverses the
`2a-decimal.md` v0.1 Gate-A "no `__int128`" decision — sound now because the v0.1 rejection was of an
*unguarded* scale; the `k ≥ 19` dominance guard is exactly what v0.1 lacked. The reversal is carried by
a **contract amendment** (FR-009) leading with the overflow bound proof, and the equivalence is proven
by a **differential oracle** against a retained verbatim copy of today's comparator (deterministic
corpus = hard Tier-1 gate; libFuzzer target = continuous coverage, per the /clarify decision).

## Technical Context

**Language/Version**: C++20 (Clang 22 local + Tier-1 CI per `[const §II.2]`; GCC Release sanity; MSVC
  Tier-2 `windows-2022`).
**Primary Dependencies**: none new. TU-local helper only. `<compare>`, `<cstdint>`; `<intrin.h>` only
  inside the MSVC `#elif` branches.
**Storage**: N/A.
**Testing**: GoogleTest (C++ unit + differential-oracle corpus), pytest (extended Python-`Decimal`
  oracle seam), libFuzzer (new differential compare target), Google Benchmark (existing
  `BM_decimal_compare*`).
**Target Platform**: Linux/Clang + Linux/GCC (Tier 1); Windows/MSVC x64 + ARM64 (Tier 2); portable
  `#else` for any other compiler.
**Project Type**: C++ library (single project — `src/core` + `include/fixpp/core`).
**Performance Goals**: reduce `BM_decimal_compare_diff_exp` instruction count (single-filter callgrind,
  deterministic) with no regression on `BM_decimal_compare` / `BM_decimal_compare_diff_bucket`; WSL2 ns
  are shape-only. Regression budget `±5%` vs `bench/baselines/decimal_baseline.json` per `[const §VIII.2]`.
**Constraints**: `noexcept`, zero allocation, zero loops/divisions on the new path; total order over all
  non-sentinel inputs incl. out-of-canonical `int8` exponents; **zero public/C-ABI/wire/error/layout
  surface change** (`[const §X.3]` decimal PoD frozen).
**Scale/Scope**: one function body in `src/core/decimal.cpp`; ~1 TU-local helper + one `constexpr`
  table; test + bench + doc/contract-amendment surface. No header change.

## Constitution Check

*GATE: evaluated against `.specify/constitution.md` v0.3. Re-checked after Phase 1 (unchanged).*

| Article | Gate | Status |
|---|---|---|
| **II §2/§4** Compilers | Clang 22 Tier-1, MSVC Tier-2, no compiler-version pin | **PASS** — per-compiler `#if` is a *primitive* selection, not a version pin; `#else` covers unknown compilers. |
| **VII §3/§4** TDD, no code without a test | red-first, differential oracle + witnesses | **PASS** — TDD ordering enforced (reference-oracle test + witnesses written red before the swap). |
| **VII §7 / IX §6** Fuzz for parser-touching code | new differential libFuzzer target | **PASS (exceeds)** — compare is not parser-touching, but FR-010a adds a fuzz target anyway (also closes the compare-path no-assert gap). |
| **VIII §2/§3** ±5% budget, no perf change without a bench in-PR, baseline updated in-PR | `BM_decimal_compare*` exist | **PASS w/ obligation** — update `bench/baselines/decimal_baseline.json` in this PR with rationale; the diff-exp win is the intentional change, same-exp/diff-bucket must stay within ±5%. |
| **VIII §5** Zero hot-path alloc | new path is pure integer ops | **PASS** — no `new`/`delete`, no PMR; `noexcept` preserved. |
| **IX §1** Coverage ≥95/85 touched modules | Linux/Clang lcov | **PASS w/ design item** — the MSVC `#elif` + portable `#else` branches are `#if`-excluded on Linux (not counted). The `#else` fallback MUST be force-compiled in a dedicated test TU so it is *covered*, not silently absent (see research.md R2 / FR-012). |
| **IX §2** ASan/UBSan/TSan Tier-1 | | **PASS (critical)** — UBSan is the primary guard on the widening multiply/shifts; all arithmetic unsigned, product bound-proved. |
| **IX §5 / X §3,§6** C-ABI frozen, abidiff | decimal PoD unchanged | **PASS** — internal function body only; `decimal.hpp` signature + `_checked` wrappers untouched → abidiff clean, no `/analyze`+Gate-A ABI *surface* trigger. NB: `fixpp_decimal_compare` **is** a C-ABI symbol (the Python oracle calls through it), but its signature/layout/**behavior** are all preserved — behavior-preservation is proven by the oracle running through the C-ABI wrapper. The amendment is a *doc/contract* change, still Gate-A-reviewed per XVII. |
| **XV §1** No hot-path allocation | | **PASS.** |
| **XVI §3** `/clarify` mandatory before `/plan` | ran 2026-07-04 (1 Q) | **PASS.** |
| **XVII** Codex Gate A/B | contract reversal | **PASS w/ obligation** — Gate A MUST review the `__int128` reversal; the bundle leads with the §2 bound proof + differential-oracle evidence (research.md). |
| **VI §5** Normative References in `/specify` artifacts | `## Normative References` in spec.md | **PASS** — section present in **spec.md** (the `/specify` artifact §VI.5 names); states no new OFFICIAL FIX rows and lists the governing internal contracts (`2a-decimal.md §6.3`, `research.md D-5`) the amendment updates. |

**No unjustified violations → Complexity Tracking left empty.** The single documented obligation set
(bench-baseline update, `#else`-coverage TU, Gate-A-reviewed amendment) is carried into tasks, not a
constitution deviation.

## Project Structure

### Documentation (this feature)

```text
specs/060-int128-decimal-compare/
├── plan.md              # This file
├── spec.md              # Feature spec (+ Clarifications)
├── research.md          # Phase 0 — decisions R1..R6 (algorithm, MSVC primitive, coverage, bench, oracle, amendment)
├── data-model.md        # Phase 1 — pod_decimal domain, mul_u64_wide contract, kPow10 table, oracle entities
├── quickstart.md        # Phase 1 — how to build/run the oracle, fuzzer, bench, MSVC lane
├── contracts/
│   └── compare-contract.md   # Observable compare contract (unchanged semantics) + amendment checklist
├── checklists/
│   └── requirements.md  # Spec-quality checklist (from /specify)
└── tasks.md             # Phase 2 — /speckit-tasks (NOT created here)
```

### Source Code (repository root = library submodule)

```text
src/core/decimal.cpp                         # THE change: replace diff-exp body; add TU-local mul_u64_wide + kPow10
include/fixpp/core/decimal.hpp               # UNCHANGED (signature, PoD, sentinel) — verified untouched at verify

tests/core/decimal_compare_test.cpp          # existing US1 AC-C1..C5 unit tests (regression, keep green)
tests/core/decimal_compare_diff_oracle_test.cpp   # NEW — retained digit-string reference + deterministic differential corpus + witness matrix + antisymmetry/transitivity
tests/oracle/decimal_compare_oracle_test.py  # EXTEND — add cross-exponent pairs to the seed=42 corpus (currently canonical-domain only)
tests/fuzz/fuzz_decimal_compare.cpp          # NEW — differential libFuzzer target (new vs reference; trap on mismatch)
tests/fuzz/CMakeLists.txt                    # register fuzz_decimal_compare (fuzzer,address,undefined)
tests/fuzz/corpus/decimal_compare/           # NEW — committed seed corpus

bench/core/decimal_bench.cpp                 # UNCHANGED benchmark bodies (BM_decimal_compare / _diff_exp / _diff_bucket)
bench/baselines/decimal_baseline.json        # UPDATE in-PR with rationale ([const §VIII.2/3])

.specify/2a-decimal.md                        # AMEND §6.3 (algorithm + bound proof; semantics unchanged)
specs/001-core-decimal/research.md            # AMEND D-5 (supersession note)
# + src/core/decimal.cpp contract comment (drop "No __int128")
# + research/.../perf-investigation/02-lowlatency-recommendations.md (parent-repo, post-merge close-out — dated (2026-07-04) supersession notes at all three C1 framings — Tier-C entry :365+, Tier-C preamble caveat :354-362, rejected-__int128 bullet :600-611 — not just a pointer)
# + research/.../remaining-work/perf-and-hardening-findings.md (parent-repo, post-merge close-out — reclassify C1 default-path at all three sites: :62, :72 rewritten not just flipped, :141 Low-latency-MODE list)
```

**Structure Decision**: Single-project C++ library layout, already established. The change is confined
to `src/core/decimal.cpp`; every other touched path is test / bench-baseline / documentation. No new
module, no header, no dependency, no build-graph target beyond the one fuzz executable.

## Phase 0 — Research

See [research.md](./research.md). All spec unknowns are resolved by the vetted design note
(`msvc-int128-decimal.md`); research.md consolidates six decisions (algorithm & bound proof, MSVC
primitive split, `#else`-coverage mechanism, bench-baseline protocol, oracle-corpus extension, contract
amendment) with the confirm-at-implement obligation on the MSVC intrinsic signatures.

## Phase 1 — Design & Contracts

- [data-model.md](./data-model.md): `pod_decimal` domain + sentinel, the `mul_u64_wide` primitive
  contract (inputs/outputs/`hi` semantics/noexcept), the `kPow10[0..18]` table, and the test-oracle
  entities (reference comparator, deterministic corpus, witness matrix, fuzz corpus).
- [contracts/compare-contract.md](./contracts/compare-contract.md): the observable `compare` contract
  (total `strong_ordering`, canonical equality, sentinel-greatest, `noexcept`) — **unchanged** — plus
  the amendment checklist across 5 files (2a-decimal.md, 001 research.md, decimal.cpp, 02-lowlatency-recommendations.md, remaining-work/perf-and-hardening-findings.md).
- Agent context: update the `<!-- SPECKIT ... -->` block in `library/CLAUDE.md` to point at this plan.

## Complexity Tracking

> No Constitution Check violations. Table intentionally empty.

## Gate A

- Round 1 applied 2026-07-04: Codex P1=0 P2=1 P3=0; Opus post-judging P1=0 P2=2 P3=0; rewrite addresses RC — (a) amendment-scope: reclassify C1 default-path at all opt-in-mode sites incl. perf-tracker :62/:72/:141 + 02-lowlatency Tier-C preamble/rejected-bullet, strengthen SC-005; (b) hi-limb witness row 2 corrected 2^63→2^64 (product ≥2^64 so hi≠0 kills drop-hi mutant). Reviews: research/reviews/codex_060-int128-decimal-compare_gate_a_review.md, research/reviews/opus_060-int128-decimal-compare_gate_a_adversarial_review.md.
- Round 2 applied 2026-07-04: Codex P1=0 P2=1 P3=1; Opus post-judging P1=0 P2=1 P3=2; rewrite = pure doc-drift sweep (RC: round-1 rewrite left data-model.md unswept + stale "4 docs"/"cross-ref" counts) — data-model.md witness rows repointed to corrected research R5 (design-note §6 row 2 superseded), plan.md 02-lowlatency wording + added remaining-work amendment line + "four"→"five-file", quickstart "4 docs"→"5 files". Reviews: research/reviews/codex_060-int128-decimal-compare_gate_a_2_review.md, research/reviews/opus_060-int128-decimal-compare_gate_a_2_adversarial_review.md.
