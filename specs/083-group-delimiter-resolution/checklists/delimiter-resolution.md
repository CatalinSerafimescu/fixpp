# Delimiter-Resolution Requirements Checklist: Group Delimiter Resolution

**Purpose**: Unit-tests-for-English on the requirements governing this feature's primary axis — per-context delimiter resolution, its consumer contract, loader symmetry and fail-closed disposition, the receiver's descend-at-delimiter rule, member-set exactness, and the regression pin's non-circularity. Tests whether these requirements are complete, clear, consistent, and measurable — NOT whether the code works.
**Created**: 2026-07-31
**Feature**: [spec.md](../spec.md) · [plan.md](../plan.md) · [tasks.md](../tasks.md) · [research.md](../research.md) · [data-model.md](../data-model.md)
**Contracts in scope**: `contracts/group_ctx_delims.md` · `contracts/loader_tolerant_mode.md` · `contracts/consume_group.md`
**Audience**: Gate A/B reviewer (PR) · **Depth**: Standard

> **Item derivation.** Items are derived from the Gate A convergence record's named root causes — **RC1** (the consumer of the per-context delimiter was never designed), **RC2** (phase gates stated over artifacts their phase does not move), **RC4** (traceability one pass behind) — and from this project's recorded anti-pattern classes (circular corpus, unproven-RED gate, subset-not-exact-set gate, half-restructure, silent fallback, count-delta asserted rather than constructed). Items derived by re-reading the FR text this orchestrator wrote would be PASS-by-construction; these are pointed at what actually failed five times.

## Per-Context Resolution and Its Consumer (RC1)

- [ ] CHK001 Is the delimiter's defining rule stated once, unconditionally (declaration-order first member, resolved per context), rather than as a set of per-site behaviours a reader must reconcile? [Clarity, Spec §FR-001/§FR-002]
- [ ] CHK002 Is the disposition of a consumer-side lookup miss on the per-context delimiter table specified — where completeness is enforced, and which fallback values are forbidden **by name**? [Completeness, Spec §FR-023]
- [ ] CHK003 Are the non-throwing contract on `as_table_view()` and the load-time enforcement point stated together, so the pair cannot be satisfied by relocating the check to the consumer? [Consistency, Spec §FR-023 / group_ctx_delims.md C-3.4]
- [ ] CHK004 Is the **computation** of the set the completeness invariant checks over defined, rather than left as the unelaborated phrase "every context the consumer enumerates"? [Completeness, group_ctx_delims.md C-3.4a]
- [ ] CHK005 Is the leg of that invariant which depends on an unresolved research question marked **conditional**, with the resolving work named and sequenced **ahead of** the change it gates? [Clarity, research.md §D-12 / C-3.4a]
- [ ] CHK006 Is the interaction between tolerant mode and the completeness invariant stated as its own requirement, rather than left inferable from the two in combination? [Consistency, Spec §FR-023a / §FR-006a]
- [ ] CHK007 Is the invariant's premise identified as holding **by measurement rather than by construction**, and given a precondition instead of an assumption? [Clarity, Spec §FR-023b]
- [ ] CHK008 Is the store's structural inability to represent more than one context per `(msg_type, no_tag)` recorded as an inherited limit with its mechanism, rather than presented as a property of this feature's design? [Completeness, Spec §Edge Cases / C-3.5]

## Loader Symmetry, Recursion, and the Fail-Closed Default

- [ ] CHK009 Is cross-loader equivalence stated as a requirement with an explicit statement of what partial delivery would constitute, rather than as an aspiration? [Completeness, Spec §FR-005]
- [ ] CHK010 Is the fail-closed default specified to **mirror an existing loader disposition**, with the exception type per loader and both fuzz harnesses' documented exception sets named? [Completeness, Spec §FR-006c]
- [ ] CHK011 Is the condition that triggers the fail-closed path distinguished, **at one granularity**, from a group that is declared but referenced by no message? [Clarity, Spec §FR-006d / loader_tolerant_mode.md C-6.1a]
- [ ] CHK012 Is the requirement that all ten shipped dictionaries still load under the new default stated as a **gating measurement before the path is enabled**, not as an expected outcome? [Completeness, Spec §FR-006b]
- [ ] CHK013 Is the consequence of deleting the one-level component scan on the **global** first-field lookup specified, given that lookup still serves as an is-this-a-group predicate at other call sites? [Completeness, C-1.4b / research.md §D-10]
- [ ] CHK014 Is the ordering between writing the new per-context projection and the load-time collision guard that reads it stated as a requirement, rather than left to implementation order? [Consistency, loader_tolerant_mode.md C-7.2]
- [ ] CHK015 Is the source of declaration order named as an existing traversal the loaders already perform, rather than a new one — and is the reason a third traversal is rejected stated? [Clarity, research.md §D-1]

