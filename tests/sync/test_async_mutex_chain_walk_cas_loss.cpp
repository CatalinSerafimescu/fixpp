// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/sync/test_async_mutex_chain_walk_cas_loss.cpp
//
// 058-async-mutex-hardening Gate-B MAJOR-1 — deterministic seam witnesses
// for unlock()'s two chain-walk `queued -> granted` CAS-loss arms (the
// `ph = expected_ph;` fallthrough after a failed CAS at ~:1357 [residual
// walk] and ~:1438 [fresh LIFO->FIFO walk]): a waiter's `on_cancel()` can
// win the `queued -> cancelled` race concurrently with unlock()'s walk
// observing that waiter as `queued` and attempting to grant it. The
// coverage-design doc (.specify/decisions/058-async-mutex-hardening-
// coverage-design.md) originally waived this arm as W-7 ("hard to force");
// the 2026-07-03 Gate-B correction determined it is REACHABLE under
// supported cancel-vs-grant contention and must be WITNESSED, not waived
// (relabelled W-12 in the verify doc to avoid colliding with the coverage-
// design doc's own W-7, `on_cancel record_==nullptr`).
//
// NO CODE CHANGE beyond the macro-gated seam call-sites: the existing
// `if (ph == waiter_phase::cancelled) { unlink; release_ref; continue; }`
// fallthrough is already correct — these witnesses PROVE it, and are
// mutation-tested against a deliberately broken loser path (see the
// "MUTATION" verification note in the Gate-B fixer report; not shipped
// here).
//
// DETERMINISTIC REPRODUCTION — same two-thread pin idiom as
// test_async_mutex_terminal_cas_recursive_unlock.cpp: a dedicated thread
// calls unlock() directly (a plain synchronous method) and parks at the
// new seam phase, immediately after the walk loads a waiter's `phase_` as
// `queued`, BEFORE the `queued -> granted` CAS. While parked, a SEPARATE
// waiter's cancellation is driven to completion (confirmed via its own
// future resolving `sync_lock_aborted`), proving on_cancel's
// `queued -> cancelled` CAS has already committed. Releasing the pin then
// makes unlock()'s CAS observe `cancelled`, not `queued` — a genuine,
// non-simulated CAS loss.
//
// ORACLE (discriminating, per the brief): after unlock() returns,
//   (a) the cancelled waiter's future resolved `sync_lock_aborted` (proves
//       on_cancel won), and
//   (b) the cancelled waiter's pool slot has been returned to the free list
//       as its new head (`test_seam_free_list_head_slot_index() == idx`),
//       proving the CAS-loser's `release_ref` ran its 1->0 transition and
//       pushed the slot — a "skip the release" mutation leaves the free
//       list unchanged (still pointing at whatever it held before this
//       waiter's push, never `idx`), which is exactly what a leaked ref /
//       pool slot never freed looks like. (2026-07-03 test-oracle-hygiene
//       fix: this replaces a post-`~waiter_record()` read of the destroyed
//       record's own `refcount_` — technically-UB even though the pool
//       slot's storage bytes persist — with a read of the mutex's OWN,
//       still-live `waiter_pool_free_` head. A second/double release
//       cannot double-push here regardless [its `fetch_sub` returns != 1
//       and early-returns before ever reaching the free-list push], so this
//       oracle does not need to separately discriminate that direction),
//       and
//   (c) unlock() returns without hanging (thread_a.join() completes) and
//       without a double-resume (the existing T023/T024 impossible-state
//       traps would std::terminate() on a stray second schedule_resume for
//       the same record, per waiter_record's single-schedule invariant).
//
// FR-012: coordination uses only atomics / std::binary_semaphore /
// std::promise-std::future — no std::mutex / std::condition_variable.
//
// ODR discipline (research.md D-7): standalone target, ZERO fixpp_sync (or
// any other fixpp library/object) linkage — same discipline as every other
// FIXPP_ASYNC_MUTEX_TEST_SEAM-gated target in this directory.

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
using fixpp::sync::detail::waiter_phase;

// ─────────────────────────────────────────────────────────────────────────────
// Shared seam-hook plumbing — same idiom as
// test_async_mutex_terminal_cas_recursive_unlock.cpp's TerminalCasCtx.
// ─────────────────────────────────────────────────────────────────────────────

struct ChainWalkCtx {
    async_mutex_seam_phase target_phase;
    std::atomic<std::thread::id> t1_tid{};
    std::atomic<bool> t1_already_parked{false};
    std::binary_semaphore t1_parked{0};
    std::binary_semaphore t1_release{0};
};

ChainWalkCtx* g_chain_walk_ctx = nullptr;

