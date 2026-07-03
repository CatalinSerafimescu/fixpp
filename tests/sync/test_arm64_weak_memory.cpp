// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_arm64_weak_memory.cpp — Seam #18
//
// Weak-memory contention stress for the atomic state machine. Verifies the
// I-01..I-31 memory-ordering specification ([2f §6.2/§6.2.2]; FR-013; SC-007)
// holds under genuine cross-core contention: a MULTI-THREADED io_context runs
// the acquire/release/cancel mix so the release/acquire pairings on `state_`,
// `phase_`, `next_drain_head_`, the waiter_record refcount and the latch are
// exercised by distinct hardware threads.
//
// Host note: this build host is x86_64 (TSO). TSan models the C++ memory
// model independent of host ISA, so an under-specified ordering (e.g. a
// relaxed where release/acquire is required by I-03/I-06/I-07/I-08/I-32)
// surfaces here regardless of host. Native ARM64 (weak/LL-SC) execution of
// this seam is recorded host-unavailable in the verify doc; the TSan run
// under this seam is the portable I-01..I-31 verification (SC-007).

#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <array>
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

// Multi-threaded contention: T worker threads draining one io_context, N
// coroutines contending for a single mutex. Mutual exclusion must hold with
// zero observed overlap; every coroutine completes exactly once.
TEST(SeamArm64WeakMemory, MultiThreadedContentionMutualExclusion) {
    constexpr int N = 4'000;
    const unsigned T = std::max(2u, std::thread::hardware_concurrency());

    std::atomic<int> in_critical{0};
    std::atomic<int> overlap{0};
    std::atomic<int> completed{0};

    asio::io_context ioc;
    async_mutex mtx;

    auto make_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        if (g.has_value()) {
            int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (v > 1) overlap.fetch_add(1, std::memory_order_relaxed);
            in_critical.fetch_sub(1, std::memory_order_acq_rel);
        }
        completed.fetch_add(1, std::memory_order_acq_rel);
    };

    std::vector<std::future<void>> futs;
    futs.reserve(N);
    for (int i = 0; i < N; ++i) futs.push_back(asio::co_spawn(ioc, make_coro(), asio::use_future));

    std::vector<std::thread> pool;
    pool.reserve(T);
    for (unsigned t = 0; t < T; ++t) pool.emplace_back([&] { ioc.run(); });
    for (auto& th : pool) th.join();
    for (auto& f : futs) f.get();

    EXPECT_EQ(overlap.load(), 0) << "mutual exclusion broken under weak memory";
    EXPECT_EQ(completed.load(), N) << "lost or double-completed coroutine";
}

// NOTE (048 / Erratum E-5 / Gate B P2-5): the former `DrainUnderMultiThreadContention`
// test ran cancel_and_drain() concurrently with acquirers/unlocks on a MULTI-THREADED
// io_context. Under the strand-local-reap contract (contracts/async_mutex-contract.md
// §Unsupported), drain-overlap with another thread's acquire/cancel/unlock is UNDEFINED
// — so a green sanitizer result there exercises undefined behavior and proves nothing.
// It was removed. Ordinary cross-thread async_lock/unlock contention (the §1.1 seam) IS
// supported and remains covered by MultiThreadedContentionMutualExclusion above. The
// unsupported drain-overlap is documentation-enforced (no production assertion seam,
// Gate-A P2-4); we do NOT invoke the UB to "test" it.

