// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_066_arena_fit_test.cpp
//
// 066-dict-backed-inbound-parse T014 — arena-fit witnesses (SC-004/FR-009).
//
// FR-009: dict-backed nested reads build sub-`OffsetTable`s from the stack
// arena — a NEW cost on both `parse_and_dispatch_` arenas
// (src/session/session.cpp:293-294). This file witnesses:
//
//   1. AppMessageFitsInboundParseArena  — a representative group-bearing APP
//      message parses+reads within `kInboundParseArena=16384`, via REAL
//      Session dispatch (no heap fallback: `fixpp::detail::arena_upstream()`
//      is `null_memory_resource()` on this toolchain — pmr_arena_upstream.hpp
//      — so an overflow would surface as a parse failure, not a silent heap
//      spill; dispatch SUCCEEDING is itself the "fits, no heap fallback"
//      witness).
//   2. AdminGroupMessageFitsAdminArena  — a group-bearing ADMIN message
//      (Logon `NoMsgTypes(384)`, dictionaries/FIX44.xml:286-289 — a REAL
//      dict-registered admin group) parses+reads within the tighter
//      `kAdminParseArena=8192`. `kAdminParseArena` is exercised in production
//      ONLY by the private `fire_to_admin_` (outbound-emit toAdmin) call
//      site (session.cpp:344-354), so this probe mirrors that exact
//      construction directly (stack array + `monotonic_buffer_resource` +
//      `Parser<Index>{tv}`) rather than going through `Session`.
//   3. NearCapHeadroomProbe             — a large-but-realistic group-bearing
//      message (many `NoLegs` instances) fits `kInboundParseArena=16384`
//      with comfortable headroom, empirically sized (not a byte-exact
//      boundary search — see the test body comment).
//   4. PathologicalDeepNestingFailsClosed — a 17-level nested repeating-group
//      chain (one level beyond `kMaxGroupDepth=16`,
//      include/fixpp/wire/offset_table.hpp:259) fails CLOSED:
//      `OffsetTable::group()` returns `wire_group_too_large`
//      (src/wire/offset_table.cpp:433-434/478-482/541-543), never an
//      over-read/corrupt/partial result. Bracketed (per FR-009's "never
//      over-read" concern, a shared post-state like "empty span" would be a
//      non-discriminating witness — 16-deep succeeds with a real
//      `group_index`, 17-deep fails with the SPECIFIC `wire_group_too_large`
//      error) — proves the overflow disposition, not merely "the read
//      degraded to nothing" (which an unrelated dictionary bug could also
//      produce).
//
// Anchors: tasks.md T014; spec.md FR-009/SC-004; contracts/inbound-parse.md
// C5/C6; src/session/session.cpp:271-330 (parse_and_dispatch_, the
// construction mirrored by probes 2-4); src/wire/offset_table.cpp
// (consume_group_extent, group()).

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <string>
#include <vector>

#include <fixpp/core/error.hpp>
#include <fixpp/core/pmr_arena_upstream.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/view.hpp>

#include "support/app_message_read_scaffold.hpp"  // fixpp_test_support::make_frame
#include "support/fix44_dictionary.hpp"
#include "support/fix44_group_frame_bodies.hpp"
#include "support/group_dispatch_fixture.hpp"

using fixpp::session::test066::GroupDispatchFixture;

