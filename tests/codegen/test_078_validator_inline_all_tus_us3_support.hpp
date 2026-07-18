// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_078_validator_inline_all_tus_us3_support.hpp
//
// 078-precompiled-builder-libs Gate B RC#3 -- validator-side twin of
// test_078_builder_inline_all_tus_us3_support.hpp. Shared declaration for
// the inline-XOR-link SAFE-path counter-test (quickstart.md Scenario 4d).
// The TU that DEFINES validate_email_other_tu
// (test_078_validator_inline_all_tus_us3_tu2.cpp) MUST itself
// #define FIXPP_VALIDATORS_HEADER_ONLY_Email and
// #include <fixpp/v44/messages/Email.hpp> BEFORE including this header --
// see the builder-side twin's header comment for why (#pragma once /
// first-inclusion-selects-mode).
#pragma once

#include <fixpp/core/error.hpp>
#include <fixpp/v44/messages/Email.hpp>

namespace fixpp_test_078_inline_all_tus {

::fixpp::core::expected_t<void> validate_email_other_tu(::fixpp::v44::EmailArgs const& args);

}  // namespace fixpp_test_078_inline_all_tus
