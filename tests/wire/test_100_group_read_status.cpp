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
//
// T039 (US2; FR-004, FR-005, FR-007, FR-011; research D5, D7, D10, D11) adds `status` and
// `is_group` on T015's cells and on the wire-layer twins of tests/capi/test_100_group_getters.cpp's
// T012 cells, each read twice, with data-model's invariants asserted on every read: a failed read
// has no slices and is not a group, and on the nested path it means a tag that is present in the
// instance and registered in the read's context. Each cell states presence and registration from
// its own frame and the shipped FIX 4.4 dictionary, not through the code under test. Its D7 (ii)
// twin sets the overlay seed, so it lives in tests/wire/test_100_seed_hook_status.cpp.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fixpp/wire/group_view.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "support/context_group_delim_fn.hpp"
#include "support/context_group_member_fn.hpp"
#include "support/dict_hooks_test_access.hpp"
#include "support/frame_view_factory.hpp"
#include "support/group_read_100.hpp"
#include "support/msvc_debug_arena_skip.hpp"

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
    fixpp::wire::group_read_status status = fixpp::wire::group_read_status::ok;
    bool is_group = false;
};

struct step_obs {
    std::size_t room = 0;
    alloc_kind kind = alloc_kind::none;
    wire_read first;
    wire_read repeat;
    // The requests the first read made before its first failed one, in order.
    std::vector<fixpp::test100::recording_resource::event> before_failure;
};

using view_t = MessageView<access_mode::Index>;

