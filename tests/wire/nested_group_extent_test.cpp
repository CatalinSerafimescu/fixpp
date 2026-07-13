// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/nested_group_extent_test.cpp — 063 Phase 4 (US2 Defect B):
// T019 (multi-entry nested extent guard, mutation-proven RED against the
// pre-063 flat walk) + T020 (regression guards: single-entry nested,
// count-of-zero nested, flat/non-nested group, benign same-membership
// reuse — FR-006/C-5) + T022 (K=16 depth-overflow -> err_group_too_large,
// Round-2 tasks-pins §4).
//
// Drives OffsetTable::group()/group_slices()/nested_group_slices() directly
// through the PUBLIC API with a hand-built fixpp::dict::table_view, mirroring
// tests/wire/group_slice_trailing_soh_test.cpp's established pattern (the
// group_member_fn_t copy, make_raw_frame, Parser<Index>{dict}).
//
// RED-proof (T019, recorded in the 063 Phase-4 phase-implementer report):
//   git checkout 88ad2763~1 -- src/wire/offset_table.cpp \
//       include/fixpp/wire/offset_table.hpp
//   cmake --build build/linux-clang-debug -j2 && \
//       ctest --test-dir build/linux-clang-debug -R wire_nested_group_extent_test \
//       --output-on-failure
//   git checkout 88ad2763 -- src/wire/offset_table.cpp \
//       include/fixpp/wire/offset_table.hpp
// reverts to the pre-063 flat `seen_in_instance` walk, under which
// MultiEntryNestedExtentGuard fails (the outer extent truncates at the 2nd
// nested entry).

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/context_group_member_fn.hpp"
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

// Context-aware lookup — the SAME group_member_fn_t shape the Parser
// dict-lvalue ctor installs (parser.hpp:494-517) / defect_a_group_context_
// test.cpp's copy. Needed here to call nested_group_slices() directly
// (opaque_dict_/group_member_fn_ are private on MessageView/Parser). Tries
// the context store FIRST, falling back to the legacy bare-no_tag store on a
// MISS (table_view.hpp) — safe for T019/T020(a-c)/T022 above, which register
// ONLY via the bare (context-free) builder API and so always MISS the
// context store and fall back identically; BenignSameMembershipReuseAcross
// Contexts below is the one test that populates group_ctx_ and so actually
// exercises the context-store HIT path.
// Shared definition: tests/support/context_group_member_fn.hpp. A reference
// alias keeps this file's local name and preserves the &-address-of sites.
constexpr auto& dict_group_member = fixpp_test_support::context_group_member_fn;

}  // namespace

// ── T019 (SC-003) ────────────────────────────────────────────────────────
// Defect-B multi-entry extent guard: an outer group instance (453, delim
// 448) contains a nested group (802, delim 523) with TWO entries. The outer
// extent must enclose BOTH nested entries (INV-B) — the pre-063 flat walk
// truncates at the 2nd nested entry (its repeated delimiter looks like a new
// outer-instance boundary).
TEST(NestedGroupExtent, MultiEntryNestedExtentGuard) {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 802)
        .add_valid("D", 523)
        .add_valid("D", 524)
        .set_group_first(453, 448)
        .add_group_member(453, 802)   // nested count field — transitively under 453
        .add_group_member(453, 523)   // nested delim — transitively under 453
        .add_group_member(453, 524)   // nested member — transitively under 453
        .set_group_first(802, 523)
        .add_group_member(802, 524);

    auto buf = make_raw_frame(
        "35=D\x01"
        "453=1\x01"
        "448=PA\x01"
        "802=2\x01"
        "523=E0\x01"
        "524=V0\x01"
        "523=E1\x01"
        "524=V1\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    auto g = mv->offsets().group(453);
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(g->entry_count(), 6U) << "448,802,523,524,523,524 — the outer extent must "
                                       "enclose BOTH nested entries, not truncate at the 2nd";

    auto outer_slices = mv->offsets().group_slices(453);
    ASSERT_EQ(outer_slices.size(), 1U);
    auto const& outer0 = outer_slices[0];

    fixpp::wire::group_context const ctx{.msg_type = "D"};
    auto nested_slices = mv->offsets().nested_group_slices(
        outer0.data, outer0.len, /*nested_no_tag=*/802, &dict, &dict_group_member, fv->token(), ctx).slices;
    ASSERT_EQ(nested_slices.size(), 2U)
        << "INV-B: the nested group's full 2-entry extent must be enclosed by the outer";

    auto e0_field = fixpp::wire::get({nested_slices[0].data, nested_slices[0].len}, 524, fv->token());
    ASSERT_TRUE(e0_field.has_value());
    EXPECT_EQ(e0_field->as_string(), "V0");

    auto e1_field = fixpp::wire::get({nested_slices[1].data, nested_slices[1].len}, 524, fv->token());
    ASSERT_TRUE(e1_field.has_value());
    EXPECT_EQ(e1_field->as_string(), "V1");
}

