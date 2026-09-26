#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/session_test_access.hpp — TEST-ONLY read access to Session's
// private state, through the unconditional `friend struct session_test_access;`
// in include/fixpp/session/session.hpp. Defined here, once, so every TU that
// uses it sees the same definition. Needs no FIXPP_TEST_HOOKS.
//
// Every accessor reads strand-owned state, so call it on the session's strand
// (post to Session::executor()).

#include "fixpp/session/session.hpp"

namespace fixpp::session {

struct session_test_access {
    // True iff the session reached lifecycle::closed_drained, the state in which
    // Session::close returns session_already_closed.
    [[nodiscard]] static bool is_drained(Session const& s) noexcept {
        return s.state_ == Session::lifecycle::closed_drained;
    }
};

}  // namespace fixpp::session
