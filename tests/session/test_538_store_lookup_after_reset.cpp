// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_538_store_lookup_after_reset.cpp
//
// fixpp#538 (.specify/536-537-538-recovery-liveness.md §2.2, D-538-1 and D-538-2; §5, the
// table "#538"): a store looks a sequence number up relative to the first entry it
// retains for that direction. The cells here run on a bare store, with no session:
//
//   StoreLookupAfterReset      a reset whose first outbound number is 2, then two
//                              stores; and a direction left empty by a reset. Each cell
//                              runs on a bounded MemoryStore, an unbounded one and a
//                              FileStore.
//   DefaultResetToBody         a delegating store that does not override reset_to, so
//                              that MessageStore's default body produces the state
//                              (D-538-2), over a MemoryStore and over a FileStore.
//   FileStoreLookupAfterReset  a FileStore closed and opened again on the same
//                              directory after such a reset.
//
// A (in, out) reset below is MessageStore::reset_to(in, out).
//
// Each cell is one coroutine on a one-thread pool, which stands in for a session's
// serialisation domain; a FileStore's writes run on a second pool.

#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/thread_pool.hpp>
#include <cstddef>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/seqnum.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "_fixtures_/test_double_fsm.hpp"
#include "support/recording_store.hpp"
#include "support/temp_dir.hpp"

namespace {

using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::session::capacity_policy;
using fixpp::session::direction_t;
using fixpp::session::FileStore;
using fixpp::session::FileStoreFactory;
using fixpp::session::MemoryStore;
using fixpp::session::MessageStore;
using fixpp::session::seqnum_t;
using fixpp::store_test::byte_collecting_visitor;
using fixpp::store_test::make_test_frame;
using fixpp::store_test::run_on_pool;
using fixpp::test_support::RecordingStore;
using fixpp::test_support::store_op;
using fixpp::test_support::StoreCall;
using fixpp::test_support::StoreCallLog;

enum class StoreKind : std::uint8_t { memory_bounded, memory_unbounded, file };

std::string kind_name(StoreKind const kind) {
    switch (kind) {
        case StoreKind::memory_bounded:
            return "MemoryBounded";
        case StoreKind::memory_unbounded:
            return "MemoryUnbounded";
        case StoreKind::file:
            return "File";
    }
    return "Unknown";
}

// One retrieve call: its status, and the sequence numbers and frames it visited.
struct Retrieved {
    expected_t<void> status;
    std::vector<byte_collecting_visitor::entry> visited;

    [[nodiscard]] bool is_error(error const e) const { return !status && status.error() == e; }
    [[nodiscard]] std::string text() const {
        std::string t = status ? std::string{"ok"}
                               : "error " + std::to_string(static_cast<int>(status.error()));
        t += ", visited";
        for (auto const& e : visited) t += " " + std::to_string(e.seq);
        if (visited.empty()) t += " nothing";
        return t;
    }
};

asio::awaitable<Retrieved> retrieve(MessageStore& store, seqnum_t const begin, seqnum_t const end,
                                    direction_t const dir) {
    byte_collecting_visitor v;
    Retrieved out;
    out.status = co_await store.retrieve(begin, end, dir, v);
    out.visited = v.entries();
    co_return out;
}

// Stores make_test_frame(seq, dir) under each of [first, last]; false at the first refusal.
asio::awaitable<bool> store_range(MessageStore& store, seqnum_t const first, seqnum_t const last,
                                  direction_t const dir) {
    for (seqnum_t s = first; s <= last; ++s) {
        auto const frame = make_test_frame(s, dir);
        if (auto r = co_await store.store(s, std::span<const std::byte>{frame}, dir); !r) {
            co_return false;
        }
    }
    co_return true;
}

// `r` visited exactly the frames stored under [first, last], in order.
bool visited_range(Retrieved const& r, seqnum_t const first, seqnum_t const last,
                   direction_t const dir) {
    if (r.visited.size() != static_cast<std::size_t>(last - first + 1)) return false;
    for (std::size_t i = 0; i < r.visited.size(); ++i) {
        auto const seq = first + static_cast<seqnum_t>(i);
        if (r.visited[i].seq != seq || r.visited[i].bytes != make_test_frame(seq, dir)) {
            return false;
        }
    }
    return true;
}

// The pools, the directory of a FileStore, and the store of each kind.
class StoreFixture {
public:
    // Declared first, destroyed last: the directory is removed after every store over it.
    struct temp_dir {
        std::filesystem::path path = fixpp::test_support::unique_temp_dir("b28_538_lookup");
        temp_dir() = default;
        ~temp_dir() { (void)fixpp::test_support::try_remove_temp_dir(path); }
        temp_dir(temp_dir const&) = delete;
        temp_dir& operator=(temp_dir const&) = delete;
    } dir;
    asio::thread_pool file_io{1};  // a FileStore's file I/O executor
    asio::thread_pool pool{1};     // the cell's coroutine

