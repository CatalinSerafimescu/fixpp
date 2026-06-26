---
description: "Task list for 053-python-thin-binding (PY-001)"
---

# Tasks: Thin End-to-End Python Binding (PY-001)

**Input**: Design documents from `specs/053-python-thin-binding/`
**Prerequisites**: plan.md, spec.md, research.md (D-1..D-9), data-model.md (E-1..E-5), contracts/{python-module-surface,swig-typemap-contract}.md, quickstart.md

**Tests**: REQUIRED. TDD is mandatory (`[const §VII.3]`), and the e2e round-trip is written **first / RED** as the
deliberate false-green guard (research D-4): a blanket SWIG include compiles unusable wrappers, so only an
end-to-end test forces every typemap to actually work. The pytest e2e is the feature's definition of done.

**Organization**: One P1 user story (the loopback round-trip). Setup + Foundational build/interface
prerequisites block it; Polish carries the mandatory close-out tasks.

**Scope guard**: This feature is an **additive consumer** — it does NOT modify `include/fix/c_api.h`
(FR-012; the `0→1` freeze stays held). All new code is under `bindings/python/`. PY-002 (full GIL),
PY-003 (typed exceptions), PY-004 (lifetime hardening), PY-005 (wheel) are out of scope.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an incomplete task)
- **[US1]**: the loopback-round-trip user story

---

## Phase 1: Setup

**Purpose**: Confirm the build/test vehicle and resolve the one implement-time choice the design deferred.

- [X] T001 [P] Confirm prerequisites on the current tree: bundled `dictionaries/FIX44.xml` is present + readable; `cmake --preset linux-clang-debug -DFIXPP_BUILD_PYTHON=ON` configures and builds the existing one-function `bindings/python/fixpp.i` (baseline import+`version_string` smoke green); SWIG ≥4.0 and `pytest` available. — DONE: SWIG 4.2.0, pytest 9.0.3, Python 3.12.3, FIX44.xml present; baseline `fixpp_py` built in `build/linux-clang-debug-py` (after a conan re-install to pull `tomlplusplus/3.4.0`); `test_smoke.py` 2/2 green.
- [X] T002 Resolve the deferred msg-type/field choice (research D-7): pick a **FIX 4.4 application** MsgType with **no required repeating group** and a simple scalar **string** tag, confirm by inspection of `message.h`/`session.h` docs that sparse-body commit is accepted for a missing required field/group (inbound validation defaults OFF per 041), and record the concrete MsgType + tag in `specs/053-python-thin-binding/research.md` D-7.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The build wiring + SWIG interface skeleton every round-trip step depends on.

**⚠️ CRITICAL**: US1 cannot be implemented until this phase is complete.

- [X] T003 Extend `bindings/python/CMakeLists.txt`: link the **static** `fixpp_capi` archive (ensure `-fPIC`) into the `_fixpp` MODULE, add `-static-libstdc++ -static-libgcc` (Linux), keep `Python3::Module`; confirm the import still loads with no runtime `libfixpp_capi.so` dependency (FR-010, research D-6).
- [X] T004 [P] Add sanitizer build paths for the extension for the SC-004 sanitizer builds — a CMake option/preset pair (e.g. `FIXPP_PYTHON_SANITIZER={asan,tsan,none}`) that propagates `-fsanitize=address` **or** `-fsanitize=thread` (mutually exclusive) to `_fixpp` + its static deps; document the `ASAN_OPTIONS=detect_leaks=0` + `LD_PRELOAD` invocation (ASan) and the TSan run with its CPython suppressions file in `bindings/python/tests/` or quickstart (SC-004, research D-9).
- [X] T005 Rework `bindings/python/fixpp.i` interface shape (contracts/swig-typemap-contract.md T-1): replace the blanket `%include "fix/c_api.h"` with a **selective** interface (`%include` the in-scope declarations or `%ignore` everything out of scope per the research D-4 table); `%include "typemaps.i"`; expose the `FIXPP_C_ABI_VERSION_MAJOR/_MINOR` macros (`%constant`/`%inline` over `version.h`) and the `ROLE_*` / `RESET_SEQNUM_*` / `SECURITY_INSECURE_PLAIN_TCP` enum constants to Python. Add explicit SWIG **`%rename`** directives to strip the `fixpp_` / `FIXPP_` prefix from wrapped functions and enum constants so the Python surface matches the contracts' short names (`fixpp.session_open`, `fixpp.ROLE_ACCEPTOR`, `fixpp.version_string`). **This renames the existing `test_smoke.py` call `fixpp.fixpp_version_string()` → `fixpp.version_string()`** — update it (T001's baseline already says `version_string`).

**Checkpoint**: extension builds (static-linked, ASan variant available) with the selective interface skeleton; round-trip functions present but not yet usable (no typemaps) — the RED test will prove it.

---

## Phase 3: User Story 1 — Loopback FIX round-trip from Python (Priority: P1) 🎯 MVP

