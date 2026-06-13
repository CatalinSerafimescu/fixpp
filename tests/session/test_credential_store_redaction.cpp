// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_credential_store_redaction.cpp
//
// 034-credential-store-redaction — witness suite.
//
// Phase 1 (T001): skeleton + CMake registration.
// Phase 2 (T002/T003): standalone masker unit — Masker_SameLength_FieldAnchored_unit.
// Later phases (T005–T011) append cells to this file.
//
// TDD: Phase 2 cells are RED until T003 (masker impl) ships; the masker is in
// include/fixpp/session/logon_credentials.hpp (sibling of redact_tag554).
//
// FileStore-fixture headers (_fixtures_/store_temp_dir.hpp,
// _fixtures_/test_double_fsm.hpp) are included for later phases (T005 US1
// disk-byte witnesses); they add zero link cost for the Phase-2 masker unit.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <fixpp/session/logon_credentials.hpp>

#include "_fixtures_/store_temp_dir.hpp"
#include "_fixtures_/test_double_fsm.hpp"

namespace {

using fixpp::session::mask_tag554_same_length_inplace;

// ── helper ───────────────────────────────────────────────────────────────────

// Build a mutable byte buffer from a char literal (no NUL appended).
// SOH characters in the source string are represented as '\x01'.
template <std::size_t N>
std::array<std::byte, N - 1> make_buf(const char (&src)[N]) {
    std::array<std::byte, N - 1> buf{};
    for (std::size_t i = 0; i < N - 1; ++i) {
        buf[i] = std::byte{static_cast<std::uint8_t>(src[i])};
    }
    return buf;
}

// Returns true iff every byte in [begin, end) equals '*' (0x2A).
bool all_stars(const std::byte* begin, const std::byte* end) {
    for (auto p = begin; p != end; ++p) {
        if (*p != std::byte{0x2A}) return false;
    }
    return true;
}

// ── Masker_SameLength_FieldAnchored_unit ─────────────────────────────────────
//
// Witnesses C1 / E1 / I-E1-* from contracts/store-redaction.md:
//
//   (a) A genuine \x01554=secret\x01 value is overwritten with an EQUAL-LENGTH
//       run of '*' (0x2A).
//   (b) frame.size() (the span's extent) is unchanged — and no byte outside the
//       value extent is modified (I-E1-1 full form: snapshot + compare).
//   (c) A decoy "554=" inside a tag-58 free-text value is NOT touched (field-
//       boundary anchored; C1 field-detection rule).
//   (d) Calling the masker a second time on the already-masked buffer is
//       byte-stable (idempotent, I-E1-2) — the function returns true (found the
//       field) but no byte changes.
//   (e) A buffer with no genuine 554 field returns false and is unchanged.
//
// Additional cells not enumerated in (a)–(e) but required by the spec:
//   (f) Empty-value case \x01554=\x01: returns true, frame is byte-unchanged
//       (zero-length overwrite, no UB — I-E1-4).
//   (g) Frame-start occurrence "554=secret\x01" (no leading SOH): matched and
//       masked correctly (data-model.md E1 field-detection rule, offset-0 arm).

TEST(Masker_SameLength_FieldAnchored_unit, A_MasksGenuineField_StarRun) {
    // Minimal frame: just the \x01554=secret\x01 sequence (mid-frame occurrence).
    // The 554 value "secret" is 6 bytes; after masking it must be "******".
    auto buf = make_buf("\x01""554=secret\x01");
    const std::size_t orig_size = buf.size();

    // Save a copy to compare untouched regions.
    auto before = buf;

    bool result = mask_tag554_same_length_inplace(std::span<std::byte>{buf});

    EXPECT_TRUE(result) << "expected true: at least one genuine 554 field found";

    // (a) The value bytes [after "554=", before the trailing SOH] must be '*'.
    // Offset breakdown: \x01(0), 5(1), 5(2), 4(3), =(4), v0(5)..v5(10), \x01(11)
    // Value extent: indices 5..10 inclusive (6 bytes).
    EXPECT_TRUE(all_stars(buf.data() + 5, buf.data() + 11))
        << "value extent must be entirely '*'";

    // (b) frame.size() unchanged; no byte outside the value extent changed.
    EXPECT_EQ(buf.size(), orig_size) << "span extent must not change";
    // Bytes before the value (the \x01554= prefix) unchanged.
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(buf[i], before[i]) << "byte " << i << " (prefix) must be unchanged";
    }
    // Byte after the value (the trailing SOH at index 11) unchanged.
    EXPECT_EQ(buf[11], before[11]) << "trailing SOH must be unchanged";

    // Confirm star count equals original value length.
    std::size_t star_count = 0;
    for (std::size_t i = 5; i < 11; ++i) {
        if (buf[i] == std::byte{0x2A}) ++star_count;
    }
    EXPECT_EQ(star_count, 6u) << "must have exactly 6 stars (same length as 'secret')";
}

TEST(Masker_SameLength_FieldAnchored_unit, B_FrameSizeUnchanged_NoByteOutsideValueModified) {
    // Full FIX-like frame with 554 in the middle.
    // "8=FIX.4.2\x0149=S\x01554=pass\x0156=T\x01"
    auto buf = make_buf("8=FIX.4.2\x01""49=S\x01""554=pass\x01""56=T\x01");
    auto before = buf;
    const std::size_t orig_size = buf.size();

    bool result = mask_tag554_same_length_inplace(std::span<std::byte>{buf});
    EXPECT_TRUE(result) << "genuine 554 field present → must return true";

    EXPECT_EQ(buf.size(), orig_size);

    // Locate the 554 field in the raw source to find value offset.
    // "8=FIX.4.2\x01" = 10 bytes (indices 0..9), "49=S\x01" = 5 bytes (10..14;
    // the SOH is at index 14), then "554=" at 15..18, "pass" at 19..22, SOH at 23.
    // Value extent: indices 19..22 inclusive (4 bytes).
    const std::size_t val_start = 19;
    const std::size_t val_end   = 23;  // exclusive

    for (std::size_t i = 0; i < orig_size; ++i) {
        if (i >= val_start && i < val_end) {
            EXPECT_EQ(buf[i], std::byte{0x2A})
                << "value byte at index " << i << " must be '*'";
        } else {
            EXPECT_EQ(buf[i], before[i])
                << "non-value byte at index " << i << " must be unchanged";
        }
    }
}

TEST(Masker_SameLength_FieldAnchored_unit, C_DecoyInFreeTextUntouched) {
    // Frame where "554=" appears inside tag 58 (free text), not as a real field.
    // The decoy is: \x0158=foo554=bar\x01
    // No real \x01554= or leading "554=" exists.
    auto buf = make_buf("\x01""58=foo554=bar\x01""49=S\x01");
    auto before = buf;

    bool result = mask_tag554_same_length_inplace(std::span<std::byte>{buf});

    EXPECT_FALSE(result) << "no genuine 554 field: must return false";
    // Every byte must be unchanged.
    for (std::size_t i = 0; i < buf.size(); ++i) {
        EXPECT_EQ(buf[i], before[i]) << "byte " << i << " must be unchanged";
    }
}

