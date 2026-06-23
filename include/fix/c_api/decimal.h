/*
 * include/fix/c_api/decimal.h — C-ABI decimal boundary functions.
 * Phase 4 / 001-core-decimal — matches contracts/c_api_decimal.h byte-for-byte.
 * Source: .specify/2a-decimal.md v0.3 §5.1 and §5.2. Literal extract.
 *
 * This header is self-sufficient for a pure-C consumer that includes only
 * fix/c_api/decimal.h. The full fixpp_error_t enum lives in fix/c_api.h (2i);
 * this typedef must stay byte-compatible. [const §X.1] [const §X.2] [const §X.3]
 */

#ifndef FIX_C_API_DECIMAL_H
#define FIX_C_API_DECIMAL_H

// NOLINT(hicpp-deprecated-headers,modernize-deprecated-headers): C-ABI header; C-style
// includes are correct for consumers that compile as C (not C++).
#include <stddef.h>  // NOLINT(hicpp-deprecated-headers,modernize-deprecated-headers)
#include <stdint.h>  // NOLINT(hicpp-deprecated-headers,modernize-deprecated-headers)

#ifdef __cplusplus
extern "C" {
#endif

/* ── fixpp_error_t + error codes ─────────────────────────────────────────────
   049 T012 (FR-011): the provisional `typedef int fixpp_error_t` and the
   provisional error #defines (BUFFER_TOO_SMALL/DECIMAL_INVALID/DECIMAL_PRECISION_LOSS
   at the old slots 3/10/11) are removed. fix/c_api/error.h is now the single
   source for `fixpp_error_t` (int32_t) and the full master code layout — the
   decimal codes live in their [800,899] block (DECIMAL_INVALID=800,
   DECIMAL_PRECISION_LOSS=801; decimal_buffer_too_small reuses BUFFER_TOO_SMALL=6).
   Codes are referenced by macro name everywhere, so the value change is
   transparent to this header's consumers. */
#include "fix/c_api/error.h"

/* ── §5.1 Layout — frozen for FIXPP_C_ABI_VERSION_MAJOR == 1 [const §X.1] ─── */
typedef struct fixpp_decimal {
    int64_t mantissa;
    int8_t exponent;
    int8_t _reserved[7]; /* ignored on read in v1.0; init via FIXPP_DECIMAL_INITIALIZER */
} fixpp_decimal_t;

#define FIXPP_DECIMAL_INITIALIZER {0, 0, {0, 0, 0, 0, 0, 0, 0}}
#define FIXPP_DECIMAL_INVALID {INT64_MIN, 0, {0, 0, 0, 0, 0, 0, 0}}

/* ── §5.2 Boundary functions — verbatim from 2a-decimal.md v0.3 §5.2 ──────── */

/* No engine-global state, no allocation [const §X.5].
   Reentrancy: thread-safe (049 T019 / [2i §4.10]). */
fixpp_error_t fixpp_decimal_parse(const char* src, size_t src_len, fixpp_decimal_t* out);

/* Writes only the caller-supplied dst buffer; no shared state.
   Reentrancy: thread-safe. */
fixpp_error_t fixpp_decimal_format(fixpp_decimal_t d, char* dst, size_t dst_cap, size_t* written);

/* ASSUMES canonical domain (exponent ∈ [-38,0]). Returns -1/0/+1.
   Reentrancy: thread-safe. */
int fixpp_decimal_compare(fixpp_decimal_t a, fixpp_decimal_t b);

/* Convenience equality. Returns 0/1. ASSUMES canonical domain.
   Reentrancy: thread-safe. */
int fixpp_decimal_equal(fixpp_decimal_t a, fixpp_decimal_t b);

/* Zero-init helper for forward-compat with future _reserved semantics.
   Reentrancy: thread-safe. */
void fixpp_decimal_init(fixpp_decimal_t* out);

/* ── _checked siblings — ratified 2026-05-12 (AC-C6, research.md D-12) ────── */

/* Validates exponent ∈ [-38,0] on both inputs. On out-of-domain returns
   FIXPP_ERR_DECIMAL_INVALID; on in-domain writes ordering to *out_ordering.
   Reentrancy: thread-safe. */
fixpp_error_t fixpp_decimal_compare_checked(fixpp_decimal_t a, fixpp_decimal_t b,
                                            int* out_ordering);

/* Same validation as _compare_checked. On in-domain writes 0/1 to *out_equal.
   Reentrancy: thread-safe. */
fixpp_error_t fixpp_decimal_equal_checked(fixpp_decimal_t a, fixpp_decimal_t b, int* out_equal);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FIX_C_API_DECIMAL_H */
