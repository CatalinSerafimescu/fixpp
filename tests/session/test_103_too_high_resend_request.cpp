// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_103_too_high_resend_request.cpp — test-only.
//
// Feature 103 (specs/103-resend-logon-reset-fidelity/), fixpp#577, the ResendRequest half:
// a ResendRequest above NextNumIn is answered as an in-sequence one with the same
// BeginSeqNo(7) and EndSeqNo(16); NextNumIn does not move, and at most one ResendRequest of
// fixpp's own is outstanding (research D4.1, "#577 — a too-high ResendRequest"; FR-008,
// FR-009; tasks T047).
//
// Every cell runs on both roles, on resend_gap_scene.hpp's Scene with a MemoryStore
// (Options::with_store), so the answer to a ResendRequest is replays and not the storeless
// walk's single GapFill. Each cell first checks the store exists.
//
// The session's own Logon is its number 1, and the cells send two application messages,
// stored at 2 and 3. A ResendRequest from 1 through 0 is answered by a GapFill at 34=1 over
// the Logon, then the replays of 2 and 3 with PossDupFlag(43)=Y.
#include <gtest/gtest.h>

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <cstddef>
#include <cstdint>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "session/test_103_support.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"

namespace {

namespace rg = fixpp::test_support::resend_gap;
namespace t103 = fixpp::test_support::t103;
using fixpp::session::fsm_state;
using fixpp::session::seqnum_t;
using fixpp::session::session_role;
using fixpp::session::session_test_access;

constexpr unsigned kSends = 2;

class TooHighResendRequest103 : public ::testing::TestWithParam<session_role> {};

// Active, then `kSends` application messages from the session. False after a failed step.
[[nodiscard]] bool active_with_two_sends(rg::Scene& s, std::vector<std::string>& payloads) {
    if (!s.open_to_active()) {
        ADD_FAILURE() << "setup: the session did not reach Active";
        return false;
    }
    if (session_test_access::store(*s.session) == nullptr) {
        ADD_FAILURE() << "setup: the session has no store, so a ResendRequest gets no replay";
        return false;
    }
    for (unsigned i = 0; i < kSends; ++i) {
        payloads.push_back(
            "35=D\x01"
            "11=ORD-577-" +
            std::to_string(i) + "\x01" + std::string{rg::kSideAndTime});
        auto const& p = payloads.back();
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                  s.session->send(std::span<const std::byte>{
                                      reinterpret_cast<std::byte const*>(p.data()), p.size()}),
                                  asio::use_future);
        if (!t103::pump_or_fail(
                s.ioc, [&] { return rg::Scene::ready(fut); }, fixpp::test_support::kPumpBudget,
                "t103_577/send")) {
            return false;
        }
        if (!fut.get().has_value()) {
            ADD_FAILURE() << "setup: send " << i << " failed";
            return false;
        }
    }
    return true;
}

std::vector<std::string> since(rg::Scene const& s, std::size_t mark) {
    return {s.sent.begin() + static_cast<std::ptrdiff_t>(mark), s.sent.end()};
}

// The MsgSeqNum(34) of each frame written since `mark` that replays an application message:
// MsgType D with PossDupFlag(43)=Y, in order.
std::vector<std::string> replayed(rg::Scene const& s, std::size_t mark) {
    std::vector<std::string> out;
    for (std::size_t i = mark; i < s.sent.size(); ++i) {
        if (rg::field(s.sent[i], 35) == "D" && rg::field(s.sent[i], 43) == "Y") {
            out.push_back(rg::field(s.sent[i], 34));
        }
    }
    return out;
}

// The index in `sent` of the last frame since `mark` of MsgType `type`, or `sent.size()`.
std::size_t last_index_of(rg::Scene const& s, std::size_t mark, std::string_view type,
                          bool poss_dup_only = false) {
    std::size_t at = s.sent.size();
    for (std::size_t i = mark; i < s.sent.size(); ++i) {
        if (rg::field(s.sent[i], 35) == type && (!poss_dup_only || rg::field(s.sent[i], 43) == "Y"))
            at = i;
    }
    return at;
}

