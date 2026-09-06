// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/transport/bounded_resolve.hpp — internal transport header.
//
// A name resolution that a deadline and a cancellation can actually end (#361).
//
// THE DEFECT. Both transports used to `co_await resolver.async_resolve(...)`
// directly, and armed their `connect_timeout` timer only afterwards. asio's
// resolver takes no cancellation slot — `resolver_service`'s `resolve_query_op`
// is constructed and scheduled without obtaining one, unlike the raw socket
// path — so a `cancellation_type::total` from `Engine::stop()` reached the
// coroutine frame, was recorded, and forwarded into nothing. The await
// therefore ran to whatever the host's name-service stack decided, bounded by
// neither cancellation nor the configured `connect_timeout`.
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
// ⚠️ WHAT ABANDONING DOES **NOT** BUY — do not overclaim this header. Two
// unconditional facts in asio 1.38, both independent of the IOCP/reactor split:
// `resolver_thread_pool::start_resolve_op` calls `scheduler_.work_started()`, so
// an abandoned resolve still counts as OUTSTANDING WORK on the caller's
// io_context; and `resolver_thread_pool::shutdown()` JOINS its work threads. So
// this bounds THE OPERATION AND ITS CALLER — `io_context::run()` still does not
// return, and `~io_context` still blocks, until the host's name-service stack
// gives up. Live limitation **L-361-2**; bounding that half too would mean
// leaving asio's resolver for a thread fixpp detaches, deliberately not done.
//
// ⚠️ NO IN-TREE CELL DRIVES THE ABANDONMENT, and that is a limit of the
// PLATFORM, not an oversight: there is no process-local way to wedge glibc's
// resolver (no env override for `/etc/resolv.conf`, no per-process nameserver),
// so a cell could only fake a never-completing op — which would assert a
// property of `steady_timer`, not of this path. Re-derive both halves with
// `tools/probes/resolve_bound_probe.cpp`, which carries the recipe and insists on
// its control arm; the figures live in B-361-1 / L-361-2, not here, because a
// number in a comment is a RESULT and nothing ever re-runs it.
//
// STRAND CONFINEMENT IS LOAD-BEARING. `done` / `ec` / `endpoints` are plain
// members, read by the caller's frame and written by the completion handler.
// Both run on `exec`: the handler's associated executor defaults to the
// resolver's, which is `exec`, and every transport operation is confined to the
// transport's own executor — the rule `timer_epoch_state.hpp` already states
// for its own plain integers. That confinement is also what makes the single
// pre-wait test unnecessary: the handler cannot run until this frame suspends.
#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <fixpp/core/error.hpp>
#include <memory>
#include <string>
#include <utility>

namespace fixpp::transport::detail {

// Shared state for one abandonable resolve. Owned jointly by the awaiting frame
// and the completion handler; after an abandonment the handler is the last
// owner, which is what keeps `resolver` alive under the running getaddrinfo.
struct bounded_resolve_state {
    explicit bounded_resolve_state(const asio::any_io_executor& ex) : resolver{ex}, gate{ex} {}

    asio::ip::tcp::resolver resolver;
    asio::steady_timer gate;
    asio::ip::tcp::resolver::results_type endpoints;
    asio::error_code ec;
    bool done{false};
};

// Resolve `host`/`service`, giving up at `deadline` or on this coroutine's own
// cancellation, whichever comes first.
//
// Errors, all of which leave the transport retryable (state_ untouched):
//   transport_connect_cancelled — our cancellation reached us, or the op itself
//                                 completed with operation_aborted
//   transport_connect_timeout   — `deadline` passed with the resolve still out
//   transport_resolve_failed    — the resolver answered, with a failure
//
// ⚠️ THE DEADLINE IS ABSOLUTE AND SHARED WITH THE CONNECT LEG. Callers pass
// ONE `now() + connect_timeout` computed before this call and reuse it to arm
// the connect timer, so `connect_timeout` bounds `async_connect` AS A WHOLE
// rather than being spent once per phase — the same "one budget for the whole
// call" decision `close_async` documents for `tls_close_timeout`. Passing a
// fresh deadline to the connect leg would restore a 2x worst case.
[[nodiscard]] inline asio::awaitable<core::expected_t<asio::ip::tcp::resolver::results_type>>
resolve_bounded(const asio::any_io_executor& exec, std::string host, std::string service,
                std::chrono::steady_clock::time_point deadline) {
    using E = core::error;

    auto st = std::make_shared<bounded_resolve_state>(exec);
    st->gate.expires_at(deadline);

    // The handler owns `st` by value — never `this` and never the caller's
    // frame — so an abandoned resolve reads and writes only memory it keeps
    // alive itself. gate.cancel() is what wakes the wait below; on the
    // abandoned path there is no wait outstanding and it is a no-op.
    // NOT std::move'd: async_resolve's parameters are string_views and it copies
    // them into its query synchronously, so a move here would be a no-op that
    // reads like ownership transfer. The by-value std::string parameters exist so
    // the caller may pass a temporary (both do: std::to_string(ep.port)).
    st->resolver.async_resolve(
        host, service,
        [st](const asio::error_code& ec, asio::ip::tcp::resolver::results_type endpoints) {
            st->ec = ec;
            st->endpoints = std::move(endpoints);
            st->done = true;
            st->gate.cancel();
        });

    // The gate carries BOTH exits: the handler cancels it on completion, and
    // this coroutine's cancellation slot cancels it on a stop() — which is the
    // whole point, because the resolve op itself accepts neither.
    // `done` below is the discriminator, NOT wait_ec: a completion and a
    // cancellation both land here as operation_aborted, and an expired deadline
    // lands as success.
    asio::error_code wait_ec;
    co_await st->gate.async_wait(asio::redirect_error(asio::use_awaitable, wait_ec));

    if (!st->done) {
        // ABANDONED. `st` outlives this frame through the handler.
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

}  // namespace fixpp::transport::detail
