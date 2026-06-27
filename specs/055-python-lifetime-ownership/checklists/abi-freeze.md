# C-ABI Freeze & Error-Contract Requirements Checklist: Python bindings ownership / lifetime layer (PY-004)

**Purpose**: Validate that the freeze-boundary and error-code REQUIREMENTS (no `c_api.h` change, codes 1201/1202/1204 raised-not-minted, `0→1` freeze invariant, Article XX `[2m]` amendment scope) are complete, unambiguous, consistent, and measurable. Tests requirements writing, not code.
**Created**: 2026-06-27
**Feature**: [spec.md](../spec.md) · [data-model.md](../data-model.md) · [research.md](../research.md)

## Requirement Completeness

- [ ] CHK001 Is the "no `include/fix/c_api.h` change" constraint stated as a hard requirement with a definite verification (header byte-unchanged / no new symbol)? [Completeness, Spec §FR-015, SC-005]
- [ ] CHK002 Is it required that codes 1201/1202/1204 are RAISED Python-side (not minted), so no new `fixpp_error_t` is introduced and the freeze is unaffected? [Completeness, Spec §FR-003/FR-017/FR-018, data-model E-8]
- [ ] CHK003 Is the requirement that these binding-internal codes are NEVER returned across `extern "C"` stated explicitly? [Completeness, data-model E-8, contracts C-8]
- [ ] CHK004 Is the Article XX `[2m]` amendment site set enumerated (the substantive + editorial sites) so the design-doc edit is bounded and auditable? [Completeness, research Proposed amendment, plan Gate A row]
- [ ] CHK005 Is the boundary that the deferred §4.5 6-method director and value-typed classes would be ADDITIVE (not freeze-breaking) and are explicitly out of scope, stated as a requirement? [Completeness, Spec Assumptions, research D-1, plan Article X row]

## Requirement Clarity

- [ ] CHK006 Is "the `0→1` freeze holds" defined with a measurable invariant (e.g. `git diff` of the header is empty AND no new exported symbol), not just asserted prose? [Clarity, Spec §SC-005, FR-015]
- [ ] CHK007 Is it unambiguous that the liveness guard is a Python-side check performed BEFORE the C-ABI call, backed by the existing 050/052 native tombstones (i.e. no native support is required)? [Clarity, Spec §FR-015, contracts C-8]
- [ ] CHK008 Is the conditional path "if `/plan`/`/implement` discovers a genuine native need, it is added at MINOR before the freeze closes" stated clearly enough to act on (escalate, not silently change)? [Clarity, Spec Assumptions, plan Constraints]

## Requirement Consistency

- [ ] CHK009 Do the three error codes and their numeric values (1201 SubInterpreter / 1202 ObjectLifetime / 1204 CallbackReentrantClose) match across Spec (Key Entities + FR/SC), data-model E-8, and contracts C-2/C-6/C-7? [Consistency]
- [ ] CHK010 Is the claim "`ObjectLifetime`/`CallbackReentrantClose`/`SubInterpreterRejected` already exist (PY-003/054)" consistent with the data-model E-8 source-line citations (`error.h` :157/:159/:163; `fixpp.i` class/mapping lines)? [Consistency, Spec Assumptions ↔ data-model E-8]
- [ ] CHK011 Is the Article XX amendment's deferral-to-`/implement` (carried as proposed text in the bundle, live `[2m]` edit NOT pre-applied) described consistently with the 043/051/054 precedent across plan §Gate-A and research? [Consistency, plan Article XX row ↔ research]
- [ ] CHK012 Is the substantive amendment-site count stated consistently between the plan (6 substantive + 1 editorial) and the research enumeration of the same sites? [Consistency, plan Article XX row ↔ research Proposed amendment]

## Acceptance Criteria Quality & Measurability

- [ ] CHK013 Can SC-005 (C-ABI surface byte-unchanged) be objectively verified by a single deterministic check? [Measurability, Spec §SC-005]
- [ ] CHK014 Is FR-001's "the flat substrate remains available and unchanged in behaviour" measurable via the pre-existing test suite staying green (SC-004)? [Measurability, Spec §FR-001 ↔ SC-004]
- [ ] CHK015 Is SC-006 (Tier-1 `python-bindings` none/asan/tsan matrix green) tied to a concrete CI gate rather than left as an aspiration? [Measurability, Spec §SC-006]

## Scenario & Edge-Case Coverage

- [ ] CHK016 Are requirements defined for the case where a raised binding code (e.g. `ObjectLifetime`) coincides with an error the C-ABI also reports — is precedence (Python-side check first) specified? [Coverage, contracts C-2/C-8]
- [ ] CHK017 Is the relationship between the binding-internal `[1200,1299]` block and the Phase-4 `[1400,1499]` C-ABI block (051) specified so no overlap/minting is reintroduced? [Coverage, Gap, data-model E-8]

## Ambiguities & Gaps

- [ ] CHK018 Does the spec define what evidence the freeze-held claim requires at close-out (T026 header-diff check + symbol audit), so SC-005 is auditable rather than assumed? [Gap, Spec §SC-005, tasks T026]
- [ ] CHK019 Is it unambiguous that applying the Article XX `[2m]` amendment is a DESIGN-DOC edit (not a constitution amendment), so it does not trigger a separate constitution-change process? [Ambiguity, plan Article XX row, research]

## Notes

- This checklist tests REQUIREMENTS quality around the freeze boundary, not the freeze itself (that is the T026/SC-005 runtime check).
- CHK012 traces to the `/speckit-analyze` scope: the amendment is carried-as-proposed-text and applied at /implement (task T025); the substantive-site list must agree between plan and research so T025 + the Gate-B completeness audit can verify it landed in full.
- Traceability: 19/19 items carry a spec/data-model/research/plan/tasks reference or a `[Gap]`/`[Ambiguity]` marker.
