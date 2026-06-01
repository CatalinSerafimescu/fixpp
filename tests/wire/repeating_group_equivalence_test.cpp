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
    // Parse the first field from the sub-frame (tag=value<SOH>).
    // Returns {tag, value_sv} of the first field in the slice.
    struct first_field_result {
        std::uint16_t tag;
        std::string_view value;
    };
    [[nodiscard]] first_field_result parse_first_field() const noexcept {
        std::string_view raw{reinterpret_cast<char const*>(data), len};
        auto eq = raw.find('=');
        if (eq == std::string_view::npos) {
            return {0, {}};
        }
        std::uint32_t tag_val = 0;
        for (std::size_t i = 0; i < eq; ++i) {
            char c = raw[i];
            if (c < '0' || c > '9') {
                return {0, {}};
            }
            tag_val = (tag_val * 10U) + static_cast<std::uint32_t>(c - '0');
        }
        auto val_start = eq + 1;
        auto soh = raw.find('\x01', val_start);
        auto val_end = (soh == std::string_view::npos) ? raw.size() : soh;
        return {static_cast<std::uint16_t>(tag_val), raw.substr(val_start, val_end - val_start)};
    }
};

TEST(WireRepeatingGroupEquivalence, IterAndIndexAgreeIncludingNested) {
    // 555=NoLegs (group), each leg: 600=LegSymbol delimiter with 608=A field.
    // Both instances have the same member structure (both have 608), so the
    // first-instance member set {600, 608} correctly covers both instances.
    // Both enumeration paths must produce identical leg slices in document order.
    // ([PR68-09] boundary fix: the group extent is bounded by member-set, so
    // trailing top-level fields after the group would be excluded. The frame
    // below ends with the group, so no trailing fields exist.)
    auto buf = make_raw_frame(
        "35=AB\x01"
        "34=1\x01"
        "555=2\x01"
        "600=SYM1\x01"
        "608=A\x01"
        "600=SYM2\x01"
        "608=B\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{};
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

    // RC#2 real-parse assertion: each slice starts with the delimiter field
    // (tag 600 = "600=") so a real GroupT can parse it as "tag=value<SOH>...".
    auto ff0 = gv[0].parse_first_field();
    EXPECT_EQ(ff0.tag, 600U) << "first parsed field of leg[0] must be the delimiter tag 600, got "
                             << ff0.tag;
    EXPECT_EQ(ff0.value, "SYM1");

    auto ff1 = gv[1].parse_first_field();
    EXPECT_EQ(ff1.tag, 600U) << "first parsed field of leg[1] must be the delimiter tag 600, got "
                             << ff1.tag;
    EXPECT_EQ(ff1.value, "SYM2");

    // Each leg should contain its 608 field byte for byte.
    EXPECT_NE(via_index[0].find("608=A"), std::string::npos) << "leg[0] must contain 608=A";
    EXPECT_NE(via_index[1].find("608=B"), std::string::npos) << "leg[1] must contain 608=B";
}

// #8 regression: group slices live in the OffsetTable's per-message arena,
// not a shared static thread_local. Two group_views from the SAME message
// for DIFFERENT groups must both stay valid simultaneously (the old
// thread_local clear()'d+rebuilt the shared buffer, dangling the first).
// The frame includes two groups: 555=2 (legs, delimiter 600, member 608)
// and a separate top-level 604=2 group (delimiter 605, member none beyond
// 605 itself). Both groups end before the trailing top-level field 55=TOP.
// ([PR68-09] boundary fix: group_slices() now uses the member-set extent.)
TEST(WireRepeatingGroupEquivalence, GroupViewsDoNotAliasAcrossCalls) {
    auto buf = make_raw_frame(
        "35=AB\x01"
        "34=1\x01"
        "555=2\x01"
        "600=SYM1\x01"
        "608=A\x01"
        "600=SYM2\x01"
        "608=B\x01"
        "604=2\x01"
        "605=ALT1\x01"
        "605=ALT2\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{};
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