// Steps the arena for `body`. `setup` runs before the mark and returns false when the room is
// too small for it; it may record an instance slice for `read`.
template <class Setup, class Read>
std::vector<step_obs> step_rooms(std::string const& body, OffsetTable::Config cfg,
                                 std::size_t n_slices, Setup setup, Read read) {
    std::vector<step_obs> out;
    bool const framed = fixpp::test100::step_parse_rooms(
        body, fix44().tv, cfg, kMaxRoom,
        [&](std::size_t room, stepped_arena& arena, view_t const& mv) {
            std::optional<fixpp::wire::group_slice> outer;
            if (!setup(mv, outer)) {
                return false;
            }
            arena.rec().mark();
            step_obs o;
            o.room = room;
            o.first = read(mv, outer);
            o.kind = classify_first_failure(arena.rec(), n_slices).kind;
            for (auto const& e : arena.rec().events_since_mark()) {
                if (!e.ok) {
                    break;
                }
                o.before_failure.push_back(e);
            }
            o.repeat = read(mv, outer);
            bool const complete = !arena.rec().first_failure().has_value();
            out.push_back(o);
            return complete;
        });
    if (!framed) {
        ADD_FAILURE() << "setup: frame";
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
template <class Result>
wire_read to_wire_read(Result const& r) {
    return wire_read{r.status == fixpp::wire::group_read_status::alloc_failed, r.slices.size(),
                     r.status, r.is_group};
}
auto flat_read(std::uint16_t tag) {
    return [tag](view_t const& mv, std::optional<fixpp::wire::group_slice> const& /*outer*/) {
        return to_wire_read(mv.offsets().group_slices_status(tag));
    };
}
auto nested_read(std::uint16_t outer_tag, std::uint16_t tag) {
    return [outer_tag, tag](view_t const& mv,
                            std::optional<fixpp::wire::group_slice> const& outer) {
        auto const ctx = mv.offsets().group_context_for(outer_tag);
        return to_wire_read(mv.offsets().nested_group_slices(outer->data, outer->len, tag, ctx));
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
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs = step_rooms(order3(), {}, 3, no_setup, flat_read(54));
    EXPECT_GT(expect_at_kind(obs, alloc_kind::group_row, false, "D10 flat present scalar"), 0U)
        << "setup: no room made the group_index_ row the first to fail";
}

// ── D11 (FR-004): nested reads of an absent tag and of a present scalar ────────────
TEST(GroupReadStatus100, D11_NestedAbsentModeA_NotFailed) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs = step_rooms(order3(), {}, 0, outer_setup(453, 0), nested_read(453, 802));
    EXPECT_GT(expect_at_kind(obs, alloc_kind::subtable, false, "D11 nested absent, mode (a)"), 0U)
        << "setup: no room made the sub-table shell the first to fail";
}
TEST(GroupReadStatus100, D11_NestedAbsentModeC_NotFailed) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs = step_rooms(order3(), {}, 0, outer_setup(453, 0), nested_read(453, 802));
    EXPECT_GT(expect_at_kind(obs, alloc_kind::subtable_build, false, "D11 nested absent, mode (c)"),
              0U)
        << "setup: no room made the sub-table build the first to fail";
}
TEST(GroupReadStatus100, D11_NestedPresentScalarModeA_NotFailed) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs = step_rooms(order3(), {}, 0, outer_setup(453, 0), nested_read(453, 447));
    EXPECT_GT(
        expect_at_kind(obs, alloc_kind::subtable, false, "D11 nested present scalar, mode (a)"), 0U)
        << "setup: no room made the sub-table shell the first to fail";
}
TEST(GroupReadStatus100, D11_NestedPresentScalarModeC_NotFailed) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs = step_rooms(order3(), {}, 0, outer_setup(453, 0), nested_read(453, 447));
    EXPECT_GT(expect_at_kind(obs, alloc_kind::subtable_build, false,
                             "D11 nested present scalar, mode (c)"),
              0U)
        << "setup: no room made the sub-table build the first to fail";
}
// T010's record-miss construction (802 only inside a deeper nested instance of a NoSides(552)
// instance) under mode (a): registration answers "not registered" before any scan.
TEST(GroupReadStatus100, D11_NestedRecordMissModeA_NotFailed) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
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
using fixpp::test100::kPastInstanceCap;
// `with_802`: the instance carries a NoPartySubIDs(802) group after its members.
wire_read d7_nested(std::uint16_t tag, bool with_802 = true, bool repeat = false) {
    auto const buf =
        frame44(fixpp::test100::oversized_instance(kPastInstanceCap, with_802 ? "802=1\x01"
                                                                                "523=S\x01"
                                                                                "803=1\x01"
                                                                              : ""));
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
    auto r = mv->offsets().nested_group_slices(outer.slices[0].data, outer.slices[0].len, tag, ctx);
    if (repeat) {
        r = mv->offsets().nested_group_slices(outer.slices[0].data, outer.slices[0].len, tag, ctx);
    }
    return to_wire_read(r);
}
TEST(GroupReadStatus100, D11_NestedAbsentD7_NotFailed) {
    auto const r = d7_nested(9000);
    EXPECT_FALSE(r.alloc_failed) << "D11 nested absent, D7 sub-table over its entry cap";
    EXPECT_EQ(r.status, fixpp::wire::group_read_status::ok)
        << "D11 nested absent, D7 sub-table over its entry cap: not a failed read";
    EXPECT_EQ(r.slices, 0U);
}
TEST(GroupReadStatus100, D11_NestedPresentScalarD7_NotFailed) {
    auto const r = d7_nested(447);
    EXPECT_FALSE(r.alloc_failed) << "D11 nested present scalar, D7 sub-table over its entry cap";
    EXPECT_EQ(r.status, fixpp::wire::group_read_status::ok)
        << "D11 nested present scalar, D7 sub-table over its entry cap: not a failed read";
    EXPECT_EQ(r.slices, 0U);
}

// ── D10 row cells (FR-007): a failed read stays failed on its repeat ────────────────
// A lane where no room separates the push from the following allocation records the cell as
// skipped.
TEST(GroupReadStatus100, D10_FlatRowPushFails_FailedFirstAndRepeat) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs = step_rooms(order3(), {}, 3, no_setup, flat_read(453));
    if (expect_at_kind(obs, alloc_kind::group_row, true, "D10 flat row push") == 0U) {
        GTEST_SKIP() << "no room makes the group_index_ row the first failure on this lane";
    }
}
TEST(GroupReadStatus100, D10_FlatSlicesFail_FailedFirstAndRepeat) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs = step_rooms(order3(), {}, 3, no_setup, flat_read(453));
    if (expect_at_kind(obs, alloc_kind::slices, true, "D10 flat slices") == 0U) {
        GTEST_SKIP() << "no room makes the slice array the first failure on this lane";
    }
}
TEST(GroupReadStatus100, D10_NestedRowPushFails_FailedFirstAndRepeat) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
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