// The answer to a ResendRequest from 1 through 0 after the two sends: a GapFill at 34=1 over
// the Logon first, then the replays of 2 and 3.
void expect_the_answer(rg::Scene const& s, std::size_t mark) {
    auto const gf = s.of_type("4", mark);
    ASSERT_FALSE(gf.empty()) << "no GapFill was written: " << s.written(mark);
    EXPECT_EQ(rg::field(gf.front(), 34), "1") << s.written(mark);
    EXPECT_EQ(rg::field(gf.front(), 123), "Y") << s.written(mark);
    EXPECT_EQ(rg::field(gf.front(), 36), "2") << s.written(mark);
    EXPECT_EQ(replayed(s, mark), (std::vector<std::string>{"2", "3"})) << s.written(mark);
    EXPECT_TRUE(t103::no_frame_carries_34_zero(since(s, mark))) << s.written(mark);  // SC-003
}

}  // namespace

// Arm 1, route (a), no resend outstanding: a ResendRequest at 34 = N+3 is answered, then
// fixpp sends its own ResendRequest from N, after the retransmission (FIX-SL Table 3, TC 20).
// NextNumIn does not move.
TEST_P(TooHighResendRequest103, Arm1_NoResendOutstanding_AnsweredThenOneResendRequestFromN) {
    rg::Scene s{rg::Options{.role = GetParam(), .with_store = true}};
    std::vector<std::string> payloads;
    ASSERT_TRUE(active_with_two_sends(s, payloads));
    seqnum_t const n = s.next_in();
    ASSERT_FALSE(s.resend_outstanding()) << "setup: a resend is already outstanding";

    auto const mark = s.sent.size();
    auto const r = s.feed(s.resend_request(n + 3));
    EXPECT_TRUE(r.has_value()) << "on_inbound_frame returned error " << static_cast<int>(r.error());
    ASSERT_EQ(s.state(), fsm_state::Active) << s.written(mark);

    expect_the_answer(s, mark);
    auto const rr = s.of_type("2", mark);
    ASSERT_EQ(rr.size(), 1U) << "fixpp's own ResendRequests: " << s.written(mark);
    EXPECT_EQ(rg::field(rr.front(), 7), std::to_string(n)) << s.written(mark);
    EXPECT_EQ(rg::field(rr.front(), 16), "0") << s.written(mark);
    auto const last_replay = last_index_of(s, mark, "D", /*poss_dup_only=*/true);
    ASSERT_LT(last_replay, s.sent.size()) << "no replay to order against: " << s.written(mark);
    EXPECT_GT(last_index_of(s, mark, "2"), last_replay)
        << "fixpp's ResendRequest must follow the retransmission: " << s.written(mark);
    EXPECT_EQ(s.next_in(), n) << "NextNumIn moved at a too-high ResendRequest";
    EXPECT_TRUE(s.resend_outstanding()) << "fixpp's own ResendRequest left nothing outstanding";
}

// Arm 2, route (b), a resend outstanding: the same answer, and no second ResendRequest.
TEST_P(TooHighResendRequest103, Arm2_ResendOutstanding_AnsweredAndNoSecondResendRequest) {
    rg::Scene s{rg::Options{.role = GetParam(), .with_store = true}};
    std::vector<std::string> payloads;
    ASSERT_TRUE(active_with_two_sends(s, payloads));
    seqnum_t const n = s.next_in();
    auto const gap_mark = s.sent.size();
    (void)s.feed(s.order(n + rg::kGapWidth));
    ASSERT_EQ(s.count("2", gap_mark), 1U) << "setup: no gap was opened: " << s.written(gap_mark);
    ASSERT_TRUE(s.resend_outstanding()) << "setup: no resend outstanding";

    auto const mark = s.sent.size();
    auto const r = s.feed(s.resend_request(n + 6));
    EXPECT_TRUE(r.has_value()) << "on_inbound_frame returned error " << static_cast<int>(r.error());
    ASSERT_EQ(s.state(), fsm_state::Active) << s.written(mark);

    expect_the_answer(s, mark);
    EXPECT_EQ(s.count("2", mark), 0U) << "a second ResendRequest was written: " << s.written(mark);
    EXPECT_EQ(s.next_in(), n) << "NextNumIn moved at a too-high ResendRequest";
    EXPECT_TRUE(s.resend_outstanding()) << "the answer ended the outstanding resend";
}

