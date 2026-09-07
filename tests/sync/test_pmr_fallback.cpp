// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_pmr_fallback.cpp — Seam #10, T056
//
// PMR fallback: gtest-level contract tests for async_lock(&mr).
//
// (a) Contended async_lock(&mbr) with a generously-sized monotonic_buffer_resource
//     over a stack buffer + null upstream → all acquisitions succeed; all waiter
//     storage is drawn from mbr (no global heap).
//
// (b) Exhaustion: a deliberately TINY monotonic_buffer_resource (e.g. 32 bytes,
//     null upstream) → a contended async_lock(&tiny) that must enqueue →
//     returns unexpected{sync_lock_alloc_failed}, process does NOT terminate.
//
// The mallocnesia zero-global-alloc measurement is the SEPARATE alloc-guard
// binary (tests/alloc_guard/sync_alloc_guard_test.cpp), which adds the E-4
// warm-up phase and the alloc_guard_start/end markers.
//
// Oracle: [2f §9 #10] — "PMR fallback (seam #10)".
// [2f §4.3.4] case 3: mr != nullptr → pmr_waiter_block path.

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <cstddef>
#include <fixpp/core/sync/async_mutex.hpp>
#include <memory_resource>
#include <vector>

#include "support/pump_until_ready.hpp"
#include "sync/sync_test_support.hpp"

namespace {

using fixpp::core::error;
using fixpp::sync::async_lock_guard;
using fixpp::sync::async_mutex;
using fixpp::sync::expected_t;

using fixpp::sync::test::yield_n;

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: Generously-sized PMR buffer — N contended acquires succeed.
// Each waiter is forced to actually suspend (holder stays locked long enough).
// All waiter storage drawn from mbr (null upstream catches any PMR overflow).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SyncPmrFallback, ContendedAcquiresSucceedWithPmr) {
    // N=32 waiters + 1 holder; buffer sized to hold all pmr_waiter_blocks
    // comfortably. Each pmr_waiter_block = sizeof(memory_resource*) +
    // sizeof(waiter_record) ≤ 264 B; 32 * 300 = ~9600 B well within 32 kB.
    constexpr int N = 32;
    std::array<std::byte, 32768> buf{};

    std::atomic<int> in_critical{0};
    int overlap = 0;
    int counter = 0;

    asio::io_context ioc;
    async_mutex mtx;

    // Holder: grabs the lock and holds it long enough for all N waiters to enqueue.
    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        co_await yield_n(N * 4 + 8);
        // guard released
    };

    // Waiters: each uses the shared monotonic_buffer_resource.
    // Note: monotonic_buffer_resource is not thread-safe; since we run on a
    // single io_context thread all coroutines interleave on the same thread —
    // no concurrent allocations.
    auto make_waiter = [&](std::pmr::memory_resource* mr) -> asio::awaitable<void> {
        co_await yield_n(1);
        auto g = co_await mtx.async_lock(mr);
        if (!g.has_value()) {
            ADD_FAILURE() << "PMR waiter failed unexpectedly: " << static_cast<int>(g.error());
            co_return;
        }
        int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (v > 1) ++overlap;
        ++counter;
        in_critical.fetch_sub(1, std::memory_order_acq_rel);
        // guard released
    };

    auto fh = asio::co_spawn(ioc, holder_coro(), asio::use_future);

    // Build the PMR resource BEFORE spawning waiters (monotonic_buffer_resource
    // needs a valid backing buffer for the entire duration of io_context::run).
    std::pmr::monotonic_buffer_resource mbr{
        buf.data(), buf.size(),
        std::pmr::null_memory_resource()};  // null upstream: no silent fallback

    std::vector<std::future<void>> futs;
    futs.reserve(N);
    for (int i = 0; i < N; ++i)
        futs.push_back(asio::co_spawn(ioc, make_waiter(&mbr), asio::use_future));

    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, fh, "SyncPmrFallback::ContendedAcquiresSucceedWithPmr")) {
        return;
    }
    fh.get();
    for (auto& f : futs) f.get();

    EXPECT_EQ(overlap, 0) << "Mutual exclusion violated on PMR path";
    EXPECT_EQ(counter, N) << "Lost waiter on PMR path";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: Tiny PMR buffer (32 bytes, null upstream) — contended enqueue must