// ── T020 (FR-006 / INV-preserve / C-5) ───────────────────────────────────

// (a) single-entry nested: no over-consumption past the one entry.
TEST(NestedGroupExtent, SingleEntryNestedNoOverConsumption) {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 802)
        .add_valid("D", 523)
        .add_valid("D", 524)
        .set_group_first(453, 448)
        .add_group_member(453, 802)
        .add_group_member(453, 523)
        .add_group_member(453, 524)
        .set_group_first(802, 523)
        .add_group_member(802, 524);

    auto buf = make_raw_frame(
        "35=D\x01"
        "453=1\x01"
        "448=PA\x01"
        "802=1\x01"
        "523=E0\x01"
        "524=V0\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    auto g = mv->offsets().group(453);
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(g->entry_count(), 4U) << "448,802,523,524";

    auto outer_slices = mv->offsets().group_slices(453);
    ASSERT_EQ(outer_slices.size(), 1U);
    auto const& outer0 = outer_slices[0];

    fixpp::wire::group_context const ctx{.msg_type = "D"};
    auto nested_slices = mv->offsets().nested_group_slices(
        outer0.data, outer0.len, 802, &dict, &dict_group_member, fv->token(), ctx).slices;
    ASSERT_EQ(nested_slices.size(), 1U);
    auto field = fixpp::wire::get({nested_slices[0].data, nested_slices[0].len}, 524, fv->token());
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->as_string(), "V0");
}

// (b) count-of-zero nested: consumes no extent (B-004-7); the outer walk
// continues normally past the zero-count group to a trailing scalar.
TEST(NestedGroupExtent, CountOfZeroNestedConsumesNoExtent) {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 802)
        .add_valid("D", 449)
        .set_group_first(453, 448)
        .add_group_member(453, 802)
        .add_group_member(453, 449);

    auto buf = make_raw_frame(
        "35=D\x01"
        "453=1\x01"
        "448=PA\x01"
        "802=0\x01"
        "449=DIRECT\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    auto g = mv->offsets().group(453);
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(g->entry_count(), 3U) << "448,802,449 — zero-count nested group consumes no extent";

    auto outer_slices = mv->offsets().group_slices(453);
    ASSERT_EQ(outer_slices.size(), 1U);
    auto const& outer0 = outer_slices[0];

    // The trailing scalar 449 must be reachable inside the outer occurrence:
    // the zero-count nested group didn't swallow anything, nor did it break
    // the outer walk's continuation past it.
    auto f = fixpp::wire::get({outer0.data, outer0.len}, 449, fv->token());
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->as_string(), "DIRECT");

    fixpp::wire::group_context const ctx{.msg_type = "D"};
    auto nested_slices = mv->offsets().nested_group_slices(
        outer0.data, outer0.len, 802, &dict, &dict_group_member, fv->token(), ctx).slices;
    EXPECT_TRUE(nested_slices.empty()) << "802=0 must yield zero nested instances";
}