namespace fixpp::session::test066 {
namespace {

using fixpp::wire::access_mode;
using fixpp::wire::Framer;
using fixpp::wire::frame_view;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;
using fixpp::wire::pmr_carry_buffer;

// Mirrors src/session/session.cpp:293-294 exactly.
constexpr std::size_t kAdminParseArena = 8192;
constexpr std::size_t kInboundParseArena = 16384;

// Mirrors test_066_group_membership_red_test.cpp's slice_has_tag helper.
bool slice_has_tag(fixpp::wire::group_slice const& s, std::uint16_t tag) {
    std::string_view sv{reinterpret_cast<char const*>(s.data), s.len};
    std::string const needle = std::to_string(tag) + "=";
    if (sv.size() >= needle.size() && sv.substr(0, needle.size()) == needle) {
        return true;
    }
    std::string const soh_needle = std::string("\x01") + needle;
    return sv.find(soh_needle) != std::string_view::npos;
}

// Mirrors Session::parse_and_dispatch_'s stack-arena construction
// (session.cpp:298-322) so probes 2-4 measure the exact production shape
// without needing access to the private method / a full Session.
struct MirroredParse {
    std::array<std::byte, kInboundParseArena> pa_buf{};
    std::pmr::monotonic_buffer_resource pa_mr;
    std::array<std::byte, 512> carry_store{};
    std::pmr::monotonic_buffer_resource carry_mr;
    pmr_carry_buffer carry;
    Framer framer;

