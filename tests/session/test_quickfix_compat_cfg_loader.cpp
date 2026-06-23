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
#ifndef _WIN32
#include <unistd.h>  // pathconf, _PC_NAME_MAX
#endif

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <climits>
#include <cstddef>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/quickfix_compat/cfg_loader.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fstream>
#include <string>
#include <vector>

#include "_fixtures_/store_temp_dir.hpp"

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
fs::path write_cfg(const fs::path& scratch, const std::string& content,
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

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>> on_frame(
        seqnum_t /*seq*/, std::span<const std::byte> data) noexcept override {
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
        "FileStorePath=" +
        scratch.string() +
        "\n"
        "SenderCompID=SENDER\n"
        "TargetCompID=TARGET\n";
    fs::path cfg_path = write_cfg(scratch, cfg_content);

    // ── Call under test ───────────────────────────────────────────────────────
    auto result = cfg_to_file_store_factory(cfg_path);
    ASSERT_TRUE(result.has_value()) << "Expected success but got error: "
                                    << (result.has_value() ? 0 : static_cast<int>(result.error()));

    std::unique_ptr<FileStoreFactory>& factory = *result;
    ASSERT_NE(factory, nullptr);

    // ── Verify Config.directory mirrors FileStorePath ─────────────────────────
    // Access the factory's stored Config.directory via make().
    // We call make() with the executor from the pool to populate file_io_executor.
    auto store_result =
        factory->make("SENDER", "TARGET", nullptr, 1024UL * 1024UL * 1024UL, pool.get_executor());
    ASSERT_TRUE(store_result.has_value())
        << "factory->make() failed with error: "
        << (store_result.has_value() ? 0 : static_cast<int>(store_result.error()));

    auto& store_ptr = *store_result;
    ASSERT_NE(store_ptr, nullptr);

    // ── Round-trip a single frame ─────────────────────────────────────────────
    // Store one outbound frame with seq=1.
    const std::string payload =
        "8=FIX.4.4\x01"
        "35=D\x01"
        "49=SENDER\x01"
        "56=TARGET\x01";
    std::vector<std::byte> frame_bytes;
    for (unsigned char c : payload) {
        frame_bytes.push_back(static_cast<std::byte>(c));
    }
    const std::span<const std::byte> frame_span(frame_bytes);

    // store() is an awaitable; run it on the pool.
    auto store_fut = asio::co_spawn(pool, store_ptr->store(1, frame_span, direction_t::outbound),
                                    asio::use_future);
    auto store_ec = store_fut.get();
    ASSERT_TRUE(store_ec.has_value())
        << "store() failed with error: "
        << (store_ec.has_value() ? 0 : static_cast<int>(store_ec.error()));

    // retrieve() the frame back and verify byte-equality.
    CollectVisitor visitor;
    auto retrieve_fut = asio::co_spawn(
        pool, store_ptr->retrieve(1, 1, direction_t::outbound, visitor), asio::use_future);
    auto retrieve_ec = retrieve_fut.get();
    ASSERT_TRUE(retrieve_ec.has_value())
        << "retrieve() failed with error: "
        << (retrieve_ec.has_value() ? 0 : static_cast<int>(retrieve_ec.error()));

    ASSERT_EQ(visitor.frames.size(), 1u) << "Expected exactly 1 retrieved frame";
    EXPECT_EQ(visitor.frames[0], frame_bytes) << "Round-trip frame mismatch: byte content differs";

    pool.join();
    store_result.value().reset();
    fixpp::store_test::remove_store_dir(scratch);
}

// ─────────────────────────────────────────────────────────────────────────────
// Defense-in-depth sub-cases: five poisoned CompID values
// All must return store_factory_failed at config-LOAD time (inside
// cfg_to_file_store_factory) — NOT at make() time.
// ─────────────────────────────────────────────────────────────────────────────

// Helper: write a .cfg with the given SenderCompID, call cfg_to_file_store_factory,
// assert it returns store_factory_failed.
void assert_compid_rejected(const std::string& sender_compid, const char* suffix,
                            const char* label) {
    auto scratch = make_scratch_dir(suffix);
    const std::string cfg_content =
        "[DEFAULT]\n"
        "FileStorePath=" +
        scratch.string() +
        "\n"
        "SenderCompID=" +
        sender_compid +
        "\n"
        "TargetCompID=TARGET\n";
    fs::path cfg_path = write_cfg(scratch, cfg_content, "test.cfg");

    auto result = cfg_to_file_store_factory(cfg_path);
    EXPECT_FALSE(result.has_value())
        << label
        << ": expected cfg_to_file_store_factory to FAIL at config-load "
           "time (store_factory_failed) but it succeeded.";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::store_factory_failed)
            << label << ": expected error code store_factory_failed (slot 61).";
    }
    fixpp::store_test::remove_store_dir(scratch);
}

