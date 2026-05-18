# Pre-Implement Requirements-Quality Checklist: Session Establishment & FSM Core

**Purpose**: Pipeline step-7 requirements-quality gate (reviewer-facing) — "unit tests for the English" of `spec.md`/`plan.md`/`research.md`/`data-model.md` before `/speckit-implement`. Tests whether the requirements are complete, clear, consistent, and measurable — NOT whether the implementation works.
**Created**: 2026-05-18
**Feature**: [`spec.md`](../spec.md) · [`plan.md`](../plan.md) · `005-session-establishment-fsm` (Gate A converged round 3; `/tasks` + `/analyze` done)
**Focus areas**: FSM completeness & determinism · conformance scope & deferral traceability · concurrency & cancellation clarity · NFR measurability · standard pre-implement depth

## Requirement Completeness

- [ ] CHK001 Are requirements defined for BOTH initiator and acceptor roles across every admin flow (Logon/Logout/Heartbeat/TestRequest/Reject), not only stated generally? [Completeness, Spec §FR-002 / §Assumptions]
- [ ] CHK002 Is the full FR-001 event alphabet enumerated so every inbound admin message AND timer event has a named, defined transition (no implicit/undefined, no silent no-op)? [Completeness, Spec §FR-001, Data-model E2]
- [ ] CHK003 Are `seqnum_t` overflow requirements stated concretely (value, no-wrap, operator-intervention, surfaced `store_seqnum_overflow`) rather than left as the bare word "session-fatal"? [Completeness, Spec §FR-008/§FR-009, SC-003]
- [ ] CHK004 Is receipt of out-of-scope admin types (ResendRequest/SequenceReset) specified as a bounded, defined transition (`session_admin_not_supported`) rather than "deferred" silence? [Completeness, Spec §FR-017 / §Edge Cases]
- [ ] CHK005 Are the threshold default VALUES 005 owns (heartbeat interval, test-request grace, MaxLatency, reject policy) documented as requirements, not deferred to implementation? [Completeness, Research D-8, Spec §Assumptions]
- [ ] CHK006 Are time-helper #4 grammar requirements stated for every supported precision (s/ms/µs) including the "never coarser than seconds" and "lossless round-trip at emitted precision" bounds? [Completeness, Spec §FR-012, Research D-3]

## Requirement Clarity & Measurability

- [ ] CHK007 Is "session-fatal" defined everywhere with a concrete observable disposition (surface error → orderly Logout-with-text → disconnect), not as a bare adjective? [Clarity, Spec §FR-008, Data-model E3/I-4]
- [ ] CHK008 Is the too-high-gap behavior unambiguously specified to emit NO ResendRequest/SequenceReset, and clearly distinguished from the deferred recovery feature's behavior? [Clarity, Spec §FR-001/§FR-008, Clarification Session-2026-05-18]
- [ ] CHK009 Is the graceful-close timeout quantified (bound source = clock seam; owned value) with a measurable expiry disposition? [Measurability, Spec §FR-005, Research D-8]
- [ ] CHK010 Is the Q3 stale-`SendingTime` disposition specified distinctly for the established-session path vs the Logon path (Reject+Logout vs logout-with-error)? [Clarity, Spec §FR-013, Clarification Q3]
- [ ] CHK011 Can "zero heap allocation on the steady-state inbound-process and timer-fire paths" be objectively verified as written (paths named, allocation harness named)? [Measurability, Spec §SC-009, Plan Constraints]
- [ ] CHK012 Are the performance ceilings expressed with workload + unit + environment so they are objectively measurable rather than "fast"/"not the hot path"? [Measurability, Plan Technical-Context table]

## Acceptance Criteria Quality

- [ ] CHK013 Does each SC-001..SC-010 state a measurable outcome (count, %, deterministic disposition) rather than a behavioral assertion? [Acceptance Criteria, Spec §SC-001..010]
- [ ] CHK014 Is SC-001's version scope (FIX.4.2/4.4 only; FIXT.1.1/5.0SP2 explicitly NOT claimed; 4.0/4.1/4.3/5.0 deferred) stated as a bounded, testable claim with the grounding oracle named? [Measurability, Spec §SC-001, Research D-10]
- [ ] CHK015 Is each US1–US5 "Independent Test" statement sufficient to verify that story in isolation (transport/store doubles + mock clock named)? [Acceptance Criteria, Spec §User Stories]
- [ ] CHK016 Is SC-010 (the `[2e §10 Q9]` `seqnum_t` handoff + `core/` exit close in the same PR) phrased as an objectively checkable build/consumer condition? [Measurability, Spec §SC-010]

## Requirement Consistency (cross-document)

- [ ] CHK017 Are spec FR/SC, plan Project-Structure files, and the data-model transition matrix mutually consistent (every AC test seam has a named file; every FR maps to a matrix cell)? [Consistency, Plan §Test-seam mapping]
- [ ] CHK018 Is the 6-state FSM (no `RecoveryPending`) stated identically across spec §FR-001, plan, research D-2, data-model E2, and `contracts/session_fsm.hpp`? [Consistency, Spec §FR-001 / Data-model E2 / Research D-2]
- [ ] CHK019 Is the `[const §VII.5]` representation identical everywhere (NOT SATISFIED → Article XVII §1 recorded Gate-A-blocker waiver; article itself not waived) with no residual "WAIVER of §VII.5"/"§V.5 allowance" shorthand? [Consistency, Spec §FR-018/§SC-008, Plan Constitution Check/Complexity Tracking]
- [ ] CHK020 Do scenario-14 references agree across spec/plan/research D-10 and `feature-catalogue.md` (corpus `14a–14j`; `14a–14g` in scope; `14h/14i/14j` deferred)? [Consistency, Research D-10, Feature-catalogue TC-010]
- [ ] CHK021 Is the canonical pipeline order (`/plan`→Gate A→`/tasks`→`/analyze`→`/implement`) stated consistently in plan (Constitution-Check `[const §XVI.4]` row + Phase-2 checklist) without the prior `/analyze`-before-`/tasks` inversion? [Consistency, Plan §[const §XVI.4]]
- [ ] CHK022 Are S-016 ownership boundaries consistent (49/56 point-to-point in scope; 115/128 third-party addressing deferred) across FR-004/FR-017/SC-001? [Consistency, Spec §FR-004/§FR-017]

