# Completeness-Verification Requirements Checklist: FIX Latest Typed Codegen (`fixpp::vlatest`)

**Purpose**: Validate the quality of the requirements governing the **non-circular completeness proof** (FR-006) — the load-bearing, highest-risk area, since FIX Latest has no QuickFIX reference peer. Gate-B review audience.
**Created**: 2026-07-15
**Feature**: [spec.md](../spec.md) · [contracts/build-and-verification.md](../contracts/build-and-verification.md)

## Requirement Completeness

- [x] CHK001 Is the census ground-truth source (raw `OrchestraFIXLatest.xml`, read independently of the loader/Dictionary the emitter uses) specified? [Completeness, Spec §FR-006] — PASS: FR-006, research R5, contract V-1 all state "directly from raw `OrchestraFIXLatest.xml`... independent pugixml walk, NOT via `OrchestraLoader`/`Dictionary`" verbatim-consistently.
- [x] CHK002 Is the emitted side's source (the projection-sourced per-message manifest) specified, AND are the **forbidden** sources (`MessageIR.fields` tag-deduped/immediate-parent-only; read-side `G_<no_tag>` version-wide union flyweights) explicitly excluded? [Completeness, Spec §FR-006 / data-model Entity 3] — PASS + realizability-checked: FR-006/R5/data-model Entity 3 name the manifest source (R2b projection's lossless occurrence list) and forbid both alternatives with the reason each is lossy/wrong-granularity. Realizability composes: `GroupOrderEntry.parent_path` (code-read, `tools/codegen/fixpp-codegen/ir.hpp:58-59`, populated at `ir.cpp:87-96` for the existing `<fix>`-schema walker) is the concrete existing pattern the R2b projection is specified to mirror for `fixr:`, and `emit_manifest` is specified as a real new sibling emitter (T012) consuming that occurrence list — not a forward-declared/aspirational artifact.
- [x] CHK003 Is the two-leg composition specified — V-1 (census: manifest ≡ raw-XML) plus V-1b (manifest ↔ shipped-class consistency) — with the reason the census alone does not pin the shipped read surface? [Completeness, Spec §FR-006 / US2] — PASS: FR-006, US2, data-model Entity 3b, and contract V-1b all state the composition and the reason (manifest is projection-sourced, shipped read classes are `MessageIR`/Dictionary-sourced — a different derivation) identically.
- [x] CHK004 Is the app-subset silent-misclassification gap covered by a discriminating requirement (V-2b: admin complement == exact set `{0,1,2,3,4,5,A,n}`)? [Coverage, Spec §FR-001 / V-2b] — PASS: FR-001, contract V-2b, data-model Entity 5 INV-5, and task T010 all specify the exact-set pin and its rationale (silent misclassification would otherwise have no RED test).

## Requirement Clarity

- [x] CHK005 Is completeness specified as **exact-multiset equality** (symmetric difference empty), explicitly NOT subset-presence? [Clarity, Spec §FR-006 / SC-001] — PASS: FR-006 and SC-001 both use "EXACT-MULTISET equality... not subset-presence" verbatim.
- [x] CHK006 Is the occurrence-path key `(MsgType, group_path, tag, presence/rule, datatype)` defined, with justification for why a flat per-message tag set is insufficient (top-level-vs-in-group, cross-group movement, reused-tag-under-different-parent)? [Clarity, Spec §FR-006] — PASS: FR-006, research R5, data-model Entity 3 all define the 5-tuple key and give the three-case justification identically.
- [x] CHK007 Is the per-surface granularity stated honestly and unambiguously — **occurrence-path** for the projection/builder surface (V-1) vs the coarser **class-reachable-field** for the read surface (V-1b ∘ V-1)? [Clarity, Spec §FR-006 / SC-001] — PASS: FR-006 "Per-surface granularity (stated honestly)" paragraph, contract V-1b granularity note, and research R5 all state this without overclaiming occurrence-path for the read classes.

## Requirement Consistency

