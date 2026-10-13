// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_537_too_high_heartbeat.cpp
//
// fixpp#537, D-537-6 and D-537-8 (.specify/536-537-538-recovery-liveness.md §4.3, §4.6 and
// the #537 table of §5): a Heartbeat above NextNumIn answers a pending TestRequest, with
// the TestReqID(112) rule of an in-sequence Heartbeat, whether it arrives while a resend
// is outstanding (D-537-6) or is the frame that opens the gap (D-537-8).
//
// The cells are timelines of the liveness loop on a mock clock. HB is the Scene's
// HeartBtInt and E the instant a gap opened. The loop writes a TestRequest when no frame
// arrived for one HB, and ends the session, silently, when that TestRequest is still
// pending one HB after it was due. So "the session is Active just past that instant" is
// the reading that tells an answered TestRequest from one a dropped Heartbeat left
// pending, and each cell that makes it first reads that the TestRequest was written.

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <string>

#include "session/resend_gap_scene.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {

namespace {

using fixpp::test_support::resend_gap::deadline_logout_written;
using fixpp::test_support::resend_gap::field;
using fixpp::test_support::resend_gap::Gap;
using fixpp::test_support::resend_gap::kGapBound;
using fixpp::test_support::resend_gap::kGapWidth;
using fixpp::test_support::resend_gap::kHeartBtInt;
using fixpp::test_support::resend_gap::open_gap;
using fixpp::test_support::resend_gap::Options;
using fixpp::test_support::resend_gap::Scene;
using fixpp::test_support::resend_gap::state_name;
using orig = Scene::orig_time;

// Past a grace deadline that is one HB away, and short of the one after it.
constexpr auto kPastTheGrace = kHeartBtInt + 1s;
static_assert(kGapBound == 2 * kHeartBtInt);

// Moves the clock one HB with nothing delivered, and reads that the loop wrote its
// `nth` TestRequest. Returns its TestReqID(112), or "" after a failed gate.
[[nodiscard]] std::string test_request_after_one_quiet_interval(Scene& s, std::size_t nth = 1) {
    s.pass(kHeartBtInt);
    if (s.count("1") != nth || s.state() != fsm_state::Active || !s.test_request_pending()) {
        ADD_FAILURE() << "setup: after one HeartBtInt with no frame, the session is "
                      << state_name(s.state()) << " and wrote " << s.written();
        return {};
    }
    return s.last_test_req_id();
}

}  // namespace

// ── D-537-6: a Heartbeat above NextNumIn while a resend is outstanding ───────────────

// Q. The peer answers the TestRequest of E + HB with a Heartbeat above NextNumIn. Just
// past E + 2 HB the session is still Active: the answer cleared the TestRequest. At that
// wake the loop writes a second TestRequest, whose grace ends near E + 3 HB, so the cell
// stays short of it. A Heartbeat above NextNumIn then draws the deadline's Logout.
TEST(TooHighHeartbeatDuringTheGap, Q_AnswersTheTestRequestAndTheDeadlineEndsTheSession) {
    Scene s;
    const Gap g = open_gap(s);
    ASSERT_NE(g.n, 0U);
    const std::string id = test_request_after_one_quiet_interval(s);
    ASSERT_FALSE(id.empty());

    (void)s.feed(s.heartbeat(g.n + kGapWidth + 1, id));
    ASSERT_EQ(s.state(), fsm_state::Active) << "at the answering Heartbeat";

    s.pass(kPastTheGrace);
    ASSERT_EQ(s.state(), fsm_state::Active)
        << "just past E + 2 HB the session is " << state_name(s.state())
        << ": the Heartbeat above NextNumIn did not answer the TestRequest; since the gap opened "
        << "the session wrote " << s.written(g.mark);
    EXPECT_EQ(s.count("5", g.mark), 0U) << "a Logout was written with no frame delivered";
    EXPECT_EQ(s.count("1"), 2U) << "TestRequests written by the wake just past E + 2 HB";

    const auto mark = s.sent.size();
    const auto r = s.feed(s.heartbeat(g.n + kGapWidth + 2));
    EXPECT_TRUE(deadline_logout_written(s, mark, r));
}

