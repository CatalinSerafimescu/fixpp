// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/nested_group_slices_failloud_test.cpp — 073 T001 (Foundational
// wire-level primitive witness, L-065-2 / #184).
//
// Proves OffsetTable::nested_group_slices()'s `nested_slices_result` reports
// `alloc_failed == true` for EACH of the three arena-exhaustion origins
// research.md §D2 documents:
//   (a) build_nested_subview's own shell allocation fails -> nullptr
//       (offset_table.cpp:722-724).
//   (b) the sub-table builds non-null, but its OWN group_slices_status()
//       materialization throws bad_alloc (offset_table.cpp:684-689).
//   (c) the sub-table builds non-null, but its ctor's internal build()
//       degrades on bad_alloc (status_ = out_of_memory,
//       offset_table.cpp:366-370) — the mode found at /speckit-implement
//       (feedback_status_origin_must_cover_all_alloc_catch_sites).
//
// Exhaustion is driven by a genuinely faithful tiny-capacity
// `std::pmr::monotonic_buffer_resource` over `std::pmr::null_memory_resource()`
// (quickstart.md "Faithful exhaustion harness") — NOT a hand-built 16 KiB
// message, NOT a post-hoc flag
// ([[feedback_fault_injection_posthoc_flag_unfaithful]]).
//
// `sizeof(OffsetTable)` (≈280 B on clang-debug) shifts across toolchains, so a
// cap tuned to one mode here is NOT portable — this witness therefore pins
// each mode by INTROSPECTION on the actual built sub-table
// (`nested_cache_access_for_testing::resolve`, a TEST-ONLY friend seam
// declared in offset_table.hpp / defined in wire_test_hooks.hpp — mirrors the
// frame_view_access / frame_view_slice_access split, framer.hpp:104-115),
// NOT by cap band alone: every mode test asserts the introspected sub-table
// state FIRST (failing loud if a platform's cap lands in a different mode)
// and only then asserts the observable `alloc_failed` contract.
//
// Cap values (1500 / 1930 / 2700 bytes) were derived empirically on this
// build (clang-debug, sizeof(OffsetTable)==280) via a byte-granularity sweep
// against the fixture below (20-instance nested group 802/523 inside a
// single 453/448 occurrence) and land in the MIDDLE of wide (>=150-byte)
// stable per-mode bands, not at a fragile edge. They are clang-debug-local;
// the introspection assert is what keeps the witness correct (not silently
// vacuous) if a different toolchain's `sizeof(OffsetTable)` shifts the band.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <string>
#include <vector>

#include "support/context_group_member_fn.hpp"
#include "support/frame_view_factory.hpp"
#include "support/mock_dict_table.hpp"
#include "support/wire_test_hooks.hpp"

namespace {

using fixpp::core::error;
using fixpp::wire::group_context;
using fixpp::wire::nested_cache_access_for_testing;
using fixpp::wire::OffsetTable;

std::vector<std::byte> make_raw_frame(std::string const& body) {
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string full = "8=FIX.4.4\x01" + nine + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// Same context-aware group_member_fn_t the Parser dict-lvalue ctor installs
// (shared copy, tests/support/context_group_member_fn.hpp).
constexpr auto& dict_group_member = fixpp_test_support::context_group_member_fn;

constexpr group_context kTestCtx{.msg_type = "D"};
constexpr std::uint16_t kOuterNoTag = 453;   // outer group count field
constexpr std::uint16_t kOuterDelim = 448;   // outer group first member
constexpr std::uint16_t kInnerNoTag = 802;   // nested group count field
constexpr std::uint16_t kInnerDelim = 523;   // nested group first (only) member
constexpr int kInnerInstances = 20;          // sized so a fully-built sub-table's
                                              // own group_slices_ reserve() is
                                              // large enough to isolate mode (b)
                                              // from mode (c) by cap alone.

fixpp::dict::table_view make_dict() {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 34)
        .add_valid("D", kOuterNoTag)
        .add_valid("D", kOuterDelim)
        .add_valid("D", kInnerNoTag)
        .add_valid("D", kInnerDelim)
        .set_group_first(kOuterNoTag, kOuterDelim)
        .add_group_member(kOuterNoTag, kInnerNoTag)
        .add_group_member(kOuterNoTag, kInnerDelim)
        .set_group_first(kInnerNoTag, kInnerDelim);
    return dict;
}

// One outer (453/448) occurrence containing a nested group (802/523) with
// `kInnerInstances` instances — genuinely present, non-trivial extent.
std::string make_present_body() {
    std::string body = "35=D\x01" "34=1\x01";
    body += std::to_string(kOuterNoTag) + "=1\x01";
    body += std::to_string(kOuterDelim) + "=PA\x01";
    body += std::to_string(kInnerNoTag) + "=" + std::to_string(kInnerInstances) + "\x01";
    for (int i = 0; i < kInnerInstances; ++i) {
        body += std::to_string(kInnerDelim) + "=Q\x01";
    }
    return body;
}

}  // namespace

