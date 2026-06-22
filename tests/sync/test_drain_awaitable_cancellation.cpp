// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_drain_awaitable_cancellation.cpp — Seam #26
//
// cancel_and_drain() uninterruptible behaviour (Erratum E-5 / 048).
//
// 048 narrowing: cancel_and_drain() now DISABLES its own cancellation at entry
// (co_await disable_cancellation{}) and always runs to completion. The old
// "abort path" (drain's own slot fires → returns sync_lock_aborted) is REMOVED.
// The drain is uninterruptible per E-5 / contracts/async_mutex-contract.md.
//
// This test verifies:
//   - A drain with a bound cancellation_slot that has its signal fired
//     COMPLETES (no hang) and returns ok (has_value) — NOT aborted.
//   - draining_ remains set; post-drain async_lock() returns sync_lock_drained.
//   - No dangling futures (no hang).
//
// [superseded test removed: DrainCancelledReturnsAborted tested the OLD
//  interruptible drain behaviour; the drain is now uninterruptible (E-5).]
//
// Oracle: [2f §4.7.3] E-5 (drain is uninterruptible); data-model.md state diagram.
// SC-001: cancel_and_drain() completes even when its slot is signalled.

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

using fixpp::core::error;
using fixpp::sync::async_lock_guard;
using fixpp::sync::async_mutex;
using fixpp::sync::expected_t;

using fixpp::sync::test::yield_n;

// ─────────────────────────────────────────────────────────────────────────────
// Test: cancel_and_drain() is UNINTERRUPTIBLE.
// Fire the drain's cancellation slot — drain must complete (not hang, not abort).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDrainAwaitableCancellation, NoDanglingFuturesOnDrainCancellation) {
    asio::cancellation_signal drain_sig;

    bool drain_done = false;
    bool drain_ok = false;

    asio::io_context ioc;
    async_mutex mtx;

    auto drain_coro = [&]() -> asio::awaitable<void> {
        auto d = co_await mtx.cancel_and_drain();
        drain_done = true;
        // 048: drain is uninterruptible — even if its slot is signalled, it
        // must return ok (has_value), not aborted.
        drain_ok = d.has_value();
    };

    // Fire the drain's slot immediately before it runs.
    drain_sig.emit(asio::cancellation_type::total);

    auto fd = asio::co_spawn(ioc, drain_coro(),
                             asio::bind_cancellation_slot(drain_sig.slot(), asio::use_future));

    ioc.run();
    fd.get();  // must not block

    EXPECT_TRUE(drain_done)
        << "cancel_and_drain() must complete (no hang) even when cancelled immediately";
    EXPECT_TRUE(drain_ok)
        << "cancel_and_drain() is uninterruptible (E-5); must return ok, not aborted";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: cancel fired during drain mid-flight — drain still completes with ok.
// (Replaces DrainCancelledReturnsAborted; verifies the OPPOSITE: no abort.)
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDrainAwaitableCancellation, DrainCompletesEvenIfSlotFiredMidFlight) {
    constexpr int N = 4;

    asio::cancellation_signal drain_cancel_sig;
    bool drain_ok = false;
    bool post_drain_lock_drained = false;
    std::atomic<int> waiter_completed{0};

    asio::io_context ioc;
    async_mutex mtx;

    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        co_await yield_n(N * 8 + 16);
        // Guard dtor → unlock() (draining_ == true → short-circuit).
    };

    auto make_waiter = [&]() -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx.async_lock();
        (void)r;
        waiter_completed.fetch_add(1, std::memory_order_acq_rel);
    };

    auto drain_coro = [&]() -> asio::awaitable<void> {
        co_await yield_n(N * 2);
        auto d = co_await mtx.cancel_and_drain();
        // 048: drain is uninterruptible — must return ok regardless of slot.
        drain_ok = d.has_value();
    };

    auto canceller = [&]() -> asio::awaitable<void> {
        // Fire the drain's slot mid-flight while the holder still holds.
        co_await yield_n(N * 4 + 4);
        drain_cancel_sig.emit(asio::cancellation_type::total);
    };

    auto fh = asio::co_spawn(ioc, holder_coro(), asio::use_future);
    std::vector<std::future<void>> futs;
    for (int i = 0; i < N; ++i)
        futs.push_back(asio::co_spawn(ioc, make_waiter(), asio::use_future));
    auto fd = asio::co_spawn(
        ioc, drain_coro(), asio::bind_cancellation_slot(drain_cancel_sig.slot(), asio::use_future));
    auto fcn = asio::co_spawn(ioc, canceller(), asio::use_future);

    ioc.run();
    fh.get();
    for (auto& f : futs) f.get();
    fd.get();
    fcn.get();

    // The drain must succeed (uninterruptible) even with the slot fired.
    EXPECT_TRUE(drain_ok) << "cancel_and_drain() is uninterruptible (E-5): must return ok";

    // After drain, async_lock() returns sync_lock_drained.
    asio::io_context ioc2;
    auto check = [&]() -> asio::awaitable<void> {
        auto r = co_await mtx.async_lock();
        post_drain_lock_drained = !r.has_value() && r.error() == error::sync_lock_drained;
    };
    auto fc = asio::co_spawn(ioc2, check(), asio::use_future);
    ioc2.run();
    fc.get();

    EXPECT_TRUE(post_drain_lock_drained)
        << "After drain, async_lock() must return sync_lock_drained";
}

}  // namespace
