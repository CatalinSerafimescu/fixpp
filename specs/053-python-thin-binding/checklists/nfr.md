# Non-Functional Checklist: Thin End-to-End Python Binding (PY-001)

**Purpose**: Requirements-quality gate for the GIL trampoline / worker-thread safety, callable & message lifetime, and the sanitizer CI legs. Audience: Gate B reviewer.
**Created**: 2026-06-26
**Feature**: [spec.md](../spec.md) · [research.md](../research.md) (D-2, D-9) · [data-model.md](../data-model.md) (E-3/E-4)

## Threading / GIL Requirement Completeness

- [x] CHK031 Is the GIL-reacquisition requirement specified for the inbound trampoline that runs on an internal engine worker thread? [Completeness, Spec §FR-007] — PASS: FR-007 states "the binding MUST correctly reacquire the Python GIL for that trampoline so executing Python code in the callback does not corrupt the interpreter"; `swig-typemap-contract.md T-4` specifies `PyGILState_Ensure()`/`PyGILState_Release()` in the trampoline body; `data-model.md E-4` records the GIL requirement.
- [x] CHK032 Is the scope of the GIL guarantee bounded explicitly (only this one trampoline; comprehensive GIL discipline = PY-002), so the reviewer knows what is NOT promised? [Clarity, Spec §FR-007] — PASS: FR-007 states "(Only this one trampoline; comprehensive GIL discipline — release around blocking calls, all other trampolines, witnessed failure modes — is deferred to PY-002.)" — the scope boundary and what-is-not-promised are explicit.
- [x] CHK033 Is the no-blocking-call-from-callback rule (no `session_send`/`session_close` inside the trampoline) specified with its deadlock rationale? [Completeness, Spec §FR-013a] — PASS: FR-013a states "The Python callback MUST NOT make a blocking C-ABI call (`session_send` / `session_close`) from inside the inbound trampoline — doing so risks deadlock against the engine worker (as-built 050, `session.h:256-260`)"; `python-module-surface.md §Callback` repeats the rule with the deadlock rationale.

## Lifetime Requirement Completeness & Clarity

- [x] CHK034 Is the `Py_INCREF`-callable-on-register requirement specified, including how long it is held (until interpreter exit for PY-001) and the deferred DECREF/registry boundary (PY-004)? [Clarity, Spec §FR-013] — PASS: FR-013 states "the binding MUST `Py_INCREF` the Python callable registered … on register and keep it alive for as long as the native session can invoke it; for PY-001's single-callback test the callable is held until interpreter exit. Releasing it on reregister / deregistration / engine teardown … is deferred to PY-004"; `swig-typemap-contract.md T-4` records "INCREF'd at register"; `data-model.md E-4` repeats the lifetime scope.
- [x] CHK035 Is the borrowed-message guarantee specified as read-within-the-window with the binding exposing a non-owning view? [Completeness, Spec §FR-014] — PASS: FR-014 states "The inbound message object handed to the Python callback is valid ONLY for the duration of that callback invocation … the binding MUST expose it as a non-owning view and the callback MUST read its field(s) within the call"; `data-model.md E-3` specifies "SWIG proxy is created non-owning (`SWIG_NewPointerObj(..., own=0)`)"; `swig-typemap-contract.md T-4` shows `own=0` in the trampoline.
- [x] CHK036 Is the absence of an active post-window invalidation guard documented as a named limitation (storing the view past the callback = UAF), rather than left as an implied behavior? [Edge Case, Spec §FR-014, §L-053-1] — PASS: FR-014 explicitly states "A Python reference to the view stored past the callback and dereferenced later is a use-after-free; that escape is a documented limitation (L-053-1), not a guard 053 ships"; `spec.md §Assumptions` names "L-053-1 (documented limitation)" and says the binding ships no active post-window invalidation guard; the UAF risk is explicitly named, not left implied.

## Requirement Consistency

- [x] CHK037 Is FR-013a's "documentary-only guarantee" consistent across spec, the callback contract, and T017 (docstring) — i.e., nowhere implies active enforcement in PY-001? [Consistency, Spec §FR-013a, contracts/python-module-surface.md §Callback, tasks.md T017] — PASS: FR-013a states "PY-001's guarantee is documentary: the binding states this in the callback docstring (T017); active detection/enforcement is deferred"; `python-module-surface.md §Callback` uses informational language ("No `session_send` / `session_close` from inside the callback"); `tasks.md T017` adds the docstring only; no artifact implies active enforcement for PY-001.
- [x] CHK038 Does FR-013a's supersession of `[2m §6.5]` ("send-from-fromApp is legal") agree with the data-model E-4 provenance note? [Consistency, Spec §FR-013a, data-model.md E-4] — PASS: FR-013a states "This supersedes `[2m §6.5]`'s 'send-from-`fromApp` is legal' note"; `data-model.md E-4` provenance note states "FR-013a … supersedes `[2m §6.5]` / `2m-pybind.md:89`"; `[2m §6.5]` was verified at line 1144 of `.specify/2m-pybind.md` and the old "legal" note confirmed; both artifacts agree on the supersession direction and cite the same anchor.
- [x] CHK039 Is the sanitizer requirement consistent across SC-004, plan IX §2, T014/T014a, and research D-9 (all stating CI-wired ASan+TSan, none retaining the superseded "local-only / deferred to PY-002" language)? [Consistency, Spec §SC-004, plan.md IX §2, tasks.md T014a, research.md D-9] — PASS: SC-004 mandates CI-wired ASan+TSan (not local-only); `plan.md §Constitution Check IX §2` row states "SC-004 wires an ASan (+ TSan) build+pytest leg of the extension into the Tier-1 `python-bindings` CI job"; `tasks.md T014a` enumerates both legs with the CI-wired mandate; `research.md D-9` explicitly records the superseded "local-only / TSan not required / CI-sanitized Python deferred to PY-002" language was "overturned by the SC-004 amendment"; all four artifacts are now consistent.

