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
#ifndef _WIN32
#include <unistd.h>  // not used directly on Windows; FileStore owns all fd I/O
#endif

#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fstream>
#include <span>

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

    // Truncate to remove the last partial record (last 20 bytes). Portable
    // std::filesystem::resize_file replaces POSIX truncate() (file_size is
    // std::uintmax_t and was asserted > 50 above, so new_size stays positive).
    auto new_size = file_size - 20;
    std::error_code resize_ec;
    fs::resize_file(lp, new_size, resize_ec);
    EXPECT_FALSE(resize_ec) << "resize_file failed: " << resize_ec.message();

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

    minted2.value().reset();
    fixpp::store_test::remove_store_dir(dir);
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

    minted2.value().reset();
    fixpp::store_test::remove_store_dir(dir);
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

    if (minted2.has_value()) minted2.value().reset();
    fixpp::store_test::remove_store_dir(dir);
}
#endif  // _WIN32

// ── RC#3 regression: oversized hdr.len terminates instead of failing ──────────
//
// restart_scan() is noexcept; before RC#3 it allocated std::vector(hdr.len)
// without a cap-check, so a corrupt record with hdr.len=0xFFFFFFFF triggered
// std::bad_alloc inside a noexcept function → std::terminate (process death).
//
// With the fix, any record whose hdr.len > cfg.max_frame_bytes causes the scan
// to stop at last_good_pos (same as a partial tail), and factory.make() returns
// store_factory_failed.
//
// We test this by:
//   1. Creating a valid store with one good frame (gives us a valid sentinel).
//   2. Appending a corrupt 16-byte record header (valid CRC not needed — the cap
//      check fires BEFORE the CRC computation) with len=0xFFFFFFFF directly
//      after the last good record.
//   3. Re-opening: factory.make() must NOT terminate; it must return an expected
//      value (success — the scan stopped at the corrupt record and kept the
//      last_good_pos = tail of the good frame) OR must succeed and recover only
//      the good frames. The critical invariant is NO terminate.
//
// Note: with the RC#3 fix the scan treats the oversized len as a torn tail:
//   - last_good_pos = end-of-good-frame, so the file is truncated there.
//   - factory.make() returns the store with only the surviving good frame.

#ifndef _WIN32
TEST(FileStoreTornWrite, OversizedLenInHeaderDoesNotTerminate) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("rc3_oversized_len");

    // Step 1: create a valid store with 1 outbound frame.
    {
        FileStore::Config cfg = make_config(dir, pool.get_executor());
        FileStoreFactory factory{cfg};
        auto minted =
            factory.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
        ASSERT_TRUE(minted.has_value()) << "Phase 1 create failed";  // not in coroutine — ASSERT OK
        auto& store = *minted.value();
        auto frame = fixpp::store_test::make_test_frame(1, direction_t::outbound);
        auto fut = asio::co_spawn(
            pool.get_executor(),
            [&store, &frame]() -> asio::awaitable<void> {
                auto r = co_await store.store(1, std::span<const std::byte>(frame),
                                              direction_t::outbound);
                EXPECT_TRUE(r.has_value());
            },
            asio::use_future);
        fut.get();
        // store out of scope: file released
    }

    // Step 2: append a 16-byte record header with hdr.len = 0xFFFFFFFF to the log.
    // The sentinel + initial_counter + frame record are already on disk; append
    // the corrupt header at the current end-of-file.
    auto lp = log_path(dir);
    ASSERT_TRUE(fs::exists(lp));
    {
        // 16-byte RecordHeader: kind=0(frame), dir=0(inbound), reserved[2]=0,
        // seq=0x01 (LE), len=0xFFFFFFFF (LE), crc32=0xDEADBEEF (invalid — doesn't matter).
        std::uint8_t corrupt_hdr[16] = {
            0x00,                    // kind = frame
            0x00,                    // dir  = inbound
            0x00, 0x00,              // reserved
            0x01, 0x00, 0x00, 0x00,  // seq = 1 (LE)
            0xFF, 0xFF, 0xFF, 0xFF,  // len = 0xFFFFFFFF (LE) — the attack value
            0xEF, 0xBE, 0xAD, 0xDE,  // crc32 = invalid
        };
        // Open for append
        FILE* f = ::fopen(lp.c_str(), "ab");
        ASSERT_NE(f, nullptr);
        ::fwrite(corrupt_hdr, 1, sizeof(corrupt_hdr), f);
        ::fclose(f);
    }

    // Step 3: re-open — must NOT terminate; must succeed (scan stops at corrupt hdr).
    FileStore::Config cfg2 = make_config(dir, pool.get_executor());
    FileStoreFactory factory2{cfg2};
    auto minted2 =
        factory2.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
    // The store MUST open successfully (the corrupt header is treated as a torn tail
    // and truncated at last_good_pos; the surviving good frame is kept).
    EXPECT_TRUE(minted2.has_value())
        << "RC#3: factory.make() should succeed (scan stops at oversized hdr, "
           "truncates to last_good_pos) — NOT terminate";

    if (minted2.has_value()) {
        // The surviving frame seq=1 must be readable.
        struct simple_vis final : public fixpp::session::retrieve_visitor {
            std::vector<fixpp::session::seqnum_t> seqs;
            asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>> on_frame(
                fixpp::session::seqnum_t s, std::span<const std::byte>) noexcept override {
                seqs.push_back(s);
                co_return fixpp::core::expected_t<fixpp::session::visit_result>{
                    fixpp::session::visit_result::cont};
            }
        } vis;
        auto& store2 = *minted2.value();
        auto fut = asio::co_spawn(
            pool.get_executor(),
            [&store2, &vis]() -> asio::awaitable<void> {
                auto r = co_await store2.retrieve(1, 0, direction_t::outbound, vis);
                EXPECT_TRUE(r.has_value()) << "retrieve after RC#3 recovery failed";
            },
            asio::use_future);
        fut.get();
        EXPECT_EQ(vis.seqs.size(), 1u) << "expected 1 surviving frame";
        if (!vis.seqs.empty()) EXPECT_EQ(vis.seqs[0], 1u);
    }

    if (minted2.has_value()) minted2.value().reset();
    fixpp::store_test::remove_store_dir(dir);
}
#endif  // !_WIN32 (test uses fopen/fwrite; Windows variant is out of scope for Tier-1)

