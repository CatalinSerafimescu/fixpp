# Specification Quality Checklist: Dialect overlay (D-009)

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-10-31
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — see note 1
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders — see note 1
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain. The owner's three scope rulings and the orchestrator's
  technical answers (including Gate A round 1's, three of them owner-to-confirm defaults) are in spec
  Clarifications. `/speckit-clarify` (mandatory, `[const §XVI.3]`:
  dictionary, ABI) settled the four open points: Orchestra base in scope, enum values on existing fields
  allowed, L-422-1 fixed in 098, one overlay per load.
- [x] Requirements are testable and unambiguous. Each behaviour FR maps to an acceptance scenario or a
  success criterion; FR-017 requires each differential to be shown RED against a seeded mismatch.
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic — see note 1
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified, including the custom-header send case (L-422-1), the custom Data send
  case (091 R-2), the FIXT registry case (FR-021) and the typed-tier case
- [x] Scope is clearly bounded (Out of Scope lists the side findings to be filed as issues)
- [x] Dependencies and assumptions identified

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification — see note 1

## Notes

1. fixpp is a library whose users are C++, C and Python integrators, so the spec names the library's
   public types (`dict::XmlLoader`, `dict::Dictionary`, `dict::table_view`, `SessionConfig`), C-ABI
   codes, TOML keys and FIX tag numbers. That is how this repo's specs state behaviour (precedent: 091,
   096, 097). FR-002 lists the `table_view` queries the equivalence covers because the equivalence *is*
   the observable contract; plan chooses the merge mechanism.
2. US1 AS-1's "refused today" was checked in source at `8319c206`: `dictionary_driven_validator`'s
   step (a) refuses any tag not in `valid_tags_for(msg_type)` with `wire_unexpected_tag`, with no
   exemption for tags ≥ 5000.
