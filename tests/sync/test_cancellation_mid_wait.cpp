// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_cancellation_mid_wait.cpp — Seam #4
//
// Cancellation mid-wait: park a waiter, fire cancellation_type::total, verify
// the waiter completes with expected_t::unexpected{error::sync_lock_aborted},
// the mutex LIFO list is empty, and the waiter never acquired ownership.
//
// ASIO cancellation model for awaitable<> coroutines:
//   co_spawn(..., coro(), bind_cancellation_slot(sig.slot(), use_future))
//   propagates sig's slot as the coroutine's cancellation_state.
//   Inside the coroutine, co_await mtx.async_lock() inherits that state;
//   on_cancel() (T038) is registered on the inherited slot.
//
// Oracle: [2f §9 #4] — "Cancellation mid-wait".
// [2f §4.5]: total, partial (treated as total), terminal (treated as total).
// SC-002: cancel-vs-drain CAS-arbitration yields exactly one of {granted,cancelled}.

#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <atomic>
#include <optional>

#include <fixpp/core/sync/async_mutex.hpp>

namespace {

using fixpp::sync::async_mutex;
using fixpp::sync::async_lock_guard;
using fixpp::sync::expected_t;
using fixpp::core::error;

// Post N yields on the calling coroutine's executor.
static asio::awaitable<void> yield_n(int n) {
    auto ex = co_await asio::this_coro::executor;
    for (int i = 0; i < n; ++i)
        co_await asio::post(ex, asio::use_awaitable);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: total — basic mid-wait cancel.
//   Holder acquires. Waiter suspends (pushed onto LIFO). Cancel signal fires.
//   Waiter must resume with sync_lock_aborted.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamCancellationMidWait, TotalCancelYieldsSyncLockAborted) {
    async_mutex mtx;
    asio::cancellation_signal cancel_sig;

    std::optional<expected_t<async_lock_guard>> waiter_result;
    bool waiter_ran     = false;
    int  holder_counter = 0;

    asio::io_context ioc;

    // Holder coroutine: acquires mutex, yields to let waiter park,
    // fires cancellation, then releases.
    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());

        // Allow the waiter coroutine to push onto the LIFO.
        co_await yield_n(6);

        // Fire cancellation_type::total on the waiter's slot.
        cancel_sig.emit(asio::cancellation_type::total);

        // Yield to let the cancellation handler run.
        co_await yield_n(4);
        ++holder_counter;
        // Guard dtor → unlock().
    };

    // Waiter coroutine: co_await async_lock() inherits the cancellation slot
    // from the co_spawn token (bind_cancellation_slot below).
    auto waiter = [&]() -> asio::awaitable<void> {
        // Yield once so the holder acquires first.
        co_await yield_n(1);

        waiter_result = co_await mtx.async_lock();
        waiter_ran = true;
    };

    auto fh = asio::co_spawn(ioc, holder(), asio::use_future);
    // Bind the cancellation slot to the waiter's co_spawn token so the
    // waiter coroutine's this_coro::cancellation_state propagates into
    // co_await mtx.async_lock() → on_cancel registration (T038).
    auto fw = asio::co_spawn(ioc, waiter(),
                             asio::bind_cancellation_slot(cancel_sig.slot(),
                                                          asio::use_future));
    ioc.run();
    fh.get();
    fw.get();

    EXPECT_TRUE(waiter_ran) << "Waiter must complete";
    ASSERT_TRUE(waiter_result.has_value());

    // Primary assertion: waiter must see sync_lock_aborted.
    EXPECT_FALSE(waiter_result->has_value())
        << "Cancelled waiter must NOT hold the lock";
    EXPECT_EQ(waiter_result->error(), error::sync_lock_aborted)
        << "Cancelled waiter error must be sync_lock_aborted";

    EXPECT_EQ(holder_counter, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: partial — treated as total (no partial acquisition semantics).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamCancellationMidWait, PartialCancelTreatedAsTotal) {
    async_mutex mtx;
    asio::cancellation_signal cancel_sig;

    std::optional<expected_t<async_lock_guard>> waiter_result;

    asio::io_context ioc;

    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        co_await yield_n(6);
        cancel_sig.emit(asio::cancellation_type::partial);
        co_await yield_n(4);
    };

    auto waiter = [&]() -> asio::awaitable<void> {
        co_await yield_n(1);
        waiter_result = co_await mtx.async_lock();
    };

    auto fh = asio::co_spawn(ioc, holder(), asio::use_future);
    auto fw = asio::co_spawn(ioc, waiter(),
                             asio::bind_cancellation_slot(cancel_sig.slot(),
                                                          asio::use_future));
    ioc.run();
    fh.get();
    fw.get();

    ASSERT_TRUE(waiter_result.has_value());
    // partial must be treated as total → sync_lock_aborted.
    EXPECT_FALSE(waiter_result->has_value());
    EXPECT_EQ(waiter_result->error(), error::sync_lock_aborted);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: terminal — treated as total.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamCancellationMidWait, TerminalCancelTreatedAsTotal) {
    async_mutex mtx;
    asio::cancellation_signal cancel_sig;

    std::optional<expected_t<async_lock_guard>> waiter_result;

    asio::io_context ioc;

    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        co_await yield_n(6);
        cancel_sig.emit(asio::cancellation_type::terminal);
        co_await yield_n(4);
    };

    auto waiter = [&]() -> asio::awaitable<void> {
        co_await yield_n(1);
        waiter_result = co_await mtx.async_lock();
    };

    auto fh = asio::co_spawn(ioc, holder(), asio::use_future);
    auto fw = asio::co_spawn(ioc, waiter(),
                             asio::bind_cancellation_slot(cancel_sig.slot(),
                                                          asio::use_future));
    ioc.run();
    fh.get();
    fw.get();

    ASSERT_TRUE(waiter_result.has_value());
    EXPECT_FALSE(waiter_result->has_value());
    EXPECT_EQ(waiter_result->error(), error::sync_lock_aborted);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4: cancelled waiter does NOT acquire ownership.
