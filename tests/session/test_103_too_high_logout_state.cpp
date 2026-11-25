// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_103_too_high_logout_state.cpp — test-only.
//
// 105-transport-liveness (the R-2 wait, contract C-2) supersedes, for every Logout on C-2's
// "Which Logouts" list, the "Logout, then Disconnected at once" ending this file pinned: the
// session stays Active until the peer's Logout or the bound (logout_disconnect_timeout_ms from
// the Logout's write start), and ends there; the cells read it through
// tests/support/r2_wait_reading.hpp or their fixture's form of it.
//
// Feature 103 (specs/103-resend-logon-reset-fidelity/), fixpp#577, the Logout half: TU2, the
// cells that read 103 state (the pending-Logout mark's phase, the Logout owner, the bound's
// join counter; research D4.2 to D4.7, "#577 — a too-high Logout"; tasks T080 onward). The
// arms that read none are test_103_too_high_logout.cpp's.
//
// Every cell runs on both roles, on resend_gap_scene.hpp's Scene. The mock clock moves only
// where a cell calls pass(); a cell that holds a store call uses Options::recording_store and
// pumps with the Scene's bounded pumps instead.
#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

#include "session/test_103_support.hpp"
#include "support/seqnum_manager_test_access.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"

namespace {

namespace rg = fixpp::test_support::resend_gap;
using fixpp::session::close_mode;
using fixpp::session::fsm_state;
using fixpp::session::seqnum_t;
using fixpp::session::session_role;
using fixpp::session::session_test_access;

constexpr std::uint32_t kT = 2000;  // the bound T, in milliseconds
constexpr int kNone = 0;            // session_test_access::peer_logout_phase's `none`
constexpr int kPending = 1;         // session_test_access::peer_logout_phase's `pending`

// Counts fromAdmin calls by MsgType.
class AdminCountingApp final : public rg::GapApp {
public:
    std::vector<std::string> from_admin_types;
    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
        const fixpp::session::SessionId& /*id*/) override {
        from_admin_types.emplace_back(msg.msg_type());
        return {};
    }
    [[nodiscard]] int from_admin_count(std::string_view msg_type) const {
        int n = 0;
        for (auto const& t : from_admin_types) n += static_cast<int>(t == msg_type);
        return n;
    }
};

class TooHighLogoutState103 : public ::testing::TestWithParam<session_role> {};

std::future<fixpp::core::expected_t<void>> spawn_close(rg::Scene& s, close_mode const mode) {
    return asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                          s.session->close(mode), asio::use_future);
}

int phase(rg::Scene const& s) { return session_test_access::peer_logout_phase(*s.session); }
int bound_count(rg::Scene const& s) {
    return session_test_access::peer_logout_bound_count(*s.session);
}

// close(graceful)'s phase-1 grace is a real-time timer of T, so a close that waits for it
// completes within T; the budget is derived from it.
constexpr auto kCloseBudget = 2 * std::chrono::milliseconds{kT};
// A close() whose wait ends with the Logout in flight, after that Logout's hold is released:
// below the grace T, so a wait that only the grace ends cannot meet it.
constexpr auto kAfterReleaseBudget = std::chrono::milliseconds{kT} / 4;

}  // namespace

// (e) The Logout while a resend is outstanding: no second ResendRequest, and the
// acknowledgement at T.
TEST_P(TooHighLogoutState103, E_LogoutWhileAResendIsOutstanding_NoSecondResendRequestAckAtT) {
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = kT}};
    ASSERT_TRUE(s.open_to_active());
    seqnum_t const n = s.next_in();
    auto const gap_mark = s.sent.size();
    (void)s.feed(s.order(n + rg::kGapWidth));
    ASSERT_EQ(s.count("2", gap_mark), 1U) << "setup: no gap was opened: " << s.written(gap_mark);
    auto const mark = s.sent.size();
    (void)s.feed(s.logout(n + rg::kGapWidth + 1));
    EXPECT_EQ(s.count("2", mark), 0U) << "a second ResendRequest: " << s.written(mark);
    s.pass(std::chrono::milliseconds{kT});
    EXPECT_EQ(s.count("5", mark), 1U) << "no acknowledgement at T: " << s.written(mark);
    EXPECT_EQ(s.state(), fsm_state::Disconnected) << s.written(mark);
}

// (f), T > 2 × HB: the gap deadline comes first and writes its Logout; the bound writes none
// after it. The peer answers each TestRequest so the liveness loop does not end the session.
TEST_P(TooHighLogoutState103, F1_DeadlineBeforeTheBound_ExactlyOneLogout) {
    constexpr std::uint32_t kLongT = 3 * 30'000;  // above 2 × kHeartBtInt
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = kLongT}};
    ASSERT_TRUE(s.open_to_active());
    seqnum_t const n = s.next_in();
    auto const mark = s.sent.size();
    (void)s.feed(s.logout(n + 3));
    std::uint32_t seq = n + 3;
    std::size_t answered = 0;
    constexpr auto kTick = rg::kHeartBtInt / 2;
    for (int i = 0; i < 6 && s.state() == fsm_state::Active; ++i) {
        s.pass(kTick);
        if (s.count("1") > answered) {
            (void)s.feed(s.heartbeat(++seq, s.last_test_req_id()));
            ++answered;
        }
    }
    // Past 2 × HB: a frame above NextNumIn draws the deadline's Logout, if it has not.
    if (s.state() == fsm_state::Active) (void)s.feed(s.heartbeat(++seq));
    ASSERT_EQ(s.count("5", mark), 1U) << "the deadline wrote no Logout: " << s.written(mark);
    // The deadline's claim (research D4.6): the mark is `none` once its ending ran, before
    // any close().
    EXPECT_EQ(phase(s), kNone) << "the deadline did not claim the mark";
    s.pass(std::chrono::milliseconds{kLongT});
    EXPECT_EQ(s.count("5", mark), 1U) << s.written(mark);
    EXPECT_EQ(s.state(), fsm_state::Disconnected);
}

// (f), T < 2 × HB: the bound acknowledges at T; a frame past 2 × HB draws no second Logout.
TEST_P(TooHighLogoutState103, F2_BoundBeforeTheDeadline_ExactlyOneLogout) {
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = kT}};
    ASSERT_TRUE(s.open_to_active());
    seqnum_t const n = s.next_in();
    auto const mark = s.sent.size();
    (void)s.feed(s.logout(n + 3));
    s.pass(std::chrono::milliseconds{kT});
    EXPECT_EQ(s.count("5", mark), 1U) << "no acknowledgement at T: " << s.written(mark);
    s.pass(2 * rg::kHeartBtInt);
    (void)s.feed(s.heartbeat(n + 4));
    EXPECT_EQ(s.count("5", mark), 1U) << s.written(mark);
}

// (g) close(terminal) while pending: no Logout; the join counter 0 when close() returns.
TEST_P(TooHighLogoutState103, G_CloseTerminalWhilePending_NoLogout) {
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = kT}};
    ASSERT_TRUE(s.open_to_active());
    seqnum_t const n = s.next_in();
    (void)s.feed(s.logout(n + 3));
    ASSERT_EQ(phase(s), kPending) << "setup: no pending mark";
    auto const mark = s.sent.size();
    auto close_fut = spawn_close(s, close_mode::terminal);
    ASSERT_TRUE(s.pump_until([&] { return rg::Scene::ready(close_fut); }, kCloseBudget));
    EXPECT_EQ(s.count("5", mark), 0U) << s.written(mark);
    EXPECT_EQ(bound_count(s), 0);
}

