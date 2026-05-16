// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/unknown_fields_test.cpp — T017 (US1, seam #9).
// unknown_fields_view yields ONLY dictionary-MISSING tags (known-but-invalid
// tags raise wire_unexpected_tag via validator rule 5, US4 — not here). No
// vector materialization: the iterator walks a borrowed span of (tag,value)
// pairs the parser recorded in document order, so a round-trip preserves the
// original on-wire byte order. Authored red; GREEN against T024/T026.
//
// The dictionary-aware missing-vs-known-invalid SPLIT is exercised through
// the validator seam (US4) which holds table_view by value; without a
// dictionary bound here MessageView::unknown_fields() is empty by
// construction (asserted, the honest seam boundary).

#include <cstddef>
#include <cstring>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/unknown_fields.hpp>

#include "support/frame_view_factory.hpp"
#include "support/mock_dict_table.hpp"

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

TEST(WireUnknownFields, EmptyByConstructionWithoutDictionary) {
    // Seam boundary: the parser does not classify tags without a dictionary,
    // so MessageView::unknown_fields() is empty by construction. The
    // missing-vs-known-invalid split lands at the validator (US4).
    auto buf = make_raw_frame("35=D\x01" "34=1\x01" "5000=x\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{fixpp::dict::table_view{}};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    auto uf = mv->unknown_fields();
    EXPECT_TRUE(uf.empty());
    EXPECT_TRUE(uf.begin() == uf.end());
}

}  // namespace
