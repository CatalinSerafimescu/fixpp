# Phase 0 Research — Python GIL Discipline & Typed Exceptions (054)

All decisions resolve the spec's requirements against the **as-built** PY-001 binding (`bindings/python/fixpp.i`), the shipped C-ABI (`include/fix/c_api/`), and the ratified Phase-2 design (`.specify/2m-pybind.md`). No NEEDS CLARIFICATION remain (the four `/speckit-clarify` answers + the `[2m §4.6]` reconciliation are encoded in the spec).

---

## D-1 — GIL-discipline audit table (FR-001/FR-002/FR-003/SC-007)

**Decision.** Ship a documented table in `fixpp.i` (a comment block + the `data-model.md` E-1 table) classifying **every** wrapped C-ABI function as **GIL-releasing** (blocks waiting on the engine worker / io_context) or **GIL-holding** (non-blocking, pure in-memory, construction-time). The as-built classification (verified against `fixpp.i` lines 384–409 + the C-ABI sources):

| Wrapper | Class | Justification |
|---|---|---|
| `session_close` | **release** | `co_spawn(close_exec, …, use_future)` + `fut.get()` blocks on the session strand draining the close coroutine. |
| `session_send` | **release** | `co_spawn(ioc_, engine_->send(...), use_future)` + `fut.get()` blocks until the send coroutine runs (mechanism `src/capi/session.cpp:284-286`; deadlock rule `session.h:255-258`; engine deep-copies the span on the worker). |
| `engine_destroy` | **release** | `stop_fut.get()` + worker-thread joins; blocks until all workers drain. |
| `engine_create` / `engine_start` | hold | construction + `io_context` worker spawn; returns without blocking on a round-trip. |
| `engine_config_create` / `session_config_create` | hold | construction-time; allocate + return a config-builder handle (return `fixpp_error_t`, reach the error bridge, but no engine round-trip). |
| `engine_config_destroy` / `session_config_destroy` | hold | in-memory free; **`void`-returning** — never reach the error bridge. |
| `session_open` / `session_is_established` / `acceptor_bound_endpoint` / `session_register_callback` | hold | in-memory state mutation / poll / registry write; no strand round-trip. |
| all config setters, all `msg_*` builders, `dict` load/destroy, `version_string`, `strerror` | hold | pure in-memory (or a synchronous XML parse for `dict_load_from_xml` — CPU-bound, no engine round-trip; **flagged** below). |

The census is **exhaustive over the `%include`d surface** of `engine.h` + `session.h` (`fixpp.i:418-419`), not a grouped sample — every wrapped function is classified (the four config create/destroy functions are listed explicitly above so a reviewer can mechanically diff the table against `fixpp.i`, satisfying SC-007/FR-001).

**`dict_load_from_xml` nuance.** It does synchronous file I/O + XML parse. It does **not** block on the engine worker (the deadlock class PY-002 targets), so its GIL is **held** — consistent with PY-001. A future "release GIL around long file parse" is a throughput nicety, not a correctness gap; the audit records it as *hold, with a note* rather than reclassifying (avoids scope creep; no deadlock risk).

**Rationale.** FR-001's value is the *census itself* — a reviewer and a future wrapper-adder can see the rule. The table is the terminating move against a missed blocking wrapper (cf. `[[feedback_census_all_handrolled_scanners_before_scoping_parse_fix]]`).

**Alternatives rejected.** Auto-deriving the classification at runtime (impossible — "does this block on the worker" is a static property of the C-ABI shape); reclassifying `dict_load` to release (scope creep, no deadlock).

---

## D-2 — Discriminating GIL-release canary (FR-004/SC-003)

**Decision.** Add `-DFIXPP_PY_GIL_RELEASE_CANARY` (mirroring the existing `FIXPP_PY_GIL_CANARY` reacquire-canary) that **elides** the `Py_BEGIN/END_ALLOW_THREADS` bands on `session_close`/`session_send`/`engine_destroy`. The witness test (`test_gil_release_canary.py`) constructs the **053 deadlock shape**:

