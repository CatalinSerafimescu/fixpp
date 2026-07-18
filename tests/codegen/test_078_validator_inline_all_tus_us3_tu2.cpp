// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_078_validator_inline_all_tus_us3_tu2.cpp
//
// 078-precompiled-builder-libs Gate B RC#3 -- second TU of the validator-side
// inline-XOR-link SAFE-path counter-test (see
// test_078_validator_inline_all_tus_us3.cpp header comment and
// quickstart.md Scenario 4d). This TU force-inlines validate_Email too --
// the SAME message is force-inlined in EVERY TU of this 2-TU program, never
// link-resolved anywhere in it.

#define FIXPP_VALIDATORS_HEADER_ONLY_Email
#include <fixpp/v44/messages/Email.hpp>
#undef FIXPP_VALIDATORS_HEADER_ONLY_Email

#include "test_078_validator_inline_all_tus_us3_support.hpp"

namespace fixpp_test_078_inline_all_tus {

::fixpp::core::expected_t<void> validate_email_other_tu(::fixpp::v44::EmailArgs const& args) {
    return ::fixpp::v44::validate_Email(args);
}

}  // namespace fixpp_test_078_inline_all_tus