// ── Mode (a): shell allocation fails -> nullptr ────────────────────────────
TEST(NestedGroupSlicesFailLoud, ModeA_ShellAllocNullReportsFailLoud) {
    constexpr std::size_t kCapModeA = 1500;
    auto frame_buf = make_raw_frame(make_present_body());
    auto fv = fixpp::wire::test::make_frame_view(frame_buf);
    ASSERT_TRUE(fv.has_value());
    auto dict = make_dict();

    std::vector<std::byte> arena_buf(kCapModeA);
    std::pmr::monotonic_buffer_resource arena{arena_buf.data(), arena_buf.size(),
                                              std::pmr::null_memory_resource()};
    OffsetTable root{*fv, &arena, &dict, &dict_group_member};
    ASSERT_TRUE(root.build_status()) << "root itself must build at this cap";
    auto outer = root.group_slices(kOuterNoTag);
    ASSERT_EQ(outer.size(), 1U);
    auto const slice = outer[0];

    auto const r1 = root.nested_group_slices(slice.data, slice.len, kInnerNoTag, kTestCtx);

    // Introspection FIRST: pin that this cap actually lands in mode (a) on
    // THIS build, before trusting the observable contract.
    auto const* sub = nested_cache_access_for_testing::resolve(root, slice.data, kInnerNoTag);
    ASSERT_EQ(sub, nullptr) << "cap did not land in mode (a) on this build -- "
                                "sizeof(OffsetTable)/allocator behaviour drifted; "
                                "re-derive kCapModeA";

    EXPECT_TRUE(r1.alloc_failed);
    EXPECT_TRUE(r1.slices.empty());

    // Repeated-read (D2 cache-hit exit discriminator): the cached null row
    // must fail loud again, not silently degrade to empty-without-signal.
    auto const r2 = root.nested_group_slices(slice.data, slice.len, kInnerNoTag, kTestCtx);
    EXPECT_TRUE(r2.alloc_failed);
    EXPECT_TRUE(r2.slices.empty());
}

// ── Mode (c): sub-table non-null, ctor build() degraded -> out_of_memory ──
TEST(NestedGroupSlicesFailLoud, ModeC_CtorDegradedOutOfMemoryReportsFailLoud) {
    constexpr std::size_t kCapModeC = 1930;
    auto frame_buf = make_raw_frame(make_present_body());
    auto fv = fixpp::wire::test::make_frame_view(frame_buf);
    ASSERT_TRUE(fv.has_value());
    auto dict = make_dict();

    std::vector<std::byte> arena_buf(kCapModeC);
    std::pmr::monotonic_buffer_resource arena{arena_buf.data(), arena_buf.size(),
                                              std::pmr::null_memory_resource()};
    OffsetTable root{*fv, &arena, &dict, &dict_group_member};
    ASSERT_TRUE(root.build_status());
    auto outer = root.group_slices(kOuterNoTag);
    ASSERT_EQ(outer.size(), 1U);
    auto const slice = outer[0];

    auto const r1 = root.nested_group_slices(slice.data, slice.len, kInnerNoTag, kTestCtx);

    auto const* sub = nested_cache_access_for_testing::resolve(root, slice.data, kInnerNoTag);
    ASSERT_NE(sub, nullptr) << "cap did not land in mode (c) on this build (shell alloc itself "
                                "failed -- mode (a)); re-derive kCapModeC";
    ASSERT_FALSE(sub->build_status()) << "cap did not land in mode (c) on this build (sub-table "
                                          "ctor build() succeeded); re-derive kCapModeC";
    ASSERT_EQ(sub->build_status().error(), error::out_of_memory)
        << "sub-table degraded for a NON-arena reason (malformed data), not mode (c)";

    EXPECT_TRUE(r1.alloc_failed);
    EXPECT_TRUE(r1.slices.empty());

    // Repeated-read: mode (c)'s cache-hit exit rides the PERSISTENT
    // build_status()==out_of_memory check (research.md §D2), not a re-throw
    // (the degraded table's group_slices_status() itself no longer throws on
    // read 2 -- it serves a cached count-0 row). Killing the OR term collapses
    // this to a silent empty on read 2 specifically (M2 below).
    auto const r2 = root.nested_group_slices(slice.data, slice.len, kInnerNoTag, kTestCtx);
    EXPECT_TRUE(r2.alloc_failed);
    EXPECT_TRUE(r2.slices.empty());
}

