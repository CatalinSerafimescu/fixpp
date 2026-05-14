# Specification Quality Checklist: 002-dictionary-xml-loader

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-05-14
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — *Note: spec inherits a C++ API surface from design doc `[2c §4.5]` (`fixpp::dict::XmlLoader`, exception types). This is a deliberate carve-out for this project: Phase 4 specs are anchored to converged design docs and re-state the public types they ship. Same pattern as 001-core-decimal/spec.md.*
- [x] Focused on user value and business needs — §2 "Why" + §3 user stories carry the value framing.
- [x] Written for non-technical stakeholders — *Partial: §3 user stories are stakeholder-readable; §4 ACs and §6 NFRs use type names. Same trade-off the project accepted on 001.*
- [x] All mandatory sections completed (Summary, Why, User stories, ACs, NFRs, Out of scope, DoD, References).

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain — all 3 markers resolved by /clarify Session 2026-05-14 (Q1 → B four codegen-target versions; Q2 → A overlay absent; Q3 → A parser deferred to /plan). §10 converted from "Open questions" to "Follow-ups & deferred work" with trigger criteria for each gap (F1/F2/F3). The Edge-Cases "or document as caller-side UB" fork on null-`mr` was also closed in Gate A round 1 (Codex rescue P2.2 → Opus confirm) — spec §3 Edge Cases now states the ratified debug-asserted / release-undefined policy verbatim, matching research.md D-5 and `contracts/xml_loader.hpp` Preconditions.
- [x] Requirements are testable and unambiguous — every AC is paired with a concrete test seam (§9) and an observable failure mode.
- [x] Success criteria are measurable — §6 NFRs each carry a "How verified" cell with a concrete test or bench.
- [x] Success criteria are technology-agnostic for user-facing outcomes — NFR-002-1 specifies wall-clock target; technology-specific verifications (PMR allocator counting, TSan) sit on the "How verified" side, not the requirement side.
- [x] All acceptance scenarios are defined — §4 ACs cover load (10 cases), lookup (8), shape (5), threading (2), allocator (2) = 27 ACs.
- [x] Edge cases are identified — §3 Edge Cases enumerates double-load, null `mr`, semantically-inconsistent XML, mid-load OOM, unsupported version.
- [x] Scope is clearly bounded — §5 lists 10 explicit out-of-scope items, each citing the design-doc section that will eventually own them.
- [x] Dependencies and assumptions identified — §8 (in-tree + third-party + unblocks), §11 Risk register, and §Assumptions (A1..A5).

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria — §4 ACs directly map to §6 NFR verification mechanisms; §9 test seams enumerated.
- [x] User scenarios cover primary flows — P1 (wire integrator), P1 (test author), P2 (codegen consumer), P3 (runtime-XML-only version).
- [x] Feature meets measurable outcomes defined in Success Criteria — §6 NFR-002-1..6 are all quantitative or `noexcept`-checkable.
- [x] No implementation details leak into specification *beyond what `[2c §4.5]` already locked* — XML parser library choice deferred to /plan (R1); `Dictionary` storage layout deferred to /plan (only the ABI shape of `FieldRef` / `ComponentRef` / `GroupRef` is locked, since those are in the public header).

## Notes

- Items marked incomplete require spec updates before `/speckit-clarify` or `/speckit-plan`.
- All 3 NEEDS CLARIFICATION slots resolved inline during /specify (Session 2026-05-14 in spec.md). Per /clarify discipline this is unusual — markers are typically left for /speckit-clarify in a separate phase — but the user invoked clarification interactively as part of /specify and signed off. Tracking the gaps in §10 satisfies the user's explicit "Clearly mark follow ups for gaps" direction.
- Spec deliberately overrides one piece of the triggering description (return-type `expected_t<Dictionary>` → `Dictionary` by value with construction throws) per `[2c §4.5]`. Surfaced in §1 "Style note" so /clarify and reviewers see the deviation explicitly.