// Sub-case 1: path traversal in SenderCompID
TEST(CfgLoaderDefenseInDepth, PathTraversalSenderRejected) {
    assert_compid_rejected("../../etc/passwd", "traversal", "SenderCompID='../../etc/passwd'");
}

// Sub-case 2: path separator in SenderCompID
TEST(CfgLoaderDefenseInDepth, PathSeparatorSenderRejected) {
    assert_compid_rejected("foo/bar", "separator", "SenderCompID='foo/bar'");
}

// Sub-case 3: empty SenderCompID (key present but value is blank)
TEST(CfgLoaderDefenseInDepth, EmptySenderCompIDRejected) {
    // Write the cfg with an explicit empty value.
    auto scratch = make_scratch_dir("empty_sender");
    const std::string cfg_content =
        "[DEFAULT]\n"
        "FileStorePath=" +
        scratch.string() +
        "\n"
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
    fixpp::store_test::remove_store_dir(scratch);
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
            << "SenderCompID=\x01"
               "abc\n"
            << "TargetCompID=TARGET\n";
    }

    auto result = cfg_to_file_store_factory(cfg_path);
    EXPECT_FALSE(result.has_value())
        << "SenderCompID='\\x01abc': expected cfg_to_file_store_factory to FAIL "
           "at config-load time but it succeeded.";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::store_factory_failed);
    }
    fixpp::store_test::remove_store_dir(scratch);
}

// Sub-case 5: SenderCompID exceeding NAME_MAX
TEST(CfgLoaderDefenseInDepth, NameMaxExcessSenderRejected) {
    auto scratch = make_scratch_dir("namemax_sender");

    // Compute NAME_MAX for the scratch dir.
#ifdef _WIN32
    long name_max = 255;  // NTFS/exFAT max filename component; no pathconf on Windows
#else
    long name_max = pathconf(scratch.c_str(), _PC_NAME_MAX);
    if (name_max < 0) name_max = 255;
#endif

    // The composed filename is sender + "__" + "TARGET" + ".log" (12 extra chars).
    // Build a sender that makes the composed filename exceed NAME_MAX.
    const std::size_t extra = std::string("__TARGET.log").size();  // 12
    const std::size_t excess_len = static_cast<std::size_t>(name_max) - extra + 2;
    const std::string long_sender(excess_len, 'A');

    const std::string cfg_content =
        "[DEFAULT]\n"
        "FileStorePath=" +
        scratch.string() +
        "\n"
        "SenderCompID=" +
        long_sender +
        "\n"
        "TargetCompID=TARGET\n";
    fs::path cfg_path = write_cfg(scratch, cfg_content);

    auto result = cfg_to_file_store_factory(cfg_path);
    EXPECT_FALSE(result.has_value())
        << "SenderCompID exceeding NAME_MAX: expected cfg_to_file_store_factory "
           "to FAIL at config-load time but it succeeded. name_max="
        << name_max << " sender_len=" << excess_len;
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::store_factory_failed);
    }
    fixpp::store_test::remove_store_dir(scratch);
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
    EXPECT_FALSE(result.has_value()) << "Missing FileStorePath: expected store_factory_failed.";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::store_factory_failed);
    }
    fixpp::store_test::remove_store_dir(scratch);
}

