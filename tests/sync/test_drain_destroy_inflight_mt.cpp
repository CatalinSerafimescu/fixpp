// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_drain_destroy_inflight_mt.cpp — 058 T015 (US2)
//
// Genuinely multi-threaded, cross-executor drain-then-destroy witness.
//
// Positive HB-observation witness for research.md D-2 / contracts/async_mutex-
// contract-delta.md's "New guarantee (scoped)" (the parked-then-reaped case):
// a waiter parks on a DIFFERENT io_context/OS thread than the drain-owning
// strand; the drain reaps it (marks it cancelled, then posts its abort-
// delivery runner onto the WAITER's OWN stored executor — genuinely
// cross-executor, cross-core, per store_executor()'s resume_fn_). That runner
// pushes the freed slot into the mutex-owned free-list pool storage
// (release_ref, async_mutex.hpp:790-828) and, as its LAST statement,
// decrements in_flight_resumers_ with RELEASE ordering (T016,
// async_mutex.hpp:672). The drain's terminal condition observes
// in_flight_resumers_==0 via an ACQUIRE load (T016, async_mutex.hpp:1399) —
// establishing the happens-before that makes destroying the mutex
// immediately after drain-return memory-safe even though the pool write ran
// on a different core/thread.
//
// FRAMING (per orchestrator brief — do not overclaim): this is a POSITIVE
// witness, not a RED-first mutation test. On this x86_64 (TSO) host the
// property would hold trivially even under a hypothetical relaxed-ordering
// regression (no store-buffering fence is needed for cross-core visibility
// on TSO), so this test is not expected to discriminate a reverted T016 on
// this host. The D-2 release/acquire pairing is a TSan-MODELED discriminator
// (TSan enforces the abstract-machine happens-before requirements
// independent of host ISA) — its dedicated RED-on-mutation proof is T045
// (the extension of test_arm64_weak_memory.cpp), landing later in this
// feature. This test is therefore run under TSan as its primary gate: a
// clean pass here is evidence the cross-executor teardown sequence is
// race-free as specified, NOT a substitute for T045's discriminating proof.
//
// Coordination: deterministic only — no sleep(). The precondition state
// (holder acquired, waiter genuinely PARKED on state_'s LIFO on its own
// io_context) is reached via bounded single-threaded io_context::poll_one()
// loops BEFORE any background thread starts (same idiom as
// test_destructor_release_death.cpp) — there is no race in getting to the
// interesting state. Only the drain + cross-executor reap + destroy sequence
// itself runs on genuine OS threads (one per io_context, joined via
// std::future/std::thread — no std::mutex/condition_variable is introduced
// by this test, FR-012). The holder/drain relative ordering on the single
// drain-owning io_context (serviced by exactly one background thread, so
// still strictly FIFO) is governed by well-separated yield_n() counts —
// the same "canonical §4.7.4 sequencing" idiom already proven by
// test_destructor_release_death.cpp's ProperlyDrainedMutexDoesNotTerminate.
//
// Design: research.md D-2; contracts/async_mutex-contract-delta.md
// "New guarantee (scoped)" + "Explicit EXCLUSION".
// Task: T015.

#include <gtest/gtest.h>

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
#include <thread>

#include "sync/sync_test_support.hpp"

namespace {

using fixpp::core::error;
using fixpp::sync::async_lock_guard;
using fixpp::sync::async_mutex;
using fixpp::sync::expected_t;

using fixpp::sync::test::yield_n;

// One full cross-executor drain-then-destroy cycle. Uses ASSERT_* (this is a
// void helper, matching the death-test/drain-test convention in this suite)
// so a failed precondition aborts the cycle cleanly rather than corrupting
// later assertions.
void run_one_cycle() {
    constexpr int N = 4;

    auto* mtx = new async_mutex;

    asio::io_context ioc_drain;   // the drain-owning strand's executor
    asio::io_context ioc_waiter;  // the waiter's OWN executor — serviced by a
                                   // genuinely different OS thread below

    // ── Step 1 (deterministic, single-threaded): holder acquires ───────────
    // No background thread has started yet — polling ioc_drain from the
    // calling thread is race-free.
    bool holder_acquired = false;
    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        holder_acquired = g.has_value();
        // Hold across a LONG yield sequence so the eventual real unlock()
        // (guard destructor, below) happens well after drain_coro (spawned
        // later, with a much SHORTER yield delay) has set draining_ and run
        // its first reap pass — see the sequencing note near drain_coro.
        co_await yield_n(N * 20);
        // `g` destructs here -> the REAL unlock(). By now draining_ is long
        // since true, so this unlock() takes the draining_ short-circuit
        // path (async_mutex.hpp:1159) and does NOT touch the waiter list —
        // the parked-then-reaped waiter below is exclusively the drain's
        // concern, never granted by this unlock().
    };
    asio::co_spawn(ioc_drain, holder_coro(), asio::detached);
    for (int i = 0; i < 16 && !holder_acquired; ++i) ioc_drain.poll_one();
    ASSERT_TRUE(holder_acquired) << "setup: holder failed to acquire";

