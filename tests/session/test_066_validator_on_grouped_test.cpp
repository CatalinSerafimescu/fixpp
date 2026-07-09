// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_066_validator_on_grouped_test.cpp
//
// 066-dict-backed-inbound-parse T009 — validator-ON grouped witness (FR-006).
//
// Assessment (recorded here + tasks.md T009): `Session::validate_inbound_`
// (session.cpp `vg_parser`, a locally default-constructed dict-FREE
// `Parser<Index>`) is NOT re-wired to `inbound_tv_`. `dictionary_driven_
// validator::validate()`'s repeating-group structure check (Step 3,
// validator.hpp ~177-278) does NOT rely on the passed MessageView's own
// `opaque_dict_`/group_slices()/consume_group_extent() machinery at all: it
// walks `msg.offsets().entries()` (the flat, dict-INDEPENDENT entry list)
// directly and resolves membership via the validator's OWN held `table_view
// dict_` member (`dict_.group_first_field(...)`, `dict_.group_member_tags(...)`,
// validator.hpp:207/232) — a fully self-contained walk, exactly the
// pre-existing L-063-3 note already inline in validator.hpp documents. So
// `vg_parser`'s dict-backing status is irrelevant to validate()'s own
// group-structure correctness; Option B (record, don't change) applies.
//
// This witness proves the CONSEQUENCE: with `validate_inbound_messages`
// enabled, a group-bearing frame (NoLegs(555) x2 + trailing TransactTime(60))
// (a) is NOT spuriously rejected by the validator's group-count check (it
// reaches fromApp), and (b) the ACTUAL dispatch path (parse_and_dispatch_,
// T006, entirely separate from validate_inbound_) still observes
// membership-bounded extents identical to the validator-OFF case (T004) --
// i.e. turning the validator ON does not regress T006's fix.
//
// Anchors: tasks.md T009; data-model.md; contracts/inbound-parse.md C1/C5;
// include/fixpp/wire/validator.hpp L-063-3 note.

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

// Mirrors test_066_group_membership_red_test.cpp's slice_has_tag helper
// (genuine tag-boundary scan, no substring false-positive).
bool slice_has_tag(fixpp::wire::group_slice const& s, std::uint16_t tag) {
    std::string_view sv{reinterpret_cast<char const*>(s.data), s.len};
    std::string const needle = std::to_string(tag) + "=";
    if (sv.size() >= needle.size() && sv.substr(0, needle.size()) == needle) {
        return true;
    }
    std::string const soh_needle = std::string("\x01") + needle;
    return sv.find(soh_needle) != std::string_view::npos;
}

TEST(ValidatorOnGrouped, TrailingFieldAbsentFromLastInstanceWithValidatorEnabled) {
    GroupDispatchFixture f;
    auto cfg = f.make_cfg();
    cfg.validate_inbound_messages = true;  // opt-in validator (041), T009 focus
    Session sess(f.engine_cfg, cfg);
    f.open_to_active(sess);

    ASSERT_EQ(f.app->from_app_calls, 0) << "no app message dispatched yet";

    std::size_t count = 0;
    bool leg0_has_symbol = false;
    bool leg1_has_symbol = false;
    bool last_has_trailing = true;  // default true: an un-run callback must not silently pass

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

    // (a) The validator's own group-count check (declared NoLegs=2 == actual
    // 2 instances, trailing tag 60 correctly excluded as a non-member) must
    // NOT reject this message -- it must reach fromApp.
    ASSERT_EQ(f.app->from_app_calls, 1)
        << "validator-ON: a well-formed group-bearing ExecutionReport must reach fromApp, "
           "not be spuriously Reject'ed by the group-count check";
    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "validator-ON: session must remain Active (no session Reject/Disconnect)";

    // (b) Non-discriminating sanity: instance count + each leg's own member.
    EXPECT_EQ(count, 2U) << "NoLegs(555)=2 must yield exactly 2 instances";
    EXPECT_TRUE(leg0_has_symbol) << "leg #1's own LegSymbol(600) must be present";
    EXPECT_TRUE(leg1_has_symbol) << "leg #2's own LegSymbol(600) must be present";

    // (c) DISCRIMINATING: identical to the validator-OFF (T004/T006) result --
    // the trailing outer field TransactTime(60) is absent from the last
    // NoLegs(555) instance. Proves validate_inbound_'s dict-free vg_parser
    // does not regress the dispatch path's membership-bounded reads.
    EXPECT_FALSE(last_has_trailing)
        << "validator-ON: trailing TransactTime(60) must NOT be part of the last "
           "NoLegs(555) instance (same as validator-OFF)";
}

}  // namespace
}  // namespace fixpp::session::test066
