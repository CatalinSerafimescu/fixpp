// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/test_100_group_getters.cpp — 100-wire-group-read-integrity, tasks T010 (US1) and
// T012 (US2): the C group getters `fixpp_msg_get_group` and `fixpp_group_get_nested_group`
// against contracts/capi-group-getters.md's ordered table (research D4, D9).
//
// T010's frames are FIX 4.4 messages parsed by `Parser` with the shipped FIX44.xml's
// `as_table_view()` and wrapped as `fixpp_msg` through capi_internal.hpp. Each cell names its
// spec scenario and requirement; a cell named `..._Twice` reads the same tag a second time on
// the same view, and the second answer must equal the first (US1.11). T012's cells, appended
// below, own a null-upstream arena whose room they step across the allocations they target.
//
// Every getter call goes through `test100::get_group` / `get_nested`, which pre-set the outputs
// to sentinels and turn an exception leaving the getter into `kEscaped`, so each cell reports
// the answer it got as a code.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>
#include <memory_resource>
#include <optional>
#include <string>
#include <vector>

#include "group_getters_100_support.hpp"
#include "support/frame_view_factory.hpp"
#include "support/group_read_100.hpp"
#include "support/msvc_debug_arena_skip.hpp"

namespace {

using fixpp::test100::fix44;
using fixpp::test100::frame44;
using fixpp::test100::get_group;
using fixpp::test100::get_nested;
using fixpp::test100::header44;
using fixpp::test100::inbound_handle;
using fixpp::test100::rc_name;
using fixpp::test100::read_out;
using fixpp::wire::access_mode;
using fixpp::wire::Parser;

using fixpp::test100::parsed44;

void expect_code(read_out const& r, int code, std::string const& what) {
    EXPECT_EQ(r.rc, code) << what << ": expected " << rc_name(code) << "; actual " << rc_name(r.rc)
                          << " (count " << r.count << ")";
    EXPECT_EQ(r.g, nullptr) << what << ": *out must be null";
    EXPECT_EQ(r.count, 0U) << what << ": *count_out must be 0";
}

void expect_ok_empty(read_out const& r, std::string const& what) {
    expect_code(r, FIXPP_ERR_OK, what);
}

void expect_cursor(read_out const& r, std::size_t n, std::string const& what) {
    EXPECT_EQ(r.rc, FIXPP_ERR_OK) << what << ": expected a cursor of " << n << "; actual "
                                  << rc_name(r.rc);
    EXPECT_NE(r.g, nullptr) << what << ": a cursor";
    EXPECT_EQ(r.count, n) << what << ": instance count";
}

// The flat getter for `tag` on `body`; when `twice`, a second read on the same view must answer
// the same code and count.
read_out flat(std::string const& body, std::uint16_t tag, bool twice = false) {
    parsed44 const p{body};
    if (!p.mv) {
        ADD_FAILURE() << "setup: the frame did not parse";
        return {};
    }
    inbound_handle const h{*p.mv};
    auto const r1 = get_group(h.ptr(), tag);
    if (twice) {
        auto const r2 = get_group(h.ptr(), tag);
        EXPECT_EQ(r2.rc, r1.rc) << "US1.11: the repeat read's code differs: first "
                                << rc_name(r1.rc) << ", repeat " << rc_name(r2.rc);
        EXPECT_EQ(r2.count, r1.count) << "US1.11: the repeat read's count differs";
    }
    return r1;
}

// The nested getter for `nested_tag` in instance `i` of the top-level group `outer_tag`.
read_out nested(std::string const& body, std::uint16_t outer_tag, std::size_t i,
                std::uint16_t nested_tag, bool twice = false) {
    parsed44 const p{body};
    if (!p.mv) {
        ADD_FAILURE() << "setup: the frame did not parse";
        return {};
    }
    inbound_handle const h{*p.mv};
    auto const outer = get_group(h.ptr(), outer_tag);
    if (outer.rc != FIXPP_ERR_OK || outer.g == nullptr || outer.count <= i) {
        ADD_FAILURE() << "setup: the outer group " << outer_tag << " read " << rc_name(outer.rc)
                      << " with count " << outer.count;
        return {};
    }
    auto const r1 = get_nested(outer.g, i, nested_tag);
    if (twice) {
        auto const r2 = get_nested(outer.g, i, nested_tag);
        EXPECT_EQ(r2.rc, r1.rc) << "US1.11: the repeat nested read's code differs: first "
                                << rc_name(r1.rc) << ", repeat " << rc_name(r2.rc);
        EXPECT_EQ(r2.count, r1.count) << "US1.11: the repeat nested read's count differs";
    }
    return r1;
}

std::string const kOrd = "11=ORD1\x01";
std::string const kParty =
    "448=PA\x01"
    "447=D\x01"
    "452=1\x01";
// A TradeCaptureReport(AE) head; its NoSides(552) group holds Parties(453).
std::string ae_head() { return header44("AE") + "571=R1\x01"; }

// ── US1.1 / US1.2 (FR-002): a registered zero count ─────────────────────────────

TEST(GroupGetters100, US1_1_FlatZeroCountMidBody_OkZero) {
    expect_ok_empty(flat(header44("D") + kOrd +
                             "453=0\x01"
                             "54=1\x01"
                             "38=100\x01",
                         453),
                    "US1.1 / FR-002");
}
TEST(GroupGetters100, US1_1_FlatZeroCountMidBody_Twice) {
    expect_ok_empty(flat(header44("D") + kOrd +
                             "453=0\x01"
                             "54=1\x01"
                             "38=100\x01",
                         453, true),
                    "US1.1 / US1.11");
}
TEST(GroupGetters100, US1_2_FlatZeroCountLastBodyField_OkZero) {
    expect_ok_empty(flat(header44("D") + kOrd +
                             "54=1\x01"
                             "453=0\x01",
                         453),
                    "US1.2 / FR-002");
}
TEST(GroupGetters100, US1_2_FlatZeroCountLastBodyField_Twice) {
    expect_ok_empty(flat(header44("D") + kOrd +
                             "54=1\x01"
                             "453=0\x01",
                         453, true),
                    "US1.2 / US1.11");
}

// ── spec Edge Cases (FR-002): a registered count holding 0 followed by its members ──

TEST(GroupGetters100, Edge_FlatZeroCountFollowedByMembers_OkZero) {
    expect_ok_empty(flat(header44("D") + kOrd + "453=0\x01" + kParty + "54=1\x01", 453),
                    "Edge Cases (zero count then members) / FR-002");
}
TEST(GroupGetters100, Edge_FlatZeroCountFollowedByMembers_Twice) {
    expect_ok_empty(flat(header44("D") + kOrd + "453=0\x01" + kParty + "54=1\x01", 453, true),
                    "Edge Cases / US1.11");
}
// Control. Inside a NoSides(552) instance the members that follow a zero NoPartyIDs(453) are
// not NoSides members, so the instance ends before them and the nested read sees the count last
// in its instance.
TEST(GroupGetters100, Edge_Control_NestedZeroCountFollowedByMembers_OkZero) {
    expect_ok_empty(nested(ae_head() +
                               "552=1\x01"
                               "54=1\x01"
                               "37=O1\x01"
                               "453=0\x01" +
                               kParty,
                           552, 0, 453),
                    "Edge Cases nested (control) / FR-002");
}

// ── spec Edge Cases (FR-002): a count value with no leading digit reads as 0 ────

TEST(GroupGetters100, Edge_FlatNonDigitCountFollowedByMembers_OkZero) {
    expect_ok_empty(flat(header44("D") + kOrd + "453=abc\x01" + kParty + "54=1\x01", 453),
                    "Edge Cases (no leading digit, members follow) / FR-002");
}
TEST(GroupGetters100, Edge_FlatNonDigitCountFollowedByMembers_Twice) {
    expect_ok_empty(flat(header44("D") + kOrd + "453=abc\x01" + kParty + "54=1\x01", 453, true),
                    "Edge Cases / US1.11");
}
TEST(GroupGetters100, Edge_FlatNonDigitCountFollowedByNonMember_OkZero) {
    expect_ok_empty(flat(header44("D") + kOrd +
                             "453=abc\x01"
                             "54=1\x01",
                         453),
                    "Edge Cases (no leading digit, non-member follows) / FR-002");
}
TEST(GroupGetters100, Edge_FlatNonDigitCountFollowedByNonMember_Twice) {
    expect_ok_empty(flat(header44("D") + kOrd +
                             "453=abc\x01"
                             "54=1\x01",
                         453, true),
                    "Edge Cases / US1.11");
}

// ── US1.3 (FR-002): a nested zero count mid-instance (control) ──────────────────

TEST(GroupGetters100, US1_3_Control_NestedZeroCountMidInstance_OkZero) {
    expect_ok_empty(nested(ae_head() + "552=1\x01"
                                       "54=1\x01"
                                       "453=0\x01"
                                       "37=O1\x01",
                           552, 0, 453),
                    "US1.3 (control) / FR-002");
}

// ── US1.4 (FR-003): a scalar is not a group ─────────────────────────────────────

TEST(GroupGetters100, US1_4_Control_FlatScalarHoldingZero_TypeMismatch) {
    expect_code(flat(header44("D") + kOrd +
                         "38=0\x01"
                         "54=1\x01",
                     38),
                FIXPP_ERR_TYPE_MISMATCH, "US1.4 (control) / FR-003");
}
TEST(GroupGetters100, US1_4_NestedScalarMidInstance_TypeMismatch) {
    expect_code(nested(header44("D") + kOrd + "453=1\x01" + kParty + "54=1\x01", 453, 0, 447),
                FIXPP_ERR_TYPE_MISMATCH, "US1.4 nested scalar mid-instance / FR-003");
}
TEST(GroupGetters100, US1_4_NestedScalarMidInstance_Twice) {
    expect_code(nested(header44("D") + kOrd + "453=1\x01" + kParty + "54=1\x01", 453, 0, 447, true),
                FIXPP_ERR_TYPE_MISMATCH, "US1.4 / US1.11");
}
TEST(GroupGetters100, US1_4_NestedScalarLastInInstance_TypeMismatch) {
    expect_code(nested(header44("D") + kOrd + "453=1\x01" + kParty + "54=1\x01", 453, 0, 452),
                FIXPP_ERR_TYPE_MISMATCH, "US1.4 nested scalar last in instance / FR-003");
}
TEST(GroupGetters100, US1_4_NestedScalarLastInInstance_Twice) {
    expect_code(nested(header44("D") + kOrd + "453=1\x01" + kParty + "54=1\x01", 453, 0, 452, true),
                FIXPP_ERR_TYPE_MISMATCH, "US1.4 / US1.11");
}

// ── US1.5 (FR-004): absent, and CheckSum(10) (controls) ─────────────────────────

TEST(GroupGetters100, US1_5_Control_FlatAbsent_TagNotFound) {
    expect_code(flat(header44("D") + kOrd + "54=1\x01", 453), FIXPP_ERR_TAG_NOT_FOUND,
                "US1.5 (control) / FR-004");
}
TEST(GroupGetters100, US1_5_Control_NestedAbsent_TagNotFound) {
    expect_code(nested(header44("D") + kOrd + "453=1\x01" + kParty + "54=1\x01", 453, 0, 802),
                FIXPP_ERR_TAG_NOT_FOUND, "US1.5 nested (control) / FR-004");
}
TEST(GroupGetters100, US1_5_Control_FlatCheckSum_TypeMismatch) {
    expect_code(flat(header44("D") + kOrd + "54=1\x01", 10), FIXPP_ERR_TYPE_MISMATCH,
                "US1.5 CheckSum (control)");
}

// ── US1.7 (FR-002): a lying count ───────────────────────────────────────────────

TEST(GroupGetters100, US1_7_FlatLyingCount_OkZero) {
    expect_ok_empty(flat(header44("D") + kOrd +
                             "453=2\x01"
                             "54=1\x01",
                         453),
                    "US1.7 / FR-002");
}
TEST(GroupGetters100, US1_7_FlatLyingCount_Twice) {
    expect_ok_empty(flat(header44("D") + kOrd +
                             "453=2\x01"
                             "54=1\x01",
                         453, true),
                    "US1.7 / US1.11");
}
TEST(GroupGetters100, US1_7_Control_NestedLyingCount_OkZero) {
    expect_ok_empty(nested(ae_head() + "552=1\x01"
                                       "54=1\x01"
                                       "453=2\x01"
                                       "37=O1\x01",
                           552, 0, 453),
                    "US1.7 nested (control, contract)");
}

// ── US1.8 (FR-003): NoPartySubIDs(802) outside its NoPartyIDs(453) parent ────────

TEST(GroupGetters100, US1_8_Control_Flat802AtRootValueZero_TypeMismatch) {
    expect_code(flat(header44("D") + kOrd +
                         "802=0\x01"
                         "523=S\x01"
                         "54=1\x01",
                     802),
                FIXPP_ERR_TYPE_MISMATCH, "US1.8 value 0 (control) / FR-003");
}
TEST(GroupGetters100, US1_8_Flat802AtRootValueN_TypeMismatch) {
    expect_code(flat(header44("D") + kOrd +
                         "802=2\x01"
                         "523=S\x01"
                         "803=1\x01"
                         "523=T\x01"
                         "803=2\x01"
                         "54=1\x01",
                     802),
                FIXPP_ERR_TYPE_MISMATCH, "US1.8 value n / FR-003");
}
TEST(GroupGetters100, US1_8_Flat802AtRootValueN_Twice) {
    expect_code(flat(header44("D") + kOrd +
                         "802=2\x01"
                         "523=S\x01"
                         "803=1\x01"
                         "523=T\x01"
                         "803=2\x01"
                         "54=1\x01",
                     802, true),
                FIXPP_ERR_TYPE_MISMATCH, "US1.8 / US1.11");
}
// 802 with value n inside a 453 instance, read with the flat getter: the root table indexes it,
// so it is present; it is not registered at the message root.
TEST(GroupGetters100, US1_8_Flat802InsideA453Instance_TypeMismatch) {
    expect_code(flat(header44("D") + kOrd + "453=1\x01" + kParty +
                         "802=1\x01"
                         "523=S\x01"
                         "803=1\x01"
                         "54=1\x01",
                     802),
                FIXPP_ERR_TYPE_MISMATCH, "US1.8 802 inside 453, flat getter / FR-003");
}
TEST(GroupGetters100, US1_8_Flat802InsideA453Instance_Twice) {
    expect_code(flat(header44("D") + kOrd + "453=1\x01" + kParty +
                         "802=1\x01"
                         "523=S\x01"
                         "803=1\x01"
                         "54=1\x01",
                     802, true),
                FIXPP_ERR_TYPE_MISMATCH, "US1.8 / US1.11");
}

// ── US1.9 (FR-003): a body group in a context its dictionary does not declare ────

TEST(GroupGetters100, US1_9_Flat453InDeclaredMessageWithoutParties_TypeMismatch) {
    expect_code(flat(header44("B") +
                         "148=H\x01"
                         "453=1\x01" +
                         kParty + "58=x\x01",
                     453),
                FIXPP_ERR_TYPE_MISMATCH, "US1.9 News(B) / FR-003");
}
TEST(GroupGetters100, US1_9_Flat453InDeclaredMessageWithoutParties_Twice) {
    expect_code(flat(header44("B") +
                         "148=H\x01"
                         "453=1\x01" +
                         kParty + "58=x\x01",
                     453, true),
                FIXPP_ERR_TYPE_MISMATCH, "US1.9 / US1.11");
}
TEST(GroupGetters100, US1_9_FlatBodyGroupInUndeclaredMsgType_TypeMismatch) {
    expect_code(flat(header44("ZZ") + "453=1\x01" + kParty + "58=x\x01", 453),
                FIXPP_ERR_TYPE_MISMATCH, "US1.9 undeclared MsgType / FR-003");
}
TEST(GroupGetters100, US1_9_FlatBodyGroupInUndeclaredMsgType_Twice) {
    expect_code(flat(header44("ZZ") + "453=1\x01" + kParty + "58=x\x01", 453, true),
                FIXPP_ERR_TYPE_MISMATCH, "US1.9 / US1.11");
}

// ── US1.10 (FR-001, research D2 section arm): NoHops(627) in an undeclared MsgType ──

TEST(GroupGetters100, US1_10_Control_FlatNoHopsUndeclaredMsgType_Cursor) {
    expect_cursor(flat(header44("ZZ") + "627=1\x01"
                                        "628=H1\x01"
                                        "629=20240101-00:00:00\x01"
                                        "58=x\x01",
                       627),
                  1, "US1.10 n >= 1 (control)");
}
TEST(GroupGetters100, US1_10_FlatNoHopsZeroUndeclaredMsgType_OkZero) {
    expect_ok_empty(flat(header44("ZZ") + "627=0\x01"
                                          "58=x\x01",
                         627),
                    "US1.10 count 0 (D9 (ii-0h)) / FR-002");
}
TEST(GroupGetters100, US1_10_FlatNoHopsZeroUndeclaredMsgType_Twice) {
    expect_ok_empty(flat(header44("ZZ") + "627=0\x01"
                                          "58=x\x01",
                         627, true),
                    "US1.10 / US1.11");
}

// ── contract "Present" / research D11 step 2: a registered count tag read in a context
// it has no record for (802 from a NoSides(552) instance, inside that instance's Parties) ──

std::string const kAeRecordMiss =
    "552=1\x01"
    "54=1\x01"
    "37=O1\x01"
    "453=1\x01"
    "448=PA\x01"
    "447=D\x01"
    "452=1\x01"
    "802=1\x01"
    "523=S\x01"
    "803=1\x01";
TEST(GroupGetters100, NestedRecordMiss802From552Instance_TypeMismatch) {
    expect_code(nested(ae_head() + kAeRecordMiss, 552, 0, 802), FIXPP_ERR_TYPE_MISMATCH,
                "nested record miss / FR-003");
}
TEST(GroupGetters100, NestedRecordMiss802From552Instance_Twice) {
    expect_code(nested(ae_head() + kAeRecordMiss, 552, 0, 802, true), FIXPP_ERR_TYPE_MISMATCH,
                "nested record miss / US1.11");
}

// ── US1.11 controls: a scalar read twice on each getter ─────────────────────────

TEST(GroupGetters100, US1_11_Control_FlatScalarTwice_TypeMismatch) {
    expect_code(flat(header44("D") + kOrd +
                         "38=100\x01"
                         "54=1\x01",
                     38, true),
                FIXPP_ERR_TYPE_MISMATCH, "US1.11 flat scalar (control)");
}
TEST(GroupGetters100, US1_11_NestedScalarTwice_TypeMismatch) {
    expect_code(nested(header44("D") + kOrd + "453=1\x01" + kParty + "54=1\x01", 453, 0, 448, true),
                FIXPP_ERR_TYPE_MISMATCH, "US1.11 nested scalar");
}

// ── SC-004: the validator and the read agree on a zero-count group registered in its exact
// context ─────────────────────────────────────────────────────────────────────────

TEST(GroupGetters100, SC004_ValidatorAcceptsAndFlatGetterReadsOkZero) {
    std::string const body = header44("D") + kOrd +
                             "21=1\x01"
                             "453=0\x01"
                             "55=IBM\x01"
                             "54=1\x01"
                             "60=20240101-00:00:00\x01"
                             "38=100\x01"
                             "40=1\x01";
    parsed44 const p{body};
    ASSERT_TRUE(p.mv.has_value()) << "setup: the frame did not parse";
    fixpp::wire::dictionary_driven_validator const v{fix44().dict.as_table_view()};
    std::pmr::monotonic_buffer_resource scratch;
    std::uint16_t ref_tag = 0;
    auto const vr = v.validate(*p.mv, &scratch, &ref_tag);
    EXPECT_TRUE(vr.has_value()) << "SC-004: the validator must accept the frame; error "
                                << (vr ? 0 : static_cast<int>(vr.error())) << " ref_tag "
                                << ref_tag;
    inbound_handle const h{*p.mv};
    expect_ok_empty(get_group(h.ptr(), 453), "SC-004 flat getter");
}

// ════════════════════════════════════════════════════════════════════════════════
// T012 (US2): exhaustion and limit cells.
//
// Construction: a `test100::stepped_arena` (a fixed buffer as a monotonic_buffer_resource over
// null_memory_resource(), behind a recording resource) is the parse arena. Its room is stepped
// one byte at a time from empty until the read under test completes with no failed request; at
// each room where the parse succeeds, the cell marks the arena after its setup reads, makes the
// read under test (twice, US2.6), and names the first request of the read that failed by its
// size and alignment (`classify_first_failure`). Each cell asserts at every room where its
// targeted allocation is the first to fail, and requires that at least one room did so.
// ════════════════════════════════════════════════════════════════════════════════

using fixpp::test100::alloc_kind;
using fixpp::test100::alloc_kind_name;
using fixpp::test100::classify_first_failure;
using fixpp::test100::stepped_arena;

// Cursor shells and slice arrays must be told apart by size for the classification.
static_assert(sizeof(fixpp_group) != 3U * sizeof(fixpp::wire::group_slice));
static_assert(sizeof(fixpp_group) != fixpp::wire::offset_table_test_access::group_span_size);
static_assert(sizeof(fixpp_group) != fixpp::wire::offset_table_test_access::nested_cache_row_size);
static_assert(3U * sizeof(fixpp::wire::group_slice) !=
              fixpp::wire::offset_table_test_access::group_span_size);
static_assert(3U * sizeof(fixpp::wire::group_slice) !=
              fixpp::wire::offset_table_test_access::nested_cache_row_size);

constexpr std::size_t kMaxRoom = 1U << 16U;

struct step_obs {
    std::size_t room = 0;
    alloc_kind kind = alloc_kind::none;
    bool shell = false;  // the first failed request was a C cursor shell
    read_out first;
    read_out repeat;
};

// Steps the arena for `body` (parsed with `tv` and `cfg`). `setup` runs before the mark and
// returns false when the room is too small for it; `read` is the read under test.
template <class Setup, class Read>
std::vector<step_obs> step_rooms(std::string const& body, fixpp::dict::table_view const& tv,
                                 fixpp::wire::OffsetTable::Config cfg, std::size_t n_slices,
                                 Setup setup, Read read) {
    std::vector<step_obs> out;
    bool const framed = fixpp::test100::step_parse_rooms(
        body, tv, cfg, kMaxRoom, [&](std::size_t room, stepped_arena& arena, auto const& mv) {
            inbound_handle const h{mv};
            fixpp_group_t const* g = nullptr;
            if (!setup(h, g)) {
                return false;
            }
            arena.rec().mark();
            step_obs o;
            o.room = room;
            o.first = read(h, g);
            auto const c = classify_first_failure(arena.rec(), n_slices, sizeof(fixpp_group));
            o.kind = c.kind;
            o.shell = c.is_extra;
            o.repeat = read(h, g);
            bool const complete = !arena.rec().first_failure().has_value();
            out.push_back(o);
            return complete;
        });
    if (!framed) {
        ADD_FAILURE() << "setup: frame";
    }
    return out;
}

bool no_setup(inbound_handle const& /*h*/, fixpp_group_t const*& /*g*/) { return true; }

// The outer group's cursor, read before the mark.
auto outer_setup(std::uint16_t outer_tag, std::size_t want) {
    return [outer_tag, want](inbound_handle const& h, fixpp_group_t const*& g) {
        auto const o = get_group(h.ptr(), outer_tag);
        if (o.rc != FIXPP_ERR_OK || o.g == nullptr) {
            return false;
        }
        EXPECT_EQ(o.count, want) << "setup: the outer group's instance count";
        g = o.g;
        return true;
    };
}

auto flat_read(std::uint16_t tag) {
    return [tag](inbound_handle const& h, fixpp_group_t const* /*g*/) {
        return get_group(h.ptr(), tag);
    };
}
auto nested_read(std::size_t i, std::uint16_t tag) {
    return [i, tag](inbound_handle const& /*h*/, fixpp_group_t const* g) {
        return get_nested(g, i, tag);
    };
}

// Asserts `code` on both reads at every room whose first failure is `kind`; returns how many
// rooms that was.
std::size_t expect_at_kind(std::vector<step_obs> const& obs, alloc_kind kind, int code,
                           std::string const& what) {
    std::size_t hits = 0;
    for (auto const& o : obs) {
        if (o.shell || o.kind != kind) {
            continue;
        }
        ++hits;
        EXPECT_EQ(o.first.rc, code)
            << what << " (first read, room " << o.room << ", " << alloc_kind_name(kind)
            << " failed): expected " << rc_name(code) << "; actual " << rc_name(o.first.rc);
        EXPECT_EQ(o.repeat.rc, code)
            << what << " (repeat read, room " << o.room << ", " << alloc_kind_name(kind)
            << " failed first): expected " << rc_name(code) << "; actual " << rc_name(o.repeat.rc);
        if (o.first.rc != code) {
            break;  // one room's report is enough to read the cell
        }
    }
    return hits;
}

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

// ── US2.2 (FR-005): a top-level group whose slice allocation fails ───────────────
TEST(GroupGetters100, US2_2_FlatSliceAllocationFails_WireLimitExceeded) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs = step_rooms(order3(), fix44().tv, {}, 3, no_setup, flat_read(453));
    EXPECT_GT(
        expect_at_kind(obs, alloc_kind::slices, FIXPP_ERR_WIRE_LIMIT_EXCEEDED, "US2.2 / FR-005"),
        0U)
        << "setup: no room made the slice allocation the first to fail";
}

