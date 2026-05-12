# NFR & Quality-Gate Requirements Quality Checklist: 001-core-decimal

**Purpose**: Validate the requirements quality of the non-functional surface — latency bars (50/30/20 ns), zero-allocation discipline, coverage thresholds, sanitizer matrix, fuzz harness, property oracle, abidiff golden, and the 10 test seams. Tests whether the NFRs are written well enough that a Gate B reviewer can hostile-check them without re-asking, and a CI engineer can wire them up without inferring missing bars.

**Created**: 2026-05-12
**Reviewed**: 2026-05-12 — 60 met (initial review found 2 partial; both resolved via spec.md §6 edits same day — see Findings at bottom)
**Feature**: [spec.md §6](../spec.md) | [plan.md §Test seam → file mapping](../plan.md) | [quickstart.md](../quickstart.md)
**Audience**: Gate B reviewer (pre-merge hostile review per `[const §XVII.2]`)
**Depth**: Formal release gate

## Performance Bars — Clarity & Measurability

- [x] CHK001 Are the three latency NFRs (50 ns parse, 30 ns format, 20 ns compare) specified with the full workload context (Linux/Clang/x86_64, warm cache, 5-digit mantissa, default `pod_decimal` traits)? [Clarity, Spec §6, Plan §Tech Context]
- [x] CHK002 Is "median" specified as the central-tendency measure (vs mean / p99 / max)? [Clarity, Spec §6]
- [x] CHK003 Is the ±5 % regression budget per `[const §VIII.2]` documented as the gate vs `bench/baselines/decimal_baseline.json`? [Completeness, Spec §6]
- [x] CHK004 Is each latency NFR row in spec.md §6 tagged with its enforcement Tier (Tier 1 here)? [Completeness, Spec §6] — Tier column explicit
- [x] CHK005 Is the miss-tolerance rule (≤ 2× miss is a PR note, not a blocker) explicitly specified with a citation, so reviewers don't reject under-spec misses prematurely? [Clarity, Spec §11 Risk row 1, Research §D-9]
- [x] CHK006 Are the latency bars marked as "bench-spike, revisable per 2a §10 Q4 after 2b integration" so the absolute numbers aren't treated as constitutional? [Traceability, Research §D-9]
- [x] CHK007 Is the bench baseline location (`bench/baselines/decimal_baseline.json`) specified for both the initial capture (T052) and the comparison step (T053)? [Completeness, Quickstart §4, Tasks T052/T053]
- [x] CHK008 Is the bench output format (Google Benchmark JSON via `--benchmark_format=json --benchmark_out=...`) specified? [Completeness, Quickstart §4]

## Allocation Discipline

- [x] CHK009 Is the zero-alloc requirement bound precisely between "parse and `fromApp` callback" rather than vaguely "on the hot path"? [Clarity, Spec §6, [const §VIII.5]]
- [x] CHK010 Is the PMR resource passthrough contract (required, non-null, ignored by non-allocating traits like `pod_decimal`) specified? [Completeness, Research §D-6, Data-Model §Entity 2]
- [x] CHK011 Is the Linux-only enforcement vehicle (`mallocnesia` LD_PRELOAD) specified, with the Windows gap acknowledged as Tier 2 v1.x deferred? [Coverage, Research §D-10]
- [x] CHK012 Is the seam #6 entry point pair (`tools/check_alloc.py` wrapper + `tests/alloc_guard/decimal_alloc_guard_test.cpp`) specified? [Completeness, Plan §Seam 6]
- [x] CHK013 Is AC-P9 ("single-pass, no allocation, no exception") tied to seam #6 as its measurement vehicle? [Traceability, Spec §4.1 AC-P9, Plan §Seam 6]
- [x] CHK014 Is `[const §XV.1]` ("heap-allocate per message or per field on hot path is banned") cited as the bottom-line constitutional rule, not just `[const §VIII.5]`? [Traceability, Plan §Constitution Check]

## Test Seam Completeness (10/10)