    explicit MirroredParse(std::size_t arena_bytes)
        : pa_mr(pa_buf.data(), arena_bytes, ::fixpp::detail::arena_upstream()),
          carry_mr(carry_store.data(), carry_store.size(), ::fixpp::detail::arena_upstream()),
          carry(carry_store.size(), &carry_mr) {}
};

}  // namespace

// ── 1. App-path fit: real Session dispatch, kInboundParseArena=16384 ────────
TEST(ArenaFit, AppMessageFitsInboundParseArena) {
    GroupDispatchFixture f;
    auto cfg = f.make_cfg();
    Session sess(f.engine_cfg, cfg);
    f.open_to_active(sess);

    std::size_t count = 0;
    bool leg0_has_symbol = false;

    f.app->on_from_app =
        [&](const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg) {
            auto slices = msg.offsets().group_slices(555);
            count = slices.size();
            if (count >= 1) leg0_has_symbol = slice_has_tag(slices[0], 600);
        };

    auto suffix = fixpp_test_support::execution_report_two_legs_trailing_suffix();
    auto frame = fixpp_test_support::make_execution_report_frame(suffix, /*seq=*/2, "TW", "ISLD");
    ASSERT_LT(frame.size(), kInboundParseArena)
        << "representative message must be well within the raw byte budget";

    f.feed(sess, frame);

    ASSERT_EQ(f.app->from_app_calls, 1)
        << "a representative group-bearing app message must dispatch successfully "
           "within kInboundParseArena=16384 (no heap fallback: arena_upstream() is "
           "null_memory_resource() on this toolchain, so overflow would surface as "
           "a parse failure, not a silent success)";
    EXPECT_EQ(count, 2U);
    EXPECT_TRUE(leg0_has_symbol);
}

// ── 2. Admin-path fit: mirrored construction, kAdminParseArena=8192 ─────────
// A group-bearing Logon(35=A) carrying NoMsgTypes(384) x2 (RefMsgType(372) +
// MsgDirection(385) members, dictionaries/FIX44.xml:286-289).
TEST(ArenaFit, AdminGroupMessageFitsAdminArena) {
    auto dict = fixpp::test_support::make_fix44_dictionary();
    auto tv = dict->as_table_view();

    std::string body =
        "35=A\x01"
        "34=1\x01"
        "49=TW\x01"
        "52=20240101-00:00:00.000\x01"
        "56=ISLD\x01"
        "98=0\x01"
        "108=30\x01"
        "384=2\x01"
        "372=D\x01"
        "385=S\x01"
        "372=8\x01"
        "385=R\x01";
    auto raw = fixpp_test_support::make_frame("FIX.4.4", body);

    MirroredParse mp{kAdminParseArena};
    std::array<frame_view, 1> out{};
    auto feed_r = mp.framer.feed(std::span<const std::byte>{raw}, mp.carry, std::span{out});
    ASSERT_TRUE(feed_r.has_value()) << "Framer::feed failed";
    ASSERT_FALSE(feed_r->empty());

    Parser<access_mode::Index> parser{tv};
    auto mv_r = parser.parse(out[0], &mp.pa_mr);
    ASSERT_TRUE(mv_r.has_value())
        << "a group-bearing admin (Logon NoMsgTypes) message must parse within "
           "kAdminParseArena=8192 (no heap fallback)";

    auto slices = mv_r->offsets().group_slices(384);
    ASSERT_EQ(slices.size(), 2U) << "NoMsgTypes(384)=2 must yield exactly 2 instances";
    EXPECT_TRUE(slice_has_tag(slices[0], 372));
    EXPECT_TRUE(slice_has_tag(slices[1], 372));
}

// ── 3. Near-cap / headroom probe ────────────────────────────────────────────
// A large-but-realistic group-bearing ExecutionReport (many NoLegs
// instances). Empirically sized to a comfortably large instance count (NOT a
// byte-exact boundary search — the arena holds the OffsetTable's own PMR
// structures (entries_/group_slices_/overlay_/nested_cache_), not raw frame
// bytes, so "near-cap" is field-count-driven, not byte-count-driven); this
// probe demonstrates real headroom for realistic message sizes, not the
// precise failure boundary.
TEST(ArenaFit, NearCapHeadroomProbe) {
    constexpr int kLegs = 75;
    std::string suffix;
    suffix += "37=ORDID-1\x01";
    suffix += "17=EXEC-1\x01";
    suffix += "150=0\x01";
    suffix += "39=0\x01";
    suffix += "55=AAPL\x01";
    suffix += "54=1\x01";
    suffix += "151=0\x01";
    suffix += "14=0\x01";
    suffix += "6=0\x01";
    suffix += "555=" + std::to_string(kLegs) + "\x01";
    for (int i = 0; i < kLegs; ++i) {
        suffix += "600=LEG" + std::to_string(i) + "\x01";
        suffix += "624=1\x01";
        suffix += "687=100\x01";
    }

    auto frame = fixpp_test_support::make_execution_report_frame(suffix, /*seq=*/2, "TW", "ISLD");
    ASSERT_LT(frame.size(), kInboundParseArena)
        << "the raw frame itself must stay under the arena's nominal byte budget "
           "(sanity — the arena governs parsed metadata, not raw bytes)";

    auto dict = fixpp::test_support::make_fix44_dictionary();
    auto tv = dict->as_table_view();

    MirroredParse mp{kInboundParseArena};
    std::array<frame_view, 1> out{};
    auto feed_r = mp.framer.feed(std::span<const std::byte>{frame}, mp.carry, std::span{out});
    ASSERT_TRUE(feed_r.has_value());
    ASSERT_FALSE(feed_r->empty());

    Parser<access_mode::Index> parser{tv};
    auto mv_r = parser.parse(out[0], &mp.pa_mr);
    ASSERT_TRUE(mv_r.has_value())
        << kLegs << "-leg ExecutionReport must parse within kInboundParseArena=16384 "
                    "with headroom (no heap fallback)";

    auto slices = mv_r->offsets().group_slices(555);
    EXPECT_EQ(slices.size(), static_cast<std::size_t>(kLegs));
}

// ── 4. Pathological deeply-nested message fails CLOSED ──────────────────────
// Build a repeating-group chain of `n` groups: T_i = 9000+i, delimiter
// D_i = 8000+i. Each level i<n-1 has members {D_i, T_(i+1)} (T_(i+1) is
// itself the next level's own count field, the same "member that heads a
// nested group" shape as the MassQuote NoQuoteSets->NoQuoteEntries example
// in tests/codegen/group_entry_alloc_gate_test.cpp). The LAST level (n-1)
// has members {D_(n-1), 7000} (a plain trailing tag) so a field exists right
// after its delimiter to trigger the nested-descent check one level deeper.
//
// Recursion depth: T_0's own consume_group_extent runs at depth 0; recursing
// into T_k runs at depth k. `kMaxGroupDepth=16` (offset_table.hpp:259) is
// checked as the FIRST line of consume_group_extent
// (offset_table.cpp:433-434), so building a chain of n=17 groups (T_0..T_16)
// makes the recursion into T_16 run at depth=16 -> immediate overflow.
// n=16 (T_0..T_15) never reaches depth 16 -> succeeds. This n/n-1 bracket is
// the discriminating check (a shared post-state like "empty result" would
// pass for the WRONG reason — a dictionary-registration bug would also yield
// an empty/absent read; asserting the SPECIFIC wire_group_too_large error at
// n=17 while n=16 yields a real group_index rules that out).
struct ChainFixture {
    fixpp::dict::table_view tv;
    std::vector<std::byte> frame_bytes;

