# Specification Quality Checklist: Installable Packaging (CPack) + CMake Package-Config Export

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-07-31
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs)
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic (no implementation details)
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded
- [x] Dependencies and assumptions identified

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification

## Notes

- Items marked incomplete require spec updates before `/speckit-clarify` or `/speckit-plan`.

### Validation record

Validated in a single pass — the spec was authored against these criteria rather than drafted and then corrected, so there is no multi-iteration history to report. Three points where the criteria actively shaped the wording:

1. *No implementation details* — the feature description that seeded this spec named concrete CMake mechanisms (`install(EXPORT)`, `CMakePackageConfigHelpers`, `$<INSTALL_INTERFACE:>`). These were deliberately **not** carried into FR-001..FR-010, which state capabilities instead; selecting the mechanism is `/speckit-plan`'s job. Package *formats* (DEB/RPM/TGZ/ZIP) **are** retained — they are the user-visible deliverable and an explicit user decision, not an implementation choice.
2. *Success criteria technology-agnostic* — SC-005 states the outcome ("Debug packages yield usable symbolication … each by its own platform-appropriate check") rather than naming symbol-file formats or inspection tools, even though the underlying platform asymmetry is real and is captured as a requirement in FR-019.
3. *Requirements testable* — "the denylist must stay coherent with the export set" is unfalsifiable as prose, so FR-009 requires a **machine-checkable** statement of it, and SC-007 requires that assertion be **proven to fail on a deliberately broken input** before it counts as a gate. This follows the project's standing rule that a gate never observed failing proves nothing.

**Deliberate deviations from a strict reading of the checklist:**

- *"Written for non-technical stakeholders"* is satisfied only in the sense available to a build-and-distribution feature. Terms like "static library", "export set", and "debug information" are the domain vocabulary of the actual stakeholder (an integrator or release engineer). Replacing them would obscure rather than clarify.
- The **Context: verified starting state** table cites `file:line` evidence. This is intentional and is not "implementation detail leaking into spec": the anchor doc contains three verified-stale claims (FR-024), so this spec pins what was actually checked, on what date, against what source. Removing the citations would reintroduce exactly the drift this feature exists to correct.

### Carried constraints (do not relitigate downstream)

- **FR-007** (builder/validator libraries stay unexported) is settled by 078 Gate B P1.
- **FR-010** is a *verification* obligation with an escalation path, not a decision to be made in this feature.
- The **explicit non-goal** on shared-library variants is an ABI decision owned by REMAINING-WORK A-1.

### Open items blocking `/speckit-plan`

None. The three questions raised at specification time were resolved in the 2026-07-31 clarification session (Debug publication → CI artifacts only; GA version → keep `0.0.1`, defer to item 13; package licensing → ship dictionaries with full attribution). Each answer is recorded in the spec's **Clarifications** section and applied to Assumptions 2, 3, and 10 respectively.

### Re-validation after clarification (2026-07-31)

All 16 items remain passing; no regressions. Three notes on items the new content touches:

- *No implementation details* — still passing. FR-018b names a `NOTICE` file, which is a legally-required deliverable artifact with a conventional name, not an implementation choice. FR-018a/c/d likewise state obligations rather than mechanisms.
- *Requirements testable* — strengthened. The attribution requirements pair with SC-013/SC-014, and FR-018d requires verification by **enumerating installed package contents** rather than by reading install rules, because an install rule that silently matches nothing yields a legally deficient package that looks correct in the build system.
- *Dependencies and assumptions identified* — strengthened. Assumption 10 states explicitly that this feature discharges the mechanical attribution only and does **not** close REMAINING-WORK item 15d, whose counsel review remains open. That boundary is recorded so downstream review does not mistake shipped attribution for legal clearance.
