// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/nested_group_slices_cache_test.cpp — 062 coverage witnesses
// (/speckit-verify follow-up) for OffsetTable::nested_group_slices()
// (src/wire/offset_table.cpp:557-599) branches not hit by
// group_slice_trailing_soh_test.cpp (T008) or fuzz_wire_nested_slice.cpp:
//
//  - NullSliceDataReturnsEmptySpan: the `slice_data == nullptr` guard
//    (:561-563).
//  - DifferentSliceContinuesThenSameSliceReusesSubTable: the flat cache
//    loop's `row.slice_data != slice_data` -> continue (:576-577), and the
//    same-slice/different-no_tag sub-table REUSE branch (:583-586) — one
//    outer occurrence containing TWO DISTINCT nested groups (single-entry
//    each, per the 062/063 scoping note — a multi-entry nested group is
//    blocked by a separate defect and out of scope here).
//  - BuildNestedSubviewAllocFailureDegradesToEmpty /
//    CacheInsertAllocFailureServesWithoutCaching: the two defensive
//    bad_alloc degrade paths (:548-550 build_nested_subview itself, and
//    :595-598 the cache-row push_back), driven via a call-counted
//    `failing_pmr_resource` (Nth-allocate-call injection) rather than a
//    tight arena — nested_group_slices()'s `resource()` is fixed to the
//    ROOT table's own arena, so a byte-budget arena cannot isolate a single
//    allocation site without also perturbing the root table's own build.
//    CacheInsertAllocFailureServesWithoutCaching is skipped on the MSVC debug
//    STL (_ITERATOR_DEBUG_LEVEL != 0) — see the GTEST_SKIP rationale on that
//    test: pmr-vector member-init proxy allocs escape the noexcept ctor there.
//
// Uses the same PUBLIC-API-only construction style as
// group_slice_trailing_soh_test.cpp (T008), driving OffsetTable directly
// (dict-aware ctor) rather than through Parser/MessageView.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/parser.hpp>  // fixpp::wire::get
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

#include "support/failing_pmr_resource.hpp"
#include "support/frame_view_factory.hpp"
#include "support/mock_dict_table.hpp"