void chain_walk_hook(async_mutex_seam_phase phase) noexcept {
    auto* ctx = g_chain_walk_ctx;
    if (ctx == nullptr) return;
    if (phase != ctx->target_phase) return;
    if (std::this_thread::get_id() != ctx->t1_tid.load(std::memory_order_acquire)) return;
    if (ctx->t1_already_parked.exchange(true, std::memory_order_acq_rel)) return;  // park once
    ctx->t1_parked.release();
    ctx->t1_release.acquire();
}

[[nodiscard]] bool confirm_committed(asio::io_context& ioc) {
    std::promise<void> p;
    auto fut = p.get_future();
    asio::post(ioc.get_executor(), [&p]() { p.set_value(); });
    return fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// FIFO walk (:1438-area) — a single fresh waiter is cancelled while
// unlock()'s LIFO->FIFO walk holds it as `queued`.
//
// Pool-slot index: this is the FIRST slow-path waiter_record allocated on a
// fresh mutex (waiter_pool_next_ bump allocator starts at 0; T0's fast-path
// acquire never allocates a record) — deterministically index 0.
// ─────────────────────────────────────────────────────────────────────────────

TEST(AsyncMutexChainWalkCasLoss, FifoWalkCancelWinsGrantCasLoss) {
    ChainWalkCtx ctx;
    ctx.target_phase = async_mutex_seam_phase::unlock_pre_grant_cas_fifo;
    g_chain_walk_ctx = &ctx;
    fixpp::sync::detail::async_mutex_test_seam = &chain_walk_hook;
    struct SeamReset {
        ~SeamReset() {
            fixpp::sync::detail::async_mutex_test_seam = nullptr;
            g_chain_walk_ctx = nullptr;
        }
    } seam_reset;

    async_mutex mtx;

    // ---- T1 acquires uncontended (fast path). Disengage without unlocking.
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

    // ---- W1 queues (slow path, pool index 0), with a cancellation slot
    // bound so the test can drive on_cancel() explicitly.
    asio::io_context ioc_w1;
    asio::cancellation_signal sig_w1;
    bool w1_aborted = false;
    auto waiter_w1 = [&]() -> asio::awaitable<void> {
        auto r = co_await m->async_lock();
        w1_aborted = !r.has_value();
    };
    auto fw1 = asio::co_spawn(ioc_w1, waiter_w1(),
                              asio::bind_cancellation_slot(sig_w1.slot(), asio::use_future));
    std::thread thread_w1([&] { ioc_w1.run(); });

    ASSERT_TRUE(confirm_committed(ioc_w1))
        << "W1's queuing CAS was not observed committed within the bound";

    // ---- T1: dedicated thread calls unlock() directly, pins at
    // unlock_pre_grant_cas_fifo — immediately after the FIFO walk loads
    // W1's phase_ as queued, before the grant CAS.
    std::thread thread_a([&] {
        ctx.t1_tid.store(std::this_thread::get_id(), std::memory_order_release);
        m->unlock();
    });
    ctx.t1_parked.acquire();

    // ---- Cancel W1 — posted onto W1's OWN executor (cancellation_signal /
    // slot are not thread-safe). Wait for W1's future to resolve aborted,
    // proving on_cancel's queued->cancelled CAS has already committed
    // BEFORE we release T1's pin.
    asio::post(ioc_w1.get_executor(), [&] { sig_w1.emit(asio::cancellation_type::total); });
    ASSERT_EQ(fw1.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    fw1.get();
    thread_w1.join();
    ASSERT_TRUE(w1_aborted);

    // ---- Release T1. Its grant CAS (expected=queued) now genuinely fails
    // against W1's cancelled phase_ — a real, non-simulated CAS loss.
    ctx.t1_release.release();
    thread_a.join();

    // ---- Oracle (b): W1's pool slot (index 0) is the free-list head — the
    // CAS-loser's `release_ref` pushed it. This is a fresh mutex and W1 is
    // the only slow-path record ever allocated (bump allocator index 0), so
    // the free list was empty before this push; a "skip the release"
    // mutation leaves the head at the empty sentinel, not 0.
    EXPECT_EQ(mtx.test_seam_free_list_head_slot_index(), 0u)
        << "W1's waiter_record was not released (pushed to the free list) by "
           "unlock()'s cancelled-branch fallthrough after the CAS loss";
}

// ─────────────────────────────────────────────────────────────────────────────
// Residual walk (:1357-area) — a waiter spliced onto next_drain_head_ by a
// PRIOR unlock() call is cancelled while a SUBSEQUENT unlock()'s residual
// walk holds it as `queued`.
//
// Setup: T1 holds. W1 and W2 both queue (LIFO: W2->W1, so FIFO order is
// W1 first). unlock() #1 (called directly, no seam) grants W1 and splices
// W2 onto next_drain_head_ as the residual. W1's grant is run to completion
// (its guard obtained) so it becomes the new holder. unlock() #2 (the
// pinned call) then walks the residual, observing W2 as queued.
//
// Pool-slot indices: W1 is the first slow-path record (index 0), W2 the
// second (index 1) — deterministic since W1's queuing is confirmed
// committed before W2 starts.
// ─────────────────────────────────────────────────────────────────────────────

TEST(AsyncMutexChainWalkCasLoss, ResidualWalkCancelWinsGrantCasLoss) {
    ChainWalkCtx ctx;
    ctx.target_phase = async_mutex_seam_phase::unlock_pre_grant_cas_residual;
    g_chain_walk_ctx = &ctx;
    fixpp::sync::detail::async_mutex_test_seam = &chain_walk_hook;
    struct SeamReset {
        ~SeamReset() {
            fixpp::sync::detail::async_mutex_test_seam = nullptr;
            g_chain_walk_ctx = nullptr;
        }
    } seam_reset;

    async_mutex mtx;

    // ---- T1 acquires uncontended (fast path). Disengage without unlocking.
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

    // ---- W1 queues first (pool index 0). No cancellation — W1 is meant to
    // be legitimately granted by unlock() #1.
    asio::io_context ioc_w1;
    bool w1_ok = false;
    std::optional<async_lock_guard> gw1;
    auto waiter_w1 = [&]() -> asio::awaitable<void> {
        auto r = co_await m->async_lock();
        w1_ok = r.has_value();
        if (w1_ok) gw1.emplace(std::move(*r));
    };
    auto fw1 = asio::co_spawn(ioc_w1, waiter_w1(), asio::use_future);
    std::thread thread_w1([&] { ioc_w1.run(); });
    ASSERT_TRUE(confirm_committed(ioc_w1))
        << "W1's queuing CAS was not observed committed within the bound";

    // ---- W2 queues second (pool index 1), with a cancellation slot bound.
    asio::io_context ioc_w2;
    asio::cancellation_signal sig_w2;
    bool w2_aborted = false;
    auto waiter_w2 = [&]() -> asio::awaitable<void> {
        auto r = co_await m->async_lock();
        w2_aborted = !r.has_value();
    };
    auto fw2 = asio::co_spawn(ioc_w2, waiter_w2(),
                              asio::bind_cancellation_slot(sig_w2.slot(), asio::use_future));
    std::thread thread_w2([&] { ioc_w2.run(); });
    ASSERT_TRUE(confirm_committed(ioc_w2))
        << "W2's queuing CAS was not observed committed within the bound";

    // ---- unlock() #1 (T1 releasing, direct synchronous call, no seam
    // pinning needed): grants W1, splices W2 onto next_drain_head_ as the
    // residual chain.
    m->unlock();

    // ---- Wait for W1's grant to actually resolve (its resume is posted
    // onto ioc_w1, already running). W1 becomes the new holder.
    ASSERT_EQ(fw1.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    fw1.get();
    thread_w1.join();
    ASSERT_TRUE(w1_ok);
    ASSERT_TRUE(gw1.has_value());
    async_mutex* m2 = gw1->release();
    ASSERT_EQ(m2, m);

    // ---- T1: dedicated thread calls unlock() #2 directly (releasing W1),
    // pins at unlock_pre_grant_cas_residual — immediately after the
    // residual walk loads W2's phase_ as queued, before the grant CAS.
    std::thread thread_a([&] {
        ctx.t1_tid.store(std::this_thread::get_id(), std::memory_order_release);
        m->unlock();
    });
    ctx.t1_parked.acquire();

    // ---- Cancel W2 — posted onto W2's OWN executor. Wait for W2's future
    // to resolve aborted, proving on_cancel's queued->cancelled CAS has
    // already committed BEFORE we release T1's pin.
    asio::post(ioc_w2.get_executor(), [&] { sig_w2.emit(asio::cancellation_type::total); });
    ASSERT_EQ(fw2.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    fw2.get();
    thread_w2.join();
    ASSERT_TRUE(w2_aborted);

    // ---- Release T1. Its grant CAS (expected=queued) now genuinely fails
    // against W2's cancelled phase_ — a real, non-simulated CAS loss in the
    // RESIDUAL walk specifically (distinct code from the FIFO walk above).
    ctx.t1_release.release();
    thread_a.join();

    // ---- Oracle (b): W2's pool slot (index 1) is the free-list head. W1
    // (index 0) has already been fully released (its grant + resume-runner
    // release_ref both ran before this point) and pushed onto the free list
    // earlier, but W2's push — the CAS-loser's `release_ref` — is always the
    // LAST push (LIFO), so it is always the head regardless of W1's timing.
    // A "skip the release" mutation on W2 leaves the head at whatever it was
    // before (0, from W1's earlier push), never 1.
    EXPECT_EQ(mtx.test_seam_free_list_head_slot_index(), 1u)
        << "W2's waiter_record was not released (pushed to the free list) by "
           "unlock()'s cancelled-branch fallthrough after the residual-walk "
           "CAS loss";
}
