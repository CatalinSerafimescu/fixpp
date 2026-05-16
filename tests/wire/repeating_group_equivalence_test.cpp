// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/repeating_group_equivalence_test.cpp — T016 (US1, seam #8).
// group_view<GroupT>::iter() and operator[] MUST enumerate identical
// instances in identical document order, including a nested group. iter()
// skips any sub-index build and walks the same instance slices operator[]
// addresses — the two are required to agree. Authored red; GREEN against
// T024/T025.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/frame_view_factory.hpp"
#include "support/mock_dict_table.hpp"

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::Parser;

std::vector<std::byte> make_raw_frame(std::string const& body) {
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string full = "8=FIX.4.4\x01" + nine + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// A 2c-shaped group type: constructible from a sub-frame byte span; the wire
// layer never decodes its fields, it only slices.
struct TestLeg {
    explicit TestLeg(std::span<const std::byte> s) noexcept : data{s.data()}, len{s.size()} {}
    std::byte const* data;
    std::size_t len;
    [[nodiscard]] std::string_view sv() const noexcept {
        return {reinterpret_cast<char const*>(data), len};
    }
};

TEST(WireRepeatingGroupEquivalence, IterAndIndexAgreeIncludingNested) {
    // 555=NoLegs (group), each leg: 600=LegSymbol delimiter. The second leg
    // carries a nested 604=NoLegSecurityAltID group — both enumeration paths
    // must still produce identical leg slices in document order.
    auto buf = make_raw_frame(
        "35=AB\x01"
        "34=1\x01"
        "555=2\x01"
        "600=SYM1\x01"
        "608=A\x01"
        "600=SYM2\x01"
        "604=2\x01"
        "605=ALT1\x01"
        "605=ALT2\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{fixpp::dict::table_view{}};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    auto gv = mv->template group<555, TestLeg>();
    ASSERT_EQ(gv.size(), 2U);

    // Path A: operator[] random access.
    std::vector<std::string> via_index;
    via_index.reserve(gv.size());
    for (std::size_t i = 0; i < gv.size(); ++i) {
        via_index.emplace_back(gv[i].sv());
    }

    // Path B: iter()/begin()..end() streaming (no sub-index build).
    std::vector<std::string> via_iter;
    for (auto it = gv.iter(); !(it == gv.end()); ++it) {
        via_iter.emplace_back((*it).sv());
    }

    ASSERT_EQ(via_index.size(), via_iter.size());
    EXPECT_EQ(via_index, via_iter) << "seam #8: iter() and operator[] must enumerate identical "
                                      "instances in identical order";

    // The first leg slice carries its delimiter value (SYM1); the second
    // leg slice carries the nested 604/605 group bytes — both enumeration
    // paths see identical content (asserted via via_index == via_iter above).
    EXPECT_NE(via_index[0].find("SYM1"), std::string::npos);
    EXPECT_NE(via_index[1].find("605=ALT2"), std::string::npos);
}

// #8 regression: group slices live in the OffsetTable's per-message arena,
// not a shared static thread_local. Two group_views from the SAME message
// for DIFFERENT groups must both stay valid simultaneously (the old
// thread_local clear()'d+rebuilt the shared buffer, dangling the first).
TEST(WireRepeatingGroupEquivalence, GroupViewsDoNotAliasAcrossCalls) {
    auto buf = make_raw_frame(
        "35=AB\x01"
        "34=1\x01"
        "555=2\x01"
        "600=SYM1\x01"
        "608=A\x01"
        "600=SYM2\x01"
        "604=2\x01"
        "605=ALT1\x01"
        "605=ALT2\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{fixpp::dict::table_view{}};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    auto legs = mv->template group<555, TestLeg>();
    ASSERT_EQ(legs.size(), 2U);
    std::string const leg0_before{legs[0].sv()};
    std::string const leg1_before{legs[1].sv()};

    // Build a second, unrelated group view — under the old static
    // thread_local this cleared+rebuilt the shared buffer, dangling `legs`.
    auto alt = mv->template group<604, TestLeg>();
    ASSERT_GE(alt.size(), 1U);

    // `legs` must be UNCHANGED and still valid alongside `alt`.
    ASSERT_EQ(legs.size(), 2U);
    EXPECT_EQ(std::string(legs[0].sv()), leg0_before);
    EXPECT_EQ(std::string(legs[1].sv()), leg1_before);
    EXPECT_NE(leg0_before.find("SYM1"), std::string::npos);

    // Re-requesting the same group returns the stable cached span.
    auto legs_again = mv->template group<555, TestLeg>();
    ASSERT_EQ(legs_again.size(), 2U);
    EXPECT_EQ(std::string(legs_again[0].sv()), leg0_before);
}

}  // namespace
