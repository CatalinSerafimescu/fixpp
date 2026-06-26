# Phase 1 Data Model — 054 Python GIL Discipline & Typed Exceptions

Entities are SWIG-layer / Python-layer constructs (no C-ABI types change; freeze held).

---

## E-1 — GIL-discipline audit table (FR-001/SC-007)

The authoritative classification (also a comment block in `fixpp.i`). "release" = `Py_BEGIN/END_ALLOW_THREADS` around `$action`; "hold" = GIL retained.

| Wrapped C-ABI function | Class | Why |
|---|---|---|
| `session_close` | release | `co_spawn(close_exec,…,use_future)` + `fut.get()` — strand drain |
| `session_send` | release | `co_spawn(ioc_,…,use_future)` + `fut.get()` — worker run (mechanism `src/capi/session.cpp:284-286`; rule `session.h:255-258`) |
| `engine_destroy` | release | `stop_fut.get()` + worker joins |
| `engine_create` | hold | construct + worker spawn; no round-trip |
| `engine_start` | hold | starts workers; returns immediately |
| `engine_config_create` | hold | construction-time; returns `fixpp_error_t` (reaches the error bridge) but no engine round-trip |
| `session_config_create` | hold | construction-time; returns `fixpp_error_t` (reaches the error bridge) but no engine round-trip |
| `engine_config_destroy` | hold | in-memory free; **`void`-returning** — never reaches the error bridge |
| `session_config_destroy` | hold | in-memory free; **`void`-returning** — never reaches the error bridge |
| `session_open` | hold | in-memory state mutation |
| `session_is_established` | hold | poll |
| `acceptor_bound_endpoint` | hold | registry read |
| `session_register_callback` (`fixpp_py_register_callback`) | hold | registry write + INCREF |
| all `*_config_set_*` setters | hold | in-memory |
| `dict_load_from_xml` | hold (note) | sync file/XML parse; CPU-bound, **no engine round-trip** → no worker-deadlock class. Releasing for throughput is a deferred nicety, not a correctness gap. |
| `dict_destroy` | hold | in-memory |
| all `msg_*` (create/set/commit/get/destroy) | hold | in-memory arena ops |
| `version_string`, `strerror` | hold | pure function |

The table is **exhaustive over the `%include`d surface** of `engine.h` + `session.h` (`fixpp.i:418-419`) — not a grouped sample; the four config create/destroy functions are listed explicitly so a reviewer can mechanically diff the audit against `fixpp.i` (SC-007/FR-001).

**Bound C→Python trampoline census (FR-003):** exactly **one** — `fixpp_py_recv_trampoline` (recv callback), already GIL-correct (`PyGILState_Ensure/Release`, `fixpp.i:288/302`). The `toApp`/send callback (`fixpp_session_register_send_callback`) is `%ignore`d (unbound); no establishment/state callback exists in the C-ABI. **Conclusion: 1 bound trampoline; no unbound trampoline is a GIL gap.**

---

## E-2 — Exception class hierarchy (FR-006) — `[2m §4.6]` verbatim + `AppError`

```text
FixppError                         (root; alias: Error = FixppError)
├── CapiError                      [0,99]
│   ├── Cancelled                  (1)
│   └── Unknown                    (2)   # C-ABI FIXPP_ERR_UNKNOWN downgrade — NOT the unmapped-block fallback
├── ParseError                     [100,199]   (wire)
├── ValidatorError                 [200,299]   (dict)
├── SessionError                   [300,399]   (threading)
├── StoreError                     [400,499]
├── SyncError                      [500,599]
├── TlsError                       [600,699]
├── TransportError                 [700,799]
├── DecimalError                   [800,899]
├── ControlPlaneError              [900,999]
├── LogError                       [1000,1099] (reserved; no codes in v1.0)
├── TapError                       [1100,1199] (reserved; no codes in v1.0)
├── BindingError                   [1200,1299]
│   ├── PythonCallbackRaised       (1200)
│   ├── SubInterpreterRejected     (1201)
│   ├── ObjectLifetime             (1202)
│   ├── WheelAbiMismatch           (1203)
│   └── CallbackReentrantClose     (1204)
└── AppError                       [1400,1499]  # NEW in 054 (post-2m block; 051 D-6)
```

**Fallback:** a code in a wholly unmapped/future block → the root `FixppError` (forward-compat only; no populated block hits it once `AppError` exists). **No `UnknownError` class** (would collide with `Unknown`).

**Attributes (two-tier, T-2 / FR-007):** every **translated `fixpp_error_t`** exception carries `.code: int`, `.name: str` (e.g. `"FIXPP_ERR_DICT_CONFIG"`), and `.message: str` (= `fixpp_strerror(code)`, and the `str()` of the exception). **Non-`fixpp_error_t` in-typemap conversion failures** (str/NUL/UTF-8/bytes — E-3 / D-9) are `FixppError`-rooted and carry `.message` only (no `.code`/`.name`; no fabricated code, so the `fixpp_strerror(code)` clause does not apply to them).