- [x] CHK015 Are all 10 seams enumerated in spec.md §9 with a one-line description each? [Completeness, Spec §9]
- [x] CHK016 Are all 10 seams mapped to named on-disk file paths in plan.md §Test seam → file mapping table? [Completeness, Plan §Test seam → file mapping]
- [x] CHK017 Is the rule "no seam may map to the existing `decimal_*_test.cpp`s collectively" stated as a discipline (round-1 root cause #3 preventer)? [Consistency, Plan §Project Structure]
- [x] CHK018 Is Q6 ("all 10 seams in this PR, not follow-up") resolved with a dated `/clarify` entry (Session 2026-05-10) and consistent across spec.md §10 and §12.3? [Conflict resolution, Spec §Clarifications]
- [x] CHK019 Is seam #1 (`mock_decimal_traits<T>`) specified as a parameterizable failing-traits helper (configurable per-member failure modes), not a fixed mock? [Clarity, Plan §Seam 1]
- [x] CHK020 Is seam #2 sample-size (10⁴ generated + `[FIX50SP2 §3.3]` example table) specified so a CI engineer doesn't under-sample? [Completeness, Plan §Seam 2]
- [x] CHK021 Is seam #3 scoped to value-preserving identity (T≠U funnel) + `decimal_precision_loss` (out-of-domain), with the failing-traits helper from seam #1 as the wider-T stand-in? [Completeness, Plan §Seam 3] — note: post-/analyze remediation T037 also defines `test_decimal_wide` inline; both vehicles documented
- [x] CHK022 Is seam #4 split between compile-time `decimal_assert.cpp` static_asserts and Tier 2 abidiff golden specified so a reviewer doesn't conflate the two? [Clarity, Plan §Seam 4]
- [x] CHK023 Is seam #5 (Google Benchmark) tied to the 50/30/20 ns regression bars via `--benchmark_filter`? [Traceability, Plan §Seam 5, Quickstart §4]
- [x] CHK024 Is seam #6 scoped specifically to the parse-format loop (not the whole library), so the alloc guard is meaningful? [Clarity, Plan §Seam 6]
- [x] CHK025 Is seam #7 (libFuzzer harness) tied to ASan + UBSan invariants (no crash, no `std::terminate`, output valid-or-error)? [Completeness, Plan §Seam 7]
- [x] CHK026 Is seam #8 (Python `Decimal` oracle) Tier 1 promotion documented (vs the Tier 2 default in 2a §9 seam #8) with rationale? [Traceability, Plan §Seam 8]
- [x] CHK027 Is seam #9 (link-time mismatch) specified as "expected-to-fail link with sentinel-symbol unresolved" — including the substring assertion mechanism so spurious link errors aren't accepted? [Completeness, Plan §Seam 9, Tasks T025 (post-remediation)]
- [x] CHK028 Is seam #10 (`_reserved` byte tolerance) tied to AC-A4 + AC-A5b as a regression guard for the "ignore on read" rule? [Traceability, Plan §Seam 10]

## Coverage Thresholds

- [x] CHK029 Are line ≥ 90 % and branch ≥ 80 % thresholds per `[const §IX.1]` documented as gates for this feature's touched files? [Completeness, Spec §6]
- [x] CHK030 Is the measurement scope ("touched modules" — i.e., the new `decimal_*` files only) specified rather than "all of `core/`"? [Clarity, [const §IX.1]]
- [x] CHK031 Is the `linux-clang-coverage` preset documented as the measurement vehicle (`llvm-cov` + `llvm-profdata`)? [Completeness, Plan §Tech Context, [const §IX.1]]
- [x] CHK032 Is the Windows coverage-gap acknowledgement documented (coverage not measured on Tier 2 per `[const §IX.1]`)? [Coverage, [const §IX.1]] — inherited via constitution reference; not re-stated for this feature (acceptable per constitution-binding-by-reference convention)

## Sanitizers

- [x] CHK033 Are all three Tier 1 sanitizers (ASan, UBSan, TSan) listed as required gates per `[const §IX.2]`? [Completeness, Spec §6]
- [x] CHK034 Is the per-PR cadence ("every PR, Linux/Clang") for sanitizers documented? [Clarity, [const §IX.6]]
- [x] CHK035 Is the Tier 2 Windows scope (ASan only, manual/nightly) documented per `[const §IX.3]`? [Coverage, [const §IX.3]] — inherited via constitution reference

## Fuzz Harness

- [x] CHK036 Is the ≥ 10 min CI cadence specified for `fuzz_decimal_parse.cpp` per `[const §VII.7]`? [Completeness, Spec §6]
- [x] CHK037 Is the "longer overnight on main" cadence specified separately from the 10-min PR smoke gate? [Coverage, Spec §6]
- [x] CHK038 Is the fuzz invariant set (no crash, no `std::terminate`, output is valid `pod_decimal` or `fixpp::core::error` code) explicitly stated, so the harness assertions are unambiguous? [Completeness, Plan §Seam 7, Tasks T016]

## Static Analysis

- [x] CHK039 Are clang-tidy, cppcheck, IWYU, and clang-format all named as Tier 1 gates per `[const §IX.4]`? [Completeness, Spec §6]
- [x] CHK040 Is the "pre-commit + Tier 1" enforcement layer for static analysis documented? [Coverage, Plan §Tech Context]
- [x] CHK041 Are the tooling versions (e.g., clang-tidy from Clang 22 per `[const §II.2]`) implicitly pinned via the Conan profile? [Traceability, [const §II.2], [const §III.3]]

## Property Oracle

- [x] CHK042 Is the Python `Decimal` oracle's role ("gates `compare`") documented? [Clarity, Plan §Seam 8, Spec §4.3 AC-C5]
- [x] CHK043 Is the oracle's Tier 1 promotion (from Tier 2 default in 2a §9 seam #8) documented with rationale (value-correctness gate, not just nice-to-have)? [Traceability, Plan §Seam 8] — **RESOLVED 2026-05-12**: spec.md §6 NFR row for the property oracle now reads "Tier 1 (promoted from 2a §9 seam 8's Tier 2 default — Python `Decimal` is the canonical arbitrary-precision oracle, so any divergence under our `compare` algorithm is a correctness bug, not a tuning issue)."
- [x] CHK044 Is the oracle's invocation path (pytest + ctypes against `libfixpp_capi.so`, with explicit conftest fixture per Tasks T030b) specified? [Completeness, Tasks T030b/T031 (post-remediation)]