// ── US2.4 (FR-007): an ample arena, a group read many times (control) ────────────
TEST(GroupGetters100, US2_4_Control_AmpleArenaRepeatedReadsAllOk) {
    parsed44 const p{order3()};
    ASSERT_TRUE(p.mv.has_value());
    inbound_handle const h{*p.mv};
    for (int i = 0; i < 64; ++i) {
        expect_cursor(get_group(h.ptr(), 453), 3, "US2.4 (control) read " + std::to_string(i));
    }
}

// ── US2.5 (FR-004): reads that need no memory keep their answer on an exhausted arena ──
TEST(GroupGetters100, US2_5_Control_FlatAbsentOnExhaustedArena_TagNotFound) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs = step_rooms(order3(), fix44().tv, {}, 3, no_setup, flat_read(802));
    ASSERT_FALSE(obs.empty());
    for (auto const& o : obs) {
        EXPECT_EQ(o.first.rc, FIXPP_ERR_TAG_NOT_FOUND)
            << "room " << o.room << ": " << rc_name(o.first.rc);
        EXPECT_EQ(o.repeat.rc, FIXPP_ERR_TAG_NOT_FOUND) << "room " << o.room;
    }
}
TEST(GroupGetters100, US2_5_Control_FlatPresentScalarOnExhaustedArena_TypeMismatch) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs = step_rooms(order3(), fix44().tv, {}, 3, no_setup, flat_read(54));
    std::size_t row_hits = 0;
    for (auto const& o : obs) {
        row_hits += (o.kind == alloc_kind::group_row) ? 1U : 0U;
        EXPECT_EQ(o.first.rc, FIXPP_ERR_TYPE_MISMATCH)
            << "room " << o.room << " (" << alloc_kind_name(o.kind) << "): " << rc_name(o.first.rc);
        EXPECT_EQ(o.repeat.rc, FIXPP_ERR_TYPE_MISMATCH) << "room " << o.room;
    }
    EXPECT_GT(row_hits, 0U) << "setup: no room made the group_index_ row the first to fail";
}

