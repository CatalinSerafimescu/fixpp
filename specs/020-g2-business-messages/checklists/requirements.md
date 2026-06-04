# Specification Quality Checklist: G2 Business-Message Interop (typed NewOrderSingle + ExecutionReport)

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-06-04
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — **exception (see Notes)**: FIX tag numbers (35=D/35=8/etc.), message types, API/type names (`Engine::send`, `fixpp::decimal_t`), generated namespaces (`fixpp::v44`), and prior-feature/catalogue identifiers are **domain vocabulary**, not implementation prescription — the FIX protocol + the project catalogue ARE the stakeholder domain here (consistent with 016–019).
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
- [x] No implementation details leak into specification (same domain-vocabulary exception as Content Quality, see Notes)

## Notes

- The spec references FIX tag numbers, message types (35=D/35=8), the numeric type
  `fixpp::decimal_t`, generated namespace `fixpp::v44`, and prior-feature
  identifiers (019 `Application`/`Engine::send`, A-001/A-006/A-014 catalogue rows,
  `[const §VII.6]`) as **domain vocabulary**, not implementation prescription — the
  FIX protocol and the project catalogue ARE the stakeholder domain here, consistent
  with every prior fixpp spec (016/017/018/019). The "No implementation details"
  checklist items are marked with this explicit exception rather than an unqualified check.
- Two forward obligations are captured as tracked deferrals (FR-015): full-field
  coverage and all-protocol-version coverage (the latter explicitly scheduled
  post-v1.0). Recorded in Out of Scope + SC-006.
- Items marked incomplete would require spec updates before `/speckit-clarify` or
  `/speckit-plan`. All items pass.
