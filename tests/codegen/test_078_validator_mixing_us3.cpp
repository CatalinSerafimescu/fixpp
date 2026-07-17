// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_078_validator_mixing_us3.cpp
//
// 078-precompiled-builder-libs T027 [US3] [TESTS-FIRST]: validator-side
// mixing witness (R2a leg i, at real-lib scale). This TU force-inlines
// validate_Email (via FIXPP_VALIDATORS_HEADER_ONLY_Email, defined BEFORE
// the slim header is included) while linking fixpp::validators::v44 for
// validate_News. Email and News were chosen because BOTH share the exact
// same repeating-group plan, `groups::G_33Args` (v44's NoLinesOfText group:
// each entry carries a REQUIRED Text(58)) -- confirmed by grep over the
// generated fixpp/v44/messages/{Email,News}.hpp: both declare
// `lines_of_text` as a REQUIRED (non-optional) `std::span<const
// groups::G_33Args>`.
//
// The shared-plan trait is `template<> struct writer_traits<G_33Args>`,
// emitted ONCE into fixpp/v44/validators/traits.hpp and #include'd by BOTH
// Email.validator.inl (this TU, force-inlined) and News.validator.cpp
// (compiled into fixpp_validators_v44.a). Per C++ [basic.def.odr], a class
// TYPE definition (unlike a function/variable) may appear identically in
// multiple translation units without needing the `inline` keyword; its
// `static constexpr` array members ARE implicitly `inline` variables
// (C++17), so the linker COMDAT-folds the duplicate definitions rather than
// erroring. If either were NOT true -- e.g. `writer_traits<G_33Args>`
// carried a non-inline static member, or codegen accidentally emitted the
// SAME free-function helpers non-identically across the two .validator.inl
// / .validator.cpp copies -- this binary would fail to LINK with a
// duplicate-symbol error rather than fail an assertion below; the test IS
// the link succeeding, mirroring test_078_odr_sc003_probe.cpp's leg (i),
// now proven against the REAL emitted split instead of a hand-written mock.
//
// Foundational (T002-T019) already landed the real emitter split + the
// per-message FIXPP_VALIDATORS_HEADER_ONLY_<Msg> macro surface, so this
// witness is BORN-GREEN (the property it asserts already holds) --
// feedback_fail_placeholder_red_test: no fake red-first cycle staged here.
//
// Discrimination: a missing-required-Text(58) entry inside `lines_of_text`
// must fail validation for BOTH the force-inlined Email leg and the linked
// News leg -- and the failure must originate from INSIDE the shared group
// plan (not an earlier top-level required check), so every OTHER top-level
// required field is populated. A positive control (same shape, Text(58)
// present) validates clean on both legs, proving the rejection is
// specifically the omitted group-entry field, not a broken/always-failing
// validator (mirrors test_078_validator_linked_us2.cpp's discrimination
// shape). validate_<Msg> returns a RESULT (success/error), not wire bytes
// -- result-identity, not byte-identity, per FR-009/SC-004's Gate-A-round-2
// correction.
//
// Anchors: specs/078-precompiled-builder-libs/spec.md US3 (lines 80-92),
// FR-006/FR-007, SC-004 (validator leg); quickstart.md Scenario 4a;
// tests/codegen/test_078_odr_sc003_probe.cpp (mock ODR precedent, leg i);
// tests/session/test_078_validator_linked_us2.cpp (missing-required-field
// discrimination shape); fixpp/v44/validators/traits.hpp (shared plan
// home).

#define FIXPP_VALIDATORS_HEADER_ONLY_Email
#include <fixpp/v44/messages/Email.hpp>  // force-inlined validate_ body
#undef FIXPP_VALIDATORS_HEADER_ONLY_Email

#include <fixpp/v44/messages/News.hpp>  // declaration only -- resolves from fixpp::validators::v44

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

// Force-inlined leg: validate_Email's body was emitted directly into THIS
// TU. Every top-level required field (EmailType(94)/Subject(147)/
// EmailThreadID(164)) is set so the assertion below discriminates the
// GROUP-level check, not an earlier top-level one.
TEST(ValidatorMixingUS3, ForceInlinedEmail_SharedGroupPlanRequiredCheck) {
    std::array<fixpp::v44::groups::G_33Args, 1> missing_text_line{make_line(false)};
    fixpp::v44::EmailArgs args{};
    args.email_type = 'E';
    args.subject = "US3-SUBJECT";
    args.email_thread_id = "US3-THREAD";
    args.lines_of_text = missing_text_line;

    auto r = fixpp::v44::validate_Email(args);
    ASSERT_FALSE(r.has_value())
        << "Email lines_of_text[0] missing Text(58) must fail via the shared "
           "G_33Args group-plan required check, force-inlined in this TU";
    EXPECT_EQ(r.error(), fixpp::core::error::wire_required_field_missing);

    // Positive control: same shape, Text(58) present -> validates clean.
    std::array<fixpp::v44::groups::G_33Args, 1> present_text_line{make_line(true)};
    args.lines_of_text = present_text_line;
    auto r_ok = fixpp::v44::validate_Email(args);
    EXPECT_TRUE(r_ok.has_value())
        << "with Text(58) present the identical shape must validate clean";
}

// Linked leg: validate_News resolves from fixpp::validators::v44 in THIS
// binary, alongside the force-inlined Email above -- proving the SAME
// shared G_33Args group-plan required check fires correctly under real
// mixing (not just in isolation, as T024/T025 tested it).
TEST(ValidatorMixingUS3, LinkedNews_SharedGroupPlanRequiredCheck) {
    std::array<fixpp::v44::groups::G_33Args, 1> missing_text_line{make_line(false)};
    fixpp::v44::NewsArgs args{};
    args.headline = "US3-HEADLINE";
    args.lines_of_text = missing_text_line;

    auto r = fixpp::v44::validate_News(args);
    ASSERT_FALSE(r.has_value())
        << "News lines_of_text[0] missing Text(58) must fail via the shared "
           "G_33Args group-plan required check, resolved from the linked "
           "fixpp::validators::v44 lib";
    EXPECT_EQ(r.error(), fixpp::core::error::wire_required_field_missing);

    // Positive control: same shape, Text(58) present -> validates clean.
    std::array<fixpp::v44::groups::G_33Args, 1> present_text_line{make_line(true)};
    args.lines_of_text = present_text_line;
    auto r_ok = fixpp::v44::validate_News(args);
    EXPECT_TRUE(r_ok.has_value())
        << "with Text(58) present the identical shape must validate clean";
}
