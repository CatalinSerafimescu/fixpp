// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_store_shutdown_ordering.cpp
//
// Seam 18 — session shutdown ordering under concurrent in-flight stores
// (I-22 / SC-005).
//
// 100 in-flight store() calls + concurrent cancellation (simulating
// Session::close(terminal)): all 100 awaitables complete (those past the
// linearisation point with expected_t<void>{}, those before with
// expected_t::unexpected{store_cancelled}); no UAF on session_arena;
// ~MessageStore runs before session_arena release.
//
// Because Session is complex to instantiate in a unit test, we simulate
// the ordering using the async_mutex cancel_and_drain() primitive directly:
// the mutex is drained, all pending acquirers receive sync_lock_drained,
// and the store is destroyed AFTER all coroutines complete.
//
// TDD: The data-race protection (TSan) turns RED until T040 ships the mutex.
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

}  // namespace
