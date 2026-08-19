/*
 * tests/capi/reset_seqnum_policy_range_test.c — #268
 *
 * The out-of-range acceptance test for fixpp_session_config_set_reset_seqnum_policy,
 * written in C because that is the only language in which the thing it tests is
 * not itself undefined behaviour.
 *
 * ── WHY THIS TU EXISTS, AND WHY IT IS NOT A C++ gtest ───────────────────────
 *
 * The production function exists to survive an FFI caller passing a value outside
 * the enumerator range (src/capi/config.cpp — it memcpy's its parameter into an
 * `int` and switches on that, naming -fsanitize=enum in its own comment). The old
 * test for it lived in tests/capi/public_roundtrip_test.cpp and was C++. Passing
 * an out-of-range enum BY VALUE from C++ is an lvalue-to-rvalue conversion of an
 * invalid enum value, i.e. undefined behaviour in the caller, and UBSan reported
 * it on every run:
 *
 *     public_roundtrip_test.cpp:129:5: runtime error: load of value 99, which is
 *     not a valid value for type 'fixpp_reset_seqnum_policy'
 *
 * (runs 32003367497 / 32007171995 on linux-clang-ubsan; 32024674144 job
 * 95371515603 on linux-clang-libc++-ubsan). Nobody noticed for as long as it
 * existed because the lane ran UBSan in its default RECOVERABLE mode: the report
 * printed, execution continued, the process exited 0. #268 makes that lane fatal,
 * so the load has to go.
 *
 * ⚠️ THE LANGUAGE IS THE FIX, NOT A SUPPRESSION. Measured with clang-22
 * (2026-08-18) on ONE source file compiled twice, changing nothing but `-x c` vs
 * `-x c++`, both under `-fsanitize=undefined`:
 *
 *     as C     0 reports  — both the memcpy form AND a direct `(pol_t)99` cast
 *     as C++   2 reports  — one per call site, `load of value 99`
 *
 * Same source, same flags, same guard returning the same rejection in both. So
 * the finding is an artifact of exercising a C ABI from C++, and a real C client
 * cannot reach it. Writing the test in C removes the undefined behaviour instead
 * of hiding the diagnostic — strictly better than annotating the C++ call site
 * with `no_sanitize("enum")`, which was the first fix considered and rejected
 * once this TU turned out to be four lines of CMake (the pattern already exists
 * twice in this directory: capi_version_smoke.c and handles_compile_test.c).
 *
 * It is also the more faithful test: "FFI bypass" is what the production comment
 * calls the case, and this exercises it from an actual FFI caller.
 *
 * The direct cast is included deliberately. It is what a C client would more
 * plausibly write than a memcpy, it is a SECOND way into the same guard, and it
 * is UB-free in C — so covering it costs nothing here while it could not have
 * been covered at all from C++.
 *
 * Linked with LINKER_LANGUAGE CXX, like its two siblings: fixpp_capi is static
 * and drags C++ runtime symbols the C linker driver cannot resolve.
 */

#include <fix/c_api.h> /* umbrella; includes session.h, error.h, handles.h */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char* what, int got, int want) {
    printf("FAIL: %s -> %d, expected %d\n", what, got, want);
    return 1;
}

int main(void) {
    fixpp_session_config_t* cfg = NULL;
    int bad_errors = 0;

    if (fixpp_session_config_create(&cfg) != FIXPP_ERR_OK || cfg == NULL) {
        printf("FAIL: fixpp_session_config_create\n");
        return EXIT_FAILURE;
    }

    /* (1) Out of range via memcpy — mirrors the production guard's own technique. */
    {
        fixpp_reset_seqnum_policy bad;
        int raw = 99;
        memcpy(&bad, &raw, sizeof(bad));
        fixpp_error_t rc = fixpp_session_config_set_reset_seqnum_policy(cfg, bad);
        if (rc != FIXPP_ERR_CAPI_CONFIG_INVALID) {
            bad_errors += fail("set_reset_seqnum_policy(memcpy 99)", (int)rc,
                               (int)FIXPP_ERR_CAPI_CONFIG_INVALID);
        }
    }

    /* (2) Out of range via a direct cast — the form a C client writes. UB in C++,
       well-defined here, and a second entry into the same guard. */
    {
        fixpp_error_t rc =
            fixpp_session_config_set_reset_seqnum_policy(cfg, (fixpp_reset_seqnum_policy)99);
        if (rc != FIXPP_ERR_CAPI_CONFIG_INVALID) {
            bad_errors += fail("set_reset_seqnum_policy(cast 99)", (int)rc,
                               (int)FIXPP_ERR_CAPI_CONFIG_INVALID);
        }
    }

    /* (3) The three valid enumerators still map to OK — without this the test
       would pass just as well against a function that rejected EVERYTHING. */
    {
        const fixpp_reset_seqnum_policy ok_values[] = {FIXPP_RESET_SEQNUM_BILATERAL_STRICT,
                                                       FIXPP_RESET_SEQNUM_BILATERAL_LENIENT,
                                                       FIXPP_RESET_SEQNUM_UNILATERAL};
        size_t i;
        for (i = 0; i < sizeof(ok_values) / sizeof(ok_values[0]); ++i) {
            fixpp_error_t rc = fixpp_session_config_set_reset_seqnum_policy(cfg, ok_values[i]);
            if (rc != FIXPP_ERR_OK) {
                bad_errors += fail("set_reset_seqnum_policy(valid)", (int)rc, (int)FIXPP_ERR_OK);
            }
        }
    }

    /* (4) NULL handle is still NULL_HANDLE, not CONFIG_INVALID — the two rejection
       paths must stay distinguishable from a C caller. */
    {
        fixpp_error_t rc = fixpp_session_config_set_reset_seqnum_policy(
            NULL, FIXPP_RESET_SEQNUM_BILATERAL_LENIENT);
        if (rc != FIXPP_ERR_NULL_HANDLE) {
            bad_errors += fail("set_reset_seqnum_policy(NULL cfg)", (int)rc,
                               (int)FIXPP_ERR_NULL_HANDLE);
        }
    }

    fixpp_session_config_destroy(cfg);

    if (bad_errors != 0) {
        printf("FAIL: %d assertion(s) failed\n", bad_errors);
        return EXIT_FAILURE;
    }
    printf("OK: reset_seqnum_policy range checks (memcpy, cast, 3 valid, NULL)\n");
    return EXIT_SUCCESS;
}
