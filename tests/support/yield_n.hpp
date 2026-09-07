// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/yield_n.hpp
//
// Yield the current coroutine back to its executor `n` times.
//
// Hoisted (#289 batch 19) from `tests/sync/sync_test_support.hpp`, which had itself
// hoisted it from 15 byte-identical copies during the 006-async-mutex `/simplify` pass.
// It moves here because `tests/support/pump_until_ready.hpp` needs it too, for
// `yield_window_then_ready`'s window loop, and the dependency edge only runs one way:
// the sync suite may reach into `tests/support/`, not the reverse.
//
// ⚠️ IT DOES NOT LIVE IN `pump_until_ready.hpp`, and the reason is the one
// `tests/support/wait_until.hpp` already states for itself: that header drags gtest, the
// Clock interface and Transport in with it, and the ~20 `tests/sync` TUs that want only
// this need four asio headers. Same shape, same remedy — a small standalone header both
// sides include. `sync_test_support.hpp` keeps the `fixpp::sync::test::yield_n` spelling
// alive as a `using`, so no existing call site changes.
//
// A third copy still exists in an anonymous namespace in
// `tests/alloc_guard/sync_alloc_guard_test.cpp`; left alone as pre-existing.

#pragma once

#include <asio/awaitable.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

namespace fixpp::test_support {

// Used by the contention/drain seams to force interleaving and to bound asio
// inline-resume recursion depth. `n <= 0` yields not at all, which is the identity a
// caller passing a computed window relies on.
inline asio::awaitable<void> yield_n(int n) {
    auto ex = co_await asio::this_coro::executor;
    for (int i = 0; i < n; ++i) co_await asio::post(ex, asio::use_awaitable);
}

}  // namespace fixpp::test_support