// Nested reads from instance 0 of a three-instance NoPartyIDs(453): an absent tag, and a
// present scalar, under mode (a) (no sub-table shell) and mode (c) (sub-build out of memory).
TEST(GroupGetters100, US2_5_NestedAbsentModeA_TagNotFound) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs =
        step_rooms(order3(), fix44().tv, {}, 0, outer_setup(453, 3), nested_read(0, 802));
    EXPECT_GT(expect_at_kind(obs, alloc_kind::subtable, FIXPP_ERR_TAG_NOT_FOUND,
                             "US2.5 nested absent, mode (a) / FR-004"),
              0U)
        << "setup: no room made the sub-table shell the first to fail";
}
TEST(GroupGetters100, US2_5_NestedAbsentModeC_TagNotFound) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs =
        step_rooms(order3(), fix44().tv, {}, 0, outer_setup(453, 3), nested_read(0, 802));
    EXPECT_GT(expect_at_kind(obs, alloc_kind::subtable_build, FIXPP_ERR_TAG_NOT_FOUND,
                             "US2.5 nested absent, mode (c) / FR-004"),
              0U)
        << "setup: no room made the sub-table build the first to fail";
}
TEST(GroupGetters100, US2_5_NestedPresentScalarModeA_TypeMismatch) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs =
        step_rooms(order3(), fix44().tv, {}, 0, outer_setup(453, 3), nested_read(0, 447));
    EXPECT_GT(expect_at_kind(obs, alloc_kind::subtable, FIXPP_ERR_TYPE_MISMATCH,
                             "US2.5 nested present scalar, mode (a) / FR-003"),
              0U)
        << "setup: no room made the sub-table shell the first to fail";
}
TEST(GroupGetters100, US2_5_NestedPresentScalarModeC_TypeMismatch) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs =
        step_rooms(order3(), fix44().tv, {}, 0, outer_setup(453, 3), nested_read(0, 447));
    EXPECT_GT(expect_at_kind(obs, alloc_kind::subtable_build, FIXPP_ERR_TYPE_MISMATCH,
                             "US2.5 nested present scalar, mode (c) / FR-003"),
              0U)
        << "setup: no room made the sub-table build the first to fail";
}
// T010's record-miss construction under mode (a): registration answers "not registered", and
// the presence probe, which walks nested instances, finds the tag.
TEST(GroupGetters100, US2_5_NestedRecordMissModeA_TypeMismatch) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs = step_rooms(ae_head() + kAeRecordMiss, fix44().tv, {}, 0, outer_setup(552, 1),
                                nested_read(0, 802));
    EXPECT_GT(expect_at_kind(obs, alloc_kind::subtable, FIXPP_ERR_TYPE_MISMATCH,
                             "US2.5 nested record miss, mode (a) / FR-003"),
              0U)
        << "setup: no room made the sub-table shell the first to fail";
}

