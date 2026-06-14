# Specification Quality Checklist: Acceptor inbound-Logon SendingTime guard + session/reconnect hardening riders

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-06-14
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — *FIX tag numbers (52/371/1137) and protocol message types (Logon/Reject/Logout) are domain vocabulary, not implementation; no source symbols, file paths, or function names appear in requirements.*
- [x] Focused on user value and business needs — *anti-replay/parity on the establishment boundary; robustness against third-party callback misbehaviour; verification completeness.*
- [x] Written for non-technical stakeholders — *scenarios stated as counterparty/operator-observable behaviour.*
- [x] All mandatory sections completed — *User Scenarios, Requirements, Success Criteria all present.*

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain — *open scope questions (missing-52 upstream handling, realizability) captured as Assumptions with an explicit "confirm during planning" note rather than blocking markers, each with a reasonable default.*
- [x] Requirements are testable and unambiguous — *each FR maps to an observable accept/reject/establish/disconnect/persist outcome.*
- [x] Success criteria are measurable — *SC-001..004 each assert a concrete observable outcome demonstrable by test.*
- [x] Success criteria are technology-agnostic — *no source symbols; protocol-level outcomes only.*
- [x] All acceptance scenarios are defined — *3 user stories, each with Given/When/Then scenarios incl. conforming no-regression paths.*
- [x] Edge cases are identified — *future-dated, boundary divergence, missing-52, latency-disabled, non-rotation reconnect.*
- [x] Scope is clearly bounded — *FR-010/FR-011 + Overview explicitly fence out the other F-f tail items and any new wire/config/codegen/C-ABI surface.*
- [x] Dependencies and assumptions identified — *Assumptions section: upstream missing-52, realizability, latency-policy reuse, reference-engine grounding, Group 2 mechanism, fixture churn.*

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria — *FR-001..009 trace to User Story 1/2/3 acceptance scenarios; FR-010/011 are scope fences verified by a no-new-surface check.*
- [x] User scenarios cover primary flows — *reject-stale, reject-malformed, conforming-establish (Group 1); throw-contained, transparent-passthrough (Group 2); absent/non-conformant 1137 witness (Group 3).*
- [x] Feature meets measurable outcomes defined in Success Criteria — *SC-001..004 cover Groups 1 and 3; Group 2 deliberately SC-free with a stated rationale.*
- [x] No implementation details leak into specification — *confirmed; grounding references to reference-engine functions live only in Assumptions as provenance, not as requirements.*

## Notes

- Group 2 (FR-006/FR-007) is intentionally SC-free — recorded in Success Criteria with rationale (marginal graceful-degradation, not a measurable user-facing outcome).
- Both previously-flagged assumptions are now RESOLVED during `/speckit-clarify` (2026-06-14): (a) missing-`52` is NOT caught upstream — `interpret_logon` skips tag 52, so the guard dispositions absent/empty as a uniform `reason=10` reject mirroring established-Q3; (b) first-Logon realizability confirmed — `52` is extractable via `scan_frame_header(frame).sending_time` in the acceptor first-Logon arm.
- All items pass on iteration 1; clarify pass complete (1 question). Ready for `/speckit-plan`.