**Code → class map (`_map_to_class`, `[2m §4.6]` "Mapping rule" + the 054 1400 row):**

| `fixpp_error_t` | class |
|---|---|
| 0 (`OK`) | (never raised) |
| 1 | `Cancelled` |
| 2 | `Unknown` |
| [3,99] | `CapiError` |
| [100,199] | `ParseError` |
| [200,299] | `ValidatorError` |
| [300,399] | `SessionError` |
| [400,499] | `StoreError` |
| [500,599] | `SyncError` |
| [600,699] | `TlsError` |
| [700,799] | `TransportError` |
| [800,899] | `DecimalError` |
| [900,999] | `ControlPlaneError` |
| [1000,1099] | `LogError` |
| [1100,1199] | `TapError` |
| 1200 | `PythonCallbackRaised` |
| 1201 | `SubInterpreterRejected` |
| 1202 | `ObjectLifetime` |
| 1203 | `WheelAbiMismatch` |
| 1204 | `CallbackReentrantClose` |
| [1205,1299] | `BindingError` |
| **[1400,1499]** | **`AppError`** (054) |
| else (unmapped block) | `FixppError` (fallback) |

Currently-populated codes: 0–99 (11), 100s (3), 200s (3), 300s (3), 400s (4), 500s (1), 600s (4), 700s (4), 800s (2), 900s (2), 1200s (5), 1400s (6) = **48** (47 raisable; `OK` never raised).

---

## E-3 — Translator + helpers (FR-008) — single source of truth

Defined in `%pythoncode` (module `fixpp`):