// ── D5 top level (FR-011): an instance past the per-instance cap ───────────────────
using fixpp::test100::kPastInstanceCap;
using fixpp::test100::oversized_instance;

TEST(GroupGetters100, D5_FlatInstancePastPerInstanceCap_WireLimitExceededTwice) {
    auto const buf = frame44(oversized_instance(kPastInstanceCap, ""));
    auto const fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{fix44().tv};
    auto const mv = parser.parse(
        *fv, &arena, fixpp::wire::OffsetTable::Config{.max_offset_entries = 4U * kPastInstanceCap});
    ASSERT_TRUE(mv.has_value()) << "setup: the raised entry cap admits the frame";
    inbound_handle const h{*mv};
    expect_code(get_group(h.ptr(), 453), FIXPP_ERR_WIRE_LIMIT_EXCEEDED, "D5 first read / FR-011");
    expect_code(get_group(h.ptr(), 453), FIXPP_ERR_WIRE_LIMIT_EXCEEDED, "D5 repeat read / FR-011");
}

// ── D7 (i) (FR-011): a nested sub-table over its entry cap ─────────────────────────
// Raised root caps admit an instance larger than the sub-table's entry cap (the default
// Config); the instance carries a nested NoPartySubIDs(802) group and the scalar 447.
fixpp::wire::OffsetTable::Config raised_caps() {
    return {.max_offset_entries = 4U * kPastInstanceCap,
            .max_group_entries_per_instance = 4U * kPastInstanceCap};
}
read_out d7_nested(std::uint16_t tag, bool repeat) {
    static std::vector<std::byte> const buf = frame44(oversized_instance(kPastInstanceCap,
                                                                         "802=1\x01"
                                                                         "523=S\x01"
                                                                         "803=1\x01"));
    auto const fv = fixpp::wire::test::make_frame_view(buf);
    if (!fv) {
        ADD_FAILURE() << "setup: frame";
        return {};
    }
    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{fix44().tv};
    auto const mv = parser.parse(*fv, &arena, raised_caps());
    if (!mv) {
        ADD_FAILURE() << "setup: the raised caps admit the frame";
        return {};
    }
    inbound_handle const h{*mv};
    auto const outer = get_group(h.ptr(), 453);
    if (outer.rc != FIXPP_ERR_OK || outer.count != 1U) {
        ADD_FAILURE() << "setup: the outer read with raised caps: " << rc_name(outer.rc);
        return {};
    }
    auto const r1 = get_nested(outer.g, 0, tag);
    if (!repeat) {
        return r1;
    }
    return get_nested(outer.g, 0, tag);
}
TEST(GroupGetters100, D7i_NestedSubTableOverEntryCap_WireLimitExceeded) {
    expect_code(d7_nested(802, false), FIXPP_ERR_WIRE_LIMIT_EXCEEDED, "D7 (i) first / FR-011");
}
TEST(GroupGetters100, D7i_NestedSubTableOverEntryCap_RepeatWireLimitExceeded) {
    expect_code(d7_nested(802, true), FIXPP_ERR_WIRE_LIMIT_EXCEEDED, "D7 (i) repeat / FR-011");
}
// US2.5's D7 origin: a present scalar in the same instance.
TEST(GroupGetters100, US2_5_NestedPresentScalarD7_TypeMismatch) {
    expect_code(d7_nested(447, false), FIXPP_ERR_TYPE_MISMATCH, "US2.5 D7 present scalar first");
    expect_code(d7_nested(447, true), FIXPP_ERR_TYPE_MISMATCH, "US2.5 D7 present scalar repeat");
}