TEST(Masker_SameLength_FieldAnchored_unit, D_Idempotent_SecondPassByteStable) {
    // (I-E1-2): masking an already-masked frame is a no-op.
    auto buf = make_buf("\x01""554=abc\x01");
    auto before_first = buf;

    bool r1 = mask_tag554_same_length_inplace(std::span<std::byte>{buf});
    EXPECT_TRUE(r1) << "first pass: must return true (field found)";

    // Capture after first mask.
    auto after_first = buf;

    bool r2 = mask_tag554_same_length_inplace(std::span<std::byte>{buf});
    EXPECT_TRUE(r2) << "second pass: must return true (field still found — '***')";

    // Byte-stability: second pass produces exactly the same bytes as after first.
    for (std::size_t i = 0; i < buf.size(); ++i) {
        EXPECT_EQ(buf[i], after_first[i])
            << "byte " << i << " must be byte-stable after second mask";
    }

    // Sanity: the value region is still all stars.
    // \x01(0), 5(1), 5(2), 4(3), =(4), v0(5), v1(6), v2(7), \x01(8)
    EXPECT_TRUE(all_stars(buf.data() + 5, buf.data() + 8))
        << "value must remain all stars after second pass";
}

TEST(Masker_SameLength_FieldAnchored_unit, E_NoGenuine554_ReturnsFalse_NoChange) {
    // Frame with no 554 field at all.
    auto buf = make_buf("8=FIX.4.2\x01""35=A\x01""49=SENDER\x01");
    auto before = buf;

    bool result = mask_tag554_same_length_inplace(std::span<std::byte>{buf});

    EXPECT_FALSE(result) << "no 554 field: must return false";
    for (std::size_t i = 0; i < buf.size(); ++i) {
        EXPECT_EQ(buf[i], before[i]) << "byte " << i << " must be unchanged";
    }
}

TEST(Masker_SameLength_FieldAnchored_unit, F_EmptyValue_ReturnsTrueByteUnchanged) {
    // Empty value: \x01554=\x01 — zero value bytes. Must return true; no bytes
    // are overwritten (zero-length overwrite), but the field was found (I-E1).
    auto buf = make_buf("\x01""554=\x01""49=S\x01");
    auto before = buf;

    bool result = mask_tag554_same_length_inplace(std::span<std::byte>{buf});

    EXPECT_TRUE(result) << "empty value: field was found, must return true";
    // Frame must be byte-for-byte unchanged (nothing to overwrite).
    for (std::size_t i = 0; i < buf.size(); ++i) {
        EXPECT_EQ(buf[i], before[i])
            << "byte " << i << " must be unchanged (empty value)";
    }
}

TEST(Masker_SameLength_FieldAnchored_unit, G_FrameStartOccurrence_Masked) {
    // 554 at offset 0 (no leading SOH): "554=secret\x01rest"
    auto buf = make_buf("554=secret\x01""49=S\x01");
    auto before = buf;

    bool result = mask_tag554_same_length_inplace(std::span<std::byte>{buf});

    EXPECT_TRUE(result) << "offset-0 554 field: must return true";

    // "554=" is 4 bytes (indices 0..3), value is indices 4..9 (6 bytes "secret"),
    // then SOH at index 10.
    EXPECT_TRUE(all_stars(buf.data() + 4, buf.data() + 10))
        << "value extent must be all '*'";

    // Prefix "554=" (indices 0..3) and SOH (index 10) unchanged.
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(buf[i], before[i]) << "prefix byte " << i << " must be unchanged";
    }
    EXPECT_EQ(buf[10], before[10]) << "trailing SOH must be unchanged";
}

}  // namespace

// ── Phase 3 (T005/T008/T009): session-level store-masking + wire-clarity witnesses
//
// Witnesses for:
//   T005 (US1/P1) — store masking: FileStore disk bytes + MemoryStore retrieve bytes
//     have no cleartext password; a same-length '*' run replaces the 554 value.
//   T008 (US2/P2) — wire clarity: transmitted Logon carries the cleartext 554.
//   T009 (US3/P3) — no-op path: credential-free Logon and non-Logon-with-554 are
//     stored byte-for-byte identical to the transmitted frame (no masking occurs).
//
// Harness: asio::thread_pool{2} + pool.get_executor() for session + FileStore I/O.
// FIXT sessions (is_fixt() == true) are the only sessions that carry 554 in their
// Logon; the 3-arg Session(engine, cfg, &registry) ctor is required by the
// FQ-1 open()-time FIXT validation gate.
//
// Anchors: contracts/store-redaction.md C2; session.cpp:4446 (store_then_emit gate);
//          data-model.md E1/INV-034-1..5; test_fixt_credentials.cpp (FIXT harness
//          pattern); test_file_store_flush_for_session_close.cpp (FileStore pattern).

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <cstring>
#include <filesystem>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/version_registry.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

// mallocnesia weak-symbol hooks — replaced by LD_PRELOAD; no-ops otherwise.
// Must be at file scope for the LD_PRELOAD override to bind. (T011 alloc gate;
// mirrors the NoHeap pattern in test_next_expected_msgseqnum.cpp.)
extern "C" {
// NOLINTNEXTLINE(misc-use-anonymous-namespace) — must be at file scope for LD_PRELOAD override.
__attribute__((weak)) void alloc_guard_start() {}
// NOLINTNEXTLINE(misc-use-anonymous-namespace)
__attribute__((weak)) void alloc_guard_end() {}
// NOLINTNEXTLINE(misc-use-anonymous-namespace)
__attribute__((weak)) long alloc_guard_count() { return 0; }
}