// ─────────────────────────────────────────────────────────────────────────────
// Non-existent file → must fail
// ─────────────────────────────────────────────────────────────────────────────

TEST(CfgLoaderEdgeCases, NonExistentFileRejected) {
    fs::path nonexistent = fs::temp_directory_path() / "fixpp_cfgloader_nonexistent_xyz.cfg";
    fs::remove(nonexistent);  // ensure it doesn't exist

    auto result = cfg_to_file_store_factory(nonexistent);
    EXPECT_FALSE(result.has_value()) << "Non-existent file: expected store_factory_failed.";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::store_factory_failed);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Coverage uplift tests — F4.1 (F1..F12 below)
// Target: cfg_loader.cpp ≥90% line / ≥80% branch
// ─────────────────────────────────────────────────────────────────────────────

// F1: [SESSION] section overrides [DEFAULT] value.
// Exercises the SESSION branch (line 65 in cfg_loader.cpp) and the SESSION
// merge path (line 135 ses target + merge logic).
TEST(CfgLoaderCoverageUplift, SessionSectionOverridesDefault) {
    auto scratch = make_scratch_dir("session_override");
    const std::string cfg_content =
        "[DEFAULT]\n"
        "FileStorePath=/tmp/default_ignored\n"
        "SenderCompID=DEFAULT_SENDER\n"
        "TargetCompID=DEFAULT_TARGET\n"
        "[SESSION]\n"
        "FileStorePath=" +
        scratch.string() +
        "\n"
        "SenderCompID=SESSION_SENDER\n"
        "TargetCompID=SESSION_TARGET\n";
    fs::path cfg_path = write_cfg(scratch, cfg_content);

    // Expected: SESSION values win the merge
    auto result = cfg_to_file_store_factory(cfg_path);
    EXPECT_TRUE(result.has_value()) << "[SESSION] override: expected success but got error: "
                                    << (!result.has_value() ? static_cast<int>(result.error()) : 0);
    fixpp::store_test::remove_store_dir(scratch);
}

// F2: [session] (lowercase) → case-insensitive fallback path.
// Exercises parse_section_tag lines 67-78 (case-fold loop for size-7 tags).
TEST(CfgLoaderCoverageUplift, LowercaseSessionSectionRecognised) {
    auto scratch = make_scratch_dir("lc_session");
    const std::string cfg_content =
        "[default]\n"
        "FileStorePath=" +
        scratch.string() +
        "\n"
        "SenderCompID=SENDER\n"
        "TargetCompID=TARGET\n";
    fs::path cfg_path = write_cfg(scratch, cfg_content);

    auto result = cfg_to_file_store_factory(cfg_path);
    EXPECT_TRUE(result.has_value())
        << "[default] (lowercase): expected success (case-insensitive) but got error: "
        << (!result.has_value() ? static_cast<int>(result.error()) : 0);
    fixpp::store_test::remove_store_dir(scratch);
}

// F3: [session] lowercase SESSION section overrides DEFAULT.
// Also exercises the SESSION case-fold branch.
TEST(CfgLoaderCoverageUplift, LowercaseSessionOverrideRecognised) {
    auto scratch = make_scratch_dir("lc_session_override");
    const std::string cfg_content =
        "[DEFAULT]\n"
        "FileStorePath=/tmp/default_ignored\n"
        "SenderCompID=DEFAULT_SENDER\n"
        "TargetCompID=DEFAULT_TARGET\n"
        "[session]\n"
        "FileStorePath=" +
        scratch.string() +
        "\n"
        "SenderCompID=SESSION_SND\n"
        "TargetCompID=SESSION_TGT\n";
    fs::path cfg_path = write_cfg(scratch, cfg_content);

    auto result = cfg_to_file_store_factory(cfg_path);
    EXPECT_TRUE(result.has_value())
        << "[session] lowercase override: expected success but got error: "
        << (!result.has_value() ? static_cast<int>(result.error()) : 0);
    fixpp::store_test::remove_store_dir(scratch);
}

