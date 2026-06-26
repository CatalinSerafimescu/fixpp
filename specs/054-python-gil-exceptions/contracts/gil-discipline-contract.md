# Contract — GIL discipline (PY-002)

## G-1 — Release on blocking wrappers (FR-002)

The three wrappers that block on the engine worker/io_context release the GIL around `$action` (`Py_BEGIN/END_ALLOW_THREADS`): `session_close`, `session_send`, `engine_destroy`. The in-typemap (arg conversion, needs GIL) runs before the band; the out-typemap (raises `FixppError`, needs GIL) runs after. No other wrapper releases (E-1 audit; all others are non-blocking/in-memory).

## G-2 — Reacquire on the callback trampoline (FR-003)

The one bound C→Python trampoline (`fixpp_py_recv_trampoline`) reacquires the GIL via `PyGILState_Ensure/Release` before touching Python objects. **Census conclusion:** exactly one bound trampoline; the `toApp`/send callback is unbound (`%ignore`) and no state callback exists → no unbound-trampoline GIL gap.

## G-3 — Audit table (FR-001/SC-007)

The E-1 table classifies every wrapped C-ABI function release/hold with justification, and matches `fixpp.i`. No wrapped function is unclassified.

## G-4 — Discriminating release canary (FR-004/SC-003)

- `-DFIXPP_PY_GIL_RELEASE_CANARY` elides the G-1 release bands.
- Witness (`test_gil_release_canary.py`) is **two-mode**: a **normal build** runs the teardown (`engine_destroy`/`session_close`)-vs-in-flight-recv-callback scenario and must **complete GREEN** (the pass-without-canary leg, in-matrix); a `FIXPP_PY_GIL_RELEASE_CANARY` build runs it in a subprocess and must **hang (RED)** (subprocess hard-timeout assertion).
- A "two threads both send" test does NOT discriminate (serializes, never deadlocks) — explicitly rejected.
- The **GREEN leg is in-matrix** (`none`/`asan`/`tsan`); **only the canary RED leg is local-only** — its CI-automation is waived (053 SC-004 precedent).
- Distinct from 053's `FIXPP_PY_GIL_CANARY` (reacquire canary → segfault).

## G-5 — `[2m]` reentrancy amendment (FR-005) — Article XX

`session_send`-from-inside-callback **will be added** to the `[2m]` deadlock carve-outs at **all four** sites (§1.3 rule 2, §3.12, §6.5 table, §4.6 `CallbackReentrantClose` docstring) at `/implement`; the carve-out shape and the four target sites are ratified at Gate A, grounded on the as-built 050 blocking shape (mechanism `src/capi/session.cpp:284-286` (send) / `:202-205` (close); documented deadlock rule `session.h:255-258`). Recorded as **L-054-1** (a current limitation tied to the blocking as-built; a strand/io_context reentrancy deadlock distinct from the GIL-teardown deadlock), **not** permanent-forbidden. Binding guarantee stays documentary (callback docstring); active enforcement (`session._in_callback` + `CallbackReentrantClose`/1204 pre-call) is PY-004. `AppError` **will also be added** to §4.6/§6.7 at `/implement` (D-5).

## G-6 — Invariants under sanitizers (FR-013/SC-005)

The non-canary suite (typed-exception tests + the watchdog) runs green under the Tier-1 `python-bindings` matrix `none`/`asan`/`tsan`. The deliberate-deadlock canary build is excluded from the matrix.
