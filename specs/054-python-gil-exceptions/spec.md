# Feature Specification: Python GIL Discipline & Typed Exception Translation (PY-002 + PY-003)

**Feature Branch**: `054-python-gil-exceptions`
**Created**: 2026-06-26
**Status**: Draft
**Input**: User description: "Python bindings GIL correctness and typed exception translation (PY-002 + PY-003), building on the merged PY-001 thin SWIG binding (053). PY-002 — fine-grained GIL discipline: audit every binding that can call a blocking C-ABI path and ensure GIL release; ensure every C-to-Python callback trampoline reacquires the GIL; formally amend [2m §6.5] for FR-013a (blocking-call-from-callback stays documentary). PY-003 — typed exception translation: a hierarchy rooted at fixpp.Error mapping every fixpp_error_t code to a typed subclass, fixpp_strerror() as the message baseline, unknown/future codes → fixpp.UnknownError; include the subprocess-watchdog regression test deferred from 053 (a raising Python callback must not deadlock the engine). Wraps the SHIPPED C-ABI only — NO include/fix/c_api.h change (hold the FR-012 0→1 freeze). Tier-1 python-bindings matrix none/asan/tsan stays green."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - GIL discipline lets multi-threaded Python use the binding without deadlock or corruption (Priority: P1)

A Python developer drives the binding from a multi-threaded program: one thread blocks in a long-running native call (send / close / engine teardown) while the engine's worker thread concurrently delivers an inbound message to the developer's registered Python callback. Neither side deadlocks against the other, and the callback executes interpreter code without corrupting the interpreter.

**Why this priority**: A binding that can deadlock the interpreter or corrupt it under ordinary multi-threaded use is unusable, and this is the freeze-validator's hardest correctness surface. PY-001 fixed the acute deadlock reactively for three wrappers; PY-002 turns that into a **systematic, audited, witnessed** discipline so a reviewer (and a future maintainer adding a wrapper) can tell which calls release the GIL and why.

**Independent Test**: Build the GIL-release canary (a compile-time flag that elides the GIL release around the blocking wrappers) and run a pytest that puts an inbound message in flight on the worker while the main thread blocks in a teardown call — proven to **hang under the canary** and **pass without it**. Delivers a discriminating witness that the GIL is actually released, plus a documented audit table classifying every wrapped function.

**Acceptance Scenarios**:

1. **Given** an established session with a registered Python recv callback and an inbound message in flight on the engine worker, **When** the main thread calls a blocking teardown (`session_close` / `engine_destroy`) that waits for the worker to drain, **Then** the worker reacquires the GIL, runs the callback, drains, and the teardown returns — no deadlock — and the same scenario **hangs** when the GIL-release canary is active.
2. **Given** the documented GIL-discipline audit table, **When** a reviewer cross-checks it against `fixpp.i`, **Then** every wrapped C-ABI function is classified GIL-releasing (blocking) or GIL-holding (non-blocking), each with a justification, and the implementation matches the table.
3. **Given** the inbound callback fires on an engine worker thread, **When** it touches Python objects, **Then** the binding has reacquired the GIL for that trampoline, and the audit's conclusion states exactly how many bound C→Python trampolines exist (one: the recv trampoline) and that binding the unbound toApp/send callback is out of scope for this feature.

---

### User Story 2 - Callers catch FIX errors by category and recover the exact code (Priority: P2)

A Python developer wraps a binding call in `try/except` and catches a **specific category** of failure (a wire/conformance error vs a dictionary-config error vs a thread-lifecycle error) by exception type, then reads the exact numeric error code and message off the caught exception to log or branch on it.

**Why this priority**: PY-001 routes every non-OK status through a single `fixpp.Error`, so callers cannot distinguish a malformed-wire failure from a bad-config failure except by string-matching the message. The typed hierarchy is the user-facing ergonomics that makes the binding usable for real error handling, and it is a pure SWIG/Python-layer change that holds the C-ABI freeze.

**Independent Test**: A pytest triggers representative non-OK paths (e.g., a bad dictionary path, an out-of-range index) and asserts each raises the **block-matching** typed subclass, that the subclass is catchable as the `fixpp.Error` root, and that the caught exception exposes the numeric code and the `fixpp_strerror` message.

**Acceptance Scenarios**:

