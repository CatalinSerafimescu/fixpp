/* bindings/python/fixpp.i — selective SWIG interface for the PY-001 round-trip.
 *
 * 053-python-thin-binding. Wraps ONLY the ~26 C-ABI functions the loopback FIX
 * round-trip needs (python-module-surface.md), with explicit typemaps for every
 * out-param + a hand-written GIL trampoline for the inbound callback. The
 * deliberately SELECTIVE surface (re-declare in-scope message functions instead
 * of %include-ing the whole message.h) is the D-4 false-green guard: a blanket
 * %include compiles unusable wrappers, so only the e2e test forces the typemaps
 * to actually work.
 *
 * Section map (Spec-Kit tasks):
 *   T005  skeleton: selective wrap, %rename prefix strip, enum + version-macro
 *         exposure, the fixpp.Error type.
 *   T007  stock OUTPUT typemaps for the **out handle / scalar out-params.
 *   T008  engine_create(cfg) hand-wrapper (injects the version macros).
 *   T009  config const char* in typemaps (embedded-NUL reject).
 *   T010  message str<->ptr+len / commit->bytes / send bytes->ptr+len typemaps.
 *   T011  inbound callback trampoline (GIL + Py_INCREF callable + borrowed msg).
 *   T012  error bridge: %typemap(out) fixpp_error_t -> raise fixpp.Error.
 */

%module(docstring="Python binding over the fixpp C ABI (PY-001 + PY-002 GIL + PY-003 typed exceptions).\n\n"
"Threading / GIL contract (PY-002 FR-001 audit / FR-003 trampoline; L-054-1 no-blocking-from-callback):\n"
"  * The inbound callback registered with session_register_callback runs on an\n"
"    engine WORKER THREAD (the session strand); the binding reacquires the GIL\n"
"    before invoking it and releases it after.\n"
"  * The inbound message handed to the callback is a BORROWED, dispatch-window\n"
"    view: read its fields inside the callback (e.g. msg_get_string); do NOT\n"
"    store it or use it after the callback returns.\n"
"  * Do NOT call a blocking API (session_send / session_close) from inside the\n"
"    callback — it deadlocks (FR-013a). Copy the field out and send from another\n"
"    thread.") fixpp

%{
#include <string.h>  /* strlen — embedded-NUL check in the config-str typemaps */
#include "fix/c_api.h"

/* PY-003: the typed-exception hierarchy + the single-source translator live in
 * the fixpp.py proxy (%pythoncode below). The wrapper TU is _fixpp, so BOTH the
 * out-typemap error bridge and the in-typemap conversion-failure path reach them
 * via a cached lazy import of the "fixpp" module (data-model E-3 routing). The
 * import runs only when a wrapped call raises — always under the GIL (the
 * blocking wrappers re-take it before the out-typemap), so the static cache is
 * GIL-protected. */
static PyObject* fixpp_py_module(void) {
    static PyObject* mod = NULL;  /* cached; one deliberate ref held for process life */
    if (mod == NULL) mod = PyImport_ImportModule("fixpp");
    return mod;
}

/* In-typemap conversion failures (FR-010 / T-2 tier b): raise the ROOT
 * fixpp.FixppError with a message ONLY — no .code/.name, no fabricated code
 * (no header code fits an argument-type error). */
static void fixpp_py_raise_root(const char* msg) {
    PyObject* mod = fixpp_py_module();
    if (mod == NULL) return;  /* ImportError already set */
    PyObject* root = PyObject_GetAttrString(mod, "FixppError");
    if (root == NULL) return;
    PyErr_SetString(root, msg);
    Py_DECREF(root);
}

/* Out-typemap error bridge (FR-008 / T-4): delegate a non-OK code to the
 * single-source Python translator fixpp._raise_for_code(code), which raises the
 * block-matching typed subclass carrying .code/.name/.message. No parallel C
 * mapping — the runtime path and the tests share _map_to_class / _CODE_TO_NAME. */
static void fixpp_py_raise_for_code(fixpp_error_t code) {
    PyObject* mod = fixpp_py_module();
    if (mod == NULL) return;  /* ImportError already set */
    PyObject* fn = PyObject_GetAttrString(mod, "_raise_for_code");
    if (fn == NULL) return;
    PyObject* r = PyObject_CallFunction(fn, "i", (int)code);  /* always raises -> NULL */
    Py_XDECREF(r);
    Py_DECREF(fn);
}

/* T011: forward-declare the inbound trampoline so the register_callback in-typemap
 * can reference it. The body (which needs SWIGTYPE_p_fixpp_msg, defined later in
 * the wrapper) lives in the %wrapper block below. */
static void fixpp_py_recv_trampoline(const fixpp_msg_t* inbound, void* userdata);

/* Raise the root fixpp.FixppError from an `in`-typemap conversion failure
 * (contract T-2/T-3 / FR-010). SWIG_fail is `goto fail` inside the wrapper. */
#define FIXPP_PY_RAISE(MSG) do { fixpp_py_raise_root(MSG); SWIG_fail; } while (0)

/* Shared str -> interned UTF-8 buffer for the config-string + message in-typemaps.
 * Returns the buffer (len in *out_len; buffer owned by the str, alive for the
 * call), or NULL on failure with *err set so the caller can raise via the bridge:
 *   *err = 1  -> not a str           (caller raises fixpp.Error)
 *   *err = 2  -> embedded NUL        (only when reject_nul; caller raises)
 *   *err = 0  -> a Python error is already set (encoding) -> caller SWIG_fails.
 * reject_nul=1 for NUL-terminated config strings, 0 for ptr+len payloads. */
static const char* fixpp_py_str_utf8(PyObject* o, Py_ssize_t* out_len,
                                     int reject_nul, int* err) {
    if (!PyUnicode_Check(o)) { *err = 1; return NULL; }
    Py_ssize_t n = 0;
    const char* s = PyUnicode_AsUTF8AndSize(o, &n);
    if (s == NULL) { *err = 0; return NULL; }
    if (reject_nul && (Py_ssize_t)strlen(s) != n) { *err = 2; return NULL; }
    *out_len = n;
    *err = -1;
    return s;
}
%}

