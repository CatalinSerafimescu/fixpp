# Specification Quality Checklist: Harden doubly-nested repeating-group correctness

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-07-12
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — *project convention (systems library): named code anchors (`emit_messages.cpp:263-268`, `group_view::operator[]`, `as_table_view()`, `first_field_tag`, `group_context`) are the contract surface, not implementation prescription; matches the 065 spec precedent. The load-vs-`as_table_view()` guard seam is deliberately left to `/plan`.*
- [x] Focused on user value and business needs — eliminates a silent wire-corruption path (mis-split) reachable from public dialect-XML load, and a silent-wrong typed read on doubly-nested layouts
- [x] Written for non-technical stakeholders — as far as a wire-protocol library admits; stakeholders here are integrating engineers
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic — *SC-006 references the GA-frozen C-ABI `1.5.0`; domain-essential, not a tooling detail*
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified — incl. the **independent** Part A / Part B fixtures: Part A = an inline-XML load-reject fixture driven through `XmlLoader::load_*`; Part B = a real generated `v44::MassQuote` + a hand-built `table_view` that **bypasses** `XmlLoader::load_*` (so it never encounters US1's guard). The two share no fixture.
- [x] Scope is clearly bounded — #184 (L-065-2) explicitly out of scope; guard scope limited to L-063-4 delimiter only
- [x] Dependencies and assumptions identified

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows (two independent P1 stories: guard+pins / typed-context fix)
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification — per the project-convention note above

## Notes

- **RESOLVED (`/speckit-clarify` Session 2026-07-12)**: the guard's enforcement seam is the `XmlLoader::load_*` load path (throw a new distinct typed `dict::` error during load; `as_table_view()` stays non-throwing; the loader gains the parent-chain walk). See spec `## Clarifications` + FR-003/FR-005.
- **Design note (independent fixtures — CORRECTED post-research)**: Part A's guard witness uses an inline XML loaded through `XmlLoader::load_*`; Part B's discrimination witness uses a real generated `v44::MassQuote` type over a **hand-built `table_view`** that bypasses `XmlLoader::load_*` entirely. The two share **no** test fixture. The earlier "US2 witness dialect must jointly satisfy US1's delimiter guard" coupling is **wrong** — a hand-built `table_view` never passes through the loader, so the guard is never invoked on it. See spec Edge Cases (US2 witness decoupled) + research D-B6.
- All items pass; spec ready for `/speckit-clarify`.
