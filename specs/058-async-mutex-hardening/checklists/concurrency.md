# Concurrency & Memory-Safety Requirements Checklist: 058-async-mutex-hardening

**Purpose**: Validate that the REQUIREMENTS/design for the async_mutex hardening are complete, clear,
consistent, and measurable — the "unit tests for English" gate before `/implement`. Audience: Gate B
reviewers. Depth: formal release gate.
**Created**: 2026-07-02
**Feature**: [spec.md](../spec.md) · [research.md](../research.md) · [contracts/](../contracts/)

## AM-P1 memory-ordering & free-list correctness

- [x] CHK001 Is the AM-P1 free-list pop's ordering specified per step (head acquire-load, free-link acquire-load, CAS with generation bump)? [Completeness, research D-1] — PASS: research.md D-1 "Pop" gives the exact 3-step sequence (`load(acquire)` head → `free_link.load(acquire)` → `CAS(acq_rel, acquire)` with `gen+1`); matches the planned replacement of `async_mutex.hpp:808-816`.
- [x] CHK002 Is it stated WHY the generation bump belongs on the pop only (not the push), i.e. is the ABA-invalidation argument written down? [Clarity, research D-1] — PASS: research.md D-1 states the monotonic-generation argument ("a stale head observed after a pop-pop-push cycle carries the old generation, the CAS fails, retry") and the Push step explicitly "carries" (does not bump) the loaded generation — the asymmetry and its justification are written down.
- [x] CHK003 Is the requirement that "no concurrent read of a slot's link field may conflict with that slot's reuse" stated as a testable property, distinct from the ABA property? [Completeness, Spec §FR-001] — PASS: FR-001 states both properties as separate clauses ("no allocation may return a slot that holds a live parked record" vs "no concurrent read of a slot's link field may conflict with that slot's reuse"); tested distinctly by SC-001 (no double-grant) and SC-003 (TSan clean on reuse path).
- [x] CHK004 Is the generation bit-width (54) justified against wrap-reachability with a quantified bound, not asserted as "unreachable"? [Measurability, research D-1] — PASS: research.md D-1 quantifies "2⁵⁴ pops... unreachable (>50 yr at 10M contended pops/s)", contrasted with the reachable 2³² wrap of the sibling counter (~7 min, D-4).
- [x] CHK005 Is the empty-list sentinel value defined unambiguously (which index encodes "empty")? [Clarity, research D-1] — PASS: research.md D-1 "Sentinel: `index == 0x3FF` = empty list" (10-bit `slot_index` field, 512 valid slots + reserved sentinel).

## Free-link object lifetime

- [x] CHK006 Does the design specify that the free-link is PERSISTENT slot metadata whose lifetime is the mutex's, NOT a `waiter_record` member? [Completeness, research D-1 / data-model] — PASS: research.md D-1 and data-model.md both state the link is constructed once with `waiter_pool_storage_{}` and lives for the whole mutex, hosted in `waiter_pool_slot`, not `waiter_record`.
- [x] CHK007 Is the exact failure mode of a record-member link (destroy at `:659` before push; placement-new re-init at `:831`) documented as the reason for the slot-metadata design? [Clarity, gatea record] — PASS: research.md D-1 + gatea record finding #1 (BLOCKER) cite exactly these two anchors; spot-verified against `include/fixpp/core/sync/async_mutex.hpp` — line 659 is `record->~waiter_record();`, line 831 is `::new (record) waiter_record{};` — both resolve exactly as described.
- [x] CHK008 Is there a requirement that no free-link access occurs across a record's construct/destroy boundary? [Coverage, research D-1] — PASS: data-model.md "Free-link link semantics" states a record is never simultaneously chained and free (no aliasing hazard, Gate-A both reviewers), and D-1 states the link is never touched by the per-waiter placement-new — the link is orthogonal to record lifetime by construction.
- [x] CHK009 Is the rejected `next_`-type-pun fallback explicitly marked unsafe (same lifetime class), so it cannot be reintroduced? [Consistency, research D-1] — PASS: research.md D-1 Rejected alternatives (b) "type-pun `next_` storage as an atomic-index-while-free — same lifetime-churn class, worse... rejected."

