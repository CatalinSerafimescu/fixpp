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

    // Accounts one summary of disregarded inbound bytes (data-model E-4): the read
    // pump's after a feed, the first-frame read's once after open(). Session strand.
    static void note_garbles(Session& s, fixpp::wire::garble_summary const& g) noexcept {
        s.note_garbles_(g);
    }

    // Whether the session has first reached Active (data-model E-6): the read pump tests
    // the establishment deadline only while this is false. Session strand.
    [[nodiscard]] static bool has_reached_active(Session const& s) noexcept {
        return s.reached_active_;
    }

    // The establishment deadline passed before the first Active (contract C-4): the
    // event and the log record. The pump then closes the session. Session strand.
    static void note_establishment_timeout(Session& s) noexcept { s.note_establishment_timeout_(); }

    // The carry open() allocated (data-model E-2, plan OD-13): L plus one read. The read
    // pump borrows it for the connection. Session strand.
    [[nodiscard]] static fixpp::wire::pmr_carry_buffer& carry(Session& s) noexcept {
        // Precondition: open() succeeded, so carry_ holds a value.
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        return *s.carry_;
    }

    // A frame over L was refused at framing (FR-013): the log record. The pump then
    // closes the session terminally. Session strand.
    static void note_frame_too_large(Session& s) noexcept { s.note_frame_too_large_(); }
    // Engine::stop()'s step 1 has reached the session's strand (data-model E-13): the
    // engine-stop flag Logon arms test. Session strand, before the stop's emit.
    static void note_engine_stop(Session& s) noexcept { s.note_engine_stop_(); }
};

}  // namespace fixpp::session