/* The export-visibility macro is meaningless to the wrapper TU (it links the
 * static archive); strip it so SWIG parses the prototypes cleanly. */
#define FIXPP_API_EXPORT

%include "stdint.i"
%include "typemaps.i"   /* stock OUTPUT typemaps for bool / unsigned short (T007) */

/* ── Prefix stripping — the Python surface uses short names ──────────────────
 * fixpp_session_open -> session_open ; FIXPP_ROLE_ACCEPTOR -> ROLE_ACCEPTOR ;
 * FIXPP_C_ABI_VERSION_MINOR -> C_ABI_VERSION_MINOR. A SINGLE regex rule strips
 * EITHER prefix — two blanket `%(strip:..)s` rules do not stack (the later one
 * overrides the earlier, leaving the lowercase `fixpp_` functions un-stripped).
 * Capture-group form (empty-substitution `/…//` silently no-ops in PCRE2). */
%rename("%(regex:/^(fixpp_|FIXPP_)(.*)/\\2/)s") "";

/* ── Out of scope (python-module-surface.md §Out of scope) ───────────────────
 * Drop the toApp send callback + the bare version accessors/descriptor; the
 * round-trip needs neither, and keeping fixpp_version() out avoids a name clash
 * with the fixpp_version struct under the prefix strip. The version *macros*
 * (consumed by the engine_create wrapper, T008) stay. */
%ignore fixpp_session_register_send_callback;
%ignore fixpp_version;
%ignore fixpp_library_version;
/* The raw 4-arg fixpp_engine_create is replaced by the 1-arg engine_create
 * hand-wrapper (T008) that injects the C-ABI version macros. */
%ignore fixpp_engine_create;
/* The (fixpp_recv_cb cb, void* userdata) variant is replaced by the
 * fixpp_py_register_callback hand-wrapper (below) that INCREFs the callable
 * only after the native call returns FIXPP_ERR_OK (Fix 1 / RC-A). */
%ignore fixpp_session_register_callback;

/* ── PY-003: typed exception hierarchy + single-source translator ────────────
 * [2m §4.6] verbatim (+ AppError for the post-2m [1400,1499] block, D-5),
 * realized as pure-Python classes in the fixpp.py proxy. The C wrapper reaches
 * _raise_for_code (out-typemap, FR-008) and FixppError (in-typemap, FR-010) via
 * fixpp_py_module() above. See contracts/python-exception-surface.md (T-1..T-5)
 * and data-model E-2/E-3. */