// ─────────────────────────────────────────────────────────────────────────────
// 058 T045 (US5, closes gap T-5): the D-2 release/acquire discriminator.
//
// research.md D-2 pairs the resume runner's `in_flight_resumers_.fetch_sub(1,
// release)` (async_mutex.hpp:767, the LAST statement after `release_ref`'s
// pool-slot write) with two ACQUIRE loads: the drain terminal condition
// (async_mutex.hpp:1613) and the destructor guard (async_mutex.hpp:909). This
// is the happens-before edge that makes destroying the mutex immediately
// after a drain/quiescence observation memory-safe even though the resumer's
// pool write ran on a different OS thread/core.
// `test_drain_destroy_inflight_mt.cpp` (T015) is a POSITIVE HB-observation
// witness for this same pairing but was explicitly NOT verified to
// discriminate a relaxed-ordering regression (see its file header) — this is
// the dedicated RED-on-mutation proof that file deferred. TSan's
// happens-before model flags a missing synchronizes-with edge based on
// recognized synchronization primitives, independent of real wall-clock
// overlap or host ISA (see this file's header above) — so a genuine
// discriminator does not require winning a timing race, only that the
// resumer's write and the destroyer's read/deallocate are never connected by
// any OTHER recognized edge.
//
// Topology (load-bearing): every epoch below uses TWO separate
// single-threaded `asio::io_context` instances, each serviced by exactly ONE
// dedicated OS thread — never a shared multi-worker `thread_pool` driving one
// `io_context`. A shared io_context's internal scheduler lock is itself a
// real mutex acquire/release that TSan recognizes as a synchronizes-with
// edge; because mutex release carries every prior program-order write on
// that thread forward (transitively), any cross-thread handoff through a
// SHARED io_context would silently mask a relaxed-ordering mutation on
// `in_flight_resumers_` regardless of what ordering the test's own code uses
// (feedback_strand_in_any_executor_refcount_race is the sibling masking
// risk for a different primitive). Two independently-single-threaded
// io_contexts have no such shared lock.
//
// Channel audit (verified by reading the production code, not assumed):
// neither the drain terminal loop (async_mutex.hpp:1610-1617) nor the
// destructor (async_mutex.hpp:906-912) reads or CASes `waiter_pool_free_`
// (the D-1 free-list head) — both touch only `state_`, `next_drain_head_`,
// `active_holders_count_` and `in_flight_resumers_`. The resumer runner's
// pool-slot push (`release_ref`, async_mutex.hpp:940-978) is a RELEASE CAS on
// `waiter_pool_free_` with no corresponding ACQUIRE anywhere on the
// destroyer side in these epochs (no further pop happens — the mutex is
// destroyed, not reused) — so it grants no alternate happens-before edge that
// could rescue a D-2 mutation. `in_flight_resumers_` is confirmed the SOLE
// channel.
//
// Each epoch's SEQUENCING (not its correctness) may use a plain test-local
// atomic poll — always gated on state observed to have been set BEFORE the
// resumer runner's risky pool write in program order (documented per-epoch
// below), never AFTER it, so the poll cannot accidentally supply the very
// happens-before edge under test (a distinct trap from the topology one
// above).

