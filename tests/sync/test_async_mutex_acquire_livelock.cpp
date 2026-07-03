// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/sync/test_async_mutex_acquire_livelock.cpp
//
// 058-async-mutex-hardening Gate-B MAJOR-2 — deterministic seam witness for
// the pre-existing acquisition livelock at async_mutex.hpp's contended
// acquire loop (~:1290): the `if (old_state == not_locked)` branch CASed
// with a FRESH `uintptr_t exp2 = not_locked;` (NOT `old_state`), and on CAS
// failure did `continue;` WITHOUT refreshing `old_state`. A waiter that
// observed `not_locked` and lost the CAS to another acquirer kept
// `old_state == not_locked` FOREVER — busy-spinning retrying the doomed
// not_locked -> locked_no_waiters CAS instead of falling through to the
// queueing branch (livelock; deadlock on a shared/oversubscribed
// single-thread executor).
//
// FIX (async_mutex.hpp, surgical): the CAS now operates directly on
// `old_state` (not a discarded local `exp2`), so a failed CAS refreshes
// `old_state` with the actual current value via the standard
// compare_exchange_weak(expected&, ...) contract — the next loop iteration
// correctly re-observes reality and falls through to the queueing branch.
//
// DETERMINISTIC REPRODUCTION — two cooperating seam phases pin the SAME
// acquirer thread ("A") twice (mirrors the T046 two-phase idiom:
// pop_pre_link_load + pop_pre_cas construct ONE ABA reproduction in
// test_async_mutex_aba_interleave.cpp):
//
//   1. acq_pre_state_reload — pinned immediately BEFORE the loop's initial
//      `old_state = state_.load()`. A has already failed its top-level
//      fast-path CAS (a temporary holder T0 holds the lock, zero waiters
//      queued) and created its waiter record — this is the LAST synchronous
//      step before the read the bug concerns. While A is parked here, the
//      test releases T0 (zero waiters queued, so the mutex genuinely
//      becomes `not_locked`) — deterministically, not racily, since A
//      cannot observe anything until released.
//   2. acq_pre_notlocked_cas — pinned immediately AFTER A observes
//      `old_state == not_locked`, BEFORE the CAS. While A is parked here, a
//      second thread ("B") wins the fast-path CAS, taking the lock. Release
//      A: its CAS now fails against B's held state.
//
// ORACLE (discriminating, per the T007/T046 "bounded wait turns a hang into
// a discriminating result" idiom): after releasing A's second pin, a probe
// is posted onto A's OWN io_context and awaited with a bound.
//   - POST-FIX: A's failed CAS refreshes `old_state` to B's held value: the
//     next iteration takes the QUEUEING branch (old_state != not_locked),
//     which SUCCEEDS (nothing else is racing state_ at that instant) — A's
//     coroutine genuinely SUSPENDS (the initiation lambda returns without
//     invoking the handler). This frees A's io_context to process the
//     probe, which resolves quickly.
//   - PRE-FIX: A's `old_state` local stays permanently stale at
//     `not_locked` (the CAS's `exp2` was a discarded local). A NEVER
//     suspends — it busy-spins forever inside the same synchronous
//     initiation-lambda call, fully occupying A's io_context thread. The
//     probe — queued strictly after the spin begins — never gets a chance
//     to run within the bound: PRE-FIX, this is the discriminating RED.
//
// Test-process hygiene: after the probe check (which must run WHILE B still
// holds the lock, since PRE-FIX A must never suspend on its own), B is
// released regardless of the probe's outcome. This lets a PRE-FIX A's
// still-spinning retry opportunistically observe the now-free state and win
// the CAS via the (buggy, unfair) direct-grant path, so A's coroutine
// eventually completes and `thread_a.join()` does not hang the test binary
// even when the discriminating assertion below has already gone RED.
//
// MUTATION: reverting the fix (CAS on a fresh `exp2` local instead of
// `old_state`) makes the probe time out — RED.
//
// FR-012: coordination uses only atomics / std::binary_semaphore /
// std::promise-std::future — no std::mutex / std::condition_variable.
//
// ODR discipline (research.md D-7, matches test_async_mutex_aba_interleave.cpp
// and test_async_mutex_terminal_cas_recursive_unlock.cpp): standalone target,
// ZERO fixpp_sync (or any other fixpp library/object) linkage.

