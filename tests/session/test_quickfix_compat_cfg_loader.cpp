// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_quickfix_compat_cfg_loader.cpp
//
// Seam 12 — cfg_loader round-trip (FR-008 / FR-030 / AC US4 #1 / SC-009 /
// [2e §D.4] defense-in-depth mirror).
// Task T045.
//
// Test structure:
//   Happy path (1):
//     - Write a minimal QuickFIX .cfg with [DEFAULT]\nFileStorePath=<tmpdir>
//       + SenderCompID=SENDER + TargetCompID=TARGET.
//     - cfg_to_file_store_factory() returns a valid FileStoreFactory.
//     - The factory's Config.directory matches <tmpdir>.
//     - Mint a store via factory->make(), round-trip one frame, verify byte-equality.
//
//   Defense-in-depth sub-cases (5): each writes a CFG with a poisoned CompID
//   value and asserts cfg_to_file_store_factory() returns
//   expected_t::unexpected{store_factory_failed} at config-load time.

#include <gtest/gtest.h>

#include <array>
#include <climits>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>   // pathconf, _PC_NAME_MAX
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>

#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/quickfix_compat/cfg_loader.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>

namespace {

namespace fs = std::filesystem;
using fixpp::core::error;
using fixpp::session::direction_t;
using fixpp::session::FileStoreFactory;
using fixpp::session::seqnum_t;
using fixpp::session::visit_result;
using fixpp::session::quickfix_compat::cfg_to_file_store_factory;

// ── Fixture helpers ───────────────────────────────────────────────────────────

// Create a unique scratch directory for a test.
fs::path make_scratch_dir(const char* suffix) {
    auto p = fs::temp_directory_path() / (std::string("fixpp_cfgloader_") + suffix);
    fs::create_directories(p);
    return p;
}

// Write a QuickFIX .cfg snippet to a file and return the path.
fs::path write_cfg(const fs::path& scratch,
                   const std::string& content,
                   const char* name = "test.cfg") {
    fs::path p = scratch / name;
    std::ofstream ofs(p, std::ios::trunc);
    ofs << content;
    return p;
}

// ── A minimal collect visitor for round-trip verification ─────────────────────

class CollectVisitor : public fixpp::session::retrieve_visitor {
public:
    std::vector<std::vector<std::byte>> frames;

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>>
    on_frame(seqnum_t /*seq*/,
             std::span<const std::byte> data) noexcept override {
        frames.emplace_back(data.begin(), data.end());
        co_return fixpp::session::visit_result::cont;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Happy path
// ─────────────────────────────────────────────────────────────────────────────

TEST(CfgLoaderHappyPath, ParsesDirectoryAndRoundTripsFrame) {
    asio::thread_pool pool{2};
    auto scratch = make_scratch_dir("happy");

    // Write a minimal QuickFIX .cfg.
    const std::string cfg_content =
        "[DEFAULT]\n"
        "FileStorePath=" + scratch.string() + "\n"
        "SenderCompID=SENDER\n"
        "TargetCompID=TARGET\n";
    fs::path cfg_path = write_cfg(scratch, cfg_content);

    // ── Call under test ───────────────────────────────────────────────────────
    auto result = cfg_to_file_store_factory(cfg_path);
    ASSERT_TRUE(result.has_value())
        << "Expected success but got error: "
        << (result.has_value() ? 0 : static_cast<int>(result.error()));

    std::unique_ptr<FileStoreFactory>& factory = *result;
    ASSERT_NE(factory, nullptr);

    // ── Verify Config.directory mirrors FileStorePath ─────────────────────────
    // Access the factory's stored Config.directory via make().
    // We call make() with the executor from the pool to populate file_io_executor.
    auto store_result = factory->make(
        "SENDER", "TARGET", nullptr, 1024UL * 1024UL * 1024UL,
        pool.get_executor());
    ASSERT_TRUE(store_result.has_value())
        << "factory->make() failed with error: "
        << (store_result.has_value() ? 0 : static_cast<int>(store_result.error()));

    auto& store_ptr = *store_result;
    ASSERT_NE(store_ptr, nullptr);

    // ── Round-trip a single frame ─────────────────────────────────────────────
    // Store one outbound frame with seq=1.
    const std::string payload = "8=FIX.4.4\x01""35=D\x01""49=SENDER\x01""56=TARGET\x01";
    std::vector<std::byte> frame_bytes;
    for (unsigned char c : payload) {
        frame_bytes.push_back(static_cast<std::byte>(c));
    }
    const std::span<const std::byte> frame_span(frame_bytes);

    // store() is an awaitable; run it on the pool.
    auto store_fut = asio::co_spawn(
        pool,
        store_ptr->store(1, frame_span, direction_t::outbound),
        asio::use_future);
    auto store_ec = store_fut.get();
    ASSERT_TRUE(store_ec.has_value())
        << "store() failed with error: "
        << (store_ec.has_value() ? 0 : static_cast<int>(store_ec.error()));

    // retrieve() the frame back and verify byte-equality.
    CollectVisitor visitor;
    auto retrieve_fut = asio::co_spawn(
        pool,
        store_ptr->retrieve(1, 1, direction_t::outbound, visitor),
        asio::use_future);
    auto retrieve_ec = retrieve_fut.get();
    ASSERT_TRUE(retrieve_ec.has_value())
        << "retrieve() failed with error: "
        << (retrieve_ec.has_value() ? 0 : static_cast<int>(retrieve_ec.error()));

    ASSERT_EQ(visitor.frames.size(), 1u) << "Expected exactly 1 retrieved frame";
    EXPECT_EQ(visitor.frames[0], frame_bytes)
        << "Round-trip frame mismatch: byte content differs";

    pool.join();
    fs::remove_all(scratch);
}

// ─────────────────────────────────────────────────────────────────────────────
// Defense-in-depth sub-cases: five poisoned CompID values
// All must return store_factory_failed at config-LOAD time (inside
// cfg_to_file_store_factory) — NOT at make() time.
// ─────────────────────────────────────────────────────────────────────────────

// Helper: write a .cfg with the given SenderCompID, call cfg_to_file_store_factory,
// assert it returns store_factory_failed.
void assert_compid_rejected(const std::string& sender_compid,
                             const char* suffix,
                             const char* label) {
    auto scratch = make_scratch_dir(suffix);
    const std::string cfg_content =
        "[DEFAULT]\n"
        "FileStorePath=" + scratch.string() + "\n"
        "SenderCompID=" + sender_compid + "\n"
        "TargetCompID=TARGET\n";
    fs::path cfg_path = write_cfg(scratch, cfg_content, "test.cfg");

    auto result = cfg_to_file_store_factory(cfg_path);
    EXPECT_FALSE(result.has_value())
        << label << ": expected cfg_to_file_store_factory to FAIL at config-load "
           "time (store_factory_failed) but it succeeded.";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::store_factory_failed)
            << label << ": expected error code store_factory_failed (slot 61).";
    }
    fs::remove_all(scratch);
}

// Sub-case 1: path traversal in SenderCompID
TEST(CfgLoaderDefenseInDepth, PathTraversalSenderRejected) {
    assert_compid_rejected("../../etc/passwd", "traversal",
                           "SenderCompID='../../etc/passwd'");
}

// Sub-case 2: path separator in SenderCompID
TEST(CfgLoaderDefenseInDepth, PathSeparatorSenderRejected) {
    assert_compid_rejected("foo/bar", "separator",
                           "SenderCompID='foo/bar'");
}

// Sub-case 3: empty SenderCompID (key present but value is blank)
TEST(CfgLoaderDefenseInDepth, EmptySenderCompIDRejected) {
    // Write the cfg with an explicit empty value.
    auto scratch = make_scratch_dir("empty_sender");
    const std::string cfg_content =
        "[DEFAULT]\n"
        "FileStorePath=" + scratch.string() + "\n"
        "SenderCompID=\n"
        "TargetCompID=TARGET\n";
    fs::path cfg_path = write_cfg(scratch, cfg_content);

    auto result = cfg_to_file_store_factory(cfg_path);
    EXPECT_FALSE(result.has_value())
        << "SenderCompID=<empty>: expected cfg_to_file_store_factory to FAIL "
           "at config-load time but it succeeded.";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::store_factory_failed);
    }
    fs::remove_all(scratch);
}

