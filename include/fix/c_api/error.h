/*
 * include/fix/c_api/error.h — fixpp_error_t type + published error codes (E-2)
 *
 * C-clean: no C++ headers, no C++ syntax.
 * Thread-safety class (E-5): not a function; passive type/constant declarations.
 *
 * ABI contract ([2i §4.3]):
 *   - typedef int32_t fixpp_error_t  (NOT a C enum; storage size is ABI-stable).
 *   - Constants via #define so C89 compilers can use them without casts.
 *   - Once C-ABI MAJOR==1, a published slot never changes meaning.
 *   - All codes introduced in C-ABI 0.2.0 have introducing_minor=2.
 *     (Used by translate_for_consumer() forward-compat downgrade, E-5.)
 *
 * Published code space (data-model.md E-2):
 *   Cross-cutting  0–99   | Wire      100–199 | Dict      200–299
 *   Threading    300–399  | Store     400–499  | Sync      500–599
 *   TLS          600–699  | Transport 700–799  | Decimal   800–899
 *   Control-plane 900–999 | Log+OTel 1000–1099 (reserved; no #define yet)
 *   Tap         1100–1199 (reserved)           | Bindings 1200–1299
 *
 * Exported function:
 *   fixpp_strerror(fixpp_error_t) — static const char*, zero alloc, non-null.
 */

#ifndef FIXPP_C_API_ERROR_H
#define FIXPP_C_API_ERROR_H

/* NOLINT(hicpp-deprecated-headers,modernize-deprecated-headers): C-ABI header */
#include <stdint.h>  /* NOLINT(hicpp-deprecated-headers,modernize-deprecated-headers) */

#include "fix/c_api/export.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Underlying type ─────────────────────────────────────────────────────── */

/* NOLINTBEGIN(cppcoreguidelines-macro-usage) */
typedef int32_t fixpp_error_t;

/* ── Cross-cutting block (0–99) ─────────────────────────────────────────── */

/** No error. */
#define FIXPP_ERR_OK                    ((fixpp_error_t)0)
/** Operation was cancelled via a cancellation_token / asio cancellation slot. */
#define FIXPP_ERR_CANCELLED             ((fixpp_error_t)1)
/**
 * Error code is unknown to this consumer — either a future code
 * (introduce_minor > consumer_minor) or an override-group placeholder
 * (session_*, log_*, otel_*, app_*, out_of_memory in C-ABI 0.2.x).
 */
#define FIXPP_ERR_UNKNOWN               ((fixpp_error_t)2)
/** A NULL pointer was passed where a non-null handle is required. */
#define FIXPP_ERR_NULL_HANDLE           ((fixpp_error_t)3)
/** The handle pointer is non-null but refers to a destroyed or corrupted object. */
#define FIXPP_ERR_INVALID_HANDLE        ((fixpp_error_t)4)
/** C-ABI major version mismatch between producer and consumer. */
#define FIXPP_ERR_VERSION_MISMATCH      ((fixpp_error_t)5)
/** Output buffer too small to hold the result (e.g. decimal format). */
#define FIXPP_ERR_BUFFER_TOO_SMALL      ((fixpp_error_t)6)
/** Wrong handle type passed to a typed accessor. */
#define FIXPP_ERR_TYPE_MISMATCH         ((fixpp_error_t)7)
/** Requested tag not present in the message. */
#define FIXPP_ERR_TAG_NOT_FOUND         ((fixpp_error_t)8)
/** Index out of range for a repeated group or sequence. */
#define FIXPP_ERR_INDEX_OUT_OF_RANGE    ((fixpp_error_t)9)
/** C-ABI configuration is invalid (e.g. conflicting options). */
#define FIXPP_ERR_CAPI_CONFIG_INVALID   ((fixpp_error_t)10)

/* ── Wire block (100–199) [2b §6.7] ──────────────────────────────────────── */

/** Inbound frame is malformed at the framing / protocol level. */
#define FIXPP_ERR_WIRE_INVALID_FRAME    ((fixpp_error_t)100)
/** Wire-level capacity or size limit exceeded (DoS bound). */
#define FIXPP_ERR_WIRE_LIMIT_EXCEEDED   ((fixpp_error_t)101)
/** Frame fails FIX conformance validation (missing required field, bad value, etc.). */
#define FIXPP_ERR_WIRE_CONFORMANCE      ((fixpp_error_t)102)

/* ── Dict block (200–299) [2c §6.7] ──────────────────────────────────────── */

/** Dictionary configuration or schema error. */
#define FIXPP_ERR_DICT_CONFIG           ((fixpp_error_t)200)
/** Dictionary capacity limit exceeded. */
#define FIXPP_ERR_DICT_LIMIT_EXCEEDED   ((fixpp_error_t)201)
/** Out-of-memory during dictionary load/reify. */
#define FIXPP_ERR_DICT_OOM              ((fixpp_error_t)202)

/* ── Threading block (300–399) [2d §6.7] ────────────────────────────────── */

