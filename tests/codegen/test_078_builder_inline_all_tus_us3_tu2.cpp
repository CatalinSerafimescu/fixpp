// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_078_builder_inline_all_tus_us3_tu2.cpp
//
// 078-precompiled-builder-libs Gate B RC#3 -- second TU of the
// inline-XOR-link SAFE-path counter-test (see
// test_078_builder_inline_all_tus_us3.cpp header comment and
// quickstart.md Scenario 4d). This TU force-inlines build_NewOrderSingle
// too -- the SAME message is force-inlined in EVERY TU of this 2-TU
// program, never link-resolved anywhere in it.

#define FIXPP_BUILDERS_HEADER_ONLY_NewOrderSingle
#include <fixpp/v44/messages/NewOrderSingle.hpp>
#undef FIXPP_BUILDERS_HEADER_ONLY_NewOrderSingle

#include "test_078_builder_inline_all_tus_us3_support.hpp"

namespace fixpp_test_078_inline_all_tus {

::fixpp::core::expected_t<::std::span<::std::byte>> build_new_order_single_other_tu(
    ::std::span<::std::byte> out, ::fixpp::v44::NewOrderSingleArgs const& args) {
    return ::fixpp::v44::build_NewOrderSingle(out, args);
}

}  // namespace fixpp_test_078_inline_all_tus
