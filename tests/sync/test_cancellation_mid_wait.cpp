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
//
// 058-async-mutex-hardening T031 (US5, FR-008): converted from a
// single-threaded-in-disguise `ioc.run()` driver (holder + waiter(s) + every
// cancel emission were all sequenced deterministically by ONE io_context on
// the calling thread — feedback_single_threaded_harness_masks_strand_races)
// to genuinely multi-threaded: the holder runs on its own io_context serviced
// by a dedicated OS thread (thread_a); the waiter(s) (and their cancellation
// slot(s)) run on a SEPARATE io_context/thread (thread_b). Every cancellation
// signal is POSTED onto the waiters' own executor (thread_b) rather than
// fired directly from the holder's thread: asio::cancellation_signal/slot are
// NOT thread-safe, and the completion path clears each slot on the resuming
// waiter's own executor — firing emit() from an unsynchronized thread would
// race asio's own (non-thread-safe) slot bookkeeping (a harness bug, not
// evidence about async_mutex's phase_ arbitration). Keeping every emit()
// confined to thread_b while the grant CAS chain runs via unlock()'s drain on
// the holder's executor (thread_a) still creates a genuine, unsynchronized
// cross-thread race on each targeted waiter's shared `phase_` atomic — the
// two threads are never causally ordered by this test's own code, only by
// the primitive's CAS arbitration and real OS scheduling.
//
// Setup (holder acquires; waiter(s) park) is driven deterministically via
// bounded single-threaded `poll_one()` loops BEFORE either background thread
// starts (same idiom as test_drain_destroy_inflight_mt.cpp / test_race_cancel_
// during_resume.cpp).
//
// UNLIKE test_race_cancel_during_resume.cpp / test_race_multi_cancel.cpp /
// test_race_cancel_pre_drain.cpp (T027-T029), this seam (#4, "cancellation
// mid-wait") is NOT a race: its contract is that a cancel fired while a
// waiter is genuinely parked (no contending drain in flight yet) MUST
// deterministically abort that waiter — a single required outcome, not
// "exactly one of {granted, cancelled}". Letting the holder's own unlock()
// run concurrently and unsynchronized with the cross-thread cancel delivery
// would turn this into an actual grant-vs-cancel race (T027's scenario) and
// make the fixed `EXPECT_FALSE(...has_value())` assertions below flaky by
// construction — sometimes the grant CAS would legitimately win. So each
// holder here still runs on its own OS thread (thread_a) and the waiter(s)
// on a genuinely separate one (thread_b) — the cross-executor cancellation
// delivery itself is real, unsynchronized, cross-thread machinery, not
// same-thread-in-disguise — but the holder additionally BARRIERS on the
// waiter's completion flag (a cross-thread-safe atomic busy-poll via
// `asio::post`, not a std::mutex/condition_variable — FR-012) before
// proceeding to its own unlock, pinning the ordering the deterministic
// single-required-outcome scenario needs while still exercising the real
// cross-thread post + cancellation-slot delivery path under TSan.

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
#include <chrono>
#include <fixpp/core/sync/async_mutex.hpp>
#include <future>
#include <optional>
#include <thread>

#include "sync/sync_test_support.hpp"

namespace {

using fixpp::core::error;
using fixpp::sync::async_lock_guard;
using fixpp::sync::async_mutex;
using fixpp::sync::expected_t;

// Post N yields on the calling coroutine's executor.
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

// Cross-thread-safe barrier: reposts on the CALLER's own executor until
// `flag` (set with release ordering on another OS thread) is observed true
// (acquire load). Used by each holder below to pin "cancellation fully
// delivered" strictly BEFORE its own unlock, without a std::mutex/
// condition_variable (FR-012) and without collapsing the two roles onto one
// thread.
asio::awaitable<void> wait_flag(std::atomic<bool> const& flag) {
    auto ex = co_await asio::this_coro::executor;
    while (!flag.load(std::memory_order_acquire)) co_await asio::post(ex, asio::use_awaitable);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: total — basic mid-wait cancel.
//   Holder acquires (thread_a). Waiter suspends (pushed onto LIFO, thread_b).
//   Cancel signal fires (posted onto thread_b). Waiter must resume with
//   sync_lock_aborted.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamCancellationMidWait, TotalCancelYieldsSyncLockAborted) {
    asio::cancellation_signal cancel_sig;

    std::optional<expected_t<async_lock_guard>> waiter_result;
    std::atomic<bool> waiter_ran{false};
    int holder_counter = 0;

    asio::io_context ioc_a;  // holder's own context/thread
    asio::io_context ioc_b;  // waiter's own context/thread
    async_mutex mtx;

    bool holder_acquired = false;
    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        holder_acquired = g.has_value();
        EXPECT_TRUE(holder_acquired);
        co_await yield_n(2);
        // Fire cancellation_type::total on the waiter's slot — posted onto
        // the waiter's OWN executor (thread_b), genuinely cross-thread from
        // this coroutine (thread_a).
        asio::post(ioc_b.get_executor(),
                   [&cancel_sig] { cancel_sig.emit(asio::cancellation_type::total); });
        // Barrier (see file header): wait for the cancellation to be fully
        // delivered on thread_b before this holder's own unlock — this seam
        // is a deterministic single-required-outcome contract, not a race.
        co_await wait_flag(waiter_ran);
        ++holder_counter;
        // Guard dtor → unlock().
    };

