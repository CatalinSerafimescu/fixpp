// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/toplevel_read_regression_test.cpp — 062 T022 (Polish, FR-007 /
// SC-004 no-regression guard).
//
// 062's codegen edit (emit_messages.cpp emit_group_class / emit_scalar /
// emit_field_value ptr-branch, T012/T016) is confined to the entry class
// `G_<no_tag>` body — the message-level flyweight (`MessageView const&` by
// reference) is UNTOUCHED per tasks.md T012/research §Entry storage & ripple.
// But the entry-class scalar/decimal accessors now call the SAME shared
// decode helpers (`fixpp::dict::decode_field<T>` / `fixpp::decimal_t::parse`)
// that every TOP-LEVEL (non-group) generated message accessor already called
// before 062 — so a regression in those shared helpers, or an accidental
// ripple from the forced codegen regen (T013/T017 golden updates), would
// silently corrupt ordinary top-level reads too.
//
// This test pins that top-level, non-group read path is byte-for-byte
// unaffected: a real generated message (NewOrderSingle) parsed from a real
// frame, reading its string/char/int/decimal fields and asserting the exact
// wire values. Frame-construction pattern mirrors
// tests/wire/repeating_group_equivalence_test.cpp /
// tests/wire/group_slice_trailing_soh_test.cpp (make_raw_frame + the
// frame_view_factory test factory, dummy checksum — Parser<Index> does not
// validate the checksum digit, only uses it as a boundary marker).

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <fixpp/v44/Messages.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::decimal_t;
using fixpp::wire::access_mode;
using fixpp::wire::Parser;

std::vector<std::byte> make_raw_frame(std::string const& body) {
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string full = "8=FIX.4.4\x01" + nine + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

decimal_t parse_decimal(std::string_view sv, std::pmr::memory_resource* mr) {
    auto r = decimal_t::parse(
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()}, mr);
    EXPECT_TRUE(r.has_value()) << "parse_decimal failed for: " << sv;
    return r.value_or(decimal_t{});
}

TEST(TopLevelReadRegression, TopLevelNonGroupReadUnchanged) {
    // NewOrderSingle: no repeating groups involved anywhere in this frame —
    // purely a top-level message read. Fields chosen to cover all four typed
    // accessor shapes the 062 entry-class ptr-branch was rewritten to
    // support (string, char, int, decimal), so a regression shared with the
    // entry-class rewrite would show up here too.
    auto buf = make_raw_frame(
        "35=D\x01"    // MsgType (string)
        "34=42\x01"   // MsgSeqNum (int)
        "11=CLORD-99\x01"  // ClOrdID (string)
        "40=2\x01"    // OrdType (char)
        "38=150.25\x01");  // OrderQty (decimal)
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    fixpp::v44::NewOrderSingle nos{*mv};

    // string
    auto msg_type = nos.msg_type();
    ASSERT_TRUE(msg_type.has_value());
    EXPECT_EQ(*msg_type, "D");

    auto cl_ord_id = nos.cl_ord_id();
    ASSERT_TRUE(cl_ord_id.has_value());
    EXPECT_EQ(*cl_ord_id, "CLORD-99");

    // int
    auto seq = nos.msg_seq_num();
    ASSERT_TRUE(seq.has_value());
    EXPECT_EQ(*seq, 42);

    // char
    auto ord_type = nos.ord_type();
    ASSERT_TRUE(ord_type.has_value());
    EXPECT_EQ(*ord_type, '2');

    // decimal
    std::pmr::monotonic_buffer_resource dec_arena;
    auto qty = nos.order_qty(&dec_arena);
    ASSERT_TRUE(qty.has_value());
    EXPECT_EQ(*qty, parse_decimal("150.25", &dec_arena));
}

}  // namespace
