// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/006-async-mutex/contracts/completion_policy.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// SHAPE ORACLE (NOT THE BUILD HEADER)
// This file is a planning-phase contract artifact.
// It specifies the exact types and names that the implementation MUST match.
// It is NOT compiled into the library.
//
// Build header target: include/fixpp/core/sync/async_mutex.hpp
//   (the completion_policy enum is defined in the fixpp::sync namespace within
//   the main async_mutex header)
// ─────────────────────────────────────────────────────────────────────────────
//
// Source of truth: .specify/2f-async-mutex.md v1.5 §4.1
//
// Per-mutex completion policy. Governs the inline-vs-post behaviour of
// unlock()'s drain handoff (item 4 of [SYN §3.2 Q6b]'s six-item list).
// The default is `dispatch`; HFT / fairness-sensitive sites pick `post`.
//
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <cstdint>

namespace fixpp::sync {

// completion_policy — per-mutex enum. Immutable after construction.
// Set via async_mutex(completion_policy cp) explicit ctor (second ctor form).
// Queried via async_mutex::policy() noexcept.
//
// Not a plugin (zero pure-virtual; not a base class; not extensible per 2f §1).
enum class completion_policy : std::uint8_t {
    // ASIO `dispatch` semantics on the bound executor: if
    // executor.running_in_this_thread() is true at unlock time, the resumed
    // coroutine runs inline on the unlocking thread; if false, the resumption
    // posts. Under `direct_executor` mode + a user-attested executor that does
    // not publish running_in_this_thread(), the predicate falls to
    // always-post.
    //
    // Default; matches the [2d §7.4] surface lock.
    // §9 seam #12: verified against `post` for ≤ 0 ns extra vs ≈ 25 ns extra.
    dispatch = 0,

    // Always post the resumed coroutine through the bound executor; one
    // executor hop per resumption regardless of caller thread.
    // Selected by HFT / fairness-sensitive sites where no inline resumption
    // is acceptable.
    post = 1,
};

}  // namespace fixpp::sync

// ─────────────────────────────────────────────────────────────────────────────
// Usage contract:
//   async_mutex m{};                              // dispatch (default)
//   async_mutex m{completion_policy::post};       // always-post
//   completion_policy p = m.policy();             // query (noexcept, const)
// ─────────────────────────────────────────────────────────────────────────────
