// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/unknown_fields_test.cpp — T017 (US1, seam #9).
// unknown_fields_view yields ONLY dictionary-MISSING tags (known-but-invalid
// tags raise wire_unexpected_tag via validator rule 5, US4 — not here). No
// vector materialization: the iterator walks a borrowed span of (tag,value)
// pairs the parser recorded in document order, so a round-trip preserves the
// original on-wire byte order. Authored red; GREEN against T024/T026.
//
// gate-b/r1: the dict is now captured by Parser and threaded into
// MessageView; unknown_fields() performs the real classification against
// the seam-#1 mock table_view (unknown = not in dict for this msg_type).

#include <cstddef>
#include <cstring>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

// seam #1 — mock_dict_table.hpp BEFORE parser.hpp
#include "support/mock_dict_table.hpp"
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/unknown_fields.hpp>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::Parser;
using fixpp::wire::unknown_fields_view;

std::vector<std::byte> make_raw_frame(std::string const& body) {
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string full = "8=FIX.4.4\x01" + nine + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

TEST(WireUnknownFields, DocumentOrderRoundTripNoMaterialization) {
    // Three "unknown" fields recorded in document order. The view borrows the
    // kv span (no owning vector) — iteration yields them in wire order and
    // each value aliases the originating buffer byte-for-byte.
    std::string raw = "5000=alpha\x01" "5001=beta\x01" "5000=gamma\x01";
    std::vector<std::byte> buf(raw.size());
    std::memcpy(buf.data(), raw.data(), raw.size());

    // Value slices, in document order (tag, ptr-into-buf, len).
    std::vector<unknown_fields_view::kv> items{
        {5000, buf.data() + 5, 5},    // "alpha"
        {5001, buf.data() + 16, 4},   // "beta"
        {5000, buf.data() + 26, 5},   // "gamma" (repeat tag, kept in order)
    };
    unknown_fields_view uf{
        std::span<unknown_fields_view::kv const>{items.data(), items.size()},
        {}};

    std::vector<std::uint16_t> tags;
    std::vector<std::string> vals;
    for (auto it = uf.begin(); !(it == uf.end()); ++it) {
        tags.push_back((*it).tag);
        vals.emplace_back(reinterpret_cast<char const*>((*it).data),
                          (*it).len);
    }
    ASSERT_EQ(tags.size(), 3U);
    EXPECT_EQ(tags[0], 5000U);
    EXPECT_EQ(tags[1], 5001U);
    EXPECT_EQ(tags[2], 5000U);
    EXPECT_EQ(vals[0], "alpha");
    EXPECT_EQ(vals[1], "beta");
    EXPECT_EQ(vals[2], "gamma");
    EXPECT_FALSE(uf.empty());
}

// gate-b/r1: classify_unknown(dict) performs the real dict-aware split.
// Tag 5000 is not registered for "D", so it must appear as unknown; known
// fields (35, 34) and framing tags (8, 9, 10) must NOT appear.
TEST(WireUnknownFields, DictBoundUnknownSplit) {
    // Build a dict that knows 35 (MsgType) and 34 (MsgSeqNum) for "D".
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)   // MsgType
        .add_valid("D", 34);  // MsgSeqNum

    auto buf = make_raw_frame("35=D\x01" "34=1\x01" "5000=x\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{fixpp::dict::table_view{}};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    // classify_unknown(dict) walks the offset table and classifies against dict.
    auto uf = mv->classify_unknown(dict);
    // tag 5000 is not in the dict for "D" → must appear as unknown.
    EXPECT_FALSE(uf.empty()) << "tag 5000 must be classified as unknown";
    std::vector<std::uint16_t> unknown_tags;
    for (auto it = uf.begin(); !(it == uf.end()); ++it) {
        unknown_tags.push_back((*it).tag);
    }
    EXPECT_EQ(unknown_tags.size(), 1U);
    EXPECT_EQ(unknown_tags[0], 5000U)
        << "only tag 5000 should be unknown; framing tags 8/9/10 and "
           "known dict tags 35/34 must not appear";
}

// With an empty dict (no known tags for this msg_type), all non-framing
// tags are classified as unknown.
TEST(WireUnknownFields, EmptyDictAllTagsUnknown) {
    auto buf = make_raw_frame("35=D\x01" "34=1\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    fixpp::dict::table_view empty_dict;
    Parser<access_mode::Index> parser{fixpp::dict::table_view{}};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    // With an empty dict, all non-framing tags are "unknown".
    auto uf = mv->classify_unknown(empty_dict);
    // 35 (MsgType) and 34 (MsgSeqNum) are not in the empty dict → unknown.
    // Framing tags 8, 9, 10 must NOT appear.
    bool has_35 = false, has_34 = false, has_framing = false;
    for (auto it = uf.begin(); !(it == uf.end()); ++it) {
        auto tag = (*it).tag;
        if (tag == 35) { has_35 = true; }
        if (tag == 34) { has_34 = true; }
        if (tag == 8 || tag == 9 || tag == 10) { has_framing = true; }
    }
    EXPECT_TRUE(has_35) << "tag 35 must appear as unknown with empty dict";
    EXPECT_TRUE(has_34) << "tag 34 must appear as unknown with empty dict";
    EXPECT_FALSE(has_framing) << "framing tags 8/9/10 must never appear as unknown";
}

}  // namespace