#include <gtest/gtest.h>

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
// Shared seam-hook plumbing. Same idiom as
// test_async_mutex_terminal_cas_recursive_unlock.cpp's TerminalCasCtx, but
// tracks TWO independent one-shot park points for the SAME designated
// thread (A), each with its own park-once flag and semaphore pair — no
// "current stage" bookkeeping needed since the phases are structurally
// distinct and always visited in program order (state_reload strictly
// before notlocked_cas within one async_lock() call).
// ─────────────────────────────────────────────────────────────────────────────

struct LivelockCtx {
    std::atomic<std::thread::id> a_tid{};

    std::atomic<bool> parked1_done{false};
    std::binary_semaphore parked1{0};
    std::binary_semaphore release1{0};

    std::atomic<bool> parked2_done{false};
    std::binary_semaphore parked2{0};
    std::binary_semaphore release2{0};
};

LivelockCtx* g_ctx = nullptr;

void livelock_hook(async_mutex_seam_phase phase) noexcept {
    auto* ctx = g_ctx;
    if (ctx == nullptr) return;
    if (std::this_thread::get_id() != ctx->a_tid.load(std::memory_order_acquire)) return;

    if (phase == async_mutex_seam_phase::acq_pre_state_reload) {
        if (ctx->parked1_done.exchange(true, std::memory_order_acq_rel)) return;
        ctx->parked1.release();
        ctx->release1.acquire();
        return;
    }
    if (phase == async_mutex_seam_phase::acq_pre_notlocked_cas) {
        if (ctx->parked2_done.exchange(true, std::memory_order_acq_rel)) return;
        ctx->parked2.release();
        ctx->release2.acquire();
        return;
    }
}

// Posts a probe onto `ioc`'s executor and waits (bounded) for it to run.
// Same idiom as test_async_mutex_terminal_cas_recursive_unlock.cpp's
// confirm_committed, EXCEPT the promise is heap-allocated behind a
// shared_ptr captured BY VALUE in the posted lambda (not by reference):
// unlike every other confirm_committed call site in this suite, THIS
// witness's whole purpose is to test a probe that may legitimately never
// fire within the bound (the pre-fix livelock case) — the posted work item
// then survives, unconsumed, past this function's return (the pinned
// acquirer's io_context is stuck spinning and cannot drain it yet, until
// the test later releases B). A stack-local promise captured by reference
// would leave that surviving posted lambda holding a dangling reference —
// a real UAF once the io_context eventually drains it. The shared_ptr keeps
// the promise alive for as long as the posted lambda needs it; nothing
// waits on `fut` once this function has returned, so a late `set_value()`
// on an already-abandoned future is harmless.
[[nodiscard]] bool confirm_committed(asio::io_context& ioc, std::chrono::milliseconds bound) {
    auto p = std::make_shared<std::promise<void>>();
    auto fut = p->get_future();
    asio::post(ioc.get_executor(), [p]() { p->set_value(); });
    return fut.wait_for(bound) == std::future_status::ready;
}

}  // namespace

