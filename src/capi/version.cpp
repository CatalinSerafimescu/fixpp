// src/capi/version.cpp — C-ABI version accessors (CA-004, T007)
// [2i §4.5] / data-model E-4

// The umbrella supplies BOTH the library SemVer macros FIXPP_VERSION_{MAJOR,
// MINOR,PATCH} (0/0/1, used by fixpp_library_version) AND — via its include of
// fix/c_api/version.h — the C-ABI macros FIXPP_C_ABI_VERSION_* (0/2/0) and
// fixpp_version_t. As of 049 T022 the umbrella no longer defines a stale
// FIXPP_C_ABI_VERSION_* block, so there is a single consistent definition and
// no #undef bridge is needed.
#include "fix/c_api.h"

extern "C" {

/**
 * fixpp_version — return the C-ABI surface version.
 *
 * Values sourced from the version.h macros (FIXPP_C_ABI_VERSION_{MAJOR,MINOR,PATCH}),
 * guaranteed equal to what a consumer compiled against the same binary sees.
 * Zero-alloc, value-typed.
 *
 * Thread-safety: thread-safe.
 */
fixpp_version_t fixpp_version(void) {
    fixpp_version_t v;
    v.major     = FIXPP_C_ABI_VERSION_MAJOR;
    v.minor     = FIXPP_C_ABI_VERSION_MINOR;
    v.patch     = FIXPP_C_ABI_VERSION_PATCH;
    v._reserved = 0;
    return v;
}

/**
 * fixpp_library_version — return the C++ library version.
 *
 * Sourced from FIXPP_VERSION_{MAJOR,MINOR,PATCH} in fix/c_api.h (currently 0/0/1).
 * fixpp::core::FIXPP_VERSION is a constexpr string "0.0.1"; we do NOT parse it
 * at runtime — the numeric macros are the ground truth (analyze C1).
 * Zero-alloc, value-typed.
 *
 * Thread-safety: thread-safe.
 */
fixpp_version_t fixpp_library_version(void) {
    fixpp_version_t v;
    v.major     = FIXPP_VERSION_MAJOR;
    v.minor     = FIXPP_VERSION_MINOR;
    v.patch     = FIXPP_VERSION_PATCH;
    v._reserved = 0;
    return v;
}

} // extern "C"
