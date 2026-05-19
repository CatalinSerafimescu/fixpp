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
// Source of truth: .specify/2f-async-mutex.md v1.5 §4.1, AS AMENDED BY
//                  v1.6 Erratum E-3 (§4.6.2; recorded post-sign-off at
//                  /implement; re-touches 006 Gate A scope).
//
// Per-mutex completion policy (item 4 of [SYN §3.2 Q6b]'s six-item list).
//
// ── E-3 (§4.6.2, v1.6, TSan-diagnosed) ──  2f's waiter-resumption site is
//   INTRINSICALLY RE-ENTRANT (always inside unlock()/on_cancel(), always
//   nested in a coroutine on the bound-executor thread), so asio::dispatch's
//   inline fast-path is NEVER safe here (re-entrant inline resume = heap-UAF).
//   2f WAITER RESUMPTION IS ALWAYS post-ed to the bound executor — never
//   inline-dispatched — REGARDLESS of completion_policy or
//   running_in_this_thread(). completion_policy is preserved ONLY as a
//   semantic/intent knob (constructor-set, const, queryable via policy());
//   for waiter resumption BOTH `dispatch` and `post` post. The
//   "inline if running_in_this_thread()" predicate is superseded for waiter
//   resumption (it would still apply to a non-reentrant completion context,
//   of which 2f has none).
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
    // Semantic/intent default; matches the [2d §7.4] surface lock.
    //
    // E-3: for 2f WAITER RESUMPTION this ALWAYS post-s — the
    // "inline if executor.running_in_this_thread()" asio::dispatch fast-path
    // is SUPERSEDED here (re-entrant inline resume was a TSan-diagnosed
    // heap-UAF on US1 sync_fifo_fairness / US2 sync_cancellation_mid_wait).
    // The knob is retained for intent/queryability (policy()); it does not
    // re-enable inline resumption for 2f.
    // §9 seam #12 asserts mutual-exclusion + completion under BOTH policies
    // (not inline-ness); seam #13 asserts at-most-one post hop (exactly one).
    dispatch = 0,

    // Always post the resumed coroutine through the bound executor; one
    // executor hop per resumption regardless of caller thread.
    // E-3: this was already the always-post behaviour 2f waiter resumption
    // now uses under BOTH policies. Selected by HFT / fairness-sensitive
    // sites for explicit-intent symmetry.
    post = 1,
};

}  // namespace fixpp::sync

// ─────────────────────────────────────────────────────────────────────────────
// Usage contract:
//   async_mutex m{};                              // dispatch (default)
//   async_mutex m{completion_policy::post};       // always-post
//   completion_policy p = m.policy();             // query (noexcept, const)
// ─────────────────────────────────────────────────────────────────────────────
