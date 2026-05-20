// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/core/cancellable_dispatch.hpp
//
// fixpp::core::cancellable_dispatch — the project-owned reaping-aware dispatch
// primitive ([2d §6.5] / E9 / I-08; T036). Realizes
// specs/007-threading-clock/contracts/cancellable_dispatch.hpp.
//
// Header-only function template. core/ MUST NOT #include any session/ header
// ([arch §2.3] leaf rule); it uses fixpp::session::Session ONLY through the
// session_executor wrapper's typed accessor, and only the `->session_arena()`
// call requires Session complete — which it is at every instantiation site
// (the session TU and the seam tests both include <fixpp/session/session.hpp>
// before instantiating). No std::mutex here ([const §XI.3] — this header
// includes <asio/awaitable.hpp>).
//
// Deterministic THREE-CASE contract (frozen by the shape oracle):
//   (1) slot signalled BEFORE pickup  → handler REAPED (never invoked);
//       completes expected_t<void>{ unexpect, error::dispatch_aborted }
//       (→ FIXPP_ERR_CANCELLED at the C ABI).
//   (2) slot signalled DURING execution → the handler runs to its first
//       cancellable suspension point, then the slot is honoured (ASIO
//       standard — the handler is invoked under `exec`, which carries the
//       slot; a coroutine handler observes it at its next co_await).
//   (3) slot NOT signalled → behaviourally == asio::dispatch(exec, handler)
//       plus a single relaxed-atomic slot check at hand-off (≤ 5 ns);
//       completes expected_t<void>{}.
//
// T054 (US6 / D-6): HALO-ELIGIBILITY + PMR FALLBACK + EXCEPTION CONTRACT
//   HALO: this function is HEADER-ONLY and the parse→fromApp chain is a
//   SINGLE STRAND-LOCAL INVOCATION chain that clang's HALO analysis targets
//   for coroutine frame elision ([const §XI.6] / [2d §6.2]). When HALO fires,
//   the awaiter-frame heap allocation is completely elided. When HALO does
//   NOT fire (e.g., cross-thread dispatch with a non-deterministic control
//   flow from the compiler's perspective), the coroutine frame is allocated
//   by asio's default frame allocator; in that case the PMR arena is used for
//   the DISPATCH NODE and the SLOT-OBSERVATION FLAG (see ARENA DERIVATION
//   below) — not the coroutine frame itself (asio::awaitable<> frame alloc
//   is opaque to the session PMR layer). In the SESSION context the measured
//   global-heap zero-alloc property is satisfied because: (a) in-strand HALO
//   fires, and (b) cross-strand post uses a bind_allocator PMR hint at the
//   asio::dispatch call site (seam 7 / T051 mallocnesia guard confirms).
//   EXCEPTION CONTRACT: every potentially-throwing internal call is fenced
//   by a catch(bad_alloc) → strand_dispatch_failed_oom projection, so no
//   exception crosses the parse→fromApp window ([arch §5.3]). This mirrors
//   the `fixpp::core::detail::trap_throw` pattern from 001/004 (D-6).
//
// ARENA DERIVATION (pinned — [2d §6.5]:1153-1154): the dispatch node is
// allocated from the session PMR arena recovered THROUGH `exec`
// (`exec.session_ptr()->session_arena()` — the [2d §4.5] never-null chain).
// The resource never falls back to the global heap, so arena exhaustion
// surfaces as error::strand_dispatch_failed_oom (slot 50), never a silent
// global-heap fallback (seam 7's alloc guard fails the build otherwise).
#pragma once

#include <atomic>
#include <exception>
#include <memory>
#include <memory_resource>
#include <type_traits>
#include <utility>

#include <asio/awaitable.hpp>
#include <asio/bind_allocator.hpp>
#include <asio/bind_executor.hpp>
#include <asio/cancellation_signal.hpp>   // asio::cancellation_slot/type
#include <asio/dispatch.hpp>
#include <asio/use_awaitable.hpp>

#include <fixpp/core/error.hpp>            // expected_t, error
#include <fixpp/core/session_executor.hpp>

namespace fixpp::core {

namespace detail {

// True iff T is asio::awaitable<...> (so a coroutine handler is co_await-ed
// rather than treated as a completed void call — supports BOTH the plain
// callback shape and a coroutine application callback).
template <class T>          struct is_awaitable                       : std::false_type {};
template <class U, class E> struct is_awaitable<asio::awaitable<U, E>> : std::true_type  {};

}  // namespace detail

template <typename Handler>
[[nodiscard]] asio::awaitable<expected_t<void>>
cancellable_dispatch(session_executor exec,
                      asio::cancellation_slot slot,
                      Handler&& handler) {
    // [2d §4.5] never-null chain THROUGH the wrapper, via the session-TU
    // bridge (core stays a leaf — [arch §2.3]; the raw
    // exec.session_ptr()->session_arena() is non-dependent and would force
    // Session complete in this core header). The resource itself performs no
    // global-heap fallback; arena exhaustion is reported, not silently
    // absorbed (contract violation otherwise — seam 7).
    std::pmr::memory_resource* arena = session_arena_of(exec);

    // Slot observation flag lives in the SESSION arena (dispatch-node
    // allocation — no global heap). On exhaustion: strand_dispatch_failed_oom
    // (slot 50), never a silent fallback. Allocated before any co_await so a
    // plain try/catch suffices.
    std::shared_ptr<std::atomic<bool>> cancelled;
    try {
        cancelled = std::allocate_shared<std::atomic<bool>>(
            std::pmr::polymorphic_allocator<std::atomic<bool>>{arena}, false);
    } catch (const std::bad_alloc&) {
        co_return std::unexpected(error::strand_dispatch_failed_oom);
    }

    if (slot.is_connected()) {
        slot.assign([cancelled](asio::cancellation_type) noexcept {
            cancelled->store(true, std::memory_order_relaxed);
        });
    }

    // Hop onto the session executor. The dispatch node is allocated from the
    // session arena via the bound allocator (never the global heap). If the
    // arena is exhausted asio's allocation throws; convert to the contract
    // error rather than letting it escape this noexcept-equivalent seam.
    std::pmr::polymorphic_allocator<std::byte> alloc{arena};
    try {
        co_await asio::dispatch(asio::bind_allocator(
            alloc, asio::bind_executor(exec, asio::use_awaitable)));
    } catch (const std::bad_alloc&) {
        co_return std::unexpected(error::strand_dispatch_failed_oom);
    }

    // Case 1: signalled BEFORE pickup → reap (handler NEVER invoked). One
    // relaxed-atomic read (≤ 5 ns — the case-3 cost bound).
    if (cancelled->load(std::memory_order_relaxed)) {
        co_return std::unexpected(error::dispatch_aborted);
    }

    // Case 2 (signalled DURING → the handler's own co_await checkpoints
    // honour the slot, ASIO standard) / Case 3 (not signalled): invoke.
    if constexpr (detail::is_awaitable<
                      std::invoke_result_t<Handler>>::value) {
        co_await std::forward<Handler>(handler)();
    } else {
        std::forward<Handler>(handler)();
    }
    co_return expected_t<void>{};
}

}  // namespace fixpp::core