// N, control for a peer that answers nothing: the same gap, nothing delivered, and the
// clock past the grace deadline. The ending is the dead-peer one, with or without a gap:
// Disconnected, and no Logout.
TEST(TooHighHeartbeatDuringTheGap, N_Control_NothingDelivered_EndsSilentlyAtTheGraceCheck) {
    Scene s;
    const Gap g = open_gap(s);
    ASSERT_NE(g.n, 0U);
    ASSERT_FALSE(test_request_after_one_quiet_interval(s).empty());

    s.pass(kPastTheGrace);
    EXPECT_EQ(s.state(), fsm_state::Disconnected) << "past the grace deadline";
    EXPECT_EQ(s.count("5", g.mark), 0U) << "the dead-peer ending wrote a Logout";
    EXPECT_EQ(s.count("1"), 1U);
}

// R. The peer replays slowly: one in-sequence PossDup message every 1.5 HB from E. In
// between, the loop writes a TestRequest, and the peer answers each with a Heartbeat
// above NextNumIn. The session is never Disconnected.
//
// Precondition: the gap is kGapWidth wide and the cell delivers far fewer in-sequence
// frames, so the resend stays outstanding for the whole cell and every answering
// Heartbeat takes the out-of-sequence arm. Were the resend to end part-way, the next
// answer would open a gap and the cell would measure D-537-8.
TEST(TooHighHeartbeatDuringTheGap, R_SlowReplayWithEveryTestRequestAnswered_StaysActive) {
    constexpr auto kTick = kHeartBtInt / 2;
    constexpr int kTicks = 14;
    constexpr int kTicksPerReplayedFrame = 3;
    Scene s;
    const Gap g = open_gap(s);
    ASSERT_NE(g.n, 0U);

    std::size_t answered = 0;
    seqnum_t replayed = g.n;
    std::uint32_t heartbeat_seq = g.n + kGapWidth;
    for (int i = 1; i <= kTicks; ++i) {
        s.pass(kTick);
        const std::string at = "at E + " + std::to_string((i * kTick).count()) + " s";
        ASSERT_EQ(s.state(), fsm_state::Active)
            << at << " the session is " << state_name(s.state()) << " after " << answered
            << " answered TestRequest(s); since the gap opened it wrote " << s.written(g.mark);
        if (s.count("1") > answered) {
            ASSERT_TRUE(s.resend_outstanding()) << at << ": precondition, at an answer";
            (void)s.feed(s.heartbeat(++heartbeat_seq, s.last_test_req_id()));
            ++answered;
            ASSERT_EQ(s.state(), fsm_state::Active) << at << ", at the answering Heartbeat";
        }
        if (i % kTicksPerReplayedFrame == 0) {
            (void)s.feed(s.poss_dup_order(replayed++, orig::valid));
            ASSERT_EQ(s.state(), fsm_state::Active) << at << ", at the replayed frame";
            ASSERT_EQ(s.next_in(), replayed) << at << ": the replayed frame did not advance";
        }
    }
    EXPECT_GE(answered, 3U) << "the timeline has a TestRequest in each 1.5 HB; the cell "
                               "answered fewer";
    EXPECT_TRUE(s.resend_outstanding()) << "precondition: the resend ended part-way";
    EXPECT_EQ(s.count("5", g.mark), 0U) << "a Logout was written";
    EXPECT_EQ(s.count("2", g.mark), 0U) << "a second ResendRequest was written";
}

// Mismatch. During the gap, a Heartbeat above NextNumIn carries a TestReqID(112) other
// than the pending one: the in-sequence rule, a silent Disconnected at that frame.
TEST(TooHighHeartbeatDuringTheGap, OtherTestReqId_EndsTheSessionAtThatFrame) {
    Scene s;
    const Gap g = open_gap(s);
    ASSERT_NE(g.n, 0U);
    const std::string id = test_request_after_one_quiet_interval(s);
    ASSERT_FALSE(id.empty());

    const auto mark = s.sent.size();
    const auto r = s.feed(s.heartbeat(g.n + kGapWidth + 1, "NOT-" + id));
    EXPECT_TRUE(r.has_value());
    ASSERT_EQ(s.state(), fsm_state::Disconnected)
        << "a Heartbeat above NextNumIn with another TestReqID left the session "
        << state_name(s.state());
    EXPECT_EQ(s.sent.size(), mark) << "the ending is silent; the session wrote " << s.written(mark);
}

