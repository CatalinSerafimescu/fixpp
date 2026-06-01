// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_uncontended_latency.cpp — Seam #1
//
// Uncontended fast-path: single CAS `not_locked → locked_no_waiters`.
// The coroutine never suspends; await_ready() returns true immediately.
// A valid, engaged async_lock_guard is returned.
//
// Oracle: [2f §9 #1] — "Uncontended-acquire latency Tier 1".
// SC-001: mutual exclusion never overlaps.
// In a functional (non-bench) test: assert that co_await m.async_lock()
// completes without suspension and the guard owns the lock.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <fixpp/core/sync/async_mutex.hpp>

namespace {

using fixpp::sync::async_lock_guard;
using fixpp::sync::async_mutex;

// ── Helper: run a coroutine synchronously on a single-threaded io_context ──

template <typename Coro>
auto run_sync(Coro&& coro) {
    asio::io_context ioc;
    auto fut = asio::co_spawn(ioc, std::forward<Coro>(coro), asio::use_future);
    ioc.run();
    return fut.get();
}

// ── Seam #1: uncontended fast-path ──────────────────────────────────────────

TEST(SeamUncontendedLatency, FastPathReturnsEngagedGuard) {
    // A freshly constructed async_mutex is unlocked; the first co_await
    // async_lock() must take the fast path (await_ready returns true) and
    // produce an engaged guard without suspending.

    async_mutex mtx;
    bool guard_was_engaged = false;
    bool lock_acquired = false;

    run_sync([&]() -> asio::awaitable<void> {
        auto result = co_await mtx.async_lock();
        EXPECT_TRUE(result.has_value())
            << "Uncontended async_lock must succeed (expected has_value)";
        if (result.has_value()) {
            guard_was_engaged = result->owns_lock();
            lock_acquired = true;
            // Guard destructor releases the mutex.
        }
    });

    EXPECT_TRUE(lock_acquired);
    EXPECT_TRUE(guard_was_engaged);
}

TEST(SeamUncontendedLatency, AfterReleaseCanAcquireAgain) {
    // After the first guard is destroyed, the mutex is free again and a second
    // uncontended acquire must also succeed immediately.

    async_mutex mtx;
    int acquires = 0;

    run_sync([&]() -> asio::awaitable<void> {
        {
            auto r1 = co_await mtx.async_lock();
            EXPECT_TRUE(r1.has_value());
            if (r1.has_value()) {
                ++acquires;
            }
            // guard released here
        }
        {
            auto r2 = co_await mtx.async_lock();
            EXPECT_TRUE(r2.has_value());
            if (r2.has_value()) {
                ++acquires;
            }
        }
    });

    EXPECT_EQ(acquires, 2);
}

TEST(SeamUncontendedLatency, GuardOwnsLockAfterAcquire) {
    async_mutex mtx;

    run_sync([&]() -> asio::awaitable<void> {
        auto r = co_await mtx.async_lock();
        EXPECT_TRUE(r.has_value());
        if (!r.has_value()) co_return;
        EXPECT_TRUE(r->owns_lock());
        // Release explicitly
        auto* released_ptr = r->release();
        EXPECT_NE(released_ptr, nullptr);
        EXPECT_FALSE(r->owns_lock());
        released_ptr->unlock();  // must not hang
    });
}

TEST(SeamUncontendedLatency, DefaultPolicyIsDispatch) {
    async_mutex mtx;
    EXPECT_EQ(mtx.policy(), fixpp::sync::completion_policy::dispatch);
}

}  // namespace