namespace {

using fixpp::wire::OffsetTable;

std::vector<std::byte> make_raw_frame(std::string const& body) {
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string full = "8=FIX.4.4\x01" + nine + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// Same shape as group_slice_trailing_soh_test.cpp's dict_group_member — the
// test's own copy of the group_member_fn_t Parser would otherwise capture.
// 063 T003: widened with an ignored `group_context const&` param (Phase 2
// seam — context carried-but-unused).
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

// Trivial group-member predicate for the null-slice test, which needs no
// real dictionary membership semantics (nested_group_slices returns before
// touching the dict at all on the null-slice path).
bool always_group_member(void const*, fixpp::wire::group_context const&, std::uint16_t,
                         std::uint16_t) noexcept {
    return true;
}

// 063 T008: the new context arg — carried but unused in Phase 2.
fixpp::wire::group_context const kTestCtx{.msg_type = "D"};

}  // namespace

// ─────────────────────────────────────────────────────────────────
// 1) Null-slice guard (:561-563)
// ─────────────────────────────────────────────────────────────────

TEST(NestedGroupSlicesCache, NullSliceDataReturnsEmptySpan) {
    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable root{*fv, &arena};  // dict-free ctor: the null guard never
                                     // reaches dict-aware code.
    int dict_token = 0;
    auto slices = root.nested_group_slices(nullptr, 0, /*nested_no_tag=*/802, &dict_token,
                                           &always_group_member, fv->token(), kTestCtx).slices;
    EXPECT_TRUE(slices.empty());
}

// ─────────────────────────────────────────────────────────────────
// 2) + 3) Flat-cache-loop `continue` (different slice) and sub-table REUSE
// (same slice, different no_tag) — offset_table.cpp:576-577 / :583-586.
// ─────────────────────────────────────────────────────────────────

TEST(NestedGroupSlicesCache, DifferentSliceContinuesThenSameSliceReusesSubTable) {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 34)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 802)
        .add_valid("D", 523)
        .add_valid("D", 900)
        .add_valid("D", 901)
        .set_group_first(453, 448)
        .add_group_member(453, 802)
        .add_group_member(453, 523)
        .add_group_member(453, 900)
        .add_group_member(453, 901)
        .set_group_first(802, 523)
        .set_group_first(900, 901);

    // Outer group 453 (delimiter 448), TWO occurrences: the first contains
    // TWO distinct single-entry nested groups (802/523 and 900/901); the
    // second contains only 802/523. Neither nested group repeats an entry
    // within an occurrence (single-entry-per-occurrence, per the 062/063
    // scoping note — a multi-entry nested group is a separate, deferred
    // defect).
    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "453=2\x01"
        "448=PA\x01"
        "802=1\x01"
        "523=Q\x01"
        "900=1\x01"
        "901=R\x01"
        "448=PB\x01"
        "802=1\x01"
        "523=S\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable root{*fv, &arena, &dict, &dict_group_member};

    auto outer = root.group_slices(453);
    ASSERT_EQ(outer.size(), 2U);
    auto const sliceA = outer[0];
    auto const sliceB = outer[1];
    ASSERT_NE(sliceA.data, sliceB.data);

    // (sliceA, 802): first request — builds + caches a sub-table over
    // sliceA. Cache is empty, so the loop body never executes at all yet.
    auto a802 = root.nested_group_slices(sliceA.data, sliceA.len, /*nested_no_tag=*/802, &dict,
                                         &dict_group_member, fv->token(), kTestCtx).slices;
    ASSERT_EQ(a802.size(), 1U);
    auto a523 = fixpp::wire::get({a802[0].data, a802[0].len}, /*tag=*/523, fv->token());
    ASSERT_TRUE(a523.has_value());
    EXPECT_EQ(a523->as_string(), "Q");

    // (sliceB, 802): the ONE existing cache row is keyed on sliceA, a
    // DIFFERENT slice_data pointer than sliceB — the loop's
    // `row.slice_data != slice_data` guard fires `continue`
    // (offset_table.cpp:576-577) before falling through to build a fresh
    // sub-table for sliceB.
    auto b802 = root.nested_group_slices(sliceB.data, sliceB.len, /*nested_no_tag=*/802, &dict,
                                         &dict_group_member, fv->token(), kTestCtx).slices;
    ASSERT_EQ(b802.size(), 1U);
    auto b523 = fixpp::wire::get({b802[0].data, b802[0].len}, /*tag=*/523, fv->token());
    ASSERT_TRUE(b523.has_value());
    EXPECT_EQ(b523->as_string(), "S");

    // (sliceA, 900): SAME slice_data as the first row, a DIFFERENT
    // nested_no_tag. The loop encounters the sliceA row first (same slice,
    // no_tag mismatch) and takes the "remember this slice's sub-table"
    // branch (offset_table.cpp:583-586), then the sliceB row (different
    // slice -> continue) — reusing sliceA's already-built sub-table
    // WITHOUT rebuilding, and resolving 900/901 correctly from it (the one
    // sub-OffsetTable over sliceA indexes every nested group in that
    // slice).
    auto a900 = root.nested_group_slices(sliceA.data, sliceA.len, /*nested_no_tag=*/900, &dict,
                                         &dict_group_member, fv->token(), kTestCtx).slices;
    ASSERT_EQ(a900.size(), 1U);
    auto a901 = fixpp::wire::get({a900[0].data, a900[0].len}, /*tag=*/901, fv->token());
    ASSERT_TRUE(a901.has_value());
    EXPECT_EQ(a901->as_string(), "R");
}

// ─────────────────────────────────────────────────────────────────
// 4) + 5) Defensive bad_alloc degrade paths — build_nested_subview's own
// object allocation (:548-550) and the cache-row push_back (:595-598).
//
// nested_group_slices()'s allocator is fixed to `resource()` (the ROOT
// table's own arena), so these are driven via a call-counted
// `failing_pmr_resource` (Nth-allocate-call injection, 1-indexed) rather
// than a byte-budget arena: a byte budget can't isolate a single downstream
// allocation site without also risking perturbing the root table's own
// (unrelated) build.
// ─────────────────────────────────────────────────────────────────