%pythoncode %{
class FixppError(Exception):
    """Root of the fixpp typed-exception hierarchy ([2m §4.6])."""

# Alias — the shipped 053 `fixpp.Error` surface (and pytest.raises(fixpp.Error)) survives.
Error = FixppError

# Cross-cutting C-ABI block [0,99] (Cancelled/1, Unknown/2 are sub-blocks).
class CapiError(FixppError): pass
class Cancelled(CapiError): pass
class Unknown(CapiError): pass            # FIXPP_ERR_UNKNOWN/2 — NOT the unmapped-block fallback
# One subclass per fixpp_error_t block.
class ParseError(FixppError): pass        # [100,199] wire
class ValidatorError(FixppError): pass    # [200,299] dict
class SessionError(FixppError): pass      # [300,399] threading
class StoreError(FixppError): pass        # [400,499]
class SyncError(FixppError): pass         # [500,599]
class TlsError(FixppError): pass          # [600,699]
class TransportError(FixppError): pass    # [700,799]
class DecimalError(FixppError): pass      # [800,899]
class ControlPlaneError(FixppError): pass # [900,999]
class LogError(FixppError): pass          # [1000,1099] reserved
class TapError(FixppError): pass          # [1100,1199] reserved
class BindingError(FixppError): pass      # [1200,1299]
class PythonCallbackRaised(BindingError): pass      # 1200
class SubInterpreterRejected(BindingError): pass    # 1201
class ObjectLifetime(BindingError): pass            # 1202
class WheelAbiMismatch(BindingError): pass          # 1203
class CallbackReentrantClose(BindingError): pass    # 1204
class AppError(FixppError): pass          # [1400,1499] NEW in 054 (post-2m block; 051 D-6)

# Hand-written code -> symbolic name (the FIXPP_ERR_* #defines are NOT SWIG-exposed,
# D-3; guarded by the header-sourced set-equality coverage test, T-5). 47 entries.
_CODE_TO_NAME = {
    1: "FIXPP_ERR_CANCELLED",
    2: "FIXPP_ERR_UNKNOWN",
    3: "FIXPP_ERR_NULL_HANDLE",
    4: "FIXPP_ERR_INVALID_HANDLE",
    5: "FIXPP_ERR_VERSION_MISMATCH",
    6: "FIXPP_ERR_BUFFER_TOO_SMALL",
    7: "FIXPP_ERR_TYPE_MISMATCH",
    8: "FIXPP_ERR_TAG_NOT_FOUND",
    9: "FIXPP_ERR_INDEX_OUT_OF_RANGE",
    10: "FIXPP_ERR_CAPI_CONFIG_INVALID",
    100: "FIXPP_ERR_WIRE_INVALID_FRAME",
    101: "FIXPP_ERR_WIRE_LIMIT_EXCEEDED",
    102: "FIXPP_ERR_WIRE_CONFORMANCE",
    200: "FIXPP_ERR_DICT_CONFIG",
    201: "FIXPP_ERR_DICT_LIMIT_EXCEEDED",
    202: "FIXPP_ERR_DICT_OOM",
    300: "FIXPP_ERR_THREAD_CONFIG",
    301: "FIXPP_ERR_THREAD_SESSION_LIFECYCLE",
    302: "FIXPP_ERR_THREAD_RUNTIME",
    400: "FIXPP_ERR_STORE_RUNTIME",
    401: "FIXPP_ERR_STORE_CONSISTENCY",
    402: "FIXPP_ERR_STORE_CONFIG",
    403: "FIXPP_ERR_STORE_VISITOR",
    500: "FIXPP_ERR_SYNC_RUNTIME",
    600: "FIXPP_ERR_TLS_CONFIG",
    601: "FIXPP_ERR_TLS_HANDSHAKE",
    602: "FIXPP_ERR_TLS_PINSET",
    603: "FIXPP_ERR_TLS_RUNTIME",
    700: "FIXPP_ERR_TRANSPORT_LIFECYCLE",
    701: "FIXPP_ERR_TRANSPORT_IO",
    702: "FIXPP_ERR_TRANSPORT_HANDSHAKE",
    703: "FIXPP_ERR_TRANSPORT_CONFIG",
    800: "FIXPP_ERR_DECIMAL_INVALID",
    801: "FIXPP_ERR_DECIMAL_PRECISION_LOSS",
    900: "FIXPP_ERR_CTRL_CONFIG",
    901: "FIXPP_ERR_CTRL_RUNTIME",
    1200: "FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED",
    1201: "FIXPP_ERR_BINDING_SUBINTERPRETER",
    1202: "FIXPP_ERR_BINDING_OBJECT_LIFETIME",
    1203: "FIXPP_ERR_BINDING_WHEEL_ABI_MISMATCH",
    1204: "FIXPP_ERR_BINDING_CALLBACK_REENTRANT_CLOSE",
    1400: "FIXPP_ERR_SESSION_INVALID_ARGUMENT",
    1401: "FIXPP_ERR_SESSION_INVALID_STATE",
    1402: "FIXPP_ERR_APP_DO_NOT_SEND",
    1403: "FIXPP_ERR_APP_CALLBACK_THREW",
    1404: "FIXPP_ERR_APP_PAYLOAD_MALFORMED",
    1405: "FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN",
}

def _map_to_class(code):
    """Single source of truth: fixpp_error_t -> typed subclass (E-2 block map).

    A code in a known populated block -> that block's class (an unrecognized value
    within a known block still maps to the block class, FR-009). A code in a wholly
    unmapped/future block -> the root FixppError fallback (no UnknownError class —
    it would collide with Unknown/2). The runtime out-typemap never hands an
    out-of-table code here: the [2i §4.4] downgrade collapses a future code to
    FIXPP_ERR_UNKNOWN(2) first; this fallback is the direct-call (SC-006) /
    forward-compat path."""
    if code == 1: return Cancelled
    if code == 2: return Unknown
    if 3 <= code <= 99: return CapiError
    if 100 <= code <= 199: return ParseError
    if 200 <= code <= 299: return ValidatorError
    if 300 <= code <= 399: return SessionError
    if 400 <= code <= 499: return StoreError
    if 500 <= code <= 599: return SyncError
    if 600 <= code <= 699: return TlsError
    if 700 <= code <= 799: return TransportError
    if 800 <= code <= 899: return DecimalError
    if 900 <= code <= 999: return ControlPlaneError
    if 1000 <= code <= 1099: return LogError
    if 1100 <= code <= 1199: return TapError
    if code == 1200: return PythonCallbackRaised
    if code == 1201: return SubInterpreterRejected
    if code == 1202: return ObjectLifetime
    if code == 1203: return WheelAbiMismatch
    if code == 1204: return CallbackReentrantClose
    if 1205 <= code <= 1299: return BindingError
    if 1400 <= code <= 1499: return AppError
    return FixppError  # unmapped/future block (FR-009 forward-compat fallback)

# Public alias so callers/tests can introspect the mapping (FR-008).
exception_for_code = _map_to_class

def _make_error(code):
    """Construct the typed instance carrying .code/.name/.message (FR-007).

    .name falls back to f"FIXPP_ERR_{code}" for a code absent from _CODE_TO_NAME
    (SC-006 synthetic / FR-009 future-in-known-block) so it is total over its
    input domain (never KeyError/None)."""
    cls = _map_to_class(code)
    name = _CODE_TO_NAME.get(code, "FIXPP_ERR_%d" % code)
    message = strerror(code)
    exc = cls(message)
    exc.code = code
    exc.name = name
    exc.message = message
    return exc

def _raise_for_code(code):
    raise _make_error(code)
%}

