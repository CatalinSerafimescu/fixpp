// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/nested_group_slices_failloud_test.cpp — 073 T001 (Foundational
// wire-level primitive witness, L-065-2 / #184).
//
// Proves OffsetTable::nested_group_slices()'s `nested_slices_result` reports
// `alloc_failed == true` for a genuinely present nested group whose sub-view
// build is forced to fail by a tiny arena, per research.md §D2's three
// arena-exhaustion origins:
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
// false-passing) and aborting the test. Replaced with a runtime cap sweep
// across a wide range of arena caps.
//
// gate-b/ci-fix (PR #191 round 2): round 1's sweep still used cache-row
// introspection to classify each cap's mode and GATE a per-cap
// `EXPECT_TRUE`/`EXPECT_FALSE(alloc_failed)` assertion on that
// classification — ambiguous at the exact boundary where
// `OffsetTable::nested_cache_.push_back()` itself throws bad_alloc. Replaced
// with a cap-independent `alloc_failed == slices.empty()` invariant.
//
// gate-b/ci-fix (PR #191 round 3): MSVC-verified data showed the round-2
// TINY-ARENA SWEEP itself CRASHES on MSVC debug — a fine-grained cap sweep
// necessarily drives through a narrow window where the nested sub-table's
// SHELL allocation fits but its `noexcept` ctor's MSVC-debug pmr-member
// proxies do not, so `bad_alloc` escapes the `noexcept` ctor and
// `std::terminate`s mid-sweep, before any assertion runs
// (`feedback_noexcept_ctor_pmr_member_proxy_alloc_escapes_msvc_debug` — a
// pre-existing MSVC-debug-STL characteristic; production is MSVC-release
// with a 16 KiB arena, no proxies, no terminate). A sweep will ALWAYS cross
// this window on MSVC debug, so it is dropped entirely.
//
// Round 3 instead reuses T006's (tests/capi/message_read_failloud_test.cpp)
// EXACT message shape — same dict (453/447/448 outer, 539/524/525 nested),
// same body construction, same `kNestedInstances=40` / `kTinyCap=6000` — and
// drives it through the SAME `Parser<access_mode::Index>::parse()` entry
// point T006 uses (not a raw/bare `OffsetTable` ctor: a raw-ctor path skips
// the top-level MessageView/Header allocation overhead T006's calibration
// against Parser::parse() accounts for, and empirically lands the SAME
// fixture's nested-build failure at a materially LOWER cap — verified
// locally: the round-2 bare-ctor construction fails at ~2460-5180 bytes for
// this exact shape, not T006's [4360,8000+] band). Reproducing T006's exact
// allocation path byte-for-byte lets this witness reuse T006's
// cross-tier-verified (and, per the coordinator's local MSVC probe,
// terminate-window-clear) `kTinyCap=6000` with confidence, while still
// asserting directly on the WIRE-LEVEL primitive
// (`OffsetTable::nested_group_slices` via `MessageView::offsets()`), not the
// C-ABI (T006's own surface) — this witness's whole reason to exist
// alongside T006/T008.

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

#include "support/frame_view_factory.hpp"
#include "support/wire_test_hooks.hpp"

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::group_context;
using fixpp::wire::nested_cache_access_for_testing;
using fixpp::wire::Parser;

