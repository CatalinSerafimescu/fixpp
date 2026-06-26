# Contract: SWIG typemaps & trampoline (PY-001)

Defines the SWIG-layer obligations so the round-trip functions are actually usable from Python (not just
compiled). The e2e RED test is the enforcement: a missing/incorrect typemap makes the round-trip fail.

## T-1 — Interface shape: selective, not blanket

- Replace `%include "fix/c_api.h"` (wraps the whole umbrella) with a selective interface: `%include` only
  the headers/declarations the round-trip needs, or keep the umbrella include but `%ignore` everything
  out of scope. The in-scope set is the table in `python-module-surface.md`.
- Bring in `%include "typemaps.i"` for stock `OUTPUT` typemaps.
- **Expose the C-ABI version macros to the SWIG layer** (`%constant`/`%inline` over `version.h:32-33`,
  `FIXPP_C_ABI_VERSION_MAJOR` / `_MINOR`) and provide a thin **`engine_create(cfg)` hand-wrapper** that calls
  the real 4-arg `fixpp_engine_create(cfg, FIXPP_C_ABI_VERSION_MAJOR, FIXPP_C_ABI_VERSION_MINOR, &out)`
  (`engine.h:81-84`). Python callers pass only `cfg`; the wrapper injects the version macros. No `c_api.h`
  change.

## T-2 — Stock `OUTPUT` typemaps (scalar/handle out-params)

Applied via `%apply` to every `**out` handle and scalar out-param, so the C `fixpp_error_t` return is
checked and the out-value becomes the Python return:

- `fixpp_X** out` (dict/engine/engine_config/session/session_config/msg) → returns the opaque proxy.
- `bool* out_established` → returns Python `bool`.
- `uint16_t* port_out` → returns Python `int`.

The wrapped function's `fixpp_error_t` return is consumed by a `%typemap(out)`/exception check (T-5), not
returned to Python.

## T-3 — Hand-written typemaps (the 3–4 that need it)

| Site | `in` / `out` typemap |
|---|---|
| `msg_create_outbound(s, const char* msg_type, size_t len)` | `(char* STRING, size_t LENGTH)` multi-arg `in`: one Python `str` → ptr+len |
| `msg_set_string(m, tag, const char* value, size_t len)` | same `(STRING, LENGTH)` multi-arg `in` |
| `msg_commit(m, const uint8_t** payload, size_t* len)` | `out`: `(payload,len)` → one Python `bytes` (copy) |
| `session_send(s, const uint8_t* frame, size_t len)` | `in`: Python `bytes` → `(frame, len)` |
| `msg_get_string(m, tag, …out buffer…)` | `out`: C out-buffer/len → Python `str` (read exact `get_string` signature at implement time; it has an out-pointer+len shape) |
| **config `const char*` inputs** — `set_comp_ids(sender, target)`, `set_begin_string(s)`, `set_tcp_endpoint(host, port)`, and the `cert`/`key` of `set_security(kind, cert, key)` | `in`: Python `str` → UTF-8-encoded NUL-terminated `const char*`; **reject an embedded NUL** (these are NUL-terminated C inputs, not ptr+len); borrowed for the call only (the C-ABI copies what it needs); a conversion failure raises via the shared error bridge (T-5). `cert`/`key` accept `None` → `NULL` (ignored for the plaintext kind). |

`bytes` is the correct Python type for the committed wire payload (binary, SOH-delimited) — not `str`.
The config `const char*` inputs above are the opposite case: NUL-terminated C strings, so `str` (UTF-8) is
correct and an embedded NUL must be rejected (it would silently truncate the C string).

## T-4 — Callback trampoline (in the `.i` `%{ %}` / `%inline` block)

A single fixed C trampoline, defined where it has the SWIG runtime + type tables:

```c
static void fixpp_py_recv_trampoline(const fixpp_msg_t* inbound, void* userdata) {
    PyGILState_STATE g = PyGILState_Ensure();                 /* FR-007 */
    PyObject* cb = (PyObject*)userdata;                        /* INCREF'd at register */
    PyObject* proxy = SWIG_NewPointerObj(SWIG_as_voidptr(inbound),
                                         SWIGTYPE_p_fixpp_msg_t, 0 /* own=0 */);  /* FR-014 non-owning */
    PyObject* r = PyObject_CallFunctionObjArgs(cb, proxy, NULL);
    Py_XDECREF(r); Py_DECREF(proxy);
    if (PyErr_Occurred()) PyErr_Print();   /* thin: do not propagate into the worker (PY-003 owns policy) */
    PyGILState_Release(g);
}
```

- `session_register_callback`'s `in` typemap: take a Python callable, `Py_INCREF` it, pass
  `fixpp_py_recv_trampoline` as `cb` and the callable as `userdata` (FR-013).
- The `Py_INCREF` is the load-bearing half; for the thin single-callback test the callable is **held until
  interpreter exit**. DECREF-on-reregister / deregister / teardown requires a session-keyed registry and is
  deferred to **PY-004** (consistent with `data-model.md` E-4).
- `SWIGTYPE_p_fixpp_msg_t` is the SWIG type descriptor for `fixpp_msg_t` (present because the type is
  wrapped); confirm the exact descriptor symbol at implement time.

## T-5 — Error → exception bridge (thin)

A `%exception` (or per-call check) converts a non-OK `fixpp_error_t` into `raise fixpp.Error(strerror(code))`.
The check **MUST be scoped to functions whose return is `fixpp_error_t`** — a *blanket* `%exception` applies
to every wrapped function and would misfire on non-`fixpp_error_t` returns: `fixpp_version_string` returns
`const char*` (`c_api.h:57`) and `fixpp_engine_destroy` returns `void` (`engine.h:107`), where comparing
`result != FIXPP_ERR_OK` is a latent type misfire. Scope it via a typed `%exception fixpp_error_t { … }` or
a per-call `%exception <fn> { … }`, not a bare `%exception { … }`:

```c
/* applies ONLY to functions whose result type is fixpp_error_t */
%exception fixpp_error_t {
    $action
    if (result != FIXPP_ERR_OK) {
        SWIG_exception(SWIG_RuntimeError, fixpp_strerror(result));
    }
}
```

- This **reinforces the selective-wrap decision (D-4)**: a blanket `%include` + blanket `%exception` is the
  false-green base; selectively wrapping (or `%ignore`-ing out-of-scope symbols) keeps the error check off
  the `const char*` / `void` returns.
- `fixpp.Error` is a single Python exception type (PY-003 introduces the hierarchy).
- Poll functions that legitimately return a value with `FIXPP_ERR_OK` (`is_established`,
  `acceptor_bound_endpoint`) return that value, not raise.

## T-6 — Build obligations (CMake)

- `_fixpp` MODULE links the **static** `fixpp_capi` archive (PIC) + `-static-libstdc++ -static-libgcc`
  (Linux) + `Python3::Module`. (D-6: safe because nothing C++ crosses `extern "C"`.)
- `fixpp.py` + `_fixpp.so` co-located in `lib/` (already wired); pytest runs with `PYTHONPATH=…/lib`.
- An AddressSanitizer variant of the extension exists for the SC-004 local run (`-fsanitize=address`;
  `ASAN_OPTIONS=detect_leaks=0` when running pytest under it).

## Enforcement

Every row above is exercised by `tests/test_roundtrip.py`. A missing typemap → the corresponding step
raises/returns wrong → the e2e fails. That is the PY-001 definition of done for the binding layer.
