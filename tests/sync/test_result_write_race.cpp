// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_result_write_race.cpp — Seam #28
//
// *result_ CAS-then-publish: only the CAS winner writes.
//
// For US1 (no cancel): the unlock drain is the sole writer.  We verify that
// even under high-concurrency the *result_ slot is never double-written and
// the guard is always exactly one of: engaged (granted) or error (aborted).
//
// The TSan-clean assertion is the primary oracle; the functional assertion is
// that every co_await async_lock() that completes returns either a valid
// guard or an explicit error, never garbage.
//
// Oracle: [2f §9 #28] — "*result_ CAS-then-publish arbitration" (v1.4).
//         I-06/I-09 memory-ordering invariants.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <fixpp/core/sync/async_mutex.hpp>
#include <vector>

namespace {

using fixpp::sync::async_mutex;

TEST(SeamResultWriteRace, EveryAcquireReceivesWellFormedResult) {
    // N coroutines contend.  Each verifies its result is either has_value()
    // (a valid guard) or has_error().  Never an uninitialised slot.
    constexpr int N = 256;
    std::atomic<int> in_critical{0};
    int overlap = 0;
    int good_results = 0;
    int bad_results = 0;

    asio::io_context ioc;
    async_mutex mtx;

    auto make_coro = [&]() -> asio::awaitable<void> {
        auto r = co_await mtx.async_lock();
        // Result must be well-formed: either value or error.
        if (r.has_value()) {
            int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (v > 1) ++overlap;
            ++good_results;
            in_critical.fetch_sub(1, std::memory_order_acq_rel);
        } else {
            // For US1 without cancel, no error is expected; flag it.
            ++bad_results;
        }
    };

    std::vector<std::future<void>> futs;
    for (int i = 0; i < N; ++i) futs.push_back(asio::co_spawn(ioc, make_coro(), asio::use_future));
    ioc.run();
    for (auto& f : futs) f.get();

    EXPECT_EQ(overlap, 0) << "Mutual exclusion violated";
    EXPECT_EQ(bad_results, 0) << "Unexpected error result in US1 (no cancel)";
    EXPECT_EQ(good_results, N);
}

TEST(SeamResultWriteRace, SequentialResultsAreConsistent) {
    // Sequential acquires: each result must be an engaged guard.
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
    // After await_resume() the guard is valid; no stale pointer.
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
