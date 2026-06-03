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

// ---------------------------------------------------------------------------
// RC#2 (Gate-B/r1) — validate_admin_descriptor negative tests (counterparty-free).
// ---------------------------------------------------------------------------
//
// These run without any live peer (no INTEROP_REQUIRE_COUNTERPARTY) and
// exercise the two new checks added to validate_admin_descriptor:
//   (a) T029 identity invariant: golden_ref == "happy/golden/" + cell_id + ".fix"
//   (b) FR-010 / rule 4: self_deadline_ms > 0
//
// [feedback_fail_placeholder_red_test]: real error-string assertions, no SUCCEED().

// Helper: build a minimal valid AdminScenarioDescriptor for testrequest_echo.
static AdminScenarioDescriptor make_valid_descriptor()
{
    AdminScenarioDescriptor d;
    d.cell_id        = "HP-QFj-init-fix44-testrequest-echo";
    d.scenario_group = AdminScenarioGroup::testrequest_echo;
    d.role           = Role::fixpp_initiator;
    d.counterparty   = Counterparty::quickfix_j;
    d.spec_ref       = "[FIX-SL §4.5.5]";
    d.golden_ref     = "happy/golden/" + d.cell_id + ".fix";
    d.induction      = AdminInduction::inbound_silence;
    d.self_deadline_ms = std::chrono::milliseconds{10000};
    d.round_trips    = {
        {"US1-1", "[FIX-SL §4.5.5]"},
        {"US1-2", "[FIX-SL §4.5.1]"},
        {"US1-3", "[FIX-SL §4.5.5]"},
    };
    d.acceptance_ids = {"US1-1", "US1-2", "US1-3"};
    return d;
}

TEST(AdminDescriptorValidation, ValidDescriptorPasses) {
    // Confirm the helper builds a descriptor that passes all checks.
    auto d = make_valid_descriptor();
    EXPECT_TRUE(validate_admin_descriptor(d).empty())
        << "expected valid descriptor to pass; error: " << validate_admin_descriptor(d);
}

TEST(AdminDescriptorValidation, WrongGoldenRefFailsValidation) {
    // T029 identity invariant: golden_ref must equal "happy/golden/" + cell_id + ".fix".
    // A copy/paste drift (wrong suffix) must produce a non-empty error.
    auto d = make_valid_descriptor();
    d.golden_ref = "happy/golden/HP-QFj-init-fix44-WRONG.fix";  // mutated cell_id portion
    const std::string err = validate_admin_descriptor(d);
    EXPECT_FALSE(err.empty())
        << "validate_admin_descriptor should reject a golden_ref that does not match cell_id";
}

TEST(AdminDescriptorValidation, ZeroSelfDeadlineFailsValidation) {
    // FR-010 / rule 4: self_deadline_ms must be > 0. A zero deadline must produce
    // a non-empty error.
    auto d = make_valid_descriptor();
    d.self_deadline_ms = std::chrono::milliseconds{0};
    const std::string err = validate_admin_descriptor(d);
    EXPECT_FALSE(err.empty())
        << "validate_admin_descriptor should reject self_deadline_ms == 0";
}
