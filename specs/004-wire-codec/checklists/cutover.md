# Cutover / Migration Requirements Quality Checklist: Wire Codec

**Purpose**: Formal release-gate validation that the 2b cutover (FR-018 / SC-006) requirements are unambiguous, complete, consistent, and measurable — the highest-novelty, highest-risk surface in this feature. Tests the requirements, not the migration.
**Created**: 2026-05-16
**Feature**: [spec.md](../spec.md) · **Plan**: [plan.md](../plan.md) · **Research**: [research.md](../research.md) (D-15/D-16/D-17)
**Audience**: Reviewer at Gate A/B

## Surface-Migration Definition

- [ ] CHK001 - Is "surface migration" defined explicitly and contrasted with "body-only swap" so the two are not conflatable? [Clarity, Spec FR-018 / research D-15]
- [ ] CHK002 - Is the frozen-stub surface vs the `[2b §4.3]` real surface delta enumerated member-by-member (View base, `access_mode`, `msg_type/msg_seq_num/begin/end/offsets`, `field_view` derivation)? [Completeness, Spec FR-018 / data-model E4]
- [ ] CHK003 - Is "include path preserved, surface changed" stated as a single coherent requirement (not two contradictory statements about the same header)? [Consistency, Spec FR-018 / Plan Structure Decision]
- [ ] CHK004 - Is the design-doc-wins precedence over the under-specified frozen stub stated as an explicit requirement, not left to inference? [Clarity, Spec Authority anchor / research D-15]

## 003 Drift-Guard Reconciliation

- [ ] CHK005 - Is the requirement to retire the stub's own `sizeof(MessageView<Index>)==pointer` assertion stated distinctly from the requirement to preserve 003's I-1 `sizeof(<Msg>)==sizeof(MessageView<Index> const*)`? [Clarity, Spec FR-018 / SC-006(b)]
- [ ] CHK006 - Is the rationale (a `: public View` type is no longer pointer-sized) documented so the retirement is auditable, not arbitrary? [Completeness, research D-15]
- [ ] CHK007 - Is "reconcile, not delete" specified for the 003 `flyweight_shape_test.cpp` drift guard so partial/over-deletion is out of spec? [Ambiguity, Spec FR-018 / SC-006(b)]
- [ ] CHK008 - Is seam #18 / I-12 ownership (003-side) vs 004's reconciliation obligation delineated so the cross-feature responsibility is unambiguous? [Consistency, Plan Test-seam mapping]

## 001 Leg (Net-New, Not Repoint)

- [ ] CHK009 - Is the 001 FLOAT-accessor leg specified as 004-authored net-new wire code with an explicit statement that no 001 file exists to repoint? [Clarity, Spec FR-018 / research D-17]
- [ ] CHK010 - Is the 001-side success criterion stated measurably (decode `decimal_t` from a real `MessageView` field, allocation-free for `pod_decimal`)? [Measurability, research D-17 / Spec SC-006(d)]
- [ ] CHK011 - Is the exact accessor path (`field_view::bytes()` → `decimal_t::parse(span, mr)`) specified as the FR-006 trait-decode boundary, with no wire-side decoding? [Completeness, Spec FR-006/FR-018]

## field_view Authority

- [ ] CHK012 - Is there a single authoritative `field_view` shape oracle, and is its precedence over the frozen-stub / 003-oracle non-`View` shape stated explicitly? [Conflict, research D-16 / contracts/field_view.hpp]
- [ ] CHK013 - Is the retained `as_string()` member's purpose (source-compatibility with merged-003 call sites) documented so its presence is intentional, not incidental? [Completeness, research D-16 / data-model E-FV]

## SC-006 Measurability

- [ ] CHK014 - Are all four SC-006 sub-clauses (a stub gone, b drift-guard reconciled, c 003 reify green, d 001 FLOAT green) individually objectively verifiable? [Measurability, Spec SC-006]
- [ ] CHK015 - Is "zero references to the frozen-stub surface remain" specified with a concrete detection method (grep target) so it is testable, not aspirational? [Measurability, Spec SC-006 / quickstart §9]
- [ ] CHK016 - Is the distinction between "include path survives as re-export" and "surface references must be zero" stated so they are not read as contradictory? [Ambiguity, Spec SC-006(a)]

## Residual / Obsolescence

- [ ] CHK017 - Is the disposition of `dict_reify_wire_body_not_ready` (slot 29) specified (kept, non-renumbering, annotated obsolete — not deleted)? [Completeness, data-model Error mapping]
- [ ] CHK018 - Are the cross-doc items (D-9..D-12) explicitly classified as NOT 004 blockers so cutover scope is bounded? [Clarity, research / Plan Gate A]

## Traceability

- [ ] CHK019 - Does every cutover requirement trace to FR-018 and a SC-006 sub-clause, with no cutover obligation lacking a measurable outcome? [Traceability, Spec FR-018/SC-006]
