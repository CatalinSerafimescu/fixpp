// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_537_second_too_high.cpp
//
// fixpp#537, D-537-1 and D-537-2 (.specify/536-537-538-recovery-liveness.md §4.3 and the
// #537 table of §5): a frame above NextNumIn that arrives while a resend is outstanding
// is ignored. The session stays in its state, sends nothing and does not advance; the
// outstanding ResendRequest runs to 0, so the peer's replay covers the frame.
//
// FIX Session Layer (Nov 2020 errata), §4.8.2: "the application could ignore messages 3
// through 5 and generate a resend request for messages 2 through 5, or, preferably 2
// through 0"; §4.10, precedence 12, has the other approach the text allows ("Queue
// incoming messages with MsgSeqNum(34) too high"). No document ends the session.
//
// Shape of every cell, on both roles:
//   1. Session Active at NextNumIn = N.
//   2. The peer sends 34 = N+5. A ResendRequest with BeginSeqNo(7) = N goes out.
//   3. Before any replay, the peer sends a second frame above NextNumIn.
//   4. The session is still Active, wrote nothing for that frame and did not advance, and
//      it is back in step once the peer's replay covers N to the highest number sent.
//
// The discriminating assertion is the state after step 3. The path this issue removes
// returned success and wrote no Logout, so neither the returned value nor a Logout count
// tells the two outcomes apart.
//
// No cell here moves the clock: HeartBtInt is the Scene's, and the liveness loop stays
// parked in its first sleep for the whole cell.

#include <gtest/gtest.h>

#include <cstdint>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <functional>
#include <string>
#include <vector>

#include "session/resend_gap_scene.hpp"

