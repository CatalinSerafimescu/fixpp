// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/cutover_2b_gated_test.cpp — T019 (US1, SC-006).
// The 2b cutover gate: the 004-authored 001 wire FLOAT accessor leg
// (field_view::bytes() -> fixpp::decimal_t::parse(span, mr), allocation-free
// for the default pod_decimal) on the REAL MessageView/field_view surface,
// PLUS the 003 dict::reify round-trip on that same real surface. The wire
// FLOAT leg is exercised GREEN here; the 003 reify round-trip is genuinely
// blocked on the surface migration (T028 message_view_contract re-export +
// T029 reify rewire) and is the honest RED marker (DISABLED below — T031
// flips it GREEN after the cutover, then quickstart §9 grep sanity).

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory_resource>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/wire/field_view.hpp>
#include <fixpp/wire/parser.hpp>

#include "support/frame_view_factory.hpp"
#include "support/mock_dict_table.hpp"

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::Parser;

std::vector<std::byte> make_frame(std::string const& body) {
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = "8=FIX.4.4\x01" + nine + body;
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    std::array<char, 8> chk{};
    std::snprintf(chk.data(), chk.size(), "10=%03u\x01", sum % 256U);
    std::string full = pre + chk.data();
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

TEST(WireCutover2bGated, WireFloatAccessorLegOnRealSurface) {
    // 44=Price (FLOAT). The wire layer decodes NOTHING: it hands the field's
    // raw bytes across the 2a trait boundary to decimal_t::parse. field_view
    // is the real `: public View` shape the cutover migrates merged-003 onto.
    auto buf = make_frame("35=D\x01" "34=1\x01" "44=1234.56\x01"
                          "38=100\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{fixpp::dict::table_view{}};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    // The load-bearing cutover shape: get<Tag>() returns field_view (a View),
    // bytes() aliases the frame, decimal_t::parse consumes it.
    auto px = mv->template get<44>();
    ASSERT_TRUE(px.has_value());
    static_assert(std::is_base_of_v<fixpp::wire::View, fixpp::wire::field_view>,
                  "field_view must be the real `: public View` shape");
    EXPECT_EQ(px->as_string(), "1234.56");

    auto dec = mv->get_decimal(44, &arena);
    ASSERT_TRUE(dec.has_value())
        << "field_view::bytes() -> decimal_t::parse must succeed";

    auto missing = mv->get_decimal(9999, &arena);
    EXPECT_FALSE(missing.has_value());
}

// RED marker (SC-006), HONESTLY RE-SCOPED 2026-05-16: the type-surface
// cutover IS done — message_view_contract.hpp re-exports the real
// wire::MessageView/field_view (T028), reify.hpp/field_traits.hpp bind it,
// and the FLOAT/field_traits decode path above is GREEN. The 003 dict::reify
// *round-trip* is NOT just a header rewire: it runs through
// owning_<Msg>::from_view, which deep-copies frame bytes into a caller arena
// and is emitted by fixpp-codegen as a hardcoded R6 stub returning
// dict_reify_wire_body_not_ready (generated <vXX>/Reify.hpp). Making it real
// = a 003 codegen-emitter change + the owning-message deep-copy — carved as
// its own deliverable, tasks.md T059. Stays DISABLED (not fake-green) until
// T059 lands; flipping it today would be RED, not GREEN.
TEST(WireCutover2bGated, DISABLED_ReifyRoundTripOnRealMessageView) {
    FAIL() << "blocked on T059: owning_<Msg>::from_view is a codegen-emitted "
              "R6 stub (dict_reify_wire_body_not_ready); needs the codegen "
              "from_view + owning-message deep-copy (not a header rewire)";
}

}  // namespace
