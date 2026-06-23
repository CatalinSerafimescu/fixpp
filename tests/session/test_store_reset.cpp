// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_store_reset.cpp
//
// Seam 10 — reset() happy path only (FR-010 / I-15 / SC-003).
//
// Verifies happy-path reset() truncates stored frames and re-initialises
// counters to next_inbound = next_outbound = 1.
//
// Crash-cut variants live in US3 T036.
//
// Applies to both MemoryStore and FileStore.
//
// TDD: linker-RED until T020 (MemoryStore) and T023/T024/T026 (FileStore) ship.
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <span>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "_fixtures_/store_temp_dir.hpp"
#include "_fixtures_/test_double_fsm.hpp"

namespace {

using fixpp::session::direction_t;
using fixpp::session::FileStore;
using fixpp::session::FileStoreFactory;
using fixpp::session::MemoryStore;
using fixpp::store_test::byte_collecting_visitor;
using fixpp::store_test::make_store_script;
using fixpp::store_test::unique_store_dir;
namespace fs = std::filesystem;

// ── MemoryStore reset tests ──────────────────────────────────────────────────

MemoryStore make_memory_store() {
    MemoryStore::Config cfg;
    cfg.policy = fixpp::session::capacity_policy::bounded;
    cfg.inbound_capacity = 200;
    cfg.outbound_capacity = 200;
    cfg.max_frame_bytes = 4096;
    return MemoryStore{cfg};
}

// Happy-path reset: clears all frames in both directions + rewinds counters
TEST(StoreResetMemory, HappyPathClearsAndRewinds) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(
        pool.get_executor(),
        []() -> asio::awaitable<void> {
            auto store = make_memory_store();

            // Store some frames in both directions
            auto inbound_script = make_store_script(5, direction_t::inbound);
            auto outbound_script = make_store_script(7, direction_t::outbound);

            for (const auto& step : inbound_script) {
                co_await store.store(step.seq, std::span<const std::byte>(step.frame_bytes),
                                     step.dir);
            }
            for (const auto& step : outbound_script) {
                co_await store.store(step.seq, std::span<const std::byte>(step.frame_bytes),
                                     step.dir);
            }

            // Verify counters are advanced before reset
            auto ni = co_await store.next_seqnum(direction_t::inbound, false);
            EXPECT_EQ(*ni, 6u) << "inbound next_seqnum should be 6 before reset";
            auto no = co_await store.next_seqnum(direction_t::outbound, false);
            EXPECT_EQ(*no, 8u) << "outbound next_seqnum should be 8 before reset";

            // Reset
            auto r = co_await store.reset();
            EXPECT_TRUE(r.has_value()) << "reset() failed";

            // After reset: counters should be 1 for both directions
            auto ni2 = co_await store.next_seqnum(direction_t::inbound, false);
            EXPECT_TRUE(ni2.has_value());
            EXPECT_EQ(*ni2, 1u) << "inbound counter must be 1 after reset";
            auto no2 = co_await store.next_seqnum(direction_t::outbound, false);
            EXPECT_TRUE(no2.has_value());
            EXPECT_EQ(*no2, 1u) << "outbound counter must be 1 after reset";

            // After reset: retrieve should find no frames (or empty)
            // We can store new frame at seq=1 to verify the slate is clean
            auto frame = fixpp::store_test::make_test_frame(1, direction_t::outbound);
            auto r2 =
                co_await store.store(1, std::span<const std::byte>(frame), direction_t::outbound);
            EXPECT_TRUE(r2.has_value()) << "storing seq=1 after reset must succeed";
        },
        asio::use_future);
    fut.get();
}

// Reset on empty store is idempotent
TEST(StoreResetMemory, ResetOnEmptyStoreIsIdempotent) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(
        pool.get_executor(),
        []() -> asio::awaitable<void> {
            auto store = make_memory_store();
            auto r = co_await store.reset();
            EXPECT_TRUE(r.has_value()) << "reset() on empty store must succeed";
            auto ni = co_await store.next_seqnum(direction_t::inbound, false);
            EXPECT_EQ(*ni, 1u);
        },
        asio::use_future);
    fut.get();
}

// Double reset is idempotent
TEST(StoreResetMemory, DoubleResetIsIdempotent) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(
        pool.get_executor(),
        []() -> asio::awaitable<void> {
            auto store = make_memory_store();
            auto script = make_store_script(3, direction_t::inbound);
            for (const auto& s : script) {
                co_await store.store(s.seq, std::span<const std::byte>(s.frame_bytes), s.dir);
            }
            auto r1 = co_await store.reset();
            EXPECT_TRUE(r1.has_value());
            auto r2 = co_await store.reset();
            EXPECT_TRUE(r2.has_value());
            auto ni = co_await store.next_seqnum(direction_t::inbound, false);
            EXPECT_EQ(*ni, 1u);
        },
        asio::use_future);
    fut.get();
}

// ── FileStore reset tests ────────────────────────────────────────────────────

