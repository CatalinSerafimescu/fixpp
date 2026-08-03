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

// 085 T013 (US2, FR-004/SC-004). Pins the DoS cap on the DICT-FREE fallback
// branch of OffsetTable::group() (src/wire/offset_table.cpp's `else` arm,
// post-085-relocation) — the one branch the dictionary-path tests above
// cannot exercise, since a dict-free table has no `opaque_dict_`/
// `group_member_fn_` and so always takes that `else`.
//
// Both a DICT-FREE construction (the 3-arg (frame_view, memory_resource*,
// Config) constructor — never Parser<access_mode::Index>, which threads a
// dictionary and lands on the dictionary branch instead) AND a TIGHTENED
// Config are required to reach this: under the default Config the branch is
// arithmetically unreachable (research.md R-2 — default_max_offset_entries ==
// default_max_group_entries_per_instance == 4096, and build() clamps the
// table at the former (offset_table.cpp:326), so the dict-free segment
// entries_.size() - first is always <= 4095 < 4096 and inst_count > cap can
// never hold). That is exactly why no such test existed before 085.
//
// Frame layout (envelope + body, whole-frame entries_ in document order):
//   idx 0: 8=FIX.4.4   idx 1: 9=<len>   idx 2: 35=D   idx 3: 34=1
//   idx 4: 453=1  <- count field (no_tag)
//   idx 5: 448=PA <- first = count_idx+1 = 5; delim = entries_[5].tag = 448
//   idx 6: 447=D  idx 7: 452=1  idx 8: 802=1
//   idx 9: 10=000 <- checksum; dict-free group_end = entries_.size() = 10
//
// Dict-free extent rule (no dictionary => no per-instance re-walk on 448):
// group_end is rest-of-message (10), so the loop's ONLY boundary before the
// final one would be a LATER entry whose tag == delim (448) — there is none
// here, so the sole boundary hit is k == group_end == 10. That makes ONE
// instance spanning idx 5..9, inst_count = 10 - 5 = 5, which breaches
// max_group_entries_per_instance = 3 (5 > 3) and must return
// error::wire_group_too_large. Verified by running, not by reasoning alone
// (per this task's instruction) — see T015's mutation transcript.
//
// The RED evidence for the companion mutation-transcript task (T015) lives in
// .specify/decisions/085-fold-flat-cap-loop-verify.md.
//
// ⚠ THESE TWO TESTS ENCODE A KNOWN, FILED DEFECT — read the numbers with that
// in mind. The 5-entry instance above is 448/447/452/802 **plus the trailing
// checksum field 10=000**: on the dict-free path `group_end` is
// rest-of-message, so top-level fields after the group count toward the last
// instance. That is limitation **L-085-1 / fixpp#220**, preserved (not
// repaired) by this feature because 085 is a semantics-preserving relocation.
// Contrast `GroupExtentExcludesTrailingTopLevelFields` in this same file: on
// the DICTIONARY path the trailing top-level field is correctly excluded.
// When #220 is fixed, the real instance here becomes 4 entries, so
// `AllowsWhenCapRaised`'s `entry_count()` expectation below changes 5 -> 4.
// The rejecting pin is unaffected either way (4 still breaches a cap of 3);
// only the entry_count assertion pins the buggy value.
// Stated explicitly so these tests cannot be read as blessing the trailer
// inclusion as intended behaviour.
//
// The two tests below MUST drive the identical frame — that is what makes them
// a bracket around the cap threshold rather than two unrelated observations.
// The bytes therefore live in this one helper instead of being duplicated in
// each TEST body, so the invariant is enforced by construction rather than by
// a comment asking the next editor to keep them in sync.
std::vector<std::byte> dict_free_over_cap_frame() {
    return make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "453=1\x01"
        "448=PA\x01"
        "447=D\x01"
        "452=1\x01"
        "802=1\x01");
}

TEST(WireOffsetTable, DictFreeDoSCapPerInstanceRejectsOversizedInstance) {
    auto buf = dict_free_over_cap_frame();
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable::Config tight_cfg{.max_offset_entries = 4096, .max_group_entries_per_instance = 3};
    OffsetTable t{*fv, &arena, tight_cfg};
    ASSERT_TRUE(t.build_status().has_value());

    auto g = t.group(453);
    ASSERT_FALSE(g.has_value());
    EXPECT_EQ(g.error(), error::wire_group_too_large);
}

