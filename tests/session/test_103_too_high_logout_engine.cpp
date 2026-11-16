// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_103_too_high_logout_engine.cpp — test-only.
//
// Feature 103 (specs/103-resend-logon-reset-fidelity/), fixpp#577, the Logout half (research
// D4.4, D4.6, "#577 — a too-high Logout"; tasks T080 (g)). Cell (g)'s EOF arm
// under an Engine: the peer's EOF while a too-high Logout is pending, read by the Engine's read
// pump. The Scene cells drive Session::close(close_mode::terminal) themselves; this cell has
// the pump read a real half-close, so the ending is the pump's own.
//
// Each cell runs one session under an Engine on the plain transport, facing a raw peer, on
// plain_engine_rig.hpp's Rig (a mock engine clock). The mock clock is never advanced, so the
// bound's T cannot end anything here: only the EOF can.
#include <gtest/gtest.h>

#include <algorithm>
#include <asio/ip/tcp.hpp>
#include <cstdint>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <string>
#include <string_view>
#include <system_error>

#include "session/plain_engine_rig.hpp"
#include "session/resend_gap_scene.hpp"
#include "support/session_test_access.hpp"

namespace {

namespace plain_rig = fixpp::test_support::plain_rig;
namespace rg = fixpp::test_support::resend_gap;
using fixpp::session::fsm_state;
using fixpp::session::session_role;
using fixpp::session::session_test_access;

constexpr std::uint32_t kT = 2000;  // the bound T, in milliseconds
constexpr int kPending = 1;         // session_test_access::peer_logout_phase's `pending`

class TooHighLogoutEngine103 : public ::testing::TestWithParam<session_role> {};

// The peer's received bytes with SOH shown as '|'.
std::string printable(std::string_view wire) {
    std::string out{wire};
    std::replace(out.begin(), out.end(), '\x01', '|');
    return out;
}

}  // namespace

// (g), the peer's EOF (C6): while a too-high Logout is pending, the peer half-closes its
// connection. The read pump reads the EOF and ends the session: no 35=5, the transport
// closed, Disconnected, and the bound's join counter 0.
// FR-010 case 4.  claim-ok: a spec case, not a tally
TEST_P(TooHighLogoutEngine103, G_PeerEofWhilePending_NoLogout) {
    plain_rig::Rig rig;
    auto c = rig.cfg(GetParam());
    c.logout_disconnect_timeout_ms = kT;
    ASSERT_TRUE(rig.start(std::move(c))) << "setup: the engine did not start";
    ASSERT_TRUE(rig.to_active()) << "setup: the session did not reach Active";
    auto const sess = rig.session();
    ASSERT_NE(sess, nullptr) << "setup: the session is not published";
    auto const mark = rig.peer.received.size();
    auto written = [&] { return std::string_view{rig.peer.received}.substr(mark); };

    // The Logon is 34=1, so 34=5 is above NextNumIn.
    rig.peer.send(rig.msg("5", 5));
    ASSERT_TRUE(rig.run_until([&] { return !plain_rig::frames_of_type(written(), "2").empty(); }))
        << "setup: the too-high Logout drew no ResendRequest";
    ASSERT_EQ(session_test_access::peer_logout_phase(*sess), kPending) << "setup: no pending mark";

    std::error_code ec;
    rig.peer.sock.shutdown(asio::ip::tcp::socket::shutdown_send, ec);
    ASSERT_FALSE(ec) << "setup: the half-close failed: " << ec.message();

    EXPECT_TRUE(rig.run_until([&] { return rig.peer.read_ended; }))
        << "the session did not close the connection after the peer's EOF";
    EXPECT_TRUE(plain_rig::frames_of_type(written(), "5").empty())
        << "a Logout was written: " << printable(written());
    EXPECT_EQ(sess->state(), fsm_state::Disconnected);
    EXPECT_TRUE(rig.run_until([&] {
        return session_test_access::peer_logout_bound_count(*sess) == 0;
    })) << "the bound did not exit";
    rig.stop();
}

INSTANTIATE_TEST_SUITE_P(BothRoles, TooHighLogoutEngine103,
                         ::testing::Values(session_role::initiator, session_role::acceptor),
                         [](auto const& info) { return std::string{rg::role_name(info.param)}; });