// (g) close(graceful) while pending: exactly one Logout, phase 1's; the counter 0.
TEST_P(TooHighLogoutState103, G_CloseGracefulWhilePending_ExactlyOneLogoutPhase1s) {
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = kT}};
    ASSERT_TRUE(s.open_to_active());
    seqnum_t const n = s.next_in();
    (void)s.feed(s.logout(n + 3));
    ASSERT_EQ(phase(s), kPending) << "setup: no pending mark";
    auto const mark = s.sent.size();
    auto close_fut = spawn_close(s, close_mode::graceful);
    ASSERT_TRUE(s.pump_until([&] { return rg::Scene::ready(close_fut); }, kCloseBudget));
    EXPECT_EQ(s.count("5", mark), 1U) << s.written(mark);
    EXPECT_EQ(bound_count(s), 0);
}

// (h) The Logout resent in sequence while pending: one Logout, the in-sequence
// acknowledgement; fromAdmin once, for the resent frame.
TEST_P(TooHighLogoutState103, H_LogoutResentInSequence_OneAckFromAdminOnce) {
    auto const app = std::make_shared<AdminCountingApp>();
    rg::Scene s{rg::Options{.role = GetParam(), .app = app, .logout_timeout_ms = kT}};
    ASSERT_TRUE(s.open_to_active());
    seqnum_t const n = s.next_in();
    auto const mark = s.sent.size();
    (void)s.feed(s.logout(n + 3));
    ASSERT_EQ(phase(s), kPending) << "setup: no pending mark";
    (void)s.feed(s.gap_fill(n, n + 3));
    (void)s.feed(s.logout(n + 3));
    EXPECT_EQ(s.count("5", mark), 1U) << s.written(mark);
    // The in-sequence Logout's claim (research D4.6): `none`, before any close().
    EXPECT_EQ(phase(s), kNone) << "the in-sequence Logout did not claim the mark";
    EXPECT_EQ(app->from_admin_count("5"), 1);
    s.pass(std::chrono::milliseconds{kT});
    EXPECT_EQ(s.count("5", mark), 1U) << "a second Logout at T: " << s.written(mark);
}

// (j) A frame that ends the session with no Logout and no close() (a wrong SenderCompID)
// while pending, the clock not moved: Disconnected, and the join counter 0 within a bounded
// pump.
TEST_P(TooHighLogoutState103, J_WrongCompIdWhilePending_CounterZeroWithNoClockMove) {
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = kT}};
    ASSERT_TRUE(s.open_to_active());
    seqnum_t const n = s.next_in();
    (void)s.feed(s.logout(n + 3));
    ASSERT_EQ(phase(s), kPending) << "setup: no pending mark";
    (void)s.feed(s.order_from_other_comp_id(n));
    EXPECT_EQ(s.state(), fsm_state::Disconnected);
    EXPECT_TRUE(s.pump_until([&] { return bound_count(s) == 0; }))
        << "the bound did not exit with the clock not moved";
}

// (k) A TestRequest and an in-sequence ResendRequest while pending: a Heartbeat with the
// TestReqID, then the replay; Active with the mark pending.
TEST_P(TooHighLogoutState103, K_AdminTrafficWhilePending_AnsweredStillPending) {
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = kT}};
    ASSERT_TRUE(s.open_to_active());
    seqnum_t const n = s.next_in();
    (void)s.feed(s.logout(n + 3));
    auto const mark = s.sent.size();
    (void)s.feed(s.test_request(n));
    auto const hb = s.of_type("0", mark);
    ASSERT_EQ(hb.size(), 1U) << s.written(mark);
    EXPECT_EQ(rg::field(hb.front(), 112), "PEER-TR") << s.written(mark);
    auto const rr_mark = s.sent.size();
    (void)s.feed(s.resend_request(n + 1));
    EXPECT_GE(s.count("4", rr_mark), 1U) << "no replay: " << s.written(rr_mark);
    EXPECT_EQ(s.state(), fsm_state::Active);
    EXPECT_EQ(phase(s), kPending);
}

// ── Held store calls: R-588-e and LO-1 ───────────────────────────────────────────────

namespace {

using fixpp::test_support::store_op;
using fixpp::test_support::StoreCall;

// The next outbound number: the one the session's next frame takes.
seqnum_t next_out(rg::Scene const& s) {
    return session_test_access::seqnum_mgr(*s.session).peek_outbound();
}
// Holds the store() of outbound number `seq`.
void hold_store_of(rg::Scene& s, seqnum_t const seq) {
    s.log->hold_in = [seq](StoreCall const& c) {
        return c.op == store_op::store && c.dir == fixpp::session::direction_t::outbound &&
               c.first == seq;
    };
}
// A PossDup NewOrderSingle whose OrigSendingTime(122) is a minute after its SendingTime(52):
// on_inbound_active_poss_dup_'s Arm D, a Reject and then a Logout.
std::vector<std::byte> poss_dup_order_with_later_orig(rg::Scene const& s, std::uint32_t seq) {
    return rg::Scene::raw("D", seq, "TW", s.sending_time(),
                          "43=Y\x01"
                          "122=" +
                              s.sending_time(std::chrono::seconds{60}) + "\x01" +
                              rg::Scene::order_fields(seq));
}

// Brings a recording-store Scene to Active with the log armed.
[[nodiscard]] bool active_recorded(rg::Scene& s) {
    if (!s.open_to_active()) return false;
    s.log->arm();
    return true;
}

}  // namespace

// LO-1 (research D4.6; tasks T082): an inbound Logout emitter's Logout is held in its store;
// close(graceful) is spawned; the hold is released. Exactly one Logout, the emitter's, and
// close() returns with the owner clear. Control: close(graceful) with no Logout in flight
// sends phase 1's.
enum class Emitter : std::uint8_t {
    in_sequence_ack,
    bound_zero_ack,
    sending_time_fatal,
    arm_d,
    gap_deadline,
    arm_b  // a frame below NextNumIn without 43=Y (research D9)
};
struct Lo1Case {
    Emitter emitter;
    bool with_mark;
};

class Lo1 : public ::testing::TestWithParam<std::tuple<session_role, Lo1Case>> {};

