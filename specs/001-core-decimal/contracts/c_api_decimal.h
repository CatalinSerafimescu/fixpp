/* ============================================================================
 * include/fix/c_api/decimal.h
 * Phase 4 / 001-core-decimal — C-ABI contract extract.
 *
 * This file is a /plan contract extract, NOT the final installed header. The
 * final header lands in `include/fix/c_api/decimal.h` during /implement; this
 * extract is the contract that the implementation must match byte-for-byte on
 * the boundary-function signatures and the struct layout.
 *
 * EVERY non-comment block below is a literal copy-paste from
 * .specify/2a-decimal.md v0.3 (Gate A round 2 converged, signed off 2026-05-07).
 * Where the block is novel to Phase 4 (e.g., the _checked siblings pending
 * /clarify resolution per research.md D-12), the lineage comment says so.
 *
 * Reviewers verifying inheritance fidelity: diff each block below against the
 * cited 2a-decimal.md section. Drift = round-1-style memory-of-2a failure.
 *
 * Citations use canonical [const §Roman.arabic] form per constitution.md:5.
 *
 * Constitution refs: [const §X.1] (ABI versioned contract);
 *                    [const §X.2] (no C++ leakage through C ABI);
 *                    [const §X.3] (decimal at C-ABI is PoD);
 *                    [const §X.4] (bounded error reporting + forwards-compat);
 *                    [const §X.5] (reentrancy contract).
 * ============================================================================
 */

#ifndef FIX_C_API_DECIMAL_H
#define FIX_C_API_DECIMAL_H

#include <stdint.h>
#include <stddef.h>   /* size_t — required by fixpp_decimal_parse / _format below */

