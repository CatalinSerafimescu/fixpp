// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_file_store_coverage_uplift.cpp
//
// F4.3 coverage uplift for file_store.cpp — exercises paths not reached by
// existing seam tests (which focus on MemoryStore for many generic operations).
//
// Targeted uncovered paths:
//   1. next_seqnum(dir, increment=true) — writes a counter record to disk
//   2. retrieve() with begin=0 → store_seqnum_invalid
//   3. retrieve() with end < begin → store_invalid_range
//   4. retrieve() traversal that hits a gap → store_seqnum_gap
//   5. retrieve() visitor returns visit_result::stop
//   6. retrieve() visitor returns visit_result::abort
//   7. flush_for_session_close() direct call on an open FileStore
//   8. Counter record recovery after re-open
//   9. retrieve() with begin > tail_end → empty success
//
// All tests run under commit_per_message policy (fdatasync per frame)
// to avoid timer-based commit_interval complexity.
//
// COROUTINE NOTE: ASSERT_* macros internally call return, which is illegal
// inside a coroutine. Use EXPECT_* + guard `if (!...) co_return;` instead.

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <cstddef>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <span>
#include <vector>

#include "_fixtures_/store_temp_dir.hpp"
#include "_fixtures_/test_double_fsm.hpp"

namespace {

namespace fs = std::filesystem;
using fixpp::core::error;
using fixpp::session::direction_t;
using fixpp::session::FileStoreFactory;
using fixpp::session::FileStorePolicy;
using fixpp::session::seqnum_t;
using fixpp::session::visit_result;
using fixpp::store_test::make_test_frame;
using fixpp::store_test::run_on_pool;
using fixpp::store_test::unique_store_dir;

// ── Helpers ──────────────────────────────────────────────────────────────────

// Open a FileStore via the factory using the standard test credentials.
std::unique_ptr<fixpp::session::MessageStore> open_store(const fs::path& dir,
                                                         asio::any_io_executor exec,
                                                         FileStorePolicy policy = {}) {
    fixpp::session::FileStore::Config cfg;
    cfg.directory = dir;
    cfg.sender_comp_id = "SENDER";
    cfg.target_comp_id = "TARGET";
    cfg.policy = policy;
    cfg.max_frame_bytes = 4096;
    cfg.file_io_executor = exec;
    FileStoreFactory factory{cfg};
    auto r = factory.make("SENDER", "TARGET", nullptr, 1ULL << 30, exec);
    if (!r.has_value()) return nullptr;
    return std::move(*r);
}

// ── Visitors ─────────────────────────────────────────────────────────────────

// Collects all frames.
class CollectVisitor : public fixpp::session::retrieve_visitor {
public:
    std::vector<std::vector<std::byte>> frames;

    asio::awaitable<fixpp::core::expected_t<visit_result>> on_frame(
        seqnum_t /*seq*/, std::span<const std::byte> data) noexcept override {
        frames.emplace_back(data.begin(), data.end());
        co_return visit_result::cont;
    }
};

// Returns stop after the first frame.
class StopAfterOneVisitor : public fixpp::session::retrieve_visitor {
public:
    int call_count = 0;
    asio::awaitable<fixpp::core::expected_t<visit_result>> on_frame(
        seqnum_t /*seq*/, std::span<const std::byte> /*data*/) noexcept override {
        ++call_count;
        co_return visit_result::stop;
    }
};

// Returns abort after the first frame.
class AbortAfterOneVisitor : public fixpp::session::retrieve_visitor {
public:
    int call_count = 0;
    asio::awaitable<fixpp::core::expected_t<visit_result>> on_frame(
        seqnum_t /*seq*/, std::span<const std::byte> /*data*/) noexcept override {
        ++call_count;
        co_return visit_result::abort;
    }
    // Uses base class abort_error() which returns store_visitor_aborted.
};

// ─────────────────────────────────────────────────────────────────────────────
// 1. next_seqnum(dir, increment=true) writes a counter record to disk
// ─────────────────────────────────────────────────────────────────────────────

TEST(FileStoreCoverageUplift, NextSeqnumIncrementWritesCounterRecord) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("ns_incr");

