// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/parser_iter_test.cpp — T013 (US1).
// Parser<Iter>::parse_iter streaming, dict-free field_iterator, zero-alloc
// end-to-end (no memory_resource on the Iter path), static constexpr
// Length+Data pair table so a Data field carrying embedded SOH is delimited
// by its preceding Length field. Authored red; GREEN against T024's surface.

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include <fixpp/wire/parser.hpp>

#include "support/frame_view_factory.hpp"
#include "support/mock_dict_table.hpp"

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::Parser;

std::vector<std::byte> make_frame(std::string_view body_after_bodylen) {
    std::string head = "8=FIX.4.4\x01";
    std::string body{body_after_bodylen};
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = head + nine + body;
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    std::array<char, 8> chk{};
    std::snprintf(chk.data(), chk.size(), "10=%03u\x01", sum % 256U);
    std::string full = pre + chk.data();
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// The Length+Data table is a compile-time constant — assert it dict-free.
static_assert(fixpp::wire::detail::data_tag_for_length(95) == 96);
static_assert(fixpp::wire::detail::data_tag_for_length(90) == 91);
static_assert(fixpp::wire::detail::data_tag_for_length(212) == 213);
static_assert(fixpp::wire::detail::data_tag_for_length(7) == 0);

// parse_iter takes NO memory_resource — the streaming path is zero-alloc by
// construction (compile-time contract, FR-003).
static_assert(
    std::is_invocable_v<decltype(&Parser<access_mode::Iter>::parse_iter),
                        Parser<access_mode::Iter>&,
                        fixpp::wire::frame_view const&>);

TEST(WireParserIter, StreamingDictFreeInDocumentOrder) {
    auto buf = make_frame("35=0\x01" "34=7\x01" "112=TESTREQ\x01" "58=hi\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    Parser<access_mode::Iter> parser{fixpp::dict::table_view{}};
    auto mv = parser.parse_iter(*fv);
    ASSERT_TRUE(mv.has_value());
    EXPECT_EQ(mv->msg_type(), "0");

    // The Iter view streams the whole frame (envelope 8/9/10 included); the
    // body tags must appear as an in-order subsequence (document order).
    std::vector<std::uint16_t> tags;
    for (auto it = mv->begin(); !(it == mv->end()); ++it) {
        tags.push_back((*it).tag);
    }
    std::vector<std::uint16_t> const want{35U, 34U, 112U, 58U};
    std::size_t w = 0;
    for (auto t : tags) {
        if (w < want.size() && t == want[w]) {
            ++w;
        }
    }
    EXPECT_EQ(w, want.size())
        << "body tags must stream in document order within the frame";
}

TEST(WireParserIter, LengthDataFieldCarriesEmbeddedSOH) {
    // 95=SecureDataLen, 96=SecureData. The 96 value "ab<SOH>cd" is 5 bytes;
    // the iterator must read it by the preceding 95 length, NOT stop at the
    // embedded SOH. Dict-free: resolved via the static Length+Data table.
    auto buf = make_frame("35=D\x01" "34=1\x01" "95=5\x01"
                          "96=ab\x01" "cd\x01" "58=END\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    Parser<access_mode::Iter> parser{fixpp::dict::table_view{}};
    auto mv = parser.parse_iter(*fv);
    ASSERT_TRUE(mv.has_value());

    bool saw_96 = false;
    bool saw_58 = false;
    for (auto it = mv->begin(); !(it == mv->end()); ++it) {
        if ((*it).tag == 96) {
            saw_96 = true;
            std::string_view v{
                reinterpret_cast<char const*>((*it).value.data()),
                (*it).value.size()};
            EXPECT_EQ(v.size(), 5U);
            EXPECT_EQ(v, std::string_view("ab\x01" "cd", 5));
        }
        if ((*it).tag == 58) {
            saw_58 = true;
        }
    }
    EXPECT_TRUE(saw_96);
    EXPECT_TRUE(saw_58) << "iteration must resume past the Length+Data field";
}

}  // namespace