- [x] CHK008 Is the non-circularity requirement stated consistently as **two independently-implemented walkers that share no code** (double-entry cross-check), across spec, data-model, and contract V-1? [Consistency, Spec §FR-006 / data-model Entity 3 / contract V-1] — PASS: FR-006, data-model Entity 3, and contract V-1 all state "N-1 — shares no code" / "independently-implemented walkers" consistently; T013 operationalizes it as a hard constraint on the test implementation.
- [x] CHK009 Is it consistently specified that the FR-007 differential round-trip is **circular** and blind to an absent field, so it cannot substitute for the completeness proof — retained only for value-fidelity? [Consistency, Spec §US2 / V-1b] — PASS: spec US2 and contract V-1b use the same "circular"/"blind to an absent field"/"retained only for value-fidelity" language.
- [x] CHK010 Do SC-001, FR-006, data-model Entities 3/3b, and contracts V-1/V-1b describe the same two-leg argument without contradiction (verbosity accepted; contradiction not)? [Consistency, Spec §SC-001] — PASS: cross-read all five artifacts; the argument (census pins projection/builder at occurrence-path, V-1b∘V-1 pins read surface at class-reachable-field, composition yields class≡raw-XML) is restated with growing detail across the chain but never contradicted.

## Acceptance Criteria Quality

- [x] CHK011 Is the discrimination requirement for V-1 specified as mandatory and measurable — mutation-tested RED under dropped-message, dropped-field, wrong-parent/wrong-depth, AND reused-tag-under-different-parent? [Measurability, Spec §V-1 / SC-001] — PASS: contract V-1 "Discrimination" bullet and task T013 both list all four mutation classes as mandatory RED-proof obligations.
- [x] CHK012 Is V-1b required to have its **own** class-side mutation witness (drop a field/message on the `emit_messages`/reify side), distinct from V-1's raw-XML/manifest mutations? [Measurability, Spec §V-1b] — PASS: contract V-1b "Discrimination" bullet and task T014 explicitly require a class-side witness "DISTINCT from V-1's raw-XML/manifest mutations."
- [x] CHK013 Is the manifest specified as emitted for **all 181** (ungated by the app-subset builder filter) so the census covers the full set, not just app messages? [Coverage, Spec §FR-006 / data-model Entity 3] — PASS: FR-006, data-model Entity 3, and task T012 all state "emitted for all 181, ungated by the app-subset builder filter" verbatim.

## Notes

- Audience: Gate B reviewers. These test whether the completeness **requirements** are written to be non-circular, exact, and discriminating — the failure modes recorded in [[feedback_verification_corpus_built_from_the_read_it_checks_is_blind]] and [[feedback_noncircular_census_projection_source_stops_pinning_shipped_classes]].
- Disposition each item at `/speckit-checklist-audit`. Completeness/Clarity/Consistency gaps here MUST be SPEC-FIXED or DD-DECIDED — never WAIVED.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 13 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 13 |

### SPEC-FIXED items

None.

### DD-DECIDED items

None.

### WAIVED items

None.

### Realizability sub-check (CHK002)

The census manifest is a codegen-time construct, not a runtime value type — checked per the task-prompt guidance that this feature's "entities" are IR projection outputs and CMake/verification gates. Verified the manifest's data source composes end-to-end and is not merely forward-declared: (a) `GroupOrderEntry.parent_path` (the multi-level parent-chain field the occurrence list needs) already exists in the codebase for the `<fix>`-schema walker (`tools/codegen/fixpp-codegen/ir.hpp:58-59`, populated `ir.cpp:87-96`) — the R2b projection is specified to produce an analogous structure for `fixr:`, not invent an unspecified shape; (b) `emit_manifest` is planned as a real new sibling emitter (task T012, mirroring the existing `emit_normative_refs.cpp` pattern and `main.cpp:91`'s `write_file` line), not a stub. No latent gap found.

Anchors spot-verified (code-read, this audit): `tools/codegen/fixpp-codegen/ir.hpp:58-59` / `ir.cpp:87-96` (`GroupOrderEntry.parent_path`); `tools/codegen/fixpp-codegen/emit_builders.cpp:646-648` (the `if (ir.ns != "v44") return {};` gate research/spec cite as needing widening — confirmed present verbatim) — all resolve as cited against the current `main` tree.
