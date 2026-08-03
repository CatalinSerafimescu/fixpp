# Checklist: Verification evidence & false-green resistance

**Feature**: `085-fold-flat-cap-loop` · **Created**: 2026-08-03 · **Audience**: Gate B
**Domain**: does the evidence this feature ships actually prove what it claims — and can any of it pass while measuring nothing?

> **Why this domain exists.** This bundle's validation procedure shipped a **false-green mechanism in four consecutive Gate A rounds**, twice introduced by the fix for the previous one: zero-match `ctest -R` selectors that exit 0 → count guards using `return 1` at top level → a stale `/tmp` JSON parsed after a failed benchmark run → a per-name comparator whose early failure was overwritten by a shell loop. Assume a fifth exists until checked.

## Article VII §3 — the red-first artifact

- [ ] CHK101 Does `WireOffsetTable.FR001_SingleTraversalSourceInspection` **exist**, in `tests/wire/offset_table_test.cpp`, in the `wire_pure_tests` bucket, with **no new executable**? [FR-001b, `[const §VII.8]`, T006]
- [ ] CHK102 Was it **authored and observed RED on the unmodified tree, before** the relocation — with the RED output captured verbatim in `.specify/decisions/085-fold-flat-cap-loop-verify.md`? [FR-001b, SC-005b, T006]
- [ ] CHK103 Is it GREEN after the relocation, with that transcript recorded beside the RED one? [SC-005b, T009]
- [ ] CHK104 Does `tests/wire/CMakeLists.txt` define `FIXPP_SRC_DIR` for the bucket, mirroring `tests/dictionary/CMakeLists.txt:178-183`? Is that the **only** build change the feature makes? [FR-001b, T002]
- [ ] CHK105 Does the pin's comment block state that it is a **source-inspection assertion, not a behaviour test**, and **why** behaviour cannot distinguish the two states? [FR-001b, T006]
- [ ] CHK106 Does its discriminant actually discriminate on the delivered tree — is the 4-space / 8-space / 16-space key set still mutually exclusive after `clang-format` has run? [FR-001b]
- [ ] CHK107 Is `plan.md`'s Article VII §3 row **`PASS (planned)` against this pin** — not the withdrawn "NOT CLEANLY APPLICABLE", and not resting on mutation testing as a substitute? [`[const §VII.3]`, `[const §XX.1]`]

## Mutation evidence — both sites

