// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/conformance/test_store_corpus_replay.cpp
//
// Seam 17 — Store-side raw-frame round-trip conformance corpus replay.
// (SC-010 / [const §VII.5] / research D-5 / research D-13)
//
// Loads representative recorded FIX session byte sequences from
// tests/conformance/corpus/ and round-trips every frame through MemoryStore
// AND FileStore, asserting byte equality to the original input.
//
// SCOPE GUARD (research D-5 partition):
//   - Store-side raw-frame round-trip ONLY
//   - Does NOT invoke a FIX FSM (TC-001..TC-017 FIX FSM transitions are
//     005's discharge per research D-5)
//   - Corpus frames are consumed as OPAQUE byte sequences with seqnum +
//     direction metadata
//
// When the corpus directory is empty (before FIX session recordings land),
// the test generates a synthetic in-memory corpus covering the relevant
// byte-sequence shapes (short, medium, large frames; both directions).
//
// TDD: linker-RED until T020 (MemoryStore) and T023/T024/T026 (FileStore) ship.
#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>

#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>

#include "../session/_fixtures_/test_double_fsm.hpp"

namespace {

using fixpp::session::direction_t;
using fixpp::session::MemoryStore;
using fixpp::session::FileStore;
using fixpp::session::FileStoreFactory;
using fixpp::session::seqnum_t;
using fixpp::store_test::byte_collecting_visitor;
namespace fs = std::filesystem;

// ── Corpus loading ────────────────────────────────────────────────────────────

struct CorpusEntry {
    seqnum_t    seq{};
    direction_t dir{};
    std::vector<std::byte> bytes;
};

// Synthetic corpus: representative FIX frame shapes.
// These cover the store contract's raw-frame round-trip independently of any
// FIX FSM (research D-5 / [const §VII.5] store-side applicability).
std::vector<CorpusEntry> make_synthetic_corpus() {
    std::vector<CorpusEntry> corpus;

    // Group 1: Short outbound frames (Logon-like, <100 bytes)
    const char* short_frames[] = {
        "8=FIX.4.4\x019=56\x01""35=A\x01""49=SENDER\x01""56=TARGET\x01""34=1\x01""108=30\x01""10=099\x01",
        "8=FIX.4.4\x019=57\x01""35=A\x01""49=SENDER\x01""56=TARGET\x01""34=2\x01""108=30\x01""10=100\x01",
    };
    seqnum_t seq = 1;
    for (const char* raw : short_frames) {
        CorpusEntry e;
        e.seq = seq++;
        e.dir = direction_t::outbound;
        std::size_t n = std::strlen(raw);
        e.bytes.resize(n);
        std::memcpy(e.bytes.data(), raw, n);
        corpus.push_back(std::move(e));
    }

    // Group 2: Medium inbound frames (NewOrderSingle-like, ~200 bytes)
    for (int i = 0; i < 10; ++i) {
        CorpusEntry e;
        e.seq = static_cast<seqnum_t>(i + 1);
        e.dir = direction_t::inbound;
        std::string raw = "8=FIX.4.4\x01";
        raw += "9=120\x01";
        raw += "35=D\x01";
        raw += "49=CLIENTFIRM\x01";
        raw += "56=EXCHANGE\x01";
        raw += "34=";
        raw += std::to_string(i + 1);
        raw += "\x01";
        raw += "11=ORDER";
        raw += std::to_string(i);
        raw += "\x01";
        raw += "55=AAPL\x01";
        raw += "54=1\x01";
        raw += "60=20240101-09:30:00\x01";
        raw += "38=100\x01";
        raw += "40=2\x01";
        raw += "44=150.00\x01";
        raw += "10=000\x01";
        e.bytes.resize(raw.size());
        std::memcpy(e.bytes.data(), raw.data(), raw.size());
        corpus.push_back(std::move(e));
    }

    // Group 3: Large outbound frames (ExecutionReport-like, ~400 bytes)
    for (int i = 0; i < 5; ++i) {
        CorpusEntry e;
        e.seq = static_cast<seqnum_t>(i + 1);
        e.dir = direction_t::outbound;
        std::string raw = "8=FIX.4.4\x01";
        raw += "9=300\x01";
        raw += "35=8\x01";
        raw += "49=EXCHANGE\x01";
        raw += "56=CLIENTFIRM\x01";
        raw += "34=";
        raw += std::to_string(i + 1);
        raw += "\x01";
        raw += "17=EXEC";
        raw += std::to_string(i);
        raw += "\x01";
        raw += "20=0\x01";
        raw += "150=2\x01";
        raw += "39=2\x01";
        raw += "11=ORDER";
        raw += std::to_string(i);
        raw += "\x01";
        raw += "55=AAPL\x01";
        raw += "54=1\x01";
        raw += "38=100\x01";
        raw += "44=150.00\x01";
        raw += "32=100\x01";
        raw += "31=150.00\x01";
        raw += "14=100\x01";
        raw += "6=150.00\x01";
        raw += "58=Execution completed successfully at the requested price\x01";
        raw += "10=000\x01";
        e.bytes.resize(raw.size());
        std::memcpy(e.bytes.data(), raw.data(), raw.size());
        corpus.push_back(std::move(e));
    }

    return corpus;
}

// Try to load corpus from disk; fall back to synthetic if empty/missing.
std::vector<CorpusEntry> load_corpus() {
    fs::path corpus_dir = fs::path(__FILE__).parent_path() / "corpus";

    std::vector<CorpusEntry> loaded;
    if (fs::exists(corpus_dir)) {
        for (const auto& ent : fs::directory_iterator(corpus_dir)) {
            if (ent.path().extension() == ".frm") {
                std::ifstream f(ent.path(), std::ios::binary);
                if (!f) continue;
                CorpusEntry e;
                std::uint32_t seq_le{};
                std::uint8_t  dir_byte{};
                std::uint8_t  padding[3]{};
                f.read(reinterpret_cast<char*>(&seq_le), 4);
                f.read(reinterpret_cast<char*>(&dir_byte), 1);
                f.read(reinterpret_cast<char*>(padding), 3);
                if (!f) continue;
                e.seq = seq_le;
                e.dir = (dir_byte == 0) ? direction_t::inbound : direction_t::outbound;
                std::string raw(std::istreambuf_iterator<char>(f), {});
                e.bytes.resize(raw.size());
                std::memcpy(e.bytes.data(), raw.data(), raw.size());
                loaded.push_back(std::move(e));
            }
        }
    }

    if (!loaded.empty()) return loaded;
    return make_synthetic_corpus();
}

// ── MemoryStore corpus round-trip ─────────────────────────────────────────────

TEST(StoreCorpusReplay, MemoryStoreRoundTrip) {
    asio::thread_pool pool{1};
    auto corpus = load_corpus();
    ASSERT_GT(corpus.size(), 0u) << "corpus must be non-empty";

    // Separate inbound and outbound entries
    std::vector<CorpusEntry*> inbound_entries, outbound_entries;
    for (auto& e : corpus) {
        if (e.dir == direction_t::inbound)  inbound_entries.push_back(&e);
        else                                outbound_entries.push_back(&e);
    }

    auto run = [&](const std::vector<CorpusEntry*>& entries, direction_t dir_val) {
        if (entries.empty()) return;
        auto fut = asio::co_spawn(pool.get_executor(),
            [entries_copy = entries, dir_val]() -> asio::awaitable<void> {
                MemoryStore::Config cfg;
                cfg.policy            = fixpp::session::capacity_policy::bounded;
                cfg.inbound_capacity  = entries_copy.size() + 10;
                cfg.outbound_capacity = entries_copy.size() + 10;
                cfg.max_frame_bytes   = 256 * 1024;
                MemoryStore store{cfg};

                seqnum_t seq = 1;
                for (const auto* e : entries_copy) {
                    auto r = co_await store.store(seq,
                        std::span<const std::byte>(e->bytes), dir_val);
                    EXPECT_TRUE(r.has_value())
                        << "MemoryStore::store() failed at seq=" << seq;
                    ++seq;
                }

                byte_collecting_visitor visitor;
                auto rr = co_await store.retrieve(1, 0, dir_val, visitor);
                EXPECT_TRUE(rr.has_value()) << "MemoryStore::retrieve() failed";
                EXPECT_EQ(visitor.entries().size(), entries_copy.size());

                for (std::size_t i = 0; i < entries_copy.size() &&
                                        i < visitor.entries().size(); ++i) {
                    EXPECT_EQ(visitor.entries()[i].bytes, entries_copy[i]->bytes)
                        << "MemoryStore corpus frame " << i
                        << " failed byte-equality check";
                }
            },
            asio::use_future);
        fut.get();
    };

    run(inbound_entries,  direction_t::inbound);
    run(outbound_entries, direction_t::outbound);
}

// ── FileStore corpus round-trip ───────────────────────────────────────────────

TEST(StoreCorpusReplay, FileStoreRoundTrip) {
    asio::thread_pool pool{2};
    auto corpus = load_corpus();
    ASSERT_GT(corpus.size(), 0u) << "corpus must be non-empty";

    auto base_dir = fs::temp_directory_path() / "fixpp_test_corpus_replay";
    fs::create_directories(base_dir);

    std::vector<CorpusEntry*> inbound_entries, outbound_entries;
    for (auto& e : corpus) {
        if (e.dir == direction_t::inbound)  inbound_entries.push_back(&e);
        else                                outbound_entries.push_back(&e);
    }

    auto run = [&](const std::vector<CorpusEntry*>& entries,
                   direction_t dir_val, const std::string& suffix) {
        if (entries.empty()) return;
        auto sub_dir = base_dir / suffix;
        fs::create_directories(sub_dir);

        auto fut = asio::co_spawn(pool.get_executor(),
            [entries_copy = entries, sub_dir, dir_val, &pool]()
                -> asio::awaitable<void>
            {
                FileStore::Config cfg;
                cfg.directory        = sub_dir;
                cfg.sender_comp_id   = "CORPUS";
                cfg.target_comp_id   = "REPLAY";
                cfg.max_frame_bytes  = 256 * 1024;
                cfg.file_io_executor = pool.get_executor();

                FileStoreFactory factory{cfg};
                auto minted = factory.make("CORPUS", "REPLAY", nullptr,
                                            1024*1024*1024, pool.get_executor());
                EXPECT_TRUE(minted.has_value()) << "FileStore make() failed";
                if (!minted.has_value()) co_return;
                auto& store = *minted.value();

                seqnum_t seq = 1;
                for (const auto* e : entries_copy) {
                    auto r = co_await store.store(seq,
                        std::span<const std::byte>(e->bytes), dir_val);
                    EXPECT_TRUE(r.has_value())
                        << "FileStore::store() failed at seq=" << seq;
                    ++seq;
                }

                byte_collecting_visitor visitor;
                auto rr = co_await store.retrieve(1, 0, dir_val, visitor);
                EXPECT_TRUE(rr.has_value()) << "FileStore::retrieve() failed";
                EXPECT_EQ(visitor.entries().size(), entries_copy.size());

                for (std::size_t i = 0; i < entries_copy.size() &&
                                        i < visitor.entries().size(); ++i) {
                    EXPECT_EQ(visitor.entries()[i].bytes, entries_copy[i]->bytes)
                        << "FileStore corpus frame " << i
                        << " failed byte-equality check";
                }
            },
            asio::use_future);
        fut.get();
    };

    run(inbound_entries,  direction_t::inbound,  "inbound");
    run(outbound_entries, direction_t::outbound, "outbound");

    fs::remove_all(base_dir);
}

}  // namespace
