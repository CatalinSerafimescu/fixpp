# Verification-Evidence Requirements Checklist: 087 system include binding

**Purpose**: Unit tests for the *requirements writing* around what this feature must **observe and record** to
ship — the nine demonstrated reds and their asserted tokens, the cross-platform obligation, the CI
registration-count assertion, and the close-out evidence `/gate-b` reads.
**Created**: 2026-08-05
**Feature**: [contracts/system-include-interface.md](../contracts/system-include-interface.md) §5/§6 · [quickstart.md](../quickstart.md) §4 · [tasks.md](../tasks.md)
**Audience**: Gate B reviewer. Depth: formal release gate.

> These items test whether the **evidence requirements are written well** — whether each demonstration's
> induction, asserted token and recording obligation are specified unambiguously — not whether the
> demonstrations were run.

## Requirement Completeness — what must be recorded

- [ ] CHK045 - Is the recording obligation for each red specified as **exit status + asserted token + first diagnostic line**, with the reason an exit status alone is insufficient? [Completeness, Contract §5 preamble]
- [ ] CHK046 - Is every **induction class** defined in prose, and does the count in the prose agree with the classes the table actually uses? [Completeness, Consistency, Contract §5, quickstart §4]
- [ ] CHK047 - For each row that mutates the tree, is the **exact mutation** written out rather than described by a keyword flip whose meaning is ambiguous? [Completeness, Contract §5 demonstration-#2 and -#8 boxes]
- [ ] CHK048 - Where a mutation's expected magnitude has never been measured, is the expectation stated **qualitatively** with the reason no figure is given? [Completeness, Clarity, Contract §5 demonstration-#2 box]
- [ ] CHK049 - Is the fault-injection **seam** specified — against what artifact a reply-side mutation is applied — given the sub-build is wiped and reconfigured on every run? [Completeness, Contract §5 reply-side class, `run_consumer_witness.cmake:46`]
- [ ] CHK050 - Are requirements stated for the arguments a **direct** comparator invocation must be given, including any the enclosing CMake run would otherwise have supplied? [Completeness, Contract C-6.1, quickstart §4 closing paragraph, `tasks.md` T005a]
- [ ] CHK051 - Is there a requirement that every fault-inducing demonstration run against the **shipped** script rather than a re-implementation? [Completeness, Contract C-6.5]
- [ ] CHK052 - Are the evidence-durability requirements stated — a durable location rather than `/tmp`, and capture of the real exit code rather than a redirect that discards it? [Completeness, quickstart §0/§4]

## Requirement Clarity — each row's asserted token is determinate

- [ ] CHK053 - Does each demonstration row state the **complete** token set it asserts, rather than a single token where more than one fires? [Clarity, Measurability, Contract §5 rows 4 and 8, C-1]
- [ ] CHK054 - Is the rule governing multi-token reporting stated, with the scope of what may and may not co-occur? [Clarity, Contract §3, C-1]
- [ ] CHK055 - For the row whose token set depends on the comparison's **staging**, is the staging stated as normative rather than as an implementation note? [Clarity, Contract C-1, §5 row 4, data-model I2]
- [ ] CHK056 - Are the sub-cases of any multi-part row **enumerated and each marked mandatory**, so discharging one is not read as discharging the row? [Clarity, Contract §5 row 6a, quickstart §4]
- [ ] CHK057 - Is generator-specific diagnostic phrasing tied to the generator actually in use, with the alternative named as *not* applicable? [Clarity, Contract C-6.3, §5 row 7, Spec §Edge Cases]
- [ ] CHK058 - Is the same-run evidence obligation clear about **what produces it** — a gate output vs a follow-up read by the demonstrator? [Clarity, Spec §FR-007a, Contract §2b, quickstart §4 box]

## Requirement Consistency

- [ ] CHK059 - Does the number of red demonstrations agree across the contract's table, the plan's Technical Context, the quickstart and `tasks.md`? [Consistency, Contract §5, plan §Technical Context, quickstart §4, `tasks.md` header]
- [ ] CHK060 - Do the workflow anchors and expected count cited for the CI assertion agree between the contract and the plan, and are they cited to the lines that decide them? [Consistency, Contract §6b, plan sequencing step 9]
- [ ] CHK061 - Is the set of files this feature edits stated consistently between the plan's Technical Context, its Article VII §8 row and the task list? [Consistency, plan §Technical Context / Article VII §8, `tasks.md`]

## Coverage & Measurability

- [ ] CHK062 - Are requirements stated for verifying the mechanism on **both** measured platforms before any artifact prescribes it, and is the record per platform? [Coverage, Measurability, Spec §FR-009, §SC-006, research R6]
- [ ] CHK063 - Is there a requirement covering the **shipped comparator's** first execution on the second platform, distinctly from the instrument having been measured there? [Coverage, Gap; `tasks.md` T038]
- [ ] CHK064 - Are requirements stated for confirming the existing three-property comparison still passes unchanged, and is the comparison basis specified (per-test status vs a name set)? [Coverage, Measurability, Spec §FR-012, §SC-005]
- [ ] CHK065 - Is the scope of the regression run specified — full suite vs a label-filtered one — given this feature's diff reaches outside the labelled subsystem? [Coverage, Spec §SC-005, quickstart §5]
- [ ] CHK066 - Are the close-out evidence requirements stated as **hard preconditions** with the artifact and section that must carry them? [Completeness, Traceability, `tasks.md` T043/T044, `[const §XVII.8]`]

## Notes

- Check items off as dispositioned: `[x]` with an inline tag — `PASS` / `SPEC-FIXED` / `DD-DECIDED §X` / `WAIVED: <reason>`.
- Genuine Completeness / Clarity / Consistency gaps MUST be SPEC-FIXED or DD-DECIDED — never WAIVED.
- If **any** item is SPEC-FIXED, `/speckit-analyze` (pipeline step 6) is re-run before `/speckit-implement`.
