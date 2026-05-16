// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/offset_table_test.cpp — T014 (US1).
// OffsetTable: entry sizeof==12/alignof==4 invariant, document-order entries,
// O(1) first-occurrence find, and the bounded DoS caps
// (wire_offset_table_full at >4096 occ, wire_tag_out_of_range,
// wire_invalid_field_format; wire_group_too_large is defense-in-depth —
// unreachable through the 4096-capped build, asserted as such). Authored
// red; GREEN against T022/T023.

#include <cstddef>
#include <cstring>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <fixpp/core/error.hpp>
#include <fixpp/wire/offset_table.hpp>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::core::error;
using fixpp::wire::OffsetTable;

// Co-located shape invariant ([2b §4.4]) — the cutover-load-bearing layout.
static_assert(sizeof(OffsetTable::entry) == 12);
static_assert(alignof(OffsetTable::entry) == 4);

// A structurally-framed buffer whose body is exactly `body` (no checksum
// correctness needed — OffsetTable scans bytes, it does not verify the
// trailer; the factory only needs the 9= and <SOH>10= structural markers).
std::vector<std::byte> make_raw_frame(std::string const& body) {
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string full = "8=FIX.4.4\x01" + nine + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

TEST(WireOffsetTable, DocumentOrderAndFirstOccurrence) {
    auto buf = make_raw_frame("35=D\x01" "34=1\x01" "448=A\x01"
                              "448=B\x01" "448=C\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};
    ASSERT_TRUE(t.build_status().has_value());

    // entries() preserves on-wire document order, repeats included (the
    // table spans the whole frame, envelope 8/9/10 included). Assert the
    // three 448 occurrences are contiguous and in document order, and that
    // the values land in order.
    auto ents = t.entries();
    std::vector<std::size_t> idx448;
    for (std::size_t i = 0; i < ents.size(); ++i) {
        if (ents[i].tag == 448U) {
            idx448.push_back(i);
        }
    }
    ASSERT_EQ(idx448.size(), 3U);
    EXPECT_EQ(idx448[1], idx448[0] + 1);
    EXPECT_EQ(idx448[2], idx448[1] + 1);
    auto val_at = [&](std::size_t i) {
        return std::string_view{
            reinterpret_cast<char const*>(buf.data()) + ents[i].offset,
            ents[i].length};
    };
    EXPECT_EQ(val_at(idx448[0]), "A");
    EXPECT_EQ(val_at(idx448[1]), "B");
    EXPECT_EQ(val_at(idx448[2]), "C");

    // find() returns the FIRST occurrence (O(1) overlay probe).
    auto first = t.find(448);
    ASSERT_TRUE(first.has_value());
    std::string_view v{
        reinterpret_cast<char const*>(buf.data()) + first->offset,
        first->length};
    EXPECT_EQ(v, "A");

    auto missing = t.find(9999);
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error(), error::wire_required_field_missing);
}

TEST(WireOffsetTable, DoSCapOffsetTableFull) {
    std::string body;
    for (int i = 0; i < 4097; ++i) {
        body += "1=x\x01";
    }
    auto buf = make_raw_frame(body);
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};
    auto s = t.build_status();
    ASSERT_FALSE(s.has_value());
    EXPECT_EQ(s.error(), error::wire_offset_table_full);
    EXPECT_EQ(t.size(), 0U) << "the table is left empty on a cap breach";
}

TEST(WireOffsetTable, DoSCapTagOutOfRange) {
    auto buf = make_raw_frame("70000=x\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};
    auto s = t.build_status();
    ASSERT_FALSE(s.has_value());
    EXPECT_EQ(s.error(), error::wire_tag_out_of_range);
}

TEST(WireOffsetTable, InvalidFieldFormatRejected) {
    auto buf = make_raw_frame("nofieldsep\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};
    auto s = t.build_status();
    ASSERT_FALSE(s.has_value());
    EXPECT_EQ(s.error(), error::wire_invalid_field_format);
}

TEST(WireOffsetTable, GroupBoundedAndUnreachableTooLargeUnderCap) {
    // A well-formed group: 453=count, then 448/447 instances. With <=4096
    // entries the build never trips the offset cap, so group() returns a
    // bounded range and NEVER wire_group_too_large (that cap is reachable
    // only above the offset cap, which fails the build first — asserted as a
    // defense-in-depth invariant).
    auto buf = make_raw_frame("35=D\x01" "34=1\x01" "453=2\x01"
                              "448=PA\x01" "447=D\x01"
                              "448=PB\x01" "447=D\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};
    ASSERT_TRUE(t.build_status().has_value());

    auto g = t.group(453);
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(g->no_tag(), 453U);
    EXPECT_GT(g->entry_count(), 0U);
    EXPECT_LE(g->entry_count(),
              fixpp::wire::default_max_group_entries_per_instance);

    auto none = t.group(9999);
    ASSERT_FALSE(none.has_value());
    EXPECT_EQ(none.error(), error::wire_required_field_missing);
}

}  // namespace
