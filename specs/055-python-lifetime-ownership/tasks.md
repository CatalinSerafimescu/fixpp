---
description: "Task list — PY-004 Python lifetime/ownership OO layer (055)"
---

# Tasks: Python bindings ownership / lifetime layer (PY-004)

**Input**: Design documents from `/specs/055-python-lifetime-ownership/`
**Prerequisites**: plan.md, spec.md (US1/US2/US3, FR-001..018, SC-001..007), research.md (D-1..D-12 + Proposed `[2m]` Article XX amendment), data-model.md (E-1..E-8), contracts/python-lifetime-api.md (C-1..C-8), quickstart.md

**Tests**: INCLUDED. The spec mandates per-story Independent Tests and witnesses every SC with a named pytest (plan §Source Code lists the files); `[const §VII.2]` makes pytest the binding gate. TDD ordering per phase: write the story's tests RED first, then implement to green.

**Repository root** = the library submodule (`research/G19-fix-fpml-iso20022/library/`). All paths below are relative to it.

**Scope guard (freeze-clean)**: NO `include/fix/c_api.h` change. Codes 1201/1202/1204 already exist in `error.h` and `fixpp.i` (PY-003/054). The OO layer is pure-Python + targeted `fixpp.i` trampoline/register/engine_create edits over the FROZEN C-ABI. The `[2m §4.5]` 6-method director and value-typed config/decimal classes are OUT (D-1).

## Format: `[ID] [P?] [Story] Description`

- **[P]**: parallelizable (different file, no dependency on an incomplete task)
- **[Story]**: US1 / US2 / US3 (Setup / Foundational / Polish carry no story label)

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Bring the new OO module into existence and onto the `fixpp` package surface.

- [X] T001 [P] Create `bindings/python/fixpp_oo.py` skeleton: module docstring (scope per `[2m §6.2]` / D-1), import the flat substrate functions and the typed exception classes (`ObjectLifetime`, `CallbackReentrantClose`, `SubInterpreterRejected`, `CapiError`, `FixppError`) from the generated `fixpp` module, and declare the five wrapper class names (`Engine`, `Session`, `Message`, `Application`, `Dictionary`) as empty stubs. (FR-001 surface; contracts C-1)
- [X] T002 Wire `fixpp_oo.py` into the package and re-export the OO classes from the `fixpp` surface so `import fixpp; fixpp.Engine` resolves alongside the surviving flat functions: edit `bindings/python/CMakeLists.txt` to install the new `.py` next to `_fixpp.so` + `fixpp.py`, and add the re-export glue. Verify: `python -c "import fixpp; fixpp.Engine; fixpp.Session; fixpp.Message; fixpp.Application; fixpp.Dictionary"` succeeds and the flat `fixpp.session_send` symbol still exists. (FR-001 additive; Structure Decision)

**Checkpoint**: `import fixpp` exposes the five class names + the unchanged flat substrate.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The shared liveness primitive every wrapper composes. BLOCKS all user stories.

**⚠️ CRITICAL**: No user-story work begins until this is complete.

- [X] T003 Implement the shared liveness-sentinel base in `bindings/python/fixpp_oo.py`: the per-wrapper `(_handle, _dead)` state + a `_ensure_live()` guard helper that raises `fixpp.ObjectLifetime` (numeric 1202) and returns WITHOUT touching the C-ABI when `_dead` is True; every wrapper method that would call the C-ABI calls it FIRST. State is GIL-protected (no `threading.local`, no OS-thread-id assumptions). (FR-002, FR-003, FR-016 mechanism; data-model E-6; contracts C-2/C-8)

**Checkpoint**: the sentinel guard is unit-exercisable in isolation (fake handle → `_dead=True` → `ObjectLifetime`).

---

## Phase 3: User Story 1 - No use-after-free reachable from Python (Priority: P1) 🎯 MVP

**Goal**: Build the full pure-Python wrapper layer (`Engine`/`Session`/`Message`/`Application`/`Dictionary`) carrying the liveness sentinel, so every post-close / post-dispatch-window access raises `fixpp.ObjectLifetime` (1202) instead of a UAF. Closes **L-053-1**. Engine construction rejects sub-interpreters.

