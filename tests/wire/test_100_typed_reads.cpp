// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/test_100_typed_reads.cpp — 100-wire-group-read-integrity, tasks T016 (US1; FR-002,
// FR-003; US1.6, US1.11) and T017 (US2; US2.1, FR-005; research D6): typed group reads.
//
// T016: tests/capi/test_100_group_getters.cpp's frames read through `MessageView::group<>()`
// (top level) and through the generated FIX 4.4 nested accessor (`fixpp::v44`), each read
// twice; the count field and the fields after it read through `MessageView::get`.
//
// T017: a top-level group whose slice allocation fails, read through `MessageView::group<>()`.
// The arena is a fixed buffer whose upstream is `fixpp::detail::arena_upstream()`, as the
// session's parse arena is, sized to the parse's own footprint plus a headroom smaller than the
// group's slice array (the condition InboundFrameDispositionsQ33.GroupSlicesAtHeadroomExhaustion
// reaches through a live session). The cell branches on that upstream as the Q33 cells do: where
// it is null the read fails and `alloc_failed()` is the witness; where it forwards (MSVC debug)
// the read succeeds, and that is what the cell asserts.
//
// T040 (US2; FR-005, FR-011, FR-004; research D5, D6, D7, D11): `read_failed()` beside
// `alloc_failed()` on US2.1's rig; D5's top-level wire limit through `MessageView::group<>()`;
// D7 (i) through the generated FIX 4.4 nested accessor; and an absent nested tag on an exhausted
// arena through that accessor, on a `test100::stepped_arena`
// (tests/wire/test_100_group_read_status.cpp states the construction). D7 (ii)'s typed twin sets
// the overlay seed, so it lives in tests/wire/test_100_seed_hook_typed.cpp.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fixpp/core/pmr_arena_upstream.hpp>
#include <fixpp/v44/Messages.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "support/frame_view_factory.hpp"
#include "support/group_read_100.hpp"

