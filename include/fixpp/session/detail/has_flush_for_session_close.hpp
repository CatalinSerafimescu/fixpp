// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/detail/has_flush_for_session_close.hpp
//
// fixpp::session::detail::has_flush_for_session_close — engine-internal
// concept selecting impls that opt in to a graceful-close drain hook.
//
// Anchor: .specify/2e-msgstore.md v0.5 §7.6 (round-2 RC#2 close per Codex
// C-R2-P1-5; Opus N3-P2-1 concept-shaped-non-virtual close). FR-028 /
// FR-029 / I-17. Entity E11. Research D-11.
//
// Engine-internal. Gated by a factory-type tag retained at session open
// via MessageStore::flush_thunk_for<Self>() (NOT RTTI / NOT dynamic_cast).
// FileStore defines flush_for_session_close() — the concept matches and the
// engine resolves the typed thunk at session open; MemoryStore does NOT
// define it — the thunk is nullptr and the engine skips the close-path call
// per Appendix D §D.2. User impls inherit the no-op default for free.
//
// The hook runs under Session::close(graceful) outside phase-1's child
// timeout; NOT invoked under Session::close(terminal). Returns
// expected_t<void>{} on success or store_io_failure on mid-flush error;
// does NOT surface store_cancelled under graceful close.
//
// Mirror of specs/008-message-store/contracts/has_flush_for_session_close.hpp
// (engine-internal — the concept is the canonical documentation form
// referenced by message_store.hpp's flush_thunk_for<Self>() per FR-028).
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
