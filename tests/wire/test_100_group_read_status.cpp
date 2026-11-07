// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/test_100_group_read_status.cpp — 100-wire-group-read-integrity, tasks T015 (US2;
// FR-004, FR-007; research D10, D11): the wire layer's group read results,
// `OffsetTable::group_slices_status` and `OffsetTable::nested_group_slices`, on an exhausted
// arena, read through the results' `status` field: `status == group_read_status::alloc_failed`
// is what 073's `alloc_failed` field reported.
//
// Construction (as tests/capi/test_100_group_getters.cpp's T012 cells): a
// `test100::stepped_arena` (a fixed buffer as a monotonic_buffer_resource over
// null_memory_resource(), behind a recording resource) is the parse arena; its room is stepped
// one byte at a time until the read under test completes with no failed request. At each room
// where the parse succeeds, the cell marks the arena after its setup reads, makes the read under
// test twice, and names the first request of the read that failed (`classify_first_failure`).
// Each cell asserts at every room where its targeted allocation is the first to fail and
// requires that at least one room did so, except where a cell says a lane may skip.
//
// The frames are FIX 4.4 messages parsed with the shipped FIX44.xml's `as_table_view()`.
//
// T027 (US1; FR-001 to FR-003; research D2, D3, D9 "Repeat reads") adds the `is_group`
// assertions on the wire-layer twins of tests/capi/test_100_group_getters.cpp's T010 cells, on
// an ample arena, each read twice, and the spec Clarification's dict-aware bundle without the
// registration callback.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fixpp/wire/group_view.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <optional>
#include <string>
#include <vector>

#include "support/context_group_delim_fn.hpp"
#include "support/context_group_member_fn.hpp"
#include "support/dict_hooks_test_access.hpp"
#include "support/frame_view_factory.hpp"
#include "support/group_read_100.hpp"

