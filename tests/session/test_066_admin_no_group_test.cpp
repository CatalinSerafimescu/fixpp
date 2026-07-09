// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_066_admin_no_group_test.cpp
//
// 066-dict-backed-inbound-parse T012 — admin / no-group witness (SC-003/FR-005).
//
// FR-005: "Non-group traffic and admin messages MUST be behaviorally
// unchanged, because membership is consulted LAZILY (only on a group read)
// and admin messages carry no read repeating groups — not because admin
// bypasses the change." This file proves two things:
//
// (a) FUNCTIONAL CORROBORATION (non-discriminating alone): an admin message
//     (inbound Heartbeat) and a no-group app message (inbound NewOrderSingle,
//     no repeating groups referenced) dispatch correctly through the SAME
//     dict-backed `parse_and_dispatch_` site (session.cpp:316-330, T006) and
//     read their scalar fields correctly — proving 066 did not regress
//     admin/no-group behavior. "Dispatches fine" alone does NOT discriminate
//     lazy-vs-eager membership consultation (it would pass either way) — see
//     (b) below for the actual structural laziness proof.
//
// (b) STRUCTURAL LAZINESS PROOF (the actual discriminator, code-cited, not a
//     test): `group_member_fn_` (the membership predicate the dict-backed
//     `Parser` ctor installs, parser.hpp:566-585) is invoked ONLY from three
//     call sites, all inside `OffsetTable::consume_group_extent` /
//     `OffsetTable::group` (src/wire/offset_table.cpp — confirmed by census,
//     grep `group_member_fn_(` across src/wire/offset_table.cpp):
//       - offset_table.cpp:448  (consume_group_extent: confirm count field
//         heads a group in-context)
//       - offset_table.cpp:469  (consume_group_extent: is a scanned entry a
//         member of the CURRENT group)
//       - offset_table.cpp:477  (consume_group_extent: nested-descent trigger
//         check)
//       - offset_table.cpp:532  (group(): confirm `no_tag` is a real group
//         count field before consuming its extent)
//     `OffsetTable::group()`/`group_slices()` are themselves invoked ONLY from
//     a caller doing an EXPLICIT group read (`MessageView::offsets().group_
//     slices(tag)`, the typed `group<>()` accessor, or the C-ABI
//     `fixpp_msg_get_group`/`fixpp_group_get_field_*` thunks) — never from
//     scalar `get(tag)`/`find(tag)` field lookups (those walk `entries_`
//     directly, dict-independent). So a callback that never calls a group
//     accessor triggers ZERO membership-predicate invocations, by
//     construction — this is a structural fact about the 062/063 machinery
//     066 reuses unchanged (066 only changed WHICH `table_view`/`Parser`
//     feeds the root `OffsetTable`, not this call-site structure), not
//     something a runtime test can independently "prove" without
//     instrumenting the private function pointer. Test (c) below corroborates
//     this at the callback-authoring level: a GROUP-BEARING message whose
//     callback reads ONLY scalar fields dispatches identically to a no-group
//     message (no group accessor is ever invoked in either case).
//
// Anchors: tasks.md T012; spec.md FR-005; contracts/inbound-parse.md C5;
// research.md Decision 1 (root-cause census of `Parser<...>` construction
// sites) / Decision 6 (real-dispatch witness discipline).

#include <gtest/gtest.h>

#include <cstddef>
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

// Field-value scan (genuine tag boundary, same discipline as slice_has_tag),
// for reading a scalar top-level field out of the raw wire bytes when we only
// have the callback's captured MessageView (no group involved).
bool msg_has_tag_value(fixpp::wire::MessageView<fixpp::wire::access_mode::Index> const& msg,
                       std::uint16_t tag, std::string_view expected) {
    auto v = msg.get(tag);
    return v.has_value() && v->as_string() == expected;
}