// fail with sync_lock_alloc_failed, not terminate.
//
// A pmr_waiter_block requires sizeof(memory_resource*) + sizeof(waiter_record)
// ≥ 256 B. A 32-byte buffer cannot satisfy this allocation → bad_alloc →
// trapped by async_lock → returns unexpected{sync_lock_alloc_failed}.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SyncPmrFallback, TinyPmrExhaustionReturnsAllocFailed) {
    // 32 bytes: definitely too small for one pmr_waiter_block.
    std::array<std::byte, 32> tiny_buf{};

    bool alloc_failed_received = false;

    asio::io_context ioc;
    async_mutex mtx;

    auto run = [&]() -> asio::awaitable<void> {
        // Step 1: hold the lock so the next async_lock MUST enqueue (contended path).
        auto holder = co_await mtx.async_lock();
        EXPECT_TRUE(holder.has_value());

        std::pmr::monotonic_buffer_resource tiny_mr{tiny_buf.data(), tiny_buf.size(),
                                                    std::pmr::null_memory_resource()};

        auto ex = co_await asio::this_coro::executor;
        std::atomic<bool> result_ready{false};
        std::atomic<bool> got_alloc_failed{false};

        // Spawn a waiter that uses the tiny resource — must fail to allocate.
        asio::co_spawn(
            ex,
            [&]() -> asio::awaitable<void> {
                co_await yield_n(1);
                auto r = co_await mtx.async_lock(&tiny_mr);
                if (!r.has_value() && r.error() == error::sync_lock_alloc_failed)
                    got_alloc_failed.store(true, std::memory_order_release);
                result_ready.store(true, std::memory_order_release);
            },
            asio::detached);

        // Yield until the waiter has run and set its result.
        co_await yield_n(8);

        // Release the holder — the waiter should already have returned
        // sync_lock_alloc_failed before even reaching the LIFO list push.
        holder = expected_t<async_lock_guard>{};

        // One more yield to let any posted callbacks complete.
        co_await yield_n(4);

        alloc_failed_received = got_alloc_failed.load(std::memory_order_acquire);

        // Drain the mutex so its destructor doesn't std::terminate.
        auto d = co_await mtx.cancel_and_drain();
        EXPECT_TRUE(d.has_value());
    };

    auto f = asio::co_spawn(ioc, run(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, f, "SyncPmrFallback::TinyPmrExhaustionReturnsAllocFailed")) {
        return;
    }
    f.get();

    EXPECT_TRUE(alloc_failed_received)
        << "Expected sync_lock_alloc_failed when PMR resource is exhausted; "
           "process must not terminate.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: PMR-fallback fast-path — uncontended acquire with a PMR resource
// still takes the fast-path CAS (no waiter_record allocated).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SyncPmrFallback, UncontendedPmrTakesFastPath) {
    std::array<std::byte, 4096> buf{};
    std::pmr::monotonic_buffer_resource mbr{buf.data(), buf.size(),
                                            std::pmr::null_memory_resource()};

    asio::io_context ioc;
    async_mutex mtx;

    auto run = [&]() -> asio::awaitable<void> {
        // Uncontended: fast-path CAS → no pmr_waiter_block allocated.
        auto g = co_await mtx.async_lock(&mbr);
        EXPECT_TRUE(g.has_value()) << "Uncontended PMR acquire must succeed";
        // guard released
    };

    auto f = asio::co_spawn(ioc, run(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, f, "SyncPmrFallback::UncontendedPmrTakesFastPath")) {
        return;
    }
    f.get();
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4 (058 T042(a)): unconditionally-throwing PMR resource — deterministic,
// layout-independent witness for the `mr->allocate(...)` `catch (std::bad_alloc
// const&)` arm (async_mutex.hpp, `pmr_waiter_block` allocation inside the
// contended enqueue path). Unlike TinyPmrExhaustionReturnsAllocFailed above
// (which relies on a 32-byte buffer being smaller than sizeof(waiter_record) —
// a magic number that silently stops discriminating if waiter_record's layout
// changes), this resource throws unconditionally regardless of the requested
// size, so it stays correct across any future waiter_record resize.
//
// Oracle: result == unexpected{sync_lock_alloc_failed} AND the mutex is left
// healthy — no waiter_record is ever constructed on this arm (the `catch`
// returns nullptr from the lambda BEFORE `::new (record) waiter_record{}` and
// before any `add_ref`, so there is nothing to leak or release), which we
// confirm by proving a subsequent fresh acquire on the SAME mutex still
// succeeds and the mutex destructs cleanly (no drain needed, no destructor
// trap fires).
// ─────────────────────────────────────────────────────────────────────────────

