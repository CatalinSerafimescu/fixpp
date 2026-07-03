// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/sync/test_async_mutex_terminal_cas_recursive_unlock.cpp
//
// 058-async-mutex-hardening T046 — deterministic seam witnesses for the two
// `unlock()` terminal-CAS-fail -> recursive-unlock arms
// (.specify/decisions/058-async-mutex-hardening-coverage-design.md
// "EMPIRICAL CORRECTION", 2026-07-03):
//
//   - F4 (`async_mutex.hpp` ~:1401, the "fast"/no-waiters path): after
//     `state_.exchange(locked_no_waiters)` observes no waiters were queued,
//     the immediately-following CAS(locked_no_waiters -> not_locked) can
//     fail if a waiter's queuing CAS lands in that narrow window; the
//     failure recurses into `unlock()` to grant the newly-arrived waiter.
//   - F6 (`async_mutex.hpp` ~:1471, post-FIFO-walk): after a fresh
//     LIFO->FIFO walk exhausts an ALL-CANCELLED chain (no grant, so no
//     early return), the same terminal-CAS-fail -> recursive-unlock shape
//     recurs at the second call site.
//
// Both arms are REACHABLE IN-CONTRACT (a waiter arriving in the terminal-CAS
// window is ordinary supported cross-thread contention) but were measured
// (T040, fresh llvm-cov) NOT reliably organic on the seam-OFF coverage lane
// (F4 flaky ~0.3-1.5%; F6 0/all-trials) — see the coverage-design doc's
// EMPIRICAL CORRECTION section. This file forces the interleaving
// deterministically via the two new `unlock_pre_terminal_cas_fast` /
// `unlock_pre_terminal_cas_fifo` seam phases (async_mutex.hpp T046),
// following the same standalone-target / T1-pins-T2-arrives idiom as
// test_async_mutex_aba_interleave.cpp's `pop_pre_cas` witness (T007).
//
// Oracle (both tests): the DISCRIMINATING claim is "the recursive unlock()
// call ran AND granted the newly-arrived waiter" — witnessed by the
// newly-arrived waiter's own `async_lock()` actually resolving
// (`r.has_value()==true`) within a bounded wait, NOT merely "no crash". A
// hand-mutation that comments out the `unlock();` recursive call at either
// arm leaves the waiter's record permanently queued (never resumed) — the
// bounded wait times out, turning the hang into a clean, discriminating RED
// (per the T007 "bounded wait_for turns a hang into a discriminating count"
// idiom) instead of an indefinite hang.
//
// FR-012: coordination uses only atomics / std::binary_semaphore /
// std::promise-std::future — no std::mutex / std::condition_variable.
//
// ODR discipline (research.md D-7, matches test_async_mutex_aba_interleave.cpp):
// this target is standalone — built directly against the header + asio +
// GTest, ZERO fixpp_sync (or any other fixpp library/object) linkage. A
// FIXPP_ASYNC_MUTEX_TEST_SEAM-enabled TU must never link against a
// non-seam-enabled TU that also instantiates async_mutex.hpp's inline
// out-of-line bodies (ODR violation on the differently-preprocessed bodies).

#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/use_future.hpp>

#include <atomic>
#include <chrono>
#include <fixpp/core/sync/async_mutex.hpp>
#include <future>
#include <optional>
#include <semaphore>
#include <thread>

