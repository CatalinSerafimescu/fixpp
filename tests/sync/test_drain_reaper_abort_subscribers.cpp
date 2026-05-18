// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_drain_reaper_abort_subscribers.cpp — Seam #29
//
// Reaper abort wakes all subscribers (v1.4).
//
// Multiple concurrent cancel_and_drain() subscribers on one epoch.
// The reaper is cancelled (abort) => EVERY subscriber is woken with the abort
// outcome (unexpected(error::sync_lock_aborted)) — none hangs forever.
//
// Oracle: [2f §9 #29] — "reaper abort wakes all subscribers" (v1.4).
//         [2f §4.7.3] — drain_latch_state::signal_abort() notifies all waiters.
//         RC-β: lazy latch; v1.4 abort propagation.
//
// Per [2f §4.7.3] v1.4: when the reaper's cancellation slot fires,
// drain_latch_state::signal_abort() must wake ALL concurrent drain subscribers
// with sync_lock_aborted — even those whose own cancellation slot was NOT fired.
// Without the subscriber mechanism (current stub: drain_in_progress_ never set),
// non-reaper drainers independently succeed, so the "all get aborted" assertion
// catches the gap.
//
// HARNESS NOTE: drain subscriber futures are top-level co_spawn (not nested in
// another coroutine on the same ioc) to avoid deadlock from .get() inside ioc.run().

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
#include <future>
#include <vector>

#include <fixpp/core/sync/async_mutex.hpp>

namespace {

using fixpp::sync::async_mutex;
using fixpp::sync::async_lock_guard;
using fixpp::sync::expected_t;
using fixpp::core::error;

static asio::awaitable<void> yield_n(int n) {
    auto ex = co_await asio::this_coro::executor;
    for (int i = 0; i < n; ++i)
        co_await asio::post(ex, asio::use_awaitable);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: S=3 concurrent drain subscribers; reaper (sigs[0]) is aborted;
// ALL S subscribers must get sync_lock_aborted — including those whose own
// cancellation slot was NOT fired (sigs[1], sigs[2]).
//
// Stronger oracle: drain_aborted_count must equal S (not just 1).
// This catches the missing subscriber wake mechanism.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDrainReaperAbortSubscribers, AllSubscribersGetAbortedWhenReaperAborts) {
    constexpr int S = 3;   // reaper + 2 subscribers (only reaper's slot fires)
    constexpr int N = 4;   // waiters queued on the mutex

    async_mutex mtx;

    // Only sigs[0] is fired; sigs[1] and sigs[2] are never fired.
    // Per v1.4 contract, sigs[1] and sigs[2] must still receive sync_lock_aborted.
    std::vector<asio::cancellation_signal> sigs(S);

    std::atomic<int> drain_aborted_count{0};
    std::atomic<int> drain_success_count{0};
    std::atomic<int> drain_completed_count{0};

    asio::io_context ioc;

    // Canonical §4.7.3 I-5/v1.4 sequencing (single ioc.run()): the holder
    // stays HELD so the reaper reaps the N waiters and PARKS on its wait()
    // (step (h)). Drainer 0 calls cancel_and_drain() first (stagger) → wins
    // drain_in_progress_ and is the reaper; drainers 1..S-1 subscribe to the
    // same epoch. Firing the reaper's own slot (sigs[0]) while it is parked
    // must signal_abort() and wake EVERY subscriber with sync_lock_aborted —
    // even those whose own slot never fired. (Releasing the holder first
    // would quiesce the epoch before the reaper parks — not this contract.)
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
    };

    auto make_drainer = [&](int s) -> asio::awaitable<void> {
        co_await yield_n(N * 2 + s);  // stagger: drainer 0 reaches drain first
        auto d = co_await mtx.cancel_and_drain();
        if (d.has_value()) {
            drain_success_count.fetch_add(1, std::memory_order_acq_rel);
        } else if (d.error() == error::sync_lock_aborted) {
            drain_aborted_count.fetch_add(1, std::memory_order_acq_rel);
        }
        drain_completed_count.fetch_add(1, std::memory_order_acq_rel);
    };

    auto canceller = [&]() -> asio::awaitable<void> {
        // Let the reaper reap the waiters and PARK on wait() (holder held).
        co_await yield_n(N * 6 + S + 8);
        sigs[0].emit(asio::cancellation_type::total);  // reaper's slot only
        co_await yield_n(4);
        holder_release.store(true, std::memory_order_release);
    };

