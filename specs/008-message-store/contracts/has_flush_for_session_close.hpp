// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/008-message-store/contracts/has_flush_for_session_close.hpp
//
// SHAPE ORACLE — declaration-only contract for the
// fixpp::session::detail::has_flush_for_session_close concept. Anchor:
// .specify/2e-msgstore.md v0.4 §7.6 (round-2 RC#2 close per Codex C-R2-P1-5;
// Opus N3-P2-1 concept-shaped-non-virtual close). FR-028 / FR-029. I-17.
// Research D-11.
//
// Engine-internal concept. Gated by a factory-type tag retained at session
// open (NOT RTTI / NOT dynamic_cast). FileStore defines
// flush_for_session_close(); MemoryStore does not (the concept fails on it,
// engine skips the call); user impls inherit the no-op default for free.
//
// The hook runs under Session::close(graceful) outside phase-1's child
// timeout; NOT invoked under Session::close(terminal) per Appendix D §D.2.
// Returns expected_t<void>{} on success or store_io_failure on mid-flush
// error; does NOT surface store_cancelled under graceful close.
#pragma once

#include <concepts>

#include <asio/awaitable.hpp>

#include <fixpp/core/error.hpp>           // expected_t

namespace fixpp::session::detail {

template <class S>
concept has_flush_for_session_close = requires(S& s) {
    { s.flush_for_session_close() } -> std::same_as<
        asio::awaitable<fixpp::core::expected_t<void>>>;
};

}  // namespace fixpp::session::detail