// (c) flat/non-nested group: unaffected by the nesting-aware walk.
TEST(NestedGroupExtent, FlatNonNestedGroupUnchanged) {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 447)
        .set_group_first(453, 448)
        .add_group_member(453, 447);

    auto buf = make_raw_frame(
        "35=D\x01"
        "453=1\x01"
        "448=PA\x01"
        "447=D\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    auto g = mv->offsets().group(453);
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(g->entry_count(), 2U);

    auto slices = mv->offsets().group_slices(453);
    ASSERT_EQ(slices.size(), 1U);
    std::string_view sv{reinterpret_cast<char const*>(slices[0].data), slices[0].len};
    EXPECT_EQ(sv, "448=PA\x01"
                  "447=D");
}

// (d) multiple occurrences of the SAME group (no_tag 802, SAME membership,
// same context) — each outer occurrence gets its own 2-entry nested group
// with DISTINCT values. Proves no cross-occurrence collision (the
// per-occurrence slicing is keyed by outer-slice byte identity, not just
// no_tag) when combined with the new multi-entry walk. This is a SIBLING
// regression to (e) below, not the C-5 "same tag reused across differing
// CONTEXTS" case — see (e)'s comment for that.
TEST(NestedGroupExtent, MultipleOccurrencesOfSameGroupNoCollision) {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 802)
        .add_valid("D", 523)
        .add_valid("D", 524)
        .set_group_first(453, 448)
        .add_group_member(453, 802)
        .add_group_member(453, 523)
        .add_group_member(453, 524)
        .set_group_first(802, 523)
        .add_group_member(802, 524);

    auto buf = make_raw_frame(
        "35=D\x01"
        "453=2\x01"
        "448=PA0\x01"
        "802=2\x01"
        "523=E00\x01"
        "524=V00\x01"
        "523=E01\x01"
        "524=V01\x01"
        "448=PA1\x01"
        "802=2\x01"
        "523=E10\x01"
        "524=V10\x01"
        "523=E11\x01"
        "524=V11\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    auto outer_slices = mv->offsets().group_slices(453);
    ASSERT_EQ(outer_slices.size(), 2U);

    fixpp::wire::group_context const ctx{.msg_type = "D"};

    auto nested0 = mv->offsets().nested_group_slices(outer_slices[0].data, outer_slices[0].len, 802,
                                                      &dict, &dict_group_member, fv->token(), ctx).slices;
    ASSERT_EQ(nested0.size(), 2U);
    auto v00 = fixpp::wire::get({nested0[0].data, nested0[0].len}, 524, fv->token());
    ASSERT_TRUE(v00.has_value());
    EXPECT_EQ(v00->as_string(), "V00");
    auto v01 = fixpp::wire::get({nested0[1].data, nested0[1].len}, 524, fv->token());
    ASSERT_TRUE(v01.has_value());
    EXPECT_EQ(v01->as_string(), "V01");

    auto nested1 = mv->offsets().nested_group_slices(outer_slices[1].data, outer_slices[1].len, 802,
                                                      &dict, &dict_group_member, fv->token(), ctx).slices;
    ASSERT_EQ(nested1.size(), 2U);
    auto v10 = fixpp::wire::get({nested1[0].data, nested1[0].len}, 524, fv->token());
    ASSERT_TRUE(v10.has_value());
    EXPECT_EQ(v10->as_string(), "V10");
    auto v11 = fixpp::wire::get({nested1[1].data, nested1[1].len}, 524, fv->token());
    ASSERT_TRUE(v11.has_value());
    EXPECT_EQ(v11->as_string(), "V11");
}

