// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_103_too_high_logout.cpp — test-only.
//
// Feature 103 (specs/103-resend-logon-reset-fidelity/), fixpp#577, the Logout half: a Logout
// above NextNumIn in Active is acknowledged after a bounded recovery (research D4.2 to D4.5,
// D4.7, "#577 — a too-high Logout"; FR-010, FR-011; tasks T076). TU1: the arms that read no
// 103 state, so this file compiles on the base as well. The arms that need it are
// test_103_too_high_logout_state.cpp's.
//
// Every cell runs on both roles, on resend_gap_scene.hpp's Scene. The bound T is
// SessionConfig::logout_disconnect_timeout_ms; the cells set it explicitly. The mock clock
// moves only where a cell calls pass(), so an acknowledgement in a cell that never moves it
// was not written by the bound's timer.
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <span>
#include <string>
#include <string_view>

#include "session/test_103_support.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"

namespace {

namespace rg = fixpp::test_support::resend_gap;
namespace t103 = fixpp::test_support::t103;
using fixpp::core::error;
using fixpp::session::fsm_state;
using fixpp::session::seqnum_t;
using fixpp::session::session_role;
using fixpp::session::session_test_access;

constexpr std::uint32_t kT = 2000;  // the bound T, in milliseconds

class TooHighLogout103 : public ::testing::TestWithParam<session_role> {};

// Session::send of one application message; its result, or kWindowMissSentinel when it did
// not complete within the pump.
fixpp::core::expected_t<void> send_one(rg::Scene& s, std::string const& payload) {
    auto fut =
        asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                       s.session->send(std::span<const std::byte>{
                           reinterpret_cast<std::byte const*>(payload.data()), payload.size()}),
                       asio::use_future);
    if (!t103::pump_or_fail(
            s.ioc, [&] { return rg::Scene::ready(fut); }, fixpp::test_support::kPumpBudget,
            "t103_577_logout/send")) {
        return std::unexpected(fixpp::test_support::kWindowMissSentinel);
    }
    s.drain();
    return fut.get();
}

// The session's lifecycle: 1 (open) when no close() has run (session.hpp's Session::lifecycle).
int lifecycle(rg::Scene const& s) { return session_test_access::lifecycle_value(*s.session); }

}  // namespace

// Control: an in-sequence Logout is acknowledged at once.
TEST_P(TooHighLogout103, Control_InSequenceLogout_AcknowledgedAtOnce) {
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = kT}};
    ASSERT_TRUE(s.open_to_active());
    seqnum_t const n = s.next_in();
    auto const mark = s.sent.size();
    (void)s.feed(s.logout(n));
    EXPECT_EQ(s.count("5", mark), 1U) << s.written(mark);
    EXPECT_EQ(s.state(), fsm_state::Disconnected) << s.written(mark);
}

// (a) a Logout at N+3, then the peer's GapFill from N to N+4, with no clock move: one 35=5,
// written after the GapFill arrived, then Disconnected, and no close() ran.
TEST_P(TooHighLogout103, ArmA_GapFilledPastTheLogout_AcknowledgedWithNoClockMove) {
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = kT}};
    ASSERT_TRUE(s.open_to_active());
    seqnum_t const n = s.next_in();
    auto const mark = s.sent.size();
    (void)s.feed(s.logout(n + 3));
    EXPECT_EQ(s.count("5", mark), 0U)
        << "acknowledged before the gap was filled: " << s.written(mark);
    auto const gap_mark = s.sent.size();
    (void)s.feed(s.gap_fill(n, n + 4));
    EXPECT_EQ(s.count("5", mark), 1U) << s.written(mark);
    EXPECT_EQ(s.count("5", gap_mark), 1U)
        << "the acknowledgement was not written after the GapFill: " << s.written(mark);
    EXPECT_EQ(s.state(), fsm_state::Disconnected) << s.written(mark);
    EXPECT_EQ(lifecycle(s), 1) << "a close() ran: the transport is to stay open";
}

// (b) a Logout at N+3 and no recovery: none at T−1ms, one at T.
TEST_P(TooHighLogout103, ArmB_NoRecovery_AcknowledgedAtTheBound) {
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = kT}};
    ASSERT_TRUE(s.open_to_active());
    seqnum_t const n = s.next_in();
    auto const mark = s.sent.size();
    (void)s.feed(s.logout(n + 3));
    s.pass(std::chrono::milliseconds{kT} - std::chrono::milliseconds{1});
    EXPECT_EQ(s.count("5", mark), 0U) << "acknowledged before T: " << s.written(mark);
    EXPECT_EQ(s.state(), fsm_state::Active) << s.written(mark);
    s.pass(std::chrono::milliseconds{1});
    EXPECT_EQ(s.count("5", mark), 1U) << "no acknowledgement at T: " << s.written(mark);
    EXPECT_EQ(s.state(), fsm_state::Disconnected) << s.written(mark);
}

// (c) T = 0: one 35=5 at once, and no ResendRequest.
TEST_P(TooHighLogout103, ArmC_BoundZero_AcknowledgedAtOnceNoResendRequest) {
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = 0}};
    ASSERT_TRUE(s.open_to_active());
    seqnum_t const n = s.next_in();
    auto const mark = s.sent.size();
    (void)s.feed(s.logout(n + 3));
    EXPECT_EQ(s.count("5", mark), 1U) << s.written(mark);
    EXPECT_EQ(s.count("2", mark), 0U) << s.written(mark);
    EXPECT_EQ(s.state(), fsm_state::Disconnected) << s.written(mark);
}

// (i) no clock: as (c).
TEST_P(TooHighLogout103, ArmI_NoClock_AcknowledgedAtOnceNoResendRequest) {
    rg::Scene s{rg::Options{.role = GetParam(), .with_clock = false, .logout_timeout_ms = kT}};
    ASSERT_TRUE(s.open_to_active());
    seqnum_t const n = s.next_in();
    auto const mark = s.sent.size();
    (void)s.feed(s.logout(n + 3));
    EXPECT_EQ(s.count("5", mark), 1U) << s.written(mark);
    EXPECT_EQ(s.count("2", mark), 0U) << s.written(mark);
    EXPECT_EQ(s.state(), fsm_state::Disconnected) << s.written(mark);
}

// (d) Session::send while a too-high Logout waits for its acknowledgement: refused, and
// nothing written.
TEST_P(TooHighLogout103, ArmD_SendWhilePending_Refused) {
    rg::Scene s{rg::Options{.role = GetParam(), .logout_timeout_ms = kT}};
    ASSERT_TRUE(s.open_to_active());
    seqnum_t const n = s.next_in();
    (void)s.feed(s.logout(n + 3));
    ASSERT_EQ(s.state(), fsm_state::Active) << "setup: the Logout ended the session";
    auto const mark = s.sent.size();
    std::string const payload =
        "35=D\x01"
        "11=ORD-577-D\x01" +
        std::string{rg::kSideAndTime};
    auto const r = send_one(s, payload);
    ASSERT_FALSE(r.has_value()) << "the send was admitted: " << s.written(mark);
    EXPECT_EQ(r.error(), error::session_invalid_state_for_send) << static_cast<int>(r.error());
    EXPECT_EQ(s.sent.size(), mark) << s.written(mark);
}

INSTANTIATE_TEST_SUITE_P(BothRoles, TooHighLogout103,
                         ::testing::Values(session_role::initiator, session_role::acceptor),
                         [](auto const& info) { return std::string{rg::role_name(info.param)}; });
