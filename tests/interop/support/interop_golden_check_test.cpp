// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/interop_golden_check_test.cpp — #445.
//
// Regression test for the interop_golden_check CLI tool. Invokes the REAL
// built binary (INTEROP_GOLDEN_CHECK_BIN, injected via
// $<TARGET_FILE:interop_golden_check>) so the CLI surface itself — argv
// parsing, exit codes, the one-line stdout contract — is under test, not just
// the shared golden_check.cpp logic.
//
// Process-spawn plumbing (quote()/run_system()/exit_code()) mirrors the
// established portable pattern in tests/codegen/determinism_test.cpp
// (std::system() through the platform shell; POSIX wait(2)-encodes the
// status, Windows does not).
//
// [const §XV.9]: tests/-only.

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Process/fixture plumbing (portable — see file header).
// ---------------------------------------------------------------------------

std::string quote(const std::string& s) {
#ifdef _WIN32
    return "\"" + s + "\"";
#else
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
#endif
}

int run_system(const std::string& cmd) {
#ifdef _WIN32
    return std::system(("\"" + cmd + "\"").c_str());
#else
    return std::system(cmd.c_str());
#endif
}

int exit_code(int system_ret) {
#ifdef _WIN32
    return system_ret;
#else
    return WIFEXITED(system_ret) ? WEXITSTATUS(system_ret) : -1;
#endif
}

std::string unique_tag() {
    static std::atomic<int> counter{0};
    std::ostringstream oss;
    oss << std::this_thread::get_id() << "_" << counter++;
    return oss.str();
}

// One temp file per call, unique per (test name, counter) so -j10 parallel
// ctest runs never collide.
fs::path make_temp_file(const std::string& suffix, const std::string& content) {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string stem =
        std::string(info->test_suite_name()) + "_" + info->name() + "_" + unique_tag();
    const fs::path path = fs::temp_directory_path() / (stem + "_" + suffix + ".fix");
    std::ofstream f{path, std::ios::binary};
    f << content;
    f.close();
    return path;
}

struct RunResult {
    int code = -1;
    std::string stdout_line;  // trailing newline stripped; must be exactly one line.
};

RunResult run_tool(const std::vector<std::string>& args) {
    const fs::path out_file =
        fs::temp_directory_path() / ("interop_golden_check_stdout_" + unique_tag() + ".txt");

    std::string cmd = quote(INTEROP_GOLDEN_CHECK_BIN);
    for (const auto& a : args) {
        cmd += " " + quote(a);
    }
    cmd += " > " + quote(out_file.string());

    const int ret = run_system(cmd);

    std::string out;
    {
        std::ifstream f{out_file, std::ios::binary};
        std::ostringstream oss;
        oss << f.rdbuf();
        out = oss.str();
    }
    std::error_code ec;
    fs::remove(out_file, ec);  // best-effort cleanup

    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
        out.pop_back();
    }
    return {.code = exit_code(ret), .stdout_line = out};
}

// A single, well-formed frame most tests start from. SOH rendered as the
// checked-in "\x01" escape (golden_diff.hpp::decode_frame_bytes decodes it).
const char* kBaseFrame =
    "> 8=FIX.4.4\\x0135=0\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
    "\\x0134=1\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";

}  // namespace

// ---------------------------------------------------------------------------
// verbatim-admin
// ---------------------------------------------------------------------------

TEST(InteropGoldenCheck, VerbatimAdminIdenticalPasses) {
    const auto golden = make_temp_file("golden", kBaseFrame);
    const auto capture = make_temp_file("capture", kBaseFrame);
    const auto r = run_tool({"--check", "verbatim-admin", "--golden", golden.string(), "--capture",
                             capture.string()});
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(r.stdout_line.rfind("ok: ", 0), 0U) << "stdout: " << r.stdout_line;
}

TEST(InteropGoldenCheck, VerbatimAdminRealDifferenceMismatches) {
    const auto golden = make_temp_file("golden", kBaseFrame);
    const char* different =
        "> 8=FIX.4.4\\x0135=1\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x0134=1\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
    const auto capture = make_temp_file("capture", different);
    const auto r = run_tool({"--check", "verbatim-admin", "--golden", golden.string(), "--capture",
                             capture.string()});
    EXPECT_EQ(r.code, 1);
    EXPECT_EQ(r.stdout_line.rfind("mismatch: ", 0), 0U) << "stdout: " << r.stdout_line;
}

