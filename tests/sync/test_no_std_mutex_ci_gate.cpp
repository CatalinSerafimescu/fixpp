// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/sync/test_no_std_mutex_ci_gate.cpp
//
// Seam #14 — T062: GTest driver for the US5 CI grep gate.
//
// Drives tools/check_no_std_mutex_in_awaitable_headers.sh over the T063–T065
// fixture corpus and asserts the zero-FN / zero-FP contract specified in
// [2f §6.6] / [const §XV.9] / FR-014.
//
// Environment (injected by CMake set_tests_properties):
//   SYNC_GREP_GATE_SCRIPT   — absolute path to the shell gate script
//   SYNC_FIXTURES_DIR       — absolute path to tests/sync/fixtures/
//   SYNC_GATE_INC           — project include root (CMAKE_SOURCE_DIR/include)
//   SYNC_ASIO_INC           — asio interface include directory
// ─────────────────────────────────────────────────────────────────────────────
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>

namespace {

// ─── Environment helpers ─────────────────────────────────────────────────────

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

// ─── Gate invocation helper ───────────────────────────────────────────────────
//
// Builds and runs:
//   bash '<script>' -I '<gate_inc>' -I '<asio_inc>' [<defs>...] -- '<fixture>'
//   2>'<tmpfile>'
//
// Returns the gate's exit code (WEXITSTATUS) and populates *stderr_out with
// the full stderr content.

struct GateResult {
    int  exit_code;
    std::string stderr_out;
};

GateResult run_gate(const std::string& fixture_name,
                    const std::string& extra_def = "") {
    const std::string script      = env("SYNC_GREP_GATE_SCRIPT");
    const std::string fixtures    = env("SYNC_FIXTURES_DIR");
    const std::string gate_inc    = env("SYNC_GATE_INC");
    const std::string asio_inc    = env("SYNC_ASIO_INC");

    EXPECT_FALSE(script.empty())   << "SYNC_GREP_GATE_SCRIPT not set";
    EXPECT_FALSE(fixtures.empty()) << "SYNC_FIXTURES_DIR not set";
    EXPECT_FALSE(gate_inc.empty()) << "SYNC_GATE_INC not set";
    EXPECT_FALSE(asio_inc.empty()) << "SYNC_ASIO_INC not set";

    // Write stderr to a temp file so we can read it back.
    char tmp_path[] = "/tmp/sync_gate_stderr_XXXXXX";
    int  tmp_fd = ::mkstemp(tmp_path);
    if (tmp_fd >= 0) ::close(tmp_fd);

    std::string fixture_path = fixtures + "/" + fixture_name;

    std::string def_part;
    if (!extra_def.empty()) {
        def_part = " -D" + extra_def;
    }

    std::string cmd = "bash '" + script + "'"
        + " -I '" + gate_inc + "'"
        + " -I '" + asio_inc + "'"
        + def_part
        + " -- '" + fixture_path + "'"
        + " 2>'" + std::string(tmp_path) + "'";

    int raw = std::system(cmd.c_str());
    int exit_code = WIFEXITED(raw) ? WEXITSTATUS(raw) : 127;

    // Read stderr from the temp file.
    std::ifstream ifs(tmp_path);
    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::remove(tmp_path);

    return {exit_code, oss.str()};
}

}  // namespace

// ─── Test suite ──────────────────────────────────────────────────────────────

class SyncNoStdMutexCiGate : public ::testing::Test {};

// T063: Zero false negatives — all six banned spellings each fire when the
// corresponding preprocessor guard is active.
TEST_F(SyncNoStdMutexCiGate, ViolationFixtureFiresPerSpelling) {
    struct Row {
        const char* macro;
        const char* spelling;
    };
    static constexpr Row kTable[] = {
        {"FX_MUTEX",                "std::mutex"},
        {"FX_RECURSIVE_MUTEX",      "std::recursive_mutex"},
        {"FX_TIMED_MUTEX",          "std::timed_mutex"},
        {"FX_RECURSIVE_TIMED_MUTEX","std::recursive_timed_mutex"},
        {"FX_SHARED_MUTEX",         "std::shared_mutex"},
        {"FX_SHARED_TIMED_MUTEX",   "std::shared_timed_mutex"},
    };

    for (const auto& row : kTable) {
        SCOPED_TRACE(std::string("macro=") + row.macro);
        GateResult r = run_gate("header_with_std_mutex_and_awaitable.hpp", row.macro);
        EXPECT_NE(r.exit_code, 0)
            << "Gate did not fire for " << row.macro
            << "; stderr:\n" << r.stderr_out;
        EXPECT_NE(r.stderr_out.find(row.spelling), std::string::npos)
            << "Spelling '" << row.spelling << "' not found in stderr:\n" << r.stderr_out;
        EXPECT_NE(r.stderr_out.find("VIOLATION:"), std::string::npos)
            << "'VIOLATION:' not found in stderr for " << row.macro << ":\n" << r.stderr_out;
    }
}

// T064: Zero false positives — a legitimate awaitable header that uses only
// fixpp::sync::async_mutex does NOT trigger the gate, even though asio
// internally uses std::mutex (the zero-FP guarantee).
TEST_F(SyncNoStdMutexCiGate, LegitimateAwaitableHeaderDoesNotFire) {
    GateResult r = run_gate("header_without_violation.hpp");
    EXPECT_EQ(r.exit_code, 0)
        << "Gate fired a false positive; stderr:\n" << r.stderr_out;
    EXPECT_EQ(r.stderr_out.find("VIOLATION:"), std::string::npos)
        << "Unexpected 'VIOLATION:' in stderr:\n" << r.stderr_out;
}

// T065: Transitive-awaitable catch — asio::awaitable pulled transitively (not
// directly) still puts the header in scope, and a fixture-owned std::mutex is
// correctly caught.
TEST_F(SyncNoStdMutexCiGate, TransitiveAwaitablePullIsCaught) {
    GateResult r = run_gate("header_transitive_awaitable_include.hpp");
    EXPECT_NE(r.exit_code, 0)
        << "Gate missed transitive awaitable violation; stderr:\n" << r.stderr_out;
    EXPECT_NE(r.stderr_out.find("std::mutex"), std::string::npos)
        << "'std::mutex' not found in stderr:\n" << r.stderr_out;
}