FileStore::Config make_file_config(const fs::path& dir, asio::any_io_executor exec) {
    FileStore::Config cfg;
    cfg.directory = dir;
    cfg.sender_comp_id = "SENDER";
    cfg.target_comp_id = "TARGET";
    cfg.policy = {};  // commit_per_message
    cfg.max_frame_bytes = 4096;
    cfg.file_io_executor = exec;
    return cfg;
}

// FileStore happy-path reset: truncates log + rewinds counters
TEST(StoreResetFile, HappyPathClearsAndRewinds) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("file_happy");

    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&dir, &pool]() -> asio::awaitable<void> {
            FileStore::Config cfg = make_file_config(dir, pool.get_executor());
            FileStoreFactory factory{cfg};
            auto minted =
                factory.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
            EXPECT_TRUE(minted.has_value());
            if (!minted.has_value()) co_return;
            auto& store = *minted.value();

            // Store 5 outbound frames
            auto script = make_store_script(5, direction_t::outbound);
            for (const auto& step : script) {
                co_await store.store(step.seq, std::span<const std::byte>(step.frame_bytes),
                                     step.dir);
            }

            // Check counter before reset
            auto ns = co_await store.next_seqnum(direction_t::outbound, false);
            EXPECT_EQ(*ns, 6u);

            // Reset
            auto r = co_await store.reset();
            EXPECT_TRUE(r.has_value()) << "FileStore reset() failed";

            // After reset: counters = 1
            auto ni = co_await store.next_seqnum(direction_t::inbound, false);
            EXPECT_TRUE(ni.has_value());
            EXPECT_EQ(*ni, 1u);
            auto no = co_await store.next_seqnum(direction_t::outbound, false);
            EXPECT_TRUE(no.has_value());
            EXPECT_EQ(*no, 1u);
        },
        asio::use_future);
    fut.get();

    fixpp::store_test::remove_store_dir(dir);
}

// FileStore: after reset, a new open sees counter = 1
TEST(StoreResetFile, AfterResetNewOpenSeesCounterOne) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("file_reopen");

    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&dir, &pool]() -> asio::awaitable<void> {
            // Open, store, reset, close
            {
                FileStore::Config cfg = make_file_config(dir, pool.get_executor());
                FileStoreFactory factory{cfg};
                auto minted = factory.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024,
                                           pool.get_executor());
                EXPECT_TRUE(minted.has_value());
                if (!minted.has_value()) co_return;
                auto& store = *minted.value();

                auto script = make_store_script(3, direction_t::outbound);
                for (const auto& step : script) {
                    co_await store.store(step.seq, std::span<const std::byte>(step.frame_bytes),
                                         step.dir);
                }
                co_await store.reset();
                // store goes out of scope = file released
            }

            // Re-open: should see counter=1
            FileStore::Config cfg2 = make_file_config(dir, pool.get_executor());
            FileStoreFactory factory2{cfg2};
            auto minted2 =
                factory2.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
            EXPECT_TRUE(minted2.has_value()) << "re-open after reset failed";
            if (!minted2.has_value()) co_return;
            auto& store2 = *minted2.value();

            auto ni = co_await store2.next_seqnum(direction_t::inbound, false);
            EXPECT_TRUE(ni.has_value());
            EXPECT_EQ(*ni, 1u) << "inbound counter must be 1 after reset+reopen";
            auto no = co_await store2.next_seqnum(direction_t::outbound, false);
            EXPECT_TRUE(no.has_value());
            EXPECT_EQ(*no, 1u) << "outbound counter must be 1 after reset+reopen";
        },
        asio::use_future);
    fut.get();

    fixpp::store_test::remove_store_dir(dir);
}

