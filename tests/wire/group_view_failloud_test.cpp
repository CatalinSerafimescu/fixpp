// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/group_view_failloud_test.cpp — 073 T008 (US2 typed
// arena-exhaustion witness, L-065-2 / #184).
//
// Proves the TYPED nested-group descent chain (the codegen-emitted
// `group_view<G>::…` accessor, tools/codegen/fixpp-codegen/emit_messages.cpp
// ~:256-290) surfaces a nested sub-table arena-exhaustion failure via
// `group_view::alloc_failed() == true` (SC-002, FR-004) rather than a silent
// `size()==0` — as a VALUE, not a throw across the `noexcept` boundary, and
// the process does NOT terminate.
//
// Drives `fixpp::v44::MassQuote`'s generated `quote_sets()[0].quote_entries()`
// (NoQuoteSets(296) -> NoQuoteEntries(295)), the exact codepath at
// build/<preset>/_codegen/include/fixpp/v44/Messages.hpp:3544-3550, which
// binds `auto const r = …nested_group_slices(…)` and returns
// `group_view<G_295>{r.slices, child_ctx, r.alloc_failed}`.
//
// Unlike the wire-level T001 witness (which holds the `OffsetTable` directly
// and can introspect the sub-table to pin one of the three arena-exhaustion
// modes precisely — research.md §D2/D6), this typed consumer witness CANNOT
// introspect: `group_view` only exposes `alloc_failed()`/`size()`. Per
// research.md §D6 "Platform-robust mode pinning", the discipline here is a
// WIDE-MARGIN FIXTURE instead of mode-specific cap tuning: a nested group
// with 40 instances (double T001's 20-instance fixture) widens the gap
// between "the outer group(296) — a cheap 1-entry top-level read — succeeds"
// and "the nested sub-OffsetTable for 295's 40 occurrences fully builds" to
// ~2.6 KiB on this build (see the empirical sweep below), so ONE cap value
// robustly lands inside "outer succeeds, nested fails" on every CI tier —
// not just clang-debug ([[feedback_local_verify_clang_only_misses_gcc_release_ci_job]]).
//
// Empirical sweep (both `linux-clang-debug` and `linux-gcc-release`, byte
// step 40, identical transition points on both toolchains):
//   cap <  1800: top-level Parser::parse() itself fails (unrelated —
//                out of the fixture's usable window, same caveat as T006's
//                header comment re: the unrelated cursor-shell allocation).
//   cap in [1800, 2440]: top-level parses but the OUTER group(296) read
//                itself comes back empty (not the nested-only failure we
//                need — excluded).
//   cap in [2480, 5160]: OUTER_OK (quote_sets().size()==1), nested
//                quote_entries().alloc_failed()==true, size()==0.  <- our
//                band, 2680 bytes wide.
//   cap >= 5200: everything fits; quote_entries().size()==40 (full success).
// kTinyCap = 3800 sits in the middle of the [2480,5160] band (1320 bytes of
// margin on either side), not at either edge.
//
// Faithful exhaustion harness (research.md D6 / quickstart.md "Faithful
// exhaustion harness"): a tiny-capacity `std::pmr::monotonic_buffer_resource`
// over `std::pmr::null_memory_resource()` is the SAME parse arena used for
// `Parser::parse()` — not a hand-built 16 KiB message, not a post-hoc flag
// ([[feedback_fault_injection_posthoc_flag_unfaithful]]). Frame construction
// itself is arena-free (`fixpp::wire::test::make_frame_view`, mirrors T001)
// so the tiny arena is consumed only by the OffsetTable build.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/v44/Messages.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <string>
#include <vector>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::Parser;

std::vector<std::byte> make_raw_frame(std::string const& body) {
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string full = "8=FIX.4.4\x01" + nine + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

constexpr std::uint16_t kOuterNoTag = 296;  // NoQuoteSets
constexpr std::uint16_t kOuterDelim = 302;  // QuoteSetID
constexpr std::uint16_t kInnerNoTag = 295;  // NoQuoteEntries
constexpr std::uint16_t kInnerDelim = 299;  // QuoteEntryID
constexpr int kInnerInstances = 40;         // wide-margin fixture (2x T001's 20)
constexpr std::size_t kTinyCap = 3800;      // mid-band, see sweep above
constexpr std::size_t kAmpleCap = 16384;

fixpp::dict::table_view make_dict() {
    fixpp::dict::table_view dict;
    dict.add_valid("i", 35)
        .add_valid("i", kOuterNoTag)
        .add_valid("i", kOuterDelim)
        .add_valid("i", kInnerNoTag)
        .add_valid("i", kInnerDelim)
        .set_group_first(kOuterNoTag, kOuterDelim)
        .add_group_member(kOuterNoTag, kInnerNoTag)
        .add_group_member(kOuterNoTag, kInnerDelim)
        .set_group_first(kInnerNoTag, kInnerDelim);
    return dict;
}

// One outer (296/302) occurrence containing a nested group (295/299) with
// kInnerInstances instances — genuinely present, non-trivial extent.
std::string make_present_body() {
    std::string body = "35=i\x01";
    body += std::to_string(kOuterNoTag) + "=1\x01";
    body += std::to_string(kOuterDelim) + "=SET0\x01";
    body += std::to_string(kInnerNoTag) + "=" + std::to_string(kInnerInstances) + "\x01";
    for (int i = 0; i < kInnerInstances; ++i) {
        body += std::to_string(kInnerDelim) + "=E" + std::to_string(i) + "\x01";
    }
    return body;
}

std::string make_absent_body() {
    std::string body = "35=i\x01";
    body += std::to_string(kOuterNoTag) + "=1\x01";
    body += std::to_string(kOuterDelim) + "=SET0\x01";  // no 295 nested group at all
    return body;
}

std::string make_count_zero_body() {
    std::string body = "35=i\x01";
    body += std::to_string(kOuterNoTag) + "=1\x01";
    body += std::to_string(kOuterDelim) + "=SET0\x01";
    body += std::to_string(kInnerNoTag) + "=0\x01";  // genuinely empty nested group
    return body;
}

}  // namespace

