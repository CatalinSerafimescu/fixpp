// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/support/session_executor_of.hpp — test-only.
//
// The executor a cell spawns a directly driven Session's coroutines on. Session::open()
// binds the session's executor (under per_session_strand, a strand over the configured
// executor); on_inbound_frame(), send() and close() are documented as running there
// (session.hpp, "session-strand"), and the Session's own detached work, its liveness loop,
// is spawned on the executor of the coroutine that starts it. So once open() has bound it,
// a cell spawns on Session::executor().underlying(); before that, on `fallback`, the
// executor the session is configured with.
//
// Feature 103 (research D5′; tasks T061): replay_gate checks, in a build with assertions,
// that every acquire runs on the session's strand.
#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/io_context.hpp>
#include <fixpp/session/session.hpp>

namespace fixpp::test_support {

[[nodiscard]] inline asio::any_io_executor session_executor_of(
    fixpp::session::Session const& s, asio::any_io_executor const& fallback) {
    auto const& bound = s.executor().underlying();
    if (bound) return bound;
    return fallback;
}

[[nodiscard]] inline asio::any_io_executor session_executor_of(fixpp::session::Session const& s,
                                                               asio::io_context& ioc) {
    return session_executor_of(s, asio::any_io_executor{ioc.get_executor()});
}

}  // namespace fixpp::test_support
