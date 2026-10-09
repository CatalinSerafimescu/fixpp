// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_race_cancel_during_resume.cpp — Seam #17
//
// Cancel-during-await_resume race (RC#1).
//
// A waiter's drain CAS wins (it gets the lock). A second cancellation signal
// arrives on the same slot after the grant. The second signal must be a no-op:
//   - the waiter receives the guard (not unexpected);
//   - the slot is cleared cleanly.
//
// [2f §4.2.3]: "Cancellation-after-resume is a no-op: the slot is cleared, and
//   even if a stale signal arrives between the read and the clear, the lambda's
//   first action is to CAS-acquire phase_; the CAS fails because phase is
//   already terminal."
// [2f §4.5.1 window 3]: stale post-resumption cancel is no-op.
//
// Oracle: [2f §9 #17] — "cancel-during-await_resume race" (RC#1).
// SC-002: TSan-clean.
//
// 058-async-mutex-hardening T027 (US5, FR-008): converted from a
// single-threaded-in-disguise `ioc.run()` driver (holder + waiter + the
// cancel emission were all sequenced deterministically by ONE io_context on
// the calling thread — feedback_single_threaded_harness_masks_strand_races)
// to genuinely multi-threaded: the holder runs on its own io_context serviced
// by a dedicated OS thread (thread_a), the waiter (and its cancellation
// slot) on a SEPARATE io_context/thread (thread_b). Where a race against the
// unlock()-drain's grant CAS is exercised (tests 2 and 3), the cancellation
// signal's `emit()` call is POSTED onto the waiter's own executor
// (asio::post(ioc_b.get_executor(), ...)) rather than fired from a third
// (e.g. main-test) thread: asio::cancellation_signal/slot are NOT
// thread-safe, and the completion path clears the slot on the waiter's own
// executor (async_mutex.hpp's resume runner) — firing emit() from an
// unsynchronized third thread would race asio's own (non-thread-safe) slot
// bookkeeping, producing a TSan report that is a HARNESS bug, not evidence
// about async_mutex's phase_ arbitration. Keeping emit() confined to the
// waiter's executor (thread_b) while the grant CAS runs via unlock()'s drain
// on the holder's executor (thread_a) still creates a genuine, unsynchronized
// cross-thread race on the shared `phase_` atomic — the two threads are never
// causally ordered by this test's own code, only by the primitive's CAS
// arbitration and real OS scheduling.
//
// Setup (holder acquires; waiter parks) is driven deterministically via
// bounded single-threaded `poll_one()` loops BEFORE either background thread
// starts (same idiom as test_drain_destroy_inflight_mt.cpp) — only the
// interesting cancel-vs-grant race itself runs genuinely concurrently.

#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <fixpp/core/sync/async_mutex.hpp>
#include <future>
#include <optional>
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
// Test 1: basic no-op after resume.
// Waiter acquires (granted by unlock drain). A cancel signal fires after resume.
// The signal must be ignored: waiter keeps the lock.
//
// This scenario is sequential-by-construction (the waiter fires the cancel on
// its OWN already-resolved slot, from within its own coroutine) — there is no
// external race to contend. It is still driven on genuinely separate OS
// threads (holder on thread_a, waiter on thread_b) so the driver mechanics
// match the rest of the file; the discriminator here is the sequential no-op
// logic, not thread contention.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamRaceCancelDuringResume, LateSignalIsNoOpWaiterKeepsLock) {
    asio::cancellation_signal cancel_sig;

    bool waiter_got_lock = false;
    bool waiter_kept_lock = false;

    asio::io_context ioc_a;  // holder's own context/thread
    asio::io_context ioc_b;  // waiter's own context/thread
    async_mutex mtx;

    bool holder_acquired = false;
    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        holder_acquired = g.has_value();
        EXPECT_TRUE(holder_acquired);
        co_await yield_n(4);
        // unlock here — waiter gets granted.
    };

    std::atomic<bool> waiter_resolved{false};
    auto waiter = [&]() -> asio::awaitable<void> {
        co_await yield_n(1);

        // co_await mtx.async_lock() — gets granted after holder releases.
        auto r = co_await mtx.async_lock();

        // At this point, the waiter has been granted. phase_ == granted (terminal).
        // The slot was cleared inside async_lock()'s completion.
        waiter_got_lock = r.has_value();

        if (waiter_got_lock) {
            // Fire a "late" cancel on OUR OWN slot, from OUR OWN executor
            // (thread_b) — the slot is already cleared; if it fires the
            // cancel handler, the phase_ CAS will fail (terminal) → no-op.
            cancel_sig.emit(asio::cancellation_type::total);

            co_await yield_n(2);

            waiter_kept_lock = r->owns_lock();
        }
        waiter_resolved.store(true, std::memory_order_release);
        // guard dtor → unlock().
    };

    auto fh = asio::co_spawn(ioc_a, holder(), asio::use_future);
    for (int i = 0; i < 16 && !holder_acquired; ++i) ioc_a.poll_one();
    ASSERT_TRUE(holder_acquired) << "setup: holder failed to acquire";

    // Wire the cancel signal into the waiter's cancellation state.
    auto fw = asio::co_spawn(ioc_b, waiter(),
                             asio::bind_cancellation_slot(cancel_sig.slot(), asio::use_future));
    for (int i = 0; i < 16 && !waiter_resolved.load(std::memory_order_acquire); ++i)
        ioc_b.poll_one();
    ASSERT_FALSE(waiter_resolved.load(std::memory_order_acquire))
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

    EXPECT_TRUE(waiter_got_lock) << "Waiter must receive the granted lock";
    EXPECT_TRUE(waiter_kept_lock) << "Late cancel signal must NOT revoke the already-granted lock";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: N rounds — grant or cancel, exactly one outcome per waiter.
