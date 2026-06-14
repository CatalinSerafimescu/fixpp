# Specification Quality Checklist: toAdmin/toApp observation coverage for engine-originated Reject and Logout emits

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-06-14
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

- **Clarification RESOLVED (`/speckit-clarify`, Session 2026-06-14):** BusinessMessageReject(35=j) veto semantics → **full `toApp` parity, veto honoured**. Grounded by a reference-engine sweep: QuickFIX-cpp `Session::sendRaw` routes `35=j` through the app branch (`isAdminMsgType("j")` false) and `catch (DoNotSend&) → return false`, and advances the inbound seqnum *before* the reject emit so suppression cannot desync. fixpp reuses its existing originate-path `toApp` (`app_do_not_send`) contract rather than adding an observe-only variant. Spec FR-004, the BMR edge case, and the assumptions were updated accordingly.
- The line-numbered emit-site inventory (assessment 2.4 §2) is intentionally kept out of the spec (implementation detail); it belongs in plan.md / tasks.md and will be re-verified against current HEAD during `/speckit-plan` (line numbers shifted ~+16 post-035).