| Symbol | Shape | Role |
|---|---|---|
| `_CODE_TO_NAME` | `dict[int,str]` **hand-written** (47 entries) | code → `"FIXPP_ERR_*"` symbolic name. The `ERR_*` constants are **not exposed** (verified — SWIG drops cast-to-typedef `#define`s), so this is a maintained dict guarded by the D-4 header-parsing test. |
| `_map_to_class(code:int) -> type[FixppError]` | block-range map (E-2) | the single source of truth; returns the fallback `FixppError` for unmapped blocks |
| `_make_error(code:int) -> FixppError` | sets `.code/.name/.message` | constructs the typed instance; `.name = _CODE_TO_NAME.get(code, f"FIXPP_ERR_{code}")` — a **fallback for codes absent from the dict** so it is **total** over its input domain (SC-006 synthetic code + FR-009 future-in-known-block code don't `KeyError`). Runtime is independently safe via `[const §X.4]` downgrade (E-3 routing note). |
| `_raise_for_code(code:int)` | `raise _make_error(code)` | the raise path |
| `exception_for_code(code:int) -> type` | public alias of `_map_to_class` | lets callers/tests introspect the mapping |
| `strerror(code:int) -> str` | `fixpp_strerror` (**already exposed** — verified present) | `.message` source |

**Routing (cross-module):** `%typemap(out) fixpp_error_t` → on non-`OK`, call the C `%wrapper` helper `fixpp_py_raise_for_code($1)` which lazily `PyImport_ImportModule("fixpp")` (cached `static`) and calls its `_raise_for_code`, then `SWIG_fail`. The hop is needed because the wrapper is in `_fixpp` while the translator is in the `fixpp.py` proxy. The runtime path and the tests share `_map_to_class` (FR-008). In-typemap conversion failures (str/NUL/UTF-8/bytes) raise the root `FixppError` (D-9 — no fabricated code; carries `.message` only, no `.code`/`.name` — see T-2's carve-out). **Runtime `.name` safety:** the out-typemap never hands `_make_error` an out-of-table code at runtime — per `[const §X.4]` (`constitution.md:153`) + `error.h:13-14`, the engine's `translate_for_consumer()` downgrades any code newer than the consumer's registered minor to `FIXPP_ERR_UNKNOWN`(2) before return, and 054 is an in-tree same-version build (consumer-minor == engine-minor); the `_make_error` `.name` fallback is therefore a totality fix for **direct** translator calls (SC-006) and the forward-compat L-row, not a runtime-crash fix.

**Deferred:** the `fixpp.errors` submodule (`[2m §4.6]` `fixpp.errors._map_to_class`) — needs a package restructure; as-built lives in the `fixpp` module namespace (D-3; Gate-A-flagged).

---

## E-4 — GIL-release canary (FR-004) — local-only

| Field | Value |
|---|---|
| Macro | `FIXPP_PY_GIL_RELEASE_CANARY` (CMake `-D` option, local-only) |
| Effect | elides the `Py_BEGIN/END_ALLOW_THREADS` bands on `session_close`/`session_send`/`engine_destroy` |
| Witness | `test_gil_release_canary.py` — **two-mode**: a **normal build** runs the teardown-vs-recv-callback scenario (E-1 release class) and must **complete GREEN**; a `FIXPP_PY_GIL_RELEASE_CANARY` build runs it in a subprocess and must **hang (RED)** |
| Assertion | subprocess hard timeout (the deadlock is a hang, not a sanitizer report) |
| CI | **GREEN leg is in-matrix** (normal build, `none`/`asan`/`tsan` — the pass-without leg is witnessed every PR, not skipped); **only the canary RED leg is local-only** (deliberate-hang) + waiver (053 SC-004 precedent) |

Distinct from 053's `FIXPP_PY_GIL_CANARY` (reacquire canary → segfault). Both coexist.

---

## E-5 — Subprocess watchdog (FR-011/SC-004)

| Field | Value |
|---|---|
| Test | `test_callback_raise_watchdog.py` |
| Scenario | **re-uses the D-2 teardown-vs-in-flight-callback staging** (a bare raising callback with no concurrent blocking teardown just `PyErr_Print`s and returns — the child exits cleanly with OR without the GIL release, so it would **not** pin the 053 fix). Staged: the recv callback **blocks on a `threading.Event`** then **raises** (provably mid-flight); the main thread enters a **blocking teardown** (`engine_destroy`/`session_close`) and sets the Event; child must exit within a hard timeout |
| Pass | WITH the GIL release the worker acquires the GIL, runs (+`PyErr_Print`s) the raising callback, the teardown drains, child completes (no hang) → the raising-callback-+-concurrent-teardown fix holds |
| Fail | WITHOUT the release (bands deleted) the main thread holds the GIL in the teardown wait, the worker can't run the raising callback, drain never completes → timeout → engine deadlocked |
| Isolation | child process (a hung worker can't wedge the parent pytest) |
| CI | **in** the matrix (`none`/`asan`/`tsan`) — expected outcome is no-hang |
| As-built behavior witnessed | flat trampoline catch + `PyErr_Print` + continue (D-8); no 1200 engine-translation |

---

## E-6 — `[2m]` design amendment + L-054-1 (FR-005) — Article XX

| Site (`.specify/2m-pybind.md`) | Current | Amended to |
|---|---|---|
| §1.3 rule (2) (~89) | "`Session.send()` from inside `fromApp` is legal" | send-from-callback **deadlocks as-built** (L-054-1); only the non-blocking-strand-dispatch design made it legal |
| §3.12 (~207) | "dispatches onto the session strand" | note the as-built `use_future` blocking shape |
| §6.5 carve-out table | lists only `close()` | **add `session_send`** to the deadlock carve-outs (limitation) |
| §4.6 `CallbackReentrantClose` docstring (~802-804) | "Session.send … is NOT banned" | corrected to the L-054-1 limitation |
| §4.6 / §6.7 hierarchy | 12 block classes (no 1400) | **add `AppError`** `[1400,1499]` (D-5) |
| §4.6 mapping table — fallback row (~840) | "`anything else` → `Unknown` (per `[2i §4.4]` forward-compat downgrade)" | reconcile with FR-009: a **direct** `_map_to_class(code)` on a wholly unmapped block → **root `FixppError`** (no collision with `Unknown`/code-2); the "→ `Unknown`" wording is the **runtime** `[2i §4.4]` path, where the engine downgrades an unrecognized code to `FIXPP_ERR_UNKNOWN`(2) **before** it reaches the translator — so the typemap never hands `_map_to_class` a truly-unmapped code at runtime. Annotate the row with both paths so the signed-off doc is not self-contradicting against the binding (the FR-005 census discipline applied to the fallback row). |

**L-054-1 (behaviors-and-limitations):** `session_send` from inside the inbound callback deadlocks (as-built 050 blocking `co_spawn(ioc_,…,use_future)`+`fut.get()` — mechanism `src/capi/session.cpp:284-286` (send) / `:202-205` (close), the documented deadlock rule at `session.h:255-258`) — a strand/io_context reentrancy deadlock, **distinct** from the 053 GIL-teardown deadlock and unaffected by the GIL release. Documentary (binding docstring); active detection is PY-004. Restoring true legality (non-blocking send-from-callback) is a deferred **engine** item. **Not** a permanent "forbidden by design."

---

## Validation rules summary

- Every populated `error.h` code maps to a non-fallback class (E-3 `_map_to_class`; FR-008/SC-002; the coverage test parses `error.h` as the independent source, asserts `len==47` non-vacuous + non-fallback + `_CODE_TO_NAME` key-set match; `[1400,1499]` is the RED proof until `AppError` lands).
- Every translated `fixpp_error_t` exception has `.code/.name/.message`; non-`fixpp_error_t` in-typemap conversion failures are `FixppError`-rooted and carry `.message` only (two-tier T-2; FR-007; see the E-3 carve-out and `contracts/python-exception-surface.md` T-2).
- `Error is FixppError` (alias; 053 surface survives).
- Exactly one bound trampoline; all three blocking wrappers release (E-1; FR-002/FR-003).
- Release canary RED-under / GREEN-without (E-4; FR-004).
- Watchdog child exits within timeout (E-5; FR-011).
- All four `[2m]` send-legal sites amended (E-6; FR-005).
