// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/golden_check.cpp — see golden_check.hpp.

#include "golden_check.hpp"

namespace fixpp::interop {

std::optional<GoldenCheckMode> parse_golden_check_mode(std::string_view name) {
    if (name == "verbatim-admin") return GoldenCheckMode::verbatim_admin;
    if (name == "verbatim-poss-dup") return GoldenCheckMode::verbatim_poss_dup;
    if (name == "idle-cadence") return GoldenCheckMode::idle_cadence;
    if (name == "app-replay") return GoldenCheckMode::app_replay;
    return std::nullopt;
}

namespace {

GoldenCheckOutcome check_verbatim(std::span<const GoldenFrame> golden,
                                  std::span<const GoldenFrame> capture,
                                  const std::set<int>& excluded_tags) {
    const DiffResult diff = diff_transcripts(golden, capture, excluded_tags);
    if (static_cast<bool>(diff)) {
        return {.ok = true,
                .detail = std::to_string(capture.size()) + " frame(s) verbatim match"};
    }
    return {.ok = false, .detail = diff.detail};
}

// Moved verbatim (matcher + threshold) from
// hp_fix44_idle_heartbeat_cadence_test.cpp's expect_idle_cadence_or_skip().
GoldenCheckOutcome check_idle_cadence(std::span<const GoldenFrame> capture) {
    const auto is_heartbeat = [](const GoldenFrame& f) {
        const std::string_view w{reinterpret_cast<const char*>(f.bytes.data()), f.bytes.size()};
        return w.contains(
            "\x01"
            "35=0"
            "\x01");
    };
    int out_hb = 0;
    int in_hb = 0;
    for (const auto& f : capture) {
        if (!is_heartbeat(f)) continue;
        if (f.dir == '>') {
            ++out_hb;
        } else if (f.dir == '<') {
            ++in_hb;
        }
    }
    if (out_hb >= 3 && in_hb >= 3) {
        return {.ok = true,
                .detail = "out_hb=" + std::to_string(out_hb) + " in_hb=" + std::to_string(in_hb) +
                          " (>=3 each direction)"};
    }
    return {.ok = false,
            .detail = "idle-cadence: out_hb=" + std::to_string(out_hb) +
                      " in_hb=" + std::to_string(in_hb) +
                      "; expected >=3 Heartbeat(35=0) in each direction (US2-1)"};
}

// Moved verbatim (matcher + threshold) from
// hp_fix44_recovery_outbound_answer_test.cpp's expect_app_replay_or_skip().
GoldenCheckOutcome check_app_replay(std::span<const GoldenFrame> capture) {
    int replayed_nos = 0;
    for (const auto& f : capture) {
        if (f.dir != '>') continue;  // fixpp->peer only
        const std::string_view w{reinterpret_cast<const char*>(f.bytes.data()), f.bytes.size()};
        const bool is_nos = w.contains(
            "\x01"
            "35=D"
            "\x01");
        const bool poss_dup = w.contains(
            "\x01"
            "43=Y"
            "\x01");
        if (is_nos && poss_dup) ++replayed_nos;
    }
    if (replayed_nos >= 1) {
        return {.ok = true,
                .detail = std::to_string(replayed_nos) + " replayed NewOrderSingle(35=D,43=Y)"};
    }
    return {.ok = false,
            .detail =
                "app-replay: no fixpp->peer NewOrderSingle(35=D) carrying PossDupFlag(43=Y); "
                "fixpp did not REPLAY the stored app message (US3-3)"};
}

}  // namespace

GoldenCheckOutcome run_golden_check(GoldenCheckMode mode, std::span<const GoldenFrame> golden,
                                    std::span<const GoldenFrame> capture) {
    switch (mode) {
        case GoldenCheckMode::verbatim_admin:
            return check_verbatim(golden, capture, admin_profile_excluded_tags());
        case GoldenCheckMode::verbatim_poss_dup:
            return check_verbatim(golden, capture, poss_dup_profile_excluded_tags());
        case GoldenCheckMode::idle_cadence:
            return check_idle_cadence(capture);
        case GoldenCheckMode::app_replay:
            return check_app_replay(capture);
    }
    return {.ok = false, .detail = "unreachable: unknown GoldenCheckMode"};
}

}  // namespace fixpp::interop
