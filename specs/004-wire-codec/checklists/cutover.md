# Cutover / Migration Requirements Quality Checklist: Wire Codec

**Purpose**: Formal release-gate validation that the 2b cutover (FR-018 / SC-006) requirements are unambiguous, complete, consistent, and measurable — the highest-novelty, highest-risk surface in this feature. Tests the requirements, not the migration.
**Created**: 2026-05-16
**Audited**: 2026-05-20 (pipeline step 9 — `/speckit-checklist-audit`, retroactive post-merge per POLICY OVERRIDE)
**Feature**: [spec.md](../spec.md) · **Plan**: [plan.md](../plan.md) · **Research**: [research.md](../research.md) (D-15/D-16/D-17)
**Audience**: Reviewer at Gate A/B

## Surface-Migration Definition

- [x] CHK001 - Is "surface migration" defined explicitly and contrasted with "body-only swap" so the two are not conflatable? [Clarity, Spec FR-018 / research D-15] — **PASS**: spec FR-018 explicitly "**surface migration**, not a frozen-surface body swap"; research D-15 contrasts the two end-to-end ("body-only swap behind the frozen surface (rejected — mechanically impossible; breaks 003 I-12 with no scoped reconciliation)"); plan.md Structure Decision repeats the contrast ("Cutover = surface migration (D-15/RC#1), not a body-only swap"). Unambiguous.
- [x] CHK002 - Is the frozen-stub surface vs the `[2b §4.3]` real surface delta enumerated member-by-member (View base, `access_mode`, `msg_type/msg_seq_num/begin/end/offsets`, `field_view` derivation)? [Completeness, Spec FR-018 / data-model E4] — **PASS**: spec FR-018 enumerates the delta member-by-member ("frozen stub … pins a deliberately thin `MessageView`/`field_view` surface (no `View` base, `access_mode{Index}` only, no `msg_type/msg_seq_num/begin/end/offsets`, `field_view` non-`View`), whereas the `[2b §4.3]` real surface this feature delivers is `MessageView<Mode> : public View` with `access_mode{Iter,Index}` and the full member set, returning a `View`-derived `field_view`"); data-model E4 mirrors the enumeration; research D-15 repeats it. Complete.
- [x] CHK003 - Is "include path preserved, surface changed" stated as a single coherent requirement (not two contradictory statements about the same header)? [Consistency, Spec FR-018 / Plan Structure Decision] — **PASS**: spec FR-018 explicitly "the include path `<fixpp/wire/message_view_contract.hpp>` is retained as a thin re-export of the real `parser.hpp` `MessageView`, but its **surface changes** — it is not body-only"; SC-006(a) re-states "path-preserved, surface-replaced is one coherent requirement, not two conflicting ones"; plan.md Structure Decision binds the same; research D-15 confirms. One coherent requirement.
- [x] CHK004 - Is the design-doc-wins precedence over the under-specified frozen stub stated as an explicit requirement, not left to inference? [Clarity, Spec Authority anchor / research D-15] — **PASS**: spec.md Authority anchor: "Where this spec and the design doc disagree, the design doc wins; an inconsistency is a defect in this spec, not a design change"; research D-15 explicitly "These are **different surfaces**; the design doc wins, so the migration target is the `[2b §4.3]` surface and the frozen stub was a 003 under-specification relative to the anchor"; plan.md Structure Decision repeats "Per the design-doc-wins rule the migration target is the `[2b §4.3]` real ... surface". Explicit.

## 003 Drift-Guard Reconciliation

