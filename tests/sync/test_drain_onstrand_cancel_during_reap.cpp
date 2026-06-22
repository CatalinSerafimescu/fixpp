// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_drain_onstrand_cancel_during_reap.cpp — 048 T012 (FR-004/P1-N2)
//
// The per-waiter phase_ CAS (queued→{cancelled,granted}) guarantees a single
// winner between on_cancel and the reap: the waiter is resolved EXACTLY once.
//
// P2-2 (Gate B): a SAME-STRAND cancel cannot interleave INSIDE the synchronous
// reap (the reaper runs to completion before the strand yields), so we test the
// two REALIZABLE orderings explicitly:
//   (a) cancel emitted SYNCHRONOUSLY BEFORE the drain → on_cancel wins the CAS and
//       schedules the single aborted resume; the later reap's CAS FAILS (no double).
//   (b) drain runs FIRST (reap wins the CAS) → a LATER cancel on the reaped waiter
//       loses the CAS and no-ops — no double-resume.
// Both keep the holder held through the reap so NO waiter is granted; every waiter
// is resolved exactly once with sync_lock_aborted. ASan+TSan-clean.
//
// Design: research.md D-4; INV-B; contracts/async_mutex-contract.md FR-004. Task: T012.

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

#include "sync/sync_test_support.hpp"

namespace {

using fixpp::core::error;
using fixpp::sync::async_lock_guard;
using fixpp::sync::async_mutex;
using fixpp::sync::expected_t;

using fixpp::sync::test::yield_n;

// Drive an io_context until `done` (or a 5s self-deadline). Returns false on hang.
template <class DoneFn>
bool run_until(asio::io_context& ioc, DoneFn done) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        ioc.run_for(std::chrono::milliseconds(50));
        if (done()) return true;
    }
    ioc.stop();
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Ordering (a): on_cancel wins (cancel precedes drain). Holder held through reap.
// ─────────────────────────────────────────────────────────────────────────────

