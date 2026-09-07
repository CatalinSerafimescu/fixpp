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

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/v44/Messages.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "support/typed_group_table_views.hpp"

namespace {
using MV = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;

// 062 T011 helper — build a real (dict-free) frame the same way
// tests/codegen/group_entry_read_test.cpp does, so operator[] instantiates
// against an ACTUAL entry, not a default-constructed group_view.
std::vector<std::byte> make_frame_062(std::string_view body) {
    std::string pre =
        "8=FIX.4.4\x01" + std::string("9=") + std::to_string(body.size()) + "\x01" +
        std::string(body);
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    char checksum[16]{};
    std::snprintf(checksum, sizeof(checksum), "10=%03u\x01", sum % 256U);
    std::string full = pre + checksum;
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

MV parse_frame_062(std::vector<std::byte> const& buf, std::pmr::memory_resource* mr) {
    fixpp::wire::pmr_carry_buffer carry{buf.size(), mr};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(
        std::span<const std::byte>{buf.data(), buf.size()}, carry,
        std::span<fixpp::wire::frame_view>{fvs, 1});
    EXPECT_TRUE(framed.has_value()) << "Framer::feed failed";
    EXPECT_FALSE(framed->empty()) << "Framer produced no frames";
    // 220: DICT-AWARE. group() is now a dictionary-only operation, so a
    // dict-free parse yields no typed group and GeneratedEntryOperator-
    // SubscriptInstantiates below would read orders().size() == 0.
    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{
        fixpp_test_support::group73_table_view()};
    auto mv = parser.parse((*framed)[0], mr);
    if (!mv.has_value()) {
        ADD_FAILURE() << "dict-aware parse failed";
        return MV{(*framed)[0], mr};
    }
    return std::move(*mv);
}
}  // namespace

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
    // 2b cutover (004 T028/T030): the R6 stub forwarded its placeholder
    // dict_xml_parse_failed sentinel; the real OffsetTable-backed surface
    // forwards the genuine wire error — a default MessageView has an empty
    // table, so an absent field is wire_required_field_missing [2b §6.5.4].
    EXPECT_EQ(cl.error(), fixpp::core::error::wire_required_field_missing);  // AC-FT3
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

// 062 T011 — FR-006 / SC-003 regression guard: instantiate operator[]/iter()
// on a GENERATED entry (NewOrderList orders() / G_73) over a REAL parsed
// frame and read a field. Before 062 T007/T012, `group_view<G_73>::operator[]`
// could not build a `G_73` at all (it stored `MessageView<Index> const*`,
// not an `entry_context`) — this test's mere COMPILATION plus a correct
// executed assertion is the revert-detector: reverting the entry-ctor change
// re-breaks the build/test, it does not silently skip.
TEST(CodegenTypedAccessor, GeneratedEntryOperatorSubscriptInstantiates) {
    std::string body =
        "35=E\x01"
        "73=1\x01"
        "11=REVERT-DETECTOR\x01"
        "54=1\x01";
    auto buf = make_frame_062(body);
    std::pmr::monotonic_buffer_resource arena{4096};
    auto mv = parse_frame_062(buf, &arena);

    fixpp::v44::NewOrderList nol{mv};
    auto orders = nol.orders();
    ASSERT_EQ(orders.size(), 1U);

    // operator[] instantiation + real read.
    auto entry_via_index = orders[0];
    auto cl_via_index = entry_via_index.cl_ord_id();
    ASSERT_TRUE(cl_via_index.has_value());
    EXPECT_EQ(*cl_via_index, "REVERT-DETECTOR");

    // iter() instantiation + real read (both entry-construction paths).
    auto it = orders.iter();
    ASSERT_FALSE(it == orders.end());
    auto entry_via_iter = *it;
    auto cl_via_iter = entry_via_iter.cl_ord_id();
    ASSERT_TRUE(cl_via_iter.has_value());
    EXPECT_EQ(*cl_via_iter, "REVERT-DETECTOR");
}