namespace fixpp_redaction_session {

using namespace std::chrono_literals;
using fixpp::dict::application_version;
using fixpp::dict::version_registry;
using fixpp::session::direction_t;
using fixpp::session::FileStore;
using fixpp::session::FileStoreFactory;
using fixpp::session::MemoryStore;
using fixpp::session::MessageStore;
using fixpp::session::MessageStoreFactory;
using fixpp::session::fsm_state;
using fixpp::session::Session;
using fixpp::session::SessionConfig;
using fixpp::store_test::byte_collecting_visitor;
using fixpp::store_test::unique_store_dir;

// ── Minimal FIX 5.0 SP2 XML dict (shared with test_fixt_credentials.cpp) ──────
// Redefine locally to avoid cross-file dependency.
constexpr std::string_view kFix50sp2Xml = R"xml(
<fix major="5" minor="0" servicepack="2">
  <header>
    <field number="8"  name="BeginString"  required="Y"/>
    <field number="9"  name="BodyLength"   required="Y"/>
    <field number="35" name="MsgType"      required="Y"/>
    <field number="49" name="SenderCompID" required="Y"/>
    <field number="56" name="TargetCompID" required="Y"/>
    <field number="34" name="MsgSeqNum"    required="Y"/>
    <field number="52" name="SendingTime"  required="Y"/>
    <field number="10" name="CheckSum"     required="Y"/>
  </header>
  <trailer>
    <field number="10" name="CheckSum" required="Y"/>
  </trailer>
  <messages>
    <message name="Heartbeat" msgtype="0" msgcat="admin">
      <field number="112" name="TestReqID" required="N"/>
    </message>
  </messages>
  <fields>
    <field number="8"   name="BeginString"  type="STRING"/>
    <field number="9"   name="BodyLength"   type="INT"/>
    <field number="35"  name="MsgType"      type="STRING"/>
    <field number="49"  name="SenderCompID" type="STRING"/>
    <field number="56"  name="TargetCompID" type="STRING"/>
    <field number="34"  name="MsgSeqNum"    type="INT"/>
    <field number="52"  name="SendingTime"  type="UTCTIMESTAMP"/>
    <field number="10"  name="CheckSum"     type="STRING"/>
    <field number="112" name="TestReqID"    type="STRING"/>
  </fields>
</fix>
)xml";

[[nodiscard]] std::shared_ptr<const fixpp::dict::Dictionary> make_fix50sp2_dict() {
    constexpr std::size_t kBufSize = 64u * 1024u;
    auto buf = std::make_unique<std::array<std::byte, kBufSize>>();
    auto* mr = new std::pmr::monotonic_buffer_resource{buf->data(), buf->size()};
    fixpp::dict::Dictionary d = fixpp::dict::XmlLoader{}.load_from_string(kFix50sp2Xml, mr);
    auto* raw_dict = new fixpp::dict::Dictionary{std::move(d)};
    auto* raw_buf = buf.release();
    return std::shared_ptr<const fixpp::dict::Dictionary>{
        raw_dict, [mr, raw_buf](const fixpp::dict::Dictionary* p) {
            delete p;
            delete mr;
            delete raw_buf;
        }};
}

// ── make_mock_clock helper ─────────────────────────────────────────────────────

std::shared_ptr<fixpp::core::mock_clock> make_mock_clock(asio::any_io_executor ex) {
    return std::make_shared<fixpp::core::mock_clock>(fixpp::core::utc_time_point{},
                                                     fixpp::core::steady_time_point{}, ex);
}

// ── FIXT session config factory ────────────────────────────────────────────────
//
// Builds a FIXT.1.1 SessionConfig for use with a pool-based executor.
// The 3-arg Session(engine, cfg, &registry) ctor is required by FQ-1.

[[nodiscard]] SessionConfig make_fixt_initiator_cfg(asio::any_io_executor ex) {
    SessionConfig cfg;
    cfg.sender_comp_id = "INITR";
    cfg.target_comp_id = "ACCEPTR";
    cfg.begin_string = "FIXT.1.1";
    cfg.heartbeat_interval = std::chrono::seconds{30};
    cfg.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::one_way_ca};
    cfg.dictionary = make_fix50sp2_dict();
    cfg.executor_override = ex;
    cfg.role = fixpp::session::session_role::initiator;
    cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    cfg.default_appl_ver_id = application_version::v50sp2;
    return cfg;
}

[[nodiscard]] SessionConfig make_fixt_acceptor_cfg(asio::any_io_executor ex) {
    SessionConfig cfg;
    cfg.sender_comp_id = "ACCEPTR";
    cfg.target_comp_id = "INITR";
    cfg.begin_string = "FIXT.1.1";
    cfg.heartbeat_interval = std::chrono::seconds{30};
    cfg.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::one_way_ca};
    cfg.dictionary = make_fix50sp2_dict();
    cfg.executor_override = ex;
    cfg.role = fixpp::session::session_role::acceptor;
    cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    cfg.default_appl_ver_id = application_version::v50sp2;
    return cfg;
}

// ── FIXT Logon frame builder ─────────────────────────────────────────────────
// Builds a minimal FIXT.1.1 Logon with optional 1137/553/554 fields.
// sending_time defaults to the UTC epoch ("19700101-00:00:00.000") so that
// it passes the SendingTime latency check (mock_clock is seeded at utc_time_point{}).
[[nodiscard]] std::vector<std::byte> make_fixt_logon(std::string_view sender_comp_id,
                                                      std::string_view target_comp_id,
                                                      std::uint32_t seq = 1,
                                                      std::string_view dav_id = "9",
                                                      std::string_view username = "",
                                                      std::string_view password = "",
                                                      std::string_view sending_time = "19700101-00:00:00.000") {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender_comp_id) + "\x01";
    body += "52=" + std::string(sending_time) + "\x01";
    body += "56=" + std::string(target_comp_id) + "\x01";
    body += "98=0\x01";
    body += "108=30\x01";
    if (!dav_id.empty()) {
        body += "1137=" + std::string(dav_id) + "\x01";
    }
    if (!username.empty()) {
        body += "553=" + std::string(username) + "\x01";
    }
    if (!password.empty()) {
        body += "554=" + std::string(password) + "\x01";
    }

    std::string hdr = "8=FIXT.1.1\x01" "9=" + std::to_string(body.size()) + "\x01";
    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) cs += c;
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(full.size());
    for (char c : full) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

// ── bytes_as_str ─────────────────────────────────────────────────────────────
[[nodiscard]] std::string bytes_as_str(std::span<const std::byte> b) {
    std::string s;
    s.resize(b.size());
    for (std::size_t i = 0; i < b.size(); ++i)
        s[i] = static_cast<char>(b[i]);
    return s;
}

// ── CapturingMemStoreFactory ─────────────────────────────────────────────────
//
// Wraps MemoryStore and stashes the raw pointer so the test can call retrieve().
// Lifetime: the Session owns the unique_ptr<MessageStore>; the raw pointer is
// valid as long as the Session is alive.
struct CapturingMemStoreFactory final : public MessageStoreFactory {
    mutable MemoryStore* last_store = nullptr;

    [[nodiscard]] bool yields_persistent_store() const noexcept override { return false; }

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/,
        std::pmr::memory_resource* mr, std::size_t max_bytes,
        asio::any_io_executor /*exec*/) noexcept override {
        (void)mr;
        (void)max_bytes;
        MemoryStore::Config mcfg;
        mcfg.policy = fixpp::session::capacity_policy::unbounded;
        auto store = std::make_unique<MemoryStore>(mcfg);
        last_store = store.get();
        return store;
    }
};

