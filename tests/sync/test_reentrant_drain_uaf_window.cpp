// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_reentrant_drain_uaf_window.cpp — Seam #31
//
// F-3-residual (gate-b/r1): in-flight-acquirer + reentrant-drain UAF window.
//
// This seam verifies the I-6 escape-hatch remedy now works correctly after the
// F-2 fix: a consumer that follows the documented discipline
//   (a) re-calls cancel_and_drain() after an abort,
//   (b) observes sync_lock_aborted (not false success),
//   (c) waits until the mutex state is quiescent (holder releases),
//   (d) then destroys the mutex
// does NOT trigger a use-after-free or std::terminate.
//
// Pre-F-2-fix this seam would exercise the UAF path:
//   1. Reaper aborts (in-flight resumptions still pending).
//   2. Reentrant cancel_and_drain() returns false success (line 1170).
//   3. Caller destroys mutex — posted resumption's release_ref dereferences
//      destroyed mutex → UAF of waiter_pool_storage_.
//
// Post-F-2-fix:
//   2. Reentrant returns unexpected{sync_lock_aborted}.
//   3. Caller waits for holder to release (state_ → not_locked).
//   4. Destroy — clean; no UAF.
//
// Oracle: [2f §4.7.3] I-6 (partial-drain destructor discipline), I-5, I-7.
//         F-3-residual (gate-b/r1): missing seam for the in-flight-acquirer
//         + reentrant-drain UAF window identified in Opus triage.
//
// TSan/ASan: must be clean. ASan will detect the UAF if the F-2 fix regresses.
//
// HARNESS NOTE: ioc is declared BEFORE async_mutex so that the mutex destructs
// first (reverse C++ order), keeping the io_context service registry alive
// when drain_latch_state's concurrent_channel destructs (TSan-clean).

#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <atomic>
#include <future>
#include <memory>
#include <vector>

#include <fixpp/core/sync/async_mutex.hpp>

#include "sync/sync_test_support.hpp"

namespace {

using fixpp::sync::async_mutex;
using fixpp::sync::async_lock_guard;
using fixpp::sync::expected_t;
using fixpp::core::error;

using fixpp::sync::test::yield_n;

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: I-6 escape-hatch (a) works after F-2 fix.
//
// Sequence:
//  (1) Hold mutex; park a waiter.
//  (2) Abort the reaper (first cancel_and_drain).
//  (3) Reentrant cancel_and_drain() returns unexpected{sync_lock_aborted}.
//  (4) Release the holder → state_ → not_locked via unlock() under draining_.
//  (5) Destroy the mutex → must not terminate or UAF.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamReentrantDrainUAFWindow, I6EscapeHatchWorksAfterAbort) {
    // ioc declared BEFORE mtx: mtx destructs before ioc (reverse C++ order).
    asio::io_context ioc;
    auto mtx = std::make_unique<async_mutex>();

    asio::cancellation_signal reaper_sig;

    std::atomic<int>  reaper_result{-1};
    std::atomic<int>  reentrant_result{-1};
    std::atomic<bool> holder_release{false};
    std::atomic<bool> waiter_done{false};

    // Holder: keeps mutex locked until explicitly released.
    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        EXPECT_TRUE(g.has_value());
        while (!holder_release.load(std::memory_order_acquire))
            co_await yield_n(1);
        // guard dtor → unlock() → state_ CAS locked_no_waiters→not_locked
        // (because draining_==true, the short-circuit path runs).
    };

    // One waiter (gets aborted by the reaper).
    auto waiter_coro = [&]() -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx->async_lock();
        (void)r;
        waiter_done.store(true, std::memory_order_release);
    };

    // Reaper: parked on wait() while holder is active.
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

    // Reentrant + orderly shutdown: observes abort, then releases holder, then
    // waits for quiescence before returning (so the ioc drains before
    // mtx.reset()).
    auto reentrant_coro = [&]() -> asio::awaitable<void> {
        while (reaper_result.load(std::memory_order_acquire) == -1)
            co_await yield_n(1);

        // I-6 re-call — must return unexpected{sync_lock_aborted} (not success).
        auto d = co_await mtx->cancel_and_drain();
        if (d.has_value())
            reentrant_result.store(0, std::memory_order_release);  // BAD
        else if (d.error() == error::sync_lock_aborted)
            reentrant_result.store(1, std::memory_order_release);  // GOOD
        else
            reentrant_result.store(2, std::memory_order_release);

        // Release the holder → unlock() → state_==not_locked.
        holder_release.store(true, std::memory_order_release);
        // Let the unlock + waiter resumption quiesce.
        co_await yield_n(16);
    };

    // Canceller: fires reaper's slot while it is parked on wait().
    auto canceller = [&]() -> asio::awaitable<void> {
        co_await yield_n(12);
        reaper_sig.emit(asio::cancellation_type::total);
    };

    auto fh  = asio::co_spawn(ioc, holder_coro(), asio::use_future);
    auto fw  = asio::co_spawn(ioc, waiter_coro(), asio::use_future);
    auto fr  = asio::co_spawn(
        ioc, reaper_coro(),
        asio::bind_cancellation_slot(reaper_sig.slot(), asio::use_future));
    auto frt = asio::co_spawn(ioc, reentrant_coro(), asio::use_future);
    auto fcn = asio::co_spawn(ioc, canceller(), asio::use_future);

    ioc.run();
    fh.get();
    fw.get();
    fr.get();
    frt.get();
    fcn.get();

    EXPECT_EQ(reaper_result.load(), 1)
        << "Reaper must return sync_lock_aborted";
    EXPECT_EQ(reentrant_result.load(), 1)
        << "I-6 re-call must return unexpected{sync_lock_aborted} (not success). "
           "Got: " << reentrant_result.load()
        << " (0=false-success/UAF-risk, 1=aborted/OK)";

    // Destroy: state_==not_locked (holder released via draining_ short-circuit).
    // ASan monitors for any release_ref touching the destroyed mutex memory.
    // mtx.reset() here — ioc is still alive, so the channel destructs cleanly.
    EXPECT_NO_FATAL_FAILURE(mtx.reset());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: Multiple reentrant calls after abort all return sync_lock_aborted.
