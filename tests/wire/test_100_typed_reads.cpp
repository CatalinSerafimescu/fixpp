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

struct parsed44 {
    std::vector<std::byte> buf;
    std::optional<fixpp::wire::frame_view> fv;
    std::pmr::monotonic_buffer_resource arena;
    std::optional<MessageView<access_mode::Index>> mv;
    explicit parsed44(std::string const& body) : buf(frame44(body)) {
        auto f = fixpp::wire::test::make_frame_view(buf);
        if (!f) {
            return;
        }
        fv.emplace(*f);
        Parser<access_mode::Index> parser{fix44().tv};
        auto r = parser.parse(*fv, &arena);
        if (r) {
            mv.emplace(std::move(*r));
        }
    }
};

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

}  // namespace