TEST_P(Lo1, InboundLogoutHeldThenCloseGraceful_ExactlyOneLogout) {
    auto const [role, c] = GetParam();
    rg::Scene s{rg::Options{.role = role,
                            .logout_timeout_ms = c.emitter == Emitter::bound_zero_ack ? 0 : kT,
                            .recording_store = true}};
    ASSERT_TRUE(active_recorded(s)) << "setup";
    seqnum_t n = s.next_in();
    if (c.with_mark) {
        (void)s.feed(s.logout(n + 3));
        ASSERT_EQ(phase(s), kPending) << "setup: no pending mark";
        (void)s.feed(s.gap_fill(n, n + 3));  // N to S, which does not pass it
        n = s.next_in();
    }
    std::uint32_t deadline_seq = 0;
    if (c.emitter == Emitter::gap_deadline) {
        (void)s.feed(s.order(n + rg::kGapWidth));
        deadline_seq = n + rg::kGapWidth;
        std::size_t answered = 0;
        for (int i = 0; i < 4 && s.state() == fsm_state::Active; ++i) {
            s.pass(rg::kHeartBtInt / 2);
            if (s.count("1") > answered) {
                (void)s.feed(s.heartbeat(++deadline_seq, s.last_test_req_id()));
                ++answered;
            }
        }
        ASSERT_EQ(s.state(), fsm_state::Active) << "setup: " << s.written();
        s.pass(std::chrono::seconds{1});
    }
    // The emitter's Logout is the session's frame after any Reject.
    seqnum_t const logout_seq =
        next_out(s) +
        ((c.emitter == Emitter::sending_time_fatal || c.emitter == Emitter::arm_d) ? 1U : 0U);
    hold_store_of(s, logout_seq);
    auto const mark = s.sent.size();
    std::vector<std::byte> frame;
    switch (c.emitter) {
        case Emitter::in_sequence_ack:
            frame = s.logout(n);
            break;
        case Emitter::bound_zero_ack:
            frame = s.logout(n + 3);
            break;
        case Emitter::sending_time_fatal:
            frame = s.order_with_stale_sending_time(n);
            break;
        case Emitter::arm_d:
            frame = poss_dup_order_with_later_orig(s, n);
            break;
        case Emitter::gap_deadline:
            frame = s.heartbeat(++deadline_seq);
            break;
        case Emitter::arm_b:
            frame = s.order(n - 1);
            break;
    }
    auto feed_fut = s.feed_spawn(frame);
    ASSERT_TRUE(s.pump_until([&] { return s.log->holding(); }))
        << "setup: the emitter's Logout store was not held: " << s.written(mark);
    auto close_fut = spawn_close(s, close_mode::graceful);
    if (c.emitter == Emitter::bound_zero_ack) {
        // With T = 0, close()'s grace is 0, so it does not wait.
        // FR-010 case 5.  claim-ok: a spec case, not a tally
        s.pump_for(std::chrono::milliseconds{20});
    } else {
        // close(graceful) waits inside its grace T for the Logout in flight (research D4.6,
        // the poll-join): while that Logout's store call is held it has neither returned nor
        // recorded Disconnected. The window is below the grace, so the grace cannot end the
        // wait inside it.
        EXPECT_FALSE(s.pump_until([&] { return rg::Scene::ready(close_fut); },
                                  std::chrono::milliseconds{kT} / 8))
            << "close() returned under the Logout in flight: " << s.written(mark);
        EXPECT_EQ(s.state(), fsm_state::Active)
            << "close() recorded Disconnected under the Logout in flight";
    }
    s.log->release_hold();
    bool const arms_a_wait =
        !c.with_mark && (c.emitter == Emitter::sending_time_fatal || c.emitter == Emitter::arm_d ||
                         c.emitter == Emitter::gap_deadline || c.emitter == Emitter::arm_b);
    if (arms_a_wait) {
        // 105-transport-liveness (R-2, contract C-2 W-13): the emitter's Logout is an R-2 ending,
        // so the session waits for the peer's Logout or the bound, logout_disconnect_timeout_ms
        // from the Logout's write start on the session's clock, and close(graceful) proceeds at
        // that end. The mock clock has not moved since the write started; close()'s grace is a
        // real-time timer of T, above every pump budget here.
        EXPECT_TRUE(s.pump_until([&] { return rg::Scene::ready(feed_fut); }, kAfterReleaseBudget))
            << "the delivery did not return";
        s.mock->advance(std::chrono::milliseconds{kT} - std::chrono::milliseconds{1});
        EXPECT_FALSE(
            s.pump_until([&] { return rg::Scene::ready(close_fut); }, kAfterReleaseBudget / 4))
            << "close() returned before the wait's bound: " << s.written(mark);
        s.mock->advance(std::chrono::milliseconds{1});
    }
    EXPECT_TRUE(
        s.pump_until([&] { return rg::Scene::ready(close_fut) && rg::Scene::ready(feed_fut); },
                     kAfterReleaseBudget))
        << "close() or the delivery did not return";
    EXPECT_EQ(s.count("5", mark), 1U) << s.written(mark);
    EXPECT_FALSE(session_test_access::logout_in_flight(*s.session));
    if (c.emitter == Emitter::bound_zero_ack) {
        EXPECT_EQ(s.count("2", mark), 0U)
            << "the T = 0 Logout drew a ResendRequest: " << s.written(mark);
    }
}

TEST_P(TooHighLogoutState103, Lo1Control_CloseGracefulWithNoLogoutInFlight_SendsPhase1s) {
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = kT}};
    ASSERT_TRUE(s.open_to_active());
    auto const mark = s.sent.size();
    auto close_fut = spawn_close(s, close_mode::graceful);
    ASSERT_TRUE(s.pump_until([&] { return rg::Scene::ready(close_fut); }, kCloseBudget));
    EXPECT_EQ(s.count("5", mark), 1U) << s.written(mark);
}

// R-588-e (1) (US3 AS7; tasks T081): with the mark set, a too-high ResendRequest starts a
// walk held at its read; the clock moves to T, so the bound waits at the replay gate; the
// hold is released. The acknowledgement follows the answer, exactly one Logout.
TEST_P(TooHighLogoutState103, R588e1_BoundAtTheGateBehindAHeldWalk_AckAfterTheAnswer) {
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = kT, .recording_store = true}};
    ASSERT_TRUE(active_recorded(s)) << "setup";
    seqnum_t const n = s.next_in();
    (void)s.feed(s.logout(n + 3));
    ASSERT_EQ(phase(s), kPending) << "setup: no pending mark";
    s.log->hold_in = [](StoreCall const& c) { return c.op == store_op::retrieve; };
    auto const mark = s.sent.size();
    auto rr_fut = s.feed_spawn(s.resend_request(n + 5));
    ASSERT_TRUE(s.pump_until([&] { return s.log->holding(); })) << "setup: the walk was not held";
    auto const& gate = session_test_access::replay_gate(*s.session);
    auto const waits = gate.timer_waits;
    s.mock->advance(std::chrono::milliseconds{kT});
    ASSERT_TRUE(s.pump_until([&] { return gate.timer_waits > waits; }))
        << "setup: the bound did not wait at the gate";
    s.log->release_hold();
    // Named, so the pump call carries no string literal for the site-label census to read.
    auto const answered_and_acknowledged = [&] {
        return rg::Scene::ready(rr_fut) && s.count("5", mark) != 0;
    };
    ASSERT_TRUE(s.pump_until(answered_and_acknowledged));
    s.pump_for(std::chrono::milliseconds{20});
    EXPECT_EQ(s.count("5", mark), 1U) << s.written(mark);
    std::size_t last_answer = 0;
    std::size_t ack = 0;
    for (std::size_t i = mark; i < s.sent.size(); ++i) {
        if (rg::field(s.sent[i], 43) == "Y" || rg::field(s.sent[i], 123) == "Y") last_answer = i;
        if (rg::field(s.sent[i], 35) == "5") ack = i;
    }
    EXPECT_GT(ack, last_answer) << "the acknowledgement precedes the answer: " << s.written(mark);
}

