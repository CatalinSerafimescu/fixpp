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
// cap tuned to one mode here is NOT portable.
//
// gate-b/ci-fix (PR #191 round 1): the three original fixed caps (1500 /
// 1930 / 2700 bytes) were derived empirically on ONE build (clang-debug,
// sizeof(OffsetTable)==280) and were NOT portable — on MSVC debug/release
// `sizeof(OffsetTable)` differs, so a cap tuned to land in (say) mode (c) on
// clang-debug instead landed in mode (a) on MSVC, tripping an introspection
// ASSERT_NE/ASSERT_EQ guard (working as designed — failing loud rather than
// false-passing) and aborting the test. Replaced with a RUNTIME CAP SWEEP
// (below): drive the same fixture across a wide range of arena caps.
//
// gate-b/ci-fix (PR #191 round 2): round 1's sweep still used cache-row
// INTROSPECTION (`nested_cache_access_for_testing::resolve` +
// `build_status()` + `group_slices_status()`) to classify each cap's mode
// and GATE a per-cap `EXPECT_TRUE`/`EXPECT_FALSE(alloc_failed)` assertion on
// that classification. That introspection is ambiguous at the exact
// boundary where `OffsetTable::nested_cache_.push_back()` itself throws
// bad_alloc (the code degrades gracefully — "still serve this call from the
// built table" — but no cache ROW is recorded, so `resolve()` returns
// nullptr both for genuine mode (a) AND for "a non-null, possibly-healthy
// table whose cache row never landed"), which is exactly the class of
// platform-timing fragility this fix targets. Round 2 replaces the
// classification-gated per-cap assertion with a CAP- AND
// PLATFORM-INDEPENDENT invariant intrinsic to this fixture: the nested
// group is genuinely present with `kInnerInstances` (>=1) instances, so a
// materialized read yields a non-empty span and an EMPTY read can only mean
// arena-exhaustion failure —
// `alloc_failed == slices.empty()` — asserted on BOTH reads (r1 AND r2)
// independently at every above-floor cap, with NO cross-read equality
// requirement (a cross-read `r1.alloc_failed == r2.alloc_failed` check is
// itself fragile over the monotonic, non-freeing arena: if read 1's cache
// insert fails, read 2 rebuilds against a MORE-consumed arena and can
// legitimately diverge from read 1 without either read being wrong).
// Introspection is retained ONLY to tally saw_A/saw_B/saw_C/saw_ok
// (non-vacuity of the sweep across all three origins plus a healthy cap) —
// it never gates a per-cap pass/fail assertion.

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
constexpr std::uint16_t kOuterNoTag = 453;  // outer group count field
constexpr std::uint16_t kOuterDelim = 448;  // outer group first member
constexpr std::uint16_t kInnerNoTag = 802;  // nested group count field
constexpr std::uint16_t kInnerDelim = 523;  // nested group first (only) member
constexpr int kInnerInstances = 20;         // sized so a fully-built sub-table's
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
    std::string body =
        "35=D\x01"
        "34=1\x01";
    body += std::to_string(kOuterNoTag) + "=1\x01";
    body += std::to_string(kOuterDelim) + "=PA\x01";
    body += std::to_string(kInnerNoTag) + "=" + std::to_string(kInnerInstances) + "\x01";
    for (int i = 0; i < kInnerInstances; ++i) {
        body += std::to_string(kInnerDelim) + "=Q\x01";
    }
    return body;
}

}  // namespace

