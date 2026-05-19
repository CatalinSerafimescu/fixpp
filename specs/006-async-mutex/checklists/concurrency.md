# Concurrency-Primitive Requirements-Quality Checklist: `fixpp::sync::async_mutex`

**Purpose**: Strict release-gate validation of requirements *quality* (completeness, clarity, consistency, measurability, coverage) for the awaitable mutex spec — the step-8.5 CHECKLIST AUDIT gate (`.specify/pipeline.md`), MANDATORY before `/speckit-implement`. Tests whether `spec.md` is well-written, NOT whether the implementation works.
**Created**: 2026-05-18
**Audited**: 2026-05-18 (step-8.5 gate, orchestrator self-audit vs spec.md × plan/tasks/data-model/contracts × design-doc anchor)
**Feature**: [spec.md](../spec.md) — anchored to `.specify/2f-async-mutex.md` v1.5 (design doc wins on conflict)

**Disposition legend**: `PASS` = spec (with the bundle's contracts/tasks per the project artifact-distribution pattern) already adequate, no action. `SPEC-FIXED` = `spec.md` edited this gate. `DD-DECIDED §X` = settled in the signed-off design doc (anchor rule) — recorded, not re-spec'd. `WAIVED:` = recorded, allowed only for non-Completeness/Clarity/Consistency items (none used).

## Concurrency Correctness — Completeness

- [x] CHK001 - Is the *observable* cancellation-vs-drain arbitration contract ("exactly one of {granted, cancelled} per waiter, resumed exactly once") stated at spec level independently of the design-doc §4.5 CAS mechanism, sufficient to author an acceptance test from the spec alone? [Completeness, Spec §FR-005/US2] — **PASS**: FR-005 + US2.2 + Edge Cases + SC-002 state the observable contract; §4.5 mechanism rightly delegated.
- [x] CHK002 - Are requirements defined for the "in-flight acquirer during cancel_and_drain (between drain-flag load and fast-path CAS)" window, including the observable termination condition that closes it? [Completeness, Spec §Edge Cases / §FR-006] — **PASS**: Edge-case + FR-006 — drain does not return until the acquirer count quiesces (DD §4.7 `active_acquirers_count_`).
- [x] CHK003 - Is the observable result for *losing* concurrent `cancel_and_drain()` callers specified (block until epoch completes vs. return immediately with the shared outcome)? [Gap, Spec §FR-006/US3.4] — **DD-DECIDED §4.7**: serialised single drain epoch; every caller's awaitable completes with the shared released-or-aborted outcome after quiesce (FR-006 "consistent result to every caller" is the observable).
- [x] CHK004 - Does the spec state which acquire/release memory-ordering pairings are normative behavioral requirements versus which are `static_assert`-only? [Completeness, Spec §FR-013] — **PASS**: FR-013 scopes the 3 `static_assert`s; the normative pairing table is `data-model.md` I-01..I-31 (design doc §6.2.2).
- [x] CHK005 - Are weak-memory (ARM64) correctness requirements expressed as a verifiable functional obligation, not only as the SC-007 outcome? [Coverage, Spec §FR-013/SC-007] — **PASS**: FR-013 (publish spec) + SC-007 + seam #18 (`tasks.md` T068).
- [x] CHK006 - Are recovery-path requirements defined after `sync_lock_alloc_failed` / `sync_lock_outside_session` (retry semantics, reusability, state invariant)? [Recovery, Gap] — **DD-DECIDED §4.7**: "mutex state unaffected" (FR-010/US4.3) ⇒ mutex remains acquirable/retryable; `sync_lock_outside_session` recovery = use the explicit `async_lock(mr)` overload (Edge Cases / FR-011).
- [x] CHK007 - Is the misuse contract for `unlock()` by a non-owner or with no waiters specified? [Gap] — **DD-DECIDED §4.5.2**: design doc owns "`unlock()` semantics" (§1.2) and the §4.5.2 unlock algorithm presumes a holder; the only sanctioned ownership path is the guard (`co_await async_lock()` → `async_lock_guard`), reinforced by the FR-001 try_lock closure. Direct non-owner `unlock()` is a precondition violation, not a supported path.

## Concurrency Correctness — Clarity & Measurability

- [x] CHK008 - Is "drain cycle" defined as a bounded unit so "FIFO fairness within a drain cycle" is objectively testable? [Clarity, Spec §FR-004] — **DD-DECIDED §2/§4.5.2**: a drain cycle = the LIFO waiter snapshot reversed-and-drained by one `unlock()` (design doc §2 L109, §4.5.2); FR-004's inline "(LIFO list reversed on unlock)" already carries that operative definition.
- [x] CHK009 - Is "lost wakeup" / "lost waiter" given an observable detection criterion? [Measurability, Spec §FR-005/SC-001/SC-002] — **PASS**: operationalized by SC-001/SC-002 + US1/US2 Independent Tests (granted+cancelled count == enqueued; no double-resume).
- [x] CHK010 - Is "all holders, in-flight acquirers, and in-flight resumptions have quiesced" defined with an observable terminal condition? [Clarity, Spec §FR-006/US3.1] — **PASS**: US3 Independent Test — returns only when holder/acquirer/resumption counts reach zero (DD §4.7 counters).
- [x] CHK011 - Is the inline-vs-`post` predicate stated precisely enough to be testable, including the per-mutex `post` opt-in mechanism? [Clarity, Spec §FR-004] — **DD-DECIDED §4.6.2/§4.1**: FR-004 states the predicate verbatim (`running_in_this_thread()`); opt-in = construction-time `completion_policy` (DD §4.1; `tasks.md` T013 `explicit constexpr async_mutex(completion_policy)`).
- [x] CHK012 - Are SC-001/SC-002 "stress corpus"/"cancellation runs" quantified? [Measurability, Spec §SC-001/SC-002] — **PASS** (bundle-resolved): N is pinned in the seams (#6 ≥10⁴ coroutines, `tasks.md` T019); SC text is intentionally seam-referencing per the project artifact-distribution pattern. Measurability item — not a Completeness/Clarity/Consistency blocker.
- [x] CHK013 - Is the hard-ceiling vs "bench-harness-soft" boundary documented? [Ambiguity, Spec §SC-005 / DD §6.3] — **PASS**: SC-005 explicitly partitions hard ceilings (25/80/15 ns, CI fails >5%) from the bench-soft long-tail rows.
- [x] CHK014 - Are SC-005 measurement conditions fully pinned (statistic, iteration count)? [Measurability, Spec §SC-005] — **PASS** (bundle-resolved): percentile/iteration model owned by the bench harness (`tasks.md` T071 + `tools/bench_compare.py`, DD §6.3). Measurability item, non-blocking.

## API Contract & Lifecycle — Requirements Quality

- [x] CHK015 - Is the complete public surface specified at signature level? [Completeness, Spec §FR-001] — **PASS**: full signatures are the `contracts/` shape oracles (project signature authority); FR-001 is name/contract-level by design.
- [x] CHK016 - Is `async_lock_guard` movability fully specified (move-construction + post-move source state)? [Gap, Spec §FR-003] — **DD-DECIDED §4.4**: public move-ctor + destructive move-assign + post-move disengaged source (DD §4.4 L887-906; `tasks.md` T012).
- [x] CHK017 - Is the `std::terminate()` destructor precondition "still has waiters" precisely scoped? [Clarity, Spec §FR-008/US3.3] — **SPEC-FIXED**: FR-008 + the Edge Cases bullet now read "a live **holder or** any waiters" (matches DD §4.7 "both debug AND release", `tasks.md` T050 `state_`/`next_drain_head_`); US3.3 noted as a non-exhaustive acceptance instance.
- [x] CHK018 - Is double-`release()` / `release()`-after-move / non-owning `release()` specified? [Edge Case, Gap, Spec §FR-003] — **DD-DECIDED §4.4**: `owns_lock()` guard + self-assign no-op + `[[nodiscard]] release()` no-op when disengaged (DD §4.4; data-model E3; `tasks.md` T012).
- [x] CHK019 - Is the `completion_policy` selection mechanism consistent between FR-001 `policy()` and FR-004 "per-mutex opt-in"? [Consistency, Spec §FR-001/FR-004] — **DD-DECIDED §4.1**: policy fixed at construction (default `dispatch`, `explicit` ctor opt-in), immutable, queried via `policy()` (DD §4.1; `tasks.md` T013). FR-001/FR-004 are consistent under that mechanism.
- [x] CHK020 - Do FR-001, FR-015, and the Clarification resolve the `try_lock()` public-surface exclusion without residual conflict? [Conflict, Spec §FR-001/§FR-015/§Clarifications] — **PASS**: explicitly reconciled in Clarifications 2026-05-18; FR-001/FR-015/`tasks.md` T013/T288 + DD §4.1 (Opus N-P3-1) all agree — no public `try_lock()`.
- [x] CHK021 - Is the declared session-side helper contract precise enough to compile against with no implementation? [Completeness, Spec §FR-011/SC-008] — **PASS**: exact signature in `contracts/async_lock_via_session_executor.hpp` + `tasks.md` T014; verified by the T078 consumer compile/link check (SC-008 / `[2e §3.1]` hand-off).
- [x] CHK022 - Is "v1.0 hot path" enumerated identically across FR-009/SC-004/§XI.5? [Consistency, Spec §FR-009/SC-004] — **PASS**: "acquire/contend/cancel/drain" identical in FR-009/SC-004/US4; §XI.5 is the driver, not a divergent enumeration.

## Allocation Discipline — Requirements Quality

- [x] CHK023 - Is the trigger for the explicit `async_lock(mr)` overload defined objectively? [Clarity, Gap, Spec §FR-010/US4] — **DD-DECIDED §4.1**: it is an **explicit caller-elected overload** (`async_lock(std::pmr::memory_resource* mr = nullptr)`, DD §4.1 / §1.2 item 2 RC#2), not a runtime auto-fallback. FR-010's normative text already says "explicit overload"; the loose US4 narrative phrasing is non-normative.
- [x] CHK024 - Is "mutex state unaffected" after `sync_lock_alloc_failed` given an observable invariant? [Measurability, Spec §FR-010/US4.3] — **DD-DECIDED §4.7**: trapped via `trap_throw` with no enqueue / no epoch change / mutex still acquirable (DD §4.7; `tasks.md` T059); FR-010/US4.3 carry the observable.
- [x] CHK025 - Is the awaiter inline-buffer capacity stated, and over-budget handler behavior specified? [Gap, Spec §FR-009 / Key Entities] — **DD-DECIDED §4.2**: inline `slot_storage_` = 32 B (DD §4.2 L70; `tasks.md` T011 `slot_storage_[32]` + `sizeof ≤ 96 B` `static_assert`); the handler closure fits by construction (compile-time asserted), so there is no runtime over-budget path.
- [x] CHK026 - Are embedded zero-alloc and `mr`-path requirements independently measurable (harness contract defined)? [Measurability, Spec §SC-004] — **PASS**: US4 Independent Test defines the alloc-counting harness + instrumented `memory_resource` contract.

## CI Enforcement Gate — Requirements Quality

- [x] CHK027 - Is the finalized gate capability specified distinctly from the 005-Phase-1 scaffold? [Completeness, Spec §FR-014 / Assumptions] — **PASS** (reinforced by CHK037 SPEC-FIX): FR-014/SC-006/US5 (`tasks.md` T065-T067) enumerate corpus + diagnostic + CI wiring as this feature's deliverable.
- [x] CHK028 - Is the labelled corpus's required composition defined? [Measurability, Gap, Spec §SC-006] — **PASS**: US5 Independent Test defines the TP (std::mutex in awaitable header → must fail) and TN (async_mutex in awaitable header → must pass) categories; seam #14 = 3 explicitly-named fixtures.
- [x] CHK029 - Is the banned-symbol set fully enumerated rather than "etc."? [Ambiguity, Spec §FR-014] — **SPEC-FIXED**: FR-014 now enumerates exactly six `std::`-qualified types (`mutex`, `recursive_mutex`, `timed_mutex`, `recursive_timed_mutex`, `shared_mutex`, `shared_timed_mutex`) and records the alias/`using` out-of-grep-scope limitation explicitly (no longer a hidden SC-006 false-negative risk).
- [x] CHK030 - Is the `asio::awaitable<...>`-include detection scope precise? [Clarity, Gap, Spec §FR-014/US5] — **PASS**: `tasks.md` T015 + FR-014 (now) pin post-preprocessing (`-E`, transitive) scope — resolves macro-guarded/commented-out ambiguity.

## Cross-Artifact Consistency & Traceability

- [x] CHK031 - Are all spec references to design-doc sections verifiable as existing in `2f-async-mutex.md` v1.5? [Traceability, Consistency] — **PASS — ANCHOR SPOT-CHECK VERIFIED 2026-05-18**: §4.1 (L302), §4.5/§4.5.1/§4.5.2 (L948/961/973), §6.2/§6.2.2 (L1376/1426), §6.3 (L1467), RC#1/RC#3/RC#4, N-P1-3/N-P3-1 all present; doc Status = "Draft v1.5". Zero dangling §/RC anchors.
- [x] CHK032 - Is the SC-009 verbatim-match claim verifiable? [Consistency, Assumption, Spec §SC-009] — **PASS**: already reconciled in Gate A round 2 (`plan.md` notes — SC-009 scope consistent across spec/plan/quickstart).
- [x] CHK033 - Are the four error variants uniquely mapped to one FR each, with the slot assignment a requirement? [Completeness, Traceability, Spec §FR-012 / data-model] — **PASS**: `sync_lock_aborted`→FR-005, `_alloc_failed`→FR-010, `_outside_session`→FR-011, `_drained`→FR-007; slots 43–46 a requirement (`tasks.md` T006). No orphan/unreachable variant.
- [x] CHK034 - Is the `[const §IX.5]` abidiff "N/A" a traceable recorded exclusion consistent with FR-012? [Consistency, Spec §Assumptions/§FR-012] — **PASS**: recorded in Assumptions + `plan.md` L111, consistent with FR-012 "no C-ABI surface".
- [x] CHK035 - Is every SC traceable to ≥1 FR and ≥1 user story, with an ID scheme? [Traceability] — **PASS**: FR/SC/US scheme present; SC-005/008/009/010 legitimately NFR/process-level (no US required); tasks cite FR/SC.
- [x] CHK036 - Does every Edge Case map to an FR/acceptance scenario (incl. recursive-acquire observable)? [Coverage, Gap, Spec §Edge Cases] — **DD-DECIDED §2**: all edge cases map to an FR; recursive acquire is "unsupported by construction" (DD §2; FR-015 out-of-scope) — the observable consequence (holder self-suspends, never granted) is the documented non-feature, not a missing requirement.

## Dependencies, Assumptions & Acceptance-Criteria Quality

- [x] CHK037 - Is the "005 Phase 1 scaffolded the grep gate" precondition a validated dependency or an unverified assumption? [Assumption, Dependency, Spec §Assumptions/§FR-014] — **SPEC-FIXED**: branch state verified 2026-05-18 — the script is **absent on `main` and on `006-async-mutex`**, present only on the unmerged `005` branch (commit `d6a17b7`). The false "committed on 005 branch / main" claim is corrected in Assumptions + FR-014: this feature creates it from scratch (T015) and owns/finalizes it. Spec↔tasks contradiction resolved.
- [x] CHK038 - Are the consumed `[2d §7.4]`/`[2d §4.8]` contracts reproduced enough for FR-002/FR-011 self-containment? [Dependency, Gap, Spec §FR-002/§FR-011/§Assumptions] — **PASS**: Normative References reproduce the `[2d §7.4]` essence (completion on bound executor; `cancellation_type::total`; dispatch/post); `contracts/` pins the shapes; 2d ships next (recorded, not re-litigated).
- [x] CHK039 - Is the SC-008 consumer contract shape reproduced enough to author the check? [Traceability, Gap, Spec §SC-008] — **PASS**: `tasks.md` T078 fully enumerates the positive/negative `[2e §6.4]` writer-mutex contract asserts.
- [x] CHK040 - Is the SC-003 death test reconciled with the SC-009 coverage floor? [Consistency, Spec §SC-003/§SC-009] — **PASS**: `tasks.md` T072 reconciles — the `std::terminate()` precondition is a written coverage-justified unreachable exclusion, not an uncovered-line failure.

## Audit Disposition Summary (step-8.5 gate, 2026-05-18)

| Disposition | Count | Items |
|---|---|---|
| PASS (spec/bundle adequate; incl. CHK031 anchor-verified) | 25 | 001 002 004 005 009 010 012 013 014 015 020 021 022 026 027 028 030 031 032 033 034 035 038 039 040 |
| DD-DECIDED §X (design-doc anchor; recorded, not re-spec'd) | 12 | 003 006 007 008 011 016 018 019 023 024 025 036 |
| SPEC-FIXED (spec.md edited this gate) | 3 | 017 029 037 |
| WAIVED | 0 | — |

**Zero un-dispositioned `[ ]` boxes — gate PASSES.** No Completeness/Clarity/Consistency item was WAIVED (rule satisfied). CHK031 ACTION discharged: design-doc anchors spot-checked clean against `2f-async-mutex.md` v1.5.

**Gate consequence:** 3 items were SPEC-FIXED (`spec.md` FR-008, FR-014, Edge Cases bullet, Assumptions). Per pipeline step 8.5, **`/speckit-analyze` (step 6) must be re-run before `/speckit-implement`** — the spec edits invalidate the prior cross-artifact drift check (`tasks.md` T006/T015/T050/T072 and `data-model`/`contracts` already match the corrected text, so re-`/analyze` is expected to be clean, but it is mandatory, not optional).

## Notes

- Check items off as resolved: `[x]`; the inline disposition tag IS the record (no separate decision doc — the checklist file is the step-8.5 gate artifact).
- Anchor rule: a spec/design-doc disagreement is a defect in the spec; DD-DECIDED items defer to `2f-async-mutex.md` v1.5 as authoritative.
- Strict-gate intent: Completeness/Clarity/Consistency items must be SPEC-FIXED or DD-DECIDED — never WAIVED (enforced; 0 WAIVED here).
- Traceability: 40/40 items carry a spec §ref or `[Gap]`/`[Ambiguity]`/`[Conflict]`/`[Assumption]` marker (≥80% satisfied).