namespace {

// Shared single-nested-group fixture for the OOM tests below.
fixpp::dict::table_view make_oom_dict() {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 34)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 802)
        .add_valid("D", 523)
        .set_group_first(453, 448)
        .add_group_member(453, 802)
        .add_group_member(453, 523)
        .set_group_first(802, 523);
    return dict;
}

std::vector<std::byte> make_oom_frame() {
    return make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "453=1\x01"
        "448=PA\x01"
        "802=1\x01"
        "523=Q\x01");
}

}  // namespace

TEST(NestedGroupSlicesCache, BuildNestedSubviewAllocFailureDegradesToEmpty) {
    auto dict = make_oom_dict();
    auto buf = make_oom_frame();
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    // Baseline: the exact number of PMR allocate() calls needed to build the
    // root OffsetTable and obtain the outer occurrence slice, with no
    // injected failure (fail_on_call_n == 0 means "never fail").
    std::size_t baseline_calls = 0;
    {
        // Back the injector with a monotonic arena (bulk-frees on destruction,
        // mirroring the production per-message arena) — NOT new_delete: a
        // successfully-built nested sub-OffsetTable is placement-new'd and held
        // as a raw pointer in nested_cache_ (never destructed by design), so a
        // matched-free upstream would leak it under ASan; the arena reclaims it.
        std::pmr::monotonic_buffer_resource backing;
        fixpp::test_support::failing_pmr_resource mr{&backing, 0};
        OffsetTable root{*fv, &mr, &dict, &dict_group_member};
        auto outer = root.group_slices(453);
        ASSERT_EQ(outer.size(), 1U);
        baseline_calls = mr.allocate_calls();
        ASSERT_GT(baseline_calls, 0U);
    }

    // build_nested_subview's own top-level object allocation
    // (offset_table.cpp:541, `mr->allocate(sizeof(OffsetTable), ...)`) is
    // deterministically the FIRST allocate() call inside the nested
    // sequence (nested_group_slices() itself allocates nothing before
    // calling it) — failing exactly there exercises the :548-550 catch and
    // must degrade to an empty span, never crash/UB.
    std::pmr::monotonic_buffer_resource backing;  // bulk-frees (see baseline note above)
    fixpp::test_support::failing_pmr_resource mr{&backing, baseline_calls + 1};
    OffsetTable root{*fv, &mr, &dict, &dict_group_member};
    auto outer = root.group_slices(453);
    ASSERT_EQ(outer.size(), 1U);
    auto inner = root.nested_group_slices(outer[0].data, outer[0].len, /*nested_no_tag=*/802,
                                          &dict, &dict_group_member, fv->token(), kTestCtx).slices;
    EXPECT_TRUE(inner.empty()) << "build_nested_subview's object allocation failing must "
                                  "degrade to an empty span (offset_table.cpp:548-550)";
}

