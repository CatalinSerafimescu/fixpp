# Specification Quality Checklist: Session Establishment & FSM Core

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-05-17
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

- **All clarifications resolved 2026-05-17 via `/speckit-clarify` (3 questions, all OSS-reference-grounded against `reference-engines/` @ pinned tags).** 0 [NEEDS CLARIFICATION] markers remain; checklist fully passes.
  - **Q1** — sequence-gap disposition → **re-scoped Session-2026-05-18 (Gate A round 1):** the 2026-05-17 "explicit `RecoveryPending`, hold, no ResendRequest" answer was verified inverted against the bound QFJ `.def` oracle + C++/fix8 (all emit `ResendRequest(35=2)` on the gap). Corrected: too-high gap = **session-fatal** (Logout-with-text → disconnect; no `RecoveryPending` state); the real ResendRequest-driven recovery is the deferred session-recovery feature's.
  - **Q2** — conformance subset → **capability-partitioned**; in-scope = establishment/liveness/logout/reject/in-seq+too-low/SendingTime-CompID-version (FIX.4.2/4.4), recovery-dependent + too-high + FIXT/5.0SP2/4.0/4.1/4.3/5.0 deferred-with-traceability; QuickFIX/J acceptance defs = executable oracle; concrete TC-### split at `/plan`. **No constitutional scoped-subset allowance exists — `[const §VII.5]` is a recorded scoped WAIVER, not greened (Gate A round 1 corrected the fabricated `[const §V.5]` cite).**
  - **Q3** — stale-SendingTime → `Reject`(reason 10)→`Logout`→disconnect (Logon→logout-with-error); engine-unanimous, `[FIX-SL §4.2.3]`.
  Both are genuine scope forks with no safe default given the explicit deferrals; they are deliberately deferred to the **mandatory `/speckit-clarify`** step (canonical pipeline `.specify/pipeline.md` step 2 — mandatory for session/threading/error-semantics features per `[const §XVI.3]`). The generic spec-skill inline-Q&A is intentionally not used here because `/speckit-clarify` is the dedicated, authoritative next step and will encode the answers back into the spec + this checklist.
- All other items pass. On `/speckit-clarify` resolving Q1/Q2, the first checkbox flips to `[x]` and the spec is `/speckit-plan`-ready (Gate A is mandatory for this non-trivial design before `/speckit-tasks`).
