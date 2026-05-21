// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_store_seqnum_out_of_order.cpp
//
// Seam 20 — seqnum out-of-order rejection under writer-mutex (FR-018 / I-05 /
// AC US3 #5).
//
// store(seq=5, _, outbound) while next_seqnum(outbound, false) == 1:
//   - Verification fails INSIDE the writer-mutex critical section.
//   - Awaitable returns store_seqnum_out_of_order.
//   - Entry-array unchanged.
//   - Mutex acquired-then-released cleanly (no deadlock; subsequent stores work).
//
// TDD: Under US1 the seqnum check exists but is NOT inside a mutex CS.
// The test validates correct behavior (which US1 also has); under US3 it
// additionally validates the mutex-guarded check path via TSan (concurrent
// caller sees clean release).
#include <gtest/gtest.h>

#include <span>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>

#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>

#include "_fixtures_/test_double_fsm.hpp"

namespace {

using fixpp::session::direction_t;
using fixpp::session::MemoryStore;
using fixpp::session::seqnum_max;
using fixpp::session::seqnum_t;
using fixpp::store_test::byte_collecting_visitor;
using fixpp::store_test::make_test_frame;
using fixpp::store_test::run_on_pool;

MemoryStore make_store() {
    MemoryStore::Config cfg;
    cfg.policy            = fixpp::session::capacity_policy::bounded;
    cfg.inbound_capacity  = 100;
    cfg.outbound_capacity = 100;
    cfg.max_frame_bytes   = 4096;
    cfg.store_resource    = nullptr;
    return MemoryStore{cfg};
}

// ── Test 1: store(seq=5) when next_seqnum==1 → store_seqnum_out_of_order ─────

TEST(StoreSeqnumOutOfOrder, SkipAheadRejected) {
    asio::thread_pool pool{1};
    auto store = make_store();

    auto fut = asio::co_spawn(pool.get_executor(),
        [&store]() -> asio::awaitable<void> {
            // Verify initial state: next_seqnum outbound = 1
            auto ns = co_await store.next_seqnum(direction_t::outbound, false);
            EXPECT_TRUE(ns.has_value());
            EXPECT_EQ(*ns, 1u) << "fresh store must have next_seqnum == 1";

            // Attempt to store seq=5 while next is 1 → should fail
            auto frame5 = make_test_frame(5, direction_t::outbound);
            auto r = co_await store.store(5,
                std::span<const std::byte>(frame5), direction_t::outbound);
            EXPECT_FALSE(r.has_value())
                << "store(seq=5) when next==1 must fail";
            EXPECT_EQ(r.error(), fixpp::core::error::store_seqnum_out_of_order)
                << "error must be store_seqnum_out_of_order";

            // Verify entry-array unchanged: next_seqnum still 1
            auto ns2 = co_await store.next_seqnum(direction_t::outbound, false);
            EXPECT_TRUE(ns2.has_value());
            EXPECT_EQ(*ns2, 1u)
                << "next_seqnum must remain 1 after rejected out-of-order store";

            // Verify no frames were stored
            byte_collecting_visitor vis;
            auto rr = co_await store.retrieve(1, 0, direction_t::outbound, vis);
            EXPECT_TRUE(rr.has_value());
            EXPECT_EQ(vis.entries().size(), 0u)
                << "no frames must be stored after rejected out-of-order store";
        },
        asio::use_future);
    fut.get();
}

// ── Test 2: seq=0 is also rejected ──────────────────────────────────────────

TEST(StoreSeqnumOutOfOrder, ZeroSeqnumRejected) {
    asio::thread_pool pool{1};
    auto store = make_store();

    auto fut = asio::co_spawn(pool.get_executor(),
        [&store]() -> asio::awaitable<void> {
            auto frame0 = make_test_frame(0, direction_t::inbound);
            auto r = co_await store.store(0,
                std::span<const std::byte>(frame0), direction_t::inbound);
            EXPECT_FALSE(r.has_value());
            // seq=0 < next=1 → store_seqnum_out_of_order
            EXPECT_EQ(r.error(), fixpp::core::error::store_seqnum_out_of_order);
        },
        asio::use_future);
    fut.get();
}

// ── Test 3: Mutex released cleanly — subsequent correct store succeeds ────────

TEST(StoreSeqnumOutOfOrder, MutexReleasedAfterRejection_NextStoreSucceeds) {
    asio::thread_pool pool{1};
    auto store = make_store();

    auto fut = asio::co_spawn(pool.get_executor(),
        [&store]() -> asio::awaitable<void> {
            // Rejected store
            auto frame5 = make_test_frame(5, direction_t::outbound);
            auto r = co_await store.store(5,
                std::span<const std::byte>(frame5), direction_t::outbound);
            EXPECT_FALSE(r.has_value());
            EXPECT_EQ(r.error(), fixpp::core::error::store_seqnum_out_of_order);

            // Correct store must now succeed (mutex is released cleanly)
            auto frame1 = make_test_frame(1, direction_t::outbound);
            auto r2 = co_await store.store(1,
                std::span<const std::byte>(frame1), direction_t::outbound);
            EXPECT_TRUE(r2.has_value())
                << "store(seq=1) after rejected out-of-order must succeed";

            // next_seqnum must advance
            auto ns = co_await store.next_seqnum(direction_t::outbound, false);
            EXPECT_TRUE(ns.has_value());
            EXPECT_EQ(*ns, 2u)
                << "next_seqnum must be 2 after storing seq=1";
        },
        asio::use_future);
    fut.get();
}

// ── Test 4: Both directions checked independently ─────────────────────────────

TEST(StoreSeqnumOutOfOrder, OutOfOrderInboundDoesNotAffectOutbound) {
    asio::thread_pool pool{1};
    auto store = make_store();

    auto fut = asio::co_spawn(pool.get_executor(),
        [&store]() -> asio::awaitable<void> {
            // Store seq=1 outbound (succeeds)
            auto frame_o1 = make_test_frame(1, direction_t::outbound);
            auto r1 = co_await store.store(1,
                std::span<const std::byte>(frame_o1), direction_t::outbound);
            EXPECT_TRUE(r1.has_value());

            // Store seq=5 inbound (rejected — next inbound is 1)
            auto frame_i5 = make_test_frame(5, direction_t::inbound);
            auto r2 = co_await store.store(5,
                std::span<const std::byte>(frame_i5), direction_t::inbound);
            EXPECT_FALSE(r2.has_value());
            EXPECT_EQ(r2.error(), fixpp::core::error::store_seqnum_out_of_order);

            // Outbound counter is still at 2 (1 was stored)
            auto ns_out = co_await store.next_seqnum(direction_t::outbound, false);
            EXPECT_EQ(*ns_out, 2u);

            // Inbound counter is still at 1 (nothing was stored)
            auto ns_in = co_await store.next_seqnum(direction_t::inbound, false);
            EXPECT_EQ(*ns_in, 1u);
        },
        asio::use_future);
    fut.get();
}

// ── Test 5: Concurrent out-of-order and valid store — mutex serialises ────────
//
// Under US3: the out-of-order store(seq=5) holds the mutex briefly, checks
// seqnum, fails, releases. The valid store(seq=1) then acquires the mutex.
// TSan must see 0 races. Under US1 (no mutex) this is a benign race on the
// counter reads but the outcome is correct (one succeeds, one fails).

TEST(StoreSeqnumOutOfOrder, ConcurrentOutOfOrderAndValidStore) {
    asio::thread_pool pool{2};

    MemoryStore::Config cfg;
    cfg.policy            = fixpp::session::capacity_policy::bounded;
    cfg.inbound_capacity  = 100;
    cfg.outbound_capacity = 100;
    cfg.max_frame_bytes   = 4096;
    auto store = std::make_shared<MemoryStore>(cfg);

    auto frame1 = make_test_frame(1, direction_t::outbound);
    auto frame5 = make_test_frame(5, direction_t::outbound);

    std::atomic<bool> got_success{false};
    std::atomic<bool> got_out_of_order{false};

    auto fut_valid = asio::co_spawn(pool.get_executor(),
        [store, frame1, &got_success]() mutable -> asio::awaitable<void> {
            auto r = co_await store->store(1,
                std::span<const std::byte>(frame1), direction_t::outbound);
            if (r.has_value()) {
                got_success.store(true, std::memory_order_relaxed);
            }
        },
        asio::use_future);

    auto fut_invalid = asio::co_spawn(pool.get_executor(),
        [store, frame5, &got_out_of_order]() mutable -> asio::awaitable<void> {
            auto r = co_await store->store(5,
                std::span<const std::byte>(frame5), direction_t::outbound);
            if (!r.has_value() &&
                r.error() == fixpp::core::error::store_seqnum_out_of_order) {
                got_out_of_order.store(true, std::memory_order_relaxed);
            }
        },
        asio::use_future);

    fut_valid.get();
    fut_invalid.get();

    // Phase 7 TSan fix: reset the shared_ptr before pool.join() so the store's
    // destructor (and async_mutex drain) complete on the pool threads before the
    // pool is destroyed. Without this, the main thread's ~shared_ptr races with
    // drain_latch_state accessed by the last pool worker completing its handler.
    store.reset();
    pool.join();

    EXPECT_TRUE(got_success.load())
        << "Valid store(seq=1) must succeed";
    EXPECT_TRUE(got_out_of_order.load())
        << "Invalid store(seq=5) must return store_seqnum_out_of_order";
}

// ── Test 6: next_seqnum_overflow → store_seqnum_overflow (T040 overflow check) ─
//
// FR-022 / I-18: when the outbound counter is at seqnum_max,
// next_seqnum(outbound, true) must return store_seqnum_overflow WITHOUT
// advancing the counter.
//
// Determinism: uses FIXPP_TEST_HOOKS to force the counter to seqnum_max
// without running 4 billion stores. The hook is only compiled in for this
// target (see CMakeLists.txt target_compile_definitions).

TEST(StoreSeqnumOutOfOrder, NextSeqnumAtMaxReturnsOverflow) {
    asio::thread_pool pool{1};

    MemoryStore::Config cfg;
    cfg.policy          = fixpp::session::capacity_policy::unbounded;
    cfg.max_frame_bytes = 4096;
    cfg.store_resource  = nullptr;
    auto store = MemoryStore{cfg};

    // Force counter to seqnum_max via test hook.
    store.test_set_counter(direction_t::outbound, seqnum_max);

    run_on_pool(pool, [&store]() -> asio::awaitable<void> {
        // next_seqnum(_, true) at seqnum_max → overflow, counter unchanged.
        auto r = co_await store.next_seqnum(direction_t::outbound, /*increment=*/true);
        EXPECT_FALSE(r.has_value()) << "next_seqnum(true) at seqnum_max must overflow";
        EXPECT_EQ(r.error(), fixpp::core::error::store_seqnum_overflow)
            << "error must be store_seqnum_overflow";

        // Counter must NOT have advanced after overflow.
        auto r2 = co_await store.next_seqnum(direction_t::outbound, /*increment=*/false);
        EXPECT_TRUE(r2.has_value()) << "non-incrementing query must succeed";
        EXPECT_EQ(*r2, seqnum_max)
            << "counter must remain at seqnum_max after overflow (no wrap)";
    });
}

}  // namespace
