// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_retrieve_with_gaps.cpp
//
// Seam 8 — retrieve() with gaps and boundary conditions
// (I-19 / [FIX-SL §4.8.3] / [FIX-SL §4.8.5]).
//
// Tests:
//   - replay over [42, 99]: walks visitor in seqnum order
//   - retrieve(begin=0) → store_seqnum_invalid before any visitor call
//   - retrieve(begin=10, end=5) → store_invalid_range
//   - retrieve over a never-persisted gap (mid-range) → store_seqnum_gap
//   - retrieve(1, 0) (end-of-store sentinel) → normal termination
//
// TDD: linker-RED until T020 (MemoryStore) ships.
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

using fixpp::session::MemoryStore;
using fixpp::session::direction_t;
using fixpp::session::seqnum_t;
using fixpp::store_test::byte_collecting_visitor;
using fixpp::store_test::make_test_frame;

MemoryStore make_store(std::size_t cap = 1000) {
    MemoryStore::Config cfg;
    cfg.policy            = fixpp::session::capacity_policy::bounded;
    cfg.inbound_capacity  = cap;
    cfg.outbound_capacity = cap;
    cfg.max_frame_bytes   = 4096;
    return MemoryStore{cfg};
}

// Store frames at seqnums [begin, end] (inclusive) for the given direction
asio::awaitable<void>
store_range(MemoryStore& store, seqnum_t begin, seqnum_t end, direction_t dir)
{
    for (seqnum_t s = begin; s <= end; ++s) {
        auto frame = make_test_frame(s, dir);
        auto r = co_await store.store(s, std::span<const std::byte>(frame), dir);
        EXPECT_TRUE(r.has_value()) << "store() failed at seq=" << s;
    }
}

// ── Tests ────────────────────────────────────────────────────────────────────

// Replay over [42, 99] walks visitor in seqnum order
TEST(RetrieveWithGaps, ReplaySubRange) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(pool.get_executor(),
        []() -> asio::awaitable<void> {
            auto store = make_store();
            // Store frames 1..99 (so [42,99] is a valid contiguous sub-range)
            co_await store_range(store, 1, 99, direction_t::inbound);

            byte_collecting_visitor visitor;
            auto r = co_await store.retrieve(42, 99, direction_t::inbound, visitor);
            EXPECT_TRUE(r.has_value()) << "retrieve [42,99] failed";
            EXPECT_EQ(visitor.entries().size(), static_cast<std::size_t>(99 - 42 + 1));
            for (std::size_t i = 0; i < visitor.entries().size(); ++i) {
                auto expected_seq = static_cast<seqnum_t>(42 + i);
                EXPECT_EQ(visitor.entries()[i].seq, expected_seq)
                    << "seqnum out of order at position " << i;
            }
        },
        asio::use_future);
    fut.get();
}

// retrieve(begin=0) → store_seqnum_invalid, no visitor calls
TEST(RetrieveWithGaps, BeginZeroIsInvalid) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(pool.get_executor(),
        []() -> asio::awaitable<void> {
            auto store = make_store();
            co_await store_range(store, 1, 5, direction_t::outbound);

            byte_collecting_visitor visitor;
            auto r = co_await store.retrieve(0, 5, direction_t::outbound, visitor);
            EXPECT_FALSE(r.has_value());
            EXPECT_EQ(r.error(), fixpp::core::error::store_seqnum_invalid)
                << "begin=0 must return store_seqnum_invalid";
            EXPECT_TRUE(visitor.entries().empty())
                << "visitor must NOT be called before returning store_seqnum_invalid";
        },
        asio::use_future);
    fut.get();
}

// retrieve(begin=10, end=5) → store_invalid_range
TEST(RetrieveWithGaps, InvalidRange) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(pool.get_executor(),
        []() -> asio::awaitable<void> {
            auto store = make_store();
            co_await store_range(store, 1, 15, direction_t::inbound);

            byte_collecting_visitor visitor;
            auto r = co_await store.retrieve(10, 5, direction_t::inbound, visitor);
            EXPECT_FALSE(r.has_value());
            EXPECT_EQ(r.error(), fixpp::core::error::store_invalid_range)
                << "end < begin (non-zero end) must return store_invalid_range";
            EXPECT_TRUE(visitor.entries().empty())
                << "visitor must NOT be called before returning store_invalid_range";
        },
        asio::use_future);
    fut.get();
}

