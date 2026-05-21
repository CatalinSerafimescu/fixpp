// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_outbound_store_post_commit.cpp
//
// Seam 7 — Outbound store-after-commit: persisted bytes carry the finalised
// BodyLength + CheckSum, never the pre-commit placeholder (AC US1 #3 / [2b §4.5]).
//
// Forces the BodyLength digit-count grow path (2 → 3) at Writer::commit;
// FSM calls store(seq, committed_span, outbound); persisted bytes carry the
// finalised form.
//
// Since the real Wire Writer is 004-owned and the FSM is 005-owned, this seam
// tests the contract: "the frame bytes stored are the ones passed in, byte-
// identical". The digit-grow path is simulated by constructing frames that
// have multi-digit BodyLength values. The store stores whatever is handed to
// it byte-identical — it doesn't inspect FIX fields.
//
// TDD: linker-RED until T020 (MemoryStore) ships.
#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <cstring>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>

#include "_fixtures_/test_double_fsm.hpp"

namespace {

using fixpp::session::direction_t;
using fixpp::session::MemoryStore;
using fixpp::store_test::byte_collecting_visitor;

// Simulate the "committed span" after Writer::commit has finalised the frame.
// In a real FIX message after commit, BodyLength is finalised with exact digit
// count. We construct two frames:
//   - frame_small: BodyLength has 2 digits (e.g. "9=12\x01...")
//   - frame_large: BodyLength has 3 digits (e.g. "9=123\x01...")
// The commit path "grows" the digit count. We store BOTH and verify byte equality.

std::vector<std::byte> make_frame_with_bodylength(const std::string& body_length_str) {
    // 8=FIX.4.4\x01 9=<body_length>\x01 ... 10=000\x01
    std::string raw = "8=FIX.4.4\x01";
    raw += "9=";
    raw += body_length_str;
    raw += "\x01";
    raw += "35=D\x01";
    raw += "49=SENDER\x01";
    raw += "56=TARGET\x01";
    raw += "34=1\x01";
    raw += "10=000\x01";

    std::vector<std::byte> bytes(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        bytes[i] = static_cast<std::byte>(raw[i]);
    }
    return bytes;
}

MemoryStore make_store() {
    MemoryStore::Config cfg;
    cfg.policy = fixpp::session::capacity_policy::bounded;
    cfg.inbound_capacity = 100;
    cfg.outbound_capacity = 100;
    cfg.max_frame_bytes = 4096;
    return MemoryStore{cfg};
}

TEST(OutboundStorePostCommit, BodyLengthTwoDigitPreserved) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(
        pool.get_executor(),
        []() -> asio::awaitable<void> {
            auto store = make_store();
            auto frame = make_frame_with_bodylength("99");  // 2-digit BodyLength

            auto r =
                co_await store.store(1, std::span<const std::byte>(frame), direction_t::outbound);
            EXPECT_TRUE(r.has_value());

            byte_collecting_visitor visitor;
            auto rr = co_await store.retrieve(1, 0, direction_t::outbound, visitor);
            EXPECT_TRUE(rr.has_value());
            EXPECT_EQ(visitor.entries().size(), 1u);
            if (!visitor.entries().empty()) {
                EXPECT_EQ(visitor.entries()[0].bytes, frame)
                    << "Stored bytes differ from committed frame (2-digit BodyLength)";
            }
        },
        asio::use_future);
    fut.get();
}

TEST(OutboundStorePostCommit, BodyLengthThreeDigitPreserved) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(
        pool.get_executor(),
        []() -> asio::awaitable<void> {
            auto store = make_store();
            // This represents the BodyLength grow path: 2 → 3 digits
            // The committed frame has 3-digit BodyLength (the finalised form)
            auto frame = make_frame_with_bodylength("123");  // 3-digit BodyLength

            auto r =
                co_await store.store(1, std::span<const std::byte>(frame), direction_t::outbound);
            EXPECT_TRUE(r.has_value());

            byte_collecting_visitor visitor;
            auto rr = co_await store.retrieve(1, 0, direction_t::outbound, visitor);
            EXPECT_TRUE(rr.has_value());
            EXPECT_EQ(visitor.entries().size(), 1u);
            if (!visitor.entries().empty()) {
                EXPECT_EQ(visitor.entries()[0].bytes, frame)
                    << "Stored bytes differ from committed frame (3-digit BodyLength)";
            }
        },
        asio::use_future);
    fut.get();
}

TEST(OutboundStorePostCommit, MultipleFramesWithGrowingBodyLength) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(
        pool.get_executor(),
        []() -> asio::awaitable<void> {
            auto store = make_store();

            // Store frames simulating the grow path at various sizes
            std::vector<std::vector<std::byte>> frames = {
                make_frame_with_bodylength("12"),    // 2-digit
                make_frame_with_bodylength("100"),   // 3-digit (grew at commit)
                make_frame_with_bodylength("999"),   // 3-digit max
                make_frame_with_bodylength("1000"),  // 4-digit (grew further)
            };

            for (std::size_t i = 0; i < frames.size(); ++i) {
                auto r = co_await store.store(static_cast<fixpp::session::seqnum_t>(i + 1),
                                              std::span<const std::byte>(frames[i]),
                                              direction_t::outbound);
                EXPECT_TRUE(r.has_value()) << "store() failed at i=" << i;
            }

            byte_collecting_visitor visitor;
            auto rr = co_await store.retrieve(1, 0, direction_t::outbound, visitor);
            EXPECT_TRUE(rr.has_value());
            EXPECT_EQ(visitor.entries().size(), frames.size());

            for (std::size_t i = 0; i < frames.size(); ++i) {
                EXPECT_EQ(visitor.entries()[i].bytes, frames[i])
                    << "frame " << i << " byte mismatch (BodyLength grow path)";
            }
        },
        asio::use_future);
    fut.get();
}

}  // namespace