// (e) C-5's actual "benign same-membership tag reuse": no_tag 802 reused as
// a NESTED group under TWO DIFFERING CONTEXTS — msg_type "D" nested under
// outer group 453, and msg_type "8" nested under a DIFFERENT outer group
// 460 — with IDENTICAL declared membership {523, 524} registered via the
// context-scoped `_ctx` store (NOT the bare fallback: two DISTINCT
// `(msg_type, parent_path, 802)` keys are populated so `dict_group_member`'s
// context-store lookup HITS for both, mirroring the benign counterpart to
// Defect A's differing-membership collision covered by
// tests/dictionary/defect_a_group_context_test.cpp). Both messages must
// resolve their own 2-entry nested group correctly — no cross-context
// interference, no collision, both context keys independently correct.
TEST(NestedGroupExtent, BenignSameMembershipReuseAcrossContexts) {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("8", 460)
        .add_valid("8", 461)
        .set_group_first(453, 448)
        .add_group_member(453, 802)
        .add_group_member(453, 523)
        .add_group_member(453, 524)
        .set_group_first(460, 461)
        .add_group_member(460, 802)
        .add_group_member(460, 523)
        .add_group_member(460, 524);
    // Context-scoped registration: no_tag 802 nested under 453 in msg "D",
    // and under 460 in msg "8" — TWO DISTINCT context keys, IDENTICAL
    // declared membership {523, 524}.
    dict.set_group_first_ctx("D", std::array<std::uint16_t, 1>{453}, 802, 523);
    dict.add_group_member_ctx("D", std::array<std::uint16_t, 1>{453}, 802, 524);
    dict.set_group_first_ctx("8", std::array<std::uint16_t, 1>{460}, 802, 523);
    dict.add_group_member_ctx("8", std::array<std::uint16_t, 1>{460}, 802, 524);

    auto buf_d = make_raw_frame(
        "35=D\x01"
        "453=1\x01"
        "448=PA\x01"
        "802=2\x01"
        "523=E00\x01"
        "524=V00\x01"
        "523=E01\x01"
        "524=V01\x01");
    auto fv_d = fixpp::wire::test::make_frame_view(buf_d);
    ASSERT_TRUE(fv_d.has_value());
    std::pmr::monotonic_buffer_resource arena_d;
    Parser<access_mode::Index> parser_d{dict};
    auto mv_d = parser_d.parse(*fv_d, &arena_d);
    ASSERT_TRUE(mv_d.has_value());

    // Seed the ROOT table's stored context (msg_type) — the same seeding
    // MessageView::group<>() performs in production (parser.hpp:272-273)
    // before group()'s membership predicate calls; this low-level test
    // drives OffsetTable::group_slices() directly, so it must seed context
    // itself.
    mv_d->offsets().set_group_context(fixpp::wire::group_context{.msg_type = "D"});
    auto outer_d = mv_d->offsets().group_slices(453);
    ASSERT_EQ(outer_d.size(), 1U);
    // ctx passed to nested_group_slices is the CALLING ENTRY's OWN context —
    // its container path + its own no_tag (offset_table.hpp's
    // nested_group_slices doc) — i.e. path=[453], depth=1 (we are inside a
    // 453-instance), NOT the bare root context.
    fixpp::wire::group_context const ctx_d{.msg_type = "D", .parent_path = {453}, .depth = 1};
    auto nested_d = mv_d->offsets().nested_group_slices(outer_d[0].data, outer_d[0].len, 802, &dict,
                                                        &dict_group_member, fv_d->token(), ctx_d).slices;
    ASSERT_EQ(nested_d.size(), 2U) << "context (\"D\",[453],802) must resolve its own membership";
    auto d0 = fixpp::wire::get({nested_d[0].data, nested_d[0].len}, 524, fv_d->token());
    ASSERT_TRUE(d0.has_value());
    EXPECT_EQ(d0->as_string(), "V00");
    auto d1 = fixpp::wire::get({nested_d[1].data, nested_d[1].len}, 524, fv_d->token());
    ASSERT_TRUE(d1.has_value());
    EXPECT_EQ(d1->as_string(), "V01");

    auto buf_8 = make_raw_frame(
        "35=8\x01"
        "460=1\x01"
        "461=QA\x01"
        "802=2\x01"
        "523=E10\x01"
        "524=V10\x01"
        "523=E11\x01"
        "524=V11\x01");
    auto fv_8 = fixpp::wire::test::make_frame_view(buf_8);
    ASSERT_TRUE(fv_8.has_value());
    std::pmr::monotonic_buffer_resource arena_8;
    Parser<access_mode::Index> parser_8{dict};
    auto mv_8 = parser_8.parse(*fv_8, &arena_8);
    ASSERT_TRUE(mv_8.has_value());

    mv_8->offsets().set_group_context(fixpp::wire::group_context{.msg_type = "8"});
    auto outer_8 = mv_8->offsets().group_slices(460);
    ASSERT_EQ(outer_8.size(), 1U);
    fixpp::wire::group_context const ctx_8{.msg_type = "8", .parent_path = {460}, .depth = 1};
    auto nested_8 = mv_8->offsets().nested_group_slices(outer_8[0].data, outer_8[0].len, 802, &dict,
                                                        &dict_group_member, fv_8->token(), ctx_8).slices;
    ASSERT_EQ(nested_8.size(), 2U) << "context (\"8\",[460],802) must resolve its own membership";
    auto e0 = fixpp::wire::get({nested_8[0].data, nested_8[0].len}, 524, fv_8->token());
    ASSERT_TRUE(e0.has_value());
    EXPECT_EQ(e0->as_string(), "V10");
    auto e1 = fixpp::wire::get({nested_8[1].data, nested_8[1].len}, 524, fv_8->token());
    ASSERT_TRUE(e1.has_value());
    EXPECT_EQ(e1->as_string(), "V11");
}

