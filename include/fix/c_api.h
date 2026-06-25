/*
 * fix/c_api.h — fixpp C ABI entry point
 *
 * This is the ONE header that C-ABI consumers include.
 * No C++ headers are included here — only <stddef.h>, <stdint.h>, <stdbool.h>
 * per [arch §9.1] / [api §6].
 *
 * Feature A (CA-001..004) exposes the foundation C-ABI surface aggregated here:
 * opaque handle catalogue (handles.h), error codes + fixpp_strerror (error.h),
 * version accessors + macros (version.h), and the frozen decimal boundary
 * (decimal.h) — all decorated via the FIXPP_API_EXPORT visibility macro
 * (export.h). The remaining surface (CA-005..010: session lifecycle, message
 * send/receive, field accessors) lands incrementally in Features B/C.
 * [const §XII allows incremental rollout; constitution Article XII §...]
 */

#ifndef FIXPP_C_API_H
#define FIXPP_C_API_H

// NOLINT(hicpp-deprecated-headers,modernize-deprecated-headers): C-ABI header; C-style
// includes are correct for consumers that compile as C (not C++).
#include <stdbool.h>  // NOLINT(hicpp-deprecated-headers,modernize-deprecated-headers)
#include <stddef.h>   // NOLINT(hicpp-deprecated-headers,modernize-deprecated-headers)
#include <stdint.h>   // NOLINT(hicpp-deprecated-headers,modernize-deprecated-headers)

#ifdef __cplusplus
extern "C" {
#endif

/* ── Library SemVer — independent from C ABI SemVer per [arch §9.2] ───────── */
/* The C-ABI SemVer macros (FIXPP_C_ABI_VERSION_{MAJOR,MINOR,PATCH} + composite)
   live in fix/c_api/version.h (049 T022) — the single source — and are NOT
   duplicated here. The stale inline FIXPP_C_ABI_VERSION_* block (0.1.0) was
   removed; version.h carries the current 0.2.0 values. */
#define FIXPP_VERSION_MAJOR 0
#define FIXPP_VERSION_MINOR 0
#define FIXPP_VERSION_PATCH 1

/* ── Aggregate split headers ([2i §4.1] per-domain split) ─────────────────── */
#include "fix/c_api/export.h"   // NOLINT(misc-include-cleaner): FIXPP_API_EXPORT
#include "fix/c_api/error.h"    // NOLINT(misc-include-cleaner): fixpp_error_t + fixpp_strerror
#include "fix/c_api/version.h"  // NOLINT(misc-include-cleaner): fixpp_version_t + accessors + C-ABI macros
#include "fix/c_api/handles.h"  // NOLINT(misc-include-cleaner): opaque handle catalogue
#include "fix/c_api/decimal.h"  // NOLINT(misc-include-cleaner): decimal boundary functions
#include "fix/c_api/engine.h"   // NOLINT(misc-include-cleaner): engine lifecycle + engine-config builder (CA-005)
#include "fix/c_api/session.h"  // NOLINT(misc-include-cleaner): session lifecycle/send/recv + session-config builder (CA-005/006/007)
#include "fix/c_api/message.h"  // NOLINT(misc-include-cleaner): message handle types (CA-008/CA-009/CA-010, Feature C)

/* ── Version accessor ───────────────────────────────────────────────────── */
/**
 * fixpp_version_string — return the library version as a null-terminated string.
 *
 * Thread-safety: thread-safe (returns a static const string literal).
 * Returns: non-null pointer to "MAJOR.MINOR.PATCH".
 */
const char* fixpp_version_string(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FIXPP_C_API_H */