// Sub-case 4: control character in SenderCompID (\x01abc)
TEST(CfgLoaderDefenseInDepth, ControlCharSenderRejected) {
    // Note: constructing a C string literal with \x01 in the middle is valid.
    // The .cfg file will contain the raw control byte.
    auto scratch = make_scratch_dir("ctrl_sender");
    // We write the file directly to embed the control character.
    fs::path cfg_path = scratch / "test.cfg";
    {
        std::ofstream ofs(cfg_path, std::ios::trunc | std::ios::binary);
        ofs << "[DEFAULT]\n"
            << "FileStorePath=" << scratch.string() << "\n"
            << "SenderCompID=\x01" "abc\n"
            << "TargetCompID=TARGET\n";
    }

    auto result = cfg_to_file_store_factory(cfg_path);
    EXPECT_FALSE(result.has_value())
        << "SenderCompID='\\x01abc': expected cfg_to_file_store_factory to FAIL "
           "at config-load time but it succeeded.";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::store_factory_failed);
    }
    fs::remove_all(scratch);
}

// Sub-case 5: SenderCompID exceeding NAME_MAX
TEST(CfgLoaderDefenseInDepth, NameMaxExcessSenderRejected) {
    auto scratch = make_scratch_dir("namemax_sender");

    // Compute NAME_MAX for the scratch dir.
    long name_max = pathconf(scratch.c_str(), _PC_NAME_MAX);
    if (name_max < 0) name_max = 255;

    // The composed filename is sender + "__" + "TARGET" + ".log" (12 extra chars).
    // Build a sender that makes the composed filename exceed NAME_MAX.
    const std::size_t extra = std::string("__TARGET.log").size();  // 12
    const std::size_t excess_len = static_cast<std::size_t>(name_max) - extra + 2;
    const std::string long_sender(excess_len, 'A');

    const std::string cfg_content =
        "[DEFAULT]\n"
        "FileStorePath=" + scratch.string() + "\n"
        "SenderCompID=" + long_sender + "\n"
        "TargetCompID=TARGET\n";
    fs::path cfg_path = write_cfg(scratch, cfg_content);

    auto result = cfg_to_file_store_factory(cfg_path);
    EXPECT_FALSE(result.has_value())
        << "SenderCompID exceeding NAME_MAX: expected cfg_to_file_store_factory "
           "to FAIL at config-load time but it succeeded. name_max=" << name_max
        << " sender_len=" << excess_len;
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::store_factory_failed);
    }
    fs::remove_all(scratch);
}

