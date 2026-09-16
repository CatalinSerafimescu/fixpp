// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/golden_check.hpp
//
// #445: the golden-vs-capture checks formerly run INSIDE each interop gtest
// against a lagging capture sidecar (hp::diff_golden_or_skip,
// expect_idle_cadence_or_skip in hp_fix44_idle_heartbeat_cadence_test.cpp,
// expect_app_replay_or_skip in hp_fix44_recovery_outbound_answer_test.cpp)
// are moved here, unchanged in logic, so there is exactly ONE copy shared by:
//   - the interop_golden_check CLI tool (tool/interop_golden_check_main.cpp),
//     invoked by the parent harness's _finalize AFTER it has written THIS
//     run's own capture — closing the one-run lag #445 describes;
//   - support/interop_golden_check_test.cpp (this tool's own regression test).
//
// Standard-library only; no gtest, no fixpp production dependency — same
// constraint as golden_diff.hpp/.cpp (this header only builds on top of it).
#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "golden_diff.hpp"

namespace fixpp::interop {

enum class GoldenCheckMode { verbatim_admin, verbatim_poss_dup, idle_cadence, app_replay };

// Maps a --check CLI token to a mode. Returns nullopt for an unrecognized token
// (CLI: usage error, exit 2).
std::optional<GoldenCheckMode> parse_golden_check_mode(std::string_view name);

struct GoldenCheckOutcome {
    bool ok = false;
    // Reason (pass) or mismatch detail (fail). No "ok: "/"mismatch: " prefix —
    // the CLI prepends that; a caller printing this directly should add one.
    std::string detail;
};

// Dispatches to the check named by `mode` over already-parsed golden/capture
// frame sequences.
//
// `verbatim_admin` / `verbatim_poss_dup`: byte-for-byte diff_transcripts()
// under the {52,10} / {52,10,122} exclusion profile (golden_diff.hpp).
//
// `idle_cadence` / `app_replay`: count-tolerant / presence checks over the
// CAPTURE only (the golden argument is not read by these two — matches the
// moved-from gtest logic byte-for-byte; a golden is still a required,
// non-empty file at the CLI/file level, enforced by the caller before this
// function is reached — fail-closed, no skip outcome).
GoldenCheckOutcome run_golden_check(GoldenCheckMode mode, std::span<const GoldenFrame> golden,
                                    std::span<const GoldenFrame> capture);

}  // namespace fixpp::interop
