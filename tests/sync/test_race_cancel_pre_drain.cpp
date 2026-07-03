// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_race_cancel_pre_drain.cpp — Seam #15
//
// cancel-after-detach-pre-drain race (RC#1).
//
// Park three waiters on a mutex. Release the holder so unlock() begins its
// drain. From a different coroutine, fire cancellation_type::total on the
// second waiter's slot CONCURRENTLY with the unlock drain. Verify:
//   (a) each waiter completes exactly once (guard or sync_lock_aborted);
//   (b) no double-resume;
//   (c) the mutex ends up not_locked (no std::terminate on destruction);
//   (d) TSan-clean (verified by running under linux-clang-tsan).
//
// ASIO cancellation pattern: spawn each waiter with bind_cancellation_slot
// so its co_await mtx.async_lock() inherits the cancellation state.
//
// Oracle: [2f §9 #15] — "cancel-after-detach-pre-drain race" (RC#1).
// SC-002: TSan-clean concurrency.
//
// 058-async-mutex-hardening T029 (US5, FR-008): converted from a
// single-threaded-in-disguise `ioc.run()` driver (holder + all waiters +
// every cancel emission were sequenced deterministically by ONE io_context on
// the calling thread — feedback_single_threaded_harness_masks_strand_races)
// to genuinely multi-threaded: the holder runs on its own io_context serviced
// by a dedicated OS thread (thread_a); all waiters (and their cancellation
// slots) run on a SEPARATE io_context/thread (thread_b). Every cancel signal
// is POSTED onto the waiters' own executor (thread_b) rather than fired from
// a third thread: asio::cancellation_signal/slot are NOT thread-safe, and the
// completion path clears each slot on the resuming waiter's own executor —
// firing emit() from an unsynchronized third thread would race asio's own
// (non-thread-safe) slot bookkeeping (a harness bug, not evidence about
// async_mutex's phase_ arbitration). Keeping every emit() confined to
// thread_b while the grant CAS chain runs via unlock()'s drain on the
// holder's executor (thread_a) still creates a genuine, unsynchronized
// cross-thread race on each targeted waiter's shared `phase_` atomic — the
// two threads are never causally ordered by this test's own code, only by
// the primitive's CAS arbitration and real OS scheduling.
//
// Setup (holder acquires; all waiters park) is driven deterministically via
// bounded single-threaded `poll_one()` loops BEFORE either background thread
// starts (same idiom as test_drain_destroy_inflight_mt.cpp) — only the
// interesting cancel-vs-drain race itself runs genuinely concurrently.

#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <fixpp/core/sync/async_mutex.hpp>
#include <future>
#include <thread>
#include <vector>

#include "sync/sync_test_support.hpp"

namespace {

using fixpp::core::error;
using fixpp::sync::async_mutex;

using fixpp::sync::test::yield_n;

// Bounded wait for a future's readiness — avoids hanging the suite if a
// genuinely-multi-threaded conversion wedges (feedback_ci_hung_test_no_
// timeout_burns_6h). NOT a substitute for the discriminating assertions
// below; it only bounds how long a stuck run can block CI.
template <typename T>
bool wait_ready(std::future<T>& f, std::chrono::steady_clock::time_point deadline) {
    while (std::chrono::steady_clock::now() < deadline) {
        if (f.wait_for(std::chrono::milliseconds(5)) == std::future_status::ready) return true;
    }
    return f.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
}

// ─────────────────────────────────────────────────────────────────────────────
// Race scenario: N waiters park while holder holds. Holder releases while a
// cancel fires on one waiter concurrently. The per-waiter phase_ CAS
// arbitrates: exactly one of {granted, cancelled} per waiter.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamRaceCancelPreDrain, ThreeWaitersOneRacingCancel) {
    constexpr int N = 3;

    std::vector<asio::cancellation_signal> sigs(N);

    std::atomic<int> granted_count{0};
    std::atomic<int> aborted_count{0};
    std::atomic<int> total_completed{0};

    asio::io_context ioc_a;  // holder's own context/thread
    asio::io_context ioc_b;  // every waiter's own context/thread (shared)
    async_mutex mtx;

