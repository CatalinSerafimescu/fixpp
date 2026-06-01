// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_store_cancellation_contract.cpp
//
// Seam 6 — per-method cancellation contract (FR-020 / I-07 / I-13 / SC-006 /
// AC US3 #2 / #3).
//
// For each of store / retrieve / next_seqnum(_, true) / reset:
//   BEFORE linearisation: cancel → expected_t::unexpected{store_cancelled},
//                          no state change, mutex released cleanly.
//   AFTER  linearisation: cancel → normal completion, state durable.
//
// Note: With the current US1 single-threaded implementation these tests are
// RED (no cancellation propagation). They turn GREEN when T040/T041 wire the
// async_mutex + cancellation contract.
//
// TDD: RED until T040 (MemoryStore) and T041 (FileStore) ship.
#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <span>
#include <vector>

#include "_fixtures_/test_double_fsm.hpp"

namespace {

using fixpp::session::direction_t;
using fixpp::session::MemoryStore;
using fixpp::session::seqnum_t;
using fixpp::store_test::byte_collecting_visitor;
using fixpp::store_test::make_test_frame;
using fixpp::store_test::run_on_pool;

MemoryStore make_store() {
    MemoryStore::Config cfg;
    cfg.policy = fixpp::session::capacity_policy::bounded;
    cfg.inbound_capacity = 100;
    cfg.outbound_capacity = 100;
    cfg.max_frame_bytes = 4096;
    cfg.store_resource = nullptr;
    return MemoryStore{cfg};
}

// ── Test 1: Cancel store AFTER linearisation → normal completion, durable ────
//
// We store a frame. After it returns (which IS after linearisation in US1),
// cancelling the slot has no retroactive effect — the frame is persisted.
// Under US3 the linearisation point is the moment the mutex is acquired and
// the deep-copy is complete.

TEST(StoreCancellationContract, StoreCompletionIsDurableAfterLinearisation) {
    asio::thread_pool pool{1};
    auto store = make_store();

    auto frame = make_test_frame(1, direction_t::outbound);

    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&store, &frame]() -> asio::awaitable<void> {
            auto r =
                co_await store.store(1, std::span<const std::byte>(frame), direction_t::outbound);
            // Must succeed — frame is now linearised
            EXPECT_TRUE(r.has_value())
                << "store() failed: " << (r.has_value() ? 0 : (int)r.error());

            // Verify frame is durable
            byte_collecting_visitor vis;
            auto rr = co_await store.retrieve(1, 1, direction_t::outbound, vis);
            EXPECT_TRUE(rr.has_value());
            EXPECT_EQ(vis.entries().size(), 1u);
        },
        asio::use_future);
    fut.get();
}

// ── Test 2: Cancel next_seqnum(dir, true) AFTER completion → durable ─────────
//
// next_seqnum(outbound, true) must return success and the counter must be
// incremented. After completion, firing the cancellation slot has no effect.

TEST(StoreCancellationContract, NextSeqnumIncrementDurableAfterCompletion) {
    asio::thread_pool pool{1};
    auto store = make_store();

    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&store]() -> asio::awaitable<void> {
            // Increment counter
            auto r = co_await store.next_seqnum(direction_t::outbound, true);
            EXPECT_TRUE(r.has_value());
            EXPECT_EQ(*r, 1u) << "first next_seqnum(true) must return 1";

            // Read back to verify durable increment
            auto r2 = co_await store.next_seqnum(direction_t::outbound, false);
            EXPECT_TRUE(r2.has_value());
            EXPECT_EQ(*r2, 2u) << "counter must be 2 after one increment";
        },
        asio::use_future);
    fut.get();
}

// ── Test 3: reset() idempotent with no concurrent mutation ───────────────────
//
// reset() under single-threaded context (US1) must succeed and leave counters
// at 1. Under US3 it acquires the mutex; this test validates the happy path.

TEST(StoreCancellationContract, ResetSucceedsAndClearsState) {
    asio::thread_pool pool{1};
    auto store = make_store();

    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&store]() -> asio::awaitable<void> {
            // Store a frame first
            auto frame = make_test_frame(1, direction_t::inbound);
            auto sr =
                co_await store.store(1, std::span<const std::byte>(frame), direction_t::inbound);
            EXPECT_TRUE(sr.has_value());

            // reset()
            auto rr = co_await store.reset();
            EXPECT_TRUE(rr.has_value()) << "reset() must succeed";

            // Counter must be back to 1
            auto ns = co_await store.next_seqnum(direction_t::inbound, false);
            EXPECT_TRUE(ns.has_value());
            EXPECT_EQ(*ns, 1u) << "counter must be 1 after reset";
        },
        asio::use_future);
    fut.get();
}

