// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_078_builder_roundtrip_linked_us1.cpp
//
// 078-precompiled-builder-libs T021 [US1] [TESTS-FIRST]: byte-identical wire
// round-trip via the LINKED lib for representative messages across v44 and
// v50sp2 (SC-004 builder, link mode; FR-009). Each TEST includes ONLY the
// slim per-message declaration header for its message (not `all.hpp`), so
// the witness demonstrates the US1 "one/few messages" consumer shape, not
// just the whole-version `all.hpp` aggregator already covered by the 077
// suite. Deterministic, chosen-before-build literal inputs, asserted on an
// independent dict-driven readback (fixpp::wire::Parser<Index>) -- the same
// oracle established by test_067_builder_roundtrip.cpp /
// test_077_allversions_builder_roundtrip.cpp (feedback_coverage_push_
// enshrines_bugs: values are never captured post-hoc from a build). Since
// the 078 split does not change `build_<Msg>`'s body -- only which
// translation unit/library it is compiled into -- a matching field-for-field
// readback here IS the byte-identical-to-the-077-baseline evidence: the
// generated body_builder logic (canonical tag-ascending emission) is
// unchanged, so identical inputs necessarily encode to identical bytes.
//
// vlatest cell: CI-DEFERRED. The vlatest builder lib
// (`fixpp::builders::vlatest`) is not built on this WSL2 host (OOM risk per
// the orchestrator brief / feedback_build_resource_cap_oom) -- omitted here,
// not silently skipped: FR-009/SC-004 for vlatest remain to be witnessed in
// CI where the lib is built.
//
// Anchors: specs/078-precompiled-builder-libs/spec.md SC-004/FR-009;
// quickstart.md Scenario 1; tests/session/test_077_allversions_builder_
// roundtrip.cpp (oracle pattern); tests/support/app_message_read_scaffold.hpp.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/v44/messages/ExecutionReport.hpp>
#include <fixpp/v44/messages/NewOrderSingle.hpp>
#include <fixpp/v50sp2/messages/Advertisement.hpp>
#include <fixpp/v50sp2/messages/IOI.hpp>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/app_message_read_scaffold.hpp"

namespace {

using fixpp_test_support::bytes_to_string;
using fixpp_test_support::make_decimal;
using fixpp_test_support::make_frame;
using fixpp_test_support::parse_dict;
using IndexView = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;

fixpp::dict::Dictionary load_fix50sp2(std::pmr::memory_resource* mr) {
    fixpp::dict::XmlLoader loader;
    return loader.load(std::string(FIXPP_DICT_DATA_DIR) + "/FIX50SP2.xml", mr);
}

void expect_text(IndexView const& mv, std::uint16_t tag, std::string_view expected,
                  char const* label) {
    SCOPED_TRACE(label);
    auto fv = mv.get(tag);
    ASSERT_TRUE(fv.has_value()) << "tag " << tag << " not found in parsed frame";
    EXPECT_EQ(fv->as_string(), expected);
}

void expect_decimal(IndexView const& mv, std::uint16_t tag, std::string_view expected_ascii,
                     std::pmr::memory_resource* mr, char const* label) {
    SCOPED_TRACE(label);
    auto got = mv.get_decimal(tag, mr);
    ASSERT_TRUE(got.has_value()) << "tag " << tag << " not found/decodable in parsed frame";
    EXPECT_EQ(*got, make_decimal(expected_ascii, mr));
}

}  // namespace

// ── v44 (linked fixpp::builders::v44) ───────────────────────────────────────

