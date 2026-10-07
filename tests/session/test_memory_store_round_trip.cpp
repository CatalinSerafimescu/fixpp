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
// Q-28 (093-inbound-frame-dispositions tasks.md T079; contract C-6; data-model E-9):
// MessageStore::reset_to refuses a target outside {1, 2} with no effect, on MemoryStore's
// override and on the default body. The section comment above ResetToStore states each
// cell.
//
// TDD: linker-RED until T020 (MemoryStore) and T022 (MemoryStoreFactory)
// ship. Expected RED state: undefined reference to MemoryStore symbols.
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "_fixtures_/test_double_fsm.hpp"

namespace {

using fixpp::session::direction_t;
using fixpp::session::MemoryStore;
using fixpp::store_test::byte_collecting_visitor;
using fixpp::store_test::make_store_script;

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
            EXPECT_EQ(*r, 1U);
            auto r2 = co_await store.next_seqnum(direction_t::outbound, false);
            EXPECT_TRUE(r2.has_value());
            EXPECT_EQ(*r2, 1U);
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
            EXPECT_EQ(*r1, 1U);
            // Second read (increment=false) should be 2
            auto r2 = co_await store.next_seqnum(direction_t::outbound, false);
            EXPECT_TRUE(r2.has_value());
            EXPECT_EQ(*r2, 2U);
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
                auto sr = co_await store.store(
                    step.seq, std::span<const std::byte>(step.frame_bytes), step.dir);
                EXPECT_TRUE(sr.has_value()) << "setup store of seq " << step.seq << " failed";
            }
            // Premise: the counter has advanced past 1, so the post-reset check
            // below observes a rewind rather than an untouched empty store.
            auto pre = co_await store.next_seqnum(direction_t::outbound, false);
            EXPECT_EQ(pre.value_or(0), script.size() + 1);
            auto rr = co_await store.reset();
            EXPECT_TRUE(rr.has_value());
            // After reset, next_seqnum should be 1 again
            auto ns = co_await store.next_seqnum(direction_t::outbound, false);
            EXPECT_TRUE(ns.has_value());
            EXPECT_EQ(*ns, 1U);
        },
        asio::use_future);
    fut.get();
}

// ── Q-28 (093 tasks.md T079): reset_to's precondition ─────────────────────────
//
// Each cell first advances the store past both targets (frames stored outbound and
// the inbound counter incremented), so "no effect" is observable: after a refused
// pair the counters and the stored frames are what they were. An accepted pair is the
// control, so a store that refused everything would fail it.

using fixpp::session::seqnum_t;

// A store that keeps MessageStore's default reset_to body: it forwards the four pure
// virtuals to a MemoryStore and counts reset() calls.
class ResetToStore final : public fixpp::session::MessageStore {
public:
    MemoryStore inner = make_store();
    int resets = 0;

    asio::awaitable<fixpp::core::expected_t<void>> store(seqnum_t seq,
                                                         std::span<const std::byte> frame,
                                                         direction_t dir) noexcept override {
        return inner.store(seq, frame, dir);
    }
    asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t begin, seqnum_t end, direction_t dir,
        fixpp::session::retrieve_visitor& visitor) noexcept override {
        return inner.retrieve(begin, end, dir, visitor);
    }
    asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool increment) noexcept override {
        return inner.next_seqnum(dir, increment);
    }
    asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        ++resets;
        return inner.reset();
    }
};

struct Counters {
    seqnum_t in = 0;
    seqnum_t out = 0;
    std::size_t outbound_frames = 0;
};

asio::awaitable<Counters> read_counters(fixpp::session::MessageStore& store) {
    Counters c;
    auto in = co_await store.next_seqnum(direction_t::inbound, false);
    auto out = co_await store.next_seqnum(direction_t::outbound, false);
    c.in = in.value_or(0);
    c.out = out.value_or(0);
    byte_collecting_visitor visitor;
    if (c.out > 1) {
        (void)co_await store.retrieve(1, 0, direction_t::outbound, visitor);
    }
    c.outbound_frames = visitor.entries().size();
    co_return c;
}