**Independent Test**: under AddressSanitizer — keep an inbound `Message` past the dispatch window / past `session.close()` and read a field (seam #8); keep a `Session` past `engine.close()` and call `send` (seam #3). Each raises `fixpp.ObjectLifetime`, zero ASan findings, no crash. Engine from a sub-interpreter raises `SubInterpreterRejected`.

### Tests for User Story 1 (write RED first)

- [X] T004 [P] [US1] `bindings/python/tests/test_lifetime.py`: seam #3 (Session outlives `engine.close()` → `send` raises `ObjectLifetime` 1202) and seam #8 (inbound `Message` stashed past callback return / past `session.close()` → accessor raises `ObjectLifetime`) — **including a raising-callback variant**: a callback that stashes the inbound `Message` then RAISES must still leave it `_dead` (accessor afterward raises `ObjectLifetime`, no UAF — FR-017 Trampoline exit discipline, the L-053-1 shape under exception-exit); all runnable under the ASan leg. **Also a `Dictionary` liveness witness** — construct a `Dictionary`, call `destroy()`, then call an accessor → raises `ObjectLifetime` (1202) (FR-003 covers ALL handle-bearing wrappers, not just Session/Message; closes the Article VII §4 test gap for T009's new Dictionary code). Assert the exact numeric code + no dereference. (SC-001, FR-003/FR-004; AC1/AC2/AC3)
- [X] T005 [P] [US1] `bindings/python/tests/test_subinterpreter.py`: constructing `fixpp.Engine(config)` from a PEP 554 sub-interpreter raises `fixpp.SubInterpreterRejected` (1201) before any native engine is created. (FR-018, SC-007; contracts C-7) **[055 impl note]** On CPython 3.12 the `_fixpp` extension refuses subinterpreter import outright, so the `_is_main_interpreter()` 1201 guard is shadowed by that stronger import-time barrier; the FR-018 goal holds, the typed `SubInterpreterRejected` path is unwitnessed on this build (test tolerates the import block, keeps the 1201 assertion armed). Recorded as a limitation at Polish.

### Implementation for User Story 1

- [X] T006 [US1] `Engine` wrapper in `bindings/python/fixpp_oo.py`: `__init__` creates the native handle via the flat `engine_create` AND rejects a non-main CPython interpreter → `SubInterpreterRejected` (1201) before native creation; `open_session(...)` (wraps `fixpp_session_open`, tracks the child in a `_sessions` `weakref.WeakSet`), `start()`; all public methods guard with `_ensure_live()`. (FR-001, FR-002, FR-018, FR-006-init; data-model E-1)
- [X] T007 [US1] `Session` wrapper in `bindings/python/fixpp_oo.py`: constructed by `Engine.open_session`; holds `_handle`, a **strong** `_engine` ref UP (Py_INCREF — parent cannot be GC'd first), a `_messages` `weakref.WeakSet`, `_application=None`, `_in_callback=False`; `send(msg)` and accessors guard with `_ensure_live()`; `register_application(app)` registers via the **additive OO** hand-wrapper from T010 (`fixpp_py_register_application_oo`, userdata = this `Session`) — NOT the flat `session_register_callback`. (FR-001, FR-002, FR-005, FR-006-init; data-model E-2)
- [X] T008 [US1] `Message` wrapper (two flavours) in `bindings/python/fixpp_oo.py`: `_handle`, `_dead`, `_is_inbound`, a **strong** `_session` ref UP (Py_INCREF); accessors (`get_string`/`get_int`/… as needed) guard with `_ensure_live()`; `set_*` on an inbound message short-circuits to `CapiError(code=4)` (`_is_inbound`, distinct from `_dead`); constructed instances add themselves to `Session._messages`. **Outbound-construction Python API**: an outbound `Message` is created via `session.create_message(msg_type)` (wrapping `fixpp_msg_create_outbound`, `message.h:290` / `fixpp.i:640`; D-11) with `_is_inbound=False`; add this surface to `contracts/python-lifetime-api.md` C-1 if absent. (FR-001, FR-002, FR-005, FR-012-distinction; data-model E-3; contracts C-1; research D-11)
- [X] T009 [US1] `Dictionary` + `Application` wrappers in `bindings/python/fixpp_oo.py`: `Dictionary.load_xml(path)` is a `@staticmethod` factory (wraps `fixpp_dict_load_from_xml`) — **no liveness guard applicable** (no instance exists at the call site); `Dictionary` **instance** methods (`destroy()` and any future accessors) guard with `_ensure_live()` (E-5). `Application` base class exposing `fromApp(self, session, msg)` (v1.0 inbound-only per D-1). (FR-001/FR-002; data-model E-4/E-5; contracts C-1)
- [X] T010 [US1] Add an **ADDITIVE** OO registration path in `bindings/python/fixpp.i` — do NOT mutate the flat `session_register_callback` / `fixpp_py_recv_trampoline` (they stay byte-behaviour-identical: `userdata` = INCREF'd bare callable, 1-arg `cb(proxy)` — required by FR-001 "flat substrate unchanged" + SC-004). Add a new SWIG-invisible hand-wrapper (e.g. `fixpp_py_register_application_oo(session, session_wrapper_obj)`) whose `userdata` is the **INCREF'd owning `Session` wrapper**, and a new **OO trampoline** (`fixpp_py_recv_trampoline_oo`) that: reaches the callable via `session._application`, builds the inbound `Message` wrapper (engine-owned, `_is_inbound=True`, `own=0`), registers it in `session._messages`, dispatches `session._application.fromApp(session_wrapper, msg_wrapper)` (2-arg OO shape — B3), and **arms `msg._dead=True` before the GIL is released at callback return** so a stashed read afterward raises `ObjectLifetime`. **Exit discipline (FR-017 Trampoline exit discipline):** the arming MUST happen unconditionally — including on the `PyErr_Print` exception-exit path when the callback raised; no early-return may skip it (else a message stashed by a raising callback UAFs — the L-053-1 shape). `Session.register_application` (T007) calls this new OO entry-point. (FR-004 — **closes L-053-1**; FR-017 exit discipline; FR-001 flat-unchanged + OO substrate for US2; SC-004; data-model E-3 inbound lifecycle; contracts C-2/C-4)
- [X] T011 [US1] SC-004 regression gate: confirm the existing flat-substrate tests (`test_roundtrip.py`, `test_exceptions.py`, `test_error_coverage.py`, `test_gil_release_canary.py`, `test_callback_raise_watchdog.py`, `test_smoke.py`) stay green unchanged after the OO layer is added. **These tests exercise the FLAT `session_register_callback` path directly** — the gate proves T010's additive OO path did NOT mutate it (F1). Run the FULL (unfiltered) test suite, not a prefix-filtered subset (header-consumer blind-spot per standing CI lesson). The T004/T005 tests also pass (ASan leg for T004). (SC-001 non-concurrent, SC-004)

**Checkpoint**: MVP — the UAF class is eliminated (seams #3/#8 raise `ObjectLifetime` under ASan) and L-053-1 is closed; flat substrate unchanged.

---

## Phase 4: User Story 2 - Deterministic teardown, no callback leak, no deadlock (Priority: P2)

**Goal**: Ordered `close()` that invalidates children first, releases the binding-owned callback (fixing the hold-until-interpreter-exit leak), and destroys native handles in order; context-manager teardown + `DeprecationWarning` on GC-only teardown; blocking-API-from-callback raises `CallbackReentrantClose` (1204) instead of deadlocking.

**Independent Test**: weakref-track a registered callback, drop the caller's external strong ref, close the session → after `gc.collect()` the weakref is dead (binding released its own ref); re-register → prior callable released. `with Engine(config)` → `close()` ran in documented order on exit; GC-only teardown emits `DeprecationWarning`. send/close/engine.close from inside a callback raise `CallbackReentrantClose` (watchdog would hang on a regression).

### Tests for User Story 2 (write RED first)

- [X] T012 [P] [US2] `bindings/python/tests/test_close_flow.py`: ordered close (derived `Message` wrappers marked `_dead` BEFORE the native close), idempotent double-close (no double-free/error), weakref child invalidation; **concurrent-engine-entry witness** — a second thread attempts `engine.open_session(...)` / `engine.start()` while `engine.close()` is parked in native destroy and must raise `ObjectLifetime` (1202) without entering the C-ABI. (FR-007, FR-008; SC-001 close-race characterization)
- [X] T013 [P] [US2] `bindings/python/tests/test_callback_lifetime.py`: register a callback, **drop the caller's external strong ref**, close the session → `weakref` dead after `gc.collect()` (binding dropped its own ref — discriminating witness); re-registering a different callback releases the prior one. (FR-011, SC-002; contracts C-4)
- [X] T014 [P] [US2] `bindings/python/tests/test_context_manager.py`: `with Engine(config) as engine:` runs `close()` deterministically on exit in the documented order; `with` Session likewise; GC-only teardown without explicit close emits a `DeprecationWarning` and still attempts best-effort cleanup. (FR-009, FR-010)
- [X] T015 [P] [US2] `bindings/python/tests/test_reentrancy.py`: send / session.close / engine.close from inside an inbound callback each raise `CallbackReentrantClose` (1204) — under a watchdog/timeout that would hang on a deadlock regression; **io_threads>1 (e.g. 4) rotating-pool arm** firing the in-callback raise on a different OS thread (FR-016 / `[2m §9 seam #4]`); **Engine.close all-sessions preflight** — `engine.close()` from inside one session's callback raises with NO sibling session closed (C-3 step 1 / Codex P1#2); **Session.close step-0 backstop** — `session.close()` from inside its OWN callback raises 1204 AND leaves state unmodified (`_dead` not armed, `_application` not dropped, no native close); **exception-exit marker-clear witness** — a callback that RAISES (then `PyErr_Print`) must still clear `_in_callback`, so a *subsequent* legitimate `session.send(...)` from a non-callback context does NOT falsely raise `CallbackReentrantClose` (1204) (FR-017 Trampoline exit discipline). (FR-017, SC-007; contracts C-3/C-6, data-model E-7)

### Implementation for User Story 2

- [X] T016 [US2] Ordered `Session.close()` (contracts C-3) in `bindings/python/fixpp_oo.py`: step-0 reentrancy fail-fast `if self._in_callback: raise CallbackReentrantClose (1204)` BEFORE any state mutation/C-ABI; then arm `self._dead=True` FIRST (NEW-P2a); walk `_messages` set each `_dead=True`; drop `self._application` (releases the binding's callable ref — the leak fix); call `fixpp_session_close(self._handle)` directly (unguarded); release the `Session`-`userdata` INCREF only AFTER the native close; `_was_explicitly_closed=True`; idempotent. (FR-007, FR-008, FR-011, FR-017; data-model E-2)
- [X] T017 [US2] Ordered `Engine.close()` (contracts C-3) in `bindings/python/fixpp_oo.py`: all-sessions `_in_callback` preflight → raise `CallbackReentrantClose` (1204) with NO session closed and NO C-ABI; arm own `self._dead=True` FIRST before the GIL-releasing teardown; for each child `Session` call a **private unguarded** close helper; call a **private unguarded** `fixpp_engine_destroy(self._handle)`; `_was_explicitly_closed=True`; idempotent. (FR-007, FR-008, FR-017; data-model E-1; SC-001 engine close-race)
- [X] T018 [US2] Context-manager + finaliser protocol on `Engine` and `Session` in `bindings/python/fixpp_oo.py`: `__enter__`/`__exit__` (→ `close()`); `__del__` attempts best-effort close and emits a `DeprecationWarning` when not previously explicitly closed (cross-module `__del__` order at shutdown not relied upon). (FR-009, FR-010)
- [X] T019 [US2] Callback ownership / leak fix on the **OO registration path** (`fixpp_py_register_application_oo` from T010 + `fixpp_oo.py`; the flat path is untouched per F1): `Session._application` is the SINGLE binding-owned strong ref to the callable; re-registration reassigns it (prior callable released, no native re-pointing, no `userdata` DECREF); the `Session`-`userdata` INCREF is released at/after `fixpp_session_close` (never before — an in-flight callback must not dispatch into a finalized `Session`). (FR-011, contracts C-4; research D-7)
- [X] T020 [US2] Reentrancy marker in `bindings/python/fixpp.i` trampoline: set `session._in_callback=True` on callback entry (under GIL) and **clear it before the single GIL release on exit — unconditionally, including on the `PyErr_Print` exception-exit path when the callback raised** (FR-017 Trampoline exit discipline; no early-return may skip the clear, else a post-exception legitimate blocking call falsely raises `CallbackReentrantClose` 1204). The checks live in `Session.send`/`Session.close`/`Engine.close` (T016/T017). Marker is GIL-protected, not OS-thread-keyed — correct regardless of which worker thread runs the callback. (FR-017, FR-016; data-model E-7)
- [X] T021 [US2] `Message` finalisation (two flavours) in `bindings/python/fixpp_oo.py`: outbound/clone `destroy()` / `__del__` → `fixpp_msg_destroy` (idempotent); inbound flyweight `__del__` is a no-op (engine-owned handle, must NOT free). (FR-012; data-model E-3)
- [X] T022 [US2] Regression + SC gate: SC-002 (callback weakref dead after close/re-register), SC-007 (reentrancy — all three blocking APIs raise 1204, no deadlock), close-flow + context-manager tests green; SC-004 still green (no regression since the T011 gate).

**Checkpoint**: deterministic teardown, zero binding-owned callback leak, and no blocking-from-callback deadlock — US1 + US2 both independently testable.

---

## Phase 5: User Story 3 - Handles cannot silently cross process boundaries (Priority: P3)

**Goal**: Handle-bearing wrappers refuse to be pickled (loud `TypeError`, not a meaningless pointer that UAFs on unpickle).

**Independent Test**: `pickle.dumps(engine)` / `(session)` / `(message)` / `(application)` / `(dictionary)` each raise `TypeError` with the documented message.

### Tests for User Story 3 (write RED first)

- [ ] T023 [P] [US3] `bindings/python/tests/test_pickle_ban.py`: `pickle.dumps` on each handle-bearing wrapper (`Engine`/`Session`/`Message`/`Application`/`Dictionary`) raises `TypeError` whose message explains native handles cannot cross process boundaries. (SC-003, FR-013; contracts C-5)

### Implementation for User Story 3

- [ ] T024 [US3] Pickle-ban in `bindings/python/fixpp_oo.py`: a shared mixin overriding `__reduce_ex__`/`__reduce__` to raise `TypeError("fixpp.<ClassName> objects are not pickleable; native handles cannot cross process boundaries")`, composed into all five wrappers. Scoped to handle-bearing wrappers ONLY — no value-typed classes introduced (FR-014 leg deferred). (FR-013, FR-014; contracts C-5)

**Checkpoint**: all three user stories independently functional.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [ ] T025 [P] Apply the deferred Article XX `[2m]` amendment in `.specify/2m-pybind.md` at the 6 substantive + 1 editorial sites per research.md "Proposed `[2m]` Article XX amendment": §1.3 rule (2), §6.5 send row, §6.5 close row, §9 seam #4, §6.7 1204 table row, §6.7 1204 prose docstring (`:818-820`), + editorial `Session.send` docstring (`:455-460`). (Gate A reviewed; carried-as-proposed-text, applied now per the 043/051/054 precedent.)
- [ ] T026 [P] SC-005 / FR-015 invariant check: assert `git diff` shows `include/fix/c_api.h` byte-unchanged by this feature and no new C-ABI symbol was added; the `0→1` freeze holds.
- [ ] T027 [P] quickstart.md validation: run the documented snippets (context-manager teardown, post-close `ObjectLifetime`, inbound-stash `ObjectLifetime`, reentrancy `CallbackReentrantClose`, pickle `TypeError`, sub-interpreter `SubInterpreterRejected`) as a smoke check; note SC-006 (Tier-1 `python-bindings` none/asan/tsan matrix green with the new tests) is witnessed at `/speckit-verify`.
- [ ] T028 B&L close-out in `spec/behaviors-and-limitations.md` (055 section): record **L-053-1 closed** (inbound flyweight invalidated at callback return) and any new B-/L- rows (e.g. concurrent-close characterization, `[2m]` send-from-callback amendment).
- [ ] T029 [P] **Catalogue close-out**: flip the `PY-004` OFFICIAL row in `spec/feature-catalogue.md` from `backlog` → `done` (with the PR / evidence ref) AND add/update its matching `spec/coverage-index.md` 055 entry.
- [ ] T030 **Feature-completeness audit (FINAL task)**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale; (ii) every FR-001..018 and SC-001..007 maps to a landed test AND a landed implementation; (iii) the `PY-004` OFFICIAL catalogue row is `done` with a matching `coverage-index.md` entry. Record the verdict (100% or fully-waived) in `.specify/decisions/055-python-lifetime-ownership-verify.md` (`## Completeness`) or a sibling `.specify/decisions/055-python-lifetime-ownership-completeness.md`. **Hard `/gate-b` precondition (Article XVII §8 / pre-flight 4d).**

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup (Phase 1)**: no dependencies — start immediately. T002 depends on T001.
- **Foundational (Phase 2, T003)**: depends on Setup — **BLOCKS all user stories**.
- **User Story 1 (Phase 3)**: depends on T003. Internal order: T004/T005 (RED) → T006 → T007 → T008/T009 → T010 → T011. T010 (trampoline restructure to userdata=Session) is the substrate US2 builds on.
- **User Story 2 (Phase 4)**: depends on US1 (needs the wrapper classes + the trampoline userdata=Session restructure from T010). Tests T012–T015 RED first; impl T016/T017 (close-flow) → T018 (context mgr) / T019 (leak fix) / T020 (marker) / T021 (msg finalise) → T022 gate.
- **User Story 3 (Phase 5)**: depends only on the wrapper classes existing (US1). Independently testable; could run in parallel with US2 by a second developer.
- **Polish (Phase 6)**: depends on all desired stories. T030 is the FINAL task.

### Story independence

- **US1 (P1)** is the MVP — shippable alone (eliminates the UAF class, closes L-053-1).
- **US2 (P2)** layers teardown/leak/reentrancy on top of US1's classes.
- **US3 (P3)** (pickle-ban) only needs the classes to exist — independent of US2.

### Parallel opportunities

- T004 / T005 (US1 tests) — parallel (different files).
- T012 / T013 / T014 / T015 (US2 tests) — parallel.
- Within US1 impl, T008 and T009 are parallel (different classes, both after T007). T006/T007/T008/T009 share `fixpp_oo.py`; T010 is `fixpp.i`.
- Polish T025 / T026 / T027 / T029 — parallel (different files); T028 then T030 last.
- ⚠️ Tasks editing the SAME file are NOT parallel: `fixpp_oo.py` (T003, T006–T009, T016–T018, T021, T024) and `fixpp.i` (T010, T019, T020) each serialize within their file.

---

## Parallel Example: User Story 2 tests

```bash
# Launch the US2 RED tests together (different files):
Task: "test_close_flow.py — ordered close + idempotency + concurrent-engine-entry"
Task: "test_callback_lifetime.py — callback weakref dead after close/re-register"
Task: "test_context_manager.py — with-block teardown + DeprecationWarning"
Task: "test_reentrancy.py — blocking-from-callback raises 1204 (watchdog, io_threads>1)"
```

---

## Implementation Strategy

### MVP first (US1 only)

1. Phase 1 Setup → 2. Phase 2 Foundational (T003 sentinel) → 3. Phase 3 US1 → **STOP & VALIDATE** under ASan (seams #3/#8 raise `ObjectLifetime`; L-053-1 closed; flat substrate green). Shippable.

### Incremental delivery

US1 (no UAF) → US2 (teardown + leak fix + no deadlock) → US3 (pickle-ban) → Polish (amendment + catalogue + completeness audit). Each story adds value without breaking the previous.

### Build/sanitizer caps (WSL2)

Build parallelism max `-j2`; sanitizer/verify presets run strictly ONE AT A TIME (OOM cap). ASan witnesses no-UAF (SC-001); the reentrancy watchdog witnesses no-deadlock (SC-007).

---

## Notes

- Tests RED before implementation; assert the exact numeric error code (1201/1202/1204) and the named post-condition directly (no bypassable proxy).
- SC-002 / SC-007 witnesses must be discriminating: SC-002 drops the external strong ref FIRST; SC-007's watchdog must actually hang on a regression and each blocking API must be the decisive parked op.
- No `include/fix/c_api.h` edit at any point — if a genuine native need surfaces, STOP and escalate (it would be a MINOR add before the freeze, not a silent change).
- Commit after each task or logical group.
