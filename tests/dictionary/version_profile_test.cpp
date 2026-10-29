// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/version_profile_test.cpp
//
// 003-dictionary-codegen — T004. AC-VP1..AC-VP6 (RC#1). The 003-owned
// version_profile / resolved_message_version structs + the free function
// dict::resolve_application_version + the full wire ApplVerID(1128)→C++
// application_version map + the AC-VP4 negative (C++ index NOT reused) +
// _reserved discipline + the six locked core::error slots. Pure / compile-
// time only (no wire::MessageView — the frozen R6 stub has no frame state;
// the running reify suites are R6-blocked, spec §11 R6). Oracle:
// specs/003-dictionary-codegen/contracts/version_profile.hpp; data-model
// Entity 10.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using fixpp::core::error;
using fixpp::dict::application_version;
using fixpp::dict::resolve_application_version;
using fixpp::dict::resolved_message_version;
using fixpp::dict::session_version;
using fixpp::dict::version_profile;

// AC-VP1 — struct shapes (re-asserted here for ABI-drift detection alongside
// the in-header static_asserts).
static_assert(sizeof(version_profile) == 4);
static_assert(std::is_trivially_copyable_v<version_profile>);
static_assert(sizeof(resolved_message_version) == 4);
static_assert(alignof(resolved_message_version) == 1);
static_assert(std::is_trivially_copyable_v<resolved_message_version>);

// AC-VP2 — resolve_application_version is a free function (not a member).
static_assert(
    std::is_same_v<decltype(resolve_application_version(std::declval<version_profile>(),
                                                        std::declval<std::string_view>())),
                   fixpp::core::expected_t<application_version>>);

constexpr version_profile kFixt{.session = session_version::vt11,
                                .default_appl = application_version::v50sp2,
                                .has_per_message_override = true,
                                ._reserved = 0};
constexpr version_profile kUnknownDefault{.session = session_version::vt11,
                                          .default_appl = application_version::Unknown,
                                          .has_per_message_override = true,
                                          ._reserved = 0};

TEST(VersionProfileAcVp1, ReservedZeroOnEmit) {
    // AC-VP5 — _reserved zero on emit, ignored on read in v1.0.
    version_profile p{.session = session_version::v44,
                      .default_appl = application_version::v44,
                      .has_per_message_override = false,
                      ._reserved = 0};
    EXPECT_EQ(p._reserved, 0);
    resolved_message_version r{.k = resolved_message_version::kind::application,
                               .session = session_version::v44,
                               .application = application_version::v44,
                               ._reserved = 0};
    EXPECT_EQ(r._reserved, 0);
}

TEST(VersionProfileAcVp3, FullWireToCppMap) {
    // AC-VP3 — every wire ApplVerID value "2".."9".
    struct Case {
        std::string_view wire;
        application_version expect;
    };
    constexpr Case cases[] = {
        {.wire = "2", .expect = application_version::v40},
        {.wire = "3", .expect = application_version::v41},
        {.wire = "4", .expect = application_version::v42},
        {.wire = "5", .expect = application_version::v43},
        {.wire = "6", .expect = application_version::v44},
        {.wire = "7", .expect = application_version::v50},
        {.wire = "8", .expect = application_version::v50sp1},
        {.wire = "9", .expect = application_version::v50sp2},
    };
    for (auto const& c : cases) {
        auto got = resolve_application_version(kFixt, c.wire);
        ASSERT_TRUE(got.has_value()) << "wire=" << c.wire;
        EXPECT_EQ(*got, c.expect) << "wire=" << c.wire;
    }
}

TEST(VersionProfileAcVp3, EmptyUsesDefault) {
    auto got = resolve_application_version(kFixt, "");
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(*got, application_version::v50sp2);
}

TEST(VersionProfileAcVp3, EmptyWithUnknownDefaultIsUnresolved) {
    // RC#1 / AC-D6 — NOT a sentinel fall-through to dict_reify_unknown_msg_type.
    auto got = resolve_application_version(kUnknownDefault, "");
    ASSERT_FALSE(got.has_value());
    EXPECT_EQ(got.error(), error::dict_unresolved_application_version);
}

TEST(VersionProfileAcVp3, NonParsingRejected) {
    for (std::string_view bad : {"0", "1", "A", "x", "99"}) {
        auto got = resolve_application_version(kFixt, bad);
        EXPECT_FALSE(got.has_value()) << "bad=" << bad;
        if (got.has_value()) {
            continue;
        }
        EXPECT_EQ(got.error(), error::dict_unknown_appl_ver_id) << "bad=" << bad;
    }
}

// 097-fix-latest-applverid-10 W-1 (string half): ApplVerID "10" is FIX Latest's own wire
// value, so it resolves to a value instead of dict_unknown_appl_ver_id. This half names no
// application_version member; the full wire table is W-1's other half.
TEST(VersionProfile097, Wire10Resolves) {
    auto got = resolve_application_version(kFixt, "10");
    if (!got.has_value()) {
        ADD_FAILURE() << "ApplVerID \"10\" must resolve to a value; got error code "
                      << static_cast<int>(got.error());
    }
}

