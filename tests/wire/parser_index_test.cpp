// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/parser_index_test.cpp — T012 (US1, seam-bound).
// Parser<Index>::parse random access: get<Tag>/get(tag), offsets(),
// repeated-tag occurrence addressing, all via the Foundational
// frame_view_factory (T008) — NO dependency on the US3 Framer algorithm
// (resolves analyze I1). Authored red; GREEN once T021–T024 land.

#include <array>
#include <cstddef>
#include <cstring>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <fixpp/wire/parser.hpp>

#include "support/frame_view_factory.hpp"
#include "support/mock_dict_table.hpp"  // concrete dict::table_view (seam #1)

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::MessageView;

// Build a raw FIX 4.4 frame with correct 9=BodyLength and 10=CheckSum so
// the factory's boundary computation succeeds.
std::vector<std::byte> make_frame(std::string_view body_after_bodylen) {
    std::string head = "8=FIX.4.4\x01";
    std::string body{body_after_bodylen};
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = head + nine + body;
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    char chk[8];
    std::snprintf(chk, sizeof(chk), "10=%03u\x01", sum % 256U);
    std::string full = pre + chk;
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

TEST(WireParserIndex, RandomAccessByTag) {
    auto buf = make_frame("35=D\x01" "49=SENDER\x01" "56=TARGET\x01"
                          "34=42\x01" "11=ORD-1\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    fixpp::wire::Parser<access_mode::Index> parser{fixpp::dict::table_view{}};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    EXPECT_EQ(mv->msg_type(), "D");
    EXPECT_EQ(mv->msg_seq_num(), 42U);

    auto sender = mv->template get<49>();
    ASSERT_TRUE(sender.has_value());
    EXPECT_EQ(sender->as_string(), "SENDER");

    auto ord = mv->get(11);
    ASSERT_TRUE(ord.has_value());
    EXPECT_EQ(ord->as_string(), "ORD-1");

    auto missing = mv->get(9999);
    EXPECT_FALSE(missing.has_value());
}

TEST(WireParserIndex, OffsetsAndRepeatedOccurrence) {
    // Two occurrences of tag 448 (document order must be preserved).
    auto buf = make_frame("35=D\x01" "34=1\x01" "448=PARTY-A\x01"
                          "448=PARTY-B\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    fixpp::wire::Parser<access_mode::Index> parser{fixpp::dict::table_view{}};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    // get() returns the FIRST occurrence; both are addressable via entries().
    auto first = mv->get(448);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->as_string(), "PARTY-A");

    int seen = 0;
    std::string second;
    for (auto const& e : mv->offsets().entries()) {
        if (e.tag == 448) {
            ++seen;
            if (seen == 2) {
                second.assign(
                    reinterpret_cast<char const*>(buf.data()) + e.offset,
                    e.length);
            }
        }
    }
    EXPECT_EQ(seen, 2);
    EXPECT_EQ(second, "PARTY-B");
}

TEST(WireParserIter, StreamingDictFree) {
    auto buf = make_frame("35=0\x01" "34=7\x01" "112=TESTREQ\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    fixpp::wire::Parser<access_mode::Iter> parser{fixpp::dict::table_view{}};
    auto mv = parser.parse_iter(*fv);
    ASSERT_TRUE(mv.has_value());
    EXPECT_EQ(mv->msg_type(), "0");

    bool found_112 = false;
    for (auto it = mv->begin(); !(it == mv->end()); ++it) {
        if ((*it).tag == 112) {
            found_112 = true;
            std::string_view v{
                reinterpret_cast<char const*>((*it).value.data()),
                (*it).value.size()};
            EXPECT_EQ(v, "TESTREQ");
        }
    }
    EXPECT_TRUE(found_112);
}

}  // namespace
