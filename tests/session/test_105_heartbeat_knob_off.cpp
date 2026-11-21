// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_105_heartbeat_knob_off.cpp — 105-transport-liveness (tasks.md T003, T081,
// T082).
//
// An out-of-sequence Heartbeat with sequence validation off (#582; contract C-5.1, C-5.2), and the
// TestReqID(112) rule on every Heartbeat route (ruling D-d; contract C-5.4; research D5).
//
// The Engine cells run both roles on tests/session/plain_engine_rig.hpp with HeartBtInt 10 s and
// test_request_threshold = HeartBtInt (tasks.md, Execution rules, "Liveness instants"); each
// TestRequest is read by test_105_support.hpp's wire reader. An "at once" reading holds the mock
// clock still and pumps until the peer's read ends or the wall cap passes. The wall-clock timers:
// none (plain rig, mock transport).
//
// No ASSERT_* runs between a rig's start() and its stop() (tasks.md, Execution rules).
#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "session/mock_reconnect_factory.hpp"
#include "session/plain_engine_rig.hpp"
#include "session/resend_gap_scene.hpp"
#include "session/test_105_support.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"

namespace {

using namespace std::chrono_literals;
using fixpp::session::fsm_state;
using fixpp::session::session_role;
using fixpp::session::session_test_access;
using fixpp::session::SessionConfig;
namespace pr = fixpp::test_support::plain_rig;
namespace rg = fixpp::test_support::resend_gap;
namespace t105 = fixpp::test_support::t105;

constexpr std::chrono::milliseconds kWallCap{1500};
constexpr std::chrono::seconds kHb{10};
// The window a cell that expects the connection to stay open waits, with the clock still, for a
// close that must not come; no wall-clock timer of the plain rig closes the connection in it.
constexpr std::chrono::milliseconds kOpenWindow{300};

// Pumps with the mock clock held still until the peer's read ends or window passes; a miss is the
// reading, not a failure.
bool read_ends_within(pr::Rig& rig, std::chrono::milliseconds window) {
    return fixpp::test_support::pump_until(
        rig.ioc, [&] { return rig.peer.read_ended; }, window, fixpp::test_support::kPumpSlice,
        nullptr);
}

std::string hb_with_id(pr::Rig const& rig, std::uint32_t seq, std::optional<std::string> id) {
    if (!id) return rig.msg("0", seq);
    return rig.msg("0", seq, "112=" + *id + "\x01");
}

// The readings of a run of answered TestRequest cycles.
struct CycleReading {
    bool active = false;  // the session reached Active
    int cycles = 0;       // TestRequests read and answered with the session still Active
    bool still_active = false;
    bool logout_read = false;
    bool read_ended = false;
    std::string wire;
};

// Reaches Active, then for each of `count` cycles reads the next TestRequest and answers it with
// a Heartbeat at next_seq() carrying the TestRequest's own 112 (or none, when answer_with_id is
// false). A cycle counts only when the session is still Active after the answer.
CycleReading run_cycles(session_role role, bool validate_sequence_numbers, int count,
                        std::function<std::uint32_t()> const& next_seq, bool answer_with_id,
                        const char* site) {
    pr::Rig rig{std::make_shared<pr::CountingApp>()};
    auto cfg = rig.cfg(role);
    cfg.heartbeat_interval = kHb;
    cfg.test_request_threshold = std::chrono::milliseconds{kHb};
    cfg.validate_sequence_numbers = validate_sequence_numbers;
    CycleReading r;
    bool up = rig.start(std::move(cfg)) && rig.to_active();
    r.active = up;
    for (int c = 0; up && c < count; ++c) {
        if (rig.state() != fsm_state::Active) break;
        auto const tr = t105::advance_until_msg_type(
            *rig.clock, rig.ioc, [&] { return rig.peer.received; }, "1",
            static_cast<std::size_t>(c), 1s, t105::liveness_wire_cap(kHb, 1s), site);
        if (!tr || rig.state() != fsm_state::Active) break;
        std::optional<std::string> id;
        if (answer_with_id) id = pr::field_value(tr->frame, "112");
        if (!rig.deliver(hb_with_id(rig, next_seq(), id))) break;
        (void)t105::drive_bounded(rig.ioc, 64);
        if (rig.state() != fsm_state::Active) break;
        ++r.cycles;
    }
    if (up) {
        r.still_active = rig.state() == fsm_state::Active;
        r.logout_read = !pr::frames_of_type(rig.peer.received, "5").empty();
        r.read_ended = rig.peer.read_ended;
        r.wire = rig.peer.received;
    }
    rig.stop();
    return r;
}

void expect_stays_active(CycleReading const& r, int count, const char* site) {
    ASSERT_TRUE(r.active) << site << ": setup";
    EXPECT_EQ(r.cycles, count) << site << ": the session left Active before the last answer";
    EXPECT_TRUE(r.still_active) << site;
    EXPECT_FALSE(r.logout_read) << site << ": the peer read a Logout: " << r.wire;
    EXPECT_FALSE(r.read_ended) << site << ": the peer's read ended";
}

// The readings of one Heartbeat sent while a TestRequest is pending, with the clock held still.
struct OneFrameReading {
    bool active = false;
    std::string tr_id;           // the pending TestRequest's 112
    std::string earlier_id;      // an earlier TestRequest's 112 (two-TestRequest runs)
    bool eof_at_once = false;    // the peer's read ended in EOF with the clock held still
    bool active_after = false;   // the session still Active after the frame
    bool pending_after = false;  // a TestRequest still pending after the frame
    std::size_t logouts_after = 0;
    std::size_t resend_requests_after = 0;
    std::string give_up_text;  // Text(58) of the give-up's Logout, when one followed
};

enum class second_tr : std::uint8_t { none, answer_first };

// Reaches Active, reads TestRequest 1; with second_tr::answer_first answers it in sequence and
// reads TestRequest 2. Then delivers frame(rig, tr1_id, tr2_id) and reads the outcome with the
// clock still; with follow_to_give_up, then steps the clock until the session leaves Active and
// keeps the Text of the Logout that follows the frame.
OneFrameReading run_one_frame(
    session_role role, bool validate_sequence_numbers, second_tr mode,
    std::function<std::string(pr::Rig const&, std::string const&, std::string const&)> const& frame,
    bool expect_end, bool follow_to_give_up, const char* site) {
    pr::Rig rig{std::make_shared<pr::CountingApp>()};
    auto cfg = rig.cfg(role);
    cfg.heartbeat_interval = kHb;
    cfg.test_request_threshold = std::chrono::milliseconds{kHb};
    cfg.validate_sequence_numbers = validate_sequence_numbers;
    OneFrameReading r;
    bool up = rig.start(std::move(cfg)) && rig.to_active();
    r.active = up;
    std::string tr1;
    std::string tr2;
    if (up) {
        auto const a = t105::advance_until_msg_type(
            *rig.clock, rig.ioc, [&] { return rig.peer.received; }, "1", 0, 1s,
            t105::liveness_wire_cap(kHb, 1s), site);
        up = a.has_value();
        if (up) tr1 = pr::field_value(a->frame, "112");
    }
    if (up && mode == second_tr::answer_first) {
        up = rig.deliver(hb_with_id(rig, 2, tr1));
        if (up) {
            auto const b = t105::advance_until_msg_type(
                *rig.clock, rig.ioc, [&] { return rig.peer.received; }, "1", 1, 1s,
                t105::liveness_wire_cap(kHb, 1s), site);
            up = b.has_value() && rig.state() == fsm_state::Active;
            if (up) tr2 = pr::field_value(b->frame, "112");
        }
    }
    if (up) {
        r.tr_id = mode == second_tr::answer_first ? tr2 : tr1;
        r.earlier_id = mode == second_tr::answer_first ? tr1 : std::string{};
        auto const mark = rig.peer.received.size();
        rig.peer.send(frame(rig, tr1, tr2));
        (void)rig.run_until([&] { return rig.peer.all_written(); });
        bool const ended = expect_end
                               ? t105::pump_until_peer_read_ends(
                                     rig.ioc, [&] { return rig.peer.read_ended; }, kWallCap, site)
                               : read_ends_within(rig, kOpenWindow);
        r.eof_at_once = ended && t105::ending_of(rig.peer) == t105::peer_ending::eof;
        r.active_after = rig.state() == fsm_state::Active;
        if (auto const s = rig.session())
            r.pending_after = session_test_access::test_request_pending(*s);
        auto const after = rig.peer.received.substr(mark);
        r.logouts_after = pr::frames_of_type(after, "5").size();
        r.resend_requests_after = pr::frames_of_type(after, "2").size();
        if (follow_to_give_up && r.active_after) {
            std::chrono::nanoseconds advanced{0};
            while (rig.state() == fsm_state::Active &&
                   advanced < t105::liveness_wire_cap(kHb, 1s)) {
                rig.clock->advance(1s);
                advanced += 1s;
                (void)t105::drive_bounded(rig.ioc, 64);
            }
            auto const lo = pr::frames_of_type(rig.peer.received.substr(mark), "5");
            if (!lo.empty()) r.give_up_text = pr::field_value(lo.front(), "58");
        }
    }
    rig.stop();
    return r;
}

class HeartbeatKnobOff105 : public ::testing::TestWithParam<session_role> {};

// SC-003 (C-5.1): knob off, the peer's numbers ahead of NextNumIn; ten TestRequests, each answered
// with its own 112. The session stays Active and the peer never reads a Logout or EOF.
TEST_P(HeartbeatKnobOff105, AheadPeerAnswersTenCycles_StaysActive) {
    std::uint32_t seq = 100;
    auto const r =
        run_cycles(GetParam(), false, 10, [&] { return seq++; }, true, "AheadPeerAnswersTenCycles");
    expect_stays_active(r, 10, "AheadPeerAnswersTenCycles");
}

// C-5.2: knob off, a Heartbeat ahead of NextNumIn whose 112 fixpp never sent ends the session with
// no Logout, and the peer reads EOF at once.
TEST_P(HeartbeatKnobOff105, MismatchedTestReqId_EndsAndCloses) {
    auto const r = run_one_frame(
        GetParam(), false, second_tr::none,
        [](pr::Rig const& rig, std::string const&, std::string const&) {
            return hb_with_id(rig, 100, std::string{"XYZ"});
        },
        true, false, "MismatchedTestReqId");
    ASSERT_TRUE(r.active) << "setup";
    ASSERT_FALSE(r.tr_id.empty()) << "setup: no TestRequest was read";
    EXPECT_TRUE(r.eof_at_once) << "the peer's read did not end in EOF with the clock held still";
    EXPECT_FALSE(r.active_after);
    EXPECT_EQ(r.logouts_after, 0U) << "a Logout was written";
}

// C-5.1: knob off, a Heartbeat ahead of NextNumIn with no 112 answers the TestRequest.
TEST_P(HeartbeatKnobOff105, AbsentTestReqId_Answers) {
    std::uint32_t seq = 100;
    auto const r =
        run_cycles(GetParam(), false, 3, [&] { return seq++; }, false, "AbsentTestReqId");
    expect_stays_active(r, 3, "AbsentTestReqId");
}

// C-5.1: knob off, the peer's numbers behind NextNumIn (each answer at 1, below it).
TEST_P(HeartbeatKnobOff105, BehindPeerAnswers) {
    auto const r = run_cycles(GetParam(), false, 3, [] { return 1U; }, true, "BehindPeerAnswers");
    expect_stays_active(r, 3, "BehindPeerAnswers");
}

// [guard: the #582 rule applied with the knob on] With the knob on, a Heartbeat below NextNumIn
// carrying the pending 112 does not answer: it draws the too-low Logout, as B-537-3 and 103 say.
TEST_P(HeartbeatKnobOff105, KnobOn_Unchanged) {
    auto const r = run_one_frame(
        GetParam(), true, second_tr::none,
        [](pr::Rig const& rig, std::string const& tr1, std::string const&) {
            return hb_with_id(rig, 1, tr1);
        },
        false, false, "KnobOn_Unchanged");
    ASSERT_TRUE(r.active) << "setup";
    ASSERT_FALSE(r.tr_id.empty()) << "setup: no TestRequest was read";
    EXPECT_EQ(r.logouts_after, 1U) << "the too-low Logout";
    EXPECT_TRUE(r.pending_after) << "the TestRequest was answered by a Heartbeat below NextNumIn";
}

FIXPP_105_BOTH_ROLES(HeartbeatKnobOff105);

class TestReqId105 : public ::testing::TestWithParam<session_role> {};

// C-5.4, the in-sequence arm: after TestRequest 2, an in-sequence Heartbeat carrying TestRequest
// 1's 112 neither answers TestRequest 2 nor ends the session; TestRequest 2 stays pending, so the
// give-up that follows names it.
TEST_P(TestReqId105, InSequenceArm_EarlierId_NeitherAnswersNorEnds) {
    auto const r = run_one_frame(
        GetParam(), true, second_tr::answer_first,
        [](pr::Rig const& rig, std::string const& tr1, std::string const&) {
            return hb_with_id(rig, 3, tr1);
        },
        false, true, "InSequenceArm_EarlierId");
    ASSERT_TRUE(r.active) << "setup";
    ASSERT_FALSE(r.earlier_id.empty()) << "setup: TestRequest 2 was not reached";
    EXPECT_FALSE(r.eof_at_once) << "the earlier 112 closed the connection";
    EXPECT_TRUE(r.active_after) << "the earlier 112 ended the session";
    EXPECT_EQ(r.logouts_after, 0U);
    EXPECT_TRUE(r.pending_after) << "the earlier 112 answered TestRequest 2";
    EXPECT_EQ(r.give_up_text, "TestRequest unanswered, TestReqID " + r.tr_id);
}

// C-5.4, the answer-pending route (a Heartbeat above NextNumIn, which opens a gap): TestRequest 1's
// 112 after TestRequest 2 neither answers nor ends; the frame goes on to the gap's ResendRequest.
TEST_P(TestReqId105, AnswerPendingRoute_EarlierId_NeitherAnswersNorEnds) {
    auto const r = run_one_frame(
        GetParam(), true, second_tr::answer_first,
        [](pr::Rig const& rig, std::string const& tr1, std::string const&) {
            return hb_with_id(rig, 10, tr1);
        },
        false, false, "AnswerPendingRoute_EarlierId");
    ASSERT_TRUE(r.active) << "setup";
    ASSERT_FALSE(r.earlier_id.empty()) << "setup: TestRequest 2 was not reached";
    EXPECT_FALSE(r.eof_at_once) << "the earlier 112 closed the connection";
    EXPECT_TRUE(r.active_after) << "the earlier 112 ended the session";
    EXPECT_EQ(r.logouts_after, 0U);
    EXPECT_TRUE(r.pending_after) << "the earlier 112 answered TestRequest 2";
    EXPECT_EQ(r.resend_requests_after, 1U) << "the frame did not go on to the gap's ResendRequest";
}

// [guard: the canonical-form test deleted] TestRequest 1's number with a leading zero is not an id
// fixpp sent: a mismatch, EOF at once and no Logout.
TEST_P(TestReqId105, LeadingZeroId_Mismatch) {
    auto const r = run_one_frame(
        GetParam(), true, second_tr::answer_first,
        [](pr::Rig const& rig, std::string const& tr1, std::string const&) {
            std::string id = tr1;
            if (id.size() > 2) id.insert(2, "0");
            return hb_with_id(rig, 3, id);
        },
        true, false, "LeadingZeroId");
    ASSERT_TRUE(r.active) << "setup";
    ASSERT_FALSE(r.earlier_id.empty()) << "setup: TestRequest 2 was not reached";
    EXPECT_TRUE(r.eof_at_once) << "the leading-zero 112 did not end the session at once";
    EXPECT_FALSE(r.active_after);
    EXPECT_EQ(r.logouts_after, 0U);
}

// [guard: the pending test deleted] With no TestRequest pending (before the first) the 112 is not
// compared: a Heartbeat carrying one fixpp never sent leaves the
// session Active.
TEST_P(TestReqId105, NoTestRequestPending_NotCompared) {
    pr::Rig rig{std::make_shared<pr::CountingApp>()};
    auto cfg = rig.cfg(GetParam());
    cfg.heartbeat_interval = kHb;
    cfg.test_request_threshold = std::chrono::milliseconds{kHb};
    bool up = rig.start(std::move(cfg)) && rig.to_active();
    bool const active = up;
    bool pending_before = true;
    bool eof = false;
    bool active_after = false;
    if (up) {
        if (auto const s = rig.session())
            pending_before = session_test_access::test_request_pending(*s);
        rig.peer.send(hb_with_id(rig, 2, std::string{"XYZ"}));
        (void)rig.run_until([&] { return rig.peer.all_written(); });
        eof = read_ends_within(rig, kOpenWindow);
        active_after = rig.state() == fsm_state::Active;
    }
    rig.stop();
    ASSERT_TRUE(active) << "setup";
    ASSERT_FALSE(pending_before) << "setup: a TestRequest was pending";
    EXPECT_FALSE(eof) << "the 112 was compared with no TestRequest pending";
    EXPECT_TRUE(active_after);
}

FIXPP_105_BOTH_ROLES(TestReqId105);

// C-5.4: a reconnect starts a new set. TestRequest 1's 112, answered on the first connection, sent
// on the second while that connection's TestRequest is pending, is a mismatch: the session ends,
// no Logout is written, and the connection closes at the next call's exit. A direct initiator on
// mock transports.
TEST(TestReqId105Direct, PreviousConnectionId_Mismatch) {
    auto app = std::make_shared<rg::GapApp>();
    rg::Scene s{rg::Options{.heartbeat = kHb, .app = app}};
    auto factory = std::make_shared<fixpp::test_support::MockReconnectFactory>();
    auto closed = std::make_shared<std::atomic<bool>>(false);
    factory->tweak_script = [closed](fixpp::transport::test::Script& sc) {
        sc.closed_flag = closed;
    };
    s.cfg.transport_factory_override = factory;
    s.cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 19099};
    s.cfg.test_request_threshold = std::chrono::milliseconds{kHb};
    s.session = std::make_unique<fixpp::session::Session>(s.engine, s.cfg);
    auto run = [&](auto a) {
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                  std::move(a), asio::use_future);
        (void)fixpp::test_support::pump_until(
            s.ioc, [&] { return rg::Scene::ready(fut); }, fixpp::test_support::kPumpBudget,
            fixpp::test_support::kPumpSlice, "PreviousConnectionId");
        bool const ok = rg::Scene::ready(fut) && fut.get().has_value();
        s.drain();
        return ok;
    };
    auto wire = [&] {
        auto const b = factory->last_transport->outbound_bytes_seen();
        return std::string{reinterpret_cast<const char*>(b.data()), b.size()};
    };
    // The first TestRequest on the current connection's transport.
    auto first_tr = [&] {
        std::size_t steps = 0;
        while (pr::frames_of_type(wire(), "1").empty() && steps++ < 30) s.pass(1s);
        auto const trs = pr::frames_of_type(wire(), "1");
        return trs.empty() ? std::string{} : pr::field_value(trs.front(), "112");
    };
    ASSERT_TRUE(run(s.session->open())) << "setup: open()";
    ASSERT_TRUE(run(s.session->drive_reconnect())) << "setup: drive_reconnect()";
    (void)s.feed(s.frame("A", 1,
                         "98=0\x01"
                         "108=10\x01"));
    ASSERT_EQ(s.state(), fsm_state::Active) << "setup";
    std::string const tr1 = first_tr();
    ASSERT_FALSE(tr1.empty()) << "setup: no TestRequest on the first connection";
    (void)s.feed(s.frame("0", 2, "112=" + tr1 + "\x01"));
    ASSERT_FALSE(session_test_access::test_request_pending(*s.session))
        << "setup: TestRequest 1 was not answered";
    ASSERT_TRUE(run(s.session->drive_reconnect())) << "setup: the second connection";
    closed->store(false);
    (void)s.feed(s.frame("A", 3,
                         "98=0\x01"
                         "108=10\x01"));
    ASSERT_EQ(s.state(), fsm_state::Active) << "setup: Active on the second connection";
    std::string const tr2 = first_tr();
    ASSERT_FALSE(tr2.empty()) << "setup: no TestRequest on the second connection";
    ASSERT_NE(tr2, tr1) << "setup";
    auto const mark = wire().size();
    (void)s.feed(s.frame("0", 4, "112=" + tr1 + "\x01"));
    s.drain();
    EXPECT_EQ(s.state(), fsm_state::Disconnected)
        << "a previous connection's 112 did not end the session";
    // A direct Session has no pump: the ended connection is closed at the exit of the next call
    // (tests/session/test_105_connection_close.cpp, DirectInitiator_EndingThenDriveReconnect).
    static constexpr char kPayload[] = "35=D\x01";
    (void)run(s.session->send(std::as_bytes(std::span{kPayload, sizeof(kPayload) - 1})));
    EXPECT_TRUE(closed->load()) << "the ended connection is still open after the next call";
    EXPECT_TRUE(pr::frames_of_type(wire().substr(mark), "5").empty()) << "a Logout was written";
}

}  // namespace