1. An established loopback pair with a registered Python recv callback.
2. An inbound message **in flight** on the worker (the worker wants the GIL to run the recv callback).
3. The main thread calls a **blocking teardown** (`engine_destroy` / `session_close`) that waits for the worker to drain/join.
4. **Without** the release band the main thread holds the GIL while blocked in the join → the worker blocks forever in `PyGILState_Ensure` → never drains → **deadlock** (witnessed as a subprocess hard-timeout).
5. **With** the release band the main thread yields the GIL during the wait → the worker runs the callback, drains, join completes → **pass**.

The test is a **two-mode witness** so the GREEN (pass-without) leg is actually observed in-matrix rather than skipped:
- **Normal build (in-matrix, `none`/`asan`/`tsan`):** the test runs the teardown-vs-recv-callback scenario and must **complete GREEN** within its hard deadline — this is the in-matrix proof that the GIL release is present and the teardown drains the worker callback.
- **`FIXPP_PY_GIL_RELEASE_CANARY` build (local-only):** the same scenario runs in a subprocess and must **hang (hit the hard timeout) — RED** — the proof the release is load-bearing.

It asserts on a hard timeout (the deadlock is a hang, not a sanitizer report — cf. `[[feedback_sanitizer_canary_must_be_proven_red]]`).

**Why this differs from 053's canary.** `FIXPP_PY_GIL_CANARY` (053, SC-004) elides the **reacquire** in the recv trampoline → the worker touches CPython **without** the GIL → segfault. `FIXPP_PY_GIL_RELEASE_CANARY` (054) elides the **release** in the blocking wrappers → the main thread **holds** the GIL while the worker needs it → deadlock. Two different bugs, two different canaries; 054 adds the second.

**Why a "two threads both send" test does NOT discriminate (the false-green guard).** Without the release a blocking `send` still completes via the C++ worker (the send coroutine needs no Python); it merely *serializes*. The deadlock only manifests when the blocked op's completion **depends on a worker-thread callback acquiring the GIL** — i.e. the teardown-vs-recv-callback race above. A multi-send test is green with or without the release and proves nothing (advisor + `[[feedback_swig_blocking_wrapper_holds_gil_deadlock]]`).

**Local-only + waiver (FR-013/SC-003) — RED leg only.** Only the `FIXPP_PY_GIL_RELEASE_CANARY` build (the RED leg) ships a deliberate deadlock; wiring *that* into the matrix would add a timeout-based hang job, so it stays **local-only**, proven RED, documented, with a CI-automation waiver (the 053 SC-004 canary precedent). The **GREEN leg runs in a normal build and is therefore in-matrix** — the discrimination's "pass-without" half is witnessed every PR, not skipped.

