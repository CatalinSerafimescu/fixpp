// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_cancel_and_drain_reentrant_after_abort.cpp — Seam #30
//
// Abort-then-reentrant cancel_and_drain() must NOT return false success.
//
// Regression lock for F-2 (gate-b/r1): after a reaper is aborted, the latch
// stays published (aborted_==true). A fresh subsequent cancel_and_drain() call
// must observe the aborted latch via subscribe() and return
// unexpected{sync_lock_aborted} — never expected_t<void>{} (false success).
//
// Without the F-2 fix:
//   - abort path clears drain_latch_ptr_ to nullptr;
//   - reentrant caller hits the "draining_ && ptr==null" fast path at hpp:1170
//     and returns expected_t<void>{} while in-flight resumptions are still live.
//
// Oracle: [2f §4.7.3] I-5 (abort outcome observable to reentrant callers),
//         I-6 (re-call cancel_and_drain must not return success until quiescent),
//         I-7 (drain_latch_ptr_ cleared only after signal_release()).
//
// TSan/ASan: these seams must be clean under the Tier-1 matrix.
//
// HARNESS NOTE: ioc is declared BEFORE async_mutex in each test so that the
// mutex destructs before the io_context (C++ reverse-order destruction).
// With the F-2 fix, drain_latch_ptr_ holds the aborted latch for the
// mutex's lifetime; the latch's concurrent_channel destructs cleanly only
// while the io_context object (and its service registry) is still alive.

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
#include <fixpp/core/sync/async_mutex.hpp>
#include <future>
#include <vector>

#include "sync/sync_test_support.hpp"

namespace {

using fixpp::core::error;
using fixpp::sync::async_lock_guard;
using fixpp::sync::async_mutex;
using fixpp::sync::expected_t;

using fixpp::sync::test::yield_n;

// ─────────────────────────────────────────────────────────────────────────────
// Test 1 (core regression lock): abort first reaper; fresh subsequent
// cancel_and_drain() must return unexpected{sync_lock_aborted}, never success.
//
// Sequence:
//  (a) Hold the mutex; park N waiters.
//  (b) Abort the reaper (first cancel_and_drain) while it is parked on wait().
//  (c) After the reaper returns sync_lock_aborted, call cancel_and_drain() again
//      (fresh call, no cancellation slot).
//  (d) Assert the second call returns unexpected{sync_lock_aborted} (not success).
//      Release the holder so all in-flight resumptions quiesce and the mutex
//      becomes destructible.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamCancelAndDrainReentrantAfterAbort, ReentrantCallReturnsAbortNotSuccess) {
    constexpr int N = 2;  // waiters; small to keep the test fast

    // ioc declared BEFORE mtx: mtx destructs before ioc (reverse C++ order).
    // This ensures drain_latch_state's concurrent_channel destructs while
    // the io_context service registry is still alive (TSan-clean).
    asio::io_context ioc;
    async_mutex mtx;
    asio::cancellation_signal reaper_sig;

    std::atomic<int> reaper_result{-1};     // 0=success, 1=aborted, 2=other
    std::atomic<int> reentrant_result{-1};  // 0=success (BAD), 1=aborted (GOOD), 2=other
    std::atomic<bool> holder_release{false};

    // Holder: stays locked until explicitly released so the reaper parks on
    // wait() (active_holders_count_ stays 1 while the holder is held).
    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        while (!holder_release.load(std::memory_order_acquire)) co_await yield_n(1);
        // guard dtor → unlock() → active_holders_count_ decrements
    };

    // Spawn N waiters (they get aborted by the reaper).
    auto make_waiter = [&]() -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx.async_lock();
        (void)r;  // expected: sync_lock_aborted
    };

    // Reaper (bound to reaper_sig slot): will park on wait() while holder is held.
    auto reaper_coro = [&]() -> asio::awaitable<void> {
        co_await yield_n(N * 2 + 2);  // after holder + waiters enqueue
        auto d = co_await mtx.cancel_and_drain();
        if (d.has_value())
            reaper_result.store(0, std::memory_order_release);
        else if (d.error() == error::sync_lock_aborted)
            reaper_result.store(1, std::memory_order_release);
        else
            reaper_result.store(2, std::memory_order_release);
    };

    // Reentrant caller: runs AFTER the reaper has returned, observes the
    // aborted latch (drain_latch_ptr_ stays non-null with aborted_==true).
    auto reentrant_coro = [&]() -> asio::awaitable<void> {
        // Wait until reaper has returned (reaper_result != -1).
        while (reaper_result.load(std::memory_order_acquire) == -1) co_await yield_n(1);

        // Fresh cancel_and_drain() — no cancellation slot.
        // Must return unexpected{sync_lock_aborted}, not success.
        auto d = co_await mtx.cancel_and_drain();
        if (d.has_value())
            reentrant_result.store(0, std::memory_order_release);  // BAD
        else if (d.error() == error::sync_lock_aborted)
            reentrant_result.store(1, std::memory_order_release);  // GOOD
        else
            reentrant_result.store(2, std::memory_order_release);

        // Now release the holder so resumptions can quiesce.
        holder_release.store(true, std::memory_order_release);
    };

    // Canceller: aborts the reaper while it is parked on wait().
    auto canceller = [&]() -> asio::awaitable<void> {
        // Enough yield to let the reaper park (holder still held).
        co_await yield_n(N * 6 + 8);
        reaper_sig.emit(asio::cancellation_type::total);
    };

    auto fh = asio::co_spawn(ioc, holder_coro(), asio::use_future);
    std::vector<std::future<void>> wfuts;
    wfuts.reserve(N);
    for (int i = 0; i < N; ++i)
        wfuts.push_back(asio::co_spawn(ioc, make_waiter(), asio::use_future));
    auto fr = asio::co_spawn(ioc, reaper_coro(),
                             asio::bind_cancellation_slot(reaper_sig.slot(), asio::use_future));
    auto frt = asio::co_spawn(ioc, reentrant_coro(), asio::use_future);
    auto fcn = asio::co_spawn(ioc, canceller(), asio::use_future);

    ioc.run();
    fh.get();
    for (auto& f : wfuts) f.get();
    fr.get();
    frt.get();
    fcn.get();

    EXPECT_EQ(reaper_result.load(), 1)
        << "Reaper must return sync_lock_aborted when its slot is fired";

    // Core regression assertion (F-2 fix):
    EXPECT_EQ(reentrant_result.load(), 1)
        << "Reentrant cancel_and_drain() after abort must return "
           "unexpected{sync_lock_aborted}, NOT success. "
           "Got: "
        << reentrant_result.load() << " (0=false-success/BUG, 1=aborted/OK, 2=other)";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: Reentrant call returns sync_lock_aborted — no success until a new
