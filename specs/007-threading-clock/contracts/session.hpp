// SPDX-License-Identifier: AGPL-3.0-or-later
// SHAPE ORACLE — NOT the build header. Minimal real Session SKELETON
// (Clarifications 2026-05-19 Q1 / D-4). 2d-OWNED surface ONLY — NO FIX FSM
// (Logon/gap-fill/ResendRequest/sequence-reset/heartbeat live in 005, which
// extends THIS type). [2d §4.5]/§4.6/§4.7.
#pragma once
#include "session_local.hpp"
#include "trace_context.hpp"
#include <asio/awaitable.hpp>
#include <cstdint>
#include <memory_resource>

namespace fixpp::core { template <class T> class expected_t; }

namespace fixpp::session {

// graceful: phase 1 (Logout exchange under a CHILD asio::cancellation_state
// below the root) → phase 2 (root cancellation_type::total). terminal:
// skip phase 1. partial is NOT in the v1.0 surface (N-P1-3).
enum class close_mode : std::uint8_t { graceful = 0, terminal = 1 };

class Session {
public:
    // ... full FSM surface (open, send, callbacks, identity) owned by 005 ...

    // Two-phase close. Idempotent: second / never-opened / already-closed
    // call → error::session_already_closed, no side effects. Completes when
    // BOTH phases drained: transport closed; per-message PMR arenas reset;
    // session_local<trace_context> slot cleared; per-session reusable timer
    // slot pool returned to the session arena.
    asio::awaitable<fixpp::core::expected_t<void>> close(close_mode = close_mode::graceful);

    // ENGINE-INTERNAL accessor (callable from fixpp::session/ ONLY — [arch
    // §2.3] leaf rule; consumed by the merged-006 session-side helper
    // async_lock_via_session_executor per [2f App D §D.1]). noexcept; NEVER
    // null — the ctor pre-conditions a non-null session_arena via the
    // [2d §4.5] resolution chain (SessionConfig::session_arena ?:
    // EngineConfig::default_session_resource ?: std::pmr::
    // get_default_resource()); frozen at session open, never swaps
    // mid-session, so the never-null contract holds for the session lifetime.
    [[nodiscard]] std::pmr::memory_resource* session_arena() const noexcept;

private:
    fixpp::core::session_local<fixpp::otel::trace_context> trace_slot_;  // [2d §4.6]
    // ... session_executor binding, root cancellation_state, etc. ...
};

}  // namespace fixpp::session