// R-588-e (2): close(graceful) while the bound waits at the gate: one Logout, phase 1's.
TEST_P(TooHighLogoutState103, R588e2_CloseGracefulWhileTheBoundWaitsAtTheGate_OneLogout) {
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = kT, .recording_store = true}};
    ASSERT_TRUE(active_recorded(s)) << "setup";
    seqnum_t const n = s.next_in();
    (void)s.feed(s.logout(n + 3));
    ASSERT_EQ(phase(s), kPending) << "setup: no pending mark";
    s.log->hold_in = [](StoreCall const& c) { return c.op == store_op::retrieve; };
    auto const mark = s.sent.size();
    auto rr_fut = s.feed_spawn(s.resend_request(n + 5));
    ASSERT_TRUE(s.pump_until([&] { return s.log->holding(); })) << "setup: the walk was not held";
    auto const& gate = session_test_access::replay_gate(*s.session);
    auto const waits = gate.timer_waits;
    s.mock->advance(std::chrono::milliseconds{kT});
    ASSERT_TRUE(s.pump_until([&] { return gate.timer_waits > waits; }))
        << "setup: the bound did not wait at the gate";
    auto close_fut = spawn_close(s, close_mode::graceful);
    s.pump_for(std::chrono::milliseconds{20});
    s.log->release_hold();
    EXPECT_TRUE(s.pump_until(
        [&] { return rg::Scene::ready(close_fut) && rg::Scene::ready(rr_fut); }, kCloseBudget));
    EXPECT_EQ(s.count("5", mark), 1U) << s.written(mark);
    // close()'s claim of the pending mark (research D4.6): the bound, which had not claimed
    // before its gate wait, left it `none`.
    EXPECT_EQ(phase(s), kNone) << "the mark was left set";
}

// R-588-e (3): the bound has claimed and its acknowledgement's store is held;
// close(graceful) is spawned; the hold is released. Exactly one Logout, the
// acknowledgement, and close() returns.
TEST_P(TooHighLogoutState103, R588e3_AckStoreHeldThenCloseGraceful_ExactlyOneLogout) {
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = kT, .recording_store = true}};
    ASSERT_TRUE(active_recorded(s)) << "setup";
    seqnum_t const n = s.next_in();
    (void)s.feed(s.logout(n + 3));
    ASSERT_EQ(phase(s), kPending) << "setup: no pending mark";
    hold_store_of(s, next_out(s));
    auto const mark = s.sent.size();
    // KIND F: the advance reaches the bound; a lost one fails the next ASSERT_TRUE, whose
    // pump is bounded, with "the acknowledgement's store was not held".
    s.mock->advance(std::chrono::milliseconds{kT});
    ASSERT_TRUE(s.pump_until([&] { return s.log->holding(); }))
        << "setup: the acknowledgement's store was not held";
    auto close_fut = spawn_close(s, close_mode::graceful);
    // close(graceful) waits inside its grace T for the acknowledgement in flight (research
    // D4.6, the poll-join): it has neither returned nor recorded Disconnected; the window is
    // below the grace.
    EXPECT_FALSE(s.pump_until([&] { return rg::Scene::ready(close_fut); },
                              std::chrono::milliseconds{kT} / 8))
        << "close() returned under the acknowledgement in flight: " << s.written(mark);
    EXPECT_EQ(s.state(), fsm_state::Active)
        << "close() recorded Disconnected under the acknowledgement in flight";
    s.log->release_hold();
    EXPECT_TRUE(s.pump_until([&] { return rg::Scene::ready(close_fut); }, kAfterReleaseBudget));
    EXPECT_EQ(s.count("5", mark), 1U) << s.written(mark);
}

// Route (b) at once (research D4.1, D4.5; FR-010): with T = 0, a too-high Logout while a
// resend is outstanding is acknowledged at once. The acknowledgement cannot take a number here
// (the next outbound number is at its maximum), so the frame returns that error, no Logout is
// written, and the acknowledgement's sequence_numbers_reset event is not emitted.
TEST_P(TooHighLogoutState103, RouteB_AtOnce_AcknowledgementCannotBeNumbered_ReturnsItsError) {
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = 0}};
    ASSERT_TRUE(s.open_to_active());
    seqnum_t const n = s.next_in();
    auto const gap_mark = s.sent.size();
    (void)s.feed(s.order(n + rg::kGapWidth));
    ASSERT_EQ(s.count("2", gap_mark), 1U) << "setup: no gap was opened: " << s.written(gap_mark);
    auto& mgr = session_test_access::seqnum_mgr(*s.session);
    fixpp::session::seqnum_manager_test_access::set_counters(mgr, mgr.next_inbound_unsafe(),
                                                             fixpp::session::seqnum_max);
    auto const resets = [&s] {
        int k = 0;
        for (auto const& ev : s.session->recent_events()) {
            auto const* e = std::get_if<fixpp::session::session_event_sequence_numbers_reset>(&ev);
            k += static_cast<int>(e != nullptr && !e->by_peer_request);
        }
        return k;
    };
    int const resets_before = resets();
    auto const mark = s.sent.size();
    auto const res = s.feed(s.logout(n + rg::kGapWidth + 1));
    ASSERT_FALSE(res.has_value()) << "the frame returned success";
    EXPECT_EQ(res.error(), fixpp::core::error::store_seqnum_overflow)
        << static_cast<int>(res.error());
    EXPECT_EQ(s.count("5", mark), 0U) << s.written(mark);
    EXPECT_EQ(s.count("2", mark), 0U) << "a second ResendRequest: " << s.written(mark);
    EXPECT_EQ(resets(), resets_before) << "the failed acknowledgement emitted its reset event";
    EXPECT_EQ(s.state(), fsm_state::Disconnected);
}

// The acknowledgement in flight (research D4.6's last row): while the bound acknowledges, its
// Logout held in its store, an in-sequence frame is drained, as the LogoutSent row drains:
// it returns at once and NextNumIn does not move.
TEST_P(TooHighLogoutState103, Acking_InSequenceFrameDrained_NextNumInUnchanged) {
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = kT, .recording_store = true}};
    ASSERT_TRUE(active_recorded(s)) << "setup";
    seqnum_t const n = s.next_in();
    (void)s.feed(s.logout(n + 3));
    ASSERT_EQ(phase(s), kPending) << "setup: no pending mark";
    hold_store_of(s, next_out(s));
    auto const mark = s.sent.size();
    // KIND F: the advance reaches the bound; a lost one fails the next ASSERT_TRUE, whose
    // pump is bounded, with "the acknowledgement's store was not held".
    s.mock->advance(std::chrono::milliseconds{kT});
    ASSERT_TRUE(s.pump_until([&] { return s.log->holding(); }))
        << "setup: the acknowledgement's store was not held";
    ASSERT_EQ(phase(s), 2) << "setup: the bound did not claim the mark";
    auto frame_fut = s.feed_spawn(s.heartbeat(n));
    EXPECT_TRUE(s.pump_until([&] { return rg::Scene::ready(frame_fut); },
                             std::chrono::milliseconds{kT} / 8))
        << "the frame was not drained while the bound acknowledges";
    EXPECT_EQ(s.next_in(), n) << "the frame advanced NextNumIn while the bound acknowledges";
    s.log->release_hold();
    EXPECT_TRUE(s.pump_until([&] { return bound_count(s) == 0; }, kAfterReleaseBudget));
    EXPECT_EQ(s.count("5", mark), 1U) << s.written(mark);
}