// 097-fix-latest-applverid-10 W-1 (full table): every ApplVerID wire value from "0" through
// "10". "2".."9" keep their pre-097 mapping, "10" is FIX Latest (application_version::vlatest),
// and "0"/"1" stay refused with dict_unknown_appl_ver_id.
TEST(VersionProfile097, FullWireTableThrough10) {
    struct Case {
        std::string_view wire;
        application_version expect;
    };
    constexpr Case accepted[] = {
        {.wire = "2", .expect = application_version::v40},
        {.wire = "3", .expect = application_version::v41},
        {.wire = "4", .expect = application_version::v42},
        {.wire = "5", .expect = application_version::v43},
        {.wire = "6", .expect = application_version::v44},
        {.wire = "7", .expect = application_version::v50},
        {.wire = "8", .expect = application_version::v50sp1},
        {.wire = "9", .expect = application_version::v50sp2},
        {.wire = "10", .expect = application_version::vlatest},
    };
    for (auto const& c : accepted) {
        auto got = resolve_application_version(kFixt, c.wire);
        EXPECT_TRUE(got.has_value()) << "wire=" << c.wire;
        if (!got.has_value()) {
            continue;
        }
        EXPECT_EQ(*got, c.expect) << "wire=" << c.wire;
    }
    for (std::string_view refused : {"0", "1"}) {
        auto got = resolve_application_version(kFixt, refused);
        EXPECT_FALSE(got.has_value()) << "wire=" << refused;
        if (got.has_value()) {
            continue;
        }
        EXPECT_EQ(got.error(), error::dict_unknown_appl_ver_id) << "wire=" << refused;
    }
}

// 097 W-1 (malformed): near-misses of "10" keep dict_unknown_appl_ver_id: the "10" arm
// matches the exact string "10".
TEST(VersionProfile097, MalformedNear10StayRefused) {
    for (std::string_view bad : {"10 ", "010", "1O", "11", "100", "99"}) {
        auto got = resolve_application_version(kFixt, bad);
        EXPECT_FALSE(got.has_value()) << "bad=\"" << bad << "\"";
        if (got.has_value()) {
            continue;
        }
        EXPECT_EQ(got.error(), error::dict_unknown_appl_ver_id) << "bad=\"" << bad << "\"";
    }
}

// 097 W-2 (render): render(vlatest) is "10"; over every renderable member v40..vlatest,
// render then parse is the identity and the rendered strings are pairwise distinct.
TEST(VersionProfile097, RenderVlatestIs10_RoundTripAndDistinct) {
    using fixpp::dict::render_appl_ver_id;
    auto latest = render_appl_ver_id(application_version::vlatest);
    EXPECT_TRUE(latest.has_value());
    if (latest.has_value()) {
        EXPECT_EQ(*latest, "10");
    }

    constexpr application_version renderable[] = {
        application_version::v40,    application_version::v41,    application_version::v42,
        application_version::v43,    application_version::v44,    application_version::v50,
        application_version::v50sp1, application_version::v50sp2, application_version::vlatest,
    };
    // Each member's checks are non-fatal, so a failing member does not hide the members
    // after it; `rendered` keeps the member beside its wire value for the distinctness pass.
    std::vector<std::pair<application_version, std::string_view>> rendered;
    for (auto v : renderable) {
        auto wire = render_appl_ver_id(v);
        EXPECT_TRUE(wire.has_value()) << "render failed for enum=" << static_cast<int>(v);
        if (!wire.has_value()) {
            continue;
        }
        rendered.emplace_back(v, *wire);
        auto back = resolve_application_version(kFixt, *wire);
        EXPECT_TRUE(back.has_value()) << "re-parse failed for wire=" << *wire;
        if (!back.has_value()) {
            continue;
        }
        EXPECT_EQ(*back, v) << "round-trip mismatch for enum=" << static_cast<int>(v)
                            << " wire=" << *wire;
    }
    for (std::size_t i = 0; i < rendered.size(); ++i) {
        for (std::size_t j = i + 1; j < rendered.size(); ++j) {
            EXPECT_NE(rendered[i].second, rendered[j].second)
                << "members " << static_cast<int>(rendered[i].first) << " and "
                << static_cast<int>(rendered[j].first) << " render the same wire value";
        }
    }
}

// 097 W-2: render(Unknown) stays the error dict_unknown_appl_ver_id.
TEST(VersionProfile097, RenderUnknownIsError) {
    auto got = fixpp::dict::render_appl_ver_id(application_version::Unknown);
    ASSERT_FALSE(got.has_value());
    EXPECT_EQ(got.error(), error::dict_unknown_appl_ver_id);
}

TEST(VersionProfileAcVp4, CppIndexNotReused) {
    // AC-VP4 negative — wire "7" is FIX 5.0 → application_version::v50, NOT the
    // C++ internal index 7 (which is v50sp1). Reusing the index mis-maps FIX
    // 5.0 + the SP variants (I-13 / N2-P3-1).
    auto got = resolve_application_version(kFixt, "7");
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(*got, application_version::v50);
    EXPECT_NE(*got, application_version::v50sp1);
    static_assert(static_cast<std::uint8_t>(application_version::v50sp1) == 7,
                  "guards the AC-VP4 premise: C++ index 7 == v50sp1");
}

TEST(VersionProfileAcVp6, SixErrorSlotsLocked) {
    // AC-VP6 — six 003-owned slots appended non-renumbering at 23..28.
    EXPECT_EQ(static_cast<std::uint8_t>(error::dict_reify_msg_type_mismatch), 23);
    EXPECT_EQ(static_cast<std::uint8_t>(error::dict_reify_unknown_msg_type), 24);
    EXPECT_EQ(static_cast<std::uint8_t>(error::dict_reify_oom), 25);
    EXPECT_EQ(static_cast<std::uint8_t>(error::dict_unresolved_application_version), 26);
    EXPECT_EQ(static_cast<std::uint8_t>(error::dict_unknown_appl_ver_id), 27);
    EXPECT_EQ(static_cast<std::uint8_t>(error::dict_no_dictionary_for_application_version), 28);
    // Existing slots preserved verbatim ([const §X.4]).
    EXPECT_EQ(static_cast<std::uint8_t>(error::out_of_memory), 1);
    EXPECT_EQ(static_cast<std::uint8_t>(error::dict_xml_oom), 22);
}

}  // namespace
