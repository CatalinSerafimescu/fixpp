// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_memory_store_round_trip.cpp
//
// Seam 1 — MemoryStore round-trip byte equality (SC-001 / FR-006 / FR-007).
//
// For each N ∈ {1, 10, 100, 10000} and each direction ∈ {inbound, outbound},
// store(seq, frame_n, dir) × N then retrieve(1, 0, dir, visitor) produces a
// byte-identical sequence in seqnum order.
//
// TDD: linker-RED until T020 (MemoryStore) and T022 (MemoryStoreFactory)
// ship. Expected RED state: undefined reference to MemoryStore symbols.
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>

#include "_fixtures_/test_double_fsm.hpp"

namespace {

using fixpp::session::direction_t;
using fixpp::session::MemoryStore;
using fixpp::store_test::byte_collecting_visitor;
using fixpp::store_test::make_store_script;
using fixpp::store_test::run_on_pool;

// ── Helpers ──────────────────────────────────────────────────────────────────

MemoryStore make_store(std::size_t capacity = 20000) {
    MemoryStore::Config cfg;
    cfg.policy = fixpp::session::capacity_policy::bounded;
    cfg.inbound_capacity = capacity;
    cfg.outbound_capacity = capacity;
    cfg.max_frame_bytes = 4096;
    cfg.store_resource = nullptr;  // uses default (new/delete fallback)
    return MemoryStore{cfg};
}

asio::awaitable<void> do_round_trip_test(std::size_t count, direction_t dir) {
    auto store = make_store(count + 10);
    auto script = make_store_script(count, dir);

    // Store all frames
    for (const auto& step : script) {
        auto r =
            co_await store.store(step.seq, std::span<const std::byte>(step.frame_bytes), step.dir);
        EXPECT_TRUE(r.has_value()) << "store() failed at seq=" << step.seq;
        if (!r) co_return;
    }

    // Retrieve and check
    byte_collecting_visitor visitor;
    auto rr = co_await store.retrieve(1, 0, dir, visitor);
    EXPECT_TRUE(rr.has_value()) << "retrieve() failed";
    if (!rr) co_return;

    EXPECT_EQ(visitor.entries().size(), count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& expected = script[i].frame_bytes;
        const auto& got = visitor.entries()[i].bytes;
        EXPECT_EQ(expected, got) << "frame " << i << " bytes differ at seqnum=" << script[i].seq;
    }
}

// ── Tests ────────────────────────────────────────────────────────────────────

struct RoundTripParams {
    std::size_t count;
    direction_t dir;
};

class MemoryStoreRoundTrip : public ::testing::TestWithParam<RoundTripParams> {};

TEST_P(MemoryStoreRoundTrip, ByteIdenticalSequence) {
    const auto& p = GetParam();
    asio::thread_pool pool{1};
    auto fut =
        asio::co_spawn(pool.get_executor(), do_round_trip_test(p.count, p.dir), asio::use_future);
    fut.get();
}

INSTANTIATE_TEST_SUITE_P(Counts, MemoryStoreRoundTrip,
                         ::testing::Values(RoundTripParams{1, direction_t::inbound},
                                           RoundTripParams{1, direction_t::outbound},
                                           RoundTripParams{10, direction_t::inbound},
                                           RoundTripParams{10, direction_t::outbound},
                                           RoundTripParams{100, direction_t::inbound},
                                           RoundTripParams{100, direction_t::outbound},
                                           RoundTripParams{10000, direction_t::inbound},
                                           RoundTripParams{10000, direction_t::outbound}));

// retrieve(begin=0) → store_seqnum_invalid (I-19)
TEST(MemoryStoreRoundTrip, RetrieveBeginZeroIsInvalid) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&]() -> asio::awaitable<void> {
            auto store = make_store();
            byte_collecting_visitor visitor;
            auto r = co_await store.retrieve(0, 10, direction_t::inbound, visitor);
            EXPECT_FALSE(r.has_value());
            EXPECT_EQ(r.error(), fixpp::core::error::store_seqnum_invalid);
        },
        asio::use_future);
    fut.get();
}

// retrieve(begin > end, end != 0) → store_invalid_range (I-19)
TEST(MemoryStoreRoundTrip, RetrieveInvalidRange) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&]() -> asio::awaitable<void> {
            auto store = make_store();
            byte_collecting_visitor visitor;
            auto r = co_await store.retrieve(10, 5, direction_t::inbound, visitor);
            EXPECT_FALSE(r.has_value());
            EXPECT_EQ(r.error(), fixpp::core::error::store_invalid_range);
        },
        asio::use_future);
    fut.get();
}

// next_seqnum on fresh store returns 1 (seqnum_min)
TEST(MemoryStoreRoundTrip, NextSeqnumStartsAtOne) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&]() -> asio::awaitable<void> {
            auto store = make_store();
            auto r = co_await store.next_seqnum(direction_t::inbound, false);
            EXPECT_TRUE(r.has_value());
            EXPECT_EQ(*r, 1u);
            auto r2 = co_await store.next_seqnum(direction_t::outbound, false);
            EXPECT_TRUE(r2.has_value());
            EXPECT_EQ(*r2, 1u);
        },
        asio::use_future);
    fut.get();
}

// next_seqnum(_, true) increments the counter
TEST(MemoryStoreRoundTrip, NextSeqnumIncrements) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&]() -> asio::awaitable<void> {
            auto store = make_store();
            // First call (increment=true) returns 1 and advances to 2
            auto r1 = co_await store.next_seqnum(direction_t::outbound, true);
            EXPECT_TRUE(r1.has_value());
            EXPECT_EQ(*r1, 1u);
            // Second read (increment=false) should be 2
            auto r2 = co_await store.next_seqnum(direction_t::outbound, false);
            EXPECT_TRUE(r2.has_value());
            EXPECT_EQ(*r2, 2u);
        },
        asio::use_future);
    fut.get();
}

// reset() rewinds counters and clears stored frames
TEST(MemoryStoreRoundTrip, ResetClearsAndRewinds) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&]() -> asio::awaitable<void> {
            auto store = make_store();
            auto script = make_store_script(5, direction_t::outbound);
            for (const auto& step : script) {
                co_await store.store(step.seq, std::span<const std::byte>(step.frame_bytes),
                                     step.dir);
            }
            auto rr = co_await store.reset();
            EXPECT_TRUE(rr.has_value());
            // After reset, next_seqnum should be 1 again
            auto ns = co_await store.next_seqnum(direction_t::outbound, false);
            EXPECT_TRUE(ns.has_value());
            EXPECT_EQ(*ns, 1u);
        },
        asio::use_future);
    fut.get();
}

}  // namespace