## Layout-golden & invariant preservation

- [x] CHK010 Is the requirement that `sizeof(waiter_pool_slot)==256` after the restructure stated with the arithmetic (248 storage + 4 link → align-16 → 256)? [Measurability, data-model] — PASS: data-model.md member table states exactly "still 256 B, 248+4 rounded to align 16".
- [x] CHK011 Is the tightened `sizeof(waiter_record) <= 248` static-assert specified, replacing the prior `<= 256`? [Completeness, research D-1] — PASS: research.md D-1 + tasks.md T005 both specify the tighten at `async_mutex.hpp:619`; spot-verified — line 619 currently reads `static_assert(sizeof(waiter_record) <= 256, ...)`, confirming the anchor and the pre-fix value the task will tighten.
- [x] CHK012 Is "no change to `sizeof(async_mutex)==131120` / `alignof==16`" a stated hard invariant with an escape clause (FR-011 flag-if-required)? [Consistency, Spec §FR-011] — PASS: FR-011 states the golden MUST remain unchanged "unless a fix strictly requires otherwise; any required change MUST be explicitly called out and justified."
- [x] CHK013 Is "no new `std::mutex`" / no-std-mutex-gate preservation an explicit requirement (Article XV §9)? [Completeness, Spec §FR-012] — PASS: FR-012 "The `no-std-mutex` CI gate and the load-bearing `test_async_mutex_layout_golden` MUST be preserved"; plan.md Constitution Check cites Article XV §9 explicitly.
- [x] CHK014 Is "public `async_mutex` API unchanged" stated, and are the ABI/consumer-neutrality claims traceable? [Completeness, Spec §FR-011/FR-012] — PASS: FR-011 (API surface unchanged) + FR-012 (strand-confined consumers unaffected) + Assumptions ("no consumer... requires an API or call-site change") + contract-delta "Unchanged (explicitly)" section — all traceable and mutually consistent.

## AM-P2-1 teardown ordering & contract scope