    [[nodiscard]] std::unique_ptr<MessageStore> make(StoreKind const kind) {
        if (kind == StoreKind::file) {
            FileStore::Config cfg;
            cfg.directory = dir.path;
            cfg.sender_comp_id = "SENDER";
            cfg.target_comp_id = "TARGET";
            cfg.max_frame_bytes = 4096;
            cfg.file_io_executor = file_io.get_executor();
            auto made = FileStoreFactory{cfg}.make("SENDER", "TARGET", nullptr,
                                                   std::size_t{1} << 30, file_io.get_executor());
            return made ? std::move(*made) : nullptr;
        }
        MemoryStore::Config cfg;
        cfg.policy = kind == StoreKind::memory_bounded ? capacity_policy::bounded
                                                       : capacity_policy::unbounded;
        cfg.inbound_capacity = 16;
        cfg.outbound_capacity = 16;
        cfg.max_frame_bytes = 4096;
        return std::make_unique<MemoryStore>(cfg);
    }
};

class StoreLookupAfterReset : public ::testing::TestWithParam<StoreKind> {
protected:
    StoreFixture fx;

    // A (1, 2) reset, then two stores outbound, at 2 and 3. Null when a step failed.
    asio::awaitable<std::unique_ptr<MessageStore>> first_retained_is_two() {
        auto store = fx.make(GetParam());
        if (!store) co_return nullptr;
        if (auto r = co_await store->reset_to(1, 2); !r) co_return nullptr;
        if (!co_await store_range(*store, 2, 3, direction_t::outbound)) co_return nullptr;
        co_return store;
    }
};

// D-538-1, "After a (·, 2) reset and two stores". A number below the first retained one
// is a gap, and nothing is visited: one slot.
TEST_P(StoreLookupAfterReset, FirstRetainedIsTwo_TheNumberBelowIt_IsAGap) {
    run_on_pool(fx.pool, [&]() -> asio::awaitable<void> {
        auto const store = co_await first_retained_is_two();
        EXPECT_NE(store, nullptr) << "setup: the reset and the two stores";
        if (!store) co_return;
        auto const r = co_await retrieve(*store, 1, 1, direction_t::outbound);
        EXPECT_TRUE(r.is_error(error::store_seqnum_gap) && r.visited.empty())
            << "retrieve(1, 1): " << r.text();
    });
}

// The same from below the first retained number to the tail: a gap stops a range walk
// at its first number, so nothing is visited.
TEST_P(StoreLookupAfterReset, FirstRetainedIsTwo_FromBelowItToTheTail_IsAGapAndVisitsNothing) {
    run_on_pool(fx.pool, [&]() -> asio::awaitable<void> {
        auto const store = co_await first_retained_is_two();
        EXPECT_NE(store, nullptr) << "setup: the reset and the two stores";
        if (!store) co_return;
        auto const r = co_await retrieve(*store, 1, 0, direction_t::outbound);
        EXPECT_TRUE(r.is_error(error::store_seqnum_gap) && r.visited.empty())
            << "retrieve(1, 0): " << r.text();
    });
}

// From the first retained number to the tail: both stored frames, each under its own
// number.
TEST_P(StoreLookupAfterReset, FirstRetainedIsTwo_FromItToTheTail_VisitsBothFrames) {
    run_on_pool(fx.pool, [&]() -> asio::awaitable<void> {
        auto const store = co_await first_retained_is_two();
        EXPECT_NE(store, nullptr) << "setup: the reset and the two stores";
        if (!store) co_return;
        auto const r = co_await retrieve(*store, 2, 0, direction_t::outbound);
        EXPECT_TRUE(r.status.has_value() && visited_range(r, 2, 3, direction_t::outbound))
            << "retrieve(2, 0): " << r.text();
    });
}

// D-538-1, empty direction. After a (2, 2) reset neither direction holds a frame and
// each counter is 2: every requested number is a gap, and a range that starts at the
// counter is the empty-range success.
TEST_P(StoreLookupAfterReset, Guard_EmptyDirection_EveryNumberIsAGap) {
    run_on_pool(fx.pool, [&]() -> asio::awaitable<void> {
        auto const store = fx.make(GetParam());
        EXPECT_NE(store, nullptr) << "setup: the store";
        if (!store) co_return;
        auto const reset = co_await store->reset_to(2, 2);
        EXPECT_TRUE(reset.has_value()) << "setup: reset_to(2, 2)";
        if (!reset) co_return;
        for (direction_t const dir : {direction_t::inbound, direction_t::outbound}) {
            SCOPED_TRACE(dir == direction_t::inbound ? "inbound" : "outbound");
            auto const one = co_await retrieve(*store, 1, 1, dir);
            EXPECT_TRUE(one.is_error(error::store_seqnum_gap) && one.visited.empty())
                << "retrieve(1, 1): " << one.text();
            auto const to_tail = co_await retrieve(*store, 1, 0, dir);
            EXPECT_TRUE(to_tail.is_error(error::store_seqnum_gap) && to_tail.visited.empty())
                << "retrieve(1, 0): " << to_tail.text();
            auto const from_counter = co_await retrieve(*store, 2, 0, dir);
            EXPECT_TRUE(from_counter.status.has_value() && from_counter.visited.empty())
                << "retrieve(2, 0): " << from_counter.text();
        }
    });
}

INSTANTIATE_TEST_SUITE_P(Stores, StoreLookupAfterReset,
                         ::testing::Values(StoreKind::memory_bounded, StoreKind::memory_unbounded,
                                           StoreKind::file),
                         [](auto const& info) { return kind_name(info.param); });

// ── D-538-2: a store that inherits MessageStore's default reset_to body ──────────────

class DefaultResetToBody : public ::testing::TestWithParam<StoreKind> {
protected:
    StoreFixture fx;
};

// The wrapper forwards store, retrieve, next_seqnum and reset to a shipped store and
// does not override reset_to. reset_to(1, 2) is then the shipped store's reset() and one
// advance of its outbound counter; the frame stored next, under 2, is retrievable under
// 2.
TEST_P(DefaultResetToBody, FrameStoredAfterTheResetIsRetrievableUnderItsOwnNumber) {
    run_on_pool(fx.pool, [&]() -> asio::awaitable<void> {
        auto inner = fx.make(GetParam());
        EXPECT_NE(inner, nullptr) << "setup: the shipped store";
        if (!inner) co_return;
        auto const log = std::make_shared<StoreCallLog>();
        log->reset_to_default_body = true;
        log->arm();
        RecordingStore store{std::move(inner), log};

        auto const reset = co_await store.reset_to(1, 2);
        EXPECT_TRUE(reset.has_value()) << "setup: reset_to(1, 2)";
        if (!reset) co_return;
        // The default body ran: the shipped store was given a reset() and one outbound
        // advance, and no reset_to.
        std::size_t resets = 0;
        std::size_t outbound_advances = 0;
        std::size_t others = 0;
        for (StoreCall const& c : log->calls()) {
            if (c.op == store_op::reset && c.returned() && !c.error) {
                ++resets;
            } else if (c.op == store_op::next_seqnum && c.dir == direction_t::outbound &&
                       c.increment && c.returned() && !c.error) {
                ++outbound_advances;
            } else {
                ++others;
            }
        }
        EXPECT_TRUE(resets == 1 && outbound_advances == 1 && others == 0)
            << "the path was not exercised: the shipped store was given " << resets << " reset(), "
            << outbound_advances << " outbound advance(s) and " << others << " other call(s)";

        auto const stored = co_await store_range(store, 2, 2, direction_t::outbound);
        EXPECT_TRUE(stored) << "store(2) must be accepted";
        auto const r = co_await retrieve(store, 2, 2, direction_t::outbound);
        EXPECT_TRUE(r.status.has_value() && visited_range(r, 2, 2, direction_t::outbound))
            << "retrieve(2, 2): " << r.text();
    });
}

INSTANTIATE_TEST_SUITE_P(Stores, DefaultResetToBody,
                         ::testing::Values(StoreKind::memory_bounded, StoreKind::file),
                         [](auto const& info) { return kind_name(info.param); });

// ── D-538-1: a FileStore opened again ────────────────────────────────────────────────

// A (1, 2) reset and two stores, then the store is destroyed and a second FileStore is
// opened on the same directory. It rebuilds its index from the log, and the two frames
// are retrievable under 2 and 3.
TEST(FileStoreLookupAfterReset, Reopened_FramesStoredAfterTheResetAreRetrievable) {
    StoreFixture fx;
    run_on_pool(fx.pool, [&]() -> asio::awaitable<void> {
        {
            auto const first = fx.make(StoreKind::file);
            EXPECT_NE(first, nullptr) << "setup: the first FileStore";
            if (!first) co_return;
            auto const reset = co_await first->reset_to(1, 2);
            EXPECT_TRUE(reset.has_value()) << "setup: reset_to(1, 2)";
            auto const stored = co_await store_range(*first, 2, 3, direction_t::outbound);
            EXPECT_TRUE(stored) << "setup: the stores at 2 and 3";
            if (!reset || !stored) co_return;
        }
        auto const second = fx.make(StoreKind::file);
        EXPECT_NE(second, nullptr) << "setup: the second FileStore, on the same directory";
        if (!second) co_return;
        auto const next = co_await second->next_seqnum(direction_t::outbound, false);
        EXPECT_TRUE(next.has_value() && *next == 4)
            << "the path was not exercised: the reopened store's next outbound number must "
               "follow the two frames in the log";

        auto const two = co_await retrieve(*second, 2, 2, direction_t::outbound);
        EXPECT_TRUE(two.status.has_value() && visited_range(two, 2, 2, direction_t::outbound))
            << "retrieve(2, 2): " << two.text();
        auto const three = co_await retrieve(*second, 3, 3, direction_t::outbound);
        EXPECT_TRUE(three.status.has_value() && visited_range(three, 3, 3, direction_t::outbound))
            << "retrieve(3, 3): " << three.text();
        auto const both = co_await retrieve(*second, 2, 0, direction_t::outbound);
        EXPECT_TRUE(both.status.has_value() && visited_range(both, 2, 3, direction_t::outbound))
            << "retrieve(2, 0): " << both.text();
        auto const below = co_await retrieve(*second, 1, 1, direction_t::outbound);
        EXPECT_TRUE(below.is_error(error::store_seqnum_gap) && below.visited.empty())
            << "retrieve(1, 1): " << below.text();
    });
}

}  // namespace
