# ABI-and-Disclosure Requirements Checklist: Group Delimiter Resolution

**Purpose**: Unit-tests-for-English on the requirements governing the GA-frozen C ABI's construction path, the operator-facing disclosure of a behaviour change reachable through that frozen surface, the interop policy, and the three-path performance obligation. Tests whether these requirements are complete, clear, consistent, and measurable — NOT whether the code works.
**Created**: 2026-07-31
**Feature**: [spec.md](../spec.md) · [plan.md](../plan.md) · [tasks.md](../tasks.md) · [research.md](../research.md)
**Contracts in scope**: `contracts/capi_group_grammar.md`
**Audience**: Gate A/B reviewer (PR) · **Depth**: Standard

> **Item derivation.** The C-ABI surface is the second of the two surfaces `/clarify` widened onto and that **failed three consecutive Gate A rounds for want of design depth (RC3)** — the route from a `const Dictionary*` to the per-context delimiter was an outcome with no named mechanism until round 2. Disclosure items are pointed at this project's recorded *"undischargeable artifact re-entering through prose"* class (075 Gate A round 2), and the performance items at *"a narrative assertion in place of a bench"* (Article VIII §3).

## The C-ABI Construction Path — Route, Not Outcome (RC3)

- [ ] CHK001 Is the route from the C-ABI handle to the per-context delimiter **named** — which construction is used, which is barred, and under which constraint? [Completeness, Spec §FR-018b / capi_group_grammar.md C-9.2a]
- [ ] CHK002 Are the lifetime, ownership, and allocation disposition of the session-cached view specified, rather than left to the implementer? [Completeness, C-9.2a]
- [ ] CHK003 Is the behaviour of a handle carrying **no dictionary** stated as explicitly unchanged, so the new route cannot alter an existing accepted case? [Consistency, Spec §FR-018b / C-9.4]
- [ ] CHK004 Is a dictionary-present handle that cannot reach the context-keyed view required to **fail closed**, with the reason a context-free fallback would reinstate the very defect being removed? [Consistency, Spec §FR-018b]
- [ ] CHK005 Is the ancestor path stated as **already available** at the construction check — carried through an existing recursion — rather than requiring new plumbing across the boundary? [Clarity, Spec §FR-018 / C-9.2]
- [ ] CHK006 Are the call sites that are deliberately **not** converted enumerated, so a class-fix does not sweep them in? [Completeness, C-9.5]
- [ ] CHK007 Is the "one rule, two paths" property stated as a **bidirectional** criterion — accepted at construction if and only if inbound validation would accept the same placement? [Clarity, Spec §SC-012 / C-9.1]

## Frozen-ABI Integrity and Disclosure

- [ ] CHK008 Is the no-exported-signature-change requirement stated with the **concrete gate** that checks it, and is that gate one this repository currently runs? [Measurability, Spec §FR-018a / W-14]
- [ ] CHK009 Is the distinction between *a behaviour change reachable through a frozen surface* and *a surface change* stated explicitly, so the ABI-affecting gate treatment is not read as contradicting the freeze? [Clarity, Spec §FR-018a / §Assumptions]
- [ ] CHK010 Is the behaviour change **enumerated by group**, with the opening order that is accepted today and rejected after, rather than disclosed as a category? [Measurability, Spec §FR-019 / C-9.6]
- [ ] CHK011 Is the artifact that carries the operator-facing disclosure named as one that **exists in this repository**, rather than as a deliverable with no home? [Gap, Spec §FR-019 / §FR-020]
- [ ] CHK012 Is the relationship between the disclosed rejection and the no-new-rejections criterion stated as an **enumerated exception** rather than left as an apparent conflict? [Consistency, Spec §SC-007 / C-9.7]
- [ ] CHK013 Is the newly-correct rejection of an over-permissive membership case likewise reconciled against the no-new-rejections criterion, rather than only the C-ABI case? [Consistency, Spec §US1 scenario 4 / §SC-007]

## Interop Policy

- [ ] CHK014 Is the interop gate's role stated as **observational**, with an explicit prohibition on it altering resolution, a configuration surface, or an inbound branch? [Clarity, Spec §FR-020 / §FR-020a]
- [ ] CHK015 Is the absence of any compatibility or leniency mode stated as a requirement, with its consequence for keeping the regression pin carve-out-free? [Consistency, Spec §FR-020a / §FR-012]
- [ ] CHK016 Is the unavailability of the external reference engines stated as a bounded limitation on **corroboration** — with what is consequently discovered later — rather than as a blocker or as a silent omission? [Clarity, Spec §Assumptions / §Dependencies]
- [ ] CHK017 Is the premise that any delimiter choice rejects some schema-legal shape stated, so that the unconditional rule is understood as a policy decision rather than a costless one? [Completeness, Spec §FR-020]

## Performance — Three Paths, Benches in the Same Change

- [ ] CHK018 Are **all three** changed hot paths enumerated, each with the mechanism by which this feature makes it more expensive? [Completeness, Spec §FR-022]
- [ ] CHK019 Is a benchmark required **in the same change** for each of the three, with a narrative assertion explicitly refused as a substitute? [Measurability, Spec §FR-022 / §SC-009]
- [ ] CHK020 Is any intentional baseline update required to carry its **rationale** in the same change? [Completeness, Spec §FR-022]
- [ ] CHK021 Is the requirement that the C-ABI view be built **once per session, never per message** given a witness — given that no behavioural result distinguishes the correct route from the barred one? [Measurability, Spec §FR-022 / W-11a]
- [ ] CHK022 Is the regression budget stated against a recorded baseline for each of the three paths, rather than for the inbound path alone? [Consistency, Spec §SC-009]

## Notes

- Check items off as `[x]` with exactly one inline disposition tag: `PASS` / `SPEC-FIXED` / `DD-DECIDED §X` / `WAIVED: <reason>`.
- Items tagged **Completeness / Clarity / Consistency** may **not** be closed as `WAIVED` (pipeline.md step 9). CHK011 is deliberately tagged `[Gap]`, not `[Completeness]` — if the disclosure artifact genuinely does not exist, the honest resolution is a spec fix naming the one that does, but the tag does not pre-judge that.