## FSM Determinism & Completeness

- [ ] CHK023 Is guard precedence (parse/type → CompID/BeginString → SendingTime/MaxLatency → seqnum class → type-for-state) specified as an ordered, total rule applicable to every state? [Clarity, Data-model E2 preamble, Spec §FR-001]
- [ ] CHK024 Are simultaneous-logon and duplicate-Logon-while-Active resolutions specified to a single deterministic outcome (no deadlock, no double-confirm)? [Coverage, Spec §Edge Cases, Data-model E2]
- [ ] CHK025 Is every non-Active-state Logout receipt mapped to a defined `[FIX-SL §4.6]` transition, not just the Active path? [Coverage, Spec §FR-005/§Edge Cases, Data-model matrix]
- [ ] CHK026 Are the `LogoutSent` "drained" cells defined as a requirement (what is dropped vs what still transitions) rather than left implied? [Completeness, Data-model E2 matrix]

## Conformance Scope & Deferral Traceability

- [ ] CHK027 Is the in-scope vs deferred `[FIX-TC]` split enumerated to specific `.def` oracle cases (not just capability labels) so it is traceable and testable? [Traceability, Research D-10, Spec §FR-018]
- [ ] CHK028 Does every deferred conformance / version / S-016 item carry an explicit forward-reference to the named successor feature (deferred-with-traceability, not silent omission per `[const §I.4]`)? [Traceability, Spec §FR-017/§SC-008, Research D-10]
- [ ] CHK029 Is the `coverage-index.md` deferral ledger asserted to EXIST (recorded now), making the bundle's "recorded in coverage-index" claims true rather than promissory? [Completeness, Plan Constitution-Check §VI.4/§VII.5]
- [ ] CHK030 Is the waiver scope precisely bounded — only the deferred TC cases are NOT SATISFIED, the in-scope subset ships green — so it cannot be read as waiving Article VII §5 wholesale? [Clarity, Spec §SC-008, Plan Complexity Tracking]

## Concurrency & Cancellation Specification

- [ ] CHK031 Are durable-before-transmit ordering AND inbound parse→store→`fromAdmin`/`fromApp` ordering specified precisely enough to test the cancelled-transmit no-inconsistency invariant? [Measurability, Spec §FR-010, Data-model I-3]
- [ ] CHK032 Is the two-phase `close(graceful|terminal)` contract (child cancellation state, phase-2-only-after-phase-1, idempotency, `terminal` skips phase 1) specified without ordering/repeat-call ambiguity? [Clarity, Plan Constraints, Data-model I-9]
- [ ] CHK033 Is "no parallel stop-token; ASIO native cancellation slots end-to-end" stated as a binding constraint and consistent with FR-014? [Consistency, Spec §FR-014, Plan Constraints]
- [ ] CHK034 Is the effective-clock rule (`clock_override ?: EngineConfig::clock`, resolved once at open, never a direct wall-clock) stated as a testable requirement covering ALL time consumers (SendingTime, heartbeat, test-request, graceful-close)? [Completeness, Spec §FR-011, Research D-5]

## Non-Functional Requirement Measurability

- [ ] CHK035 Is the coverage floor (≥95% line / ≥85% branch) tied to a named measurement basis (lcov DA/BRDA) and named touched files so it is objectively gate-able? [Measurability, Plan §[const §IX.1], Quickstart §6]
- [ ] CHK036 Are fuzz and abidiff explicitly recorded as N/A-with-reason (not silently absent) so their non-applicability is auditable at `/speckit-verify`? [Completeness, Research D-12, Plan §[const §VII.7]/§IX.5]
- [ ] CHK037 Is the `noexcept`-window + throwing-user-callback-trap requirement specified with a defined boundary (which calls are in the window, what "trap" means)? [Clarity, Spec §FR-015, Plan Constraints]

## Dependencies, Assumptions & Inheritance

- [ ] CHK038 Are the inherited anchor decisions (2e v0.4 / 2d v0.4 / 2f v1.5 seams) explicitly scoped as consumed-not-reopened, with the "anchor wins on conflict; an inconsistency is a defect in this spec" rule stated? [Assumption, Spec §Authority anchors]
- [ ] CHK039 Is the assumption "a test-double store satisfies the seam; no persistent impl required" validated against the FR-010/FR-017 scope boundary (S-012/S-013 out of scope)? [Assumption, Spec §Assumptions, Research D-4]
- [ ] CHK040 Are the error-slot allocations (43..N) documented as a PLANNED, not-yet-frozen allocation pinned at Gate A / `/tasks`, with the non-renumbering-once-published rule (`[const §X.4]`) stated? [Clarity, Data-model "Error mapping", Plan Scale/Scope]

## Notes

- Check items off as the requirement-quality concern is confirmed satisfied in the spec/plan/research/data-model: `[x]`. An unchecked item = the requirement text needs hardening before `/speckit-implement`, not that code is wrong.
- Many items (CHK008/018/019/020/021/030) re-assert decisions Gate A and the `/analyze` remediation already landed — they are regression guards: confirm the wording still holds, do not re-litigate the decision.
- This checklist tests requirements quality only; behavioral verification is `/speckit-verify` + Gate B's job.