std::vector<std::byte> make_raw_frame(std::string const& body) {
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string full = "8=FIX.4.4\x01" + nine + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

constexpr group_context kTestCtx{.msg_type = "D"};
constexpr std::uint16_t kOuterNoTag = 453;   // NoPartyIDs-shaped outer group count field
constexpr std::uint16_t kOuterDelim = 448;   // outer group first member
constexpr std::uint16_t kOuterScalar = 447;  // outer group second (scalar) member
constexpr std::uint16_t kInnerNoTag = 539;   // NoNestedPartyIDs
constexpr std::uint16_t kInnerDelim = 524;   // NestedPartyID
constexpr std::uint16_t kInnerMember = 525;  // nested group second member
// gate-b/ci-fix round 3: T006's exact fixture — kNestedInstances=40,
// kTinyCap=6000, same dict/body shape (mirrors
// tests/capi/message_read_failloud_test.cpp verbatim). Reused byte-for-byte
// so this witness's cap is backed by T006's cross-tier (clang-debug +
// gcc-release) empirical derivation AND the coordinator's local MSVC probe.
constexpr int kInnerInstances = 40;
constexpr std::size_t kFailCap = 6000;

fixpp::dict::table_view make_dict() {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 34)
        .add_valid("D", kOuterNoTag)
        .add_valid("D", kOuterDelim)
        .add_valid("D", kOuterScalar)
        .add_valid("D", kInnerNoTag)
        .add_valid("D", kInnerDelim)
        .add_valid("D", kInnerMember)
        .set_group_first(kOuterNoTag, kOuterDelim)
        .add_group_member(kOuterNoTag, kOuterScalar)
        .add_group_member(kOuterNoTag, kInnerNoTag)
        .add_group_member(kOuterNoTag, kInnerDelim)
        .add_group_member(kOuterNoTag, kInnerMember)
        .set_group_first(kInnerNoTag, kInnerDelim)
        .add_group_member(kInnerNoTag, kInnerMember);
    return dict;
}

// One outer (453/448/447) occurrence containing a nested group (539/524/525)
// with kInnerInstances instances — genuinely present, non-trivial extent.
// Byte-for-byte the same shape as T006's present_nested_group_frame().
std::string make_present_body() {
    std::string body =
        "35=D\x01"
        "453=1\x01"
        "448=PA\x01"
        "447=D\x01";
    body += std::to_string(kInnerNoTag) + "=" + std::to_string(kInnerInstances) + "\x01";
    for (int i = 0; i < kInnerInstances; ++i) {
        body += std::to_string(kInnerDelim) + "=N" + std::to_string(i) + "\x01";
        body += std::to_string(kInnerMember) + "=C\x01";
    }
    return body;
}

}  // namespace

// ── Failure case: a genuinely present nested group whose sub-view build is
//    forced to fail by a tiny (but wide-margin, non-knife-edge) arena must
//    report `alloc_failed == true` on EVERY read (D2 cache-hit exit
//    discriminator) — never a silent empty ─────────────────────────────────
TEST(NestedGroupSlicesFailLoud, PresentNestedGroup_ArenaExhausted_ReportsFailLoudBothReads) {
    auto dict = make_dict();
    auto frame_buf = make_raw_frame(make_present_body());
    auto fv = fixpp::wire::test::make_frame_view(frame_buf);
    ASSERT_TRUE(fv.has_value());

    std::vector<std::byte> arena_buf(kFailCap);
    std::pmr::monotonic_buffer_resource arena{arena_buf.data(), arena_buf.size(),
                                              std::pmr::null_memory_resource()};
    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value())
        << "top-level parse must succeed at kFailCap (fixture invariant)";
    auto const& offsets = mv_res->offsets();

    auto outer = offsets.group_slices(kOuterNoTag);
    ASSERT_EQ(outer.size(), 1U) << "outer group(453) read must succeed at kFailCap";
    auto const slice = outer[0];

    auto const r1 = offsets.nested_group_slices(slice.data, slice.len, kInnerNoTag, kTestCtx);
    EXPECT_TRUE(r1.alloc_failed) << "read #1";
    EXPECT_TRUE(r1.slices.empty()) << "read #1";

    // Repeated-read (D2 cache-hit exit discriminator): the cached row must
    // fail loud again, not silently degrade to empty-without-signal.
    auto const r2 = offsets.nested_group_slices(slice.data, slice.len, kInnerNoTag, kTestCtx);
    EXPECT_TRUE(r2.alloc_failed) << "read #2 (cache-hit exit)";
    EXPECT_TRUE(r2.slices.empty()) << "read #2 (cache-hit exit)";

    // Soft introspection tally (NOT a hard precondition -- mode (a),
    // sub == nullptr, is ALSO a valid fail-loud outcome): if the sub-table
    // did build non-null, confirm this is a genuine CAUGHT mode (b)/(c)
    // failure -- the sub-table's own ctor build() degraded to
    // out_of_memory, or its group_slices_status() materialization threw --
    // not some other, unexpected state landing on a false empty.
    auto const* sub = nested_cache_access_for_testing::resolve(offsets, slice.data, kInnerNoTag);
    if (sub != nullptr) {
        bool const ctor_oom =
            !sub->build_status() && sub->build_status().error() == fixpp::core::error::out_of_memory;
        bool const slices_throw = sub->group_slices_status(kInnerNoTag).alloc_failed;
        EXPECT_TRUE(ctor_oom || slices_throw)
            << "sub-table built non-null but neither build_status()==out_of_memory nor "
               "group_slices_status().alloc_failed -- kFailCap landed outside the intended "
               "mode (b)/(c) band; re-derive against T006's fixture";
    }
}

