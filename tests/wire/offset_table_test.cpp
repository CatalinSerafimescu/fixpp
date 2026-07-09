// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/offset_table_test.cpp — T014 (US1).
// OffsetTable: entry sizeof==12/alignof==4 invariant, document-order entries,
// O(1) first-occurrence find, and the bounded DoS caps
// (wire_offset_table_full at >4096 occ, wire_tag_out_of_range,
// wire_invalid_field_format; wire_group_too_large is defense-in-depth —
// unreachable through the 4096-capped build, asserted as such). Authored
// red; GREEN against T022/T023.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

#include "support/frame_view_factory.hpp"
#include "support/mock_dict_table.hpp"

namespace {

using fixpp::core::error;
using fixpp::wire::access_mode;
using fixpp::wire::OffsetTable;
using fixpp::wire::Parser;

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
    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "448=A\x01"
        "448=B\x01"
        "448=C\x01");
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
        return std::string_view{reinterpret_cast<char const*>(buf.data()) + ents[i].offset,
                                ents[i].length};
    };
    EXPECT_EQ(val_at(idx448[0]), "A");
    EXPECT_EQ(val_at(idx448[1]), "B");
    EXPECT_EQ(val_at(idx448[2]), "C");

    // find() returns the FIRST occurrence (O(1) overlay probe).
    auto first = t.find(448);
    ASSERT_TRUE(first.has_value());
    std::string_view v{reinterpret_cast<char const*>(buf.data()) + first->offset, first->length};
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

TEST(WireOffsetTable, GroupBoundedUnderDefaultCap) {
    // A well-formed group: 453=count, then 448/447 instances. The default
    // 4096-entry cap allows normal groups; group() returns a bounded range.
    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "453=2\x01"
        "448=PA\x01"
        "447=D\x01"
        "448=PB\x01"
        "447=D\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};
    ASSERT_TRUE(t.build_status().has_value());

    auto g = t.group(453);
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(g->no_tag(), 453U);
    EXPECT_GT(g->entry_count(), 0U);
    EXPECT_LE(g->entry_count(), fixpp::wire::default_max_group_entries_per_instance);

    auto none = t.group(9999);
    ASSERT_FALSE(none.has_value());
    EXPECT_EQ(none.error(), error::wire_required_field_missing);
}

TEST(WireOffsetTable, DoSCapPerInstanceAllowsAggregateOverCap) {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 34)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 447)
        .set_group_first(453, 448)
        .add_group_member(453, 447);

    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "453=2\x01"
        "448=PA\x01"
        "447=D\x01"
        "448=PB\x01"
        "447=D\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable::Config tight_cfg{.max_offset_entries = 4096, .max_group_entries_per_instance = 3};
    auto mv = [&]() {
        Parser<access_mode::Index> parser{dict};
        return parser.parse(*fv, &arena, tight_cfg);
    }();
    ASSERT_TRUE(mv.has_value());

    auto const& table = mv->offsets();
    auto g = table.group(453);
    ASSERT_TRUE(g.has_value()) << "each instance is under the cap even though the aggregate is 4";
    EXPECT_EQ(g->entry_count(), 4U);
}

TEST(WireOffsetTable, DoSCapPerInstanceRejectsOversizedSingleInstance) {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 34)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 447)
        .add_valid("D", 452)
        .add_valid("D", 802)
        .set_group_first(453, 448)
        .add_group_member(453, 447)
        .add_group_member(453, 452)
        .add_group_member(453, 802);

    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "453=1\x01"
        "448=PA\x01"
        "447=D\x01"
        "452=1\x01"
        "802=1\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable::Config tight_cfg{.max_offset_entries = 4096, .max_group_entries_per_instance = 3};
    auto mv = [&]() {
        Parser<access_mode::Index> parser{dict};
        return parser.parse(*fv, &arena, tight_cfg);
    }();
    ASSERT_TRUE(mv.has_value());

    auto g = mv->offsets().group(453);
    ASSERT_FALSE(g.has_value());
    EXPECT_EQ(g.error(), error::wire_group_too_large);
}

// RC#2: group slice must begin at the delimiter field's "tag=" prefix, NOT
// at its value.  A real 2c GroupT parses "tag=value<SOH>..." as a sub-frame;
// the leading "NNN=" must be present.
TEST(WireOffsetTable, GroupSliceStartsAtTagEquals) {
    // Group 453, delimiter 448.  The first instance is "448=PA<SOH>447=D<SOH>".
    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "453=2\x01"
        "448=PA\x01"
        "447=D\x01"
        "448=PB\x01"
        "447=D\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};
    ASSERT_TRUE(t.build_status().has_value());

    auto slices = t.group_slices(453);
    ASSERT_EQ(slices.size(), 2U);

    // The first slice must start with "448=" (the delimiter tag=).
    auto const& s0 = slices[0];
    ASSERT_GE(s0.len, 4U) << "slice must be long enough to contain '448='";
    std::string_view sv0{reinterpret_cast<char const*>(s0.data), s0.len};
    EXPECT_EQ(sv0.substr(0, 4), "448=")
        << "group slice must start at the delimiter tag=, not its value; got: " << sv0;

    auto const& s1 = slices[1];
    ASSERT_GE(s1.len, 4U);
    std::string_view sv1{reinterpret_cast<char const*>(s1.data), s1.len};
    EXPECT_EQ(sv1.substr(0, 4), "448=") << "second slice must start at '448='; got: " << sv1;
}

