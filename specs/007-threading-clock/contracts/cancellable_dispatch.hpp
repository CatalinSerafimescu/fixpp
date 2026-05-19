// SPDX-License-Identifier: AGPL-3.0-or-later
// SHAPE ORACLE — NOT the build header. [2d §6.5] project-owned reaping-aware
// dispatch primitive.
#pragma once
#include "session_executor.hpp"
#include <asio/awaitable.hpp>
#include <asio/cancellation_signal.hpp>  // asio::cancellation_slot

namespace fixpp::core {

template <class T> class expected_t;  // fwd

// Posts `handler` to the session executor `exec` such that:
//   (1) slot signalled BEFORE pickup → handler REAPED (not invoked);
//       awaitable completes expected_t<void>{ unexpect,
//       error::dispatch_aborted } (→ FIXPP_ERR_CANCELLED at the C ABI).
//   (2) slot signalled DURING execution → runs to its first cancellable
//       suspension point, then the slot is honoured (ASIO standard
//       "next co_await checkpoint observes cancellation").
//   (3) slot NOT signalled → behaviourally == asio::dispatch(exec,handler)
//       plus a single relaxed-atomic slot check at hand-off (≤ 5 ns);
//       awaitable completes expected_t<void>{}.
// First parameter is the project-owned session_executor wrapper (round 3
// root cause #1) so the primitive uniformly accepts BOTH per_session_strand
// (strand-wrapped) and direct_executor (bare attested) shapes.
//
// ARENA DERIVATION (pinned — [2d §6.5]:1153-1154): the dispatch node is
// allocated from the session PMR arena recovered THROUGH `exec`, i.e. from
// `exec.session_ptr()->session_arena()` (the [2d §4.5] resolution chain,
// never-null). Touching the global heap on the dispatch path is a CONTRACT
// VIOLATION surfaced as error::strand_dispatch_failed_oom (slot 50) on
// arena exhaustion, never a silent global-heap fallback — seam 7's alloc
// guard fails the build otherwise. The signature carries no separate arena
// parameter precisely because the arena is reachable via session_ptr();
// an implementation taking the global heap while matching this signature is
// non-conformant.
template <typename Handler>
[[nodiscard]] asio::awaitable<expected_t<void>>
cancellable_dispatch(session_executor exec,
                      asio::cancellation_slot slot,
                      Handler&& handler);

}  // namespace fixpp::core