/* ════════════════════════════════════════════════════════════════════════════
 * TYPEMAPS — must precede the declarations they apply to.
 * ════════════════════════════════════════════════════════════════════════════ */

/* ── T012: error bridge — fixpp_error_t -> raise fixpp.Error / consume code ───
 * contracts T-5. TYPE-scoped (matched by the fixpp_error_t return), so it never
 * misfires on fixpp_version_string (const char*) or the *_destroy (void) symbols
 * — the selective-wrap decision (D-4) keeps those out of this typemap's reach.
 * Defined FIRST so it also covers the T008 %inline engine_create wrapper (SWIG
 * applies a typemap only to declarations parsed after it). On OK the code is
 * consumed (-> None) so the argout out-param(s) become the Python return; on
 * non-OK it raises a single fixpp.Error(fixpp_strerror(code)). Poll fns
 * (is_established / acceptor_bound_endpoint) return OK + their value. */
%typemap(out) fixpp_error_t {
    if ($1 != FIXPP_ERR_OK) {
        /* PY-003 (FR-008): route through the single-source Python translator so
         * the block-matching typed subclass (with .code/.name/.message) is raised
         * — NOT a flat fixpp.Error. The GIL is held here (the blocking wrappers
         * re-take it before this out-typemap runs). */
        fixpp_py_raise_for_code($1);
        SWIG_fail;
    }
    $result = SWIG_Py_Void();
}

/* ── T007: OUTPUT typemaps for **out handles + scalar out-params ─────────────
 * Each fallible function takes a trailing out-param and returns fixpp_error_t.
 * The out-param is suppressed as a Python input (numinputs=0), filled by the
 * call, and appended to the result; the fixpp_error_t return is consumed by the
 * T012 error bridge so the out-value becomes the (sole) Python return. */

/* Opaque handle out-params: TYPE** out -> a non-owning proxy (own=0; the C-ABI
 * owns the lifetime, released via the matching *_destroy / *_close). */
%define %FIXPP_HANDLE_OUT(TYPE)
%typemap(in, numinputs=0) TYPE** (TYPE* temp = NULL) { $1 = &temp; }
%typemap(argout) TYPE** {
    $result = SWIG_AppendOutput($result,
        SWIG_NewPointerObj(SWIG_as_voidptr(*$1), $descriptor(TYPE*), 0));
}
%enddef
%FIXPP_HANDLE_OUT(fixpp_dict_t)
%FIXPP_HANDLE_OUT(fixpp_engine_t)
%FIXPP_HANDLE_OUT(fixpp_engine_config_t)
%FIXPP_HANDLE_OUT(fixpp_session_t)
%FIXPP_HANDLE_OUT(fixpp_session_config_t)
%FIXPP_HANDLE_OUT(fixpp_msg_t)

/* Scalar out-params -> Python bool / int via the stock typemaps.i OUTPUT maps
 * (uint16_t resolves to unsigned short through stdint.i). */
%apply bool *OUTPUT          { bool* out_established };
%apply unsigned short *OUTPUT { uint16_t* port_out };

