// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/win_crt_no_dialog.hpp
//
// On Windows, route CRT abort()/assert()/_CrtDbgReport output to stderr instead
// of a modal message box. In headless CI a blocking "Abort/Retry/Ignore" (or
// Windows Error Reporting) dialog makes an aborting test HANG FOREVER — exactly
// the failure that stalled the first Tier-2 ctest run on
// tests/config/test_load_negative_battery (the "...NoTerminate" gates). With this
// installed an abort/assert exits fast (a diagnosable failure) rather than hanging.
//
// No-op on non-Windows. Include this header in any test TU that can trigger
// abort()/assert() on malformed input. The installer runs once per process at
// dynamic-init time (before main / before gtest's RUN_ALL_TESTS); the CRT calls
// are idempotent, so including it in several TUs of the same binary is harmless.
#pragma once

#ifdef _WIN32
#include <crtdbg.h>
#include <cstdlib>

namespace fixpp_test_detail {

// Anonymous-namespace (internal-linkage) self-installer: its constructor runs at
// program startup in the including TU.
namespace {
const int win_crt_no_dialog_installed = [] {
    // abort(): suppress the message box AND the WER fault dialog — just terminate.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    // assert()/_CrtDbgReport: write to stderr, never pop the modal assertion dialog.
    for (int report_type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(report_type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(report_type, _CRTDBG_FILE_STDERR);
    }
    return 0;
}();
}  // namespace

// Reference the installer so it is never elided as an unused static.
[[maybe_unused]] inline const int& win_crt_no_dialog_ref = win_crt_no_dialog_installed;

}  // namespace fixpp_test_detail
#endif  // _WIN32