**Goal**: A Python script using only the installed `fixpp` module loads a FIX 4.4 dictionary, stands up two
engines (acceptor + initiator) over loopback, establishes, sends one application message, receives it in a
Python callback, and reads one scalar field equal to what was sent — with no C/C++ toolchain present.

**Independent Test**: `pytest bindings/python/tests/test_roundtrip.py` is green and asserts the received
field equals the sent value (SC-001/SC-003).

### Tests for User Story 1 (write FIRST — MUST FAIL before implementation) ⚠️

- [X] T006 [US1] Write the failing e2e test `bindings/python/tests/test_roundtrip.py`: two engines (acceptor port-0 → read `acceptor_bound_endpoint`; initiator → that port), full gold-reference config (security plaintext, per-role `reset_on_logon`, heartbeat 30, `reset_seqnum_policy` bilateral_lenient), establish, build+send one app message, receive in a registered Python callback, assert the scalar field equals sent. **Every wait uses a bounded, test-failing deadline** (poll-with-deadline; never an unbounded wait → CI hang — research D-2). Run it; confirm it FAILS (RED) against the T005 skeleton (FR-011, SC-001/SC-002/SC-003).
- [X] T006a [US1] Write the failing negative-path assertion in `bindings/python/tests/test_roundtrip.py` (or a sibling): `fixpp.dict_load_from_xml('/nonexistent/path')` under `pytest.raises(fixpp.Error)`, asserting the raised message carries the `fixpp_strerror` text. Run it; confirm it FAILS (RED) against the T005 skeleton. This is the RED witness for the T012 error bridge (FR-008, SC-005).

### Implementation for User Story 1 (make T006 pass)

- [X] T007 [US1] `bindings/python/fixpp.i`: stock `OUTPUT` typemaps (`%apply`) for every `**out` handle (dict/engine/engine_config/session/session_config/msg), `bool* out_established`, `uint16_t* port_out` (contracts T-2) — FR-001/FR-002/FR-004.
- [X] T008 [US1] `bindings/python/fixpp.i`: the `engine_create(cfg)` hand-wrapper that calls the real 4-arg `fixpp_engine_create(cfg, FIXPP_C_ABI_VERSION_MAJOR, FIXPP_C_ABI_VERSION_MINOR, &out)` (contracts T-1; research D-4) — FR-002.
- [X] T009 [US1] `bindings/python/fixpp.i`: the config-`const char*` `in` typemaps — `set_comp_ids(sender,target)`, `set_begin_string`, `set_tcp_endpoint(host,…)`, and `set_security(kind, cert, key)` cert/key (`None`→`NULL`): Python `str` → UTF-8 NUL-terminated, **reject embedded NUL** (contracts T-3) — FR-003/FR-004a.
- [X] T010 [US1] `bindings/python/fixpp.i`: message typemaps — `create_outbound`/`set_string` `(str → ptr+len)`; `commit` `((payload,len) → bytes)`; `session_send` `(bytes → frame+len)`; `msg_get_string` `(out-buffer → str)` (contracts T-3; data-model E-5) — FR-005/FR-006.
- [X] T011 [US1] `bindings/python/fixpp.i`: the inbound callback trampoline in the `%{ %}`/`%inline` block — `PyGILState_Ensure/Release`, `Py_INCREF` the callable (load-bearing; held until interpreter exit — DECREF/registry = PY-004), non-owning `SWIG_NewPointerObj(inbound, …, own=0)` msg proxy read in-window; the `register_callback` `in` typemap binds the trampoline + callable (contracts T-4; data-model E-3/E-4) — FR-007/FR-013/FR-014.
- [X] T012 [US1] `bindings/python/fixpp.i`: the error bridge — `%exception` **scoped to `fixpp_error_t`-returning functions only** (typed/per-call, NOT blanket — it must not misfire on `fixpp_version_string`/`fixpp_engine_destroy`) → raise a single `fixpp.Error(fixpp_strerror(code))`; poll fns (`is_established`, `acceptor_bound_endpoint`) return their value (contracts T-5) — FR-008/FR-009.
- [X] T013 [US1] Build and run `pytest bindings/python/tests/test_roundtrip.py` until **GREEN**; confirm the received field equals the sent value (SC-003) and the run completes within the bounded per-run timeout; `test_smoke.py` stays green (the CI `python-bindings` job runs the whole dir, so the round-trip is now the meaningful Tier-1 gate — FR-011, SC-001).
- [X] T014 [US1] **SC-004**: build the extension under the T004 ASan path and run the round-trip once (`ASAN_OPTIONS=detect_leaks=0`); confirm no interpreter corruption / UAF on the GIL-trampoline + callable-lifetime + borrowed-msg path; record the evidence for the verify doc (FR-007/FR-013/FR-014, SC-004).
- [X] T014a [US1] **D1 / IX §2**: wire the Tier-1 `python-bindings` CI workflow to add **two** sanitizer build+pytest legs for the `_fixpp` extension — one ASan, one TSan (the T004 `FIXPP_PYTHON_SANITIZER={asan,tsan}` variants; they are mutually exclusive builds, run as separate legs). ASan catches the FR-013/FR-014 UAF; TSan covers the FR-007 GIL worker-thread race with a CPython suppressions file (CPython itself is not TSan-instrumented — suppress interpreter-internal reports, keep trampoline reports live). **UBSan is omitted** for the extension leg (CPython C-API aliasing generates UBSan noise; ASan+TSan cover the riskiest surfaces) — record that omission as a one-line waiver in the `/speckit-verify` decision doc. Satisfies constitution IX §2 for the binding (SC-004 in CI, not local-only). Depends on T004 + T013; build caps: each leg runs alone (WSL2/CI resource guard).
- [X] T015 [US1] **SC-002**: run `test_roundtrip.py` ≥50 consecutive times locally; confirm zero flakes and zero hangs (the bounded deadlines fail rather than hang); record the stress result (SC-002).