TEST(AsyncMutexAcquireLivelock, ContendedNotLockedCasLossQueuesInsteadOfSpinning) {
    LivelockCtx ctx;
    g_ctx = &ctx;
    fixpp::sync::detail::async_mutex_test_seam = &livelock_hook;
    struct SeamReset {
        ~SeamReset() {
            fixpp::sync::detail::async_mutex_test_seam = nullptr;
            g_ctx = nullptr;
        }
    } seam_reset;

    async_mutex mtx;

    // ---- T0: temporary holder, fast path (uncontended), no waiters.
    // Disengage the guard WITHOUT unlocking (`release()`) so the test
    // controls the exact unlock() call point explicitly.
    asio::io_context ioc_t0;
    bool t0_ok = false;
    std::optional<async_lock_guard> g0;
    auto t0_coro = [&]() -> asio::awaitable<void> {
        auto r = co_await mtx.async_lock();
        t0_ok = r.has_value();
        if (t0_ok) g0.emplace(std::move(*r));
    };
    auto ft0 = asio::co_spawn(ioc_t0, t0_coro(), asio::use_future);
    ioc_t0.run();
    ft0.get();
    ASSERT_TRUE(t0_ok);
    ASSERT_TRUE(g0.has_value());
    async_mutex* m = g0->release();

    // ---- A: dedicated io_context/thread. Its top-level fast-path CAS fails
    // (T0 holds), so it creates a waiter record and reaches the contended
    // acquire loop, parking at acq_pre_state_reload (pin 1).
    asio::io_context ioc_a;
    bool a_ok = false;
    auto a_coro = [&]() -> asio::awaitable<void> {
        auto r = co_await m->async_lock();
        a_ok = r.has_value();
    };
    auto fa = asio::co_spawn(ioc_a, a_coro(), asio::use_future);
    std::thread thread_a([&] {
        ctx.a_tid.store(std::this_thread::get_id(), std::memory_order_release);
        ioc_a.run();
    });

    ctx.parked1.acquire();  // A has failed the fast CAS, created its record,
                            // and is blocked immediately before the initial
                            // old_state load.

    // ---- Release T0. Zero waiters are queued (A hasn't touched state_
    // yet), so this deterministically restores state_ to not_locked.
    m->unlock();

    // ---- Release A's first pin. Its old_state load now deterministically
    // observes not_locked, and it parks again at acq_pre_notlocked_cas
    // (pin 2) — immediately before the CAS the bug concerns.
    ctx.release1.release();
    ctx.parked2.acquire();

    // ---- B: dedicated io_context/thread, wins the fast-path CAS (state_
    // is not_locked right now, confirmed by A having reached pin 2).
    asio::io_context ioc_b;
    bool b_ok = false;
    std::optional<async_lock_guard> gb;
    auto b_coro = [&]() -> asio::awaitable<void> {
        auto r = co_await m->async_lock();
        b_ok = r.has_value();
        if (b_ok) gb.emplace(std::move(*r));
    };
    auto fb = asio::co_spawn(ioc_b, b_coro(), asio::use_future);
    ioc_b.run();
    fb.get();
    ASSERT_TRUE(b_ok);
    ASSERT_TRUE(gb.has_value());

    // ---- Release A's second pin. Its CAS (expected=not_locked) now fails
    // against B's held state_.
    ctx.release2.release();

    // ---- Discriminating oracle: WHILE B still holds (no other transition
    // can rescue a pre-fix spin into completing), a probe posted onto A's
    // own io_context must be able to run within the bound. Post-fix, A
    // suspends (queues) almost immediately, freeing ioc_a to service the
    // probe. Pre-fix, A never suspends — the probe cannot run.
    bool probe_ok = confirm_committed(ioc_a, std::chrono::seconds(2));

    // ---- Test-process hygiene: release B regardless of the probe outcome.
    // Post-fix this grants queued-A. Pre-fix this lets A's still-spinning
    // retry opportunistically observe the free state and win the CAS
    // directly (the buggy, unfair bypass) — either way A's coroutine
    // eventually completes, so ioc_a.run() returns and thread_a.join()
    // below does not hang the test binary.
    async_mutex* mm = gb->release();
    mm->unlock();

    thread_a.join();

    ASSERT_TRUE(probe_ok) << "the pinned acquirer never yielded control back to its own executor — "
                             "PRE-FIX livelock: the contended not_locked branch retried the same "
                             "doomed CAS forever on a stale old_state instead of re-observing "
                             "reality and queueing";

    ASSERT_EQ(fa.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    fa.get();
    EXPECT_TRUE(a_ok);
}
