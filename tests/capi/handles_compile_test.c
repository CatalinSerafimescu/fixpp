/*
 * tests/capi/handles_compile_test.c — US3 (T016, SC-003 / FR-004)
 *
 * Pure-C compile+link smoke for the opaque handle catalogue. Includes ONLY the
 * umbrella <fix/c_api.h>, declares a pointer of each of the five opaque handle
 * typedefs, references the null/invalid-handle codes, and exercises an exported
 * function (fixpp_strerror) so this is a genuine link smoke. MUST compile as C
 * (no C++ headers pulled) and link against fixpp_capi.
 */

#include <fix/c_api.h>  /* umbrella: export.h, error.h, version.h, handles.h, decimal.h */

int main(void) {
    /* A pointer of each of the five opaque handle typedefs (definitions are
       engine-internal; only the incomplete forward types are needed here). */
    fixpp_engine_t*  engine  = NULL;
    fixpp_session_t* session = NULL;
    fixpp_msg_t*     msg     = NULL;
    fixpp_dict_t*    dict    = NULL;
    fixpp_store_t*   store   = NULL;

    (void)engine;
    (void)session;
    (void)msg;
    (void)dict;
    (void)store;

    /* Reference the null/invalid-handle codes (published by error.h) and call
       a real exported function to force the link. */
    const char* s = fixpp_strerror(FIXPP_ERR_NULL_HANDLE);
    return (s != NULL && FIXPP_ERR_INVALID_HANDLE == 4) ? 0 : 1;
}
