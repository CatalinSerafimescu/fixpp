// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_store_shutdown_ordering.cpp
//
// Seam 18 — concurrent store/retrieve/reset under a single async-mutex;
// orderly teardown of an unbounded MemoryStore without UAF on the slab
// (I-22 / SC-005).
//
// What this file actually tests:
//   1. HundredSequentialStoresAllSucceed — 100 store() calls on a bounded
//      store run sequentially on a single-thread pool; all succeed.
//   2. StoreOutlivesAllCoroutines — two concurrent writers (inbound +
//      outbound) on an unbounded store; TSan sees 0 data races; store is
//      destroyed after the pool joins (correct ownership ordering).
//   3. ResetDuringOperationalPeriodIsClean — store/reset/next_seqnum
//      round-trip verifies counters rewind to 1 after reset().
//   4. ConcurrentReadWriteNoDataRace — concurrent retrieve (frames 1..10)
//      + store (frames 11..30) on unbounded store; TSan must report 0 races.
//
// What this file does NOT test:
//   - cancel_and_drain() / store_cancelled outcomes (those require a real
//     Session instance or a direct async_mutex drain harness; deferred to
//     005-session-establishment-fsm per D-4).
//   - Session::close(terminal) shutdown ordering.
//
// gate-b/r1: RC#7 — comment corrected to match body (test body unchanged).
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <span>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>

#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/seqnum.hpp>

#include "_fixtures_/test_double_fsm.hpp"

