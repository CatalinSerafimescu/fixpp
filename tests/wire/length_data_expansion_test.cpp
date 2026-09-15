// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/length_data_expansion_test.cpp — fixpp#426
//
// Both dictionary-free scanners (Parser<Iter>'s field_iterator and OffsetTable's
// build) must count a Data value by its Length for every standard pair, not only
// the six the old tables listed.
//
// RED on the unfixed tree: EncodedText 354/355, the non-adjacent 1678/1697 and the
// inverted 2372/2371 were in neither old table. Regression pins, GREEN before and
// after: EncodedIssuer 348/349 and XmlData 212/213 were already counted.
//
// Each Data value carries an embedded `<SOH>58=F`. A scanner that does not know the
// pair splits the value there and reads a forged Text(58).

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// seam #1: complete table_view must precede parser.hpp (single-definition rule).
// clang-format off
#include "support/mock_dict_table.hpp"
// clang-format on
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/parser.hpp>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::OffsetTable;
using fixpp::wire::Parser;

struct pair_case {
    std::uint16_t length_tag;
    std::uint16_t data_tag;
    char const* why;
};

// The Data value: 6 bytes, one of them SOH.
constexpr std::string_view kValue{
    "x\x01"
    "58=F",
    6};

std::vector<std::byte> make_raw_frame(pair_case const& p) {
    std::string body = "35=D\x01";
    body += std::to_string(p.length_tag) + "=" + std::to_string(kValue.size()) + "\x01";
    body += std::to_string(p.data_tag) + "=";
    body += kValue;
    body +=
        "\x01"
        "77=Z\x01";
    std::string const full =
        "8=FIX.4.4\x01"
        "9=" +
        std::to_string(body.size()) + "\x01" + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

std::string_view as_sv(std::span<std::byte const> v) {
    return {reinterpret_cast<char const*>(v.data()), v.size()};
}

class LengthDataExpansion : public ::testing::TestWithParam<pair_case> {};

TEST_P(LengthDataExpansion, IterCountsTheValueAndReadsTheNextField) {
    auto const& p = GetParam();
    auto buf = make_raw_frame(p);
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    Parser<access_mode::Iter> parser{};
    auto mv = parser.parse_iter(*fv);
    ASSERT_TRUE(mv.has_value());

    std::string data_value;
    bool saw_data = false;
    bool saw_forged_58 = false;
    bool saw_77 = false;
    for (auto it = mv->begin(); !(it == mv->end()); ++it) {
        auto const& f = *it;
        if (f.tag == p.data_tag) {
            saw_data = true;
            data_value = std::string{as_sv(f.value)};
        }
        saw_forged_58 = saw_forged_58 || f.tag == 58;
        saw_77 = saw_77 || (f.tag == 77 && as_sv(f.value) == "Z");
    }
    EXPECT_TRUE(saw_data) << p.why;
    EXPECT_EQ(data_value, kValue) << p.why;
    EXPECT_FALSE(saw_forged_58) << p.why;
    EXPECT_TRUE(saw_77) << p.why;
}

TEST_P(LengthDataExpansion, IndexCountsTheValueAndFindsTheNextField) {
    auto const& p = GetParam();
    auto buf = make_raw_frame(p);
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};
    ASSERT_TRUE(t.build_status().has_value()) << p.why;

    auto const d = t.find(p.data_tag);
    ASSERT_TRUE(d.has_value()) << p.why;
    EXPECT_EQ(as_sv(fv->bytes().subspan(d->offset, d->length)), kValue) << p.why;
    EXPECT_FALSE(t.find(58).has_value()) << p.why;
    EXPECT_TRUE(t.find(77).has_value()) << p.why;
}

INSTANTIATE_TEST_SUITE_P(
    ExpandedPairs, LengthDataExpansion,
    ::testing::Values(pair_case{354, 355, "EncodedText: absent from the old 6-pair tables"},
                      pair_case{1678, 1697, "non-adjacent pair: absent from the old tables"},
                      pair_case{2372, 2371,
                                "Length tag above Data tag: absent from the old tables"}));

INSTANTIATE_TEST_SUITE_P(
    RegressionPins, LengthDataExpansion,
    ::testing::Values(pair_case{348, 349, "EncodedIssuer: already counted before fixpp#426"},
                      pair_case{212, 213, "XmlData: already counted before fixpp#426"}));

}  // namespace
