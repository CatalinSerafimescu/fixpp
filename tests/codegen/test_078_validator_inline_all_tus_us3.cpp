// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_078_validator_inline_all_tus_us3.cpp
//
// 078-precompiled-builder-libs Gate B RC#3 [US3]: validator-side SAFE-path
// counter-test for the inline-XOR-link contract (spec.md Edge Case ~line
// 128; FR-006/FR-007; quickstart.md Scenario 4d). Twin of
// test_078_builder_inline_all_tus_us3.cpp -- see that file's header comment
// for the full rationale (why the existing mixing tests
// (test_078_builder_mixing_us3.cpp / test_078_validator_mixing_us3.cpp) do
// NOT exercise same-message inline-vs-link mixing, and why this test
// deliberately demonstrates only the SAFE, non-mixed side).
//
// This TU force-inlines validate_Email (FIXPP_VALIDATORS_HEADER_ONLY_Email)
// in BOTH translation units of this 2-TU program -- this file AND
// test_078_validator_inline_all_tus_us3_tu2.cpp. The message is inline
// EVERYWHERE it is referenced in this binary; the test target deliberately
// does not link fixpp::validators::v44 at all, so no archive member for
// validate_Email exists in this program. Both TUs' `inline` definitions of
// validate_Email (and the shared `writer_traits<G_33Args>` class-type
// specialization each pulls in via validators/traits.hpp) must link cleanly
// and both must produce result-identical validation outcomes (same
// success/error and offending tag) for the same Args -- FR-009/SC-004's
// validator equivalence is result-identity, not byte-identity.
//
// Anchors: specs/078-precompiled-builder-libs/spec.md Edge Case (~line 128),
// FR-006/FR-007, SC-004 (validator leg); quickstart.md Scenario 4d;
// tests/codegen/test_078_validator_mixing_us3.cpp (mixed-message precedent,
// does not cover same-message mixing);
// tools/codegen/fixpp-codegen/emit_builders.cpp (emitter contract comment).

#define FIXPP_VALIDATORS_HEADER_ONLY_Email
#include <fixpp/v44/messages/Email.hpp>
#undef FIXPP_VALIDATORS_HEADER_ONLY_Email

#include "test_078_validator_inline_all_tus_us3_support.hpp"

#include <gtest/gtest.h>

#include <array>
#include <fixpp/core/error.hpp>

namespace {

fixpp::v44::groups::G_33Args make_line(bool with_text) {
    fixpp::v44::groups::G_33Args line{};
    if (with_text) {
        line.text = "hello";
    }
    return line;
}

}  // namespace

TEST(ValidatorInlineAllTUsUS3, SafePath_InlineInBothTUs_ResultIdentical) {
    // Negative shape: missing required Text(58) inside the shared group
    // entry -- both TUs must reject with the same error.
    std::array<fixpp::v44::groups::G_33Args, 1> missing_text_line{make_line(false)};
    fixpp::v44::EmailArgs bad_args{};
    bad_args.email_type = 'E';
    bad_args.subject = "US3-RC3-SUBJECT";
    bad_args.email_thread_id = "US3-RC3-THREAD";
    bad_args.lines_of_text = missing_text_line;

    auto r_here = fixpp::v44::validate_Email(bad_args);
    ASSERT_FALSE(r_here.has_value())
        << "force-inlined validate_Email (this TU) must reject the missing "
           "required Text(58) group entry";
    EXPECT_EQ(r_here.error(), fixpp::core::error::wire_required_field_missing);

    auto r_there = fixpp_test_078_inline_all_tus::validate_email_other_tu(bad_args);
    ASSERT_FALSE(r_there.has_value())
        << "force-inlined validate_Email (other TU) must reject the same way";
    EXPECT_EQ(r_there.error(), r_here.error())
        << "two TUs that BOTH force-inline the SAME message must produce "
           "result-identical validation outcomes (safe, non-mixed side of "
           "the inline-XOR-link contract)";

    // Positive control: same shape, Text(58) present -- both TUs accept.
    std::array<fixpp::v44::groups::G_33Args, 1> present_text_line{make_line(true)};
    fixpp::v44::EmailArgs good_args{};
    good_args.email_type = 'E';
    good_args.subject = "US3-RC3-SUBJECT";
    good_args.email_thread_id = "US3-RC3-THREAD";
    good_args.lines_of_text = present_text_line;

    auto ok_here = fixpp::v44::validate_Email(good_args);
    EXPECT_TRUE(ok_here.has_value())
        << "with Text(58) present, force-inlined validate_Email (this TU) "
           "must validate clean";

    auto ok_there = fixpp_test_078_inline_all_tus::validate_email_other_tu(good_args);
    EXPECT_TRUE(ok_there.has_value())
        << "with Text(58) present, force-inlined validate_Email (other TU) "
           "must validate clean";
}