TEST(NestedGroupSlicesCache, CacheInsertAllocFailureServesWithoutCaching) {
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL != 0
    // MSVC debug STL only (_ITERATOR_DEBUG_LEVEL != 0; Debug + asan-on-Debug
    // lanes). Under iterator debugging every std::pmr::vector allocates a
    // `_Container_proxy` through its resource AT CONSTRUCTION — i.e. in the
    // sub-OffsetTable's member-init list, which runs BEFORE the ctor body and
    // OUTSIDE build()'s internal bad_alloc try/catch. Because the OffsetTable
    // ctors are noexcept (offset_table.hpp:65-77), a failing_pmr_resource
    // injection that lands on one of those proxy allocations escapes the
    // noexcept boundary and std::terminates before build_nested_subview's
    // catch can fire. This k-sweep inevitably hits such an index, so the test
    // cannot reach its target (cache-row push_back) scenario here. libstdc++
    // (macro undefined) and MSVC RELEASE (_ITERATOR_DEBUG_LEVEL == 0) allocate
    // nothing in a vector ctor, so the site does not exist and this test runs
    // there — the platform-independent push-back-catch contract stays covered.
    // The escape is a debug-STL-only, genuine-OOM-only property universal to
    // every pmr-holding noexcept type; the release path is unaffected.
    GTEST_SKIP() << "noexcept-ctor member-init proxy allocation escapes under "
                    "MSVC debug STL (_ITERATOR_DEBUG_LEVEL != 0); covered on "
                    "libstdc++ and MSVC release";
#endif
    auto dict = make_oom_dict();
    auto buf = make_oom_frame();
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::size_t baseline_calls = 0;
    {
        std::pmr::monotonic_buffer_resource backing;  // bulk-frees (see note above)
        fixpp::test_support::failing_pmr_resource mr{&backing, 0};
        OffsetTable root{*fv, &mr, &dict, &dict_group_member};
        auto outer = root.group_slices(453);
        ASSERT_EQ(outer.size(), 1U);
        baseline_calls = mr.allocate_calls();
    }

    // The exact allocate() index at which `nested_cache_.push_back(...)`
    // (offset_table.cpp:593-594) itself runs is not analytically obvious
    // (it depends on how many PMR allocations the sub-table's OWN internal
    // build needs, which is an implementation detail this test must not
    // assume). Sweep candidate failure points and behaviorally identify the
    // boundary: the smallest offset at which the call still returns VALID,
    // CORRECT nested-group content despite the injected failure is exactly
    // the point where build_nested_subview has already fully succeeded and
    // the very next allocation (the cache-row push_back) is the one that
    // fails — any earlier offset lands inside build_nested_subview's own
    // (self-degrading) work and yields empty/wrong content instead.
    bool found = false;
    for (std::size_t k = 1; k <= 64 && !found; ++k) {
        std::pmr::monotonic_buffer_resource backing;  // bulk-frees (see note above)
        fixpp::test_support::failing_pmr_resource mr{&backing, baseline_calls + k};
        OffsetTable root{*fv, &mr, &dict, &dict_group_member};
        auto outer = root.group_slices(453);
        ASSERT_EQ(outer.size(), 1U);

        auto inner1 = root.nested_group_slices(outer[0].data, outer[0].len,
                                               /*nested_no_tag=*/802, &dict, &dict_group_member,
                                               fv->token(), kTestCtx).slices;
        if (inner1.size() != 1U) {
            continue;  // failure landed inside build_nested_subview itself
        }
        auto delim1 = fixpp::wire::get({inner1[0].data, inner1[0].len}, /*tag=*/523, fv->token());
        if (!delim1.has_value() || delim1->as_string() != "Q") {
            continue;  // sub-table built but internally degraded/garbage
        }

        // Valid content on the FIRST call despite the injected failure. The
        // resource's one-shot failure is now spent (every later call
        // succeeds), so if the cache row failed to insert, a second
        // same-key lookup MUST rebuild (strictly more allocate() calls)
        // rather than being served for free from the cache.
        auto const calls_after_first = mr.allocate_calls();
        auto inner2 = root.nested_group_slices(outer[0].data, outer[0].len,
                                               /*nested_no_tag=*/802, &dict, &dict_group_member,
                                               fv->token(), kTestCtx).slices;
        ASSERT_EQ(inner2.size(), 1U);
        auto const calls_after_second = mr.allocate_calls();

        if (calls_after_second > calls_after_first) {
            found = true;
            EXPECT_GT(calls_after_second, calls_after_first)
                << "second same-key lookup rebuilt instead of being served from cache -- proves "
                   "the cache-insert push_back failed and was caught (offset_table.cpp:595-598)";
        }
        // Otherwise this k produced a fully successful call (push_back also
        // succeeded, second call is a pure cache hit) — not the boundary;
        // keep scanning.
    }
    ASSERT_TRUE(found) << "no injected allocation offset within the search bound produced a "
                          "serve-without-caching result (offset_table.cpp:595-598 unreached)";
}