/* ── T009: config const char* in-typemaps (NUL-terminated, reject inner NUL) ─
 * These are NUL-terminated C inputs (not ptr+len), borrowed for the call only
 * (the C-ABI copies what it keeps). Python str -> UTF-8; an embedded NUL is
 * REJECTED (it would silently truncate the C string). PyUnicode_AsUTF8AndSize
 * returns an interned buffer owned by the str (alive for the call), so no free. */
%typemap(in) const char* FIXPP_CONFIG_STR {
    Py_ssize_t _n = 0; int _err = 0;
    const char* _s = fixpp_py_str_utf8($input, &_n, 1 /* reject_nul */, &_err);
    if (_s == NULL) {
        if (_err == 1) FIXPP_PY_RAISE("in '$symname': '$1_name' must be a str");
        if (_err == 2) FIXPP_PY_RAISE("in '$symname': '$1_name' must not contain an embedded NUL");
        /* _err == 0: PyUnicode_AsUTF8AndSize failed (e.g. lone surrogate). Clear
         * the bare codec exception and route through fixpp.Error (T-3 / FR-008). */
        PyErr_Clear();
        FIXPP_PY_RAISE("in '$symname': '$1_name' must be valid UTF-8 (no surrogate characters)");
    }
    $1 = (char*)_s;
}
/* cert / key: same, but Python None -> NULL (ignored for the plaintext kind). */
%typemap(in) const char* FIXPP_CONFIG_STR_OR_NONE {
    if ($input == Py_None) {
        $1 = NULL;
    } else {
        Py_ssize_t _n = 0; int _err = 0;
        const char* _s = fixpp_py_str_utf8($input, &_n, 1 /* reject_nul */, &_err);
        if (_s == NULL) {
            if (_err == 1) FIXPP_PY_RAISE("in '$symname': '$1_name' must be str or None");
            if (_err == 2) FIXPP_PY_RAISE("in '$symname': '$1_name' must not contain an embedded NUL");
            /* _err == 0: codec failure — same bridge as FIXPP_CONFIG_STR above. */
            PyErr_Clear();
            FIXPP_PY_RAISE("in '$symname': '$1_name' must be valid UTF-8 (no surrogate characters)");
        }
        $1 = (char*)_s;
    }
}
%apply const char* FIXPP_CONFIG_STR {
    const char* sender, const char* target, const char* begin_string, const char* host };
%apply const char* FIXPP_CONFIG_STR_OR_NONE { const char* cert, const char* key };

/* ── T010: message typemaps (str<->ptr+len, commit->bytes, send bytes) ───────
 * data-model E-5. These use ptr+len semantics (NOT NUL-terminated), so an
 * embedded NUL is legal payload here, unlike the T009 config strings. */

/* str -> (ptr, len) for the outbound builder's MsgType + string value. UTF-8;
 * the buffer is interned in the str (alive for the call; the C-ABI deep-copies). */
%typemap(in) (const char* FIXPP_STRPTR, size_t FIXPP_STRLEN) {
    Py_ssize_t _n = 0; int _err = 0;
    const char* _s = fixpp_py_str_utf8($input, &_n, 0 /* allow inner NUL: ptr+len */, &_err);
    if (_s == NULL) {
        if (_err == 1) FIXPP_PY_RAISE("in '$symname': expected a str");
        /* _err == 0: codec failure (ptr+len path; embedded NUL is allowed here). */
        PyErr_Clear();
        FIXPP_PY_RAISE("in '$symname': string must be valid UTF-8 (no surrogate characters)");
    }
    $1 = (char*)_s;
    $2 = (size_t)_n;
}
%apply (const char* FIXPP_STRPTR, size_t FIXPP_STRLEN) {
    (const char* msg_type, size_t msg_type_len),
    (const char* value, size_t len) };

/* Python bytes -> (frame, len) for session_send (borrowed; engine deep-copies). */
%typemap(in) (const uint8_t* frame, size_t len) {
    if (!PyBytes_Check($input)) {
        FIXPP_PY_RAISE("in '$symname': 'frame' must be bytes");
    }
    char* _b = NULL;
    Py_ssize_t _n = 0;
    if (PyBytes_AsStringAndSize($input, &_b, &_n) != 0) SWIG_fail;
    $1 = (uint8_t*)_b;   /* SWIG strips the pointee const from the arg local */
    $2 = (size_t)_n;
}

/* commit: (payload_out, len_out) -> one Python bytes (copies the arena span). */
%typemap(in, numinputs=0) (const uint8_t** payload_out, size_t* len_out)
        (uint8_t* _p = NULL, size_t _n = 0) {   /* SWIG arg local is uint8_t** */
    $1 = &_p;
    $2 = &_n;
}
%typemap(argout) (const uint8_t** payload_out, size_t* len_out) {
    $result = SWIG_AppendOutput($result,
        PyBytes_FromStringAndSize((const char*)(*$1), (Py_ssize_t)(*$2)));
}

