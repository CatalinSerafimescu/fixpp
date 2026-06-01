// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/async_lock_via_session_executor.hpp
//
// Thin session-layer helper — declaration only.
//
// ─────────────────────────────────────────────────────────────────────────────
// BSL-1.0 algorithm attribution (per [const §V.4]):
//   See include/fixpp/core/sync/async_mutex.hpp for the attribution block.
//   This helper is a layering shim; the algorithm is in async_mutex.
// ─────────────────────────────────────────────────────────────────────────────
//
// LAYERING NOTE (RC#2 fix — critical boundary):
//
//   core::async_mutex carries ZERO dependency on Session / EngineConfig.
//   It operates on the caller-supplied std::pmr::memory_resource* `mr`.
//
//   This helper is the glue that core::async_mutex does NOT own:
//   - Lives in include/fixpp/session/ (downstream of core/ per [arch §2.3]).
//   - DECLARED by 2f at sign-off (this file).
//   - IMPLEMENTED by the session-module spec, NOT this feature.
//     (design-doc §1.2 / §4.3.2: the session-module spec ships the body;
//      2e is 2f's first consumer, not licensed to ship this body.)
//   - Recovers the per-session PMR resource from the awaiter's bound executor
//     and forwards into async_lock(mr).
//
//   Declaration only — NOT implemented here.
//
// Design anchor: .specify/2f-async-mutex.md v1.6 §4.3.2
// Shape oracle:  specs/006-async-mutex/contracts/async_lock_via_session_executor.hpp
// ─────────────────────────────────────────────────────────────────────────────
//
// Note (I1): sync_lock_outside_session (slot 45) is a declaration-contract
// error variant only — no runtime path is implemented in this feature.
// Per SC-009 declaration-only benignity clause, it is NOT coverage-applicable
// (do not flag it uncovered).
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

// The build header gets asio/awaitable.hpp and fixpp/core/error.hpp transitively
// from async_mutex.hpp; we use the same forward-declaration approach as the
// shape oracle (RC#2 oracle-fidelity policy: literal declaration,
// forward-declared dependencies).
#include "fixpp/core/sync/async_mutex.hpp"

namespace fixpp::session {

// async_lock_via_session_executor — thin session-layer helper.
//
// Acquires `m` by recovering the per-session PMR resource from the awaiter's
// bound executor and forwarding into m.async_lock(mr).
//
// EXACT declaration per design-doc §4.3.2 (lines 843-844):
//   [[nodiscard]] asio::awaitable<expected_t<fixpp::sync::async_lock_guard>>
//       async_lock_via_session_executor(fixpp::sync::async_mutex& m) noexcept;
//
// Errors (all defined in include/fixpp/core/error.hpp at slots 43-46):
//   sync_lock_aborted         (=43) — cancellation won the §4.5 CAS race.
//   sync_lock_alloc_failed    (=44) — session arena exhausted.
//   sync_lock_outside_session (=45) — bound executor is NOT a session_executor.
//   sync_lock_drained         (=46) — mutex is in the drained state.
//
// [[nodiscard]]: the caller MUST check the expected_t; discarding a lock
// acquisition is a logic error.
[[nodiscard]] asio::awaitable<fixpp::sync::expected_t<fixpp::sync::async_lock_guard>>
async_lock_via_session_executor(fixpp::sync::async_mutex& m) noexcept;

}  // namespace fixpp::session