namespace weak_memory_d2 {

// Bounded wait for a future's readiness — avoids hanging the suite if a
// genuinely-multi-threaded run wedges (feedback_ci_hung_test_no_timeout_
// burns_6h). Not a substitute for the discriminating assertions; it only
// bounds how long a stuck run can block CI.
template <typename T>
bool wait_ready(std::future<T>& f, std::chrono::steady_clock::time_point deadline) {
    while (std::chrono::steady_clock::now() < deadline) {
        if (f.wait_for(std::chrono::milliseconds(5)) == std::future_status::ready) return true;
    }
    return f.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
}

// ── Epoch 1: cancellation-driven cross-thread resume, then drain+destroy ──
//
// The waiter is resolved via a cancellation signal (never via a grant): the
// holder posts `cancel_sig.emit()` onto the waiter's own executor (thread_b,
// per the established asio slot-thread-affinity idiom — asio::cancellation_
// signal/slot are not thread-safe, so emit() must run on the slot owner's own
// executor) and then WAITS (mutex still held throughout, so a grant is
// structurally impossible) for `waiter_resolved` before it ever unlocks. This
// makes cancellation win deterministically by construction, not by timing.
// `waiter_resolved` is set by the waiter coroutine INSIDE invoke_handler,
// which the resume runner calls BEFORE `release_ref`/`fetch_sub` (async_
// mutex.hpp:717-767) — so gating the holder's own unlock on it does not
// observe (and cannot rescue) the risky writes that come after.
void run_cancellation_epoch() {
    constexpr int N = 4;
    auto* mtx = new async_mutex;

    asio::io_context ioc_a;  // holder + drain + destroy, thread_a
    asio::io_context ioc_b;  // waiter + its cancellation slot, thread_b
    asio::cancellation_signal cancel_sig;

    bool holder_acquired = false;
    std::atomic<bool> waiter_resolved{false};
    std::atomic<bool> waiter_aborted{false};
    std::atomic<std::thread::id> resume_thread_id{};
    bool drain_ok = false;

    auto holder_coro = [&]() -> asio::awaitable<void> {
        {
            auto g = co_await mtx->async_lock();
            holder_acquired = g.has_value();
            co_await yield_n(N);
            asio::post(ioc_b.get_executor(),
                       [&] { cancel_sig.emit(asio::cancellation_type::terminal); });
            // Mutex is still held here — a grant is impossible, so this can
            // only observe the cancellation's own resolution.
            while (!waiter_resolved.load(std::memory_order_acquire))
                co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        }  // g destructs -> real unlock(); no waiter remains queued, fast path.
        auto d = co_await mtx->cancel_and_drain();
        drain_ok = d.has_value();
        delete mtx;
    };

    auto waiter_coro = [&]() -> asio::awaitable<void> {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        auto r = co_await mtx->async_lock();
        resume_thread_id.store(std::this_thread::get_id(), std::memory_order_release);
        waiter_aborted.store(!r.has_value() && r.error() == error::sync_lock_aborted,
                              std::memory_order_release);
        waiter_resolved.store(true, std::memory_order_release);
    };

    auto holder_future = asio::co_spawn(ioc_a, holder_coro(), asio::use_future);
    for (int i = 0; i < 16 && !holder_acquired; ++i) ioc_a.poll_one();
    ASSERT_TRUE(holder_acquired) << "setup: holder failed to acquire";

    asio::co_spawn(ioc_b, waiter_coro(),
                    asio::bind_cancellation_slot(cancel_sig.slot(), asio::detached));
    for (int i = 0; i < 16 && !waiter_resolved.load(std::memory_order_acquire); ++i)
        ioc_b.poll_one();
    ASSERT_FALSE(waiter_resolved.load(std::memory_order_acquire))
        << "setup: waiter resolved before parking";

    std::thread thread_a([&] { ioc_a.run(); });
    std::thread thread_b([&] { ioc_b.run(); });
    auto const thread_a_id = thread_a.get_id();
    auto const thread_b_id = thread_b.get_id();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool ready = wait_ready(holder_future, deadline);
    if (!ready) {
        ioc_a.stop();
        ioc_b.stop();
    }
    thread_a.join();
    thread_b.join();

    ASSERT_TRUE(ready) << "cancellation-then-drain-then-destroy hung";
    holder_future.get();

    EXPECT_TRUE(drain_ok) << "cancel_and_drain() must succeed";
    EXPECT_TRUE(waiter_resolved.load(std::memory_order_acquire));
    EXPECT_TRUE(waiter_aborted.load(std::memory_order_acquire))
        << "the mutex was held throughout the wait, so only the cancellation "
           "could have resolved the waiter";
    EXPECT_EQ(resume_thread_id.load(std::memory_order_acquire), thread_b_id)
        << "the cancel-delivery resume must run on the waiter's own thread";
    EXPECT_NE(resume_thread_id.load(std::memory_order_acquire), thread_a_id);
}

// ── Epoch 2: drain-reaped parked waiter, then destroy ──────────────────────
//
// The waiter is still genuinely QUEUED (never self-cancelled) when the drain
// reaps it: `cancel_and_drain()`'s `reap_chain` (async_mutex.hpp:1558-1576)
// CASes the queued waiter to cancelled and calls `schedule_record_resume`
// FROM thread_a, which posts the actual resumer runner onto the waiter's OWN
// stored executor (thread_b) — the cross-executor edge D-2 makes safe.
void run_drain_reap_epoch() {
    constexpr int N = 4;
    auto* mtx = new async_mutex;

    asio::io_context ioc_a;  // drain-owning thread_a
    asio::io_context ioc_b;  // waiter's own executor, thread_b

    bool holder_acquired = false;
    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        holder_acquired = g.has_value();
        co_await yield_n(N * 20);
        // g destructs here -> real unlock(), well after drain_coro has set
        // draining_ and reaped this coroutine's queued waiter (short N*2
        // delay below) — takes the draining_ short-circuit, touches nothing.
    };
    asio::co_spawn(ioc_a, holder_coro(), asio::detached);
    for (int i = 0; i < 16 && !holder_acquired; ++i) ioc_a.poll_one();
    ASSERT_TRUE(holder_acquired) << "setup: holder failed to acquire";

