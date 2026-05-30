# Lifecycle & Concurrency Requirements Quality Checklist: Public Initiator/Acceptor Runtime Engine & Full T-041 Closure

**Purpose**: Validate that the session-FSM / concurrency-lifecycle requirements (engine-strand sequencing, total-cancellation teardown, join-before-clear ordering, idempotent stop + strict dtor, on-strand in-order exactly-once read-pump, and the attach-vs-reconnect primitive distinction) are complete, clear, consistent, and measurable — BEFORE implementation. "Unit tests for the requirements," not for the code.
**Created**: 2026-05-30
**Feature**: [spec.md](../spec.md)
**Domain**: Session-FSM / concurrency-lifecycle (Gate A trigger). Audience: reviewer / `/speckit-checklist-audit` gate (step 9).

## Requirement Completeness

- [ ] CHK001 Are total-cancellation teardown requirements enumerated for EACH cancellable work item — accept loops, connect loops, read-pumps, in-flight handshakes, AND the engine-level accept-scope cancellation domain — rather than stated only in aggregate? [Completeness, Spec §FR-011/§Edge Cases]
- [ ] CHK002 Is the join-before-clear ordering (all outstanding work joined before the registry that owns the `Session` objects is cleared, so no read-pump dereferences a freed `Session*`) captured as a requirement? [Completeness, Spec §FR-011/§Clarifications]
- [ ] CHK003 Is the `lookup()` pre-open semantics (may return null / a not-yet-open session because `open()` is awaited inside each loop, not in synchronous `start()`) stated as a requirement? [Completeness, Spec §FR-001/§Clarifications]
- [ ] CHK004 Is the `~Engine()` precondition (strict `assert(stopped())`, no synchronous best-effort drain) captured as a requirement? [Completeness, Spec §Clarifications]
- [ ] CHK005 Is the engine-strand sequencing of registry mutations (register / lookup / duplicate-reject / clear) stated, rather than left implied by "registry"? [Gap, Spec §FR-002/§FR-011]

## Requirement Clarity

- [ ] CHK006 Is "cancellation-safe" defined with objective criteria (no leak / no UAF / no hang under the sanitizer matrix), rather than used as a vague adjective? [Clarity, Spec §FR-011]
- [ ] CHK007 Is "clean teardown" / "no leaked work" quantified (named sanitizer matrix + second-stop no-op), not left qualitative? [Clarity, Spec §FR-011/§SC-005]
- [ ] CHK008 Is the read-pump guarantee "in arrival order, on the session strand, exactly once" expressed as objectively-testable acceptance criteria? [Clarity, Spec §FR-004/§SC-003]
- [ ] CHK009 Is the backpressure model (natural — synchronous `co_await on_inbound_frame`, NO inbound queue) stated, so "continuously read" cannot be misread as buffering/queueing? [Ambiguity, Spec §FR-004]
- [ ] CHK010 Is the distinction between the acceptor attach primitive (sets `live_peer_id_` + rebinds outbound, NO FSM transition) and the initiator-only `install_reconnected_transport` (re-enters `LogonSent`) stated as a requirement constraint, not only design narrative? [Clarity, Spec §Clarifications]

## Requirement Consistency

- [ ] CHK011 Is the "no new disposition" requirement (FR-012) consistent with the read-pump EOF/read-error behavior (pump stops; session uses its existing disconnect handling) so no new state is implied? [Consistency, Spec §FR-012/§US2 AC2]
- [ ] CHK012 Do the idempotent-stop requirement (FR-011) and the SC-005 second-stop-no-op outcome reference one mechanism (the `stopped` flag) without divergent wording? [Consistency, Spec §FR-011/§SC-005]
- [ ] CHK013 Is the "no engine-owned worker threads / caller drives the executor" model consistent across FR-001, FR-003, and the Assumptions, with no requirement implying an internal thread? [Consistency, Spec §FR-001/§FR-003/§Assumptions]

## Acceptance Criteria Quality (Measurability)

- [ ] CHK014 Can SC-005's "tears down cleanly … under the full sanitizer matrix" be objectively verified (the ASan/UBSan/TSan matrix and 0-findings condition are named)? [Measurability, Spec §SC-005/§SC-008]
- [ ] CHK015 Can SC-003's "no drops or duplicates" be objectively measured (e.g., N-frame in-order exactly-once witness), not just asserted? [Measurability, Spec §SC-003]
- [ ] CHK016 Can SC-004's duplicate-rejection be objectively verified against a defined key (the FIX SessionID tuple) and a named error (`session_invalid_argument = 119`)? [Measurability, Spec §SC-004/§FR-002]

## Edge Case & Scenario Coverage

- [ ] CHK017 Are requirements defined for `stop()` invoked while a handshake OR a reconnect attempt is in flight (aborted without leak/UAF)? [Edge Case, Spec §Edge Cases]
- [ ] CHK018 Are requirements defined for a read-pump frame exceeding carry capacity (`wire_frame_too_large` → close, no silent truncation)? [Coverage, Spec §FR-004/§US2]
- [ ] CHK019 Are requirements defined for concurrent sessions sharing the engine executor (per-session work stays on its own strand; no cross-session data races)? [Coverage, Spec §Edge Cases]
- [ ] CHK020 Is the `[const §XV.9]` "no `std::mutex` in the engine/coroutine-corpus headers" constraint captured anywhere in the spec's requirements/constraints, or does it live only in plan/tasks? [Gap, Spec]

## Notes

- Check items off as resolved: `[x]`. Disposition during `/speckit-checklist-audit` (step 9) as PASS / SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>.
- These items test whether the LIFECYCLE/CONCURRENCY REQUIREMENTS are well-written — not whether the implementation tears down correctly (that is `/speckit-implement` + `/speckit-verify` under the sanitizer matrix).