// 085 T014 (US2, FR-005a(ii)/SC-004a). Bracketing companion to the case
// above: the IDENTICAL frame bytes, with max_group_entries_per_instance
// raised to exactly the breaching instance's size (5) so the strict `>`
// comparison no longer fires (5 > 5 is false) and group() succeeds. Same
// frame is required — changing it would prove nothing about the threshold
// this brackets.
TEST(WireOffsetTable, DictFreeDoSCapPerInstanceAllowsWhenCapRaised) {
    auto buf = dict_free_over_cap_frame();  // SAME frame as the rejecting pin — that is the bracket
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable::Config raised_cfg{.max_offset_entries = 4096, .max_group_entries_per_instance = 5};
    OffsetTable t{*fv, &arena, raised_cfg};
    ASSERT_TRUE(t.build_status().has_value());

    auto g = t.group(453);
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(g->no_tag(), 453U);
    // 5 = the four real members (448/447/452/802) + the trailing checksum
    // 10=000, which the dict-free rest-of-message extent wrongly absorbs.
    // This value pins L-085-1 / fixpp#220, NOT intended behaviour — it becomes
    // 4 when that limitation is repaired.
    EXPECT_EQ(g->entry_count(), 5U)
        << "expected 5 only because L-085-1/#220 counts the trailing checksum "
           "into the last dict-free instance; becomes 4 when #220 is fixed";
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

// T006 — FR-001b red-first structural pin (085-fold-flat-cap-loop).
// A SOURCE-inspection assertion, not a behaviour test: it asserts WHERE the
// flat per-instance cap loop (currently OffsetTable::group()'s function
// body, after the dict/dict-free if/else) lives in the source, and that it
// moved into the dict-free `else` branch *unchanged*. Behaviour cannot
// distinguish "flat loop in the function body" from "flat loop relocated
// into the else branch" because the dictionary path's cap is already
// enforced, redundantly, by consume_group_extent's nesting-aware comparison
// (research.md R-1) — the flat re-walk is unreachable-as-an-error on the
// dictionary path either way, so every behavioural test (dictionary-path
// and dict-free) stays green whether or not the relocation has happened.
// Only reading the source can tell the two states apart.
//
// Two independent keys are asserted, because one alone is insufficient:
//
// (a) THE BLOCK MOVED: `inst_start`'s declaration sits at 4-space
//     (function-body) indentation before the move, and at 8-space
//     (else-body) indentation after. This alone would also pass for an
//     *inverted* relocation — `if (boundary) { ... }` replacing
//     `if (!boundary) continue;` while re-indenting — which is semantically
//     identical (so no behavioural test catches it) but violates C-3 #4 and
//     FR-001a's "moved unchanged" requirement. So:
//
// (b) THE BLOCK MOVED UNCHANGED: the early-continue guard
//     `if (!boundary) {` (immediately followed by `continue;`) sits at
//     8-space before the move and at 12-space after. An else-inverted
//     relocation would fail this key even though it passes key (a) — this
//     is the only guard in the whole feature that would catch it (per
//     tasks.md T006/T007).
//
// Mirrors tests/dictionary/load_any_test.cpp's
// LoadAny.FR004_SingleSharedDispatchSourceInspection construction: plain
// std::string::find/count, no AST.
TEST(WireOffsetTable, FR001_SingleTraversalSourceInspection) {
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

    // Key (a): the block moved.
    std::size_t const inst_start_4space =
        count_occurrences(tu, "\n    std::size_t inst_start = first;\n");
    std::size_t const inst_start_8space =
        count_occurrences(tu, "\n        std::size_t inst_start = first;\n");

    // Key (b): the block moved unchanged.
    std::size_t const boundary_guard_8space = count_occurrences(tu, "\n        if (!boundary) {\n");
    std::size_t const boundary_guard_12space =
        count_occurrences(tu, "\n            if (!boundary) {\n");

    // ANTI-VACUITY GUARD — assert the block is in EXACTLY ONE of the two
    // tracked indentations before asserting WHICH one.
    //
    // Every count above is an indentation-keyed needle anchored on "\n" at both
    // ends, so anything that stops the matcher seeing the TU the way it expects
    // — CRLF line endings, a reformat, a rename, a truncated read — drives all
    // four counts to 0. That is not neutral: it SATISFIES both "expect 0"
    // assertions, so the pin half-passes for a reason having nothing to do with
    // the relocation it exists to prove. (Observed: this pin went red on
    // windows-msvc with 4space=0/8space=0, where the "expect 0" leg passed
    // vacuously. The CRLF cause is fixed in slurp() above; this guard makes the
    // whole vacuity class impossible rather than just that one instance.)
    //
    // The sum is the right invariant because the block must exist somewhere:
    // pre-move it is 4-space, post-move 8-space, never both and never neither.
    // Deliberately NOT a whole-file count — `std::size_t inst_start = first;`
    // legitimately appears twice, ours and group_slices_status's sibling walk at
    // 16-space (which uses the POSITIVE `if (boundary) {` at 20-space, so it
    // matches none of these four needles).
    ASSERT_EQ(inst_start_4space + inst_start_8space, 1U)
        << "the flat cap block's `inst_start` is in NEITHER tracked indentation (4-space="
        << inst_start_4space << ", 8-space=" << inst_start_8space
        << ") — the matcher cannot see the TU it is pinning, so the counts below are vacuous";
    ASSERT_EQ(boundary_guard_8space + boundary_guard_12space, 1U)
        << "the early-continue guard is in NEITHER tracked indentation (8-space="
        << boundary_guard_8space << ", 12-space=" << boundary_guard_12space
        << ") — the counts below are vacuous";

    EXPECT_EQ(inst_start_4space, 0U)
        << "4-space (function-body) inst_start occurrences: " << inst_start_4space
        << " — expected 0 once the flat cap block has moved into the dict-free else branch";
    EXPECT_EQ(inst_start_8space, 1U)
        << "8-space (else-body) inst_start occurrences: " << inst_start_8space
        << " — expected exactly 1 once the flat cap block has moved into the dict-free else branch";
    EXPECT_EQ(boundary_guard_8space, 0U)
        << "8-space (function-body) 'if (!boundary) {' occurrences: " << boundary_guard_8space
        << " — expected 0 once the early-continue guard has moved unchanged into the else branch";
    EXPECT_EQ(boundary_guard_12space, 1U)
        << "12-space (else-body) 'if (!boundary) {' occurrences: " << boundary_guard_12space
        << " — expected exactly 1 once the early-continue guard has moved unchanged into the else "
           "branch";
}

}  // namespace