/* msg_get_string: (value_out, len_out) -> one Python str (UTF-8 decode of the
 * aliased wire bytes; read in-window per FR-014). */
%typemap(in, numinputs=0) (const char** value_out, size_t* len_out)
        (char* _p = NULL, size_t _n = 0) {   /* SWIG arg local is char** */
    $1 = &_p;
    $2 = &_n;
}
%typemap(argout) (const char** value_out, size_t* len_out) {
    PyObject* _str = PyUnicode_FromStringAndSize(*$1, (Py_ssize_t)(*$2));
    if (_str == NULL) {
        /* Wire bytes not valid UTF-8 (P3: unlikely on a conforming peer, but
         * a non-NULL result from PyUnicode_FromStringAndSize is not guaranteed).
         * Route through fixpp.Error (T-3 / FR-008). */
        PyErr_Clear();
        FIXPP_PY_RAISE("msg_get_string: field value is not valid UTF-8");
    }
    $result = SWIG_AppendOutput($result, _str);
}

/* ── T011: inbound callback trampoline (GIL + Py_INCREF + borrowed msg) ──────
 * contracts T-4 / data-model E-3/E-4. The trampoline runs on a fixpp worker
 * thread (the session strand) that does not hold the GIL.
 *   1. GIL (FR-007): PyGILState_Ensure/Release around every Python touch.
 *   2. Callable lifetime (FR-013): the in-typemap Py_INCREFs the callable (held
 *      until interpreter exit; DECREF-on-reregister / registry = PY-004).
 *   3. Borrowed msg (FR-014): the const fixpp_msg_t* is wrapped NON-owning
 *      (own=0) and is valid only for the dispatch window — the Python callback
 *      must read in-window (L-053-1; no active post-window guard until PY-004). */
%wrapper %{
static void fixpp_py_recv_trampoline(const fixpp_msg_t* inbound, void* userdata) {
    /* FIXPP_PY_GIL_CANARY: define at compile-time to ELIDE the GIL acquire /
     * release.  With the canary active, the worker touches CPython without the
     * GIL and the TSan loopback leg goes RED — empirically a reproducible
     * `Fatal Python error: Segmentation fault` (a crash, NOT a TSan data-race
     * report), reliably 5/5 and independent of tsan_suppressions.txt (a segfault
     * is unsuppressible).  This proves the SC-004 TSan gate goes RED on a real
     * missing-GIL bug.  Canary instructions: configure with
     * -DFIXPP_PY_GIL_CANARY=ON, build the TSan preset, then run test_roundtrip
     * under TSan and confirm the RED crash.  DO NOT define in production / CI. */
#ifndef FIXPP_PY_GIL_CANARY
    PyGILState_STATE gil = PyGILState_Ensure();
#endif
    PyObject* cb = (PyObject*)userdata;                       /* INCREF'd at register */
    PyObject* proxy = SWIG_NewPointerObj(SWIG_as_voidptr((void*)inbound),
                                         SWIGTYPE_p_fixpp_msg, 0 /* own=0, non-owning */);
    if (proxy != NULL) {
        PyObject* r = PyObject_CallFunctionObjArgs(cb, proxy, NULL);
        Py_XDECREF(r);
        Py_DECREF(proxy);
    }
    /* Thin: surface but do not propagate a Python exception into the worker
     * (PY-003 owns the propagation policy). */
    if (PyErr_Occurred()) PyErr_Print();
#ifndef FIXPP_PY_GIL_CANARY
    PyGILState_Release(gil);
#endif
}
%}

/* T011: pass-through typemap for the hand-wrapper's callable argument.
 * The callable-check lives here (FIXPP_PY_RAISE fires before $action if the
 * check fails, so the wrapper body never runs and no INCREF occurs).
 * The INCREF itself is deferred to the wrapper body so it happens only after
 * fixpp_session_register_callback returns FIXPP_ERR_OK. */
%typemap(in) PyObject* py_callable {
    if (!PyCallable_Check($input)) {
        FIXPP_PY_RAISE("in 'session_register_callback': the callback must be callable");
    }
    $1 = $input;  /* pass through; wrapper body manages INCREF on success only */
}

/* FR-006 docstring: the threading/GIL contract on the public registration fn. */
%feature("docstring") fixpp_py_register_callback
"session_register_callback(session, callable) -> None\n\n"
"Register the inbound receive callback (MUST be called before engine_start).\n"
"`callable` is invoked as callable(inbound_msg) on an engine worker thread with\n"
"the GIL reacquired by the binding. `inbound_msg` is a borrowed, dispatch-window\n"
"view: read it inside the callback (msg_get_string); do not store it. Do NOT call\n"
"a blocking API (session_send / session_close) from inside the callback (FR-013a:\n"
"deadlock). The callable is held alive until interpreter exit (PY-004 adds the\n"
"deregistration / refcount-release registry).";

