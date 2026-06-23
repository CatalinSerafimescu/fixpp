/*
 * tests/capi/capi_version_smoke.c — pure-C compile+link smoke (SC-001, T005)
 *
 * Includes ONLY fix/c_api/version.h (no umbrella, no C++ header).
 * Verifies both accessors are callable and their .major matches the macro.
 *
 * NOTE: This TU is compiled by the C compiler to prove the header is C-clean.
 * It is linked with LINKER_LANGUAGE CXX because fixpp_capi (static) drags in
 * C++ runtime symbols (libstdc++/__cxa_*).
 *
 * Do NOT define FIXPP_CAPI_BUILD here — this is a consumer TU, not the library
 * builder; on _WIN32 the macro selects dllimport (the consuming side).
 */

#include <fix/c_api/version.h>
#include <stdio.h>  /* printf */
#include <stdlib.h> /* EXIT_SUCCESS / EXIT_FAILURE */

int main(void) {
    fixpp_version_t cabi = fixpp_version();
    fixpp_version_t lib  = fixpp_library_version();

    /* Compare C-ABI major against the compile-time macro (SC-001) */
    if (cabi.major != (unsigned short)FIXPP_C_ABI_VERSION_MAJOR) {
        printf("FAIL: fixpp_version().major=%u != FIXPP_C_ABI_VERSION_MAJOR=%u\n",
               (unsigned)cabi.major, (unsigned)FIXPP_C_ABI_VERSION_MAJOR);
        return EXIT_FAILURE;
    }
    if (cabi.minor != (unsigned short)FIXPP_C_ABI_VERSION_MINOR) {
        printf("FAIL: fixpp_version().minor=%u != FIXPP_C_ABI_VERSION_MINOR=%u\n",
               (unsigned)cabi.minor, (unsigned)FIXPP_C_ABI_VERSION_MINOR);
        return EXIT_FAILURE;
    }
    if (cabi.patch != (unsigned short)FIXPP_C_ABI_VERSION_PATCH) {
        printf("FAIL: fixpp_version().patch=%u != FIXPP_C_ABI_VERSION_PATCH=%u\n",
               (unsigned)cabi.patch, (unsigned)FIXPP_C_ABI_VERSION_PATCH);
        return EXIT_FAILURE;
    }

    /* Library version is callable and decoupled — just print it */
    printf("C-ABI  version: %u.%u.%u\n",
           (unsigned)cabi.major, (unsigned)cabi.minor, (unsigned)cabi.patch);
    printf("Library version: %u.%u.%u\n",
           (unsigned)lib.major, (unsigned)lib.minor, (unsigned)lib.patch);

    return EXIT_SUCCESS;
}
