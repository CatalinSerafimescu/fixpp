// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_fixt_logon_establishment.cpp
//
// 033-fixt-fix50sp2-session — T002 (target creation) / T004 (RED render helper
// test) / T005 (impl: make T004 green) / T014 (W4 — FIX.4.x regression guard) /
// T016–T017 emit-half of W1.
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
//     Does NOT assert round-trip/Active yet (inbound arm is T018 / next phase).
//
// US1 witnesses (T011–T013, T015, T035 / W1-full/W2/W3/W5/W8) land in later phases.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <fixpp/core/error.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/session/admin_messages.hpp>

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
    using fixpp::dict::version_profile;
    using fixpp::dict::session_version;

    const version_profile kProfile{session_version::vt11, application_version::v50sp2, true, 0};

    constexpr application_version kAll[] = {
        application_version::v40,   application_version::v41,  application_version::v42,
        application_version::v43,   application_version::v44,  application_version::v50,
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
        << "  got (escaped):  " << [&]() {
               std::string esc;
               for (char c : frame) {
                   if (c == '\x01') esc += "\\x01";
                   else esc += c;
               }
               return esc;
           }()
        << "\n  want (escaped): "
        << [&]() {
               std::string esc;
               for (char c : kFix44LogonBaseline) {
                   if (c == '\x01') esc += "\\x01";
                   else esc += c;
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
    auto result = fixpp::session::build_logon(
        std::span<std::byte>(buf),
        /*seq=*/1,
        /*sender_comp_id=*/"SENDER",
        /*target_comp_id=*/"TARGET",
        /*begin_string=*/"FIXT.1.1",
        /*heartbt_int=*/30,
        /*sending_time=*/"20240101-00:00:00.000",
        /*reset_seqnum=*/false,
        /*next_expected_seq=*/std::nullopt,
        /*default_appl_ver_id=*/application_version::v50sp2
    );
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
    EXPECT_LT(pos_108, pos_1137)
        << "1137 must appear AFTER 108 in the wire frame (data-model E4)";

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
    auto result = fixpp::session::build_logon(
        std::span<std::byte>(buf),
        /*seq=*/1,
        /*sender_comp_id=*/"SENDER",
        /*target_comp_id=*/"TARGET",
        /*begin_string=*/"FIXT.1.1",
        /*heartbt_int=*/30,
        /*sending_time=*/"20240101-00:00:00.000",
        /*reset_seqnum=*/false,
        /*next_expected_seq=*/std::nullopt,
        /*default_appl_ver_id=*/application_version::v44
    );
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

}  // namespace