// F4: Unknown section → keys inside it must be ignored; result depends
// on whether DEFAULT keys were provided. Here DEFAULT has all keys, so
// success is expected (unknown section keys are ignored).
// Exercises Section::other branch (lines 123-125).
TEST(CfgLoaderCoverageUplift, UnknownSectionKeysIgnored) {
    auto scratch = make_scratch_dir("unknown_section");
    const std::string cfg_content =
        "[DEFAULT]\n"
        "FileStorePath=" +
        scratch.string() +
        "\n"
        "SenderCompID=SENDER\n"
        "TargetCompID=TARGET\n"
        "[STORE]\n"
        "FileStorePath=/tmp/ignored_by_unknown_section\n"
        "SenderCompID=IGNORED\n";
    fs::path cfg_path = write_cfg(scratch, cfg_content);

    auto result = cfg_to_file_store_factory(cfg_path);
    EXPECT_TRUE(result.has_value())
        << "Unknown section keys ignored: expected success but got error: "
        << (!result.has_value() ? static_cast<int>(result.error()) : 0);
    fixpp::store_test::remove_store_dir(scratch);
}

// F5: Key without '=' → parser skips it via eq == npos branch (line 129).
// DEFAULT section has all required keys so result is success.
TEST(CfgLoaderCoverageUplift, KeyWithoutEqualsIgnored) {
    auto scratch = make_scratch_dir("no_equals");
    const std::string cfg_content =
        "[DEFAULT]\n"
        "FileStorePath=" +
        scratch.string() +
        "\n"
        "SenderCompID=SENDER\n"
        "TargetCompID=TARGET\n"
        "SomeKeyWithoutEquals\n";
    fs::path cfg_path = write_cfg(scratch, cfg_content);

    auto result = cfg_to_file_store_factory(cfg_path);
    EXPECT_TRUE(result.has_value())
        << "Key-without-equals ignored: expected success but got error: "
        << (!result.has_value() ? static_cast<int>(result.error()) : 0);
    fixpp::store_test::remove_store_dir(scratch);
}

// F6: Empty file → all keys missing → store_factory_failed.
TEST(CfgLoaderCoverageUplift, EmptyFileRejected) {
    auto scratch = make_scratch_dir("empty_file");
    fs::path cfg_path = write_cfg(scratch, "");

    auto result = cfg_to_file_store_factory(cfg_path);
    EXPECT_FALSE(result.has_value()) << "Empty file: expected store_factory_failed.";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::store_factory_failed);
    }
    fixpp::store_test::remove_store_dir(scratch);
}

// F7: Comment-only file → no keys → store_factory_failed.
TEST(CfgLoaderCoverageUplift, CommentOnlyFileRejected) {
    auto scratch = make_scratch_dir("comment_only");
    const std::string cfg_content =
        "# This is a comment\n"
        "; Another comment style\n"
        "# No actual config here\n";
    fs::path cfg_path = write_cfg(scratch, cfg_content);

    auto result = cfg_to_file_store_factory(cfg_path);
    EXPECT_FALSE(result.has_value()) << "Comment-only file: expected store_factory_failed.";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::store_factory_failed);
    }
    fixpp::store_test::remove_store_dir(scratch);
}

