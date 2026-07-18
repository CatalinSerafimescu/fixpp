// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_078_builder_inline_all_tus_us3_support.hpp
//
// 078-precompiled-builder-libs Gate B RC#3 -- shared declaration for the
// inline-XOR-link SAFE-path counter-test (quickstart.md Scenario 4d). The
// TU that DEFINES build_new_order_single_other_tu
// (test_078_builder_inline_all_tus_us3_tu2.cpp) MUST itself
// #define FIXPP_BUILDERS_HEADER_ONLY_NewOrderSingle and
// #include <fixpp/v44/messages/NewOrderSingle.hpp> BEFORE including this
// header -- that TU's force-inline mode is selected by its OWN first
// inclusion of NewOrderSingle.hpp (#pragma once means a later, unguarded
// inclusion here is a no-op that does not change the mode already chosen).
// The calling TU (test_078_builder_inline_all_tus_us3.cpp) does the same
// independently, so BOTH TUs force-inline build_NewOrderSingle -- the safe,
// non-mixed side of the contract documented in
// tools/codegen/fixpp-codegen/emit_builders.cpp.
#pragma once

#include <cstddef>
#include <fixpp/v44/messages/NewOrderSingle.hpp>
#include <span>

namespace fixpp_test_078_inline_all_tus {

::fixpp::core::expected_t<::std::span<::std::byte>> build_new_order_single_other_tu(
    ::std::span<::std::byte> out, ::fixpp::v44::NewOrderSingleArgs const& args);

}  // namespace fixpp_test_078_inline_all_tus
