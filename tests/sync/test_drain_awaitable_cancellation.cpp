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
#include <chrono>
#include <fixpp/core/sync/async_mutex.hpp>
#include <future>
#include <vector>

#include "support/pump_until_ready.hpp"
#include "sync/sync_test_support.hpp"

namespace {

using fixpp::core::error;
using fixpp::sync::async_lock_guard;
using fixpp::sync::async_mutex;
using fixpp::sync::expected_t;

using fixpp::sync::test::yield_n;

// ─────────────────────────────────────────────────────────────────────────────
// Test: cancel_and_drain() is UNINTERRUPTIBLE — emit through the ATTACHED slot
// while the drain is parked in its quiescence loop behind a held mutex (P2-3:
// the prior version emitted BEFORE the slot was attached → stimulus-free no-op).
// The DISCRIMINATOR: the drain must NOT return after the cancel (it ignored it and
// is still waiting for the holder). An interruptible-drain regression returns early.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDrainAwaitableCancellation, CancelThroughAttachedSlotDoesNotInterrupt) {
    asio::cancellation_signal drain_sig;
    std::atomic<bool> drain_done{false};
    std::atomic<bool> drain_ok{false};

    asio::io_context ioc;
    async_mutex mtx;

    auto main_coro = [&]() -> asio::awaitable<void> {
        auto ex = co_await asio::this_coro::executor;

        // Hold the mutex so the drain cannot finalize (it parks in its quiescence
        // loop waiting for active_holders_count_==0).
        auto holder = co_await mtx.async_lock();
        EXPECT_TRUE(holder.has_value());

        // Spawn the drain WITH its cancellation slot bound (now the slot attaches).
        asio::co_spawn(
            ex,
            [&]() -> asio::awaitable<void> {
                auto d = co_await mtx.cancel_and_drain();
                drain_ok.store(d.has_value(), std::memory_order_release);
                drain_done.store(true, std::memory_order_release);
            },
            asio::bind_cancellation_slot(drain_sig.slot(), asio::detached));

        // Let the drain start and enter its quiescence loop (holder still held).
        co_await yield_n(8);
        // Emit through the NOW-ATTACHED slot.
        drain_sig.emit(asio::cancellation_type::total);
        co_await yield_n(8);

        // DISCRIMINATOR: the drain must NOT have returned — it ignored the cancel
        // (uninterruptible, E-5) AND is still waiting for the holder.
        EXPECT_FALSE(drain_done.load(std::memory_order_acquire))
            << "drain returned after a cancel through its attached slot — not uninterruptible";

        // Release the holder → the drain can finalize now.
        holder = expected_t<async_lock_guard>{};
        co_await yield_n(8);
    };

    auto f = asio::co_spawn(ioc, main_coro(), asio::use_future);
    bool timed_out = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        ioc.run_for(std::chrono::milliseconds(50));
        if (drain_done.load(std::memory_order_acquire) &&
            f.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
            break;
    }
    if (f.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        timed_out = true;
        ioc.stop();
    }
    ASSERT_FALSE(timed_out) << "drain hung (or main coroutine did not finish)";
    f.get();

    EXPECT_TRUE(drain_done.load()) << "drain must complete after holder release";
    EXPECT_TRUE(drain_ok.load())
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

    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, fh, "SeamDrainAwaitableCancellation::DrainCompletesEvenIfSlotFiredMidFlight/fh")) {
        return;
    }
    fh.get();
    for (auto& f : futs) f.get();
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, fd, "SeamDrainAwaitableCancellation::DrainCompletesEvenIfSlotFiredMidFlight/fd")) {
        return;
    }
    fd.get();
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, fcn,
            "SeamDrainAwaitableCancellation::DrainCompletesEvenIfSlotFiredMidFlight/fcn")) {
        return;
    }
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
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc2, fc,
            "SeamDrainAwaitableCancellation::DrainCompletesEvenIfSlotFiredMidFlight/fc")) {
        return;
    }
    fc.get();

    EXPECT_TRUE(post_drain_lock_drained)
        << "After drain, async_lock() must return sync_lock_drained";
}

}  // namespace
