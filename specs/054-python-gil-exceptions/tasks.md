# Tasks: Python GIL Discipline & Typed Exception Translation (PY-002 + PY-003)

**Input**: Design documents from `specs/054-python-gil-exceptions/`
**Prerequisites**: plan.md, spec.md, research.md (D-1..D-9), data-model.md (E-1..E-6), contracts/ (gil-discipline, python-exception-surface)

**Tests**: TDD is mandatory here (constitution VII §3). Each axis is authored test-first (RED): the exact-mapping coverage test is RED until `AppError` lands; the GIL-release canary test is RED under the canary build; the watchdog test is RED (timeout) if the GIL-release bands are removed.

**Scope guard (FR-012)**: NO `include/fix/c_api.h` or `fix/c_api/*.h` edit at any point (the `0→1` GA freeze stays HELD). All work is confined to `bindings/python/`, the pytest suite, and the `.specify/2m-pybind.md` phase-2 design note. If a real C-ABI gap surfaces, STOP and raise it as a separate additive (MINOR) C-ABI feature.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on incomplete tasks)
- **[Story]**: US1 = GIL discipline (P1), US2 = typed exceptions (P2), US3 = raising-callback watchdog (P3)

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: build options + shared test staging that the canary and watchdog both consume.

- [ ] T001 [P] Add the local-only `FIXPP_PY_GIL_RELEASE_CANARY` CMake option in `bindings/python/CMakeLists.txt` (mirrors the existing `FIXPP_PY_GIL_CANARY` reacquire-canary option): when ON, pass `-DFIXPP_PY_GIL_RELEASE_CANARY` to the SWIG/C trampoline compile. OFF by default; never set in any CI preset (deliberate-deadlock build is local-only, FR-013). (E-4)
- [ ] T002 [P] Add a shared loopback + in-flight-recv-callback staging helper in `bindings/python/tests/_gil_staging.py` (extracted from `test_roundtrip.py`'s establish recipe): establishes a loopback pair with the `insecure_plain_tcp` profile, registers a recv callback that blocks on a `threading.Event`, puts an inbound message in flight on the worker, then the main thread enters a blocking teardown (`engine_destroy`/`session_close`) and sets the Event. Reused by US1 canary (T005) and US3 watchdog (T013) — the D-2 discriminating staging. (E-4/E-5)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: the macro-guarded GIL-release bands are the shared substrate both US1 (canary) and US3 (watchdog) discriminate against. MUST land before either witness.

- [ ] T003 Guard the existing `session_close` / `session_send` / `engine_destroy` `%exception` GIL-release bands (`Py_BEGIN/END_ALLOW_THREADS` around `$action`) behind `#ifndef FIXPP_PY_GIL_RELEASE_CANARY` in `bindings/python/fixpp.i`, so the canary build elides exactly those three bands and nothing else. The in-typemap (arg conversion) stays before the band; the out-typemap (raise) stays after. (FR-002/FR-004 mechanism; G-1)

---

## Phase 3: User Story 1 — GIL discipline audit + discriminating release canary (P1)

**Goal**: turn PY-001's reactive 3-wrapper GIL release into a systematic, audited, witnessed discipline.

**Independent test**: the canary test hangs (subprocess hard-timeout) under `-DFIXPP_PY_GIL_RELEASE_CANARY` and passes (GREEN, in-matrix) without it; the audit table mechanically diffs against `fixpp.i`.

- [ ] T004 [P] [US1] Write the GIL-discipline audit table as a comment block in `bindings/python/fixpp.i` (just above the `%include`d engine.h/session.h surface): classify **every** wrapped C-ABI function release/hold with a one-line justification (E-1 table verbatim — exhaustive over the `%include`d surface, the four config create/destroy functions listed explicitly), plus the bound-trampoline census conclusion (exactly one: `fixpp_py_recv_trampoline`; `toApp`/send callback `%ignore`d/unbound; no state callback). Implementation MUST match the table. Also fix the stale `%module` docstring at `fixpp.i:23` — its `FR-006 / FR-013a` label is a 053 cross-version reference; update it to the correct 054 FRs (`FR-001 / FR-003`; the no-blocking-from-callback constraint is L-054-1). (FR-001/FR-003/SC-007; G-2/G-3)
- [ ] T005 [P] [US1] RED test `bindings/python/tests/test_gil_release_canary.py`: two-mode. Normal build runs the T002 teardown-vs-in-flight-recv-callback scenario and asserts it completes (GREEN, in-matrix). A `FIXPP_PY_GIL_RELEASE_CANARY` build runs the same scenario in a subprocess with a hard timeout and asserts it hangs (RED) — distinct from 053's reacquire canary (segfault). Use a bounded deadline, never an unbounded wait. (FR-004/SC-003; G-4)
- [ ] T006 [US1] Prove the canary discriminates: build `-DFIXPP_PY_GIL_RELEASE_CANARY=ON` and run T005 **5×** → all hang (RED); build without it and run **5×** → all pass (GREEN). Record the RED/GREEN evidence (commands + outcomes) for the verify doc. The release deadlock is a hang, not a sanitizer report — do not assume RED, prove it. (SC-003)

**Checkpoint**: GIL discipline is audited + the release is discriminatingly witnessed.

---

## Phase 4: User Story 2 — typed exception hierarchy + single-source translator (P2)

**Goal**: callers catch FIX errors by category (typed subclass) and recover the exact code/name/message.

**Independent test**: representative non-OK paths raise the block-matching subclass, catchable as `fixpp.Error`/`FixppError`, carrying `.code/.name/.message`; the header-sourced coverage test asserts set-equality.

- [ ] T007 [P] [US2] RED test `bindings/python/tests/test_exceptions.py`: assert the E-2 hierarchy (every block subclass `issubclass(_, FixppError)`; `Cancelled`/`Unknown` ⊂ `CapiError`; the five binding subclasses ⊂ `BindingError`); `fixpp.Error is fixpp.FixppError`; representative non-OK paths (bad dict path → `ValidatorError`, out-of-range index, etc.) raise the block-matching subclass carrying `.code` (int) + `.name` (symbolic) + `.message` (= `fixpp_strerror(code)` = `str(exc)`); `exception_for_code(99999) is FixppError` (synthetic unmapped-block fallback, SC-006); an in-typemap conversion failure (embedded-NUL) raises root `FixppError` with `.message` only, no `.code`/`.name` (T-2 two-tier carve-out, FR-010). (FR-006/007/009/010; SC-001/006; T-1..T-4)
- [ ] T007a [P] [US2] RED test `bindings/python/tests/test_error_coverage.py`: parse `include/fix/c_api/error.h` (the independent source) for `FIXPP_ERR_*` `#define`s → `{code: name}`; assert `len(codes) == 47` (non-vacuous), then for every code `exception_for_code(code)` is a `FixppError` subclass **and not** the bare `FixppError` fallback, and `set(fixpp._CODE_TO_NAME) == set(codes)` (set-EQUALITY both directions — fails on a header code added OR removed without updating the map). RED until `AppError` covers `[1400,1499]`. (FR-008/SC-002; T-5)
- [ ] T008 [US2] Create the `FixppError` hierarchy in `bindings/python/fixpp.i` (`%init` parented-chain class creation + `%pythoncode` module block): root `FixppError(Exception)`, `Error = FixppError` alias, one subclass per `fixpp_error_t` block (E-2 tree), the five `BindingError` subclasses, the `CapiError` `Cancelled`/`Unknown` subclasses, and **`AppError`** for `[1400,1499]` (the new-in-054 name, D-5). No `UnknownError` class (collides with `Unknown`). (FR-006; T-1)
- [ ] T009 [US2] Add the single-source translator + helpers in the same `%pythoncode` block: hand-written `_CODE_TO_NAME` dict (47 entries, D-3 — constants are not SWIG-exposed); `_map_to_class(code) -> type[FixppError]` (E-2 block-range map, fallback = root for unmapped block); `_make_error(code)` (sets `.code/.name/.message`; `.name` falls back to `f"FIXPP_ERR_{code}"` so it is total over SC-006/FR-009 codes); `_raise_for_code(code)`; public alias `exception_for_code = _map_to_class`; expose `strerror` (already present) as the `.message` source. (FR-007/008; T-4)
- [ ] T010 [US2] Rewire `%typemap(out) fixpp_error_t` in `fixpp.i` to route through a C `%wrapper` helper `fixpp_py_raise_for_code($1)` that lazily `PyImport_ImportModule("fixpp")` (cached `static`) and calls its `_raise_for_code`, then `SWIG_fail` — so the runtime path and the tests share the **same** `_map_to_class`/`_CODE_TO_NAME` (single source of truth, no parallel C mapping). Keep the in-typemap conversion-failure path raising root `FixppError` message-only (FR-010, the as-built `FIXPP_PY_RAISE`). (FR-008; T-4)
- [ ] T011 [US2] GREEN: T007 + T007a pass. Update `test_roundtrip.py` so its `fixpp.Error` assertions stay valid under the `Error = FixppError` alias (the bad-dict-path → `fixpp.Error` and strerror-message assertions). (FR-006)
- [ ] T012 [US2] Run `test_exceptions.py` + `test_error_coverage.py` green under the `python-bindings` ASan and TSan presets (one preset at a time, `-j2`). (SC-005)

**Checkpoint**: typed exceptions usable for real error handling; set-equality coverage guards drift.

---

## Phase 5: User Story 3 — a raising callback never deadlocks the engine (P3)

**Goal**: pin the 053 raising-callback fix with a subprocess-watchdog regression test.

**Independent test**: the watchdog child completes within a hard timeout (no hang) across repeated runs; it would time out if the GIL-release bands were removed.

- [ ] T013 [US3] Witness test `bindings/python/tests/test_callback_raise_watchdog.py`: run the raising-callback scenario in a **child process** (hard timeout) reusing the T002 D-2 staging — the recv callback blocks on a `threading.Event` then **raises** (provably mid-flight); the main thread enters a blocking `engine_destroy`/`session_close` and sets the Event; assert the child exits within the timeout. Assert the as-built containment: the raise is caught + `PyErr_Print`'d at the flat trampoline, execution continues, NOT propagated into the C++ worker, no 1200 engine-translation (D-8 divergence). In-matrix (`none`/`asan`/`tsan`), expected outcome no-hang. (FR-011/SC-004; E-5)
- [ ] T014 [US3] Confirm the watchdog discriminates: it times out (FAIL) under `-DFIXPP_PY_GIL_RELEASE_CANARY` (bands elided → worker can't get the GIL to run the raising callback) and completes normally — record the evidence. A bare raising callback with no concurrent teardown does NOT discriminate; verify the staging is what pins the fix. (FR-011)

**Checkpoint**: the 053 raising-callback-+-concurrent-teardown fix cannot silently regress.

---

## Phase 6: Polish, design amendment & close-out

- [ ] T015 [P] Amend `.specify/2m-pybind.md` (Article XX, committed in THIS PR) at **all four** send-from-callback sites (E-6): §1.3 rule (2), §3.12, the §6.5 carve-out table (add `session_send`), and the §4.6 `CallbackReentrantClose` docstring — recording the as-built-050 blocking-send deadlock as **L-054-1** (a current limitation tied to the blocking as-built; a strand/io_context reentrancy deadlock distinct from the GIL-teardown deadlock, NOT permanent-forbidden). Also add `AppError` `[1400,1499]` to the §4.6/§6.7 hierarchy (D-5), **and** reconcile the §4.6 mapping-table fallback row (~840) per E-6: a direct `_map_to_class` on a wholly unmapped block → root `FixppError` (FR-009; no collision with `Unknown`/code-2), annotating that "→ `Unknown`" is the runtime `[2i §4.4]` downgrade path — so the signed-off doc is not self-contradicting against the binding. Ground the carve-out on `src/capi/session.cpp:284-286` (send) / `:202-205` (close) + the `session.h:255-258` rule. (FR-005/FR-009; G-5)
- [ ] T016 [P] Append **L-054-1** to the behaviors-and-limitations catalogue (`spec/behaviors-and-limitations.md`): `session_send` from inside the inbound callback deadlocks (as-built blocking shape); documentary; active detection = PY-004.
- [ ] T017 [P] Run `specs/054-python-gil-exceptions/quickstart.md` end-to-end against the built `-DFIXPP_BUILD_PYTHON=ON` extension (typed-exception catch, `exception_for_code` introspection, fallback, GIL teardown demo) and confirm every snippet behaves as documented.
- [ ] T018 Ensure **all four** new pytests (`test_exceptions.py`, `test_error_coverage.py`, `test_callback_raise_watchdog.py`, and `test_gil_release_canary.py`) are discovered + run by the Tier-1 `python-bindings` ctest matrix legs (`none`/`asan`/`tsan`) — verify `bindings/python/CMakeLists.txt` registration picks them up. **`test_gil_release_canary.py`'s normal-build GREEN leg MUST be in-matrix (SC-003)**; only its RED leg (the `FIXPP_PY_GIL_RELEASE_CANARY` build) stays out of the matrix. Run the full `python-bindings` suite green under each preset (one at a time, `-j2`). (FR-013/SC-003/SC-005)
- [ ] T019 Re-read the T004 audit table against the final `fixpp.i` and reconcile any drift (the implementation MUST match the table). (SC-007 consistency)

### Mandatory close-out tasks (Gate-B preconditions, Article XVII §8)

- [ ] T020 [P] **Catalogue close-out**: flip `spec/feature-catalogue.md` rows **PY-002** (line 273) and **PY-003** (line 274) from `backlog` → `done` with the `054-python-gil-exceptions` owner + test evidence refs; add/update their `spec/coverage-index.md` PY-002/PY-003 entries to point at the landed tests + the `[2m]` amendment.
- [ ] T021 **Feature-completeness audit (FINAL task)**: assert against the tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver; (ii) every FR-001..FR-013 and SC-001..SC-007 maps to a landed test AND a landed implementation; (iii) PY-002 + PY-003 catalogue rows are `done` with matching `coverage-index.md` entries. Record the verdict (100% or fully-waived, with the UBSan-leg 053 D-9 carry-forward waiver noted) in `.specify/decisions/054-python-gil-exceptions-verify.md` (`## Completeness`). HARD `/gate-b` precondition (pre-flight 4d).

---

## Dependencies & Execution Order

- **Setup (T001–T002)**: start immediately; both [P] (different files). T001 (CMake macro) blocks T003; T002 (staging helper) blocks T005 + T013.
- **Foundational (T003)**: depends on T001; **blocks US1 (T005/T006) and US3 (T013/T014)** — the macro-guarded bands are the shared discriminator.
- **US1 (T004–T006)**: T004 (audit table) ‖ T005 (RED canary test, depends on T002+T003). T006 (RED/GREEN proof) depends on T005.
- **US2 (T007–T012)**: T007 + T007a (RED tests) first. T008→T009→T010 all edit `fixpp.i` → **sequential, not [P]** (same-file). T011 (GREEN + roundtrip alias) depends on T008–T010. T012 (sanitizers) depends on T011.
- **US3 (T013–T014)**: T013 depends on T002 + T003. T014 (discrimination proof) depends on T013.
- **Polish (T015–T021)**: after the three stories. T015/T016/T017/T020 are [P] (different files). T018 depends on T012 (registration). T019 depends on T004 + final `fixpp.i`. T021 is the **final** task — audits the landed tree, depends on everything.

### Within each story (TDD)
- US1: T005 (RED canary) → T003 already guards bands → T006 (prove RED 5/5 / GREEN 5/5).
- US2: T007 + T007a (RED) → T008/T009/T010 (`fixpp.i` sequential) → T011 (GREEN) → T012 (sanitizers).
- US3: T013 (witness, RED if bands removed) → T014 (discrimination proof).

### Parallel opportunities
- T001 ‖ T002.
- T004 ‖ T005.
- T007 ‖ T007a; T015 ‖ T016 ‖ T017 ‖ T020.
- **NOT parallel**: T008/T009/T010/T004/T019 all edit `bindings/python/fixpp.i`.

---

## Implementation Strategy

### MVP / increment order
1. Setup → 2. Foundational (macro-guarded bands) → 3. US1 (audit + discriminating release canary, prove RED 5/5) → 4. US2 (typed hierarchy + translator + set-equality coverage) → 5. US3 (watchdog) → 6. Polish ([2m] amendment in-PR + close-out).

### Notes
- TDD: T005/T007/T007a/T013 MUST be RED before the implementation that greens them.
- Build caps: max `-j2`; sanitizer presets run strictly ONE AT A TIME (WSL2 OOM guard).
- Commit after each logical group (Setup; Foundational; each story GREEN; Polish). Per-phase commits keep the run resumable.
- The `FIXPP_PY_GIL_RELEASE_CANARY` build is local-only — never added to a CI preset (FR-013). Only its GREEN leg (normal build) is in-matrix.
- Set-equality coverage (T007a) is BOTH directions — a subset test passes when a code is deleted from `error.h`. Assert `set(map) == set(header)`.
- The `[2m]` amendment (T015) MUST be committed in THIS PR (Article XX §1 — Gate A has no PR).
- No `include/fix/c_api/*.h` edit at any point (FR-012).
