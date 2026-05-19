#pragma once

// Shared test-only helpers for the sync_* (async_mutex) seam suite.
// Hoisted from 15 byte-identical copies during the 006-async-mutex
// /simplify pass — test harness only, never linked into the primitive.

#include <asio/awaitable.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

namespace fixpp::sync::test {

// Yield the current coroutine back to its executor `n` times. Used by the
// contention/drain seams to force interleaving and to bound asio
// inline-resume recursion depth.
inline asio::awaitable<void> yield_n(int n) {
    auto ex = co_await asio::this_coro::executor;
    for (int i = 0; i < n; ++i)
        co_await asio::post(ex, asio::use_awaitable);
}

}  // namespace fixpp::sync::test