// ── Sweep: reproduces all three fail-loud origins on ANY sizeof(OffsetTable)
//    via a cap- and platform-independent `alloc_failed == slices.empty()`
//    invariant (round 2); introspection tallies non-vacuity only ──────────
//
// Range/step: 128..8192 step 32 (252 caps). Lower bound (128) is below any
// plausible root-table build floor so the sweep's `continue`-below-floor
// branch is exercised at the low end; upper bound (8192) is >=4x the widest
// clang-debug per-mode band (2700B) observed for this fixture, giving ample
// headroom for a toolchain whose `sizeof(OffsetTable)`/vector-growth
// behaviour (e.g. MSVC debug iterators) is materially larger. Step 32 is
// coarse enough to keep the sweep fast (a few hundred OffsetTable
// constructions) while staying well under the >=150-byte stable per-mode
// bands the original fixed-cap derivation observed.
TEST(NestedGroupSlicesFailLoud, ArenaCapSweepAllModesReportFailLoud) {
    constexpr std::size_t kCapLo = 128;
    constexpr std::size_t kCapHi = 8192;
    constexpr std::size_t kCapStep = 32;

    auto frame_buf = make_raw_frame(make_present_body());
    auto fv = fixpp::wire::test::make_frame_view(frame_buf);
    ASSERT_TRUE(fv.has_value());
    auto dict = make_dict();

    bool saw_A = false;
    bool saw_B = false;
    bool saw_C = false;
    bool saw_ok = false;

    for (std::size_t cap = kCapLo; cap <= kCapHi; cap += kCapStep) {
        std::vector<std::byte> arena_buf(cap);
        std::pmr::monotonic_buffer_resource arena{arena_buf.data(), arena_buf.size(),
                                                  std::pmr::null_memory_resource()};
        OffsetTable root{*fv, &arena, &dict, &dict_group_member};
        if (!root.build_status()) {
            continue;  // below the floor for the root table itself -- not under test
        }
        auto outer = root.group_slices(kOuterNoTag);
        if (outer.size() != 1U) {
            continue;  // root built but the outer group itself degraded -- not under test
        }
        auto const slice = outer[0];

        auto const r1 = root.nested_group_slices(slice.data, slice.len, kInnerNoTag, kTestCtx);
        auto const r2 = root.nested_group_slices(slice.data, slice.len, kInnerNoTag, kTestCtx);

        // Core, cap- and platform-independent invariant: the fixture's
        // nested group is GENUINELY present with kInnerInstances (>=1)
        // instances, so a materialized read yields a non-empty span and an
        // EMPTY read can only mean arena-exhaustion failure. This holds for
        // EVERY above-floor cap on ANY platform/sizeof(OffsetTable) --
        // unlike a mode classified by cache-row introspection (which can be
        // ambiguous at the exact boundary where the nested_cache_ insert
        // itself throws -- gate-b/ci-fix round 2 residual). Asserted on
        // BOTH reads independently: read 2 (typically served from the
        // cache-hit exit once a row exists) must satisfy the SAME
        // invariant, which is what actually pins that exit -- not a
        // cross-read equality that can legitimately diverge over the
        // monotonic (non-freeing) arena if read 1's cache insert failed and
        // read 2 rebuilds against a more-consumed arena.
        EXPECT_EQ(r1.alloc_failed, r1.slices.empty()) << "cap=" << cap << " read=1";
        EXPECT_EQ(r2.alloc_failed, r2.slices.empty()) << "cap=" << cap << " read=2";

        // Introspection ONLY for the saw_A/B/C/ok non-vacuity tally below --
        // never gates a per-cap assertion (that ambiguity is exactly the
        // residual this round replaces).
        auto const* sub = nested_cache_access_for_testing::resolve(root, slice.data, kInnerNoTag);
        if (sub == nullptr) {
            saw_A = true;
        } else if (!sub->build_status()) {
            if (sub->build_status().error() == error::out_of_memory) {
                saw_C = true;
            }
        } else if (sub->group_slices_status(kInnerNoTag).alloc_failed) {
            saw_B = true;
        } else if (!r1.slices.empty()) {
            saw_ok = true;
        }
    }

    EXPECT_TRUE(saw_A) << "sweep never starved the shell allocation (mode a) -- "
                          "widen kCapLo/kCapHi or narrow kCapStep";
    EXPECT_TRUE(saw_B || saw_C) << "sweep never exercised a non-null sub-table exhaustion "
                                   "(mode b or c) -- widen kCapLo/kCapHi or narrow kCapStep";
    EXPECT_TRUE(saw_ok) << "sweep never reached a healthy (non-failing) cap -- "
                           "widen kCapHi";
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
    std::string body =
        "35=D\x01"
        "34=1\x01";
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