    auto waiter = [&]() -> asio::awaitable<void> {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        waiter_result = co_await mtx.async_lock();
        waiter_ran.store(true, std::memory_order_release);
    };

    auto fh = asio::co_spawn(ioc_a, holder(), asio::use_future);
    for (int i = 0; i < 16 && !holder_acquired; ++i) ioc_a.poll_one();
    ASSERT_TRUE(holder_acquired) << "setup: holder failed to acquire";

    // Bind the cancellation slot to the waiter's co_spawn token so the
    // waiter coroutine's this_coro::cancellation_state propagates into
    // co_await mtx.async_lock() → on_cancel registration.
    auto fw = asio::co_spawn(ioc_b, waiter(),
                             asio::bind_cancellation_slot(cancel_sig.slot(), asio::use_future));
    for (int i = 0; i < 16 && !waiter_ran.load(std::memory_order_acquire); ++i) ioc_b.poll_one();
    ASSERT_FALSE(waiter_ran.load(std::memory_order_acquire))
        << "setup: waiter resolved before parking — the mutex was not held";

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::thread thread_a([&] { ioc_a.run(); });
    std::thread thread_b([&] { ioc_b.run(); });

    bool h_ready = wait_ready(fh, deadline);
    bool w_ready = wait_ready(fw, deadline);
    if (!h_ready || !w_ready) {
        ioc_a.stop();
        ioc_b.stop();
    }
    thread_a.join();
    thread_b.join();

    ASSERT_TRUE(h_ready) << "holder thread timed out";
    ASSERT_TRUE(w_ready) << "waiter thread timed out";
    fh.get();
    fw.get();

    EXPECT_TRUE(waiter_ran.load(std::memory_order_acquire)) << "Waiter must complete";
    ASSERT_TRUE(waiter_result.has_value());

    // Primary assertion: waiter must see sync_lock_aborted.
    EXPECT_FALSE(waiter_result->has_value()) << "Cancelled waiter must NOT hold the lock";
    EXPECT_EQ(waiter_result->error(), error::sync_lock_aborted)
        << "Cancelled waiter error must be sync_lock_aborted";

    EXPECT_EQ(holder_counter, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: partial — treated as total (no partial acquisition semantics).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamCancellationMidWait, PartialCancelTreatedAsTotal) {
    asio::cancellation_signal cancel_sig;

    std::optional<expected_t<async_lock_guard>> waiter_result;
    std::atomic<bool> waiter_ran{false};

    asio::io_context ioc_a;
    asio::io_context ioc_b;
    async_mutex mtx;

    bool holder_acquired = false;
    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        holder_acquired = g.has_value();
        EXPECT_TRUE(holder_acquired);
        co_await yield_n(2);
        asio::post(ioc_b.get_executor(),
                   [&cancel_sig] { cancel_sig.emit(asio::cancellation_type::partial); });
        co_await wait_flag(waiter_ran);  // see file header — deterministic, not a race
    };

    auto waiter = [&]() -> asio::awaitable<void> {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        waiter_result = co_await mtx.async_lock();
        waiter_ran.store(true, std::memory_order_release);
    };

    auto fh = asio::co_spawn(ioc_a, holder(), asio::use_future);
    for (int i = 0; i < 16 && !holder_acquired; ++i) ioc_a.poll_one();
    ASSERT_TRUE(holder_acquired) << "setup: holder failed to acquire";