namespace {

using fixpp::test100::fix44;
using fixpp::test100::frame44;
using fixpp::test100::header44;
using fixpp::v44::groups::G_453;
using fixpp::v44::groups::G_802;
using fixpp::wire::access_mode;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;

using fixpp::test100::parsed44;

std::string field(MessageView<access_mode::Index> const& mv, std::uint16_t tag) {
    auto const f = mv.get(tag);
    if (!f) {
        return "<absent>";
    }
    return std::string{f->as_string()};
}

// `mv.group<NoTag, GroupT>()` read twice: both reads report `size` and `!alloc_failed()`.
template <std::uint16_t NoTag, class GroupT>
void expect_typed(MessageView<access_mode::Index> const& mv, std::size_t size,
                  std::string const& what) {
    for (int read = 0; read < 2; ++read) {
        auto const gv = mv.template group<NoTag, GroupT>();
        EXPECT_EQ(gv.size(), size) << what << (read == 0 ? " (first read)" : " (repeat read)");
        EXPECT_FALSE(gv.alloc_failed()) << what << (read == 0 ? " (first read)" : " (repeat read)");
    }
}

std::string const kOrd = "11=ORD1\x01";
std::string const kParty =
    "448=PA\x01"
    "447=D\x01"
    "452=1\x01";

// ── T016: zero instances read (controls) ──────────────────────────────────────────

TEST(TypedReads100, US1_6_Control_ZeroCountMidBody_SizeZeroAndFieldsRead) {
    parsed44 const p{header44("D") + kOrd +
                     "453=0\x01"
                     "54=1\x01"
                     "38=100\x01"};
    ASSERT_TRUE(p.mv.has_value());
    expect_typed<453, G_453>(*p.mv, 0, "US1.6 zero count mid-body (control)");
    EXPECT_EQ(field(*p.mv, 453), "0") << "US1.6: the count field reads 0";
    EXPECT_EQ(field(*p.mv, 54), "1") << "US1.6: the field after it reads as an ordinary field";
    EXPECT_EQ(field(*p.mv, 38), "100");
}
TEST(TypedReads100, US1_6_Control_ZeroCountLastBodyField_SizeZero) {
    parsed44 const p{header44("D") + kOrd +
                     "54=1\x01"
                     "453=0\x01"};
    ASSERT_TRUE(p.mv.has_value());
    expect_typed<453, G_453>(*p.mv, 0, "US1.6 zero count last (control)");
    EXPECT_EQ(field(*p.mv, 453), "0");
}
TEST(TypedReads100, US1_6_Control_ZeroCountFollowedByMembers_SizeZeroAndMembersRead) {
    parsed44 const p{header44("D") + kOrd + "453=0\x01" + kParty + "54=1\x01"};
    ASSERT_TRUE(p.mv.has_value());
    expect_typed<453, G_453>(*p.mv, 0, "US1.6 zero count then members (control)");
    EXPECT_EQ(field(*p.mv, 453), "0");
    EXPECT_EQ(field(*p.mv, 448), "PA") << "US1.6: a member field after a zero count reads as an "
                                          "ordinary field";
    EXPECT_EQ(field(*p.mv, 54), "1");
}
TEST(TypedReads100, US1_7_Control_LyingCount_SizeZero) {
    parsed44 const p{header44("D") + kOrd +
                     "453=2\x01"
                     "54=1\x01"};
    ASSERT_TRUE(p.mv.has_value());
    expect_typed<453, G_453>(*p.mv, 0, "US1.7 lying count (control)");
    EXPECT_EQ(field(*p.mv, 54), "1");
}
TEST(TypedReads100, Edge_Control_NonDigitCountFollowedByMembers_SizeZero) {
    parsed44 const p{header44("D") + kOrd + "453=abc\x01" + kParty + "54=1\x01"};
    ASSERT_TRUE(p.mv.has_value());
    expect_typed<453, G_453>(*p.mv, 0, "Edge Cases no leading digit, members follow (control)");
}
TEST(TypedReads100, Edge_Control_NonDigitCountFollowedByNonMember_SizeZero) {
    parsed44 const p{header44("D") + kOrd +
                     "453=abc\x01"
                     "54=1\x01"};
    ASSERT_TRUE(p.mv.has_value());
    expect_typed<453, G_453>(*p.mv, 0, "Edge Cases no leading digit, non-member follows (control)");
}

// ── T016: not registered in the read's context (FR-003) ───────────────────────────

TEST(TypedReads100, US1_8_802AtRootValueN_SizeZero) {
    parsed44 const p{header44("D") + kOrd +
                     "802=2\x01"
                     "523=S\x01"
                     "803=1\x01"
                     "523=T\x01"
                     "803=2\x01"
                     "54=1\x01"};
    ASSERT_TRUE(p.mv.has_value());
    expect_typed<802, G_802>(*p.mv, 0, "US1.8 802 at the root, value n / FR-003");
}
TEST(TypedReads100, US1_9_453UndeclaredInNews_SizeZero) {
    parsed44 const p{header44("B") +
                     "148=H\x01"
                     "453=1\x01" +
                     kParty + "58=x\x01"};
    ASSERT_TRUE(p.mv.has_value());
    expect_typed<453, G_453>(*p.mv, 0, "US1.9 453 in News(B) / FR-003");
}

// ── T016: the generated nested accessor (US1.3, control) ─────────────────────────

TEST(TypedReads100, US1_3_Control_NestedZeroCountMidInstance_SizeZero) {
    parsed44 const p{header44("D") + kOrd +
                     "453=1\x01"
                     "448=PA\x01"
                     "447=D\x01"
                     "802=0\x01"
                     "452=1\x01"
                     "54=1\x01"};
    ASSERT_TRUE(p.mv.has_value());
    fixpp::v44::NewOrderSingle const nos{*p.mv};
    auto const parties = nos.party_i_ds();
    ASSERT_EQ(parties.size(), 1U) << "setup: one NoPartyIDs instance";
    G_453 const entry = parties[0];
    for (int read = 0; read < 2; ++read) {
        auto const subs = entry.party_sub_i_ds();
        EXPECT_EQ(subs.size(), 0U) << "US1.3 nested zero count (control), read " << read;
        EXPECT_FALSE(subs.alloc_failed()) << "US1.3, read " << read;
    }
}

// ── T017: US2.1's first half (FR-005, research D6) ────────────────────────────────

constexpr std::size_t kHeadroomInstances = 2000;

std::string headroom_group() {
    std::string g = "453=" + std::to_string(kHeadroomInstances) + "\x01";
    for (std::size_t i = 0; i < kHeadroomInstances; ++i) {
        g += "448=P\x01"
             "447=D\x01"
             "452=1\x01";
    }
    return g;
}

TEST(TypedReads100, US2_1_TopLevelSliceAllocationFails_AllocFailedTwice) {
    auto const buf = frame44(header44("D") + kOrd + "54=1\x01" + headroom_group());
    auto const fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    Parser<access_mode::Index> parser{fix44().tv};
    // The group's fields exceed the default entry cap; a session raises it from its inbound
    // limit, and this rig raises it the same way.
    fixpp::wire::OffsetTable::Config const cfg{.max_offset_entries = 8U * kHeadroomInstances};

    // The parse's own footprint: the least room a null-upstream arena needs for it.
    std::size_t lo = 0;
    std::size_t hi = std::size_t{1} << 22U;
    while (lo < hi) {
        std::size_t const mid = lo + ((hi - lo) / 2U);
        std::vector<std::byte> b(mid == 0 ? 1 : mid);
        std::pmr::monotonic_buffer_resource m{b.data(), mid, std::pmr::null_memory_resource()};
        if (parser.parse(*fv, &m, cfg).has_value()) {
            hi = mid;
        } else {
            lo = mid + 1U;
        }
    }
    std::size_t const slice_bytes = kHeadroomInstances * sizeof(fixpp::wire::group_slice);
    std::size_t const room = lo + (slice_bytes / 2U);
    std::vector<std::byte> b(room);
    std::pmr::monotonic_buffer_resource arena{b.data(), room, fixpp::detail::arena_upstream()};
    auto const mv = parser.parse(*fv, &arena, cfg);
    ASSERT_TRUE(mv.has_value()) << "setup: the parse fits its footprint";

    bool const null_upstream = fixpp::detail::arena_upstream() == std::pmr::null_memory_resource();
    for (int read = 0; read < 2; ++read) {
        auto const gv = mv->group<453, G_453>();
        if (null_upstream) {
            EXPECT_EQ(gv.size(), 0U) << "US2.1, read " << read;
            EXPECT_TRUE(gv.alloc_failed())
                << "US2.1 / FR-005: a top-level read whose slices do not fit reports "
                   "alloc_failed(); read "
                << read;
        } else {
            EXPECT_EQ(gv.size(), kHeadroomInstances)
                << "the read succeeds from the heap; read " << read;
            EXPECT_FALSE(gv.alloc_failed());
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════════
// T040 (US2)
// ════════════════════════════════════════════════════════════════════════════════

// ── US2.1 (FR-005, research D6): `read_failed()` beside T017's `alloc_failed()` ─────
// T017's rig: a fixed buffer over `fixpp::detail::arena_upstream()`, the parse's own footprint
// plus half the group's slice array.
TEST(TypedReads100, T040_US2_1_TopLevelSliceAllocationFails_ReadFailedTwice) {
    auto const buf = frame44(header44("D") + kOrd + "54=1\x01" + headroom_group());
    auto const fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    Parser<access_mode::Index> parser{fix44().tv};
    fixpp::wire::OffsetTable::Config const cfg{.max_offset_entries = 8U * kHeadroomInstances};
    std::size_t lo = 0;
    std::size_t hi = std::size_t{1} << 22U;
    while (lo < hi) {
        std::size_t const mid = lo + ((hi - lo) / 2U);
        std::vector<std::byte> b(mid == 0 ? 1 : mid);
        std::pmr::monotonic_buffer_resource m{b.data(), mid, std::pmr::null_memory_resource()};
        if (parser.parse(*fv, &m, cfg).has_value()) {
            hi = mid;
        } else {
            lo = mid + 1U;
        }
    }
    std::size_t const slice_bytes = kHeadroomInstances * sizeof(fixpp::wire::group_slice);
    std::size_t const room = lo + (slice_bytes / 2U);
    std::vector<std::byte> b(room);
    std::pmr::monotonic_buffer_resource arena{b.data(), room, fixpp::detail::arena_upstream()};
    auto const mv = parser.parse(*fv, &arena, cfg);
    ASSERT_TRUE(mv.has_value()) << "setup: the parse fits its footprint";

    bool const null_upstream = fixpp::detail::arena_upstream() == std::pmr::null_memory_resource();
    for (int read = 0; read < 2; ++read) {
        auto const gv = mv->group<453, G_453>();
        if (null_upstream) {
            EXPECT_EQ(gv.size(), 0U) << "US2.1, read " << read;
            EXPECT_TRUE(gv.read_failed())
                << "US2.1 / FR-005: a top-level read whose slices do not fit reports "
                   "read_failed(); read "
                << read;
            EXPECT_TRUE(gv.alloc_failed()) << "US2.1, read " << read;
        } else {
            EXPECT_EQ(gv.size(), kHeadroomInstances)
                << "the read succeeds from the heap; read " << read;
            EXPECT_FALSE(gv.read_failed());
        }
    }
}

using fixpp::test100::kPastInstanceCap;
using fixpp::test100::oversized_instance;

// ── D5 (FR-011): a top-level instance past the per-instance cap ────────────────────
TEST(TypedReads100, T040_D5_TopLevelInstancePastPerInstanceCap_ReadFailedNotAllocFailedTwice) {
    auto const buf = frame44(oversized_instance(kPastInstanceCap, ""));
    auto const fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{fix44().tv};
    auto const mv = parser.parse(
        *fv, &arena, fixpp::wire::OffsetTable::Config{.max_offset_entries = 4U * kPastInstanceCap});
    ASSERT_TRUE(mv.has_value()) << "setup: the raised entry cap admits the frame";
    for (int read = 0; read < 2; ++read) {
        auto const gv = mv->group<453, G_453>();
        EXPECT_EQ(gv.size(), 0U) << "D5 typed, read " << read;
        EXPECT_TRUE(gv.read_failed()) << "D5 typed / FR-011: a group wire limit reports "
                                         "read_failed(); read "
                                      << read;
        EXPECT_FALSE(gv.alloc_failed())
            << "D5 typed: a wire limit is not an allocation failure; read " << read;
    }
}

// ── D7 (i) (FR-011): a nested sub-table over its entry cap, typed nested accessor ────
// Raised root caps admit an instance larger than the sub-table's entry cap (the default Config);
// the instance carries NoPartySubIDs(802), read through the generated `party_sub_i_ds()`.
TEST(TypedReads100, T040_D7i_TypedNestedSubTableOverEntryCap_ReadFailedTwice) {
    auto const buf = frame44(oversized_instance(kPastInstanceCap,
                                                "802=1\x01"
                                                "523=S\x01"
                                                "803=1\x01"));
    auto const fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{fix44().tv};
    auto const mv = parser.parse(
        *fv, &arena,
        fixpp::wire::OffsetTable::Config{.max_offset_entries = 4U * kPastInstanceCap,
                                         .max_group_entries_per_instance = 4U * kPastInstanceCap});
    ASSERT_TRUE(mv.has_value()) << "setup: the raised caps admit the frame";
    auto const parties = mv->group<453, G_453>();
    ASSERT_EQ(parties.size(), 1U) << "setup: one NoPartyIDs instance";
    G_453 const entry = parties[0];
    for (int read = 0; read < 2; ++read) {
        auto const subs = entry.party_sub_i_ds();
        EXPECT_EQ(subs.size(), 0U) << "D7 (i) typed, read " << read;
        EXPECT_TRUE(subs.read_failed()) << "D7 (i) typed / FR-011, read " << read;
        EXPECT_FALSE(subs.alloc_failed()) << "D7 (i) typed: a wire limit; read " << read;
    }
}

// ── D11 (FR-004): an absent nested tag on an exhausted arena, typed nested accessor ──
// From instance 0 of a three-instance NoPartyIDs(453) with no NoPartySubIDs(802), the arena's
// room is stepped as tests/wire/test_100_group_read_status.cpp's `step_rooms` steps it; at each
// room the top-level typed read completes before the mark, then `party_sub_i_ds()` is read twice.
std::string const kThreePartiesNo802 =
    "453=3\x01"
    "448=P1\x01"
    "447=D\x01"
    "452=1\x01"
    "448=P2\x01"
    "447=D\x01"
    "452=1\x01"
    "448=P3\x01"
    "447=D\x01"
    "452=1\x01";

std::size_t typed_absent_nested_at(fixpp::test100::alloc_kind kind, std::string const& what) {
    std::size_t hits = 0;
    bool const framed = fixpp::test100::step_parse_rooms(
        header44("D") + kOrd + kThreePartiesNo802 + "54=1\x01", fix44().tv, std::nullopt, 1U << 16U,
        [&](std::size_t room, fixpp::test100::stepped_arena& arena,
            MessageView<access_mode::Index> const& mv) {
            auto const parties = mv.group<453, G_453>();
            if (parties.size() != 3U) {
                return false;
            }
            G_453 const entry = parties[0];
            arena.rec().mark();
            auto const first = entry.party_sub_i_ds();
            auto const k = fixpp::test100::classify_first_failure(arena.rec(), 0).kind;
            auto const repeat = entry.party_sub_i_ds();
            bool const complete = !arena.rec().first_failure().has_value();
            if (k == kind) {
                ++hits;
                EXPECT_FALSE(first.read_failed()) << what << " (first read, room " << room
                                                  << "): an absent tag is not a failed read";
                EXPECT_FALSE(repeat.read_failed()) << what << " (repeat read, room " << room << ")";
                EXPECT_EQ(first.size(), 0U) << what << " (room " << room << ")";
                EXPECT_EQ(repeat.size(), 0U) << what << " (room " << room << ")";
            }
            return complete;
        });
    if (!framed) {
        ADD_FAILURE() << "setup: frame";
    }
    return hits;
}
TEST(TypedReads100, T040_D11_TypedNestedAbsentModeA_NotReadFailed) {
    EXPECT_GT(typed_absent_nested_at(fixpp::test100::alloc_kind::subtable,
                                     "D11 typed nested absent, mode (a)"),
              0U)
        << "setup: no room made the sub-table shell the first to fail";
}
TEST(TypedReads100, T040_D11_TypedNestedAbsentModeC_NotReadFailed) {
    EXPECT_GT(typed_absent_nested_at(fixpp::test100::alloc_kind::subtable_build,
                                     "D11 typed nested absent, mode (c)"),
              0U)
        << "setup: no room made the sub-table build the first to fail";
}

}  // namespace