#ifdef __cplusplus
extern "C" {
#endif

/* ─── fixpp_error_t (forward declaration; full definition in c_api.h) ────────
 * Declared here so this header is self-sufficient for a pure-C consumer that
 * includes only c_api/decimal.h. The full fixpp_error_t enum lives in the
 * master c_api.h (owned by 2i); this typedef must stay byte-compatible.
 */
typedef int fixpp_error_t;

/* ============================================================================
 * Provisional numeric error-code allocations
 *
 * Allocated 2026-05-12, owned by 2i (final ratification in 2i's PR). Per
 * spec.md §4.7 Clarifications 2026-05-10 line 60: this PR defines provisional
 * numeric values; 2i ratifies by re-using the same numeric range. Any change
 * after ratification is a Tier 2 ABI breakage per [const §IX.5].
 * ============================================================================
 */

/* NOTE (049-c-abi-handles-errors T012, 2026-06-23): these provisional values
   are ARCHIVAL. The live C-ABI codes were renumbered into their [2i §4.3]
   master blocks — BUFFER_TOO_SMALL 3→6, DECIMAL_INVALID 10→800,
   DECIMAL_PRECISION_LOSS 11→801 — and now live in include/fix/c_api/error.h
   (the single source). decimal.h includes error.h; references are by macro
   name so the value change is transparent. The values below are kept as the
   original 001 contract record, not the current ABI. */
#define FIXPP_ERR_OK                       0
#define FIXPP_ERR_UNKNOWN                  2   /* reserved by [const §X.4] / 2i §4.5 */
/* ARCHIVAL only — live values are in include/fix/c_api/error.h (FR-011 lockstep).
 * Original 001 provisional values (DO NOT USE — macro definitions removed):
 *   FIXPP_ERR_BUFFER_TOO_SMALL  = 3   (live: 6,  renumbered by 049 T012)
 *   FIXPP_ERR_DECIMAL_INVALID   = 10  (live: 800, renumbered by 049 T012)
 *   FIXPP_ERR_DECIMAL_PRECISION_LOSS = 11 (live: 801, renumbered by 049 T012)
 */

/* ============================================================================
 * §5.1 — Layout
 *
 * Source: .specify/2a-decimal.md v0.3 §5.1 (lines 233–243). Literal extract.
 * ============================================================================
 */

/* extract from .specify/2a-decimal.md v0.3 §5.1 */
typedef struct fixpp_decimal {
    int64_t mantissa;
    int8_t  exponent;
    int8_t  _reserved[7];   /* reserved for future use under FIXPP_C_ABI_DECIMAL_RESERVED_USED.
                               In v1.0 the engine IGNORES these bytes on read. Consumers
                               SHOULD initialize via FIXPP_DECIMAL_INITIALIZER or
                               fixpp_decimal_init() to remain forward-compatible. */
} fixpp_decimal_t;

#define FIXPP_DECIMAL_INITIALIZER { 0, 0, {0,0,0,0,0,0,0} }
#define FIXPP_DECIMAL_INVALID     { INT64_MIN, 0, {0,0,0,0,0,0,0} }

/* This shape is FROZEN for FIXPP_C_ABI_VERSION_MAJOR == 1 per [const §X.1]. */

/* ============================================================================
 * §5.2 — Boundary functions
 *
 * Source: .specify/2a-decimal.md v0.3 §5.2 (lines 250–273). Literal extract.
 * Each signature is byte-for-byte identical to the 2a source. ANY deviation
 * from these shapes is an inheritance-fidelity defect (round-1 root cause #1).
 *
 * Reentrancy [const §X.5]: all five boundary functions are thread-safe (they
 * operate on caller-owned data, no engine-global state, no allocation).
 * ============================================================================
 */

/* extract from .specify/2a-decimal.md v0.3 §5.2 */
/* parse FIX FLOAT bytes from src into out; returns FIXPP_ERR_OK on success. */
fixpp_error_t
fixpp_decimal_parse(const char* src, size_t src_len, fixpp_decimal_t* out);

/* extract from .specify/2a-decimal.md v0.3 §5.2 */
/* serialize d into dst; writes byte count to *written.
   Worst-case bound: 41 bytes (sign + "0." + 38 leading zeros + 19 mantissa digits).
   The 41-byte ceiling holds because exponent is restricted to [-38, 0] at the boundary;
   positive exponents are rejected (FIXPP_ERR_DECIMAL_INVALID).
   Returns FIXPP_ERR_BUFFER_TOO_SMALL if dst_cap < required. */
fixpp_error_t
fixpp_decimal_format(fixpp_decimal_t d, char* dst, size_t dst_cap, size_t* written);

/* extract from .specify/2a-decimal.md v0.3 §5.2 */
/* lexicographic-after-canonicalization comparison; returns -1 / 0 / +1.
   Algorithm is the digit-string compare of §6.3 — never overflows.
   ASSUMES canonical domain (caller has produced exponent ∈ [-38, 0]).
   For the AC-C6 defensive-validation path, use fixpp_decimal_compare_checked
   below (ratified per /clarify 2026-05-12, option 1; research.md D-12). */
int fixpp_decimal_compare(fixpp_decimal_t a, fixpp_decimal_t b);

/* extract from .specify/2a-decimal.md v0.3 §5.2 */
/* Convenience: equality. Equivalent to fixpp_decimal_compare(a,b) == 0.
   Returns 0 (not equal) / 1 (equal). ASSUMES canonical domain. */
int fixpp_decimal_equal  (fixpp_decimal_t a, fixpp_decimal_t b);

/* extract from .specify/2a-decimal.md v0.3 §5.2 */
/* Zero-init helper for forward compatibility with future _reserved-byte semantics. */
void fixpp_decimal_init  (fixpp_decimal_t* out);

/* ============================================================================
 * `_checked` siblings — ratified per /clarify 2026-05-12 (option 1, research.md D-12)
 *
 * NOT in 2a v0.3. These are a Phase-4 addition that owns AC-C6's defensive
 * exponent-domain validation on the C-ABI boundary. The bare _compare /
 * _equal entry points above stay 2a §5.2 verbatim; the _checked siblings
 * here add the error channel that AC-C6 requires.
 *
 * Call-site discipline:
 *  - C++ engine code (which produces canonical values by construction)
 *    calls the bare entry points (`fixpp_decimal_compare` / `_equal`).
 *  - SWIG bindings and future C consumers that receive `fixpp_decimal_t`
 *    from untrusted contexts call the _checked siblings.
 *
 * 2i is expected to ratify these symbols in its own PR (alongside the
 * provisional FIXPP_ERR_DECIMAL_* codes above).
 *
 * Reentrancy [const §X.5]: thread-safe (no global state, no allocation).
 * ============================================================================
 */

/* ratified per /clarify 2026-05-12 (option 1, research.md D-12) */
/* AC-C6 defensive-validation overload of fixpp_decimal_compare.
   Validates exponent ∈ [-38, 0] on BOTH inputs (symmetric with AC-S3).
   On out-of-domain: returns FIXPP_ERR_DECIMAL_INVALID; *out_ordering unmodified.
   On in-domain: returns FIXPP_ERR_OK; *out_ordering ∈ {-1, 0, +1}. */
fixpp_error_t
fixpp_decimal_compare_checked(fixpp_decimal_t a, fixpp_decimal_t b, int* out_ordering);

/* ratified per /clarify 2026-05-12 (option 1, research.md D-12) */
/* AC-C6 defensive-validation overload of fixpp_decimal_equal.
   Same validation as _compare_checked.
   On in-domain: returns FIXPP_ERR_OK; *out_equal ∈ {0, 1}. */
fixpp_error_t
fixpp_decimal_equal_checked(fixpp_decimal_t a, fixpp_decimal_t b, int* out_equal);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* FIX_C_API_DECIMAL_H */