using fixpp::test100::parsed44;

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

// ════════════════════════════════════════════════════════════════════════════════
// T039 (US2): `status` and `is_group` on T015's cells and T012's wire twins.
// ════════════════════════════════════════════════════════════════════════════════

using fixpp::wire::group_read_status;

char const* status_name(group_read_status s) {
    switch (s) {
        case group_read_status::ok:
            return "ok";
        case group_read_status::alloc_failed:
            return "alloc_failed";
        case group_read_status::limit_exceeded:
            return "limit_exceeded";
    }
    return "?";
}

// data-model's invariants for one read. `present_and_registered` is the fixture's own statement
// of whether its tag is in the bytes the read covers and registered in the read's context.
void expect_invariants(wire_read const& r, bool present_and_registered, std::string const& what) {
    if (r.status != group_read_status::ok) {
        EXPECT_EQ(r.slices, 0U) << what << ": a failed read carries no slices";
        EXPECT_FALSE(r.is_group) << what << ": a failed read is not a group";
        EXPECT_TRUE(present_and_registered) << what << ": " << status_name(r.status)
                                            << " for a tag that is not both present and registered";
    }
    if (!r.is_group) {
        EXPECT_EQ(r.slices, 0U) << what << ": not a group, so no slices";
    }
}

void expect_ending(wire_read const& r, group_read_status want_status, bool want_is_group,
                   std::string const& what) {
    EXPECT_EQ(r.status, want_status)
        << what << ": status " << status_name(r.status) << ", want " << status_name(want_status);
    EXPECT_EQ(r.is_group, want_is_group) << what << ": is_group";
    EXPECT_EQ(r.slices, 0U) << what << ": no slices";
}

// Every read at every room keeps the invariants; at every room whose first failure is `kind`,
// both reads end with `want_status`, `want_is_group` and no slices. Returns how many rooms had
// `kind` first.
std::size_t expect_ending_at_kind(std::vector<step_obs> const& obs, alloc_kind kind,
                                  group_read_status want_status, bool want_is_group,
                                  bool present_and_registered, std::string const& what) {
    std::size_t hits = 0;
    for (auto const& o : obs) {
        std::string const at = what + " (room " + std::to_string(o.room) + ", " +
                               alloc_kind_name(o.kind) + " failed first)";
        expect_invariants(o.first, present_and_registered, at + ", first read");
        expect_invariants(o.repeat, present_and_registered, at + ", repeat read");
        if (o.kind != kind) {
            continue;
        }
        ++hits;
        expect_ending(o.first, want_status, want_is_group, at + ", first read");
        expect_ending(o.repeat, want_status, want_is_group, at + ", repeat read");
    }
    return hits;
}

// D10 / FR-007: at every room the repeat read answers as the first did.
void expect_repeat_as_first(std::vector<step_obs> const& obs, std::string const& what) {
    for (auto const& o : obs) {
        std::string const at = what + " (room " + std::to_string(o.room) + ", " +
                               alloc_kind_name(o.kind) + " failed first)";
        EXPECT_EQ(o.repeat.status, o.first.status)
            << at << ": the repeat read's status " << status_name(o.repeat.status)
            << ", the first read's " << status_name(o.first.status);
        EXPECT_EQ(o.repeat.is_group, o.first.is_group) << at << ": is_group";
        EXPECT_EQ(o.repeat.slices, o.first.slices) << at << ": instance count";
    }
}

std::size_t count_kind(std::vector<step_obs> const& obs, alloc_kind kind) {
    std::size_t n = 0;
    for (auto const& o : obs) {
        n += (o.kind == kind) ? 1U : 0U;
    }
    return n;
}

