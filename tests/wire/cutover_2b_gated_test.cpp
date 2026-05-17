// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/cutover_2b_gated_test.cpp — T019 (US1, SC-006).
// The 2b cutover gate: the 004-authored 001 wire FLOAT accessor leg
// (field_view::bytes() -> fixpp::decimal_t::parse(span, mr), allocation-free
// for the default pod_decimal) on the REAL MessageView/field_view surface,
// PLUS the 003 dict::reify round-trip on that same real surface. Both are
// GREEN: the wire FLOAT leg AND the 003 owning deep-copy round-trip
// (owning_<Msg>::from_view) including survival of a SOURCE-arena reset
// (T059 / AC-R4 / SC-006). reify_as<Msg> the free function remains declared-
// only (deferred with owning_message_handle runtime dispatch); the implemented
// typed entry point is owning_<Msg>::from_view, exercised here.

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory_resource>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/dict/reify.hpp>
#include <fixpp/wire/field_view.hpp>
#include <fixpp/wire/parser.hpp>

// Generated owning_<Msg> (build-tree only).
#include <fixpp/v44/Reify.hpp>

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
    Parser<access_mode::Index> parser{};
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

// SC-006 / AC-R4 — the 003 owning deep-copy round-trip on the REAL
// MessageView surface (T059). from_view deep-copies the validated frame into
// the caller arena; the owning_<Msg> must read its typed fields even after
// the SOURCE frame buffer + its arena are destroyed. ENABLED + GREEN
// post-T059 (was DISABLED under the honest re-scope until T059 landed).
TEST(WireCutover2bGated, ReifyRoundTripOnRealMessageView) {
    using ONOS = fixpp::v44::owning_NewOrderSingle;
    std::pmr::monotonic_buffer_resource owning_mr;
    std::optional<ONOS> owned;
    {
        // Source frame + its arena live ONLY in this scope.
        auto buf = make_frame("35=D\x01" "34=1\x01" "49=S\x01" "56=T\x01"
                              "11=ORD1\x01" "55=AAPL\x01");
        auto fv = fixpp::wire::test::make_frame_view(buf);
        ASSERT_TRUE(fv.has_value());
        std::pmr::monotonic_buffer_resource source_mr;
        Parser<access_mode::Index> parser{};
        auto src = parser.parse(*fv, &source_mr);
        ASSERT_TRUE(src.has_value());

        auto r = ONOS::from_view(*src, &owning_mr);
        ASSERT_TRUE(r.has_value())
            << "owning deep-copy from_view must succeed (T059)";
        // Sanity inside scope: ClOrdID readable.
        ASSERT_TRUE(r->cl_ord_id().has_value());
        EXPECT_EQ(r->cl_ord_id().value(), "ORD1");
        owned.emplace(std::move(*r));
    }  // buf + source_mr + src destroyed

    // SC-006 / AC-R4: the owning copy survives the source's death.
    auto cl = owned->cl_ord_id();
    ASSERT_TRUE(cl.has_value())
        << "owning_<Msg> must read fields after SOURCE arena reset (AC-R4/SC-006)";
    EXPECT_EQ(cl.value(), "ORD1");
    EXPECT_TRUE(owned->field_value(55).has_value());  // Symbol present
    EXPECT_FALSE(owned->field_value(44).has_value())  // Price not in frame
        << "absent field reports absent (no UB)";
}

}  // namespace