// ─────────────────────────────────────────────────────────────────────────────
// Missing required key: no FileStorePath → must fail
// ─────────────────────────────────────────────────────────────────────────────

TEST(CfgLoaderEdgeCases, MissingFileStorePathRejected) {
    auto scratch = make_scratch_dir("missing_fsp");
    const std::string cfg_content =
        "[DEFAULT]\n"
        "SenderCompID=SENDER\n"
        "TargetCompID=TARGET\n";
    fs::path cfg_path = write_cfg(scratch, cfg_content);

    auto result = cfg_to_file_store_factory(cfg_path);
    EXPECT_FALSE(result.has_value())
        << "Missing FileStorePath: expected store_factory_failed.";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::store_factory_failed);
    }
    fs::remove_all(scratch);
}

// ─────────────────────────────────────────────────────────────────────────────
// Non-existent file → must fail
// ─────────────────────────────────────────────────────────────────────────────

TEST(CfgLoaderEdgeCases, NonExistentFileRejected) {
    fs::path nonexistent = fs::temp_directory_path() / "fixpp_cfgloader_nonexistent_xyz.cfg";
    fs::remove(nonexistent);  // ensure it doesn't exist

    auto result = cfg_to_file_store_factory(nonexistent);
    EXPECT_FALSE(result.has_value())
        << "Non-existent file: expected store_factory_failed.";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::store_factory_failed);
    }
}

}  // namespace