namespace {

using fixpp::sync::async_lock_guard;
using fixpp::sync::async_mutex;
using fixpp::sync::detail::async_mutex_seam_phase;

// ─────────────────────────────────────────────────────────────────────────────
// Shared seam-hook plumbing. Only one TEST's context is ever installed at a
// time (gtest runs tests in this binary sequentially) — matches the
// `pre_cas_aba_hook` (T007) idiom: T1 (identified by thread id) parks
// exactly once at the target phase; every other thread's entry (including
// T1's own harmless re-entries once already parked) is a no-op. T1 is
// released only by an explicit main-thread signal once the test has
// deterministically confirmed the arriving waiter's queuing CAS committed —
// never by the arriving waiter's own entry (that thread never calls
// unlock() at this phase in these tests).
// ─────────────────────────────────────────────────────────────────────────────

struct TerminalCasCtx {
    async_mutex_seam_phase target_phase;
    std::atomic<std::thread::id> t1_tid{};
    std::atomic<bool> t1_already_parked{false};
    std::binary_semaphore t1_parked{0};
    std::binary_semaphore t1_release{0};
};

TerminalCasCtx* g_terminal_cas_ctx = nullptr;

void terminal_cas_hook(async_mutex_seam_phase phase) noexcept {
    auto* ctx = g_terminal_cas_ctx;
    if (ctx == nullptr) return;
    if (phase != ctx->target_phase) return;
    if (std::this_thread::get_id() != ctx->t1_tid.load(std::memory_order_acquire)) return;
    if (ctx->t1_already_parked.exchange(true, std::memory_order_acq_rel)) return;  // park once
    ctx->t1_parked.release();
    ctx->t1_release.acquire();
}

// Posts a probe onto `ioc`'s executor and waits (bounded) for it to run.
// asio's single-threaded FIFO handler execution guarantees any work already
// posted onto `ioc` (e.g. an in-flight coroutine's synchronous queuing CAS)
// has completed by the time a probe posted STRICTLY AFTER it runs — the
// same "hard, non-timing confirmation" idiom as T007's `t1_committed` probe.
[[nodiscard]] bool confirm_committed(asio::io_context& ioc) {
    std::promise<void> p;
    auto fut = p.get_future();
    asio::post(ioc.get_executor(), [&p]() { p.set_value(); });
    return fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
}

// ─────────────────────────────────────────────────────────────────────────────
// F4 — no-waiters fast path.
// ─────────────────────────────────────────────────────────────────────────────

TEST(AsyncMutexTerminalCasRecursiveUnlock, F4FastPathTerminalCasFailGrantsWaiter) {
    TerminalCasCtx ctx;
    ctx.target_phase = async_mutex_seam_phase::unlock_pre_terminal_cas_fast;
    g_terminal_cas_ctx = &ctx;
    fixpp::sync::detail::async_mutex_test_seam = &terminal_cas_hook;
    struct SeamReset {
        ~SeamReset() {
            fixpp::sync::detail::async_mutex_test_seam = nullptr;
            g_terminal_cas_ctx = nullptr;
        }
    } seam_reset;

    async_mutex mtx;

    // ---- T1 acquires uncontended (fast path CAS, no waiters). Disengage
    // the guard WITHOUT unlocking (`release()`) so this test controls the
    // exact `unlock()` call point explicitly, on a dedicated thread below.
    asio::io_context ioc_main;
    bool acquire_ok = false;
    std::optional<async_lock_guard> g1;
    auto acquire_holder = [&]() -> asio::awaitable<void> {
        auto r = co_await mtx.async_lock();
        acquire_ok = r.has_value();
        if (acquire_ok) g1.emplace(std::move(*r));
    };
    auto f1 = asio::co_spawn(ioc_main, acquire_holder(), asio::use_future);
    ioc_main.run();
    f1.get();
    ASSERT_TRUE(acquire_ok);
    ASSERT_TRUE(g1.has_value());
    async_mutex* m = g1->release();

    // ---- T1: dedicated thread calls unlock() directly (a plain
    // synchronous method, not a coroutine) and pins at
    // `unlock_pre_terminal_cas_fast` — immediately after
    // `state_.exchange(locked_no_waiters)`, immediately before the terminal
    // CAS back to `not_locked`.
    std::thread thread_a([&] {
        ctx.t1_tid.store(std::this_thread::get_id(), std::memory_order_release);
        m->unlock();
    });
    ctx.t1_parked.acquire();  // T1 has exchanged state_ and is blocked pre-CAS.

    // ---- T2: dedicated io_context/thread, arrives while T1 is pinned.
    // state_ == locked_no_waiters right now (T1's exchange already ran), so
    // T2's async_lock() queues itself via the push CAS, landing exactly in
    // T1's terminal-CAS window.
    asio::io_context ioc_b;
    bool t2_ok = false;
    auto t2_coro = [&]() -> asio::awaitable<void> {
        auto r = co_await m->async_lock();
        t2_ok = r.has_value();
    };
    auto ft2 = asio::co_spawn(ioc_b, t2_coro(), asio::use_future);
    std::thread thread_b([&] { ioc_b.run(); });

    ASSERT_TRUE(confirm_committed(ioc_b))
        << "T2's queuing CAS was not observed committed within the bound";

    // ---- Release T1. Its terminal CAS (expected=locked_no_waiters) must
    // now fail against T2's queued record pointer, recursing into unlock()
    // to grant T2.
    ctx.t1_release.release();
    thread_a.join();

    // Bounded wait: a hang here (rather than the expected GREEN) is the
    // discriminating RED signal for a neutered recursive unlock() call.
    auto status = ft2.wait_for(std::chrono::seconds(5));
    if (status != std::future_status::ready) {
        ioc_b.stop();
    }
    thread_b.join();
    ASSERT_EQ(status, std::future_status::ready)
        << "T2 was never granted — F4's recursive unlock() call did not run/grant";
    ft2.get();
    EXPECT_TRUE(t2_ok);
}

// ─────────────────────────────────────────────────────────────────────────────
// F6 — post-FIFO-walk, all-cancelled exhaustion.
// ─────────────────────────────────────────────────────────────────────────────

TEST(AsyncMutexTerminalCasRecursiveUnlock, F6FifoExhaustedTerminalCasFailGrantsWaiter) {
    TerminalCasCtx ctx;
    ctx.target_phase = async_mutex_seam_phase::unlock_pre_terminal_cas_fifo;
    g_terminal_cas_ctx = &ctx;
    fixpp::sync::detail::async_mutex_test_seam = &terminal_cas_hook;
    struct SeamReset {
        ~SeamReset() {
            fixpp::sync::detail::async_mutex_test_seam = nullptr;
            g_terminal_cas_ctx = nullptr;
        }
    } seam_reset;

    async_mutex mtx;

    // ---- T1 acquires uncontended (fast path).
    asio::io_context ioc_main;
    bool acquire_ok = false;
    std::optional<async_lock_guard> g1;
    auto acquire_holder = [&]() -> asio::awaitable<void> {
        auto r = co_await mtx.async_lock();
        acquire_ok = r.has_value();
        if (acquire_ok) g1.emplace(std::move(*r));
    };
    auto f1 = asio::co_spawn(ioc_main, acquire_holder(), asio::use_future);
    ioc_main.run();
    f1.get();
    ASSERT_TRUE(acquire_ok);
    ASSERT_TRUE(g1.has_value());
    async_mutex* m = g1->release();

    // ---- W: queues while T1 holds, then is CANCELLED before T1 unlocks —
    // so unlock()'s fresh LIFO->FIFO walk finds an all-cancelled chain and
    // falls through to the terminal CAS (no grant, no early return): the
    // exact precondition for F6. Cancellation alone does not unlink W's
    // record from state_'s LIFO chain (only a subsequent unlock() drain
    // walk does — same fact test_async_mutex_aba_interleave.cpp's setup
    // relies on), so it is still linked, phase_==cancelled, when T1 unlocks.
    asio::io_context ioc_w;
    asio::cancellation_signal sig_w;
    bool w_aborted = false;
    auto waiter_w = [&]() -> asio::awaitable<void> {
        auto r = co_await m->async_lock();
        w_aborted = !r.has_value();
    };
    auto fw = asio::co_spawn(ioc_w, waiter_w(),
                              asio::bind_cancellation_slot(sig_w.slot(), asio::use_future));
    std::thread thread_w([&] { ioc_w.run(); });

    ASSERT_TRUE(confirm_committed(ioc_w))
        << "W's queuing CAS was not observed committed within the bound";

    // Cancel W — posted onto W's OWN executor. asio::cancellation_signal /
    // slot are not thread-safe; firing from an unrelated thread would race
    // asio's own bookkeeping (feedback_single_threaded_harness_masks_strand_races
    // precedent, T027).
    asio::post(ioc_w.get_executor(), [&] { sig_w.emit(asio::cancellation_type::total); });
    ASSERT_EQ(fw.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    fw.get();
    thread_w.join();
    ASSERT_TRUE(w_aborted);

    // ---- T1: dedicated thread calls unlock() directly, pins at
    // `unlock_pre_terminal_cas_fifo` — after the FIFO walk exhausts W's
    // all-cancelled chain, before the terminal CAS.
    std::thread thread_a([&] {
        ctx.t1_tid.store(std::this_thread::get_id(), std::memory_order_release);
        m->unlock();
    });
    ctx.t1_parked.acquire();

    // ---- T2: arrives while T1 is pinned. state_ == locked_no_waiters right
    // now (set by unlock()'s exchange before the FIFO walk), so T2's
    // async_lock() queues itself via the push CAS, landing in T1's
    // terminal-CAS window.
    asio::io_context ioc_b;
    bool t2_ok = false;
    auto t2_coro = [&]() -> asio::awaitable<void> {
        auto r = co_await m->async_lock();
        t2_ok = r.has_value();
    };
    auto ft2 = asio::co_spawn(ioc_b, t2_coro(), asio::use_future);
    std::thread thread_b([&] { ioc_b.run(); });

    ASSERT_TRUE(confirm_committed(ioc_b))
        << "T2's queuing CAS was not observed committed within the bound";

    ctx.t1_release.release();
    thread_a.join();

    auto status = ft2.wait_for(std::chrono::seconds(5));
    if (status != std::future_status::ready) {
        ioc_b.stop();
    }
    thread_b.join();
    ASSERT_EQ(status, std::future_status::ready)
        << "T2 was never granted — F6's recursive unlock() call did not run/grant";
    ft2.get();
    EXPECT_TRUE(t2_ok);
}

}  // namespace
