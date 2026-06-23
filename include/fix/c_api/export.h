/*
 * include/fix/c_api/export.h — FIXPP_API_EXPORT visibility macro
 *
 * Included by every public C-ABI header that declares an exported symbol.
 * C-clean: no C++ headers, no C++ syntax.
 *
 * Macro selection (research D-5, [2i §4.3/§4.5]):
 *
 *   _WIN32 + FIXPP_CAPI_SHARED + FIXPP_CAPI_BUILD (building the shared DLL) → dllexport
 *   _WIN32 + FIXPP_CAPI_SHARED                    (consuming the shared DLL) → dllimport
 *   POSIX (everything else)                                                   → visibility("default")
 *   Static archive default (incl. _WIN32 with no FIXPP_CAPI_SHARED)          → empty (falls through)
 *
 * The shipped artifact is the STATIC archive fixpp_capi; FIXPP_CAPI_SHARED is
 * not set by any target today, so the _WIN32 default is correctly empty.
 * The test-only shared lib uses WINDOWS_EXPORT_ALL_SYMBOLS ON and does not
 * require FIXPP_CAPI_SHARED.  The GNU version script (fixpp_capi.map) handles
 * §X.2 symbol filtering on POSIX.
 */

#ifndef FIXPP_C_API_EXPORT_H
#define FIXPP_C_API_EXPORT_H

/* NOLINTBEGIN(cppcoreguidelines-macro-usage) */
#if defined(_WIN32) && defined(FIXPP_CAPI_SHARED)
#  if defined(FIXPP_CAPI_BUILD)
#    define FIXPP_API_EXPORT __declspec(dllexport)   /* building the shared DLL */
#  else
#    define FIXPP_API_EXPORT __declspec(dllimport)   /* consuming the shared DLL */
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define FIXPP_API_EXPORT __attribute__((visibility("default")))  /* POSIX — unchanged */
#else
#  define FIXPP_API_EXPORT                            /* static archive (incl. _WIN32 default) → empty */
#endif
/* NOLINTEND(cppcoreguidelines-macro-usage) */

#endif /* FIXPP_C_API_EXPORT_H */