// retrieve over a never-persisted gap → store_seqnum_gap
// The MemoryStore index is slot-based (seq=1 maps to index 0, seq=2 to 1, etc).
// A "gap" means: retrieve requests range [begin, end] but a seqnum in that
// range was never stored. We simulate this by storing frames 1..5, then
// attempting to retrieve [1, 10] — seqnums 6..10 are never stored (gap).
TEST(RetrieveWithGaps, NeverPersistedGap) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(pool.get_executor(),
        []() -> asio::awaitable<void> {
            auto store = make_store();
            // Store only frames 1..5 (seqnums 6..10 are never persisted)
            co_await store_range(store, 1, 5, direction_t::outbound);

            // retrieve(1, 10) should fail with store_seqnum_gap at seqnum 6
            byte_collecting_visitor visitor;
            auto r = co_await store.retrieve(1, 10, direction_t::outbound, visitor);
            EXPECT_FALSE(r.has_value());
            EXPECT_EQ(r.error(), fixpp::core::error::store_seqnum_gap)
                << "retrieving beyond stored tail must return store_seqnum_gap";
            // Exactly 5 frames should have been visited before the gap
            EXPECT_EQ(visitor.entries().size(), static_cast<std::size_t>(5));
        },
        asio::use_future);
    fut.get();
}

// retrieve(1, 0) with end=0 (to-end sentinel) → normal termination
TEST(RetrieveWithGaps, EndZeroSentinel) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(pool.get_executor(),
        []() -> asio::awaitable<void> {
            auto store = make_store();
            constexpr seqnum_t kFrames = 20;
            co_await store_range(store, 1, kFrames, direction_t::inbound);

            byte_collecting_visitor visitor;
            auto r = co_await store.retrieve(1, 0, direction_t::inbound, visitor);
            EXPECT_TRUE(r.has_value())
                << "retrieve(1, 0) with end=0 sentinel must succeed";
            EXPECT_EQ(visitor.entries().size(), static_cast<std::size_t>(kFrames));
        },
        asio::use_future);
    fut.get();
}

// visitor returning visit_result::stop terminates early with success
TEST(RetrieveWithGaps, VisitorStopTerminatesCleanly) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(pool.get_executor(),
        []() -> asio::awaitable<void> {
            auto store = make_store();
            co_await store_range(store, 1, 20, direction_t::inbound);

            // Visitor that stops after 5 frames
            struct StopAfterFive final : fixpp::session::retrieve_visitor {
                std::size_t count{0};
                [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>>
                on_frame(seqnum_t, std::span<const std::byte>) noexcept override {
                    ++count;
                    if (count >= 5) {
                        co_return fixpp::core::expected_t<fixpp::session::visit_result>{
                            fixpp::session::visit_result::stop};
                    }
                    co_return fixpp::core::expected_t<fixpp::session::visit_result>{
                        fixpp::session::visit_result::cont};
                }
            } v;

            auto r = co_await store.retrieve(1, 0, direction_t::inbound, v);
            EXPECT_TRUE(r.has_value())
                << "visit_result::stop must return success (not an error)";
            EXPECT_EQ(v.count, static_cast<std::size_t>(5));
        },
        asio::use_future);
    fut.get();
}

// visitor returning visit_result::abort returns store_visitor_aborted
TEST(RetrieveWithGaps, VisitorAbortReturnsAbortError) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(pool.get_executor(),
        []() -> asio::awaitable<void> {
            auto store = make_store();
            co_await store_range(store, 1, 20, direction_t::inbound);

            struct AbortOnThird final : fixpp::session::retrieve_visitor {
                std::size_t count{0};
                [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>>
                on_frame(seqnum_t, std::span<const std::byte>) noexcept override {
                    ++count;
                    if (count >= 3) {
                        co_return fixpp::core::expected_t<fixpp::session::visit_result>{
                            fixpp::session::visit_result::abort};
                    }
                    co_return fixpp::core::expected_t<fixpp::session::visit_result>{
                        fixpp::session::visit_result::cont};
                }
            } v;

            auto r = co_await store.retrieve(1, 0, direction_t::inbound, v);
            EXPECT_FALSE(r.has_value());
            EXPECT_EQ(r.error(), fixpp::core::error::store_visitor_aborted)
                << "visit_result::abort must return store_visitor_aborted";
        },
        asio::use_future);
    fut.get();
}

}  // namespace