// A second too-high Logout while the mark is pending is ignored (research D4.2, as under
// B-537-1): the mark keeps the first Logout's number S, no second bound starts, and filling the
// gap past S, though not past the second Logout's number, draws the one acknowledgement.
TEST_P(TooHighLogoutState103, SecondTooHighLogoutWhilePending_IgnoredOneBoundAckPastTheFirstS) {
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = kT}};
    ASSERT_TRUE(s.open_to_active());
    seqnum_t const n = s.next_in();
    auto const mark = s.sent.size();
    (void)s.feed(s.logout(n + 3));
    ASSERT_EQ(phase(s), kPending) << "setup: no pending mark";
    ASSERT_EQ(bound_count(s), 1) << "setup: no bound";
    (void)s.feed(s.logout(n + 5));
    EXPECT_EQ(bound_count(s), 1) << "a second bound started";
    (void)s.feed(s.gap_fill(n, n + 4));
    EXPECT_EQ(s.count("5", mark), 1U)
        << "no acknowledgement once N passed the first S: " << s.written(mark);
    if (bound_count(s) != 0) {
        // A bound still running is ended at its own deadline, so the Session is not destroyed
        // with it and the cell ends as a test failure, not an abort.
        s.pass(std::chrono::milliseconds{kT});
    }
    EXPECT_EQ(bound_count(s), 0);
}

// ── The bound's exception exit (data-model "Pending-Logout mark", its exception rows) ──
//
// An exception that reaches the bound's handler ends it closed: no wake published, the mark
// `none`, the session Disconnected, no Logout, and the join counter 0. The wake is read
// directly, not through a sanitizer, which an allocator's recycling of the freed frame can
// keep silent.

namespace {

// After a cell's readings: a frame past S, Engine::stop()'s step and a close(terminal) run,
// and still no Logout is written.
void the_ending_runs_on(rg::Scene& s, seqnum_t const n, std::size_t const mark) {
    (void)s.feed(s.gap_fill(n, n + 4));
    session_test_access::note_engine_stop(*s.session);
    auto close_fut = spawn_close(s, close_mode::terminal);
    EXPECT_TRUE(s.pump_until([&] { return rg::Scene::ready(close_fut); }, kCloseBudget))
        << "close(terminal) did not complete";
    EXPECT_EQ(s.count("5", mark), 0U) << s.written(mark);
    EXPECT_EQ(bound_count(s), 0);
}

}  // namespace

// The exception between the wake's publish and the race: the before-race seam throws.
TEST_P(TooHighLogoutState103, BoundThrowsAfterItsPublish_NoWakeMarkNoneDisconnected) {
    bool throw_in_seam = true;
    bool seam_ran = false;
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = kT}};
    ASSERT_TRUE(s.open_to_active());
    session_test_access::set_peer_logout_bound_hold_before_race(
        *s.session, [&throw_in_seam, &seam_ran]() -> asio::awaitable<void> {
            seam_ran = true;
            if (throw_in_seam) throw std::runtime_error{"103 cell: the before-race seam throws"};
            co_return;
        });
    seqnum_t const n = s.next_in();
    auto const mark = s.sent.size();
    (void)s.feed(s.logout(n + 3));
    EXPECT_TRUE(s.pump_until([&] { return bound_count(s) == 0; })) << "the bound did not exit";
    EXPECT_TRUE(seam_ran) << "setup: the bound did not reach its before-race seam";
    bool const wake_left = session_test_access::peer_logout_wake_published(*s.session);
    EXPECT_FALSE(wake_left) << "the bound's exception exit left its wake published";
    if (wake_left) session_test_access::drop_peer_logout_wake(*s.session);
    EXPECT_EQ(phase(s), kNone) << "the bound's exception exit left the mark set";
    EXPECT_EQ(s.state(), fsm_state::Disconnected);
    EXPECT_EQ(s.count("5", mark), 0U) << s.written(mark);
    EXPECT_EQ(bound_count(s), 0);
    throw_in_seam = false;
    the_ending_runs_on(s, n, mark);
}

// The exception before the wake's publish: the first-run seam throws.
TEST_P(TooHighLogoutState103, BoundThrowsBeforeItsPublish_MarkNoneDisconnected) {
    bool throw_in_seam = true;
    bool seam_ran = false;
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = kT}};
    ASSERT_TRUE(s.open_to_active());
    session_test_access::set_peer_logout_bound_hold_first_run(
        *s.session, [&throw_in_seam, &seam_ran]() -> asio::awaitable<void> {
            seam_ran = true;
            if (throw_in_seam) throw std::runtime_error{"103 cell: the first-run seam throws"};
            co_return;
        });
    seqnum_t const n = s.next_in();
    auto const mark = s.sent.size();
    (void)s.feed(s.logout(n + 3));
    EXPECT_TRUE(s.pump_until([&] { return bound_count(s) == 0; })) << "the bound did not exit";
    EXPECT_TRUE(seam_ran) << "setup: the bound did not reach its first-run seam";
    bool const wake_left = session_test_access::peer_logout_wake_published(*s.session);
    EXPECT_FALSE(wake_left) << "a wake is published with no bound running";
    if (wake_left) session_test_access::drop_peer_logout_wake(*s.session);
    EXPECT_EQ(phase(s), kNone) << "the bound's exception exit left the mark set";
    EXPECT_EQ(s.state(), fsm_state::Disconnected);
    EXPECT_EQ(s.count("5", mark), 0U) << s.written(mark);
    EXPECT_EQ(bound_count(s), 0);
    throw_in_seam = false;
    the_ending_runs_on(s, n, mark);
}

// The exception after the claim: the acknowledgement's store call returns store_cancelled,
// so store_then_emit leaves by operation_aborted (its declaration in session.hpp), which
// reaches the bound's handler with the mark `acking`.
TEST_P(TooHighLogoutState103, AckStoreCancelled_MarkNoneDisconnectedNoLogout) {
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = kT, .recording_store = true}};
    ASSERT_TRUE(active_recorded(s)) << "setup";
    seqnum_t const n = s.next_in();
    (void)s.feed(s.logout(n + 3));
    ASSERT_EQ(phase(s), kPending) << "setup: no pending mark";
    seqnum_t const ack_seq = next_out(s);
    auto const is_ack_store = [ack_seq](StoreCall const& c) {
        return c.op == store_op::store && c.dir == fixpp::session::direction_t::outbound &&
               c.first == ack_seq;
    };
    s.log->fail_code = fixpp::core::error::store_cancelled;
    s.log->fail_in = is_ack_store;
    auto const mark = s.sent.size();
    auto const log_mark = s.log->mark();
    s.pass(std::chrono::milliseconds{kT});
    EXPECT_TRUE(s.pump_until([&] { return bound_count(s) == 0; })) << "the bound did not exit";
    auto const* call = s.log->first_after(log_mark, is_ack_store);
    ASSERT_NE(call, nullptr) << "setup: the acknowledgement's store was not called";
    ASSERT_TRUE(call->error.has_value()) << "setup: the acknowledgement's store did not fail";
    EXPECT_EQ(*call->error, fixpp::core::error::store_cancelled) << "setup";
    EXPECT_EQ(phase(s), kNone) << "the bound's exception exit left the mark `acking`";
    EXPECT_EQ(s.state(), fsm_state::Disconnected);
    EXPECT_FALSE(session_test_access::logout_in_flight(*s.session)) << "the Logout owner is set";
    EXPECT_FALSE(session_test_access::peer_logout_wake_published(*s.session));
    EXPECT_EQ(s.count("5", mark), 0U) << s.written(mark);
    EXPECT_EQ(bound_count(s), 0);
}

INSTANTIATE_TEST_SUITE_P(BothRoles, TooHighLogoutState103,
                         ::testing::Values(session_role::initiator, session_role::acceptor),
                         [](auto const& info) { return std::string{rg::role_name(info.param)}; });

