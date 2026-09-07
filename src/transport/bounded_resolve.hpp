// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/transport/bounded_resolve.hpp — internal transport header.
//
// A name resolution that a deadline and a cancellation can actually end (#361).
//
// THE DEFECT. Both transports used to `co_await resolver.async_resolve(...)`
// directly, and armed their `connect_timeout` timer only afterwards. asio's
// resolver takes no cancellation slot — `resolver_service::async_resolve`
// constructs a `resolve_query_op` and calls `start_resolve_op` with no
// `get_associated_cancellation_slot` anywhere on the path — so a
// `cancellation_type::total` from `Engine::stop()` reached the coroutine frame,
// was recorded, and forwarded into nothing. The await therefore ran to whatever
// the host's name-service stack decided, bounded by neither cancellation nor the
// configured `connect_timeout`.
//
// ⚠️ `resolver.cancel()` IS NOT THE FIX, and the issue's own suggestion to join
// the resolve against a timer with `operator||` is WORSE than useless. Measured
// in asio's source rather than argued:
//
//   asio/detail/impl/socket_ops.ipp — background_getaddrinfo()
//       if (cancel_token.expired())  ec = operation_aborted;
//       else                         socket_ops::getaddrinfo(...);
//
// The expired-token test runs ONCE, BEFORE the blocking call, and
// `resolver.cancel()` is only `impl.reset()` — so it wins the QUEUED window and
// nothing else. And `operator||` retires only when BOTH arms retire, so a join
// would still wait for `getaddrinfo` and merely add an arm (the same trap #359
// records in that construction).
//
// THE ONLY CONSTRUCTION THAT BOUNDS THE CALLER is to stop AWAITING the resolve.
// The op is issued with a completion handler that owns the shared state; the
// caller then waits on a local `gate` timer which the handler cancels. If the
// deadline or the caller's own cancellation wins, the caller returns and the
// resolve is ABANDONED — the handler is its last owner, so the resolver and the
// result storage stay alive for it and nothing dangles.
//
// ── PRECONDITION, and it is the caller's to meet ─────────────────────────────
//
// ⚠️ THE AWAITING FRAME AND THE COMPLETION HANDLER MUST BE SERIALISED WITH EACH
// OTHER. This is a REQUIREMENT ON THE CALLER, not a property this header can
// derive, and saying otherwise was a defect in its first draft.
//
// What the header CAN guarantee, and now does: both the resolver and the `gate`
// live on `co_await asio::this_coro::executor`, so the handler's associated
// executor IS the awaiting frame's executor (a bare completion token has no
// associated executor, so `resolve_query_op`'s `handler_work` falls back to the
// I/O object's — see `asio/detail/resolve_query_op.hpp`). The helper used to
// take that executor as a PARAMETER, which let the two differ silently.
//
// What it still cannot guarantee: that the executor SERIALISES. On a strand, or
// a single-threaded `io_context`, it does, and then the handler cannot run until
// this frame suspends — which is why no pre-wait test of `done` is needed and
// why `done`/`ec`/`endpoints` are plain members. On a bare multi-threaded
// `io_context` executor all three of those break at once: concurrent
// `async_wait` and `cancel()` on one timer (asio documents `basic_waitable_timer`
// as "Shared objects: Unsafe"), a lost wake-up that stalls the caller for the
// whole budget, and a plain-member race. That is the same serialisation rule
// every transport operation already runs under ([2d §7.6]; `timer_epoch_state.hpp`
// states it for its own plain integers; `session_executor.cpp` makes
// `direct_executor` assert it), so this helper inherits it rather than adding
// one — but it inherits it as an ASSUMPTION, and an assumption written down is
// the point of this paragraph.
//
// ── WHAT ABANDONING COSTS, AND WHY THERE IS A CAP ────────────────────────────
//
// ⚠️ ABANDONING BOUNDS THE CALLER, NOT THE RESOLVER — and left uncapped it makes
// the resolver WORSE than before this change. Two unconditional facts in asio
// 1.38, both independent of the IOCP/reactor split:
//
//   * `resolver_thread_pool::start_resolve_op` calls `scheduler_.work_started()`,
//     so an abandoned resolve still counts as OUTSTANDING WORK on the caller's
//     io_context, and `resolver_thread_pool::shutdown()` JOINS its work threads.
//     So `io_context::run()` does not return and `~io_context` does not complete
//     until the host's name-service stack gives up. Live limitation L-361-2.
//   * The pool defaults to ONE work thread (`config(context).get("resolver",
//     "threads", 0U)`, then `num_work_threads_ = 1`), and it is an
//     `execution_context_service_base` — ONE pool per io_context, shared by every
//     session on it. Blocking lookups are therefore drained SERIALLY.
//
// Before this change a wedged `getaddrinfo` blocked its own caller, so at most
// one lookup was outstanding per transport and the next reconnect attempt could
// not start until the previous lookup returned. Now the attempt retires at the
// deadline while the lookup keeps running, so retries STACK behind it — and a
// reconnect policy with `max_attempts == 0` (`defaults_quickfix_compat()`) and a
// `connect_timeout` below the host's name-service bound would enqueue them faster
// than one thread drains them.
//
// `kMaxAbandonedResolves` is the answer: once that many abandoned lookups are
// still outstanding process-wide, `resolve_bounded` REFUSES immediately with
// `transport_resolve_failed` instead of adding one more. Refusing is not a
// consolation prize — a lookup queued behind N wedged ones on a single thread
// could not have completed inside any caller's budget anyway, so the fast
// failure is the honest answer and the retry lands on the reconnect policy's
// backoff where it belongs. The cap also converts L-361-2's drain from
// "unbounded" to "at most kMaxAbandonedResolves x the host's name-service bound".
//
// ⚠️ "COULD NOT HAVE COMPLETED ANYWAY" IS CONDITIONAL ON THE ONE POOL THREAD.
// An application that raises asio's `resolver`/`threads` above 1 drains lookups
// in PARALLEL, and then a refused fifth resolve might have answered promptly —
// the cap becomes a false refusal rather than an honest one. It should be raised
// alongside that key. Left a compile-time constant rather than configuration
// because no caller has asked for the knob and the value it must track is one
// fixpp cannot read back from asio.
//
// ⚠️ THE COUNT IS PROCESS-WIDE while the pool it protects is per-io_context, so
// the cap is CONSERVATIVE: with two io_contexts it can refuse earlier than
// strictly necessary, never later. It is also deliberately not a knob — the
// lever that actually helps lives at the application's io_context construction
// (asio's `resolver`/`threads` config key), which fixpp does not own.
//
// ⚠️ ONE WAY TO UNDER-COUNT, deliberately accepted: a frame DESTROYED while
// suspended (io_context teardown — see inflight_flag_guard.hpp) never runs the
// give-up path, so its abandoned lookup is not counted. That happens only during
// teardown, when no further attempt will be made, so it cannot feed the
// accumulation this cap exists to stop.
//
// ── WITNESSES ────────────────────────────────────────────────────────────────
//
// The CONTROL FLOW here — deadline, cancellation, error mapping, abandonment
// accounting, the cap — is driven in-tree by tests/transport/test_bounded_resolve.cpp
// through `resolve_bounded_with`, which takes the operation's INITIATION as a
// parameter. A test initiation that never completes is a faithful model of a
// wedged `getaddrinfo` AS SEEN BY THIS CODE, which is all this code sees.
//
// What that cannot show is that a real wedged `getaddrinfo` behaves that way, or
// what abandoning costs the io_context: there is no process-local way to wedge
// glibc's resolver (no env override for `/etc/resolv.conf`, no per-process
// nameserver) and wedging it needs a private mount namespace a gtest cell cannot
// enter. That half is `tools/probes/resolve_bound_probe.cpp`, run by hand, which
// carries the recipe and insists on its control arm. Figures live in B-361-1 /
// L-361-2, not here — a number in a comment is a RESULT and nothing re-runs it.
#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <atomic>
#include <chrono>
#include <fixpp/core/error.hpp>
#include <memory>
#include <string>
#include <utility>

