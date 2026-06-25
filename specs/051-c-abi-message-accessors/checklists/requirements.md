# Specification Quality Checklist: C ABI message surface — Feature C + [2i §4.3] error-block amendment

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-06-24
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — *exception: this is a C-ABI surface feature, so the published `extern "C"` symbol names and `fixpp_error_t` codes ARE the user-facing contract (the anchor `[2i]` is the spec of record); naming them is describing WHAT the consumer sees, consistent with the 049/050 house style.*
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders — *to the extent a C-ABI feature can be; the user is a C/binding-author integrator.*
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain — *all scope questions are DECIDED post-clarify/post-plan: FR-012 outbound + nested-group construction IN scope (+ the `fixpp_entry_group_begin` nested ABI), the toApp send-callback hook, the outbound session-close tombstone (lazy weak-ptr token), and the `[2i §4.3]` error-block placement (dedicated Phase-4 `[1400,1499]`). None remain flagged for `/speckit-clarify`.*
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic — *as far as a C-ABI feature allows; SC framed as observable consumer outcomes.*
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded — *Out of Scope explicit; FR-012 scope flagged.*
- [x] Dependencies and assumptions identified

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows (read, construct/commit/send, group read, group construct, error-block)
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification — *beyond the inherent C-ABI contract surface.*

## Notes

- **DECIDED (user, 2026-06-24): FR-012 (outbound repeating-group construction) is IN SCOPE** — the feature ships the full `[2i §4.8]` builder surface (read + write groups), INCLUDING nested outbound groups via the net-new `fixpp_entry_group_begin` ABI (closed by `fixpp_msg_group_end` under a LIFO contract). No L-051-x group limitation.
- **DECIDED at Gate A round 1: the session/app error-block numeric range = a dedicated Phase-4 block `[1400,1499]`** (not the cross-cutting `[11,99]` sentinel range), with a distinct message-construction reject `FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN` (1405) and a per-code introducing-minor table. The real `[2i §4.3]` diff was reviewed on the actual amendment.
- **DECIDED: the toApp send-callback hook ships** (closed `fixpp_toapp_verdict` enum), the outbound message is **tombstoned on session close** (lazy weak-ptr token check, FR-009a), and `fixpp_msg_commit`'s signature is authored in `contracts/message-write.md` against the real serialise surface.
- Items pass; spec + clarify + plan are complete and Gate A round 1 findings are applied.