## ABI Golden (Tier 2)

- [x] CHK045 Is the Tier 2 manual/nightly cadence per `[const §IX.6]` documented for the abidiff seam (vs the every-PR Tier 1 default)? [Coverage, [const §IX.6]] — Plan §Seam 4: "Tier 2 hard-fail"
- [x] CHK046 Is the abidiff suppressions scope (`fixpp_decimal_t` struct + 7 boundary fn symbols) specified to prevent unrelated symbol churn polluting the golden? [Completeness, Plan §Seam 4, Tasks T043]
- [x] CHK047 Is the rule "drift in abidiff is a Tier 2 hard-fail; every change must pair with a MAJOR bump on `FIXPP_C_ABI_VERSION_MAJOR`" documented? [Completeness, Quickstart §3, [const §IX.5]]
- [x] CHK048 Is the abidiff golden file path (`tests/abi/golden/fixpp_decimal_t.abidiff`) documented and consistent across plan.md, quickstart.md, and tasks.md? [Consistency, three-way cross-check]

## CI Tier Split

- [x] CHK049 Is the Tier 1 (Linux/Clang every PR) vs Tier 2 (Windows + ABI manual/nightly) split per `[const §IX.6]` referenced for this feature? [Coverage, Plan §Tech Context]
- [x] CHK050 Is the Windows allocation-guard gap explicitly documented as Tier 2 v1.x deferred per `[const §IX.6]` (research.md D-10)? [Coverage, Research §D-10]
- [x] CHK051 Are the Tier 1 vs Tier 2 assignments consistent across every NFR row in spec.md §6 (e.g., latency = Tier 1, abidiff = Tier 2, fuzz smoke = Tier 1 / fuzz long = Tier 2)? [Consistency, Spec §6]