1. **Given** a C-ABI call that returns a dictionary-block error code, **When** it surfaces to Python, **Then** a dictionary-category subclass of `fixpp.Error` is raised, catchable both as that subclass and as `fixpp.Error`.
2. **Given** any raised typed exception, **When** the caller inspects it, **Then** it carries the numeric `fixpp_error_t` code and a symbolic code name, with the `fixpp_strerror` text as the message — so the caller can branch on category by type **and** recover the exact code.
3. **Given** the binding's code→exception mapping and the `error.h` code set, **When** a coverage test compares them, **Then** the mapping covers **exactly** the `error.h` code set (set-equality), so adding or removing a code in the header without updating the mapping fails the test.
4. **Given** a non-OK code in an unknown/future block (a block with no assigned subclass), **When** it surfaces to Python, **Then** it maps to `fixpp.UnknownError` (forward-compat, mirroring the C-ABI `FIXPP_ERR_UNKNOWN` rule); a code in a **known** block but with an unrecognized numeric value maps to that block's subclass.

---

### User Story 3 - A raising Python callback never deadlocks the engine (Priority: P3)

A Python developer's registered inbound callback raises an exception (a bug in their handler). The engine does not deadlock, terminate, or corrupt the interpreter; the exception is surfaced (printed) and the engine keeps running.

**Why this priority**: This is the regression test deferred out of 053 (the raising-callback deadlock that the GIL fix resolved). It is a robustness guard rather than new capability, but it must be pinned with a hard-timeout witness so the fix cannot silently regress.

**Independent Test**: A subprocess-watchdog pytest registers a callback that raises, drives an inbound message, and asserts the subprocess completes within a hard timeout (no hang) — failing the watchdog if the engine deadlocks.

**Acceptance Scenarios**:

1. **Given** a registered inbound callback that raises, **When** an inbound message is delivered, **Then** the exception is contained at the trampoline (caught, printed via the interpreter's error-print path, execution continues) and is **not** propagated into the C++ worker.
2. **Given** the raising-callback scenario runs in a child process under a watchdog with a hard timeout, **When** the test runs, **Then** the child completes within the timeout (no deadlock) across repeated runs.

### Edge Cases

- A blocking wrapper newly identified by the audit as blocking (if any beyond the three PY-001 already covers) must release the GIL; the audit table is the census that prevents a missed one.
- A C→Python callback path that is **not** bound by this feature (the toApp/send callback is `%ignore`d; no establishment/state callback exists in the shipped C-ABI) must be explicitly recorded as out of scope so "reacquire the GIL on every trampoline" is not read as a gap against an unbound path.
- An error code that exists in `error.h` but is never returned on any exercised path still must have a mapping entry (set-equality coverage is static against the header, not dynamic against exercised paths).
- A future code added to an existing block must land on that block's subclass (not `UnknownError`); only a code in a wholly unassigned block falls to `UnknownError`.
- The GIL-release canary may, like the PY-001 missing-reacquire canary, manifest as a **hang** rather than a sanitizer report — the witness asserts on a hard timeout, not on a TSan/ASan finding.

## Clarifications

### Session 2026-06-26

- Q: Naming scheme for the per-block exception subclasses under `fixpp.Error`? → A: Idiomatic PascalCase by category — one subclass per `error.h` block: `GeneralError` (0–99), `WireError` (100–199), `DictError` (200–299), `ThreadError` (300–399), `StoreError` (400–499), `SyncError` (500–599), `TlsError` (600–699), `TransportError` (700–799), `DecimalError` (800–899), `ControlPlaneError` (900–999), `BindingError` (1200–1299), `SessionError` (1400–1499) — plus `UnknownError`. (Final class names/spelling fixed in data-model; the per-block scheme is locked.)
- Q: Is the C-ABI-code→exception translation an exposed callable Python function, or inline in the SWIG out-typemap only? → A: An **exposed helper** (a callable translator the out-typemap also routes through) — single source of truth, so the UnknownError path and exact-set coverage are testable directly with a synthetic code (SC-002/SC-006).
- Q: How is the GIL-release discriminating canary (FR-004) wired? → A: **Local-only**, proven RED (hang under canary), documented, with a CI-automation waiver mirroring 053's SC-004 canary precedent (no deliberate-hang job in the CI matrix).
- Q: How does each exception carry its numeric code + symbolic name (FR-007)? → A: Plain attributes — `.code` (int) + `.code_name` (str, e.g. `"FIXPP_ERR_DICT_CONFIG"`). No `IntEnum`.

## Requirements *(mandatory)*

### Functional Requirements

**PY-002 — GIL discipline**

- **FR-001**: The binding MUST carry a documented **GIL-discipline audit table** that classifies every wrapped C-ABI function as GIL-releasing (blocks waiting on the engine worker / io_context) or GIL-holding (non-blocking, pure in-memory, or construction-time), each with a one-line justification; the implementation in `fixpp.i` MUST match the table.
- **FR-002**: Every wrapper the audit classifies as blocking MUST release the GIL around the native call (the `Py_BEGIN/END_ALLOW_THREADS` band covering only `$action`). The three PY-001 already covers (`session_close`, `session_send`, `engine_destroy`) are retained; any additional blocking wrapper the audit identifies MUST be brought under the same discipline.
- **FR-003**: Every C→Python callback trampoline MUST reacquire the GIL before touching Python objects. The audit MUST enumerate all **bound** trampolines and state the conclusion explicitly: exactly one exists (the inbound recv trampoline, already GIL-correct from PY-001); binding the unbound toApp/send callback is **out of scope** for this feature.
- **FR-004**: The GIL **release** MUST have a **discriminating, proven-RED witness**: a compile-time canary (mirroring the PY-001 missing-reacquire canary) that elides the `Py_BEGIN/END_ALLOW_THREADS` band, plus a test exercising a blocking teardown whose completion depends on the engine worker acquiring the GIL to run the recv callback — **proven to hang under the canary and pass without it**. If no such hang-discriminating scenario is constructible, that MUST be documented as a limitation rather than substituted with a non-discriminating ("two threads both send") test that is green with or without the release.
- **FR-005**: The FR-013a constraint (no blocking C-ABI call — `session_send` / `session_close` — from inside the inbound callback) MUST be formalized by **amending the `[2m §6.5]` note** that previously stated "send-from-`fromApp` is legal." The binding-level guarantee stays **documentary** (the callback docstring states the deadlock hazard); active detection/enforcement is NOT added.

**PY-003 — typed exception translation**

- **FR-006**: The binding MUST provide a **typed exception hierarchy rooted at the existing `fixpp.Error`**, with **one subclass per `fixpp_error_t` block**, named in idiomatic PascalCase by category (`GeneralError`, `WireError`, `DictError`, `ThreadError`, `StoreError`, `SyncError`, `TlsError`, `TransportError`, `DecimalError`, `ControlPlaneError`, `BindingError`, `SessionError` — per the `error.h` block layout and the ratified `[2m]` "one Python subclass per block" design), including the binding-specific block (`FIXPP_ERR_BINDING_*`, 1200–1299). Every non-OK C-ABI status surfaced to Python MUST raise the subclass matching the code's block. (Exact class spelling is fixed in data-model; the per-block scheme is locked.)
- **FR-007**: Each raised typed exception MUST carry the numeric `fixpp_error_t` code as a plain integer attribute `.code` and a symbolic name as a string attribute `.code_name` (e.g. `"FIXPP_ERR_DICT_CONFIG"`), with the `fixpp_strerror` text as the exception message — so a caller can branch on category by exception type **and** recover the exact numeric code. (No `IntEnum`.)
- **FR-008**: The code→exception translation MUST be a single **exposed callable** (a Python-reachable translator that the SWIG out-typemap also routes through — the single source of truth), and it MUST cover **exactly** the `error.h` code set (set-equality, not subset). A test MUST derive the expected set from the C-ABI header and fail if the mapping omits a code or contains a code the header no longer defines — a drift guard for future blocks/codes.
- **FR-009**: A non-OK code in a **known** block but with an unrecognized numeric value MUST map to that block's subclass; a code in an **unknown/future** block (no assigned subclass) MUST map to `fixpp.UnknownError` (forward-compat, mirroring the C-ABI `FIXPP_ERR_UNKNOWN` *rule* — distinct from the defined code `FIXPP_ERR_UNKNOWN`=2, which lives in the cross-cutting block and maps to `GeneralError`). `fixpp.UnknownError` MUST itself be a subclass of `fixpp.Error`. Because the translator is exposed (FR-008), this path is testable directly with a synthetic out-of-range code.
- **FR-010**: In-typemap conversion failures (non-str / embedded-NUL / invalid-UTF-8 / non-bytes inputs) MUST continue to raise a `fixpp.Error`-rooted exception (the PY-001 routing), not a bare built-in `TypeError`/`ValueError`, so all binding-originated failures share the typed root.

**PY-003 — raising-callback robustness**

- **FR-011**: A Python inbound callback that raises MUST NOT deadlock the engine, terminate the process, or corrupt the interpreter; the exception MUST be **contained at the trampoline** (caught, printed via the interpreter error-print path, execution continues) and MUST NOT propagate into the C++ worker. A **subprocess-watchdog regression test** with a hard timeout MUST witness no-deadlock (the test deferred from 053).

**Cross-cutting**

- **FR-012**: This feature MUST NOT modify `include/fix/c_api.h` or any `fix/c_api/*.h` header (the `0→1` ABI freeze stays held). All work is confined to the SWIG layer (`bindings/python/`), the pytest suite, and the `[2m]` phase-2 design note.
- **FR-013**: The new/updated pytest(s) MUST run in the Tier-1 `python-bindings` CI matrix (`none` / `asan` / `tsan`) and the matrix MUST stay green. The discriminating GIL-release canary build (FR-004) is **local-only**: proven RED locally and documented, with the CI-automation waived under the same rationale as 053's SC-004 canary (no deliberate-hang/timeout job is added to the matrix).

### Key Entities *(include if feature involves data)*

- **Error block / category**: a contiguous `fixpp_error_t` numeric range (general 0–10, wire 100–102, dict 200–203, thread 300–302, store 400–403, sync 500, tls 600–603, … binding 1200–1204, … 1400–1405) that maps to one Python exception subclass.
- **Typed exception**: a Python class rooted at `fixpp.Error`, one per block, carrying `.code` (int) and `.code_name` (str); `fixpp.UnknownError` is the forward-compat fallback for unassigned blocks.
- **Code→exception translator**: a single Python-reachable callable mapping a numeric `fixpp_error_t` to its block subclass (or `UnknownError`); the SWIG out-typemap routes through it, and tests exercise it directly for exact-set coverage and the UnknownError path.
- **GIL-discipline audit table**: the documented classification of every wrapped C-ABI function (blocking → GIL-releasing; non-blocking → GIL-holding) plus the bound-trampoline census.
- **GIL-release canary**: a compile-time switch that elides the GIL release, used to prove the release is load-bearing (hang under canary, pass without).
- **Subprocess watchdog**: a child-process test harness with a hard timeout that fails if a raising callback deadlocks the engine.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A Python developer can catch a specific error category by exception type (e.g., catch only dictionary-config errors) and read the numeric code and message off the caught exception — verified by a pytest that triggers representative non-OK paths and asserts the block-matching subclass, root catchability, and the recoverable code.
- **SC-002**: The typed code→exception mapping covers **100%** of the `error.h` code set with set-equality — a test that derives the expected set from the header passes, and mutating the header (adding/removing a code) without updating the mapping fails it.
- **SC-003**: The GIL-release witness is **discriminating**: the canary test **hangs (hits its hard timeout) with the release-canary active** and **passes without it**; or, if a hang-discriminating scenario is not constructible, the limitation is documented and the non-discriminating substitute is explicitly NOT presented as proof.
- **SC-004**: A raising Python callback never deadlocks the engine — the subprocess-watchdog test completes within its hard timeout across repeated runs, and the interpreter is not corrupted.
- **SC-005**: The full Tier-1 `python-bindings` matrix (`none` / `asan` / `tsan`) stays green with the new tests, satisfying the binding's sanitizer gate.
- **SC-006**: A code in an unknown/future block maps to `fixpp.UnknownError` — witnessed by surfacing a synthetic out-of-assigned-range code and asserting `fixpp.UnknownError` (still a `fixpp.Error`).
- **SC-007**: The GIL-discipline audit table accounts for every wrapped C-ABI function (no wrapped function is unclassified) and the bound-trampoline census matches `fixpp.i` exactly.

## Assumptions

- The exception hierarchy is rooted at the **already-shipped `fixpp.Error`** (PY-001), not a renamed `fixpp.FixppError`; the `[2m]` design's `FixppError` naming is satisfied by `fixpp.Error` as the root (an optional `FixppError = Error` alias is an implementation detail for `/speckit-clarify`/`/speckit-plan`, not a behavior change). Renaming the shipped `fixpp.Error` is out of scope (it would break the 053 surface).
- Exception granularity is **per-block** (one subclass per `fixpp_error_t` block), not per-code (12 block classes vs 48 code classes) — this is the ratified `[2m]` design and gives better forward-compat (a future code in an existing block lands on the right category). The block-class scheme is locked (FR-006, Clarifications); only the exact class spelling is fixed in data-model.
- The GIL-release discriminating scenario is the **PY-001 deadlock shape**: a blocking teardown (`session_close` / `engine_destroy`) on the main thread waiting for the worker to drain, while the worker needs the GIL to run the inbound recv callback. This is assumed constructible (it is the exact bug PY-001 fixed); FR-004 requires documenting a limitation if it proves otherwise.
- The callback-raise policy is **contained-at-trampoline** (catch + print + continue); the typed hierarchy is for **API-return** errors only. Propagating a Python exception into the C++ worker is explicitly NOT done (it would risk `std::terminate` across the noexcept boundary). PY-003 does not add a re-raise-into-caller mechanism for callback exceptions.
- Only the **bound** recv trampoline is in scope for the GIL-reacquire requirement. The toApp/send callback (`fixpp_session_register_send_callback`) is `%ignore`d (PY-001) and stays unbound here; no establishment/state callback exists in the shipped C-ABI (GAP-004 was deferred). Binding either is a later slice.
- Ownership/lifetime hardening (DECREF-on-reregister, session-keyed callable registry, the active post-window message-view guard / L-053-1) is **PY-004**, not this feature; wheel/pip/abi3/manylinux packaging is **PY-005**.
- `[2m §6.5]` lives in the phase-2 pybind design (`research/.../decisions/2m-pybind.md` convergence story and its normative source); amending the note is a documentation deliverable that touches no C-ABI header, so FR-012's freeze holds. `/speckit-plan` resolves the exact edit location.
- Build and test are in-tree via the existing `-DFIXPP_BUILD_PYTHON=ON` Tier-1 `python-bindings` job on Linux x86_64, CPython 3.12 reference interpreter (matches the job's `setup-python`); the binding targets CPython 3.10–3.13 single-interpreter per `[2m]`.
- SWIG remains the generator; the binding continues to wrap the C-ABI headers (`fix/c_api/*.h`), not the C++ API.

## Normative References

Per constitution Article VI §5, the normative entries informing this spec (from the coverage index / catalogue rows PY-002, PY-003):

- `[2m §6.1]` Per-call GIL discipline — release on blocking calls (`SWIG_PYTHON_THREAD_BEGIN_ALLOW` / `Py_BEGIN_ALLOW_THREADS`), `PyGILState_Ensure` on engine→Python callbacks. **This feature's PY-002 core.**
- `[2m §6.5]` Blocking-call-from-callback note — **amended by FR-005** (supersedes the "send-from-`fromApp` is legal" wording to reflect FR-013a's documentary no-blocking-from-callback constraint).
- `[2m §4.6, §6.3, §6.7]` Exception translation / ownership context — the `fixpp.FixppError` block-mapped hierarchy ("one Python subclass per `fixpp_error_t` block"); **this feature's PY-003 core** (ownership/lifetime per §6.2/§6.7 stays the deferred PY-004 boundary).
- `[2i §1.1]` Error-code block layout — the `[1200, 1299]` binding-error block (`FIXPP_ERR_BINDING_*`, 1200–1204) that PY-003's hierarchy must cover.
- `[const §IV.3]` Distribution Model — Python bindings ship via SWIG over the C ABI.
- `[const §VII.2, §VII.3, §VII.4]` Testing — pytest against the SWIG bindings; TDD mandatory; no code without a test.
- `[const §IX]` Tier-1 gate / error reporting — `pytest bindings/python/tests/` runs on every PR; the `FIXPP_ERR_UNKNOWN` forward-compat rule the `UnknownError` fallback mirrors.
- `[const §X.1, §X.5, §X.6]` ABI Policy — the C ABI is **consumed unchanged** by this feature (no `include/fix/c_api.h` modification; the `0→1` freeze stays held).
- C-ABI contract headers exercised (read-only): `fix/c_api/{error,session,message,engine,dict,version,handles}.h`.