TEST(WireOffsetTable, GroupExtentExcludesTrailingTopLevelFields) {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 34)
        .add_valid("D", 55)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 447)
        .set_group_first(453, 448)
        .add_group_member(453, 447);

    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "453=1\x01"
        "448=PA\x01"
        "447=D\x01"
        "55=AAPL\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    auto mv = [&]() {
        Parser<access_mode::Index> parser{dict};
        return parser.parse(*fv, &arena);
    }();
    ASSERT_TRUE(mv.has_value());

    auto const& t = mv->offsets();
    auto g = t.group(453);
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(g->entry_count(), 2U)
        << "single-instance group extent must exclude the trailing 55=AAPL field";

    // group_slices: the lone instance must not include 55=AAPL.
    auto slices = t.group_slices(453);
    ASSERT_EQ(slices.size(), 1U) << "one group instance expected";
    std::string_view sv0{reinterpret_cast<char const*>(slices[0].data), slices[0].len};
    EXPECT_EQ(sv0.substr(0, 4), "448=") << "slice[0] must start at '448='; got: " << sv0;
    EXPECT_EQ(sv0.find("55="), std::string_view::npos)
        << "slice[0] must NOT include trailing 55=AAPL; content: " << sv0;
}

// Witnesses the group_slices_ reserve invariant directly (PR #181 arena_fit
// follow-up): reading a SECOND top-level group tag must not reallocate the
// shared group_slices_ vector that an earlier tag's span already points into.
// The reserve-once bound (group_slices_reserve_bound()) must cover the SUM of
// both groups' instances. If it under-provisions, the B-read reallocates
// group_slices_ into a fresh buffer — the earlier span `a` then points at the
// stranded old buffer while a re-fetch of the same tag returns the NEW buffer,
// so their .data() pointers diverge. (The parse arena is a
// monotonic_buffer_resource, which never frees, so a stale span stays readable
// — the harm of a reallocation is the STRANDED buffer that exhausts the fixed
// arena, i.e. the exact arena_fit failure. A pointer-stability check, not ASan,
// is therefore the discriminating witness.) Mutation-proven: shrinking the
// reserve to 2 makes the B-read reallocate and this test RED.
TEST(WireOffsetTable, TwoTopLevelGroupsSpanStableAcrossReads) {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 34)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 447)
        .add_valid("D", 555)
        .add_valid("D", 600)
        .add_valid("D", 624)
        .set_group_first(453, 448)
        .add_group_member(453, 447)
        .set_group_first(555, 600)
        .add_group_member(555, 624);

    // Two top-level groups: NoPartyIDs(453)=2 then NoLegs(555)=3.
    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "453=2\x01"
        "448=PA\x01"
        "447=D\x01"
        "448=PB\x01"
        "447=D\x01"
        "555=3\x01"
        "600=L0\x01"
        "624=1\x01"
        "600=L1\x01"
        "624=1\x01"
        "600=L2\x01"
        "624=1\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    auto mv = [&]() {
        Parser<access_mode::Index> parser{dict};
        return parser.parse(*fv, &arena);
    }();
    ASSERT_TRUE(mv.has_value());
    auto const& t = mv->offsets();

    // Read group A and HOLD its span. a.data() is a pointer INTO group_slices_
    // (its backing vector), captured now.
    auto a = t.group_slices(453);
    ASSERT_EQ(a.size(), 2U);
    auto const* const a_backing_before = a.data();

    // Read group B (different tag) — appends 3 instances to group_slices_. With
    // the reserve bound = declared(453)+declared(555)=5, this must NOT reallocate.
    auto b = t.group_slices(555);
    ASSERT_EQ(b.size(), 3U);

    // Re-fetch group A (cached): line 570 recomputes the span from the CURRENT
    // group_slices_.data(). If the B-read reallocated, this points into the new
    // buffer and diverges from the held pointer.
    auto a2 = t.group_slices(453);
    ASSERT_EQ(a2.size(), 2U);
    EXPECT_EQ(a2.data(), a_backing_before)
        << "reading a second top-level group reallocated group_slices_ (reserve bound too "
           "small) — the buffer must be stable so it does not strand memory in the fixed arena";

    // Content sanity: A's slices still read correctly (data pointers are into the
    // stable frame regardless).
    std::string_view const a0{reinterpret_cast<char const*>(a2[0].data), a2[0].len};
    EXPECT_EQ(a0.substr(0, 4), "448=");
}

TEST(WireOffsetTable, GroupExtentSupportsMoreThanThirtyTwoDistinctMembers) {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 34)
        .add_valid("D", 9000)
        .add_valid("D", 9999)
        .set_group_first(9000, 9001);
    for (std::uint16_t tag = 9001; tag <= 9033; ++tag) {
        dict.add_valid("D", tag);
        if (tag != 9001) {
            dict.add_group_member(9000, tag);
        }
    }

    std::string body =
        "35=D\x01"
        "34=1\x01"
        "9000=2\x01";
    for (std::uint16_t tag = 9001; tag <= 9033; ++tag) {
        body += std::to_string(tag) + "=X\x01";
    }
    body +=
        "9001=Y\x01"
        "9999=TAIL\x01";
    auto buf = make_raw_frame(body);
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    auto mv = [&]() {
        Parser<access_mode::Index> parser{dict};
        return parser.parse(*fv, &arena);
    }();
    ASSERT_TRUE(mv.has_value());

    auto g = mv->offsets().group(9000);
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(g->entry_count(), 34U);

    auto slices = mv->offsets().group_slices(9000);
    ASSERT_EQ(slices.size(), 2U);
    std::string_view first_instance{reinterpret_cast<char const*>(slices[0].data), slices[0].len};
    std::string_view second_instance{reinterpret_cast<char const*>(slices[1].data), slices[1].len};
    EXPECT_NE(first_instance.find("9033="), std::string_view::npos);
    EXPECT_EQ(second_instance.find("9999="), std::string_view::npos);
}

}  // namespace