    auto fh = asio::co_spawn(ioc, holder_coro(), asio::use_future);
    std::vector<std::future<void>> wfuts;
    for (int i = 0; i < N; ++i)
        wfuts.push_back(asio::co_spawn(ioc, make_waiter(), asio::use_future));
    std::vector<std::future<void>> dfuts;
    dfuts.reserve(S);
    for (int s = 0; s < S; ++s)
        dfuts.push_back(asio::co_spawn(
            ioc, make_drainer(s),
            asio::bind_cancellation_slot(sigs[s].slot(), asio::use_future)));
    auto fcn = asio::co_spawn(ioc, canceller(), asio::use_future);

    ioc.run();
    fh.get();
    for (auto& f : wfuts) f.get();
    for (auto& df : dfuts) df.get();
    fcn.get();

    // All S subscribers must complete (no hang).
    EXPECT_EQ(drain_completed_count.load(), S)
        << "All S drain subscribers must complete (none may hang forever)";

    // Per [2f §4.7.3] v1.4: reaper abort => ALL S subscribers get sync_lock_aborted.
    // Without the subscriber mechanism: sigs[1,2] succeed independently
    // => drain_aborted_count < S. This assertion catches the gap.
    EXPECT_EQ(drain_aborted_count.load(), S)
        << "When reaper is aborted, ALL S subscribers must get sync_lock_aborted. "
           "drain_aborted=" << drain_aborted_count.load()
        << ", drain_success=" << drain_success_count.load();
    EXPECT_EQ(drain_success_count.load(), 0)
        << "No subscriber must succeed when the reaper is aborted";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: All subscriber futures complete — simplified no-waiters form.
// S=3 concurrent drains, ALL aborted immediately.
// No hang; all complete.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDrainReaperAbortSubscribers, NoHangWithImmediateReaperAbort) {
    constexpr int S = 3;
    async_mutex mtx;

    std::vector<asio::cancellation_signal> sigs(S);
    std::atomic<int> completed{0};

    asio::io_context ioc;

    std::vector<std::future<void>> dfuts;
    dfuts.reserve(S);
    for (int s = 0; s < S; ++s) {
        dfuts.push_back(
            asio::co_spawn(
                ioc,
                [&]() -> asio::awaitable<void> {
                    auto d = co_await mtx.cancel_and_drain();
                    (void)d;
                    completed.fetch_add(1, std::memory_order_acq_rel);
                },
                asio::bind_cancellation_slot(sigs[s].slot(), asio::use_future)));
    }

    // Abort ALL subscribers immediately (including the reaper, sigs[0]).
    for (auto& sig : sigs)
        sig.emit(asio::cancellation_type::total);

    ioc.run();
    for (auto& df : dfuts) df.get();  // must not block

    EXPECT_EQ(completed.load(), S)
        << "All S subscribers must complete when all slots are aborted";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: Late abort — abort arrives after drain has begun processing waiters.
// All S subscribers must still complete, none hangs.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDrainReaperAbortSubscribers, LateAbortDoesNotLeaveSubscriberHanging) {
    constexpr int S = 2;
    constexpr int N = 2;
    async_mutex mtx;

    std::vector<asio::cancellation_signal> sigs(S);
    std::atomic<int> completed{0};
    std::atomic<int> waiter_done{0};

    asio::io_context ioc;

    // Phase 1: hold, park N waiters, release.
    auto phase1 = [&]() -> asio::awaitable<void> {
        auto ex = co_await asio::this_coro::executor;

        auto holder = co_await mtx.async_lock();
        EXPECT_TRUE(holder.has_value());

        for (int i = 0; i < N; ++i) {
            asio::co_spawn(ex, [&]() -> asio::awaitable<void> {
                co_await yield_n(1);
                auto r = co_await mtx.async_lock();
                (void)r;
                waiter_done.fetch_add(1, std::memory_order_acq_rel);
            }, asio::detached);
        }

        co_await yield_n(N * 3);
        holder = expected_t<async_lock_guard>{};  // unlock
    };

    auto fp1 = asio::co_spawn(ioc, phase1(), asio::use_future);
    ioc.run();
    fp1.get();

