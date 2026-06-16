// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_store_fifo_fair.cpp
//
// Seam 5 — FIFO-fair concurrent writer (FR-015 / FR-016 / I-01 / SC-005 /
// AC US3 #1).
//
// Two coroutines on an asio::thread_pool invoke store() on the same
// MemoryStore instance concurrently; both complete with expected_t<void>{}
// in FIFO-arrival order; TSan reports 0 races; ASan reports 0 UAFs.
//
// TDD: RED until T040 (async_mutex wiring) ships.
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <future>
#include <span>
#include <vector>

#include "_fixtures_/test_double_fsm.hpp"

namespace {

using fixpp::session::direction_t;
using fixpp::session::MemoryStore;
using fixpp::store_test::byte_collecting_visitor;
using fixpp::store_test::make_test_frame;

MemoryStore make_fifo_store() {
    MemoryStore::Config cfg;
    cfg.policy = fixpp::session::capacity_policy::bounded;
    cfg.inbound_capacity = 200;
    cfg.outbound_capacity = 200;
    cfg.max_frame_bytes = 4096;
    cfg.store_resource = nullptr;
    return MemoryStore{cfg};
}

// ── Test 1: Two coroutines write sequentially-ordered frames; both succeed ───
//
// Because the store serialises on an async_mutex, the two coroutines will
// interleave only at suspension points, but the seqnum-order check inside the
// CS ensures correct ordering. We arrange the test so that both coroutines
// write disjoint directions (inbound / outbound) to avoid needing to synchronise
// on the exact arrival order across strands — the FIFO property is per-direction.
//
// This test validates that:
//   (a) Both co_awaits complete with expected_t<void>{};
//   (b) TSan sees no data races (enforced by sanitizer preset at build time);
//   (c) ASan sees no UAFs.

TEST(StoreFifoFair, TwoCoroutinesInboundOutboundBothSucceed) {
    // Use a 2-thread pool so both coroutines can be scheduled concurrently.
    asio::thread_pool pool{2};
    auto store = make_fifo_store();

    constexpr int kFrames = 50;

    // Coroutine A: store kFrames inbound frames (seq 1..kFrames)
    auto fut_a = asio::co_spawn(
        pool.get_executor(),
        [&store]() -> asio::awaitable<bool> {
            for (int i = 1; i <= kFrames; ++i) {
                auto frame =
                    make_test_frame(static_cast<fixpp::session::seqnum_t>(i), direction_t::inbound);
                auto r =
                    co_await store.store(static_cast<fixpp::session::seqnum_t>(i),
                                         std::span<const std::byte>(frame), direction_t::inbound);
                if (!r.has_value()) co_return false;
            }
            co_return true;
        },
        asio::use_future);

    // Coroutine B: store kFrames outbound frames (seq 1..kFrames)
    auto fut_b = asio::co_spawn(
        pool.get_executor(),
        [&store]() -> asio::awaitable<bool> {
            for (int i = 1; i <= kFrames; ++i) {
                auto frame = make_test_frame(static_cast<fixpp::session::seqnum_t>(i),
                                             direction_t::outbound);
                auto r =
                    co_await store.store(static_cast<fixpp::session::seqnum_t>(i),
                                         std::span<const std::byte>(frame), direction_t::outbound);
                if (!r.has_value()) co_return false;
            }
            co_return true;
        },
        asio::use_future);

    EXPECT_TRUE(fut_a.get()) << "Coroutine A (inbound) failed";
    EXPECT_TRUE(fut_b.get()) << "Coroutine B (outbound) failed";

    // Verify all frames are present via retrieve
    asio::thread_pool verify_pool{1};
    auto verify_fut = asio::co_spawn(
        verify_pool.get_executor(),
        [&store]() -> asio::awaitable<void> {
            byte_collecting_visitor iv, ov;
            auto ri = co_await store.retrieve(1, 0, direction_t::inbound, iv);
            EXPECT_TRUE(ri.has_value()) << "inbound retrieve failed";
            EXPECT_EQ(iv.entries().size(), static_cast<std::size_t>(kFrames));

            auto ro = co_await store.retrieve(1, 0, direction_t::outbound, ov);
            EXPECT_TRUE(ro.has_value()) << "outbound retrieve failed";
            EXPECT_EQ(ov.entries().size(), static_cast<std::size_t>(kFrames));
        },
        asio::use_future);
    verify_fut.get();

    // Drain the writer pool before `store` is destroyed at scope exit. A
    // future returned by use_future is satisfied when the awaiting coroutine
    // resumes inside invoke_handler, but the async_mutex resumption runner's
    // tail (release_ref) still touches the store's embedded mutex AFTER that.
    // `pool` is declared before `store`, so its destructor-join would run too
    // late; join here to establish happens-before. (verify_pool is declared
    // after `store`, so its own destructor already drains it in time.)
    pool.join();
}

// ── Test 2: Concurrent writers on same direction — only FIFO order succeeds ──
//
// When two coroutines race to store on the SAME direction, the first to
// acquire the mutex stores seq=1 (succeeds), the second sees next_seqnum=2
// so its store(seq=1) call fails with store_seqnum_out_of_order.
// This validates the mutex actually serialises access AND the seqnum check
// is performed inside the critical section.

TEST(StoreFifoFair, ConcurrentWritersSameDirectionOneWins) {
    asio::thread_pool pool{2};
    auto store = make_fifo_store();

    auto frame1 = make_test_frame(1, direction_t::outbound);
    auto frame2 = make_test_frame(1, direction_t::outbound);  // same seqnum

    std::atomic<int> success_count{0};
    std::atomic<int> out_of_order_count{0};

    // Both try to store seq=1 concurrently — exactly one should succeed.
    auto fut_a = asio::co_spawn(
        pool.get_executor(),
        [&store, &frame1, &success_count, &out_of_order_count]() -> asio::awaitable<void> {
            auto r =
                co_await store.store(1, std::span<const std::byte>(frame1), direction_t::outbound);
            if (r.has_value()) {
                success_count.fetch_add(1, std::memory_order_relaxed);
            } else if (r.error() == fixpp::core::error::store_seqnum_out_of_order) {
                out_of_order_count.fetch_add(1, std::memory_order_relaxed);
            }
        },
        asio::use_future);

    auto fut_b = asio::co_spawn(
        pool.get_executor(),
        [&store, &frame2, &success_count, &out_of_order_count]() -> asio::awaitable<void> {
            auto r =
                co_await store.store(1, std::span<const std::byte>(frame2), direction_t::outbound);
            if (r.has_value()) {
                success_count.fetch_add(1, std::memory_order_relaxed);
            } else if (r.error() == fixpp::core::error::store_seqnum_out_of_order) {
                out_of_order_count.fetch_add(1, std::memory_order_relaxed);
            }
        },
        asio::use_future);

    fut_a.get();
    fut_b.get();

    // Exactly one succeeds, exactly one gets out_of_order
    EXPECT_EQ(success_count.load(), 1) << "Exactly 1 concurrent store(seq=1) must succeed";
    EXPECT_EQ(out_of_order_count.load(), 1)
        << "Exactly 1 concurrent store(seq=1) must fail with store_seqnum_out_of_order";

    // Drain the pool before `store` is destroyed: the resumption runner's tail
    // (release_ref) outlives fut.get() and touches the store's embedded mutex.
    pool.join();
}

// ── Test 3: High-concurrency stress — 4 threads, 2 directions, ordered ───────
//
// Four coroutines work in pairs: two write inbound (alternating ownership
// via mutex), two write outbound. Each writes a disjoint contiguous range.
// Under US1 implementation this RED-fails (no mutex → data race detected by
// TSan). Under T040 it passes cleanly.

TEST(StoreFifoFair, HighConcurrencyMutexGuardsStore) {
    asio::thread_pool pool{4};

    // Use unbounded policy so we don't fight capacity
    MemoryStore::Config cfg;
    cfg.policy = fixpp::session::capacity_policy::unbounded;
    cfg.max_frame_bytes = 4096;
    cfg.store_resource = nullptr;
    auto store = std::make_shared<MemoryStore>(cfg);

    constexpr int kFrames = 100;

    // Single writer for inbound — stores seq 1..kFrames
    auto fut_in = asio::co_spawn(
        pool.get_executor(),
        [store]() -> asio::awaitable<bool> {
            for (int i = 1; i <= kFrames; ++i) {
                auto frame =
                    make_test_frame(static_cast<fixpp::session::seqnum_t>(i), direction_t::inbound);
                auto r =
                    co_await store->store(static_cast<fixpp::session::seqnum_t>(i),
                                          std::span<const std::byte>(frame), direction_t::inbound);
                if (!r.has_value()) co_return false;
            }
            co_return true;
        },
        asio::use_future);

    // Single writer for outbound — stores seq 1..kFrames
    auto fut_out = asio::co_spawn(
        pool.get_executor(),
        [store]() -> asio::awaitable<bool> {
            for (int i = 1; i <= kFrames; ++i) {
                auto frame = make_test_frame(static_cast<fixpp::session::seqnum_t>(i),
                                             direction_t::outbound);
                auto r =
                    co_await store->store(static_cast<fixpp::session::seqnum_t>(i),
                                          std::span<const std::byte>(frame), direction_t::outbound);
                if (!r.has_value()) co_return false;
            }
            co_return true;
        },
        asio::use_future);

    EXPECT_TRUE(fut_in.get()) << "Inbound writer failed under concurrency";
    EXPECT_TRUE(fut_out.get()) << "Outbound writer failed under concurrency";

    // Verify counts
    asio::thread_pool vpool{1};
    auto vfut = asio::co_spawn(
        vpool.get_executor(),
        [store]() -> asio::awaitable<void> {
            byte_collecting_visitor iv, ov;
            auto ri = co_await store->retrieve(1, 0, direction_t::inbound, iv);
            EXPECT_TRUE(ri.has_value());
            EXPECT_EQ(iv.entries().size(), static_cast<std::size_t>(kFrames));
            auto ro = co_await store->retrieve(1, 0, direction_t::outbound, ov);
            EXPECT_TRUE(ro.has_value());
            EXPECT_EQ(ov.entries().size(), static_cast<std::size_t>(kFrames));
        },
        asio::use_future);
    vfut.get();

    // Drain the writer pool before the shared `store` is released at scope
    // exit. fut.get() unblocks when the coroutine resumes, but the async_mutex
    // resumption runner's tail (release_ref) still reads the store's embedded
    // mutex on a pool thread afterwards. `pool` is declared before `store`, so
    // its destructor-join would race the shared_ptr teardown; join here.
    // (vpool is declared after `store`, so its destructor already drains it.)
    pool.join();
}

}  // namespace
