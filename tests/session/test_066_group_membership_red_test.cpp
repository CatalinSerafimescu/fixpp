// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_066_group_membership_red_test.cpp
//
// 066-dict-backed-inbound-parse T004 — RED-first witness (C++ real
// dispatch). Drives the T001 group-bearing FIX44 ExecutionReport(35=8)
// frames through the real Session dispatch harness
// (tests/session/support/group_dispatch_fixture.hpp) and inspects the
// delivered MessageView's group-membership behavior.
//
// Both assertions below MUST be observed RED against the current
// dict-free positional parse (`Session::parse_and_dispatch_`,
// src/session/session.cpp:316, default `Parser<access_mode::Index>` with
// no dictionary) — this file does NOT flip that site (T006's job).
//
// Anchors: tasks.md T004; spec.md US1 Independent Test; contracts/
// inbound-parse.md C1/C3; research.md Decision 6.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/view.hpp>

#include "support/fix44_group_frame_bodies.hpp"
#include "support/group_dispatch_fixture.hpp"

using fixpp::session::test066::GroupDispatchFixture;

namespace fixpp::session::test066 {
namespace {

// Scan a group_slice's raw bytes for "<tag>=" at a genuine tag boundary —
// the slice start (per offset_table_test.cpp's GroupSliceStartsAtTagEquals
// invariant: a slice always begins at "delim=") or immediately after a SOH
// separator. Never a substring false-positive (e.g. "160=" containing
// "60=").
bool slice_has_tag(fixpp::wire::group_slice const& s, std::uint16_t tag) {
    std::string_view sv{reinterpret_cast<char const*>(s.data), s.len};
    std::string const needle = std::to_string(tag) + "=";
    if (sv.size() >= needle.size() && sv.substr(0, needle.size()) == needle) {
        return true;
    }
    std::string const soh_needle = std::string("\x01") + needle;
    return sv.find(soh_needle) != std::string_view::npos;
}

// (a) trailing outer field TransactTime(60), AFTER NoLegs(555) x2, absorbed
// into the last group instance on the current dict-free positional parse.
TEST(GroupMembershipRed, TrailingFieldAbsentFromLastInstance) {
    GroupDispatchFixture f;
    auto cfg = f.make_cfg();
    Session sess(f.engine_cfg, cfg);
    f.open_to_active(sess);

    ASSERT_EQ(f.app->from_app_calls, 0) << "no app message dispatched yet";

    std::size_t count = 0;
    bool leg0_has_symbol = false;
    bool leg1_has_symbol = false;
    // Default true: if the callback never runs (or never reaches this
    // branch), the discriminating EXPECT_FALSE below must NOT silently pass.
    bool last_has_trailing = true;

    f.app->on_from_app =
        [&](const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg) {
            auto slices = msg.offsets().group_slices(555);
            count = slices.size();
            if (count >= 1) {
                leg0_has_symbol = slice_has_tag(slices[0], 600);
                last_has_trailing = slice_has_tag(slices[count - 1], 60);
            }
            if (count >= 2) {
                leg1_has_symbol = slice_has_tag(slices[1], 600);
            }
        };

    auto suffix = fixpp_test_support::execution_report_two_legs_trailing_suffix();
    auto frame = fixpp_test_support::make_execution_report_frame(suffix, /*seq=*/2, "TW", "ISLD");
    ASSERT_FALSE(frame.empty());

    f.feed(sess, frame);

    ASSERT_EQ(f.app->from_app_calls, 1) << "ExecutionReport must reach fromApp";

    // Non-discriminating sanity checks: instance count and each leg's own
    // declared members read correctly.
    EXPECT_EQ(count, 2U) << "NoLegs(555)=2 must yield exactly 2 instances";
    EXPECT_TRUE(leg0_has_symbol) << "leg #1's own LegSymbol(600) must be present";
    EXPECT_TRUE(leg1_has_symbol) << "leg #2's own LegSymbol(600) must be present";

    // DISCRIMINATING RED assertion: the trailing outer field TransactTime(60)
    // queried on the LAST NoLegs(555) instance must be ABSENT. On the
    // current dict-free positional parse the last instance's extent runs to
    // end-of-message and ABSORBS tag 60 — so this is RED pre-066/T006.
    EXPECT_FALSE(last_has_trailing)
        << "trailing TransactTime(60) must NOT be part of the last NoLegs(555) instance";
}

// (b) interior-truncation: an undeclared tag (9999) INTERIOR to leg entry
// #1, between its declared LegSymbol(600) and LegSide(624) members. On the
// current dict-free/permissive parse entry #1 is NOT truncated at 9999, so
// LegSide(624) remains present in entry #1.
TEST(GroupMembershipRed, InteriorUndeclaredTagTruncatesInstance) {
    GroupDispatchFixture f;
    auto cfg = f.make_cfg();
    Session sess(f.engine_cfg, cfg);
    f.open_to_active(sess);

    std::size_t count = 0;
    bool entry0_has_symbol = false;
    // Default true: guard against a silently-skipped callback masking the
    // discriminating EXPECT_FALSE below.
    bool entry0_has_side = true;

    f.app->on_from_app =
        [&](const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg) {
            auto slices = msg.offsets().group_slices(555);
            count = slices.size();
            if (count >= 1) {
                entry0_has_symbol = slice_has_tag(slices[0], 600);
                entry0_has_side = slice_has_tag(slices[0], 624);
            }
        };

    auto suffix = fixpp_test_support::execution_report_interior_undeclared_tag_suffix();
    auto frame = fixpp_test_support::make_execution_report_frame(suffix, /*seq=*/2, "TW", "ISLD");
    ASSERT_FALSE(frame.empty());

    f.feed(sess, frame);

    ASSERT_EQ(f.app->from_app_calls, 1)
        << "ExecutionReport (interior-undeclared variant) must reach fromApp";
    EXPECT_TRUE(entry0_has_symbol) << "leg #1's own declared LegSymbol(600) must be present";

    // DISCRIMINATING RED assertion: LegSide(624), declared AFTER the
    // undeclared interior tag 9999 in leg entry #1, must be ABSENT from
    // entry #1 (FR-008/C3 permissive->strict: an unknown interior field
    // terminates the instance). On the current dict-free/permissive parse
    // entry #1 continues past 9999 and 624 IS present — RED pre-066/T006.
    EXPECT_FALSE(entry0_has_side)
        << "LegSide(624), declared after the undeclared interior tag 9999, "
           "must be absent from truncated leg entry #1";
}

}  // namespace
}  // namespace fixpp::session::test066