- [x] CHK015 Is the release/acquire pairing on `in_flight_resumers_` specified at BOTH sites (runner decrement release; drain + destructor acquire)? [Completeness, research D-2] — PASS: research.md D-2 specifies all three sites (runner `fetch_sub` → release at `:583`; drain terminal `load` → acquire at `:1195-1196`; destructor's new check → acquire); tasks.md T016 mirrors this exactly. Anchors spot-verified against the header (`:583` = `in_flight_resumers_.fetch_sub(1, relaxed)`; `:1195-1196` = the drain terminal condition load).
- [x] CHK016 Is the happens-before guarantee scoped precisely to the PARKED-then-REAPED waiter case? [Clarity, contracts] — PASS: contracts/async_mutex-contract-delta.md states the new guarantee applies "even for a waiter that was still PARKED at drain start and got reaped" and explicitly names this "the AM-P2-1 cross-executor write-after-free for parked-then-reaped waiters."
- [x] CHK017 Is the cross-executor GRANTED-holder case explicitly EXCLUDED from the safe-teardown guarantee, with the mechanism (early `active_holders_count_` decrement) documented? [Consistency, contracts / Spec §FR-002/FR-003] — PASS: contract-delta "Explicit EXCLUSION" cites the early relaxed decrement of `active_holders_count_` at `:961` before the `state_` CAS; spot-verified — header line 961 is `active_holders_count_.fetch_sub(1, std::memory_order_relaxed);`, the first statement of `unlock()`, confirming the mechanism as described.
- [x] CHK018 Do FR-002 and FR-003 read consistently about what the guard enforces vs what the contract documents (no "guard-enforced strand-locality" over-claim)? [Conflict, Spec §FR-002/FR-003] — PASS: FR-002 states "the destructor guard enforces the in-flight-resumer barrier; strand-locality of the granted-holder case is documented-contract-only, NOT guard-enforced — see FR-003"; FR-003 states the granted-holder hazard "is handled by the drain CONTRACT... not the guard" — consistent both directions. Also confirms the gatea-record round-2 residual fix (stale US2 "in-flight resumer or holder" over-claim) is applied — current US2 prose reads "handled by keeping the drain strand-local per the contract... not by the guard, since the holder count is not a valid teardown barrier."

## AM-P2-2 destructor guard barrier correctness

- [x] CHK019 Is `in_flight_resumers_` identified as THE load-bearing barrier (decremented last, `:583`), with the reason stated? [Clarity, research D-3] — PASS: research.md D-3 "`in_flight_resumers_` is THE load-bearing barrier — it is decremented as the runner's LAST statement (`:583`)"; spot-verified against the header — the decrement at line 583 is preceded by an explicit comment "Decrement LAST — the drain may destroy the mutex once it observes 0."
- [x] CHK020 Is it explicitly required that the guard is NOT gated on `active_holders_count_`, with the "not a barrier" justification? [Consistency, research D-3] — PASS: research.md D-3 "Gate-A refinement (both reviewers): `active_holders_count_` is NOT a valid teardown barrier — do not gate the guard on it" with the early-decrement (`:961`) justification.
- [x] CHK021 Are BOTH catch shapes specified (cancel-delivered-then-destroy AND the grant-shaped sibling)? [Coverage, research D-9] — PASS: research.md D-9 + gatea record finding #7 both specify BOTH shapes; tasks.md T013 (cancel-shape) and T014 (grant-shape, Gate-A: Fable bonus finding) implement both in `test_destructor_release_death.cpp`.
- [x] CHK022 Is the no-false-positive requirement on a legitimately drained-then-destroyed mutex stated and testable? [Measurability, Spec §SC-002] — PASS: SC-002 "does NOT terminate on the legitimate drained-then-destroyed shape (both directions asserted)"; US2 Acceptance Scenario 3 states the identical no-false-positive requirement, testable via T019.

## AM-P2-3 bounded exhaustion

- [x] CHK023 Is the bounded-CAS-counter behavior specified (refuse past capacity without incrementing; else CAS)? [Completeness, research D-4] — PASS: research.md D-4 "load, if `>= capacity` return the `sync_lock_alloc_failed` fail-closed path without incrementing, else `CAS(cur, cur+1)`"; tasks.md T021 mirrors this exactly against `:818`.
- [x] CHK024 Is "no u32 wrap / no live-slot reissue" stated as a property, and is the reachability of the wrap (2³² / ~7 min) documented? [Measurability, Spec §FR-004/SC-005] — PASS: FR-004/SC-005 state the property; the Phase-0 verification doc and research.md D-1 both quantify the wrap reachability ("~4.29e9 failing attempts, ~7 min tight-retry @10M/s").
- [x] CHK025 Is fail-closed behavior (`sync_lock_alloc_failed`) at exhaustion specified as deterministic? [Clarity, Spec §FR-004] — PASS: FR-004 + US3 Independent Test both state "deterministically"; US3 AS1 gives the concrete 513th-acquire scenario.

## AM-P3 impossible-state traps

- [x] CHK026 Is the chain-walk trap specified as the release-safe `std::terminate()` idiom (NOT debug-only assert, NOT `std::unreachable()`), and is FR-005 consistent with research D-5? [Conflict, Spec §FR-005 / research D-5] — PASS: FR-005 and research.md D-5 use identical wording (the `std::terminate()` idiom, mirroring the destructor guard, NOT debug-only `assert`, NOT `std::unreachable()`); gatea record finding #4 confirms this reconciliation was applied (superseding an earlier debug-assert-only draft). Anchors `:1001-1003` / `:1056-1058` spot-verified against the header — both are the `else` arms of the residual and reversed-LIFO chain walks that silently step past a `granted` record today.
- [x] CHK027 Is the no-phantom-unlock requirement for the null-awaiter disarm stated with the exact neutralize-without-unlock semantics (leaked-lock failure direction)? [Clarity, research D-6] — PASS: research.md D-6 specifies the exact mechanism (`guard.release()` before the engaged guard's destructor runs) and the failure-direction requirement ("MUST be a leaked lock, never a phantom `unlock()`"). Anchor `:575-580` spot-verified as the null-awaiter arm of the resume runner.
- [x] CHK028 Is the AM-P3-3 OOM disposition (accepted documented terminate; asymmetry inherent to grant-ordering) stated as a decision, not left as "either/or"? [Completeness, research D-8 / Spec §FR-007] — SPEC-FIXED: found a genuine spec↔design inconsistency. research.md D-8 and contracts/async_mutex-contract-delta.md both commit firmly to "accept documented terminate" (the escape is NOT closed — no error channel exists post-grant), but `spec.md` FR-007 still read "either the escape is closed or a terminate-on-OOM is documented as accepted", i.e. the requirement text hadn't been updated to reflect the settled design and self-contradicted D-8. Edited `spec.md` FR-007 to state the settled disposition explicitly (accepted fail-stop, asymmetry inherent to grant-ordering, mirroring D-8/contract-delta) instead of leaving the disjunction open.

## Test validity & coverage requirements

- [x] CHK029 Is "genuinely multi-threaded" defined for the converted race tests (real threads, contended arbitration), distinguishing them from single-threaded-in-disguise? [Clarity, Spec §FR-008] — PASS: FR-008 + US5 body ("real threads... not a single-threaded `io_context` in disguise") + Clarifications session both define the term; Phase-0 verification doc documents the current single-threaded-in-disguise shape being replaced ("the `phase_` CAS 'races' are deterministic interleavings").
- [x] CHK030 Are BOTH seam phases (`pop_pre_cas` for part 1, `pop_pre_link_load` for part 2) required, each mapped to the AM-P1 half it makes RED? [Coverage, research D-7] — PASS: research.md D-7 maps each phase to its half explicitly (`pop_pre_cas` → ABA part 1; `pop_pre_link_load` → plain-read/reuse race part 2, "the witness that would have caught the Gate-A-1 BLOCKER"); tasks.md T007/T008 mirror this 1:1. Gatea record finding #3 confirms `pop_pre_link_load` was added in round 1 (initial draft had only one phase).
- [x] CHK031 Is the seam-target ODR/standalone constraint stated as a hard requirement (not a footnote)? [Completeness, research D-7] — PASS: research.md D-7 states verbatim "ODR (Gate-A: Fable, hard requirement — not a footnote)"; tasks.md T001 carries the same constraint into Setup.
- [x] CHK032 Is the mutation-tested (RED-against-pre-fix) requirement stated for EVERY witness, with the anti-enshrinement rationale? [Measurability, Spec §FR-008/SC-007] — PASS: FR-008/SC-007 state the universal requirement; research.md D-9 "Anti-enshrinement" paragraph gives the rationale; tasks.md T012/T019/T022/T034 implement per-story mutation checks.
- [x] CHK033 Is the coverage DoD unambiguous (100% of REACHABLE branches, lcov BRDA/DA, every unreachable branch waived with a WRITTEN proof)? [Clarity, Spec §FR-010/SC-004] — PASS: FR-010/SC-004 state this precisely and identically; quickstart.md reiterates the lcov BRDA/DA basis (NOT `llvm-cov report` aggregate) per the project's coverage-gate convention.
- [x] CHK034 Is AM-P1's TSan-invisibility addressed by an explicit requirement that seam+reasoning — not TSan-green — carries its correctness? [Completeness, Spec §FR-013] — PASS: FR-013 states branch coverage alone is NOT accepted as proof of correctness for the atomic cycle and requires the targeted stress witness plus regression test; quickstart.md's FR-013 NOTE makes the TSan-specific framing explicit ("TSan-green is necessary but NOT sufficient... the deterministic seam witness + the generation-bump reasoning argument carry its correctness").
- [x] CHK035 Is the pre-fix-RED-run-executes-UB caveat handled (non-TSan lane or documented expected report)? [Edge Case, research D-7] — PASS: research.md D-7 "Pre-fix RED runs known UB" states the non-TSan-lane-or-documented-report handling; tasks.md T002 implements it as a Setup task.

## Dependencies, assumptions & traceability

- [x] CHK036 Are the strand-confined production consumers' behavioral-neutrality assumptions documented and validated? [Assumption, Spec §Assumptions] — PASS: Spec Assumptions + Context & Provenance + FR-012 document the assumption (all four shipped consumers strand-confined, fixes additive); validation vehicle is the project-standard full-suite `ctest` at `/speckit-verify` (T036) plus `test_async_mutex_layout_golden`/`no-std-mutex` regression (T033) — no consumer call-site changes are made, so the existing consumer test suites are the validating witnesses.
- [x] CHK037 Is the ARM64-weak-memory-HW-unavailable assumption stated, with the reasoning that carries AM-P2-1 instead of native execution? [Assumption, Spec §Assumptions] — PASS: Spec Assumptions states this verbatim ("Native ARM64 weak-memory hardware remains unavailable in CI... the AM-P2-1 ordering argument is therefore carried by reasoning + the release/acquire pairing, not a native weak-memory execution").
- [x] CHK038 Does every FR (001–013) and buildable SC (001–007) trace to at least one task, and is that mapping current after the Gate-A rewrite? [Traceability, tasks.md] — PASS: verified full FR-001..013 / SC-001..007 → task mapping (FR-001→T007-T012; FR-002/003→T013-T019; FR-004→T020-T022; FR-005→T023/T026; FR-006→T024; FR-007→T025; FR-008→T012/T019/T022/T027-T031/T034; FR-009→T020/T022/T032; FR-010→T036; FR-011→T003/T005/T033; FR-012→T033; FR-013→T001/T002/T007/T008/T036; SC-001→T007/T008/T011/T012; SC-002→T013/T014/T019; SC-003→T011/T036; SC-004→T036; SC-005→T020/T022; SC-006→T005/T033; SC-007→T012/T019/T022/T034). All five Gate-A round-1 fixes (persistent slot-link, `pop_pre_link_load` seam, in-flight-only guard, FR-005 terminate idiom, D-6 no-self-unlock disarm) are reflected in the current tasks.md — mapping is current post-rewrite.

## Notes

Items are requirements-quality checks (is the concern SPECIFIED well?), not implementation tests.
Dispositioned at the `/speckit-checklist-audit` gate (SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>)
before `/speckit-implement`.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 37 |
| SPEC-FIXED | 1 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 38 |

### SPEC-FIXED items

- CHK028 — `spec.md` FR-007 read "either the escape is closed or a terminate-on-OOM is documented as
  accepted" — a live disjunction that contradicted the already-settled research.md D-8 /
  contracts/async_mutex-contract-delta.md decision (accepted documented terminate; escape NOT closed;
  asymmetry inherent to grant-ordering, not a defect). Edited FR-007 to state the settled disposition
  explicitly instead of leaving it open; affected: `spec.md` §Requirements / FR-007.

### DD-DECIDED items

None.

### WAIVED items

None.

Anchors spot-verified (all resolve against `include/fixpp/core/sync/async_mutex.hpp`, current `main`
`a6a1302`, 1221 lines, post-057): `:583`, `:596`, `:619`, `:659`, `:666-671`, `:808-816`, `:831`,
`:857-873`, `:961`, `:1001-1003`, `:1056-1058`, `:1195-1196`, `:575-580`, `:641-646`, `:263-266`,
`:566-569`. Design-doc anchor `[2f]` (bare, no §, FR-011 layout golden) — no dangling `[2f §N]`
cross-references found in the 058 bundle (`grep` swept `spec.md`/`plan.md`/`research.md`/`data-model.md`/
`contracts/*.md`/`checklists/*.md`). Gate-A convergence record
`.specify/decisions/058-async-mutex-hardening-gatea.md` (round 1 + round-2 Codex confirmation, dated
2026-07-02) verified — all 7 findings' dispositions cross-checked against research.md D-1..D-9 and
confirmed reflected in the current spec.md/plan.md/tasks.md/data-model.md/contracts bundle.