// Bounds of the rule, first: a Heartbeat below NextNumIn does not clear a pending
// TestRequest, even with its TestReqID. The session ends at the grace check.
TEST(TooHighHeartbeatDuringTheGap, Control_HeartbeatBelowNextNumIn_LeavesTheTestRequestPending) {
    Scene s;
    const Gap g = open_gap(s);
    ASSERT_NE(g.n, 0U);
    ASSERT_GT(g.n, 1U);
    const std::string id = test_request_after_one_quiet_interval(s);
    ASSERT_FALSE(id.empty());

    (void)s.feed(s.heartbeat(1, id));
    EXPECT_TRUE(s.test_request_pending()) << "a Heartbeat below NextNumIn cleared the TestRequest";
    EXPECT_EQ(s.state(), fsm_state::Active);

    s.pass(kPastTheGrace);
    EXPECT_EQ(s.state(), fsm_state::Disconnected) << "past the grace deadline";
    EXPECT_EQ(s.count("5", g.mark), 0U);
}

// Bounds of the rule, second: with validate_sequence_numbers off no resend is ever
// outstanding, and a Heartbeat above NextNumIn does not clear a pending TestRequest.
TEST(TooHighHeartbeatDuringTheGap, Control_KnobOff_HeartbeatAboveNextNumIn_LeavesItPending) {
    Scene s{Options{.validate_sequence_numbers = false}};
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    const seqnum_t n = s.next_in();
    const std::string id = test_request_after_one_quiet_interval(s);
    ASSERT_FALSE(id.empty());

    const auto mark = s.sent.size();
    (void)s.feed(s.heartbeat(n + 5, id));
    ASSERT_FALSE(s.resend_outstanding()) << "the knob-off session entered AwaitingResend";
    EXPECT_EQ(s.sent.size(), mark) << "the session wrote " << s.written(mark);
    // fixpp#582 is the follow-up on this knob: its fix flips the next assertion.
    EXPECT_TRUE(s.test_request_pending())
        << "with the knob off, a Heartbeat above NextNumIn cleared the TestRequest";
    EXPECT_EQ(s.state(), fsm_state::Active);
}

// ── D-537-8: a Heartbeat above NextNumIn that opens the gap ─────────────────────────

// T1. A quiet session. The answer to its TestRequest is the frame that reveals the gap.
// The ResendRequest goes out, the peer fills the gap, and past the TestRequest's grace
// deadline the session is still Active.
TEST(TooHighHeartbeatOpensTheGap, T1_AnswersTheTestRequest_PeerFillsTheGap_StaysActive) {
    Scene s;
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    const seqnum_t n = s.next_in();
    const std::string id = test_request_after_one_quiet_interval(s);
    ASSERT_FALSE(id.empty());

    const auto mark = s.sent.size();
    (void)s.feed(s.heartbeat(n + 5, id));
    const auto rr = s.of_type("2", mark);
    ASSERT_EQ(rr.size(), 1U) << "the gap-opening Heartbeat drew " << s.written(mark);
    EXPECT_EQ(field(rr.front(), 7), std::to_string(n));

    (void)s.feed(s.gap_fill(n, n + 6));
    ASSERT_EQ(s.next_in(), n + 6) << "the GapFill did not fill the gap";
    ASSERT_FALSE(s.resend_outstanding());

    s.pass(kPastTheGrace);
    ASSERT_EQ(s.state(), fsm_state::Active)
        << "past the grace deadline the session is " << state_name(s.state())
        << ": the Heartbeat that opened the gap did not answer the TestRequest";
    EXPECT_EQ(s.count("5"), 0U) << "a Logout was written";
}