//
// Verifies the aborted latch stays observable across multiple reentrant callers
// — the latch shared_ptr is non-expiring (the mutex holds it) so every
// subscriber gets the abort outcome.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamReentrantDrainUAFWindow, MultipleReentrantCallsAllReturnAbort) {
    constexpr int R = 3;  // reentrant callers after the abort

    // ioc declared BEFORE mtx: mtx destructs before ioc (reverse C++ order).
    asio::io_context ioc;
    auto mtx = std::make_unique<async_mutex>();
    asio::cancellation_signal reaper_sig;

    std::atomic<int>  reaper_result{-1};
    std::atomic<int>  reentrant_aborted{0};
    std::atomic<int>  reentrant_success{0};
    std::atomic<bool> holder_release{false};

    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        EXPECT_TRUE(g.has_value());
        while (!holder_release.load(std::memory_order_acquire))
            co_await yield_n(1);
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

    // R concurrent reentrant callers, all after reaper returns.
    auto make_reentrant = [&](int idx) -> asio::awaitable<void> {
        while (reaper_result.load(std::memory_order_acquire) == -1)
            co_await yield_n(1);
        co_await yield_n(idx);  // stagger
        auto d = co_await mtx->cancel_and_drain();
        if (d.has_value())
            reentrant_success.fetch_add(1, std::memory_order_acq_rel);
        else if (d.error() == error::sync_lock_aborted)
            reentrant_aborted.fetch_add(1, std::memory_order_acq_rel);
    };

    // Shutdown: after all reentrant calls, release holder.
    auto shutdown_coro = [&]() -> asio::awaitable<void> {
        while (reentrant_aborted.load(std::memory_order_acquire) +
               reentrant_success.load(std::memory_order_acquire) < R)
            co_await yield_n(2);
        holder_release.store(true, std::memory_order_release);
        co_await yield_n(8);
    };

    auto canceller = [&]() -> asio::awaitable<void> {
        co_await yield_n(12);
        reaper_sig.emit(asio::cancellation_type::total);
    };

    auto fh  = asio::co_spawn(ioc, holder_coro(), asio::use_future);
    auto fr  = asio::co_spawn(
        ioc, reaper_coro(),
        asio::bind_cancellation_slot(reaper_sig.slot(), asio::use_future));
    std::vector<std::future<void>> rfuts;
    rfuts.reserve(R);
    for (int i = 0; i < R; ++i)
        rfuts.push_back(asio::co_spawn(ioc, make_reentrant(i), asio::use_future));
    auto fsd = asio::co_spawn(ioc, shutdown_coro(), asio::use_future);
    auto fcn = asio::co_spawn(ioc, canceller(), asio::use_future);

    ioc.run();
    fh.get();
    fr.get();
    for (auto& f : rfuts) f.get();
    fsd.get();
    fcn.get();

    EXPECT_EQ(reaper_result.load(), 1)
        << "Reaper must return sync_lock_aborted";
    EXPECT_EQ(reentrant_success.load(), 0)
        << "No reentrant call must return success after abort (UAF risk)";
    EXPECT_EQ(reentrant_aborted.load(), R)
        << "All R reentrant calls must return sync_lock_aborted. "
           "aborted=" << reentrant_aborted.load()
        << " success=" << reentrant_success.load();

    EXPECT_NO_FATAL_FAILURE(mtx.reset());
}

}  // namespace
