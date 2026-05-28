# Specification Quality Checklist: 013 — Session Reconnect FSM + Recovery + CompID↔TLS-Identity Binding

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-05-28
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — *spec describes WHAT (recovery semantics, binding policy, event surface) not HOW; type names like `Session`, `Transport`, `cert_source`, `SessionEvent`, `CompIdAuthorizationPolicy` are contract surfaces published by precondition features 005 / 010 / 011 / 012, not implementation choices added by this feature*
- [x] Focused on user value and business needs — *every user story leads with operator-facing value (24/7 deployment viability, multi-tenant security, operational observability, zero-downtime credential rotation)*
- [x] Written for non-technical stakeholders — *user stories use plain-language descriptions; FIX protocol references cited for traceability not as primary content*
- [x] All mandatory sections completed — *User Scenarios & Testing, Requirements, Success Criteria all present and populated*

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain — **RESOLVED 2026-05-28 via `/speckit-clarify` session (5 Qs answered, reference-engine sweep performed BEFORE each per `[[feedback_always_invoke_speckit_clarify]]`)**: Q1 FR-017 ResetSeqNumFlag → operator-config 3-mode policy; Q2 FR-022 principal-extraction → canonical-fixed CN→SAN-DNS→SAN-URI→fingerprint; Q3 FR-023 policy mode → allow-list-only default-deny; Q4 FR-033 ReloadCertSource race → defer-until-handshake-completes; Q5 FR-008 logout timeout → 2000 ms QFJ-aligned default. All `[NEEDS CLARIFICATION]` markers removed inline; defaulted Assumptions §A.6 / §A.7 promoted to explicit decisions
- [x] Requirements are testable and unambiguous — *every FR cites a specific binding contract (FIX-SL §X / FIXS §X / catalogue row T-X / 005 FR-X); the 3 `[NEEDS CLARIFICATION]` markers are themselves the explicit "untestable until resolved" flags*
- [x] Success criteria are measurable — *SC-001 through SC-008 each pin a specific quantity (1-month uninterrupted operation, 44/44 scenarios, 100 sessions, 1.05× HB cadence, 2× wall-clock recovery throughput, 0 Logouts on rotation, 100 % event coverage, catalogue-row flips)*
- [x] Success criteria are technology-agnostic — *no API names, framework names, language constructs in SC-001..SC-008; user-facing or interop-facing metrics only*
- [x] All acceptance scenarios are defined — *4 user stories × 3-7 acceptance scenarios each (US1: 7, US2: 6, US3: 3, US4: 3); Given/When/Then form throughout*
- [x] Edge cases are identified — *12 edge cases enumerated (admin replay, store horizon, EndSeqNo=0, too-low, too-high, repeating-group replay, TestRequest mid-recovery, concurrent rotation, empty policy, Listener.cancel during recovery, Logout during recovery, cipher-policy mid-reconnect)*
- [x] Scope is clearly bounded — *4 cross-feature obligations explicitly named in Input; out-of-scope items (C-ABI surface — §A.9, 2i bridges — §A.9, interop harness — Outgoing hook) explicitly marked deferred*
- [x] Dependencies and assumptions identified — *§A.1..A.9 + Dependencies-and-Outgoing-Hooks section list all preconditions (005/009/010/011/012) and all unblocked-by-merge items (interop harness, catalogue-row flips, 005 FR-008 amendment)*

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria — *FR-001..FR-038 each tie to ≥1 acceptance scenario, success criterion, or edge case*
- [x] User scenarios cover primary flows — *US1 covers the v1.0-GA precondition flow; US2 covers the security-binding flow; US3 covers the observability flow; US4 covers the operational-rotation flow*
- [x] Feature meets measurable outcomes defined in Success Criteria — *SC-001 (uninterrupted operation) maps to US1; SC-002 (44 scenarios) maps to US1 + FR-016; SC-003 (multi-tenant binding) maps to US2; SC-005 (recovery throughput) maps to US1; SC-006 (rotation no-Logout) maps to US4; SC-007 (event coverage) maps to US3; SC-008 (catalogue flip) maps to FR-016 + FR-025 + FR-029 + FR-034*
- [x] No implementation details leak into specification — *no class member layout, no internal lock ordering, no per-state-transition pseudocode; reference engines mentioned for grounding-by-precedent only, not as implementation prescription*

## Notes

- **2026-05-28 `/speckit-clarify` session complete**: 5 of 5 questions asked + answered (max quota); reference-engine sweep across QuickFIX-cpp v1.16.0 / QuickFIX/J v3.0.1 / Fix8 v1.4.3 performed BEFORE each question per `[[feedback_always_invoke_speckit_clarify]]`. All 3 inline `[NEEDS CLARIFICATION]` markers resolved (FR-017 / FR-022 / FR-023); Assumptions §A.6 / §A.7 promoted from defaulted to explicit clarify-confirmed decisions. New error variant `error::session_seqnum_reset_mismatch` joins `error::session_compid_unauthorized` as the two new error slots this feature introduces (slot allocation deferred to `/speckit-tasks` per §A.8).
- The "next free error slot" placeholder in FR-021 / Assumptions §A.8 (`error::session_compid_unauthorized` + `error::session_seqnum_reset_mismatch`) is intentionally left for `/speckit-tasks` to bind — slot numbering follows the ABI-hygiene rule from `[const §X.2]` and depends on the current `error::session_*` registry, which the tasks phase audits.
- Anchor-citation discipline (FR-036) is the spec's own self-binding rule — every FR cites its source. The `/gate-a` reviewer should sample a representative slice and verify each cited anchor exists in the signed-off design doc or precondition spec.