- [x] CHK005 - Is the requirement to retire the stub's own `sizeof(MessageView<Index>)==pointer` assertion stated distinctly from the requirement to preserve 003's I-1 `sizeof(<Msg>)==sizeof(MessageView<Index> const*)`? [Clarity, Spec FR-018 / SC-006(b)] — **PASS**: spec FR-018 explicit "003's own `I-1` `sizeof(<Msg>) == sizeof(MessageView<Index> const*)` invariant is *preserved* because a generated message holds a *pointer*, only the stub's own `sizeof(MessageView)==pointer` assertion is retired"; data-model E4 same; research D-15 same; tasks.md T030 same; plan.md Structure Decision same. The two assertions are addressed distinctly across all artifacts.
- [x] CHK006 - Is the rationale (a `: public View` type is no longer pointer-sized) documented so the retirement is auditable, not arbitrary? [Completeness, research D-15] — **PASS**: research D-15 explicit "that stub-`sizeof` assertion does NOT survive `: public View` and is retired"; data-model E4 same ("does not survive `: public View`"); plan.md Structure Decision same ("a `: public View` `MessageView` is no longer pointer-sized"); tasks.md T030 same. Rationale documented in 4 places.
- [x] CHK007 - Is "reconcile, not delete" specified for the 003 `flyweight_shape_test.cpp` drift guard so partial/over-deletion is out of spec? [Ambiguity, Spec FR-018 / SC-006(b)] — **PASS**: spec FR-018 binds "**reconcile** 003's R6 drift guard … which `static_assert`s the *frozen* member signatures and a `sizeof(MessageView<Index>) == pointer` invariant that does NOT survive `: public View` — that guard MUST be **updated** to the migrated surface (003's own `I-1` ... is *preserved* ...; only the stub's own `sizeof(MessageView)==pointer` assertion is retired)"; SC-006(b) "drift guard (seam #18) is reconciled to the migrated `MessageView : public View` surface and passes"; tasks.md T030 binds "**preserve** 003's I-1". The "reconcile, not delete" semantics is unambiguous.
- [x] CHK008 - Is seam #18 / I-12 ownership (003-side) vs 004's reconciliation obligation delineated so the cross-feature responsibility is unambiguous? [Consistency, Plan Test-seam mapping] — **PASS**: plan.md Test-seam mapping line "(cutover) 003 drift-guard reconcile | `tests/codegen/flyweight_shape_test.cpp` (003 seam #18/I-12 — updated to the migrated `MessageView:View` surface ...)" explicitly names "003 seam #18/I-12" as the owned-by-003 guard that 004 reconciles. Spec FR-018(b) same. Cross-feature responsibility delineated.

## 001 Leg (Net-New, Not Repoint)

- [x] CHK009 - Is the 001 FLOAT-accessor leg specified as 004-authored net-new wire code with an explicit statement that no 001 file exists to repoint? [Clarity, Spec FR-018 / research D-17] — **PASS**: spec FR-018 explicit "(d) deliver the `001-core-decimal` wire FLOAT-field accessor — this is **net-new wire code authored in 004** (001 shipped only `decimal_traits<T>::from_chars(span, mr)` and explicitly deferred the wire FLOAT parser/serializer to 2b per 001 spec.md:176 'Blocks: 2b'; there is no 001 file to repoint)"; research D-17 dedicates an entire entry; plan.md Project Structure repeats "(001 leg = 004-AUTHORED net-new wire code, NOT a 001 file to repoint — D-17)". Explicit and triplicated.
- [x] CHK010 - Is the 001-side success criterion stated measurably (decode `decimal_t` from a real `MessageView` field, allocation-free for `pod_decimal`)? [Measurability, research D-17 / Spec SC-006(d)] — **PASS**: research D-17 binds "the 001-side success criterion is 'the 004-authored wire FLOAT accessor decodes 001's `decimal_t` from a real `MessageView` field, allocation-free for `pod_decimal`'"; spec SC-006(d) binds "the 004-authored `001-core-decimal` wire FLOAT-field accessor executes end-to-end via `fixpp::decimal_t::parse(field_view::bytes(), mr)`"; tasks.md T019/T027 binds the verifying test `cutover_2b_gated_test::WireFloatAccessorLegOnRealSurface` with the "allocation-free for `pod_decimal`" predicate. Measurable.
- [x] CHK011 - Is the exact accessor path (`field_view::bytes()` → `decimal_t::parse(span, mr)`) specified as the FR-006 trait-decode boundary, with no wire-side decoding? [Completeness, Spec FR-006/FR-018] — **PASS**: spec FR-018 explicit "exercising it via `fixpp::decimal_t::parse(field_view::bytes(), mr)` on the real `field_view` (FR-006 path)"; spec FR-006 explicit "delegating decode/encode to the field-representation traits owned by 2a (`decimal<T>`) and 2c (`dict::field_traits<...>`); the wire layer MUST NOT re-implement field decoding"; data-model E-FV "the decimal arm reads `fv->bytes()` then `fixpp::decimal_t::parse(span, mr)` (003 I-1/RC#2)"; contracts/field_view.hpp pins the shape. No wire-side decoding.