// ── D10 / FR-004: a present scalar whose cache row does not fit (T015's twin) ───────
// 54 is in the message and FIX 4.4 registers no group at it.
TEST(GroupReadStatus100, T039_D10_FlatPresentScalarRowPushFails_OkNotAGroup) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs = step_rooms(order3(), {}, 3, no_setup, flat_read(54));
    EXPECT_GT(expect_ending_at_kind(obs, alloc_kind::group_row, group_read_status::ok, false, false,
                                    "D10 flat present scalar"),
              0U)
        << "setup: no room made the group_index_ row the first to fail";
}

// ── D11 / FR-004: nested reads under mode (a), mode (c) and D7 (T015's twins) ──────
// From instance 0 of order3's NoPartyIDs(453): 802 is registered under [453] and absent from
// the instance; 447 is present and is a member, not a group.
TEST(GroupReadStatus100, T039_D11_NestedAbsentModeA_OkNotAGroup) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs = step_rooms(order3(), {}, 0, outer_setup(453, 0), nested_read(453, 802));
    EXPECT_GT(expect_ending_at_kind(obs, alloc_kind::subtable, group_read_status::ok, false, false,
                                    "D11 nested absent, mode (a)"),
              0U)
        << "setup: no room made the sub-table shell the first to fail";
}
TEST(GroupReadStatus100, T039_D11_NestedAbsentModeC_OkNotAGroup) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs = step_rooms(order3(), {}, 0, outer_setup(453, 0), nested_read(453, 802));
    EXPECT_GT(expect_ending_at_kind(obs, alloc_kind::subtable_build, group_read_status::ok, false,
                                    false, "D11 nested absent, mode (c)"),
              0U)
        << "setup: no room made the sub-table build the first to fail";
}
TEST(GroupReadStatus100, T039_D11_NestedPresentScalarModeA_OkNotAGroup) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs = step_rooms(order3(), {}, 0, outer_setup(453, 0), nested_read(453, 447));
    EXPECT_GT(expect_ending_at_kind(obs, alloc_kind::subtable, group_read_status::ok, false, false,
                                    "D11 nested present scalar, mode (a)"),
              0U)
        << "setup: no room made the sub-table shell the first to fail";
}
TEST(GroupReadStatus100, T039_D11_NestedPresentScalarModeC_OkNotAGroup) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs = step_rooms(order3(), {}, 0, outer_setup(453, 0), nested_read(453, 447));
    EXPECT_GT(expect_ending_at_kind(obs, alloc_kind::subtable_build, group_read_status::ok, false,
                                    false, "D11 nested present scalar, mode (c)"),
              0U)
        << "setup: no room made the sub-table build the first to fail";
}
// 802 occurs only inside the NoPartyIDs(453) instance nested in the NoSides(552) instance, so it
// is present in the 552 instance, and FIX 4.4 registers it under [552, 453], not under [552].
TEST(GroupReadStatus100, T039_D11_NestedRecordMissModeA_OkNotAGroup) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
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
    EXPECT_GT(expect_ending_at_kind(obs, alloc_kind::subtable, group_read_status::ok, false, false,
                                    "D11 nested record miss, mode (a)"),
              0U)
        << "setup: no room made the sub-table shell the first to fail";
}
// D7 origin, on an ample arena: the sub-table is over its entry cap. Controls at `H4`, where the
// sub-build's `wire_offset_table_full` is not yet a failure (tasks Mutant table: M-20 for the
// absent tag, registered under [453]; M-21 for the present scalar).
TEST(GroupReadStatus100, T039_D11_Control_NestedAbsentD7_OkNotAGroup) {
    auto const first = d7_nested(802, /*with_802=*/false);
    auto const repeat = d7_nested(802, /*with_802=*/false, /*repeat=*/true);
    expect_invariants(first, false, "D11 nested absent, D7, first read");
    expect_invariants(repeat, false, "D11 nested absent, D7, repeat read");
    expect_ending(first, group_read_status::ok, false, "D11 nested absent, D7, first read");
    expect_ending(repeat, group_read_status::ok, false, "D11 nested absent, D7, repeat read");
}
TEST(GroupReadStatus100, T039_D11_Control_NestedPresentScalarD7_OkNotAGroup) {
    auto const first = d7_nested(447);
    auto const repeat = d7_nested(447, /*with_802=*/true, /*repeat=*/true);
    expect_invariants(first, false, "D11 nested present scalar, D7, first read");
    expect_invariants(repeat, false, "D11 nested present scalar, D7, repeat read");
    expect_ending(first, group_read_status::ok, false, "D11 nested present scalar, D7, first");
    expect_ending(repeat, group_read_status::ok, false, "D11 nested present scalar, D7, repeat");
}

