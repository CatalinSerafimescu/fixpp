// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_066_scalar_as_group_test.cpp
//
// 066-dict-backed-inbound-parse T010 — scalar-as-group witness (C++ real
// dispatch). Drives the T001 group-bearing FIX44 ExecutionReport(35=8) frame
// (tests/support/fix44_group_frame_bodies.hpp) through the real Session
// dispatch harness (tests/session/support/group_dispatch_fixture.hpp) and
// queries a plain SCALAR tag — Symbol(55), `<field number='55' name='Symbol'
// type='STRING' />` at dictionaries/FIX44.xml:4028, NOT a NUMINGROUP count
// field — as if it were a repeating group.
//
// Mirrors the C-ABI thunk's own discrimination
// (src/capi/message_read.cpp:349-361): `offsets().find(55)` must find the
// tag present (it is, mid-body), while `offsets().group_slices(55)` must be
// EMPTY (present-but-not-a-group => TYPE_MISMATCH at the C-ABI layer). This
// is the distinct documented result C2 describes at the OffsetTable level.
//
// GREEN today (T006 already dict-backs the parse site). RED-first is proven
// by TEMPORARY mutation of src/session/session.cpp:316 (not committed) per
// the phase-implementer brief — this file is unconditionally the same in
// both configurations.
//
// Anchors: tasks.md T010; spec.md US2 Independent Test / SC-002;
// contracts/inbound-parse.md C2; research.md Decision 6.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

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

TEST(ScalarAsGroup, SymbolTagQueriedAsGroupIsNotASpuriousInstance) {
    GroupDispatchFixture f;
    auto cfg = f.make_cfg();
    Session sess(f.engine_cfg, cfg);
    f.open_to_active(sess);

    ASSERT_EQ(f.app->from_app_calls, 0) << "no app message dispatched yet";

    bool tag_present = false;
    // Default true: if the callback never runs (or never reaches this
    // branch), the discriminating EXPECT_TRUE(...empty()) below must NOT
    // silently pass on a stale false-negative slices span.
    bool slices_nonempty = true;
    std::size_t slice_count = 999;

    f.app->on_from_app =
        [&](const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg) {
            const auto& offsets = msg.offsets();
            tag_present = static_cast<bool>(offsets.find(55));
            auto slices = offsets.group_slices(55);
            slice_count = slices.size();
            slices_nonempty = !slices.empty();
        };

    auto suffix = fixpp_test_support::execution_report_two_legs_trailing_suffix();
    auto frame = fixpp_test_support::make_execution_report_frame(suffix, /*seq=*/2, "TW", "ISLD");
    ASSERT_FALSE(frame.empty());

    f.feed(sess, frame);

    ASSERT_EQ(f.app->from_app_calls, 1) << "ExecutionReport must reach fromApp";

    // Non-discriminating sanity: Symbol(55) is present as a scalar field.
    EXPECT_TRUE(tag_present) << "Symbol(55) must be present in the delivered message";

    // DISCRIMINATING assertion (C2 / SC-002): Symbol(55) is a plain scalar
    // (FIX44.xml:4028, type='STRING'), never a NUMINGROUP count field.
    // group_slices(55) must be EMPTY — the OffsetTable-level signal the
    // C-ABI thunk maps to FIXPP_ERR_TYPE_MISMATCH — NOT a bogus 1-instance
    // span running to end-of-message (the dict-free-parse symptom).
    EXPECT_FALSE(slices_nonempty)
        << "querying scalar tag Symbol(55) as a group must yield an EMPTY "
           "group_slices() span (TYPE_MISMATCH), not a spurious instance; got count="
        << slice_count;
}

}  // namespace
}  // namespace fixpp::session::test066