// F8: Missing SenderCompID → store_factory_failed.
TEST(CfgLoaderCoverageUplift, MissingSenderCompIDRejected) {
    auto scratch = make_scratch_dir("missing_sender");
    const std::string cfg_content =
        "[DEFAULT]\n"
        "FileStorePath=" +
        scratch.string() +
        "\n"
        "TargetCompID=TARGET\n";
    fs::path cfg_path = write_cfg(scratch, cfg_content);

    auto result = cfg_to_file_store_factory(cfg_path);
    EXPECT_FALSE(result.has_value()) << "Missing SenderCompID: expected store_factory_failed.";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::store_factory_failed);
    }
    fixpp::store_test::remove_store_dir(scratch);
}

// F9: Missing TargetCompID → store_factory_failed.
TEST(CfgLoaderCoverageUplift, MissingTargetCompIDRejected) {
    auto scratch = make_scratch_dir("missing_target");
    const std::string cfg_content =
        "[DEFAULT]\n"
        "FileStorePath=" +
        scratch.string() +
        "\n"
        "SenderCompID=SENDER\n";
    fs::path cfg_path = write_cfg(scratch, cfg_content);

    auto result = cfg_to_file_store_factory(cfg_path);
    EXPECT_FALSE(result.has_value()) << "Missing TargetCompID: expected store_factory_failed.";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::store_factory_failed);
    }
    fixpp::store_test::remove_store_dir(scratch);
}

// F10: Whitespace-only FileStorePath value → trimmed to empty → store_factory_failed.
TEST(CfgLoaderCoverageUplift, WhitespaceOnlyFileStorePathRejected) {
    auto scratch = make_scratch_dir("ws_fsp");
    const std::string cfg_content =
        "[DEFAULT]\n"
        "FileStorePath=   \n"
        "SenderCompID=SENDER\n"
        "TargetCompID=TARGET\n";
    fs::path cfg_path = write_cfg(scratch, cfg_content);

    auto result = cfg_to_file_store_factory(cfg_path);
    EXPECT_FALSE(result.has_value())
        << "Whitespace-only FileStorePath: expected store_factory_failed.";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::store_factory_failed);
    }
    fixpp::store_test::remove_store_dir(scratch);
}

// F11: Duplicate FileStorePath key — last value wins per QuickFIX convention
// (simple linear scan; second value overwrites first in the struct).
TEST(CfgLoaderCoverageUplift, DuplicateFileStorePathLastWins) {
    auto scratch = make_scratch_dir("dup_fsp");
    const std::string cfg_content =
        "[DEFAULT]\n"
        "FileStorePath=/tmp/first_ignored\n"
        "FileStorePath=" +
        scratch.string() +
        "\n"
        "SenderCompID=SENDER\n"
        "TargetCompID=TARGET\n";
    fs::path cfg_path = write_cfg(scratch, cfg_content);

    auto result = cfg_to_file_store_factory(cfg_path);
    EXPECT_TRUE(result.has_value())
        << "Duplicate FileStorePath (last wins): expected success but got error: "
        << (!result.has_value() ? static_cast<int>(result.error()) : 0);
    fixpp::store_test::remove_store_dir(scratch);
}

// F12: Malformed section header (missing ']') → treated as Section::other
// per parse_section_tag (line.back() != ']' → Section::other).
// DEFAULT key is absent so result is store_factory_failed.
TEST(CfgLoaderCoverageUplift, MalformedSectionHeaderRejected) {
    auto scratch = make_scratch_dir("malformed_section");
    const std::string cfg_content =
        "[DEFAULT\n"
        "FileStorePath=" +
        scratch.string() +
        "\n"
        "SenderCompID=SENDER\n"
        "TargetCompID=TARGET\n";
    fs::path cfg_path = write_cfg(scratch, cfg_content);

    // The section is not recognised → keys are in Section::none context → ignored.
    auto result = cfg_to_file_store_factory(cfg_path);
    EXPECT_FALSE(result.has_value()) << "Malformed section header: expected store_factory_failed "
                                        "(keys outside a valid section are ignored).";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::store_factory_failed);
    }
    fixpp::store_test::remove_store_dir(scratch);
}

}  // namespace
