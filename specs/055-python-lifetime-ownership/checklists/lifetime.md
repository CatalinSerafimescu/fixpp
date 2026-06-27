# Lifetime & Memory-Safety Requirements Checklist: Python bindings ownership / lifetime layer (PY-004)

**Purpose**: Validate that the lifetime/ownership/memory-safety REQUIREMENTS (sentinel, ordered close-flow, ownership graph, callback-leak release, flyweight invalidation) are complete, unambiguous, consistent, and measurable — before implementation. Tests the requirements writing, not the code.
**Created**: 2026-06-27
**Feature**: [spec.md](../spec.md) · [data-model.md](../data-model.md) · [contracts/python-lifetime-api.md](../contracts/python-lifetime-api.md)

## Requirement Completeness

- [ ] CHK001 Is the pre-call liveness-check requirement specified for *every* wrapper method that would touch the C-ABI, across all five wrapper types (Engine/Session/Message/Application/Dictionary)? [Completeness, Spec §FR-002, data-model E-6]
- [ ] CHK002 Are the arming sources of the liveness sentinel enumerated for every path that invalidates a handle (parent `close()` walk, inbound-callback return)? [Completeness, Spec §FR-003/FR-004, data-model E-6]
- [ ] CHK003 Are both the strong-reference (child→parent UP) and weak-reference (parent→child DOWN) directions of the ownership graph each explicitly required? [Completeness, Spec §FR-005/FR-006]
- [ ] CHK004 Is the ordered close sequence (invalidate children → release application ref → native close/destroy) fully enumerated as discrete steps for BOTH `Engine.close()` and `Session.close()`? [Completeness, Spec §FR-007, contracts C-3]
- [ ] CHK005 Are requirements defined for releasing the binding-owned callback reference on BOTH session close AND callback re-registration? [Completeness, Spec §FR-011, contracts C-4]
- [ ] CHK006 Is the two-flavour Message distinction (inbound flyweight vs outbound/clone) specified for all three concerns: ownership, finalisation, and invalidation? [Completeness, Spec §FR-012, data-model E-3]
- [ ] CHK007 Is the engine-destroy-last ordering ("engine destroys its native handle only after all its sessions are closed") explicitly required? [Completeness, Spec §FR-007, contracts C-3]

## Requirement Clarity

- [ ] CHK008 Is "marked invalid when the callback returns / before the dispatch window closes" pinned to a specific lifecycle point (armed before GIL release at callback return)? [Clarity, Spec §FR-004, data-model E-3]
- [ ] CHK009 Is "binding-owned reference" defined precisely enough to distinguish the binding's `_application` ref from the user's external references? [Clarity, Spec §FR-011, SC-002, contracts C-4]
- [ ] CHK010 Is the ordering constraint "arm `_dead` FIRST, before any GIL-releasing teardown" stated unambiguously for both the Session and Engine close-flows? [Clarity, contracts C-3, Spec §SC-001]
- [ ] CHK011 Is it unambiguous that the C-ABI registration `userdata` is the INCREF'd `Session` (NOT the callable), and that it is released only at/after the native close? [Clarity, contracts C-4, data-model E-2]
- [ ] CHK012 Is the "private unguarded close/destroy helper" requirement (teardown must not route through `_dead`-guarded public methods) stated clearly enough to implement without re-entering the sentinel? [Clarity, contracts C-3 steps 3-4]

## Requirement Consistency

- [ ] CHK013 Do the close-flow steps in contracts C-3 match the lifecycle prose in data-model E-1/E-2 (reentrancy fail-fast → arm `_dead` first → invalidate children → drop `_application` → native close → release userdata)? [Consistency, contracts C-3 ↔ data-model E-1/E-2]
- [ ] CHK014 Is the "strong-ref UP / weak-ref DOWN" graph described consistently across Spec §FR-005/FR-006, data-model E-1..E-3, and contracts C-3? [Consistency]
- [ ] CHK015 Does the SC-001 concurrent-close characterization (both wrappers arm `_dead` first before native teardown) align with the FR-007 ordered-close requirement and the C-3 step ordering? [Consistency, Spec §SC-001 ↔ FR-007 ↔ contracts C-3]
- [ ] CHK016 Is the callback-ownership model (`userdata`=Session, `_application`=callable, release-on-close/re-register) described without contradiction between Spec §FR-011, data-model E-2, and contracts C-4? [Consistency]

## Acceptance Criteria Quality & Measurability

- [ ] CHK017 Is SC-001's "100% of post-close accesses raise `ObjectLifetime`" scoped with an explicit, testable boundary (the non-concurrent-close case) so the criterion is objectively pass/fail? [Measurability, Spec §SC-001]
- [ ] CHK018 Does SC-002 specify the exact observable witness (callback `weakref` dead after `gc.collect()`, with the caller's external strong refs dropped FIRST) that makes "no binding-owned ref leaks" measurable and discriminating? [Measurability, Spec §SC-002, contracts C-4]
- [ ] CHK019 Are the acceptance seams (#3 session-outlives-engine, #8 message-outlives-session) each defined precisely enough to map to a single pass/fail observation under ASan? [Acceptance Criteria, Spec §US1 Independent Test]

## Scenario & Edge-Case Coverage

- [ ] CHK020 Are idempotency requirements for a second `close()` (including a `with`-exit after an explicit close) specified as a no-op — explicitly not an error or double-free? [Coverage, Edge Case, Spec §FR-008]
- [ ] CHK021 Are requirements defined for the Application↔Session reference cycle being collectable (broken on explicit close; otherwise left to the cycle collector)? [Coverage, Edge Case, Spec Edge Cases]
- [ ] CHK022 Is the GC-without-explicit-close behavior specified (DeprecationWarning + best-effort native cleanup, with finalisation order unspecified)? [Coverage, Spec §FR-010]
- [ ] CHK023 Are requirements defined so an outbound-vs-inbound Message mix-up neither double-frees nor leaks (inbound `__del__` no-op; outbound frees its handle, idempotently)? [Coverage, Edge Case, Spec §FR-012, data-model E-3]
- [ ] CHK024 Is the inbound `set_*` rejection path (`CapiError` code=4 via `_is_inbound`, a distinct path from `_dead`) specified? [Coverage, data-model E-3, contracts C-1]

## Ambiguities & Gaps

- [ ] CHK025 Does the spec define that a weakly-tracked child already GC'd is safely skipped during the parent's invalidation walk (WeakSet semantics relied upon)? [Gap, Spec §FR-006]
- [ ] CHK026 Is the outbound-Message construction Python API (callable signature, e.g. `session.create_message(...)`) specified in the contract, or only the native mechanism (`fixpp_msg_create_outbound`)? [Gap, contracts C-1, research D-11]
- [ ] CHK027 Does the spec specify which Message accessors are in scope for the liveness guard vs deferred (callback-surface completeness), so "every accessor checks `_dead`" is bounded? [Ambiguity, contracts C-1, research D-1/D-8]

## Notes

- This checklist tests REQUIREMENTS quality (are they complete/clear/consistent/measurable), not implementation behavior.
- CHK026 traces to the `/speckit-analyze` C1 finding (outbound-ctor signature absent from contract C-1; T008 now references the mechanism but the contract surface should name the Python signature).
- Traceability: 27/27 items carry a spec/data-model/contracts reference or a `[Gap]`/`[Ambiguity]` marker.
