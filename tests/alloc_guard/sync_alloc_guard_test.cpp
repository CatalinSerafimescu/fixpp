// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/alloc_guard/sync_alloc_guard_test.cpp
// 006-async-mutex T056 — Seam #10 (b) zero-global-alloc guard for the
// async_mutex embedded (mr==nullptr) contended hot path.
//
// Run under mallocnesia via tools/check_alloc.py, which fails the run on a global
// allocation between alloc_guard_start() and alloc_guard_end().
//
// THE WINDOW: two long-lived coroutines contend for one async_mutex on one io_context.
// The holder acquires, holds across two executor round-trips, releases; the waiter
// yields once, then acquires — finding the mutex held, so it suspends on the embedded
// (mr==nullptr) waiter path and is resumed by the holder's release. The window opens
// inside the holder after a warm-up and closes when it finishes, so it covers the
// acquire, suspend, hand-off and resumption of the steady state, including the
// executor resumptions they go through. Creating the coroutines (co_spawn) and starting
// the io_context happen before it opens.
//
// Why long-lived coroutines: a co_spawn allocates its frame and its dispatch op through
// asio's per-thread recycling allocator, which falls through to the global heap
// (::aligned_alloc) for blocks it cannot recycle. A window that co_spawns per iteration
// measures that, not the mutex.
//
// Erratum E-4 (2026-05-19): asio 1.36.0's cancellation_slot has NO
// allocator-binding hook. Cancellation-handler memory comes from
// asio::detail::thread_info_base's per-thread recycling cache. The FIRST
// cancellation-slot assignment on a given thread does ONE global aligned_new;
// every subsequent one reuses the thread-local block. The warm-up iterations before
// the window opens prime that cache.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <cstddef>
#include <fixpp/core/sync/async_mutex.hpp>

// mallocnesia replaces these weak no-ops with its interceptor scope markers.
#include "support/alloc_guard_markers.hpp"

namespace {

using fixpp::sync::async_mutex;
using fixpp::sync::expected_t;

asio::awaitable<void> yield_n(int n) {
    auto ex = co_await asio::this_coro::executor;
    for (int i = 0; i < n; ++i) co_await asio::post(ex, asio::use_awaitable);
}

}  // namespace

TEST(SyncAllocGuard, ContendedEmbeddedPathNoHeapAlloc) {
    constexpr int kWarmup = 8;
    constexpr int kIterations = kWarmup + 500;

    async_mutex mtx;
    asio::io_context ioc;
    // Plain counters, asserted after the window: a gtest assertion inside it would
    // allocate on failure and blur the verdict.
    bool held = false;
    int contended = 0;
    int granted = 0;
    int lock_failures = 0;

    auto holder = [&]() -> asio::awaitable<void> {
        for (int i = 0; i < kIterations; ++i) {
            if (i == kWarmup && alloc_guard_start) alloc_guard_start();
            auto g = co_await mtx.async_lock(nullptr);
            if (!g.has_value()) ++lock_failures;
            held = true;
            co_await yield_n(2);  // the waiter runs here and finds the mutex held
            held = false;
        }  // each iteration's guard releases here, granting the suspended waiter
        if (alloc_guard_end) alloc_guard_end();
    };
    auto waiter = [&]() -> asio::awaitable<void> {
        for (int i = 0; i < kIterations; ++i) {
            co_await yield_n(1);
            if (held) ++contended;
            auto g = co_await mtx.async_lock(nullptr);
            if (g.has_value()) {
                ++granted;
            } else {
                ++lock_failures;
            }
        }
    };

    auto holder_done = asio::co_spawn(ioc, holder(), asio::use_future);
    auto waiter_done = asio::co_spawn(ioc, waiter(), asio::use_future);
    ioc.run();
    holder_done.get();
    waiter_done.get();

    EXPECT_EQ(lock_failures, 0);
    EXPECT_EQ(granted, kIterations) << "Not all waiter acquisitions completed";
    // The window measures the CONTENDED path only if the waiter usually found the
    // mutex held; otherwise the window covered the uncontended fast path.
    EXPECT_GT(contended, kIterations / 2) << "contended=" << contended;
}
