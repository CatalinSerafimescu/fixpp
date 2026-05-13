/*
 * fix/c_api.h — fixpp C ABI entry point
 *
 * This is the ONE header that C-ABI consumers include.
 * No C++ headers are included here — only <stddef.h>, <stdint.h>, <stdbool.h>
 * per [arch §9.1] / [api §6].
 *
 * Phase 3 exposes only fixpp_version_string(). The full C ABI surface
 * enumerated in api-contract.md §7 lands incrementally starting Phase 4.
 * [const §XII allows incremental rollout; constitution Article XII §...]
 */

#ifndef FIXPP_C_API_H
#define FIXPP_C_API_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Version macros ─────────────────────────────────────────────────────── */
/* Library SemVer — independent from C ABI SemVer per [arch §9.2]. */
#define FIXPP_VERSION_MAJOR 0
#define FIXPP_VERSION_MINOR 0
#define FIXPP_VERSION_PATCH 1

/* C ABI SemVer — stable contract; bumped independently of library. */
#define FIXPP_C_ABI_VERSION_MAJOR 0
#define FIXPP_C_ABI_VERSION_MINOR 1
#define FIXPP_C_ABI_VERSION_PATCH 0

/* ── Phase 4 decimal boundary functions ─────────────────────────────────── */
#include "fix/c_api/decimal.h"

/* ── Version accessor ───────────────────────────────────────────────────── */
/**
 * fixpp_version_string — return the library version as a null-terminated string.
 *
 * Thread-safety: thread-safe (returns a static const string literal).
 * Returns: non-null pointer to "MAJOR.MINOR.PATCH".
 */
const char* fixpp_version_string(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* FIXPP_C_API_H */