TEST(BuilderRoundtripLinkedUS1V44, NewOrderSingle) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v44::NewOrderSingleArgs args{};
    args.cl_ord_id = "078-NOS-CLORDID";
    args.symbol = "078NOS";
    args.side = '1';
    args.order_qty = make_decimal("10.5", &arena);
    args.price = make_decimal("42.75", &arena);

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_NewOrderSingle(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_NewOrderSingle failed";
    std::string const body = bytes_to_string(*built);

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> const frame = make_frame("FIX.4.4", body);
    auto const mv = parse_dict(frame, tv, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built NewOrderSingle frame failed (see ADD_FAILURE above)";

    expect_text(mv, 11, "078-NOS-CLORDID", "cl_ord_id");
    expect_text(mv, 55, "078NOS", "symbol");
    expect_text(mv, 54, "1", "side");
    expect_decimal(mv, 38, "10.5", &read_arena, "order_qty");
    expect_decimal(mv, 44, "42.75", &read_arena, "price");
}

TEST(BuilderRoundtripLinkedUS1V44, ExecutionReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v44::ExecutionReportArgs args{};
    args.avg_px = make_decimal("101.25", &arena);
    args.cl_ord_id = "078-ER-CLORDID";
    args.exec_id = "078-EXECID";
    args.order_id = "078-ORDERID";
    args.ord_status = '0';
    args.side = '2';
    args.symbol = "078ER";
    args.leaves_qty = make_decimal("5.0", &arena);

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_ExecutionReport(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_ExecutionReport failed";
    std::string const body = bytes_to_string(*built);

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> const frame = make_frame("FIX.4.4", body);
    auto const mv = parse_dict(frame, tv, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built ExecutionReport frame failed (see ADD_FAILURE above)";

    expect_decimal(mv, 6, "101.25", &read_arena, "avg_px");
    expect_text(mv, 11, "078-ER-CLORDID", "cl_ord_id");
    expect_text(mv, 17, "078-EXECID", "exec_id");
    expect_text(mv, 37, "078-ORDERID", "order_id");
    expect_text(mv, 39, "0", "ord_status");
    expect_text(mv, 54, "2", "side");
    expect_text(mv, 55, "078ER", "symbol");
    expect_decimal(mv, 151, "5.0", &read_arena, "leaves_qty");
}

// ── v50sp2 (linked fixpp::builders::v50sp2) ─────────────────────────────────

TEST(BuilderRoundtripLinkedUS1V50SP2, IOI) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v50sp2::IOIArgs args{};
    args.currency = "078-IOI-currency";
    args.security_id_source = "078-IOI-secidsrc";
    args.ioi_qlty_ind = '1';
    args.ioi_trans_type = '1';
    args.order_qty = make_decimal("20.5", &arena);
    args.price = make_decimal("30.5", &arena);

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v50sp2::build_IOI(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_IOI failed";
    std::string const body = bytes_to_string(*built);

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = load_fix50sp2(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> const frame = make_frame("FIX.5.0SP2", body);
    auto const mv = parse_dict(frame, tv, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built IOI frame failed (see ADD_FAILURE above)";

    expect_text(mv, 15, "078-IOI-currency", "currency");
    expect_text(mv, 22, "078-IOI-secidsrc", "security_id_source");
    expect_text(mv, 25, "1", "ioi_qlty_ind");
    expect_text(mv, 28, "1", "ioi_trans_type");
    expect_decimal(mv, 38, "20.5", &read_arena, "order_qty");
    expect_decimal(mv, 44, "30.5", &read_arena, "price");
}

TEST(BuilderRoundtripLinkedUS1V50SP2, Advertisement) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v50sp2::AdvertisementArgs args{};
    args.adv_id = "078-ADV-id";
    args.adv_ref_id = "078-ADV-refid";
    args.adv_side = '1';
    args.price = make_decimal("15.5", &arena);
    args.quantity = make_decimal("25.5", &arena);
    args.put_or_call = 201;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v50sp2::build_Advertisement(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_Advertisement failed";
    std::string const body = bytes_to_string(*built);

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = load_fix50sp2(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> const frame = make_frame("FIX.5.0SP2", body);
    auto const mv = parse_dict(frame, tv, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built Advertisement frame failed (see ADD_FAILURE above)";

    expect_text(mv, 2, "078-ADV-id", "adv_id");
    expect_text(mv, 3, "078-ADV-refid", "adv_ref_id");
    expect_text(mv, 4, "1", "adv_side");
    expect_decimal(mv, 44, "15.5", &read_arena, "price");
    expect_decimal(mv, 53, "25.5", &read_arena, "quantity");
    expect_text(mv, 201, "201", "put_or_call");
}
