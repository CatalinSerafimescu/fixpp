// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/tool/interop_golden_check_main.cpp — #445.
//
// interop_golden_check --check <verbatim-admin|verbatim-poss-dup|idle-cadence|
//                                app-replay>
//                       --golden <committed golden .fix>
//                       --capture <this run's capture .fix>
//                       [--cell <id>]
//
// A small standalone tool (NOT a gtest binary) the PARENT harness invokes from
// _finalize, AFTER it has written this run's own transcript to `--capture` —
// closing the one-run lag #445 describes (the gtest used to read whatever the
// PREVIOUS run's harness invocation had left behind).
//
// stdout: exactly one line.
//   pass:          "ok: <short reason>"
//   mismatch:      "mismatch: <detail>"       (verbatim modes reuse DiffResult::detail)
//   usage / file:  "error: <reason>"          (exit 2 only; not pinned by the
//                                              design doc as "ok:"/"mismatch:" are)
// exit code: 0 pass, 1 mismatch, 2 usage error or unreadable/missing/EMPTY
// golden or capture or unparseable content. There is NO skip outcome — every
// path that used to GTEST_SKIP() now fails closed with exit 2.
//
// `--cell` is accepted and ignored beyond echoing it into error/ok text where
// convenient; it exists so the harness's invocation is self-describing in logs
// (matches the moved-from gtests, which always had a cell_id at hand for their
// diagnostic <<).
//
// [const §XV.9]: tests/-only; standard-library only (mirrors golden_diff.cpp) —
// no fixpp production dependency, no gtest.

#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "support/golden_check.hpp"
#include "support/golden_diff.hpp"

namespace {

using fixpp::interop::GoldenCheckMode;
using fixpp::interop::GoldenCheckOutcome;
using fixpp::interop::parse_golden;
using fixpp::interop::parse_golden_check_mode;
using fixpp::interop::run_golden_check;

struct Args {
    std::optional<std::string> check;
    std::optional<std::string> golden_path;
    std::optional<std::string> capture_path;
    std::optional<std::string> cell;
};

// Returns nullopt on a malformed argv (unknown flag / missing value).
std::optional<Args> parse_args(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        auto take_value = [&](std::optional<std::string>& out) -> bool {
            if (i + 1 >= argc) return false;
            out = argv[++i];
            return true;
        };
        if (arg == "--check") {
            if (!take_value(args.check)) return std::nullopt;
        } else if (arg == "--golden") {
            if (!take_value(args.golden_path)) return std::nullopt;
        } else if (arg == "--capture") {
            if (!take_value(args.capture_path)) return std::nullopt;
        } else if (arg == "--cell") {
            if (!take_value(args.cell)) return std::nullopt;
        } else {
            return std::nullopt;
        }
    }
    if (!args.check.has_value() || !args.golden_path.has_value() ||
        !args.capture_path.has_value()) {
        return std::nullopt;
    }
    return args;
}

// Reads the whole file. Returns nullopt (with `error` set) on unreadable or
// empty content — the two file-level fail-closed conditions the design
// specifies for both --golden and --capture.
std::optional<std::string> read_nonempty_file(const std::string& path, std::string& error) {
    std::ifstream file{path, std::ios::binary};
    if (!file.is_open()) {
        error = "cannot open: " + path;
        return std::nullopt;
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    std::string text = oss.str();
    if (text.empty()) {
        error = "empty file: " + path;
        return std::nullopt;
    }
    return text;
}

int usage_error(const std::string& reason) {
    std::printf("error: %s\n", reason.c_str());
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    const auto args = parse_args(argc, argv);
    if (!args.has_value()) {
        return usage_error(
            "usage: interop_golden_check --check <verbatim-admin|verbatim-poss-dup|"
            "idle-cadence|app-replay> --golden <path> --capture <path> [--cell <id>]");
    }

    const auto mode = parse_golden_check_mode(*args->check);
    if (!mode.has_value()) {
        return usage_error("unknown --check mode: " + *args->check);
    }

    std::string error;
    const auto golden_text = read_nonempty_file(*args->golden_path, error);
    if (!golden_text.has_value()) {
        return usage_error(error);
    }
    const auto capture_text = read_nonempty_file(*args->capture_path, error);
    if (!capture_text.has_value()) {
        return usage_error(error);
    }

    const auto golden_frames = parse_golden(*golden_text);
    const auto capture_frames = parse_golden(*capture_text);
    if (golden_frames.empty() || capture_frames.empty()) {
        return usage_error("unparseable content: no frames parsed from " +
                           (golden_frames.empty() ? *args->golden_path : *args->capture_path));
    }

    const GoldenCheckOutcome outcome = run_golden_check(*mode, golden_frames, capture_frames);
    if (outcome.ok) {
        std::printf("ok: %s\n", outcome.detail.c_str());
        return 0;
    }
    std::printf("mismatch: %s\n", outcome.detail.c_str());
    return 1;
}
