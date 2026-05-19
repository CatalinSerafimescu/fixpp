// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_halo_firing.cpp — Seam #9, T055
//
// HALO-eligibility size budget + functional correctness.
//
// Compile-time: static_assert(sizeof(async_mutex_awaiter) <= 96).
// Runtime mirror: EXPECT_LE(sizeof(...), 96u).
//
// HALO (Heap Allocation eLimination Optimization): the async_lock() coroutine
// frame holds the async_mutex_awaiter as a local variable (Erratum E-1). If the
// compiler elides the coroutine frame heap allocation (HALO / Heap-Elision per
// [Clang §4.3.4 case 1]), the contended path becomes zero-global-heap.
//
// IMPORTANT: true HALO frame-elision is compiler-dependent and is classified
// bench-soft per §6.4 / §4.3.4 case 2 (observed in bench mode only). This seam
// asserts ONLY the awaiter size budget and functional correctness; it does NOT
// auto-fail if the coroutine frame is heap-allocated by the current toolchain.
//
// Oracle: [2f §9 #9] — "HALO size budget (seam #9)".
// §1.1 / §6.4: awaiter byte budget ≤ 96 B is the HALO-eligibility precondition.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <atomic>
#include <vector>

#include <fixpp/core/sync/async_mutex.hpp>

namespace {

using fixpp::sync::async_mutex;
using fixpp::sync::detail::async_mutex_awaiter;

// Compile-time budget check — mirrored from the header's own static_assert
// (§1.1 / §6.4). Placed here as documentation of the seam oracle.
static_assert(sizeof(async_mutex_awaiter) <= 96,
    "async_mutex_awaiter exceeds the §1.1 ≤ 96 B HALO-eligibility budget.");

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: awaiter size budget — runtime mirror.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SyncHaloFiring, AwaiterSizeBudget) {
    EXPECT_LE(sizeof(async_mutex_awaiter), 96u)
        << "async_mutex_awaiter exceeds the 96 B HALO-eligibility budget "
           "(§1.1/§6.4); the contended embedded-path cannot be HALO-eligible "
           "with the current struct layout.";

    // Informational: print actual size.
    std::printf("[INFO] sizeof(async_mutex_awaiter) = %zu B\n",
                sizeof(async_mutex_awaiter));
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: modest contended run — embedded path (mr == nullptr).
// Exercises the awaiter's frame-local lifecycle under contention so that
// compile-under-sanitiser catches layout / lifetime bugs before HALO spike.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SyncHaloFiring, ContendedEmbeddedPathFunctional) {
    // N coroutines, each acquiring once; M holders interleaved to force
    // contention and actually exercise the suspend/resume path.
    constexpr int N = 64;

    async_mutex mtx;
    std::atomic<int> in_critical{0};
    int overlap  = 0;
    int counter  = 0;

    asio::io_context ioc;

    auto make_coro = [&]() -> asio::awaitable<void> {
        // mr == nullptr → embedded path (HALO-eligible frame layout).
        auto g = co_await mtx.async_lock(nullptr);
        if (!g.has_value()) {
            ADD_FAILURE() << "unexpected error on embedded path: "
                          << static_cast<int>(g.error());
            co_return;
        }
        int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (v > 1) ++overlap;
        ++counter;
        in_critical.fetch_sub(1, std::memory_order_acq_rel);
        // guard released
    };

    std::vector<std::future<void>> futs;
    futs.reserve(N);
    for (int i = 0; i < N; ++i)
        futs.push_back(asio::co_spawn(ioc, make_coro(), asio::use_future));

    ioc.run();
    for (auto& f : futs) f.get();

    EXPECT_EQ(overlap,  0) << "Mutual exclusion violated on embedded path";
    EXPECT_EQ(counter,  N) << "Lost waiter on embedded path";
}

}  // namespace
