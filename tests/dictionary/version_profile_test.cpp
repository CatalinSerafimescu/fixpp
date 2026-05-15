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

#include <cstdint>
#include <string_view>
#include <type_traits>

#include <fixpp/core/error.hpp>
#include <fixpp/dict/version_profile.hpp>

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
static_assert(std::is_same_v<
    decltype(resolve_application_version(std::declval<version_profile>(),
                                         std::declval<std::string_view>())),
    fixpp::core::expected_t<application_version>>);

constexpr version_profile kFixt{session_version::vt11, application_version::v50sp2,
                                true, 0};
constexpr version_profile kUnknownDefault{session_version::vt11,
                                           application_version::Unknown, true, 0};

TEST(VersionProfileAcVp1, ReservedZeroOnEmit) {
    // AC-VP5 — _reserved zero on emit, ignored on read in v1.0.
    version_profile p{session_version::v44, application_version::v44, false, 0};
    EXPECT_EQ(p._reserved, 0);
    resolved_message_version r{resolved_message_version::kind::application,
                               session_version::v44, application_version::v44, 0};
    EXPECT_EQ(r._reserved, 0);
}

TEST(VersionProfileAcVp3, FullWireToCppMap) {
    // AC-VP3 — every wire ApplVerID value "2".."9".
    struct Case { std::string_view wire; application_version expect; };
    constexpr Case cases[] = {
        {"2", application_version::v40},  {"3", application_version::v41},
        {"4", application_version::v42},  {"5", application_version::v43},
        {"6", application_version::v44},  {"7", application_version::v50},
        {"8", application_version::v50sp1}, {"9", application_version::v50sp2},
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
    for (std::string_view bad : {"0", "1", "10", "A", "x", "99"}) {
        auto got = resolve_application_version(kFixt, bad);
        ASSERT_FALSE(got.has_value()) << "bad=" << bad;
        EXPECT_EQ(got.error(), error::dict_unknown_appl_ver_id) << "bad=" << bad;
    }
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
    EXPECT_EQ(static_cast<std::uint8_t>(error::dict_no_dictionary_for_application_version),
              28);
    // Existing slots preserved verbatim ([const §X.4]).
    EXPECT_EQ(static_cast<std::uint8_t>(error::out_of_memory), 1);
    EXPECT_EQ(static_cast<std::uint8_t>(error::dict_xml_oom), 22);
}

}  // namespace