- [ ] CHK108 Is the **dict-free** mutation transcript (FR-005a(i)) recorded: check deleted → pin RED → restored → green? [FR-005a, SC-004a, T015]
- [ ] CHK109 Is the **dictionary** mutation transcript (FR-005b) recorded, and does every statement of it carry the **post-relocation only** qualifier? [FR-005b, SC-003, T012]
- [ ] CHK110 Is the **baseline control** recorded — that deleting `consume_group_extent:521-524` on the *unmodified* tree leaves the dictionary pin **GREEN**, because the flat loop still catches the breach? [T005]
- [ ] CHK111 Is that control written to the verify doc? *(It is obtainable only pre-relocation; unrecorded, it cannot be reconstructed, and T012's RED loses its contrast.)* [T005, `/speckit-analyze` F2]
- [ ] CHK112 Is the dict-free pin **bracketed on both sides** — over-cap frame fails, same frame with the cap raised succeeds? [FR-005a(ii), SC-004a, T014]
- [ ] CHK113 Do both dict-free cases use dict-free construction **and** a tightened `Config`? *(Under default `Config` the branch is arithmetically unreachable — both bounds 4096, table clamped at `:326`. A pin using defaults tests nothing.)* [SC-004, R-2, T013]

## False-green resistance in the procedure itself

- [ ] CHK114 Does **every** test selection in `quickstart.md` assert a count, inside a subshell, chained so a wrong count **halts before** the command it guards? [`/gate-a` r2, T023]
- [ ] CHK115 Is every asserted count the **real** count? *(A wrong number fails a correct run — the mirror-image defect.)* [T023]
- [ ] CHK116 Does the benchmark step carry an **executable** count assertion (`--benchmark_list_tests=true`), not a prose expectation? *(A zero-match `--benchmark_filter` exits 0.)* [`/gate-a` r2, T020]
- [ ] CHK117 Is the benchmark output path **removed before** the run and **asserted non-empty after** it, with the invocation chained `|| exit 1`? *(Google Benchmark `std::exit(1)`s on an unopenable `--benchmark_out`; the parse step then reads the previous run's file.)* [`/gate-a` r3, T020]
- [ ] CHK118 Do the two A/B legs write to **distinct** paths (`TAG=main` / `TAG=branch`)? *(One shared path lets a failed branch run compare `main` against itself for a ≈0% delta and **pass the blocking gate on identical data**.)* [`/gate-a` r3, T004, T020]
- [ ] CHK119 Does the A/B comparator decide over **all three cases in one process before printing any row**, so an early missing case cannot be overwritten by a later success? *(The per-name shell loop was order-dependent: a missing last case failed, a missing first case did not.)* [`/gate-a` r4, T020]
- [ ] CHK120 Is the release **build** inside the guarded subshell and chained? *(§3a is the only step that builds release; a release-only compile failure is invisible everywhere else.)* [`/gate-a` r3, T020]
- [ ] CHK121 Were the failure paths **executed**, not asserted — each guard shown to halt? [T023]

## Regression corpora

- [ ] CHK122 SC-001 — `DelimiterCensus.RedCountsReconcileWithSpecBaseline` and all **seven** `TypedReadSplitAgreement.*` green, with **no fixture or baseline edit** made to achieve it? *(An edit to turn these green is a failure of SC-001, not a fix.)* [SC-001, T010]
- [ ] CHK123 SC-002 — `ctest -L wire` 4/4, `-L capi` 22/22, `-L dictionary` 17/17, identical pass set to `main`? [SC-002, T011]
- [ ] CHK124 Are the two pre-existing dictionary cap pins still green — `DoSCapPerInstanceRejectsOversizedSingleInstance` (`:199-235`) and `DoSCapPerInstanceAllowsAggregateOverCap` (`:164-197`)? [SC-003, T010]

## Performance (Article VIII)

- [ ] CHK125 Is the leg-1 observation the **median of three per-case medians**, with the run count fixed in advance and **all three runs reported**? *(A "single run within budget" rule permits repeating until one lands.)* [SC-006, T020]
- [ ] CHK126 Does SC-006 rest first on the **baseline's own seed-row provenance** — `Flat2`'s "not a recorded-baseline regression budget", `pre083_median_ns: null` on the other two — rather than on locally measured host noise? [SC-006]
- [ ] CHK127 Are both legs' numbers **in the PR body**, and is `bench/baselines/` **not** updated? *(This is not an intentional perf change.)* [`[const §VIII.2/3]`, A-004, T020]

## Coverage, sanitizers, hygiene

- [ ] CHK128 Is the stale waiver at `tests/wire/offset_table_error_path_test.cpp:10-14` **repaired, not merely re-pointed**? *(It is wrong twice: pre-063 line numbers, **and** a "provably unreachable" claim that is false for `:577` and, after this feature, for the relocated branch.)* [R-3, T019]
- [ ] CHK129 Are the Article IX §1 percentages **measured** at `/speckit-verify` with fresh per-binary profraw and recorded — not asserted from the "coverage improves" argument? [`[const §IX.1]`, T022]
- [ ] CHK130 Does the sanitizer matrix (ASan/UBSan/TSan) and static analysis pass with no new findings? *(No new constructs are introduced, so any finding is a signal the relocation was not semantic-preserving.)* [SC-008, T022]

## Close-out

- [ ] CHK131 Does the L-063-4 update record leg 2 **DELIVERED**, leg 1's descope evidence **intact**, and both surviving flat rules named with their **differing delimiter sources**? [FR-006, FR-007, FR-007a, SC-009]
- [ ] CHK132 Is the splitter named for its real home — **`group_slices_status()`**, not the delegating wrapper `group_slices()` (`:639-641`)? [FR-007, `/speckit-analyze`]
- [ ] CHK133 Are references anchored **function-and-role first**, line numbers stamped as-of the merge commit, historical brackets **byte-unchanged**? [FR-007b, SC-009]
- [ ] CHK134 Does the fixpp#220 row cite the issue **and** state the default-config unreachability with its `max_group_entries_per_instance < max_offset_entries - 1` precondition? *(Overstating fails SC-010 exactly as omitting does.)* [FR-003a, SC-010, T018]
- [ ] CHK135 Are the two mandatory close-out tasks done — catalogue/coverage-index flip, and the completeness audit recorded in the verify doc? *(`/gate-b` pre-flight 4d **hard-blocks** without the audit record.)* [`[const §XVII.8]`, T024, T025]
- [ ] CHK136 Does the close-out compute the **post-merge** open-issue count rather than copying a mid-feature figure? [T024, `/speckit-analyze` F3]

## Notes

- Dispositioned at `/speckit-checklist-audit` as PASS / SPEC-FIXED / DD-DECIDED §X / WAIVED:`<reason>`. Completeness/Clarity/Consistency gaps may **not** be WAIVED.
- CHK114–CHK121 exist because this exact section false-greened four times. Treat a green run of the procedure as evidence only after the guards themselves have been shown to fire.