namespace fixpp::transport::detail {

// Process-wide count of resolves that were abandoned and have not yet completed.
// Incremented on the give-up path, decremented by ~bounded_resolve_state — the
// DESTRUCTOR rather than the handler body, so a handler destroyed unrun at
// io_context teardown still returns its budget.
inline std::atomic<int>& abandoned_resolve_backlog() noexcept {
    static std::atomic<int> count{0};
    return count;
}

// See the cap rationale in this file's header comment, including the condition
// this number depends on. Four, not forty: the worst-case drain is this many
// serialised name-service timeouts, and against asio's default ONE pool thread a
// fifth concurrent wedged lookup cannot start until those four return, so no
// caller's budget survives it.
inline constexpr int kMaxAbandonedResolves = 4;

// Shared state for one abandonable resolve. Owned jointly by the awaiting frame
// and the completion handler; after an abandonment the handler is the last
// owner, which is what keeps `resolver` alive under the running getaddrinfo.
struct bounded_resolve_state {
    explicit bounded_resolve_state(const asio::any_io_executor& ex) : resolver{ex}, gate{ex} {}

    ~bounded_resolve_state() {
        if (abandoned) {
            abandoned_resolve_backlog().fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    bounded_resolve_state(const bounded_resolve_state&) = delete;
    bounded_resolve_state& operator=(const bounded_resolve_state&) = delete;

    asio::ip::tcp::resolver resolver;
    asio::steady_timer gate;
    asio::ip::tcp::resolver::results_type endpoints;
    asio::error_code ec;
    bool done{false};

    // Set by the awaiting frame on the give-up path, paired with a fetch_add on
    // abandoned_resolve_backlog(). Read only by ~bounded_resolve_state, which the
    // shared_ptr release/acquire orders after that write.
    bool abandoned{false};
};

// The core, with the operation's INITIATION as a parameter so the control flow
// below has an in-tree witness. `initiate(st, completion)` must start exactly one
// operation that eventually invokes `completion(ec, endpoints)` on this frame's
// executor — or, for a test that models a wedge, never invokes it at all.
//
// Errors, all of which leave a transport retryable (`state_` untouched):
//   transport_connect_cancelled — our cancellation reached us, or the op itself
//                                 completed with operation_aborted
//   transport_connect_timeout   — `deadline` passed with the resolve still out
//   transport_resolve_failed    — the resolver answered with a failure, OR the
//                                 abandoned-resolve cap refused to start one
//
// ⚠️ THE DEADLINE IS ABSOLUTE AND SHARED WITH THE CONNECT LEG. Callers pass ONE
// `now() + connect_timeout` computed before this call and reuse it to arm the
// connect timer, so `connect_timeout` bounds `async_connect` AS A WHOLE rather
// than being spent once per phase — the same "one budget for the whole call"
// decision `close_async` documents for `tls_close_timeout`. Passing a fresh
// deadline to the connect leg would restore a 2x worst case.
template <class Initiate>
[[nodiscard]] asio::awaitable<core::expected_t<asio::ip::tcp::resolver::results_type>>
resolve_bounded_with(std::chrono::steady_clock::time_point deadline, Initiate initiate) {
    using E = core::error;

    if (abandoned_resolve_backlog().load(std::memory_order_acquire) >= kMaxAbandonedResolves) {
        co_return std::unexpected{E::transport_resolve_failed};
    }

    // THIS FRAME's executor, not one passed in — see the PRECONDITION block at
    // the top. It is what makes "the handler runs where this frame runs" a
    // property of the code rather than of the call site.
    auto st = std::make_shared<bounded_resolve_state>(co_await asio::this_coro::executor);
    st->gate.expires_at(deadline);

    // The completion owns `st` by value — never `this` and never the caller's
    // frame — so an abandoned resolve reads and writes only memory it keeps alive
    // itself. gate.cancel() is what wakes the wait below; on the abandoned path
    // there is no wait outstanding and it is a no-op.
    initiate(st, [st](const asio::error_code& ec, asio::ip::tcp::resolver::results_type endpoints) {
        st->ec = ec;
        st->endpoints = std::move(endpoints);
        st->done = true;
        st->gate.cancel();
    });

    // The gate carries BOTH exits: the completion cancels it, and this
    // coroutine's cancellation slot cancels it on a stop() — which is the whole
    // point, because the resolve op itself accepts neither. `done` below is the
    // discriminator, NOT wait_ec: a completion and a cancellation both land here
    // as operation_aborted, and an expired deadline lands as success.
    asio::error_code wait_ec;
    co_await st->gate.async_wait(asio::redirect_error(asio::use_awaitable, wait_ec));

    if (!st->done) {
        // ABANDONED. `st` outlives this frame through the completion handler;
        // charge it to the backlog until that handler (or its destruction) frees
        // the slot.
        st->abandoned = true;
        abandoned_resolve_backlog().fetch_add(1, std::memory_order_acq_rel);

        auto cs = co_await asio::this_coro::cancellation_state;
        if (cs.cancelled() != asio::cancellation_type::none) {
            co_return std::unexpected{E::transport_connect_cancelled};
        }
        co_return std::unexpected{E::transport_connect_timeout};
    }

    if (st->ec) {
        if (st->ec == asio::error::operation_aborted) {
            co_return std::unexpected{E::transport_connect_cancelled};
        }
        co_return std::unexpected{E::transport_resolve_failed};
    }

    co_return std::move(st->endpoints);
}

// Production entry point: the initiation is the real resolver.
//
// `host`/`service` are taken by value and NOT std::move'd into async_resolve:
// its parameters are string_views and it copies them into its query
// synchronously, so a move would be a no-op that reads like ownership transfer.
// By value so a caller may pass a temporary (both do: std::to_string(ep.port)).
[[nodiscard]] inline asio::awaitable<core::expected_t<asio::ip::tcp::resolver::results_type>>
resolve_bounded(std::string host, std::string service,
                std::chrono::steady_clock::time_point deadline) {
    return resolve_bounded_with(
        deadline, [host = std::move(host), service = std::move(service)](
                      const std::shared_ptr<bounded_resolve_state>& st, auto completion) {
            st->resolver.async_resolve(host, service, std::move(completion));
        });
}

}  // namespace fixpp::transport::detail