    // Phase 2: S drain subscribers + late abort.
    ioc.restart();

    std::vector<std::future<void>> dfuts;
    dfuts.reserve(S);
    for (int s = 0; s < S; ++s) {
        dfuts.push_back(
            asio::co_spawn(
                ioc,
                [&, s]() -> asio::awaitable<void> {
                    co_await yield_n(s);
                    auto d = co_await mtx.cancel_and_drain();
                    (void)d;
                    completed.fetch_add(1, std::memory_order_acq_rel);
                },
                asio::bind_cancellation_slot(sigs[s].slot(), asio::use_future)));
    }

    // Let drain progress a bit before aborting sigs[0].
    for (int i = 0; i < 8; ++i) ioc.poll_one();
    sigs[0].emit(asio::cancellation_type::total);

    ioc.run();
    for (auto& df : dfuts) df.get();

    EXPECT_EQ(completed.load(), S)
        << "All S subscribers must complete even on late reaper abort";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4 (v1.4 strict): Reaper aborted => subscriber (no own abort) also gets
// sync_lock_aborted. Two-caller minimal form.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamDrainReaperAbortSubscribers, SubscriberGetsAbortedWhenReaperAborted) {
    async_mutex mtx;

    asio::cancellation_signal reaper_sig;
    // subscriber_sig is never fired.

    std::atomic<int> reaper_result{-1};    // 0=success, 1=aborted, 2=other
    std::atomic<int> subscriber_result{-1};

    std::atomic<bool> holder_release{false};
    asio::io_context ioc;

    // Canonical §4.7.3 I-5 sequencing: a holder stays HELD so the reaper
    // reaches and PARKS on its wait(); only then is the reaper's slot fired,
    // so it observes the cancellation, signal_abort()s, and the subscriber
    // (whose own slot never fires) is woken aborted via the latch.
    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        while (!holder_release.load(std::memory_order_acquire))
            co_await yield_n(1);
        // Guard dtor → unlock() (draining_ == true → short-circuit).
    };

    // Reaper: first to call cancel_and_drain() (wins drain_in_progress_).
    auto f_reaper = asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            co_await yield_n(2);  // after holder acquires
            auto d = co_await mtx.cancel_and_drain();
            if (d.has_value())
                reaper_result.store(0, std::memory_order_release);
            else if (d.error() == error::sync_lock_aborted)
                reaper_result.store(1, std::memory_order_release);
            else
                reaper_result.store(2, std::memory_order_release);
        },
        asio::bind_cancellation_slot(reaper_sig.slot(), asio::use_future));

    // Subscriber: second drain, NO cancellation slot fired.
    auto f_subscriber = asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            co_await yield_n(4);  // after the reaper claims the epoch
            auto d = co_await mtx.cancel_and_drain();
            if (d.has_value())
                subscriber_result.store(0, std::memory_order_release);
            else if (d.error() == error::sync_lock_aborted)
                subscriber_result.store(1, std::memory_order_release);
            else
                subscriber_result.store(2, std::memory_order_release);
        },
        asio::use_future);  // no cancellation slot

    auto canceller = [&]() -> asio::awaitable<void> {
        co_await yield_n(10);  // reaper parked (holder held)
        reaper_sig.emit(asio::cancellation_type::total);
        co_await yield_n(4);
        holder_release.store(true, std::memory_order_release);
    };

    auto fh  = asio::co_spawn(ioc, holder_coro(), asio::use_future);
    auto fcn = asio::co_spawn(ioc, canceller(), asio::use_future);

    ioc.run();
    fh.get();
    fcn.get();
    f_reaper.get();
    f_subscriber.get();

    // Reaper must be aborted.
    EXPECT_EQ(reaper_result.load(), 1)
        << "Reaper must receive sync_lock_aborted when its slot fires";

    // Per v1.4: subscriber must ALSO be aborted (via latch signal_abort()).
    // Without the mechanism: subscriber returns success (0). This catches the gap.
    EXPECT_EQ(subscriber_result.load(), 1)
        << "Subscriber (no own abort slot fired) must ALSO receive sync_lock_aborted "
           "via drain_latch_state::signal_abort(). Got: "
        << subscriber_result.load() << " (0=success, 1=aborted)";
}

}  // namespace
