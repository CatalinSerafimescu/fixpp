// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support_smoke_test.cpp — 016 T002 Phase-1 checkpoint.
//
// Proves the interop support layer configures, builds, links, and runs:
//   - golden_diff   (T005, Codex-authored): parse + normalize + diff.
//   - descriptors   (T006, Codex-authored): value types + has_spec_ref.
//   - probe         (T003): env-token mapping + unavailable-skip-reason.
//   - fixture       (T004): Engine lifecycle + clean stop_within() on an idle engine.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>

#include "support/counterparty_probe.hpp"
#include "support/golden_diff.hpp"
#include "support/interop_fixture.hpp"
#include "support/scenario_descriptor.hpp"

using namespace fixpp::interop;

namespace {

// One '>' frame + one '<' frame. SOH rendered as the checked-in \x01 escape; tag 10
// (CheckSum) present so normalization is exercised. No 9/checksum validity needed —
// the comparator only splits + compares tag=value.
const char* kGolden =
    "> 8=FIX.4.4\\x0135=A\\x0149=A\\x0156=B\\x0134=1\\x0110=000\\x01\n"
    "< 8=FIX.4.4\\x0135=A\\x0149=B\\x0156=A\\x0134=1\\x0110=111\\x01\n";

}  // namespace

TEST(InteropSupportSmoke, GoldenParseRoundtrip) {
    auto frames = parse_golden(kGolden);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[0].dir, '>');
    EXPECT_EQ(frames[1].dir, '<');
    // The \x01 escape decoded to a real SOH byte (0x01).
    EXPECT_NE(std::find(frames[0].bytes.begin(), frames[0].bytes.end(), std::byte{0x01}),
              frames[0].bytes.end());
}

TEST(InteropSupportSmoke, IdenticalTranscriptsMatch) {
    auto a = parse_golden(kGolden);
    auto b = parse_golden(kGolden);
    auto r = diff_transcripts(a, b);
    EXPECT_TRUE(static_cast<bool>(r)) << r.detail;
}

TEST(InteropSupportSmoke, SeqnumDifferenceNormalizedAway) {
    // tag 34 (MsgSeqNum) differs — must be normalized out ⇒ still match.
    auto expected = parse_golden(kGolden);
    const char* renumbered =
        "> 8=FIX.4.4\\x0135=A\\x0149=A\\x0156=B\\x0134=99\\x0110=000\\x01\n"
        "< 8=FIX.4.4\\x0135=A\\x0149=B\\x0156=A\\x0134=7\\x0110=111\\x01\n";
    auto actual = parse_golden(renumbered);
    auto r = diff_transcripts(expected, actual);
    EXPECT_TRUE(static_cast<bool>(r)) << r.detail;
}

TEST(InteropSupportSmoke, MsgTypeDifferenceIsMismatch) {
    // tag 35 (MsgType) differs on the first frame ⇒ mismatch reported at tag 35.
    auto expected = parse_golden(kGolden);
    const char* wrong_type =
        "> 8=FIX.4.4\\x0135=0\\x0149=A\\x0156=B\\x0134=1\\x0110=000\\x01\n"
        "< 8=FIX.4.4\\x0135=A\\x0149=B\\x0156=A\\x0134=1\\x0110=111\\x01\n";
    auto actual = parse_golden(wrong_type);
    auto r = diff_transcripts(expected, actual);
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.detail, "mismatch:>:0:35");
}

TEST(InteropSupportSmoke, FrameCountMismatchReported) {
    auto expected = parse_golden(kGolden);
    const char* one_frame = "> 8=FIX.4.4\\x0135=A\\x0134=1\\x0110=000\\x01\n";
    auto actual = parse_golden(one_frame);
    auto r = diff_transcripts(expected, actual);
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_NE(r.detail.find(":count"), std::string::npos);
}

TEST(InteropSupportSmoke, DescriptorSpecRefInvariant) {
    MatrixCell cell;
    cell.id = "HP-QFcpp-init-fix44-logon-hb-logout";
    cell.counterparty = Counterparty::quickfix_cpp;
    cell.role = Role::fixpp_initiator;
    cell.event_chain = "logon-hb-logout";
    EXPECT_FALSE(has_spec_ref(cell));  // FR-018: a cell with no spec_ref is invalid
    cell.spec_ref = "[FIX-SL §4.3]";
    EXPECT_TRUE(has_spec_ref(cell));
}

TEST(InteropSupportSmoke, ProbeEnvTokenMapping) {
    EXPECT_EQ(env_token("quickfix-cpp"), "QUICKFIX_CPP");
    EXPECT_EQ(env_token("quickfix-j"), "QUICKFIX_J");
}

TEST(InteropSupportSmoke, ProbeUnavailableCarriesReason) {
    // No INTEROP_*_PORT exported in the unit environment ⇒ unavailable + reason
    // (never silent-pass, FR-023). We assert the *contract*, not skip the test.
    auto r = probe_counterparty("quickfix-cpp-absent-xyz");
    EXPECT_FALSE(r.available);
    EXPECT_FALSE(r.reason.empty());
}

TEST(InteropSupportSmoke, IdleEngineStopsPromptly) {
    // Fixture brings up an Engine with no registered sessions; start()+stop_within()
    // must complete well within the bound (no work to tear down). Exercises T004.
    InteropEngineFixture fx;
    fx.start();
    auto took = fx.stop_within(std::chrono::seconds{2});
    EXPECT_TRUE(fx.stopped());
    EXPECT_LT(took, std::chrono::seconds{2});
}