    run_on_pool(pool, [&]() -> asio::awaitable<void> {
        auto store = open_store(dir, pool.get_executor());
        EXPECT_NE(store, nullptr);
        if (!store) co_return;

        const auto d = direction_t::outbound;

        // Verify initial state: next_seqnum(false) == 1
        auto r1 = co_await store->next_seqnum(d, false);
        EXPECT_TRUE(r1.has_value());
        if (!r1) co_return;
        EXPECT_EQ(*r1, seqnum_t{1});

        // Store one frame to advance the internal state
        auto frame1 = make_test_frame(1, d);
        auto sr = co_await store->store(1, std::span<const std::byte>(frame1), d);
        EXPECT_TRUE(sr.has_value());
        if (!sr) co_return;

        // next_seqnum(true) should return current value and advance counter.
        // This writes a counter record to disk (the increment=true path in
        // file_store.cpp lines 904-923).
        auto r2 = co_await store->next_seqnum(d, true);
        EXPECT_TRUE(r2.has_value());
        if (!r2) co_return;
        EXPECT_EQ(*r2, seqnum_t{2}) << "next_seqnum(true) must return current before increment";

        // After increment, next_seqnum(false) must return 3
        auto r3 = co_await store->next_seqnum(d, false);
        EXPECT_TRUE(r3.has_value());
        if (!r3) co_return;
        EXPECT_EQ(*r3, seqnum_t{3});
    });

    pool.join();
    fs::remove_all(dir);
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. retrieve() with begin=0 → store_seqnum_invalid
// ─────────────────────────────────────────────────────────────────────────────

TEST(FileStoreCoverageUplift, RetrieveBeginZeroIsInvalid) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("retr_begin0");

    run_on_pool(pool, [&]() -> asio::awaitable<void> {
        auto store = open_store(dir, pool.get_executor());
        EXPECT_NE(store, nullptr);
        if (!store) co_return;

        CollectVisitor vis;
        // begin == 0 is always invalid per FR-016 / I-03
        auto r = co_await store->retrieve(0, 0, direction_t::outbound, vis);
        EXPECT_FALSE(r.has_value());
        if (!r.has_value()) {
            EXPECT_EQ(r.error(), error::store_seqnum_invalid);
        }
    });

    pool.join();
    fs::remove_all(dir);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. retrieve() with end < begin → store_invalid_range
// ─────────────────────────────────────────────────────────────────────────────

TEST(FileStoreCoverageUplift, RetrieveInvalidRangeRejected) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("retr_inv_range");

    run_on_pool(pool, [&]() -> asio::awaitable<void> {
        auto store = open_store(dir, pool.get_executor());
        EXPECT_NE(store, nullptr);
        if (!store) co_return;

        CollectVisitor vis;
        // end=2, begin=5 → end < begin → invalid range
        auto r = co_await store->retrieve(5, 2, direction_t::outbound, vis);
        EXPECT_FALSE(r.has_value());
        if (!r.has_value()) {
            EXPECT_EQ(r.error(), error::store_invalid_range);
        }
    });

    pool.join();
    fs::remove_all(dir);
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. retrieve() that traverses a gap → store_seqnum_gap
// ─────────────────────────────────────────────────────────────────────────────

TEST(FileStoreCoverageUplift, RetrieveGapReturnedWhenSequenceHasHole) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("retr_gap");

    run_on_pool(pool, [&]() -> asio::awaitable<void> {
        auto store = open_store(dir, pool.get_executor());
        EXPECT_NE(store, nullptr);
        if (!store) co_return;

        const auto d = direction_t::outbound;

        // Store frame at seq=1 only; seq=2 is not stored.
        auto frame1 = make_test_frame(1, d);
        auto sr1 = co_await store->store(1, std::span<const std::byte>(frame1), d);
        EXPECT_TRUE(sr1.has_value());
        if (!sr1) co_return;

        // retrieve [1..2] — seq 2 was never stored → gap → store_seqnum_gap
        CollectVisitor vis;
        auto r = co_await store->retrieve(1, 2, d, vis);
        EXPECT_FALSE(r.has_value());
        if (!r.has_value()) {
            EXPECT_EQ(r.error(), error::store_seqnum_gap);
        }
        // seq 1 should have been visited before the gap was detected
        EXPECT_EQ(vis.frames.size(), 1u);
    });

