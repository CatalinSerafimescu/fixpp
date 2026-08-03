# Specification Quality Checklist: C-ABI include isolation, delivered by the installed package

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-08-03
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — *requirements are stated as reachability and
      observable build outcomes, not as CMake property assignments. The measured-facts table cites files
      because the spec's job here is to record what was measured before scoping; those citations are evidence,
      not prescriptions.*
- [x] Focused on user value and business needs — *the value is the AGPL/commercial legal-isolation boundary
      (Article IV §2) actually holding for a C-ABI integrator.*
- [x] Written for non-technical stakeholders — *within the limits of a build/packaging feature; every user
      story is stated as what an integrator can and cannot do.*
- [x] All mandatory sections completed

## Requirement Completeness

- [ ] **No [NEEDS CLARIFICATION] markers remain** — **2 remain, deliberately.** FR-011 (`fixpp::service`
      scope) and FR-012 (non-CMake consumers). Both are genuine scope decisions with no defensible default,
      and pipeline step 2 (`/speckit-clarify`) is mandatory for ABI-surface features per const §XVI.3. They
      are routed there rather than guessed. **This box closes at `/speckit-clarify`, not before `/speckit-plan`.**
- [x] Requirements are testable and unambiguous — *FR-003 is stated as transitive reachability precisely
      because the #218 defect was a direct property reading clean while the transitive one was open.*
- [x] Success criteria are measurable — *SC-001 counts headers; SC-002 requires a red observation; SC-006
      requires a re-measurement.*
- [x] Success criteria are technology-agnostic (no implementation details) — *stated as reachability counts and
      observed pass/fail, not as target properties.*
- [x] All acceptance scenarios are defined — *5 user stories, 11 scenarios.*
- [x] Edge cases are identified — *6, including the `usr/`-prefix asymmetry and the `find_package`-time
      existence check that makes a naive export-set change break every consumer.*
- [x] Scope is clearly bounded — *bounded by the two open clarifications, which are explicit rather than
      implicit.*
- [x] Dependencies and assumptions identified — *7 assumptions, 3 dependencies.*

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification

## Notes

- **The issue's stated remedy was rejected during spec authoring**, with evidence: §7.4:503's
  `INTERFACE_INCLUDE_DIRECTORIES = include/fix/` cannot be satisfied without breaking `<fix/c_api.h>`, because
  every C-ABI header is included through the `fix/` component. The spec records this rather than inheriting the
  issue's framing.
- **FR-007 (demonstrated-red) is load-bearing** and is written to the project's standing rule that a gate never
  observed failing proves nothing. FR-008 guards the adjacent trap — a compile-fails assertion that would pass
  for the wrong reason is a proxy, not a witness.
- **Two clarifications block `/speckit-plan`.** Proceed: `/speckit-clarify` → re-validate this checklist →
  `/speckit-plan`.
