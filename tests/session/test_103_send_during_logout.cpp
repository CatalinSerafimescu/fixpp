// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_103_send_during_logout.cpp — test-only.
//
// Feature 103 (specs/103-resend-logon-reset-fidelity/; FR-021, an addendum by orchestrator
// ruling in Phase 8; tasks T187). A Session::send made while the session is ending by a
// Logout of its own is refused for the whole ending, by every Logout emitter of an
// established session: no application frame is numbered after the Logout. Each cell holds
// the emitter's Logout in its store call (resend_gap_scene.hpp's recording store) and
// makes the send during the hold. This file reads no 103 state, so it compiles on the base.
//
// The emitters (research D4.6, the Logout owner's two leaves):
//   in_sequence_logout  the peer's in-sequence Logout, acknowledged;
//   gap_deadline        a gap kept open past its deadline, then a frame that trips it;
//   sending_time_fatal  a frame whose SendingTime(52) is outside the threshold: a Reject,
//                       then the Logout;
//   arm_d               a PossDup frame whose OrigSendingTime(122) is after its
//                       SendingTime(52): a Reject, then the Logout;
//   too_high_no_clock   a too-high Logout on a session with no clock, acknowledged at once;
//   too_high_bound      a too-high Logout acknowledged by its bound at T;
//   close_phase1        close(graceful)'s phase-1 Logout, the other leaf (R588f_GracefulClose
//                       covers a send contended at the replay gate during that close);
//   too_low             a frame below NextNumIn without PossDupFlag(43): Arm B's Logout
//                       (research D9, FR-018).
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include "session/test_103_support.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"

namespace {

namespace rg = fixpp::test_support::resend_gap;
using fixpp::session::fsm_state;
using fixpp::session::seqnum_t;
using fixpp::session::session_role;
using fixpp::session::session_test_access;
using fixpp::test_support::store_op;
using fixpp::test_support::StoreCall;

constexpr std::uint32_t kT = 2000;  // the bound T, in milliseconds
// The wait for the ending after the release: below the grace T.
constexpr auto kAfterReleaseBudget = std::chrono::milliseconds{kT} / 4;

enum class Emitter : std::uint8_t {
    in_sequence_logout,
    gap_deadline,
    sending_time_fatal,
    arm_d,
    too_high_no_clock,
    too_high_bound,
    close_phase1,
    too_low
};

seqnum_t next_out(rg::Scene const& s) {
    return session_test_access::seqnum_mgr(*s.session).peek_outbound();
}
void hold_store_of(rg::Scene& s, seqnum_t const seq) {
    s.log->hold_in = [seq](StoreCall const& c) {
        return c.op == store_op::store && c.dir == fixpp::session::direction_t::outbound &&
               c.first == seq;
    };
}
std::vector<std::byte> poss_dup_order_with_later_orig(rg::Scene const& s, std::uint32_t seq) {
    return rg::Scene::raw("D", seq, "TW", s.sending_time(),
                          "43=Y\x01"
                          "122=" +
                              s.sending_time(std::chrono::seconds{60}) + "\x01" +
                              rg::Scene::order_fields(seq));
}

// The MsgSeqNum(34) of each frame of `type` written from `mark` on.
std::vector<seqnum_t> seqs_of(rg::Scene const& s, std::string_view type, std::size_t mark) {
    std::vector<seqnum_t> out;
    for (std::size_t i = mark; i < s.sent.size(); ++i) {
        if (rg::field(s.sent[i], 35) == type) {
            out.push_back(static_cast<seqnum_t>(std::stoul(std::string{rg::field(s.sent[i], 34)})));
        }
    }
    return out;
}

class SendDuringLogout103 : public ::testing::TestWithParam<std::tuple<session_role, Emitter>> {};

}  // namespace