namespace {

using fixpp::test100::alloc_kind;
using fixpp::test100::alloc_kind_name;
using fixpp::test100::classify_first_failure;
using fixpp::test100::fix44;
using fixpp::test100::frame44;
using fixpp::test100::header44;
using fixpp::test100::stepped_arena;
using fixpp::wire::access_mode;
using fixpp::wire::MessageView;
using fixpp::wire::OffsetTable;
using fixpp::wire::Parser;

constexpr std::size_t kMaxRoom = 1U << 16U;

// One read's outcome at the wire layer.
struct wire_read {
    bool alloc_failed = false;
    std::size_t slices = 0;
};

struct step_obs {
    std::size_t room = 0;
    alloc_kind kind = alloc_kind::none;
    wire_read first;
    wire_read repeat;
};

using view_t = MessageView<access_mode::Index>;

// Steps the arena for `body`. `setup` runs before the mark and returns false when the room is
// too small for it; it may record an instance slice for `read`.
template <class Setup, class Read>
std::vector<step_obs> step_rooms(std::string const& body, OffsetTable::Config cfg,
                                 std::size_t n_slices, Setup setup, Read read) {
    std::vector<step_obs> out;
    auto const buf = frame44(body);
    auto const fv = fixpp::wire::test::make_frame_view(buf);
    if (!fv) {
        ADD_FAILURE() << "setup: frame";
        return out;
    }
    Parser<access_mode::Index> parser{fix44().tv};
    for (std::size_t room = 0; room <= kMaxRoom; ++room) {
        stepped_arena arena{room};
        auto mv = parser.parse(*fv, arena.mr(), cfg);
        if (!mv) {
            continue;
        }
        std::optional<fixpp::wire::group_slice> outer;
        if (!setup(*mv, outer)) {
            continue;
        }
        arena.rec().mark();
        step_obs o;
        o.room = room;
        o.first = read(*mv, outer);
        o.kind = classify_first_failure(arena.rec(), n_slices).kind;
        o.repeat = read(*mv, outer);
        bool const complete = !arena.rec().first_failure().has_value();
        out.push_back(o);
        if (complete) {
            break;
        }
    }
    return out;
}

bool no_setup(view_t const& /*mv*/, std::optional<fixpp::wire::group_slice>& /*outer*/) {
    return true;
}
// Instance `i` of the top-level `outer_tag`, read before the mark.
auto outer_setup(std::uint16_t outer_tag, std::size_t i) {
    return [outer_tag, i](view_t const& mv, std::optional<fixpp::wire::group_slice>& outer) {
        auto const r = mv.offsets().group_slices_status(outer_tag);
        if (r.status == fixpp::wire::group_read_status::alloc_failed || r.slices.size() <= i) {
            return false;
        }
        outer = r.slices[i];
        return true;
    };
}
auto flat_read(std::uint16_t tag) {
    return [tag](view_t const& mv, std::optional<fixpp::wire::group_slice> const& /*outer*/) {
        auto const r = mv.offsets().group_slices_status(tag);
        return wire_read{r.status == fixpp::wire::group_read_status::alloc_failed, r.slices.size()};
    };
}
auto nested_read(std::uint16_t outer_tag, std::uint16_t tag) {
    return [outer_tag, tag](view_t const& mv,
                            std::optional<fixpp::wire::group_slice> const& outer) {
        auto const ctx = mv.offsets().group_context_for(outer_tag);
        auto const r = mv.offsets().nested_group_slices(outer->data, outer->len, tag, ctx);
        return wire_read{r.status == fixpp::wire::group_read_status::alloc_failed, r.slices.size()};
    };
}

// At every room whose first failure is `kind`, both reads report `alloc_failed == want_failed`
// (and no slices when not failed). Returns how many rooms that was.
std::size_t expect_at_kind(std::vector<step_obs> const& obs, alloc_kind kind, bool want_failed,
                           std::string const& what) {
    std::size_t hits = 0;
    for (auto const& o : obs) {
        if (o.kind != kind) {
            continue;
        }
        ++hits;
        EXPECT_EQ(o.first.alloc_failed, want_failed)
            << what << " (first read, room " << o.room << ", " << alloc_kind_name(kind)
            << " failed first): alloc_failed should be " << want_failed;
        EXPECT_EQ(o.repeat.alloc_failed, want_failed)
            << what << " (repeat read, room " << o.room << "): alloc_failed should be "
            << want_failed;
        if (!want_failed) {
            EXPECT_EQ(o.first.slices, 0U) << what << ": no slices";
        }
        if (o.first.alloc_failed != want_failed || o.repeat.alloc_failed != want_failed) {
            break;  // one room's report is enough to read the cell
        }
    }
    return hits;
}

std::string const kOrd = "11=ORD1\x01";
std::string const kParty =
    "448=PA\x01"
    "447=D\x01"
    "452=1\x01";
std::string const kThreeParties =
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
std::string order3() { return header44("D") + kOrd + kThreeParties + "54=1\x01"; }

// ── D10 (FR-004): a present scalar on an exhausted arena is not a failed read ─────
TEST(GroupReadStatus100, D10_FlatPresentScalarRowPushFails_NotFailed) {
    auto const obs = step_rooms(order3(), {}, 3, no_setup, flat_read(54));
    EXPECT_GT(expect_at_kind(obs, alloc_kind::group_row, false, "D10 flat present scalar"), 0U)
        << "setup: no room made the group_index_ row the first to fail";
}

// ── D11 (FR-004): nested reads of an absent tag and of a present scalar ────────────
TEST(GroupReadStatus100, D11_NestedAbsentModeA_NotFailed) {
    auto const obs = step_rooms(order3(), {}, 0, outer_setup(453, 0), nested_read(453, 802));
    EXPECT_GT(expect_at_kind(obs, alloc_kind::subtable, false, "D11 nested absent, mode (a)"), 0U)
        << "setup: no room made the sub-table shell the first to fail";
}
TEST(GroupReadStatus100, D11_NestedAbsentModeC_NotFailed) {
    auto const obs = step_rooms(order3(), {}, 0, outer_setup(453, 0), nested_read(453, 802));
    EXPECT_GT(expect_at_kind(obs, alloc_kind::subtable_build, false, "D11 nested absent, mode (c)"),
              0U)
        << "setup: no room made the sub-table build the first to fail";
}
TEST(GroupReadStatus100, D11_NestedPresentScalarModeA_NotFailed) {
    auto const obs = step_rooms(order3(), {}, 0, outer_setup(453, 0), nested_read(453, 447));
    EXPECT_GT(
        expect_at_kind(obs, alloc_kind::subtable, false, "D11 nested present scalar, mode (a)"), 0U)
        << "setup: no room made the sub-table shell the first to fail";
}
TEST(GroupReadStatus100, D11_NestedPresentScalarModeC_NotFailed) {
    auto const obs = step_rooms(order3(), {}, 0, outer_setup(453, 0), nested_read(453, 447));
    EXPECT_GT(expect_at_kind(obs, alloc_kind::subtable_build, false,
                             "D11 nested present scalar, mode (c)"),
              0U)
        << "setup: no room made the sub-table build the first to fail";
}
// T010's record-miss construction (802 only inside a deeper nested instance of a NoSides(552)
// instance) under mode (a): registration answers "not registered" before any scan.
TEST(GroupReadStatus100, D11_NestedRecordMissModeA_NotFailed) {
    std::string const body = header44("AE") +
                             "571=R1\x01"
                             "552=1\x01"
                             "54=1\x01"
                             "37=O1\x01"
                             "453=1\x01" +
                             kParty +
                             "802=1\x01"
                             "523=S\x01"
                             "803=1\x01";
    auto const obs = step_rooms(body, {}, 0, outer_setup(552, 0), nested_read(552, 802));
    EXPECT_GT(expect_at_kind(obs, alloc_kind::subtable, false, "D11 nested record miss, mode (a)"),
              0U)
        << "setup: no room made the sub-table shell the first to fail";
}

// D7: a sub-table over its entry cap (raised root caps admit an instance larger than the
// sub-table's default entry cap). On an ample arena, so the only failure is the sub-build's.
constexpr std::size_t kPastInstanceCap = fixpp::wire::default_max_group_entries_per_instance + 8U;
wire_read d7_nested(std::uint16_t tag) {
    std::string b = header44("D") + kOrd +
                    "453=1\x01"
                    "448=P\x01";
    for (std::size_t i = 0; i < kPastInstanceCap; ++i) {
        b += "447=D\x01";
    }
    b += "802=1\x01"
         "523=S\x01"
         "803=1\x01"
         "54=1\x01";
    auto const buf = frame44(b);
    auto const fv = fixpp::wire::test::make_frame_view(buf);
    if (!fv) {
        ADD_FAILURE() << "setup: frame";
        return {};
    }
    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{fix44().tv};
    auto const mv =
        parser.parse(*fv, &arena,
                     OffsetTable::Config{.max_offset_entries = 4U * kPastInstanceCap,
                                         .max_group_entries_per_instance = 4U * kPastInstanceCap});
    if (!mv) {
        ADD_FAILURE() << "setup: the raised caps admit the frame";
        return {};
    }
    auto const outer = mv->offsets().group_slices_status(453);
    if (outer.slices.size() != 1U) {
        ADD_FAILURE() << "setup: one outer instance";
        return {};
    }
    auto const ctx = mv->offsets().group_context_for(453);
    auto const r =
        mv->offsets().nested_group_slices(outer.slices[0].data, outer.slices[0].len, tag, ctx);
    return {r.status == fixpp::wire::group_read_status::alloc_failed, r.slices.size()};
}
TEST(GroupReadStatus100, D11_NestedAbsentD7_NotFailed) {
    auto const r = d7_nested(9000);
    EXPECT_FALSE(r.alloc_failed) << "D11 nested absent, D7 sub-table over its entry cap";
    EXPECT_EQ(r.slices, 0U);
}
TEST(GroupReadStatus100, D11_NestedPresentScalarD7_NotFailed) {
    auto const r = d7_nested(447);
    EXPECT_FALSE(r.alloc_failed) << "D11 nested present scalar, D7 sub-table over its entry cap";
    EXPECT_EQ(r.slices, 0U);
}

// ── D10 row cells (FR-007): a failed read stays failed on its repeat ────────────────
// A lane where no room separates the push from the following allocation records the cell as
// skipped.
TEST(GroupReadStatus100, D10_FlatRowPushFails_FailedFirstAndRepeat) {
    auto const obs = step_rooms(order3(), {}, 3, no_setup, flat_read(453));
    if (expect_at_kind(obs, alloc_kind::group_row, true, "D10 flat row push") == 0U) {
        GTEST_SKIP() << "no room makes the group_index_ row the first failure on this lane";
    }
}
TEST(GroupReadStatus100, D10_FlatSlicesFail_FailedFirstAndRepeat) {
    auto const obs = step_rooms(order3(), {}, 3, no_setup, flat_read(453));
    if (expect_at_kind(obs, alloc_kind::slices, true, "D10 flat slices") == 0U) {
        GTEST_SKIP() << "no room makes the slice array the first failure on this lane";
    }
}
TEST(GroupReadStatus100, D10_NestedRowPushFails_FailedFirstAndRepeat) {
    std::string const body = header44("D") + kOrd + "453=1\x01" + kParty +
                             "802=3\x01"
                             "523=S1\x01"
                             "803=1\x01"
                             "523=S2\x01"
                             "803=1\x01"
                             "523=S3\x01"
                             "803=1\x01"
                             "54=1\x01";
    auto const obs = step_rooms(body, {}, 3, outer_setup(453, 0), nested_read(453, 802));
    if (expect_at_kind(obs, alloc_kind::nested_row, true, "D10 nested row push") == 0U) {
        GTEST_SKIP() << "no room makes the nested_cache_ row the first failure on this lane";
    }
}

// ── T027 (US1): `is_group` on the wire-layer twins of T010's cells ─────────────────
// Each read is made twice: the second answers from the `group_index_` row (flat) or the
// `nested_cache_` row (nested), so a repeat that differs from the first is a cache defect
// (research D3).

struct parsed44 {
    std::vector<std::byte> buf;
    std::optional<fixpp::wire::frame_view> fv;
    std::pmr::monotonic_buffer_resource arena;
    std::optional<view_t> mv;
    explicit parsed44(std::string const& body) : buf(frame44(body)) {
        if (auto f = fixpp::wire::test::make_frame_view(buf)) {
            fv.emplace(*f);
            Parser<access_mode::Index> parser{fix44().tv};
            if (auto m = parser.parse(*fv, &arena)) {
                mv.emplace(std::move(*m));
            }
        }
    }
};

// A flat read of `tag`, twice: `is_group`, no slices, `status == ok` both times.
void expect_flat_shape(std::string const& body, std::uint16_t tag, bool want_group,
                       std::string const& what) {
    parsed44 p{body};
    ASSERT_TRUE(p.mv.has_value()) << "setup: " << what << " parses";
    for (int read = 0; read < 2; ++read) {
        auto const r = p.mv->offsets().group_slices_status(tag);
        EXPECT_EQ(r.is_group, want_group)
            << what << " (read " << read << "): is_group should be " << want_group;
        EXPECT_EQ(r.slices.size(), 0U) << what << " (read " << read << "): no instances";
        EXPECT_EQ(r.status, fixpp::wire::group_read_status::ok) << what << " (read " << read << ")";
    }
}
// A nested read of `tag` from instance `i` of the top-level `outer_tag`, twice.
void expect_nested_shape(std::string const& body, std::uint16_t outer_tag, std::size_t i,
                         std::uint16_t tag, bool want_group, std::string const& what) {
    parsed44 p{body};
    ASSERT_TRUE(p.mv.has_value()) << "setup: " << what << " parses";
    auto const outer = p.mv->offsets().group_slices_status(outer_tag);
    ASSERT_GT(outer.slices.size(), i) << "setup: " << what << " has the outer instance";
    auto const ctx = p.mv->offsets().group_context_for(outer_tag);
    for (int read = 0; read < 2; ++read) {
        auto const r = p.mv->offsets().nested_group_slices(outer.slices[i].data,
                                                           outer.slices[i].len, tag, ctx);
        EXPECT_EQ(r.is_group, want_group)
            << what << " (read " << read << "): is_group should be " << want_group;
        EXPECT_EQ(r.slices.size(), 0U) << what << " (read " << read << "): no instances";
        EXPECT_EQ(r.status, fixpp::wire::group_read_status::ok) << what << " (read " << read << ")";
    }
}
std::string ae_head() { return header44("AE") + "571=R1\x01"; }

TEST(GroupReadStatus100, T027_FlatZeroCountMidBody_IsGroup) {
    expect_flat_shape(header44("D") + kOrd +
                          "453=0\x01"
                          "54=1\x01"
                          "38=100\x01",
                      453, true, "US1.1 / FR-002 zero count mid-body");
}
TEST(GroupReadStatus100, T027_FlatZeroCountLastBodyField_IsGroup) {
    expect_flat_shape(header44("D") + kOrd +
                          "54=1\x01"
                          "453=0\x01",
                      453, true, "US1.2 / FR-002 zero count as the last body field");
}
TEST(GroupReadStatus100, T027_FlatZeroCountFollowedByMembers_IsGroup) {
    expect_flat_shape(header44("D") + kOrd + "453=0\x01" + kParty + "54=1\x01", 453, true,
                      "Edge Cases / FR-002 zero count followed by its members");
}
TEST(GroupReadStatus100, T027_FlatLyingCount_IsGroup) {
    expect_flat_shape(header44("D") + kOrd +
                          "453=2\x01"
                          "54=1\x01",
                      453, true, "US1.7 / FR-002 lying count");
}
TEST(GroupReadStatus100, T027_NestedZeroCountMidInstance_IsGroup) {
    expect_nested_shape(ae_head() +
                            "552=1\x01"
                            "54=1\x01"
                            "453=0\x01"
                            "37=O1\x01",
                        552, 0, 453, true, "US1.3 / FR-002 nested zero count mid-instance");
}
TEST(GroupReadStatus100, T027_NestedLyingCount_IsGroup) {
    expect_nested_shape(ae_head() +
                            "552=1\x01"
                            "54=1\x01"
                            "453=2\x01"
                            "37=O1\x01",
                        552, 0, 453, true, "US1.7 / FR-002 nested lying count");
}
TEST(GroupReadStatus100, T027_NestedScalarMidInstance_NotAGroup) {
    expect_nested_shape(header44("D") + kOrd + "453=1\x01" + kParty + "54=1\x01", 453, 0, 447,
                        false, "US1.4 / FR-003 nested scalar mid-instance");
}
TEST(GroupReadStatus100, T027_NestedScalarLastInInstance_NotAGroup) {
    expect_nested_shape(header44("D") + kOrd + "453=1\x01" + kParty + "54=1\x01", 453, 0, 452,
                        false, "US1.4 / FR-003 nested scalar last in its instance");
}
TEST(GroupReadStatus100, T027_Flat802AtRootValueN_NotAGroup) {
    expect_flat_shape(header44("D") + kOrd +
                          "802=2\x01"
                          "523=S\x01"
                          "803=1\x01"
                          "523=T\x01"
                          "803=2\x01"
                          "54=1\x01",
                      802, false, "US1.8 / FR-003 NoPartySubIDs(802) at the root");
}
TEST(GroupReadStatus100, T027_Flat453InDeclaredMessageWithoutParties_NotAGroup) {
    expect_flat_shape(header44("B") +
                          "148=H\x01"
                          "453=1\x01" +
                          kParty + "58=x\x01",
                      453, false, "US1.9 / FR-003 NoPartyIDs(453) in News(B)");
}
// Control against a mutant that sets `is_group = true` everywhere (research D9 "Repeat reads").
TEST(GroupReadStatus100, T027_Control_FlatScalarTwice_NotAGroup) {
    expect_flat_shape(header44("D") + kOrd +
                          "54=1\x01"
                          "38=100\x01",
                      54, false, "US1.11 flat scalar (control)");
}

// spec Clarification: a dict-aware bundle without the registration callback declines the
// group read, as a dict-free one does (research D2 step 1).
TEST(GroupReadStatus100, T027_NullRegistrationCallback_DeclinesTheGroupRead) {
    auto const buf = frame44(header44("D") + kOrd + "453=1\x01" + kParty + "54=1\x01");
    auto const fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value()) << "setup: frame";
    auto const& tv = fix44().tv;
    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena,
                  fixpp::wire::dict_hooks_test_access::make(
                      &tv, /*classify=*/nullptr, &fixpp_test_support::context_group_member_fn,
                      &fixpp_test_support::context_group_delim_fn, /*length_pair=*/nullptr,
                      /*group_registered=*/nullptr)};
    ASSERT_TRUE(t.build_status().has_value()) << "setup: build";
    t.set_group_context(fixpp::wire::group_context{.msg_type = "D"});
    ASSERT_TRUE(t.find(453).has_value()) << "setup: the count tag is present";
    auto const g = t.group(453);
    EXPECT_FALSE(g.has_value()) << "a bundle with no registration callback declines group()";
    for (int read = 0; read < 2; ++read) {
        auto const r = t.group_slices_status(453);
        EXPECT_EQ(r.slices.size(), 0U) << "read " << read << ": no instances";
        EXPECT_FALSE(r.is_group) << "read " << read << ": not a group";
        EXPECT_EQ(r.status, fixpp::wire::group_read_status::ok) << "read " << read;
    }
}

}  // namespace
