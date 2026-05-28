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

## Post-/speckit-analyze Amendment Quality (Pass 2, 2026-05-28)

These items test the META-quality of the amendments landed by today's `/speckit-analyze` second pass (F1/D1 slot migration + B1 + C1 + C2 + E1) — cross-file consistency, traceability, and amendment-note discipline. Distinct from the domain checklists (recovery / binding / events), these are CROSS-CUTTING items the next `/speckit-checklist-audit` pass disposititions.

- [ ] CHK001 Did the F1/D1 slot migration (slots 73/74 reused; 118/119 dropped; 120/121 renumbered to 118/119) update ALL referencing artifacts consistently (spec.md, plan.md, tasks.md, data-model.md, contracts/, checklists/, library/CLAUDE.md) — verified by zero residual hits for the dropped error symbols (`session_logout_disconnect_timeout` non-`_ms`, `session_heartbeat_timeout`) and zero residual hits for the stale boundary (`116..121`)? [Consistency, Spec §A.8 + Plan §Summary]
- [ ] CHK002 Is the F1/D1 reference-engine sweep result (zero industry precedent for typed code-level discrimination of inbound-silence / logout-await / logon-await timeouts) cited in §A.8 with EVIDENCE — specific QuickFIX-cpp / QuickFIX-J / Fix8 source files + line numbers, not just engine names? [Traceability, Spec §A.8]
- [ ] CHK003 Are the prior `/speckit-analyze` Pass 1 anchor notes (B2 at spec.md:58, F2 at spec.md:158, C3 at plan.md §Summary item 8) NOT contradicted by the Pass 2 anchor notes added today (B1 / F1/D1 / C1 / C2 / E1)? [Consistency, Spec multiple]
- [ ] CHK004 Are all Pass 2 anchor notes uniformly attributed (`(F1/D1 anchor-added 2026-05-28 per /speckit-analyze ...)` / `(B1 anchor-added ...)` / `(C1 anchor-added ...)` / `(C2 anchor-added ...)` / `(E1 anchor-added ...)`) with consistent format, so a future Gate B reviewer can sweep them by grep? [Traceability, Multiple]
- [ ] CHK005 Does the SessionConfig field `logout_disconnect_timeout_ms` (preserved with `_ms` suffix; 23 occurrences) remain visually distinct from the renamed error code (`session_logout_timeout`, no `_ms` suffix; 25 occurrences) — and does the spec document the intentional naming-distinction so future implementers don't mis-rename one for the other? [Clarity, Spec §FR-008 + Spec §A.8]

## Notes

- **2026-05-28 `/speckit-clarify` session complete**: 5 of 5 questions asked + answered (max quota); reference-engine sweep across QuickFIX-cpp v1.16.0 / QuickFIX/J v3.0.1 / Fix8 v1.4.3 performed BEFORE each question per `[[feedback_always_invoke_speckit_clarify]]`. All 3 inline `[NEEDS CLARIFICATION]` markers resolved (FR-017 / FR-022 / FR-023); Assumptions §A.6 / §A.7 promoted from defaulted to explicit clarify-confirmed decisions. New error variant `error::session_seqnum_reset_mismatch` joins `error::session_compid_unauthorized` as the two new error slots this feature introduces (slot allocation deferred to `/speckit-tasks` per §A.8).
- The "next free error slot" placeholder in FR-021 / Assumptions §A.8 (`error::session_compid_unauthorized` + `error::session_seqnum_reset_mismatch`) is intentionally left for `/speckit-tasks` to bind — slot numbering follows the ABI-hygiene rule from `[const §X.2]` and depends on the current `error::session_*` registry, which the tasks phase audits.
- Anchor-citation discipline (FR-036) is the spec's own self-binding rule — every FR cites its source. The `/gate-a` reviewer should sample a representative slice and verify each cited anchor exists in the signed-off design doc or precondition spec.
- **2026-05-28 `/speckit-analyze` Pass 2 amendments**: F1/D1 (slot 73/74 reuse + 4-slot block 116..119), B1 (FR-001 pre-Logon HeartBtInt source), C1 (FR-019 one_way_ca no-client-cert edge case), C2 (T006 slot 73/74 dual-emission comment update), E1 (T040 ring drop-oldest test cell). 4 file-edits this pass + 12 files modified by the F1/D1 sweep. CHK001..CHK005 above test the meta-quality of these amendments; domain CHKs are in `recovery.md` (CHK043..CHK051), `binding.md` (CHK039..CHK043), `events.md` (CHK042..CHK046).