// ── D5 / FR-011: a top-level instance past the per-instance cap ──────────────────────
TEST(GroupReadStatus100, T039_D5_FlatInstancePastPerInstanceCap_LimitExceededTwice) {
    auto const buf = frame44(fixpp::test100::oversized_instance(kPastInstanceCap, ""));
    auto const fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{fix44().tv};
    auto const mv =
        parser.parse(*fv, &arena, OffsetTable::Config{.max_offset_entries = 4U * kPastInstanceCap});
    ASSERT_TRUE(mv.has_value()) << "setup: the raised entry cap admits the frame";
    for (int read = 0; read < 2; ++read) {
        std::string const what = "D5 top level, read " + std::to_string(read);
        auto const r = to_wire_read(mv->offsets().group_slices_status(453));
        expect_invariants(r, true, what);
        expect_ending(r, group_read_status::limit_exceeded, false, what);
    }
}

// ── D7 (i) / FR-011: a nested sub-table over its entry cap, at the wire layer ─────────
// 802 is in the instance and registered under [453].
TEST(GroupReadStatus100, T039_D7i_NestedSubTableOverEntryCap_LimitExceededTwice) {
    auto const first = d7_nested(802);
    auto const repeat = d7_nested(802, /*with_802=*/true, /*repeat=*/true);
    expect_invariants(first, true, "D7 (i) first read");
    expect_invariants(repeat, true, "D7 (i) repeat read");
    expect_ending(first, group_read_status::limit_exceeded, false, "D7 (i) first read");
    expect_ending(repeat, group_read_status::limit_exceeded, false, "D7 (i) repeat read");
}

// ── D11 step 1: a dict-aware bundle with no registration callback, under mode (a) ─────
// The root is parsed with FIX 4.4's own bundle, so the outer read succeeds; the nested read
// passes a bundle built through `dict_hooks_test_access::make` over the same table with the
// membership and delimiter callbacks and a null registration callback. NoPartySubIDs(802) is in
// the instance, so a presence scan would find it: the answer must come from step 1. No nested read
// of that slice is made with any other bundle on that root.
TEST(GroupReadStatus100, T039_D11_NullRegistrationCallbackModeA_OkNotAGroup) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    std::string const body = header44("D") + kOrd + "453=1\x01" + kParty +
                             "802=1\x01"
                             "523=S\x01"
                             "803=1\x01"
                             "54=1\x01";
    auto const& tv = fix44().tv;
    auto const no_registration = fixpp::wire::dict_hooks_test_access::make(
        &tv, /*classify=*/nullptr, &fixpp_test_support::context_group_member_fn,
        &fixpp_test_support::context_group_delim_fn, /*length_pair=*/nullptr,
        /*group_registered=*/nullptr);
    auto const read = [&no_registration](view_t const& mv,
                                         std::optional<fixpp::wire::group_slice> const& outer) {
        auto const ctx = mv.offsets().group_context_for(453);
        // The generation token `test::make_frame_view` gives `step_rooms`' frame by default.
        return to_wire_read(
            mv.offsets().nested_group_slices(outer->data, outer->len, 802, no_registration,
                                             fixpp::wire::detail::generation_token{}, ctx));
    };
    auto const obs = step_rooms(body, {}, 0, outer_setup(453, 0), read);
    EXPECT_GT(expect_ending_at_kind(obs, alloc_kind::subtable, group_read_status::ok, false, false,
                                    "D11 step 1, null registration callback, mode (a)"),
              0U)
        << "setup: no room made the sub-table shell the first to fail";
}

