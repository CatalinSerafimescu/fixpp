// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/typed_accessor_test.cpp — T017 [US1]
//
// AC-G1..G8/G11: per-field typed accessors on the generated flyweight, the
// v1.4 price(mr) PMR decimal accessor, repeating-group flyweight, the
// field_value(uint16_t) forwarder, view() bridge. The vendored frozen wire
// stub (R6) carries no frame state, so every get<>() reports field-absent;
// these tests pin accessor SHAPE + TYPE + error-forwarding (the behavioural
// round-trip arrives when 2b swaps in the real body — spec §11 R6).
#include <gtest/gtest.h>

#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/v44/Messages.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <memory_resource>
#include <string_view>
#include <type_traits>

namespace {
using MV = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;
}

TEST(CodegenTypedAccessor, MsgTypeAndVersionAreConstexpr) {
    static_assert(fixpp::v44::NewOrderSingle::msg_type_v == "D");  // AC-G2
    static_assert(fixpp::v44::NewOrderSingle::version_v ==
                  fixpp::dict::application_version::v44);  // AC-G2
    SUCCEED();
}

TEST(CodegenTypedAccessor, StringAndCharAccessorsForwardWireError) {
    MV mv;
    fixpp::v44::NewOrderSingle nos(mv);  // AC-G3
    auto cl = nos.cl_ord_id();
    static_assert(
        std::is_same_v<decltype(cl), fixpp::core::expected_t<std::string_view>>);  // AC-G4
    ASSERT_FALSE(cl.has_value());
    EXPECT_EQ(cl.error(), fixpp::core::error::dict_xml_parse_failed);  // R6 / AC-FT3
    auto sd = nos.side();
    static_assert(std::is_same_v<decltype(sd), fixpp::core::expected_t<char>>);
    EXPECT_FALSE(sd.has_value());
}

TEST(CodegenTypedAccessor, DecimalAccessorTakesMemoryResource) {
    MV mv;
    fixpp::v44::NewOrderSingle nos(mv);
    std::pmr::monotonic_buffer_resource arena;
    auto p = nos.price(&arena);  // AC-G4 / v1.4
    static_assert(std::is_same_v<decltype(p), fixpp::core::expected_t<fixpp::decimal_t>>);
    EXPECT_FALSE(p.has_value());  // R6: get<44> field-absent
}

TEST(CodegenTypedAccessor, FieldValueForwarderAndViewBridge) {
    MV mv;
    fixpp::v44::NewOrderSingle nos(mv);
    auto fv = nos.field_value(11);  // AC-G6
    static_assert(std::is_same_v<decltype(fv), fixpp::core::expected_t<fixpp::wire::field_view>>);
    EXPECT_FALSE(fv.has_value());
    static_assert(std::is_same_v<decltype(nos.view()), MV const&>);  // AC-G6 bridge
}

TEST(CodegenTypedAccessor, RepeatingGroupFlyweight) {
    MV mv;
    auto gv = mv.template group<453, fixpp::v44::groups::G_453>();  // AC-G5
    EXPECT_EQ(gv.size(), 0U);                                       // R6 stub
    fixpp::v44::groups::G_453 g;    // default-constructible (group_view returns T{})
    auto gfv = g.field_value(448);  // AC-G6 (nested)
    static_assert(std::is_same_v<decltype(gfv), fixpp::core::expected_t<fixpp::wire::field_view>>);
    EXPECT_FALSE(gfv.has_value());
}
