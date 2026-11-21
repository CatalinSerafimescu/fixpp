// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/support/r2_wait_reading.hpp — test-only.
//
// 105-transport-liveness, the R-2 wait (contract C-2: C-2.1, C-2.5, W-12), which supersedes the
// "Logout, then Disconnected at once" ending of every Logout on C-2's "Which Logouts" list: after
// fixpp transmits such a Logout the session stays Active until the peer's Logout or the bound,
// logout_disconnect_timeout_ms after the start of the Logout's write on the session's clock.
// A cell that drives a Session directly reads that rule here, with the mock clock still at the
// instant the ending's write started: the state right after the ending, at the bound - 1 ms and
// at the bound, `pump` run after each advance.
#pragma once

#include <gtest/gtest.h>

#include <chrono>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>

namespace fixpp::test_support {

struct r2_wait_reading {
    fixpp::session::fsm_state after_ending;
    fixpp::session::fsm_state before_bound;
    fixpp::session::fsm_state at_bound;
};

template <class Pump>
[[nodiscard]] r2_wait_reading read_r2_wait(fixpp::session::Session const& s,
                                           fixpp::core::mock_clock& clock,
                                           std::chrono::milliseconds bound, Pump&& pump) {
    r2_wait_reading r{.after_ending = s.state(),
                      .before_bound = fixpp::session::fsm_state::Disconnected,
                      .at_bound = fixpp::session::fsm_state::Active};
    clock.advance(bound - std::chrono::milliseconds{1});
    pump();
    r.before_bound = s.state();
    clock.advance(std::chrono::milliseconds{1});
    pump();
    r.at_bound = s.state();
    return r;
}

// Active after the ending and at the bound - 1 ms, Disconnected at the bound.
inline void expect_r2_wait(r2_wait_reading const& r, char const* context) {
    using fixpp::session::fsm_state;
    EXPECT_EQ(r.after_ending, fsm_state::Active)
        << context << ": the session did not stay Active after its ending Logout (R-2)";
    EXPECT_EQ(r.before_bound, fsm_state::Active)
        << context << ": the session ended before the bound (R-2)";
    EXPECT_EQ(r.at_bound, fsm_state::Disconnected)
        << context << ": the session did not end at the bound (R-2)";
}

}  // namespace fixpp::test_support