// ── D10 (FR-007): the group cache records only completed reads ─────────────────────
// A lane where no room separates the two allocations records the cell as skipped.
TEST(GroupGetters100, D10_FlatRowPushFails_WireLimitExceededTwice) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs = step_rooms(order3(), fix44().tv, {}, 3, no_setup, flat_read(453));
    if (expect_at_kind(obs, alloc_kind::group_row, FIXPP_ERR_WIRE_LIMIT_EXCEEDED,
                       "D10 flat row push / FR-007") == 0U) {
        GTEST_SKIP() << "no room makes the group_index_ row the first failure on this lane";
    }
}
TEST(GroupGetters100, D10_FlatSlicesFailAfterRowFits_WireLimitExceededTwice) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto const obs = step_rooms(order3(), fix44().tv, {}, 3, no_setup, flat_read(453));
    if (expect_at_kind(obs, alloc_kind::slices, FIXPP_ERR_WIRE_LIMIT_EXCEEDED,
                       "D10 flat slices / FR-007") == 0U) {
        GTEST_SKIP() << "no room makes the slice array the first failure on this lane";
    }
}
// The nested group holds zero instances, so after a failed nested_cache_ push the sub-table's
// own answer needs only its group_index_ row; a group with instances would need its slice array
// too, which the same exhausted arena refuses, so that construction cannot tell the push's
// failure from the slices'.
TEST(GroupGetters100, D10_NestedRowPushFails_WireLimitExceededTwice) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    std::string const body = header44("D") + kOrd + "453=1\x01" + kParty +
                             "802=0\x01"
                             "54=1\x01";
    auto const obs = step_rooms(body, fix44().tv, {}, 0, outer_setup(453, 1), nested_read(0, 802));
    if (expect_at_kind(obs, alloc_kind::nested_row, FIXPP_ERR_WIRE_LIMIT_EXCEEDED,
                       "D10 nested row push / FR-007") == 0U) {
        GTEST_SKIP() << "no room makes the nested_cache_ row the first failure on this lane";
    }
}

