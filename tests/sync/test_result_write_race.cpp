// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_result_write_race.cpp — Seam #28
//
// *result_ CAS-then-publish: only the CAS winner writes.
//
// For US1 (no cancel): the unlock drain is the sole writer.  We verify that
// the *result_ slot is never double-written and the guard is always exactly
// one of: engaged (granted) or error (aborted).
//
// The TSan-clean assertion is the primary oracle; the functional assertion is
// that every co_await async_lock() that completes returns either a valid
// guard or an explicit error, never garbage.
//
// Oracle: [2f §9 #28] — "*result_ CAS-then-publish arbitration" (v1.4).
//         I-06/I-09 memory-ordering invariants.
//
// 058-async-mutex-hardening T030 (US5, FR-008): the primary contention case
// (EveryAcquireReceivesWellFormedResult) was previously single-threaded-in-
// disguise — N=256 coroutines all cooperatively scheduled on ONE io_context
// driven by a single `ioc.run()` call on the calling thread
// (feedback_single_threaded_harness_masks_strand_races). The file header's
// "even under high-concurrency" claim was an over-claim: no two coroutines
// could ever genuinely race the *result_ CAS, since only one OS thread ever
// executed. Converted to the established multi-threaded-io_context idiom
// (test_arm64_weak_memory.cpp::MultiThreadedContentionMutualExclusion): T =
// hardware_concurrency() worker threads all call ioc.run() concurrently on
// the SAME io_context, so N genuinely-concurrent coroutines contend for the
// mutex across real cores/threads — the CAS-then-publish arbitration in
// unlock()'s drain (and the first-acquire fast path) now races for real.
// SequentialResultsAreConsistent and ResultSlotClearedAfterResume are left
// single-threaded intentionally: both are UNCONTENDED-by-construction (one
// coroutine acquiring in sequence / a single solo acquire) — there is no
// concurrent writer to race, so a genuine-MT driver would add threads
// without adding any discriminating power.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <fixpp/core/sync/async_mutex.hpp>
#include <future>
#include <thread>
#include <vector>

namespace {

using fixpp::sync::async_mutex;

TEST(SeamResultWriteRace, EveryAcquireReceivesWellFormedResult) {
    // N coroutines contend, genuinely concurrently across T worker threads
    // all draining the SAME io_context. Each verifies its result is either
    // has_value() (a valid guard) or has_error(). Never an uninitialised
    // slot, and never a double-grant (mutual exclusion overlap).
    // Kept at the original N=256 (well under async_mutex's 512-slot bounded
    // waiter pool, AM-P2-3/research.md D-4): with genuine cross-thread
    // contention, T worker threads can spawn+park a large fraction of N
    // near-simultaneously, so N must stay comfortably below the pool
    // capacity or the test starts legitimately (and correctly) exercising
    // pool exhaustion (sync_lock_alloc_failed) instead of the CAS-then-
    // publish arbitration this test targets — a DIFFERENT, already-covered
    // seam (test_pool_exhaustion_reuse.cpp, T020-T022).
    constexpr int N = 256;
    const unsigned T = std::max(2u, std::thread::hardware_concurrency());

    std::atomic<int> in_critical{0};
    std::atomic<int> overlap{0};
    std::atomic<int> good_results{0};
    std::atomic<int> bad_results{0};

    asio::io_context ioc;
    async_mutex mtx;

    auto make_coro = [&]() -> asio::awaitable<void> {
        auto r = co_await mtx.async_lock();
        // Result must be well-formed: either value or error.
        if (r.has_value()) {
            int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (v > 1) overlap.fetch_add(1, std::memory_order_relaxed);
            good_results.fetch_add(1, std::memory_order_acq_rel);
            in_critical.fetch_sub(1, std::memory_order_acq_rel);
        } else {
            // For US1 without cancel, no error is expected; flag it.
            bad_results.fetch_add(1, std::memory_order_acq_rel);
        }
    };

    std::vector<std::future<void>> futs;
    futs.reserve(N);
    for (int i = 0; i < N; ++i) futs.push_back(asio::co_spawn(ioc, make_coro(), asio::use_future));

    // Genuinely multi-threaded: T worker threads all call ioc.run()
    // concurrently on the ONE io_context — the standard asio multi-threaded
    // pattern (mirrors test_arm64_weak_memory.cpp). pool join is the real
    // drain barrier; futures are only checked for exceptions afterward (not
    // used as the barrier itself — feedback_use_future_get_not_a_pool_drain_
    // barrier).
    std::vector<std::thread> pool;
    pool.reserve(T);
    for (unsigned t = 0; t < T; ++t) pool.emplace_back([&] { ioc.run(); });
    for (auto& th : pool) th.join();
    for (auto& f : futs) f.get();

    EXPECT_EQ(overlap.load(), 0) << "Mutual exclusion violated";
    EXPECT_EQ(bad_results.load(), 0) << "Unexpected error result in US1 (no cancel)";
    EXPECT_EQ(good_results.load(), N);
}

TEST(SeamResultWriteRace, SequentialResultsAreConsistent) {
    // Sequential acquires by ONE coroutine: each result must be an engaged
    // guard. Uncontended by construction (no other acquirer exists) — no
    // race to genuinely-MT-ify; single io_context is appropriate here.
    constexpr int N = 64;
    int good = 0;

    asio::io_context ioc;
    async_mutex mtx;

    auto f = asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            for (int i = 0; i < N; ++i) {
                auto r = co_await mtx.async_lock();
                EXPECT_TRUE(r.has_value()) << "Sequential acquire must always succeed";
                if (r.has_value()) ++good;
            }
        },
        asio::use_future);
    ioc.run();
    f.get();

    EXPECT_EQ(good, N);
}

TEST(SeamResultWriteRace, ResultSlotClearedAfterResume) {
    // After await_resume() the guard is valid; no stale pointer. A single
    // solo (uncontended) acquire — no race to genuinely-MT-ify.
    asio::io_context ioc;
    bool result_valid = false;

    auto f = asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            async_mutex mtx;
            auto r = co_await mtx.async_lock();
            result_valid = r.has_value() && r->owns_lock();
            // guard released here
        },
        asio::use_future);
    ioc.run();
    f.get();

    EXPECT_TRUE(result_valid);
}

}  // namespace