    explicit ChainFixture(int n) {
        for (int i = 0; i < n; ++i) {
            auto const t_i = static_cast<std::uint16_t>(9000 + i);
            auto const d_i = static_cast<std::uint16_t>(8000 + i);
            auto const second =
                (i < n - 1) ? static_cast<std::uint16_t>(9000 + i + 1) : std::uint16_t{7000};
            tv.add_group_member(t_i, d_i);
            tv.add_group_member(t_i, second);
        }

        std::string body = "35=X\x01";
        for (int i = 0; i < n; ++i) {
            body += std::to_string(9000 + i) + "=1\x01";
            body += std::to_string(8000 + i) + "=1\x01";
        }
        body += "7000=1\x01";
        frame_bytes = fixpp_test_support::make_frame("FIX.4.4", body);
    }
};

TEST(ArenaFit, PathologicalDeepNestingFailsClosed) {
    // (a) n=16 (T_0..T_15): recursion never reaches depth 16 -> succeeds.
    {
        ChainFixture cf{16};
        MirroredParse mp{kInboundParseArena};
        std::array<frame_view, 1> out{};
        auto feed_r =
            mp.framer.feed(std::span<const std::byte>{cf.frame_bytes}, mp.carry, std::span{out});
        ASSERT_TRUE(feed_r.has_value());
        ASSERT_FALSE(feed_r->empty());

        Parser<access_mode::Index> parser{cf.tv};
        auto mv_r = parser.parse(out[0], &mp.pa_mr);
        ASSERT_TRUE(mv_r.has_value());

        auto gi = mv_r->offsets().group(9000);
        EXPECT_TRUE(gi.has_value())
            << "a 16-level chain (one level under the cap) must resolve a real "
               "group_index, not fail — the bracket's non-overflow leg";
    }

    // (b) n=17 (T_0..T_16): recursion into T_16 runs at depth=16 -> overflow.
    {
        ChainFixture cf{17};
        MirroredParse mp{kInboundParseArena};
        std::array<frame_view, 1> out{};
        auto feed_r =
            mp.framer.feed(std::span<const std::byte>{cf.frame_bytes}, mp.carry, std::span{out});
        ASSERT_TRUE(feed_r.has_value());
        ASSERT_FALSE(feed_r->empty());

        Parser<access_mode::Index> parser{cf.tv};
        auto mv_r = parser.parse(out[0], &mp.pa_mr);
        ASSERT_TRUE(mv_r.has_value()) << "the top-level parse/build itself must still "
                                         "succeed — only the group() READ fails closed";

        auto gi = mv_r->offsets().group(9000);
        ASSERT_FALSE(gi.has_value())
            << "a 17-level chain (one level OVER kMaxGroupDepth=16) must fail closed, "
               "never over-read/corrupt/silently truncate";
        EXPECT_EQ(gi.error(), fixpp::core::error::wire_group_too_large)
            << "the SPECIFIC overflow disposition must fire (not a proxy/absent result "
               "some other dictionary-registration bug could also produce)";

        // Never-over-read/corrupt: a top-level unrelated group read on the SAME
        // message must still resolve absent-but-sane, not garbage.
        auto unrelated = mv_r->offsets().group_slices(555);
        EXPECT_TRUE(unrelated.empty());
    }
}

}  // namespace fixpp::session::test066