// ── D10 / FR-007: the cache rows, "repeat answers as the first" ──────────────────────
// The arena's room is stepped across every value from the push of the row to the allocation
// that follows it. D10 orders the flat row's push after the count pass and before the slice
// allocation, and the nested row's push before the sub-table's build, so at a room where the push
// is the first request to fail no slice array (flat) or sub-table shell (nested) was requested
// before it. A lane where no room fails at the allocation after a push that fitted (the
// separating step) records the cell as skipped.
TEST(GroupReadStatus100, T039_D10_FlatRow_RepeatAnswersAsFirst) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs = step_rooms(order3(), {}, 3, no_setup, flat_read(453));
    std::string const what = "D10 flat row, NoPartyIDs(453) with 3 instances";
    expect_repeat_as_first(obs, what);
    EXPECT_GT(expect_ending_at_kind(obs, alloc_kind::group_row, group_read_status::alloc_failed,
                                    false, true, what),
              0U)
        << "setup: no room made the group_index_ row the first to fail";
    (void)expect_ending_at_kind(obs, alloc_kind::slices, group_read_status::alloc_failed, false,
                                true, what);
    constexpr std::size_t slice_bytes = 3U * sizeof(fixpp::wire::group_slice);
    for (auto const& o : obs) {
        if (o.kind != alloc_kind::group_row) {
            continue;
        }
        for (auto const& e : o.before_failure) {
            EXPECT_FALSE(e.bytes == slice_bytes && e.align == alignof(fixpp::wire::group_slice))
                << what << " (room " << o.room
                << "): the slice array was requested before the row was pushed";
        }
    }
    if (count_kind(obs, alloc_kind::slices) == 0U) {
        GTEST_SKIP() << "no room fails at the slice array after the row fitted on this lane";
    }
}
// NoPartySubIDs(802) holds zero instances, as in T012's nested row cell: after its row is pushed
// and its sub-table is built, the read needs only the sub-table's own group_index_ row.
TEST(GroupReadStatus100, T039_D10_NestedRow_RepeatAnswersAsFirst) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    std::string const body = header44("D") + kOrd + "453=1\x01" + kParty +
                             "802=0\x01"
                             "54=1\x01";
    auto const obs = step_rooms(body, {}, 0, outer_setup(453, 0), nested_read(453, 802));
    std::string const what = "D10 nested row, NoPartySubIDs(802) with 0 instances";
    expect_repeat_as_first(obs, what);
    EXPECT_GT(expect_ending_at_kind(obs, alloc_kind::nested_row, group_read_status::alloc_failed,
                                    false, true, what),
              0U)
        << "setup: no room made the nested_cache_ row the first to fail";
    for (auto const& o : obs) {
        if (o.kind != alloc_kind::nested_row) {
            continue;
        }
        for (auto const& e : o.before_failure) {
            EXPECT_FALSE(e.bytes == sizeof(OffsetTable) && e.align == alignof(OffsetTable))
                << what << " (room " << o.room
                << "): the sub-table shell was requested before the row was pushed";
        }
    }
    if (count_kind(obs, alloc_kind::subtable) + count_kind(obs, alloc_kind::subtable_build) == 0U) {
        GTEST_SKIP() << "no room fails at the sub-table after the row fitted on this lane";
    }
}

