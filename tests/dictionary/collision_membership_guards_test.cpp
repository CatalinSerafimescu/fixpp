// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/collision_membership_guards_test.cpp — 063 T017 [US1]
//
// Per-collision discriminating regression guards (SC-002), driven by the
// T016 census (reused_tag_census_test.cpp). ONE parameterized test sweeps
// every dict-family that the census found to carry a REAL reused-NumInGroup-
// tag collision (a `no_tag` with ≥2 distinct member sets): FIX43, FIX44,
// FIX50, FIX50SP1, FIX50SP2. Each case asserts the DIFFERENCE between two
// real variants — a tag present in one context's member set and absent
// from the other's — via `Dictionary::as_table_view()`'s context-scoped
// `group_member_tags(msg_type, parent_path, no_tag)` (real Dictionary, real
// XML; no codegen dependency, matching tests/dictionary/'s existing
// no-codegen convention — see defect_a_group_context_test.cpp).
//
// FIX40.xml / FIX41.xml / FIX42.xml are DELIBERATELY NOT represented here:
// the T016 census found ZERO registered group contexts for all three (their
// group count fields are declared type INT, not NUMINGROUP, in the vendored
// XML — see reused_tag_census_test.cpp's "REGISTRATION GAP" escalation
// note). There is no collision to guard for these three dicts because
// nothing is registered at all; writing a passing "membership" test against
// that state would ENSHRINE the gap as expected behaviour rather than
// document it as a known, escalated, pre-existing limitation. FIXT.1.1 is
// represented by a separate benign-same-membership-reuse regression
// (NoHops(627) appears twice in the XML with IDENTICAL membership — not a
// collision — confirming the census's "benign reuse ≠ collision" rule holds
// for the session-layer dict too).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fixpp::dict::Dictionary;

bool contains(std::span<std::uint16_t const> members, std::uint16_t tag) {
    return std::find(members.begin(), members.end(), tag) != members.end();
}

Dictionary load(std::filesystem::path const& path, std::pmr::memory_resource* mr) {
    return fixpp::dict::XmlLoader{}.load(path, mr);
}

struct CollisionCase {
    std::string_view dict_file;
    std::uint16_t no_tag;

    std::string_view msg_type_present;         // context whose variant HAS discriminator_tag
    std::vector<std::uint16_t> path_present;
    std::string_view msg_type_absent;          // context whose variant LACKS discriminator_tag
    std::vector<std::uint16_t> path_absent;
    std::uint16_t discriminator_tag;

    // A tag present in BOTH variants — sanity check that both contexts
    // resolve to *something* real (not two empty misses masquerading as a
    // "discriminating" pass).
    std::uint16_t common_tag;
};

// Directly sourced from the T016 census (reused_tag_census_test.cpp)
// output — every row below is a REAL collision this census found, not a
// hypothetical.
std::vector<CollisionCase> const& collision_cases() {
    static std::vector<CollisionCase> const cases{
        // FIX43: no_tag=78 (NoAllocs). msg_type=D's variant carries 539
        // (SettlInstMode); msg_type=AB's variant does not.
        {"FIX43.xml", 78, "D", {}, "AB", {}, 539, 79},
        // FIX44: no_tag=936 (NoTrdRegPublicationReasons... — the
        // TradeCaptureReportRequestAck (BD) family). msg_type=BD's variant
        // carries 928/929; msg_type=BC's variant does not.
        {"FIX44.xml", 936, "BD", {}, "BC", {}, 928, 283},
        // FIX50 / FIX50SP1 / FIX50SP2: no_tag=386 (NoTradingSessions).
        // msg_type=BJ's variant carries 338 (TradSesMethod); msg_type=AB's
        // variant (2-member {336,625}) does not.
        {"FIX50.xml", 386, "BJ", {}, "AB", {}, 338, 336},
        {"FIX50SP1.xml", 386, "BJ", {}, "AB", {}, 338, 336},
        {"FIX50SP2.xml", 386, "BJ", {}, "AB", {}, 338, 336},
    };
    return cases;
}

class CollisionMembershipGuards : public ::testing::TestWithParam<CollisionCase> {};

}  // namespace