// ── T022 (K=16 depth-overflow, Round-2 tasks-pins §4) ─────────────────────
//
// Built by reusing the SAME (900=count, 901=delim) tag pair recursively at
// every level (900 registered as a member of ITS OWN group — a
// self-referential recursive group, valid since membership is fully
// hand-built here). 17 nested pairs are needed to reach depth==16: level d
// (d=0..15) processes pair d+1 and then recurses into pair d+2 at depth d+1;
// the 17th pair's "900" field triggers the depth==16 check (kMaxGroupDepth)
// before anything inside it is read.
TEST(NestedGroupExtent, DepthOverflowReturnsGroupTooLarge) {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 900)
        .add_valid("D", 901)
        .set_group_first(900, 901)
        .add_group_member(900, 900);  // 900 is a member of its own group -> self-nesting chain

    std::string body = "35=D\x01";
    for (int i = 0; i < 17; ++i) {
        body += "900=1\x01" "901=X\x01";
    }
    auto buf = make_raw_frame(body);
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    auto g = mv->offsets().group(900);
    ASSERT_FALSE(g.has_value());
    EXPECT_EQ(g.error(), error::wire_group_too_large);

    auto slices = mv->offsets().group_slices(900);
    EXPECT_TRUE(slices.empty()) << "group_slices() must degrade to empty, not crash/UB, on overflow";
}

// Negative control: exactly 16 repeats (one fewer than the overflow trigger)
// stays within depth<16 and must NOT overflow — proves the K=16 disposition
// is a genuine boundary, not an always-fail stub.
TEST(NestedGroupExtent, DepthSixteenNoOverflow) {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 900)
        .add_valid("D", 901)
        .set_group_first(900, 901)
        .add_group_member(900, 900);

    std::string body = "35=D\x01";
    for (int i = 0; i < 16; ++i) {
        body += "900=1\x01" "901=X\x01";
    }
    auto buf = make_raw_frame(body);
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    auto g = mv->offsets().group(900);
    ASSERT_TRUE(g.has_value())
        << "16 levels of nesting is exactly at the K=16 boundary and must NOT overflow";
    EXPECT_NE(g->entry_count(), 0U);
    // NOTE: this test's self-referential same-tag-pair reuse (900/901 at
    // EVERY level) is a deliberate scoping trick for the depth-K count only —
    // it is NOT representative of a real dictionary (where a nested group's
    // own delimiter differs from its enclosing group's). group_slices()'
    // separate flat instance-boundary re-scan (offset_table.cpp:556-568/
    // 604-624) is out of scope here (unaffected by T021/consume_group_extent,
    // which only computes the EXTENT) and is deliberately not exercised by
    // this negative control — see MultiEntryNestedExtentGuard above for the
    // correctly-distinct-tag nested-instance-count assertion.
}
