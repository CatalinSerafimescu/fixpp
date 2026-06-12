// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_fixt_logon_establishment.cpp
//
// 033-fixt-fix50sp2-session — T002 (target creation) / T004 (RED render helper
// test) / T005 (impl: make T004 green).
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
// US1 witnesses (T011–T015, T035 / W1–W5, W8) land in a later phase:
// // US1: W1–W5, W8 land here

#include <gtest/gtest.h>

#include <fixpp/core/error.hpp>
#include <fixpp/dict/version_profile.hpp>

namespace {

using fixpp::core::error;
using fixpp::dict::application_version;
using fixpp::dict::render_appl_ver_id;

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

}  // namespace
