# Read-Path-Consistency Requirements Checklist: Group Delimiter Resolution

**Purpose**: Unit-tests-for-English on the requirements governing the read path — the offset table's wire-derived delimiter census, the typed-read instance splitter, the extent walk's descend-at-delimiter repair, and the witnesses that pin validation/read agreement. Tests whether these requirements are complete, clear, consistent, and measurable — NOT whether the code works.
**Created**: 2026-07-31
**Feature**: [spec.md](../spec.md) · [plan.md](../plan.md) · [tasks.md](../tasks.md) · [research.md](../research.md) · [data-model.md](../data-model.md)
**Contracts in scope**: `contracts/typed_read_splitter.md` (owns `src/wire/offset_table.cpp`) · `contracts/consume_group.md` C-4.4
**Audience**: Gate A/B reviewer (PR) · **Depth**: Standard

> **Item derivation.** This is the surface that **exhausted Gate A loop 1**. The amendment cause — a site scoped out on a stated ground (*"its instance-boundary rule is not defective"*) that was **false at source** — plus **RC2** (a pin whose passing condition was that the defect is still present) and **RC3** (a `/clarify`-widened surface with no design depth at its centre) are what these items are pointed at. A second-order failure is in scope too: the amendment's own justification was wrong (a spin claim transplanted from a loop with no instance bound), which is why several items separate a requirement from its justification.

## Census of the Repeated Idiom, and the Grounds for Each Scope-Out

- [ ] CHK001 Is **every** wire-derived delimiter derivation in the file censused with its **role(s)**, rather than only the site whose behaviour changes? [Completeness, Spec §FR-021c]
- [ ] CHK002 Does each scope-out carry a stated ground that a reviewer can **check at source**, rather than an unfalsifiable assertion that the site is not defective? [Clarity, Spec §FR-021c / typed_read_splitter.md C-8.0b]
- [ ] CHK003 Is the site whose delimiter **source** changes distinguished from the sites whose **membership-probe** role must stay wire-derived, with the reason re-pointing the probe would collapse it into a tautology? [Clarity, Spec §FR-021c / C-8.0]
- [ ] CHK004 Is the residual left in the file after this feature recorded by line, with an explicit statement that the referenced issue is **not** closed and which of its legs remains? [Completeness, Spec §FR-021c / §Normative References L-063-4]
- [ ] CHK005 Is the effect of this feature's changed member sets on the arena **reserve estimator** required to be assessed and recorded, rather than assumed benign — given its documented failure mode is silent truncation? [Completeness, Spec §FR-021c / C-8.0a]

## The Splitter's Mechanism (RC3 — depth, not outcome)

- [ ] CHK006 Is the splitter's route to the per-context delimiter named as a **mechanism** — which seam it reuses, and why no new parameter crosses the public read API — rather than stated as an outcome? [Completeness, Spec §FR-021 / C-8.1]
- [ ] CHK007 Is the **key** the splitter resolves with specified precisely enough to distinguish it from the adjacent accessor that would produce a path one element too long? [Clarity, data-model.md Entity 2 / C-8.2]
- [ ] CHK008 Is the requirement to supply the new callback at **every** construction site stated, given that a missed site silently takes the dictionary-free fallback on nested splits? [Completeness, C-8.1 / tasks §T057]
- [ ] CHK009 Is the splitter's fallback disposition reduced to cases the splitter can **actually observe**, rather than including a state it cannot distinguish? [Measurability, C-8.4]
- [ ] CHK010 Is the requirement that public read signatures do not change stated alongside the internal seam widening, so the two are not read as in tension? [Consistency, C-8.3]

## Characterisation Before Fix

- [ ] CHK011 Is the characterisation obligation stated **symmetrically** — a reproduction of the mis-split **or** recorded evidence that none is reachable — so an unverified note cannot be carried forward a second time? [Completeness, Spec §FR-021a]
- [ ] CHK012 Is the repository's existing record of this defect required to be **cited as the starting point** and simultaneously **not inherited uncritically**, with the specific claim inside it that is false named? [Consistency, Spec §FR-021d]
- [ ] CHK013 Are the dispositions that record must carry at close-out enumerated, so a partially-discharged row is not re-stated as fully discharged? [Completeness, Spec §FR-021d / §Normative References]

## The Extent-Walk Descent (the scope amendment)

- [ ] CHK014 Is the descent stated as a requirement **separable from its justification**, so that correcting the justification does not reopen the scope decision? [Clarity, Spec §FR-021e]
- [ ] CHK015 Is the failure mode of the un-repaired walk characterised by **what the loop actually does** — bounded no-op iterations under an attacker-controlled declared count — rather than by a claim transplanted from a differently-bounded sibling loop? [Measurability, Spec §FR-021e / C-8.0c.3]
- [ ] CHK016 Is the descent required to **reuse** the walk's existing child context and existing depth guard rather than introduce a new mechanism, and is the overflow early-return's role in termination stated? [Completeness, Spec §FR-021e / C-8.0c.2 / C-8.0c.4]
- [ ] CHK017 Is the size of the population this repair affects stated with its two components (measured plus newly-registering), and does that figure agree wherever it appears? [Consistency, Spec §FR-021e / §SC-016]
- [ ] CHK018 Is the per-instance entry cap's interaction with an extent opened at the delimiter required to be **assessed and recorded** rather than assumed, given a breach would be a new rejection? [Completeness, Spec §FR-021e / §SC-007]
- [ ] CHK019 Is the claim that this repair is **not blocked by** the loader phase supported by a reason that survives the sibling clause added in the same amendment? [Consistency, C-8.6a / C-8.0c.5]

## Witness Integrity (RC2 — pins whose passing condition is the defect)

- [ ] CHK020 Are the exclusions on the invariance pin required to be **asserted inside the case** rather than secured by fixture choice alone? [Measurability, typed_read_splitter.md W-10]
- [ ] CHK021 Are **both** exclusions specified — the shape whose extent must change, and the polluted contexts whose extent moves because the member set the termination test consults stops being polluted? [Completeness, W-10]
- [ ] CHK022 Is the agreement witness required to run on a shape **structurally capable** of detecting this defect, rather than on the shape the walk already handled correctly? [Coverage, W-9]
- [ ] CHK023 Is the leg that **cannot be observed RED** given a discriminator other than a signal the implementation returns either way, with any timeout named as a backstop rather than the discriminator? [Measurability, W-10a leg 4]
- [ ] CHK024 Are the two distinct read-path shapes — a mis-split *inside* a correct extent, and an extent *truncated before the split runs* — required to be witnessed separately, with substitution of one for the other forbidden? [Consistency, Spec §US5 scenario 3 / §SC-016]
- [ ] CHK025 Is the expected extent required to be **derived from the fixture's own entry layout** rather than captured from the implementation after the fix? [Measurability, Spec §SC-016]
- [ ] CHK026 Is the previously-passing single-instance case required to keep passing, stated as its own obligation rather than implied? [Completeness, Spec §SC-016 / §US2 scenario 2]

## Notes

- Check items off as `[x]` with exactly one inline disposition tag: `PASS` / `SPEC-FIXED` / `DD-DECIDED §X` / `WAIVED: <reason>`.
- Items tagged **Completeness / Clarity / Consistency** may **not** be closed as `WAIVED` (pipeline.md step 9).
