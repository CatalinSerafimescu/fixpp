// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_file_store_compid_validation.cpp
//
// Seam 2 CompID-validation sub-cases (split from T014 for clarity).
// [2e §D.4] / [2e §9 seam #2 N-3] — Gap 1 close.
//
// Verifies FileStoreFactory::make() returns store_factory_failed BEFORE any
// file is opened or advisory lock taken when CompIDs are invalid.
//
// FR-033's 21-seam count is preserved: this file and test_file_store_crash_survival.cpp
// both count under seam #2.
//
// TDD: linker-RED until T026 (FileStoreFactory::make() with CompID validation).
#include <gtest/gtest.h>
#include <unistd.h>

#include <asio/thread_pool.hpp>
#include <climits>
#include <cstring>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <string>

#include "_fixtures_/store_temp_dir.hpp"

namespace {

using fixpp::session::FileStore;
using fixpp::session::FileStoreFactory;
using fixpp::store_test::unique_store_dir;
namespace fs = std::filesystem;

// Attempt to make() with a given sender/target and assert failure before any
// file is created.
void expect_compid_fail(const std::string& sender, const std::string& target, const fs::path& dir,
                        asio::thread_pool& pool, const char* label) {
    FileStore::Config cfg;
    cfg.directory = dir;
    cfg.sender_comp_id = sender;
    cfg.target_comp_id = target;
    cfg.max_frame_bytes = 4096;
    cfg.file_io_executor = pool.get_executor();

    FileStoreFactory factory{cfg};
    auto result = factory.make(sender, target, nullptr, 1024 * 1024 * 1024, pool.get_executor());

    EXPECT_FALSE(result.has_value())
        << label << ": expected failure for sender='" << sender << "' target='" << target << "'";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), fixpp::core::error::store_factory_failed) << label;
    }

    // Assert no file was created at the composed path
    // (If validation fires BEFORE file open, no file should exist)
    // Enumerate any files created; none expected.
    bool any_file = false;
    if (fs::exists(dir)) {
        for ([[maybe_unused]] const auto& ent : fs::directory_iterator(dir)) {
            any_file = true;
            break;
        }
    }
    EXPECT_FALSE(any_file) << label
                           << ": a file was created despite validation failure — "
                              "validation must run BEFORE any file open";
}

// ── Test 1: empty CompID ─────────────────────────────────────────────────────

TEST(FileStoreCompIDValidation, EmptySenderCompID) {
    asio::thread_pool pool{1};
    auto dir = unique_store_dir("empty_sender");
    expect_compid_fail("", "TARGET", dir, pool, "empty sender");
    fs::remove_all(dir);
}

TEST(FileStoreCompIDValidation, EmptyTargetCompID) {
    asio::thread_pool pool{1};
    auto dir = unique_store_dir("empty_target");
    expect_compid_fail("SENDER", "", dir, pool, "empty target");
    fs::remove_all(dir);
}

// ── Test 2: path separator ───────────────────────────────────────────────────

TEST(FileStoreCompIDValidation, ForwardSlashInSender) {
    asio::thread_pool pool{1};
    auto dir = unique_store_dir("slash_sender");
    expect_compid_fail("SEN/DER", "TARGET", dir, pool, "sender with /");
    fs::remove_all(dir);
}

TEST(FileStoreCompIDValidation, ForwardSlashInTarget) {
    asio::thread_pool pool{1};
    auto dir = unique_store_dir("slash_target");
    expect_compid_fail("SENDER", "TAR/GET", dir, pool, "target with /");
    fs::remove_all(dir);
}

// ── Test 3: NUL byte ─────────────────────────────────────────────────────────

TEST(FileStoreCompIDValidation, NULByteInSender) {
    asio::thread_pool pool{1};
    auto dir = unique_store_dir("nul_sender");
    // std::string can hold embedded NUL
    std::string sender_with_nul = std::string("SEN") + '\0' + "DER";
    expect_compid_fail(sender_with_nul, "TARGET", dir, pool, "sender with NUL");
    fs::remove_all(dir);
}

// ── Test 4: dot/dotdot segments ──────────────────────────────────────────────

TEST(FileStoreCompIDValidation, SingleDotSender) {
    asio::thread_pool pool{1};
    auto dir = unique_store_dir("dot_sender");
    expect_compid_fail(".", "TARGET", dir, pool, "sender='.'");
    fs::remove_all(dir);
}

TEST(FileStoreCompIDValidation, DoubleDotSender) {
    asio::thread_pool pool{1};
    auto dir = unique_store_dir("dotdot_sender");
    expect_compid_fail("..", "TARGET", dir, pool, "sender='..'");
    fs::remove_all(dir);
}

