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
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/parser.hpp>
#include <fstream>
#include <iterator>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

#include "support/context_group_delim_fn.hpp"
#include "support/context_group_member_fn.hpp"
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
    //
    // 220: DICT-AWARE construction. This cell used the dict-free ctor until
    // #220 made group() a dictionary-only operation; the assertions below are
    // about the extent walk, which was never the dict-free branch's to answer.
    // The extent is now EXACT (4 = 448/447/448/447) rather than merely
    // "> 0 and <= cap" — the trailing 10= checksum entry is excluded by
    // membership, which is the property #220 turned on for every caller that
    // can be told what a member is.
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
    auto mv = [&]() {
        Parser<access_mode::Index> parser{dict};
        return parser.parse(*fv, &arena);
    }();
    ASSERT_TRUE(mv.has_value());
    auto const& t = mv->offsets();

    auto g = t.group(453);
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(g->no_tag(), 453U);
    EXPECT_EQ(g->entry_count(), 4U)
        << "two instances of 448/447; the trailing 10= checksum is NOT a member";
    EXPECT_LE(g->entry_count(), fixpp::wire::default_max_group_entries_per_instance);

    // no_tag absent from the frame entirely -> count_idx == entries_.size().
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

// 220 — DICT-FREE CONSTRUCTION DECLINES.
//
// These cells replace 085's DictFreeDoSCapPerInstance{RejectsOversizedInstance,
// AllowsWhenCapRaised}, which bracketed a per-instance cap that no longer
// exists on this path: OffsetTable::group() is a dictionary-only operation, so
// a dict-free table reports every group ABSENT instead of deriving an extent
// from the wire.
//
// Why the cap was not repaired in place. The dict-free rule bounded the extent
// at rest-of-message, so the LAST instance absorbed every trailing top-level
// field. That over-extent did not stop at the cap — group_slices_status()
// derives its slice boundary from group()'s extent, so it reached the typed
// group_view<GroupT> too. Capping it more carefully would have left the wrong
// extent standing; and there is no sound wire-only boundary rule to put in its
// place, [2b §4.7] defining the boundary as the dictionary's
// first-field-of-group rule per [FIX50SP2 §3]. Both reference engines decline
// for the same reason (QuickFIX C++ Message::setGroup returns when
// DataDictionary::getGroup fails; QuickFIX/J guards every parseGroup call site
// on a non-null dictionary).
//
// The frame is the one 085's cells used, kept deliberately: 453=1 with a
// four-member instance (448/447/452/802) plus the trailing 10= checksum that
// the old rest-of-message rule miscounted as a fifth member.
std::vector<std::byte> group_with_trailing_field_frame() {
    return make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "453=1\x01"
        "448=PA\x01"
        "447=D\x01"
        "452=1\x01"
        "802=1\x01");
}

// Membership for the frame above: 453 is a real group, delimiter 448.
void fill_group_with_trailing_field_dict(fixpp::dict::table_view& dict) {
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
}

// PATH: opaque_dict_ == nullptr, DEFAULT Config.
TEST(WireOffsetTable, DictFreeGroupDeclinesUnderDefaultConfig) {
    auto buf = group_with_trailing_field_frame();
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};
    ASSERT_TRUE(t.build_status().has_value())
        << "the table itself builds fine — declining is about group(), not the scan";
    // ANTI-VACUITY, and it has to name the tag: group() returns
    // wire_required_field_missing from TWO sites — the dict-free guard, and
    // `count_idx == entries_.size()` when no_tag is simply not in the frame.
    // `t.size() > 0` cannot separate them; `find(453)` can.
    ASSERT_TRUE(t.find(453).has_value())
        << "anti-vacuity: 453 must BE in the table, or the absent result below could come from "
           "the tag-not-found return rather than from the dict-free guard under test";

    auto g = t.group(453);
    ASSERT_FALSE(g.has_value());
    EXPECT_EQ(g.error(), error::wire_required_field_missing)
        << "dict-free: no membership oracle, so nothing can be established as a group";
}