// ── disk_bytes_contain ────────────────────────────────────────────────────────
//
// Scans ALL files in the given directory for a needle byte string.
// Returns true iff the needle appears in ANY file.
[[nodiscard]] bool disk_bytes_contain(const std::filesystem::path& dir,
                                       std::string_view needle) {
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream f(entry.path(), std::ios::binary);
        if (!f) continue;
        std::string content((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
        if (content.find(needle) != std::string::npos) return true;
    }
    return false;
}

// ── disk_bytes_count_occurrences ──────────────────────────────────────────────
//
// Counts total occurrences of needle across ALL files in the directory.
[[nodiscard]] std::size_t disk_bytes_count(const std::filesystem::path& dir,
                                            std::string_view needle) {
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream f(entry.path(), std::ios::binary);
        if (!f) continue;
        std::string content((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
        std::size_t pos = 0;
        while ((pos = content.find(needle, pos)) != std::string::npos) {
            ++count;
            pos += needle.size();
        }
    }
    return count;
}

// ── T005 / US1 / P1 ───────────────────────────────────────────────────────────
//
// Persisted_LogonPassword_AbsentFromStoreFile_MaskPresent
//
// Harness: FIXT.1.1 initiator + FileStore + thread_pool{2}.
// The initiator password "s3cr3t-T005-sentinel" appears in the emitted Logon
// (wire) but MUST NOT appear in the FileStore's on-disk bytes.
// A same-length '*' run MUST be present after "554=" in the disk bytes.
//
// Assertion shapes (non-proxy, per anti-pattern library):
//   (a) Sentinel byte-sequence appears ZERO times in any disk file.
//   (b) "554=" followed by a run of '*' of the same length is present on disk.
//
// Anchor: contracts/store-redaction.md C2 / INV-034-1; session.cpp:4446.

TEST(CredentialStoreRedaction, T005_Persisted_LogonPassword_AbsentFromStoreFile_MaskPresent) {
    constexpr std::string_view kSentinel = "s3cr3t-T005-sentinel";
    static_assert(kSentinel.size() == 20u, "sentinel length");

    asio::thread_pool pool{2};
    auto dir = unique_store_dir("t005_disk");

    FileStore::Config fcfg;
    fcfg.directory = dir;
    fcfg.sender_comp_id = "INITR";
    fcfg.target_comp_id = "ACCEPTR";
    fcfg.max_frame_bytes = 4096;
    fcfg.file_io_executor = pool.get_executor();

    fixpp::core::EngineConfig engine;
    engine.executor = pool.get_executor();
    engine.clock = make_mock_clock(pool.get_executor());
    engine.max_store_memory_per_session = 1ULL << 30;

    auto dict = make_fix50sp2_dict();
    version_registry registry{{dict}};

    std::vector<std::byte> wire_out;
    auto init_cfg = make_fixt_initiator_cfg(pool.get_executor());
    init_cfg.password = std::string(kSentinel);
    init_cfg.store_factory = std::make_unique<FileStoreFactory>(fcfg);
    init_cfg.transport_send = [&](std::span<const std::byte> f) {
        wire_out.assign(f.begin(), f.end());
    };

    {
        Session initiator{engine, init_cfg, &registry};
        auto r = asio::co_spawn(pool.get_executor(), initiator.open(), asio::use_future).get();
        ASSERT_TRUE(r.has_value()) << "open() must succeed for a configured FIXT initiator";
    }
    // Session destructs here — advisory lock released, FileStore flushed.

    // (pre-check) The wire frame carries the cleartext sentinel (password was on wire).
    ASSERT_FALSE(wire_out.empty()) << "Initiator must emit a Logon at open()";
    std::string wire_str = bytes_as_str(wire_out);
    ASSERT_NE(wire_str.find(kSentinel), std::string::npos)
        << "Wire Logon must carry the cleartext password (peer must receive it)";

    // (a) Sentinel MUST NOT appear in any disk file.
    EXPECT_FALSE(disk_bytes_contain(dir, kSentinel))
        << "Cleartext sentinel must NOT appear in any FileStore disk file (T005/INV-034-1)";

    // (b) A same-length '*' run MUST appear after "554=" on disk.
    // The masked form is "554=" + 20 '*' characters.
    std::string masked_val = "554=";
    masked_val.append(kSentinel.size(), '*');
    EXPECT_TRUE(disk_bytes_contain(dir, masked_val))
        << "Masked form '554=' + " << kSentinel.size()
        << " stars must be present in disk files (T005/INV-034-1/2)";

    std::filesystem::remove_all(dir);
}

// ── T005 / US1 / P1 (acceptor variant) ───────────────────────────────────────
//
// Acceptor_ReplyLogon_PasswordMaskedInStore
//
// An acceptor with a configured password emits its own 554 in the reply Logon.
// The reply Logon is stored via store_then_emit with the masking path.
// The disk file must contain no cleartext; same-length mask must be present.
//
// Setup: open acceptor, feed a valid inbound FIXT Logon (no creds from peer),
// acceptor transitions Active and emits a reply Logon carrying its own 554.
//
// Anchors: session.cpp:2252-2274 (acceptor reply Logon emit path);
//          contracts/store-redaction.md C2.

TEST(CredentialStoreRedaction, T005_Acceptor_ReplyLogon_PasswordMaskedInStore) {
    constexpr std::string_view kAcceptorPwd = "acpt-T005-sentinel";
    static_assert(kAcceptorPwd.size() == 18u, "sentinel length");

    asio::thread_pool pool{2};
    auto dir = unique_store_dir("t005_acceptor");

    FileStore::Config fcfg;
    fcfg.directory = dir;
    fcfg.sender_comp_id = "ACCEPTR";
    fcfg.target_comp_id = "INITR";
    fcfg.max_frame_bytes = 4096;
    fcfg.file_io_executor = pool.get_executor();

    fixpp::core::EngineConfig engine;
    engine.executor = pool.get_executor();
    engine.clock = make_mock_clock(pool.get_executor());
    engine.max_store_memory_per_session = 1ULL << 30;

    auto dict = make_fix50sp2_dict();
    version_registry registry{{dict}};

    std::vector<std::byte> acpt_wire_out;
    auto acpt_cfg = make_fixt_acceptor_cfg(pool.get_executor());
    acpt_cfg.password = std::string(kAcceptorPwd);
    acpt_cfg.store_factory = std::make_unique<FileStoreFactory>(fcfg);
    acpt_cfg.transport_send = [&](std::span<const std::byte> f) {
        acpt_wire_out.assign(f.begin(), f.end());
    };

    {
        Session acceptor{engine, acpt_cfg, &registry};
        auto open_r =
            asio::co_spawn(pool.get_executor(), acceptor.open(), asio::use_future).get();
        ASSERT_TRUE(open_r.has_value()) << "Acceptor open() must succeed";

        // Feed a valid inbound FIXT Logon from the peer (INITR → ACCEPTR).
        auto peer_logon = make_fixt_logon("INITR", "ACCEPTR", 1, "9");
        auto inb_r = asio::co_spawn(
                         pool.get_executor(),
                         acceptor.on_inbound_frame(
                             std::span<const std::byte>{peer_logon.data(), peer_logon.size()}),
                         asio::use_future)
                         .get();
        ASSERT_TRUE(inb_r.has_value()) << "Acceptor on_inbound_frame must succeed for valid Logon";
        EXPECT_EQ(acceptor.state(), fsm_state::Active) << "Acceptor must reach Active";

        // Drain the liveness loop (detached co_spawn from going Active) before
        // the Session destructs. Without close(), the detached loop outlives the
        // stack session — UAF visible under ASan.
        // [feedback_executor_compat_dispatch_guard_outlives_stack_session]
        asio::co_spawn(pool.get_executor(), acceptor.close(fixpp::session::close_mode::terminal),
                       asio::use_future)
            .get();
    }
    // Session destructs — FileStore flushed, liveness loop drained.

    // Pre-check: wire must carry cleartext (acceptor sends it to peer).
    ASSERT_FALSE(acpt_wire_out.empty()) << "Acceptor must emit a reply Logon";
    std::string wire_str = bytes_as_str(acpt_wire_out);
    ASSERT_NE(wire_str.find(kAcceptorPwd), std::string::npos)
        << "Acceptor reply Logon wire must carry configured password (cleartext to peer)";

    // (a) Cleartext must NOT appear in disk files.
    EXPECT_FALSE(disk_bytes_contain(dir, kAcceptorPwd))
        << "Cleartext password must NOT appear in FileStore disk file (T005 acceptor)";

    // (b) Same-length mask must be present: "554=" + 18 '*'.
    std::string masked_val = "554=";
    masked_val.append(kAcceptorPwd.size(), '*');
    EXPECT_TRUE(disk_bytes_contain(dir, masked_val))
        << "Masked form '554=' + " << kAcceptorPwd.size()
        << " stars must be present on disk (T005 acceptor)";

    std::filesystem::remove_all(dir);
}

// ── T005 / US1 / P1 (MemoryStore variant) ────────────────────────────────────
//
// InMemoryStore_CredentialedLogon_AlsoMasked
//
// The masking path is in store_then_emit, not in the store backend. This cell
// confirms the path is taken regardless of store type.
//
// Uses CapturingMemStoreFactory to retain a MemoryStore* for retrieve().
// After open(), retrieve the stored Logon frame and assert:
//   (a) cleartext sentinel absent from retrieved bytes
//   (b) same-length '*' run present in retrieved bytes after "554="
//
// Anchor: contracts/store-redaction.md INV-034-4 (masking backend-agnostic).

TEST(CredentialStoreRedaction, T005_InMemoryStore_CredentialedLogon_AlsoMasked) {
    constexpr std::string_view kSentinel = "mem-T005-sentinel";
    static_assert(kSentinel.size() == 17u, "sentinel length");

    asio::thread_pool pool{2};

    fixpp::core::EngineConfig engine;
    engine.executor = pool.get_executor();
    engine.clock = make_mock_clock(pool.get_executor());
    engine.max_store_memory_per_session = 1ULL << 30;

    auto dict = make_fix50sp2_dict();
    version_registry registry{{dict}};

    auto factory = std::make_unique<CapturingMemStoreFactory>();
    CapturingMemStoreFactory* factory_raw = factory.get();

    std::vector<std::byte> wire_out;
    auto init_cfg = make_fixt_initiator_cfg(pool.get_executor());
    init_cfg.password = std::string(kSentinel);
    init_cfg.store_factory = std::move(factory);
    init_cfg.transport_send = [&](std::span<const std::byte> f) {
        wire_out.assign(f.begin(), f.end());
    };

    Session initiator{engine, init_cfg, &registry};
    auto r = asio::co_spawn(pool.get_executor(), initiator.open(), asio::use_future).get();
    ASSERT_TRUE(r.has_value()) << "open() must succeed";

    // Pre-check: wire carries cleartext.
    ASSERT_FALSE(wire_out.empty()) << "Initiator must emit a Logon";
    std::string wire_str = bytes_as_str(wire_out);
    ASSERT_NE(wire_str.find(kSentinel), std::string::npos)
        << "Wire must carry cleartext password";

    // Retrieve the stored frame via the captured MemoryStore pointer.
    ASSERT_NE(factory_raw->last_store, nullptr) << "Factory must have minted a store";
    MemoryStore* store = factory_raw->last_store;

    byte_collecting_visitor visitor;
    auto ret_r = asio::co_spawn(
                     pool.get_executor(),
                     store->retrieve(1, 0, direction_t::outbound, visitor),
                     asio::use_future)
                     .get();
    ASSERT_TRUE(ret_r.has_value()) << "retrieve() must succeed";
    ASSERT_EQ(visitor.entries().size(), 1u) << "Exactly one outbound frame must be stored";

    const auto& stored_bytes = visitor.entries()[0].bytes;
    std::string stored_str(reinterpret_cast<const char*>(stored_bytes.data()),
                           stored_bytes.size());

    // (a) Cleartext sentinel absent from stored bytes.
    EXPECT_EQ(stored_str.find(kSentinel), std::string::npos)
        << "Cleartext password must NOT appear in MemoryStore retrieved bytes (T005/INV-034-4)";

    // (b) Same-length mask: "554=" + 17 '*' present in stored bytes.
    std::string masked_val = "554=";
    masked_val.append(kSentinel.size(), '*');
    EXPECT_NE(stored_str.find(masked_val), std::string::npos)
        << "Masked form '554=' + " << kSentinel.size()
        << " stars must appear in retrieved bytes (T005/INV-034-4)";
}

// ── T008 / US2 / P2 ───────────────────────────────────────────────────────────
//
// Wire_LogonPassword_UnmaskedOnTransmit
//
// The masking happens BEFORE store (step 1: store masked) but the ORIGINAL
// unmasked frame is transmitted (step 2: transmit original).
// This cell proves the wire frame carries the cleartext password.
//
// Harness: ioc-based (no FileStore, no pool needed — wire-only capture).
// Port of W6a from test_fixt_credentials.cpp (proves the same property but
// from the 034 angle: the wire is NOT masked).
//
// Anchors: session.cpp:4443-4467 (step 2 transmit original); INV-034-5.

TEST(CredentialStoreRedaction, T008_Wire_LogonPassword_UnmaskedOnTransmit) {
    constexpr std::string_view kSentinel = "wire-T008-sentinel";
    static_assert(kSentinel.size() == 18u, "sentinel length");

    // For wire-only test, use io_context + run_for to keep things simple.
    // No FileStore I/O: no pool hang risk.
    asio::thread_pool pool{2};

    fixpp::core::EngineConfig engine;
    engine.executor = pool.get_executor();
    engine.clock = make_mock_clock(pool.get_executor());
    engine.max_store_memory_per_session = 1ULL << 30;

    auto dict = make_fix50sp2_dict();
    version_registry registry{{dict}};

    std::vector<std::byte> wire_out;
    auto init_cfg = make_fixt_initiator_cfg(pool.get_executor());
    init_cfg.password = std::string(kSentinel);
    // No store_factory — no FileStore involved (wire-only).
    init_cfg.transport_send = [&](std::span<const std::byte> f) {
        wire_out.assign(f.begin(), f.end());
    };

    Session initiator{engine, init_cfg, &registry};
    auto r = asio::co_spawn(pool.get_executor(), initiator.open(), asio::use_future).get();
    ASSERT_TRUE(r.has_value()) << "open() must succeed for a FIXT initiator with password";

    ASSERT_FALSE(wire_out.empty()) << "Initiator must emit a Logon at open()";
    std::string wire_str = bytes_as_str(wire_out);

    // The cleartext password MUST be present on the wire (peer must receive it for auth).
    EXPECT_NE(wire_str.find(kSentinel), std::string::npos)
        << "Wire Logon MUST carry cleartext password (INV-034-5 / T008): got: " << wire_str;

    // A same-length '*' run must NOT be present on the wire (masking is store-only).
    std::string masked_val = "554=";
    masked_val.append(kSentinel.size(), '*');
    EXPECT_EQ(wire_str.find(masked_val), std::string::npos)
        << "Wire Logon must NOT contain masked form (masking is store-only, not wire): "
        << wire_str;
}

// ── T009 / US3 / P3 ───────────────────────────────────────────────────────────
//
// CredentialFreeLogon_And_NonLogon_StoredByteIdentical
//
// The no-op path: when the Logon carries no 554 field, frame_has_genuine_tag554()
// returns false and the frame is stored byte-identical to the transmitted frame.
//
// Harness: FIXT.1.1 initiator with NO credentials configured + CapturingMemStoreFactory.
// After open(), capture both the wire frame and the stored frame. Assert byte-identical.
//
// Discriminating shape: pre-advance state so a no-op would fail if masking happened.
// The wire bytes are known to NOT contain "554=" (no creds configured). The retrieved
// bytes must exactly match the wire bytes.
//
// Anchor: session.cpp:4443-4444 (span_to_store = frame; default path).

TEST(CredentialStoreRedaction, T009_CredentialFreeLogon_StoredByteIdenticalToWire) {
    asio::thread_pool pool{2};

    fixpp::core::EngineConfig engine;
    engine.executor = pool.get_executor();
    engine.clock = make_mock_clock(pool.get_executor());
    engine.max_store_memory_per_session = 1ULL << 30;

    auto dict = make_fix50sp2_dict();
    version_registry registry{{dict}};

    auto factory = std::make_unique<CapturingMemStoreFactory>();
    CapturingMemStoreFactory* factory_raw = factory.get();

    std::vector<std::byte> wire_out;
    auto init_cfg = make_fixt_initiator_cfg(pool.get_executor());
    // NO credentials — password and username left unset.
    init_cfg.store_factory = std::move(factory);
    init_cfg.transport_send = [&](std::span<const std::byte> f) {
        wire_out.assign(f.begin(), f.end());
    };

    Session initiator{engine, init_cfg, &registry};
    auto r = asio::co_spawn(pool.get_executor(), initiator.open(), asio::use_future).get();
    ASSERT_TRUE(r.has_value()) << "open() must succeed for a credential-free FIXT initiator";

    ASSERT_FALSE(wire_out.empty()) << "Initiator must emit a Logon at open()";

    // Pre-check: wire contains no "554=" (no masking was needed).
    std::string wire_str = bytes_as_str(wire_out);
    ASSERT_EQ(wire_str.find("554="), std::string::npos)
        << "Credential-free Logon must not carry 554 on wire (precondition)";

    // Retrieve stored bytes.
    ASSERT_NE(factory_raw->last_store, nullptr);
    MemoryStore* store = factory_raw->last_store;

    byte_collecting_visitor visitor;
    auto ret_r = asio::co_spawn(
                     pool.get_executor(),
                     store->retrieve(1, 0, direction_t::outbound, visitor),
                     asio::use_future)
                     .get();
    ASSERT_TRUE(ret_r.has_value()) << "retrieve() must succeed";
    ASSERT_EQ(visitor.entries().size(), 1u) << "Exactly one outbound frame stored";

    const auto& stored_bytes = visitor.entries()[0].bytes;

    // Byte-identical: wire == stored (no masking occurred, no-op path).
    ASSERT_EQ(stored_bytes.size(), wire_out.size())
        << "Stored frame must be same length as wire frame (byte-identical path)";
    EXPECT_EQ(stored_bytes, wire_out)
        << "Stored frame must be byte-identical to wire frame when no 554 is present (T009)";
}

// ── T009 / US3 / P3 (non-Logon variant) ─────────────────────────────────────
//
// NonLogon_WithGenuine554_StoredUnchanged
//
// A frame with genuine "554=" but msg_type != "A" must be stored UNCHANGED.
// The msg_type == "A" gate in store_then_emit (session.cpp:4446) means only
// Logon frames are subject to masking; business messages are stored verbatim.
//
// Harness: FIXT.1.1 initiator + manually-crafted acceptor reply Logon (no second
// Session object — avoids two-session complexity). The initiator goes LogonSent
// at open(), receives the hand-crafted reply, reaches Active. Then sends a business
// message with "554=cleartext" in the payload.
//
// Discriminating: we assert the retrieved stored bytes CONTAIN "554=cleartext"
// (not just "cleartext absent" — we assert the actual cleartext is PRESENT in
// the store, proving masking was skipped for non-Logon frames).
//
// Anchor: session.cpp:4446 (`msg_type == "A"` gate); INV-034-2.

TEST(CredentialStoreRedaction, T009_NonLogon_WithGenuine554_StoredUnchanged) {
    asio::thread_pool pool{2};

    fixpp::core::EngineConfig engine;
    engine.executor = pool.get_executor();
    engine.clock = make_mock_clock(pool.get_executor());
    engine.max_store_memory_per_session = 1ULL << 30;

    auto dict = make_fix50sp2_dict();
    version_registry registry{{dict}};

    // Initiator with CapturingMemStoreFactory to inspect stored business message.
    auto factory = std::make_unique<CapturingMemStoreFactory>();
    CapturingMemStoreFactory* factory_raw = factory.get();

    std::vector<std::byte> init_wire_out;
    auto init_cfg = make_fixt_initiator_cfg(pool.get_executor());
    // No credentials on the initiator — Logon has no 554.
    init_cfg.store_factory = std::move(factory);
    init_cfg.transport_send = [&](std::span<const std::byte> f) {
        // Overwrites on each emit (Logon, then business msg).
        init_wire_out.assign(f.begin(), f.end());
    };

    Session initiator{engine, init_cfg, &registry};

    // Step 1: open initiator (emits Logon, stays LogonSent).
    auto init_open_r =
        asio::co_spawn(pool.get_executor(), initiator.open(), asio::use_future).get();
    ASSERT_TRUE(init_open_r.has_value()) << "Initiator open() must succeed";
    ASSERT_FALSE(init_wire_out.empty()) << "Initiator must emit a Logon";

    // Step 2: feed a manually-crafted FIXT reply Logon from the peer (ACCEPTR→INITR).
    // Sender and target are from the acceptor's perspective: 49=ACCEPTR, 56=INITR.
    auto peer_reply = make_fixt_logon("ACCEPTR", "INITR", 1, "9");

    auto inb_r = asio::co_spawn(
                     pool.get_executor(),
                     initiator.on_inbound_frame(
                         std::span<const std::byte>{peer_reply.data(), peer_reply.size()}),
                     asio::use_future)
                     .get();
    ASSERT_TRUE(inb_r.has_value()) << "on_inbound_frame must succeed for valid reply Logon";
    ASSERT_EQ(initiator.state(), fsm_state::Active) << "Initiator must reach Active";

    // Step 3: send a business message with genuine 554= in the payload.
    // The payload must start with "35=<type>" and end with SOH; 554 is not a session tag.
    constexpr std::string_view kCleartext = "nologon-T009-sentinel";
    std::string payload = "35=D\x01";
    payload += "554=";
    payload += kCleartext;
    payload += "\x01";

    // Clear init_wire_out before send() so we capture only the business message.
    init_wire_out.clear();

    std::vector<std::byte> payload_bytes;
    payload_bytes.reserve(payload.size());
    for (char c : payload) payload_bytes.push_back(static_cast<std::byte>(c));

    auto send_r = asio::co_spawn(
                      pool.get_executor(),
                      initiator.send(std::span<const std::byte>{payload_bytes.data(),
                                                                 payload_bytes.size()}),
                      asio::use_future)
                      .get();
    ASSERT_TRUE(send_r.has_value()) << "send() must succeed in Active state";

    // Wire frame captures the business message.
    ASSERT_FALSE(init_wire_out.empty()) << "send() must emit a frame via transport_send";
    std::string wire_str = bytes_as_str(init_wire_out);

    // Wire must contain cleartext (non-Logon, no masking on wire side either).
    ASSERT_NE(wire_str.find(std::string(kCleartext)), std::string::npos)
        << "Wire business message must contain '554=" << kCleartext << "' (no masking on wire)";

    // Retrieve the stored business message from the MemoryStore.
    ASSERT_NE(factory_raw->last_store, nullptr);
    MemoryStore* store = factory_raw->last_store;

    // The store has seq 1 (Logon) and seq 2 (business message).
    // Retrieve from seq 2 to get only the business message.
    byte_collecting_visitor visitor;
    auto ret_r = asio::co_spawn(
                     pool.get_executor(),
                     store->retrieve(2, 0, direction_t::outbound, visitor),
                     asio::use_future)
                     .get();
    ASSERT_TRUE(ret_r.has_value()) << "retrieve() must succeed";
    ASSERT_EQ(visitor.entries().size(), 1u) << "Exactly one frame starting at seq 2";

    const auto& stored_bytes = visitor.entries()[0].bytes;
    std::string stored_str(reinterpret_cast<const char*>(stored_bytes.data()),
                           stored_bytes.size());

    // Key assertion: stored bytes CONTAIN the cleartext (masking was SKIPPED for non-Logon).
    // This is the discriminating check: stored_str.find(cleartext) != npos proves
    // the msg_type == "A" gate fired and skipped masking for non-Logon frames.
    EXPECT_NE(stored_str.find(std::string(kCleartext)), std::string::npos)
        << "Stored non-Logon frame must contain cleartext '554=" << kCleartext
        << "' — masking must be SKIPPED for msg_type != 'A' (T009/INV-034-2)";

    // Wire and stored must be byte-identical (no masking, no transformation).
    EXPECT_EQ(std::vector<std::byte>(stored_bytes.begin(), stored_bytes.end()), init_wire_out)
        << "Stored non-Logon frame must be byte-identical to wire frame (T009)";

    // Drain the liveness loop (detached co_spawn from going Active) before the
    // Session destructs. Without close(), the detached loop outlives the stack
    // session — UAF visible under ASan.
    // [feedback_executor_compat_dispatch_guard_outlives_stack_session]
    asio::co_spawn(pool.get_executor(), initiator.close(fixpp::session::close_mode::terminal),
                   asio::use_future)
        .get();
}

// ── T010 / fault-injection (over-bound fail-closed) ───────────────────────────
//
// OverBound_SmallBoundSeam_SkipStoreButTransmit
//
// The over-bound branch in store_then_emit (frame.size() > kMaxMaskableLogonBytes
// → skip_store = true) is production-UNREACHABLE: build_logon caps its output at
// kMaxMaskableLogonBytes (256) and fails-closed above it, and the T007 open()-guard
// rejects oversized credentials. So `build_logon`-produced frames can never trip it.
//
// To earn the branch's BRDA we feed a HAND-CRAFTED >256-byte 35=A frame carrying a
// genuine \x01554= directly into store_then_emit via the FIXPP_TEST_HOOKS accessor
// store_then_emit_test_access() — exercising the REAL branch against the REAL 256
// bound with zero production surface. (An earlier design used a
// FIXPP_TEST_LOGON_MASK_BOUND compile override; that constant lives in
// store_then_emit, compiled into libfixpp_session WITHOUT the test define, so it
// could not be reached from a test target — frame-injection is the working seam.)
//
// Asserts the two NEW behaviors:
//   (a) NO cleartext persisted — the over-bound frame's seq is absent from the store.
//   (b) the WIRE frame still carries the real cleartext 554 (transmit is unconditional).
// Postcondition (c) — a resend over the skipped seq yields a SequenceReset-GapFill,
// not a masked verbatim replay — is NOT new behavior: an absent store slot folds
// into a GapFill in the existing resend store-walk (session.cpp:4744-4770, confirmed
// at design time), and the MsgType=A admin→GapFill classification is exercised by
// T009_NonLogon. Not re-proven here to avoid over-investing in dead-branch coverage.
//
// Anchors: contracts/store-redaction.md C2 step-2 / I-07; research R3; session.hpp
//          store_then_emit_test_access; plan ## Gate A (mechanism deviation note).
#ifdef FIXPP_TEST_HOOKS
TEST(CredentialStoreRedaction, T010_OverBound_SmallBoundSeam_SkipStoreButTransmit) {
    constexpr std::string_view kSentinel = "overbound-T010-sentinel";

    asio::thread_pool pool{2};

    fixpp::core::EngineConfig engine;
    engine.executor = pool.get_executor();
    engine.clock = make_mock_clock(pool.get_executor());
    engine.max_store_memory_per_session = 1ULL << 30;

    auto dict = make_fix50sp2_dict();
    version_registry registry{{dict}};

    auto factory = std::make_unique<CapturingMemStoreFactory>();
    CapturingMemStoreFactory* factory_raw = factory.get();

    std::vector<std::byte> wire_out;
    auto init_cfg = make_fixt_initiator_cfg(pool.get_executor());
    // No configured creds — the over-bound frame carries its own 554; open() emits
    // a small credential-free Logon (seq 1) that is irrelevant to this cell.
    init_cfg.store_factory = std::move(factory);
    init_cfg.transport_send = [&](std::span<const std::byte> f) {
        wire_out.assign(f.begin(), f.end());
    };

    Session initiator{engine, init_cfg, &registry};
    auto open_r = asio::co_spawn(pool.get_executor(), initiator.open(), asio::use_future).get();
    ASSERT_TRUE(open_r.has_value()) << "open() must succeed";
    ASSERT_NE(factory_raw->last_store, nullptr);
    MemoryStore* store = factory_raw->last_store;

    // Craft a >256-byte 35=A frame with a genuine \x01554= field (padded via tag 58).
    std::string over;
    over += "8=FIXT.1.1\x01";
    over += "35=A\x01";
    over += "49=INITR\x01";
    over += "56=ACCEPTR\x01";
    over += "34=2\x01";  // SOH here is the field-boundary preceding 554
    over += "554=" + std::string(kSentinel) + "\x01";
    over += "58=" + std::string(280, 'X') + "\x01";  // padding → total well over 256
    ASSERT_GT(over.size(), 256u) << "test invariant: frame must exceed the 256 mask bound";

    std::vector<std::byte> over_bytes;
    over_bytes.reserve(over.size());
    for (char c : over) over_bytes.push_back(static_cast<std::byte>(c));

    // Inject at the store's NEXT expected outbound seq (open() stored the Logon at
    // seq 1), so that if skip_store were (wrongly) false the store would ACCEPT the
    // write — otherwise an out-of-order seq would be rejected by the store anyway
    // and the "absent" assertion would pass spuriously (not discriminate skip_store).
    constexpr fixpp::session::seqnum_t kInjSeq = 2;
    wire_out.clear();  // capture only the injected frame's transmit
    auto inj_r = asio::co_spawn(
                     pool.get_executor(),
                     initiator.store_then_emit_test_access(
                         kInjSeq, std::span<const std::byte>{over_bytes.data(), over_bytes.size()}),
                     asio::use_future)
                     .get();
    ASSERT_TRUE(inj_r.has_value())
        << "store_then_emit must return ok on the skip-store-but-transmit path (I-07)";

    // (b) Wire frame carries the over-bound frame verbatim — cleartext 554 intact.
    ASSERT_FALSE(wire_out.empty()) << "over-bound frame must still be transmitted (fail-closed ≠ drop)";
    std::string wire_str = bytes_as_str(wire_out);
    EXPECT_NE(wire_str.find(kSentinel), std::string::npos)
        << "Wire frame must carry the real cleartext 554 on the over-bound path (T010 b)";
    EXPECT_EQ(wire_out.size(), over_bytes.size())
        << "Wire frame must be the original over-bound frame byte-for-byte";

    // (a) NO cleartext persisted — the injected seq is absent from the store (skip_store).
    byte_collecting_visitor visitor;
    auto ret_r = asio::co_spawn(pool.get_executor(),
                                store->retrieve(kInjSeq, kInjSeq, direction_t::outbound, visitor),
                                asio::use_future)
                     .get();
    // retrieve over [kInjSeq,kInjSeq] must find NOTHING (the frame was never stored).
    EXPECT_TRUE(!ret_r.has_value() || visitor.entries().empty())
        << "Over-bound frame must NOT be persisted — seq " << kInjSeq
        << " must be absent from the store (fail-closed, no cleartext at rest) (T010 a)";

    // Belt-and-suspenders: the sentinel must not appear anywhere the store could
    // hand back. (MemoryStore holds only seq 1 — the credential-free open() Logon.)
    byte_collecting_visitor all;
    (void)asio::co_spawn(pool.get_executor(),
                         store->retrieve(1, 0, direction_t::outbound, all), asio::use_future)
        .get();
    for (const auto& e : all.entries()) {
        std::string s(reinterpret_cast<const char*>(e.bytes.data()), e.bytes.size());
        EXPECT_EQ(s.find(kSentinel), std::string::npos)
            << "No stored frame may contain the over-bound cleartext (T010 a)";
    }
}
#endif  // FIXPP_TEST_HOOKS

// ── T011 — alloc gate: the masking step adds zero heap allocation ─────────────
//
// StorePath_NoNewAllocation
//
// Honest scope (mirrors NoHeap.Emit789Append in test_next_expected_msgseqnum.cpp):
// the NEW synchronous surface this feature adds on the persist path is the
// copy-into-coroutine-frame + mask_tag554_same_length_inplace step (T006). The
// store call itself is unchanged (same store_->store(...), only the span pointer
// differs), so its allocation profile is inherited baseline and out of scope here.
// We measure ONLY the new memcpy+mask between mallocnesia guard markers, with all
// buffer/setup allocation done OUTSIDE the window.
//
// The _mallocnesia ctest variant (CMakeLists.txt) re-runs this binary under
// LD_PRELOAD via tools/check_alloc.py so any global-heap escape in the window
// aborts. [SC-004 / FR-008; [const §XV.1]; [[feedback_tracking_pmr_resource_false_pass]]]
TEST(NoHeap, StorePath_NoNewAllocation) {
    // ── Setup OUTSIDE the guarded window (legitimate allocation) ─────────────
    // A representative credentialed FIXT Logon frame, on the stack.
    auto frame = make_fixt_logon("INITR", "ACCEPTR", 1, "9", /*username=*/"alice",
                                 /*password=*/"s3cr3t-T011-sentinel");
    const std::size_t n = frame.size();
    ASSERT_LE(n, 256u) << "test invariant: a normal credentialed Logon fits the mask bound";

    // The coroutine-frame copy buffer store_then_emit uses (kMaxMaskableLogonBytes).
    std::array<std::byte, 256> mask_buf{};

    // ── Guarded window: only the new memcpy + mask step ──────────────────────
    alloc_guard_start();

    std::memcpy(mask_buf.data(), frame.data(), n);
    const bool masked =
        fixpp::session::mask_tag554_same_length_inplace(std::span<std::byte>{mask_buf.data(), n});

    alloc_guard_end();

    ASSERT_TRUE(masked) << "the representative Logon carries a 554 → must mask";

    // Post-condition: the masked copy holds no cleartext (sanity; the real
    // redaction witnesses are T005).
    std::string masked_str(reinterpret_cast<const char*>(mask_buf.data()), n);
    EXPECT_EQ(masked_str.find("s3cr3t-T011-sentinel"), std::string::npos)
        << "masked copy must not contain the cleartext password";

    // Under mallocnesia: a nonzero count means the mask step touched the heap.
    const long heap_allocs = alloc_guard_count();
    EXPECT_EQ(heap_allocs, 0L)
        << "the copy+mask step must not touch the global heap; heap_allocs=" << heap_allocs
        << "; [SC-004 / FR-008 / [[feedback_tracking_pmr_resource_false_pass]]]";
}

}  // namespace fixpp_redaction_session
