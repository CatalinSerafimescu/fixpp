# ABI / NFR Requirements-Quality Checklist: F-f tail hardening bundle (039)

**Purpose**: "Unit tests for the requirements" — validate that the 039 requirements (frozen-ABI pin,
coverage remediation, §XV.9 corpus-gate extension, doc resolution) are complete, clear, consistent, and
measurable. Audience: Gate B reviewer. This checks the *spec*, not the code.
**Created**: 2026-06-15
**Feature**: [spec.md](../spec.md)

## Frozen-ABI pin (US2) — Requirement Quality

- [ ] CHK001 Is the ratified `_checked` sentinel behavior being *pinned* (not changed) stated unambiguously, with the exact result (`FIXPP_ERR_OK` + ordering/equal 0) quantified? [Clarity, Spec §FR-006]
- [ ] CHK002 Is the "no production behavior change" constraint for US2 explicit and bound to a concrete, checkable artifact (comment-only diff)? [Measurability, Spec §FR-006/§FR-013]
- [ ] CHK003 Does the spec define the operand-coverage required to pin the behavior (sentinel as left, right, and both)? [Completeness, Spec §FR-006]
- [ ] CHK004 Is the boundary that the pin must NOT erase specified — i.e. out-of-domain *exponent* still returns `FIXPP_ERR_DECIMAL_INVALID`? [Coverage, Edge Case, Spec §FR-007]
- [ ] CHK005 Is the authority for "this is ratified, not a defect" cited and traceable (001 AC-C6 / research.md D-12 / `[const §X.1]`)? [Traceability, Spec §Assumptions]
- [ ] CHK006 Does the spec explicitly place "reject the sentinel" OUT of scope, so an implementer cannot read US2 as license to change the ABI? [Consistency, Spec §Out of Scope]
- [ ] CHK007 Is there a stated requirement that the pin test must *discriminate* (fail if the behavior were changed), not merely assert today's result? [Measurability, Gap → covered by tasks T004]

## Coverage remediation (US3) — Requirement Quality

- [ ] CHK008 Are the three previously-waived lines identified specifically enough to be measured (file + symbol), or is the locating procedure specified? [Clarity, Spec §FR-008/§FR-009/§FR-010]
- [ ] CHK009 Is the acceptance condition for each line unambiguous — covered by a witness OR a *specific re-measured* waiver (not a bare "untestable")? [Measurability, Spec §SC-004]
- [ ] CHK010 Is the seam used to force the `set_next_outbound` lock-fail branch named and its determinism (no-flake) required? [Completeness, Spec §FR-008 / Edge Cases]
- [ ] CHK011 Does the spec cover both `OsFile` move-ctor variants (POSIX and Windows) rather than only one platform? [Coverage, Spec §FR-009]

## §XV.9 corpus-gate extension (US4) — Requirement Quality

- [ ] CHK012 Is the membership criterion for "uncovered awaitable header" defined precisely (post-`-E`: pulls `asio::awaitable` AND names a banned mutex)? [Clarity, Spec §FR-011 / research.md D-4]
- [ ] CHK013 Is the exact target set enumerated (the 7 headers) and is the `business_messages.hpp` exclusion justified, so the set is checkable rather than open-ended? [Completeness, Spec §SC-005]
- [ ] CHK014 Is the "must still pass on the current tree" success condition stated, and is the default-real behavior on a gate failure specified (surface + fix, not silently drop)? [Consistency, Spec §SC-005 / Edge Cases]
- [ ] CHK015 Does the spec keep US4 additive (no change to the gate's semantics), preserving the "no production behavior change" invariant? [Consistency, Spec §FR-013/§FR-014]

## Cross-cutting NFR & premise integrity

- [ ] CHK016 Is the "no new error codes / config / codegen / wire / C-ABI behavior" constraint stated once, authoritatively, and consistently across spec/plan/research? [Consistency, Spec §FR-013]
- [ ] CHK017 Is the consequence "→ Gate A not required" derived explicitly from FR-014 (no production change), and is the condition under which that premise would break identified? [Traceability, Spec §FR-014 / plan.md Constitution Check]
- [ ] CHK018 Is each Success Criterion (SC-003…SC-006) objectively verifiable from an observable artifact (test result, gate run, coverage report, doc grep)? [Measurability, Spec §Success Criteria]
- [ ] CHK019 Does the spec carry a Normative References section distinguishing references *touched* from references *extended* (no new OFFICIAL FIX coverage claimed)? [Completeness, Spec §Normative References]

## US5 doc resolution — Requirement Quality

- [ ] CHK020 Is the doc deliverable specified concretely enough to verify (L-033-3 wording resolved + absent-`1137`-ack case described, internally consistent with shipped FIXT behavior)? [Measurability, Spec §FR-012/§SC-006]

## Notes

- Check items off as completed: `[x]` with a disposition tag at audit (step 9).
- Items test requirement *quality*, not implementation correctness.