## Reception — Descend at the Delimiter

- [ ] CHK016 Is the ordering constraint between descend-at-delimiter and recursive resolution stated as a **hard prerequisite** with the consequence of violating it quantified? [Clarity, Spec §US2 "Why this priority" / §FR-007]
- [ ] CHK017 Are the properties descent must not weaken **enumerated** (instance count, required members, extent termination), rather than compressed into "must not regress"? [Completeness, Spec §FR-008]
- [ ] CHK018 Is bounded depth behaviour specified with a **named witness and a file**, rather than being a requirement no artifact owns? [Coverage, Spec §FR-009 / consume_group.md W-3]
- [ ] CHK019 Is the reception witness required to run against a **populated** context store, rather than one whose lookups resolve through the bare global fallback? [Measurability, consume_group.md W-1a]

## Membership Exactness and #210's Two Consequences

- [ ] CHK020 Are #210's two consequences each required to be witnessed **by their own mechanism**, with substitution by the set-cardinality criterion explicitly forbidden? [Consistency, Spec §FR-010a / §FR-007a / §SC-013]
- [ ] CHK021 Is the negative outcome — the swallow proving unreachable — given a defined recording obligation, so the requirement cannot be silently dropped when the reproduction fails? [Completeness, Spec §FR-007a]
- [ ] CHK022 Is the false load-bearing source comment identified by **exact location** and distinguished from adjacent **true** text at the same site? [Clarity, Spec §FR-011]
- [ ] CHK023 Is the decision to **retain** the delimiter's member injection and pin it as a no-op stated, rather than leaving a reader to infer that the injection is deleted? [Clarity, C-3.3 / research.md §D-5]

## Regression Pin — Non-Circularity, Proven RED, Exact Set

- [ ] CHK024 Is the pin's independence specified as a **derivation rule** (expected values from a source sharing no code with the implementation, plus third-authority corroboration of a documented sample) rather than as the adjective "independent"? [Measurability, Spec §FR-013]
- [ ] CHK025 Is the pin required to be **observed failing** against a reintroduced defect, with that demonstration recorded? [Measurability, Spec §FR-014 / §SC-006]
- [ ] CHK026 Is member-set exactness required to ride the **same** pin rather than being emitted as a second half-pin? [Consistency, Spec §FR-015]
- [ ] CHK027 Is the prohibition on citing the existing collision-membership guards as delimiter coverage stated as a requirement, with the reason their green is a proxy gap? [Completeness, Spec §FR-016 / §US4]
- [ ] CHK028 Is the pin's scope stated as **exact and carve-out-free** across all ten dictionaries, rather than as a subset or a per-dictionary exemption list? [Completeness, Spec §FR-012 / §SC-001]
- [ ] CHK029 Is the registered-group count change required to be justified **by naming the responsible groups**, rather than by asserting a bare number? [Measurability, Spec §FR-017 / §SC-005]
- [ ] CHK030 Is 072's load-time collision guard's dependence on the value this feature re-derives stated, together with the requirement to **re-derive** its census and re-state the inherited audit on the new basis? [Completeness, Spec §FR-012a / §SC-014]
- [ ] CHK031 Are the pre-fix baseline figures required to **reconcile** against the checked-in pin's observed counts, with authority of record transferred from the scratch probe to the pin? [Measurability, Spec §SC-015]
- [ ] CHK032 Is the one **projected rather than measured** figure distinguished from the measured ones, with the work that converts it named and sequenced first? [Clarity, Spec §SC-001 / §Baseline measurement]
- [ ] CHK033 Do the dictionaries required to show no behavioural change form one set that agrees at every place it appears? [Consistency, Spec §SC-008]

## Notes

- Check items off as `[x]` with exactly one inline disposition tag: `PASS` / `SPEC-FIXED` / `DD-DECIDED §X` / `WAIVED: <reason>`.
- Items tagged **Completeness / Clarity / Consistency** may **not** be closed as `WAIVED` (pipeline.md step 9).