TEST(InteropGoldenCheck, VerbatimAdminExcludedTagsDoNotMaskADifferentTag) {
    // 52 (SendingTime) and 10 (CheckSum) differ — both excluded under the admin
    // profile — but everything else is identical, so this must still PASS.
    const auto golden = make_temp_file("golden", kBaseFrame);
    const char* differs_only_in_excluded =
        "> 8=FIX.4.4\\x0135=0\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x0134=1\\x0152=20260603-11:11:11.000\\x0110=999\\x01\n";
    const auto capture = make_temp_file("capture", differs_only_in_excluded);
    const auto r = run_tool({"--check", "verbatim-admin", "--golden", golden.string(), "--capture",
                             capture.string()});
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(r.stdout_line.rfind("ok: ", 0), 0U) << "stdout: " << r.stdout_line;
}

// ---------------------------------------------------------------------------
// verbatim-poss-dup — the extra exclusion (122, OrigSendingTime) behaves as
// the profile says: a 122-only difference is a MISMATCH under verbatim-admin
// but an OK under verbatim-poss-dup, on the SAME pair of files (also proves
// the two modes are not accidentally aliased to one implementation).
// ---------------------------------------------------------------------------

TEST(InteropGoldenCheck, PossDupProfileExcludesTag122ButAdminDoesNot) {
    const char* golden_text =
        "> 8=FIX.4.4\\x0135=D\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x0143=Y\\x01122=20260603-10:00:00.000\\x0134=1"
        "\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
    const char* differs_only_in_122 =
        "> 8=FIX.4.4\\x0135=D\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x0143=Y\\x01122=20260603-09:59:59.000\\x0134=1"
        "\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
    const auto golden = make_temp_file("golden", golden_text);
    const auto capture = make_temp_file("capture", differs_only_in_122);

    const auto admin = run_tool({"--check", "verbatim-admin", "--golden", golden.string(),
                                 "--capture", capture.string()});
    EXPECT_EQ(admin.code, 1) << "admin profile does not exclude 122; stdout: "
                             << admin.stdout_line;
    EXPECT_EQ(admin.stdout_line.rfind("mismatch: ", 0), 0U);

    const auto poss_dup = run_tool({"--check", "verbatim-poss-dup", "--golden", golden.string(),
                                    "--capture", capture.string()});
    EXPECT_EQ(poss_dup.code, 0) << "poss-dup profile excludes 122; stdout: "
                                << poss_dup.stdout_line;
    EXPECT_EQ(poss_dup.stdout_line.rfind("ok: ", 0), 0U);
}

// ---------------------------------------------------------------------------
// idle-cadence
// ---------------------------------------------------------------------------

TEST(InteropGoldenCheck, IdleCadenceBelowThresholdMismatches) {
    const auto golden = make_temp_file("golden", kBaseFrame);
    // Only 2 Heartbeat(35=0) each direction — below the >=3 threshold.
    std::string capture_text;
    for (int i = 0; i < 2; ++i) {
        capture_text +=
            "> 8=FIX.4.4\\x0135=0\\x0149=FIXPP_INIT\\x0156=CPTY_ACC\\x0134=" +
            std::to_string(i + 1) + "\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
        capture_text +=
            "< 8=FIX.4.4\\x0135=0\\x0149=CPTY_ACC\\x0156=FIXPP_INIT\\x0134=" +
            std::to_string(i + 1) + "\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
    }
    const auto capture = make_temp_file("capture", capture_text);
    const auto r = run_tool({"--check", "idle-cadence", "--golden", golden.string(), "--capture",
                             capture.string()});
    EXPECT_EQ(r.code, 1);
    EXPECT_EQ(r.stdout_line.rfind("mismatch: ", 0), 0U) << "stdout: " << r.stdout_line;
}

TEST(InteropGoldenCheck, IdleCadenceAtThresholdPasses) {
    const auto golden = make_temp_file("golden", kBaseFrame);
    std::string capture_text;
    for (int i = 0; i < 3; ++i) {
        capture_text +=
            "> 8=FIX.4.4\\x0135=0\\x0149=FIXPP_INIT\\x0156=CPTY_ACC\\x0134=" +
            std::to_string(i + 1) + "\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
        capture_text +=
            "< 8=FIX.4.4\\x0135=0\\x0149=CPTY_ACC\\x0156=FIXPP_INIT\\x0134=" +
            std::to_string(i + 1) + "\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
    }
    const auto capture = make_temp_file("capture", capture_text);
    const auto r = run_tool({"--check", "idle-cadence", "--golden", golden.string(), "--capture",
                             capture.string()});
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(r.stdout_line.rfind("ok: ", 0), 0U) << "stdout: " << r.stdout_line;
}

// ---------------------------------------------------------------------------
// app-replay
// ---------------------------------------------------------------------------