// LO-2 (tasks T083), the reverse of LO-1: close(graceful)'s phase-1 Logout is held in its
// store; an inbound Logout emitter's frame is fed; the hold is released. Exactly one Logout,
// phase 1's; the inbound ending's Disconnected recorded; phase 1 takes its early exit, so
// close() returns with no wait for a confirming Logout (well inside the grace T). The
// at-once acknowledgement's frame runs with no clock, not with T = 0: phase 1's grace is T,
// and at T = 0 it ends phase 1 before its store call can be held.
enum class Lo2Frame : std::uint8_t {
    in_sequence_logout,
    sending_time_fatal,
    arm_d,
    no_clock,
    gap_deadline,
    too_low  // a frame below NextNumIn without 43=Y: Arm B (research D9)
};

class Lo2 : public ::testing::TestWithParam<std::tuple<session_role, Lo2Frame>> {};

TEST_P(Lo2, Phase1HeldThenAnInboundEnding_ExactlyOneLogoutPhase1s) {
    auto const [role, f] = GetParam();
    rg::Scene s{rg::Options{.role = role,
                            .with_clock = f != Lo2Frame::no_clock,
                            .logout_timeout_ms = kT,
                            .recording_store = true}};
    ASSERT_TRUE(active_recorded(s)) << "setup";
    seqnum_t const n = s.next_in();
    // The gap deadline's frame: a gap opened and kept open past its deadline, as LO-1's.
    std::uint32_t deadline_seq = 0;
    if (f == Lo2Frame::gap_deadline) {
        (void)s.feed(s.order(n + rg::kGapWidth));
        deadline_seq = n + rg::kGapWidth;
        std::size_t answered = 0;
        for (int i = 0; i < 4 && s.state() == fsm_state::Active; ++i) {
            s.pass(rg::kHeartBtInt / 2);
            if (s.count("1") > answered) {
                (void)s.feed(s.heartbeat(++deadline_seq, s.last_test_req_id()));
                ++answered;
            }
        }
        ASSERT_EQ(s.state(), fsm_state::Active) << "setup: " << s.written();
        s.pass(std::chrono::seconds{1});
    }
    hold_store_of(s, next_out(s));
    auto const mark = s.sent.size();
    auto close_fut = spawn_close(s, close_mode::graceful);
    ASSERT_TRUE(s.pump_until([&] { return s.log->holding(); }))
        << "setup: phase 1's Logout store was not held";
    std::vector<std::byte> frame;
    switch (f) {
        case Lo2Frame::in_sequence_logout:
            frame = s.logout(n);
            break;
        case Lo2Frame::sending_time_fatal:
            frame = s.order_with_stale_sending_time(n);
            break;
        case Lo2Frame::arm_d:
            frame = poss_dup_order_with_later_orig(s, n);
            break;
        case Lo2Frame::no_clock:
            frame = s.logout(n + 3);
            break;
        case Lo2Frame::gap_deadline:
            frame = s.heartbeat(++deadline_seq);
            break;
        case Lo2Frame::too_low:
            frame = s.order(n - 1);
            break;
    }
    auto feed_fut = s.feed_spawn(frame);
    s.pump_for(std::chrono::milliseconds{20});
    s.log->release_hold();
    // Phase 1's early exit: close() returns with no wait for a confirming Logout.
    EXPECT_TRUE(s.pump_until([&] { return rg::Scene::ready(close_fut); },
                             std::chrono::milliseconds{kT} / 4))
        << "close() waited for a confirming Logout";
    (void)s.pump_until([&] { return rg::Scene::ready(close_fut) && rg::Scene::ready(feed_fut); },
                       kCloseBudget);
    EXPECT_EQ(s.count("5", mark), 1U) << s.written(mark);
    if (f == Lo2Frame::no_clock) {
        // The inbound ending ran: the at-once acknowledgement, with no ResendRequest.
        EXPECT_EQ(s.count("2", mark), 0U) << s.written(mark);
    }
    EXPECT_EQ(s.state(), fsm_state::Disconnected);
}

std::string lo2_name(::testing::TestParamInfo<std::tuple<session_role, Lo2Frame>> const& info) {
    std::string e;
    switch (std::get<1>(info.param)) {
        case Lo2Frame::in_sequence_logout:
            e = "InSequenceLogout";
            break;
        case Lo2Frame::sending_time_fatal:
            e = "SendingTimeFatal";
            break;
        case Lo2Frame::arm_d:
            e = "ArmD";
            break;
        case Lo2Frame::no_clock:
            e = "TooHighLogoutNoClock";
            break;
        case Lo2Frame::gap_deadline:
            e = "GapDeadline";
            break;
        case Lo2Frame::too_low:
            e = "TooLowFrame";
            break;
    }
    return std::string{rg::role_name(std::get<0>(info.param))} + e;
}

INSTANTIATE_TEST_SUITE_P(
    BothRolesEveryFrame, Lo2,
    ::testing::Combine(::testing::Values(session_role::initiator, session_role::acceptor),
                       ::testing::Values(Lo2Frame::in_sequence_logout, Lo2Frame::sending_time_fatal,
                                         Lo2Frame::arm_d, Lo2Frame::no_clock,
                                         Lo2Frame::gap_deadline, Lo2Frame::too_low)),
    lo2_name);

// LO-3 (tasks T084; characterisation of L-103-6): close(graceful) while a Logout is in
// flight whose store outlasts the grace. No second Logout; the force-close at the grace.
// Its control is LO-1's in-sequence arm, the same hold released inside the grace. T is
// short here: the grace is a real-time timer of T.
TEST_P(TooHighLogoutState103, Lo3_InFlightLogoutOutlastsTheGrace_NoSecondLogout) {
    constexpr std::uint32_t kShortT = 200;
    rg::Scene s{
        rg::Options{.role = GetParam(), .logout_timeout_ms = kShortT, .recording_store = true}};
    ASSERT_TRUE(active_recorded(s)) << "setup";
    seqnum_t const n = s.next_in();
    hold_store_of(s, next_out(s));
    auto const mark = s.sent.size();
    auto feed_fut = s.feed_spawn(s.logout(n));
    ASSERT_TRUE(s.pump_until([&] { return s.log->holding(); })) << "setup: the ack was not held";
    auto close_fut = spawn_close(s, close_mode::graceful);
    // 105-transport-liveness (data-model E-3 rows 4 and 5c; contract C-2.7): the reply's write
    // limit, logout_disconnect_timeout_ms from its write start on the session's clock, bounds
    // close() too. The real-time grace force-closes the transport, close() then joins the
    // Logout's bound, and the bound ends at the limit while the store call is still held. With
    // one clock the limit comes first (the write started before close()); here the session's
    // clock is the mock, so close() returns when the mock reaches the limit.
    EXPECT_FALSE(s.pump_until([&] { return rg::Scene::ready(close_fut); },
                              4 * std::chrono::milliseconds{kShortT}))
        << "close() returned with the write limit not reached on the session's clock";
    s.mock->advance(std::chrono::milliseconds{kShortT} - std::chrono::milliseconds{1});
    EXPECT_FALSE(s.pump_until([&] { return rg::Scene::ready(close_fut); },
                              std::chrono::milliseconds{kShortT}))
        << "close() returned before the write limit";
    s.mock->advance(std::chrono::milliseconds{1});
    EXPECT_TRUE(s.pump_until([&] { return rg::Scene::ready(close_fut); },
                             4 * std::chrono::milliseconds{kShortT}))
        << "close() did not return at the write limit";
    s.log->release_hold();
    (void)s.pump_until([&] { return rg::Scene::ready(feed_fut); });
    EXPECT_LE(s.count("5", mark), 1U) << "a second Logout: " << s.written(mark);
}