// Each round: holder holds; waiter queues with cancel pre-wired; the cancel
// is posted onto the WAITER's own executor (thread_b) while the holder
// continues toward its own unlock on ITS own executor (thread_a) —
// genuinely racing the phase_ CAS across two real OS threads. Either the
// cancel wins (aborted) or the grant wins (guard). Sum must equal N (no
// double-resume, no lost waiter).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamRaceCancelDuringResume, GrantOrCancelExactlyOneOutcomePerWaiter) {
    constexpr int ROUNDS = 32;

    std::atomic<int> lock_granted{0};
    std::atomic<int> lock_aborted{0};

    async_mutex mtx;  // reused across rounds — each round leaves it not_locked.

    for (int r = 0; r < ROUNDS; ++r) {
        SCOPED_TRACE(r);

        asio::io_context ioc_a;  // holder's own context/thread this round
        asio::io_context ioc_b;  // waiter's own context/thread this round
        asio::cancellation_signal cancel_sig;
        std::atomic<bool> waiter_completed{false};

        bool holder_acquired = false;
        auto holder = [&]() -> asio::awaitable<void> {
            auto g = co_await mtx.async_lock();
            holder_acquired = g.has_value();
            EXPECT_TRUE(holder_acquired);
            co_await yield_n(2);
            // Post the cancel onto the WAITER's own executor — races with
            // this coroutine's own upcoming unlock (on ioc_a/thread_a).
            asio::post(ioc_b.get_executor(),
                       [&cancel_sig] { cancel_sig.emit(asio::cancellation_type::total); });
            co_await yield_n(1);
            // unlock.
        };

        auto waiter = [&]() -> asio::awaitable<void> {
            co_await yield_n(1);
            auto res = co_await mtx.async_lock();
            if (res.has_value()) {
                lock_granted.fetch_add(1, std::memory_order_acq_rel);
                co_await yield_n(1);
            } else {
                EXPECT_EQ(res.error(), error::sync_lock_aborted);
                lock_aborted.fetch_add(1, std::memory_order_acq_rel);
            }
            waiter_completed.store(true, std::memory_order_release);
        };

        auto fh = asio::co_spawn(ioc_a, holder(), asio::use_future);
        for (int i = 0; i < 16 && !holder_acquired; ++i) ioc_a.poll_one();
        ASSERT_TRUE(holder_acquired) << "setup: holder failed to acquire";

        auto fw = asio::co_spawn(ioc_b, waiter(),
                                 asio::bind_cancellation_slot(cancel_sig.slot(), asio::use_future));
        for (int i = 0; i < 16 && !waiter_completed.load(std::memory_order_acquire); ++i)
            ioc_b.poll_one();
        ASSERT_FALSE(waiter_completed.load(std::memory_order_acquire))
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

        ASSERT_TRUE(h_ready) << "Round " << r << ": holder thread timed out";
        ASSERT_TRUE(w_ready) << "Round " << r << ": waiter thread timed out";
        fh.get();
        fw.get();

        EXPECT_TRUE(waiter_completed.load(std::memory_order_acquire))
            << "Round " << r << ": waiter must always complete";
    }

    EXPECT_EQ(lock_granted.load() + lock_aborted.load(), ROUNDS)
        << "Every round's waiter must complete exactly once";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: mutex free after mixed grant/cancel rounds.
