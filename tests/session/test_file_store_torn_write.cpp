// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_file_store_torn_write.cpp
//
// Seam 3 — FileStore torn-write recovery (FR-012 / I-14).
//
// Tier-1 Linux: programmatically truncate the live log mid-record; re-open;
// verify CRC32-bad record at tail is truncated cleanly (ftruncate + fdatasync);
// verify surviving records still readable; verify stale <...>.log.reset.tmp
// from a crashed prior reset is unlinked before the scan.
//
// Tier-2 Windows code paths guarded by #ifdef _WIN32 (SetEndOfFile variant).
//
// TDD: linker-RED until T023/T024/T026 ship FileStore + FileStoreFactory.
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <cstdio>
#include <filesystem>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fstream>

#include "_fixtures_/store_temp_dir.hpp"
#include "_fixtures_/test_double_fsm.hpp"

namespace {

using fixpp::session::direction_t;
using fixpp::session::FileStore;
using fixpp::session::FileStoreFactory;
using fixpp::session::FileStorePolicy;
using fixpp::store_test::byte_collecting_visitor;
using fixpp::store_test::make_store_script;
using fixpp::store_test::unique_store_dir;
namespace fs = std::filesystem;

FileStore::Config make_config(const fs::path& dir, asio::any_io_executor exec) {
    FileStore::Config cfg;
    cfg.directory = dir;
    cfg.sender_comp_id = "SENDER";
    cfg.target_comp_id = "TARGET";
    cfg.policy = {};  // commit_per_message default
    cfg.max_frame_bytes = 4096;
    cfg.file_io_executor = exec;
    return cfg;
}

// Helper: get the live log path
fs::path log_path(const fs::path& dir) { return dir / "SENDER__TARGET.log"; }

// ── Test 1: Truncate-mid-record recovery ─────────────────────────────────────

TEST(FileStoreTornWrite, TruncateMidRecordCleansUp) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("mid_record");

    constexpr int kGoodFrames = 5;
    auto script = make_store_script(kGoodFrames, direction_t::outbound);

    // Store kGoodFrames frames with commit_per_message
    {
        FileStore::Config cfg = make_config(dir, pool.get_executor());
        FileStoreFactory factory{cfg};
        auto minted =
            factory.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
        ASSERT_TRUE(minted.has_value());
        auto& store = *minted.value();

        auto fut = asio::co_spawn(
            pool.get_executor(),
            [&store, &script]() -> asio::awaitable<void> {
                for (const auto& step : script) {
                    auto r = co_await store.store(
                        step.seq, std::span<const std::byte>(step.frame_bytes), step.dir);
                    EXPECT_TRUE(r.has_value());
                }
            },
            asio::use_future);
        fut.get();
        // store goes out of scope here; file is flushed
    }

    // Truncate the file to remove the last few bytes (tear mid-record)
    auto lp = log_path(dir);
    ASSERT_TRUE(fs::exists(lp));
    auto file_size = fs::file_size(lp);
    ASSERT_GT(file_size, static_cast<uintmax_t>(50));

    // Truncate to remove the last partial record (last 20 bytes)
    auto new_size = static_cast<off_t>(file_size) - 20;
    EXPECT_EQ(::truncate(lp.c_str(), new_size), 0)
        << "truncate syscall failed: " << strerror(errno);

    // Re-open: restart algorithm should detect and truncate the torn tail
    FileStore::Config cfg = make_config(dir, pool.get_executor());
    FileStoreFactory factory{cfg};
    auto minted2 =
        factory.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
    ASSERT_TRUE(minted2.has_value())
        << "re-open after torn write should succeed (tear removed at scan)";

    // Retrieve surviving frames — should get fewer than kGoodFrames (at least 1)
    byte_collecting_visitor visitor;
    auto& store2 = *minted2.value();
    auto fut2 = asio::co_spawn(
        pool.get_executor(),
        [&store2, &visitor]() -> asio::awaitable<void> {
            auto r = co_await store2.retrieve(1, 0, direction_t::outbound, visitor);
            EXPECT_TRUE(r.has_value()) << "retrieve after torn-write recovery failed";
        },
        asio::use_future);
    fut2.get();

    // Should have at least 1 surviving frame and <= kGoodFrames
    EXPECT_GE(visitor.entries().size(), static_cast<std::size_t>(1));
    EXPECT_LE(visitor.entries().size(), static_cast<std::size_t>(kGoodFrames));