// LO-4 (tasks T085): a pending mark; a SendingTime-fatal (twin: Arm D) frame's Logout held in
// its store; T expires; the hold is released. Exactly one Logout, the held one; the bound
// writes none.
class Lo4 : public ::testing::TestWithParam<std::tuple<session_role, bool>> {};

TEST_P(Lo4, MarkPendingFatalLogoutHeldTExpires_ExactlyOneLogout) {
    auto const [role, arm_d] = GetParam();
    rg::Scene s{rg::Options{.role = role, .logout_timeout_ms = kT, .recording_store = true}};
    ASSERT_TRUE(active_recorded(s)) << "setup";
    seqnum_t n = s.next_in();
    (void)s.feed(s.logout(n + 3));
    ASSERT_EQ(phase(s), kPending) << "setup: no pending mark";
    (void)s.feed(s.gap_fill(n, n + 3));
    n = s.next_in();
    hold_store_of(s, next_out(s) + 1U);  // after the Reject
    auto const mark = s.sent.size();
    auto feed_fut = s.feed_spawn(arm_d ? poss_dup_order_with_later_orig(s, n)
                                       : s.order_with_stale_sending_time(n));
    ASSERT_TRUE(s.pump_until([&] { return s.log->holding(); }))
        << "setup: the fatal Logout was not held";
    s.mock->advance(std::chrono::milliseconds{kT});
    s.pump_for(std::chrono::milliseconds{20});
    s.log->release_hold();
    EXPECT_TRUE(s.pump_until([&] { return rg::Scene::ready(feed_fut); }));
    s.pump_for(std::chrono::milliseconds{20});
    EXPECT_EQ(s.count("5", mark), 1U) << s.written(mark);
}

// LO-6 (tasks T087): a pending mark; a SendingTime-fatal (twin: Arm D) frame whose Reject's
// store is held; T expires, so the bound acknowledges and records Disconnected; the hold is
// released. Exactly one Logout, the bound's; Disconnected; the owner clear.
class Lo6 : public ::testing::TestWithParam<std::tuple<session_role, bool>> {};

TEST_P(Lo6, MarkPendingRejectHeldTExpires_ExactlyOneLogoutTheBounds) {
    auto const [role, arm_d] = GetParam();
    rg::Scene s{rg::Options{.role = role, .logout_timeout_ms = kT, .recording_store = true}};
    ASSERT_TRUE(active_recorded(s)) << "setup";
    seqnum_t n = s.next_in();
    (void)s.feed(s.logout(n + 3));
    ASSERT_EQ(phase(s), kPending) << "setup: no pending mark";
    (void)s.feed(s.gap_fill(n, n + 3));
    n = s.next_in();
    hold_store_of(s, next_out(s));  // the Reject
    auto const mark = s.sent.size();
    auto feed_fut = s.feed_spawn(arm_d ? poss_dup_order_with_later_orig(s, n)
                                       : s.order_with_stale_sending_time(n));
    ASSERT_TRUE(s.pump_until([&] { return s.log->holding(); })) << "setup: the Reject was not held";
    s.mock->advance(std::chrono::milliseconds{kT});
    s.pump_for(std::chrono::milliseconds{20});
    s.log->release_hold();
    EXPECT_TRUE(s.pump_until([&] { return rg::Scene::ready(feed_fut); }));
    s.pump_for(std::chrono::milliseconds{20});
    EXPECT_EQ(s.count("5", mark), 1U) << s.written(mark);
    EXPECT_EQ(s.state(), fsm_state::Disconnected);
    EXPECT_FALSE(session_test_access::logout_in_flight(*s.session));
}

std::string role_twin_name(::testing::TestParamInfo<std::tuple<session_role, bool>> const& i) {
    return std::string{rg::role_name(std::get<0>(i.param))} +
           (std::get<1>(i.param) ? "ArmD" : "SendingTimeFatal");
}
INSTANTIATE_TEST_SUITE_P(BothRolesBothFrames, Lo4,
                         ::testing::Combine(::testing::Values(session_role::initiator,
                                                              session_role::acceptor),
                                            ::testing::Bool()),
                         role_twin_name);
INSTANTIATE_TEST_SUITE_P(BothRolesBothFrames, Lo6,
                         ::testing::Combine(::testing::Values(session_role::initiator,
                                                              session_role::acceptor),
                                            ::testing::Bool()),
                         role_twin_name);

// LO-5 (tasks T086): an in-sequence peer Logout; its confirming Logout succeeds; fromAdmin
// spawns close(graceful); the inbound persist's store is held (twin: fromAdmin rejects the
// Logout and the best-effort Reject's store is held); the hold is released. Exactly one
// Logout; Disconnected; close() completes; the owner clear.
class CloseInFromAdminApp final : public rg::GapApp {
public:
    std::function<void()> on_logout;
    bool reject_logout = false;
    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
        const fixpp::session::SessionId& /*id*/) override {
        if (msg.msg_type() != "5") return {};
        if (on_logout) on_logout();
        if (reject_logout) return std::unexpected(fixpp::core::error::app_do_not_send);
        return {};
    }
};

class Lo5 : public ::testing::TestWithParam<std::tuple<session_role, bool>> {};

TEST_P(Lo5, CloseFromFromAdminWhileTheEndingIsHeld_ExactlyOneLogout) {
    auto const [role, reject] = GetParam();
    auto const app = std::make_shared<CloseInFromAdminApp>();
    rg::Scene s{rg::Options{.role = role,
                            .app = app,
                            .logout_timeout_ms = kT,
                            .recording_store = true,
                            .recording_store_persistent = true}};
    ASSERT_TRUE(active_recorded(s)) << "setup";
    seqnum_t const n = s.next_in();
    app->reject_logout = reject;
    std::future<fixpp::core::expected_t<void>> close_fut;
    app->on_logout = [&] { close_fut = spawn_close(s, close_mode::graceful); };
    if (reject) {
        hold_store_of(s, next_out(s) + 1U);  // the Reject, after the confirming Logout
    } else {
        s.log->hold_in = [](StoreCall const& c) {
            return c.op == store_op::next_seqnum && c.dir == fixpp::session::direction_t::inbound &&
                   c.increment;
        };
    }
    auto const mark = s.sent.size();
    auto feed_fut = s.feed_spawn(s.logout(n));
    ASSERT_TRUE(s.pump_until([&] { return s.log->holding(); }))
        << "setup: the ending's store call was not held: " << s.written(mark);
    ASSERT_TRUE(close_fut.valid()) << "setup: fromAdmin did not spawn close()";
    s.pump_for(std::chrono::milliseconds{20});
    s.log->release_hold();
    EXPECT_TRUE(
        s.pump_until([&] { return rg::Scene::ready(feed_fut) && rg::Scene::ready(close_fut); },
                     kAfterReleaseBudget))
        << "close() did not complete";
    EXPECT_EQ(s.count("5", mark), 1U) << s.written(mark);
    if (reject) {
        // The twin's ending: the confirming Logout, then the best-effort Reject.
        EXPECT_EQ(s.count("3", mark), 1U) << s.written(mark);
    }
    EXPECT_EQ(s.state(), fsm_state::Disconnected);
    EXPECT_FALSE(session_test_access::logout_in_flight(*s.session));
}

