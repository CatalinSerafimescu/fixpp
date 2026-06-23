/*
 * include/fix/c_api/export.h — FIXPP_API_EXPORT visibility macro
 *
 * Included by every public C-ABI header that declares an exported symbol.
 * C-clean: no C++ headers, no C++ syntax.
 *
 * Macro selection (research D-5, [2i §4.3/§4.5]):
 *
 *   _WIN32 + FIXPP_CAPI_BUILD (building the DLL) → dllexport
 *   _WIN32                    (consuming the DLL) → dllimport
 *   POSIX (everything else)                       → visibility("default")
 *   Static-archive consumer (no symbol needed)    → empty (falls through)
 *
 * On POSIX the shipped artifact is a static archive; the test-only .so uses
 * the GNU version script (fixpp_capi.map) for §X.2 symbol filtering.
 * The macro is still present on POSIX so headers compile cleanly on Windows.
 */

#ifndef FIXPP_C_API_EXPORT_H
#define FIXPP_C_API_EXPORT_H

/* NOLINTBEGIN(cppcoreguidelines-macro-usage) */
#if defined(_WIN32)
#  if defined(FIXPP_CAPI_BUILD)
#    define FIXPP_API_EXPORT __declspec(dllexport)
#  else
#    define FIXPP_API_EXPORT __declspec(dllimport)
#  endif
#else
#  define FIXPP_API_EXPORT __attribute__((visibility("default")))
#endif
/* NOLINTEND(cppcoreguidelines-macro-usage) */

#endif /* FIXPP_C_API_EXPORT_H */