/** Session or executor configuration error. */
#define FIXPP_ERR_THREAD_CONFIG         ((fixpp_error_t)300)
/** Session lifecycle state error (already open / already closed). */
#define FIXPP_ERR_THREAD_SESSION_LIFECYCLE ((fixpp_error_t)301)
/** Threading runtime error (strand dispatch failure, OOM on dispatch). */
#define FIXPP_ERR_THREAD_RUNTIME        ((fixpp_error_t)302)

/* ── Store block (400–499) [2e §6.7] ────────────────────────────────────── */

/** Store I/O or capacity runtime failure. */
#define FIXPP_ERR_STORE_RUNTIME         ((fixpp_error_t)400)
/** Store sequence-number consistency violation. */
#define FIXPP_ERR_STORE_CONSISTENCY     ((fixpp_error_t)401)
/** Store configuration error (factory failed, invalid config). */
#define FIXPP_ERR_STORE_CONFIG          ((fixpp_error_t)402)
/** Store visitor callback aborted the iteration. */
#define FIXPP_ERR_STORE_VISITOR         ((fixpp_error_t)403)

/* ── Sync block (500–599) [2f §6.7] ─────────────────────────────────────── */

/** Synchronisation primitive runtime error (alloc failure, outside-session use). */
#define FIXPP_ERR_SYNC_RUNTIME          ((fixpp_error_t)500)

/* ── TLS block (600–699) [2g §6.7] ──────────────────────────────────────── */

/** TLS configuration error (cert load, cipher, security profile). */
#define FIXPP_ERR_TLS_CONFIG            ((fixpp_error_t)600)
/** TLS handshake failed (cert validation, DoS bound, pin mismatch). */
#define FIXPP_ERR_TLS_HANDSHAKE         ((fixpp_error_t)601)
/** TLS pinset error (pin not found, duplicate, capacity exceeded). */
#define FIXPP_ERR_TLS_PINSET            ((fixpp_error_t)602)
/** TLS runtime error (PMR allocation failure during snapshot). */
#define FIXPP_ERR_TLS_RUNTIME           ((fixpp_error_t)603)

/* ── Transport block (700–799) [2h §6.7] ────────────────────────────────── */

/** Transport lifecycle error (connect, resolve, already-connected, reconnect). */
#define FIXPP_ERR_TRANSPORT_LIFECYCLE   ((fixpp_error_t)700)
/** Transport I/O error (read/write error, EOF, truncated close). */
#define FIXPP_ERR_TRANSPORT_IO          ((fixpp_error_t)701)
/** Transport handshake error (TLS handshake timeout or rejection). */
#define FIXPP_ERR_TRANSPORT_HANDSHAKE   ((fixpp_error_t)702)
/** Transport factory or PSK configuration error. */
#define FIXPP_ERR_TRANSPORT_CONFIG      ((fixpp_error_t)703)

/* ── Decimal block (800–899) [2a §6.7] ──────────────────────────────────── */

/** Decimal input is invalid or overflows the representation. */
#define FIXPP_ERR_DECIMAL_INVALID       ((fixpp_error_t)800)
/** Decimal conversion lost precision (truncated fractional digits). */
#define FIXPP_ERR_DECIMAL_PRECISION_LOSS ((fixpp_error_t)801)

/* ── Control-plane block (900–999) [2j §6.7] ────────────────────────────── */

/** Control-plane configuration error. */
#define FIXPP_ERR_CTRL_CONFIG           ((fixpp_error_t)900)
/** Control-plane runtime error. */
#define FIXPP_ERR_CTRL_RUNTIME          ((fixpp_error_t)901)

/* ── Bindings block (1200–1299) [2m §6.7] ───────────────────────────────── */

/** Python binding: callback raised an exception. */
#define FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED ((fixpp_error_t)1200)
/** Python binding: subinterpreter isolation violation. */
#define FIXPP_ERR_BINDING_SUBINTERPRETER         ((fixpp_error_t)1201)
/** Python binding: object lifetime violation. */
#define FIXPP_ERR_BINDING_OBJECT_LIFETIME        ((fixpp_error_t)1202)
/** Python binding: wheel ABI mismatch. */
#define FIXPP_ERR_BINDING_WHEEL_ABI_MISMATCH     ((fixpp_error_t)1203)
/** Python binding: reentrant close from callback. */
#define FIXPP_ERR_BINDING_CALLBACK_REENTRANT_CLOSE ((fixpp_error_t)1204)

/* NOLINTEND(cppcoreguidelines-macro-usage) */

/* ── Exported function ───────────────────────────────────────────────────── */

/**
 * fixpp_strerror — return a human-readable description of a fixpp_error_t code.
 *
 * Thread-safety: thread-safe (returns a pointer into a static const table).
 * Allocation: none — result points to static storage; caller must not free it.
 * Returns: non-null pointer to a null-terminated string.
 *          Published codes (E-2) return a descriptive message.
 *          Unknown / undefined / reserved codes return "unknown error".
 */
FIXPP_API_EXPORT const char* fixpp_strerror(fixpp_error_t code);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FIXPP_C_API_ERROR_H */
