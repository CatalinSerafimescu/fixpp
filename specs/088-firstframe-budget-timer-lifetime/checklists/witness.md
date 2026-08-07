# Witness-Plan Checklist: 088-firstframe-budget-timer-lifetime

**Purpose**: Unit-test the *requirements* governing this feature's evidence — the 13 witness cells,
their mutants and the mechanisms they need. Gate A found a defect of this class in **three
consecutive rounds**, which is why this domain gets its own checklist.
**Created**: 2026-08-05
**Feature**: [spec.md](../spec.md) · SC-005/006/012/014/015/016/018, FR-010/011 · research §D-6, §D-9
**Audience**: Gate B reviewer

## Cell Requirements — Completeness

- [x] CHK001 - Does every named cell carry a *construction* — how the state is made — rather than a description of the state hoped for? [Completeness, Research §D-6.1] — PASS: D-6.1's table gives an explicit Construction column for all 13 cells, re-derived step-by-step against the mock's actual behaviour where needed (D-6.11 for B2/B5/B6).
- [x] CHK002 - Is each cell's **named mutant** stated, so "what would this cell fail against" is answerable without inference? [Completeness, Research §D-6.1] — PASS: D-6.1's "Kills" column names the mutant per cell; the D-6.8 matrix cross-tabulates every cell against every mutant.
- [x] CHK003 - Is the RED-first obligation stated as a requirement, including that the failure text be captured? [Completeness, Spec §FR-010] — PASS: FR-010 states the RED-first obligation; D-6.7 states the capture method ("Each cell records its own RED output in the verify record"), and SC-018 clause 2 binds capture explicitly for T6.
- [x] CHK004 - Is the requirement stated that each mutant kills **exactly one** cell, rather than at least one? [Clarity, Research §D-6.8] — PASS: tasks.md's standing discipline statement ("every mutant killing exactly one cell") plus the D-6.8 matrix's 1:1 mapping and T019/T030's re-confirmation tasks state this explicitly.
- [x] CHK005 - Are requirements defined for cells whose RED-basis is the *delivered* design rather than pre-fix `main`, so their proof is not taken against the wrong baseline? [Coverage, Research §D-6.7] — PASS: D-6.7 gives an explicit per-cell RED-basis table distinguishing `main`-based cells from delivered-design-mutant cells (B4 is even flagged as GREEN-on-`main`, a regression guard).

## Constructibility — the round-3 failure class

- [x] CHK006 - Is every cell's construction stated against the *actual* capabilities of the test double, rather than against its documentation? [Clarity, Research §D-9] — PASS: D-6.11 re-derives B2/B5/B6 explicitly against the mock's actual read/cursor implementation (with source citations), not its class-doc; D-9's own "Overturned at Gate A round 3" block records a prior violation of exactly this principle and its correction.
- [x] CHK007 - Are the mechanisms a cell needs enumerated and **priced** in the ledger, so an unpriced mechanism cannot be introduced silently at implementation time? [Completeness, Research §D-9] — PASS: D-9 enumerates and prices all six mechanisms with a stated ledger criterion ("prices *artifacts*... because its purpose is to stop unpriced surface entering the tree").
- [x] CHK008 - Is a requirement stated forbidding a hand-rolled substitute (e.g. a bespoke chunking transport in the test file) as a route around the ledger? [Completeness, Research §D-9] — PASS: D-9 states this directly — "A hand-rolled chunking `Transport` inside the test file is NOT the cheaper route. It is an **unpriced mechanism**..." — and the delivered tasks (T005/T006) implement the mechanisms in `mock_transport.hpp` itself, not a substitute.
- [x] CHK009 - For any cell depending on I/O *shape* — chunking, per-read sizes, latency — is the shape stated as producible by the double rather than assumed? [Clarity, Research §D-6.11] — PASS: D-6.11 re-derives producibility step-by-step for B2/B5/B6 against the mock's actual read/cursor/latency behaviour.
- [x] CHK010 - Is the requirement stated that a mutant RED must **terminate**, not merely fail — given a non-terminating loop is indistinguishable from a wrong assertion in a CI log? [Measurability, Plan §Carried obligations] — PASS: the requirement is fully stated — plan.md's round-4 carried-obligations item 3, propagated into `tasks.md` T015/T031 ("MUST be shown to TERMINATE") and T048 ("record every mutation RED — including B4's and B5's proven termination"). The proof itself is correctly deferred to `/speckit-implement` + `/speckit-verify`; that is the normal spec-vs-execution split, not a gap in the requirement.

## Discriminating Power

- [x] CHK011 - Is it stated *which* cell is the discriminating one for each mutant column, so a green non-discriminating cell is not read as evidence? [Clarity, Research §D-6.8] — PASS: D-6.8's "Reading the matrix" paragraph names the discriminating cell per column explicitly (e.g. "B2 is the **only** cell that separates the delivered invariant from the rejected comparison-only fix... T6 the only one for FR-018").
- [x] CHK012 - Are requirements stated for columns with **no** valid RED, so an empty column is published with a reason rather than left blank? [Completeness, Research §D-6.8] — PASS: D-6.8 does exactly this for the `guard omitted` column ("has NO RED cell anywhere, and that is stated rather than hidden... discharged structurally... published empty here instead of being filled with a cell that would pass regardless").
- [x] CHK013 - Where a cell's failure mode is a hang rather than an assertion, is that recorded as unusable-as-evidence with a named alternative? [Clarity, Research §D-6.8] — PASS: D-6.8's footnote on T2b's `TLS OUT map omitted` RED states this explicitly — "real but unusable as evidence... T6 is the cell that turns that hang into a bounded, attributable assertion."
- [x] CHK014 - Is the requirement stated that a mock-driven cell cannot witness a defect the mock is contractually bound to avoid? [Coverage, Research §D-6.10] — PASS: D-6.10 states this directly, with citation to project memory `[[feedback_verification_corpus_built_from_the_read_it_checks_is_blind]]` — "the instrument shares the property under test."