// Arm 2 with PossDupFlag(43)=Y and a valid OrigSendingTime(122) equal to its SendingTime(52):
// route (b) answers it as well (research D4.1: the dispatch sits before the PossDup arm,
// whose admin row would drop it).
TEST_P(TooHighResendRequest103, Arm2_PossDupResendRequest_AnsweredAndNoSecondResendRequest) {
    rg::Scene s{rg::Options{.role = GetParam(), .with_store = true}};
    std::vector<std::string> payloads;
    ASSERT_TRUE(active_with_two_sends(s, payloads));
    seqnum_t const n = s.next_in();
    auto const gap_mark = s.sent.size();
    (void)s.feed(s.order(n + rg::kGapWidth));
    ASSERT_EQ(s.count("2", gap_mark), 1U) << "setup: no gap was opened: " << s.written(gap_mark);

    auto const mark = s.sent.size();
    auto const r = s.feed(s.frame("2", n + 6,
                                  "43=Y\x01"
                                  "122=" +
                                      s.sending_time() +
                                      "\x01"
                                      "7=1\x01"
                                      "16=0\x01"));
    EXPECT_TRUE(r.has_value()) << "on_inbound_frame returned error " << static_cast<int>(r.error());
    ASSERT_EQ(s.state(), fsm_state::Active) << s.written(mark);

    expect_the_answer(s, mark);
    EXPECT_EQ(s.count("2", mark), 0U) << "a second ResendRequest was written: " << s.written(mark);
    EXPECT_EQ(s.next_in(), n) << "NextNumIn moved at a too-high ResendRequest";
    EXPECT_TRUE(s.resend_outstanding()) << "the answer ended the outstanding resend";
}

// Control: the same request in sequence, at 34 = N, draws the same answer, no ResendRequest
// of fixpp's own, and consumes N.
TEST_P(TooHighResendRequest103, Control_InSequence_SameAnswerNoResendRequest) {
    rg::Scene s{rg::Options{.role = GetParam(), .with_store = true}};
    std::vector<std::string> payloads;
    ASSERT_TRUE(active_with_two_sends(s, payloads));
    seqnum_t const n = s.next_in();

    auto const mark = s.sent.size();
    auto const r = s.feed(s.resend_request(n));
    EXPECT_TRUE(r.has_value()) << "on_inbound_frame returned error " << static_cast<int>(r.error());
    ASSERT_EQ(s.state(), fsm_state::Active) << s.written(mark);

    expect_the_answer(s, mark);
    EXPECT_EQ(s.count("2", mark), 0U) << s.written(mark);
    EXPECT_EQ(s.next_in(), n + 1) << "the in-sequence ResendRequest was not consumed";
    EXPECT_FALSE(s.resend_outstanding());
}

// Control, the knob off (research D4.1, "with validate_sequence_numbers off, nothing
// changes"): a ResendRequest above NextNumIn is delivered by the knob-off arm and answered by
// nothing; no frame is written and NextNumIn does not move. The witness of route (b)'s
// `cfg_.validate_sequence_numbers` term.
TEST_P(TooHighResendRequest103, Control_KnobOff_TooHighResendRequestNotAnswered) {
    rg::Scene s{
        rg::Options{.role = GetParam(), .validate_sequence_numbers = false, .with_store = true}};
    std::vector<std::string> payloads;
    ASSERT_TRUE(active_with_two_sends(s, payloads));
    seqnum_t const n = s.next_in();

    auto const mark = s.sent.size();
    auto const r = s.feed(s.resend_request(n + 3));
    EXPECT_TRUE(r.has_value()) << "on_inbound_frame returned error " << static_cast<int>(r.error());
    EXPECT_EQ(s.state(), fsm_state::Active) << s.written(mark);
    EXPECT_EQ(s.sent.size(), mark) << "the knob-off session answered: " << s.written(mark);
    EXPECT_EQ(s.next_in(), n) << "NextNumIn moved";
}

INSTANTIATE_TEST_SUITE_P(BothRoles, TooHighResendRequest103,
                         ::testing::Values(session_role::initiator, session_role::acceptor),
                         [](auto const& info) { return std::string{rg::role_name(info.param)}; });