    auto fw = asio::co_spawn(ioc_b, waiter(),
                             asio::bind_cancellation_slot(cancel_sig.slot(), asio::use_future));
    for (int i = 0; i < 16 && !waiter_ran.load(std::memory_order_acquire); ++i) ioc_b.poll_one();
    ASSERT_FALSE(waiter_ran.load(std::memory_order_acquire))
        << "setup: waiter resolved before parking";

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::thread thread_a([&] { ioc_a.run(); });
    std::thread thread_b([&] { ioc_b.run(); });

    bool h_ready = wait_ready(fh, deadline);
    bool w_ready = wait_ready(fw, deadline);
    if (!h_ready || !w_ready) {
        ioc_a.stop();
        ioc_b.stop();
    }
    thread_a.join();
    thread_b.join();

    ASSERT_TRUE(h_ready) << "holder thread timed out";
    ASSERT_TRUE(w_ready) << "waiter thread timed out";
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
    asio::cancellation_signal cancel_sig;

    std::optional<expected_t<async_lock_guard>> waiter_result;
    std::atomic<bool> waiter_ran{false};

    asio::io_context ioc_a;
    asio::io_context ioc_b;
    async_mutex mtx;

    bool holder_acquired = false;
    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        holder_acquired = g.has_value();
        EXPECT_TRUE(holder_acquired);
        co_await yield_n(2);
        asio::post(ioc_b.get_executor(),
                   [&cancel_sig] { cancel_sig.emit(asio::cancellation_type::terminal); });
        co_await wait_flag(waiter_ran);  // see file header — deterministic, not a race
    };

    auto waiter = [&]() -> asio::awaitable<void> {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        waiter_result = co_await mtx.async_lock();
        waiter_ran.store(true, std::memory_order_release);
    };

    auto fh = asio::co_spawn(ioc_a, holder(), asio::use_future);
    for (int i = 0; i < 16 && !holder_acquired; ++i) ioc_a.poll_one();
    ASSERT_TRUE(holder_acquired) << "setup: holder failed to acquire";

    auto fw = asio::co_spawn(ioc_b, waiter(),
                             asio::bind_cancellation_slot(cancel_sig.slot(), asio::use_future));
    for (int i = 0; i < 16 && !waiter_ran.load(std::memory_order_acquire); ++i) ioc_b.poll_one();
    ASSERT_FALSE(waiter_ran.load(std::memory_order_acquire))
        << "setup: waiter resolved before parking";

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::thread thread_a([&] { ioc_a.run(); });
    std::thread thread_b([&] { ioc_b.run(); });

    bool h_ready = wait_ready(fh, deadline);
    bool w_ready = wait_ready(fw, deadline);
    if (!h_ready || !w_ready) {
        ioc_a.stop();
        ioc_b.stop();
    }
    thread_a.join();
    thread_b.join();

    ASSERT_TRUE(h_ready) << "holder thread timed out";
    ASSERT_TRUE(w_ready) << "waiter thread timed out";
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
    asio::cancellation_signal cancel_sig;

    std::atomic<bool> cancelled_ran{false};
    std::atomic<bool> second_ran{false};
    bool cancelled_got_lock = false;
    bool second_got_lock = false;

    asio::io_context ioc_a;  // holder's own context/thread
    asio::io_context ioc_b;  // both waiters' own context/thread (shared)
    async_mutex mtx;

    bool holder_acquired = false;
    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        holder_acquired = g.has_value();
        EXPECT_TRUE(holder_acquired);
        co_await yield_n(2);
        asio::post(ioc_b.get_executor(),
                   [&cancel_sig] { cancel_sig.emit(asio::cancellation_type::total); });
        co_await wait_flag(cancelled_ran);  // see file header — deterministic, not a race
        // unlock here via guard dtor.
    };

    auto cancelled_waiter = [&]() -> asio::awaitable<void> {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        auto r = co_await mtx.async_lock();
        cancelled_got_lock = r.has_value();
        cancelled_ran.store(true, std::memory_order_release);
    };

    // Second waiter: no cancellation signal — must eventually get the lock.
    auto second_waiter = [&]() -> asio::awaitable<void> {
        co_await yield_n(1);
        auto g = co_await mtx.async_lock();
        second_got_lock = g.has_value();
        second_ran.store(true, std::memory_order_release);
    };

    auto fh = asio::co_spawn(ioc_a, holder(), asio::use_future);
    for (int i = 0; i < 16 && !holder_acquired; ++i) ioc_a.poll_one();
    ASSERT_TRUE(holder_acquired) << "setup: holder failed to acquire";

    auto fcw = asio::co_spawn(ioc_b, cancelled_waiter(),
                              asio::bind_cancellation_slot(cancel_sig.slot(), asio::use_future));
    auto fsw = asio::co_spawn(ioc_b, second_waiter(), asio::use_future);
    for (int i = 0; i < 16 && !cancelled_ran.load(std::memory_order_acquire) &&
                    !second_ran.load(std::memory_order_acquire);
         ++i)
        ioc_b.poll_one();
    ASSERT_FALSE(cancelled_ran.load(std::memory_order_acquire))
        << "setup: cancelled waiter resolved before parking";
    ASSERT_FALSE(second_ran.load(std::memory_order_acquire))
        << "setup: second waiter resolved before parking";

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::thread thread_a([&] { ioc_a.run(); });
    std::thread thread_b([&] { ioc_b.run(); });

    bool h_ready = wait_ready(fh, deadline);
    bool cw_ready = wait_ready(fcw, deadline);
    bool sw_ready = wait_ready(fsw, deadline);
    if (!h_ready || !cw_ready || !sw_ready) {
        ioc_a.stop();
        ioc_b.stop();
    }
    thread_a.join();
    thread_b.join();

    ASSERT_TRUE(h_ready) << "holder thread timed out";
    ASSERT_TRUE(cw_ready) << "cancelled-waiter thread timed out";
    ASSERT_TRUE(sw_ready) << "second-waiter thread timed out";
    fh.get();
    fcw.get();
    fsw.get();

    EXPECT_FALSE(cancelled_got_lock) << "Cancelled waiter must NOT acquire";
    EXPECT_TRUE(second_got_lock) << "Second waiter must eventually acquire";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5: LIFO list is empty after cancellation completes.