// T2. The same opening, at E. The peer never fills the gap and answers each later
// TestRequest with a Heartbeat above NextNumIn. The session does not end at the first
// grace check; it ends with the deadline's Logout at the first frame at or after
// E + kGapBound.
TEST(TooHighHeartbeatOpensTheGap, T2_PeerDoesNotFillTheGap_EndsWithTheDeadlinesLogout) {
    Scene s;
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    const seqnum_t n = s.next_in();
    std::string id = test_request_after_one_quiet_interval(s);
    ASSERT_FALSE(id.empty());

    // E: the answer opens the gap.
    const auto gap = s.sent.size();
    (void)s.feed(s.heartbeat(n + 5, id));
    ASSERT_EQ(s.count("2", gap), 1U) << "the gap-opening Heartbeat drew " << s.written(gap);
    ASSERT_TRUE(s.resend_outstanding());

    // E + HB + 1 s: past the first TestRequest's grace deadline, before the bound.
    s.pass(kPastTheGrace);
    ASSERT_EQ(s.state(), fsm_state::Active)
        << "past the first grace deadline the session is " << state_name(s.state())
        << ": it ended silently, ahead of the deadline";
    ASSERT_EQ(s.count("1"), 2U) << "TestRequests written so far";
    ASSERT_TRUE(s.test_request_pending());
    id = s.last_test_req_id();
    (void)s.feed(s.heartbeat(n + 6, id));
    ASSERT_EQ(s.state(), fsm_state::Active) << "at the second answer, before the bound";
    EXPECT_EQ(s.count("5", gap), 0U) << "a Logout was written before the bound";

    // E + 2 HB + 1 s: past the second grace deadline, and past the bound.
    s.pass(kHeartBtInt);
    ASSERT_EQ(s.state(), fsm_state::Active)
        << "past the second grace deadline the session is " << state_name(s.state());
    ASSERT_EQ(s.count("1"), 3U) << "TestRequests written so far";
    id = s.last_test_req_id();
    const auto mark = s.sent.size();
    const auto r = s.feed(s.heartbeat(n + 7, id));
    EXPECT_TRUE(deadline_logout_written(s, mark, r));
}

// T3, mismatch. The gap-opening Heartbeat carries a TestReqID(112) other than the pending
// one: a silent Disconnected at that frame, and nothing is written, no ResendRequest in
// particular.
TEST(TooHighHeartbeatOpensTheGap, T3_OtherTestReqId_EndsTheSessionAndNoResendRequest) {
    Scene s;
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    const seqnum_t n = s.next_in();
    const std::string id = test_request_after_one_quiet_interval(s);
    ASSERT_FALSE(id.empty());

    const auto mark = s.sent.size();
    const auto r = s.feed(s.heartbeat(n + 5, "NOT-" + id));
    EXPECT_TRUE(r.has_value());
    ASSERT_EQ(s.state(), fsm_state::Disconnected)
        << "a gap-opening Heartbeat with another TestReqID left the session "
        << state_name(s.state()) << "; it wrote " << s.written(mark);
    EXPECT_EQ(s.sent.size(), mark) << "the ending is silent; the session wrote " << s.written(mark);
    EXPECT_FALSE(s.resend_outstanding());
}

// Bounds of the rule, first, a control: a gap-opening Heartbeat that carries a
// TestReqID(112) while no TestRequest is pending draws the ResendRequest, and the session
// stays Active.
TEST(TooHighHeartbeatOpensTheGap, Control_TestReqIdWithNothingPending_DrawsTheResendRequest) {
    Scene s;
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    const seqnum_t n = s.next_in();
    ASSERT_FALSE(s.test_request_pending());

    const auto mark = s.sent.size();
    const auto r = s.feed(s.heartbeat(n + 5, "UNASKED"));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(s.state(), fsm_state::Active);
    EXPECT_EQ(s.count("2", mark), 1U) << "the session wrote " << s.written(mark);
    EXPECT_TRUE(s.resend_outstanding());
}

// Bounds of the rule, second: a gap-opening Heartbeat with no TestReqID(112) while a
// TestRequest is pending clears it, as an in-sequence one does, and draws the
// ResendRequest.
TEST(TooHighHeartbeatOpensTheGap, NoTestReqIdWithOnePending_ClearsItAndDrawsTheResendRequest) {
    Scene s;
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    const seqnum_t n = s.next_in();
    ASSERT_FALSE(test_request_after_one_quiet_interval(s).empty());

    const auto mark = s.sent.size();
    (void)s.feed(s.heartbeat(n + 5));
    EXPECT_EQ(s.count("2", mark), 1U) << "the session wrote " << s.written(mark);
    EXPECT_EQ(s.state(), fsm_state::Active);
    EXPECT_FALSE(s.test_request_pending())
        << "the gap-opening Heartbeat left the TestRequest pending";
}

}  // namespace fixpp::session::test
