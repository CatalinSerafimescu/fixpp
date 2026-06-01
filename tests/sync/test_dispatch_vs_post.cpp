// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_dispatch_vs_post.cpp — Seam #12
//
// `dispatch` (default) vs `post` per-mutex completion policy.
//
// dispatch policy: if running_in_this_thread(), resume inline on the unlocking
//   thread.  Otherwise, post through the executor.
// post policy: always post through the bound executor; one extra hop.
//
// On a single-threaded io_context we verify:
//  - dispatch mutex: waiter resumes (both coroutines complete correctly).
//  - post mutex: waiter resumes (both coroutines complete correctly).
//  - Both policies produce mutual exclusion.
//
// Oracle: [2f §9 #12] — "dispatch vs post policy effect on completion".
//         [2f §4.6] — inline-vs-post predicate.

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
using fixpp::sync::completion_policy;

// Generic test body: N coroutines contend on `mtx`.
static void run_contention_test(async_mutex& mtx, int N) {
    asio::io_context ioc;
    std::atomic<int> in_critical{0};
    int overlap = 0;
    int counter = 0;

    auto make_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (v > 1) ++overlap;
        ++counter;
        in_critical.fetch_sub(1, std::memory_order_acq_rel);
    };

    std::vector<std::future<void>> futs;
    for (int i = 0; i < N; ++i) futs.push_back(asio::co_spawn(ioc, make_coro(), asio::use_future));
    ioc.run();
    for (auto& f : futs) f.get();

    EXPECT_EQ(overlap, 0) << "Mutual exclusion violated";
    EXPECT_EQ(counter, N) << "Not all coroutines completed";
}

TEST(SeamDispatchVsPost, DispatchPolicyCorrct) {
    async_mutex mtx{completion_policy::dispatch};
    run_contention_test(mtx, 64);
}

TEST(SeamDispatchVsPost, PostPolicyCorrect) {
    async_mutex mtx{completion_policy::post};
    run_contention_test(mtx, 64);
}

TEST(SeamDispatchVsPost, DefaultIsDispatch) {
    async_mutex mtx;
    EXPECT_EQ(mtx.policy(), completion_policy::dispatch);
}

TEST(SeamDispatchVsPost, ExplicitPostSetable) {
    async_mutex mtx{completion_policy::post};
    EXPECT_EQ(mtx.policy(), completion_policy::post);
}

TEST(SeamDispatchVsPost, DispatchAndPostBothCompleteCorrectly) {
    // Run the same workload under both policies and assert correctness.
    for (auto policy : {completion_policy::dispatch, completion_policy::post}) {
        async_mutex mtx{policy};
        run_contention_test(mtx, 32);
    }
}

}  // namespace
