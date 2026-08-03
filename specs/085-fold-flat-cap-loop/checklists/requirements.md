# Specification Quality Checklist: Fold the Flat Per-Instance Cap Loop into the Nesting-Aware Traversal

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-08-03
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — **DEVIATION, deliberate. See Note 1.**
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders — **DEVIATION, deliberate. See Note 1.**
- [x] All mandatory sections completed — **was FALSE when first ticked; corrected 2026-08-03. See Note 6.**

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic (no implementation details) — **partial, see Note 2**
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded
- [x] Dependencies and assumptions identified

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification — **DEVIATION, deliberate. See Note 1.**

## Notes

**Note 1 — source-anchored specification is this project's established form for an internal-defect feature.**
Three Content Quality / Feature Readiness items are ticked as *deliberate deviations*, not as passes on the template's literal wording. This feature's entire subject is the relationship between two functions in one translation unit: the deliverable is "remove a walk that provably cannot fire". Stated without naming the sites, FR-001 would be untestable and FR-002's behaviour-preservation claim would be unfalsifiable. The precedent is `specs/083-group-delimiter-resolution/spec.md`, whose Context section names files, line numbers and functions throughout and which passed Gate A on that form. Recorded here rather than silently ticked so a reviewer can overrule it.

**Note 2 — SC-005 is necessarily mechanism-referencing, and was rewritten once to stay falsifiable.**
SC-001..SC-004 and SC-006..SC-009 are outcome-stated and technology-agnostic. SC-005 ("exactly one traversal") names the mechanism because the mechanism *is* the deliverable — an outcome-only restatement ("the engine is not slower") is already SC-006 and does not discharge the issue's acceptance criterion. Its first draft demanded a "strict reduction in entries visited per call", which **nothing in the engine can measure**: there is no entries-visited counter and adding one to a hot path to satisfy a criterion would be scope creep. Restated as discharged by source inspection, with SC-006's benchmark carrying the performance claim.

**Note 2a — three named artifacts were verified to exist rather than inherited.**
SC-001's two test names came from the issue body, not from a source read. Both were checked at `main` = `c1564dd2` before this checklist was signed: `DelimiterCensus.RedCountsReconcileWithSpecBaseline` at `tests/dictionary/delimiter_census_test.cpp:476`, and seven `TypedReadSplitAgreement.*` tests in `tests/wire/typed_read_split_agreement_test.cpp`. The cap's fixture-reachability (A-006a) was verified for the same reason. This matters because a *dangling* success criterion is indistinguishable from a satisfied one until someone tries to run it.

**Note 3 — A-001 is a load-bearing assumption, not a conclusion.**
The redundancy argument that justifies FR-001/FR-002 was derived by the orchestrator at `main` = `c1564dd2`. A-001 obliges `/speckit-plan` to re-verify all four steps independently rather than inherit them. If any step does not hold, the feature is re-scoped rather than patched. Downstream reviewers should treat the argument as *claimed and checkable*, not settled.

**Note 4 — no [NEEDS CLARIFICATION] markers were raised, and `/speckit-clarify` still changed the spec materially.**
The issue text, the L-063-4 row and 083's recorded evidence between them fixed every *scope* decision up front: leg 1 stays descoped (A-002), #180 stays closed (A-003), the dict-free path is preserved not repaired (FR-003, A-005), and no configuration surface is added (A-006). That is why the draft carried no markers. `/speckit-clarify` — mandatory here under constitution §XVI.3 as a wire-layer change — nonetheless ran a full 5/5 session on 2026-08-03 and produced seven new requirements (FR-001a, FR-003a, FR-005a, FR-007b, SC-004a, SC-010, A-006a) plus a rewritten SC-006. The lesson worth recording: *no unresolved markers* is not the same as *no unresolved decisions*. The five that mattered were all questions the draft had silently answered by picking a default — what "fold" concretely means, whether a discovered dict-free false positive gets repaired here, what evidence discharges "proven RED", which benchmark, and how to anchor references that go stale on merge. None would have surfaced as a marker, because each already had a plausible answer written into the spec.

**Note 5 — one clarification opened new work rather than closing it.**
Q2 surfaced a defect the feature was not looking for: on the dict-free path `group_end = entries_.size()`, so the flat cap's final segment counts trailing top-level fields toward one instance and can return `err_group_too_large` for a group far below the cap. It is deliberately **not** repaired here (FR-003a) — doing so would break SC-002's "zero outcomes change" on the one path this feature was told to leave alone. It is instead recorded and given its own tracking issue, gated by SC-010. Reviewers should read FR-003a as a scope *decision*, not an oversight.

