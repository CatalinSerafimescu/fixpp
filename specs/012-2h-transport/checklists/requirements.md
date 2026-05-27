# Specification Quality Checklist: 012 — 2h Transport (TCP / TLS / Listener / Mock)

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-05-27
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs)
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders
- [x] All mandatory sections completed

**Notes on content quality**:
- This spec inherits a signed-off Phase-2 design doc (`.specify/2h-transport.md` v0.3) that has already settled the C++23 / ASIO / OpenSSL stack. Per the project's Spec-Kit precedent (see `specs/011-tls-policy/spec.md`), the spec NAMES the C++ types and the OpenSSL primitives because they are the user-visible *plugin surface* — operators implementing a custom `Transport` or `cert_source` see the exact type names; that is the contract this spec exists to lock. Implementation-detail "leak" is in scope by the same precedent that 011 / 008 / 007 / 006 already set in this codebase.
- Audience is the FIX engine operator + the v1.0 release-gate reviewer, not a non-technical product stakeholder; the user-story Acceptance Scenarios are written so an operator can read them without reading the design doc.

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic (no implementation details)
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded
- [x] Dependencies and assumptions identified

**Notes on requirement completeness**:
- Zero `[NEEDS CLARIFICATION]` markers needed: every `[2h §10]` open question is DEFERRED (PSK hook → post-v1; control-plane transport → 2j; per-venue reconnect presets → DECIDED v0.2; TCP socket-option docs → post-v1 docs; TLS bidi shutdown timeout → post-v1 ops; IPv6 zone-id corpus → Phase-4 conformance; acceptor accept rate → post-v1). The Assumptions section enumerates each deferral so a `/plan` reviewer can re-open if needed.
- Success criteria 001 / 004 / 005 / 006 / 007 are user-facing measurable outcomes (zero code changes / hermetic test seam / deployment scale / distinct error variants / no rollback under cancellation). SC-002 / 003 / 008 reference concrete numeric / latency budgets sourced from the design doc.
- SC-007 / SC-008 reference design-doc-internal anchors (`[2g §6.4]:927-928` for the F-1 carryover witness cached-state fast path; `[2h §6.3]` Tier 1 ceilings for latency). These are intentional — the contract this spec publishes is the contract those anchors define; verifiability requires the anchor.

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification

**Notes on feature readiness**:
- US1 (P1) is the spine — without it, none of the upstream work (011 / 2b / 2e / session FSM) lights up. US2 (P2) and US3 / US4 (P3) are independently testable per the user-story template's MVP rule: US1 alone yields a viable initiator-only TLS session; US2 adds bounded reconnect for 24/7 viability; US3 unlocks the acceptor deployment shape; US4 unlocks hermetic FSM tests.
- 011 Gate B F-1 carryover is captured in FR-033 + SC-008 + Assumptions section as a BINDING Gate B obligation. The memory anchor `[[project_011_tls_policy_closed.md]]` is preserved in the spec text so any future reader recovers the cross-feature obligation without needing the auto-memory layer.
- Cross-doc Appendix-D amendments (`[2d §4.4]` factory shape flip, `[2d §4.5]` `transport_factory_override` field, `[2g App D §D.3]` coverage-index rows) are declared in the Assumptions section. These are orchestrator obligations at sign-off, not feature-internal tasks; `/plan` should not schedule them as tasks.

## Notes

- Items marked incomplete require spec updates before `/speckit-clarify` or `/speckit-plan`.
- **Per `.specify/pipeline.md` integer steps**: this spec was authored at step 3 (`/speckit-specify`). Per `[[feedback_speckit_pipeline_order_gate_a_before_tasks]]`, the canonical order from here is: step 4 (`/speckit-clarify`) → step 5 (`/speckit-plan`) → step 5b (**Phase-4 Gate A** per `[const §XVII.1]`, reviews the `specs/012-2h-transport/` bundle before `/speckit-tasks`) → step 6 (`/speckit-tasks`) → step 7 (`/speckit-checklist`) → step 8 (`/speckit-analyze`) → step 9 (**MANDATORY** `/speckit-checklist-audit`) → step 10 (`/speckit-implement`) → step 11+ (`/simplify` → close-analysis → `/speckit-verify` → Gate B → MARK DONE).
- **`/speckit-clarify` (step 4) MUST be invoked for every feature** even when the spec appears complete from the design-doc inheritance alone. Rationale: 012's clarify run (2026-05-27) surfaced a Q1 reference-engine-divergence (reconnect lifetime — design doc said "same Transport instance"; QuickFIX-cpp + QuickFIX/J + Fix8 all use fresh-per-attempt) that the completeness signal would have shipped silently and Gate B would have caught downstream at heavier cost. Recorded as `[[feedback_always_invoke_speckit_clarify]]`.
- **011 Gate B F-1 carryover** is the load-bearing cross-feature obligation; both `/plan` (must schedule the witness as a tasks.md row) and Gate B (must verify SC-008 GREEN) consume FR-033 + SC-008 directly. Track it in the spec's lifecycle through completion.