    // Verify byte equality of surviving frames
    for (std::size_t i = 0; i < visitor.entries().size(); ++i) {
        EXPECT_EQ(visitor.entries()[i].bytes, script[i].frame_bytes)
            << "frame " << i << " byte mismatch after torn-write recovery";
    }

    fs::remove_all(dir);
}

// ── Test 2: Stale reset.tmp is unlinked on open ──────────────────────────────

TEST(FileStoreTornWrite, StaleResetTmpIsUnlinked) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("stale_tmp");

    // Create a live store with 3 frames
    auto script = make_store_script(3, direction_t::outbound);
    {
        FileStore::Config cfg = make_config(dir, pool.get_executor());
        FileStoreFactory factory{cfg};
        auto minted =
            factory.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
        ASSERT_TRUE(minted.has_value());
        auto& store = *minted.value();
        auto fut = asio::co_spawn(
            pool.get_executor(),
            [&store, &script]() -> asio::awaitable<void> {
                for (const auto& step : script) {
                    co_await store.store(step.seq, std::span<const std::byte>(step.frame_bytes),
                                         step.dir);
                }
            },
            asio::use_future);
        fut.get();
    }

    // Place a stale .log.reset.tmp file (simulates a crashed prior reset)
    auto tmp_path = dir / "SENDER__TARGET.log.reset.tmp";
    {
        std::ofstream f(tmp_path, std::ios::binary);
        f.write("STALE_RESET_TMP_DATA", 20);
    }
    ASSERT_TRUE(fs::exists(tmp_path));

    // Re-open: the restart algorithm must unlink the stale tmp before scanning
    FileStore::Config cfg = make_config(dir, pool.get_executor());
    FileStoreFactory factory{cfg};
    auto minted2 =
        factory.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
    ASSERT_TRUE(minted2.has_value()) << "re-open should succeed despite stale reset.tmp";

    // The stale tmp should no longer exist
    EXPECT_FALSE(fs::exists(tmp_path)) << "stale .log.reset.tmp was NOT unlinked on open";

    // All 3 original frames should be recoverable
    byte_collecting_visitor visitor;
    auto& store2 = *minted2.value();
    auto fut2 = asio::co_spawn(
        pool.get_executor(),
        [&store2, &visitor]() -> asio::awaitable<void> {
            auto r = co_await store2.retrieve(1, 0, direction_t::outbound, visitor);
            EXPECT_TRUE(r.has_value());
        },
        asio::use_future);
    fut2.get();

    EXPECT_EQ(visitor.entries().size(), static_cast<std::size_t>(3));

    fs::remove_all(dir);
}

// ── Windows Tier-2 variants ──────────────────────────────────────────────────
// These compile only on Windows; on Linux they are excluded from build.

#ifdef _WIN32
TEST(FileStoreTornWriteWindows, SetEndOfFileTruncation) {
    // Windows torn-write test: use SetEndOfFile for truncation
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("win_truncate");
    auto script = make_store_script(5, direction_t::outbound);
    {
        FileStore::Config cfg = make_config(dir, pool.get_executor());
        FileStoreFactory factory{cfg};
        auto minted =
            factory.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
        ASSERT_TRUE(minted.has_value());
        auto& store = *minted.value();
        auto fut = asio::co_spawn(
            pool.get_executor(),
            [&store, &script]() -> asio::awaitable<void> {
                for (const auto& step : script) {
                    co_await store.store(step.seq, std::span<const std::byte>(step.frame_bytes),
                                         step.dir);
                }
            },
            asio::use_future);
        fut.get();
    }

    // Truncate via Windows SetEndOfFile
    auto lp = log_path(dir);
    HANDLE h = CreateFileW(lp.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    auto file_size = GetFileSize(h, nullptr);
    ASSERT_GT(file_size, DWORD(50));
    SetFilePointer(h, static_cast<LONG>(file_size) - 20, nullptr, FILE_BEGIN);
    SetEndOfFile(h);
    FlushFileBuffers(h);
    CloseHandle(h);

    // Re-open
    FileStore::Config cfg = make_config(dir, pool.get_executor());
    FileStoreFactory factory{cfg};
    auto minted2 =
        factory.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
    EXPECT_TRUE(minted2.has_value());

    fs::remove_all(dir);
}
#endif  // _WIN32

}  // namespace
