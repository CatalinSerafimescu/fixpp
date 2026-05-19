// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/006-async-mutex/contracts/async_lock_via_session_executor.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// SHAPE ORACLE (NOT THE BUILD HEADER)
// This file is a planning-phase contract artifact.
// It specifies the exact declaration that the implementation MUST match.
// It is NOT compiled into the library.
//
// Build header target: include/fixpp/session/async_lock_via_session_executor.hpp
//   Namespace: fixpp::session (NOT fixpp::sync — layering rule)
// ─────────────────────────────────────────────────────────────────────────────
//
// Source of truth: .specify/2f-async-mutex.md v1.5 §4.3.2
//
// ─────────────────────────────────────────────────────────────────────────────
// LAYERING NOTE (RC#2 fix — critical boundary; must be crisp everywhere):
//
//   core::async_mutex carries ZERO dependency on Session / EngineConfig.
//   It operates on the caller-supplied std::pmr::memory_resource* `mr`.
//
//   This helper is the glue that core::async_mutex does NOT own:
//   - It lives in include/fixpp/session/ (downstream of core/ per [arch §2.3]).
//   - It is DECLARED by 2f at sign-off.
//   - It is IMPLEMENTED by the session-module spec, NOT this feature
//     (design-doc §1.2 / §4.3.2: the session-module spec ships the
//     implementation; 2e is 2f's first *consumer*, not licensed to ship
//     this body).
//   - It recovers the per-session resource from the awaiter's bound executor
//     and forwards into async_lock(mr).
//
//   The 2f planning bundle (this contracts/ directory) documents the
//   declaration shape ONLY. The implementation body ships when the session-
//   module spec lands.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

// Build header includes these properly; this oracle forward-declares the
// minimum so the EXACT signature is stated literally (RC#2 oracle-fidelity
// policy — literal declaration, forward-declared dependencies; matches the
// build header's own forward-declaration approach):
// #include <asio/awaitable.hpp>
// #include "fixpp/core/error.hpp"
// #include "fixpp/sync/async_mutex.hpp"      (for async_mutex, async_lock_guard)
#include <expected>

namespace asio {
class any_io_executor;
template <class T, class Executor = any_io_executor> class awaitable;
}  // namespace asio
namespace fixpp::core { enum class error : int; }

namespace fixpp::sync {
class async_mutex;
class async_lock_guard;
}  // namespace fixpp::sync

namespace fixpp::sync {
// expected_t<T> = fixpp::core::expected_t<T> = std::expected<T, core::error>;
// declared (not erased) so the helper's return type is stated EXACTLY as in
// design-doc §4.3.2 (lines 843–844).
template <class T>
using expected_t = std::expected<T, fixpp::core::error>;
}

namespace fixpp::session {

// async_lock_via_session_executor — thin session-layer helper.
//
// Acquires `m` by recovering the per-session PMR resource from the awaiter's
// bound executor and forwarding into m.async_lock(mr).
//
// Algorithm (implementation responsibility of the session-module spec):
//   1. co_await asio::this_coro::executor — recover the awaiter's bound executor.
//   2. Cast to fixpp::core::session_executor (per [2d §4.8]).
//   3. session_executor::session_ptr() — yields Session*.
//   4. Session::session_arena() noexcept -> std::pmr::memory_resource*
//      (published by Appendix D §D.1 at 2f sign-off; engine-internal scope).
//   5. co_await m.async_lock(arena) — forward the result.
//
// Errors:
//   sync_lock_aborted         (=43) — cancellation won the §4.5 CAS race.
//   sync_lock_alloc_failed    (=44) — session arena exhausted.
//   sync_lock_outside_session (=45) — bound executor is NOT a session_executor
//                                     (bootstrap, listener, control-plane).
//                                     Caller should use async_lock(mr) directly.
//   sync_lock_drained         (=46) — mutex is in the drained state.
//
// No v1.0 hot path returns sync_lock_outside_session (all v1.0 use cases
// acquire from inside a session).
//
// [[nodiscard]]: the caller MUST check the expected_t; discarding a lock
// acquisition is a logic error.
//
// EXACT declaration per design-doc §4.3.2 (lines 843–844) — literal, NOT a
// placeholder (RC#2 oracle-fidelity close — this file claims exactness at the
// header banner, so it MUST be exact).
[[nodiscard]] asio::awaitable<expected_t<fixpp::sync::async_lock_guard>>
    async_lock_via_session_executor(fixpp::sync::async_mutex& m) noexcept;

}  // namespace fixpp::session

// ─────────────────────────────────────────────────────────────────────────────
// Canonical call site (MemoryStore::store pattern from [2f §7.1]):
//
//   asio::awaitable<expected_t<void>> MemoryStore::store(...) noexcept {
//       auto guard_or_err =
//           co_await fixpp::session::async_lock_via_session_executor(writer_mutex_);
//       if (!guard_or_err)
//           co_return std::unexpected(map_to_store_error(guard_or_err.error()));
//       auto guard = std::move(*guard_or_err);  // RAII; releases on scope exit.
//       // ... store body ...
//       co_return {};
//   }
//
// Alternatively, if the caller already has the `mr` in scope, use the
// lower-level form directly:
//   co_await mutex_.async_lock(mr)
// ─────────────────────────────────────────────────────────────────────────────