## field_view Authority

- [x] CHK012 - Is there a single authoritative `field_view` shape oracle, and is its precedence over the frozen-stub / 003-oracle non-`View` shape stated explicitly? [Conflict, research D-16 / contracts/field_view.hpp] — **PASS**: research D-16 binds "Add `contracts/field_view.hpp` as the authoritative `field_view` shape oracle"; contracts/field_view.hpp explicitly "This oracle reconciles those against the `[2b §4.1]`/`[2b §612]` View-derived requirement so the cutover surface migration has a single authoritative `field_view` shape"; the precedence over the non-`View` stub shape is "design doc wins" per the Authority anchor (CHK004). Single authoritative source.
- [x] CHK013 - Is the retained `as_string()` member's purpose (source-compatibility with merged-003 call sites) documented so its presence is intentional, not incidental? [Completeness, research D-16 / data-model E-FV] — **PASS**: research D-16 + data-model E-FV explicit "retained `as_string()` (kept from the frozen-stub / 003-oracle surface so the cutover is source-compatible with every merged-003 call site)"; contracts/field_view.hpp comment "`as_string()` is retained from the frozen-stub / 003-oracle surface so the migration is source-compatible with every merged-003 call site". Intent documented.

## SC-006 Measurability

- [x] CHK014 - Are all four SC-006 sub-clauses (a stub gone, b drift-guard reconciled, c 003 reify green, d 001 FLOAT green) individually objectively verifiable? [Measurability, Spec SC-006] — **PASS**: SC-006(a) "frozen-stub **surface** is gone while the **include path** … is preserved" — verifiable by grep + compile; (b) "drift guard (seam #18) is reconciled … and passes" — verifiable by ctest on `flyweight_shape_test`; (c) "`dict::reify` round-trip (R6) executes end-to-end against the real `MessageView`/`field_view`" — verifiable by `cutover_2b_gated_test::ReifyRoundTripOnRealMessageView`; (d) "004-authored `001-core-decimal` wire FLOAT-field accessor executes end-to-end" — verifiable by `cutover_2b_gated_test::WireFloatAccessorLegOnRealSurface`. Each clause has a named test.
- [x] CHK015 - Is "zero references to the frozen-stub surface remain" specified with a concrete detection method (grep target) so it is testable, not aspirational? [Measurability, Spec SC-006 / quickstart §9] — **DD-DECIDED quickstart §9 + tasks.md T031**: quickstart §9 binds the "cutover sanity" grep; tasks.md T031 names the exact command "`grep -rn 'message_view_contract' include/fixpp/dict tests/` shows only the kept re-export shim and **zero references to the frozen-stub surface** remain anywhere in the tree (SC-006)". The detection method belongs in quickstart per Phase-1 split (spec carries the contract; quickstart carries the command).
- [x] CHK016 - Is the distinction between "include path survives as re-export" and "surface references must be zero" stated so they are not read as contradictory? [Ambiguity, Spec SC-006(a)] — **PASS**: SC-006(a) explicit "(a) the frozen-stub **surface** is gone while the **include path** `<fixpp/wire/message_view_contract.hpp>` is preserved — these are not contradictory: the *path* survives only as a thin re-export of the real `[2b §4.3]` `MessageView`, and the *old thin surface* no longer exists behind it (path-preserved, surface-replaced is one coherent requirement, not two conflicting ones)". The clause anticipates the reading and resolves it inline.

## Residual / Obsolescence

