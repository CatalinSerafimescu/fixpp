# ABI / NFR Requirements-Quality Checklist: F-f tail hardening bundle (039)

**Purpose**: "Unit tests for the requirements" — validate that the 039 requirements (frozen-ABI pin,
coverage remediation, §XV.9 corpus-gate extension, doc resolution) are complete, clear, consistent, and
measurable. Audience: Gate B reviewer. This checks the *spec*, not the code.
**Created**: 2026-06-15
**Feature**: [spec.md](../spec.md)

## Frozen-ABI pin (US2) — Requirement Quality

- [x] CHK001 Is the ratified `_checked` sentinel behavior being *pinned* (not changed) stated unambiguously, with the exact result (`FIXPP_ERR_OK` + ordering/equal 0) quantified? [Clarity, Spec §FR-006] — PASS: FR-006 states "return `FIXPP_ERR_OK` with ordering 0 / equal 0 (per AC-C6 / D-12)" unambiguously; quantified in US2 AC-1/AC-2 and SC-003.
- [x] CHK002 Is the "no production behavior change" constraint for US2 explicit and bound to a concrete, checkable artifact (comment-only diff)? [Measurability, Spec §FR-006/§FR-013] — PASS: FR-006 "The production `_checked` behavior MUST NOT change"; T005 in tasks.md gates on `git diff main` showing no executable-line change; FR-013 cross-cuts all stories.
- [x] CHK003 Does the spec define the operand-coverage required to pin the behavior (sentinel as left, right, and both)? [Completeness, Spec §FR-006] — PASS: US2 Independent Test states "sentinel as left, right, and both operands"; tasks T001 repeats it; Edge Cases confirms; FR-006 sentence is read together with US2.
- [x] CHK004 Is the boundary that the pin must NOT erase specified — i.e. out-of-domain *exponent* still returns `FIXPP_ERR_DECIMAL_INVALID`? [Coverage, Edge Case, Spec §FR-007] — PASS: FR-007 "exponent-domain validation (`[-38,0]` → `FIXPP_ERR_DECIMAL_INVALID`) MUST be unchanged"; US2 AC-3 asserts it.
- [x] CHK005 Is the authority for "this is ratified, not a defect" cited and traceable (001 AC-C6 / research.md D-12 / `[const §X.1]`)? [Traceability, Spec §Assumptions] — PASS: spec.md Assumptions cite all three; Normative References section lists them; research D-2 cites all three; all anchors resolve (AC-C6 + D-12 in 001/research.md:87–112; §X.1 = constitution Art X §1).
- [x] CHK006 Does the spec explicitly place "reject the sentinel" OUT of scope, so an implementer cannot read US2 as license to change the ABI? [Consistency, Spec §Out of Scope] — PASS: Out of Scope section explicitly states "Rejecting the `INT64_MIN` sentinel in the C-ABI `_checked` path (contradicts the ratified 2026-05-12 frozen-ABI decision; a separate ABI-decision feature). US2 here only *pins* the existing behavior."
- [x] CHK007 Is there a stated requirement that the pin test must *discriminate* (fail if the behavior were changed), not merely assert today's result? [Measurability, Gap → covered by tasks T004] — PASS: tasks T004 mandates mutation-discrimination explicitly ("temporarily mutate the `_checked` path...confirm T001/T002 pin test goes RED; then revert the mutation"); tasks.md is the plan-of-record for this measurability requirement.

## Coverage remediation (US3) — Requirement Quality

- [x] CHK008 Are the three previously-waived lines identified specifically enough to be measured (file + symbol), or is the locating procedure specified? [Clarity, Spec §FR-008/§FR-009/§FR-010] — PASS: (a) `seqnum_manager.cpp:188` `set_next_outbound` named in FR-008 + research D-3(a); (b) `file_store.cpp:401`/:503 `OsFile` move-ctor named in research D-3(b) + plan; (c) "033 lines deferred without per-line DA/BRDA" — locating procedure specified in tasks T008 ("recover the deferred set from the 033 verify/Gate-B coverage records") satisfying the "or locating procedure" branch of CHK008.
- [x] CHK009 Is the acceptance condition for each line unambiguous — covered by a witness OR a *specific re-measured* waiver (not a bare "untestable")? [Measurability, Spec §SC-004] — PASS: SC-004 states "either executed by a new witness or carries a specific, re-measured waiver — zero 'untestable'-without-justification dispositions remain among the three."
- [x] CHK010 Is the seam used to force the `set_next_outbound` lock-fail branch named and its determinism (no-flake) required? [Completeness, Spec §FR-008 / Edge Cases] — PASS: FR-008 names "via the `mutex_test_access` seam"; Edge Cases requires "must force lock failure deterministically (no flake)"; Assumptions confirm the seam exists; confirmed live at seqnum_manager.hpp:162.
- [x] CHK011 Does the spec cover both `OsFile` move-ctor variants (POSIX and Windows) rather than only one platform? [Coverage, Spec §FR-009] — PASS: research D-3(b) names both `:401` POSIX / `:503` Windows; plan.md states "POSIX + Windows `OsFile` variants (US3(b) covers both move-ctors at `file_store.cpp:401`/`:503`)"; tasks T007 names both; both confirmed live at file_store.cpp:401 and :503.

## §XV.9 corpus-gate extension (US4) — Requirement Quality