    // Holder: acquires, yields to let waiters queue, posts cancel on
    // sigs[1] (onto the waiters' own executor), then releases.
    bool holder_acquired = false;
    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        holder_acquired = g.has_value();
        EXPECT_TRUE(holder_acquired);

        // Let all N waiters push onto the LIFO.
        co_await yield_n(N * 4);

        // Post cancel on the middle waiter (index 1) onto thread_b — races
        // with the upcoming drain walk after this coroutine's own unlock
        // (on thread_a).
        asio::post(ioc_b.get_executor(),
                   [&sigs] { sigs[1].emit(asio::cancellation_type::total); });

        // One yield so this coroutine's own progress toward unlock may
        // interleave with thread_b processing the posted cancel.
        co_await yield_n(2);
        // unlock via guard dtor.
    };

    auto make_waiter = [&](int idx) -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx.async_lock();
        if (r.has_value()) {
            granted_count.fetch_add(1, std::memory_order_acq_rel);
            co_await yield_n(2);
        } else {
            EXPECT_EQ(r.error(), error::sync_lock_aborted);
            aborted_count.fetch_add(1, std::memory_order_acq_rel);
        }
        total_completed.fetch_add(1, std::memory_order_acq_rel);
    };

    auto fh = asio::co_spawn(ioc_a, holder(), asio::use_future);
    for (int i = 0; i < 16 && !holder_acquired; ++i) ioc_a.poll_one();
    ASSERT_TRUE(holder_acquired) << "setup: holder failed to acquire";

    std::vector<std::future<void>> futs;
    for (int i = 0; i < N; ++i) {
        futs.push_back(asio::co_spawn(
            ioc_b, make_waiter(i), asio::bind_cancellation_slot(sigs[i].slot(), asio::use_future)));
    }
    for (int i = 0; i < 16; ++i) ioc_b.poll_one();
    ASSERT_EQ(total_completed.load(std::memory_order_acquire), 0)
        << "setup: a waiter resolved before parking";

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    std::thread thread_a([&] { ioc_a.run(); });
    std::thread thread_b([&] { ioc_b.run(); });

    bool h_ready = wait_ready(fh, deadline);
    bool all_w_ready = true;
    for (auto& f : futs) all_w_ready = wait_ready(f, deadline) && all_w_ready;
    if (!h_ready || !all_w_ready) {
        ioc_a.stop();
        ioc_b.stop();
    }
    thread_a.join();
    thread_b.join();

    ASSERT_TRUE(h_ready) << "holder thread timed out";
    ASSERT_TRUE(all_w_ready) << "at least one waiter thread timed out";
    fh.get();
    for (auto& f : futs) f.get();

    EXPECT_EQ(total_completed.load(), N) << "All waiters must complete exactly once";

    int g = granted_count.load();
    int a = aborted_count.load();
    EXPECT_EQ(g + a, N) << "granted + aborted must cover all N waiters";
    EXPECT_GE(g, 1) << "At least one waiter must be granted";
}