// PATH: opaque_dict_ == nullptr, TIGHTENED Config — the exact configuration
// #220 named as turning the over-count into a false rejection
// (max_group_entries_per_instance < max_offset_entries - 1). It cannot reject
// any more, because nothing is measured: the answer is the same absent result
// as under the default Config, which is what makes the false positive
// unreachable rather than merely less likely.
TEST(WireOffsetTable, DictFreeGroupDeclinesUnderTightenedConfig) {
    auto buf = group_with_trailing_field_frame();
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable::Config tight_cfg{.max_offset_entries = 4096, .max_group_entries_per_instance = 3};
    OffsetTable t{*fv, &arena, tight_cfg};
    ASSERT_TRUE(t.build_status().has_value());
    ASSERT_TRUE(t.find(453).has_value())
        << "anti-vacuity: see DictFreeGroupDeclinesUnderDefaultConfig";

    auto g = t.group(453);
    ASSERT_FALSE(g.has_value());
    EXPECT_EQ(g.error(), error::wire_required_field_missing)
        << "must be the ABSENT result, not wire_group_too_large — a tightened cap can no "
           "longer produce a spurious rejection because no cap is applied on this path";
    EXPECT_NE(g.error(), error::wire_group_too_large);
}

// PATH: opaque_dict_ != nullptr but group_member_fn_ == nullptr — the
// half-threaded construction the `||` in the entry guard also covers. Without
// the predicate there is no oracle, so this is dict-FREE in every sense that
// matters to group().
TEST(WireOffsetTable, DictFreeGroupDeclinesWhenMembershipFnMissing) {
    auto buf = group_with_trailing_field_frame();
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    fixpp::dict::table_view dict;
    fill_group_with_trailing_field_dict(dict);

    std::pmr::monotonic_buffer_resource arena;
    // 384: the delimiter oracle is spelled out too, now that it has no default.
    // It is deliberately null here — the point of this cell is a table with no
    // MEMBERSHIP oracle, which group() declines before any delimiter is
    // resolved, so a threaded delimiter callback would never be called.
    OffsetTable t{*fv, &arena, &dict, /*group_member_fn=*/nullptr,
                  /*group_delim_fn=*/nullptr};
    ASSERT_TRUE(t.build_status().has_value());
    ASSERT_TRUE(t.find(453).has_value())
        << "anti-vacuity: see DictFreeGroupDeclinesUnderDefaultConfig";

    auto g = t.group(453);
    ASSERT_FALSE(g.has_value());
    EXPECT_EQ(g.error(), error::wire_required_field_missing)
        << "a non-null dict with no membership predicate is still no oracle";
}

// The OBSERVABLE a caller actually sees on the dict-free path: group_slices()
// yields an empty span, which src/capi/message_read.cpp maps to
// FIXPP_ERR_TYPE_MISMATCH — the documented E-2 / CA-010-read result, and the
// same thing the dict-aware path produces for a scalar tag.
TEST(WireOffsetTable, DictFreeGroupSlicesAreEmpty) {
    auto buf = group_with_trailing_field_frame();
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};
    ASSERT_TRUE(t.build_status().has_value());

    ASSERT_TRUE(t.find(453).has_value())
        << "anti-vacuity: an empty span is also what an ABSENT tag yields, so 453 must be present "
           "for the assertion below to be about the dict-free decline at all";

    EXPECT_TRUE(t.group_slices(453).empty())
        << "no group => no slices; the C-ABI thunk reports TYPE_MISMATCH from exactly this";
}