    std::atomic<bool> waiter_resolved{false};
    std::atomic<bool> waiter_aborted{false};
    std::atomic<std::thread::id> resume_thread_id{};
    auto waiter_coro = [&]() -> asio::awaitable<void> {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        auto r = co_await mtx->async_lock();
        resume_thread_id.store(std::this_thread::get_id(), std::memory_order_release);
        waiter_aborted.store(!r.has_value() && r.error() == error::sync_lock_aborted,
                              std::memory_order_release);
        waiter_resolved.store(true, std::memory_order_release);
    };
    asio::co_spawn(ioc_b, waiter_coro(), asio::detached);
    for (int i = 0; i < 16 && !waiter_resolved.load(std::memory_order_acquire); ++i)
        ioc_b.poll_one();
    ASSERT_FALSE(waiter_resolved.load(std::memory_order_acquire))
        << "setup: waiter resolved before parking";

    bool drain_ok = false;
    auto drain_coro = [&]() -> asio::awaitable<void> {
        co_await yield_n(N * 2);
        auto d = co_await mtx->cancel_and_drain();
        drain_ok = d.has_value();
        delete mtx;
    };
    auto drain_future = asio::co_spawn(ioc_a, drain_coro(), asio::use_future);

    std::thread thread_a([&] { ioc_a.run(); });
    std::thread thread_b([&] { ioc_b.run(); });
    auto const thread_a_id = thread_a.get_id();
    auto const thread_b_id = thread_b.get_id();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool ready = wait_ready(drain_future, deadline);
    if (!ready) {
        ioc_a.stop();
        ioc_b.stop();
    }
    thread_a.join();
    thread_b.join();

    ASSERT_TRUE(ready) << "drain-reap-then-destroy hung";
    drain_future.get();

    EXPECT_TRUE(drain_ok);
    EXPECT_TRUE(waiter_resolved.load(std::memory_order_acquire));
    EXPECT_TRUE(waiter_aborted.load(std::memory_order_acquire));
    EXPECT_EQ(resume_thread_id.load(std::memory_order_acquire), thread_b_id);
    EXPECT_NE(resume_thread_id.load(std::memory_order_acquire), thread_a_id);
}

