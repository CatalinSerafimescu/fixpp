// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/golden_diff.hpp
//
// Golden FIX byte-stream transcript parsing and normalization helpers for the
// interop harness. Standard-library only; no production fixpp dependencies.
#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fixpp::interop {

struct GoldenFrame {
    char dir = '>';
    std::vector<std::byte> bytes;
};

enum class DiffStatus : std::uint8_t { match, mismatch };

struct DiffResult {
    DiffStatus status = DiffStatus::match;
    std::string detail;

    explicit operator bool() const
    {
        return status == DiffStatus::match;
    }
};

inline const std::set<int>& default_normalization_tags()
{
    static const std::set<int> tags{9, 10, 34, 52, 60, 112, 122};
    return tags;
}

// 018-interop-live-admin: admin normalization profile for G1 cells.
//
// Excludes ONLY {52 (SendingTime), 10 (CheckSum)} — anchored to
// specs/018-interop-live-admin/contracts/golden-admin-transcript-format.md
// "Normalization" section.  All other tags — including 112 (TestReqID echo
// correlation), 34 (MsgSeqNum), 7/16 (ResendRequest range), 43 (PossDupFlag),
// 122 (OrigSendingTime, replay evidence), 123 (GapFillFlag) — are matched
// verbatim.  DO NOT substitute default_normalization_tags() for G1: it drops
// 112/34/122 which are exactly the tags G1 asserts (FR-001/FR-003/FR-004a).
// Usage: diff_transcripts(expected, actual, admin_profile_excluded_tags())
inline const std::set<int>& admin_profile_excluded_tags()
{
    static const std::set<int> tags{52, 10};
    return tags;
}

std::vector<GoldenFrame> parse_golden(std::string_view text);

DiffResult diff_transcripts(std::span<const GoldenFrame> expected,
                            std::span<const GoldenFrame> actual,
                            const std::set<int>& excluded_tags = default_normalization_tags());

}  // namespace fixpp::interop
