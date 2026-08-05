# Anti-Vacuity Requirements Checklist: 087 system include binding

**Purpose**: Unit tests for the *requirements writing* on this feature's declared identity — that the gate
cannot report success having observed nothing. All four of Gate A's decisive catches sat on this axis, inside
the feature's own gate, so the requirements here are load-bearing rather than aspirational.
**Created**: 2026-08-05
**Feature**: [spec.md](../spec.md) §US2 · [research.md](../research.md) R7 · [contracts/system-include-interface.md](../contracts/system-include-interface.md) §6
**Audience**: Gate B reviewer. Depth: formal release gate.

> These items test whether the **requirements are written well**, not whether the gate works. "Is the guard
> specified?" — not "does the guard fire?".

## Requirement Completeness — the guards

- [ ] CHK021 - Is every anti-vacuity guard **enumerated**, with a count stated, rather than described as a general posture? [Completeness, research R7]
- [ ] CHK022 - Does each guard state **what makes it mechanised** — the specific clause that enforces it — rather than asserting that it is? [Completeness, Measurability, research R7 guards 1–6]
- [ ] CHK023 - Are requirements stated for the case where the observation is **present and parses but yields zero entries**, distinctly from the case where the input is absent? [Completeness, data-model I3, Contract C-2 box]
- [ ] CHK024 - Is there a requirement covering an expectation that is non-empty **as declared in the tree** but reaches the comparator **empty** — and is the guard for it specified as a mechanism rather than a note? [Completeness, Contract C-6.4, data-model I3, research R7 guard 1]
- [ ] CHK025 - Are requirements stated for the gate's carrier being **deleted**, distinctly from the comparator script being deleted while the carrier survives? [Completeness, Contract C-6.3 / §5 row 7, Spec §US2 scenario 3]
- [ ] CHK026 - Is there a requirement that the comparison have a **standalone, separately-invocable identity**, with the reason an inline block would be deletable without anything noticing? [Completeness, Contract C-6, plan §Structure Decision]
- [ ] CHK027 - Are requirements stated for the vacuity path that sits **outside** the gate entirely — a lane on which the witness stops registering — and is it bound to every workflow that runs the witness? [Completeness, Coverage, Spec §FR-014, §SC-008, Contract §6/§6a]

## Requirement Clarity — enforcement level is stated, never implied

- [ ] CHK028 - Are the invariants that are **review-time only** labelled as such, so none is read as enforced by the gate? [Clarity, Contract C-4, data-model I4, research R7 "one last turtle"]
- [ ] CHK029 - Is the residual set of unclosable vacuity paths **enumerated** rather than implied by a claim of singularity ("the last path")? [Clarity, Completeness, Contract §6 box]
- [ ] CHK030 - Is the distinction between "the gate cannot fail" and "the gate can be removed without anything noticing" stated as **one defect class with two shapes**, so a requirement closing one is not read as closing the other? [Clarity, Contract C-6, research R7 guards 3–4]
- [ ] CHK031 - Is `E2.present` (reply existence and parse) clearly separated from the non-empty-expectation arithmetic, given the two are easy to conflate? [Clarity, data-model E2/I3, Contract C-2 box]
- [ ] CHK032 - Is it unambiguous **which mode** of the comparator owns each fault cause, so no cause is named in one clause and defined in none? [Clarity, Contract C-2, C-6.4]

## Requirement Consistency

- [ ] CHK033 - Do the fault-cause **counts** agree across every artifact that states one — the cause list, the mode that defines them, and the demonstration that induces them? [Consistency, Contract C-2 / C-6.4 / §5 row 6a]
- [ ] CHK034 - Does the guard enumeration in `research.md` R7 agree with the guards the contract actually specifies, including any added after R7 was first written? [Consistency, research R7, Contract §6]
- [ ] CHK035 - Are the requirements internally satisfiable — does any rule governing the carrier conflict with a demonstration the same document requires? [Consistency, Contract C-6.2 vs §5, plan §Gate A i1r3]
- [ ] CHK036 - Is the ordering constraint on the carrier's legs stated **wherever the evidence depends on it**, rather than only where it is introduced? [Consistency, Contract §2b / C-6.2, Spec §FR-007a]
- [ ] CHK037 - Do the plan's Complexity Tracking mitigations describe the mechanisms as **mandated** rather than as already made, given nothing is implemented? [Consistency, Clarity, plan §Complexity Tracking]

## Acceptance Criteria Quality & Coverage

- [ ] CHK038 - Is the requirement that makes the observed side **measured rather than defaulted** paired with a criterion that could detect its violation? [Measurability, Spec §FR-002, Contract §5 row 1]
- [ ] CHK039 - Is there a requirement that the gate be observed red **before** it is ever observed green, and is the ordering stated as constitutional rather than stylistic? [Coverage, Spec §FR-007, plan Article VII §3, `tasks.md` T013 → T015]
- [ ] CHK040 - Are requirements stated for an **unrelated** failure being reported distinguishably from a genuine violation, and is "distinguishable" given an objective form? [Measurability, Coverage, Spec §FR-008, §SC-004, Contract §3]
- [ ] CHK041 - Does the spec state a requirement that a **count**, not merely an exit code, is what discharges a filter-selectable assertion? [Measurability, Spec §FR-014, quickstart §0 banner]
- [ ] CHK042 - Are requirements defined for the boundary case in which the comparator is implemented for **one leg only**, and would otherwise run through an already-required target reporting green? [Coverage, Edge Case, Contract C-6.4]
- [ ] CHK043 - Is the assumption that "asserting a count is not a registration" stated and reconciled against the no-new-registration requirement? [Assumption, Spec §Assumptions, §FR-013, §FR-014]
- [ ] CHK044 - Are requirements stated for what a *green* run does **not** prove — specifically the classification leg, which never varies in the passing state? [Coverage, Gap, plan §Complexity Tracking, Contract §5 row 4]

## Notes

- Check items off as dispositioned: `[x]` with an inline tag — `PASS` / `SPEC-FIXED` / `DD-DECIDED §X` / `WAIVED: <reason>`.
- Genuine Completeness / Clarity / Consistency gaps MUST be SPEC-FIXED or DD-DECIDED — never WAIVED.
- If **any** item is SPEC-FIXED, `/speckit-analyze` (pipeline step 6) is re-run before `/speckit-implement`.