// #220 REGRESSION — the defect's own repro, on the path that CAN answer it.
//
// Same frame, and a cap of 4 sitting between the two measures: the old
// rest-of-message rule measured the single instance as 5 (448/447/452/802 plus
// the trailing 10= checksum) and rejected; membership measures the 4 real
// members and accepts. Both arms are asserted, so this cell cannot pass by
// the cap being inert — cap 3 still rejects the same frame through the same
// call, which is what proves the cap is live rather than removed.
TEST(WireOffsetTable, TrailingFieldNotCountedIntoLastInstance) {
    auto buf = group_with_trailing_field_frame();
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    fixpp::dict::table_view dict;
    fill_group_with_trailing_field_dict(dict);

    // ARM 1 — cap 4: the old measure (5) breached, the membership measure (4)
    // does not. This is the false rejection #220 reported.
    {
        std::pmr::monotonic_buffer_resource arena;
        OffsetTable::Config cfg{.max_offset_entries = 4096, .max_group_entries_per_instance = 4};
        auto mv = [&]() {
            Parser<access_mode::Index> parser{dict};
            return parser.parse(*fv, &arena, cfg);
        }();
        ASSERT_TRUE(mv.has_value());

        auto g = mv->offsets().group(453);
        ASSERT_TRUE(g.has_value())
            << "a one-instance group of four members must not breach a cap of four";
        EXPECT_EQ(g->entry_count(), 4U)
            << "448/447/452/802 — the trailing 10= checksum is NOT a member (was 5 under "
               "L-085-1/#220's rest-of-message extent)";
    }

    // ARM 2 — cap 3: the SAME frame through the SAME call must still be
    // rejected. Without this, arm 1 would also pass against a deleted cap.
    {
        std::pmr::monotonic_buffer_resource arena;
        OffsetTable::Config cfg{.max_offset_entries = 4096, .max_group_entries_per_instance = 3};
        auto mv = [&]() {
            Parser<access_mode::Index> parser{dict};
            return parser.parse(*fv, &arena, cfg);
        }();
        ASSERT_TRUE(mv.has_value());

        auto g = mv->offsets().group(453);
        ASSERT_FALSE(g.has_value()) << "four members DO breach a cap of three — cap still live";
        EXPECT_EQ(g.error(), error::wire_group_too_large);
    }
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

    // 220: DICT-AWARE construction — group_slices() derives its boundary from
    // group(), which is now a dictionary-only operation. The property under
    // test (a slice starts at "tag=", not at the value) is unchanged by that.
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 34)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 447)
        .set_group_first(453, 448)
        .add_group_member(453, 447);

    std::pmr::monotonic_buffer_resource arena;
    auto mv = [&]() {
        Parser<access_mode::Index> parser{dict};
        return parser.parse(*fv, &arena);
    }();
    ASSERT_TRUE(mv.has_value());
    auto const& t = mv->offsets();

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

// Reading a SECOND top-level group tag must not relocate the slices an earlier
// tag's span already points into. The discriminator is POINTER STABILITY: the
// parse arena is a monotonic_buffer_resource, which never frees, so a stale span
// stays READABLE and a contents check passes under both the broken and the
// correct shape. Comparing a held span's `.data()` against a re-fetch is what
// actually separates them — not ASan, and not the slice values.
//
// ⚠️ 389 CHANGED WHAT THIS CELL WITNESSES, and the reason is worth keeping.
// It was written (PR #181 arena_fit follow-up) to witness the `group_slices_`
// RESERVE invariant: one shared vector, reserved once to
// `group_slices_reserve_bound()`, which "must cover the SUM of both groups'
// instances". Its mutation was "shrink the reserve to 2 and the B-read
// reallocates".
//
// That invariant was FALSE on the shipped path for two features, and THIS CELL
// COULD NOT SEE IT — its two groups both split on the wire delimiter, so the
// declared-count bound was adequate here and the cell stayed green while the
// divergent-delimiter case exceeded the bound. A witness is only as strong as
// the fixture it runs on, and this one was chosen before the delimiter could
// diverge (083). The case that DOES exhibit it is
// `TypedReadSplitAgreement.MaterializingADivergentGroupDoesNotMoveAnotherGroupsSlices`,
// which is RED against the pre-#389 tree.
//
// The cell is KEPT because the property it asserts is still the one that
// matters — it is now satisfied structurally (each `no_tag` owns an exact-sized
// array, so nothing can relocate another group's slices) rather than by a
// reserve that had to be big enough. There is no reserve left to shrink, so the
// old mutation is gone; the mutation that makes this RED is putting both groups
// back into one shared growable vector.
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

    // Read group A and HOLD its span. Since #389, a.data() points into A's OWN
    // exact-sized arena array; before #389 it pointed into a shared growable
    // vector, which is what made this cell necessary.
    auto a = t.group_slices(453);
    ASSERT_EQ(a.size(), 2U);
    auto const* const a_backing_before = a.data();

    // Read group B (a different tag). Pre-#389 this appended 3 more instances
    // to the SHARED vector against a reserve bound of declared(453)+
    // declared(555)=5; since #389 it allocates its own array and cannot touch
    // A's.
    auto b = t.group_slices(555);
    ASSERT_EQ(b.size(), 3U);

    // Re-fetch group A from the cache and compare pointers.
    //
    // ⚠️ WHAT THIS ASSERTION DOES AND DOES NOT CATCH. It discriminates the
    // shape that shipped, whose cache-hit path RECOMPUTED the span from the
    // current base — so a reallocation moved it. It does NOT discriminate a
    // shared-vector variant that stores the already-resolved pointer at push
    // time: that compares EQUAL after a reallocation, into freed memory.
    // Order-independence of arena consumption is the assertion that catches
    // both; it lives with the #389 witness in
    // tests/wire/typed_read_split_agreement_test.cpp.
    auto a2 = t.group_slices(453);
    ASSERT_EQ(a2.size(), 2U);
    EXPECT_EQ(a2.data(), a_backing_before)
        << "reading a second top-level group must not relocate the first group's slices — "
           "since #389 each group owns its array, so no read can move another's";

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

