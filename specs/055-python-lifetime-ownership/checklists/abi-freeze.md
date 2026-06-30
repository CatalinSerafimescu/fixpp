# C-ABI Freeze & Error-Contract Requirements Checklist: Python bindings ownership / lifetime layer (PY-004)

**Purpose**: Validate that the freeze-boundary and error-code REQUIREMENTS (no `c_api.h` change, codes 1201/1202/1204 raised-not-minted, `0→1` freeze invariant, Article XX `[2m]` amendment scope) are complete, unambiguous, consistent, and measurable. Tests requirements writing, not code.
**Created**: 2026-06-27
**Feature**: [spec.md](../spec.md) · [data-model.md](../data-model.md) · [research.md](../research.md)

## Requirement Completeness

- [x] CHK001 Is the "no `include/fix/c_api.h` change" constraint stated as a hard requirement with a definite verification (header byte-unchanged / no new symbol)? [Completeness, Spec §FR-015, SC-005] — PASS: FR-015 states "MUST NOT change the C-ABI surface (`include/fix/c_api.h`); the `0→1` freeze stays held"; SC-005 requires the surface to be "byte-unchanged by this feature"; tasks T026 establishes the header-diff check as the verification step.
- [x] CHK002 Is it required that codes 1201/1202/1204 are RAISED Python-side (not minted), so no new `fixpp_error_t` is introduced and the freeze is unaffected? [Completeness, Spec §FR-003/FR-017/FR-018, data-model E-8] — PASS: FR-003 cites 1202 as "already defined by PY-003"; FR-017 cites 1204 as "already defined by PY-003 (054)"; FR-018 cites 1201 similarly; data-model E-8 states "binding-internal codes raised Python-side (never returned across `extern "C"`)… no new error code; `0→1` freeze unaffected."
- [x] CHK003 Is the requirement that these binding-internal codes are NEVER returned across `extern "C"` stated explicitly? [Completeness, data-model E-8, contracts C-8] — PASS: data-model E-8 final row states "These are binding-internal codes raised Python-side (never returned across `extern "C"`)"; contracts C-8 Invariants states "codes 1201/1202/1204 already exist; the `0→1` C-ABI freeze holds" (implying Python-only raising, consistent with data-model E-8).
- [x] CHK004 Is the Article XX `[2m]` amendment site set enumerated (the substantive + editorial sites) so the design-doc edit is bounded and auditable? [Completeness, research Proposed amendment, plan Gate A row] — PASS: research.md Proposed amendment section enumerates 6 substantive sites (§1.3 rule (2), §6.5 send+close carve-out rows, §6.7 table row + prose docstring :818-820, §9 seam #4 open comment) plus 1 editorial site; plan Gate A row states "6 substantive + 1 editorial sites."
- [x] CHK005 Is the boundary that the deferred §4.5 6-method director and value-typed classes would be ADDITIVE (not freeze-breaking) and are explicitly out of scope, stated as a requirement? [Completeness, Spec Assumptions, research D-1, plan Article X row] — PASS: spec Assumptions section states the §4.5 6-method director is out of scope (D-1) and value-typed classes are deferred (FR-014); plan Article X row records the deferral explicitly; research D-1 enumerates the 4 extra director methods (out of v1.0 scope).

## Requirement Clarity

- [x] CHK006 Is "the `0→1` freeze holds" defined with a measurable invariant (e.g. `git diff` of the header is empty AND no new exported symbol), not just asserted prose? [Clarity, Spec §SC-005, FR-015] — PASS: SC-005 states the invariant as "C-ABI surface (`include/fix/c_api.h`) is byte-unchanged"; T026 specifies a `git diff` header-diff check + symbol audit; contracts C-8 states "No `include/fix/c_api.h` change" as a hard invariant; the freeze invariant is a deterministic check, not prose.
- [x] CHK007 Is it unambiguous that the liveness guard is a Python-side check performed BEFORE the C-ABI call, backed by the existing 050/052 native tombstones (i.e. no native support is required)? [Clarity, Spec §FR-015, contracts C-8] — PASS: FR-015 explicitly states "The liveness guard is Python-side and relies on the existing 050/052 native-handle tombstones for defence in depth"; C-8 states "All lifetime/reentrancy state is GIL-protected (no `threading.local`…)"; contracts C-2 says "Every method … checks `self._dead` FIRST … DO NOT call the C-ABI."
- [x] CHK008 Is the conditional path "if `/plan`/`/implement` discovers a genuine native need, it is added at MINOR before the freeze closes" stated clearly enough to act on (escalate, not silently change)? [Clarity, Spec Assumptions, plan Constraints] — PASS: spec Assumptions section states "Any gap PY-004 surfaces is fixed at MINOR (free now; breaking after `MAJOR>=1` per `[const §X.1]`) before the freeze closes"; Context & background section reinforces this; the path is clear: escalate to a MINOR increment rather than silent change.

## Requirement Consistency

- [x] CHK009 Do the three error codes and their numeric values (1201 SubInterpreter / 1202 ObjectLifetime / 1204 CallbackReentrantClose) match across Spec (Key Entities + FR/SC), data-model E-8, and contracts C-2/C-6/C-7? [Consistency] — PASS: verified: 1201=SubInterpreterRejected and 1204=CallbackReentrantClose appear consistently in FR-018/FR-017/SC-007/Key Entities, data-model E-8 table, and contracts C-7/C-6; 1202=ObjectLifetime appears in FR-003/SC-001, E-8, C-2; all numeric values match across all artifacts; verified against error.h lines 157/159/163 (CodeGraph).
- [x] CHK010 Is the claim "`ObjectLifetime`/`CallbackReentrantClose`/`SubInterpreterRejected` already exist (PY-003/054)" consistent with the data-model E-8 source-line citations (`error.h` :157/:159/:163; `fixpp.i` class/mapping lines)? [Consistency, Spec Assumptions ↔ data-model E-8] — PASS: data-model E-8 cites error.h lines 157/159/163 for the three codes; verified against library source: FIXPP_ERR_BINDING_SUBINTERPRETER at :157, FIXPP_ERR_BINDING_OBJECT_LIFETIME at :159, FIXPP_ERR_BINDING_CALLBACK_REENTRANT_CLOSE at :163 — all present and matching their numeric values (1201/1202/1204).
- [x] CHK011 Is the Article XX amendment's deferral-to-`/implement` (carried as proposed text in the bundle, live `[2m]` edit NOT pre-applied) described consistently with the 043/051/054 precedent across plan §Gate-A and research? [Consistency, plan Article XX row ↔ research] — PASS: plan Gate A Article XX row states "amendment carried as proposed text — deferred to /implement"; research.md Proposed amendment section header states "not pre-applied — carried as proposed text"; spec Context section states "The amendment is carried as proposed text and deferred to `/implement`"; consistent with 054 precedent.
- [x] CHK012 Is the substantive amendment-site count stated consistently between the plan (6 substantive + 1 editorial) and the research enumeration of the same sites? [Consistency, plan Article XX row ↔ research Proposed amendment] — PASS: plan states "6 substantive + 1 editorial"; research Proposed amendment enumerates exactly 6 substantive sites (§1.3 rule (2), §6.5 send carve-out, §6.5 close carve-out, §6.7 table row 1204, §6.7 prose docstring :818-820, §9 seam #4 open comment) and 1 editorial (§6.7 table-note); counts match.

## Acceptance Criteria Quality & Measurability

- [x] CHK013 Can SC-005 (C-ABI surface byte-unchanged) be objectively verified by a single deterministic check? [Measurability, Spec §SC-005] — PASS: SC-005 criterion is "the C-ABI surface (`include/fix/c_api.h`) is byte-unchanged by this feature"; T026 implements it as a `git diff` header check + symbol audit — a single pass/fail check with no ambiguity.
- [x] CHK014 Is FR-001's "the flat substrate remains available and unchanged in behaviour" measurable via the pre-existing test suite staying green (SC-004)? [Measurability, Spec §FR-001 ↔ SC-004] — PASS: SC-004 states "All pre-existing flat-substrate tests (053 round-trip, 054 GIL-release canary / exception coverage / watchdog) pass unchanged after the OO layer is added"; this is an objectively measurable criterion (ctest prefix filter on the 053/054 test targets).
- [x] CHK015 Is SC-006 (Tier-1 `python-bindings` none/asan/tsan matrix green) tied to a concrete CI gate rather than left as an aspiration? [Measurability, Spec §SC-006] — PASS: SC-006 references "the Tier-1 `python-bindings` CI matrix (none / asan / tsan legs)"; this maps to the existing Tier-1 CI pipeline that runs the matrix as part of the run-tier1 label gate; it is a concrete, objectively verifiable CI gate.

## Scenario & Edge-Case Coverage

- [x] CHK016 Are requirements defined for the case where a raised binding code (e.g. `ObjectLifetime`) coincides with an error the C-ABI also reports — is precedence (Python-side check first) specified? [Coverage, contracts C-2/C-8] — PASS: contracts C-2 establishes precedence: "Every method … MUST check `self._dead` FIRST … DO NOT call the C-ABI"; FR-002 requires the liveness check "before making any C-ABI call"; the Python-side check therefore always fires first, and the C-ABI error is never observed for a dead wrapper.
- [x] CHK017 Is the relationship between the binding-internal `[1200,1299]` block and the Phase-4 `[1400,1499]` C-ABI block (051) specified so no overlap/minting is reintroduced? [Coverage, Gap, data-model E-8] — WAIVED: data-model E-8 documents the `[1200,1299]` block and explicitly states "no new error code; `0→1` freeze unaffected"; the `[1400,1499]` block is defined in error.h and is a separate range; no overlap is possible by numeric construction. Adding an explicit "range-non-overlap" requirement would be redundant with the existing "no new error code" invariant. Item is tagged [Coverage, Gap], not Completeness/Clarity/Consistency; WAIVED.

## Ambiguities & Gaps

- [x] CHK018 Does the spec define what evidence the freeze-held claim requires at close-out (T026 header-diff check + symbol audit), so SC-005 is auditable rather than assumed? [Gap, Spec §SC-005, tasks T026] — PASS: tasks T026 explicitly defines the evidence: "header-diff check (`git diff include/fix/c_api.h` empty) AND symbol-audit (no new exported symbol)"; SC-005 states the criterion; the combination of SC-005 + T026 makes the close-out audit concrete and deterministic.
- [x] CHK019 Is it unambiguous that applying the Article XX `[2m]` amendment is a DESIGN-DOC edit (not a constitution amendment), so it does not trigger a separate constitution-change process? [Ambiguity, plan Article XX row, research] — PASS: spec Context section calls it a design-doc amendment ("amends `[2m]` at the sites enumerated in research.md"); plan Article XX row calls it a "design-doc edit"; research.md Proposed amendment section header says "not a constitution amendment"; the precedent for this pattern is set by 054's Article XX checkpoint with the same framing; no ambiguity.

## Notes

- This checklist tests REQUIREMENTS quality around the freeze boundary, not the freeze itself (that is the T026/SC-005 runtime check).
- CHK012 traces to the `/speckit-analyze` scope: the amendment is carried-as-proposed-text and applied at /implement (task T025); the substantive-site list must agree between plan and research so T025 + the Gate-B completeness audit can verify it landed in full.
- Traceability: 19/19 items carry a spec/data-model/research/plan/tasks reference or a `[Gap]`/`[Ambiguity]` marker.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 18 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 1 |
| **Total** | **19** |

### SPEC-FIXED items
*(none)*

### DD-DECIDED items
*(none)*

### WAIVED items
- CHK017 — rationale: `[1200,1299]` and `[1400,1499]` are non-overlapping by numeric construction; "no new error code" invariant (data-model E-8) already covers this; item tagged [Coverage, Gap] only, not Completeness/Clarity/Consistency.

Anchors spot-verified: `[2m §6.7]` (section header at line 1252 confirmed via grep); the "§6.7 prose docstring" cited in research.md amendment sites is at lines 818-820 WITHIN the `[1200,1299]` error hierarchy code block (labeled "2m-owned per §6.7" at line 772) — NOT inside the §6.7 section itself; both locations verified. `[2m §9 seam #4]` (line 1428), `[const §X.1]` (not in 2m-pybind.md — referenced as a versioning convention in spec Assumptions; the constitution §X.1 is the authority) — all cited [2m §X] anchors resolve in signed-off revision `.specify/2m-pybind.md v0.3`.
