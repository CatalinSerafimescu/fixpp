// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_fixt_logon_establishment.cpp
//
// 033-fixt-fix50sp2-session — T002 (target creation) / T004 (RED render helper
// test) / T005 (impl: make T004 green) / T014 (W4 — FIX.4.x regression guard) /
// T016–T017 emit-half of W1 / T011/T012/T013/T015/T035 full inbound witnesses.
//
// Phase 2 (Foundational) witnesses:
//   RenderApplVerId_AllMappings (T004/T005):
//     Asserts the full inverse render helper application_version → wire 1137
//     string (data-model.md E3 / research R3). All four divergent values are
//     load-bearing (the helper exists precisely because the C++ enum index does
//     not coincide with the wire value):
//       v40  (index 1) → "2"   diverges from index
//       v44  (index 5) → "6"   diverges from index
//       v50  (index 6) → "7"   diverges from index
//       v50sp2 (index 8) → "9"  diverges from index
//     Plus: Unknown → error (no garbage on wire).
//
// Phase 3 witnesses (emit-side, US1):
//   T014 / W4 — FIX.4.x byte-identical regression guard (SC-002/C2):
//     A FIX.4.4 session's build_logon output carries NO 1137/553/554 and
//     the full wire is byte-for-byte the pre-033 baseline (hardcoded literal).
//     This is a GREEN-stays-GREEN guard — FIX.4.4 already works; 033 must not
//     touch it.
//
//   T016/T017 emit-half of W1 (C1):
//     A FIXT session's build_logon carries 8=FIXT.1.1 AND 1137=<rendered wire
//     value> after 108. Two cells: v50sp2→"9" and v44→"6".
//
// Phase 4 witnesses (inbound arm, US1, T018):
//   T011 / W1 — Full round-trip (C3/C6): both sides reach Active, both Logons
//     carry 1137, and negotiated_appl_version_ is recorded by both sides.
//     The discriminating assertion is negotiated_version_profile().default_appl
//     == v50sp2 (pre-T018: returns Unknown).
//   T012 / W2 — Missing-1137 (C4/FR-004): acceptor rejects with 373=1
//     (RequiredTagMissing) and does NOT reach Active.
//   T013 / W3 — Unserviceable-1137 (C5/FR-004a): acceptor rejects with
//     373=5 (ValueIsIncorrect) AND 371=1137 — DISTINCT from W2's 373=1.
//   T015 / W5 — Version-general (C6): two FIXT sessions — FIX.4.4 via 1137=6
//     and FIX.5.0SP2 via 1137=9 — both negotiate correctly.
//     Asserted via negotiated_version_profile().default_appl.
//   T035 / W8 — 1128 tolerance (C9/INV-FIXT-3): established FIXT session
//     receives inbound app message with ApplVerID(1128); delivered dict-free
//     to fromApp without parse failure; session stays Active.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/version_registry.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_security_profile.hpp"
#include "support/store_double.hpp"
#include "support/transport_double.hpp"

namespace {

using fixpp::core::error;
using fixpp::dict::application_version;
using fixpp::dict::render_appl_ver_id;

// ── Phase 2 Foundational: RenderApplVerId (T004/T005) ────────────────────────

// T004/T005 — render_appl_ver_id: application_version → wire ApplVerID string.
// All four positive mappings are load-bearing (each diverges from the C++ enum
// index — a "reuse the C++ index" bug would pass a single coincidental case but
// fail on the others). AC-VP4 in the inverse direction.
//
// Wire table (version_profile.hpp comment / [FIXT §5.1]):
//   v40=1  → "2"   v41=2 → "3"   v42=3 → "4"   v43=4 → "5"
//   v44=5  → "6"   v50=6 → "7"   v50sp1=7 → "8"  v50sp2=8 → "9"

TEST(RenderApplVerId, V40_MapsToDivergentWire2) {
    // C++ index 1, wire "2" — diverges from index.
    auto result = render_appl_ver_id(application_version::v40);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "2");
}

TEST(RenderApplVerId, V44_MapsToWire6) {
    // C++ index 5, wire "6" — diverges from index.
    auto result = render_appl_ver_id(application_version::v44);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "6");
}

TEST(RenderApplVerId, V50_MapsToDivergentWire7) {
    // C++ index 6, wire "7" — diverges from index (the key discriminating case
    // from AC-VP4: C++ index 6 == v50, but wire "7" is FIX 5.0, not "6").
    auto result = render_appl_ver_id(application_version::v50);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "7");
    // Explicitly NOT "6" (which would be v44 — the index-reuse trap).
    EXPECT_NE(*result, "6");
}

TEST(RenderApplVerId, V50sp2_MapsToWire9) {
    // C++ index 8, wire "9" — diverges from index.
    auto result = render_appl_ver_id(application_version::v50sp2);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "9");
}

TEST(RenderApplVerId, V41_MapsToWire3) {
    auto result = render_appl_ver_id(application_version::v41);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "3");
}

TEST(RenderApplVerId, V42_MapsToWire4) {
    auto result = render_appl_ver_id(application_version::v42);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "4");
}

TEST(RenderApplVerId, V43_MapsToWire5) {
    auto result = render_appl_ver_id(application_version::v43);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "5");
}

TEST(RenderApplVerId, V50sp1_MapsToWire8) {
    auto result = render_appl_ver_id(application_version::v50sp1);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "8");
}

TEST(RenderApplVerId, Unknown_ReturnsError) {
    // Unknown is the only invalid value — must not emit a garbage wire string.
    auto result = render_appl_ver_id(application_version::Unknown);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::dict_unknown_appl_ver_id);
}

TEST(RenderApplVerId, RoundTrip_AllValues) {
    // Symmetry: render then resolve must recover the original application_version
    // (every non-Unknown value must round-trip through the inverse pair).
    using fixpp::dict::resolve_application_version;
    using fixpp::dict::session_version;
    using fixpp::dict::version_profile;

    const version_profile kProfile{session_version::vt11, application_version::v50sp2, true, 0};

    constexpr application_version kAll[] = {
        application_version::v40,    application_version::v41,    application_version::v42,
        application_version::v43,    application_version::v44,    application_version::v50,
        application_version::v50sp1, application_version::v50sp2,
    };
    for (auto v : kAll) {
        auto wire = render_appl_ver_id(v);
        ASSERT_TRUE(wire.has_value()) << "render failed for enum=" << static_cast<int>(v);
        auto back = resolve_application_version(kProfile, *wire);
        ASSERT_TRUE(back.has_value()) << "resolve failed for wire=" << *wire;
        EXPECT_EQ(*back, v) << "round-trip mismatch for enum=" << static_cast<int>(v);
    }
}

// ── Phase 3 US1: W4 — FIX.4.x byte-identical regression guard (T014) ────────
//
// Asserts:
// (a) A FIX.4.4 build_logon emits NO 1137/553/554 field.
// (b) The full outbound frame is byte-for-byte identical to the pre-033 baseline
//     (hardcoded literal). Any 033 drift in the FIX.4.x emit path fails here.
//
// [SC-002 / C2; FR-009; INV-FIXT-1]
// This is NOT a RED-first test — FIX.4.4 already works; it is a GREEN guard.

namespace {

// Convert a byte span to a std::string for comparison and content checks.
[[nodiscard]] std::string bytes_to_string(std::span<const std::byte> s) {
    std::string out;
    out.resize(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        out[i] = static_cast<char>(s[i]);
    }
    return out;
}

// Return true iff a needle string appears as a complete tag boundary in a
// FIX wire frame (i.e. "\x01<needle>=" or beginning of frame "<needle>=").
// This avoids false positives from tag numbers appearing as field values.
[[nodiscard]] bool contains_tag(std::string_view frame, std::string_view tag_num) {
    // Check for SOH-prefixed tag= pattern anywhere in the frame.
    std::string needle = "\x01";
    needle += tag_num;
    needle += "=";
    return frame.find(needle) != std::string_view::npos;
}

// W4 baseline: the exact FIX.4.4 Logon produced by build_logon with:
//   seq=1, sender="SENDER", target="TARGET", begin="FIX.4.4",
//   heartbt=30, sending_time="20240101-00:00:00.000",
//   reset_seqnum=false (default), next_expected=nullopt (default),
//   default_appl_ver_id=nullopt (default).
//
// Baseline computed by:
//   body = 35=A\x01 34=1\x01 49=SENDER\x01 52=<time>\x01 56=TARGET\x01 98=0\x01 108=30\x01
//   BodyLength = 67; checksum = 101
// Eyeballed: well-formed FIX.4.4 Logon, no FIXT-only tags present.
// [FR-009/SC-002/C2; INV-FIXT-1]
constexpr std::string_view kFix44LogonBaseline =
    "8=FIX.4.4\x01"
    "9=67\x01"
    "35=A\x01"
    "34=1\x01"
    "49=SENDER\x01"
    "52=20240101-00:00:00.000\x01"
    "56=TARGET\x01"
    "98=0\x01"
    "108=30\x01"
    "10=101\x01";

}  // namespace

