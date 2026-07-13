// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/group_slice_trailing_soh_test.cpp — 062 T008 (Foundational seam
// gate). Drives OffsetTable::nested_group_slices (T005/T006) through the
// PUBLIC API — the outer group's occurrence slice obtained via the existing
// group()/group_slices() machinery, then handed to nested_group_slices on the
// ROOT OffsetTable — since grouped-entry typed reads (`group[i].field()`)
// don't compile yet (that is exactly what US1/US2 add).
//
// Covers:
//  - WholeFrameParseUnchanged (FR-007): the new seam changes nothing about
//    ordinary whole-frame parsing.
//  - NestedSliceBuildCountedLastField (RC1): a slice-scoped `len+1` nested
//    build over an outer entry whose LAST field is a counted Length+Data
//    field (95/96) succeeds, including the frame-tail placement where the
//    entry's last byte sits immediately before the `10=NNN<SOH>` checksum —
//    the exact boundary where "the terminal SOH is present at data+len" is
//    load-bearing.
//  - OversizedCountPerInstanceCapPreserved (FR-006): the existing
//    OffsetTable::Config per-instance group-cap fail-closed behaviour is
//    unchanged by the 062 seam addition.

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

std::vector<std::byte> make_raw_frame(std::string const& body) {
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string full = "8=FIX.4.4\x01" + nine + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// Mirrors the group_member_fn_t that Parser's dict-lvalue ctor installs
// (parser.hpp:462-472) — this test needs its OWN copy of the function
// pointer to hand explicitly to nested_group_slices (opaque_dict_/
// group_member_fn_ are private on MessageView/Parser; the test drives
// `dict` directly instead, which is exactly the pointer Parser would have
// captured).
// 063 T003: widened with an ignored `group_context const&` param to match the
// widened group_member_fn_t (Phase 2 seam — context carried-but-unused; the
// store stays bare-no_tag-keyed until US1).
bool dict_group_member(void const* d, fixpp::wire::group_context const& /*ctx*/,
                       std::uint16_t no_tag, std::uint16_t tag) noexcept {
    auto const* dict = static_cast<fixpp::dict::table_view const*>(d);
    for (auto const member_tag : dict->group_member_tags(no_tag)) {
        if (member_tag == tag) {
            return true;
        }
    }
    return false;
}

TEST(GroupSliceTrailingSoh, WholeFrameParseUnchanged) {
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
        "34=7\x01"
        "453=1\x01"
        "448=PA\x01"
        "447=D\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    // Top-level/non-group field reads return their exact wire values — the
    // presence of the new nested-subview seam changed nothing about ordinary
    // whole-frame parsing.
    EXPECT_EQ(mv->msg_type(), "D");
    EXPECT_EQ(mv->msg_seq_num(), 7U);

    auto g = mv->offsets().group(453);
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(g->entry_count(), 2U);

    auto slices = mv->offsets().group_slices(453);
    ASSERT_EQ(slices.size(), 1U);
    std::string_view sv{reinterpret_cast<char const*>(slices[0].data), slices[0].len};
    EXPECT_EQ(sv, "448=PA\x01"
                  "447=D");
}

// RC1 witness: the outer entry's LAST field is a counted Length+Data field
// (95/96), the entry contains a NESTED group (802/523), and the whole entry
// sits at the FRAME TAIL — its last byte (96's value) is immediately
// followed only by the terminal SOH and then the "10=NNN<SOH>" checksum. The
// nested sub-view build reads the outer occurrence slice widened by one byte
// (`len+1`) so the counted field's boundary check sees the real trailing SOH
// that is provably already present in the parent frame buffer at
// `data+len` — this is exactly that boundary.
TEST(GroupSliceTrailingSoh, NestedSliceBuildCountedLastField) {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 34)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 802)
        .add_valid("D", 523)
        .add_valid("D", 95)
        .add_valid("D", 96)
        .set_group_first(453, 448)
        .add_group_member(453, 802)
        .add_group_member(453, 523)
        .add_group_member(453, 95)
        .add_group_member(453, 96)
        .set_group_first(802, 523);

    // Outer group 453 (delimiter 448), single occurrence, whose entry
    // contains a nested group 802 (delimiter 523) and ends with the counted
    // Length+Data pair 95/96 — the entry's LAST field, and (since this is
    // the only outer occurrence and the frame ends here) the FRAME TAIL: the
    // "96=ABC" value's terminating SOH is immediately followed by the
    // trailer's "10=".
    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "453=1\x01"
        "448=PA\x01"
        "802=1\x01"
        "523=Q\x01"
        "95=3\x01"
        "96=ABC\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    auto g = mv->offsets().group(453);
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(g->entry_count(), 5U) << "448,802,523,95,96";

    auto outer_slices = mv->offsets().group_slices(453);
    ASSERT_EQ(outer_slices.size(), 1U);
    auto const& outer0 = outer_slices[0];

    // Confirm the slice really does end at the counted field's value, with
    // no bytes left over from the checksum trailer — i.e. this occurrence
    // sits at the frame tail.
    std::string_view outer_sv{reinterpret_cast<char const*>(outer0.data), outer0.len};
    EXPECT_TRUE(outer_sv.ends_with("96=ABC")) << "got: " << outer_sv;
    // The byte at data+len (one past the slice) must be the terminal SOH
    // that the nested build's `len+1` widening depends on.
    ASSERT_EQ(static_cast<char>(*(outer0.data + outer0.len)), '\x01');

    // Drive the T005/T006 seam through the PUBLIC nested_group_slices() API
    // on the ROOT OffsetTable, over this outer occurrence's slice.
    // 063 T008: the new context arg — carried but unused in Phase 2 (the
    // predicate ignores it); a plausible root context is enough.
    fixpp::wire::group_context const test_ctx{.msg_type = "D"};
    auto inner_slices = mv->offsets().nested_group_slices(
        outer0.data, outer0.len, /*nested_no_tag=*/802, &dict, &dict_group_member, fv->token(),
        test_ctx).slices;
    ASSERT_EQ(inner_slices.size(), 1U)
        << "nested sub-view build over a counted-last-field, frame-tail entry must succeed";

    auto const& inner0 = inner_slices[0];
    auto delim_field =
        fixpp::wire::get({inner0.data, inner0.len}, /*tag=*/523, fv->token());
    ASSERT_TRUE(delim_field.has_value());
    EXPECT_EQ(delim_field->as_string(), "Q");

    // A second call with the SAME (slice, no_tag) key must be served from
    // the cache and return the same content (build-once / fetch-cached).
    auto inner_slices_again = mv->offsets().nested_group_slices(
        outer0.data, outer0.len, 802, &dict, &dict_group_member, fv->token(), test_ctx).slices;
    ASSERT_EQ(inner_slices_again.size(), 1U);
    EXPECT_EQ(inner_slices_again[0].data, inner0.data);
    EXPECT_EQ(inner_slices_again[0].len, inner0.len);
}

// FR-006 no-regression: an entry count exceeding the existing
// OffsetTable::Config per-instance cap yields the SAME fail-closed
// wire_group_too_large behaviour as before the 062 entry-read seam addition
// (mirrors WireOffsetTable.DoSCapPerInstanceRejectsOversizedSingleInstance).
TEST(GroupSliceTrailingSoh, OversizedCountPerInstanceCapPreserved) {
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

    // group_slices() must degrade to empty, not crash/UB, on the same
    // RED build — mirrors group()'s own fail-closed disposition.
    auto slices = mv->offsets().group_slices(453);
    EXPECT_TRUE(slices.empty());
}

}  // namespace