// ── Success control: the SAME fixture against an ample arena must return
//    ALL kInnerInstances slices, never a failure signal -- proves the
//    failure case above is not vacuously always-failing (SC-003) ──────────
TEST(NestedGroupSlicesFailLoud, PresentNestedGroup_AmpleArena_ReturnsAllInstances) {
    auto dict = make_dict();
    auto frame_buf = make_raw_frame(make_present_body());
    auto fv = fixpp::wire::test::make_frame_view(frame_buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;  // ample, non-exhausting
    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());
    auto const& offsets = mv_res->offsets();

    auto outer = offsets.group_slices(kOuterNoTag);
    ASSERT_EQ(outer.size(), 1U);
    auto const slice = outer[0];

    auto const r = offsets.nested_group_slices(slice.data, slice.len, kInnerNoTag, kTestCtx);
    EXPECT_FALSE(r.alloc_failed);
    EXPECT_EQ(r.slices.size(), static_cast<std::size_t>(kInnerInstances));
}

// ── Controls (SC-003 / FR-007): neither a genuinely absent nor a genuinely
//    empty (count-0) group must ever raise the failure signal ─────────────
TEST(NestedGroupSlicesFailLoud, ControlAbsentSliceDataNullNeverFails) {
    auto dict = make_dict();
    auto frame_buf = make_raw_frame(make_present_body());
    auto fv = fixpp::wire::test::make_frame_view(frame_buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;  // ample, non-exhausting
    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());
    auto const& offsets = mv_res->offsets();

    auto const r = offsets.nested_group_slices(nullptr, 0, kInnerNoTag, kTestCtx);
    EXPECT_FALSE(r.alloc_failed);
    EXPECT_TRUE(r.slices.empty());
}

TEST(NestedGroupSlicesFailLoud, ControlGenuineCountZeroNonNullOkNeverFails) {
    auto dict = make_dict();
    std::string body =
        "35=D\x01"
        "453=1\x01"
        "448=PA\x01"
        "447=D\x01"
        "539=0\x01";  // genuinely empty nested group
    auto frame_buf = make_raw_frame(body);
    auto fv = fixpp::wire::test::make_frame_view(frame_buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;  // ample, non-exhausting
    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());
    auto const& offsets = mv_res->offsets();

    auto outer = offsets.group_slices(kOuterNoTag);
    ASSERT_EQ(outer.size(), 1U);
    auto const slice = outer[0];

    auto const r = offsets.nested_group_slices(slice.data, slice.len, kInnerNoTag, kTestCtx);

    auto const* sub = nested_cache_access_for_testing::resolve(offsets, slice.data, kInnerNoTag);
    ASSERT_NE(sub, nullptr);
    ASSERT_TRUE(sub->build_status());
    EXPECT_FALSE(sub->group_slices_status(kInnerNoTag).alloc_failed);

    EXPECT_FALSE(r.alloc_failed);
    EXPECT_TRUE(r.slices.empty());
}