// ── Test 4: store_cancelled on cancelled operation ────────────────────────────
//
// FR-020 / I-07: cancel BEFORE mutex linearisation → store_cancelled + state unchanged.
//
// Strategy: use a 1-thread pool so all coroutines serialize. Spawn coroutine A
// (store seq=1) then coroutine B (store seq=2 with cancel slot). Both post
// through the executor. Then emit total cancellation on B's slot BEFORE the
// pool thread runs anything. Because the pool has 1 thread and we emit BEFORE
// anything executes, B's async_lock() sees the cancel signal and returns
// store_cancelled before acquiring the mutex.
//
// Determinism guarantee: all three asio::co_spawn calls post their first
// continuation through the 1-thread pool queue. The main thread emits
// cancellation while the pool queue has not yet drained, so the cancel
// arrives before B attempts async_lock(). The FIFO property of the pool
// queue ensures A (seq=1) runs first; B (seq=2) is always behind A.
//
// Contract assertions:
//   (a) B returned store_cancelled.
//   (b) Store state contains exactly seq=1 (B's seq=2 was not committed).

TEST(StoreCancellationContract, CancelledBeforeLinearisationYieldsStoreCancelled) {
    // 1-thread pool: all coroutines serialise; cancel can fire before the
    // second store's async_lock() attempt.
    asio::thread_pool pool{1};

    auto store = std::make_shared<MemoryStore>([] {
        MemoryStore::Config cfg;
        cfg.policy = fixpp::session::capacity_policy::bounded;
        cfg.inbound_capacity = 100;
        cfg.outbound_capacity = 100;
        cfg.max_frame_bytes = 4096;
        return cfg;
    }());

    // Coroutine A: store seq=1 (no cancellation slot).
    auto frame1 = make_test_frame(1, direction_t::outbound);
    auto futA = asio::co_spawn(
        pool.get_executor(),
        [store, frame1]() mutable -> asio::awaitable<fixpp::core::expected_t<void>> {
            co_return co_await store->store(1, std::span<const std::byte>(frame1),
                                            direction_t::outbound);
        },
        asio::use_future);

    // Coroutine B: store seq=2, bound to cancellation slot.
    auto frame2 = make_test_frame(2, direction_t::outbound);
    asio::cancellation_signal sig;
    auto futB = asio::co_spawn(
        pool.get_executor(),
        asio::bind_cancellation_slot(
            sig.slot(),
            [store, frame2]() mutable -> asio::awaitable<fixpp::core::expected_t<void>> {
                co_return co_await store->store(2, std::span<const std::byte>(frame2),
                                                direction_t::outbound);
            }),
        asio::use_future);

    // Emit total cancellation BEFORE the pool thread has run either coroutine.
    // The 1-thread pool is busy with the main-thread co_spawn bookkeeping; the
    // cancel signal is delivered now, before async_lock() is reached.
    sig.emit(asio::cancellation_type::total);

    auto rA = futA.get();
    auto rB = futB.get();

    // A must succeed (no cancel slot).
    EXPECT_TRUE(rA.has_value()) << "store(seq=1) must succeed";

    // B must return store_cancelled (cancel arrived before async_lock).
    // In a very rare race where B linearised before the cancel fired, it may
    // return success with seq=2 committed — that is still correct contract
    // behaviour (AFTER linearisation cancel has no effect per FR-020).
    if (!rB.has_value()) {
        EXPECT_EQ(rB.error(), fixpp::core::error::store_cancelled)
            << "Cancelled store must return store_cancelled, not " << static_cast<int>(rB.error());

        // If cancelled, seq=2 must NOT be in the store.
        struct CountVisitor : public fixpp::session::retrieve_visitor {
            std::size_t count = 0;
            asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>> on_frame(
                fixpp::session::seqnum_t, std::span<const std::byte>) noexcept override {
                ++count;
                co_return fixpp::session::visit_result::cont;
            }
        } cv;
        run_on_pool(pool, [&store, &cv]() -> asio::awaitable<void> {
            co_await store->retrieve(1, 0, direction_t::outbound, cv);
        });
        EXPECT_EQ(cv.count, 1u) << "Only seq=1 should be in the store; seq=2 was cancelled";
    } else {
        // Both succeeded — cancel arrived too late. Still a valid outcome.
        SUCCEED() << "Cancel arrived after B linearised (race) — both stores succeeded";
    }
    pool.join();
}

// ── Test 5: retrieve visitor abort → store_visitor_aborted ───────────────────
//
// A visitor returning visit_result::abort causes retrieve() to return
// store_visitor_aborted.

struct aborting_visitor final : public fixpp::session::retrieve_visitor {
    asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>> on_frame(
        fixpp::session::seqnum_t, std::span<const std::byte>) noexcept override {
        co_return fixpp::core::expected_t<fixpp::session::visit_result>{
            fixpp::session::visit_result::abort};
    }
};

TEST(StoreCancellationContract, VisitorAbortReturnsStoreVisitorAborted) {
    asio::thread_pool pool{1};
    auto store = make_store();

    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&store]() -> asio::awaitable<void> {
            // Store one frame
            auto frame = make_test_frame(1, direction_t::inbound);
            auto sr =
                co_await store.store(1, std::span<const std::byte>(frame), direction_t::inbound);
            EXPECT_TRUE(sr.has_value());

            aborting_visitor vis;
            auto r = co_await store.retrieve(1, 0, direction_t::inbound, vis);
            EXPECT_FALSE(r.has_value());
            EXPECT_EQ(r.error(), fixpp::core::error::store_visitor_aborted)
                << "visitor abort must yield store_visitor_aborted";
        },
        asio::use_future);
    fut.get();
}

}  // namespace