// ─────────────────────────────────────────────────────────────────────────────
// Stress: fire cancel on every waiter simultaneously with the unlock drain.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamRaceCancelPreDrain, StressSimultaneousCancelAndDrain) {
    constexpr int N = 8;

    std::vector<asio::cancellation_signal> sigs(N);
    std::atomic<int> granted_count{0};
    std::atomic<int> aborted_count{0};
    std::atomic<int> total_completed{0};

    asio::io_context ioc_a;
    asio::io_context ioc_b;
    async_mutex mtx;

    bool holder_acquired = false;
    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        holder_acquired = g.has_value();
        EXPECT_TRUE(holder_acquired);
        co_await yield_n(N * 4);
        for (int i = 0; i < N; ++i) {
            asio::post(ioc_b.get_executor(),
                       [&sigs, i] { sigs[i].emit(asio::cancellation_type::total); });
        }
        co_await yield_n(N);
        // unlock.
    };

    auto make_waiter = [&](int idx) -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx.async_lock();
        if (r.has_value()) {
            granted_count.fetch_add(1, std::memory_order_acq_rel);
            co_await yield_n(1);
        } else {
            EXPECT_EQ(r.error(), error::sync_lock_aborted);
            aborted_count.fetch_add(1, std::memory_order_acq_rel);
        }
        total_completed.fetch_add(1, std::memory_order_acq_rel);
    };

    auto fh = asio::co_spawn(ioc_a, holder(), asio::use_future);
    for (int i = 0; i < 16 && !holder_acquired; ++i) ioc_a.poll_one();
    ASSERT_TRUE(holder_acquired) << "setup: holder failed to acquire";

    std::vector<std::future<void>> futs;
    for (int i = 0; i < N; ++i) {
        futs.push_back(asio::co_spawn(
            ioc_b, make_waiter(i), asio::bind_cancellation_slot(sigs[i].slot(), asio::use_future)));
    }
    for (int i = 0; i < 32; ++i) ioc_b.poll_one();
    ASSERT_EQ(total_completed.load(std::memory_order_acquire), 0)
        << "setup: a waiter resolved before parking";

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    std::thread thread_a([&] { ioc_a.run(); });
    std::thread thread_b([&] { ioc_b.run(); });

    bool h_ready = wait_ready(fh, deadline);
    bool all_w_ready = true;
    for (auto& f : futs) all_w_ready = wait_ready(f, deadline) && all_w_ready;
    if (!h_ready || !all_w_ready) {
        ioc_a.stop();
        ioc_b.stop();
    }
    thread_a.join();
    thread_b.join();

    ASSERT_TRUE(h_ready) << "holder thread timed out";
    ASSERT_TRUE(all_w_ready) << "at least one waiter thread timed out";
    fh.get();
    for (auto& f : futs) f.get();

    EXPECT_EQ(total_completed.load(), N);
    EXPECT_EQ(granted_count.load() + aborted_count.load(), N);
}

// ─────────────────────────────────────────────────────────────────────────────
// Verify mutex is free after race — destruction succeeds.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamRaceCancelPreDrain, MutexFreeAfterRace) {
    constexpr int N = 4;

    std::vector<asio::cancellation_signal> sigs(N);
    std::atomic<int> total{0};

    asio::io_context ioc_a;
    asio::io_context ioc_b;
    async_mutex mtx;

    bool holder_acquired = false;
    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        holder_acquired = g.has_value();
        EXPECT_TRUE(holder_acquired);
        co_await yield_n(N * 3);
        for (int i = 0; i < N; ++i) {
            asio::post(ioc_b.get_executor(),
                       [&sigs, i] { sigs[i].emit(asio::cancellation_type::total); });
        }
        co_await yield_n(N);
    };

    auto make_waiter = [&](int idx) -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx.async_lock();
        (void)r;
        total.fetch_add(1, std::memory_order_acq_rel);
        if (r.has_value()) co_await yield_n(1);
    };

    auto fh = asio::co_spawn(ioc_a, holder(), asio::use_future);
    for (int i = 0; i < 16 && !holder_acquired; ++i) ioc_a.poll_one();
    ASSERT_TRUE(holder_acquired) << "setup: holder failed to acquire";

    std::vector<std::future<void>> futs;
    for (int i = 0; i < N; ++i) {
        futs.push_back(asio::co_spawn(
            ioc_b, make_waiter(i), asio::bind_cancellation_slot(sigs[i].slot(), asio::use_future)));
    }
    for (int i = 0; i < 32; ++i) ioc_b.poll_one();
    ASSERT_EQ(total.load(std::memory_order_acquire), 0) << "setup: a waiter resolved before parking";

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    std::thread thread_a([&] { ioc_a.run(); });
    std::thread thread_b([&] { ioc_b.run(); });

    bool h_ready = wait_ready(fh, deadline);
    bool all_w_ready = true;
    for (auto& f : futs) all_w_ready = wait_ready(f, deadline) && all_w_ready;
    if (!h_ready || !all_w_ready) {
        ioc_a.stop();
        ioc_b.stop();
    }
    thread_a.join();
    thread_b.join();

    ASSERT_TRUE(h_ready) << "holder thread timed out";
    ASSERT_TRUE(all_w_ready) << "at least one waiter thread timed out";
    fh.get();
    for (auto& f : futs) f.get();

    EXPECT_EQ(total.load(), N);
    // mtx destruction here — must not terminate (state_ == not_locked).
}

}  // namespace