TEST(InteropGoldenCheck, AppReplayMissingFrameMismatches) {
    const auto golden = make_temp_file("golden", kBaseFrame);
    // NewOrderSingle present but NOT a replay (no 43=Y) — no replay witnessed.
    const char* no_replay =
        "> 8=FIX.4.4\\x0135=D\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x0134=2\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
    const auto capture = make_temp_file("capture", no_replay);
    const auto r = run_tool({"--check", "app-replay", "--golden", golden.string(), "--capture",
                             capture.string()});
    EXPECT_EQ(r.code, 1);
    EXPECT_EQ(r.stdout_line.rfind("mismatch: ", 0), 0U) << "stdout: " << r.stdout_line;
}

TEST(InteropGoldenCheck, AppReplayPresentPasses) {
    const auto golden = make_temp_file("golden", kBaseFrame);
    const char* replayed =
        "> 8=FIX.4.4\\x0135=D\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x0143=Y\\x01122=20260603-09:59:59.000\\x0134=2"
        "\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
    const auto capture = make_temp_file("capture", replayed);
    const auto r = run_tool({"--check", "app-replay", "--golden", golden.string(), "--capture",
                             capture.string()});
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(r.stdout_line.rfind("ok: ", 0), 0U) << "stdout: " << r.stdout_line;
}

// ---------------------------------------------------------------------------
// Fail-closed file / usage errors — exit 2, never a skip.
// ---------------------------------------------------------------------------

TEST(InteropGoldenCheck, MissingGoldenExitsTwo) {
    const auto capture = make_temp_file("capture", kBaseFrame);
    const auto r = run_tool({"--check", "verbatim-admin", "--golden",
                             "/nonexistent/path/golden.fix", "--capture", capture.string()});
    EXPECT_EQ(r.code, 2);
    EXPECT_EQ(r.stdout_line.rfind("error: ", 0), 0U) << "stdout: " << r.stdout_line;
}

TEST(InteropGoldenCheck, MissingCaptureExitsTwo) {
    const auto golden = make_temp_file("golden", kBaseFrame);
    const auto r = run_tool({"--check", "verbatim-admin", "--golden", golden.string(), "--capture",
                             "/nonexistent/path/capture.fix"});
    EXPECT_EQ(r.code, 2);
    EXPECT_EQ(r.stdout_line.rfind("error: ", 0), 0U) << "stdout: " << r.stdout_line;
}

TEST(InteropGoldenCheck, EmptyGoldenExitsTwo) {
    const auto golden = make_temp_file("golden", "");
    const auto capture = make_temp_file("capture", kBaseFrame);
    const auto r = run_tool({"--check", "verbatim-admin", "--golden", golden.string(), "--capture",
                             capture.string()});
    EXPECT_EQ(r.code, 2);
    EXPECT_EQ(r.stdout_line.rfind("error: ", 0), 0U) << "stdout: " << r.stdout_line;
}

TEST(InteropGoldenCheck, EmptyCaptureExitsTwo) {
    const auto golden = make_temp_file("golden", kBaseFrame);
    const auto capture = make_temp_file("capture", "");
    const auto r = run_tool({"--check", "verbatim-admin", "--golden", golden.string(), "--capture",
                             capture.string()});
    EXPECT_EQ(r.code, 2);
    EXPECT_EQ(r.stdout_line.rfind("error: ", 0), 0U) << "stdout: " << r.stdout_line;
}

TEST(InteropGoldenCheck, UnknownCheckModeExitsTwo) {
    const auto golden = make_temp_file("golden", kBaseFrame);
    const auto capture = make_temp_file("capture", kBaseFrame);
    const auto r = run_tool({"--check", "not-a-real-mode", "--golden", golden.string(), "--capture",
                             capture.string()});
    EXPECT_EQ(r.code, 2);
    EXPECT_EQ(r.stdout_line.rfind("error: ", 0), 0U) << "stdout: " << r.stdout_line;
}

TEST(InteropGoldenCheck, MissingRequiredFlagExitsTwo) {
    const auto golden = make_temp_file("golden", kBaseFrame);
    // --capture omitted entirely.
    const auto r = run_tool({"--check", "verbatim-admin", "--golden", golden.string()});
    EXPECT_EQ(r.code, 2);
    EXPECT_EQ(r.stdout_line.rfind("error: ", 0), 0U) << "stdout: " << r.stdout_line;
}

TEST(InteropGoldenCheck, UnparseableContentExitsTwo) {
    // Non-empty file, but no line carries any non-blank content -> zero
    // frames parsed by parse_golden().
    const auto golden = make_temp_file("golden", "\n\n\n");
    const auto capture = make_temp_file("capture", kBaseFrame);
    const auto r = run_tool({"--check", "verbatim-admin", "--golden", golden.string(), "--capture",
                             capture.string()});
    EXPECT_EQ(r.code, 2);
    EXPECT_EQ(r.stdout_line.rfind("error: ", 0), 0U) << "stdout: " << r.stdout_line;
}