// Stores five frames outbound and advances the inbound counter to 4.
asio::awaitable<void> advance(fixpp::session::MessageStore& store) {
    for (auto const& step : make_store_script(5, direction_t::outbound)) {
        auto r =
            co_await store.store(step.seq, std::span<const std::byte>(step.frame_bytes), step.dir);
        EXPECT_TRUE(r.has_value()) << "setup store of seq " << step.seq;
    }
    for (int i = 0; i < 3; ++i) {
        auto r = co_await store.next_seqnum(direction_t::inbound, true);
        EXPECT_TRUE(r.has_value()) << "setup inbound advance";
    }
}

constexpr std::pair<seqnum_t, seqnum_t> kRefused[] = {{0, 1}, {3, 1}, {1, 0}, {1, 3},
                                                      {2, 3}, {3, 2}, {0, 0}, {4, 6}};
constexpr std::pair<seqnum_t, seqnum_t> kAccepted[] = {{1, 1}, {1, 2}, {2, 1}, {2, 2}};

// Every refused pair, then every accepted pair, each on a freshly advanced store.
template <class MakeStore>
void run_q28(MakeStore make, const char* which) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&]() -> asio::awaitable<void> {
            for (auto const& [in, out] : kRefused) {
                auto store = make();
                co_await advance(*store);
                auto const before = co_await read_counters(*store);
                EXPECT_EQ(before.in, 4U) << which << " setup";
                EXPECT_EQ(before.out, 6U) << which << " setup";
                EXPECT_EQ(before.outbound_frames, 5U) << which << " setup";
                auto r = co_await store->reset_to(in, out);
                EXPECT_FALSE(r.has_value())
                    << which << ": reset_to(" << in << ", " << out << ") must be refused";
                if (!r.has_value()) {
                    EXPECT_EQ(r.error(), fixpp::core::error::session_invalid_argument)
                        << which << ": reset_to(" << in << ", " << out << ")";
                }
                auto const after = co_await read_counters(*store);
                EXPECT_EQ(after.in, before.in)
                    << which << ": reset_to(" << in << ", " << out << ") moved NextNumIn";
                EXPECT_EQ(after.out, before.out)
                    << which << ": reset_to(" << in << ", " << out << ") moved NextNumOut";
                EXPECT_EQ(after.outbound_frames, before.outbound_frames)
                    << which << ": reset_to(" << in << ", " << out << ") cleared frames";
            }
            for (auto const& [in, out] : kAccepted) {
                auto store = make();
                co_await advance(*store);
                auto r = co_await store->reset_to(in, out);
                EXPECT_TRUE(r.has_value()) << which << ": reset_to(" << in << ", " << out << ")";
                auto const after = co_await read_counters(*store);
                EXPECT_EQ(after.in, in)
                    << which << ": NextNumIn after reset_to(" << in << ", " << out << ")";
                EXPECT_EQ(after.out, out)
                    << which << ": NextNumOut after reset_to(" << in << ", " << out << ")";
                EXPECT_EQ(after.outbound_frames, 0U)
                    << which << ": frames after reset_to(" << in << ", " << out << ")";
            }
        },
        asio::use_future);
    fut.get();
}

TEST(MemoryStoreResetTo, Q28_TargetsOutsideOneTwoAreRefusedWithNoEffect) {
    run_q28(
        [] {
            MemoryStore::Config cfg;
            cfg.policy = fixpp::session::capacity_policy::bounded;
            cfg.inbound_capacity = 20000;
            cfg.outbound_capacity = 20000;
            cfg.max_frame_bytes = 4096;
            return std::make_unique<MemoryStore>(cfg);
        },
        "MemoryStore");
}

TEST(MemoryStoreResetTo, Q28_DefaultBodyRefusesTargetsOutsideOneTwoWithNoEffect) {
    run_q28([] { return std::make_unique<ResetToStore>(); }, "default body");
}

// The default body on a refused pair issues no reset(), and on an accepted pair
// exactly one.
TEST(MemoryStoreResetTo, Q28_DefaultBodyCallsResetOnlyForAnAcceptedPair) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&]() -> asio::awaitable<void> {
            ResetToStore store;
            co_await advance(store);
            (void)co_await store.reset_to(3, 1);
            EXPECT_EQ(store.resets, 0) << "a refused pair must not reach reset()";
            auto r = co_await store.reset_to(2, 2);
            EXPECT_TRUE(r.has_value());
            EXPECT_EQ(store.resets, 1) << "an accepted pair runs reset() once";
        },
        asio::use_future);
    fut.get();
}

}  // namespace
