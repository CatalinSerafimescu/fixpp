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
4. **Given** a non-OK code in an unmapped/future block (no assigned subclass — forward-compat only), **When** it surfaces to Python, **Then** it falls back to the root `fixpp.FixppError`; a code in a **known** block but with an unrecognized numeric value maps to that block's subclass.

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
- A future code added to an existing block must land on that block's subclass (not the fallback); only a code in a wholly unmapped block falls back to the root `FixppError`.
- The GIL-release canary may, like the PY-001 missing-reacquire canary, manifest as a **hang** rather than a sanitizer report — the witness asserts on a hard timeout, not on a TSan/ASan finding.

## Clarifications

### Session 2026-06-26

- Q: Naming scheme for the per-block exception subclasses? → A: Idiomatic PascalCase, one subclass per `fixpp_error_t` block. **The names are the ratified `[2m §4.6]` hierarchy verbatim** (the clarification chose the *scheme*; `[2m §4.6]` supplies the authoritative spellings, which supersede the illustrative examples): root `FixppError` (with `Error = FixppError` alias so the shipped 053 surface survives); `CapiError` (0–99, with `Cancelled`/1 and `Unknown`/2), `ParseError` (100–199), `ValidatorError` (200–299), `SessionError` (300–399 threading), `StoreError` (400–499), `SyncError` (500–599), `TlsError` (600–699), `TransportError` (700–799), `DecimalError` (800–899), `ControlPlaneError` (900–999), `LogError` (1000–1099, reserved/empty), `TapError` (1100–1199, reserved/empty), `BindingError` (1200–1299, with `PythonCallbackRaised`/1200, `SubInterpreterRejected`/1201, `ObjectLifetime`/1202, `WheelAbiMismatch`/1203, `CallbackReentrantClose`/1204). **054 ADDS one class `[2m §4.6]` predates: `AppError` for the `[1400,1499]` block** (session/app + message-construction, minted in 051 after 2m sign-off) — `SessionError` is taken by the threading block, so the new name is `AppError` (flagged for Gate A as the one new public name).
- Q: Is the C-ABI-code→exception translation an exposed callable Python function, or inline in the SWIG out-typemap only? → A: An **exposed helper** (the `[2m §4.6]` `fixpp.errors._map_to_class(code)` translator the out-typemap also routes through) — single source of truth, so the fallback path and exact-mapping coverage are testable directly with a synthetic code (SC-002/SC-006).
- Q: How is the GIL-release discriminating canary (FR-004) wired? → A: **Local-only**, proven RED (hang under canary), documented, with a CI-automation waiver mirroring 053's SC-004 canary precedent (no deliberate-hang job in the CI matrix).
- Q: How does each exception carry its numeric code + symbolic name (FR-007)? → A: Plain attributes per `[2m §4.6]` — `.code` (int) + `.name` (str symbolic, e.g. `"FIXPP_ERR_DICT_CONFIG"`) + `.message` (str = `fixpp_strerror(code)`). No `IntEnum`.

## Requirements *(mandatory)*

### Functional Requirements

**PY-002 — GIL discipline**

