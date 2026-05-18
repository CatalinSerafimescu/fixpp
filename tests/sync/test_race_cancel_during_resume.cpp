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
#include <vector>

#include <fixpp/core/sync/async_mutex.hpp>

namespace {

using fixpp::sync::async_mutex;
using fixpp::core::error;

static asio::awaitable<void> yield_n(int n) {
    auto ex = co_await asio::this_coro::executor;
    for (int i = 0; i < n; ++i)
        co_await asio::post(ex, asio::use_awaitable);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: basic no-op after resume.
// Waiter acquires (granted by unlock drain). A cancel signal fires after resume.
// The signal must be ignored: waiter keeps the lock.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamRaceCancelDuringResume, LateSignalIsNoOpWaiterKeepsLock) {
    async_mutex mtx;
    asio::cancellation_signal cancel_sig;

    bool waiter_got_lock = false;
    bool waiter_kept_lock = false;

    asio::io_context ioc;

    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        co_await yield_n(4);
        // unlock here — waiter gets granted.
    };

    auto waiter = [&]() -> asio::awaitable<void> {
        co_await yield_n(1);

        // co_await mtx.async_lock() — gets granted after holder releases.
        auto r = co_await mtx.async_lock();

        // At this point, the waiter has been granted. phase_ == granted (terminal).
        // The slot was cleared inside async_lock()'s completion.
        waiter_got_lock = r.has_value();

        if (waiter_got_lock) {
            // Fire a "late" cancel — the slot is already cleared; if it fires
            // the cancel handler, the phase_ CAS will fail (terminal) → no-op.
            cancel_sig.emit(asio::cancellation_type::total);

            co_await yield_n(2);

            waiter_kept_lock = r->owns_lock();
        }
        // guard dtor → unlock().
    };

    auto fh = asio::co_spawn(ioc, holder(), asio::use_future);
    // Wire the cancel signal into the waiter's cancellation state.
    auto fw = asio::co_spawn(ioc, waiter(),
                             asio::bind_cancellation_slot(cancel_sig.slot(),
                                                          asio::use_future));
    ioc.run();
    fh.get();
    fw.get();

    EXPECT_TRUE(waiter_got_lock)
        << "Waiter must receive the granted lock";
    EXPECT_TRUE(waiter_kept_lock)
        << "Late cancel signal must NOT revoke the already-granted lock";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: N rounds — grant or cancel, exactly one outcome per waiter.
// Each round: holder holds; waiter queues with cancel pre-wired; holder fires
// cancel; holder releases. Either the cancel wins (aborted) or the grant wins
// (guard). Sum must equal N (no double-resume, no lost waiter).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamRaceCancelDuringResume, GrantOrCancelExactlyOneOutcomePerWaiter) {
    constexpr int ROUNDS = 32;
    async_mutex mtx;

    std::atomic<int> lock_granted{0};
    std::atomic<int> lock_aborted{0};

    asio::io_context ioc;

    for (int r = 0; r < ROUNDS; ++r) {
        asio::cancellation_signal cancel_sig;
        bool waiter_completed = false;

        auto holder = [&]() -> asio::awaitable<void> {
            auto g = co_await mtx.async_lock();
            EXPECT_TRUE(g.has_value());
            co_await yield_n(2);
            // Fire cancel before releasing — races with grant CAS.
            cancel_sig.emit(asio::cancellation_type::total);
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
            waiter_completed = true;
        };

        auto fh = asio::co_spawn(ioc, holder(), asio::use_future);
        auto fw = asio::co_spawn(ioc, waiter(),
                                 asio::bind_cancellation_slot(cancel_sig.slot(),
                                                              asio::use_future));
        ioc.restart();  // drained by the prior round's run(); restart each round.
        ioc.run();
        fh.get();
        fw.get();

        EXPECT_TRUE(waiter_completed)
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
    async_mutex mtx;

    std::vector<asio::cancellation_signal> sigs(N);
    std::atomic<int> total{0};

    asio::io_context ioc;

    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        co_await yield_n(N * 2);
        for (int i = 0; i < N; ++i)
            sigs[i].emit(asio::cancellation_type::total);
        co_await yield_n(N);
    };

    auto make_waiter = [&](int idx) -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx.async_lock();
        (void)r;
        total.fetch_add(1, std::memory_order_acq_rel);
        if (r.has_value()) co_await yield_n(1);
    };

    auto fh = asio::co_spawn(ioc, holder(), asio::use_future);
    std::vector<std::future<void>> futs;
    for (int i = 0; i < N; ++i) {
        futs.push_back(asio::co_spawn(
            ioc,
            make_waiter(i),
            asio::bind_cancellation_slot(sigs[i].slot(), asio::use_future)));
    }

    ioc.run();
    fh.get();
    for (auto& f : futs) f.get();

    EXPECT_EQ(total.load(), N);
    // mtx destruction — must not std::terminate.
}

}  // namespace
