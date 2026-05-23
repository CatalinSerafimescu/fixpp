# Specification Quality Checklist: 010-session-cfg-lifetime

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-05-23
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — PASS: per the Content Quality note, C++ API surface naming is necessary for this internal refactor spec; same pattern as 005/009. spec.md "Content Quality" note documents the exception with precedent.
- [x] Focused on user value and business needs — PASS: 4 user stories with clear priorities and "Why this priority" rationale. US1 P1 = memory safety; US2/3 P2 = test coverage precision; US4 P3 = symmetry polish.
- [x] Written for non-technical stakeholders — PASS: with the documented exception; integrators and auditors are the audience; API names and file paths are required for testability.
- [x] All mandatory sections completed — PASS: spec.md has Context, Clarifications, User Scenarios, Edge Cases, Requirements (FR + Key Entities), Success Criteria, Assumptions, Normative References. All mandatory sections present.

**Notes on Content Quality**:

The spec inherently references C++ types (`SessionConfig`, `Session`, `session_error`, `[[clang::lifetimebound]]`) and file:line locations because **this is an internal library refactor of an existing, named API surface** — not a green-field user-facing feature. The "stakeholders" here are the library's integrators (C++ application developers) and the audit / coverage / sanitizer pipeline; describing the API surface in their language is required for the spec to be testable and unambiguous. This pattern matches prior shipped specs in the library (e.g., 005-session-establishment-fsm, 008-message-store, 009-session-fsm-finalize). The four W-5 candidates in FR-001 (A/B/C/D) are surfaced as a single `[NEEDS CLARIFICATION]` marker because each candidate has different observable ownership / sharing / allocator semantics — the choice is decided at `/speckit-clarify`, not silently picked in the spec.

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain — PASS: resolved at `/speckit-clarify` 2026-05-23: FR-001 = Option A (by-value `SessionConfig cfg_;`). See `## Clarifications` section.
- [x] Requirements are testable and unambiguous — PASS: FR-001 through FR-012 each include file:line or test-name references; FR-003 names the exact CMakeLists.txt block to remove; FR-005 names the one site at L1151.
- [x] Success criteria are measurable — PASS: SC-001 = "ASan reports no stack-use-after-scope" (binary); SC-002 = "every cell has one passing assertion" (mechanical count); SC-004 = "distinct error variant observable in tests"; SC-009 = waiver row annotated CLOSED with PR back-link (greppable).
- [x] Success criteria are technology-agnostic (no implementation details) — PASS: SC-001..SC-009 are stated as observable test outcomes rather than implementation choices; the one exception is the tool name (ASan) which is mandated by [const §IX.2] and is therefore a constitutional requirement, not an implementation detail.
- [x] All acceptance scenarios are defined — PASS: US1 has 3 ACs, US2 has 3 ACs, US3 has 3 ACs, US4 has 1 AC; every FR maps to at least one acceptance scenario (verified in coverage.md CHK054).
- [x] Edge cases are identified — PASS: spec.md §Edge Cases enumerates 4 scenarios: multi-session sharing, mutation-after-ctor, same-clock-value, FSM-unreachable-cells.
- [x] Scope is clearly bounded — PASS: FR-010 explicitly forbids amending 005 design; spec Assumptions §1 repeats; plan Summary repeats; scope is implementation-only.
- [x] Dependencies and assumptions identified — PASS: spec.md §Assumptions enumerates 6 assumptions; Normative References section lists all FIX spec, design-doc, and constitution citations.

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria — PASS: FR-001/002/003 → US1 ACs; FR-004 → US2 AC1; FR-005 → US2 ACs 2+3; FR-006 → US3 AC1; FR-007 → US3 AC2; FR-008 → US3 AC3; FR-009 → US4 AC1; FR-010/011/012 → hygiene FRs with their own verification gates cited.
- [x] User scenarios cover primary flows — PASS: US1 (P1 UAF fix), US2 (P2 observability + error precision), US3 (P2 matrix coverage), US4 (P3 transport-throw symmetry). All major waiver categories from PR #82 Gate B have a user story.
- [x] Feature meets measurable outcomes defined in Success Criteria — PASS: each SC maps to one or more tasks (SC-001 → T010/T011; SC-002 → T012/T017; SC-003 → T023; SC-004 → T013/T014; SC-005 → T019; SC-006 → T018; SC-007 → T021; SC-008 → T022; SC-009 → T027 bookkeeping).
- [x] No implementation details leak into specification — PASS: with documented exception per Content Quality note; the API surface naming is required for testability in an internal library refactor spec; pattern matches 005/009 precedent.

## Notes

- ~~1 intentional `[NEEDS CLARIFICATION]` marker on FR-001 (W-5 implementation choice A/B/C/D). Resolve via `/speckit-clarify` before `/speckit-plan`.~~ → **RESOLVED 2026-05-23: Option A (by-value member).**
- Gate A waiver / inheritance decision deferred to `/plan` (FR-012). Per 009 precedent and `library/CLAUDE.md`, no new design anchors are introduced — a single addendum on 005's Gate A is the expected outcome.
- W-1..W-4 (PR #82 Codecov DA/BRDA carry-forwards) have NO dedicated tasks (FR-011). The envelope rises organically as 010 tests touch `session.cpp`/`session.hpp`; `/speckit-verify` either clears or re-waives them per `[[feedback_codecov_patch_vs_lcov_da_brda_gate]]` + PR #73 precedent.
- Items marked incomplete require spec updates before `/speckit-clarify` or `/speckit-plan`.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 12 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **12** |

Anchors spot-verified: `[2d §4.5]` resolves at `2d-threading.md:496`; `[2e §4.1]` resolves at `2e-msgstore.md:16`; `[FIX-SL §4.10]` is a FIX Session Layer external doc (not in-repo; cited correctly by section number); `[const §IX.1]`, `[const §IX.2]`, `[const §XVII.8]` resolve in `constitution.md` Articles IX and XVII — all confirmed in signed-off v0.1 (2026-05-10).