    pool.join();
    fs::remove_all(dir);
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. retrieve() visitor returns stop → clean early termination
// ─────────────────────────────────────────────────────────────────────────────

TEST(FileStoreCoverageUplift, RetrieveVisitorStopTerminatesCleanly) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("retr_stop");

    run_on_pool(pool, [&]() -> asio::awaitable<void> {
        auto store = open_store(dir, pool.get_executor());
        EXPECT_NE(store, nullptr);
        if (!store) co_return;

        const auto d = direction_t::outbound;

        // Store 3 frames
        for (seqnum_t s = 1; s <= 3; ++s) {
            auto frame = make_test_frame(s, d);
            auto sr = co_await store->store(s, std::span<const std::byte>(frame), d);
            EXPECT_TRUE(sr.has_value());
            if (!sr) co_return;
        }

        // retrieve [1..3] with a visitor that stops after frame 1
        StopAfterOneVisitor vis;
        auto r = co_await store->retrieve(1, 3, d, vis);
        // stop is success
        EXPECT_TRUE(r.has_value());
        EXPECT_EQ(vis.call_count, 1) << "visitor::stop must halt iteration after first call";
    });

    pool.join();
    fs::remove_all(dir);
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. retrieve() visitor returns abort → store_visitor_aborted
// ─────────────────────────────────────────────────────────────────────────────

TEST(FileStoreCoverageUplift, RetrieveVisitorAbortReturnsError) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("retr_abort");

    run_on_pool(pool, [&]() -> asio::awaitable<void> {
        auto store = open_store(dir, pool.get_executor());
        EXPECT_NE(store, nullptr);
        if (!store) co_return;

        const auto d = direction_t::outbound;

        // Store 2 frames
        for (seqnum_t s = 1; s <= 2; ++s) {
            auto frame = make_test_frame(s, d);
            auto sr = co_await store->store(s, std::span<const std::byte>(frame), d);
            EXPECT_TRUE(sr.has_value());
            if (!sr) co_return;
        }

        AbortAfterOneVisitor vis;
        auto r = co_await store->retrieve(1, 2, d, vis);
        EXPECT_FALSE(r.has_value());
        if (!r.has_value()) {
            EXPECT_EQ(r.error(), error::store_visitor_aborted);
        }
        EXPECT_EQ(vis.call_count, 1);
    });

    pool.join();
    fs::remove_all(dir);
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. flush_for_session_close() direct call on an open FileStore
// ─────────────────────────────────────────────────────────────────────────────

TEST(FileStoreCoverageUplift, FlushForSessionCloseSucceedsOnOpenStore) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("flush_close");

    run_on_pool(pool, [&]() -> asio::awaitable<void> {
        // Use commit_batched(4) so there are kernel-buffered writes to drain.
        FileStorePolicy policy;
        policy.which = FileStorePolicy::kind::commit_batched;
        policy.batch_size = 4;

        auto store = open_store(dir, pool.get_executor(), policy);
        EXPECT_NE(store, nullptr);
        if (!store) co_return;

        const auto d = direction_t::outbound;
        // Store 2 frames (below batch threshold)
        for (seqnum_t s = 1; s <= 2; ++s) {
            auto frame = make_test_frame(s, d);
            auto sr = co_await store->store(s, std::span<const std::byte>(frame), d);
            EXPECT_TRUE(sr.has_value());
            if (!sr) co_return;
        }

        // Direct call to flush_for_session_close() — must succeed
        auto* file_store = dynamic_cast<fixpp::session::FileStore*>(store.get());
        EXPECT_NE(file_store, nullptr);
        if (!file_store) co_return;

        auto r = co_await file_store->flush_for_session_close();
        EXPECT_TRUE(r.has_value()) << "flush_for_session_close() must succeed: error="
                                   << (!r.has_value() ? static_cast<int>(r.error()) : 0);
    });

    pool.join();
    fs::remove_all(dir);
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. Counter record recovery: next_seqnum(incr) durable across re-open
// ─────────────────────────────────────────────────────────────────────────────

TEST(FileStoreCoverageUplift, CounterRecordSurvivesReopen) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("counter_reopen");

