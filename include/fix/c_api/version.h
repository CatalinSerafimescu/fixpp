/*
 * include/fix/c_api/version.h — C-ABI version detection surface (CA-004)
 *
 * [2i §4.5] / data-model E-4.  C-clean: no C++ syntax.
 *
 * Two independent version tracks ([arch §9.2]):
 *   fixpp_version()          → C-ABI surface version (MAJOR.MINOR.PATCH below)
 *   fixpp_library_version()  → C++ library SemVer (currently 0.0.1)
 *
 * A C-ABI consumer MUST check fixpp_version().major == FIXPP_C_ABI_VERSION_MAJOR
 * for major-compatibility; minor/patch differences are backward-compatible within
 * the same major per [const §X.4].
 *
 * Reentrancy: both accessors are thread-safe (value return, no global mutation).
 */

#ifndef FIXPP_C_API_VERSION_H
#define FIXPP_C_API_VERSION_H

/* NOLINTBEGIN(hicpp-deprecated-headers,modernize-deprecated-headers):
   C-ABI header; C-style includes are correct for pure-C consumers. */
#include <stdint.h>
/* NOLINTEND(hicpp-deprecated-headers,modernize-deprecated-headers) */

#include <fix/c_api/export.h>

/* ── Version macros ─────────────────────────────────────────────────────────
   NOLINTBEGIN(cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,
               modernize-macro-to-enum) */

/** C-ABI surface version — bumped independently of the C++ library version.
 *  GA freeze: MAJOR 0->1 declares the C-ABI surface stable ([const §X.1]; the
 *  shape is frozen for FIXPP_C_ABI_VERSION_MAJOR == 1). Any future breaking
 *  change requires a MAJOR bump; additive-only changes bump MINOR.
 *
 *  MINOR is PRESERVED at 5 across the freeze (not reset to 0): it is the fifth
 *  additive minor of the C-ABI (0.1..0.5), and the forward-compat error-code
 *  downgrade ([const §X.4] / src/capi/error.cpp introducing_minor) is keyed on
 *  this minor. Resetting it to 0 would place the current version BELOW the
 *  introducing_minor (2/4) of already-published codes, so a conforming consumer
 *  would see those codes downgraded to UNKNOWN — an incoherent baseline. 1.5.0
 *  keeps the downgrade frame continuous; the first stable ABI is 1.5.0.
 *
 *  Evolution of the downgrade frame:
 *    - Future MINORs within major 1 (1.6, 1.7, ...) just continue: a new code
 *      gets introducing_minor = the minor it appears at, so an older consumer
 *      forward-compat-downgrades it to UNKNOWN. No reset needed.
 *    - The next BREAKING MAJOR (2.0.0) is where MINOR resets to 0 AND the
 *      introducing_minor table is rebased so every surviving code is the 2.0
 *      baseline (introducing_minor 0); new 2.x codes gate at 1,2,... A major
 *      bump is allowed to break, and a 1.x consumer is already refused by the
 *      major==major check, so the reset is free and coherent there — unlike at
 *      this non-breaking 0->1 freeze, where preserving MINOR is required.
 *
 *  This is the C-ABI SURFACE version only — fixpp_library_version() (the C++
 *  SemVer) is unaffected. Byte-frozen by tools/check_capi_freeze.sh (NBC-1). */
#define FIXPP_C_ABI_VERSION_MAJOR 1
#define FIXPP_C_ABI_VERSION_MINOR 5
#define FIXPP_C_ABI_VERSION_PATCH 0

/** Composite: (MAJOR<<16)|(MINOR<<8)|PATCH — single-integer compatibility check. */
#define FIXPP_C_ABI_VERSION \
    (((FIXPP_C_ABI_VERSION_MAJOR) << 16) | \
     ((FIXPP_C_ABI_VERSION_MINOR) << 8)  | \
      (FIXPP_C_ABI_VERSION_PATCH))

/* NOLINTEND(cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,
             modernize-macro-to-enum) */

/* ── Version descriptor ──────────────────────────────────────────────────── */

/**
 * fixpp_version_t — plain-old-data version descriptor.
 *
 * _reserved is zero on write; consumers MUST ignore it for forward-compat.
 * Layout frozen per [const §X.1] once FIXPP_C_ABI_VERSION_MAJOR >= 1.
 */
typedef struct fixpp_version {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint16_t _reserved; /* zero-initialised; reserved for future use */
} fixpp_version_t;

/* ── Accessors ───────────────────────────────────────────────────────────── */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * fixpp_version — return the C-ABI surface version.
 *
 * The returned value equals the FIXPP_C_ABI_VERSION_{MAJOR,MINOR,PATCH} macros
 * compiled into the engine binary.  A consumer linked against the same binary
 * can use the composite FIXPP_C_ABI_VERSION macro for a single-integer check.
 *
 * @return fixpp_version_t with major/minor/patch equal to the macros above.
 *
 * Thread-safety: thread-safe.
 */
FIXPP_API_EXPORT fixpp_version_t fixpp_version(void);

/**
 * fixpp_library_version — return the C++ library version.
 *
 * This is the semantic version of the underlying C++ library (currently 0.0.1)
 * and is on a separate, independent increment track from the C-ABI version
 * per [arch §9.2].  It is informational; compatibility is governed by
 * fixpp_version() / FIXPP_C_ABI_VERSION_MAJOR.
 *
 * @return fixpp_version_t with the library's MAJOR.MINOR.PATCH.
 *
 * Thread-safety: thread-safe.
 */
FIXPP_API_EXPORT fixpp_version_t fixpp_library_version(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FIXPP_C_API_VERSION_H */