// T014 / W4 — FIX.4.4 byte-identical: no 1137/553/554, full baseline match.
// [SC-002; C2; FR-009; INV-FIXT-1; data-model E4]
TEST(BuildLogonFixt, W4_Fix44_ByteIdentical_NoFIXTTags) {
    std::array<std::byte, 512> buf{};
    auto result = fixpp::session::build_logon(
        std::span<std::byte>(buf),
        /*seq=*/1,
        /*sender_comp_id=*/"SENDER",
        /*target_comp_id=*/"TARGET",
        /*begin_string=*/"FIX.4.4",
        /*heartbt_int=*/30,
        /*sending_time=*/"20240101-00:00:00.000"
        // reset_seqnum=false (default), next_expected=nullopt (default),
        // default_appl_ver_id=nullopt (default) → byte-identical to pre-033
    );
    ASSERT_TRUE(result.has_value()) << "build_logon FIX.4.4 must succeed";

    const std::string frame = bytes_to_string(*result);

    // (a) No FIXT-only tags present in the FIX.4.4 Logon.
    EXPECT_FALSE(contains_tag(frame, "1137"))
        << "FIX.4.4 Logon must NOT carry DefaultApplVerID(1137) — SC-002/C2";
    EXPECT_FALSE(contains_tag(frame, "553"))
        << "FIX.4.4 Logon must NOT carry Username(553) — SC-002/C2";
    EXPECT_FALSE(contains_tag(frame, "554"))
        << "FIX.4.4 Logon must NOT carry Password(554) — SC-002/C2";

    // (b) Full byte-for-byte baseline comparison (pre-033 pin).
    // Any drift in the FIX.4.x emit path fails here — not just "no 1137".
    // The expected literal is the computed pre-033 baseline; it is hardcoded
    // so that BOTH the 033 impl AND any future edit that changes the FIX.4.x
    // path are caught. A second build_logon call would be a tautology.
    // [feedback_witness_asserts_named_postcondition_not_proxy (b)]
    EXPECT_EQ(frame, kFix44LogonBaseline)
        << "FIX.4.4 Logon frame must be byte-identical to the pre-033 baseline\n"
        << "  got (escaped):  " <<
        [&]() {
            std::string esc;
            for (char c : frame) {
                if (c == '\x01')
                    esc += "\\x01";
                else
                    esc += c;
            }
            return esc;
        }()
        << "\n  want (escaped): " << [&]() {
               std::string esc;
               for (char c : kFix44LogonBaseline) {
                   if (c == '\x01')
                       esc += "\\x01";
                   else
                       esc += c;
               }
               return esc;
           }();
}

// ── Phase 3 US1: W1 emit-half — FIXT 1137 field present after 108 (T016/T017) ─
//
// Asserts the emit side of C1: a build_logon call with default_appl_ver_id set
// AND begin_string="FIXT.1.1" carries:
//   - 8=FIXT.1.1
//   - 1137=<rendered wire value> in the contiguous subsequence after 108
// Two cells: v50sp2 → "1137=9" and v44 → "1137=6" (proving render_appl_ver_id
// is actually consulted, not a hardcoded "9").
//
// Does NOT assert round-trip/Active (inbound arm is T018 / next phase).
// [C1; FR-001/FR-002; data-model E4; T016/T017]

TEST(BuildLogonFixt, W1EmitHalf_V50sp2_Carries1137After108) {
    // Cell: FIXT session, DefaultApplVerID=v50sp2 → wire "9"
    std::array<std::byte, 512> buf{};
    auto result = fixpp::session::build_logon(std::span<std::byte>(buf),
                                              /*seq=*/1,
                                              /*sender_comp_id=*/"SENDER",
                                              /*target_comp_id=*/"TARGET",
                                              /*begin_string=*/"FIXT.1.1",
                                              /*heartbt_int=*/30,
                                              /*sending_time=*/"20240101-00:00:00.000",
                                              /*reset_seqnum=*/false,
                                              /*next_expected_seq=*/std::nullopt,
                                              /*default_appl_ver_id=*/application_version::v50sp2);
    ASSERT_TRUE(result.has_value()) << "build_logon FIXT v50sp2 must succeed";

    const std::string frame = bytes_to_string(*result);

    // (1) BeginString = FIXT.1.1
    EXPECT_NE(frame.find("8=FIXT.1.1\x01"), std::string::npos)
        << "FIXT Logon must carry 8=FIXT.1.1; got: " << frame;

    // (2) 1137=9 present
    const std::string kSoh{'\x01'};
    EXPECT_TRUE(contains_tag(frame, "1137"))
        << "FIXT v50sp2 Logon must carry DefaultApplVerID(1137); got: " << frame;
    EXPECT_NE(frame.find(kSoh + "1137=9" + kSoh), std::string::npos)
        << "FIXT v50sp2 must emit 1137=9 (wire value for v50sp2); got: " << frame;

    // (3) 1137 appears AFTER 108 — contiguous subsequence check.
    // This asserts the ordering requirement (data-model E4: after 108, before 141).
    const auto pos_108 = frame.find(kSoh + "108=30" + kSoh);
    ASSERT_NE(pos_108, std::string::npos) << "108=30 must appear in frame";
    const auto pos_1137 = frame.find(kSoh + "1137=9" + kSoh);
    ASSERT_NE(pos_1137, std::string::npos) << "1137=9 must appear in frame";
    EXPECT_LT(pos_108, pos_1137) << "1137 must appear AFTER 108 in the wire frame (data-model E4)";

    // (4) Contiguous: 108\x01 immediately followed by 1137= (no field between them).
    // This is the ordering-adjacent check (data-model E4: "ordered after 108, before 141").
    // The exact substring SOH+"108=30"+SOH+"1137=" must appear.
    EXPECT_NE(frame.find(kSoh + "108=30" + kSoh + "1137="), std::string::npos)
        << "1137 must be DIRECTLY after 108 in the wire frame (E4 ordering); got: " << frame;
}

TEST(BuildLogonFixt, W1EmitHalf_V44_Carries1137Wire6) {
    // Cell: FIXT session, DefaultApplVerID=v44 → wire "6"
    // Proves render_appl_ver_id is actually consulted (not a hardcoded "9").
    std::array<std::byte, 512> buf{};
    auto result = fixpp::session::build_logon(std::span<std::byte>(buf),
                                              /*seq=*/1,
                                              /*sender_comp_id=*/"SENDER",
                                              /*target_comp_id=*/"TARGET",
                                              /*begin_string=*/"FIXT.1.1",
                                              /*heartbt_int=*/30,
                                              /*sending_time=*/"20240101-00:00:00.000",
                                              /*reset_seqnum=*/false,
                                              /*next_expected_seq=*/std::nullopt,
                                              /*default_appl_ver_id=*/application_version::v44);
    ASSERT_TRUE(result.has_value()) << "build_logon FIXT v44 must succeed";

    const std::string frame = bytes_to_string(*result);

    // (1) BeginString = FIXT.1.1
    EXPECT_NE(frame.find("8=FIXT.1.1\x01"), std::string::npos)
        << "FIXT Logon must carry 8=FIXT.1.1";

    // (2) 1137=6 present (wire value for v44, NOT "5" which is the C++ enum index)
    const std::string kSoh44{'\x01'};
    EXPECT_NE(frame.find(kSoh44 + "1137=6" + kSoh44), std::string::npos)
        << "FIXT v44 must emit 1137=6 (wire value for FIX 4.4 per FIXT §5.1); "
        << "NOT the C++ enum index. Got: " << frame;

    // (3) NOT 1137=5 (C++ enum index — the index-reuse trap)
    EXPECT_EQ(frame.find(kSoh44 + "1137=5" + kSoh44), std::string::npos)
        << "1137=5 must NOT appear (that would be the C++ enum index, not the wire value)";

    // (4) 1137 directly after 108
    EXPECT_NE(frame.find(kSoh44 + "108=30" + kSoh44 + "1137="), std::string::npos)
        << "1137 must be directly after 108 in the wire frame (data-model E4)";
}

