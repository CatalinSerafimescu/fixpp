// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/scan_first_frame_ids_overflow_test.cpp
//
// 040-inbound-tag-overflow-hardening US2 Phase 4 — T011/T012 witness portion.
//
// Witnesses that scan_first_frame_ids rejects forged wrap-and-continue tag
// tokens (the defective unguarded loop admitted them) and non-digit tokens
// (FR-007a). The unguarded loop (engine.cpp pre-fix) had no overflow bound —
// a forged token like "429496729649" wraps uint32 and aliases tag 49
// (SenderCompID), allowing a forged CompID to bypass acceptor registry
// resolution.
//
// scan_first_frame_ids and FirstFrameIds are included directly from the
// internal src/session/scan_first_frame_ids.hpp header (moved from engine.cpp
// anonymous namespace to enable this unit test — 040 US2 Phase 4 T011). The
// test target adds ${CMAKE_SOURCE_DIR}/src to its include path.
//
// Cells:
//   W1: token 429496729649 (wraps to 49 without fix) → sender_comp_id NOT "FORGE49"
//   W2: token 429496729656 (wraps to 56 without fix) → target_comp_id NOT "FORGE56"
//   W3: non-digit token "4@=" → neither CompID surfaced (FR-007a)
//   W4: conforming frame (valid 8/49/56) → all fields correctly surfaced (SC-003)
//
// Anchors: spec.md FR-003/FR-007/FR-007a; research.md D-3; tasks.md T011/T012;
//          spec.md SC-003 (no regression on conforming input).
//
// [[feedback_witness_asserts_named_postcondition_not_proxy]]:
//   Each forged-field cell asserts the EXACT forged value is absent AND pairs
//   it with a conforming cell proving the SAME field IS populated on a legit
//   frame — discriminating.

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

// Internal header — available via ${CMAKE_SOURCE_DIR}/src on the include path
// (added to this test target in tests/session/CMakeLists.txt).
#include "session/scan_first_frame_ids.hpp"

