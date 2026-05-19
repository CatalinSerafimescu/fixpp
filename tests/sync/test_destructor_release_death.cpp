// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_destructor_release_death.cpp — Seam #5
//
// Destructor terminate precondition — release linkage death test.
//
// The async_mutex destructor fires std::terminate() in BOTH debug AND release
// if the mutex is held or has live waiters at destruction time. This seam
// exercises the precondition in both its triggering and non-triggering forms.
//
// Oracle: [2f §9 #5] — "Destructor terminate precondition (RC#3)".
//         [2f §4.7] — ~async_mutex() fires terminate if held or waiters present.
// RC#3: std::terminate() enforced in BOTH debug and release builds.
//
// Positive control: a properly drained-then-destroyed mutex does NOT terminate.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <chrono>
#include <memory>

#include <fixpp/core/sync/async_mutex.hpp>

#include "sync/sync_test_support.hpp"

namespace {

using fixpp::sync::async_mutex;
using fixpp::sync::async_lock_guard;
using fixpp::sync::expected_t;
using fixpp::core::error;

using fixpp::sync::test::yield_n;

// ─────────────────────────────────────────────────────────────────────────────
// Helper functions for death-test children.
// EXPECT_DEATH forks a child process and runs the statement there.
// We use static helper functions to avoid issues with braced initializer lists
// inside the macro argument (preprocessor limitation).
// ─────────────────────────────────────────────────────────────────────────────

// Trigger: holder acquires, waiter parks, mutex is destroyed → terminate.
static void destroy_with_live_waiter() {
    auto* mtx = new async_mutex{};
    asio::io_context ioc;

    // Holder: acquires and holds — leaks the guard intentionally (death child).
    auto holder_coro = [mtx]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        // Release the guard's ownership WITHOUT calling unlock().
        // This keeps the mutex in the "held" state.
        static_cast<void>(g->release());
        co_return;
    };

    // Waiter: parks behind the holder.
    auto waiter_coro = [mtx]() -> asio::awaitable<void> {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        // Suspends here — never resumes inside the death child.
        auto r = co_await mtx->async_lock();
        static_cast<void>(r.has_value());
    };

    // Run enough steps so the holder acquires and the waiter parks.
    asio::co_spawn(ioc, holder_coro(), asio::detached);
    asio::co_spawn(ioc, waiter_coro(), asio::detached);
    // Run two polling steps: holder grabs lock on step 1; waiter parks on step 2.
    for (int i = 0; i < 16; ++i) ioc.poll_one();

    // Destroy the mutex while a waiter is live => std::terminate().
    delete mtx;

    // Unreachable in normal flow (terminate fires above).
    std::exit(0);
}

// Trigger: holder acquires and never unlocks; mutex destroyed → terminate.
static void destroy_while_held() {
    auto* mtx = new async_mutex{};
    asio::io_context ioc;

    bool acquired = false;
    auto holder_coro = [mtx, &acquired]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        acquired = g.has_value();
        // Release the guard pointer WITHOUT unlocking. The mutex stays held.
        static_cast<void>(g->release());
        co_return;
    };

    asio::co_spawn(ioc, holder_coro(), asio::detached);
    ioc.run();
    // acquired == true; mutex is now held (lock granted, never unlocked).
    // Destroying it must terminate.
    delete mtx;
    std::exit(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Death test 1: destroy a mutex with a live waiter → terminate.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDestructorReleaseDeath, DestroyWithLiveWaiterTerminates) {
    EXPECT_DEATH(destroy_with_live_waiter(), "");
}

// ─────────────────────────────────────────────────────────────────────────────
// Death test 2: destroy a mutex that is held (no waiters) → terminate.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDestructorReleaseDeath, DestroyWhileHeldTerminates) {
    EXPECT_DEATH(destroy_while_held(), "");
}

// ─────────────────────────────────────────────────────────────────────────────
// Positive control: a properly drained-then-destroyed mutex does NOT terminate.
//
// cancel_and_drain() is currently unimplemented (TODO(T049)); this test
// documents the EXPECTED behaviour once it is implemented. With the stub it
// will fail — that is the correct TDD-red signal.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDestructorReleaseDeath, ProperlyDrainedMutexDoesNotTerminate) {
    constexpr int N = 4;
    asio::io_context ioc;

    auto mtx = std::make_unique<async_mutex>();

    std::atomic<int> aborted{0};
    bool drain_ok = false;

    // Canonical §4.7.4 sequencing: drain runs concurrently while the holder
    // still holds; the holder's later unlock() short-circuits on draining_.
    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        EXPECT_TRUE(g.has_value());
        co_await yield_n(N * 4 + 8);
        // Guard dtor → unlock() (draining_ == true → short-circuit).
    };

    auto make_waiter = [&]() -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx->async_lock();
        if (!r.has_value() && r.error() == error::sync_lock_aborted)
            aborted.fetch_add(1, std::memory_order_acq_rel);
    };

    auto drainer_coro = [&]() -> asio::awaitable<void> {
        co_await yield_n(N * 2);
        auto d = co_await mtx->cancel_and_drain();
        drain_ok = d.has_value();
    };

    auto fh = asio::co_spawn(ioc, holder_coro(), asio::use_future);
    auto fd = asio::co_spawn(ioc, drainer_coro(), asio::use_future);
    std::vector<std::future<void>> futs;
    futs.reserve(N);
    for (int i = 0; i < N; ++i)
        futs.push_back(asio::co_spawn(ioc, make_waiter(), asio::use_future));
    ioc.run();
    fh.get();
    fd.get();
    for (auto& f : futs) f.get();

    // After drain, destroy the mutex — must NOT terminate.
    mtx.reset();

    EXPECT_TRUE(drain_ok) << "cancel_and_drain() must return success after drain";
    EXPECT_EQ(aborted.load(), N) << "All N waiters must be aborted";
}

}  // namespace