// Read a TU for source inspection, NORMALISING CRLF -> LF.
//
// The read is binary (no CRT text-mode translation), and the windows-msvc
// runners check out with core.autocrlf=true, so on Windows this file arrives
// with "\r\n" line endings. The FR-001b needles below anchor on "\n" at BOTH
// ends in order to key on leading indentation, so without this normalisation
// none of them matches on Windows — and the failure is asymmetric: the two
// "expect 0" assertions pass VACUOUSLY while only the two "expect 1" ones fail.
// A pin that cannot distinguish "the block moved" from "the file is unreadable
// to my matcher" is no pin at all. Normalising here (rather than pinning
// src/wire/offset_table.cpp to -text in .gitattributes) keeps line endings a
// non-contract for a live source file: what FR-001b pins is indentation and
// occurrence counts, and "\r" is incidental to both. No effect on Linux, so the
// Article VII §3 RED->GREEN transcript recorded there still stands unchanged.
std::string slurp(std::filesystem::path const& p) {
    std::ifstream in(p, std::ios::binary);
    std::string s{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    std::erase(s, '\r');
    return s;
}

// 220 — RE-KEYED from 085's T006 FR-001b structural pin.
//
// 085 pinned WHERE the flat per-instance cap loop lived: 4-space
// (function-body) before its relocation, 8-space (dict-free else-body) after.
// #220 deleted that loop with the branch it lived in — OffsetTable::group() is
// now a dictionary-only operation and has no second arm — so the pin's old
// assertion ("exactly 1 occurrence at 8-space") pins a block that must no
// longer exist. It is re-keyed rather than retired, because the property it
// really guards is still live: 085's flat instance-walk block must not
// reappear in group() at either indentation it has ever occupied.
//
// ⚠️ WHAT THIS PIN DOES *NOT* DO — stated, rather than left to be discovered.
// It matches four EXACT strings. A flat wire-derived walk reintroduced with a
// different variable name, an inverted `if (boundary)` shape, or a third
// indentation would pass it. That is not a gap to close by adding needles:
// each new needle is another coverage claim for the next rewrite to falsify,
// which is exactly how 085's own version of this pin came to assert something
// untrue. Treat it as a tripwire for the SPECIFIC block 085 moved and #220
// deleted; re-derive by reading group() if you need the general property.
//
// Still a source-inspection assertion, for 085's original reason: behaviour
// cannot see it. The dictionary path's cap is enforced by
// consume_group_extent's nesting-aware comparison, so a re-introduced flat
// re-walk would be redundant-not-wrong there and every behavioural test would
// stay green. Only reading the source can tell.
//
// ANTI-VACUITY. Every needle below is indentation-keyed and anchored on "\n"
// at both ends, so anything that stops the matcher seeing the TU the way it
// expects — CRLF, a reformat, a rename, a truncated read — drives all counts
// to 0. For 085 that was survivable, because it asserted a count of 1
// somewhere. Here EVERY group() assertion is "expect 0", so an unreadable TU
// would satisfy all of them vacuously. The witness is therefore the SIBLING
// walk that must still exist: group_slices_status()'s own instance splitter
// (L-063-4 leg 1, deliberately still flat and out of #220's scope) declares
// `inst_start` at 16-space and defines the POSITIVE `is_boundary` lambda at
// 16-space. Asserting those FIRST proves the matcher can see this TU and can
// report a non-zero count, before any zero below is believed.
// ⚠️ 389: this paragraph said `if (boundary) {` at 20-space, which is the
// needle the diff replaced 15 lines below — a re-anchored needle must be
// re-stated in the prose that JUSTIFIES it, not only where it is used.
TEST(WireOffsetTable, FR001_NoFlatInstanceWalkInGroup) {
    std::filesystem::path const src{FIXPP_SRC_DIR};
    std::string const tu = slurp(src / "wire" / "offset_table.cpp");
    ASSERT_FALSE(tu.empty());

    auto count_occurrences = [](std::string const& haystack, std::string const& needle) {
        std::size_t n = 0;
        for (std::size_t pos = haystack.find(needle); pos != std::string::npos;
             pos = haystack.find(needle, pos + 1)) {
            ++n;
        }
        return n;
    };

    // ── Non-vacuity witness: the sibling splitter in group_slices_status. ──
    std::size_t const sibling_inst_start_16space =
        count_occurrences(tu, "\n                std::size_t inst_start = first;\n");
    // 389: this needle was `if (boundary) {` at 20-space. The splitter now
    // computes the boundary ONCE, in an `is_boundary` lambda shared by its count
    // pass and its fill pass, so that spelling is gone AND the call site occurs
    // TWICE — swapping the needle alone would have broken the `== 1` below too.
    // Anchored on the lambda's single DEFINITION instead, which is both unique
    // and the thing the witness actually cares about: that this TU is readable
    // and its splitter still resolves instance boundaries.
    std::size_t const sibling_boundary_defn_16space = count_occurrences(
        tu, "\n                auto const is_boundary = [&](std::size_t k) noexcept {\n");

    ASSERT_EQ(sibling_inst_start_16space, 1U)
        << "group_slices_status's sibling instance walk was not found at 16-space (found "
        << sibling_inst_start_16space
        << ") — the matcher cannot see this TU, so every 'expect 0' below would pass "
           "vacuously. Fix the matcher (or this needle) before trusting the zeros.";
    ASSERT_EQ(sibling_boundary_defn_16space, 1U)
        << "group_slices_status's boundary predicate definition was not found at 16-space (found "
        << sibling_boundary_defn_16space << ") — same vacuity risk as above.";

    // ── The assertions proper: 085's flat cap block is at neither of the
    //    indentations it has occupied. See the scope caveat in the header. ──
    // 4-space = function body (085's pre-move home); 8-space = the dict-free
    // else body (085's post-move home). #220 removed both.
    std::size_t const inst_start_4space =
        count_occurrences(tu, "\n    std::size_t inst_start = first;\n");
    std::size_t const inst_start_8space =
        count_occurrences(tu, "\n        std::size_t inst_start = first;\n");
    std::size_t const boundary_guard_8space = count_occurrences(tu, "\n        if (!boundary) {\n");
    std::size_t const boundary_guard_12space =
        count_occurrences(tu, "\n            if (!boundary) {\n");

    EXPECT_EQ(inst_start_4space, 0U)
        << "4-space (function-body) inst_start occurrences: " << inst_start_4space
        << " — a flat instance walk is back in group()'s body; group() must derive its extent "
           "only from consume_group_extent";
    EXPECT_EQ(inst_start_8space, 0U)
        << "8-space inst_start occurrences: " << inst_start_8space
        << " — 085's dict-free else body is gone (#220); a block at this indentation means a "
           "second arm has been re-introduced into group()";
    EXPECT_EQ(boundary_guard_8space, 0U)
        << "8-space 'if (!boundary) {' occurrences: " << boundary_guard_8space
        << " — the flat early-continue guard is back in group()";
    EXPECT_EQ(boundary_guard_12space, 0U)
        << "12-space 'if (!boundary) {' occurrences: " << boundary_guard_12space
        << " — the flat early-continue guard is back in a nested arm of group()";
}

// ════════════════════════════════════════════════════════════════════════════
// 384 (C-8.4 row 2): the splitter KEEPS the wire-derived delimiter when a
// threaded delimiter oracle answers 0.
//
// C-8.4 row 2 said of the dictionary-present case: "There is no wire fallback.
// A zero return means `no_tag` is not a group at all ... which the caller
// already handles as absent." Both halves are wrong about this code. The
// splitter's guard is `if (d != 0) { delim = d; }`, so a zero answer leaves
// `delim` at `entries_[first].tag`; and we only reach the splitter after
// group()'s membership check has ALREADY established that `no_tag` is a group
// with members in this context, so "absent" is not what a zero means here.
//
// The shape is a dictionary that registers a group's MEMBERS without its
// first-field record (`add_group_member` with no `set_group_first`), which
// sets `group_bit` — so membership answers yes — while `group_first_` has no
// row, so `group_first_field` answers 0. A loaded dictionary cannot be in this
// state — but NOT for the reason this comment gave until #389.
//
// ⚠️ The withdrawn carrier, kept because its shape is the point: *"as_table_view()
// calls `set_group_first_ctx` before registering members, and a context with no
// Entity-2 record is rejected by the FR-023 / C-3.4 sweep."* The first half
// cannot carry anything — `set_group_first_ctx` calls `add_group_member_ctx`
// ITSELF (`dict/table_view.hpp`), so ordering can never separate the two — and
// `as_table_view()` writes whatever the lookup returns, INCLUDING 0, with no
// guard. The second half was elsewhere dismissed as "not load-bearing", which
// is backwards: it is one of the two braces.
//
// The reason that holds is two LOADER-side facts, not one: (1) neither loader
// ever STORES a record with delimiter 0 — both gate the flush on
// `captured != 0` — so a 0 from the lookup can only mean *no record*; and
// (2) both loaders' `finalize()` THROWS if a context that will be registered
// has no Entity-2 record. (1) + (2) ⇒ the delimiter written is non-zero.
//
// The hand-built table_view surface can reach it, which is why this cell exists
// at all. See B&L B-384-2, which carries every wrong version of this on purpose.
// ⚠️ This site is itself the lesson: the correction reached B&L, the splitter's
// own source comment and the 083 contract, and MISSED this one, because those
// are where the reasoning is RE-DERIVED and this is where it was RESTATED. Fix
// such a claim by searching for the CLAIM:
//   git grep -n 'set_group_first_ctx\|capture_first_emission' -- spec specs brain src include tests
// ════════════════════════════════════════════════════════════════════════════

// Two instances of group 453, each opening on 448 — so a correct split yields
// two slices and a delimiter mix-up yields one or four.
std::vector<std::byte> two_instance_group_frame() {
    return make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "453=2\x01"
        "448=PA\x01"
        "447=D\x01"
        "448=PB\x01"
        "447=E\x01");
}

