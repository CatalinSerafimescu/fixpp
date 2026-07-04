# Gate checklist: 060-int128-decimal-compare (audience: Gate B)

**Purpose**: probe whether the bundle fully specifies the risk-bearing parts of a semantics-preserving
`decimal_traits<pod_decimal>::compare` swap that reverses the 2a `no-__int128` Gate-A decision. Each CHK
is dispositioned by `/speckit-checklist-audit` (PASS / SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>).
Ref = the governing spec requirement / design-doc section.

## A. Correctness & soundness of the replacement (the heart)

- [x] CHK001 Is **bit-identical `strong_ordering`** vs the retained digit-string reference asserted over a full-domain corpus (not a subset)? [FR-001, SC-001, US1] — PASS: FR-001/FR-010 mandate a fixed-seed full-domain (mantissa ∈ [INT64_MIN+1, INT64_MAX], exponent ∈ [−38,0] + out-of-domain int8) boundary-biased corpus as a hard Tier-1 gate; `tasks.md` T003/T006 wire it against the frozen reference. `spec.md` FR-001/FR-010, `research.md` R4, `data-model.md` "Deterministic corpus".
- [x] CHK002 Is the `k ≥ 19` **dominance bound** proven (`|m|·10^19 > INT64_MAX`) with its **zero + equal-sign premises** established *before* the branch is taken? [FR-004, research R1] — PASS: `research.md` R1 states `k≥19 ⇒ |m|·10^19 ≥ 10^19 > INT64_MAX ≥ |other|`, explicitly premised on the zero-filter and sign-filter running first; `data-model.md` control-flow diagram orders sentinel→sign→R3-hoist→zero-filter→`k` before the `k≥19` branch. Design-note `msvc-int128-decimal.md §2` confirms the same ordering.
- [x] CHK003 Is the `k ≤ 18` **product overflow bound** stated with the worst case (`(2^63−1)·10^18 < 2^123 < 2^128`)? [FR-005, research R1] — PASS: `research.md` R1 states `|m|·10^k ≤ (2^63−1)·10^18 < 2^122.8 < 2^128` (>5 bits headroom); design-note `msvc-int128-decimal.md §2` derives the identical bound. `data-model.md` `mul_u64_wide` "Range guarantee" row restates `< 2^123`.
- [x] CHK004 Is exact **equality / canonical-equivalence** specified in **both scale directions** (higher-exponent side scaled; `{100,−2}`={1,0} and the reverse)? [US1 AC3, data-model] — PASS: `spec.md` US1 AC3 gives the named example; the `a_scales` selector (`data-model.md` control flow) is validated regardless of direction by the same equality witness (a backwards `a_scales` pick scales the wrong operand and breaks equality outright — it is not order-symmetric-safe); the order-*sensitive* invert-branch defect (the actual reason "both directions" matters) is separately, explicitly mutation-tested by `research.md` R5 rows 2/3/5 ("both orders/directions/signs") which flow through the identical `mag_cmp = a_scales ? … : invert(…)` ternary. FR-010's full-domain random corpus additionally exercises both directions generically for equality pairs.
- [x] CHK005 Is the **negative-operand sign-flip** specified for **both** the dominance arm AND the multiply arm (the Gate-B P1 #1 same-bucket-negatives shape)? [US2, spec Edge Cases] — PASS: `data-model.md` control flow applies `return a_neg ? invert(mag_cmp) : mag_cmp` uniformly AFTER `mag_cmp` is computed by either the `k≥19` dominance branch or the `k≤18` multiply branch — one flip site covers both arms. `research.md` R5 row 3 (k-boundary, dominance AND multiply arms) explicitly says "both scale directions/signs"; row 5 (extremes) targets the sign-flip-omission mutant directly (the P1 #1 shape).
- [x] CHK006 Is the **`hi`-limb-must-be-consulted** invariant specified AND witnessed with a product **≥ 2^64** (the unsigned threshold — NOT the superseded `2^63`/`−17` witness)? [FR-005, SC-002 row 2] — PASS: `data-model.md` `mul_u64_wide` "Invariant under test" row and `research.md` R5 row 2 both pin `{99,0}` vs `{9223372036854775807,−18}` (product `9.9e19 ≥ 2^64`, `hi=5≠0`), with an explicit correction note superseding design-note `msvc-int128-decimal.md §6` row 2's weaker `2^63`/`…,−17}` value (verified: the design note's row 2 does say `{9223372036854775807,−17}`/"crosses 2^63" — confirmed stale). This is a bundle **override** of the design note, so PASS (not DD-DECIDED — the note's stale value is not authoritative here). `tasks.md` T007 explicitly forbids restoring the superseded witness.
- [x] CHK007 Is **totality over out-of-canonical `int8` exponents** specified (delta > 38; `k` computed in `int` so no int8 overflow)? [FR-006, US2 edge cases] — PASS: `spec.md` Edge Cases + FR-006 require totality beyond `[−38,0]` with `k` computed in `int`; `data-model.md` "canonical exponent domain... total beyond it"; `research.md` R5 row 7 + `tasks.md` T007 row 7 directed witness (`{5,+7}` vs `{5,0}`, delta>38 via int8 extremes).
- [x] CHK008 Are the **sentinel filter, sign filter, and R3 same-exponent hoist** specified as preserved **verbatim**? [FR-003] — PASS: `spec.md` FR-003; `data-model.md` control flow marks all three "UNCHANGED"; `tasks.md` T005 "Preserve verbatim the sentinel filter, sign filter, and the R3 same-exponent hoist." Confirmed live source (`src/core/decimal.cpp:244+`) shows these as the current pre-change filters, matching the scope guard.

## B. Verification vehicle

- [x] CHK009 Is the **fixed-seed deterministic corpus** the hard always-on Tier-1 gate (reproducible, SC-001 provable on a normal run)? [FR-010, /clarify] — PASS: `spec.md` Clarifications session (2026-07-04) + FR-010 "hard always-on gate (runs in Tier-1; reproducible so SC-001 is provable on a normal CI run)"; `tasks.md` T003 "Document the seed."
- [x] CHK010 Is the **differential libFuzzer** target specified with trap-on-mismatch AND a demonstrated trap on a deliberate mutant? [FR-010a, SC-006] — PASS: `spec.md` FR-010a/SC-006; `tasks.md` T010 (`__builtin_trap` on mismatch + demonstrated trap). Realizability confirmed live: `tests/fuzz/CMakeLists.txt` has an established per-target pattern (`fuzz_decimal_parse` links `fixpp_core`, `-fsanitize=fuzzer,address[,undefined]`) that `fuzz_decimal_compare` slots into directly; `tests/fuzz/fuzz_decimal_parse.cpp` is a directly-analogous existing harness.
- [x] CHK011 Is **every soundness-critical witness mutation-tested** (proven to kill its named mutant), with the `19→20` guard mutant's disposition recorded accurately? [FR-011, SC-002] — PASS (MEASURED-SUPERSEDES-PREDICTED): `spec.md` FR-011/SC-002; `research.md` R1 guard-constant note + R5 rationale; `tasks.md` T009 enumerates the mutant list. The ex-ante prediction that `19→20` is a benign "semantically-equivalent no-kill" was **corrected by T009 measurement**: `19→20` is result-equivalent but **killed via the ASan lane** (reads `kPow10[19]` OOB — guard constant `19` and the exactly-sized `kPow10` table are jointly load-bearing for memory safety). The mutation-testing requirement is satisfied *more* strongly than predicted (a predicted survivor is a kill); spec/research/tasks/kill-table were surgically corrected mid-`/implement`, and a directed k=19 correctness cell was added to the witness matrix so the boundary is discriminated by the matrix, not just the seed-42 corpus.
- [x] CHK012 Is the existing **Python-`Decimal` oracle extended** with cross-exponent pairs (augment, not rebuild)? [FR-010] — PASS: `spec.md`/`research.md` R4 "augment `_canonical_pairs` rather than build a new one"; `tasks.md` T006. Realizability confirmed live: `tests/oracle/decimal_compare_oracle_test.py` has exactly a `_canonical_pairs(n, seed=42)` generator feeding `@pytest.mark.parametrize`, directly augmentable.
- [x] CHK013 Is the retained reference a **frozen copy** of the pre-swap comparator (never re-synced to the new impl, else the oracle is vacuous)? [data-model, tasks T003] — PASS: `data-model.md` "Never changes with the impl"; `tasks.md` Phase 2 CRITICAL note ("T003 must be written against the current `compare` before T005 alters it") + closing Notes section ("never re-sync it to the new impl").

## C. Surface invariance / ABI

- [x] CHK014 Is **zero public / C-ABI / wire / error / layout change** asserted AND gated (empty `decimal.hpp` diff + `abidiff` decimal PoD byte-identical)? [FR-007, SC-004, const §X.3/§IX.5] — PASS: `spec.md` FR-007/SC-004; `tasks.md` T019 "confirm `git diff include/fixpp/core/decimal.hpp` is empty and ... `abidiff` ... decimal PoD byte-identical." CodeGraph-confirmed `pod_decimal`/`decimal_traits<pod_decimal>::compare` symbols exist with the signature the contract freezes (`include/fixpp/core/decimal.hpp:22`, `src/core/decimal.cpp:244`).
- [x] CHK015 Is **`noexcept` + zero-allocation + O(1) no-loops/no-divisions** preserved on the new path? [FR-006, const §XV.1] — PASS: `spec.md` FR-002/FR-006; `plan.md` Constitution Check XV§1 row; `data-model.md` "Zero loops, zero divisions, noexcept, O(1)."

## D. Portability (MSVC lane — L-049-3 shape)

- [x] CHK016 Is **one algorithm / per-compiler primitive only** specified (no MSVC-vs-Clang **algorithm** split; `#if` at the multiply instruction only)? [FR-008] — PASS: `spec.md` FR-008; `research.md` R2; `plan.md` Constitution Check II§2/4 row. Anchor spot-verified: `.specify/2a-decimal.md:375` carries "no MSVC-vs-Clang algorithm split" verbatim (retained per FR-009/R6 item 1).
- [x] CHK017 Are the MSVC `_umul128`/`__umulh` **signatures confirm-at-implement** (vs MS `<intrin.h>` docs) AND the **Tier-2 `run-tier2` run** required? [FR-012, research R2] — PASS: `spec.md` FR-012; `research.md` R2 "Residual obligation (confirm at implement...)"; `tasks.md` T012 (signature confirm) + T014 (`run-tier2` trigger). Per project dependency-pin discipline, T012 correctly defers to implementation-time verification rather than propagating the design note's from-memory signatures.
- [x] CHK018 Is the portable **`#else` fallback coverage mechanism** (forced-portable build define) specified, since no Linux lane compiles the `#elif`/`#else`? [FR-012, const §IX.1] — PASS: `research.md` R2 "Coverage mechanism"; `plan.md` Constitution Check IX§1 row; `data-model.md` "Forced-portable build" entity; `tasks.md` T002 (CMake option) + T013 (forced build + oracle run). Realizability confirmed live: `CMakeLists.txt` already has the identical pattern (`FIXPP_FORCE_ATOMIC_SHARED_PTR_FALLBACK`/`_NATIVE` options), so `FIXPP_DECIMAL_FORCE_PORTABLE_MUL` is a direct precedent-following addition, not a novel mechanism.

## E. Contract amendment (the Gate-A reversal — mandatory, in-PR)

- [x] CHK019 Are all **5 amendment files enumerated identically** across FR-009 / research R6 / contract checklist / tasks T015–T017, each **leading with the bound proof**? [FR-009] — SPEC-FIXED: the 5 files WERE enumerated identically, but a real cross-repo defect was found and fixed. `research/G19-fix-fpml-iso20022/phases/phase-9/.../02-lowlatency-recommendations.md` and `remaining-work/perf-and-hardening-findings.md` (sites 4–5) live in the **parent monorepo**, outside this library submodule's git tree (confirmed via `git ls-files`) — they physically cannot be part of "the library PR" the way sites 1–3 can. The bundle was internally inconsistent: `contracts/compare-contract.md` already flagged site 5 as "post-merge close-out" but not site 4 (identical constraint, untagged); `spec.md` FR-009 and `tasks.md`'s Implementation-Strategy note both said all 5 (or T015–T017) "MUST land in the same PR", contradicting site 5's own post-merge tag and structurally impossible for sites 4–5. Fixed by adding an explicit repo-split clause (sites 1–3 same-PR / sites 4–5 separate post-merge parent-repo commit, precedented by the 059 `remaining-work` close-out `790e6b1` after `f0d4876`) to `spec.md` FR-009, `research.md` R6, `contracts/compare-contract.md`, `plan.md` (Project Structure comment), and `tasks.md` (T016/T017 annotations + Implementation-Strategy + T022 assertion (iv)). SC-005's pre-existing "after merge" wording required no change — it already matched the correct timing.
- [x] CHK020 Is **SC-005 grep-verifiable** (no site frames C1 as opt-in low-latency MODE after merge)? [SC-005] — PASS: `spec.md` SC-005 names concrete grep targets (`02-lowlatency-recommendations.md` Tier-C entry + preamble caveat + rejected-`__int128` bullet; `remaining-work/perf-and-hardening-findings.md` `:62`/`:72`/`:141`) and is correctly scoped to "after merge" — this already matches the sites-4–5 post-merge timing fixed under CHK019, so no further edit was needed here. `tasks.md` T022 (as SPEC-FIXED under CHK019) now correctly defers the sites-4–5 grep to the tracked post-merge obligation rather than a false pre-merge blocker.
- [x] CHK021 Is the **"no MSVC-vs-Clang algorithm split" §6.3 virtue** re-affirmed in the amendment (only the unsound-scale text retired)? [FR-009, 2a §6.3] — PASS: `research.md` R6 item 1 + `contracts/compare-contract.md` site 1 + `tasks.md` T015 all say "retire 'No multiplication, no wide-int dependency'; keep/re-affirm 'no MSVC-vs-Clang algorithm split'". Anchor spot-verified at `.specify/2a-decimal.md:375`.

## F. Perf gate

- [x] CHK022 Is the **bench-baseline update in-PR** + **deterministic callgrind Ir** gate specified (diff-exp Ir decreases; same-exp/diff-bucket no-regression ±5%; WSL2 ns shape-only)? [NFR-001, SC-003, const §VIII.2/3] — PASS: `spec.md` NFR-001/SC-003; `research.md` R3; `plan.md` Constitution Check VIII§2/3 row; `tasks.md` T001 (pre-change baseline capture) + T018 (in-PR baseline update + regression check). Confirmed live: `bench/baselines/decimal_baseline.json` and `BM_decimal_compare`/`_diff_exp`/`_diff_bucket` all exist in `bench/core/decimal_bench.cpp`.

## G. Close-out (Gate-B preconditions, Article XVII §8)

- [x] CHK023 Are **both** mandatory close-out tasks present as the last Polish tasks — catalogue/coverage-index/B&L (T021) AND the feature-completeness audit as the FINAL task (T022)? [const §XVII.8] — PASS: `tasks.md` has T021 (catalogue/coverage-index/B&L, in-submodule `spec/*.md` — confirmed via `git ls-files` to be inside this repo, so no cross-repo timing issue) immediately followed by T022 (FINAL, Gate-B precondition). T022's content was SPEC-FIXED under CHK019 to correctly scope its (iv) assertion to the in-submodule amendment sites; its presence/ordering as the last two Polish tasks is otherwise unaffected and PASSes.

## Notes
- Items marked SPEC-FIXED by the audit loop back to `/speckit-analyze`.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 22 |
| SPEC-FIXED | 1 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 23 |

### SPEC-FIXED items
- CHK019 — cross-repo timing/consistency defect: FR-009 mandated all 5 contract-amendment files "land in the same PR", but 2 of the 5 (`02-lowlatency-recommendations.md`, `remaining-work/perf-and-hardening-findings.md`) live in the parent monorepo, outside this library submodule's git tree, so they structurally cannot be part of the library PR. The bundle was also internally asymmetric (site 5 tagged "post-merge close-out", site 4 — identical constraint — untagged). Fixed with an explicit repo-split clause (sites 1–3 same-PR / sites 4–5 separate post-merge parent-repo commit, precedented by the 059 close-out) in: `spec.md` (FR-009), `research.md` (R6), `contracts/compare-contract.md`, `plan.md` (Project Structure comment), `tasks.md` (T016/T017 annotations, Implementation-Strategy note, T022 assertion (iv)). SC-005 needed no change (already scoped "after merge").

### DD-DECIDED items
None.

### WAIVED items
None.

Anchors spot-verified — all resolve in the cited (signed-off) revisions:
- `.specify/2a-decimal.md §6.3` — line 375, "No multiplication, no wide-int dependency, no MSVC-vs-Clang algorithm split" — confirmed present verbatim.
- `specs/001-core-decimal/research.md` D-5 — lines 45–49 — confirmed present.
- `src/core/decimal.cpp` contract comment — lines 240–242 — confirmed matches the `:240-242` cite used throughout the bundle (post an earlier, already-applied correction from a stale `:236-238`).
- Design note `research/G19-fix-fpml-iso20022/phases/phase-9/perf-investigation/findings/msvc-int128-decimal.md` — §2 (bound proof), §3 (algorithm), §4 (MSVC options), §6 (witness matrix — row 2 confirmed to carry the stale `2^63`/`{…,−17}` value the bundle correctly supersedes), §8 (amendment list) — all resolve.
- `research/G19-fix-fpml-iso20022/phases/phase-9/perf-investigation/02-lowlatency-recommendations.md` — C1 Tier-C entry (:365), preamble caveat (:354-362), rejected-`__int128` bullet (confirmed at :600-611, not the stale :357-359) — all resolve; NOT yet amended (expected — T016 is a post-merge task).
- `research/G19-fix-fpml-iso20022/remaining-work/perf-and-hardening-findings.md` — Cluster-2 residual line (:62), C1 table row (:72), Low-latency MODE list entry (:141) — all resolve; NOT yet amended (expected — T017 is a post-merge task).

### Realizability sub-check
No new value-typed entity with a forward-declared-dependency member is introduced by this feature.
`pod_decimal` (`include/fixpp/core/decimal.hpp:22`) is an already-complete POD (`int64_t`/`int8_t` members
only); `mul_u64_wide` is a free function (not a class); `kPow10` is a `constexpr` array. **Verdict: clean,
no realizability gap.** CHK010 (differential fuzzer) and CHK018 (forced-portable coverage) additionally
verified buildable against live precedent: `tests/fuzz/CMakeLists.txt`'s existing per-target pattern and
`CMakeLists.txt`'s existing `FIXPP_FORCE_ATOMIC_SHARED_PTR_{FALLBACK,NATIVE}` option pattern.

### CodeGraph / live-source lookups performed
- `pod_decimal` — confirmed struct at `include/fixpp/core/decimal.hpp:22` (POD, no fwd-declared members).
- `decimal_traits<pod_decimal>::compare` — confirmed at `src/core/decimal.cpp:244`, called from `src/capi/decimal.cpp:111,132`.
- `BM_decimal_compare` / `BM_decimal_compare_diff_exp` / `BM_decimal_compare_diff_bucket` — confirmed in `bench/core/decimal_bench.cpp`.
- `codegraph status` — 880 files / 21,478 nodes indexed for the submodule (sanity check the index is live).

### Re-run /speckit-analyze?
**YES** — CHK019's SPEC-FIXED edit touched `spec.md` (FR-009), `research.md` (R6), `contracts/compare-contract.md`, `plan.md`, and `tasks.md`. The prior `/speckit-analyze` PROCEED verdict predates this edit and must be re-run before `/speckit-implement`.
