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

// 021-inbound-possdup-origsendingtime: PossDup replay normalization profile.
//
// Extends the 018 admin profile {52,10} with tag 122 (OrigSendingTime):
//   - 52  (SendingTime)      — live wall-clock, non-deterministic across runs
//   - 122 (OrigSendingTime)  — original wall-clock timestamp in replayed frames
//   - 10  (CheckSum)         — recomputed from frame body
// All structural tags compared VERBATIM: 34 (MsgSeqNum), 43 (PossDupFlag),
// 35 (MsgType), 371 (RefTagID), 373 (SessionRejectReason) — mutation of any
// of these must FAIL the golden diff (gate-biting property).
// Anchored to: specs/021-inbound-possdup-origsendingtime/quickstart.md §2.
// Usage: diff_transcripts(expected, actual, poss_dup_profile_excluded_tags())
inline const std::set<int>& poss_dup_profile_excluded_tags()
{
    // Compose from the admin profile ({52,10}) + OrigSendingTime(122) rather than
    // re-declaring a parallel literal — keeps the two in sync if the admin set
    // ever gains another time-variant tag (avoids silent golden-diff drift).
    static const std::set<int> tags = [] {
        std::set<int> s = admin_profile_excluded_tags();
        s.insert(122);
        return s;
    }();
    return tags;
}

// 061-typed-app-messages (061-slim): write shape-oracle normalization profile.
//
// Excludes ONLY the framing tags {8,9,10,34,52} — which a body-only builder
// output never contains anyway — so EVERY seeded business field is matched
// VERBATIM, including TransactTime(60). This is deliberately NOT
// default_normalization_tags() ({9,10,34,52,60,112,122}), which drops business
// tag 60 and is calibrated for live-interop transcript diffing — the wrong
// calibration for a byte-exact write shape-oracle where the builder output is
// the thing under test. Decimal comparison stays by-value (diff_transcripts
// normalizes trailing zeros); the canonical decimal *format* is pinned
// separately by each exemplar's byte-exact decimal assertion (FR-004).
// Anchored to: specs/061-typed-app-messages/data-model.md §4; FR-006/contracts C5.
// Usage: diff_transcripts(golden, {GoldenFrame{'>', body}}, shape_oracle_profile())
inline const std::set<int>& shape_oracle_profile()
{
    static const std::set<int> tags{8, 9, 10, 34, 52};
    return tags;
}

std::vector<GoldenFrame> parse_golden(std::string_view text);

DiffResult diff_transcripts(std::span<const GoldenFrame> expected,
                            std::span<const GoldenFrame> actual,
                            const std::set<int>& excluded_tags = default_normalization_tags());

}  // namespace fixpp::interop