// ── Row 0 (contract rows 0a, 0b) ──────────────────────────────────────────────────
TEST(GroupGetters100, Row0b_NestedNullGroupWritesNullAndZero) {
    auto const r = get_nested(nullptr, 0, 802);
    EXPECT_EQ(r.rc, FIXPP_ERR_NULL_HANDLE) << rc_name(r.rc);
    EXPECT_EQ(r.g, nullptr) << "row 0b: a null g with both outputs non-null writes *out = null";
    EXPECT_EQ(r.count, 0U) << "row 0b: and *count_out = 0";
}
TEST(GroupGetters100, Row0a_Control_OneNullOutPointerLeavesTheOtherUntouched) {
    parsed44 const p{order3()};
    ASSERT_TRUE(p.mv.has_value());
    inbound_handle const h{*p.mv};
    auto const outer = get_group(h.ptr(), 453);
    ASSERT_EQ(outer.rc, FIXPP_ERR_OK);

    std::size_t count = fixpp::test100::kSentinelCount;
    EXPECT_EQ(fixpp_msg_get_group(h.ptr(), 453, nullptr, &count), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(count, fixpp::test100::kSentinelCount) << "flat: count_out untouched";
    fixpp_group_t const* g = fixpp::test100::kSentinelGroup;
    EXPECT_EQ(fixpp_msg_get_group(h.ptr(), 453, &g, nullptr), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(g, fixpp::test100::kSentinelGroup) << "flat: group_out untouched";

    count = fixpp::test100::kSentinelCount;
    EXPECT_EQ(fixpp_group_get_nested_group(outer.g, 0, 802, nullptr, &count),
              FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(count, fixpp::test100::kSentinelCount) << "nested: count_out untouched";
    g = fixpp::test100::kSentinelGroup;
    EXPECT_EQ(fixpp_group_get_nested_group(outer.g, 0, 802, &g, nullptr), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(g, fixpp::test100::kSentinelGroup) << "nested: nested_out untouched";
}

// ── Contract row 3, the malformed-data arm (constructed) ──────────────────────────
// A group whose delimiter is RawData(96) and whose members include RawDataLength(95): the root
// reads the second instance's 96 by the count the first instance's 95 declares, so the value may
// hold an embedded SOH; the sub-table built over the second instance alone has no 95 before
// its 96, splits that value at the SOH, and fails on the byte after it
// (`wire_invalid_field_format`), not for an allocation or a wire limit. The value embeds a
// registered nested count tag (700) before that byte, so the presence probe finds it.
TEST(GroupGetters100, Row3_NestedSubBuildMalformed_TypeMismatch) {
    fixpp::dict::table_view_builder b;
    b.add_valid("U", 35);
    b.add_valid("U", 690);
    b.set_group_first(690, 96);
    b.add_group_member(690, 96);
    b.add_group_member(690, 95);
    b.add_group_member(690, 700);
    b.set_group_first(700, 701);
    b.add_group_member(700, 701);
    fixpp::dict::table_view const tv = std::move(b).build();

    std::string const value = std::string{"a\x01"} + "700=1\x01" + "b";
    std::string const body =
        "35=U\x01"
        "690=2\x01"
        "96=x\x01"
        "95=" +
        std::to_string(value.size()) +
        "\x01"
        "96=" +
        value +
        "\x01"
        "58=t\x01";
    auto const buf = frame44(body);
    auto const fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{tv};
    auto const mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value()) << "setup: the root parses the counted value";
    inbound_handle const h{*mv};
    auto const outer = get_group(h.ptr(), 690);
    ASSERT_EQ(outer.rc, FIXPP_ERR_OK) << rc_name(outer.rc);
    ASSERT_EQ(outer.count, 2U) << "setup: two instances";
    expect_code(get_nested(outer.g, 1, 700), FIXPP_ERR_TYPE_MISMATCH,
                "row 3 malformed-data arm, first");
    expect_code(get_nested(outer.g, 1, 700), FIXPP_ERR_TYPE_MISMATCH,
                "row 3 malformed-data arm, repeat");
}

}  // namespace