// ── SC-002 (US2 T008 main assertion) ──────────────────────────────────────
//
// A present nested group whose sub-view build is forced to fail by the tiny
// arena MUST report `alloc_failed()==true` and `size()==0` — asserted
// DIRECTLY (not a size()==0 proxy) — and MUST NOT terminate the process (no
// throw across the noexcept accessor boundary, FR-004).
TEST(GroupViewFailloud, PresentNestedGroup_ArenaExhausted_AllocFailedTrue) {
    auto dict = make_dict();
    auto frame_buf = make_raw_frame(make_present_body());
    auto fv = fixpp::wire::test::make_frame_view(frame_buf);
    ASSERT_TRUE(fv.has_value());

    std::vector<std::byte> arena_buf(kTinyCap);
    std::pmr::monotonic_buffer_resource arena{arena_buf.data(), arena_buf.size(),
                                              std::pmr::null_memory_resource()};
    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value())
        << "top-level parse must succeed at kTinyCap (fixture invariant)";

    fixpp::v44::MassQuote mq{*mv_res};
    auto sets = mq.quote_sets();
    ASSERT_EQ(sets.size(), 1U)
        << "outer group(296) read must succeed at kTinyCap (fixture invariant)";

    // Not a throw / no crash: reaching this line proves the noexcept
    // accessor returned a value rather than terminating on the underlying
    // bad_alloc.
    auto set0 = sets[0];
    auto entries = set0.quote_entries();
    EXPECT_TRUE(entries.alloc_failed())
        << "a present nested group whose sub-view allocation fails must report "
           "alloc_failed()==true, not a silent empty group_view (#184/L-065-2)";
    EXPECT_EQ(entries.size(), 0U);
}

// ── Repeated-read (D2 cache-exit discriminator, quickstart.md Scenario 3) ──
//
// A repeated read of the SAME exhausted nested group must signal failure on
// BOTH reads — the second read is served from the cached (possibly null)
// nested_cache_ row and must not silently degrade to an unsignalled empty.
TEST(GroupViewFailloud, RepeatedReadAfterArenaExhaustion_AllocFailedBothTimes) {
    auto dict = make_dict();
    auto frame_buf = make_raw_frame(make_present_body());
    auto fv = fixpp::wire::test::make_frame_view(frame_buf);
    ASSERT_TRUE(fv.has_value());

    std::vector<std::byte> arena_buf(kTinyCap);
    std::pmr::monotonic_buffer_resource arena{arena_buf.data(), arena_buf.size(),
                                              std::pmr::null_memory_resource()};
    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());

    fixpp::v44::MassQuote mq{*mv_res};
    auto sets = mq.quote_sets();
    ASSERT_EQ(sets.size(), 1U);

    auto set0 = sets[0];
    auto entries1 = set0.quote_entries();
    EXPECT_TRUE(entries1.alloc_failed()) << "read #1";
    EXPECT_EQ(entries1.size(), 0U);

    auto entries2 = set0.quote_entries();
    EXPECT_TRUE(entries2.alloc_failed()) << "read #2 (cached-row exit)";
    EXPECT_EQ(entries2.size(), 0U);
}

// ── SC-003 controls: legitimate emptiness must NOT raise the failure signal ─
//
// Both run against an ample arena and MUST assert alloc_failed()==false.

TEST(GroupViewFailloud, Control_AbsentNestedGroup_AmpleArena_AllocFailedFalse) {
    auto dict = make_dict();
    auto frame_buf = make_raw_frame(make_absent_body());
    auto fv = fixpp::wire::test::make_frame_view(frame_buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena{kAmpleCap};
    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());

    fixpp::v44::MassQuote mq{*mv_res};
    auto sets = mq.quote_sets();
    ASSERT_EQ(sets.size(), 1U);

    auto set0 = sets[0];
    auto entries = set0.quote_entries();
    EXPECT_FALSE(entries.alloc_failed());
    EXPECT_EQ(entries.size(), 0U);
}

TEST(GroupViewFailloud, Control_CountZeroNestedGroup_AmpleArena_AllocFailedFalse) {
    auto dict = make_dict();
    auto frame_buf = make_raw_frame(make_count_zero_body());
    auto fv = fixpp::wire::test::make_frame_view(frame_buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena{kAmpleCap};
    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());

    fixpp::v44::MassQuote mq{*mv_res};
    auto sets = mq.quote_sets();
    ASSERT_EQ(sets.size(), 1U);

    auto set0 = sets[0];
    auto entries = set0.quote_entries();
    EXPECT_FALSE(entries.alloc_failed());
    EXPECT_EQ(entries.size(), 0U);
}