- **FR-001**: The binding MUST carry a documented **GIL-discipline audit table** that classifies every wrapped C-ABI function as GIL-releasing (blocks waiting on the engine worker / io_context) or GIL-holding (non-blocking, pure in-memory, or construction-time), each with a one-line justification; the implementation in `fixpp.i` MUST match the table.
- **FR-002**: Every wrapper the audit classifies as blocking MUST release the GIL around the native call (the `Py_BEGIN/END_ALLOW_THREADS` band covering only `$action`). The three PY-001 already covers (`session_close`, `session_send`, `engine_destroy`) are retained; any additional blocking wrapper the audit identifies MUST be brought under the same discipline.
- **FR-003**: Every C→Python callback trampoline MUST reacquire the GIL before touching Python objects. The audit MUST enumerate all **bound** trampolines and state the conclusion explicitly: exactly one exists (the inbound recv trampoline, already GIL-correct from PY-001); binding the unbound toApp/send callback is **out of scope** for this feature.
- **FR-004**: The GIL **release** MUST have a **discriminating, proven-RED witness**: a compile-time canary (mirroring the PY-001 missing-reacquire canary) that elides the `Py_BEGIN/END_ALLOW_THREADS` band, plus a test exercising a blocking teardown whose completion depends on the engine worker acquiring the GIL to run the recv callback — **proven to hang under the canary and pass without it**. If no such hang-discriminating scenario is constructible, that MUST be documented as a limitation rather than substituted with a non-discriminating ("two threads both send") test that is green with or without the release.
- **FR-005**: The FR-013a constraint (no blocking C-ABI call — `session_send` / `session_close` — from inside the inbound callback) MUST be formalized by **amending the normative `[2m]` design** where it currently asserts `Session.send()` from inside `fromApp` is legal. The amendment MUST be a **census** of every occurrence (at minimum: `[2m]` §1.3 rule (2) ~line 89, §3.12 ~line 207, the §6.5 carve-out table, and the `§4.6 CallbackReentrantClose` docstring ~lines 802–804), not just one site — leaving any unamended makes the signed-off doc self-contradicting. The mechanism MUST be grounded on the **as-built 050** blocking shape (`fixpp_session_send` → `co_spawn(ioc_, …, use_future)` + `fut.get()`, `session.h:256-260`) — a strand/io_context reentrancy deadlock **distinct** from, and unaffected by, the GIL-teardown deadlock 053 fixed. It MUST be recorded as a **current limitation (an L-row tied to the blocking as-built)**, NOT a permanent "forbidden by design": `[2m §4.6]`'s "legal" claim was predicated on *non-blocking strand-dispatch*; restoring real legality is a deferred engine item outside 054's freeze-held scope. The binding-level guarantee stays **documentary** (the callback docstring states the deadlock hazard); active detection/enforcement is NOT added (that needs the SWIG director + `session._in_callback` marker = PY-004).

**PY-003 — typed exception translation**