/* Hand-wrapper: INCREF only after FIXPP_ERR_OK (Fix 1 / RC-A Gate-B r1).
 * The callable check is in the %typemap(in) PyObject* py_callable above.
 * The T012 fixpp_error_t out-typemap raises fixpp.Error on non-OK return. */
%rename("session_register_callback") fixpp_py_register_callback;
%inline %{
static fixpp_error_t fixpp_py_register_callback(
        fixpp_session_t* session, PyObject* py_callable) {
    /* Callable check done in typemap; py_callable is a valid callable here. */
    Py_INCREF(py_callable);
    fixpp_error_t err = fixpp_session_register_callback(
        session, fixpp_py_recv_trampoline, (void*)py_callable);
    if (err != FIXPP_ERR_OK) {
        /* Native refused (e.g. post-start registration).  Release the ref we
         * just acquired — the C-ABI never stored it (session.cpp:311-313
         * returns before slot->userdata = userdata at 314-317). */
        Py_DECREF(py_callable);
    }
    return err;
}
%}

/* ── T008: engine_create(cfg) hand-wrapper (injects the version macros) ──────
 * The real symbol is the 4-arg fixpp_engine_create(cfg, major, minor, &out)
 * (engine.h:81) — it records the consumer's ABI minor for the forward-compat
 * downgrade. Python callers pass only cfg; this thin wrapper supplies the
 * compile-time FIXPP_C_ABI_VERSION_{MAJOR,MINOR}. The fixpp_engine_t** out
 * uses the T007 HANDLE_OUT typemap; %rename forces the short Python name
 * (the global regex strip would otherwise yield `py_engine_create`). */
%rename("engine_create") fixpp_py_engine_create;
%inline %{
static fixpp_error_t fixpp_py_engine_create(fixpp_engine_config_t* cfg,
                                            fixpp_engine_t** out_engine) {
    return fixpp_engine_create(cfg, FIXPP_C_ABI_VERSION_MAJOR,
                               FIXPP_C_ABI_VERSION_MINOR, out_engine);
}
%}

/* ════════════════════════════════════════════════════════════════════════════
 * PY-002 GIL-DISCIPLINE AUDIT TABLE (FR-001 / SC-007) — data-model E-1
 * ════════════════════════════════════════════════════════════════════════════
 * EXHAUSTIVE over the %include'd surface of error.h/handles.h/version.h/dict.h/
 * engine.h/session.h + the re-declared message.h functions below — NOT a grouped
 * sample. "release" = Py_BEGIN/END_ALLOW_THREADS around $action; "hold" = GIL
 * retained. A reviewer can mechanically diff this against the %exception blocks
 * (only the three release rows have one) and the wrapped declarations.
 *
 *   Wrapped C-ABI function          | Class   | Why
 *   --------------------------------|---------|------------------------------------
 *   session_close                   | release | co_spawn(close_exec,…,use_future)+fut.get() — strand drain
 *   session_send                    | release | co_spawn(ioc_,…,use_future)+fut.get() — worker run
 *                                   |         |   (mechanism src/capi/session.cpp:284-286; rule session.h:255-258)
 *   engine_destroy                  | release | stop_fut.get() + worker joins
 *   engine_create                   | hold    | construct + worker spawn; no round-trip
 *   engine_start                    | hold    | starts workers; returns immediately
 *   engine_config_create            | hold    | construction-time; returns fixpp_error_t, no engine round-trip
 *   session_config_create           | hold    | construction-time; returns fixpp_error_t, no engine round-trip
 *   engine_config_destroy           | hold    | in-memory free; void-returning — never reaches the error bridge
 *   session_config_destroy          | hold    | in-memory free; void-returning — never reaches the error bridge
 *   session_open                    | hold    | in-memory state mutation
 *   session_is_established          | hold    | poll
 *   acceptor_bound_endpoint         | hold    | registry read
 *   session_register_callback       | hold    | registry write + INCREF (fixpp_py_register_callback)
 *   all *_config_set_* setters      | hold    | in-memory
 *   dict_load_from_xml              | hold    | sync file/XML parse; CPU-bound, NO engine round-trip ->
 *                                   |         |   no worker-deadlock class (releasing for throughput = deferred nicety, not a gap)
 *   dict_destroy                    | hold    | in-memory
 *   all msg_* (create/set/commit/   | hold    | in-memory arena ops
 *     get/destroy)                  |         |
 *   version_string, strerror        | hold    | pure function
 *
 * BOUND C->Python TRAMPOLINE CENSUS (FR-003): exactly ONE —
 *   fixpp_py_recv_trampoline (recv callback), already GIL-correct
 *   (PyGILState_Ensure/Release, see %wrapper above). The toApp/send callback
 *   (fixpp_session_register_send_callback) is %ignore'd (UNBOUND); no
 *   establishment/state callback exists in the C-ABI. Conclusion: 1 bound
 *   trampoline; no unbound trampoline is a GIL gap.
 *
 * The three release wrappers below carry a %exception band. The %typemap(in)
 * (borrows Python buffers / converts args — needs GIL) runs BEFORE the band; the
 * %typemap(out) fixpp_error_t (routes through the Python translator — needs GIL)
 * runs AFTER it; both are outside $action in the generated wrapper.
 *
 * FIXPP_PY_GIL_RELEASE_CANARY (FR-004, local-only): when defined, the
 * Py_BEGIN/END_ALLOW_THREADS bands below are ELIDED so the worker cannot acquire
 * the GIL while the main thread blocks in a teardown -> the teardown-vs-in-flight-
 * recv-callback scenario DEADLOCKS (RED, hard-timeout). Distinct from 053's
 * FIXPP_PY_GIL_CANARY (the reacquire canary -> segfault); both coexist. Never set
 * in CI — the deliberate-hang build is local-only.
 *
 * session_send borrowed-buffer safety: Engine::send() deep-copies the span at
 * the coroutine body (src/session/engine.cpp), which runs on the worker thread
 * after co_spawn. The Python bytes object is kept alive by the caller's frame
 * reference (the `payload` variable) for the entire duration of fut.get(),
 * preventing collection even when the GIL is released. */