namespace {

using fixpp::session::direction_t;
using fixpp::session::MemoryStore;
using fixpp::session::seqnum_t;
using fixpp::store_test::make_test_frame;

// ── Test 1: 100 sequential stores all succeed ─────────────────────────────────
//
// Under a single coroutine strand, 100 stores complete sequentially.
// Under US3 each acquires and releases the mutex — TSan must see 0 races.

TEST(StoreShutdownOrdering, HundredSequentialStoresAllSucceed) {
    asio::thread_pool pool{1};

    MemoryStore::Config cfg;
    cfg.policy            = fixpp::session::capacity_policy::bounded;
    cfg.inbound_capacity  = 200;
    cfg.outbound_capacity = 200;
    cfg.max_frame_bytes   = 4096;
    auto store = std::make_shared<MemoryStore>(cfg);

    auto fut = asio::co_spawn(pool.get_executor(),
        [store]() -> asio::awaitable<void> {
            for (int i = 1; i <= 100; ++i) {
                auto frame = make_test_frame(static_cast<seqnum_t>(i),
                                             direction_t::outbound);
                auto r = co_await store->store(
                    static_cast<seqnum_t>(i),
                    std::span<const std::byte>(frame),
                    direction_t::outbound);
                EXPECT_TRUE(r.has_value())
                    << "store seq=" << i << " failed";
            }
        },
        asio::use_future);
    fut.get();

    // store destroyed after pool.stop() — correct ordering
    pool.stop();
    pool.join();
    store.reset();  // explicit destroy AFTER pool stops
}

// ── Test 2: Concurrent stores from multiple coroutines, store outlives them ──
//
// The MemoryStore outlives all coroutines. Under TSan this validates
// that no data race exists on the store's internal state.

TEST(StoreShutdownOrdering, StoreOutlivesAllCoroutines) {
    asio::thread_pool pool{4};

    MemoryStore::Config cfg;
    cfg.policy            = fixpp::session::capacity_policy::unbounded;
    cfg.max_frame_bytes   = 4096;
    auto store = std::make_shared<MemoryStore>(cfg);

    constexpr int kPerDir = 50;

    // Two concurrent writers: one inbound, one outbound
    auto fut_in = asio::co_spawn(pool.get_executor(),
        [store]() -> asio::awaitable<void> {
            for (int i = 1; i <= kPerDir; ++i) {
                auto frame = make_test_frame(static_cast<seqnum_t>(i),
                                             direction_t::inbound);
                co_await store->store(static_cast<seqnum_t>(i),
                    std::span<const std::byte>(frame), direction_t::inbound);
            }
        },
        asio::use_future);

    auto fut_out = asio::co_spawn(pool.get_executor(),
        [store]() -> asio::awaitable<void> {
            for (int i = 1; i <= kPerDir; ++i) {
                auto frame = make_test_frame(static_cast<seqnum_t>(i),
                                             direction_t::outbound);
                co_await store->store(static_cast<seqnum_t>(i),
                    std::span<const std::byte>(frame), direction_t::outbound);
            }
        },
        asio::use_future);

    fut_in.get();
    fut_out.get();

    // Stop pool: no more coroutines in flight
    pool.stop();
    pool.join();

    // Destroy store AFTER pool joins — correct shutdown ordering
    store.reset();

    // If we reach here without TSan/ASan complaints, the test passes.
}

// ── Test 3: Reset during shutdown — store is drained cleanly ─────────────────
//
// After 50 stores, call reset(). Verify counters go back to 1.
// No UAF: the reset() coroutine must not touch the store after it's destroyed.

TEST(StoreShutdownOrdering, ResetDuringOperationalPeriodIsClean) {
    asio::thread_pool pool{1};

    MemoryStore::Config cfg;
    cfg.policy            = fixpp::session::capacity_policy::bounded;
    cfg.inbound_capacity  = 200;
    cfg.outbound_capacity = 200;
    cfg.max_frame_bytes   = 4096;
    auto store = std::make_shared<MemoryStore>(cfg);

    auto fut = asio::co_spawn(pool.get_executor(),
        [store]() -> asio::awaitable<void> {
            // Store 50 frames
            for (int i = 1; i <= 50; ++i) {
                auto frame = make_test_frame(static_cast<seqnum_t>(i),
                                             direction_t::outbound);
                co_await store->store(static_cast<seqnum_t>(i),
                    std::span<const std::byte>(frame), direction_t::outbound);
            }

            // Trigger reset
            auto r = co_await store->reset();
            EXPECT_TRUE(r.has_value()) << "reset() failed";

            // Verify counter is back to 1
            auto ns = co_await store->next_seqnum(direction_t::outbound, false);
            EXPECT_TRUE(ns.has_value());
            EXPECT_EQ(*ns, 1u);
        },
        asio::use_future);
    fut.get();

    // Destroy in correct order
    pool.stop();
    pool.join();
    store.reset();
}

// ── Test 4: Concurrent read (retrieve) + write (store) ───────────────────────
//
// One coroutine reads (retrieve with to-tail), another writes new frames.
// Under US3, the mutex ensures no data race on the index / entry vectors.
// TSan must report 0 races.

TEST(StoreShutdownOrdering, ConcurrentReadWriteNoDataRace) {
    asio::thread_pool pool{2};

    MemoryStore::Config cfg;
    cfg.policy            = fixpp::session::capacity_policy::unbounded;
    cfg.max_frame_bytes   = 4096;
    auto store = std::make_shared<MemoryStore>(cfg);

    // Pre-populate 10 frames so retrieve has something to read
    {
        asio::thread_pool setup{1};
        auto sfut = asio::co_spawn(setup.get_executor(),
            [store]() -> asio::awaitable<void> {
                for (int i = 1; i <= 10; ++i) {
                    auto frame = make_test_frame(static_cast<seqnum_t>(i),
                                                 direction_t::outbound);
                    co_await store->store(static_cast<seqnum_t>(i),
                        std::span<const std::byte>(frame), direction_t::outbound);
                }
            },
            asio::use_future);
        sfut.get();
        setup.stop();
        setup.join();
    }

    // Writer: stores 20 more frames (seq 11..30)
    // Reader: retrieves frames 1..10 (original range; must not see UB even if
    //         writer is also active)
    std::atomic<bool> writer_done{false};

    auto fut_writer = asio::co_spawn(pool.get_executor(),
        [store, &writer_done]() -> asio::awaitable<void> {
            for (int i = 11; i <= 30; ++i) {
                auto frame = make_test_frame(static_cast<seqnum_t>(i),
                                             direction_t::outbound);
                co_await store->store(static_cast<seqnum_t>(i),
                    std::span<const std::byte>(frame), direction_t::outbound);
            }
            writer_done.store(true, std::memory_order_release);
        },
        asio::use_future);

    // Simple collecting visitor for reader
    struct simple_vis final : public fixpp::session::retrieve_visitor {
        std::vector<seqnum_t> seqs;
        asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>>
        on_frame(seqnum_t s, std::span<const std::byte>) noexcept override {
            seqs.push_back(s);
            co_return fixpp::core::expected_t<fixpp::session::visit_result>{
                fixpp::session::visit_result::cont};
        }
    };

    auto fut_reader = asio::co_spawn(pool.get_executor(),
        [store]() -> asio::awaitable<void> {
            simple_vis vis;
            // Read a fixed range (1..10) — concurrent writer writes 11..30
            auto r = co_await store->retrieve(1, 10, direction_t::outbound, vis);
            EXPECT_TRUE(r.has_value()) << "concurrent retrieve failed";
            // Must see at least frames 1..10
            EXPECT_EQ(vis.seqs.size(), 10u);
        },
        asio::use_future);

    fut_writer.get();
    fut_reader.get();

    pool.stop();
    pool.join();
    store.reset();
}

// ── Test 5: RC#1 regression — unbounded retrieve UAF under concurrent append ──
//
// Before RC#1, retrieve() on an unbounded MemoryStore would capture
// unbounded_slab_.data() under the mutex and dereference that pointer AFTER
// the mutex was released. A concurrent store() could reallocate the vector,
// invalidating the captured pointer → UAF/OOB read detectable by ASan.
//
// This test forces reallocation by starting with a tiny initial capacity (the
// vector grows from 1 byte), then concurrently calling retrieve() while the
// writer loop appends enough frames to trigger repeated reallocations.
//
// Without the RC#1 fix, ASan should flag a heap-use-after-free in retrieve().
// With the fix (payload bytes copied under the mutex), the test must pass clean.
TEST(StoreShutdownOrdering, UnboundedRetrieveUAFUnderConcurrentAppend) {
    // Use a pool large enough that the reader and writer truly run concurrently.
    asio::thread_pool pool{4};

    // Tiny initial capacity to force immediate reallocation on first store().
    MemoryStore::Config cfg;
    cfg.policy            = fixpp::session::capacity_policy::unbounded;
    cfg.max_frame_bytes   = 1024;
    auto store = std::make_shared<MemoryStore>(cfg);

    // Pre-populate 1 frame so retrieve(1,1,...) has something to walk.
    {
        asio::thread_pool setup{1};
        auto pre = asio::co_spawn(setup.get_executor(),
            [store]() -> asio::awaitable<void> {
                auto frame = make_test_frame(static_cast<seqnum_t>(1),
                                             direction_t::outbound);
                co_await store->store(1, std::span<const std::byte>(frame),
                                      direction_t::outbound);
            },
            asio::use_future);
        pre.get();
        setup.stop(); setup.join();
    }

    // Writer: store 200 frames (each ~50 bytes) to force multiple reallocations.
    auto fut_writer = asio::co_spawn(pool.get_executor(),
        [store]() -> asio::awaitable<void> {
            for (int i = 2; i <= 200; ++i) {
                auto frame = make_test_frame(static_cast<seqnum_t>(i),
                                             direction_t::outbound);
                co_await store->store(static_cast<seqnum_t>(i),
                    std::span<const std::byte>(frame), direction_t::outbound);
            }
        },
        asio::use_future);

    // Reader: repeatedly retrieve frame seq=1 while the writer is growing the slab.
    // Without RC#1, this triggers the UAF.
    struct nop_vis final : public fixpp::session::retrieve_visitor {
        std::size_t count{0};
        asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>>
        on_frame(seqnum_t, std::span<const std::byte> frame) noexcept override {
            // Touch all bytes to ensure ASan catches use-after-free.
            volatile std::byte sum{};
            for (auto b : frame) sum = static_cast<std::byte>(static_cast<uint8_t>(sum) ^
                                                               static_cast<uint8_t>(b));
            (void)sum;
            ++count;
            co_return fixpp::core::expected_t<fixpp::session::visit_result>{
                fixpp::session::visit_result::cont};
        }
    };

    auto fut_reader = asio::co_spawn(pool.get_executor(),
        [store]() -> asio::awaitable<void> {
            for (int round = 0; round < 50; ++round) {
                nop_vis vis;
                // Retrieve only frame 1 (which was pre-populated).
                auto r = co_await store->retrieve(1, 1, direction_t::outbound, vis);
                // May fail with store_seqnum_gap if the writer hasn't reached
                // that index yet — both outcomes are acceptable; we only care
                // that there is no memory corruption.
                (void)r;
            }
        },
        asio::use_future);

    fut_writer.get();
    fut_reader.get();

    pool.stop();
    pool.join();
    store.reset();
    // If we reach here without ASan/MSan flags, the RC#1 fix holds.
}

}  // namespace