// ── (a1) Admin: inbound Heartbeat(35=0) dispatches via fromAdmin, scalar
// TestReqID(112) reads correctly. ───────────────────────────────────────────
TEST(AdminNoGroupWitness, InboundHeartbeatDispatchesAndReadsScalarCorrectly) {
    GroupDispatchFixture f;
    auto cfg = f.make_cfg();
    Session sess(f.engine_cfg, cfg);
    f.open_to_active(sess);

    bool testreqid_ok = false;
    f.app->on_from_admin =
        [&](const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg) {
            testreqid_ok = msg_has_tag_value(msg, 112, "TR-ADMIN-1");
        };

    std::string body =
        "35=0\x01"
        "34=2\x01"
        "49=TW\x01"
        "52=20240101-00:00:00.000\x01"
        "56=ISLD\x01"
        "112=TR-ADMIN-1\x01";
    auto frame = fixpp_test_support::make_frame("FIX.4.4", body);

    f.feed(sess, frame);

    ASSERT_EQ(f.app->from_admin_calls, 1) << "inbound Heartbeat must reach fromAdmin";
    EXPECT_TRUE(testreqid_ok) << "TestReqID(112) scalar field must read correctly";
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "admin dispatch must not disturb session state";
}

// ── (a2) No-group app message: inbound NewOrderSingle(35=D), NO repeating
// group referenced anywhere in the message or the callback, dispatches via
// fromApp and reads its scalar fields correctly. ────────────────────────────
TEST(AdminNoGroupWitness, NoGroupNewOrderSingleDispatchesAndReadsScalarsCorrectly) {
    GroupDispatchFixture f;
    auto cfg = f.make_cfg();
    Session sess(f.engine_cfg, cfg);
    f.open_to_active(sess);

    bool cl_ord_id_ok = false;
    bool symbol_ok = false;
    bool side_ok = false;

    f.app->on_from_app =
        [&](const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg) {
            cl_ord_id_ok = msg_has_tag_value(msg, 11, "CLORD-NOGRP-1");
            symbol_ok = msg_has_tag_value(msg, 55, "AAPL");
            side_ok = msg_has_tag_value(msg, 54, "1");
        };

    std::string body =
        "35=D\x01"
        "34=2\x01"
        "49=TW\x01"
        "52=20240101-00:00:00.000\x01"
        "56=ISLD\x01"
        "11=CLORD-NOGRP-1\x01"
        "55=AAPL\x01"
        "54=1\x01"
        "40=2\x01"
        "38=100\x01"
        "59=0\x01";
    auto frame = fixpp_test_support::make_frame("FIX.4.4", body);

    f.feed(sess, frame);

    ASSERT_EQ(f.app->from_app_calls, 1) << "no-group NewOrderSingle must reach fromApp";
    EXPECT_TRUE(cl_ord_id_ok) << "ClOrdID(11) scalar field must read correctly";
    EXPECT_TRUE(symbol_ok) << "Symbol(55) scalar field must read correctly";
    EXPECT_TRUE(side_ok) << "Side(54) scalar field must read correctly";
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "no-group app dispatch must not disturb session state";
}

// ── (c) A GROUP-BEARING message whose callback never touches a group
// accessor dispatches identically to a no-group message — corroborates (b)'s
// structural laziness argument at the callback-authoring level: the callback
// below performs zero calls to `.offsets().group_slices()`/`group<>()`, so
// (per (b)'s census) the membership predicate is invoked zero times for this
// dispatch, regardless of the fact that NoLegs(555) is present on the wire.
// ────────────────────────────────────────────────────────────────────────────
TEST(AdminNoGroupWitness, GroupBearingMessageWithUnreadGroupDispatchesLikeNoGroupMessage) {
    GroupDispatchFixture f;
    auto cfg = f.make_cfg();
    Session sess(f.engine_cfg, cfg);
    f.open_to_active(sess);

    bool order_id_ok = false;
    f.app->on_from_app =
        [&](const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg) {
            // Deliberately reads ONLY a scalar field — never NoLegs(555).
            order_id_ok = msg_has_tag_value(msg, 37, "ORDID-1");
        };

    auto suffix = fixpp_test_support::execution_report_two_legs_trailing_suffix();
    auto frame = fixpp_test_support::make_execution_report_frame(suffix, /*seq=*/2, "TW", "ISLD");
    ASSERT_FALSE(frame.empty());

    f.feed(sess, frame);

    ASSERT_EQ(f.app->from_app_calls, 1)
        << "group-bearing ExecutionReport must still reach fromApp when the "
           "callback never reads the group";
    EXPECT_TRUE(order_id_ok) << "OrderID(37) scalar field must read correctly, "
                                "unaffected by the unread NoLegs(555) group";
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active);
}

}  // namespace
}  // namespace fixpp::session::test066
