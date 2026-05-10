/*
 * c_api_decimal.h — Phase 1 contract extract for /speckit-plan
 *
 * Feature:    001-core-decimal
 * Owner:      fixpp::core (this feature) + fixpp::capi co-owned with 2i
 * Source:     .specify/2a-decimal.md §5  +  spec.md §4.5  +  spec.md §4.7
 * Status:     reviewer-facing contract for Codex Gate A
 *
 * NOT a header to compile against. The real declarations land in
 * include/fix/c_api.h during /implement, alongside the rest of the C-ABI
 * surface owned by 2i.
 *
 * Idioms below are deliberately C-style (per [const §X §2] "no C++ symbol
 * leakage through the C ABI"): `<stdint.h>`/`<stddef.h>` not `<cstdint>`,
 * `typedef struct` not `using`, snake_case `fixpp_*` not CamelCase.
 * Suppress the clang-tidy modernize/* and readability-identifier-naming
 * checks that would otherwise flag the C-ABI shape as non-modern C++.
 */
// NOLINTBEGIN(modernize-deprecated-headers,modernize-use-using,readability-identifier-naming)

#ifndef FIXPP_CONTRACT_C_API_DECIMAL_H
#define FIXPP_CONTRACT_C_API_DECIMAL_H

#include <stdint.h>     /* int64_t, int8_t, INT64_MIN */
#include <stddef.h>     /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Frozen-layout decimal at the C-ABI boundary ─────────────────────────── */
/*
 * Layout invariants (asserted in src/capi/decimal_assert.cpp):
 *   sizeof(fixpp_decimal_t) == 16
 *   alignof(fixpp_decimal_t) == 8
 *   offsetof(mantissa)  == 0
 *   offsetof(exponent)  == 8
 *   offsetof(_reserved) == 9
 *   is_standard_layout_v<fixpp_decimal_t>
 *
 * Canonical exponent domain: [-38, 0]. Anything outside is rejected at
 * the C-ABI boundary with FIXPP_ERR_DECIMAL_INVALID.
 *
 * _reserved[7]: zero-init RECOMMENDED (use FIXPP_DECIMAL_INITIALIZER or
 * fixpp_decimal_init). Library tolerates non-zero _reserved in v1.0.
 * Future v1.x semantics (if any) ship as a new explicit API, NEVER as a
 * silent meaning change.
 */
typedef struct fixpp_decimal_t {
    int64_t mantissa;       /* significand. INT64_MIN reserved as invalid sentinel. */
    int8_t  exponent;       /* power of 10. Canonical domain [-38, 0]. */
    int8_t  _reserved[7];   /* zero-init recommended; ignored on read in v1.0 */
} fixpp_decimal_t;

/* ─── Initializers ────────────────────────────────────────────────────────── */

#define FIXPP_DECIMAL_INITIALIZER  { 0, 0, { 0, 0, 0, 0, 0, 0, 0 } }
#define FIXPP_DECIMAL_INVALID      { INT64_MIN, 0, { 0, 0, 0, 0, 0, 0, 0 } }

/* Runtime helper — zero-inits _reserved.
 * Reentrancy: thread-safe (writes only to *out).
 */
void fixpp_decimal_init(fixpp_decimal_t* out);

/* ─── Boundary functions ──────────────────────────────────────────────────── */
/*
 * All boundary functions are noexcept (in C++ terms — no exceptions cross
 * the C-ABI per [const §X.4]).
 *
 * Reentrancy: thread-safe (no shared state touched).
 *
 * Error returns:
 *   FIXPP_ERR_OK (== 0)                  on success
 *   FIXPP_ERR_DECIMAL_INVALID            on bad input (empty, non-digit
 *                                        bytes, exponent outside [-38, 0],
 *                                        mantissa == INT64_MIN sentinel)
 *   FIXPP_ERR_DECIMAL_PRECISION_LOSS     reserved for cross-traits narrowing
 *                                        (the parse/format pair never returns
 *                                        this — issued only by the C++ traits
 *                                        cross-conversion path)
 *   FIXPP_ERR_BUFFER_TOO_SMALL           dst buffer < required bytes (format only)
 */

/* Parse FIX FLOAT bytes [-]?DIGITS(.DIGITS)? into *out.
 * On error, *out is unmodified (caller checks the return).
 */
int /* fixpp_error_t */
fixpp_decimal_parse(const char*       src,
                    size_t            src_len,
                    fixpp_decimal_t*  out);

/* Serialize *src into dst[0..dst_len).
 * Returns FIXPP_ERR_OK and writes *out_written on success (out_written may be NULL).
 * Worst-case for the default trait: 41 bytes (-0.<19 zeros><19 mantissa digits>).
 */
int /* fixpp_error_t */
fixpp_decimal_format(const fixpp_decimal_t* src,
                     char*                  dst,
                     size_t                 dst_len,
                     size_t*                out_written);

/* Compare two decimals by VALUE (not by representation).
 * {1,0}, {10,-1}, {100,-2} all compare equal.
 * On success, writes -1 / 0 / +1 to *out_ordering.
 *
 * Validates exponent ∈ [-38, 0] on BOTH inputs (defense-in-depth at the
 * boundary; symmetric with fixpp_decimal_format per AC-C6).
 */
int /* fixpp_error_t */
fixpp_decimal_compare(const fixpp_decimal_t* a,
                      const fixpp_decimal_t* b,
                      int*                   out_ordering);

/* Convenience: equality-only compare.
 * On success, writes 0 (unequal) or 1 (equal) to *out_equal.
 * Same exponent-domain validation as fixpp_decimal_compare.
 */
int /* fixpp_error_t */
fixpp_decimal_equal(const fixpp_decimal_t* a,
                    const fixpp_decimal_t* b,
                    int*                   out_equal);

/* ─── Decimal-related error codes (provisional) ───────────────────────────── *
 *
 * NUMERIC VALUES ALLOCATED 2026-05-10, owned by 2i.
 * 2i ratifies by re-using the same numeric range. After 2i ratifies,
 * any change is a Tier 2 ABI breakage per [const §X.4].
 *
 *   FIXPP_ERR_DECIMAL_INVALID         /+ provisional value +/
 *   FIXPP_ERR_DECIMAL_PRECISION_LOSS  /+ provisional value +/
 *   FIXPP_ERR_BUFFER_TOO_SMALL        /+ existing generic, owned by 2i +/
 *
 * The exact integer values land in include/fix/c_api.h during /implement
 * and are recorded in tools/abi_history/error_codes_v1.txt. The CI
 * occupancy gate (tools/check_capi_occupancy.sh) verifies no re-binding
 * once a value is published in a tagged release.
 */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FIXPP_CONTRACT_C_API_DECIMAL_H */