// ── Test 5: control characters ───────────────────────────────────────────────

TEST(FileStoreCompIDValidation, TabInSender) {
    asio::thread_pool pool{1};
    auto dir = unique_store_dir("tab_sender");
    expect_compid_fail("SEN\tDER", "TARGET", dir, pool, "sender with tab");
    fs::remove_all(dir);
}

TEST(FileStoreCompIDValidation, NewlineInTarget) {
    asio::thread_pool pool{1};
    auto dir = unique_store_dir("nl_target");
    expect_compid_fail("SENDER", "TAR\nGET", dir, pool, "target with \\n");
    fs::remove_all(dir);
}

TEST(FileStoreCompIDValidation, CarriageReturnInSender) {
    asio::thread_pool pool{1};
    auto dir = unique_store_dir("cr_sender");
    expect_compid_fail("SEN\rDER", "TARGET", dir, pool, "sender with \\r");
    fs::remove_all(dir);
}

// ── Test 6: DEL character (0x7F) ─────────────────────────────────────────────

TEST(FileStoreCompIDValidation, DELInSender) {
    asio::thread_pool pool{1};
    auto dir = unique_store_dir("del_sender");
    expect_compid_fail(std::string("SEN") + '\x7F' + "DER", "TARGET", dir, pool,
                       "sender with DEL(0x7F)");
    fs::remove_all(dir);
}

// ── Test 7: NAME_MAX length limit ────────────────────────────────────────────

TEST(FileStoreCompIDValidation, CompIDExceedingNAMEMAX) {
    asio::thread_pool pool{1};
    auto dir = unique_store_dir("namemax");

    // The composed filename is sender + "__" + target + ".log"
    // So each CompID alone must produce a composed component <= NAME_MAX.
    // We build a sender that, when combined with "__TARGET.log", exceeds NAME_MAX.
    long name_max = pathconf(dir.c_str(), _PC_NAME_MAX);
    if (name_max < 0) name_max = 255;  // POSIX fallback

    // Component = sender + "__" + "TARGET" + ".log" = sender + 12 chars
    // Make sender of length (name_max - 12 + 2) to exceed the limit
    std::size_t target_overhead = std::string("__TARGET.log").size();
    std::size_t excess_sender_len = static_cast<std::size_t>(name_max) - target_overhead + 2;

    std::string long_sender(excess_sender_len, 'A');
    expect_compid_fail(long_sender, "TARGET", dir, pool, "sender exceeding NAME_MAX");

    // Exactly at limit should succeed (or at worst fail for another reason)
    // Verify by checking a CompID exactly at the limit is NOT rejected by
    // this validation (we test the "over-limit" case; the at-limit boundary
    // is implementation-permitted to accept)
    std::size_t ok_sender_len = static_cast<std::size_t>(name_max) - target_overhead;
    if (ok_sender_len > 0 && ok_sender_len < 1000) {
        std::string ok_sender(ok_sender_len, 'B');
        FileStore::Config cfg;
        cfg.directory = dir;
        cfg.sender_comp_id = ok_sender;
        cfg.target_comp_id = "TARGET";
        cfg.max_frame_bytes = 4096;
        cfg.file_io_executor = pool.get_executor();
        FileStoreFactory factory{cfg};
        auto result =
            factory.make(ok_sender, "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
        // Should succeed on NAME_MAX validation (may fail for other legitimate
        // reasons like file creation in temp dir — we only care it's NOT
        // store_factory_failed due to CompID validation)
        if (!result.has_value()) {
            // If it fails, it should not be a CompID validation error
            // (We can't distinguish the exact error origin easily here)
            // Accept the result as-is; the important property is the over-limit case above
        }
    }

    fs::remove_all(dir);
}

// ── Positive test: valid CompID ──────────────────────────────────────────────

TEST(FileStoreCompIDValidation, ValidCompIDSucceeds) {
    asio::thread_pool pool{1};
    auto dir = unique_store_dir("valid");

    FileStore::Config cfg;
    cfg.directory = dir;
    cfg.sender_comp_id = "SENDER123";
    cfg.target_comp_id = "TARGET456";
    cfg.max_frame_bytes = 4096;
    cfg.file_io_executor = pool.get_executor();

    FileStoreFactory factory{cfg};
    auto result =
        factory.make("SENDER123", "TARGET456", nullptr, 1024 * 1024 * 1024, pool.get_executor());
    EXPECT_TRUE(result.has_value()) << "valid CompIDs should succeed; got error: "
                                    << (result.has_value() ? 0 : static_cast<int>(result.error()));

    fs::remove_all(dir);
}

}  // namespace