namespace fixpp::session::test {

namespace {

using fixpp::test_support::resend_gap::field;
using fixpp::test_support::resend_gap::Options;
using fixpp::test_support::resend_gap::role_name;
using fixpp::test_support::resend_gap::Scene;

// Builds the second too-high frame of a cell from its Scene and its MsgSeqNum(34).
using SecondFrame = std::function<std::vector<std::byte>(Scene const&, std::uint32_t)>;

// Steps 1 and 2: Active at N, then a too-high TestRequest at N + 5 and its ResendRequest.
// Returns N, or 0 after a failed gate.
seqnum_t open_gap_with_a_test_request(Scene& s) {
    if (!s.open_to_active()) {
        ADD_FAILURE() << "setup: the session did not reach Active";
        return 0;
    }
    const seqnum_t n = s.next_in();
    const auto mark = s.sent.size();
    (void)s.feed(s.test_request(n + 5));
    const auto rr = s.of_type("2", mark);
    if (rr.size() != 1U || field(rr.front(), 7) != std::to_string(n) || !s.resend_outstanding() ||
        s.state() != fsm_state::Active) {
        ADD_FAILURE() << "setup: the first too-high frame must draw one ResendRequest with "
                         "BeginSeqNo(7) = NextNumIn and leave a resend outstanding; the session "
                         "wrote "
                      << s.written(mark);
        return 0;
    }
    return n;
}

// Step 4: the peer's replay covers N to `last`, and an in-sequence frame follows.
void expect_recovery(Scene& s, seqnum_t n, std::uint32_t last) {
    (void)s.feed(s.gap_fill(n, last + 1));
    EXPECT_EQ(s.state(), fsm_state::Active) << "after the replay through 34=" << last;
    EXPECT_EQ(s.next_in(), last + 1) << "NextNumIn after the replay through 34=" << last;
    EXPECT_FALSE(s.resend_outstanding()) << "a resend is still outstanding after the replay";
    (void)s.feed(s.heartbeat(last + 1));
    EXPECT_EQ(s.state(), fsm_state::Active) << "an in-sequence frame after the recovery";
    EXPECT_EQ(s.next_in(), last + 2) << "NextNumIn after the in-sequence frame";
}

// Steps 1 to 4 with `second` as step 3, or without step 3 when it is empty.
void run_cell(session_role role, const char* what, SecondFrame const& second) {
    Scene s{Options{.role = role}};
    const seqnum_t n = open_gap_with_a_test_request(s);
    ASSERT_NE(n, 0U);

    std::uint32_t last = n + 5;
    if (second) {
        last = n + 6;
        const auto mark = s.sent.size();
        const auto r = s.feed(second(s, last));
        EXPECT_TRUE(r.has_value())
            << "on_inbound_frame returned error " << static_cast<int>(r.error());
        ASSERT_EQ(s.state(), fsm_state::Active)
            << "a second too-high frame (" << what << ", 34=" << last
            << ") while a resend from 34=" << n << " is outstanding ended the session";
        EXPECT_EQ(s.sent.size(), mark)
            << "the session wrote for the ignored frame: " << s.written(mark);
        EXPECT_EQ(s.next_in(), n) << "NextNumIn moved at the ignored frame";
        EXPECT_TRUE(s.resend_outstanding()) << "the ignored frame ended the outstanding resend";
    }
    expect_recovery(s, n, last);
}

class SecondTooHigh : public ::testing::TestWithParam<session_role> {};
class SecondTooHighPerType : public ::testing::TestWithParam<session_role> {};
class SecondTooHighResendRequestAndLogout : public ::testing::TestWithParam<session_role> {};
class FrameSentDuringTheReplay : public ::testing::TestWithParam<session_role> {};

}  // namespace

// ── The lifted cells ────────────────────────────────────────────────────────────────

// Control: step 3 omitted. The too-high frame reaches the sequence gate (the
// ResendRequest is its witness) and the replay recovers the session.
TEST_P(SecondTooHigh, OneTooHigh) { run_cell(GetParam(), "none", {}); }

// The claim: step 3 is a TestRequest.
TEST_P(SecondTooHigh, SecondTooHigh) {
    run_cell(GetParam(), "a TestRequest",
             [](Scene const& s, std::uint32_t seq) { return s.test_request(seq); });
}

// Step 3 is a Heartbeat, which leaves the out-of-sequence arm before the path this
// issue removes.
TEST_P(SecondTooHigh, SecondTooHighHb) {
    run_cell(GetParam(), "a Heartbeat",
             [](Scene const& s, std::uint32_t seq) { return s.heartbeat(seq); });
}

// ── Per message type ────────────────────────────────────────────────────────────────

TEST_P(SecondTooHighPerType, TestRequest) {
    run_cell(GetParam(), "a TestRequest",
             [](Scene const& s, std::uint32_t seq) { return s.test_request(seq); });
}

TEST_P(SecondTooHighPerType, ApplicationMessage) {
    run_cell(GetParam(), "an application message",
             [](Scene const& s, std::uint32_t seq) { return s.order(seq); });
}

TEST_P(SecondTooHighPerType, Reject) {
    run_cell(GetParam(), "a Reject",
             [](Scene const& s, std::uint32_t seq) { return s.reject(seq); });
}

// A GapFill that is not a resent frame: it is above NextNumIn, so it is not applied.
TEST_P(SecondTooHighPerType, GapFillWithoutPossDup) {
    run_cell(GetParam(), "a GapFill without PossDupFlag", [](Scene const& s, std::uint32_t seq) {
        return s.gap_fill(seq, seq + 3, /*poss_dup=*/false);
    });
}

TEST_P(SecondTooHighPerType, Logon) {
    run_cell(GetParam(), "a Logon", [](Scene const& s, std::uint32_t seq) { return s.logon(seq); });
}

// ── A second too-high ResendRequest, and a second too-high Logout ───────────────────
//
// What fixpp should send for either is fixpp#577's question, which is not decided here.
// The two assertions below hold under either answer to it: the frame does not advance
// NextNumIn, and the session does not end without a Logout of its own.

namespace {

void expect_no_advance_and_no_silent_end(session_role role, const char* what,
                                         SecondFrame const& second) {
    Scene s{Options{.role = role}};
    const seqnum_t n = open_gap_with_a_test_request(s);
    ASSERT_NE(n, 0U);

    const auto mark = s.sent.size();
    (void)s.feed(second(s, n + 6));
    EXPECT_EQ(s.next_in(), n) << "NextNumIn moved at a too-high " << what;
    EXPECT_FALSE(s.state() == fsm_state::Disconnected && s.count("5", mark) == 0U)
        << "a too-high " << what << " while a resend is outstanding ended the session and no "
        << "Logout was written; the session wrote " << s.written(mark);
}

}  // namespace

TEST_P(SecondTooHighResendRequestAndLogout, ResendRequest) {
    expect_no_advance_and_no_silent_end(
        GetParam(), "ResendRequest",
        [](Scene const& s, std::uint32_t seq) { return s.resend_request(seq); });
}

TEST_P(SecondTooHighResendRequestAndLogout, Logout) {
    expect_no_advance_and_no_silent_end(
        GetParam(), "Logout", [](Scene const& s, std::uint32_t seq) { return s.logout(seq); });
}

// ── A frame the peer sends during its replay (D-537-1) ──────────────────────────────
//
// The peer's messages 2 and 3 were lost. Its message 4 opens the gap. While it replays 2
// to 4 it sends a new message, 5, which the session ignores: the request it answers was
// made before 5 existed. So 5 is a new gap, which the next message, 6, opens, and a second
// ResendRequest fetches it. redeliver_poss_dup is off, so each message reaches the
// application once.
TEST_P(FrameSentDuringTheReplay, IsIgnoredAndASecondResendRequestFetchesIt) {
    using orig = Scene::orig_time;
    Scene s{Options{.role = GetParam()}};
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    ASSERT_EQ(s.next_in(), 2U);

    (void)s.feed(s.order(4));
    ASSERT_EQ(s.count("2"), 1U) << "setup: the gap drew no ResendRequest";
    ASSERT_EQ(field(s.of_type("2").front(), 7), "2");

    (void)s.feed(s.poss_dup_order(2, orig::valid));
    ASSERT_EQ(s.next_in(), 3U) << "setup: the first replayed message was not consumed";
    ASSERT_TRUE(s.resend_outstanding()) << "setup: the replay is not under way";

    const auto mark = s.sent.size();
    (void)s.feed(s.order(5));
    ASSERT_EQ(s.state(), fsm_state::Active)
        << "a new message (34=5) sent during the replay ended the session";
    EXPECT_EQ(s.sent.size(), mark) << "the session wrote for it: " << s.written(mark);
    EXPECT_EQ(s.next_in(), 3U) << "NextNumIn moved at the ignored message";

    (void)s.feed(s.poss_dup_order(3, orig::valid));
    (void)s.feed(s.poss_dup_order(4, orig::valid));
    EXPECT_EQ(s.next_in(), 5U) << "NextNumIn after the replay of 2 to 4";
    EXPECT_FALSE(s.resend_outstanding()) << "the first resend is still outstanding";
    EXPECT_EQ(s.count("2"), 1U) << "a ResendRequest was written during the replay";

    (void)s.feed(s.order(6));
    const auto rr = s.of_type("2");
    ASSERT_EQ(rr.size(), 2U) << "the message after the ignored one drew no second ResendRequest";
    EXPECT_EQ(field(rr.back(), 7), "5") << "BeginSeqNo(7) of the second ResendRequest";

    (void)s.feed(s.poss_dup_order(5, orig::valid));
    (void)s.feed(s.poss_dup_order(6, orig::valid));
    EXPECT_EQ(s.state(), fsm_state::Active);
    EXPECT_EQ(s.next_in(), 7U) << "NextNumIn after the second replay";
    EXPECT_FALSE(s.resend_outstanding());
    const std::vector<std::string> want{"ORD2", "ORD3", "ORD4", "ORD5", "ORD6"};
    EXPECT_EQ(s.app->delivered, want) << "each message once, in order";
}

namespace {
std::string role_param_name(const ::testing::TestParamInfo<session_role>& i) {
    return role_name(i.param);
}
}  // namespace

INSTANTIATE_TEST_SUITE_P(BothRoles, SecondTooHigh,
                         ::testing::Values(session_role::initiator, session_role::acceptor),
                         role_param_name);
INSTANTIATE_TEST_SUITE_P(BothRoles, SecondTooHighPerType,
                         ::testing::Values(session_role::initiator, session_role::acceptor),
                         role_param_name);
INSTANTIATE_TEST_SUITE_P(BothRoles, SecondTooHighResendRequestAndLogout,
                         ::testing::Values(session_role::initiator, session_role::acceptor),
                         role_param_name);
INSTANTIATE_TEST_SUITE_P(BothRoles, FrameSentDuringTheReplay,
                         ::testing::Values(session_role::initiator, session_role::acceptor),
                         role_param_name);

}  // namespace fixpp::session::test
