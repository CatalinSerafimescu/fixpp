# Contract — `consume_group` descend-at-delimiter

**Feature**: `083-group-delimiter-resolution` | Satisfies FR-007, FR-008, FR-009, FR-021b
*(Header brought level with the body 2026-07-31, Gate A round 3. C-4.4 below points at **FR-021e**'s repair at the second site — the pointer is this file's, the requirement and the clause are `typed_read_splitter.md`'s, so FR-021e is deliberately **not** listed as satisfied here. W-3's named case is added below; this file is the home Project Structure now gives it. The bundle has twice been found with a contract header one round behind its body — `group_ctx_delims.md` and `capi_group_grammar.md`, both in round 2's P3 sweep — so this line is checked rather than assumed.)*

## Problem

The instance scanner opens each instance at the delimiter and consumes it with a bare increment, **without descending**. The nested-group descent it already performs applies only to members scanned *after* the delimiter. When the delimiter **is** a nested group's count tag, that group's body follows immediately; those tags are not members of the outer group, so the member test fails, the inner scan breaks, the outer loop sees a non-delimiter tag and exits with one instance counted, and the declared-count check rejects.

**Silent on single-instance groups** — which is why it went unnoticed, and why the regression witness must be the two-instance form.

## Contract

**C-4.1** — When the instance-opening delimiter is itself a group **in child context**, the scanner descends into that nested group and resumes one past its extent. Otherwise it consumes the single field as today.

**C-4.2** — Descent reuses the existing depth guard and child-path construction already used for post-delimiter members. This is a **symmetry repair**: the same operation the scanner performs one line later, applied at the instance-opening position.

**C-4.3 — Depth remains bounded** by the existing nesting cap (FR-009). No new recursion limit is introduced; behaviour at and beyond the cap is unchanged.

**C-4.4 — This asymmetry exists at a SECOND site, and it is repaired there too.** *(Pointer added 2026-07-31, Gate A round 3 — user scope amendment on N23. This clause adds no work to Phase 2.)* `OffsetTable::consume_group_extent` (`src/wire/offset_table.cpp:438-503`) has the identical shape: a bare `++k` at `:475` consumes the instance-opening delimiter while the nested descent at `:485-488` sits inside the inner loop at `:476` and so reaches only post-delimiter members. Gate A round 2 recorded that walk as *"not defective"* and scoped it out; round 3 verified the claim at source and it does not hold. The repair is specified as **`contracts/typed_read_splitter.md` C-8.0c** — that contract owns `offset_table.cpp` — and is **listed in Phase 4**, with **W-10a** as its witness and **FR-021e / SC-016** as its requirement and criterion. *(Ordering pointer carried forward 2026-07-31, Gate A fresh loop round 2:)* since Gate A fresh loop round 1, **W-10a must be green at the Phase-3 exit** (`plan.md`'s Phase-3 gate, leg (iv)), which pulls the repair itself forward — C-8.0c stays specified in `typed_read_splitter.md` and listed in Phase 4's content, but must have landed by the Phase-3 exit. See C-8.0c.5.

**Phase 2's scope is unchanged**: this phase touches `include/fixpp/wire/validator.hpp` only, and its gate below is unchanged. The pointer exists so a reader arriving at the mechanism from either file finds both sites — the failure mode this bundle repeatedly hit is a symmetric API half-restructured, and the two halves are now named in each other's contracts rather than in one only.

## Invariants that MUST NOT move (FR-008)

**C-5.1** — Instance-count enforcement: a declared count that disagrees with the instances present still rejects.

**C-5.2** — Required-member enforcement: the delimiter's own required-bit is marked **before** descent, exactly as now. Descending must not skip that marking.

**C-5.3** — Extent termination: an instance still ends at the first non-member tag. Descent changes how the *delimiter* is consumed, never how the extent ends.

**C-5.4** — A genuinely invalid message still rejects. Correct resolution must not become permissiveness; a group opened with a tag that is not that context's delimiter is still an error.

## Ordering constraint — this contract lands FIRST

Non-negotiable, and the reason the phases are not interchangeable: **485 contexts** *(corrected 2026-08-02, Gate B r2 — superseding the earlier "232 measured FIX50SP2 contexts, plus 30 more"; the unconditioned re-measurement is FIX50SP2 240 + Orchestra FIX Latest 245; see `spec.md:342-346` / SC-016)* have a post-fix delimiter that is a nested group's count tag. Landing recursive delimiter resolution before this contract converts a wrong-delimiter defect into a false rejection across all of them — strictly worse than the current state.

### Gate between the two — REWRITTEN 2026-07-30 (Gate A round 1)

The previous gate read: *"the nested-delimiter reproduction is green **and the delimiter pin's failure count has not moved**."* **The second leg was vacuous.** This phase touches `include/fixpp/wire/validator.hpp` only; the delimiter pin (FR-012) is a dictionary-level assertion over loader output, so its count is unchanged **by construction**. A tripwire wired to nothing cannot detect the thing it was written to detect.

Worse, the first leg's only witness could not reach the code path the *next* phase depends on. W-1 is #208's *"minimal hand-built `table_view`"*, and hand-built fixtures **never populate `group_ctx_`** — documented at `include/fixpp/dict/table_view.hpp:346-349` and restated by `group_ctx_delims.md`'s lookup-miss section. But C-4.1's descent fires on a **context-keyed** query: the existing post-delimiter descent is `dict_.group_first_field(ctx.msg_type, child_path, t) != 0` (`include/fixpp/wire/validator.hpp:376`) and C-4.2 requires the new one to reuse that exact shape. So W-1 alone exercises only the **bare fallback** (`table_view.hpp:364`). Meanwhile no shipped dictionary has a nested-group delimiter in any context today — that is what Phase 3 creates — so nothing on the real dictionaries can exercise the context-keyed descent in Phase 2 even in principle.

Net, under the old gate: an implementation that descends correctly on the bare path and is broken on the context-keyed one — a mismatch between the loader's recorded `parent_path` and the validator's `child_path`, say — passes **every** stated Phase-2 gate, and the failure surfaces only after Phase 3, as the exact **485-context** *(corrected 2026-08-02, Gate B r2 — superseding the earlier "232+30"; see `spec.md:342-346` / SC-016)* false rejection the ordering constraint exists to prevent, at the most expensive point to unwind. (Anti-pattern: *context seeded lazily on ONE path leaves sibling paths default* — here the sibling is the production path.)

**The gate is now two assertions this phase can actually move:**

1. **W-1a** below is green — the nested-delimiter descent is proven on a **populated context store**, not only on the bare fallback.
2. The ten shipped dictionaries' **current resolved delimiters are unchanged** after this phase — the real content of "must not touch resolution", and unlike a failure count it is a statement about something this phase could plausibly break.

And the **Phase-3 exit gains a wire leg**: SC-004's named per-count-tag subset (1677, 1772, 40204, 41599, 42060, 1499, 1669, 1919) must be run and green at the Phase-3 exit, not deferred past it. `plan.md`'s Phase-3 gate was dictionary-level only, so between Phase 3 and Phase 5 the tree could sit in exactly the strictly-worse state this ordering constraint forbids, with every gate green.

## Witnesses

**W-1 — Nested-delimiter reproduction** (from fixpp#208, already reduced to a minimal shape). Hand-built `table_view`; resolves through the **bare** fallback:

| shape | before | after |
|---|---|---|
| outer group, 2 instances, delimiter is a nested group's count tag | REJECTED — instance-count mismatch | ACCEPTED, 2 instances |
| same, 1 instance | ACCEPTED | ACCEPTED *(must not regress)* |
| same, declared count ≠ instances present | REJECTED | REJECTED *(C-5.1)* |

**W-1a — the same shape on a POPULATED context store** *(added Gate A round 1; this is the Phase-2 exit witness, W-1 is not).* The identical two-instance shape, but with the outer group registered under a **non-empty parent path** so the descent resolves through `group_ctx_` rather than through the bare fallback. Either construction is acceptable: a synthetic dictionary loaded via `load_from_string`, or a fixture seeded through `set_group_first_ctx` / `add_group_member_ctx` under a non-empty path. Named case in `tests/wire/consume_group_nested_delim_test.cpp`: `ConsumeGroupNestedDelim.NestedDelimiterDescendsOnPopulatedContextStore`. Without it, the context-keyed leg of C-4.1 has no witness anywhere in the feature until Phase 3, which is after the point where it is cheap to fix.

**W-2 — Typed-read agreement** (FR-021b): a shape validating as N instances reads back as N instances. **This is an alias, not a separate witness** *(clarified Gate A round 3, N25 — round 1 left "W-2" pointing at an unnamed case).* It is delivered by **W-9** (`TypedReadSplitAgreement.ValidatedInstanceCountEqualsTypedReadInstanceCount`, fixtures (b) and (c)) and, for the delimiter-is-a-nested-count population, **W-10a** — both in `tests/wire/typed_read_split_agreement_test.cpp`; see `typed_read_splitter.md`. No case is named `W-2`.

**W-3 — Depth: a shape at the nesting cap is bounded and well-defined, not unbounded recursion.** *(Given a named case and a file at Gate A round 3, N25(b) — it was the only witness for FR-009 and User Story 2 scenario 4 and it had neither.)* Named case `ConsumeGroupNestedDelim.NestedDelimiterAtDepthCapIsBoundedNotUnbounded` in `tests/wire/consume_group_nested_delim_test.cpp` — the same file as W-1/W-1a, because it is the same mechanism at its boundary. Build the W-1 shape nested to the engine's supported limit and assert the validator returns a defined rejection rather than recursing without bound, and that the previously-passing behaviour at depths **below** the cap is unchanged.

**No new branch is introduced by W-3, and that is why it is P3 and not a gap** — C-4.3 keeps the existing `can_descend` / K=16 guard, so W-3 pins that the *new* descent position is subject to the *existing* cap. Its offset-table twin — the same question at `consume_group_extent`'s `kMaxGroupDepth` guard, where the depth-cap branch returns a **non-advancing** index — is a distinct hazard and is pinned separately by **W-10a** leg 4 (`typed_read_splitter.md` C-8.0c.3), not here.