// ── Epoch 3: free-list churn — M waiters reaped together, then destroy ─────
//
// M waiters queue behind one holder, all on the SAME waiter-side executor
// (thread_b). One drain pass reaps ALL of them: `in_flight_resumers_` is
// incremented M times (relaxed `fetch_add`, untouched by D-2) and M
// independent resumer runners each perform their own pool-slot push
// (`release_ref`) + RELEASE decrement on thread_b, sequentially (thread_b is
// single-threaded) but genuinely concurrently with thread_a's repeated
// quiescence polling. The drain's terminal condition must observe the LAST
// of the M decrements before it is safe to destroy — exercising the same D-2
// pairing under multi-writer pool churn rather than a single waiter.
void run_free_list_churn_epoch() {
    constexpr int N = 4;
    constexpr int M = 6;
    auto* mtx = new async_mutex;

    asio::io_context ioc_a;  // holder + drain + destroy, thread_a
    asio::io_context ioc_b;  // every waiter's own executor (shared), thread_b

    bool holder_acquired = false;
    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx->async_lock();
        holder_acquired = g.has_value();
        co_await yield_n(N * 20);
    };
    asio::co_spawn(ioc_a, holder_coro(), asio::detached);
    for (int i = 0; i < 16 && !holder_acquired; ++i) ioc_a.poll_one();
    ASSERT_TRUE(holder_acquired) << "setup: holder failed to acquire";

    std::atomic<int> resolved_count{0};
    std::atomic<int> aborted_count{0};
    std::array<std::atomic<std::thread::id>, M> resume_thread_ids{};
    auto make_waiter = [&](int idx) -> asio::awaitable<void> {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        auto r = co_await mtx->async_lock();
        resume_thread_ids[idx].store(std::this_thread::get_id(), std::memory_order_release);
        if (!r.has_value() && r.error() == error::sync_lock_aborted)
            aborted_count.fetch_add(1, std::memory_order_acq_rel);
        resolved_count.fetch_add(1, std::memory_order_acq_rel);
    };
    for (int i = 0; i < M; ++i) asio::co_spawn(ioc_b, make_waiter(i), asio::detached);
    for (int i = 0; i < 16 * M && resolved_count.load(std::memory_order_acquire) < M; ++i)
        ioc_b.poll_one();
    ASSERT_EQ(resolved_count.load(std::memory_order_acquire), 0)
        << "setup: a waiter resolved before parking";

    bool drain_ok = false;
    auto drain_coro = [&]() -> asio::awaitable<void> {
        co_await yield_n(N * 2);
        auto d = co_await mtx->cancel_and_drain();
        drain_ok = d.has_value();
        delete mtx;
    };
    auto drain_future = asio::co_spawn(ioc_a, drain_coro(), asio::use_future);

    std::thread thread_a([&] { ioc_a.run(); });
    std::thread thread_b([&] { ioc_b.run(); });
    auto const thread_a_id = thread_a.get_id();
    auto const thread_b_id = thread_b.get_id();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    bool ready = wait_ready(drain_future, deadline);
    if (!ready) {
        ioc_a.stop();
        ioc_b.stop();
    }
    thread_a.join();
    thread_b.join();

    ASSERT_TRUE(ready) << "free-list-churn-then-destroy hung";
    drain_future.get();

    EXPECT_TRUE(drain_ok);
    EXPECT_EQ(resolved_count.load(std::memory_order_acquire), M);
    EXPECT_EQ(aborted_count.load(std::memory_order_acquire), M)
        << "none of the M queued waiters was ever granted (holder never "
           "unlocked) — all M must be reaped by the drain";
    for (int i = 0; i < M; ++i) {
        EXPECT_EQ(resume_thread_ids[i].load(std::memory_order_acquire), thread_b_id) << i;
        EXPECT_NE(resume_thread_ids[i].load(std::memory_order_acquire), thread_a_id) << i;
    }
}

}  // namespace weak_memory_d2

TEST(SeamArm64WeakMemory, D2CancellationEpochCrossThreadDestroyIsSafe) {
    constexpr int kReps = 20;
    for (int rep = 0; rep < kReps; ++rep) {
        SCOPED_TRACE(rep);
        weak_memory_d2::run_cancellation_epoch();
        if (::testing::Test::HasFatalFailure()) break;
    }
}

TEST(SeamArm64WeakMemory, D2DrainThenDestroyEpochIsSafe) {
    constexpr int kReps = 20;
    for (int rep = 0; rep < kReps; ++rep) {
        SCOPED_TRACE(rep);
        weak_memory_d2::run_drain_reap_epoch();
        if (::testing::Test::HasFatalFailure()) break;
    }
}

TEST(SeamArm64WeakMemory, D2FreeListChurnEpochIsSafe) {
    constexpr int kReps = 15;
    for (int rep = 0; rep < kReps; ++rep) {
        SCOPED_TRACE(rep);
        weak_memory_d2::run_free_list_churn_epoch();
        if (::testing::Test::HasFatalFailure()) break;
    }
}

}  // namespace