/* PY-002 GIL-RELEASE bands (FR-002/FR-004) — defined in a VERBATIM %wrapper
 * block so SWIG passes the #ifdef through to the generated .cxx untouched (a
 * bare #ifndef inside the %exception bodies is consumed by SWIG's own
 * preprocessor; and a #define in the %{ %} header block is RESOLVED by SWIG at
 * swig-time, both of which defeat the C-compile-time -D canary). The C compiler
 * expands these at the %exception use site. Under -DFIXPP_PY_GIL_RELEASE_CANARY
 * the bands are empty, so the three blocking wrappers DO NOT release the GIL ->
 * the teardown-vs-in-flight-callback scenario deadlocks (local-only RED witness,
 * data-model E-4). Mirrors the 053 FIXPP_PY_GIL_CANARY %wrapper pattern. */
%wrapper %{
#ifdef FIXPP_PY_GIL_RELEASE_CANARY
#  define FIXPP_PY_GIL_RELEASE_BEGIN
#  define FIXPP_PY_GIL_RELEASE_END
#else
#  define FIXPP_PY_GIL_RELEASE_BEGIN  Py_BEGIN_ALLOW_THREADS
#  define FIXPP_PY_GIL_RELEASE_END    Py_END_ALLOW_THREADS
#endif
%}

%exception fixpp_session_close {
    FIXPP_PY_GIL_RELEASE_BEGIN;
    $action;
    FIXPP_PY_GIL_RELEASE_END;
}
%exception fixpp_session_send {
    FIXPP_PY_GIL_RELEASE_BEGIN;
    $action;
    FIXPP_PY_GIL_RELEASE_END;
}
%exception fixpp_engine_destroy {
    FIXPP_PY_GIL_RELEASE_BEGIN;
    $action;
    FIXPP_PY_GIL_RELEASE_END;
}

/* ── Wrapped declarations (selective) ───────────────────────────────────────
 * %include only the headers whose entire (or all-but-ignored) surface is in
 * scope; message.h is wrapped by re-declaration (5 of ~30 functions). */
%include "fix/c_api/error.h"    /* fixpp_error_t (int32) + codes */
%include "fix/c_api/handles.h"  /* opaque handle typedefs */
%include "fix/c_api/version.h"  /* FIXPP_C_ABI_VERSION_* macros */
%include "fix/c_api/dict.h"     /* dict load / destroy (both in scope) */
%include "fix/c_api/engine.h"   /* engine + engine-config (all in scope) */
%include "fix/c_api/session.h"  /* session lifecycle + session-config builder */

/* message.h — re-declare ONLY the in-scope outbound-build + read functions.
 * Groups, typed getters/setters, clone, field iteration are deferred. */
fixpp_error_t fixpp_msg_create_outbound(fixpp_session_t* session,
                                        const char* msg_type, size_t msg_type_len,
                                        fixpp_msg_t** msg_out);
fixpp_error_t fixpp_msg_set_string(fixpp_msg_t* msg, uint16_t tag,
                                   const char* value, size_t len);
fixpp_error_t fixpp_msg_commit(fixpp_msg_t* msg, const uint8_t** payload_out,
                               size_t* len_out);
fixpp_error_t fixpp_msg_destroy(fixpp_msg_t* msg);
fixpp_error_t fixpp_msg_get_string(const fixpp_msg_t* msg, uint16_t tag,
                                   const char** value_out, size_t* len_out);

/* The umbrella library-version string (existing smoke surface). */
const char* fixpp_version_string(void);
