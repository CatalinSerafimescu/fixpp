// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/scan_frame_header_overflow_test.cpp
//
// 040-inbound-tag-overflow-hardening US1 Phase 3 — T005/T006/T007.
//
// Witnesses that scan_frame_header rejects forged wrap-and-continue tag tokens
// (fixed by T006) and non-digit tokens (T007). The defective >429496729U guard
// admitted wrap-and-continue tokens; the helper-based fix rejects them in-loop.
//
// scan_frame_header and FrameHeader are included directly from the internal
// src/session/scan_frame_header.hpp header (moved from session.cpp anonymous
// namespace to enable this unit test — 040 US1 Phase 3 T005). The test target
// adds ${CMAKE_SOURCE_DIR}/src to its include path (see CMakeLists.txt).
//
// Cells:
//   T005-W1: token 429496729649 (wraps to 49 without fix) → sender_comp_id NOT "FORGE49"
//   T005-W2: token 429496729652 (wraps to 52 without fix) → sending_time   NOT "FORGETIME"
//             (SC-002: closes the 038 SendingTime-guard regression vector)
//   T005-W3: token 429496729634 (wraps to 34 without fix) → msg_seq_num    NOT "42"
//   T005-W4: token 4294967330   (rejects in pre-fix code too) → sender_comp_id still empty
//   T005-W5: conforming frame   → all fields populated correctly (no regression)
//   T007-W6: non-digit token "4@=" → target_comp_id NOT "NODIGIT" (FR-007a)
//
// Anchors: spec.md FR-003/FR-004/FR-007/FR-007a; research.md D-3; tasks.md T005/T006/T007;
//          spec.md SC-002 (038 regression vector).
//
// [[feedback_witness_asserts_named_postcondition_not_proxy]]:
//   Each forged-field cell asserts the EXACT forged value is absent AND pairs it with a
//   conforming cell proving the SAME field IS populated on a legit frame — discriminating.
// [[feedback_simplify_pass_catches_9th_burn]]:
//   T007 guards against a future fold-into-helper that would accept "4@" as tag 56.

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

// Internal header — available via ${CMAKE_SOURCE_DIR}/src on the include path
// (added to this test target in tests/session/CMakeLists.txt).
#include "session/scan_frame_header.hpp"