- [x] CHK017 - Is the disposition of `dict_reify_wire_body_not_ready` (slot 29) specified (kept, non-renumbering, annotated obsolete — not deleted)? [Completeness, data-model Error mapping] — **PASS**: data-model "Error mapping" explicit "Cutover note: `dict_reify_wire_body_not_ready (29)` (003's stub-not-ready signal) becomes unreachable once the real `MessageView` lands; it is **kept** (non-renumbering) and comment-annotated as cutover-obsolete, not deleted"; tasks.md T003 binds the action "keep slot-29 and comment-annotate it cutover-obsolete; do NOT re-introduce v0.1's deleted `wire_tag_count_exceeded`". Disposition specified.
- [x] CHK018 - Are the cross-doc items (D-9..D-12) explicitly classified as NOT 004 blockers so cutover scope is bounded? [Clarity, research / Plan Gate A] — **PASS**: research D-9/D-10/D-11/D-12 each binds the same disposition pattern ("not a 004 blocker"); research closing paragraph "Cross-doc items (D-9..D-12) are confirmations owned by 2d/2c/2e, explicitly *not* 004 blockers per `[2b §11]`"; plan.md Gate A row "Open cross-doc items inherited from `[2b §10]` ... are *cross-doc confirmations*, not 004 blockers — tracked in research D-9..D-12". Scope bounded.

## Traceability

- [x] CHK019 - Does every cutover requirement trace to FR-018 and a SC-006 sub-clause, with no cutover obligation lacking a measurable outcome? [Traceability, Spec FR-018/SC-006] — **PASS**: cutover requirement traceability matrix (verified element-wise): surface-migration → FR-018(a) ↔ SC-006(a); drift-guard reconcile → FR-018(b) ↔ SC-006(b); 003 reify rewire → FR-018(c) ↔ SC-006(c); 001 FLOAT leg → FR-018(d) ↔ SC-006(d); plan Test-seam mapping bind each to a named on-disk test (`cutover_2b_gated_test.cpp`, `flyweight_shape_test.cpp`, `contracts/field_view.hpp`). Every cutover obligation has a measurable outcome.

---

## Audit Result

**Feature**: 004-wire-codec
**Checklist**: `cutover.md` (19 items, CHK001–CHK019)
**Design-doc anchor verified**: `.specify/2b-wire.md` Draft v0.2 (Gate A round 1 converged)
**Audit mode**: retroactive post-merge per POLICY OVERRIDE (no spec/contracts/design-doc edits)

### Per-domain tally

| Domain | PASS | SPEC-FIX-CANDIDATE | DD-DECIDED | WAIVED | Total |
|--------|------|--------------------|------------|--------|-------|
| Surface-Migration Definition (CHK001–004) | 4 | 0 | 0 | 0 | 4 |
| 003 Drift-Guard Reconciliation (CHK005–008) | 4 | 0 | 0 | 0 | 4 |
| 001 Leg (Net-New) (CHK009–011) | 3 | 0 | 0 | 0 | 3 |
| field_view Authority (CHK012–013) | 2 | 0 | 0 | 0 | 2 |
| SC-006 Measurability (CHK014–016) | 2 | 0 | 1 | 0 | 3 |
| Residual / Obsolescence (CHK017–018) | 2 | 0 | 0 | 0 | 2 |
| Traceability (CHK019) | 1 | 0 | 0 | 0 | 1 |
| **Total** | **18** | **0** | **1** | **0** | **19** |

### Findings + resolutions

All 19 items dispositioned. Zero SPEC-FIX-CANDIDATEs. One DD-DECIDED (CHK015) — the grep detection method lives in quickstart §9 per the Phase-1 ownership split (spec carries the requirement contract; quickstart carries the exact command).

The cutover surface (D-15/D-16/D-17 + FR-018 + SC-006) is the highest-risk surface of 004 and was the explicit focus of Gate A round 1 RC#1; the surface migration is specified end-to-end across spec / data-model / research / contracts / plan / tasks / design-doc with consistent vocabulary and traceable per-clause anchors.

### Pipeline disposition

- **`/speckit-analyze` re-run required?** NO — zero SPEC-FIX-CANDIDATEs; spec/design-doc unchanged.
- **Verdict**: GREEN.