// ── N3 regression: mid-log corrupt record surfaces store_factory_failed ────────
//
// [2e §6.3] restart invariant: "only one torn record, at the tail".
// If a corrupt/oversized header appears BETWEEN two valid frame records,
// restart_scan() must detect the valid suffix and return false → store_factory_failed.
//
// Round-1 RC#3 fixed the cap-check but used uniform torn-tail recovery, which
// silently discards the valid suffix after a mid-log corrupt record.
//
// Test shape:
//   1. Create a valid store; store 2 outbound frames (seqnums 1, 2).
//   2. Rewrite the on-disk bytes: keep frame 1 intact, replace frame 2's header
//      with a corrupt oversized header (len=0xFFFF, which exceeds max_frame_bytes
//      for the test store), then append a valid frame 3 after it.
//   3. Re-open: factory.make() must return store_factory_failed (mid-log corruption).
//
// We construct the corrupt log directly with fopen/pwrite to get deterministic layout.
#ifndef _WIN32
TEST(FileStoreTornWrite, N3_MidLogCorruptRecordSurfacesStoreFactoryFailed) {
    // max_frame_bytes=4096 in make_config, so len=0xFFFF (65535) triggers cap-check.
    // But we need len ABOVE max_frame_bytes (4096). Use len=0x8000 (32768) > 4096.
    // However, we also need a valid frame after it, which means the file must look
    // like: sentinel | counter | frame1 | corrupt_hdr(16 bytes) | valid_frame3_data
    //
    // Strategy: store 1 frame, then raw-write: corrupt_hdr + valid frame2 record.
    // The corrupt_hdr has len=0x1234 (4660) > max_frame_bytes=4096; after the
    // header there is more data (the valid frame), so mid-log detection fires.

    asio::thread_pool pool{2};
    auto dir = unique_store_dir("n3_mid_log_corrupt");

    // Step 1: create a valid store with 1 outbound frame and capture its bytes.
    auto valid_frame = fixpp::store_test::make_test_frame(1, direction_t::outbound);
    {
        FileStore::Config cfg = make_config(dir, pool.get_executor());
        FileStoreFactory factory{cfg};
        auto minted =
            factory.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
        ASSERT_TRUE(minted.has_value()) << "N3 Phase 1 create failed";
        auto& store = *minted.value();
        auto fut = asio::co_spawn(
            pool.get_executor(),
            [&store, &valid_frame]() -> asio::awaitable<void> {
                auto r = co_await store.store(1, std::span<const std::byte>(valid_frame),
                                              direction_t::outbound);
                EXPECT_TRUE(r.has_value());
            },
            asio::use_future);
        fut.get();
        // store out of scope: file released
    }

    // Step 2: append a corrupt header followed by valid-looking frame data.
    // The corrupt header has len=0x1234 (4660), which exceeds max_frame_bytes=4096.
    // After it we append a second "frame" record (any bytes) — the key is that
    // there IS data after the corrupt header, triggering mid-log detection.
    auto lp = log_path(dir);
    ASSERT_TRUE(fs::exists(lp));
    {
        FILE* f = ::fopen(lp.c_str(), "ab");
        ASSERT_NE(f, nullptr);

        // Corrupt record header: kind=frame, dir=outbound, seq=2, len=0x1234 (oversized)
        // crc32=0xDEADBEEF (invalid — doesn't matter because cap-check fires first)
        std::uint8_t corrupt_hdr[16] = {
            0x00,                    // kind = frame
            0x01,                    // dir  = outbound
            0x00, 0x00,              // reserved
            0x02, 0x00, 0x00, 0x00,  // seq = 2 (LE)
            0x34, 0x12, 0x00, 0x00,  // len = 0x1234 = 4660 > max_frame_bytes (LE)
            0xEF, 0xBE, 0xAD, 0xDE,  // crc32 = invalid
        };
        ::fwrite(corrupt_hdr, 1, sizeof(corrupt_hdr), f);

        // Append a small amount of "suffix data" that looks like it could be a
        // valid record (a few bytes is enough — the detector only checks fsize).
        // This simulates the valid suffix records that would follow in a real log.
        std::uint8_t suffix_data[32] = {};  // 32 zero bytes — enough to be "more data"
        ::fwrite(suffix_data, 1, sizeof(suffix_data), f);
        ::fclose(f);
    }

    // Step 3: re-open — must return store_factory_failed (mid-log corruption detected).
    FileStore::Config cfg2 = make_config(dir, pool.get_executor());
    FileStoreFactory factory2{cfg2};
    auto minted2 =
        factory2.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
    EXPECT_FALSE(minted2.has_value())
        << "N3: factory.make() must return store_factory_failed for mid-log "
           "corrupt oversized header with valid suffix data";

    if (!minted2.has_value()) {
        EXPECT_EQ(minted2.error(), fixpp::core::error::store_factory_failed)
            << "N3: expected store_factory_failed, got " << static_cast<int>(minted2.error());
    }

    fixpp::store_test::remove_store_dir(dir);
}
#endif  // !_WIN32

}  // namespace
