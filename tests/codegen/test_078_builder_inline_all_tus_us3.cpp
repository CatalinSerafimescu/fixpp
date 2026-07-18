// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_078_builder_inline_all_tus_us3.cpp
//
// 078-precompiled-builder-libs Gate B RC#3 [US3]: SAFE-path counter-test for
// the inline-XOR-link contract (spec.md Edge Case ~line 128; FR-006/FR-007;
// quickstart.md Scenario 4d). test_078_builder_mixing_us3.cpp force-inlines
// ONE message (NewOrderSingle) and links a DIFFERENT one (ExecutionReport)
// -- it never puts the SAME message in both an inline form and a linked form
// in one program, so it does not exercise the inline-XOR-link discipline
// FIXPP_BUILDERS_HEADER_ONLY_<Msg> actually names (a program-wide per-
// message switch, not a per-TU one).
//
// This TU demonstrates the SAFE side: build_NewOrderSingle is force-inlined
// (FIXPP_BUILDERS_HEADER_ONLY_NewOrderSingle) in BOTH translation units of
// this 2-TU program -- this file AND
// test_078_builder_inline_all_tus_us3_tu2.cpp. The message is inline
// EVERYWHERE it is referenced in this binary, never link-resolved anywhere
// in it -- so NO fixpp::builders::v44 archive member for NewOrderSingle is
// ever needed (the test target below deliberately does not even link
// fixpp::builders::v44). Both TUs' `inline` (weak/COMDAT) definitions of the
// identically-mangled fixpp::v44::build_NewOrderSingle are COMDAT-folded by
// the linker with NO duplicate-symbol error -- that IS part of the property
// under test (mirrors test_078_odr_sc003_probe.cpp's link-succeeding legs)
// -- and both TUs must produce byte-identical, dict-readback-correct wire
// output for the same Args.
//
// This test intentionally does NOT exercise the MIXED case (force-inline in
// one TU, link-mode in another, for the SAME message, in the SAME program):
// per [dcl.inline]/4 that combination is an ODR violation (IFNDR -- no
// diagnostic required), so a test asserting it "passes" would enshrine
// reliance on unspecified/undefined linker behavior rather than pin a real
// guarantee. See the contract comment above the
// FIXPP_BUILDERS_HEADER_ONLY_<Msg> emission in
// tools/codegen/fixpp-codegen/emit_builders.cpp and quickstart.md Scenario
// 4d for the documented inline-XOR-link discipline.
//
// Anchors: specs/078-precompiled-builder-libs/spec.md Edge Case (~line 128),
// FR-006/FR-007; quickstart.md Scenario 4d;
// tests/codegen/test_078_builder_mixing_us3.cpp (mixed-message precedent,
// does not cover same-message mixing);
// tools/codegen/fixpp-codegen/emit_builders.cpp (emitter contract comment).

#define FIXPP_BUILDERS_HEADER_ONLY_NewOrderSingle
#include <fixpp/v44/messages/NewOrderSingle.hpp>
#undef FIXPP_BUILDERS_HEADER_ONLY_NewOrderSingle

#include "test_078_builder_inline_all_tus_us3_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <vector>

#include "support/app_message_read_scaffold.hpp"

namespace {

using fixpp_test_support::bytes_to_string;
using fixpp_test_support::expect_decimal;
using fixpp_test_support::expect_text;
using fixpp_test_support::make_decimal;
using fixpp_test_support::make_frame;
using fixpp_test_support::parse_dict;

fixpp::v44::NewOrderSingleArgs make_args(std::pmr::memory_resource* mr) {
    fixpp::v44::NewOrderSingleArgs args{};
    args.cl_ord_id = "078-INLALL-CLORDID";
    args.symbol = "078INLALL";
    args.side = '1';
    args.order_qty = make_decimal("10.5", mr);
    args.price = make_decimal("42.75", mr);
    return args;
}

}  // namespace

TEST(BuilderInlineAllTUsUS3, SafePath_InlineInBothTUs_ByteIdenticalAndDictReadbackCorrect) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v44::NewOrderSingleArgs const args = make_args(&arena);

    // Leg A: force-inlined build_NewOrderSingle, THIS TU.
    std::array<std::byte, 1024> out_here{};
    auto built_here = fixpp::v44::build_NewOrderSingle(std::span<std::byte>{out_here}, args);
    ASSERT_TRUE(built_here.has_value())
        << "force-inlined build_NewOrderSingle (this TU) failed";

    // Leg B: force-inlined build_NewOrderSingle, the OTHER TU
    // (test_078_builder_inline_all_tus_us3_tu2.cpp) -- co-present in the same
    // binary, no linked fixpp::builders::v44 archive member for this message
    // exists in this program at all.
    std::array<std::byte, 1024> out_there{};
    auto built_there = fixpp_test_078_inline_all_tus::build_new_order_single_other_tu(
        std::span<std::byte>{out_there}, args);
    ASSERT_TRUE(built_there.has_value())
        << "force-inlined build_NewOrderSingle (other TU) failed";

    ASSERT_EQ(built_here->size(), built_there->size())
        << "both TUs force-inline the SAME message -- output length must match";
    EXPECT_TRUE(std::equal(built_here->begin(), built_here->end(), built_there->begin()))
        << "two TUs that BOTH force-inline the SAME message must produce "
           "byte-identical wire output for the same Args (safe, non-mixed "
           "side of the inline-XOR-link contract)";

    // Confirm the output is genuinely correct, not merely identical-to-itself:
    // dict-aware readback of the "other TU" leg's bytes.
    std::string const body = bytes_to_string(*built_there);
    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> const frame = make_frame("FIX.4.4", body);
    auto const mv = parse_dict(frame, tv, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of the inline-in-both-TUs NewOrderSingle frame failed";

    expect_text(mv, 11, "078-INLALL-CLORDID", "cl_ord_id");
    expect_text(mv, 55, "078INLALL", "symbol");
    expect_text(mv, 54, "1", "side");
    expect_decimal(mv, 38, "10.5", &read_arena, "order_qty");
    expect_decimal(mv, 44, "42.75", &read_arena, "price");
}