// No std::terminate on destruction.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamRaceCancelDuringResume, MutexFreeAfterRace) {
    constexpr int N = 16;

    std::vector<asio::cancellation_signal> sigs(N);
    std::atomic<int> total{0};

    asio::io_context ioc_a;  // holder's own context/thread
    asio::io_context ioc_b;  // every waiter's own context/thread (shared)
    async_mutex mtx;

    bool holder_acquired = false;
    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        holder_acquired = g.has_value();
        EXPECT_TRUE(holder_acquired);
        co_await yield_n(N * 2);
        // Post every cancel onto the (shared) waiter executor — races with
        // this coroutine's own upcoming unlock.
        for (int i = 0; i < N; ++i) {
            asio::post(ioc_b.get_executor(),
                       [&sigs, i] { sigs[i].emit(asio::cancellation_type::total); });
        }
        co_await yield_n(N);
    };

    auto make_waiter = [&](int /*idx*/) -> asio::awaitable<void> {
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
    futs.reserve(N);
    for (int i = 0; i < N; ++i) {
        futs.push_back(asio::co_spawn(
            ioc_b, make_waiter(i), asio::bind_cancellation_slot(sigs[i].slot(), asio::use_future)));
    }
    for (int i = 0; i < 32; ++i) ioc_b.poll_one();
    ASSERT_EQ(total.load(std::memory_order_acquire), 0)
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

    EXPECT_EQ(total.load(), N);
    // mtx destruction — must not std::terminate.
}

// ─────────────────────────────────────────────────────────────────────────────
// 093-inbound-frame-dispositions (plan OD-25): a granted async_lock() leaves its
// caller's cancellation filter terminal-only, whatever the caller had set. After the
// grant, a total emission does not cancel the caller's next await; a terminal one
// still does. 093's 141=Y reset unit (Session::run_reset_unit_) runs its store
// operation on an empty cancellation slot because of this: a filter the unit set
// before its first seqnum lock would not survive that lock. A change to async_lock()
// that fails the total arm here changes the premise of that ruling: re-check it.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamRaceCancelDuringResume, Od25_GrantLeavesTheCallersFilterTerminalOnly) {
    for (auto const type : {asio::cancellation_type::total, asio::cancellation_type::terminal}) {
        SCOPED_TRACE(type == asio::cancellation_type::total ? "total" : "terminal");
        asio::io_context ioc;
        async_mutex mtx;
        asio::cancellation_signal sig;
        bool granted = false;
        std::optional<asio::error_code> wait_ec;
        auto fut = asio::co_spawn(
            ioc,
            [&]() -> asio::awaitable<void> {
                co_await asio::this_coro::reset_cancellation_state(
                    asio::enable_total_cancellation{});
                auto guard = co_await mtx.async_lock();
                granted = guard.has_value();
                asio::steady_timer t{ioc, std::chrono::milliseconds{200}};
                asio::error_code ec;
                co_await t.async_wait(asio::redirect_error(asio::use_awaitable, ec));
                wait_ec = ec;
            },
            asio::bind_cancellation_slot(sig.slot(), asio::use_future));
        for (int i = 0; i < 1000 && !granted; ++i) {
            (void)ioc.run_one_for(std::chrono::milliseconds{10});
        }
        ASSERT_TRUE(granted) << "the uncontended lock was not granted";
        sig.emit(type);
        // Run until the coroutine completes, bounded as the grant loop above is; the
        // readiness assertion below names the future before get().
        for (int i = 0;
             i < 500 && fut.wait_for(std::chrono::seconds{0}) != std::future_status::ready; ++i) {
            (void)ioc.run_one_for(std::chrono::milliseconds{10});
        }
        ASSERT_EQ(fut.wait_for(std::chrono::seconds{0}), std::future_status::ready);
        fut.get();
        ASSERT_TRUE(wait_ec.has_value());
        // The ASSERT_TRUE above returns on an empty optional; the check does not model it.
        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        if (type == asio::cancellation_type::total) {
            EXPECT_FALSE(*wait_ec) << "a total emission after the grant cancelled the next await: "
                                   << wait_ec->message();
        } else {
            EXPECT_EQ(*wait_ec, asio::error::operation_aborted)
                << "a terminal emission after the grant did not cancel the next await";
        }
        // NOLINTEND(bugprone-unchecked-optional-access)
    }
}

}  // namespace