std::string slice_text(fixpp::wire::group_slice const& gs) {
    return std::string{reinterpret_cast<char const*>(gs.data), gs.len};
}

// A DELIBERATELY WRONG oracle: it names 447 (a real member, but the second
// field of each instance) as the delimiter. Used only by the control arm, to
// show the threaded answer is what the splitter uses when it is non-zero — so
// the zero-answer arm below is about the ZERO and not about the callback being
// ignored outright.
std::uint16_t wrong_group_delim(void const*, fixpp::wire::group_context const&,
                                std::uint16_t) noexcept {
    return 447;
}

TEST(WireOffsetTable, GroupSlicesKeepsWireDelimiterWhenDelimStoreAnswersZero) {
    auto buf = two_instance_group_frame();
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    // Members registered; NO first-field record for 453.
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 34)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 447)
        .add_group_member(453, 448)
        .add_group_member(453, 447);

    // ── Fixture preconditions, asserted rather than assumed ─────────────────
    // Asserted through the ORACLE THE ARM ACTUALLY CALLS, not through the bare
    // one-arg accessor: `context_group_delim_fn` goes to the three-arg
    // context-keyed overload, and the two agree here only because a hand-built
    // table populates no context store. Pinning the bare one would be pinning a
    // different function than the arm exercises.
    ASSERT_EQ(fixpp_test_support::context_group_delim_fn(&dict, fixpp::wire::group_context{},
                                                         std::uint16_t{453}),
              0U)
        << "fixture: add_group_member must NOT register a first-field record, or this cell is "
           "about the ordinary dictionary-sourced path and not about the zero answer";
    ASSERT_FALSE(dict.group_member_tags(std::uint16_t{453}).empty())
        << "fixture: membership must be non-empty, or group() declines before the splitter runs "
           "and the split below would be empty for an unrelated reason";

    auto* const member_fn = &fixpp_test_support::context_group_member_fn;

    // ── CONTROL ARM: the threaded oracle IS consulted and DOES win ──────────
    // With a non-zero (and deliberately wrong) answer of 447, the split moves:
    // each instance now opens at 447, so the division changes. Without this
    // arm, the zero-answer arm below would also pass on a build that ignored
    // `group_delim_fn_` entirely.
    {
        std::pmr::monotonic_buffer_resource arena;
        OffsetTable t{*fv, &arena, &dict, member_fn, &wrong_group_delim};
        ASSERT_TRUE(t.build_status().has_value());
        auto const slices = t.group_slices(453);
        // The EXACT split, not `!= 2`. `EXPECT_NE(size, 2)` also holds at 0 and
        // 1 — i.e. it would pass on a splitter that had degenerated to a single
        // slice for some unrelated reason, which is the opposite of what this
        // arm is for. Naming the three instances pins that the oracle's answer
        // (447) is what drew the boundaries.
        ASSERT_EQ(slices.size(), 3U)
            << "control: with the oracle answering 447, each instance must open at 447 — three "
               "slices, not the wire-derived two. A different count means this cell cannot tell a "
               "consulted oracle from an ignored one";
        EXPECT_EQ(slice_text(slices[0]), "448=PA");
        EXPECT_EQ(slice_text(slices[1]), std::string("447=D\x01"
                                                     "448=PB"));
        EXPECT_EQ(slice_text(slices[2]), "447=E");
    }

    // ── THE ARM: a zero answer leaves the wire-derived delimiter in place ───
    {
        std::pmr::monotonic_buffer_resource arena;
        OffsetTable t{*fv, &arena, &dict, member_fn, &fixpp_test_support::context_group_delim_fn};
        ASSERT_TRUE(t.build_status().has_value());
        auto const slices = t.group_slices(453);
        EXPECT_EQ(slices.size(), 2U)
            << "with the store answering 0, the splitter must keep the membership-validated wire "
               "delimiter (448) and yield the two instances the frame carries — NOT decline, and "
               "NOT split on 0";
    }

    // ── And that is the SAME answer the explicitly-null construction gives ──
    // The two spellings produce the same delimiter and the same slices (the
    // callback is still invoked in the zero-answering arm, so they differ in
    // side effects, not in the split), which is exactly why
    // context_group_delim_fn.hpp warns against a zero-returning stub being
    // offered as "threaded".
    {
        std::pmr::monotonic_buffer_resource arena;
        OffsetTable t{*fv, &arena, &dict, member_fn, /*group_delim_fn=*/nullptr};
        ASSERT_TRUE(t.build_status().has_value());
        EXPECT_EQ(t.group_slices(453).size(), 2U)
            << "an explicit null oracle and a zero-answering one must agree — the equivalence "
               "C-8.4 row 2 denied";
    }
}

}  // namespace