//   After cancellation, a subsequent uncancelled waiter should succeed.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamCancellationMidWait, CancelledWaiterDoesNotAcquireOwnership) {
    async_mutex mtx;
    asio::cancellation_signal cancel_sig;

    bool cancelled_got_lock = false;
    bool second_got_lock    = false;

    asio::io_context ioc;

    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        co_await yield_n(6);
        cancel_sig.emit(asio::cancellation_type::total);
        co_await yield_n(4);
        // unlock here via guard dtor.
    };

    auto cancelled_waiter = [&]() -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx.async_lock();
        cancelled_got_lock = r.has_value();
    };

    // Second waiter: no cancellation signal — must eventually get the lock.
    auto second_waiter = [&]() -> asio::awaitable<void> {
        co_await yield_n(2);
        auto g = co_await mtx.async_lock();
        second_got_lock = g.has_value();
    };

    auto fh  = asio::co_spawn(ioc, holder(), asio::use_future);
    auto fcw = asio::co_spawn(ioc, cancelled_waiter(),
                              asio::bind_cancellation_slot(cancel_sig.slot(),
                                                           asio::use_future));
    auto fsw = asio::co_spawn(ioc, second_waiter(), asio::use_future);
    ioc.run();
    fh.get();
    fcw.get();
    fsw.get();

    EXPECT_FALSE(cancelled_got_lock) << "Cancelled waiter must NOT acquire";
    EXPECT_TRUE(second_got_lock)     << "Second waiter must eventually acquire";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5: LIFO list is empty after cancellation completes.
//   After a waiter is cancelled, subsequent destruction succeeds (no std::terminate).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamCancellationMidWait, MutexEmptyAfterCancellation) {
    asio::io_context ioc;
    asio::cancellation_signal cancel_sig;

    // Use top-level co_spawn with explicit futures and separate ioc.run() passes
    // to avoid blocking the ioc thread inside a co_spawn'd coroutine (deadlock).
    async_mutex mtx;

    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        co_await yield_n(6);
        cancel_sig.emit(asio::cancellation_type::total);
        co_await yield_n(4);
    };

    auto waiter = [&]() -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx.async_lock();
        EXPECT_FALSE(r.has_value());
    };

    auto fh = asio::co_spawn(ioc, holder(), asio::use_future);
    auto fw = asio::co_spawn(ioc, waiter(),
                              asio::bind_cancellation_slot(cancel_sig.slot(),
                                                           asio::use_future));
    ioc.run();
    fh.get();
    fw.get();
    // mtx destructs here — must not std::terminate (LIFO empty).
}

}  // namespace