// RC#2 regression: Reset_ThenStore_ThenReopen_RetrieveSucceeds
//
// Before RC#2 the reset() path set write_pos=0 after initialise_fresh(),
// causing the next store() to overwrite the sentinel byte at offset 0 and
// corrupt the log. On the subsequent reopen, restart_scan() would fail (wrong
// magic at sentinel) and return store_factory_failed.
//
// With the fix, write_pos is set to the correct post-fresh-init tail offset,
// so store(seq=1) appends AFTER the sentinel+counter records, and the log is
// correctly recovered on the next open.
TEST(StoreResetFile, ResetThenStoreThenReopenRetrieveSucceeds) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("file_rc2_regression");

    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&dir, &pool]() -> asio::awaitable<void> {
            // Phase 1: open, store 3 frames, reset, then store 1 frame, close.
            {
                FileStore::Config cfg = make_file_config(dir, pool.get_executor());
                FileStoreFactory factory{cfg};
                auto minted = factory.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024,
                                           pool.get_executor());
                EXPECT_TRUE(minted.has_value()) << "Phase 1 open failed";
                if (!minted.has_value()) co_return;
                auto& store = *minted.value();

                // Store 3 outbound frames (seqnums 1,2,3)
                auto script = make_store_script(3, direction_t::outbound);
                for (const auto& step : script) {
                    auto r = co_await store.store(
                        step.seq, std::span<const std::byte>(step.frame_bytes), step.dir);
                    EXPECT_TRUE(r.has_value()) << "pre-reset store() failed at seq=" << step.seq;
                    if (!r.has_value()) co_return;
                }

                // Reset: the log is replaced with a fresh sentinel+counter file.
                auto rr = co_await store.reset();
                EXPECT_TRUE(rr.has_value()) << "reset() failed";
                if (!rr.has_value()) co_return;

                // Post-reset store: seq=1 (counters rewound). Without RC#2 fix
                // this write overwrites the sentinel at offset 0.
                auto frame1 = fixpp::store_test::make_test_frame(1, direction_t::outbound);
                auto rs = co_await store.store(1, std::span<const std::byte>(frame1),
                                               direction_t::outbound);
                EXPECT_TRUE(rs.has_value()) << "post-reset store(seq=1) failed";
                if (!rs.has_value()) co_return;
                // store goes out of scope: advisory lock released
            }

            // Phase 2: reopen the same log — restart_scan() must succeed.
            // Without the RC#2 fix, the sentinel at offset 0 is overwritten by the
            // post-reset store() and restart_scan() returns false → factory_failed.
            FileStore::Config cfg2 = make_file_config(dir, pool.get_executor());
            FileStoreFactory factory2{cfg2};
            auto minted2 =
                factory2.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
            EXPECT_TRUE(minted2.has_value()) << "Phase 2 reopen failed (sentinel corrupt?)";
            if (!minted2.has_value()) co_return;
            auto& store2 = *minted2.value();

            // Phase 3: retrieve(1, 0, outbound) — must see exactly frame seq=1.
            struct collecting_vis final : public fixpp::session::retrieve_visitor {
                std::vector<fixpp::session::seqnum_t> seqs;
                asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>> on_frame(
                    fixpp::session::seqnum_t s, std::span<const std::byte>) noexcept override {
                    seqs.push_back(s);
                    co_return fixpp::core::expected_t<fixpp::session::visit_result>{
                        fixpp::session::visit_result::cont};
                }
            } vis;
            auto rv = co_await store2.retrieve(1, 0, direction_t::outbound, vis);
            EXPECT_TRUE(rv.has_value()) << "retrieve() after reset+reopen failed";
            EXPECT_EQ(vis.seqs.size(), 1u) << "expected 1 frame after reset+store(1)+reopen";
            if (!vis.seqs.empty()) EXPECT_EQ(vis.seqs[0], 1u) << "frame seqnum must be 1";
        },
        asio::use_future);
    fut.get();

    fixpp::store_test::remove_store_dir(dir);
}

// ── N2 regression: parent-dir fsync failure is fatal (gate-b/r2) ─────────────
//
// [2e §6.3.5] mandates that Linux parent-dir fsync after atomic-rename is
// MANDATORY — not best-effort. If the parent directory cannot be opened,
// reset() must return store_io_failure rather than silently succeeding.
//
// Strategy: after creating a valid store, revoke read permission on the
// parent directory before calling reset(). ::open(dir, O_RDONLY|O_DIRECTORY)
// will fail with EACCES, and reset() must propagate store_io_failure.
//
// Skipped when running as root (root bypasses POSIX file-permission checks).
#ifndef _WIN32
TEST(StoreResetFile, N2_DirOpenFailureReturnsFatalError) {
    // Skip if root — root ignores permission bits.
    if (::geteuid() == 0) {
        GTEST_SKIP() << "Skipped: root bypasses POSIX permission checks";
    }

    asio::thread_pool pool{2};
    // Create a dedicated sub-directory so we can chmod it without breaking
    // access to the temp root.
    auto outer = unique_store_dir("n2_dir_fsync");
    auto inner = outer / "store";
    std::filesystem::create_directories(inner);

    bool reset_returned_failure = false;

    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&]() -> asio::awaitable<void> {
            FileStore::Config cfg = make_file_config(inner, pool.get_executor());
            FileStoreFactory factory{cfg};
            auto minted =
                factory.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
            EXPECT_TRUE(minted.has_value()) << "initial open failed";
            if (!minted.has_value()) co_return;
            auto& store = *minted.value();

            // Store a frame so the log has content.
            auto frame = fixpp::store_test::make_test_frame(1, direction_t::outbound);
            auto sr =
                co_await store.store(1, std::span<const std::byte>(frame), direction_t::outbound);
            EXPECT_TRUE(sr.has_value());
            if (!sr.has_value()) co_return;

            // Revoke read + execute on the parent directory of the log.
            // ::open(dir, O_RDONLY | O_DIRECTORY) will fail with EACCES.
            // The store's log lives inside `inner`; revoke on `inner`.
            ::chmod(inner.c_str(), 0000);

            // reset() must return store_io_failure (dir-open fails → fatal).
            auto r = co_await store.reset();

            // Restore permissions before any EXPECT (so cleanup doesn't fail).
            ::chmod(inner.c_str(), 0755);

            reset_returned_failure =
                !r.has_value() && r.error() == fixpp::core::error::store_io_failure;
        },
        asio::use_future);
    fut.get();

    EXPECT_TRUE(reset_returned_failure)
        << "N2: reset() must return store_io_failure when parent-dir open fails";

    fixpp::store_test::remove_store_dir(outer);
}
#endif  // !_WIN32

}  // namespace
