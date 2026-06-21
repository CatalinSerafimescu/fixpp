#pragma once

// Shared test-only helpers for the sync_* (async_mutex) seam suite.
// Hoisted from 15 byte-identical copies during the 006-async-mutex
// /simplify pass — test harness only, never linked into the primitive.

#include <asio/awaitable.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <thread>

namespace fixpp::sync::test {

// Yield the current coroutine back to its executor `n` times. Used by the
// contention/drain seams to force interleaving and to bound asio
// inline-resume recursion depth.
inline asio::awaitable<void> yield_n(int n) {
    auto ex = co_await asio::this_coro::executor;
    for (int i = 0; i < n; ++i) co_await asio::post(ex, asio::use_awaitable);
}

// Bounded poll on a predicate; returns false on deadline. The drain-reap
// witnesses NEVER block-join (an orphaned waiter would hang the lane forever) —
// they poll the completion counters under an internal self-deadline so a lost
// wake surfaces as a fast, attributable FAIL. Hoisted here (047 /simplify) so
// the blockers + latch-publish witnesses share one copy.
template <typename Pred>
bool wait_until(Pred pred, std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

}  // namespace fixpp::sync::test