## Acceptance Criteria Quality (Measurability)

- [x] CHK040 Is "the interpreter is not corrupted; the test neither crashes nor deadlocks" stated as an objectively checkable criterion (over the SC-002 repeated runs)? [Measurability, Spec §SC-004, §SC-005-adjacent] — PASS: SC-004 states "Executing the Python callback from the engine worker thread never corrupts the interpreter — the test neither crashes nor deadlocks across the SC-002 repeated runs"; pass/fail is binary (crash = exit non-zero or sanitizer abort; deadlock = timeout).
- [x] CHK041 Are the sanitizer requirements quantified — which sanitizers (ASan + TSan), CI vs local, and the explicit UBSan omission with rationale — rather than a vague "sanitizer-clean"? [Measurability, Spec §SC-004, tasks.md T014a] — PASS: SC-004 names ASan and TSan explicitly; `tasks.md T014a` specifies "one ASan, one TSan" CI legs and states "UBSan is omitted for the extension leg (CPython C-API aliasing generates UBSan noise; ASan+TSan cover the riskiest surfaces) — record that omission as a one-line waiver in the `/speckit-verify` decision doc"; `research.md D-9` repeats the UBSan omission rationale; nothing is left vague.
- [x] CHK042 Is the determinism target measurable (≥50 consecutive local runs, zero flakes/hangs) and distinguished from the CI single-run gate? [Measurability, Spec §SC-002, §Clarifications] — PASS: SC-002 states the Tier-1 job "runs the pytest once as the merge gate; a local pre-PR stress run of at least 50 consecutive iterations shows no flake (CI is not looped)"; `spec.md §Clarifications` restates the ≥50× local / single-run CI distinction; both gates are independently specified.
- [x] CHK043 Is the "bounded, test-failing deadline (never an unbounded wait)" requirement specified for every live-I/O wait, so a non-establishment fails rather than hangs CI? [Completeness, plan.md §Constraints, research D-2, tasks.md T006] — PASS: `plan.md §Constraints` states "Live-I/O steps MUST use bounded, test-failing deadlines (never an unbounded wait → CI hang)"; `research.md D-2` enumerates every wait (bound-port poll, establishment poll, callback-receipt wait) and mandates bounded deadlines; `tasks.md T006` cites D-2 and repeats the rule; `quickstart.md` demonstrates `wait_until` with timeout.

## Exception / Edge Coverage

- [x] CHK044 Is the behavior specified when the Python callback itself raises (catch/log at the trampoline, do not propagate into the worker; full policy = PY-003)? [Coverage, Exception Flow, Spec §Edge Cases] — PASS: `spec.md §Edge Cases` states "The Python callback itself raises an exception → it MUST NOT corrupt the interpreter or the engine worker; minimal trampoline handling (catch/log, do not propagate into the worker) is acceptable for PY-001. Defining the full propagation policy is PY-003"; `swig-typemap-contract.md T-4` trampoline code includes `if (PyErr_Occurred()) PyErr_Print(); /* thin: do not propagate into the worker (PY-003 owns policy) */`.
- [x] CHK045 Is the initiator-cannot-connect path covered (session does not reach established within the timeout; test observes non-established without crashing)? [Coverage, Edge Case, Spec §Edge Cases] — PASS: `spec.md §Edge Cases` states "Initiator cannot connect (acceptor not yet bound, or wrong port) → the session does not reach established within the timeout; the test observes the non-established state without crashing"; the non-crash observable outcome is specified.

## Dependencies & Assumptions

- [x] CHK046 Is the out-of-order-destroy concern scoped as out of hardening (PY-004), with the P1 test using explicit correct destroy ordering stated as an assumption? [Assumption, Spec §Assumptions, §Edge Cases] — PASS: `spec.md §Assumptions` states "Object lifetime in the P1 test uses explicit, correct destroy ordering; lifetime-guard hardening (preventing use-after-free on out-of-order destroy) is PY-004"; `spec.md §Edge Cases` states "Objects destroyed out of order … → out of scope for hardening (PY-004); the P1 test uses explicit, correct destroy ordering"; `data-model.md E-2` repeats "PY-001 does not add lifetime-guard hardening (that's PY-004)".
- [x] CHK047 Is the TSan-over-CPython caveat captured (CPython not TSan-instrumented → suppressions file needed; keep trampoline reports live)? [Dependency, tasks.md T014a, research D-9] — PASS: `tasks.md T014a` states "TSan covers the FR-007 GIL worker-thread race with a CPython suppressions file (CPython itself is not TSan-instrumented — suppress interpreter-internal reports, keep trampoline reports live)"; `research.md D-9` repeats the same suppressions requirement and rationale.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 17 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **17** |

### SPEC-FIXED items
None.

### DD-DECIDED items
None.

### WAIVED items
None.

Anchors spot-verified: `[2m §6.5]`, `[2m §6.7]`, `[const §VII.2]`, `[const §VII.3]`, `[const §VII.4]`, `[const §IX.2]`, `[const §XII.5]` — all resolve in signed-off revision `.specify/2m-pybind.md` (Draft v0.3 Gate A r2) and `.specify/constitution.md`.