namespace fixpp::session::test {

namespace {

// Build a raw byte frame from a plain string (SOH must be embedded explicitly).
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

using fixpp::session::detail::scan_first_frame_ids;

// ── W1: 429496729649 must NOT alias SenderCompID(49) ─────────────────────────
// Pre-fix: unguarded loop allows 429496729649 to wrap uint32 → 49 → "FORGE49"
// surfaces as sender_comp_id, enabling a forged CompID in acceptor resolution.
// Post-fix: accumulate_tag_digit rejects before wrap; sender_comp_id stays empty.
// Conforming pair: 49=REALSENDER → sender_comp_id == "REALSENDER".
TEST(ScanFirstFrameIdsOverflow, ForgedTag49_NotSurfacedAsSenderCompId) {
    std::string forged;
    forged += "429496729649=FORGE49";
    forged += SOH;
    auto f = make_frame(forged);
    auto result = scan_first_frame_ids(std::span<const std::byte>(f));
    EXPECT_NE(result.sender_comp_id, "FORGE49")
        << "Forged tag 429496729649 must NOT alias SenderCompID(49) (FR-003/FR-007)";

    // Conforming pair: real 49= must populate sender_comp_id.
    std::string conforming;
    conforming += "49=REALSENDER";
    conforming += SOH;
    auto c = make_frame(conforming);
    auto cr = scan_first_frame_ids(std::span<const std::byte>(c));
    EXPECT_EQ(cr.sender_comp_id, "REALSENDER")
        << "Conforming tag 49=REALSENDER must be surfaced as sender_comp_id";
}

// ── W2: 429496729656 must NOT alias TargetCompID(56) ─────────────────────────
// Pre-fix: unguarded loop allows 429496729656 to wrap uint32 → 56 → "FORGE56"
// surfaces as target_comp_id, enabling a forged CompID in acceptor resolution.
// Post-fix: accumulate_tag_digit rejects before wrap; target_comp_id stays empty.
// Conforming pair: 56=REALTARGET → target_comp_id == "REALTARGET".
TEST(ScanFirstFrameIdsOverflow, ForgedTag56_NotSurfacedAsTargetCompId) {
    std::string forged;
    forged += "429496729656=FORGE56";
    forged += SOH;
    auto f = make_frame(forged);
    auto result = scan_first_frame_ids(std::span<const std::byte>(f));
    EXPECT_NE(result.target_comp_id, "FORGE56")
        << "Forged tag 429496729656 must NOT alias TargetCompID(56) (FR-003/FR-007)";

    // Conforming pair: real 56= must populate target_comp_id.
    std::string conforming;
    conforming += "56=REALTARGET";
    conforming += SOH;
    auto c = make_frame(conforming);
    auto cr = scan_first_frame_ids(std::span<const std::byte>(c));
    EXPECT_EQ(cr.target_comp_id, "REALTARGET")
        << "Conforming tag 56=REALTARGET must be surfaced as target_comp_id";
}

// ── W3: non-digit token must be rejected (FR-007a) ───────────────────────────
// Token "4@" — '@' = 0x40 = 64, c-'0' = 16, so WITHOUT the digit-class guard:
//   accumulate_tag_digit would accept (4 < (65535-16)/10=6551) → tag=4*10+16=56
//   → "NODIGIT" would surface as target_comp_id.
// The explicit `if (c<'0'||c>'9') tag_ok=false` guard fires first and skips the
// field — "NODIGIT" must NOT appear in any output field.
// Conforming pair: "56=REALVAL" must still surface after a preceding bad token.
TEST(ScanFirstFrameIdsOverflow, NonDigitToken_Rejected_NotDispatched) {
    std::string forged;
    forged += "4@=NODIGIT";  // '@' (0x40) non-digit; c-'0'=16 → 4*10+16=56 if unchecked
    forged += SOH;
    auto f = make_frame(forged);
    auto result = scan_first_frame_ids(std::span<const std::byte>(f));
    EXPECT_NE(result.sender_comp_id, "NODIGIT")
        << "Non-digit '4@=' must be rejected; must NOT surface as sender_comp_id";
    EXPECT_NE(result.target_comp_id, "NODIGIT")
        << "Non-digit '4@=' must be rejected; must NOT surface as target_comp_id (FR-007a)";

    // Conforming pair: a well-formed 56= following a bad token must still work.
    std::string conforming;
    conforming += "4@=NODIGIT";
    conforming += SOH;
    conforming += "56=REALVAL";
    conforming += SOH;
    auto c = make_frame(conforming);
    auto cr = scan_first_frame_ids(std::span<const std::byte>(c));
    EXPECT_EQ(cr.target_comp_id, "REALVAL")
        << "Conforming tag 56=REALVAL after a bad token must still be surfaced";
}

// ── W4: conforming frame with ordinary tags parses correctly (SC-003) ─────────
// No regression: a well-formed first frame with tags 8/49/56 must parse all
// three fields correctly.
TEST(ScanFirstFrameIdsOverflow, ConformingFrame_AllFieldsCorrect) {
    std::string frame;
    frame += "8=FIX.4.4";
    frame += SOH;
    frame += "49=BROKER";
    frame += SOH;
    frame += "56=CLIENT";
    frame += SOH;
    auto f = make_frame(frame);
    auto result = scan_first_frame_ids(std::span<const std::byte>(f));
    EXPECT_EQ(result.begin_string, "FIX.4.4")
        << "tag 8 (BeginString) must be surfaced";
    EXPECT_EQ(result.sender_comp_id, "BROKER")
        << "tag 49 (SenderCompID) must be surfaced";
    EXPECT_EQ(result.target_comp_id, "CLIENT")
        << "tag 56 (TargetCompID) must be surfaced";
}

}  // namespace fixpp::session::test
