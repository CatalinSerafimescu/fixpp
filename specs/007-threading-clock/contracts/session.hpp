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
    // ... full FIX FSM surface (send, callbacks, identity, Logon/gap-fill/
    // ResendRequest/sequence-reset) owned by 005, which extends THIS type ...

    // Minimal 2d-OWNED open() shape (D-4 minimal real skeleton). The FIX
    // Logon/establishment FSM is 005's; the 2d-owned obligations bound to
    // open() are: (1) executor resolution + serialisation enforcement —
    // resolve the session executor as
    //   SessionConfig::executor_override.value_or(EngineConfig::executor)
    // and feed it (with `mode`, `already_serialized_executor`, and `this`)
    // to fixpp::core::make_session_executor (§4.8:994); make_session_executor
    // is the SINGLE enforcement point for error::executor_not_serialised
    // (mode==direct_executor && !already_serialized_executor — FR-009 / I-06);
    // (2) effective_clock = SessionConfig::clock_override ?: EngineConfig::clock
    // resolved ONCE here, bound to session lifetime (FR-005 / I-03);
    // (3) populate the session_local<trace_context> slot from
    // SessionConfig::initial_trace_context (FR-014); (4) reject a null
    // dictionary / null EngineConfig::executor / default-constructed
    // security_profile sentinel / incompatible combo (e.g. direct_executor
    // + lock_policy::spin) with error::invalid_session_config (FR-018);
    // (5) reject a second open() on the same handle with
    // error::session_already_open (slot 51 / FR-018). The design doc does
    // NOT freeze an open() signature ([2d §4.7] comments the full surface as
    // 005-owned; §4.8:994 fixes only the resolution rule); this is the
    // minimal real-skeleton shape pinning the 2d-owned obligations so they
    // are testable (seam-19 open-path arm — data-model.md N5). Returns the
    // first-error of the rejection set; otherwise expected_t<void>{}.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
        open() noexcept;

    // Two-phase close ([2d §4.7]:833-834 frozen shape — [[nodiscard]] +
    // noexcept). Idempotent — signed-off THREE-STATE model
    // ([2d §4.7]:830-832,863 / [2d §6.5]:1172): calling close() on an
    // ALREADY-CLOSING (in-flight) session returns the SAME in-flight
    // awaitable as the first call (functionally a no-op for the second
    // caller), with NO error; calling close() on a NEVER-OPENED or an
    // ALREADY-CLOSED (drained) session returns
    // error::session_already_closed (§6.7). No side effects in any case.
    // Under
    // close_mode::graceful, phase 1: after the FSM's last in-flight
    // store(...) awaitable has resumed and BEFORE the Logout async_write is
    // issued, the engine invokes the engine-internal
    // FileStore::flush_for_session_close() hook once — a non-virtual,
    // non-public method on the CONCRETE FileStore (NOT on MessageStore's
    // pure-virtual interface — [2e §4.1.1]; reached via the session's
    // unique_ptr<MessageStore> friend mechanism). It drains pending
    // commit_batched / commit_interval records to durable storage so the
    // regulator-mandated tail records survive a post-close host crash; it is
    // idempotent and on a mid-flush fdatasync/FlushFileBuffers error
    // completes with expected_t::unexpected{store_io_failure}, which the
    // engine logs before proceeding with the Logout exchange. Under
    // close_mode::terminal the hook is NOT invoked (terminal fires root
    // cancellation immediately per [2d §4.7]). The concrete FileStore is
    // owned by 2e; 007 wires only the phase-1 call site and asserts the
    // 2d-owned ordering property only (D-5 / [2e §7.6] / [2e §4.1.1]).
    // Completes when BOTH phases drained: transport closed; per-message PMR
    // arenas reset; session_local<trace_context> slot cleared; per-session
    // reusable timer slot pool returned to the session arena.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
        close(close_mode = close_mode::graceful) noexcept;

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
