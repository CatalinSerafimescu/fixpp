#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// src/session/session_engine_access.hpp — the engine's seam into Session
// (093-inbound-frame-dispositions, data-model E-11, plan OD-12).
//
// `Session` declares `friend struct session_engine_access;` unconditionally.
// Define this struct only here; a differing definition elsewhere in the program is
// an ODR violation (ill-formed, no diagnostic required). It is production code for
// the engine (run_read_pump and the accept loop in src/session/engine.cpp), not
// test access: tests use session_test_access (tests/support/). Never installed
// (no install() rule names src/). Not gated on FIXPP_TEST_HOOKS.
//
// Add an engine hook here as a static member, never as an underscore-suffixed
// member of Session's installed surface.

#include <cstdint>
#include <fixpp/session/session.hpp>

namespace fixpp::session {

struct session_engine_access {
    // The inbound limit L that Session::open() stored (data-model E-2).
    [[nodiscard]] static std::uint32_t inbound_limit(Session const& s) noexcept {
        return s.inbound_limit_;
    }
};

}  // namespace fixpp::session