// (5) Discriminating mutation check (non-test artifact — ensures W1 is not vacuous):
//     Mutation-tested: with the 1137 emit block commented out in admin_messages.cpp,
//     W1EmitHalf_V50sp2_Carries1137After108 and W1EmitHalf_V44_Carries1137Wire6 go RED
//     (assert 1137 not found); W4_Fix44_ByteIdentical_NoFIXTTags stays GREEN (FIX.4.x
//     unaffected). Both W1 cells are non-vacuous discriminators of the emit path.

// ── Phase 4 US1: Inbound arm witnesses (T011/T012/T013/T015/T035) ────────────
//
// These tests drive full FIXT handshakes (acceptor + initiator) using the
// Session + TransportDouble pattern from logon_handshake_test.cpp.
//
// Infrastructure:
//   - asio::io_context (single-threaded), mock_clock injected via EngineConfig
//   - TransportDouble captures outbound frames
//   - version_registry built from version-tagged XML dictionaries
//   - make_fixt_logon_frame(): builds FIXT Logon with optional 1137 field
//   - run_sync(): co_spawn + ioc.run_for + ioc.restart helper
//
// Anchors:
//   [033 contracts/fixt-logon-establishment.md C3/C4/C5/C6/C9]
//   [033 data-model.md E2 (negotiated_appl_version_); INV-FIXT-2/3]
//   [033 research.md R1/R2/R4/R7]

