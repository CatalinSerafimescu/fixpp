# Specification Quality Checklist: `MessageStore` Async API + Default Impls

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-05-20
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — *the spec realizes signed-off design-doc 2e v0.4; per the `007-threading-clock` precedent ("Authority anchor: design doc wins"), naming concrete C++ types, headers, syscalls (`fdatasync` / `MoveFileExW` / `flock`), and `[FIX-SL]` / `[const]` / `[arch]` / `[2d]` / `[2f]` / `[2b]` anchor citations is mandatory exact-coverage per `[const §VI.5]` — not a leak of implementation choice.*
- [x] Focused on user value and business needs — *the four user stories are framed around the four consumer classes (application developer, deferred 005 FSM, C-ABI / SWIG / Python, QuickFIX migrator) and the "load-bearing invariant" (crash-survival + byte-equality) is named in P1.*
- [x] Written for non-technical stakeholders — *bounded; like 007's spec, this is a constitutionally-grounded engineering spec for the FIX engine. The Authority anchor and User Scenarios are stakeholder-readable; the FR-* requirements section is necessarily technical because the design-doc citations are normative.*
- [x] All mandatory sections completed — *Authority anchor, Normative References, Clarifications (Session 2026-05-20), User Scenarios (4 stories + Edge Cases), Functional Requirements (39), Key Entities, Success Criteria (12), Assumptions, Dependencies.*

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain — *the design doc is signed-off Gate-A-converged v0.4; the three Session 2026-05-20 entries cover codebase-reality scoping only, in the 007 precedent style.*
- [x] Requirements are testable and unambiguous — *each FR cites the design-doc section / root-cause / Codex-or-Opus finding it traces to and is paired with a §9 seam under FR-033 + a measurable SC-* outcome.*
- [x] Success criteria are measurable — *SC-001..SC-012 specify N, byte-equality, % loss, ns/µs/ms ceilings, % regression thresholds, named tool outputs (`tools/check_alloc.py`, `mallocnesia`), 0-race / 0-UAF assertions.*
- [x] Success criteria are technology-agnostic (no implementation details) — *bounded by the same constraint as Content Quality #1: SC-* names latency budgets and platform durability primitives (the named promise of the feature), not framework choices.*
- [x] All acceptance scenarios are defined — *4 user stories × 3–5 acceptance scenarios each = 17 scenarios; Edge Cases section enumerates 11 boundary / error / failure-mode cases.*
- [x] Edge cases are identified — *Sequence overflow, `begin=0` rejection, `end<begin` rejection, never-persisted gap, visitor abort, mid-traversal mutation, cross-filesystem `.reset.tmp`, sentinel mismatch, directory contention, stale `.reset.tmp`, PMR poison.*
- [x] Scope is clearly bounded — *the Authority-anchor block states "store-side contract only" for S-014 (the FSM is `005`'s); §1.1 boundaries from the design doc are mirrored in the FR groupings; FR-030 / FR-039 close `[arch §11]` Q3; Assumptions enumerate what is in v1.0 vs post-v1.0 (COM-009).*
- [x] Dependencies and assumptions identified — *Dependencies section enumerates upstream merged features (001/002/003/004/006/007) with the specific surfaces each contributes, plus downstream consumers (005 deferred, 2g–2m). Assumptions section names the 2e v0.4 authority, the pre-applied Appendix D amendments, the deferred-005 scripted test-double pattern, the COM-009 forward-compat scope.*

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria — *each FR maps to a §9 seam (FR-033) and a SC-* outcome; the Independent Test paragraph on each user story names the specific seams that validate the story's FRs.*
- [x] User scenarios cover primary flows — *P1: crash-survival + capacity (the two load-bearing invariants); P2: concurrent-writer + cancellation + reset atomicity (the safety story); P3: QuickFIX migration (the OSS-002 discharge).*
- [x] Feature meets measurable outcomes defined in Success Criteria — *SC-001 (round-trip) ↔ Story 1; SC-002/003 (crash + reset) ↔ Story 1 + 3; SC-004 (bounded) ↔ Story 2; SC-005 (FIFO-fair) ↔ Story 3; SC-006 (cancellation) ↔ Story 3; SC-007/008 (alloc + latency) ↔ all stories; SC-009 (migration) ↔ Story 4; SC-010 (conformance) ↔ Story 1; SC-011 (seams) ↔ FR-033/034; SC-012 (catalogue) ↔ FR-037/038/039.*
- [x] No implementation details leak into specification — *bounded by Content Quality #1 + #4: technical specificity here is the constitutional exact-coverage citation discipline, not framework choice.*

## Notes

- Validation passed in **one iteration** — the design doc is Gate-A-converged through v0.4 (rounds 1–3 + post-cap line-edit pass), so the spec is a faithful realization with no design-level decisions outstanding.
- The Session 2026-05-20 clarifications resolve codebase-reality scoping only (not design-doc decisions), following the `007-threading-clock` Session 2026-05-19 precedent. **Five Q/A pairs** total: (1) Phase-4 FSM consumer via scripted test-double; (2) `seqnum_t` placeholder header authored fresh; (3) `[2d]` cross-doc amendments pre-applied, `[arch]` + `coverage-index` amendments owed by this feature; (4) Hybrid catalogue update ownership (structural edits in this feature's merge; Status flip orchestrator-applied at Gate-B per `pipeline.md` step 19 / 007 / `[2c App D]` precedent); (5) `EngineConfig::max_store_memory_per_session` added by this feature (1 GiB default, engine-wide, no session override).
- `/speckit-clarify` 2026-05-20 added Q3–Q5 (the codebase-state-discovery items). The spec's FR-014/FR-014a, FR-037/FR-038/FR-039, SC-012, and the Assumptions block were updated atomically with each accepted answer.
- The spec deliberately mirrors `007-threading-clock`'s "Authority anchor: design doc wins" framing because that pattern carried `007` through Gate-A, Gate-B, and merge with no design-doc-vs-spec drift.
