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
 *  GA freeze: MAJOR 0->1 froze the C-ABI surface's shape ([const §X.1]). Until
 *  fixpp's first public release a breaking change bumps MINOR and is declared
 *  BREAKING ([const §X.7]); additive changes bump MINOR too. The compatibility
 *  promise starts at the first public release, where this version resets to
 *  1.0.0; from then on a breaking change requires a MAJOR bump.
 *
 *  MINOR is PRESERVED at 5 across the freeze (not reset to 0): it is the fifth
 *  additive minor of the C-ABI (0.1..0.5), and the forward-compat error-code
 *  downgrade ([const §X.4] / src/capi/error.cpp introducing_minor) is keyed on
 *  this minor. Resetting it to 0 would place the current version BELOW the
 *  introducing_minor (2/4) of already-published codes, so a conforming consumer
 *  would see those codes downgraded to UNKNOWN — an incoherent baseline. 1.5.0
 *  keeps the downgrade frame continuous. It is a review baseline, not a
 *  compatibility promise: the first stable ABI is the 1.0.0 of the first
 *  public release.
 *
 *  Evolution of the downgrade frame:
 *    - Future MINORs within major 1 (1.6, 1.7, ...) just continue: a new code
 *      gets introducing_minor = the minor it appears at, so an older consumer
 *      forward-compat-downgrades it to UNKNOWN. No reset needed.
 *    - The first public release ([const §X.7]) resets the version to 1.0.0 and
 *      rebases the introducing_minor table exactly as the next bullet describes;
 *      no consumer predates it, so no conforming consumer sees a downgrade.
 *    - The next BREAKING MAJOR (2.0.0) is where MINOR resets to 0 AND the
 *      introducing_minor table is rebased so every surviving code is the 2.0
 *      baseline (introducing_minor 0); new 2.x codes gate at 1,2,... A major
 *      bump is allowed to break, and a 1.x consumer is already refused by the
 *      major==major check, so the reset is free and coherent there — unlike at
 *      this non-breaking 0->1 freeze, where preserving MINOR is required.
 *
 *  This is the C-ABI SURFACE version only — fixpp_library_version() (the C++
 *  SemVer) is unaffected. Byte-frozen by tools/check_capi_freeze.sh (NBC-1). */

/*  1.9 (BREAKING, [const §X.7]; 091, fixpp#418): a Length+Data pair a loaded
 *  dictionary declares only inside a component or group is now a dictionary
 *  pair. The effects that have a carrying declaration are noted on it (dict.h
 *  fixpp_dict_load_from_xml; message.h fixpp_msg_commit, fixpp_msg_set_data,
 *  fixpp_entry_set_data and the accessor preamble; session.h
 *  fixpp_session_send, fixpp_session_register_callback,
 *  fixpp_session_register_send_callback). The effects with no carrying
 *  declaration are recorded here:
 *    - a frame a pre-1.9 engine stored with a malformed pair of that kind now
 *      fails replay (build_replay_frame) and is gap-filled rather than resent;
 *    - the session's header and Logon scans (scan_frame_header, interpret_logon,
 *      the store's frame_has_genuine_tag554 masking) read such a Data by count.
 *  interpret_logon refuses a Logon carrying a Length immediately followed by
 *  its paired Data whose counted extent reaches or passes the end of the
 *  whole framed message, or whose following byte is not SOH, whether of a
 *  component/group-only pair or of a standard pair such as
 *  RawDataLength(95) and RawData(96) (so shipped dictionaries are affected
 *  too): a Logon of that shape that was accepted is now refused, on either
 *  role. The observers are every call whose result depends on the session
 *  having logged on; each also carries its own note in session.h:
 *  fixpp_session_is_established stays false; fixpp_session_close, once the
 *  refused session has drained, returns FIXPP_ERR_THREAD_SESSION_LIFECYCLE,
 *  translated for the consumer's ABI minor (fixpp_engine_create), not
 *  FIXPP_ERR_OK; fixpp_session_send on that session returns
 *  FIXPP_ERR_SESSION_INVALID_STATE, translated for the consumer's ABI minor
 *  (fixpp_engine_create), not FIXPP_ERR_OK; neither the receive
 *  callback (fixpp_session_register_callback) nor the toApp callback
 *  (fixpp_session_register_send_callback) is ever invoked for it.
 *  Independently of the loader change that opens this entry,
 *  fixpp_msg_commit (FR-021, fixpp#506) checks a group's count field as a
 *  field with an empty value, so a group whose count tag is the Length or the
 *  Data half of a pair, a standard pair or one the session dictionary
 *  declares, which returned FIXPP_ERR_OK when its instance-count digits completed the pair,
 *  now returns FIXPP_ERR_WIRE_CONFORMANCE; this is reachable on every session
 *  with no dictionary, and on a dictionary session only where that dictionary
 *  declares a group on a tag that is a pair half.
 *  No error code is added. */
#define FIXPP_C_ABI_VERSION_MAJOR 1
#define FIXPP_C_ABI_VERSION_MINOR 9 /* 1.9: component/group-only Length+Data pairs (fixpp#418) */
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