// ── T070 (`[const §IX.1]`): research D11's presence scan over a slice it cannot read ─────
// A direct caller of the public `nested_group_slices` overload hands it any bytes. Each
// slice opens with more fields than the sub-table's entry cap, so the sub-build fails
// `wire_offset_table_full` before it reaches what follows, and the scan then decides the
// read's ending. With the registered tag after a field the scan cannot read, the scan
// stops there and the read is not failed; the same tag before that field keeps the
// failure (the control).
namespace scan100 {

constexpr std::uint16_t kTag = 700;

bool every_member(void const*, fixpp::wire::group_context const&, std::uint16_t,
                  std::uint16_t) noexcept {
    return true;
}
fixpp::wire::dict_hooks::group_resolution every_registered(void const*,
                                                           fixpp::wire::group_context const&,
                                                           std::uint16_t) noexcept {
    // Names no definition: the walk asks `every_member`.
    return {.registered = true, .definition_known = false, .delimiter = 0, .members = {}};
}

// The read's status over `filler + tail`, followed by the trailing byte `last`.
fixpp::wire::group_read_status read_status(std::string const& tail, char last = '\x01') {
    std::string slice;
    for (std::size_t i = 0; i <= fixpp::wire::default_max_offset_entries; ++i) {
        slice += "100=x\x01";
    }
    slice += tail;
    std::string bytes = slice + last;
    static constexpr char kRootFrame[] =
        "8=FIX.4.4\x01"
        "9=5\x01"
        "35=D\x01"
        "10=000\x01";
    auto const fv = fixpp::wire::test::make_frame_view(std::span<std::byte const>{
        reinterpret_cast<std::byte const*>(kRootFrame), sizeof(kRootFrame) - 1});
    EXPECT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    static constexpr int kDictToken = 0;  // a non-null dictionary: the bundle can establish groups
    auto const hooks = fixpp::wire::dict_hooks_test_access::make(
        &kDictToken, nullptr, &every_member, nullptr, nullptr, &every_registered);
    fixpp::wire::OffsetTable const root{*fv, &arena, hooks};
    fixpp::wire::group_read_status status{};
    {
        auto const r = root.nested_group_slices(
            reinterpret_cast<std::byte const*>(bytes.data()), slice.size(), kTag, hooks,
            fixpp::wire::detail::generation_token{}, fixpp::wire::group_context{});
        EXPECT_TRUE(r.slices.empty());
        EXPECT_FALSE(r.is_group);
        status = r.status;
    }
    return status;
}

}  // namespace scan100

TEST(GroupReadStatus100, T070_D11_ScanControl_TagBeforeUnreadableField_LimitExceeded) {
    EXPECT_EQ(scan100::read_status("700=1\x01"
                                   "1a=1\x01"),
              fixpp::wire::group_read_status::limit_exceeded);
}
TEST(GroupReadStatus100, T070_D11_ScanStopsAtNonDigitTag_NotFailed) {
    EXPECT_EQ(scan100::read_status("1a=1\x01"
                                   "700=1\x01"),
              fixpp::wire::group_read_status::ok);
    EXPECT_EQ(scan100::read_status("1/=1\x01"
                                   "700=1\x01"),
              fixpp::wire::group_read_status::ok)
        << "a byte below '0'";
}
TEST(GroupReadStatus100, T070_D11_ScanStopsAtTagOutOfRange_NotFailed) {
    EXPECT_EQ(scan100::read_status("70000=1\x01"
                                   "700=1\x01"),
              fixpp::wire::group_read_status::ok);
}
TEST(GroupReadStatus100, T070_D11_ScanStopsAtTagWithoutEquals_NotFailed) {
    EXPECT_EQ(scan100::read_status("12\x01"
                                   "700=1\x01"),
              fixpp::wire::group_read_status::ok);
}
TEST(GroupReadStatus100, T070_D11_ScanStopsAtEmptyTag_NotFailed) {
    EXPECT_EQ(scan100::read_status("=1\x01"
                                   "700=1\x01"),
              fixpp::wire::group_read_status::ok);
}
// A tag that runs to the end of the scanned bytes. Nothing can follow it, so the ending
// is the not-found one either way; the cell exercises the bound before the `=` read.
TEST(GroupReadStatus100, T070_D11_ScanStopsAtTagRunningToTheEnd_NotFailed) {
    EXPECT_EQ(scan100::read_status("12", '3'), fixpp::wire::group_read_status::ok);
}
// RawDataLength(95) / RawData(96) is a standard pair, counted whatever the bundle: a
// count past the end of the bytes stops the scan; a true count steps over the Data
// value's embedded SOH and reaches the tag (the control).
TEST(GroupReadStatus100, T070_D11_ScanStopsAtLyingCount_NotFailed) {
    EXPECT_EQ(scan100::read_status("95=50\x01"
                                   "96=ab\x01"
                                   "700=1\x01"),
              fixpp::wire::group_read_status::ok);
}
TEST(GroupReadStatus100, T070_D11_ScanControl_TrueCount_LimitExceeded) {
    EXPECT_EQ(scan100::read_status("95=3\x01"
                                   "96=a\x01"
                                   "b\x01"
                                   "700=1\x01"),
              fixpp::wire::group_read_status::limit_exceeded);
}
}  // namespace
