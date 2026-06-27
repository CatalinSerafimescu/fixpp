# Concurrency, GIL & Reentrancy Requirements Checklist: Python bindings ownership / lifetime layer (PY-004)

**Purpose**: Validate that the threading/GIL/reentrancy REQUIREMENTS (GIL-protected state, concurrent-close race, `_in_callback` reentrancy detection, deadlock-avoidance, sub-interpreter rejection) are complete, unambiguous, consistent, and measurable. Tests requirements writing, not code.
**Created**: 2026-06-27
**Feature**: [spec.md](../spec.md) · [data-model.md](../data-model.md) · [contracts/python-lifetime-api.md](../contracts/python-lifetime-api.md)

## Requirement Completeness

- [ ] CHK001 Is it required that ALL lifetime/ownership/marker state is GIL-protected rather than OS-thread-keyed (no `threading.local`, no thread-id assumptions)? [Completeness, Spec §FR-016, data-model E-6/E-7]
- [ ] CHK002 Is reentrancy detection required for ALL THREE blocking APIs — `session.send`, `session.close`, and `engine.close`(→`engine_destroy`)? [Completeness, Spec §FR-017, SC-007]
- [ ] CHK003 Are requirements defined for BOTH setting the `_in_callback` marker (on callback entry, under GIL) AND clearing it (before GIL release on exit)? [Completeness, data-model E-7]
- [ ] CHK004 Is the `Engine.close()` all-sessions `_in_callback` preflight specified as a distinct gate from the per-`Session` `close()` step-0 backstop? [Completeness, contracts C-3 step 1 + Session step 0]
- [ ] CHK005 Are sub-interpreter rejection requirements specified at the construction boundary — raising `SubInterpreterRejected` (1201) BEFORE any native engine is created? [Completeness, Spec §FR-018, contracts C-7]

## Requirement Clarity

- [ ] CHK006 Is "correct regardless of which worker thread runs the callback" defined in terms of GIL serialisation, not thread identity? [Clarity, Spec §FR-016, data-model E-7]
- [ ] CHK007 Is the concurrent-close window characterized precisely — which methods may race (`send` / `open_session` / `start`), what they observe (`_dead`), and what they raise (`ObjectLifetime`)? [Clarity, Spec §SC-001 close-race note]
- [ ] CHK008 Is the precedence between the step-0 reentrancy fail-fast and the NEW-P2a "`_dead`-first" arming in `Session.close()` stated unambiguously (reentrancy check comes first; does not re-order the rest)? [Clarity, contracts C-3]
- [ ] CHK009 Is "BEFORE entering the C-ABI" defined as the exact check point for the reentrancy raise (a pure-Python check on `_in_callback`)? [Clarity, Spec §FR-017, contracts C-6]
- [ ] CHK010 Is the rationale for the Engine.close preflight (prevent a half-closed engine — a sibling torn down before the loop reaches the in-callback session) stated clearly enough to justify the all-sessions walk? [Clarity, contracts C-3 step 1]

## Requirement Consistency

- [ ] CHK011 Does the reentrancy decision (raise `CallbackReentrantClose` on send TOO) appear consistently across Spec §FR-017, contracts C-6, and the Article XX amendment text in research.md? [Consistency, Spec §FR-017 ↔ contracts C-6 ↔ research Proposed amendment]
- [ ] CHK012 Is the marker described consistently as per-`Session`, GIL-protected, set/cleared by the trampoline across data-model E-7, Spec §FR-016/FR-017, and contracts C-3/C-6? [Consistency]
- [ ] CHK013 Do the Engine.close preflight (C-3 step 1) and Session.close step-0 (C-3) compose without double-raise, and is that composition stated in BOTH locations? [Consistency, contracts C-3]
- [ ] CHK014 Is the SC-001 close-race safety characterization stated symmetrically for the Session and Engine wrappers (both arm own `_dead` first before GIL-releasing teardown)? [Consistency, Spec §SC-001, plan Gate A round 3]

## Acceptance Criteria Quality & Measurability

- [ ] CHK015 Is SC-007's "100% raise `CallbackReentrantClose` instead of deadlocking" tied to an objectively observable witness (a watchdog/timeout that would hang on a regression)? [Measurability, Spec §SC-007]
- [ ] CHK016 Does the spec/plan specify the multi-worker condition (`io_threads>1`, rotating pool) under which FR-016 thread-correctness must be witnessed (the raise firing on a different OS thread)? [Measurability, Spec §FR-016, plan test notes]
- [ ] CHK017 Is the sub-interpreter rejection's observable outcome (`SubInterpreterRejected` 1201, no native engine constructed) precise enough to be decided by a single pass/fail? [Measurability, Spec §FR-018, SC-007]

## Scenario & Edge-Case Coverage

- [ ] CHK018 Are requirements defined for a callback that resumes on a DIFFERENT OS thread (state must remain correct because it is GIL-protected, not thread-keyed)? [Coverage, Edge Case, Spec §FR-016, Spec Edge Cases]
- [ ] CHK019 Is the no-UAF sub-guarantee specified for the residual close-race (native 050/052 tombstone fallback) so the concurrent window is NEVER a UAF even if the early wrapper-side `_dead` check is missed? [Coverage, Spec §SC-001, contracts C-8]
- [ ] CHK020 Are requirements defined for the half-closed-engine hazard (a sibling session's native side effects occurring before the in-callback session's fail-fast is reached)? [Coverage, contracts C-3 step 1]
- [ ] CHK021 Does the Session.close step-0 backstop specify that the reentrant call leaves state UNMODIFIED (`_dead` not armed, `_application` not dropped, no native close) when it raises? [Coverage, contracts C-3 Session step 0, plan test notes]

## Ambiguities & Gaps

- [ ] CHK022 Does the spec define whether the `_in_callback` marker is correctly cleared when the callback raises a Python exception (the `PyErr_Print` exit path), so a post-exception blocking call is not falsely flagged? [Gap, Exception Flow, data-model E-7, Application E-4]
- [ ] CHK023 Is the behavior specified when `send`/`close` is called from a NON-callback thread while a callback is in-flight on another thread (the per-session marker is set) — is that intentionally a reentrancy raise, or a distinct case? [Ambiguity, Spec §FR-016/FR-017]
- [ ] CHK024 Does the spec define a deterministic outcome for `engine.close()` racing a child `session.close()` invoked concurrently (ordering / idempotency under the GIL)? [Gap, Spec §FR-007/FR-008, SC-001]

## Notes

- This checklist tests REQUIREMENTS quality, not runtime thread-safety. The runtime witnesses (ASan/TSan/watchdog) belong to /speckit-verify, not here.
- CHK022 is a high-value gap probe: the exception-exit path of the trampoline must still clear `_in_callback`, or a subsequent legitimate blocking call falsely raises 1204. Confirm the requirement exists or flag it.
- Traceability: 24/24 items carry a spec/data-model/contracts/plan reference or a `[Gap]`/`[Ambiguity]` marker.
