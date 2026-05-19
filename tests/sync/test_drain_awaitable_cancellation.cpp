// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_drain_awaitable_cancellation.cpp — Seam #26
//
// The cancel_and_drain() awaitable's OWN cancellation (RC-β).
//
// co_spawn the drain with a bound cancellation_slot, queue waiters, emit
// cancellation_type::total on the drain's slot mid-drain.
//
// Expected behaviour:
//   - cancel_and_drain() returns unexpected(error::sync_lock_aborted);
//   - draining_ stays effective (a subsequent async_lock() returns
//     sync_lock_drained — the drain flag is NOT cleared on abort);
//   - no hang (all futures complete within the test timeout).
//
// Oracle: [2f §9 #26] — "cancel_and_drain() awaitable own cancellation" (RC-β).
//         [2f §4.7.3] — drain's own cancellation slot; draining_ stays set.

#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <atomic>

#include <fixpp/core/sync/async_mutex.hpp>

#include "sync/sync_test_support.hpp"

namespace {

using fixpp::sync::async_mutex;
using fixpp::sync::async_lock_guard;
using fixpp::sync::expected_t;
using fixpp::core::error;

using fixpp::sync::test::yield_n;

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: cancel_and_drain() returns sync_lock_aborted when its own slot fires.
// draining_ stays effective — post-cancel async_lock() returns sync_lock_drained.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDrainAwaitableCancellation, DrainCancelledReturnsAborted) {
    constexpr int N = 4;

    asio::cancellation_signal drain_cancel_sig;
    bool drain_aborted = false;
    bool post_cancel_lock_drained = false;
    std::atomic<int> waiter_completed{0};

    asio::io_context ioc;
    async_mutex mtx;  // after ioc — drain channel binds to ioc's executor.

    // Canonical §4.7.3 I-5 sequencing: the holder stays HELD so the reaper
    // reaches and PARKS on its cancellable wait (step (h)); the drain's own
    // cancellation slot is then fired while it is parked → it must observe
    // operation_aborted, signal_abort(), and return sync_lock_aborted with
    // draining_ left set. (Releasing the holder first would quiesce the
    // counters before the reaper parks, so the cancel would arrive too late
    // — that is not what this seam tests.)
    std::atomic<bool> holder_release{false};

    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        while (!holder_release.load(std::memory_order_acquire))
            co_await yield_n(1);
        // Guard dtor → unlock() (draining_ == true → short-circuit).
    };

    auto make_waiter = [&]() -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx.async_lock();
        (void)r;
        waiter_completed.fetch_add(1, std::memory_order_acq_rel);
    };

    auto drain_coro = [&]() -> asio::awaitable<void> {
        co_await yield_n(N * 2);  // let holder + waiters settle
        auto d = co_await mtx.cancel_and_drain();
        if (!d.has_value() && d.error() == error::sync_lock_aborted)
            drain_aborted = true;
    };

    auto canceller = [&]() -> asio::awaitable<void> {
        // Let the reaper reap the waiters and PARK on its wait() (holder is
        // still held → active_holders_count_ > 0), then fire its own slot.
        co_await yield_n(N * 6 + 8);
        drain_cancel_sig.emit(asio::cancellation_type::total);
        co_await yield_n(4);
        holder_release.store(true, std::memory_order_release);
    };

    auto fh = asio::co_spawn(ioc, holder_coro(), asio::use_future);
    std::vector<std::future<void>> futs;
    for (int i = 0; i < N; ++i)
        futs.push_back(asio::co_spawn(ioc, make_waiter(), asio::use_future));
    auto fd = asio::co_spawn(
        ioc,
        drain_coro(),
        asio::bind_cancellation_slot(drain_cancel_sig.slot(), asio::use_future));
    auto fcn = asio::co_spawn(ioc, canceller(), asio::use_future);

    ioc.run();
    fh.get();
    for (auto& f : futs) f.get();
    fd.get();
    fcn.get();

    // cancel_and_drain() with its own slot cancelled must return aborted.
    // NOTE: With the current TODO(T049) stub this test will likely fail (hang or
    // wrong result) — that is the expected TDD-red signal.
    EXPECT_TRUE(drain_aborted)
        << "cancel_and_drain() with cancelled slot must return sync_lock_aborted";

    // After the drain's cancellation, draining_ must remain set.
    // Subsequent async_lock() must get sync_lock_drained.
    asio::io_context ioc2;
    auto check = [&]() -> asio::awaitable<void> {
        auto r = co_await mtx.async_lock();
        post_cancel_lock_drained = !r.has_value() &&
                                   r.error() == error::sync_lock_drained;
    };
    auto fc = asio::co_spawn(ioc2, check(), asio::use_future);
    ioc2.run();
    fc.get();

    EXPECT_TRUE(post_cancel_lock_drained)
        << "After drain's own cancellation, draining_ must stay set; "
           "async_lock() must return sync_lock_drained";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: No hang — all futures complete within test timeout.
// Simpler form: fire cancellation immediately, verify no deadlock.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDrainAwaitableCancellation, NoDanglingFuturesOnDrainCancellation) {
    asio::cancellation_signal drain_sig;

    bool drain_done = false;

    asio::io_context ioc;
    async_mutex mtx;  // after ioc — drain channel binds to ioc's executor.

    auto drain_coro = [&]() -> asio::awaitable<void> {
        auto d = co_await mtx.cancel_and_drain();
        drain_done = true;
        (void)d;
    };

    // Fire the drain's slot immediately before it runs.
    drain_sig.emit(asio::cancellation_type::total);

    auto fd = asio::co_spawn(
        ioc,
        drain_coro(),
        asio::bind_cancellation_slot(drain_sig.slot(), asio::use_future));

    ioc.run();
    fd.get();  // must not block

    EXPECT_TRUE(drain_done)
        << "cancel_and_drain() must complete (no hang) even when cancelled immediately";
}

}  // namespace