    // Phase 1: write frames and counter record (increment=true)
    run_on_pool(pool, [&]() -> asio::awaitable<void> {
        const auto d = direction_t::inbound;

        auto store = open_store(dir, pool.get_executor());
        EXPECT_NE(store, nullptr);
        if (!store) co_return;

        // Store 3 frames
        for (seqnum_t s = 1; s <= 3; ++s) {
            auto frame = make_test_frame(s, d);
            auto sr = co_await store->store(s, std::span<const std::byte>(frame), d);
            EXPECT_TRUE(sr.has_value());
            if (!sr) co_return;
        }

        // Advance counter via increment — writes a counter record to disk
        auto ns = co_await store->next_seqnum(d, true);
        EXPECT_TRUE(ns.has_value());
        if (!ns) co_return;
        EXPECT_EQ(*ns, seqnum_t{4});
        // store destructs here — counter record is durable (commit_per_message)
    });

    // Phase 2: re-open and verify counter record recovered
    run_on_pool(pool, [&]() -> asio::awaitable<void> {
        const auto d = direction_t::inbound;

        auto store2 = open_store(dir, pool.get_executor());
        EXPECT_NE(store2, nullptr);
        if (!store2) co_return;

        // next_seqnum(inbound, false) must return 5 (3+1 from increment, +1 for next)
        auto ns2 = co_await store2->next_seqnum(d, false);
        EXPECT_TRUE(ns2.has_value());
        if (!ns2) co_return;
        EXPECT_EQ(*ns2, seqnum_t{5}) << "counter record must be recovered from disk on re-open";
    });

    pool.join();
    fs::remove_all(dir);
}

// ─────────────────────────────────────────────────────────────────────────────
// 9. retrieve() with begin > tail → immediate success, no frames visited
// ─────────────────────────────────────────────────────────────────────────────

TEST(FileStoreCoverageUplift, RetrieveBeginBeyondTailSucceedsEmpty) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("retr_beyond");

    run_on_pool(pool, [&]() -> asio::awaitable<void> {
        auto store = open_store(dir, pool.get_executor());
        EXPECT_NE(store, nullptr);
        if (!store) co_return;

        const auto d = direction_t::outbound;

        // Store 2 frames
        for (seqnum_t s = 1; s <= 2; ++s) {
            auto frame = make_test_frame(s, d);
            auto sr = co_await store->store(s, std::span<const std::byte>(frame), d);
            EXPECT_TRUE(sr.has_value());
            if (!sr) co_return;
        }

        // retrieve starting at seq=100 (beyond tail=2) → empty success
        CollectVisitor vis;
        auto r = co_await store->retrieve(100, 0, d, vis);
        EXPECT_TRUE(r.has_value()) << "begin > tail must return success (empty walk)";
        EXPECT_EQ(vis.frames.size(), 0u);
    });

    pool.join();
    fs::remove_all(dir);
}

// ─────────────────────────────────────────────────────────────────────────────
// RC#5: FactoryRejectsCommitInterval
// commit_interval is gated out of v1.0: factory.make() must return
// store_factory_failed when the policy is commit_interval.
// ─────────────────────────────────────────────────────────────────────────────

TEST(FileStoreCoverageUplift, FactoryRejectsCommitInterval) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("rc5_commit_interval");

    FileStorePolicy policy;
    policy.which = FileStorePolicy::kind::commit_interval;
    policy.interval = std::chrono::milliseconds{100};

    fixpp::session::FileStore::Config cfg;
    cfg.directory = dir;
    cfg.sender_comp_id = "SENDER";
    cfg.target_comp_id = "TARGET";
    cfg.policy = policy;
    cfg.max_frame_bytes = 4096;
    cfg.file_io_executor = pool.get_executor();

    FileStoreFactory factory{cfg};
    auto r = factory.make("SENDER", "TARGET", nullptr, 1ULL << 30, pool.get_executor());

    EXPECT_FALSE(r.has_value())
        << "RC#5: factory.make() with commit_interval must return store_factory_failed";
    if (!r.has_value()) {
        EXPECT_EQ(r.error(), fixpp::core::error::store_factory_failed)
            << "RC#5: expected error is store_factory_failed";
    }

    pool.join();
    fs::remove_all(dir);
}

}  // namespace