    // ── Step 2 (deterministic, single-threaded): waiter parks cross-executor ──
    // Spawned on ioc_waiter — a DIFFERENT io_context than ioc_drain. Since
    // the mutex is held (step 1), async_lock() synchronously queues this
    // waiter on state_'s LIFO before it suspends (parking is inline, before
    // control returns to ioc_waiter's poll loop) — the resume executor
    // captured by store_executor() is ioc_waiter's, which a genuinely
    // different OS thread will service below (thread_b).
    std::atomic<bool> waiter_resolved{false};
    std::atomic<bool> waiter_aborted{false};
    // Discriminator (advisor review): TSan-clean is only meaningful evidence
    // if the cross-executor path genuinely engaged. Each io_context here is
    // serviced by exactly one dedicated OS thread (thread_a / thread_b
    // below), so capturing the thread that actually runs the abort-delivery
    // resume is deterministic (not the shared-pool thread-id flake noted
    // elsewhere in the suite) and proves the resume ran on ioc_waiter's own
    // thread, not inline on the drain's thread.
    std::atomic<std::thread::id> resume_thread_id{};
    auto waiter_coro = [&]() -> asio::awaitable<void> {
        // Post once first so the coroutine is genuinely scheduled ON
        // ioc_waiter (not inline-invoked from co_spawn's initiation) before
        // it attempts the lock — matches the parking idiom used throughout
        // this suite (e.g. test_destructor_release_death.cpp's waiter_coro).
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        auto r = co_await mtx->async_lock();
        resume_thread_id.store(std::this_thread::get_id(), std::memory_order_release);
        waiter_aborted.store(!r.has_value() && r.error() == error::sync_lock_aborted,
                              std::memory_order_release);
        waiter_resolved.store(true, std::memory_order_release);
    };
    asio::co_spawn(ioc_waiter, waiter_coro(), asio::detached);
    for (int i = 0; i < 16 && !waiter_resolved.load(std::memory_order_acquire); ++i)
        ioc_waiter.poll_one();
    ASSERT_FALSE(waiter_resolved.load(std::memory_order_acquire))
        << "setup: waiter resolved before parking — the mutex was not held, so "
           "this cycle no longer exercises the cross-executor reap";

    // ── Step 3: the genuinely cross-thread phase ────────────────────────────
    //
    // drain_coro runs entirely on ioc_drain (thread_a below): a short yield_n
    // delay, then cancel_and_drain(). Because ioc_drain is serviced by
    // exactly ONE background thread, holder_coro's and drain_coro's relative
    // ordering stays strictly FIFO/deterministic even though neither runs on
    // the calling thread anymore — drain's short (N*2) yield count vs.
    // holder's long (N*20) yield count guarantees draining_ is set and the
    // FIRST reap pass runs (catching the already-parked waiter from step 2)
    // well before holder's guard destructs.
    //
    // The reap posts the waiter's abort-delivery runner onto ioc_waiter —
    // thread_b, a genuinely different OS thread/core — the exact
    // cross-executor edge D-2 makes memory-safe. `delete mtx` runs
    // immediately on drain return (thread_a), mirroring the "immediate
    // destroy after drain" idiom (test_drain_immediate_destroy_after_reap.cpp)
    // but now genuinely racing a resumer that ran on another OS thread.
    bool drain_ok = false;
    auto drain_coro = [&]() -> asio::awaitable<void> {
        co_await yield_n(N * 2);
        auto d = co_await mtx->cancel_and_drain();
        drain_ok = d.has_value();
        delete mtx;
    };
    auto drain_future = asio::co_spawn(ioc_drain, drain_coro(), asio::use_future);

    std::thread thread_a([&] { ioc_drain.run(); });
    std::thread thread_b([&] { ioc_waiter.run(); });
    auto const thread_a_id = thread_a.get_id();
    auto const thread_b_id = thread_b.get_id();

    // Bounded wait via std::future::wait_for — the established idiom in this
    // suite (test_drain_immediate_destroy_after_reap.cpp) — introduces no
    // std::mutex/condition_variable of our own (FR-012); std::future's
    // internal synchronization is standard-library machinery, not
    // user-authored.
    bool timed_out = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (drain_future.wait_for(std::chrono::milliseconds(10)) == std::future_status::ready)
            break;
    }
    if (drain_future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        timed_out = true;
        ioc_drain.stop();
        ioc_waiter.stop();
    }
    thread_a.join();
    thread_b.join();

    ASSERT_FALSE(timed_out) << "cross-executor drain-then-destroy hung";
    drain_future.get();

    EXPECT_TRUE(drain_ok) << "cancel_and_drain() must succeed";
    EXPECT_TRUE(waiter_resolved.load(std::memory_order_acquire))
        << "the cross-executor waiter must have been resolved by the reap";
    EXPECT_TRUE(waiter_aborted.load(std::memory_order_acquire))
        << "the reaped waiter must resolve sync_lock_aborted (not granted — "
           "the manual unlock() this design avoids would have granted it "
           "instead, which is the shape this test does NOT exercise)";

    // Discriminator: the abort-delivery resume (and this test's TSan-clean
    // oracle) is only meaningful if it genuinely ran on ioc_waiter's own
    // thread — not inline on the drain's thread (which would collapse this
    // into a same-thread scenario, proving nothing about the cross-executor
    // HB edge D-2 fixes).
    EXPECT_EQ(resume_thread_id.load(std::memory_order_acquire), thread_b_id)
        << "the waiter's resume must run on ioc_waiter's own thread (thread_b) "
           "-- genuinely cross-executor from the drain";
    EXPECT_NE(resume_thread_id.load(std::memory_order_acquire), thread_a_id)
        << "the resume must NOT have run inline on the drain's thread (thread_a)";
}

TEST(DrainDestroyInflightMt, CrossExecutorReapThenDestroyIsSafe) {
    run_one_cycle();
}

TEST(DrainDestroyInflightMt, RepeatedCrossExecutorReapThenDestroyIsClean) {
    constexpr int kReps = 20;
    for (int rep = 0; rep < kReps; ++rep) {
        SCOPED_TRACE(rep);
        run_one_cycle();
        if (::testing::Test::HasFatalFailure()) break;
    }
}

}  // namespace