TEST_P(SendDuringLogout103, SendWhileTheEmittersLogoutIsStored_RefusedNoAppFrameAfter) {
    auto const [role, e] = GetParam();
    rg::Scene s{rg::Options{.role = role,
                            .with_clock = e != Emitter::too_high_no_clock,
                            .logout_timeout_ms = kT,
                            .recording_store = true}};
    ASSERT_TRUE(s.open_to_active()) << "setup";
    s.log->arm();
    seqnum_t const n = s.next_in();
    std::uint32_t deadline_seq = 0;
    if (e == Emitter::gap_deadline) {
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
    if (e == Emitter::too_high_bound) {
        (void)s.feed(s.logout(n + 3));
        ASSERT_EQ(s.state(), fsm_state::Active) << "setup: the Logout ended the session";
    }
    // The emitter's Logout is the session's next frame, or the one after its Reject.
    seqnum_t const logout_seq =
        next_out(s) + ((e == Emitter::sending_time_fatal || e == Emitter::arm_d) ? 1U : 0U);
    hold_store_of(s, logout_seq);
    auto const mark = s.sent.size();
    std::optional<std::future<fixpp::core::expected_t<void>>> feed_fut;
    std::optional<std::future<fixpp::core::expected_t<void>>> close_fut;
    switch (e) {
        case Emitter::in_sequence_logout:
            feed_fut = s.feed_spawn(s.logout(n));
            break;
        case Emitter::gap_deadline:
            feed_fut = s.feed_spawn(s.heartbeat(++deadline_seq));
            break;
        case Emitter::sending_time_fatal:
            feed_fut = s.feed_spawn(s.order_with_stale_sending_time(n));
            break;
        case Emitter::arm_d:
            feed_fut = s.feed_spawn(poss_dup_order_with_later_orig(s, n));
            break;
        case Emitter::too_high_no_clock:
            feed_fut = s.feed_spawn(s.logout(n + 3));
            break;
        case Emitter::too_high_bound:
            s.mock->advance(std::chrono::milliseconds{kT});
            break;
        case Emitter::close_phase1:
            close_fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                       s.session->close(fixpp::session::close_mode::graceful),
                                       asio::use_future);
            break;
        case Emitter::too_low:
            feed_fut = s.feed_spawn(s.order(n - 1));
            break;
    }
    ASSERT_TRUE(s.pump_until([&] { return s.log->holding(); }))
        << "setup: the emitter's Logout store was not held: " << s.written(mark);

    std::string const payload =
        "35=D\x01"
        "11=ORD-103-FR021\x01" +
        std::string{rg::kSideAndTime};
    auto send_fut =
        asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                       s.session->send(std::span<const std::byte>{
                           reinterpret_cast<std::byte const*>(payload.data()), payload.size()}),
                       asio::use_future);
    s.pump_for(std::chrono::milliseconds{20});
    s.log->release_hold();
    ASSERT_TRUE(s.pump_until(
        [&] {
            return rg::Scene::ready(send_fut) && (!feed_fut || rg::Scene::ready(*feed_fut)) &&
                   s.count("5", mark) != 0;
        },
        kAfterReleaseBudget))
        << "the send, the delivery or the Logout did not complete: " << s.written(mark);

    auto const logouts = seqs_of(s, "5", mark);
    ASSERT_EQ(logouts.size(), 1U) << s.written(mark);
    for (seqnum_t const a : seqs_of(s, "D", mark)) {
        EXPECT_LT(a, logouts.front())
            << "an application frame numbered after the Logout: " << s.written(mark);
    }
    auto const r = send_fut.get();
    EXPECT_FALSE(r.has_value()) << "the send was admitted: " << s.written(mark);
    if (!r.has_value()) {
        EXPECT_EQ(r.error(), fixpp::core::error::session_invalid_state_for_send)
            << static_cast<int>(r.error());
    }
}

std::string emitter_name(::testing::TestParamInfo<std::tuple<session_role, Emitter>> const& info) {
    std::string e;
    switch (std::get<1>(info.param)) {
        case Emitter::in_sequence_logout:
            e = "InSequenceLogout";
            break;
        case Emitter::gap_deadline:
            e = "GapDeadline";
            break;
        case Emitter::sending_time_fatal:
            e = "SendingTimeFatal";
            break;
        case Emitter::arm_d:
            e = "ArmD";
            break;
        case Emitter::too_high_no_clock:
            e = "TooHighNoClock";
            break;
        case Emitter::too_high_bound:
            e = "TooHighBound";
            break;
        case Emitter::close_phase1:
            e = "ClosePhase1";
            break;
        case Emitter::too_low:
            e = "TooLow";
            break;
    }
    return std::string{rg::role_name(std::get<0>(info.param))} + e;
}

INSTANTIATE_TEST_SUITE_P(
    BothRolesEveryEmitter, SendDuringLogout103,
    ::testing::Combine(::testing::Values(session_role::initiator, session_role::acceptor),
                       ::testing::Values(Emitter::in_sequence_logout, Emitter::gap_deadline,
                                         Emitter::sending_time_fatal, Emitter::arm_d,
                                         Emitter::too_high_no_clock, Emitter::too_high_bound,
                                         Emitter::close_phase1, Emitter::too_low)),
    emitter_name);