TEST(DrainOnStrandCancelDuringReap, OnCancelWinsWhenCancelPrecedesDrain) {
    constexpr int N = 4;
    constexpr int REPS = 100;

    for (int rep = 0; rep < REPS; ++rep) {
        std::atomic<int> aborted{0};
        std::atomic<int> granted{0};
        std::atomic<int> completed{0};
        std::atomic<bool> drain_done{false};
        std::atomic<bool> drain_ok{false};

        asio::io_context ioc;
        async_mutex mtx;
        std::vector<asio::cancellation_signal> sigs(N);

        auto main_coro = [&]() -> asio::awaitable<void> {
            auto ex = co_await asio::this_coro::executor;
            auto holder = co_await mtx.async_lock();   // hold through the reap
            EXPECT_TRUE(holder.has_value());

            for (int i = 0; i < N; ++i) {
                asio::co_spawn(
                    ex,
                    asio::bind_cancellation_slot(
                        sigs[i].slot(),
                        [&]() -> asio::awaitable<void> {
                            auto r = co_await mtx.async_lock();
                            if (r.has_value()) {
                                granted.fetch_add(1, std::memory_order_relaxed);
                                r->release()->unlock();
                            } else if (r.error() == error::sync_lock_aborted) {
                                aborted.fetch_add(1, std::memory_order_relaxed);
                            }
                            completed.fetch_add(1, std::memory_order_relaxed);
                        }),
                    asio::detached);
            }
            co_await yield_n(N * 2 + 4);

            // SYNCHRONOUS emit BEFORE the drain: on_cancel CASes waiter[0]→cancelled
            // and schedules its resume; the later reap's CAS on it must FAIL.
            sigs[0].emit(asio::cancellation_type::total);

            // Drain while the holder is STILL held → no waiter can be granted.
            asio::co_spawn(
                ex,
                [&]() -> asio::awaitable<void> {
                    auto d = co_await mtx.cancel_and_drain();
                    drain_ok.store(d.has_value(), std::memory_order_release);
                    drain_done.store(true, std::memory_order_release);
                },
                asio::detached);
            co_await yield_n(N * 2 + 6);

            holder = expected_t<async_lock_guard>{};  // release → drain finalizes
        };

        asio::co_spawn(ioc, main_coro(), asio::detached);
        bool ok = run_until(ioc, [&] {
            return drain_done.load(std::memory_order_acquire) &&
                   completed.load(std::memory_order_acquire) == N;
        });
        ASSERT_TRUE(ok) << "Rep " << rep << " hung";

        EXPECT_TRUE(drain_ok.load()) << "Rep " << rep << " drain failed";
        EXPECT_EQ(completed.load(), N) << "Rep " << rep << " not exactly-once (double-resume/lost)";
        EXPECT_EQ(granted.load(), 0) << "Rep " << rep << " a waiter was granted (holder held)";
        EXPECT_EQ(aborted.load(), N) << "Rep " << rep << " not all waiters aborted";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Ordering (b): a cancel emitted AFTER the drain has reaped a waiter is BENIGN —
// no double-resume, no crash. NOTE (Gate B r2): the EXACT reap-wins-then-on_cancel-
// CAS-loss micro-window is NOT deterministically witnessable at the public API: the
// reaper posts each waiter's resume FIFO, and those resumes run (completing the
// waiter coroutine and detaching its cancellation slot) BEFORE any external code
// gets a strand turn to emit — so a later emit hits a detached slot. The single-
// winner CAS is therefore proven by (a) `OnCancelWinsWhenCancelPrecedesDrain` above
// (fully deterministic: on_cancel wins, the reaper's CAS loses) + the SYMMETRIC
// source CAS (`phase_.compare_exchange_strong(queued, cancelled)` — exactly one of
// {on_cancel, reap} succeeds; the loser observes phase!=queued and no-ops). This
// test asserts the BEHAVIORAL property a real caller cares about: a post-drain
// cancel on a reaped waiter does not double-resume it (completed==N, ASan-clean).
// ─────────────────────────────────────────────────────────────────────────────

TEST(DrainOnStrandCancelDuringReap, PostDrainCancelIsBenignNoDoubleResume) {
    constexpr int N = 8;
    constexpr int REPS = 50;

    for (int rep = 0; rep < REPS; ++rep) {
        std::atomic<int> aborted{0};
        std::atomic<int> granted{0};
        std::atomic<int> completed{0};
        std::atomic<bool> drain_done{false};
        std::atomic<bool> drain_ok{false};

        asio::io_context ioc;
        async_mutex mtx;
        std::vector<asio::cancellation_signal> sigs(N);

        auto main_coro = [&]() -> asio::awaitable<void> {
            auto ex = co_await asio::this_coro::executor;
            auto holder = co_await mtx.async_lock();
            EXPECT_TRUE(holder.has_value());

            for (int i = 0; i < N; ++i) {
                asio::co_spawn(
                    ex,
                    asio::bind_cancellation_slot(
                        sigs[i].slot(),
                        [&]() -> asio::awaitable<void> {
                            auto r = co_await mtx.async_lock();
                            if (r.has_value()) {
                                granted.fetch_add(1, std::memory_order_relaxed);
                                r->release()->unlock();
                            } else if (r.error() == error::sync_lock_aborted) {
                                aborted.fetch_add(1, std::memory_order_relaxed);
                            }
                            completed.fetch_add(1, std::memory_order_relaxed);
                        }),
                    asio::detached);
            }
            co_await yield_n(N * 2 + 4);

            // Drain while holding → reaps all N (reap wins the CAS). Holder still held.
            asio::co_spawn(
                ex,
                [&]() -> asio::awaitable<void> {
                    auto d = co_await mtx.cancel_and_drain();
                    drain_ok.store(d.has_value(), std::memory_order_release);
                    drain_done.store(true, std::memory_order_release);
                },
                asio::detached);
            co_await yield_n(N * 2 + 6);  // let the drain reap all waiters

            // LATER cancel on each (now-reaped → cancelled) waiter: CAS fails, no-op.
            // A double-resume here would push completed > N → ASan UAF.
            for (int i = 0; i < N; ++i) sigs[i].emit(asio::cancellation_type::total);

            holder = expected_t<async_lock_guard>{};  // release → drain finalizes
        };

        asio::co_spawn(ioc, main_coro(), asio::detached);
        bool ok = run_until(ioc, [&] {
            return drain_done.load(std::memory_order_acquire) &&
                   completed.load(std::memory_order_acquire) == N;
        });
        ASSERT_TRUE(ok) << "Rep " << rep << " hung";

        EXPECT_TRUE(drain_ok.load()) << "Rep " << rep << " drain failed";
        EXPECT_EQ(completed.load(), N)
            << "Rep " << rep << " not exactly-once (a later cancel double-resumed a reaped waiter)";
        EXPECT_EQ(granted.load(), 0) << "Rep " << rep << " a waiter was granted (holder held)";
        EXPECT_EQ(aborted.load(), N) << "Rep " << rep << " not all waiters reaped sync_lock_aborted";
    }
}

}  // namespace