std::string role_reject_name(::testing::TestParamInfo<std::tuple<session_role, bool>> const& i) {
    return std::string{rg::role_name(std::get<0>(i.param))} +
           (std::get<1>(i.param) ? "RejectHeld" : "PersistHeld");
}
INSTANTIATE_TEST_SUITE_P(BothRolesBothHolds, Lo5,
                         ::testing::Combine(::testing::Values(session_role::initiator,
                                                              session_role::acceptor),
                                            ::testing::Bool()),
                         role_reject_name);

std::string lo1_name(::testing::TestParamInfo<std::tuple<session_role, Lo1Case>> const& info) {
    auto const& c = std::get<1>(info.param);
    std::string e;
    switch (c.emitter) {
        case Emitter::in_sequence_ack:
            e = "InSequenceAck";
            break;
        case Emitter::bound_zero_ack:
            e = "BoundZeroAck";
            break;
        case Emitter::sending_time_fatal:
            e = "SendingTimeFatal";
            break;
        case Emitter::arm_d:
            e = "ArmD";
            break;
        case Emitter::gap_deadline:
            e = "GapDeadline";
            break;
        case Emitter::arm_b:
            e = "ArmB";
            break;
    }
    return std::string{rg::role_name(std::get<0>(info.param))} + e +
           (c.with_mark ? "WithMark" : "NoMark");
}

INSTANTIATE_TEST_SUITE_P(
    BothRolesEveryEmitter, Lo1,
    ::testing::Combine(
        ::testing::Values(session_role::initiator, session_role::acceptor),
        ::testing::Values(
            Lo1Case{Emitter::in_sequence_ack, false}, Lo1Case{Emitter::in_sequence_ack, true},
            Lo1Case{Emitter::bound_zero_ack, false}, Lo1Case{Emitter::sending_time_fatal, false},
            Lo1Case{Emitter::sending_time_fatal, true}, Lo1Case{Emitter::arm_d, false},
            Lo1Case{Emitter::arm_d, true}, Lo1Case{Emitter::gap_deadline, false},
            Lo1Case{Emitter::arm_b, false}, Lo1Case{Emitter::arm_b, true})),
    lo1_name);

// ── The crossing (spec FR-021's crossing clause; FIX-SL §4.6, §4.10.2) ──────────────────────────
// A peer Logout arrives while the session is itself ending: (owner) close(graceful)'s phase-1
// Logout is in its store call; (flush) close(graceful) has begun and waits in its store flush,
// before phase 1 takes the Logout owner; (stop) Engine::stop()'s note has run. The in-sequence
// Logout is the companion arm. Each ends Disconnected inside a quarter of the real-time grace T:
// no ResendRequest after the mark, no mark left set, no bound started, one Logout of ours; a
// too-high Logout leaves NextNumIn where it was, an in-sequence one moves it by one.
enum class CrossState : std::uint8_t { owner, flush, stop };

class Crossing103
    : public ::testing::TestWithParam<std::tuple<session_role, CrossState, bool /*too_high*/>> {};

TEST_P(Crossing103, PeerLogoutWhileTheSessionEnds_DisconnectedAtOnceNoResendRequest) {
    auto const [role, st, too_high] = GetParam();
    rg::Scene s{rg::Options{.role = role, .logout_timeout_ms = kT, .recording_store = true}};
    ASSERT_TRUE(active_recorded(s)) << "setup";
    bool bound_ran = false;
    session_test_access::set_peer_logout_bound_hold_first_run(
        *s.session, [&bound_ran]() -> asio::awaitable<void> {
            bound_ran = true;
            co_return;
        });
    seqnum_t const n = s.next_in();
    auto const mark = s.sent.size();
    std::future<fixpp::core::expected_t<void>> close_fut;
    switch (st) {
        case CrossState::owner:
            hold_store_of(s, next_out(s));
            close_fut = spawn_close(s, close_mode::graceful);
            ASSERT_TRUE(s.pump_until([&] { return s.log->holding(); }))
                << "setup: phase 1's Logout store was not held";
            ASSERT_TRUE(session_test_access::logout_in_flight(*s.session)) << "setup: no owner";
            break;
        case CrossState::flush:
            s.log->hold_flush = true;
            close_fut = spawn_close(s, close_mode::graceful);
            ASSERT_TRUE(s.pump_until([&] { return s.log->flush_holding(); }))
                << "setup: close()'s flush was not held";
            ASSERT_FALSE(session_test_access::logout_in_flight(*s.session))
                << "setup: phase 1 took the owner";
            break;
        case CrossState::stop:
            session_test_access::note_engine_stop(*s.session);
            break;
    }
    ASSERT_EQ(s.state(), fsm_state::Active) << "setup";
    auto feed_fut = s.feed_spawn(s.logout(too_high ? n + 3 : n));
    // Derived from the competing real-time grace T, which also ends a close.
    constexpr auto kBudget = std::chrono::milliseconds{kT} / 4;
    bool const disconnected =
        s.pump_until([&] { return s.state() == fsm_state::Disconnected; }, kBudget);
    int const phase_then = phase(s);
    seqnum_t const next_in_then = s.next_in();
    if (st == CrossState::owner) s.log->release_hold();
    if (st == CrossState::flush) s.log->release_flush();
    bool const ended = s.pump_until(
        [&] {
            return rg::Scene::ready(feed_fut) &&
                   (!close_fut.valid() || rg::Scene::ready(close_fut));
        },
        kBudget);
    EXPECT_TRUE(disconnected) << "not Disconnected within T/4: " << s.written(mark);
    EXPECT_TRUE(ended) << "the frame or the close did not end within T/4 of the release";
    EXPECT_EQ(phase_then, kNone) << "a mark was left set";
    EXPECT_FALSE(bound_ran) << "a bound was started";
    EXPECT_EQ(next_in_then, too_high ? n : n + 1) << "NextNumIn";
    EXPECT_EQ(s.count("2", mark), 0U) << "a ResendRequest during the ending: " << s.written(mark);
    EXPECT_EQ(s.count("5", mark), 1U) << "our Logouts: " << s.written(mark);
    // Teardown on a failure path: a close still waiting is ended by its grace, and a bound still
    // running at its own deadline, so the cell ends as a test failure, not an abort.
    if (close_fut.valid()) {
        (void)s.pump_until([&] { return rg::Scene::ready(close_fut); },
                           2 * std::chrono::milliseconds{kT});
    }
    if (bound_count(s) != 0) {
        s.mock->advance(std::chrono::milliseconds{kT});
        (void)s.pump_until([&] { return bound_count(s) == 0; });
    }
}

std::string crossing_name(
    ::testing::TestParamInfo<std::tuple<session_role, CrossState, bool>> const& info) {
    auto const [role, st, too_high] = info.param;
    std::string state;
    switch (st) {
        case CrossState::owner:
            state = "PhaseOneLogoutInItsStore";
            break;
        case CrossState::flush:
            state = "CloseInItsFlush";
            break;
        case CrossState::stop:
            state = "EngineStopNoted";
            break;
    }
    return std::string{rg::role_name(role)} + state + (too_high ? "TooHigh" : "InSequence");
}

INSTANTIATE_TEST_SUITE_P(
    BothRolesEveryState, Crossing103,
    ::testing::Combine(::testing::Values(session_role::initiator, session_role::acceptor),
                       ::testing::Values(CrossState::owner, CrossState::flush, CrossState::stop),
                       ::testing::Bool()),
    crossing_name);
