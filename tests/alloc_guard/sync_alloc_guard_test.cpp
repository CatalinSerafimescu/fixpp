// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/alloc_guard/sync_alloc_guard_test.cpp
// 006-async-mutex T056 — Seam #10 (b) zero-global-alloc guard for the
// async_mutex embedded (mr==nullptr) contended hot path.
//
// Run under mallocnesia via tools/check_alloc.py:
// ZERO global malloc/free between alloc_guard_start() and alloc_guard_end()
// over the contended embedded-path acquire/release loop (steady state).
//
// Erratum E-4 (2026-05-19): asio 1.36.0's cancellation_slot has NO
// allocator-binding hook. Cancellation-handler memory comes from
// asio::detail::thread_info_base's per-thread recycling cache. The FIRST
// cancellation-slot assignment on a given thread does ONE global aligned_new;
// every subsequent one reuses the thread-local block (zero global new/delete
// in steady state).
//
// E-4 warm-up: before alloc_guard_start() we run one full contended
// acquire+suspend+grant+release cycle on the io_context's run thread so that
// asio's per-thread cancellation recycler is already primed.
//
// The measured window (between the markers) is STEADY STATE: the expected
// allocation count is ZERO.
//
// Mirrors tests/alloc_guard/wire_alloc_guard_test.cpp exactly in structure.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <array>
#include <atomic>
#include <cstddef>

#include <fixpp/core/sync/async_mutex.hpp>

// mallocnesia replaces these weak no-ops with its interceptor scope markers.
extern "C" {
__attribute__((weak)) void alloc_guard_start() {}
__attribute__((weak)) void alloc_guard_end() {}
}

namespace {

using fixpp::sync::async_mutex;
using fixpp::sync::async_lock_guard;
using fixpp::sync::expected_t;

static asio::awaitable<void> yield_n(int n) {
    auto ex = co_await asio::this_coro::executor;
    for (int i = 0; i < n; ++i)
        co_await asio::post(ex, asio::use_awaitable);
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// SyncAllocGuard / ContendedEmbeddedPathNoHeapAlloc
//
// Strategy:
//   On a single io_context (single run-thread), we interleave a "holder" and
//   a "waiter" coroutine: the holder acquires, yields to let the waiter push
//   onto the LIFO (suspend), then releases → the waiter is granted. Each
//   acquire/release pair is one iteration of the measured loop.
//
//   We drive the loop synchronously by serialising through io_context::run
//   within the outer gtest. Between each iteration we drive the ioc again for
//   the posted callbacks to complete.
//
//   All buffers and objects are constructed BEFORE alloc_guard_start().
// ─────────────────────────────────────────────────────────────────────────────

TEST(SyncAllocGuard, ContendedEmbeddedPathNoHeapAlloc) {
    constexpr int ITERATIONS = 500;
    constexpr int WARMUP_ITERATIONS = 5;

    // Construct the mutex and io_context BEFORE the guard markers.
    // Both live on the stack for the full duration of the test.
    async_mutex mtx;
    asio::io_context ioc;
    std::atomic<int> counter{0};

    // Helper: run one contended acquire+suspend+grant+release cycle.
    // The "holder" acquires first (fast path), then the "waiter" is spawned
    // and pushed onto the LIFO (contended suspend path, mr==nullptr).
    // The holder then releases, granting the waiter.
    auto run_one_cycle = [&]() {
        std::atomic<bool> holder_done{false};
        std::atomic<bool> waiter_done{false};

        // Holder coroutine.
        asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
            auto g = co_await mtx.async_lock(nullptr);
            EXPECT_TRUE(g.has_value());
            // Yield to allow the waiter to enqueue on the LIFO.
            co_await yield_n(4);
            holder_done.store(true, std::memory_order_release);
            // guard released here → unlock() → grant the waiter
        }, asio::detached);

        // Waiter coroutine — yields once to let the holder go first.
        asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
            // One yield so the holder's async_lock fast-path wins.
            co_await yield_n(1);
            // This acquire must contend (holder holds lock) → suspend.
            auto g = co_await mtx.async_lock(nullptr);
            EXPECT_TRUE(g.has_value());
            counter.fetch_add(1, std::memory_order_acq_rel);
            waiter_done.store(true, std::memory_order_release);
            // guard released
        }, asio::detached);

        // Run until both coroutines complete.
        ioc.restart();
        ioc.run();
    };

    // ── E-4 warm-up: prime asio's per-thread cancellation recycler before
    // measuring.  One full contended acquire+suspend+cancel/grant+release cycle
    // on the io_context's run thread is sufficient to seat the thread-local
    // recycling block so subsequent assignments cost zero global new/delete.
    for (int i = 0; i < WARMUP_ITERATIONS; ++i) {
        run_one_cycle();
    }

    // ── Measured loop — ZERO global new/delete expected between markers. ──────
    alloc_guard_start();
    for (int i = 0; i < ITERATIONS; ++i) {
        run_one_cycle();
    }
    alloc_guard_end();

    EXPECT_EQ(counter.load(), WARMUP_ITERATIONS + ITERATIONS)
        << "Not all waiter acquisitions completed";
}