**Staging-determinism caveat (the proof's weak point).** The witness must reliably get an inbound callback **in flight on the worker at the instant of teardown**, AND the teardown drain path must actually **run** the pending callback (not cancel it). If that staging cannot be made deterministic (proven RED 5/5), the canary is "green-under-canary" — which proves nothing — and FR-004's **"document as a non-constructible limitation"** clause applies instead of shipping a flaky stage as proof. Tactics to make it deterministic: have the recv callback block on a `threading.Event` the main thread sets only after confirming entry, so the callback is provably mid-flight when teardown begins; confirm via the 053 evidence that `engine_destroy`/`session_close` drains (runs) rather than cancels pending dispatches.

**Alternatives rejected.** A non-discriminating multi-send test (false-green); CI-wiring the canary (adds a deliberate-hang job, no extra signal over the local proof).

---

## D-3 — Exception-hierarchy realization mechanism (FR-006/FR-008)

**Decision.** Build the `[2m §4.6]` hierarchy + translator in `fixpp.i`:

- **`%init` block** creates the class chain with `PyErr_NewException` parented correctly (root `FixppError`; each block class parented to `FixppError`; `Cancelled`/`Unknown` parented to `CapiError`; the five `BindingError` subclasses + `AppError` parented appropriately), mirroring the existing single-`fixpp.Error` pattern (`fixpp.i:105-109`). `Error` is bound as an **alias** of `FixppError` (`PyModule_AddObject(m, "Error", FixppError)`), so the shipped 053 `fixpp.Error` and `pytest.raises(fixpp.Error)` stay valid.
- **`%pythoncode` block** defines the translator + helpers in the `fixpp` module namespace:
  - `_CODE_TO_NAME`: a **hand-written dict** `{1:"FIXPP_ERR_CANCELLED", …}` (47 raisable codes). **VERIFIED (2026-06-26): the `FIXPP_ERR_*` `#define`s are NOT exposed as Python attributes** — `[n for n in dir(fixpp) if n.startswith('ERR_')]` is empty against `build/linux-clang-debug-py/lib/_fixpp.so`, because SWIG does not constant-fold a cast-to-typedef macro (`#define X ((fixpp_error_t)200)`) into a Python `%constant` (cf. `[[feedback_planning_explore_existence_claims_unreliable]]` — checked against the real module, not assumed). So the name table cannot be introspected; it is a maintained dict, **guarded** by the D-4 coverage test that parses `error.h` (the independent source) and asserts the dict's key set equals the header's code set.
  - `_map_to_class(code) -> type`: the block-range → class map (`[2m §4.6]` "Mapping rule" table); the single source of truth (13 ranges + the `AppError` row, not 48 entries).
  - `_make_error(code) -> FixppError`: constructs the instance, setting `.code` (int), `.name` (str — `_CODE_TO_NAME.get(code, f"FIXPP_ERR_{code}")`, i.e. a **fallback for codes absent from the hand-written dict** so `_make_error` is **total** over its input domain — SC-006's synthetic out-of-range code and FR-009's future-in-known-block code, e.g. `405`, do not `KeyError`), `.message` (str = `fixpp.strerror(code)` — **already exposed**, verified present in the module). **The runtime leg is already safe** independent of this fallback: per `[const §X.4]` (`constitution.md:153`) + `error.h:13-14`, the engine's `translate_for_consumer()` downgrades any code newer than the consumer's registered minor to `FIXPP_ERR_UNKNOWN`(2) *before return*, and 054 is an in-tree same-version static-linked build (consumer-minor == engine-minor), so the out-typemap never hands `_make_error` an out-of-table code at runtime — the fallback is a totality/robustness fix for **direct** translator calls (the SC-006 shape) and the forward-compat L-row, not a runtime-crash fix.
  - `_raise_for_code(code)`: `raise _make_error(code)`.
- **Out-typemap rewire (cross-module routing).** `%typemap(out) fixpp_error_t` calls a C helper `fixpp_py_raise_for_code(code)` (in `%wrapper`) which **lazily imports the `fixpp` proxy module** (`PyImport_ImportModule("fixpp")`, cached in a `static PyObject*`) and calls its `_raise_for_code`, then `SWIG_fail`. The lazy import is required because the wrapper lives in `_fixpp` while `%pythoncode` lands in the `fixpp.py` proxy (the C `%init`-time `g_fixpp_error` pointer cannot reach a Python-defined translator); by the time any wrapped function is called, `fixpp` is fully imported, so the cached import resolves the already-loaded module. **The same translator serves the runtime path and the tests** (FR-008 single source of truth).

**Rationale.** Python-side translator = matches `[2m §4.6]`'s `_map_to_class` naming, trivially testable, and keeps the readable name dict + range map in one place. The one cost is the lazy-import hop (a standard SWIG proxy idiom).

**Alternative deliberately weighed: C-side single source.** Create the classes in `%init` (like 053's `Error`), compute block→class arithmetically in a C registry, raise from C (no cross-module hop) — this is *also* single-source (Python merely re-exports `exception_for_code`). **Rejected** because `.name` (the symbolic string per `[2m §4.6]`) then needs a 48-entry **C** table (`fixpp_strerror` gives the *message*, not the macro name), which is uglier than a Python dict and does not match the ratified `_map_to_class` shape. The lazy-import hop is the smaller cost. (Flagged for Gate A as a deliberate decision, not an implement-time discovery.)

**`fixpp.errors` submodule deferred.** `[2m §4.6]` names `fixpp.errors._map_to_class`; the as-built `fixpp` is a single module (not a package). Creating a `fixpp.errors` submodule needs a package restructure (out of scope / risk for a hardening slice). The as-built exposes `fixpp._map_to_class` + a public `fixpp.exception_for_code(code)` in the module namespace; the `errors` submodule is a documented PY-005-era packaging follow-on. (Flagged for Gate A.)

**Alternatives rejected.** A C-side block→class registry + `PyObject*` array at `%init` (duplicates the mapping in C; "single source" becomes two sources); a per-call Python re-wrap layer over every function (heavy; the typemap already intercepts the return).

---

## D-4 — Exact-mapping coverage (FR-008/SC-002) — non-circular

**Decision.** Because the `ERR_*` constants are **not** exposed (D-3), the coverage test derives its expected set **from `error.h`** (the independent source — parse the `FIXPP_ERR_*` `#define`s for `{code: name}`, ~47 non-OK). It then asserts:
- **(non-vacuous)** `len(expected) == 47` — so the test cannot pass by iterating an empty set (advisor; the same proven-RED discipline as the canary).
- **(class coverage)** for every header code, `_map_to_class(code)` is a `FixppError` subclass **and is NOT the bare `FixppError` fallback** → catches an unmapped block. The `[1400,1499]` codes parse out of the header and hit the fallback until `AppError`'s range is added → **RED**.
- **(name coverage)** `set(_CODE_TO_NAME.keys()) == set(expected.keys())` → catches drift between the maintained name dict and the header.

**Why parse the header (not introspect).** The header is **independent** of the binding's dict/range-map, so the compare is **not circular** (cf. the rejected "two lists both from the SWIG constants"). Parsing is a few lines of regex over a stable `#define` grammar; it is the honest drift source given the constants aren't exposed. The `[1400,1499]` block is the live RED proof.

**Alternatives rejected.** Introspecting `dir(fixpp)` for `ERR_*` (the constants are not exposed — verified; and an empty iteration passes vacuously); a subset check (passes on a deleted/unmapped block).

---

## D-5 — `AppError` for `[1400,1499]` + the `Unknown`/fallback collision (FR-006/FR-009)

**Decision.** Add `class AppError(FixppError)` for the `[1400,1499]` block (session/app + message-construction; 6 codes 1400–1405, 051 D-6). `SessionError` is already `[300,399]` (threading) in `[2m §4.6]`, so the block **cannot** reuse it — `AppError` is the new public name (flagged for Gate A as the single new name 054 introduces). This is an **additive amendment** to the `[2m §4.6]` hierarchy + `§6.7` (Article XX, Gate-A-reviewed).

**Fallback = root `FixppError`, no `UnknownError` class.** A code in a wholly unmapped/future block returns the root `FixppError`. A dedicated `UnknownError` would **collide** with `[2m §4.6]`'s `Unknown(CapiError)` (code 2 — the C-ABI's own `FIXPP_ERR_UNKNOWN` forward-compat downgrade, a `CapiError` subclass). The two concepts are distinct: `Unknown` = a *known code* (2) in the cross-cutting block; the fallback = an *unmapped block*. Using the root as the fallback keeps them cleanly separate. After `AppError` is added, **no populated block** is unmapped — the fallback is forward-compat only (SC-006 exercises it with a synthetic out-of-range code).

**Alternatives rejected.** `SessionError` for 1400 (collision); a `MessageError`/`ProtocolError` name (narrower than the block's session+app+construction mix); a new `UnknownError` fallback (collides with `Unknown`).

---

## D-6 — As-built-050 blocking-send deadlock + the `[2m]` census amendment (FR-005)

**Decision.** Amend the normative `.specify/2m-pybind.md` at **every** site asserting `Session.send()` from inside `fromApp` is legal — the census (advisor; `[[feedback_census_all_handrolled_scanners_before_scoping_parse_fix]]`):

1. **§1.3 rule (2)** (~line 89): "`Session.send()` from inside `fromApp` is legal … the §6.5 carve-out table lists only … `engine.close()` / `session.close()`".
2. **§3.12** (~line 207): "SWIG releases the GIL but the underlying C-ABI thunk dispatches the work onto the session strand".
3. **The §6.5 carve-out table** (the reentrancy "what may a callback call" table).
4. **§4.6 `CallbackReentrantClose` docstring** (~lines 802–804): "Session.send from inside fromApp is NOT banned … only close-from-callback raises this".

**The amendment.** Add `session_send` to the deadlock carve-outs **as a current limitation**, grounded on the **as-built 050** shape: `fixpp_session_send` → `co_spawn(ioc_, …, use_future)` + `fut.get()` (mechanism `src/capi/session.cpp:284-286` (send) / `:202-205` (close); the documented deadlock rule at `session.h:255-258`) **blocks** on the io_context; called from inside the recv trampoline (which runs *on* a worker thread) this is a strand/io_context **reentrancy** deadlock — the worker waits on `fut.get()` for work that can only run on that same worker.

**Two framing constraints.**
- **Distinct deadlock class.** This is a strand/io_context reentrancy deadlock, **distinct** from and **unaffected** by the GIL-teardown deadlock 053 fixed (and by the D-2 GIL release). Both are documented separately.
- **Limitation, not permanent design.** `[2m §4.6]`'s "legal" rested on *non-blocking strand-dispatch*; the as-built diverged to a blocking `use_future`. The amendment is an **L-row** (`L-054-1`) tied to the blocking as-built; restoring true legality (non-blocking send-from-callback) is a deferred **engine** item outside 054's freeze-held scope. We do **not** enshrine "forbidden by design" (`[[feedback_coverage_push_enshrines_bugs]]`).

**Binding guarantee stays documentary** (FR-005). The callback docstring states the hazard (already in `fixpp.i:30-31` from 053); 054 adds the formal `[2m]` amendment. Active detection (`session._in_callback` marker, `CallbackReentrantClose`/1204 raised pre-call) needs the SWIG director — **PY-004**.

**Alternatives rejected.** Amending only §6.5 (leaves §1.3/§3.12/§4.6 self-contradicting); implementing active enforcement now (needs the director = PY-004); recording it as permanent-forbidden (enshrines the as-built workaround).

---

## D-7 — Subprocess-watchdog harness (FR-011/SC-004)

**Decision.** `test_callback_raise_watchdog.py` runs the raising-callback scenario in a **child process** (a helper module executed via `subprocess`/`multiprocessing` with a **hard timeout**) and **re-uses the D-2 teardown-vs-in-flight-callback staging so it actually pins the 053 fix** (a raising callback alone — no concurrent blocking teardown — just runs `PyErr_Print` and returns, so the child exits cleanly **with or without** the GIL release; that does **not** discriminate the `%exception Py_BEGIN/END_ALLOW_THREADS` bands). The discriminating staging:
1. Register a recv callback that **blocks on a `threading.Event`** (set only after the main thread confirms the callback is mid-flight) and then **raises** — so it is provably in flight when teardown begins.
2. Drive an inbound message so the worker enters the callback and blocks on the Event.
3. The main thread enters a **blocking teardown** (`engine_destroy` / `session_close`) and sets the Event.
4. **WITH** the GIL release the worker acquires the GIL, runs (and `PyErr_Print`s) the raising callback, the teardown drains, and the child **exits cleanly within the deadline** → PASS.
5. **WITHOUT** the release (a future edit deletes the `%exception` bands) the main thread holds the GIL in the teardown wait, the worker can never acquire it to run the raising callback, the drain never completes → the child **times out** → FAIL.

A timeout = the engine deadlocked = test FAIL. The parent never blocks unboundedly (the timeout is the backstop). So the watchdog is a real regression guard for the 053 raising-callback-+-concurrent-teardown fix, not a no-op that passes even if the bands are removed.

**Rationale.** A raising callback that deadlocked the engine would otherwise hang the whole pytest process with no culprit; the subprocess + hard timeout names the failure and keeps the in-matrix suite bounded (cf. `[[feedback_ci_hung_test_no_timeout_burns_6h_gdb_capture]]`). This test runs in the matrix (`none`/`asan`/`tsan`) — unlike the deliberate-deadlock canary (D-2), the *expected* outcome here is no-hang.

**Alternatives rejected.** An in-process thread + `join(timeout)` (a hung engine worker can wedge the interpreter / leave the GIL held — the subprocess isolates it); no timeout (CI-hang risk).

---

## D-8 — FR-011 divergence: flat trampoline vs `[2m]` director 1200-translation

**Decision.** 054 keeps the **as-built flat trampoline** behavior on a raising callback: catch + `PyErr_Print` + continue (`fixpp.i:298-300`). It does **not** implement `[2m §3.21/§6.1]`'s director behavior (translate the raised exception to `FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED`/1200, return it to the engine, log via the engine logger).

**Why.** The as-built recv callback is registered via `fixpp_session_register_callback` with a `void(const fixpp_msg_t*, void*)` signature — **no return channel** to hand 1200 back to the engine. The `[2m]` director model has the engine call **into** a Python `Application` director (with return values); 053 deliberately did not adopt the director (it's heavier, and needs the `fixpp.Message` wrapper + lifetime machinery). The `PythonCallbackRaised`/1200 **class** still exists in the FR-006 hierarchy and `1200` still **maps** (FR-008); only the active engine-side translation is deferred to the director slice / PY-004.

**Alternatives rejected.** Adopting the SWIG director now (large scope; pulls in PY-004 lifetime work; not required for the no-deadlock guarantee).

---

## D-9 — Attributes + 053 source compatibility (FR-007)

**Decision.** Per `[2m §4.6]`: `.code` (int), `.name` (str symbolic, e.g. `"FIXPP_ERR_DICT_CONFIG"`), `.message` (str = `fixpp_strerror(code)`, also the exception's `str()` via the constructor arg). `Error = FixppError` alias preserves 053's `pytest.raises(fixpp.Error)` and the string-message assertion in `test_roundtrip.py:182-189` (the message stays the strerror text). The in-typemap conversion failures (str/NUL/UTF-8/bytes) keep raising via the shared bridge, now under `FixppError` (FR-010) — they raise a `BindingError`-adjacent or the root; **decision: route in-typemap conversion failures to the root `FixppError`** (they are binding-layer argument errors, not a C-ABI `fixpp_error_t`; mapping them to a fabricated code would be wrong). Reconfirm 053's existing `test_roundtrip` error-bridge tests stay green under the alias.

**Rationale.** Verbatim `[2m §4.6]` attrs; the alias is the minimal-surface way to honor both the ratified `FixppError` root and the shipped `fixpp.Error`.

**Alternatives rejected.** Renaming `fixpp.Error` away (breaks 053); inventing a code for in-typemap conversion failures (no real `fixpp_error_t` underlies them).

---

## Resolved unknowns

- GIL audit classification — **D-1** (table; as-built verified).
- Discriminating release canary — **D-2** (teardown-vs-callback deadlock; local-only).
- Hierarchy mechanism / single-source translator — **D-3**.
- Non-circular exact-mapping guard — **D-4**.
- `AppError` + fallback/`Unknown` collision — **D-5**.
- `[2m]` census amendment + L-054-1 — **D-6**.
- Subprocess watchdog — **D-7**.
- Director-translation divergence — **D-8**.
- Attrs + 053 compat — **D-9**.

No NEEDS CLARIFICATION outstanding. Gate A reviews: the Article XX `[2m]` amendments (D-6), the `AppError` name + hierarchy extension (D-5), the `fixpp.errors`-submodule deferral (D-3), and the spec-vs-`[2m §4.6]` reconciliation.