// clean drain epoch runs. After all quiescence the destructor is safe.
//
// Extension: verifies that after the aborted epoch, if caller releases the
// holder so quiescence is reached, the mutex can be destroyed without
// triggering std::terminate (UAF regression lock, no ASan/TSan report).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamCancelAndDrainReentrantAfterAbort, NoUAFAfterReentrantAbortReturn) {
    // ioc declared BEFORE mtx: mtx destructs before ioc (reverse C++ order).
    asio::io_context ioc;
    // Allocate on heap so ASan/TSan can track lifetime precisely.
    auto mtx = std::make_unique<async_mutex>();

    asio::cancellation_signal reaper_sig;
    std::atomic<int> reaper_result{-1};
    std::atomic<int> reentrant_result{-1};
    std::atomic<bool> holder_release{false};
    std::atomic<bool> reentrant_done{false};

    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        EXPECT_TRUE(g.has_value());
        while (!holder_release.load(std::memory_order_acquire)) co_await yield_n(1);
    };

    auto reaper_coro = [&]() -> asio::awaitable<void> {
        co_await yield_n(4);
        auto d = co_await mtx->cancel_and_drain();
        if (d.has_value())
            reaper_result.store(0, std::memory_order_release);
        else if (d.error() == error::sync_lock_aborted)
            reaper_result.store(1, std::memory_order_release);
        else
            reaper_result.store(2, std::memory_order_release);
    };

    auto reentrant_coro = [&]() -> asio::awaitable<void> {
        while (reaper_result.load(std::memory_order_acquire) == -1) co_await yield_n(1);
        // Reentrant call must NOT return success.
        auto d = co_await mtx->cancel_and_drain();
        if (d.has_value())
            reentrant_result.store(0, std::memory_order_release);
        else if (d.error() == error::sync_lock_aborted)
            reentrant_result.store(1, std::memory_order_release);
        else
            reentrant_result.store(2, std::memory_order_release);

        // Release holder so resumptions quiesce → state_==not_locked.
        holder_release.store(true, std::memory_order_release);
        // Wait a bit for the unlock() short-circuit to fire.
        co_await yield_n(8);
        reentrant_done.store(true, std::memory_order_release);
    };

    auto canceller = [&]() -> asio::awaitable<void> {
        co_await yield_n(10);
        reaper_sig.emit(asio::cancellation_type::total);
    };

    auto fh = asio::co_spawn(ioc, holder_coro(), asio::use_future);
    auto fr = asio::co_spawn(ioc, reaper_coro(),
                             asio::bind_cancellation_slot(reaper_sig.slot(), asio::use_future));
    auto frt = asio::co_spawn(ioc, reentrant_coro(), asio::use_future);
    auto fcn = asio::co_spawn(ioc, canceller(), asio::use_future);

    ioc.run();
    fh.get();
    fr.get();
    frt.get();
    fcn.get();

    EXPECT_EQ(reaper_result.load(), 1) << "Reaper must return sync_lock_aborted";
    EXPECT_EQ(reentrant_result.load(), 1)
        << "Reentrant call must return sync_lock_aborted (not false success)";
    EXPECT_TRUE(reentrant_done.load()) << "Reentrant coroutine must complete without hanging";

    // Destroy the mutex while ioc is still alive (heap-allocated, reset here).
    // state_==not_locked after the holder's unlock() ran under draining_==true
    // (unlock() CAS: locked_no_waiters → not_locked; quiesced).
    // This is the UAF regression lock: if the reentrant call had returned false
    // success, the caller might have destroyed the mutex while posted resumption
    // handlers were still live (→ release_ref dereferences destroyed mutex).
    // The drain_latch_state's channel also destructs here while ioc is alive.
    EXPECT_NO_FATAL_FAILURE(mtx.reset());
}

}  // namespace