**Note 6 — "All mandatory sections completed" was a FALSE tick, and the tick is what a downstream gate reads.**
When this checklist was signed, `spec.md` had **no `Normative References` section**. `[const §VI.5]` (`.specify/constitution.md:164`) requires one in every `/specify` artifact — *"listing the exact `[DocAbbrev §X.Y.Z] Title` entries from the coverage index that inform the spec"* — and the direct antecedent `specs/083-group-delimiter-resolution/spec.md` carries a full one, as do 074/075/076/078/079/081. Gate A round 1 caught it (Codex P1-1, confirmed at P1 by the Opus pass). The section now exists, with every anchor resolved against the tree at `main` = `c1564dd2` before it was written; the box is ticked **on that basis**, not re-ticked on the original one.

Two things worth recording rather than quietly fixing:
1. **The tick is the defect, not the omission.** A missing section is visible; a checklist asserting completeness over a missing section is what lets it pass a gate. The same failure shape as a dangling success criterion (Note 2a) — indistinguishable from a satisfied one until someone checks.
2. **The root cause is upstream and not fixed here.** `.specify/templates/spec-template.md` has **no** `Normative References` section (`grep 'Normative References' .specify/templates/spec-template.md` → no match), which is why roughly 30% of recent specs omit it (072, 073, 077, 080, and 085). Fixing this bundle fixes 085 only. Flagged for the orchestrator as repo infrastructure, deliberately **out of scope** for this feature's branch.

**Note 7 — three further items were re-examined at Gate A round 1 and are ticked with reasons, not re-ticked silently.**
- *"Requirements are testable and unambiguous"* — held, but only after two repairs. FR-001a's "verbatim / byte-identical" rule was **unsatisfiable** (the block moves from 4-space to 8-space indentation and `clang-format` is a Tier-1 gate), so it is now a nine-item semantic-preservation checklist. And the lying-count edge case's "fail-closed" was ambiguous enough that one reviewer read it as "rejects the mismatch"; it is now explicit that the term describes the **bound**.
- *"Success criteria are measurable"* — held for SC-002 only after narrowing. As first written it claimed a per-frame result-identity measurement over the corpora that no artifact performs; the universal claim is now carried by `research.md` R-1's source proof and the corpus by what it actually asserts. SC-003's mutation clause had **no** discharging artifact at all and now has one (FR-005b).
- *"All functional requirements have clear acceptance criteria"* — held after adding SC-005a (FR-002's comment content was verified by nothing) and after recording FR-009's allocation leg as discharged by construction with no SC, in `contracts/group_cap_accounting.md`'s Verification matrix.

**Note 8 — Gate A round 2: two ticks were held up by claims that did not survive checking, and are re-based here.**
- *"Requirements are testable and unambiguous"* — held, after **withdrawing a verdict this checklist had implicitly endorsed**. Round 1 recorded Article VII §3 as **"NOT CLEANLY APPLICABLE"**, resting on two claims: that a structural red-first artifact had *no precedent in this repo*, and that one would need a *novel structural/AST gate*. **Both are false.** `tests/dictionary/load_any_test.cpp:143-171` is the shipped precedent — a permanent, behaviour-blind structural pin on an *absence* — and it uses plain `std::string::find` over a source file slurped through `FIXPP_SRC_DIR`; no AST anywhere. Worse than the factual error: `.specify/constitution.md:86` and `[const §XX.1]` (`:402`) permit only compliance, amendment or a rationale-bearing waiver for a conflict with a mandatory article, and no sibling bundle has ever used a fourth. The requirement is now **FR-001b** (red-first structural pin, named, homed, wired and with a discriminant *verified to discriminate*) and **SC-005b**. The general lesson, which is the same shape as Note 6's: **a verdict a bundle invents for itself reads exactly like a satisfied gate to everything downstream.** A missing PASS is visible; a novel disposition is not.
- *"Success criteria are measurable"* — held for SC-006 only after its **basis** was corrected. Round 1 stated SC-006 as a same-session `main`-vs-branch A/B, and `plan.md`'s VIII §2 row cited it as the ±5%-vs-`bench/baselines/` check Article VIII §2 actually names. Those are different measurements, and the named artifact did not perform the named comparison — the same failure shape as a dangling success criterion (Note 2a), one level down: the *verdict* was honest, the *basis cell* was not. SC-006 now has two explicit legs, and leg 1 was **run three times** rather than specified and left. That is also how it was found that the repo's own comparator cannot read this baseline file (`tools/bench_compare.py` keys on `cpu_time`; the file carries `seed_median_ns`) — a defect in the remedy *both round-2 reviews recommended*, caught only by executing it.

- Items marked incomplete require spec updates before `/speckit-clarify` or `/speckit-plan`. None are incomplete; three carry documented deviations, one (Content Quality, "All mandatory sections completed") was corrected at Gate A round 1 rather than having been true when signed, and two (Note 8) were re-based at Gate A round 2.