namespace {
struct throwing_resource final : std::pmr::memory_resource {
    void* do_allocate(std::size_t, std::size_t) override { throw std::bad_alloc{}; }
    void do_deallocate(void*, std::size_t, std::size_t) override {
        ADD_FAILURE() << "deallocate must never be called: allocate always throws";
    }
    bool do_is_equal(std::pmr::memory_resource const& other) const noexcept override {
        return this == &other;
    }
};
}  // namespace

TEST(SyncPmrFallback, AlwaysThrowingPmrReturnsAllocFailedAndLeavesMutexHealthy) {
    throwing_resource throwing_mr;

    bool alloc_failed_received = false;
    bool followup_acquire_succeeded = false;

    asio::io_context ioc;
    async_mutex mtx;

    auto run = [&]() -> asio::awaitable<void> {
        // Step 1: hold the lock so the next async_lock MUST enqueue (contended
        // path — the fast-path CAS never reaches mr->allocate() at all).
        auto holder = co_await mtx.async_lock();
        EXPECT_TRUE(holder.has_value());

        auto ex = co_await asio::this_coro::executor;
        std::atomic<bool> result_ready{false};
        std::atomic<bool> got_alloc_failed{false};

        asio::co_spawn(
            ex,
            [&]() -> asio::awaitable<void> {
                co_await yield_n(1);
                auto r = co_await mtx.async_lock(&throwing_mr);
                if (!r.has_value() && r.error() == error::sync_lock_alloc_failed)
                    got_alloc_failed.store(true, std::memory_order_release);
                result_ready.store(true, std::memory_order_release);
            },
            asio::detached);

        co_await yield_n(8);

        // Release the holder — the throwing-mr waiter must already have
        // returned sync_lock_alloc_failed before ever touching the LIFO list.
        holder = expected_t<async_lock_guard>{};

        co_await yield_n(4);

        alloc_failed_received = got_alloc_failed.load(std::memory_order_acquire);

        // Ref-balance / mutex-health check: a fresh, ordinary (non-PMR)
        // contended-free acquire on the SAME mutex must still succeed — the
        // failed throwing-mr attempt must not have corrupted state_,
        // active_holders_count_, or any waiter bookkeeping.
        auto g2 = co_await mtx.async_lock();
        followup_acquire_succeeded = g2.has_value();
        g2 = expected_t<async_lock_guard>{};

        // No waiters were ever queued on this mutex (the throwing-mr attempt
        // never reached the list push), so no drain is required for a clean
        // destructor — but drain anyway for symmetry with Test 2 and to prove
        // the destructor guard doesn't false-fire on this arm's residue.
        auto d = co_await mtx.cancel_and_drain();
        EXPECT_TRUE(d.has_value());
    };

    auto f = asio::co_spawn(ioc, run(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, f, "SyncPmrFallback::AlwaysThrowingPmrReturnsAllocFailedAndLeavesMutexHealthy")) {
        return;
    }
    f.get();

    EXPECT_TRUE(alloc_failed_received)
        << "Expected sync_lock_alloc_failed when the PMR resource unconditionally "
           "throws std::bad_alloc; process must not terminate.";
    EXPECT_TRUE(followup_acquire_succeeded)
        << "Mutex must remain healthy (ref-balance intact) after an allocation-"
           "failed contended attempt: a subsequent acquire must still succeed.";
}

}  // namespace
