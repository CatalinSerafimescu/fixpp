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

#include <algorithm>
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

    std::string raw;
    {
        std::ifstream f{out_file, std::ios::binary};
        std::ostringstream oss;
        oss << f.rdbuf();
        raw = oss.str();
    }
    std::error_code ec;
    fs::remove(out_file, ec);  // best-effort cleanup

    // The binding one-line stdout contract (file header): exactly one line,
    // terminated by exactly one trailing newline (a trailing "\r\n" on
    // Windows collapses to one newline for this count). Assert on the RAW
    // bytes, before any stripping — a stripped copy cannot see an embedded
    // newline the strip already removed.
    const auto newline_count = std::count(raw.begin(), raw.end(), '\n');
    EXPECT_EQ(newline_count, 1) << "not exactly one line of stdout: " << raw;
    EXPECT_FALSE(raw.empty()) << "expected non-empty stdout";
    if (!raw.empty()) {
        EXPECT_EQ(raw.back(), '\n') << "stdout does not end with a newline: " << raw;
    }

    std::string out = raw;
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

// ---------------------------------------------------------------------------
// Branches the cases above do not reach: each direction of the idle-cadence
// threshold on its own, the direction filter of app-replay, and the two argv
// shapes parse_args rejects besides an omitted flag.
// ---------------------------------------------------------------------------

namespace {
std::string heartbeats(char dir, int count) {
    std::string text;
    for (int i = 0; i < count; ++i) {
        text += std::string{dir} + " 8=FIX.4.4\\x0135=0\\x0149=A\\x0156=B\\x0134=" +
                std::to_string(i + 1) + "\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
    }
    return text;
}
}  // namespace

TEST(InteropGoldenCheck, IdleCadenceOneDirectionShortMismatches) {
    const auto golden = make_temp_file("golden", kBaseFrame);
    for (const auto& text : {heartbeats('>', 3) + heartbeats('<', 2),
                             heartbeats('>', 2) + heartbeats('<', 3)}) {
        const auto capture = make_temp_file("capture", text);
        const auto r = run_tool({"--check", "idle-cadence", "--golden", golden.string(),
                                 "--capture", capture.string()});
        EXPECT_EQ(r.code, 1) << text;
        EXPECT_EQ(r.stdout_line.rfind("mismatch: ", 0), 0U) << "stdout: " << r.stdout_line;
    }
}

TEST(InteropGoldenCheck, AppReplayFromThePeerDirectionDoesNotCount) {
    const auto golden = make_temp_file("golden", kBaseFrame);
    // The replayed NewOrderSingle must be fixpp->peer ('>'); the same frame
    // arriving from the peer ('<') is not fixpp replaying its stored message.
    const char* peer_replay =
        "< 8=FIX.4.4\\x0135=D\\x0149=CPTY_ACC\\x0156=FIXPP_INIT"
        "\\x0143=Y\\x01122=20260603-09:59:59.000\\x0134=2"
        "\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
    const auto capture = make_temp_file("capture", peer_replay);
    const auto r = run_tool({"--check", "app-replay", "--golden", golden.string(), "--capture",
                             capture.string()});
    EXPECT_EQ(r.code, 1);
    EXPECT_EQ(r.stdout_line.rfind("mismatch: ", 0), 0U) << "stdout: " << r.stdout_line;
}

TEST(InteropGoldenCheck, UnknownFlagExitsTwo) {
    const auto golden = make_temp_file("golden", kBaseFrame);
    const auto capture = make_temp_file("capture", kBaseFrame);
    const auto r = run_tool({"--check", "verbatim-admin", "--golden", golden.string(), "--capture",
                             capture.string(), "--bogus", "x"});
    EXPECT_EQ(r.code, 2);
    EXPECT_EQ(r.stdout_line.rfind("error: ", 0), 0U) << "stdout: " << r.stdout_line;
}

TEST(InteropGoldenCheck, FlagWithoutValueExitsTwo) {
    const auto golden = make_temp_file("golden", kBaseFrame);
    const auto capture = make_temp_file("capture", kBaseFrame);
    const auto r = run_tool({"--golden", golden.string(), "--capture", capture.string(), "--check"});
    EXPECT_EQ(r.code, 2);
    EXPECT_EQ(r.stdout_line.rfind("error: ", 0), 0U) << "stdout: " << r.stdout_line;
}

// ---------------------------------------------------------------------------
// Gate B r2 F2/L7: --cell (an optional, echoed-only flag) and the missing-
// value arms of --golden/--capture/--cell that FlagWithoutValueExitsTwo above
// does not reach (it only leaves --check's value off).
// ---------------------------------------------------------------------------