TEST_P(CollisionMembershipGuards, ContextResolvesToTheCorrectVariant) {
    auto const& c = GetParam();

    // Heap-backed arena — FIX50SP2.xml's expanded metadata is large (see
    // the T016 census's table-size report); this test is not alloc-gated.
    constexpr std::size_t kArenaBytes = 32UZ * 1024UZ * 1024UZ;
    auto storage = std::make_unique<std::byte[]>(kArenaBytes);
    std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / std::string{c.dict_file};
    auto dict = load(path, &mr);
    auto tv = dict.as_table_view();

    auto const present_members = tv.group_member_tags(c.msg_type_present, c.path_present, c.no_tag);
    auto const absent_members = tv.group_member_tags(c.msg_type_absent, c.path_absent, c.no_tag);

    // Sanity: both contexts must resolve to a REAL, non-empty member set —
    // otherwise "absent" would trivially hold for a miss, not a genuine
    // context-scoped resolution.
    ASSERT_TRUE(contains(present_members, c.common_tag))
        << c.dict_file << " no_tag=" << c.no_tag << " msg_type=" << c.msg_type_present
        << ": expected common_tag=" << c.common_tag << " in its member set (size "
        << present_members.size() << ")";
    ASSERT_TRUE(contains(absent_members, c.common_tag))
        << c.dict_file << " no_tag=" << c.no_tag << " msg_type=" << c.msg_type_absent
        << ": expected common_tag=" << c.common_tag << " in its member set (size "
        << absent_members.size() << ")";

    // The discriminating assertion: the SAME no_tag resolves to DIFFERENT
    // member sets depending on context — the actual difference, not merely
    // non-empty (per feedback_witness_asserts_named_postcondition_not_proxy
    // shape (b)).
    EXPECT_TRUE(contains(present_members, c.discriminator_tag))
        << c.dict_file << " no_tag=" << c.no_tag << " in msg_type=" << c.msg_type_present
        << " context must include discriminator_tag=" << c.discriminator_tag;
    EXPECT_FALSE(contains(absent_members, c.discriminator_tag))
        << c.dict_file << " no_tag=" << c.no_tag << " in msg_type=" << c.msg_type_absent
        << " context must NOT include discriminator_tag=" << c.discriminator_tag
        << " (that tag belongs to the OTHER declared variant of this reused NumInGroup tag — "
           "resolving it here would reproduce Defect A)";
}

INSTANTIATE_TEST_SUITE_P(PerDictFamily, CollisionMembershipGuards,
                        ::testing::ValuesIn(collision_cases()),
                        [](::testing::TestParamInfo<CollisionCase> const& info) {
                            std::string name{info.param.dict_file};
                            name.erase(std::remove(name.begin(), name.end(), '.'), name.end());
                            return name;
                        });

// FIXT.1.1: NoHops(627) is declared twice in FIXT11.xml (session-layer
// header, appears in more than one message context) but with IDENTICAL
// membership both times — benign reuse, NOT a collision (T016 census: 0
// colliding no_tags for FIXT11.xml). This regression pins that the census's
// "benign reuse ≠ collision" classification holds for the session-layer
// dict too, and that context-scoped resolution still works correctly when
// there is nothing to discriminate.
TEST(CollisionMembershipGuards, FixtNoHopsBenignReuseNotACollision) {
    constexpr std::size_t kArenaBytes = 4UZ * 1024UZ * 1024UZ;
    auto storage = std::make_unique<std::byte[]>(kArenaBytes);
    std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIXT11.xml";
    auto dict = load(path, &mr);
    auto tv = dict.as_table_view();

    // Logon ("A") and Logout ("5") both carry NoHops(627) in FIXT.1.1's
    // header (see dictionaries/FIXT11.xml's two `<group name='NoHops'>`
    // sites); both resolve to the SAME member set (HopCompID/HopSendingTime
    // /HopRefID), confirming this reuse is benign.
    auto const logon_members = tv.group_member_tags("A", {}, 627);
    auto const logout_members = tv.group_member_tags("5", {}, 627);

    ASSERT_FALSE(logon_members.empty()) << "NoHops(627) must resolve for Logon (\"A\")";
    ASSERT_FALSE(logout_members.empty()) << "NoHops(627) must resolve for Logout (\"5\")";

    std::vector<std::uint16_t> a{logon_members.begin(), logon_members.end()};
    std::vector<std::uint16_t> b{logout_members.begin(), logout_members.end()};
    std::ranges::sort(a);
    std::ranges::sort(b);
    EXPECT_EQ(a, b) << "NoHops(627) is benign same-membership reuse across FIXT.1.1 messages — "
                       "must NOT differ (that would make it a real collision, contradicting the "
                       "T016 census's 0-collision finding for FIXT11.xml)";
}