**Checkpoint**: PY-001 is functionally complete — the round-trip is green, sanitizer-clean, and deterministic.

---

## Phase 4: Polish & Cross-Cutting Concerns

- [X] T016 [P] Fix the Gate-A residual P3 in `specs/053-python-thin-binding/research.md` D-2 (~line 32): correct the prose so "register inbound callback" comes **after** `session_open(A)` and before `engine_start` (matching the authoritative quickstart and `session.h:268`).
- [X] T017 [P] Add the thread-safety/GIL contract to the binding docstrings/comments in `bindings/python/fixpp.i` (callback runs on a worker thread, GIL reacquired; no blocking `session_send`/`session_close` from inside the callback — FR-013a) — FR-006 docstring requirement.
- [X] T018 Run `specs/053-python-thin-binding/quickstart.md` end-to-end as written (sanity that the documented surface matches the shipped binding).

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [X] T019 [P] **Catalogue close-out**: flip the feature-owned OFFICIAL row **PY-001** in `spec/feature-catalogue.md` from `backlog` → `done` (evidence = this PR/squash) AND add/update its `spec/coverage-index.md` entry. (PY-002/PY-003/PY-004/PY-005 remain `backlog` — out of this feature's scope.)
- [X] T020 **Feature-completeness audit (MUST be the FINAL task)**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale; (ii) every spec **FR-001..014 + FR-004a + FR-013a** and **SC-001..005** maps to a landed test AND a landed implementation; (iii) the **PY-001** OFFICIAL catalogue row is `done` with a matching `coverage-index.md` entry. Record the verdict (100% or fully-waived) in `.specify/decisions/053-python-thin-binding-verify.md` `## Completeness` (or `.specify/decisions/053-python-thin-binding-completeness.md`). Hard `/gate-b` precondition (Article XVII §8 / pre-flight 4d).

---

## Dependencies & Execution Order

- **Setup (T001–T002)**: start immediately; T002 (msg-type choice) must land before T006 writes the test.
- **Foundational (T003–T005)**: depends on Setup; **blocks US1**. T003/T005 are sequential on `fixpp.i`/CMake; T004 [P] is independent.
- **US1 (T006–T015)**: T006 + T006a (RED tests) first — T006a (bad-dict-path → `fixpp.Error`) is the RED witness the T012 error bridge turns green. Then the `fixpp.i` typemap tasks T007–T012 all edit the **same file** (`fixpp.i`) → **sequential, not [P]** (same-file conflict). T013 (green) depends on T007–T012; T014 (ASan), T014a (CI sanitizer leg), and T015 (stress) depend on T013 (T014a also depends on T004).
- **Polish (T016–T020)**: after US1. T016/T017/T019 are [P] (different files). T020 is the **final** task and depends on everything (it audits the landed tree).

### Within US1 (TDD)
T006 + T006a (tests, RED) → T007–T012 (typemaps/trampoline/wrapper/error-bridge, sequential on `fixpp.i`) → T013 (GREEN) → T014 (ASan) → T014a (CI sanitizer leg) → T015 (stress).

### Parallel opportunities
- T001 ‖ (T002 must precede T006).
- T004 ‖ T003/T005 (different files).
- Polish: T016 ‖ T017 ‖ T019 (different files); T020 last.
- **Note**: T007–T012 are NOT parallel — they all edit `bindings/python/fixpp.i`.

---

## Implementation Strategy

### MVP (this whole feature is the MVP)
1. Phase 1 Setup → 2. Phase 2 Foundational → 3. Phase 3 US1 (RED test → typemaps → GREEN → ASan → stress) → **STOP and VALIDATE** the round-trip independently → 4. Polish + close-out.

### Notes
- TDD: T006 MUST fail before T007–T012; never write a typemap without the failing assertion that needs it.
- Commit after each logical group (e.g., after T005 skeleton; after T013 green; after T015).
- Build caps: max `-j2`; the ASan build runs alone (WSL2 OOM guard).
- No `include/fix/c_api.h` edit at any point (FR-012) — if a real C-ABI gap surfaces, STOP and raise it as a separate additive (MINOR) C-ABI feature before this merges (freeze held).
