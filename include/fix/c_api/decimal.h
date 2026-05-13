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

/* ── fixpp_error_t forward declaration ──────────────────────────────────────── */
typedef int fixpp_error_t;

/* ── Provisional error codes (allocated 2026-05-12, owned by 2i) ─────────── */
/* C-ABI error codes must be #define, not enum, to remain usable by pure-C consumers.
   NOLINTBEGIN(cppcoreguidelines-macro-to-enum,modernize-macro-to-enum) */
#define FIXPP_ERR_OK 0
#define FIXPP_ERR_UNKNOWN 2
#define FIXPP_ERR_BUFFER_TOO_SMALL 3
#define FIXPP_ERR_DECIMAL_INVALID 10        /* provisional 2026-05-12 */
#define FIXPP_ERR_DECIMAL_PRECISION_LOSS 11 /* provisional 2026-05-12 */
/* NOLINTEND(cppcoreguidelines-macro-to-enum,modernize-macro-to-enum) */

/* ── §5.1 Layout — frozen for FIXPP_C_ABI_VERSION_MAJOR == 1 [const §X.1] ─── */
typedef struct fixpp_decimal {
    int64_t mantissa;
    int8_t exponent;
    int8_t _reserved[7]; /* ignored on read in v1.0; init via FIXPP_DECIMAL_INITIALIZER */
} fixpp_decimal_t;

#define FIXPP_DECIMAL_INITIALIZER {0, 0, {0, 0, 0, 0, 0, 0, 0}}
#define FIXPP_DECIMAL_INVALID {INT64_MIN, 0, {0, 0, 0, 0, 0, 0, 0}}

/* ── §5.2 Boundary functions — verbatim from 2a-decimal.md v0.3 §5.2 ──────── */

/* Thread-safe (no engine-global state, no allocation) [const §X.5] */
fixpp_error_t fixpp_decimal_parse(const char* src, size_t src_len, fixpp_decimal_t* out);

fixpp_error_t fixpp_decimal_format(fixpp_decimal_t d, char* dst, size_t dst_cap, size_t* written);

/* ASSUMES canonical domain (exponent ∈ [-38,0]). Returns -1/0/+1. */
int fixpp_decimal_compare(fixpp_decimal_t a, fixpp_decimal_t b);

/* Convenience equality. Returns 0/1. ASSUMES canonical domain. */
int fixpp_decimal_equal(fixpp_decimal_t a, fixpp_decimal_t b);

/* Zero-init helper for forward-compat with future _reserved semantics. */
void fixpp_decimal_init(fixpp_decimal_t* out);

/* ── _checked siblings — ratified 2026-05-12 (AC-C6, research.md D-12) ────── */

/* Validates exponent ∈ [-38,0] on both inputs. On out-of-domain returns
   FIXPP_ERR_DECIMAL_INVALID; on in-domain writes ordering to *out_ordering. */
fixpp_error_t fixpp_decimal_compare_checked(fixpp_decimal_t a, fixpp_decimal_t b,
                                            int* out_ordering);

/* Same validation as _compare_checked. On in-domain writes 0/1 to *out_equal. */
fixpp_error_t fixpp_decimal_equal_checked(fixpp_decimal_t a, fixpp_decimal_t b, int* out_equal);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FIX_C_API_DECIMAL_H */