namespace fixpp::session::test {

namespace {

// Build a minimal raw FIX frame. scan_frame_header does NOT require tag 8 or
// length — it simply scans fields. Each field: <tag>=<value>SOH.
std::vector<std::byte> make_frame(const std::string& raw) {
    std::vector<std::byte> frame;
    frame.reserve(raw.size());
    for (char c : raw) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

constexpr char SOH = '\x01';

}  // namespace

using fixpp::session::detail::scan_frame_header;

// ── T005-W1: 429496729649 must NOT alias SenderCompID(49) ────────────────────
// Pre-fix: defective >429496729U guard admits 429496729649 → wrapped to 49 → FORGE49.
// Post-fix: accumulate_tag_digit rejects before wrap; sender_comp_id stays empty.
// Conforming pair: 49=REAL → sender_comp_id == "REAL" (proves field IS populated on legit).
TEST(ScanFrameHeaderOverflow, ForgedTag49_NotSurfacedAsSenderCompId) {
    std::string forged;
    forged += "429496729649=FORGE49";
    forged += SOH;
    forged += "34=1";
    forged += SOH;
    auto f = make_frame(forged);
    auto result = scan_frame_header(std::span<const std::byte>(f));
    EXPECT_NE(result.sender_comp_id, "FORGE49")
        << "Forged tag 429496729649 must NOT alias SenderCompID(49) (FR-003/SC-001)";

    // Conforming pair: real 49= must populate sender_comp_id.
    std::string conforming;
    conforming += "49=REAL";
    conforming += SOH;
    auto c = make_frame(conforming);
    auto cr = scan_frame_header(std::span<const std::byte>(c));
    EXPECT_EQ(cr.sender_comp_id, "REAL")
        << "Conforming tag 49=REAL must be surfaced as sender_comp_id";
}

// ── T005-W2: 429496729652 must NOT alias SendingTime(52) — SC-002 ─────────────
// SC-002: the 52-aliasing closes the 038 acceptor SendingTime-guard regression vector.
// A forged 52 could bypass the MaxLatency check with an attacker-controlled timestamp.
TEST(ScanFrameHeaderOverflow, ForgedTag52_NotSurfacedAsSendingTime) {
    std::string forged;
    forged += "429496729652=FORGETIME";
    forged += SOH;
    forged += "34=1";
    forged += SOH;
    auto f = make_frame(forged);
    auto result = scan_frame_header(std::span<const std::byte>(f));
    EXPECT_NE(result.sending_time, "FORGETIME")
        << "Forged tag 429496729652 must NOT alias SendingTime(52) (SC-002/FR-003)";

    // Conforming pair: real 52= must populate sending_time.
    std::string conforming;
    conforming += "52=20240101-00:00:00.000";
    conforming += SOH;
    auto c = make_frame(conforming);
    auto cr = scan_frame_header(std::span<const std::byte>(c));
    EXPECT_EQ(cr.sending_time, "20240101-00:00:00.000")
        << "Conforming tag 52=<time> must be surfaced as sending_time";
}

// ── T005-W3: 429496729634 must NOT alias MsgSeqNum(34) ────────────────────────
TEST(ScanFrameHeaderOverflow, ForgedTag34_NotSurfacedAsMsgSeqNum) {
    std::string forged;
    forged += "429496729634=42";
    forged += SOH;
    auto f = make_frame(forged);
    auto result = scan_frame_header(std::span<const std::byte>(f));
    EXPECT_NE(result.msg_seq_num, "42")
        << "Forged tag 429496729634 must NOT alias MsgSeqNum(34) (FR-004/SC-001)";

    // Conforming pair: real 34=42 must populate msg_seq_num.
    std::string conforming;
    conforming += "34=42";
    conforming += SOH;
    auto c = make_frame(conforming);
    auto cr = scan_frame_header(std::span<const std::byte>(c));
    EXPECT_EQ(cr.msg_seq_num, "42")
        << "Conforming tag 34=42 must be surfaced as msg_seq_num";
}

// ── T005-W4: 4294967330 already rejected by pre-fix code (regression guard) ───
// 4294967330: the pre-existing >429496729U guard catches this token because
// the accumulated value reaches 429496733 before multiplying — 429496733 > 429496729.
// This cell must be GREEN both before and after T006 (regression guard).
TEST(ScanFrameHeaderOverflow, Token4294967330_StillRejected) {
    std::string forged;
    forged += "4294967330=SENTINEL30";
    forged += SOH;
    auto f = make_frame(forged);
    auto result = scan_frame_header(std::span<const std::byte>(f));
    // The forged field must not appear in any scanned field.
    EXPECT_NE(result.sender_comp_id, "SENTINEL30")
        << "4294967330 must be rejected (regression guard)";
    EXPECT_NE(result.sending_time, "SENTINEL30");
    EXPECT_NE(result.msg_seq_num, "SENTINEL30");
    EXPECT_NE(result.target_comp_id, "SENTINEL30");
}

// ── T005-W5: conforming frame with ordinary tags parses correctly ──────────────
// SC-003: no regression on conforming-path input.
TEST(ScanFrameHeaderOverflow, ConformingFrame_AllFieldsCorrect) {
    std::string frame;
    frame += "8=FIX.4.4";
    frame += SOH;
    frame += "34=7";
    frame += SOH;
    frame += "49=SENDER";
    frame += SOH;
    frame += "52=20240101-12:00:00.000";
    frame += SOH;
    frame += "56=TARGET";
    frame += SOH;
    auto f = make_frame(frame);
    auto result = scan_frame_header(std::span<const std::byte>(f));
    EXPECT_EQ(result.begin_string, "FIX.4.4") << "tag 8 must be surfaced";
    EXPECT_EQ(result.msg_seq_num, "7") << "tag 34 must be surfaced";
    EXPECT_EQ(result.sender_comp_id, "SENDER") << "tag 49 must be surfaced";
    EXPECT_EQ(result.sending_time, "20240101-12:00:00.000") << "tag 52 must be surfaced";
    EXPECT_EQ(result.target_comp_id, "TARGET") << "tag 56 must be surfaced";
}

// ── T007-W6: non-digit token must be rejected (FR-007a) ──────────────────────
// Guards against a future "simplification" that folds the explicit digit-class
// check into the helper (removing the `if (c<'0'||c>'9') tag_ok=false` line
// and calling accumulate_tag_digit unconditionally).
//
// Discriminating construction (per [[feedback_witness_asserts_named_postcondition_not_proxy]]):
// Token "4@" — '@' = 0x40 = 64, so c-'0' = 64-48 = 16.
//   - Correct code: '@' < '0' || '@' > '9' → tag_ok=false → field skipped.
//   - Fold regression (digit check removed): accumulate_tag_digit sees 4→40+16=56
//     (no overflow: 4 < (65535-16)/10=6551) → tag=56 → "NODIGIT" surfaces as target_comp_id.
// The assertion FAILS under the fold (target_comp_id == "NODIGIT"), PASSES with the fix.
// Mutation-verified: temporarily removing the digit-check line makes this cell RED.
TEST(ScanFrameHeaderOverflow, NonDigitToken_Rejected_NotDispatched) {
    // Token "4@" + value "NODIGIT": '@' (non-digit) makes c-'0'=16, so without
    // the digit-class guard, accumulate_tag_digit would accumulate 4→56 and the
    // value would surface as target_comp_id. The guard must fire and reject the field.
    std::string forged;
    forged += "4@=NODIGIT";  // '@' (0x40) is a non-digit; c-'0' = 16 → 4*10+16 = 56 if accepted
    forged += SOH;
    auto f = make_frame(forged);
    auto result = scan_frame_header(std::span<const std::byte>(f));
    EXPECT_NE(result.target_comp_id, "NODIGIT")
        << "Non-digit token '4@=' must be rejected (tag_ok=false on '@'); NOT dispatched as "
           "tag 56 (FR-007a). Without the explicit digit check, '@' would accumulate 4→56.";

    // Conforming pair: "56=REALVAL" must still populate target_comp_id.
    std::string conforming;
    conforming += "56=REALVAL";
    conforming += SOH;
    auto c = make_frame(conforming);
    auto cr = scan_frame_header(std::span<const std::byte>(c));
    EXPECT_EQ(cr.target_comp_id, "REALVAL")
        << "Conforming tag 56=REALVAL must still be surfaced as target_comp_id";
}

}  // namespace fixpp::session::test