//   After a waiter is cancelled, subsequent destruction succeeds (no std::terminate).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamCancellationMidWait, MutexEmptyAfterCancellation) {
    asio::cancellation_signal cancel_sig;
    std::atomic<bool> waiter_ran{false};

    asio::io_context ioc_a;
    asio::io_context ioc_b;
    async_mutex mtx;

    bool holder_acquired = false;
    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        holder_acquired = g.has_value();
        co_await yield_n(2);
        asio::post(ioc_b.get_executor(),
                   [&cancel_sig] { cancel_sig.emit(asio::cancellation_type::total); });
        co_await wait_flag(waiter_ran);  // see file header — deterministic, not a race
    };

    auto waiter = [&]() -> asio::awaitable<void> {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        auto r = co_await mtx.async_lock();
        EXPECT_FALSE(r.has_value());
        waiter_ran.store(true, std::memory_order_release);
    };

    auto fh = asio::co_spawn(ioc_a, holder(), asio::use_future);
    for (int i = 0; i < 16 && !holder_acquired; ++i) ioc_a.poll_one();
    ASSERT_TRUE(holder_acquired) << "setup: holder failed to acquire";

    auto fw = asio::co_spawn(ioc_b, waiter(),
                             asio::bind_cancellation_slot(cancel_sig.slot(), asio::use_future));
    for (int i = 0; i < 16 && !waiter_ran.load(std::memory_order_acquire); ++i) ioc_b.poll_one();
    ASSERT_FALSE(waiter_ran.load(std::memory_order_acquire))
        << "setup: waiter resolved before parking";

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::thread thread_a([&] { ioc_a.run(); });
    std::thread thread_b([&] { ioc_b.run(); });

    bool h_ready = wait_ready(fh, deadline);
    bool w_ready = wait_ready(fw, deadline);
    if (!h_ready || !w_ready) {
        ioc_a.stop();
        ioc_b.stop();
    }
    thread_a.join();
    thread_b.join();

    ASSERT_TRUE(h_ready) << "holder thread timed out";
    ASSERT_TRUE(w_ready) << "waiter thread timed out";
    fh.get();
    fw.get();
    // mtx destructs here — must not std::terminate (LIFO empty).
}

}  // namespace