- **FR-006**: The binding MUST realize the **ratified `[2m §4.6]` typed exception hierarchy** verbatim: root `FixppError` with `Error = FixppError` (the shipped 053 `fixpp.Error` becomes the alias, so `pytest.raises(fixpp.Error)` survives); one subclass per `fixpp_error_t` block — `CapiError` (0–99, with `Cancelled`/1 and `Unknown`/2 as subclasses), `ParseError`, `ValidatorError`, `SessionError`, `StoreError`, `SyncError`, `TlsError`, `TransportError`, `DecimalError`, `ControlPlaneError`, `LogError`/`TapError` (reserved blocks, no codes yet), and `BindingError` (1200–1299) with `PythonCallbackRaised`/1200, `SubInterpreterRejected`/1201, `ObjectLifetime`/1202, `WheelAbiMismatch`/1203, `CallbackReentrantClose`/1204. The binding MUST additionally define **`AppError`** for the `[1400,1499]` block (session/app + message-construction, minted in 051 after the `[2m §4.6]` sign-off — an additive amendment to the hierarchy). Every non-OK C-ABI status surfaced to Python MUST raise the subclass matching the code's block.
- **FR-007**: Each raised typed exception MUST carry, per `[2m §4.6]`, `.code` (int — the numeric `fixpp_error_t`), `.name` (str — the symbolic name, e.g. `"FIXPP_ERR_DICT_CONFIG"`), and `.message` (str — the `fixpp_strerror(code)` text, also the exception's `str()`), so a caller can branch on category by exception type **and** recover the exact numeric code. (No `IntEnum`.)
- **FR-008**: The code→exception translation MUST be a single **exposed callable** (the `[2m §4.6]` `fixpp.errors._map_to_class(code)` translator that the SWIG out-typemap also routes through — the single source of truth). A coverage test MUST assert that **every** `FIXPP_ERR_*` code defined in `error.h` (except `FIXPP_ERR_OK`) maps to a **non-fallback** typed subclass — NOT a circular compare between two lists both derived from the SWIG-exposed constants. (The `[1400,1499]` block is the live proof: the test is RED until `AppError` is added.) Adding a future block to the header without extending the mapping leaves those codes on the fallback → the test fails.
- **FR-009**: A code in a **known, populated** block maps to that block's subclass (an unrecognized numeric value within a known block still maps to the block class — e.g. a future 405 → `StoreError`). The **fallback** for a code in a wholly **unmapped/future** block (none exist once `AppError` is added — forward-compat only) is the **root `FixppError`**. (No separate `fixpp.UnknownError` class — that would collide with `[2m §4.6]`'s `Unknown(CapiError)` for code 2, which is the C-ABI's own `FIXPP_ERR_UNKNOWN` downgrade and is distinct from the unmapped-block fallback.) Because the translator is exposed (FR-008), the fallback path is testable directly with a synthetic out-of-range code.
- **FR-010**: In-typemap conversion failures (non-str / embedded-NUL / invalid-UTF-8 / non-bytes inputs) MUST continue to raise a `fixpp.Error`-rooted exception (the PY-001 routing), not a bare built-in `TypeError`/`ValueError`, so all binding-originated failures share the typed root.

**PY-003 — raising-callback robustness**

- **FR-011**: A Python inbound callback that raises MUST NOT deadlock the engine, terminate the process, or corrupt the interpreter; the exception MUST be **contained at the as-built flat trampoline** (caught, printed via the interpreter error-print path, execution continues — the as-built 053 `fixpp_py_recv_trampoline` behavior) and MUST NOT propagate into the C++ worker. A **subprocess-watchdog regression test** with a hard timeout MUST witness no-deadlock (the test deferred from 053). **Divergence note**: the as-built recv callback has a `void(const fixpp_msg_t*, void*)` signature with no return channel, so 054 does NOT translate the raised exception into `FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED`/1200 nor return it to the engine (the `[2m §3.21/§6.1]` director design that does so requires the engine to call INTO a Python `Application` director — a binding shape 053 did not adopt; that translation is deferred to the director slice / PY-004). The `PythonCallbackRaised`/1200 **class** still exists in the FR-006 hierarchy and `1200` still maps (FR-008); only the active engine-side translation is deferred.

**Cross-cutting**

- **FR-012**: This feature MUST NOT modify `include/fix/c_api.h` or any `fix/c_api/*.h` header (the `0→1` ABI freeze stays held). All work is confined to the SWIG layer (`bindings/python/`), the pytest suite, and the `[2m]` phase-2 design note.
- **FR-013**: The new/updated pytest(s) MUST run in the Tier-1 `python-bindings` CI matrix (`none` / `asan` / `tsan`) and the matrix MUST stay green. The discriminating GIL-release canary build (FR-004) is **local-only**: proven RED locally and documented, with the CI-automation waived under the same rationale as 053's SC-004 canary (no deliberate-hang/timeout job is added to the matrix).

### Key Entities *(include if feature involves data)*

- **Error block / category**: a contiguous `fixpp_error_t` 100-wide range (cross-cutting 0–99, wire 100–199, dict 200–299, threading 300–399, store 400–499, sync 500–599, tls 600–699, transport 700–799, decimal 800–899, control-plane 900–999, log/tap 1000–1199 reserved/empty, binding 1200–1299, session/app 1400–1499) that maps to one Python exception subclass per `[2m §4.6]`.
- **Typed exception**: a Python class rooted at `FixppError` (alias `Error`), one per block per `[2m §4.6]`, carrying `.code` (int), `.name` (str symbolic), `.message` (str strerror); the root `FixppError` is the forward-compat fallback for an unmapped block.
- **Code→exception translator**: the single Python-reachable callable `fixpp.errors._map_to_class(code)` (`[2m §4.6]`) mapping a numeric `fixpp_error_t` to its block subclass (or the `FixppError` fallback); the SWIG out-typemap routes through it, and tests exercise it directly for full-mapping coverage and the fallback path.
- **GIL-discipline audit table**: the documented classification of every wrapped C-ABI function (blocking → GIL-releasing; non-blocking → GIL-holding) plus the bound-trampoline census.
- **GIL-release canary**: a compile-time switch that elides the GIL release, used to prove the release is load-bearing (hang under canary, pass without).
- **Subprocess watchdog**: a child-process test harness with a hard timeout that fails if a raising callback deadlocks the engine.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A Python developer can catch a specific error category by exception type (e.g., catch only dictionary-config errors) and read the numeric code and message off the caught exception — verified by a pytest that triggers representative non-OK paths and asserts the block-matching subclass, root catchability, and the recoverable code.
- **SC-002**: **Every** `FIXPP_ERR_*` code in `error.h` (except `FIXPP_ERR_OK`) maps to a non-fallback typed subclass — a test asserting this passes; a code in an unmapped block (the `[1400,1499]` block until `AppError` is added) fails it.
- **SC-003**: The GIL-release witness is **discriminating**: the canary test **hangs (hits its hard timeout) with the release-canary active** and **passes without it**; or, if a hang-discriminating scenario is not constructible, the limitation is documented and the non-discriminating substitute is explicitly NOT presented as proof.
- **SC-004**: A raising Python callback never deadlocks the engine — the subprocess-watchdog test completes within its hard timeout across repeated runs, and the interpreter is not corrupted.
- **SC-005**: The full Tier-1 `python-bindings` matrix (`none` / `asan` / `tsan`) stays green with the new tests, satisfying the binding's sanitizer gate.
- **SC-006**: A code in an unmapped/future block falls back to the root `FixppError` — witnessed by calling the exposed translator with a synthetic out-of-assigned-range code and asserting it returns `FixppError` (the forward-compat fallback; no populated block hits it once `AppError` is added).
- **SC-007**: The GIL-discipline audit table accounts for every wrapped C-ABI function (no wrapped function is unclassified) and the bound-trampoline census matches `fixpp.i` exactly.

## Assumptions

- The exception hierarchy root is **`FixppError`** per the ratified `[2m §4.6]`, with **`Error = FixppError`** as an alias so the shipped 053 `fixpp.Error` surface (and `pytest.raises(fixpp.Error)`) keeps working. The class names are `[2m §4.6]` verbatim (FR-006) — the `/speckit-clarify` "category names" answer chose the *scheme*; `[2m §4.6]` supplies the authoritative spellings, which supersede the illustrative examples (a delta surfaced and reported at `/plan`, to be confirmed at Gate A — the designed spec-vs-`[2m]` reconciliation checkpoint, as 053's own Gate A did).
- Exception granularity is **per-block** (one subclass per `fixpp_error_t` block) per `[2m §4.6]`. 054 realizes the ratified set and **adds one class `[2m §4.6]` predates** — `AppError` for the `[1400,1499]` block (minted in 051 after 2m sign-off). The `AppError` name is the one genuinely-new public name; it is flagged for Gate A (`SessionError` is already taken by the `[300,399]` threading block, so the block cannot reuse it).
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
- `[2m §4.6]` The authoritative typed exception hierarchy (root `FixppError` + one subclass per block + the five `BindingError` subclasses + the `_map_to_class` translator) — **realized verbatim by FR-006/007/008**, and **extended** with `AppError` for the post-2m `[1400,1499]` block.
- `[2i §1.1]` Error-code block layout — the `[1200, 1299]` binding-error block (`FIXPP_ERR_BINDING_*`, 1200–1204) that PY-003's hierarchy covers.
- `[2i §4.3]` / 051 D-6 — the dedicated Phase-4 `[1400,1499]` session/app + message-construction block (codes 1400–1405) minted after `[2m §4.6]` sign-off, which FR-006 maps to the new `AppError` class.
- `[const §IV.3]` Distribution Model — Python bindings ship via SWIG over the C ABI.
- `[const §VII.2, §VII.3, §VII.4]` Testing — pytest against the SWIG bindings; TDD mandatory; no code without a test.
- `[const §IX]` Tier-1 gate / error reporting — `pytest bindings/python/tests/` runs on every PR; the `FIXPP_ERR_UNKNOWN` forward-compat rule the root-`FixppError` fallback mirrors.
- `[const §X.1, §X.5, §X.6]` ABI Policy — the C ABI is **consumed unchanged** by this feature (no `include/fix/c_api.h` modification; the `0→1` freeze stays held).
- C-ABI contract headers exercised (read-only): `fix/c_api/{error,session,message,engine,dict,version,handles}.h`.