- [x] CHK012 Is the membership criterion for "uncovered awaitable header" defined precisely (post-`-E`: pulls `asio::awaitable` AND names a banned mutex)? [Clarity, Spec §FR-011 / research.md D-4] — PASS: spec.md Edge Cases states "the gate flags only headers that BOTH pull `asio::awaitable` AND name a banned mutex, post-`-E`"; Assumptions confirm the criterion; research D-4 verifies gate semantics against the actual script.
- [x] CHK013 Is the exact target set enumerated (the 7 headers) and is the `business_messages.hpp` exclusion justified, so the set is checkable rather than open-ended? [Completeness, Spec §SC-005] — PASS: SC-005 lists all 7 headers by name; `business_messages.hpp` exclusion justified in SC-005 ("self-declares 'No asio::awaitable in this header' (`:30`)") and research D-4 ("Codex/Opus Gate A round 1 P2").
- [x] CHK014 Is the "must still pass on the current tree" success condition stated, and is the default-real behavior on a gate failure specified (surface + fix, not silently drop)? [Consistency, Spec §SC-005 / Edge Cases] — PASS: FR-011 requires "MUST still pass on the current tree"; SC-005 states "Should any added header fail the gate post-preprocess, that is a real §XV.9 finding to surface (default-real)"; tasks T010 repeats "Surface and fix it...do NOT silently drop it from the list."
- [x] CHK015 Does the spec keep US4 additive (no change to the gate's semantics), preserving the "no production behavior change" invariant? [Consistency, Spec §FR-013/§FR-014] — PASS: FR-013 forbids new error codes/config/codegen/wire/C-ABI behavior change; FR-014 places US4 under "makes no production behavior change"; plan.md Constitution Check: US4 is "strictly additive; the newly-listed headers are clean today."

## Cross-cutting NFR & premise integrity

- [x] CHK016 Is the "no new error codes / config / codegen / wire / C-ABI behavior" constraint stated once, authoritatively, and consistently across spec/plan/research? [Consistency, Spec §FR-013] — PASS: FR-013 is the single normative statement; plan.md Technical Context Constraints echoes it ("No new error codes; no new config; no codegen regeneration; no wire or C-ABI behavior change"); research D-2/D-3/D-4/D-5 are all consistent; no contradictions found.
- [x] CHK017 Is the consequence "→ Gate A not required" derived explicitly from FR-014 (no production change), and is the condition under which that premise would break identified? [Traceability, Spec §FR-014 / plan.md Constitution Check] — PASS: FR-014 derives "Consequently Gate A is not required"; plan.md Constitution Check restates it; tasks T010 identifies the break condition ("a gate failure on any of the 7 would re-trigger a concurrency/Appendix-A surface and invalidate the FR-014 premise — STOP and reassess Gate A"); Article XVII §6 auto-waives comment-only/doc/test/build-gate trivial diffs.
- [x] CHK018 Is each Success Criterion (SC-003…SC-006) objectively verifiable from an observable artifact (test result, gate run, coverage report, doc grep)? [Measurability, Spec §Success Criteria] — PASS: SC-003 verifiable by ctest result; SC-004 verifiable by coverage report + test output; SC-005 verifiable by corpus-gate ctest run; SC-006 verifiable by `grep` in docs (tasks T011 independent test states the exact grep command).
- [x] CHK019 Does the spec carry a Normative References section distinguishing references *touched* from references *extended* (no new OFFICIAL FIX coverage claimed)? [Completeness, Spec §Normative References] — PASS: spec.md Normative References section opens "This feature adds **no new OFFICIAL normative FIX/FIXT coverage**...The references it *touches* (not extends):" followed by the explicit list.

## US5 doc resolution — Requirement Quality

- [x] CHK020 Is the doc deliverable specified concretely enough to verify (L-033-3 wording resolved + absent-`1137`-ack case described, internally consistent with shipped FIXT behavior)? [Measurability, Spec §FR-012/§SC-006] — PASS: FR-012 requires "L-033-3 wording follow-up and the absent-`1137`-ack case MUST be resolved in the relevant spec/behaviors documentation"; SC-006 states "no open placeholder"; tasks T011 specifies the exact file (`spec/behaviors-and-limitations.md`) and independent test (`grep -n 'L-033-3'`); L-033-3 entry verified to exist at behaviors-and-limitations.md:1096.

## Notes

- Check items off as completed: `[x]` with a disposition tag at audit (step 9).
- Items test requirement *quality*, not implementation correctness.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 20 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **20** |

### SPEC-FIXED items

None.

### DD-DECIDED items

None. All US2 sentinel-behavior anchors (AC-C6, D-12, §X.1) are cited directly in spec.md and constitute evidence for PASS dispositions, not unresolved gaps requiring DD-DECIDED recording.

### WAIVED items

None.

Anchors spot-verified: `[const §X.1]` (constitution Art X §1 — confirmed), `[const §IX.1]` (constitution Art IX §1 — confirmed), `[const §XV.9]` (constitution Art XV §9 — confirmed), `L-033-3` (spec/behaviors-and-limitations.md:1096 — confirmed), `001 AC-C6` (specs/001-core-decimal/research.md:41 — confirmed), `research.md D-12` (specs/001-core-decimal/research.md:87 — confirmed) — all resolve in their respective signed-off artifacts.