namespace fixpp_fixt_inbound {

using namespace std::chrono_literals;
using fixpp::dict::application_version;
using fixpp::dict::version_registry;
using fixpp::session::fsm_state;

// ── Minimal version-tagged dictionaries ─────────────────────────────────────
//
// We need dictionaries whose which_session_version() returns v44 and v50sp2 so
// that version_registry::ctor maps them to application_version::v44 / v50sp2.
// The XML loader maps <fix major="4" minor="4"> → session_version::v44 etc.
//
// [version_registry.cpp session_to_application; xml_loader.cpp VersionTable]

constexpr std::string_view kMinimalFix44Xml = R"xml(
<fix major="4" minor="4">
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

constexpr std::string_view kMinimalFix50sp2Xml = R"xml(
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

[[nodiscard]] std::shared_ptr<const fixpp::dict::Dictionary> make_dict(std::string_view xml) {
    constexpr std::size_t kBufSize = 64u * 1024u;
    auto buf = std::make_unique<std::array<std::byte, kBufSize>>();
    auto* mr = new std::pmr::monotonic_buffer_resource{buf->data(), buf->size()};
    fixpp::dict::Dictionary d = fixpp::dict::XmlLoader{}.load_from_string(xml, mr);
    auto* raw_dict = new fixpp::dict::Dictionary{std::move(d)};
    auto* raw_buf = buf.release();
    return std::shared_ptr<const fixpp::dict::Dictionary>{
        raw_dict, [mr, raw_buf](const fixpp::dict::Dictionary* p) {
            delete p;
            delete mr;
            delete raw_buf;
        }};
}

// ── Frame builder helpers ────────────────────────────────────────────────────

// Build a FIXT Logon frame with optional 1137 field.
// If default_appl_ver_id is non-empty, appends "1137=<value>\x01" after 108.
[[nodiscard]] std::vector<std::byte> make_fixt_logon_frame(
    std::string_view begin_string, std::uint32_t msg_seq_num, std::string_view sender_comp_id,
    std::string_view target_comp_id, int heartbt_int, std::string_view default_appl_ver_id = "",
    std::string_view sending_time = "20240101-00:00:00.000") {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(msg_seq_num) + "\x01";
    body += "49=" + std::string(sender_comp_id) + "\x01";
    body += "52=" + std::string(sending_time) + "\x01";
    body += "56=" + std::string(target_comp_id) + "\x01";
    body += "98=0\x01";
    body += "108=" + std::to_string(heartbt_int) + "\x01";
    if (!default_appl_ver_id.empty()) {
        body += "1137=" + std::string(default_appl_ver_id) + "\x01";
    }

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(full.size());
    for (char c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

// Build a minimal app message (Heartbeat) with optional ApplVerID(1128) field.
// Used for W8: verifies dict-free delivery of inbound app messages.
[[nodiscard]] std::vector<std::byte> make_heartbeat_frame_1128(std::string_view begin_string,
                                                               std::uint32_t msg_seq_num,
                                                               std::string_view sender_comp_id,
                                                               std::string_view target_comp_id,
                                                               std::string_view appl_ver_id = "") {
    std::string body;
    body += "35=0\x01";
    body += "34=" + std::to_string(msg_seq_num) + "\x01";
    body += "49=" + std::string(sender_comp_id) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target_comp_id) + "\x01";
    if (!appl_ver_id.empty()) {
        body += "1128=" + std::string(appl_ver_id) + "\x01";
    }

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(full.size());
    for (char c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

// Extract a field value from a FIX wire frame by tag number (SOH-delimited).
[[nodiscard]] std::string extract_wire_field(std::span<const std::byte> frame, int tag) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = "\x01" + std::to_string(tag) + "=";
    auto pos = wire.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    pos += needle.size();  // advance past SOH + "tag="
    auto end = wire.find('\x01', pos);
    if (end == std::string::npos) {
        return wire.substr(pos);
    }
    return wire.substr(pos, end - pos);
}

// Check if a FIX wire frame contains a tag (boundary-safe).
[[nodiscard]] bool wire_has_tag(std::span<const std::byte> frame, int tag) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = "\x01" + std::to_string(tag) + "=";
    return wire.find(needle) != std::string::npos;
}

// Run a coroutine synchronously.
template <typename Fn>
auto run_sync(asio::io_context& ioc, Fn&& fn)
    -> decltype(asio::co_spawn(ioc, fn(), asio::use_future).get()) {
    auto fut = asio::co_spawn(ioc, fn(), asio::use_future);
    ioc.run_for(200ms);
    ioc.restart();
    return fut.get();
}

// ── Shared fixture factory ───────────────────────────────────────────────────

struct FixtSetup {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    version_registry registry;  // owning; Sessions take pointer

    explicit FixtSetup(std::vector<std::shared_ptr<const fixpp::dict::Dictionary>> dicts)
        : registry(dicts) {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    // Build a FIXT acceptor SessionConfig (sender=ISLD, target=TW).
    [[nodiscard]] fixpp::session::SessionConfig make_acceptor_cfg(
        application_version default_appl) {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIXT.1.1";
        cfg.heartbeat_interval = std::chrono::seconds{30};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = make_dict(kMinimalFix44Xml);  // session-layer dict (non-null)
        cfg.executor_override = ioc.get_executor();
        cfg.role = fixpp::session::session_role::acceptor;
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.default_appl_ver_id = default_appl;
        return cfg;
    }

    // Build a FIXT initiator SessionConfig (sender=TW, target=ISLD).
    [[nodiscard]] fixpp::session::SessionConfig make_initiator_cfg(
        application_version default_appl) {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "TW";
        cfg.target_comp_id = "ISLD";
        cfg.begin_string = "FIXT.1.1";
        cfg.heartbeat_interval = std::chrono::seconds{30};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = make_dict(kMinimalFix44Xml);  // session-layer dict (non-null)
        cfg.executor_override = ioc.get_executor();
        cfg.role = fixpp::session::session_role::initiator;
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.default_appl_ver_id = default_appl;
        return cfg;
    }
};

// ── T011 / W1 — Full round-trip witness ─────────────────────────────────────
//
// Both sides reach Active AND negotiated_version_profile().default_appl == v50sp2.
// The discriminating assertion is the negotiated version (pre-T018: returns Unknown).
// Also asserts wire 1137 present on both Logon frames (C1/C3).
//
// [contracts C3/C6; data-model E2; FR-001/FR-004; INV-FIXT-2; T011]

TEST(FixtLogonEstablishment, W1_FullRoundTrip_BothActive_NegotiatedV50sp2) {
    auto v50sp2_dict = make_dict(kMinimalFix50sp2Xml);
    FixtSetup s{{v50sp2_dict}};
    const version_registry* reg = &s.registry;

    // Capture outbound frames from both sides — set BEFORE constructing Sessions.
    std::vector<std::byte> init_frame_out;
    std::vector<std::byte> acpt_frame_out;

    // Acceptor: ISLD, default_appl = v50sp2
    auto acpt_cfg = s.make_acceptor_cfg(application_version::v50sp2);
    acpt_cfg.transport_send = [&](std::span<const std::byte> f) {
        acpt_frame_out.assign(f.begin(), f.end());
    };
    fixpp::session::Session acceptor(s.engine, acpt_cfg, reg);

    // Initiator: TW, default_appl = v50sp2
    auto init_cfg = s.make_initiator_cfg(application_version::v50sp2);
    init_cfg.transport_send = [&](std::span<const std::byte> f) {
        init_frame_out.assign(f.begin(), f.end());
    };
    fixpp::session::Session initiator(s.engine, init_cfg, reg);

    // Open acceptor (enters NotConnected waiting state)
    run_sync(s.ioc, [&] { return acceptor.open(); });
    // Open initiator (emits own Logon → LogonSent)
    run_sync(s.ioc, [&] { return initiator.open(); });

    ASSERT_FALSE(init_frame_out.empty()) << "Initiator should have emitted a Logon";

    // Verify initiator's Logon carries 1137 (C1 / T016/T017 already assert this,
    // but we need the wire value for feeding the acceptor).
    EXPECT_TRUE(wire_has_tag(init_frame_out, 1137)) << "Initiator FIXT Logon must carry 1137 (C1)";

    // Feed initiator's Logon to acceptor.
    // The acceptor sees the peer (TW→ISLD) Logon with 1137=9 (v50sp2).
    auto acpt_r = run_sync(s.ioc, [&] {
        return acceptor.on_inbound_frame(
            std::span<const std::byte>{init_frame_out.data(), init_frame_out.size()});
    });
    ASSERT_TRUE(acpt_r.has_value())
        << "Acceptor on_inbound_frame must succeed for valid FIXT Logon";

    // Acceptor should have emitted its reply Logon (carries its own 1137).
    EXPECT_FALSE(acpt_frame_out.empty()) << "Acceptor should have emitted a reply Logon";
    EXPECT_TRUE(wire_has_tag(acpt_frame_out, 1137))
        << "Acceptor reply FIXT Logon must carry 1137 (C1 / T017)";

    // Acceptor must be Active (C3).
    ASSERT_EQ(acceptor.state(), fsm_state::Active)
        << "Acceptor must be Active after valid FIXT Logon with serviceable 1137";

    // Acceptor's negotiated version must be v50sp2 — this is the T018 discriminator.
    // Pre-T018: negotiated_version_profile().default_appl == Unknown.
    // Post-T018: == v50sp2. [C3/C6; data-model E2; INV-FIXT-2]
    EXPECT_EQ(acceptor.negotiated_version_profile().default_appl, application_version::v50sp2)
        << "Acceptor must record negotiated_appl_version_=v50sp2 after serviceable FIXT Logon "
        << "(pre-T018: returns Unknown — this distinguishes T018 behavior)";

    // Feed acceptor's reply Logon to initiator.
    auto init_r = run_sync(s.ioc, [&] {
        return initiator.on_inbound_frame(
            std::span<const std::byte>{acpt_frame_out.data(), acpt_frame_out.size()});
    });
    ASSERT_TRUE(init_r.has_value())
        << "Initiator on_inbound_frame must succeed for valid acceptor Logon-ack";

    // Initiator must be Active.
    ASSERT_EQ(initiator.state(), fsm_state::Active)
        << "Initiator must be Active after valid acceptor Logon-ack";

    // Initiator's negotiated version must also be v50sp2.
    EXPECT_EQ(initiator.negotiated_version_profile().default_appl, application_version::v50sp2)
        << "Initiator must record negotiated_appl_version_=v50sp2 from peer Logon-ack";
}

// ── 039 US3/US5 — Absent-1137 acceptor Logon-ack on the INITIATOR ────────────
//
// FR-004a (unserviceable/missing-1137 → Reject) is ACCEPTOR-scoped only (L-033-3).
// When fixpp is the INITIATOR and the peer's Logon-ack omits 1137, the initiator
// does NOT auto-reject — it reaches Active and leaves negotiated_appl_version_ at
// Unknown, so negotiated_version_profile() returns the unnegotiated fallback
// {session=Unknown, default_appl=Unknown}.
//
// This witness (039 US3, FR-010/SC-004) covers negotiated_version_profile's
// Unknown-fallback arm (session.cpp:189-192) — a 033-introduced line previously
// unexercised by the FIXT corpus (every other FIXT witness negotiates a known
// version) — AND validates the L-033-3 / B-033-1 documented behavior (039 US5).
// Discriminator: pre-fix none — the arm is simply reached for the first time;
// the assertion is the {Unknown,Unknown} value, which only this absent-1137 path
// produces (the W1/W5 negotiated paths return v50sp2/v44).
TEST(FixtLogonEstablishment, InitiatorAbsent1137Ack_ReachesActive_NegotiatedUnknown) {
    auto v50sp2_dict = make_dict(kMinimalFix50sp2Xml);
    FixtSetup s{{v50sp2_dict}};
    const version_registry* reg = &s.registry;

    std::vector<std::byte> init_frame_out;
    auto init_cfg = s.make_initiator_cfg(application_version::v50sp2);
    init_cfg.transport_send = [&](std::span<const std::byte> f) {
        init_frame_out.assign(f.begin(), f.end());
    };
    fixpp::session::Session initiator(s.engine, init_cfg, reg);

    run_sync(s.ioc, [&] { return initiator.open(); });
    ASSERT_FALSE(init_frame_out.empty()) << "Initiator should have emitted a Logon";

    // Synthetic acceptor Logon-ack (ISLD→TW) WITHOUT 1137 (empty default_appl_ver_id).
    auto ack_no_1137 = make_fixt_logon_frame("FIXT.1.1", 1, "ISLD", "TW", 30, "");
    auto init_r = run_sync(s.ioc, [&] {
        return initiator.on_inbound_frame(
            std::span<const std::byte>{ack_no_1137.data(), ack_no_1137.size()});
    });
    ASSERT_TRUE(init_r.has_value())
        << "Initiator must accept a 1137-less acceptor Logon-ack — no auto-reject "
           "(FR-004a is acceptor-scoped; L-033-3)";

    // L-033-3: initiator does NOT auto-reject a missing/unserviceable peer 1137.
    ASSERT_EQ(initiator.state(), fsm_state::Active)
        << "Initiator must reach Active on a 1137-less ack (L-033-3 deferred-by-design)";

    // The unnegotiated fallback (session.cpp:189-192): no application version recorded.
    auto profile = initiator.negotiated_version_profile();
    EXPECT_EQ(profile.default_appl, application_version::Unknown)
        << "Absent 1137-ack → negotiated_appl_version_ stays Unknown";
    EXPECT_EQ(profile.session, fixpp::dict::session_version::Unknown)
        << "Unknown-fallback returns session=Unknown too (negotiated_version_profile arm)";
}

// ── T012 / W2 — Missing-1137 witness ─────────────────────────────────────────
//
// Acceptor receives a FIXT Logon WITHOUT 1137. Per C4/FR-004:
//   - Must emit Reject(35=3) with 373=1 (RequiredTagMissing)
//   - Must NOT reach Active
//   - 373=1 distinguishes this from W3's 373=5 (unserviceable)
//
// [contracts C4; research R7; data-model E2 (NOT set); INV-FIXT-2; T012]

TEST(FixtLogonEstablishment, W2_Missing1137_AcceptorRejectsWithRTM_NotActive) {
    auto v50sp2_dict = make_dict(kMinimalFix50sp2Xml);
    FixtSetup s{{v50sp2_dict}};

    auto acpt_cfg = s.make_acceptor_cfg(application_version::v50sp2);
    std::vector<std::byte> acpt_frame_out;
    acpt_cfg.transport_send = [&](std::span<const std::byte> f) {
        acpt_frame_out.assign(f.begin(), f.end());
    };
    fixpp::session::Session acceptor(s.engine, acpt_cfg, &s.registry);

    run_sync(s.ioc, [&] { return acceptor.open(); });

    // Build FIXT Logon WITHOUT 1137 (empty default_appl_ver_id).
    // Peer: TW sends to ISLD.
    auto logon_no_1137 = make_fixt_logon_frame("FIXT.1.1", 1, "TW", "ISLD", 30, "");

    auto r = run_sync(s.ioc, [&] {
        return acceptor.on_inbound_frame(
            std::span<const std::byte>{logon_no_1137.data(), logon_no_1137.size()});
    });
    // The return value may or may not be an error depending on how reject+disconnect
    // is wired — what matters is the behavioral outcome.
    (void)r;

    // (a) Acceptor must NOT be Active (C4 / INV-FIXT-2).
    EXPECT_NE(acceptor.state(), fsm_state::Active)
        << "Acceptor must NOT reach Active when 1137 is absent (C4)";

    // (b) A Reject(35=3) frame must have been emitted with 373=1 (RequiredTagMissing).
    ASSERT_FALSE(acpt_frame_out.empty())
        << "Acceptor must emit a Reject frame when 1137 is missing (C4/R7)";

    // Find the 35=3 Reject in the emitted frame.
    std::string wire(reinterpret_cast<const char*>(acpt_frame_out.data()), acpt_frame_out.size());
    EXPECT_NE(wire.find("\x01"
                        "35=3\x01"),
              std::string::npos)
        << "Emitted frame must be a Reject (35=3) for missing 1137; "
        << "got: " << wire;

    // 373=1 (RequiredTagMissing) — discriminates from W3's 373=5.
    EXPECT_NE(wire.find("\x01"
                        "373=1\x01"),
              std::string::npos)
        << "Reject must carry 373=1 (RequiredTagMissing) for missing 1137 (C4); "
        << "got: " << wire;
}

// ── T013 / W3 — Unserviceable-1137 witness ───────────────────────────────────
//
// Acceptor receives a FIXT Logon with 1137=9 (v50sp2) but the registry does NOT
// contain v50sp2 → unserviceable. Per C5/FR-004a:
//   - Must emit Reject with 373=5 (ValueIsIncorrect) AND 371=1137
//   - Must NOT reach Active
//   - 373=5 DISTINGUISHES this from W2's 373=1 (RequiredTagMissing)
//
// [contracts C5; research R2; data-model E2; INV-FIXT-2; T013]
// [feedback_witness_asserts_named_postcondition_not_proxy: assert 373 value directly]

TEST(FixtLogonEstablishment, W3_Unserviceable1137_AcceptorRejectsWithVII_NotActive) {
    // Build registry with ONLY v44 dict — so v50sp2 (1137=9) is unserviceable.
    auto v44_dict = make_dict(kMinimalFix44Xml);
    FixtSetup s{{v44_dict}};  // registry: v44 only; v50sp2 absent

    // Acceptor configured with v50sp2 but registry lacks it.
    auto acpt_cfg = s.make_acceptor_cfg(application_version::v50sp2);
    std::vector<std::byte> acpt_frame_out;
    acpt_cfg.transport_send = [&](std::span<const std::byte> f) {
        acpt_frame_out.assign(f.begin(), f.end());
    };
    fixpp::session::Session acceptor(s.engine, acpt_cfg, &s.registry);

    run_sync(s.ioc, [&] { return acceptor.open(); });

    // Peer (TW) sends FIXT Logon with 1137=9 (v50sp2) — present but unserviceable.
    auto logon_unserviceable = make_fixt_logon_frame("FIXT.1.1", 1, "TW", "ISLD", 30, "9");

    auto r = run_sync(s.ioc, [&] {
        return acceptor.on_inbound_frame(
            std::span<const std::byte>{logon_unserviceable.data(), logon_unserviceable.size()});
    });
    (void)r;

    // (a) Must NOT be Active (C5 / INV-FIXT-2).
    EXPECT_NE(acceptor.state(), fsm_state::Active)
        << "Acceptor must NOT reach Active when 1137 is unserviceable (C5)";

    // (b) Reject(35=3) must be emitted.
    ASSERT_FALSE(acpt_frame_out.empty())
        << "Acceptor must emit a Reject frame when 1137 is unserviceable (C5)";

    std::string wire(reinterpret_cast<const char*>(acpt_frame_out.data()), acpt_frame_out.size());
    EXPECT_NE(wire.find("\x01"
                        "35=3\x01"),
              std::string::npos)
        << "Emitted frame must be a Reject (35=3) for unserviceable 1137; "
        << "got: " << wire;

    // (c) 373=5 (ValueIsIncorrect) — DISTINCT from W2's 373=1.
    // This is the primary discriminator: 373=5 proves the unserviceable path
    // rather than the missing path.
    EXPECT_NE(wire.find("\x01"
                        "373=5\x01"),
              std::string::npos)
        << "Reject must carry 373=5 (ValueIsIncorrect) for unserviceable 1137 (C5); "
        << "got: " << wire;

    // (d) 371=1137 (RefTagID) — identifies which field was unserviceable.
    EXPECT_NE(wire.find("\x01"
                        "371=1137\x01"),
              std::string::npos)
        << "Reject must carry 371=1137 (RefTagID=DefaultApplVerID) (C5); "
        << "got: " << wire;

    // Confirm 373=5 and NOT 373=1 (non-overlap with W2).
    EXPECT_EQ(wire.find("\x01"
                        "373=1\x01"),
              std::string::npos)
        << "Unserviceable path must emit 373=5, NOT 373=1 (W3 vs W2 discriminator); "
        << "got: " << wire;
}

// ── T015 / W5 — Version-general witness ──────────────────────────────────────
//
// Two FIXT sessions: one with v44 (1137=6) and one with v50sp2 (1137=9).
// Both must negotiate successfully and record the correct application_version.
// Asserted via negotiated_version_profile().default_appl.
//
// [contracts C6; research R1; data-model E2; SC-006/W5; T015]

TEST(FixtLogonEstablishment, W5_VersionGeneral_V44_NegotiatesCorrectly) {
    // Registry with v44 dict only.
    auto v44_dict = make_dict(kMinimalFix44Xml);
    FixtSetup s{{v44_dict}};

    // Acceptor configured with v44.
    auto acpt_cfg = s.make_acceptor_cfg(application_version::v44);
    std::vector<std::byte> acpt_frame_out;
    acpt_cfg.transport_send = [&](std::span<const std::byte> f) {
        acpt_frame_out.assign(f.begin(), f.end());
    };
    fixpp::session::Session acceptor(s.engine, acpt_cfg, &s.registry);

    run_sync(s.ioc, [&] { return acceptor.open(); });

    // Peer (TW) sends FIXT Logon with 1137=6 (v44 wire value).
    auto logon_v44 = make_fixt_logon_frame("FIXT.1.1", 1, "TW", "ISLD", 30, "6");
    auto r = run_sync(s.ioc, [&] {
        return acceptor.on_inbound_frame(
            std::span<const std::byte>{logon_v44.data(), logon_v44.size()});
    });
    ASSERT_TRUE(r.has_value()) << "on_inbound_frame must succeed for 1137=6 (v44)";
    ASSERT_EQ(acceptor.state(), fsm_state::Active) << "Acceptor must be Active after v44 1137";

    // Negotiated version must be v44.
    EXPECT_EQ(acceptor.negotiated_version_profile().default_appl, application_version::v44)
        << "Acceptor must negotiate application_version::v44 from 1137=6 (wire value for FIX 4.4)";
}

TEST(FixtLogonEstablishment, W5_VersionGeneral_V50sp2_NegotiatesCorrectly) {
    // Registry with v50sp2 dict only.
    auto v50sp2_dict = make_dict(kMinimalFix50sp2Xml);
    FixtSetup s{{v50sp2_dict}};

    // Acceptor configured with v50sp2.
    auto acpt_cfg = s.make_acceptor_cfg(application_version::v50sp2);
    std::vector<std::byte> acpt_frame_out;
    acpt_cfg.transport_send = [&](std::span<const std::byte> f) {
        acpt_frame_out.assign(f.begin(), f.end());
    };
    fixpp::session::Session acceptor(s.engine, acpt_cfg, &s.registry);

    run_sync(s.ioc, [&] { return acceptor.open(); });

    // Peer (TW) sends FIXT Logon with 1137=9 (v50sp2 wire value).
    auto logon_v50sp2 = make_fixt_logon_frame("FIXT.1.1", 1, "TW", "ISLD", 30, "9");
    auto r = run_sync(s.ioc, [&] {
        return acceptor.on_inbound_frame(
            std::span<const std::byte>{logon_v50sp2.data(), logon_v50sp2.size()});
    });
    ASSERT_TRUE(r.has_value()) << "on_inbound_frame must succeed for 1137=9 (v50sp2)";
    ASSERT_EQ(acceptor.state(), fsm_state::Active) << "Acceptor must be Active after v50sp2 1137";

    // Negotiated version must be v50sp2.
    EXPECT_EQ(acceptor.negotiated_version_profile().default_appl, application_version::v50sp2)
        << "Acceptor must negotiate application_version::v50sp2 from 1137=9 (wire value)";
}

// ── T035 / W8 — 1128 tolerance (INV-FIXT-3) ──────────────────────────────────
//
// An established FIXT session receives an inbound app message (Heartbeat) with
// ApplVerID(1128) field. Per INV-FIXT-3 / C9 / R4:
//   - Session delivers the message dict-free to fromApp (no reify, no parse failure)
//   - Session stays Active (no shutdown on 1128 presence)
//   - fromApp is called (not silently dropped)
//
// Note: This is a tolerance/INV-FIXT-3 guard. It is green even pre-T018 because
// the session delivers all app messages dict-free regardless. The discriminating
// setup is that the session IS active via a complete FIXT handshake with 1137
// negotiation (post-T018), and 1128 does not disrupt it.
//
// [contracts C9; data-model INV-FIXT-3; research R4; T035]

// Counting Application for W8 delivery assertion.
class W8CountingApp : public fixpp::session::Application {
public:
    int from_app_calls = 0;
    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const fixpp::session::SessionId& /*id*/) override {
        ++from_app_calls;
        return {};
    }
};

TEST(FixtLogonEstablishment, W8_1128Tolerance_DeliveredDictFree_StaysActive) {
    auto v50sp2_dict = make_dict(kMinimalFix50sp2Xml);
    FixtSetup s{{v50sp2_dict}};

    // Register a counting Application on the engine so fromApp calls are observable.
    auto app = std::make_shared<W8CountingApp>();
    s.engine.application = app;

    // Establish a FIXT session (acceptor side).
    auto acpt_cfg = s.make_acceptor_cfg(application_version::v50sp2);

    std::vector<std::byte> acpt_frame_out;
    acpt_cfg.transport_send = [&](std::span<const std::byte> f) {
        acpt_frame_out.assign(f.begin(), f.end());
    };
    fixpp::session::Session acceptor(s.engine, acpt_cfg, &s.registry);

    run_sync(s.ioc, [&] { return acceptor.open(); });

    // Step 1: Complete FIXT handshake to reach Active.
    // Feed a valid FIXT Logon with 1137=9 (v50sp2).
    auto logon = make_fixt_logon_frame("FIXT.1.1", 1, "TW", "ISLD", 30, "9");
    auto handshake_r = run_sync(s.ioc, [&] {
        return acceptor.on_inbound_frame(std::span<const std::byte>{logon.data(), logon.size()});
    });
    ASSERT_TRUE(handshake_r.has_value()) << "Handshake must succeed";
    ASSERT_EQ(acceptor.state(), fsm_state::Active) << "Must be Active before feeding 1128 message";

    // Step 2: Feed an inbound app message (Heartbeat, 35=0) with ApplVerID(1128).
    // Heartbeat (35=0) is an admin message, not app. We need an app message.
    // Use a NewOrderSingle (35=D) which is an app message. Since we deliver dict-free,
    // the dictionary doesn't need to know the message type — it's classified by MsgType.
    // [R4: Session delivers dict-free; never calls dict::reify for inbound messages]
    //
    // Build a minimal app frame (35=D / NewOrderSingle) with 1128 field.
    // Note: 35=0 (Heartbeat) is admin — it won't reach fromApp. Use a non-admin msgtype.
    {
        std::string body;
        body += "35=D\x01";  // NewOrderSingle — non-admin, reaches fromApp
        body += "34=2\x01";
        body += "49=TW\x01";
        body += "52=20240101-00:00:00.000\x01";
        body += "56=ISLD\x01";
        body += "1128=9\x01";  // ApplVerID(1128) = v50sp2

        std::string hdr;
        hdr += "8=FIXT.1.1\x01";
        hdr += "9=" + std::to_string(body.size()) + "\x01";
        std::string full = hdr + body;
        unsigned int cs = 0;
        for (unsigned char c : full) cs += c;
        cs &= 0xFFU;
        char csbuf[4];
        snprintf(csbuf, sizeof(csbuf), "%03u", cs);
        full += "10=" + std::string(csbuf) + "\x01";

        std::vector<std::byte> app_frame;
        app_frame.reserve(full.size());
        for (char c : full) app_frame.push_back(static_cast<std::byte>(c));

        auto app_r = run_sync(s.ioc, [&] {
            return acceptor.on_inbound_frame(
                std::span<const std::byte>{app_frame.data(), app_frame.size()});
        });

        // (a) No session failure on 1128 presence (C9/INV-FIXT-3).
        ASSERT_TRUE(app_r.has_value()) << "Session must not fail on inbound app msg with 1128";

        // (b) Session stays Active (C9).
        EXPECT_EQ(acceptor.state(), fsm_state::Active)
            << "Session must stay Active after inbound app message with 1128";

        // (c) fromApp was called — message delivered dict-free, not dropped (C9/INV-FIXT-3).
        // Discriminating: from_app_calls was 0 before step 2, must be > 0 after.
        EXPECT_GT(app->from_app_calls, 0)
            << "fromApp must be invoked for inbound app message with 1128 "
            << "(INV-FIXT-3: dict-free delivery)";
    }
}

// ── FQ-1 Gate B r1 — open()-time FIXT config validation witnesses ────────────
//
// (a) Initiator: begin_string=="FIXT.1.1" + default_appl_ver_id UNSET → open()
//     returns invalid_session_config, no Logon emitted, no Active.
//     Isolates the "default_appl_ver_id" OR-arm of the guard.
//     Registry is NON-null (FixtSetup) so only the missing-version arm fires.
//
// (b) Acceptor: begin_string=="FIXT.1.1" + default_appl_ver_id SET + null registry
//     (test-ctor path, omit 3rd arg) → open() returns invalid_session_config.
//     Isolates the "registry==nullptr" OR-arm of the guard.
//
// Both must FAIL (open() succeeds and emits garbage) before the fix;
// PASS after.  [FQ-1; FR-003; data-model E3; session_config.hpp:440]

TEST(FixtOpenValidation, FQ1a_MissingDefaultApplVerId_ReturnsInvalidConfig_NoLogon) {
    // Use a non-null registry so only the default_appl_ver_id arm can trip.
    auto v50sp2_dict = make_dict(kMinimalFix50sp2Xml);
    FixtSetup s{{v50sp2_dict}};

    std::vector<std::byte> emitted;
    auto cfg = s.make_initiator_cfg(application_version::v50sp2);
    // Explicitly clear default_appl_ver_id — isolates the missing-version arm.
    cfg.default_appl_ver_id = std::nullopt;
    cfg.transport_send = [&](std::span<const std::byte> f) {
        emitted.assign(f.begin(), f.end());
    };

    // Pass non-null registry so the registry arm does NOT fire.
    fixpp::session::Session sess(s.engine, cfg, &s.registry);
    auto result = run_sync(s.ioc, [&] { return sess.open(); });

    // Must return an error (invalid_session_config).
    ASSERT_FALSE(result.has_value())
        << "open() must fail for FIXT.1.1 + unset default_appl_ver_id (FQ-1a)";
    EXPECT_EQ(result.error(), fixpp::core::error::invalid_session_config)
        << "Error must be invalid_session_config, not: "
        << static_cast<int>(result.error());

    // No Logon must have been emitted.
    EXPECT_TRUE(emitted.empty())
        << "No frame must be emitted when open() rejects (FQ-1a)";

    // Session must NOT be Active.
    EXPECT_NE(sess.state(), fsm_state::Active)
        << "Session must NOT reach Active on invalid config (FQ-1a)";
}

TEST(FixtOpenValidation, FQ1b_NullRegistry_ReturnsInvalidConfig) {
    // Acceptor: default_appl_ver_id SET, but registry is null (test-ctor default).
    // Isolates the registry==nullptr OR-arm of the guard.
    auto v50sp2_dict = make_dict(kMinimalFix50sp2Xml);
    FixtSetup s{{v50sp2_dict}};

    std::vector<std::byte> emitted;
    auto cfg = s.make_acceptor_cfg(application_version::v50sp2);
    cfg.transport_send = [&](std::span<const std::byte> f) {
        emitted.assign(f.begin(), f.end());
    };

    // Omit 3rd argument → app_version_registry_ == nullptr (test-ctor path).
    fixpp::session::Session sess(s.engine, cfg /*, no registry */);
    auto result = run_sync(s.ioc, [&] { return sess.open(); });

    // Must return an error.
    ASSERT_FALSE(result.has_value())
        << "open() must fail for FIXT.1.1 acceptor with null registry (FQ-1b)";
    EXPECT_EQ(result.error(), fixpp::core::error::invalid_session_config)
        << "Error must be invalid_session_config, not: "
        << static_cast<int>(result.error());

    // No frame emitted.
    EXPECT_TRUE(emitted.empty())
        << "No frame must be emitted when open() rejects (FQ-1b)";

    // Not Active.
    EXPECT_NE(sess.state(), fsm_state::Active)
        << "Session must NOT reach Active on null registry (FQ-1b)";
}

// ── 038 T013 [US3] — FIXT DefaultApplVerID(1137) reject witnesses ────────────
//
// The existing acceptor 1137 reject arms (session.cpp:~2146-2208: absent → Reject
// 373=1; non-conformant → Reject 373=5, both RefTagID=1137, then Disconnected)
// are fail-closed by code-read but had zero session-level negative witnesses with
// toAdmin observation (W2/W3 cover wire shape + state only). These cells add the
// missing toAdmin + gate-ordering witnesses.
//
// (a) Absent 1137 → Reject(35=3, 371=1137, 373=1 RequiredTagMissing) + toAdmin
//     observed + Disconnected.
// (b) Non-conformant 1137 → Reject(371=1137, 373=5 ValueIsIncorrect) + toAdmin
//     + Disconnected. 373=5 DISCRIMINATES from (a)'s 373=1.
// (c) [US1 Judge-pass carry-over] — Ordering witness: FIXT + stale-52 +
//     missing-1137 → Reject(371=52, 373=10) wins; NO 371=1137 on wire;
//     Disconnected. Proves the SendingTime guard (session.cpp:~1939) pre-empts
//     the FIXT 1137 gate (session.cpp:~2146). NOTE: FixtSetup has an active
//     mock_clock (engine.clock = clock); the 52 guard is gated on
//     effective_clock_ != null, so the clock MUST be present. A stale sending_time
//     is used (far outside the 120s default threshold).
//
// Production change: NONE (FR-009). These characterize existing behaviour.
//
// [038 tasks.md T013; 038 plan G3/FR-008/FR-009; contracts C-1 (G1) + C3 (G3)]
// [033 contracts C4/C5; S-025 / [FIX-SL §4.3.7]]
// [[feedback_witness_asserts_named_postcondition_not_proxy]]: assert each clause
//   directly (toAdmin count + 371 presence/absence on wire).

// Counting Application for toAdmin observation.
class ToAdminCountingApp : public fixpp::session::Application {
public:
    int to_admin_calls = 0;
    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
                 const fixpp::session::SessionId& /*id*/) override {
        ++to_admin_calls;
    }
};

// (a) Absent 1137 → Reject(371=1137, 373=1) + toAdmin observed + Disconnected.
// Reuses FixtSetup with a non-null registry (v50sp2); acceptor configured with
// v50sp2. The peer's Logon omits 1137 → RequiredTagMissing path.
// Characterizes existing behaviour — should be GREEN.
TEST(FixtLogonEstablishment,
     W_Missing1137_ToAdminObserved_RequiredTagMissing_Disconnected) {
    auto v50sp2_dict = make_dict(kMinimalFix50sp2Xml);
    FixtSetup s{{v50sp2_dict}};

    // Register a counting Application to observe toAdmin.
    auto app = std::make_shared<ToAdminCountingApp>();
    s.engine.application = app;

    auto acpt_cfg = s.make_acceptor_cfg(application_version::v50sp2);
    std::vector<std::byte> acpt_frame_out;
    acpt_cfg.transport_send = [&](std::span<const std::byte> f) {
        acpt_frame_out.assign(f.begin(), f.end());
    };
    fixpp::session::Session acceptor(s.engine, acpt_cfg, &s.registry);

    run_sync(s.ioc, [&] { return acceptor.open(); });

    // Feed a FIXT Logon WITHOUT 1137 (empty default_appl_ver_id).
    auto logon_no_1137 = make_fixt_logon_frame("FIXT.1.1", 1, "TW", "ISLD", 30, "");
    auto r = run_sync(s.ioc, [&] {
        return acceptor.on_inbound_frame(
            std::span<const std::byte>{logon_no_1137.data(), logon_no_1137.size()});
    });
    (void)r;

    // (a) Must be Disconnected (not Active).
    EXPECT_NE(acceptor.state(), fsm_state::Active)
        << "Acceptor must NOT reach Active when 1137 is absent (C4)";

    // (b) A Reject(35=3) frame must have been emitted with 371=1137, 373=1.
    ASSERT_FALSE(acpt_frame_out.empty())
        << "Acceptor must emit a Reject frame when 1137 is missing";
    std::string wire(reinterpret_cast<const char*>(acpt_frame_out.data()),
                     acpt_frame_out.size());
    EXPECT_NE(wire.find("\x01"
                        "35=3\x01"),
              std::string::npos)
        << "Emitted frame must be Reject(35=3) for missing 1137; got: " << wire;
    EXPECT_NE(wire.find("\x01"
                        "371=1137\x01"),
              std::string::npos)
        << "Reject must carry 371=1137 (RefTagID=DefaultApplVerID); got: " << wire;
    EXPECT_NE(wire.find("\x01"
                        "373=1\x01"),
              std::string::npos)
        << "Reject must carry 373=1 (RequiredTagMissing) for missing 1137; got: " << wire;

    // (c) toAdmin was called (the Reject was observed via fire_to_admin_).
    // [[feedback_witness_asserts_named_postcondition_not_proxy]]: assert directly.
    EXPECT_GT(app->to_admin_calls, 0)
        << "toAdmin must be called for the Reject frame (fire_to_admin_ path; "
        << "[[feedback_admin_emit_bypasses_fire_to_admin]] — every admin emit must be observable). "
        << "RED: if to_admin_calls==0, the 1137 reject does not route through fire_to_admin_.";
}

// (b) Non-conformant 1137 → Reject(371=1137, 373=5 ValueIsIncorrect) + toAdmin
// observed + Disconnected. 373=5 DISCRIMINATES from (a)'s 373=1.
// Registry has v44 only → v50sp2 (1137=9) is unserviceable.
TEST(FixtLogonEstablishment,
     W_Unserviceable1137_ToAdminObserved_ValueIsIncorrect_Disconnected) {
    // Registry with v44 only — v50sp2 (1137=9) is unserviceable.
    auto v44_dict = make_dict(kMinimalFix44Xml);
    FixtSetup s{{v44_dict}};

    // Register counting Application.
    auto app = std::make_shared<ToAdminCountingApp>();
    s.engine.application = app;

    auto acpt_cfg = s.make_acceptor_cfg(application_version::v50sp2);
    std::vector<std::byte> acpt_frame_out;
    acpt_cfg.transport_send = [&](std::span<const std::byte> f) {
        acpt_frame_out.assign(f.begin(), f.end());
    };
    fixpp::session::Session acceptor(s.engine, acpt_cfg, &s.registry);

    run_sync(s.ioc, [&] { return acceptor.open(); });

    // Peer sends FIXT Logon with 1137=9 (v50sp2) — present but unserviceable.
    auto logon_unserviceable = make_fixt_logon_frame("FIXT.1.1", 1, "TW", "ISLD", 30, "9");
    auto r = run_sync(s.ioc, [&] {
        return acceptor.on_inbound_frame(
            std::span<const std::byte>{logon_unserviceable.data(), logon_unserviceable.size()});
    });
    (void)r;

    // Must be Disconnected.
    EXPECT_NE(acceptor.state(), fsm_state::Active)
        << "Acceptor must NOT reach Active when 1137 is unserviceable (C5)";

    ASSERT_FALSE(acpt_frame_out.empty())
        << "Acceptor must emit a Reject frame when 1137 is unserviceable";
    std::string wire(reinterpret_cast<const char*>(acpt_frame_out.data()),
                     acpt_frame_out.size());
    EXPECT_NE(wire.find("\x01"
                        "35=3\x01"),
              std::string::npos)
        << "Emitted frame must be Reject(35=3) for unserviceable 1137; got: " << wire;
    EXPECT_NE(wire.find("\x01"
                        "371=1137\x01"),
              std::string::npos)
        << "Reject must carry 371=1137 (RefTagID=DefaultApplVerID); got: " << wire;
    // 373=5 (ValueIsIncorrect) — DISTINCT from (a)'s 373=1.
    EXPECT_NE(wire.find("\x01"
                        "373=5\x01"),
              std::string::npos)
        << "Reject must carry 373=5 (ValueIsIncorrect) for unserviceable 1137; "
        << "373=5 discriminates from absent-1137 path's 373=1; got: " << wire;
    // Confirm 373=1 is NOT present (non-overlap with (a)).
    EXPECT_EQ(wire.find("\x01"
                        "373=1\x01"),
              std::string::npos)
        << "Unserviceable path must emit 373=5, NOT 373=1; got: " << wire;

    // toAdmin observed.
    EXPECT_GT(app->to_admin_calls, 0)
        << "toAdmin must be called for the Reject frame (fire_to_admin_ path)";
}

// (c) Gate-ordering witness [US1 Judge-pass carry-over]:
//   FIXT session + stale SendingTime(52) + missing 1137:
//   → Reject(371=52, 373=10) wins (52 guard fires before 1137 gate);
//   → NO 371=1137 on the wire;
//   → Disconnected.
//
// This cell CANNOT be in US1's FIX.4.4 cells (T004 cell 10 — the 1137 gate is
// not reachable for FIX.4.4 sessions where is_fixt()==false). It belongs here
// where FixtSetup provides the FIXT context AND the mock_clock is active.
//
// Clock wiring: FixtSetup injects engine.clock = mock_clock (seeded at
//   UTC=2024-01-01 00:00:00). effective_clock_ = engine_.clock (Session ctor).
//   52 guard: gated on (effective_clock_ != nullptr) — TRUE here.
//   Stale timestamp: "20200101-00:00:00.000" is 4 years before the clock's UTC →
//   |52 - now| >> 120s default threshold → sending_time_ok = false → reject fires.
// 1137 gate: session.cpp:~2146, unreachable on the 52-reject path (52 returns
//   Disconnected before reaching the 1137 block).
//
// Discriminating assertion: 371=52 present AND 371=1137 ABSENT on wire.
// [[feedback_witness_asserts_named_postcondition_not_proxy]]: both clauses direct.
TEST(FixtLogonEstablishment,
     W_StaleSendingTime_Beats1137Gate_Reject52_No1137) {
    // Registry with v50sp2; acceptor configured v50sp2 (so 1137 gate is armed).
    // Only the 52 guard should fire — before the 1137 gate is reached.
    auto v50sp2_dict = make_dict(kMinimalFix50sp2Xml);
    FixtSetup s{{v50sp2_dict}};

    // The FixtSetup's mock_clock is seeded at UTC=2024-01-01 00:00:00.
    // effective_clock_ will be non-null (engine.clock == mock_clock).

    auto acpt_cfg = s.make_acceptor_cfg(application_version::v50sp2);
    std::vector<std::byte> acpt_frame_out;
    acpt_cfg.transport_send = [&](std::span<const std::byte> f) {
        acpt_frame_out.assign(f.begin(), f.end());
    };
    fixpp::session::Session acceptor(s.engine, acpt_cfg, &s.registry);

    run_sync(s.ioc, [&] { return acceptor.open(); });

    // Build a FIXT Logon with:
    //   - stale 52 (2020-01-01, 4 years before the clock's UTC) → 52 guard fires
    //   - missing 1137 (empty default_appl_ver_id) → would normally fire 373=1
    //     but the 52 guard fires FIRST and returns Disconnected before reaching 1137.
    //
    // Sending time "20200101-00:00:00.000" is well outside the 120s default threshold.
    auto logon_stale52_no_1137 = make_fixt_logon_frame(
        "FIXT.1.1", 1, "TW", "ISLD", 30, "",  // missing 1137
        "20200101-00:00:00.000"                  // stale 52 (2020 vs clock 2024)
    );

    auto r = run_sync(s.ioc, [&] {
        return acceptor.on_inbound_frame(
            std::span<const std::byte>{logon_stale52_no_1137.data(),
                                       logon_stale52_no_1137.size()});
    });
    (void)r;

    // Must be Disconnected (both guards would reject, 52 wins).
    EXPECT_NE(acceptor.state(), fsm_state::Active)
        << "Acceptor must NOT reach Active (stale 52 + missing 1137 → 52 guard fires)";

    // A frame must have been emitted.
    ASSERT_FALSE(acpt_frame_out.empty())
        << "Acceptor must emit a Reject frame (52 guard fires)";

    std::string wire(reinterpret_cast<const char*>(acpt_frame_out.data()),
                     acpt_frame_out.size());

    // Reject(35=3) emitted.
    EXPECT_NE(wire.find("\x01"
                        "35=3\x01"),
              std::string::npos)
        << "Emitted frame must be Reject(35=3) for stale 52; got: " << wire;

    // 371=52 (RefTagID = SendingTime) — the 52 guard's reject.
    EXPECT_NE(wire.find("\x01"
                        "371=52\x01"),
              std::string::npos)
        << "Reject must carry 371=52 (RefTagID=SendingTime) — 52 guard wins; "
        << "got: " << wire;

    // 373=10 (SendingTime accuracy reason).
    EXPECT_NE(wire.find("\x01"
                        "373=10\x01"),
              std::string::npos)
        << "Reject must carry 373=10 (SendingTimeAccuracy); got: " << wire;

    // NO 371=1137: the 52 guard returned Disconnected BEFORE the 1137 gate was reached.
    // This is the primary discriminator: if 371=1137 appears, the ordering is WRONG.
    EXPECT_EQ(wire.find("\x01"
                        "371=1137\x01"),
              std::string::npos)
        << "Reject must NOT carry 371=1137 — the 52 guard pre-empts the 1137 gate; "
        << "if 371=1137 is present, the guard-ordering is broken; got: " << wire;
}

}  // namespace fixpp_fixt_inbound

}  // namespace