TEST(InteropGoldenCheck, CellFlagIsAcceptedAndDoesNotChangeTheVerdict) {
    const auto golden = make_temp_file("golden", kBaseFrame);
    const auto identical_capture = make_temp_file("capture", kBaseFrame);
    const auto pass = run_tool({"--check", "verbatim-admin", "--golden", golden.string(), "--capture",
                                identical_capture.string(), "--cell", "TEST-cell"});
    EXPECT_EQ(pass.code, 0) << "stdout: " << pass.stdout_line;
    EXPECT_EQ(pass.stdout_line.rfind("ok: ", 0), 0U) << "stdout: " << pass.stdout_line;

    // --cell must not turn a genuine mismatch into a pass either.
    const char* different =
        "> 8=FIX.4.4\\x0135=1\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x0134=1\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
    const auto different_capture = make_temp_file("capture", different);
    const auto fail = run_tool({"--check", "verbatim-admin", "--golden", golden.string(), "--capture",
                                different_capture.string(), "--cell", "TEST-cell"});
    EXPECT_EQ(fail.code, 1) << "stdout: " << fail.stdout_line;
    EXPECT_EQ(fail.stdout_line.rfind("mismatch: ", 0), 0U) << "stdout: " << fail.stdout_line;
}

// Condition this pins: a missing value for --golden/--capture/--cell exits 2
// with "error: ", both (a) as the trailing token with no prior occurrence of
// that flag, and (b) repeated as the trailing token AFTER the flag was
// already satisfied once. Shape (b) exists because take_value() on failure
// leaves the destination optional untouched rather than clearing it — for a
// flag also checked for presence after the parse loop (--golden, --capture),
// shape (a) alone cannot tell "the early return fired" from "the flag was
// simply never given" (the post-loop presence check catches both identically).
// --cell carries no such post-loop check, so shape (a) already discriminates
// for it; shape (b) is included for --cell too, for symmetry, and is
// harmless there.
TEST(InteropGoldenCheck, MissingValueForGoldenCaptureOrCellExitsTwo) {
    const auto golden = make_temp_file("golden", kBaseFrame);
    const auto capture = make_temp_file("capture", kBaseFrame);

    struct Case {
        const char* name;
        std::vector<std::string> args;
    };
    const std::vector<Case> cases = {
        {"--golden (trailing, first occurrence)",
         {"--check", "verbatim-admin", "--capture", capture.string(), "--golden"}},
        {"--golden (trailing, repeated after a satisfied occurrence)",
         {"--check", "verbatim-admin", "--golden", golden.string(), "--capture", capture.string(),
          "--golden"}},
        {"--capture (trailing, first occurrence)",
         {"--check", "verbatim-admin", "--golden", golden.string(), "--capture"}},
        {"--capture (trailing, repeated after a satisfied occurrence)",
         {"--check", "verbatim-admin", "--golden", golden.string(), "--capture", capture.string(),
          "--capture"}},
        {"--cell (trailing, first occurrence)",
         {"--check", "verbatim-admin", "--golden", golden.string(), "--capture", capture.string(),
          "--cell"}},
        {"--cell (trailing, repeated after a satisfied occurrence)",
         {"--check", "verbatim-admin", "--golden", golden.string(), "--capture", capture.string(),
          "--cell", "TEST-cell", "--cell"}},
    };

    int failures = 0;
    for (const auto& c : cases) {
        const auto r = run_tool(c.args);
        if (r.code != 2 || r.stdout_line.rfind("error: ", 0) != 0U) {
            ++failures;
            ADD_FAILURE() << "flag " << c.name
                          << " missing value did not exit 2 with \"error: \"; got code=" << r.code
                          << " stdout=" << r.stdout_line;
        }
    }
    EXPECT_EQ(failures, 0);
}

// ---------------------------------------------------------------------------
// Structural validation before mode dispatch (Gate B r1 L3). idle-cadence and
// app-replay only substring-search the capture and never structurally parse
// the golden/capture at all; without a validation pass ahead of dispatch,
// these two modes could report `ok:` over content that is not well-formed
// FIX. Verbatim modes already fail closed on malformed input via
// diff_transcripts, but with the WRONG exit code (1, mismatch, rather than 2,
// unparseable content) — see the garbage-golden case below.
// ---------------------------------------------------------------------------

TEST(InteropGoldenCheck, AppReplayGarbageGoldenExitsTwo) {
    // A nonblank golden line with no "> "/"< " prefix parses to dir='?' — not
    // a structural defect in the FIELDS, but an invalid direction, which is
    // exactly as unparseable. app-replay never reads the golden's content, so
    // without validation ahead of dispatch this would fall through to the
    // "at least one replayed frame" capture-only check and could still pass.
    const auto golden = make_temp_file("golden", "garbage\n");
    const char* replayed =
        "> 8=FIX.4.4\\x0135=D\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x0143=Y\\x0134=2\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
    const auto capture = make_temp_file("capture", replayed);
    const auto r = run_tool({"--check", "app-replay", "--golden", golden.string(), "--capture",
                             capture.string()});
    EXPECT_EQ(r.code, 2) << "stdout: " << r.stdout_line;
    EXPECT_EQ(r.stdout_line.rfind("error: ", 0), 0U) << "stdout: " << r.stdout_line;
}