// ── Mode (b): sub-table non-null + build() ok, its OWN group_slices_status()
//    materialization throws bad_alloc ──────────────────────────────────────
TEST(NestedGroupSlicesFailLoud, ModeB_SubTableGroupSlicesThrowsReportsFailLoud) {
    constexpr std::size_t kCapModeB = 2700;
    auto frame_buf = make_raw_frame(make_present_body());
    auto fv = fixpp::wire::test::make_frame_view(frame_buf);
    ASSERT_TRUE(fv.has_value());
    auto dict = make_dict();

    std::vector<std::byte> arena_buf(kCapModeB);
    std::pmr::monotonic_buffer_resource arena{arena_buf.data(), arena_buf.size(),
                                              std::pmr::null_memory_resource()};
    OffsetTable root{*fv, &arena, &dict, &dict_group_member};
    ASSERT_TRUE(root.build_status());
    auto outer = root.group_slices(kOuterNoTag);
    ASSERT_EQ(outer.size(), 1U);
    auto const slice = outer[0];

    auto const r1 = root.nested_group_slices(slice.data, slice.len, kInnerNoTag, kTestCtx);

    auto const* sub = nested_cache_access_for_testing::resolve(root, slice.data, kInnerNoTag);
    ASSERT_NE(sub, nullptr) << "cap did not land in mode (b) on this build (shell alloc itself "
                                "failed -- mode (a)); re-derive kCapModeB";
    ASSERT_TRUE(sub->build_status()) << "cap did not land in mode (b) on this build (sub-table "
                                         "ctor build() degraded -- mode (c)); re-derive kCapModeB";
    auto const gs = sub->group_slices_status(kInnerNoTag);
    ASSERT_TRUE(gs.alloc_failed) << "cap did not land in mode (b) on this build (sub-table's own "
                                     "group_slices_status() succeeded); re-derive kCapModeB";

    EXPECT_TRUE(r1.alloc_failed);
    EXPECT_TRUE(r1.slices.empty());

    // Repeated-read: mode (b)'s cache-hit exit re-resolves the cached
    // NON-null sub-table and re-invokes its group_slices_status(), which
    // re-throws (nothing was cached in the sub-table's own group_index_ on
    // the first throw) -- fail-loud again with no extra bookkeeping.
    auto const r2 = root.nested_group_slices(slice.data, slice.len, kInnerNoTag, kTestCtx);
    EXPECT_TRUE(r2.alloc_failed);
    EXPECT_TRUE(r2.slices.empty());
}

// ── Controls (SC-003 / FR-007): neither a genuinely absent nor a genuinely
//    empty (count-0) group must ever raise the failure signal ─────────────
TEST(NestedGroupSlicesFailLoud, ControlAbsentSliceDataNullNeverFails) {
    auto frame_buf = make_raw_frame(make_present_body());
    auto fv = fixpp::wire::test::make_frame_view(frame_buf);
    ASSERT_TRUE(fv.has_value());
    auto dict = make_dict();
    std::pmr::monotonic_buffer_resource arena;  // ample, non-exhausting
    OffsetTable root{*fv, &arena, &dict, &dict_group_member};
    ASSERT_TRUE(root.build_status());

    auto const r = root.nested_group_slices(nullptr, 0, kInnerNoTag, kTestCtx);
    EXPECT_FALSE(r.alloc_failed);
    EXPECT_TRUE(r.slices.empty());
}

TEST(NestedGroupSlicesFailLoud, ControlGenuineCountZeroNonNullOkNeverFails) {
    std::string body = "35=D\x01" "34=1\x01";
    body += std::to_string(kOuterNoTag) + "=1\x01";
    body += std::to_string(kOuterDelim) + "=PA\x01";
    body += std::to_string(kInnerNoTag) + "=0\x01";  // genuinely empty nested group
    auto frame_buf = make_raw_frame(body);
    auto fv = fixpp::wire::test::make_frame_view(frame_buf);
    ASSERT_TRUE(fv.has_value());
    auto dict = make_dict();
    std::pmr::monotonic_buffer_resource arena;  // ample, non-exhausting
    OffsetTable root{*fv, &arena, &dict, &dict_group_member};
    ASSERT_TRUE(root.build_status());
    auto outer = root.group_slices(kOuterNoTag);
    ASSERT_EQ(outer.size(), 1U);
    auto const slice = outer[0];

    auto const r = root.nested_group_slices(slice.data, slice.len, kInnerNoTag, kTestCtx);

    auto const* sub = nested_cache_access_for_testing::resolve(root, slice.data, kInnerNoTag);
    ASSERT_NE(sub, nullptr);
    ASSERT_TRUE(sub->build_status());
    EXPECT_FALSE(sub->group_slices_status(kInnerNoTag).alloc_failed);

    EXPECT_FALSE(r.alloc_failed);
    EXPECT_TRUE(r.slices.empty());
}