## Non-Vacuity

- [x] CHK015 - Is a *positive* initiation barrier required for each cell that asserts something about an in-flight operation, rather than an inference from absence? [Completeness, Research §D-6.13] — PASS: D-6.13(a)'s heading states the general principle ("an unset completion flag proves only 'not finished'... A positive barrier is required") and applies it consistently — T6 gets a real positive barrier; T2b, which cannot construct one, is honestly downgraded to "does NOT discharge the non-vacuity clause" rather than exempted silently.
- [x] CHK016 - Where non-vacuity is not discharged by a cell, is the division of labour stated explicitly — which cells carry it instead? [Clarity, Spec §SC-015] — PASS: SC-015 clauses (c)/(f) state this explicitly — "SC-015's non-vacuity therefore rests on T2a and T6," with T2b's remaining two jobs (accept-slot reclaim, promptness) named separately.
- [x] CHK017 - Are requirements defined distinguishing "the operation had not completed" from "the operation had started"? [Ambiguity, Research §D-6.13] — PASS: D-6.13(a) states this distinction as its organising principle (the withdrawn "completion flag still unset" form vs. the adopted "suspended *inside* the read" form).

## Determinism & Thresholds

- [x] CHK018 - Are timing values in cell constructions accompanied by the *rule* that makes them safe, rather than presented as chosen numbers? [Clarity, Research §D-6.11] — PASS: D-6.11 states the rule for both B5's 3 ms latency ("`3 ∤ 50`, so the two series never share an instant") and B6's 7 ms latency ("the two timer series MUST NOT share a common multiple inside the deadline window") — not bare chosen numbers.
- [x] CHK019 - Is the requirement stated that a cell must not depend on an ordering the design elsewhere disclaims as unspecified? [Consistency, Spec §SC-014, §SC-016] — PASS: actively enforced multiple times — T2a's exact-value assertion was widened at Gate A round 4 specifically to avoid relying on the `order[0]` ordering §D-6.10a/SC-014 disclaim as unspecified; SC-016 states the transport cells "no longer test an ordering at all."
- [x] CHK020 - Are ctest `TIMEOUT` and watchdog obligations stated as requirements for every cell whose mutant can hang? [Completeness, Research §D-6.10] — PASS (closed by amendment after this audit's escalation): the asymmetry was real — T6 (T024) and T2b (T022) carried a ctest `TIMEOUT` while B4/B5, whose `room == 0` mutants hang, did not. Closed in `tasks.md` by adding a numeric ctest `TIMEOUT` obligation to T015 (B5), T031 (B4) and T009 (their shared target). Every cell whose mutant can hang now carries both a construction that terminates and a `TIMEOUT` as second-line defence.

## Traceability & Preservation

- [x] CHK021 - Is each SC traceable to at least one named cell, and each cell to at least one SC? [Traceability, Spec §Success Criteria] — PASS: every *behavioural* SC maps 1:1 or many:1 to a named D-6.1 cell (SC-001→B1, SC-002→B3, SC-003→B4, SC-004→B6, SC-005/006→T1, SC-012→B2, SC-014→T3-T5, SC-015→T2a+T2b+T6, SC-018→T6). The *structural/build* SCs (SC-007 single-decision-point, SC-008 census, SC-009 sanitizer matrix, SC-010/SC-017 surface delta, SC-011 comments, SC-013 bounds) are code-shape or cross-cutting claims verified by dedicated non-mutation tasks (T018, T033, T042, T043, T044, T049/T050) rather than a 13-cell mutation witness — appropriately, since forcing e.g. "the public surface delta is empty" into a mutation cell would be nonsensical. This is standard practice, not a traceability gap.
- [x] CHK022 - Is the requirement that a pre-existing witness stay byte-identical stated, so a convenient edit to it is recognisable as a finding? [Completeness, Spec §FR-011] — PASS: FR-011 states "MUST NOT be modified to accommodate the fix. If it needs modification, the fix has changed behaviour beyond what this spec authorises"; T033 restates this as a literal byte-identity check ("a diff here is a finding, not a fixup").
- [x] CHK023 - Is the cell **count** stated as derived from the table rather than remembered, given it has drifted across rounds? [Consistency, Research §D-6.1] — PASS: D-6.1 opens with exactly this discipline stated and the full drift history (8→10→12→13) — "The count is derived from the table below, never remembered."

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 23 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| ESCALATED (not dispositioned) | 0 |
| **Total** | 23 |

> **CHK020 was escalated by the audit and closed by the orchestrator**, since the remedy was an edit
> to `tasks.md` — outside the auditor's scope. See the item for the amendment. The escalation was
> correct: B4/B5's mutants hang, this mutant class went undetected across two Gate A rounds, and no
> design decision justified their lacking the `TIMEOUT` that T6/T2b carry.

### SPEC-FIXED items
None.

### DD-DECIDED items
None.

### WAIVED items
None.

### Escalated items
None outstanding. CHK020 was escalated and is now closed — see the note above the tally.

Anchors spot-verified: `research.md` D-6.1, D-6.7, D-6.8, D-6.9, D-6.10, D-6.10a, D-6.11, D-6.12, D-6.13, D-9 (all resolve as cited) · `spec.md` FR-010/FR-011, SC-014/SC-015/SC-016/SC-018 (all resolve) · `tasks.md` T005/T006/T015/T018/T019/T021-T025/T030/T031/T033/T042-T044/T048-T050 (all resolve, referenced content matches).