TEST(InteropGoldenCheck, AppReplayMalformedCaptureFieldExitsTwo) {
    // A well-formed golden, but a capture frame with a body segment that has
    // no '=' between two SOHs — a structural defect check_app_replay's
    // substring search cannot see.
    const auto golden = make_temp_file("golden", kBaseFrame);
    const char* malformed_capture = "> garbage\\x0135=D\\x01not-a-field\\x0143=Y\\x01\n";
    const auto capture = make_temp_file("capture", malformed_capture);
    const auto r = run_tool({"--check", "app-replay", "--golden", golden.string(), "--capture",
                             capture.string()});
    EXPECT_EQ(r.code, 2) << "stdout: " << r.stdout_line;
    EXPECT_EQ(r.stdout_line.rfind("error: ", 0), 0U) << "stdout: " << r.stdout_line;
}

TEST(InteropGoldenCheck, IdleCadenceMalformedFramesExitTwo) {
    // Six frames (3 each direction) each carrying the "35=0" substring
    // check_idle_cadence searches for, but each structurally malformed (no
    // '=' in the first field) — check_idle_cadence's substring search alone
    // would count all six as Heartbeats and report `ok:`.
    const auto golden = make_temp_file("golden", kBaseFrame);
    std::string capture_text;
    for (int i = 0; i < 3; ++i) {
        capture_text += "> junk\\x0135=0\\x01\n";
        capture_text += "< junk\\x0135=0\\x01\n";
    }
    const auto capture = make_temp_file("capture", capture_text);
    const auto r = run_tool({"--check", "idle-cadence", "--golden", golden.string(), "--capture",
                             capture.string()});
    EXPECT_EQ(r.code, 2) << "stdout: " << r.stdout_line;
    EXPECT_EQ(r.stdout_line.rfind("error: ", 0), 0U) << "stdout: " << r.stdout_line;
}

TEST(InteropGoldenCheck, VerbatimAdminGarbageGoldenExitsTwoNotOne) {
    // Before L3, a golden with no valid direction fell through to
    // diff_transcripts, which reports a `direction` mismatch (exit 1) rather
    // than the "unparseable content" usage error (exit 2) the file header
    // promises for exactly this case.
    const auto golden = make_temp_file("golden", "garbage\n");
    const auto capture = make_temp_file("capture", kBaseFrame);
    const auto r = run_tool({"--check", "verbatim-admin", "--golden", golden.string(), "--capture",
                             capture.string()});
    EXPECT_EQ(r.code, 2) << "stdout: " << r.stdout_line;
    EXPECT_EQ(r.stdout_line.rfind("error: ", 0), 0U) << "stdout: " << r.stdout_line;
}

// ---------------------------------------------------------------------------
// Tag-accumulator overflow (Gate B r1 L4). parse_tag() used to accumulate an
// arbitrary-length decimal string into `int` with no bound; a tag long enough
// to wrap past INT_MAX could alias a different, valid tag number and produce
// a false verbatim MATCH rather than a mismatch or a parse failure.
// ---------------------------------------------------------------------------

TEST(InteropGoldenCheck, OverflowingTagDoesNotAliasAsAMatch) {
    // 4294967331 = 2^32 + 35 — wraps to 35 in a naive 32-bit accumulator, which
    // would make this golden falsely byte-match a capture whose real tag is 35.
    const auto golden = make_temp_file("golden", "> 4294967331=1\\x01\n");
    const auto capture = make_temp_file("capture", "> 35=1\\x01\n");
    const auto r = run_tool({"--check", "verbatim-admin", "--golden", golden.string(), "--capture",
                             capture.string()});
    EXPECT_NE(r.code, 0) << "an overflowing tag must never report `ok:`; stdout: "
                         << r.stdout_line;
    EXPECT_EQ(r.code, 2) << "stdout: " << r.stdout_line;
}

TEST(InteropGoldenCheck, OverflowingTagControlPairStillMismatches) {
    // Control: the instrument can report a genuine non-match on a
    // (non-overflowing) differing tag, proving the case above is not simply
    // "everything mismatches".
    const auto golden = make_temp_file("golden", "> 36=1\\x01\n");
    const auto capture = make_temp_file("capture", "> 35=1\\x01\n");
    const auto r = run_tool({"--check", "verbatim-admin", "--golden", golden.string(), "--capture",
                             capture.string()});
    EXPECT_EQ(r.code, 1) << "stdout: " << r.stdout_line;
    EXPECT_EQ(r.stdout_line.rfind("mismatch: ", 0), 0U) << "stdout: " << r.stdout_line;
}
