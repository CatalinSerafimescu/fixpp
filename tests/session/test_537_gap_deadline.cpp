// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_537_gap_deadline.cpp
//
// fixpp#537, D-537-5 and D-537-9 (.specify/536-537-538-recovery-liveness.md §4.3, §4.6,
// §4.7 and the #537 table of §5): an unfilled gap has a deadline. When a frame above
// NextNumIn arrives while a resend is outstanding, and NextNumIn has not advanced for
// twice HeartBtInt, the session sends a Logout whose Text(58) names the unanswered resend
// and records Disconnected. The check runs where such a frame leaves the inbound arm
// without ending the session, and nowhere else: no timer is added.
//
// E is the instant a cell's gap opened. The bound is E + kGapBound unless NextNumIn
// advanced since.
//
// Every cell that moves the clock sends the peer's frames less than one HeartBtInt apart,
// so the liveness loop sends no TestRequest, and asserts that none was written. Three
// things can end such a session, and the cells tell them apart: the deadline's Logout
// carries a Text(58); the SendingTime guard's Logout carries none and follows a Reject;
// the liveness loop's ending writes nothing.
//
// The cells are on a directly driven Session (resend_gap_scene.hpp), except those that
// assert that the transport is still open after the Logout, which need a transport whose
// state they can read: those run an Engine over loopback TCP (plain_engine_rig.hpp).

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "session/plain_engine_rig.hpp"
#include "session/read_first_frame_bounded.hpp"
#include "session/resend_gap_scene.hpp"
#include "support/pump_until_ready.hpp"
#include "support/session_test_access.hpp"
#include "support/validation_test_dictionary.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {

namespace {

using fixpp::core::error;
using fixpp::test_support::resend_gap::deadline_logout_written;
using fixpp::test_support::resend_gap::field;
using fixpp::test_support::resend_gap::Gap;
using fixpp::test_support::resend_gap::kGapBound;
using fixpp::test_support::resend_gap::kGapWidth;
using fixpp::test_support::resend_gap::kHeartBtInt;
using fixpp::test_support::resend_gap::LogoutThrowingApp;
using fixpp::test_support::resend_gap::open_gap;
using fixpp::test_support::resend_gap::Options;
using fixpp::test_support::resend_gap::ResetRejectingApp;
using fixpp::test_support::resend_gap::role_name;
using fixpp::test_support::resend_gap::Scene;
using fixpp::test_support::resend_gap::state_name;
using orig = Scene::orig_time;

// The spacing of a peer's frames: inside every HeartBtInt.
constexpr auto kStep = 15s;
static_assert(kStep < kHeartBtInt);
static_assert(kGapBound == 2 * kHeartBtInt);

[[nodiscard]] std::string seconds_text(std::chrono::nanoseconds t) {
    return std::to_string(std::chrono::duration<double>(t).count()) + " s";
}

// What holds after every frame of a cell that comes before the session's ending: Active,
// and neither a Logout nor a TestRequest written since the gap opened.
[[nodiscard]] ::testing::AssertionResult still_waiting(Scene const& s, Gap const& g,
                                                       std::string const& at) {
    if (s.state() == fsm_state::Active && s.count("5", g.mark) == 0U &&
        s.count("1", g.mark) == 0U) {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure() << at << ": the session is " << state_name(s.state())
                                         << "; since the gap opened it wrote " << s.written(g.mark);
}

// The peer's frames before the bound: one at E + kStep, E + 2 kStep and E + 3 kStep, and
// one a millisecond before E + kGapBound. `make(i)` builds the i-th. After each, the
// session is still waiting, and `each(i, mark, at)` makes the cell's own assertions on
// what the frame drew: `mark` is the size of `sent` when the frame was delivered.
template <class Make, class Each>
[[nodiscard]] ::testing::AssertionResult stream_before_the_bound(Scene& s, Gap const& g, Make make,
                                                                 Each each) {
    const std::array<std::chrono::nanoseconds, 4> steps{kStep, kStep, kStep,
                                                        kGapBound - 3 * kStep - 1ms};
    std::chrono::nanoseconds t{};
    for (std::size_t i = 0; i < steps.size(); ++i) {
        s.pass(steps[i]);
        t += steps[i];
        const auto mark = s.sent.size();
        const auto r = s.feed(make(i));
        const std::string at = "the frame at E + " + seconds_text(t);
        if (!r) {
            return ::testing::AssertionFailure()
                   << at << ": on_inbound_frame returned error " << static_cast<int>(r.error());
        }
        if (auto waiting = still_waiting(s, g, at); !waiting) return waiting;
        each(i, mark, at);
    }
    return ::testing::AssertionSuccess();
}

// The frames of the stream above, without assertions of the cell's own.
template <class Make>
[[nodiscard]] ::testing::AssertionResult stream_before_the_bound(Scene& s, Gap const& g,
                                                                 Make make) {
    return stream_before_the_bound(s, g, make, [](std::size_t, std::size_t, std::string const&) {});
}

// Carries the session past the bound with no frame above NextNumIn: a Heartbeat below
// NextNumIn at each kStep from E, the last at E + kGapBound + kStep. One of them is at
// E + kGapBound itself. After each, the session is still waiting.
[[nodiscard]] ::testing::AssertionResult too_low_heartbeats_past_the_bound(Scene& s, Gap const& g) {
    const int steps = static_cast<int>(kGapBound / kStep) + 1;
    for (int i = 1; i <= steps; ++i) {
        s.pass(kStep);
        const auto r = s.feed(s.heartbeat(1));
        const std::string at = "the Heartbeat below NextNumIn at E + " + seconds_text(i * kStep);
        if (!r) {
            return ::testing::AssertionFailure()
                   << at << ": on_inbound_frame returned error " << static_cast<int>(r.error());
        }
        if (auto waiting = still_waiting(s, g, at); !waiting) return waiting;
    }
    if (!s.resend_outstanding() || s.next_in() != g.n) {
        return ::testing::AssertionFailure()
               << "past the bound the resend is no longer outstanding, or NextNumIn moved";
    }
    return ::testing::AssertionSuccess();
}

// ── On an Engine: the cells that assert the open transport ───────────────────────────

namespace plain_rig = fixpp::test_support::plain_rig;

template <class Pred>
[[nodiscard]] bool wait_on(plain_rig::Rig& rig, Pred pred) {
    return fixpp::test_support::pump_until(rig.ioc, std::move(pred),
                                           fixpp::test_support::kPumpBudget,
                                           fixpp::test_support::kPumpSlice, nullptr);
}

[[nodiscard]] seqnum_t next_in(plain_rig::Rig const& rig) {
    const auto s = rig.session();
    return s ? session_test_access::seqnum_mgr(*s).next_inbound_unsafe() : 0;
}

// Waits until a frame sent after the clock last moved has reached the session's inbound
// liveness writer. The writer stamps the clock's instant, which no earlier frame carries.
[[nodiscard]] bool frame_reached_the_session(plain_rig::Rig& rig) {
    return wait_on(rig, [&] {
        const auto s = rig.session();
        return s && session_test_access::last_inbound_steady(*s) == rig.clock->steady_now();
    });
}

// What a cell on the rig read. Its assertions are made after Rig::stop(): a fatal failure
// while the Engine runs would end the test in ~Engine.
struct SiteRun {
    std::string setup_miss;
    // Not empty when a frame before the bound did not leave the session waiting.
    std::string before_the_bound;
    fsm_state state_after_the_bound_frame = fsm_state::NotConnected;
    // Read by the peer since the gap opened, after the frame at the bound.
    std::vector<std::string> logouts;
    std::size_t test_requests = 0;
    // Rejects the peer read for the frame at the bound.
    std::size_t rejects_for_the_bound_frame = 0;
    // The open transport, read after the Logout: whether the peer's read had ended, and
    // where the reading exists, whether the peer saw a hang-up.
    bool peer_read_ended = false;
    bool peer_saw_hang_up = false;
    // The same reading after Engine::stop(), which closes the transport.
    bool peer_read_ended_after_stop = false;
};

// The three frames before the bound on the rig: E + 20 s, E + 40 s, and a millisecond
// before E + kGapBound.
constexpr std::array<std::chrono::nanoseconds, 3> kRigStepsBeforeTheBound{20s, 20s,
                                                                          kGapBound - 40s - 1ms};

void read_the_ending(plain_rig::Rig& rig, std::size_t mark, SiteRun& o) {
    o.state_after_the_bound_frame = rig.state();
    if (o.state_after_the_bound_frame == fsm_state::Disconnected) {
        (void)wait_on(rig, [&] {
            return !plain_rig::frames_of_type(rig.peer.received.substr(mark), "5").empty();
        });
    }
    rig.settle();
    o.logouts = plain_rig::frames_of_type(rig.peer.received.substr(mark), "5");
    o.test_requests = plain_rig::frames_of_type(rig.peer.received.substr(mark), "1").size();
    o.peer_read_ended = rig.peer.read_ended;
#if defined(FIXPP_TEST_PEER_HANG_UP_READING)
    o.peer_saw_hang_up = plain_rig::peer_sees_hang_up(rig.peer);
#endif
}

void stop_and_read(plain_rig::Rig& rig, SiteRun& o) {
    rig.stop();
    o.peer_read_ended_after_stop = wait_on(rig, [&] { return rig.peer.read_ended; });
}

// A NextNumIn whose digits no fixed phrase would hold by accident.
constexpr seqnum_t kDistinctNextNumIn = 7341;

// D-537-5, the out-of-sequence site. The peer raises NextNumIn to kDistinctNextNumIn,
// opens a gap, and then sends application messages above NextNumIn.
SiteRun run_out_of_sequence_site(session_role role) {
    SiteRun o;
    plain_rig::Rig rig;
    auto cfg = rig.cfg(role);
    if (cfg.heartbeat_interval != kHeartBtInt) {
        o.setup_miss = "the rig's HeartBtInt is not the one the bound is spelled from";
        return o;
    }
    [&] {
        if (!rig.start(cfg) || !rig.to_active()) {
            o.setup_miss = "the session did not reach Active";
            return;
        }
        rig.peer.send(rig.msg("4", 2, "36=" + std::to_string(kDistinctNextNumIn) + "\x01"));
        if (!wait_on(rig, [&] { return next_in(rig) == kDistinctNextNumIn; })) {
            o.setup_miss = "the Reset-mode SequenceReset did not raise NextNumIn";
            return;
        }
        const auto mark = rig.peer.received.size();
        std::uint32_t seq = kDistinctNextNumIn + 5;
        rig.peer.send(rig.msg("D", seq, Scene::order_fields(seq)));
        if (!wait_on(rig, [&] {
                return plain_rig::frames_of_type(rig.peer.received.substr(mark), "2").size() == 1U;
            })) {
            o.setup_miss = "the gap drew no ResendRequest";
            return;
        }
        // E is the clock's present instant: nothing has moved it since the rig started.
        std::chrono::nanoseconds t{};
        for (const auto step : kRigStepsBeforeTheBound) {
            // Staging (ci/mock-clock-staging-sweep.sh): KIND A. What reads this advance is
            // the frame sent below: the builder stamps its SendingTime(52) from the clock
            // on this thread, and the session reads the clock when it processes the frame,
            // which frame_reached_the_session() waits for. KIND D for the liveness loop:
            // its sleeps are armed to instants computed from the last inbound and outbound
            // instants, which were stamped before this advance.
            rig.clock->advance(step);
            t += step;
            ++seq;
            rig.peer.send(rig.msg("D", seq, Scene::order_fields(seq)));
            const std::string at = "the frame at E + " + seconds_text(t);
            if (!frame_reached_the_session(rig)) {
                o.before_the_bound = at + " did not reach the session";
                return;
            }
            rig.settle();
            const auto logouts =
                plain_rig::frames_of_type(rig.peer.received.substr(mark), "5").size();
            if (rig.state() != fsm_state::Active || logouts != 0U) {
                o.before_the_bound = at + ": the session is " +
                                     std::string{state_name(rig.state())} + " and the peer read " +
                                     std::to_string(logouts) + " Logout(s)";
                return;
            }
        }
        // Staging: KIND A. The millisecond that puts the clock at E + kGapBound is read by
        // the frame sent below, as above. No sleep of the liveness loop is due at it: the
        // frame before this one moved the inbound anchor, and the outbound one is a
        // Heartbeat the loop wrote less than one HeartBtInt ago.
        rig.clock->advance(1ms);
        ++seq;
        rig.peer.send(rig.msg("D", seq, Scene::order_fields(seq)));
        (void)wait_on(rig, [&] { return rig.state() == fsm_state::Disconnected; });
        read_the_ending(rig, mark, o);
    }();
    stop_and_read(rig, o);
    return o;
}

// D-537-5, cell V. The validate gate is on. The peer opens a gap with a NewOrderSingle
// that passes the gate, and then sends NewOrderSingles above NextNumIn that the gate
// Rejects.
SiteRun run_validate_gate_site() {
    SiteRun o;
    plain_rig::Rig rig;
    auto cfg = rig.cfg(session_role::acceptor);
    cfg.dictionary = fixpp::test_support::make_validation_test_dictionary();
    cfg.validate_inbound_messages = true;
    if (cfg.heartbeat_interval != kHeartBtInt) {
        o.setup_miss = "the rig's HeartBtInt is not the one the bound is spelled from";
        return o;
    }
    [&] {
        if (!rig.start(cfg) || !rig.to_active()) {
            o.setup_miss = "the session did not reach Active";
            return;
        }
        const auto mark = rig.peer.received.size();
        const auto read = [&](std::string_view msg_type) {
            return plain_rig::frames_of_type(rig.peer.received.substr(mark), msg_type).size();
        };
        std::uint32_t seq = next_in(rig) + 5;
        rig.peer.send(rig.msg("D", seq, Scene::order_fields(seq)));
        if (!wait_on(rig, [&] { return read("2") == 1U; })) {
            o.setup_miss = "the gap drew no ResendRequest: the opening frame did not pass the gate";
            return;
        }
        std::size_t rejects = 0;
        std::chrono::nanoseconds t{};
        for (const auto step : kRigStepsBeforeTheBound) {
            // Staging (ci/mock-clock-staging-sweep.sh): KIND A. The frame sent below is
            // stamped from the clock on this thread, and the session reads the clock when
            // it processes it; the Reject it draws is what the wait below observes.
            // KIND D for the liveness loop: its sleeps are armed to instants computed
            // from anchors that the frame before this advance, and its Reject, stamped.
            rig.clock->advance(step);
            t += step;
            ++seq;
            ++rejects;
            rig.peer.send(rig.msg("D", seq, fixpp::test_support::resend_gap::kSideAndTime));
            const std::string at = "the frame at E + " + seconds_text(t);
            if (!wait_on(rig, [&] { return read("3") == rejects; })) {
                o.before_the_bound = at + " drew no Reject";
                return;
            }
            rig.settle();
            if (rig.state() != fsm_state::Active || read("5") != 0U || read("3") != rejects) {
                o.before_the_bound = at + ": the session is " +
                                     std::string{state_name(rig.state())} + ", and the peer read " +
                                     std::to_string(read("5")) + " Logout(s) and " +
                                     std::to_string(read("3")) + " Reject(s)";
                return;
            }
        }
        // Staging: KIND A. The millisecond that puts the clock at E + kGapBound is read by
        // the frame sent below. No sleep of the liveness loop is due at it: the Reject
        // drawn before this advance moved the outbound anchor, and its frame the inbound.
        rig.clock->advance(1ms);
        ++seq;
        rig.peer.send(rig.msg("D", seq, fixpp::test_support::resend_gap::kSideAndTime));
        // The frame's effect is one of two: the session ends, or the gate Rejects once more.
        (void)wait_on(
            rig, [&] { return rig.state() == fsm_state::Disconnected || read("3") > rejects; });
        read_the_ending(rig, mark, o);
        o.rejects_for_the_bound_frame = read("3") - rejects;
    }();
    stop_and_read(rig, o);
    return o;
}

// The assertions both rig cells share. `text_must_hold` is what Text(58) must contain, or
// empty.
void expect_logout_and_open_transport(SiteRun const& o, std::string const& text_must_hold) {
    ASSERT_TRUE(o.setup_miss.empty()) << o.setup_miss;
    EXPECT_TRUE(o.peer_read_ended_after_stop)
        << "the reading cannot report: the peer's read did not end after Engine::stop()";
    ASSERT_TRUE(o.before_the_bound.empty()) << o.before_the_bound;

    ASSERT_EQ(o.logouts.size(), 1U)
        << "the frame at the bound drew no Logout: the session is "
        << state_name(o.state_after_the_bound_frame) << ", and the peer read "
        << o.rejects_for_the_bound_frame << " Reject(s) for that frame";
    const std::string text = field(o.logouts.front(), 58);
    EXPECT_FALSE(text.empty()) << "the Logout at the bound carries no Text(58)";
    if (!text_must_hold.empty()) {
        EXPECT_NE(text.find(text_must_hold), std::string::npos)
            << "Text(58) does not hold the expected MsgSeqNum " << text_must_hold << ": \"" << text
            << "\"";
    }
    EXPECT_EQ(o.state_after_the_bound_frame, fsm_state::Disconnected);
    EXPECT_EQ(o.test_requests, 0U) << "the liveness loop sent a TestRequest during the cell";

    // fixpp#534: the transport stays open after the Logout, until the peer closes it or
    // close() or Engine::stop() runs. That issue's fix flips this assertion.
    EXPECT_FALSE(o.peer_read_ended || o.peer_saw_hang_up)
        << "the transport was closed after the deadline's Logout (read ended: " << o.peer_read_ended
        << ", hang-up seen: " << o.peer_saw_hang_up << ")";
}

class GapDeadlineOutOfSequenceSite : public ::testing::TestWithParam<session_role> {};

}  // namespace

// ── D-537-5, the out-of-sequence site ───────────────────────────────────────────────

// A frame above NextNumIn before the bound is ignored, a millisecond before it included;
// the one at the bound draws the Logout, whose Text(58) holds the expected MsgSeqNum.
TEST_P(GapDeadlineOutOfSequenceSite, FrameAtTheBoundDrawsTheLogoutAndTheTransportStaysOpen) {
    const auto o = run_out_of_sequence_site(GetParam());
    ASSERT_NO_FATAL_FAILURE(
        expect_logout_and_open_transport(o, std::to_string(kDistinctNextNumIn)));
    // The one cell that pins the phrase. It is spelled here and not read from the session:
    // the number is kDistinctNextNumIn in digits.
    EXPECT_EQ(field(o.logouts.front(), 58), "ResendRequest unanswered, expecting MsgSeqNum 7341");
    EXPECT_EQ(o.rejects_for_the_bound_frame, 0U);
}

INSTANTIATE_TEST_SUITE_P(BothRoles, GapDeadlineOutOfSequenceSite,
                         ::testing::Values(session_role::initiator, session_role::acceptor),
                         [](const ::testing::TestParamInfo<session_role>& i) {
                             return std::string{role_name(i.param)};
                         });

// ── D-537-5, V: the validate gate's site ────────────────────────────────────────────

// Before the bound each dictionary-invalid frame above NextNumIn draws a Reject. The
// first at the bound draws the Logout and no Reject.
TEST(GapDeadlineValidateGateSite, V_InvalidFrameAtTheBoundDrawsTheLogoutAndNoReject) {
    const auto o = run_validate_gate_site();
    expect_logout_and_open_transport(o, {});
    EXPECT_EQ(o.rejects_for_the_bound_frame, 0U)
        << "the frame at the bound drew a Reject as well as the Logout";
}

// V2, the consequence D-537-5 states: the validate gate is tested ahead of the CompID
// guard, so a dictionary-invalid frame above NextNumIn from another CompID draws the
// gate's Reject before the bound and the deadline's Logout at it. A frame that passes
// validation meets the guard first (cell G1).
TEST(GapDeadlineValidateGateSite, V2_InvalidFrameFromAnotherCompIdDrawsTheLogoutAtTheBound) {
    Scene s{Options{.validate = true}};
    const Gap g = open_gap(s);
    ASSERT_NE(g.n, 0U);
    std::uint32_t seq = g.n + kGapWidth;
    const auto make = [&](std::size_t) { return s.invalid_order_from_other_comp_id(++seq); };
    ASSERT_TRUE(stream_before_the_bound(
        s, g, make, [&](std::size_t, std::size_t mark, std::string const& at) {
            EXPECT_EQ(s.count("3", mark), 1U) << at << " drew " << s.written(mark);
        }));

    s.pass(1ms);
    const auto mark = s.sent.size();
    const auto r = s.feed(make(0));
    ASSERT_TRUE(deadline_logout_written(s, mark, r));
    EXPECT_EQ(s.count("3", mark), 0U) << "the frame at the bound drew a Reject as well";
}

// ── D-537-5, G1: the guards keep their dispositions ─────────────────────────────────
//
// Guard. The traffic that carries the session past the bound is a Heartbeat below
// NextNumIn inside every HeartBtInt: it keeps liveness and is not above NextNumIn. A
// frame above NextNumIn cannot be that traffic, because the first one at or after the
// bound draws the deadline's Logout before the frame under test arrives.

TEST(GapDeadlineGuards, G1_WrongCompIdAfterTheBound_EndsSilently) {
    Scene s{Options{.validate = true}};
    const Gap g = open_gap(s);
    ASSERT_NE(g.n, 0U);
    ASSERT_TRUE(too_low_heartbeats_past_the_bound(s, g));

    const auto mark = s.sent.size();
    const auto r = s.feed(s.order_from_other_comp_id(g.n + kGapWidth + 1));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(s.state(), fsm_state::Disconnected);
    EXPECT_EQ(s.sent.size(), mark)
        << "the CompID guard's ending is silent; the session wrote " << s.written(mark);
}

TEST(GapDeadlineGuards, G1_SendingTimeOutsideTheThresholdAfterTheBound_RejectThenLogout) {
    Scene s{Options{.validate = true}};
    const Gap g = open_gap(s);
    ASSERT_NE(g.n, 0U);
    ASSERT_TRUE(too_low_heartbeats_past_the_bound(s, g));

    const auto mark = s.sent.size();
    const auto r = s.feed(s.order_with_stale_sending_time(g.n + kGapWidth + 1));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(s.state(), fsm_state::Disconnected);
    ASSERT_EQ(s.sent.size(), mark + 2U)
        << "the SendingTime guard writes a Reject and a Logout; the session wrote "
        << s.written(mark);
    EXPECT_EQ(field(s.sent[mark], 35), "3");
    EXPECT_EQ(field(s.sent[mark], 371), "52") << "RefTagID(371) of the guard's Reject";
    EXPECT_EQ(field(s.sent[mark], 373), "10") << "SessionRejectReason(373) of the guard's Reject";
    EXPECT_EQ(field(s.sent[mark + 1], 35), "5");
    EXPECT_TRUE(field(s.sent[mark + 1], 58).empty())
        << "the guard's Logout carries a Text(58), as the deadline's does: \""
        << field(s.sent[mark + 1], 58) << "\"";
}

// ── D-537-5, P1 and P2: PossDup Stage 1's two sites ─────────────────────────────────

namespace {

// Inside every HeartBtInt the peer sends a PossDup application message above NextNumIn
// whose OrigSendingTime(122) is `orig_122`. Before the bound each draws a Reject naming
// tag 122; the first at the bound draws the Logout and no Reject.
void run_poss_dup_stage1_site(orig orig_122) {
    Scene s;
    const Gap g = open_gap(s);
    ASSERT_NE(g.n, 0U);
    std::uint32_t seq = g.n + kGapWidth;
    const auto make = [&](std::size_t) { return s.poss_dup_order(++seq, orig_122); };
    ASSERT_TRUE(stream_before_the_bound(
        s, g, make, [&](std::size_t, std::size_t mark, std::string const& at) {
            const auto rejects = s.of_type("3", mark);
            ASSERT_EQ(rejects.size(), 1U) << at << " drew " << s.written(mark);
            EXPECT_EQ(field(rejects.front(), 371), "122") << at << ": RefTagID(371)";
        }));
    ASSERT_EQ(s.next_in(), g.n) << "a Rejected frame above NextNumIn was consumed";

    s.pass(1ms);
    const auto mark = s.sent.size();
    const auto r = s.feed(make(0));
    ASSERT_TRUE(deadline_logout_written(s, mark, r));
    EXPECT_EQ(s.count("3", mark), 0U) << "the frame at the bound drew a Reject as well";
}

}  // namespace

TEST(GapDeadlinePossDupStage1, P1_NoOrigSendingTimeAtTheBound_LogoutAndNoReject) {
    run_poss_dup_stage1_site(orig::absent);
}

TEST(GapDeadlinePossDupStage1, P2_UnparseableOrigSendingTimeAtTheBound_LogoutAndNoReject) {
    run_poss_dup_stage1_site(orig::unparseable);
}

// ── D-537-5, S1, S2 and S3: the Reset-mode arm ──────────────────────────────────────

// S1: the application's fromAdmin rejects every SequenceReset. Each Reset-mode
// SequenceReset above NextNumIn draws a Reject and is not applied. At the bound the
// Reject goes out first and the Logout follows.
TEST(GapDeadlineResetMode, S1_ResetFromAdminRejectsAtTheBound_RejectThenLogout) {
    auto app = std::make_shared<ResetRejectingApp>();
    Scene s{Options{.app = app}};
    const Gap g = open_gap(s);
    ASSERT_NE(g.n, 0U);
    std::uint32_t seq = g.n + kGapWidth;
    const auto make = [&](std::size_t) {
        ++seq;
        return s.sequence_reset(seq, seq + 10);
    };
    ASSERT_TRUE(stream_before_the_bound(
        s, g, make, [&](std::size_t, std::size_t mark, std::string const& at) {
            EXPECT_EQ(s.count("3", mark), 1U) << at << " drew " << s.written(mark);
        }));
    ASSERT_EQ(app->resets_rejected, 4) << "fromAdmin did not reject each SequenceReset";
    ASSERT_EQ(s.next_in(), g.n) << "a reset that fromAdmin rejected was applied";

    s.pass(1ms);
    const auto mark = s.sent.size();
    const auto r = s.feed(make(0));
    ASSERT_TRUE(deadline_logout_written(s, mark, r));
    ASSERT_EQ(s.sent.size(), mark + 2U) << "the session wrote " << s.written(mark);
    EXPECT_EQ(field(s.sent[mark], 35), "3") << "the Reject goes out before the Logout";
    EXPECT_EQ(field(s.sent[mark + 1], 35), "5");
}

namespace {

enum class new_seq_no : std::uint8_t { below, equal, absent };

// S2: inside every HeartBtInt the peer sends a Reset-mode SequenceReset above NextNumIn
// whose NewSeqNo(36) moves nothing. One below NextNumIn draws a Reject; one equal to it,
// or without the field, draws nothing. At the bound the Logout follows the arm's own
// outcome.
void run_reset_that_moves_nothing(new_seq_no which) {
    Scene s;
    const Gap g = open_gap(s);
    ASSERT_NE(g.n, 0U);
    ASSERT_GT(g.n, 1U) << "the cell needs a NewSeqNo below NextNumIn";
    const std::uint32_t value = which == new_seq_no::below   ? g.n - 1
                                : which == new_seq_no::equal ? g.n
                                                             : 0U;
    const std::size_t rejects_each = which == new_seq_no::below ? 1U : 0U;
    std::uint32_t seq = g.n + kGapWidth;
    const auto make = [&](std::size_t) { return s.sequence_reset(++seq, value); };
    ASSERT_TRUE(stream_before_the_bound(
        s, g, make, [&](std::size_t, std::size_t mark, std::string const& at) {
            EXPECT_EQ(s.count("3", mark), rejects_each) << at << " drew " << s.written(mark);
        }));
    ASSERT_EQ(s.next_in(), g.n) << "the reset moved NextNumIn";
    ASSERT_TRUE(s.resend_outstanding()) << "the reset ended the outstanding resend";

    s.pass(1ms);
    const auto mark = s.sent.size();
    const auto r = s.feed(make(0));
    ASSERT_TRUE(deadline_logout_written(s, mark, r));
    ASSERT_EQ(s.sent.size(), mark + rejects_each + 1U) << "the session wrote " << s.written(mark);
    if (rejects_each != 0U) {
        EXPECT_EQ(field(s.sent[mark], 35), "3") << "the Reject goes out before the Logout";
        EXPECT_EQ(field(s.sent[mark], 371), "36") << "RefTagID(371) of the arm's Reject";
    }
    EXPECT_EQ(field(s.sent.back(), 35), "5");
}

}  // namespace

TEST(GapDeadlineResetMode, S2_NewSeqNoBelowNextNumInAtTheBound_RejectThenLogout) {
    run_reset_that_moves_nothing(new_seq_no::below);
}

TEST(GapDeadlineResetMode, S2_NewSeqNoEqualToNextNumInAtTheBound_Logout) {
    run_reset_that_moves_nothing(new_seq_no::equal);
}

TEST(GapDeadlineResetMode, S2_NewSeqNoAbsentAtTheBound_Logout) {
    run_reset_that_moves_nothing(new_seq_no::absent);
}

// S3, guard: after the bound, a Reset-mode SequenceReset whose NewSeqNo(36) is above
// NextNumIn, and whose own MsgSeqNum(34) is above that NewSeqNo, is progress: NextNumIn
// moves and the resend ends. No Logout. The traffic past the bound is G1's.
TEST(GapDeadlineResetMode, S3_ResetThatAdvancesAfterTheBound_EndsTheResendAndNoLogout) {
    Scene s;
    const Gap g = open_gap(s);
    ASSERT_NE(g.n, 0U);
    ASSERT_TRUE(too_low_heartbeats_past_the_bound(s, g));

    const auto mark = s.sent.size();
    const auto r = s.feed(s.sequence_reset(g.n + 30, g.n + 10));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(s.next_in(), g.n + 10) << "NextNumIn after the reset";
    EXPECT_FALSE(s.resend_outstanding()) << "the reset did not end the outstanding resend";
    EXPECT_EQ(s.state(), fsm_state::Active);
    EXPECT_EQ(s.count("5", mark), 0U) << "the session wrote " << s.written(mark);
}

// ── D-537-5, the helper's too-high test ─────────────────────────────────────────────
//
// A frame that is not above NextNumIn never draws the Logout. Past the bound, with
// Heartbeats below NextNumIn as the only traffic, an in-sequence frame arrives that a
// Reject arm consumes. It advances NextNumIn, so a frame above NextNumIn straight after
// it is ignored: the bound counts from that advance.
//
// Precondition: the gap is kGapWidth wide, so the consumed frame does not reach the
// recorded end and the resend is still outstanding at the frame that follows.

namespace {

enum class consumed_at : std::uint8_t { validate_gate, stage1_arm_c };

void run_helpers_too_high_test(consumed_at where) {
    Scene s{Options{.validate = where == consumed_at::validate_gate}};
    const Gap g = open_gap(s);
    ASSERT_NE(g.n, 0U);
    ASSERT_TRUE(too_low_heartbeats_past_the_bound(s, g));

    auto mark = s.sent.size();
    (void)s.feed(where == consumed_at::validate_gate ? s.invalid_order(g.n)
                                                     : s.poss_dup_order(g.n, orig::absent));
    ASSERT_EQ(s.count("3", mark), 1U)
        << "the in-sequence frame was not Rejected; the session wrote " << s.written(mark);
    ASSERT_EQ(s.next_in(), g.n + 1) << "the Rejected in-sequence frame was not consumed";
    ASSERT_TRUE(still_waiting(s, g, "the in-sequence frame a Reject arm consumed"));
    ASSERT_TRUE(s.resend_outstanding()) << "precondition: the resend is still outstanding";

    mark = s.sent.size();
    (void)s.feed(s.order(g.n + kGapWidth + 1));
    ASSERT_TRUE(still_waiting(s, g, "the frame above NextNumIn straight after the advance"));
    EXPECT_EQ(s.sent.size(), mark) << "the session wrote for it: " << s.written(mark);
}

}  // namespace

TEST(GapDeadlineHelpersTooHighTest, InSequenceFrameConsumedAtTheValidateGate_NextFrameIgnored) {
    run_helpers_too_high_test(consumed_at::validate_gate);
}

TEST(GapDeadlineHelpersTooHighTest, InSequenceFrameConsumedAtStage1ArmC_NextFrameIgnored) {
    run_helpers_too_high_test(consumed_at::stage1_arm_c);
}

// ── D-537-5, progress: the two refresh sites ────────────────────────────────────────
//
// A replay that takes longer than the bound in total, with NextNumIn advancing inside
// each window, completes. After each advance but the last the peer sends a Heartbeat
// above NextNumIn, which is a frame at which the deadline is checked.

namespace {

// The gap is `kReplayed` wide. Every kReplayStep the peer sends the next in-sequence
// PossDup message, built by `replayed(seq)`, and then the Heartbeat above NextNumIn.
constexpr std::uint32_t kReplayed = 10;
constexpr auto kReplayStep = 25s;
static_assert(kReplayStep < kHeartBtInt);
static_assert(kReplayed * kReplayStep > 2 * kGapBound);

template <class Replayed>
void run_slow_replay(Scene& s, Gap const& g, Replayed replayed) {
    for (std::uint32_t k = 0; k < kReplayed; ++k) {
        s.pass(kReplayStep);
        const std::string at = "replay step " + std::to_string(k);
        (void)s.feed(replayed(g.n + k));
        ASSERT_EQ(s.next_in(), g.n + k + 1) << at << ": the replayed frame did not advance";
        ASSERT_TRUE(still_waiting(s, g, at));
        if (k + 1 == kReplayed) break;
        ASSERT_TRUE(s.resend_outstanding()) << at << ": the resend ended before the last frame";
        (void)s.feed(s.heartbeat(g.n + kReplayed + 100));
        ASSERT_TRUE(still_waiting(s, g, at + ", the Heartbeat above NextNumIn"));
        ASSERT_EQ(s.count("2", g.mark), 0U) << at << ": a second ResendRequest was written";
    }
    EXPECT_FALSE(s.resend_outstanding()) << "the replay through the recorded end left a resend";
    (void)s.feed(s.poss_dup_order(g.n + kReplayed, orig::valid));
    EXPECT_EQ(s.state(), fsm_state::Active);
    EXPECT_EQ(s.next_in(), g.n + kReplayed + 1) << "NextNumIn after the whole replay";
    EXPECT_EQ(s.count("5", g.mark), 0U) << "a Logout was written";
}

}  // namespace

// The advances are in-sequence frames that pass the sequence gate.
TEST(GapDeadlineProgress, SlowReplayThatKeepsAdvancing_Completes) {
    Scene s;
    const Gap g = open_gap(s, kReplayed);
    ASSERT_NE(g.n, 0U);
    run_slow_replay(s, g, [&](std::uint32_t seq) { return s.poss_dup_order(seq, orig::valid); });
    EXPECT_EQ(s.app->delivered.size(), kReplayed + 1U) << "messages the application received";
    EXPECT_EQ(s.count("3", g.mark), 0U);
}

// The only advances are frames a Reject arm consumes: PossDup messages in sequence with
// no OrigSendingTime(122).
TEST(GapDeadlineProgress, SlowReplayOfFramesARejectArmConsumes_Completes) {
    Scene s;
    const Gap g = open_gap(s, kReplayed);
    ASSERT_NE(g.n, 0U);
    run_slow_replay(s, g, [&](std::uint32_t seq) { return s.poss_dup_order(seq, orig::absent); });
    EXPECT_EQ(s.count("3", g.mark), kReplayed) << "one Reject for each consumed frame";
}

// ── D-537-5, frame types ────────────────────────────────────────────────────────────

namespace {

// Before the bound each of the peer's frames above NextNumIn, built by `make(seq)`, is
// dropped and draws nothing. The first at the bound draws the Logout.
template <class Make>
void run_dropped_frame_type(Make make_frame) {
    Scene s;
    const Gap g = open_gap(s);
    ASSERT_NE(g.n, 0U);
    std::uint32_t seq = g.n + kGapWidth;
    const auto make = [&](std::size_t) { return make_frame(s, ++seq); };
    ASSERT_TRUE(stream_before_the_bound(
        s, g, make, [&](std::size_t, std::size_t mark, std::string const& at) {
            EXPECT_EQ(s.sent.size(), mark) << at << " drew " << s.written(mark);
        }));

    s.pass(1ms);
    const auto mark = s.sent.size();
    const auto r = s.feed(make(0));
    ASSERT_TRUE(deadline_logout_written(s, mark, r));
    EXPECT_EQ(s.sent.size(), mark + 1U) << "the session wrote " << s.written(mark);
}

}  // namespace

TEST(GapDeadlineFrameTypes, HeartbeatAboveNextNumInAtTheBound_Logout) {
    run_dropped_frame_type([](Scene const& s, std::uint32_t seq) { return s.heartbeat(seq); });
}

// A PossDup frame that passes Stage 1.
TEST(GapDeadlineFrameTypes, PossDupFrameAboveNextNumInAtTheBound_Logout) {
    run_dropped_frame_type(
        [](Scene const& s, std::uint32_t seq) { return s.poss_dup_order(seq, orig::valid); });
}

// ── D-537-5, the Logout leaf's error ────────────────────────────────────────────────

// The application's toAdmin throws on the deadline's Logout: no Logout is written, the
// session is Disconnected, and the frame returns the leaf's error.
TEST(GapDeadlineLogoutLeaf, ToAdminThrowsOnTheLogout_ReturnsAppCallbackThrew) {
    auto app = std::make_shared<LogoutThrowingApp>();
    Scene s{Options{.app = app}};
    const Gap g = open_gap(s);
    ASSERT_NE(g.n, 0U);
    std::uint32_t seq = g.n + kGapWidth;
    const auto make = [&](std::size_t) { return s.heartbeat(++seq); };
    ASSERT_TRUE(stream_before_the_bound(s, g, make));
    ASSERT_EQ(app->to_admin_count("5"), 0) << "a Logout reached toAdmin before the bound";

    s.pass(1ms);
    const auto mark = s.sent.size();
    const auto r = s.feed(make(0));
    ASSERT_FALSE(r.has_value())
        << "on_inbound_frame returned success for the frame at the bound; toAdmin saw "
        << app->to_admin_count("5") << " Logout(s) and the session is " << state_name(s.state());
    EXPECT_EQ(r.error(), error::app_callback_threw);
    EXPECT_EQ(s.state(), fsm_state::Disconnected);
    EXPECT_EQ(app->to_admin_count("5"), 1) << "Logouts handed to toAdmin";
    EXPECT_EQ(s.count("5", mark), 0U) << "a Logout was written after its toAdmin threw";
}

// ── D-537-5, off ────────────────────────────────────────────────────────────────────

// HeartBtInt = 0: no deadline. A frame above NextNumIn is ignored however long the gap
// has been open.
TEST(GapDeadlineOff, HeartBtIntZero_FrameLongAfterTheGapOpenedIsIgnored) {
    Scene s{Options{.heartbeat = 0s}};
    const Gap g = open_gap(s);
    ASSERT_NE(g.n, 0U);
    for (int i = 1; i <= 3; ++i) {
        s.pass(10 * kGapBound);
        const auto mark = s.sent.size();
        (void)s.feed(s.order(g.n + kGapWidth + i));
        ASSERT_TRUE(still_waiting(s, g, "frame " + std::to_string(i) + " above NextNumIn"));
        EXPECT_EQ(s.sent.size(), mark) << "the session wrote " << s.written(mark);
    }
    EXPECT_TRUE(s.resend_outstanding());
}

// A Session with no clock: no deadline. The Scene's own clock moves, and the session
// cannot read it.
TEST(GapDeadlineOff, NoClock_FrameAboveNextNumInIsIgnored) {
    Scene s{Options{.with_clock = false}};
    const Gap g = open_gap(s);
    ASSERT_NE(g.n, 0U);
    for (int i = 1; i <= 3; ++i) {
        s.pass(10 * kGapBound);
        const auto mark = s.sent.size();
        (void)s.feed(s.order(g.n + kGapWidth + i));
        ASSERT_TRUE(still_waiting(s, g, "frame " + std::to_string(i) + " above NextNumIn"));
        EXPECT_EQ(s.sent.size(), mark) << "the session wrote " << s.written(mark);
    }
    EXPECT_TRUE(s.resend_outstanding());
}

// A negative HeartBtInt: no deadline, and no assertion where assertions are compiled in.
// It is read on the function that computes the bound, not on a session: with a negative
// interval run_liveness_loop's deadlines are already past, so the loop ends the session
// before any frame could meet the deadline. The call runs in a child process, so that an
// assertion that fires there is a failure of this cell and not the end of the binary.
TEST(GapDeadlineOffDeathTest, NegativeHeartBtInt_TheBoundGivesNoDeadlineAndDoesNotAssert) {
    const auto since = fixpp::core::steady_time_point{} + 1h;
    std::chrono::seconds negative{-1};
    EXPECT_EXIT(
        {
            const auto deadline = fixpp::session::detail::resend_gap_deadline(since, negative);
            std::_Exit(deadline.has_value() ? 1 : 0);
        },
        ::testing::ExitedWithCode(0), "")
        << "exit status 1: the bound gave a deadline for a negative HeartBtInt; any other "
           "ending: the call did not return";
    // The function can give a deadline: at a positive interval it is twice the interval on.
    const auto on = fixpp::session::detail::resend_gap_deadline(since, kHeartBtInt);
    ASSERT_TRUE(on.has_value());
    EXPECT_EQ(*on, since + 60s);
}

// ── D-537-5, short GapFill ──────────────────────────────────────────────────────────

// A GapFill that lands short of the recorded end ends the resend: NextNumIn moved. The
// next frame above NextNumIn opens a new gap with a new ResendRequest, at E2, and the
// bound counts from E2: a frame past E + kGapBound and before E2 + kGapBound is ignored.
TEST(GapDeadlineShortGapFill, NewResendRequestAndADeadlineThatStartsAgain) {
    Scene s;
    const Gap g = open_gap(s, 10);
    ASSERT_NE(g.n, 0U);
    std::uint32_t seq = g.n + 100;
    for (int i = 1; i <= 3; ++i) {
        s.pass(kStep);
        (void)s.feed(s.heartbeat(++seq));
        ASSERT_TRUE(still_waiting(s, g, "a Heartbeat above NextNumIn before the GapFill"));
    }

    // At E + 3 kStep: the GapFill, then the frame that opens the second gap.
    (void)s.feed(s.gap_fill(g.n, g.n + 3));
    ASSERT_EQ(s.next_in(), g.n + 3) << "NextNumIn after the short GapFill";
    ASSERT_FALSE(s.resend_outstanding()) << "the short GapFill left the resend outstanding";
    const auto second_gap = s.sent.size();
    (void)s.feed(s.order(g.n + 11));
    const auto rr = s.of_type("2", second_gap);
    ASSERT_EQ(rr.size(), 1U) << "no new ResendRequest; the session wrote " << s.written(second_gap);
    EXPECT_EQ(field(rr.front(), 7), std::to_string(g.n + 3));
    ASSERT_TRUE(s.resend_outstanding());

    // E2 = E + 3 kStep. Frames at E2 + kStep to E2 + 3 kStep, and a millisecond before
    // E2 + kGapBound: the first of them is at E + kGapBound.
    const std::array<std::chrono::nanoseconds, 4> steps{kStep, kStep, kStep,
                                                        kGapBound - 3 * kStep - 1ms};
    for (const auto step : steps) {
        s.pass(step);
        const auto mark = s.sent.size();
        (void)s.feed(s.heartbeat(++seq));
        ASSERT_TRUE(still_waiting(s, g, "a Heartbeat above NextNumIn before E2 + the bound"));
        EXPECT_EQ(s.sent.size(), mark) << "the session wrote " << s.written(mark);
    }

    s.pass(1ms);
    const auto mark = s.sent.size();
    const auto r = s.feed(s.heartbeat(++seq));
    EXPECT_TRUE(deadline_logout_written(s, mark, r));
}

// ── D-537-9, the residual pattern ───────────────────────────────────────────────────
//
// Pin of a disclosed limitation (ruling R-11). The peer never fills the gap and, inside
// every HeartBtInt, sends only a frame that is not above NextNumIn and moves nothing.
// Each such frame refreshes liveness, so no TestRequest is sent, and none is above
// NextNumIn, so the deadline is not checked. redeliver_poss_dup is off.

namespace {

// How long the pattern runs: three times the bound, in kStep frames.
constexpr int kPatternFrames = static_cast<int>(3 * kGapBound / kStep);

// `member(s)` builds one frame of the pattern; each draws `rejects_each` Rejects.
template <class Member>
void run_residual_pattern(Member member, std::size_t rejects_each) {
    Scene s;
    const Gap g = open_gap(s);
    ASSERT_NE(g.n, 0U);
    ASSERT_GT(g.n, 1U) << "the cell needs a MsgSeqNum below NextNumIn";

    // fixpp#581 gives this pattern an ending, and flips the assertions of this loop.
    for (int i = 1; i <= kPatternFrames; ++i) {
        s.pass(kStep);
        const auto mark = s.sent.size();
        (void)s.feed(member(s, g));
        const std::string at = "pattern frame " + std::to_string(i);
        ASSERT_TRUE(still_waiting(s, g, at));
        ASSERT_EQ(s.count("3", mark), rejects_each) << at << " drew " << s.written(mark);
        ASSERT_EQ(s.next_in(), g.n) << at << " moved NextNumIn";
        ASSERT_TRUE(s.resend_outstanding()) << at << " ended the outstanding resend";
    }

    // A frame above NextNumIn then draws the deadline's Logout.
    const auto mark = s.sent.size();
    const auto r = s.feed(s.heartbeat(g.n + kGapWidth + 1));
    EXPECT_TRUE(deadline_logout_written(s, mark, r));
}

}  // namespace

TEST(GapDeadlineResidualPattern, Pin_HeartbeatsBelowNextNumIn_NoEndingUntilAFrameAbove) {
    run_residual_pattern([](Scene const& s, Gap const&) { return s.heartbeat(1); }, 0);
}

TEST(GapDeadlineResidualPattern, Pin_PossDupFramesBelowNextNumIn_NoEndingUntilAFrameAbove) {
    run_residual_pattern(
        [](Scene const& s, Gap const&) { return s.poss_dup_order(1, orig::valid); }, 0);
}

// A Reset-mode SequenceReset at NextNumIn with no NewSeqNo(36).
TEST(GapDeadlineResidualPattern, Pin_ResetsAtNextNumInThatMoveNothing_NoEndingUntilAFrameAbove) {
    run_residual_pattern([](Scene const& s, Gap const& g) { return s.sequence_reset(g.n, 0); }, 0);
}

// A PossDup application message below NextNumIn with no OrigSendingTime(122): Stage 1
// Rejects it and does not consume it.
TEST(GapDeadlineResidualPattern,
     Pin_PossDupFramesBelowNextNumInWithNoOrigSendingTime_RejectedAndNoEnding) {
    run_residual_pattern(
        [](Scene const& s, Gap const&) { return s.poss_dup_order(1, orig::absent); }, 1);
}

}  // namespace fixpp::session::test