## TDD Discipline

- [x] CHK052 Is the TDD red-green-refactor ordering per `[const §VII.3]` documented as the workflow for tasks.md? [Completeness, Plan §Constitution Check]
- [x] CHK053 Are all functional ACs (AC-P/S/C/X/A/B) tied to dedicated test files for red-green per the seam → file map? [Coverage, Plan §Project Structure]
- [x] CHK054 Is "no code without a test" per `[const §VII.4]` honored — does every implementation task in tasks.md have a preceding test-writing task in the same user story? [Coverage, Tasks Phases 3–7]
- [x] CHK055 Is the "one task at a time TDD" rule per `[const §XVI.6]` honored (no over-large implementation tasks bundling unrelated ACs)? [Consistency, Tasks T019a/T019b (post-remediation D1)] — T019 split into T019a/T019b post-/analyze

## Inheritance & Traceability

- [x] CHK056 Is the inheritance from `.specify/2a-decimal.md` v0.3 documented at each NFR row that traces to it (latency bars, allocation policy, seam list)? [Traceability, Spec §6, §9] — **RESOLVED 2026-05-12**: spec.md §6 NFR table gained a "Source" column citing each row's specific origin (`[const §VIII.5]`/§XV.1, `[arch §5.3]`+2a §6.5, 2a §6.5+Research §D-9, `[const §IX.1]`/§IX.2/§VII.7/§IX.4, 2a §9 seam 8+Plan §Seam 8). A reviewer can now audit per-row without back-deriving.
- [x] CHK057 Are all 26 cited constitution articles (per plan.md §Citation verification) documented as resolving to actual constitution text? [Completeness, Plan §Citation verification] — Plan has the 26-row citation verification pass
- [x] CHK058 Are cross-doc cites (`[arch §...]`, `[FIX50SP2 §3.3]`, `[SYN §3.1 Q5]`) inherited verbatim from `.specify/2a-decimal.md` Appendix B without rewriting? [Consistency, Plan §Citation verification]

## Catalogue Coverage

- [x] CHK059 Is the catalogue impact ("none — W-009 covers FLOAT family") stated so reviewers don't expect a new row? [Clarity, Spec §8]
- [x] CHK060 Is the W-009 status-advance requirement (move row from `planned` to `in-progress` with PR-SHA evidence) specified for the Polish phase? [Completeness, Tasks T054 (post-remediation)]

## Findings (2026-05-12 review + resolution)

**Initial review tally**: 58 met, 2 partial, 0 gaps (60 items total).
**Post-resolution tally**: 60 met, 0 partial, 0 gap (both partials resolved same day via spec.md §6 edits).

**Resolutions applied 2026-05-12:**

- **CHK043 (was partial)** — spec.md §6 NFR row for the property oracle now includes the Tier 1 promotion rationale inline ("Python `Decimal` is the canonical arbitrary-precision oracle, so any divergence is a correctness bug, not a tuning issue").
- **CHK056 (was partial)** — spec.md §6 NFR table gained a "Source" column with per-row inheritance trace (constitution §, 2a §, research D-N annotation per row). A reviewer can audit each NFR against its origin without back-deriving from the section intro.

## Notes

- Check items off as completed: `[x]`
- Add comments or findings inline
- Link to relevant resources or documentation
- Items are numbered sequentially for easy reference
- 60 items total — formal release gate depth per `/speckit-checklist` invocation 2026-05-12
- Traceability: ≥95% of items carry an explicit spec/plan/quickstart/research/const reference
- This checklist focuses on whether the NFRs themselves are written well; it does not test whether the implementation hits the bars (that is `/speckit-implement` + `ctest` territory)
