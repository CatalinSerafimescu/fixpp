// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_067_builder_roundtrip.cpp
//
// 067-codegen-writer-emitter T013 [US1] [TESTS-FIRST] — seed-driven
// build -> framed -> dict::reify()/typed-flyweight read-back for all 33
// OFFICIAL v44 MsgTypes, asserting each seeded field (incl. nested group
// entries) at its EXACT input value, PLUS non-tautological byte-structural
// asserts (G6/FR-008): top-level field order matches G3 canonical
// (tag-ascending) order, correct SOH count (SOH-count == '='-count — every
// field terminated exactly once, no leaked framing), and NO framing tags
// {8,9,10,34,49,52,56} in the body.
//
// MUST FAIL FIRST (T013 TESTS-FIRST): references the NOT-YET-GENERATED
// `fixpp::v44::build_<Msg>` / `fixpp::v44::<Msg>Args` from
// `<fixpp/v44/Builders.hpp>` — Phase 3b (T016/T017) lands that header. Until
// then this translation unit does not compile/link (RED, not a placeholder
// assertion — feedback_fail_placeholder_red_test).
//
// Seeds: tests/session/support/test_067_seeds.hpp (T012). W/X/i/E/AS carry
// real group population (research R5/contract G6 mandatory insurance); the
// other 28 messages seed 2-6 clean body-only scalar fields.
//
// Nested-group Args type naming: see the ESCALATION note at the top of
// support/test_067_seeds.hpp — per-message-qualified
// `<Msg><StrippedGroupIdentifier>Args`, not anchor-pinned, flagged for
// Phase 3b.
//
// Anchors: specs/067-codegen-writer-emitter/contracts/generated-builder.md
//          G3/G6; data-model.md §1; research.md R1/R5/R9;
//          quickstart.md (build_NewOrderSingle/NewOrderSingleArgs shape).

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/v44/Builders.hpp>  // GENERATED (Phase 3b) — build_<Msg>/<Msg>Args/registry
#include <fixpp/v44/Messages.hpp>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/app_message_read_scaffold.hpp"
#include "support/test_067_seeds.hpp"

namespace {

using fixpp::decimal_t;
using fixpp_test_support::bytes_to_string;  // shared with test_069 (same binary)
using fixpp_test_support::make_decimal;

// Generic single-line field-assertion helper (works for string_view/char/
// int32_t/bool read-back accessors compared against any comparable seed
// type).
template <typename T, typename U>
void expect_eq_field(fixpp::core::expected_t<T> got, U const& want, char const* label) {
    SCOPED_TRACE(label);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(*got, want);
}

void expect_eq_decimal(fixpp::core::expected_t<decimal_t> got, std::string_view want_ascii,
                        std::pmr::memory_resource* mr, char const* label) {
    SCOPED_TRACE(label);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(*got, make_decimal(want_ascii, mr));
}

// G6 byte-structural asserts: SOH-count == '='-count (every field
// terminated exactly once), no leaked framing tags, and the listed
// TOP-LEVEL tags appear in non-decreasing byte-position order (G3
// tag-ascending regime). `tags_ascending` MUST already be tag-sorted by the
// caller (asserting a caller mistake here would be tautological).
void assert_body_structure(std::string const& body, std::vector<std::uint16_t> const& tags_ascending) {
    auto const soh = std::count(body.begin(), body.end(), '\x01');
    auto const eq = std::count(body.begin(), body.end(), '=');
    EXPECT_EQ(soh, eq) << "SOH count must equal '=' count (each field terminated exactly once)";

    static constexpr std::array<std::string_view, 7> kFramingNeedles = {
        "\x01" "8=", "\x01" "9=", "\x01" "10=", "\x01" "34=", "\x01" "49=", "\x01" "52=", "\x01" "56="};
    for (auto needle : kFramingNeedles) {
        EXPECT_EQ(body.find(needle), std::string::npos) << "framing tag leaked into body";
    }

    std::size_t last_pos = 0;
    for (auto tag : tags_ascending) {
        std::string const needle = "\x01" + std::to_string(tag) + "=";
        auto const pos = body.find(needle);
        ASSERT_NE(pos, std::string::npos) << "tag " << tag << " not found in body";
        EXPECT_GE(pos, last_pos) << "tag " << tag << " out of ascending order";
        last_pos = pos;
    }
}

// Shared build->frame->dict-parse pipeline. Returns the parsed MessageView
// (borrowing from `read_arena`/`dict`, both caller-owned and must outlive
// the returned view — mirrors app_message_read_scaffold.hpp's contract).
template <typename BuildFn>
std::optional<fixpp::wire::MessageView<fixpp::wire::access_mode::Index>> build_and_parse(
    BuildFn&& build_fn, std::span<std::byte> out, std::string& body_out,
    fixpp::dict::Dictionary const& dict, std::pmr::memory_resource* read_arena,
    std::vector<std::byte>& frame_storage) {
    auto built = build_fn(out);
    if (!built.has_value()) {
        ADD_FAILURE() << "build_<Msg> failed";
        return std::nullopt;
    }
    body_out = bytes_to_string(*built);
    fixpp::dict::table_view tv = dict.as_table_view();
    frame_storage = fixpp_test_support::make_frame("FIX.4.4", body_out);
    auto mv = fixpp_test_support::parse_dict(frame_storage, tv, read_arena);
    return mv;
}

}  // namespace

// ── 28 group-free messages ──────────────────────────────────────────────

TEST(BuilderRoundtrip067, NewOrderSingle) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kNewOrderSingleSeed;
    std::pmr::monotonic_buffer_resource arena{4096};

    fixpp::v44::NewOrderSingleArgs args{};
    args.cl_ord_id = seed.cl_ord_id;
    args.symbol = seed.symbol;
    args.side = seed.side;
    args.order_qty = make_decimal(seed.order_qty, &arena);
    args.price = make_decimal(seed.price, &arena);
    args.locate_reqd = seed.locate_reqd;
    args.encoded_text = seed.encoded_text;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_NewOrderSingle(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_NewOrderSingle failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {11, 38, 44, 54, 55, 114, 354, 355});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::NewOrderSingle fw{mv};

    expect_eq_field(fw.cl_ord_id(), seed.cl_ord_id, "cl_ord_id");
    expect_eq_field(fw.symbol(), seed.symbol, "symbol");
    expect_eq_field(fw.side(), seed.side, "side");
    expect_eq_decimal(fw.order_qty(&read_arena), seed.order_qty, &read_arena, "order_qty");
    expect_eq_decimal(fw.price(&read_arena), seed.price, &read_arena, "price");
    expect_eq_field(fw.locate_reqd(), seed.locate_reqd, "locate_reqd");
    expect_eq_field(fw.encoded_text(), seed.encoded_text, "encoded_text");
}

TEST(BuilderRoundtrip067, OrderCancelRequest) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kOrderCancelRequestSeed;

    fixpp::v44::OrderCancelRequestArgs args{};
    args.cl_ord_id = seed.cl_ord_id;
    args.order_id = seed.order_id;
    args.orig_cl_ord_id = seed.orig_cl_ord_id;
    args.side = seed.side;
    args.symbol = seed.symbol;
    args.transact_time = seed.transact_time;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_OrderCancelRequest(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_OrderCancelRequest failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {11, 37, 41, 54, 55, 60});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::OrderCancelRequest fw{mv};

    expect_eq_field(fw.cl_ord_id(), seed.cl_ord_id, "cl_ord_id");
    expect_eq_field(fw.order_id(), seed.order_id, "order_id");
    expect_eq_field(fw.orig_cl_ord_id(), seed.orig_cl_ord_id, "orig_cl_ord_id");
    expect_eq_field(fw.side(), seed.side, "side");
    expect_eq_field(fw.symbol(), seed.symbol, "symbol");
    expect_eq_field(fw.transact_time(), seed.transact_time, "transact_time");
}

TEST(BuilderRoundtrip067, OrderCancelReplaceRequest) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kOrderCancelReplaceRequestSeed;
    std::pmr::monotonic_buffer_resource arena{4096};

    fixpp::v44::OrderCancelReplaceRequestArgs args{};
    args.cl_ord_id = seed.cl_ord_id;
    args.order_id = seed.order_id;
    args.orig_cl_ord_id = seed.orig_cl_ord_id;
    args.price = make_decimal(seed.price, &arena);
    args.side = seed.side;
    args.symbol = seed.symbol;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_OrderCancelReplaceRequest(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_OrderCancelReplaceRequest failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {11, 37, 41, 44, 54, 55});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::OrderCancelReplaceRequest fw{mv};

    expect_eq_field(fw.cl_ord_id(), seed.cl_ord_id, "cl_ord_id");
    expect_eq_field(fw.order_id(), seed.order_id, "order_id");
    expect_eq_field(fw.orig_cl_ord_id(), seed.orig_cl_ord_id, "orig_cl_ord_id");
    expect_eq_decimal(fw.price(&read_arena), seed.price, &read_arena, "price");
    expect_eq_field(fw.side(), seed.side, "side");
    expect_eq_field(fw.symbol(), seed.symbol, "symbol");
}

TEST(BuilderRoundtrip067, OrderStatusRequest) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kOrderStatusRequestSeed;

    fixpp::v44::OrderStatusRequestArgs args{};
    args.cl_ord_id = seed.cl_ord_id;
    args.order_id = seed.order_id;
    args.side = seed.side;
    args.symbol = seed.symbol;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_OrderStatusRequest(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_OrderStatusRequest failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {11, 37, 54, 55});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::OrderStatusRequest fw{mv};

    expect_eq_field(fw.cl_ord_id(), seed.cl_ord_id, "cl_ord_id");
    expect_eq_field(fw.order_id(), seed.order_id, "order_id");
    expect_eq_field(fw.side(), seed.side, "side");
    expect_eq_field(fw.symbol(), seed.symbol, "symbol");
}

TEST(BuilderRoundtrip067, ExecutionReport) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kExecutionReportSeed;
    std::pmr::monotonic_buffer_resource arena{4096};

    fixpp::v44::ExecutionReportArgs args{};
    args.avg_px = make_decimal(seed.avg_px, &arena);
    args.cl_ord_id = seed.cl_ord_id;
    args.exec_id = seed.exec_id;
    args.order_id = seed.order_id;
    args.ord_status = seed.ord_status;
    args.side = seed.side;
    args.symbol = seed.symbol;
    args.leaves_qty = make_decimal(seed.leaves_qty, &arena);

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_ExecutionReport(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_ExecutionReport failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {6, 11, 17, 37, 39, 54, 55, 151});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::ExecutionReport fw{mv};

    expect_eq_decimal(fw.avg_px(&read_arena), seed.avg_px, &read_arena, "avg_px");
    expect_eq_field(fw.cl_ord_id(), seed.cl_ord_id, "cl_ord_id");
    expect_eq_field(fw.exec_id(), seed.exec_id, "exec_id");
    expect_eq_field(fw.order_id(), seed.order_id, "order_id");
    expect_eq_field(fw.ord_status(), seed.ord_status, "ord_status");
    expect_eq_field(fw.side(), seed.side, "side");
    expect_eq_field(fw.symbol(), seed.symbol, "symbol");
    expect_eq_decimal(fw.leaves_qty(&read_arena), seed.leaves_qty, &read_arena, "leaves_qty");
}

TEST(BuilderRoundtrip067, OrderCancelReject) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kOrderCancelRejectSeed;

    fixpp::v44::OrderCancelRejectArgs args{};
    args.cl_ord_id = seed.cl_ord_id;
    args.order_id = seed.order_id;
    args.ord_status = seed.ord_status;
    args.orig_cl_ord_id = seed.orig_cl_ord_id;
    args.cxl_rej_reason = seed.cxl_rej_reason;
    args.cxl_rej_response_to = seed.cxl_rej_response_to;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_OrderCancelReject(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_OrderCancelReject failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {11, 37, 39, 41, 102, 434});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::OrderCancelReject fw{mv};

    expect_eq_field(fw.cl_ord_id(), seed.cl_ord_id, "cl_ord_id");
    expect_eq_field(fw.order_id(), seed.order_id, "order_id");
    expect_eq_field(fw.ord_status(), seed.ord_status, "ord_status");
    expect_eq_field(fw.orig_cl_ord_id(), seed.orig_cl_ord_id, "orig_cl_ord_id");
    expect_eq_field(fw.cxl_rej_reason(), seed.cxl_rej_reason, "cxl_rej_reason");
    expect_eq_field(fw.cxl_rej_response_to(), seed.cxl_rej_response_to, "cxl_rej_response_to");
}

TEST(BuilderRoundtrip067, OrderMassCancelRequest) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kOrderMassCancelRequestSeed;

    fixpp::v44::OrderMassCancelRequestArgs args{};
    args.cl_ord_id = seed.cl_ord_id;
    args.side = seed.side;
    args.symbol = seed.symbol;
    args.transact_time = seed.transact_time;
    args.mass_cancel_request_type = seed.mass_cancel_request_type;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_OrderMassCancelRequest(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_OrderMassCancelRequest failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {11, 54, 55, 60, 530});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::OrderMassCancelRequest fw{mv};

    expect_eq_field(fw.cl_ord_id(), seed.cl_ord_id, "cl_ord_id");
    expect_eq_field(fw.side(), seed.side, "side");
    expect_eq_field(fw.symbol(), seed.symbol, "symbol");
    expect_eq_field(fw.transact_time(), seed.transact_time, "transact_time");
    expect_eq_field(fw.mass_cancel_request_type(), seed.mass_cancel_request_type,
                     "mass_cancel_request_type");
}

TEST(BuilderRoundtrip067, OrderMassCancelReport) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kOrderMassCancelReportSeed;

    fixpp::v44::OrderMassCancelReportArgs args{};
    args.cl_ord_id = seed.cl_ord_id;
    args.order_id = seed.order_id;
    args.side = seed.side;
    args.symbol = seed.symbol;
    args.mass_cancel_request_type = seed.mass_cancel_request_type;
    args.mass_cancel_response = seed.mass_cancel_response;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_OrderMassCancelReport(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_OrderMassCancelReport failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {11, 37, 54, 55, 530, 531});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::OrderMassCancelReport fw{mv};

    expect_eq_field(fw.cl_ord_id(), seed.cl_ord_id, "cl_ord_id");
    expect_eq_field(fw.order_id(), seed.order_id, "order_id");
    expect_eq_field(fw.side(), seed.side, "side");
    expect_eq_field(fw.symbol(), seed.symbol, "symbol");
    expect_eq_field(fw.mass_cancel_request_type(), seed.mass_cancel_request_type,
                     "mass_cancel_request_type");
    expect_eq_field(fw.mass_cancel_response(), seed.mass_cancel_response, "mass_cancel_response");
}

TEST(BuilderRoundtrip067, OrderMassStatusRequest) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kOrderMassStatusRequestSeed;

    fixpp::v44::OrderMassStatusRequestArgs args{};
    args.symbol = seed.symbol;
    args.side = seed.side;
    args.mass_status_req_id = seed.mass_status_req_id;
    args.mass_status_req_type = seed.mass_status_req_type;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_OrderMassStatusRequest(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_OrderMassStatusRequest failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {54, 55, 584, 585});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::OrderMassStatusRequest fw{mv};

    expect_eq_field(fw.symbol(), seed.symbol, "symbol");
    expect_eq_field(fw.side(), seed.side, "side");
    expect_eq_field(fw.mass_status_req_id(), seed.mass_status_req_id, "mass_status_req_id");
    expect_eq_field(fw.mass_status_req_type(), seed.mass_status_req_type, "mass_status_req_type");
}

TEST(BuilderRoundtrip067, MultilegOrderCancelReplace) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kMultilegOrderCancelReplaceSeed;
    std::pmr::monotonic_buffer_resource arena{4096};

    fixpp::v44::MultilegOrderCancelReplaceArgs args{};
    args.cl_ord_id = seed.cl_ord_id;
    args.order_id = seed.order_id;
    args.orig_cl_ord_id = seed.orig_cl_ord_id;
    args.price = make_decimal(seed.price, &arena);
    args.side = seed.side;
    args.symbol = seed.symbol;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_MultilegOrderCancelReplace(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_MultilegOrderCancelReplace failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {11, 37, 41, 44, 54, 55});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::MultilegOrderCancelReplace fw{mv};

    expect_eq_field(fw.cl_ord_id(), seed.cl_ord_id, "cl_ord_id");
    expect_eq_field(fw.order_id(), seed.order_id, "order_id");
    expect_eq_field(fw.orig_cl_ord_id(), seed.orig_cl_ord_id, "orig_cl_ord_id");
    expect_eq_decimal(fw.price(&read_arena), seed.price, &read_arena, "price");
    expect_eq_field(fw.side(), seed.side, "side");
    expect_eq_field(fw.symbol(), seed.symbol, "symbol");
}

TEST(BuilderRoundtrip067, CrossOrderCancelReplaceRequest) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kCrossOrderCancelReplaceRequestSeed;
    std::pmr::monotonic_buffer_resource arena{4096};

    fixpp::v44::CrossOrderCancelReplaceRequestArgs args{};
    args.order_id = seed.order_id;
    args.price = make_decimal(seed.price, &arena);
    args.symbol = seed.symbol;
    args.cross_id = seed.cross_id;
    args.cross_type = seed.cross_type;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_CrossOrderCancelReplaceRequest(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_CrossOrderCancelReplaceRequest failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {37, 44, 55, 548, 549});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::CrossOrderCancelReplaceRequest fw{mv};

    expect_eq_field(fw.order_id(), seed.order_id, "order_id");
    expect_eq_decimal(fw.price(&read_arena), seed.price, &read_arena, "price");
    expect_eq_field(fw.symbol(), seed.symbol, "symbol");
    expect_eq_field(fw.cross_id(), seed.cross_id, "cross_id");
    expect_eq_field(fw.cross_type(), seed.cross_type, "cross_type");
}

TEST(BuilderRoundtrip067, CrossOrderCancelRequest) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kCrossOrderCancelRequestSeed;

    fixpp::v44::CrossOrderCancelRequestArgs args{};
    args.order_id = seed.order_id;
    args.symbol = seed.symbol;
    args.cross_id = seed.cross_id;
    args.cross_type = seed.cross_type;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_CrossOrderCancelRequest(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_CrossOrderCancelRequest failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {37, 55, 548, 549});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::CrossOrderCancelRequest fw{mv};

    expect_eq_field(fw.order_id(), seed.order_id, "order_id");
    expect_eq_field(fw.symbol(), seed.symbol, "symbol");
    expect_eq_field(fw.cross_id(), seed.cross_id, "cross_id");
    expect_eq_field(fw.cross_type(), seed.cross_type, "cross_type");
}

TEST(BuilderRoundtrip067, MarketDataRequest) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kMarketDataRequestSeed;

    fixpp::v44::MarketDataRequestArgs args{};
    args.md_req_id = seed.md_req_id;
    args.subscription_request_type = seed.subscription_request_type;
    args.market_depth = seed.market_depth;
    args.aggregated_book = seed.aggregated_book;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_MarketDataRequest(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataRequest failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {262, 263, 264, 266});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::MarketDataRequest fw{mv};

    expect_eq_field(fw.md_req_id(), seed.md_req_id, "md_req_id");
    expect_eq_field(fw.subscription_request_type(), seed.subscription_request_type,
                     "subscription_request_type");
    expect_eq_field(fw.market_depth(), seed.market_depth, "market_depth");
    expect_eq_field(fw.aggregated_book(), seed.aggregated_book, "aggregated_book");
}

TEST(BuilderRoundtrip067, MarketDataRequestReject) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kMarketDataRequestRejectSeed;

    fixpp::v44::MarketDataRequestRejectArgs args{};
    args.md_req_id = seed.md_req_id;
    args.md_req_rej_reason = seed.md_req_rej_reason;
    args.text = seed.text;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_MarketDataRequestReject(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataRequestReject failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {58, 262, 281});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::MarketDataRequestReject fw{mv};

    expect_eq_field(fw.md_req_id(), seed.md_req_id, "md_req_id");
    expect_eq_field(fw.md_req_rej_reason(), seed.md_req_rej_reason, "md_req_rej_reason");
    expect_eq_field(fw.text(), seed.text, "text");
}

TEST(BuilderRoundtrip067, SecurityDefinitionRequest) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kSecurityDefinitionRequestSeed;

    fixpp::v44::SecurityDefinitionRequestArgs args{};
    args.symbol = seed.symbol;
    args.security_req_id = seed.security_req_id;
    args.security_request_type = seed.security_request_type;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_SecurityDefinitionRequest(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityDefinitionRequest failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {55, 320, 321});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::SecurityDefinitionRequest fw{mv};

    expect_eq_field(fw.symbol(), seed.symbol, "symbol");
    expect_eq_field(fw.security_req_id(), seed.security_req_id, "security_req_id");
    expect_eq_field(fw.security_request_type(), seed.security_request_type, "security_request_type");
}

TEST(BuilderRoundtrip067, SecurityDefinition) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kSecurityDefinitionSeed;

    fixpp::v44::SecurityDefinitionArgs args{};
    args.symbol = seed.symbol;
    args.security_req_id = seed.security_req_id;
    args.security_response_id = seed.security_response_id;
    args.security_response_type = seed.security_response_type;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_SecurityDefinition(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityDefinition failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {55, 320, 322, 323});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::SecurityDefinition fw{mv};

    expect_eq_field(fw.symbol(), seed.symbol, "symbol");
    expect_eq_field(fw.security_req_id(), seed.security_req_id, "security_req_id");
    expect_eq_field(fw.security_response_id(), seed.security_response_id, "security_response_id");
    expect_eq_field(fw.security_response_type(), seed.security_response_type,
                     "security_response_type");
}

TEST(BuilderRoundtrip067, SecurityStatusRequest) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kSecurityStatusRequestSeed;

    fixpp::v44::SecurityStatusRequestArgs args{};
    args.symbol = seed.symbol;
    args.security_status_req_id = seed.security_status_req_id;
    args.subscription_request_type = seed.subscription_request_type;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_SecurityStatusRequest(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityStatusRequest failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {55, 263, 324});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::SecurityStatusRequest fw{mv};

    expect_eq_field(fw.symbol(), seed.symbol, "symbol");
    expect_eq_field(fw.security_status_req_id(), seed.security_status_req_id,
                     "security_status_req_id");
    expect_eq_field(fw.subscription_request_type(), seed.subscription_request_type,
                     "subscription_request_type");
}

TEST(BuilderRoundtrip067, SecurityStatus) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kSecurityStatusSeed;

    fixpp::v44::SecurityStatusArgs args{};
    args.symbol = seed.symbol;
    args.security_status_req_id = seed.security_status_req_id;
    args.security_trading_status = seed.security_trading_status;
    args.unsolicited_indicator = seed.unsolicited_indicator;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_SecurityStatus(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityStatus failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {55, 324, 325, 326});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::SecurityStatus fw{mv};

    expect_eq_field(fw.symbol(), seed.symbol, "symbol");
    expect_eq_field(fw.security_status_req_id(), seed.security_status_req_id,
                     "security_status_req_id");
    expect_eq_field(fw.security_trading_status(), seed.security_trading_status,
                     "security_trading_status");
    expect_eq_field(fw.unsolicited_indicator(), seed.unsolicited_indicator, "unsolicited_indicator");
}

TEST(BuilderRoundtrip067, TradingSessionStatusRequest) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kTradingSessionStatusRequestSeed;

    fixpp::v44::TradingSessionStatusRequestArgs args{};
    args.trad_ses_req_id = seed.trad_ses_req_id;
    args.trading_session_id = seed.trading_session_id;
    args.subscription_request_type = seed.subscription_request_type;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_TradingSessionStatusRequest(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_TradingSessionStatusRequest failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {263, 335, 336});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::TradingSessionStatusRequest fw{mv};

    expect_eq_field(fw.trad_ses_req_id(), seed.trad_ses_req_id, "trad_ses_req_id");
    expect_eq_field(fw.trading_session_id(), seed.trading_session_id, "trading_session_id");
    expect_eq_field(fw.subscription_request_type(), seed.subscription_request_type,
                     "subscription_request_type");
}

TEST(BuilderRoundtrip067, TradingSessionStatus) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kTradingSessionStatusSeed;

    fixpp::v44::TradingSessionStatusArgs args{};
    args.trading_session_id = seed.trading_session_id;
    args.trad_ses_status = seed.trad_ses_status;
    args.unsolicited_indicator = seed.unsolicited_indicator;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_TradingSessionStatus(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_TradingSessionStatus failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {325, 336, 340});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::TradingSessionStatus fw{mv};

    expect_eq_field(fw.trading_session_id(), seed.trading_session_id, "trading_session_id");
    expect_eq_field(fw.trad_ses_status(), seed.trad_ses_status, "trad_ses_status");
    expect_eq_field(fw.unsolicited_indicator(), seed.unsolicited_indicator, "unsolicited_indicator");
}

TEST(BuilderRoundtrip067, MassQuoteAcknowledgement) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kMassQuoteAcknowledgementSeed;

    fixpp::v44::MassQuoteAcknowledgementArgs args{};
    args.quote_id = seed.quote_id;
    args.quote_status = seed.quote_status;
    args.quote_response_level = seed.quote_response_level;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_MassQuoteAcknowledgement(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_MassQuoteAcknowledgement failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {117, 297, 301});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::MassQuoteAcknowledgement fw{mv};

    expect_eq_field(fw.quote_id(), seed.quote_id, "quote_id");
    expect_eq_field(fw.quote_status(), seed.quote_status, "quote_status");
    expect_eq_field(fw.quote_response_level(), seed.quote_response_level, "quote_response_level");
}

TEST(BuilderRoundtrip067, Quote) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kQuoteSeed;
    std::pmr::monotonic_buffer_resource arena{4096};

    fixpp::v44::QuoteArgs args{};
    args.quote_id = seed.quote_id;
    args.symbol = seed.symbol;
    args.bid_px = make_decimal(seed.bid_px, &arena);
    args.offer_px = make_decimal(seed.offer_px, &arena);
    args.side = seed.side;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_Quote(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_Quote failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {54, 55, 117, 132, 133});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::Quote fw{mv};

    expect_eq_field(fw.quote_id(), seed.quote_id, "quote_id");
    expect_eq_field(fw.symbol(), seed.symbol, "symbol");
    expect_eq_decimal(fw.bid_px(&read_arena), seed.bid_px, &read_arena, "bid_px");
    expect_eq_decimal(fw.offer_px(&read_arena), seed.offer_px, &read_arena, "offer_px");
    expect_eq_field(fw.side(), seed.side, "side");
}

TEST(BuilderRoundtrip067, QuoteRequest) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kQuoteRequestSeed;

    fixpp::v44::QuoteRequestArgs args{};
    args.quote_req_id = seed.quote_req_id;
    args.cl_ord_id = seed.cl_ord_id;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_QuoteRequest(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_QuoteRequest failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {11, 131});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::QuoteRequest fw{mv};

    expect_eq_field(fw.quote_req_id(), seed.quote_req_id, "quote_req_id");
    expect_eq_field(fw.cl_ord_id(), seed.cl_ord_id, "cl_ord_id");
}

TEST(BuilderRoundtrip067, QuoteRequestReject) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kQuoteRequestRejectSeed;

    fixpp::v44::QuoteRequestRejectArgs args{};
    args.quote_req_id = seed.quote_req_id;
    args.quote_request_reject_reason = seed.quote_request_reject_reason;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_QuoteRequestReject(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_QuoteRequestReject failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {131, 658});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::QuoteRequestReject fw{mv};

    expect_eq_field(fw.quote_req_id(), seed.quote_req_id, "quote_req_id");
    expect_eq_field(fw.quote_request_reject_reason(), seed.quote_request_reject_reason,
                     "quote_request_reject_reason");
}

TEST(BuilderRoundtrip067, QuoteCancel) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kQuoteCancelSeed;

    fixpp::v44::QuoteCancelArgs args{};
    args.quote_id = seed.quote_id;
    args.quote_cancel_type = seed.quote_cancel_type;
    args.quote_response_level = seed.quote_response_level;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_QuoteCancel(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_QuoteCancel failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {117, 298, 301});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::QuoteCancel fw{mv};

    expect_eq_field(fw.quote_id(), seed.quote_id, "quote_id");
    expect_eq_field(fw.quote_cancel_type(), seed.quote_cancel_type, "quote_cancel_type");
    expect_eq_field(fw.quote_response_level(), seed.quote_response_level, "quote_response_level");
}

TEST(BuilderRoundtrip067, QuoteStatusRequest) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kQuoteStatusRequestSeed;

    fixpp::v44::QuoteStatusRequestArgs args{};
    args.symbol = seed.symbol;
    args.quote_id = seed.quote_id;
    args.quote_status_req_id = seed.quote_status_req_id;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_QuoteStatusRequest(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_QuoteStatusRequest failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {55, 117, 649});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::QuoteStatusRequest fw{mv};

    expect_eq_field(fw.symbol(), seed.symbol, "symbol");
    expect_eq_field(fw.quote_id(), seed.quote_id, "quote_id");
    expect_eq_field(fw.quote_status_req_id(), seed.quote_status_req_id, "quote_status_req_id");
}

TEST(BuilderRoundtrip067, AllocationInstruction) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kAllocationInstructionSeed;
    std::pmr::monotonic_buffer_resource arena{4096};

    fixpp::v44::AllocationInstructionArgs args{};
    args.alloc_id = seed.alloc_id;
    args.alloc_trans_type = seed.alloc_trans_type;
    args.side = seed.side;
    args.symbol = seed.symbol;
    args.quantity = make_decimal(seed.quantity, &arena);
    args.avg_px = make_decimal(seed.avg_px, &arena);

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_AllocationInstruction(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_AllocationInstruction failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {6, 53, 54, 55, 70, 71});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::AllocationInstruction fw{mv};

    expect_eq_field(fw.alloc_id(), seed.alloc_id, "alloc_id");
    expect_eq_field(fw.alloc_trans_type(), seed.alloc_trans_type, "alloc_trans_type");
    expect_eq_field(fw.side(), seed.side, "side");
    expect_eq_field(fw.symbol(), seed.symbol, "symbol");
    expect_eq_decimal(fw.quantity(&read_arena), seed.quantity, &read_arena, "quantity");
    expect_eq_decimal(fw.avg_px(&read_arena), seed.avg_px, &read_arena, "avg_px");
}

TEST(BuilderRoundtrip067, AllocationInstructionAck) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kAllocationInstructionAckSeed;

    fixpp::v44::AllocationInstructionAckArgs args{};
    args.alloc_id = seed.alloc_id;
    args.alloc_status = seed.alloc_status;
    args.transact_time = seed.transact_time;

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_AllocationInstructionAck(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_AllocationInstructionAck failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {60, 70, 87});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::AllocationInstructionAck fw{mv};

    expect_eq_field(fw.alloc_id(), seed.alloc_id, "alloc_id");
    expect_eq_field(fw.alloc_status(), seed.alloc_status, "alloc_status");
    expect_eq_field(fw.transact_time(), seed.transact_time, "transact_time");
}

// ── 5 group-bearing messages (R5/G6 mandatory insurance) ───────────────────

TEST(BuilderRoundtrip067, NewOrderListGrouped) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kNewOrderListSeed;
    std::pmr::monotonic_buffer_resource arena{8192};

    fixpp::v44::NewOrderListOrdersPartyIDsPartySubIDsArgs sub_args{};
    sub_args.party_sub_id = seed.order.party.sub.party_sub_id;
    sub_args.party_sub_id_type = seed.order.party.sub.party_sub_id_type;
    std::array<fixpp::v44::NewOrderListOrdersPartyIDsPartySubIDsArgs, 1> subs{sub_args};

    fixpp::v44::NewOrderListOrdersPartyIDsArgs party_args{};
    party_args.party_id = seed.order.party.party_id;
    party_args.party_id_source = seed.order.party.party_id_source;
    party_args.party_role = seed.order.party.party_role;
    party_args.party_sub_i_ds = std::optional<
        std::span<const fixpp::v44::NewOrderListOrdersPartyIDsPartySubIDsArgs>>{
        std::span<const fixpp::v44::NewOrderListOrdersPartyIDsPartySubIDsArgs>{subs}};
    std::array<fixpp::v44::NewOrderListOrdersPartyIDsArgs, 1> parties{party_args};

    fixpp::v44::NewOrderListOrdersArgs order_args{};
    order_args.cl_ord_id = seed.order.cl_ord_id;
    order_args.list_seq_no = seed.order.list_seq_no;
    order_args.side = seed.order.side;
    order_args.symbol = seed.order.symbol;
    order_args.order_qty = make_decimal(seed.order.order_qty, &arena);
    order_args.party_i_ds = std::optional<std::span<const fixpp::v44::NewOrderListOrdersPartyIDsArgs>>{
        std::span<const fixpp::v44::NewOrderListOrdersPartyIDsArgs>{parties}};
    std::array<fixpp::v44::NewOrderListOrdersArgs, 1> orders{order_args};

    fixpp::v44::NewOrderListArgs args{};
    args.list_id = seed.list_id;
    args.bid_type = seed.bid_type;
    args.tot_no_orders = seed.tot_no_orders;
    args.orders = std::span<const fixpp::v44::NewOrderListOrdersArgs>{orders};  // REQUIRED group

    std::array<std::byte, 4096> out{};
    auto built = fixpp::v44::build_NewOrderList(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_NewOrderList failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {66, 68, 73, 394});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::NewOrderList fw{mv};

    expect_eq_field(fw.list_id(), seed.list_id, "list_id");
    expect_eq_field(fw.bid_type(), seed.bid_type, "bid_type");
    expect_eq_field(fw.tot_no_orders(), seed.tot_no_orders, "tot_no_orders");

    auto orders_r = fw.orders();
    ASSERT_EQ(orders_r.size(), 1U);
    auto order0 = orders_r[0];
    expect_eq_field(order0.cl_ord_id(), seed.order.cl_ord_id, "order.cl_ord_id");
    expect_eq_field(order0.list_seq_no(), seed.order.list_seq_no, "order.list_seq_no");
    expect_eq_field(order0.side(), seed.order.side, "order.side");
    expect_eq_field(order0.symbol(), seed.order.symbol, "order.symbol");
    expect_eq_decimal(order0.order_qty(&read_arena), seed.order.order_qty, &read_arena,
                       "order.order_qty");

    auto parties_r = order0.party_i_ds();
    ASSERT_EQ(parties_r.size(), 1U);
    auto party0 = parties_r[0];
    expect_eq_field(party0.party_id(), seed.order.party.party_id, "order.party.party_id");
    expect_eq_field(party0.party_id_source(), seed.order.party.party_id_source,
                     "order.party.party_id_source");
    expect_eq_field(party0.party_role(), seed.order.party.party_role, "order.party.party_role");

    auto subs_r = party0.party_sub_i_ds();
    ASSERT_EQ(subs_r.size(), 1U);
    auto sub0 = subs_r[0];
    expect_eq_field(sub0.party_sub_id(), seed.order.party.sub.party_sub_id,
                     "order.party.sub.party_sub_id");
    expect_eq_field(sub0.party_sub_id_type(), seed.order.party.sub.party_sub_id_type,
                     "order.party.sub.party_sub_id_type");
}

TEST(BuilderRoundtrip067, AllocationReportGrouped) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kAllocationReportSeed;
    std::pmr::monotonic_buffer_resource arena{8192};

    fixpp::v44::AllocationReportPartyIDsPartySubIDsArgs sub_args{};
    sub_args.party_sub_id = seed.party.sub.party_sub_id;
    sub_args.party_sub_id_type = seed.party.sub.party_sub_id_type;
    std::array<fixpp::v44::AllocationReportPartyIDsPartySubIDsArgs, 1> subs{sub_args};

    fixpp::v44::AllocationReportPartyIDsArgs party_args{};
    party_args.party_id = seed.party.party_id;
    party_args.party_id_source = seed.party.party_id_source;
    party_args.party_role = seed.party.party_role;
    party_args.party_sub_i_ds =
        std::optional<std::span<const fixpp::v44::AllocationReportPartyIDsPartySubIDsArgs>>{
            std::span<const fixpp::v44::AllocationReportPartyIDsPartySubIDsArgs>{subs}};
    std::array<fixpp::v44::AllocationReportPartyIDsArgs, 1> parties{party_args};

    fixpp::v44::AllocationReportArgs args{};
    args.alloc_report_id = seed.alloc_report_id;
    args.alloc_trans_type = seed.alloc_trans_type;
    args.alloc_report_type = seed.alloc_report_type;
    args.alloc_status = seed.alloc_status;
    args.side = seed.side;
    args.quantity = make_decimal(seed.quantity, &arena);
    args.avg_px = make_decimal(seed.avg_px, &arena);
    args.trade_date = seed.trade_date;
    args.symbol = seed.symbol;
    args.party_i_ds = std::optional<std::span<const fixpp::v44::AllocationReportPartyIDsArgs>>{
        std::span<const fixpp::v44::AllocationReportPartyIDsArgs>{parties}};

    std::array<std::byte, 4096> out{};
    auto built = fixpp::v44::build_AllocationReport(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_AllocationReport failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {6, 53, 54, 55, 71, 75, 87, 453, 755, 794});

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::AllocationReport fw{mv};

    expect_eq_field(fw.alloc_report_id(), seed.alloc_report_id, "alloc_report_id");
    expect_eq_field(fw.alloc_trans_type(), seed.alloc_trans_type, "alloc_trans_type");
    expect_eq_field(fw.alloc_report_type(), seed.alloc_report_type, "alloc_report_type");
    expect_eq_field(fw.alloc_status(), seed.alloc_status, "alloc_status");
    expect_eq_field(fw.side(), seed.side, "side");
    expect_eq_decimal(fw.quantity(&read_arena), seed.quantity, &read_arena, "quantity");
    expect_eq_decimal(fw.avg_px(&read_arena), seed.avg_px, &read_arena, "avg_px");
    expect_eq_field(fw.trade_date(), seed.trade_date, "trade_date");
    expect_eq_field(fw.symbol(), seed.symbol, "symbol");

    auto parties_r = fw.party_i_ds();
    ASSERT_EQ(parties_r.size(), 1U);
    auto party0 = parties_r[0];
    expect_eq_field(party0.party_id(), seed.party.party_id, "party.party_id");
    expect_eq_field(party0.party_id_source(), seed.party.party_id_source, "party.party_id_source");
    expect_eq_field(party0.party_role(), seed.party.party_role, "party.party_role");

    auto subs_r = party0.party_sub_i_ds();
    ASSERT_EQ(subs_r.size(), 1U);
    auto sub0 = subs_r[0];
    expect_eq_field(sub0.party_sub_id(), seed.party.sub.party_sub_id, "party.sub.party_sub_id");
    expect_eq_field(sub0.party_sub_id_type(), seed.party.sub.party_sub_id_type,
                     "party.sub.party_sub_id_type");
}

TEST(BuilderRoundtrip067, MarketDataSnapshotFullRefreshGrouped) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kMarketDataSnapshotFullRefreshSeed;
    std::pmr::monotonic_buffer_resource arena{4096};

    fixpp::v44::MarketDataSnapshotFullRefreshMDEntriesArgs entry_args{};
    entry_args.md_entry_type = seed.entry.md_entry_type;
    entry_args.md_entry_px = make_decimal(seed.entry.md_entry_px, &arena);
    entry_args.md_entry_size = make_decimal(seed.entry.md_entry_size, &arena);
    std::array<fixpp::v44::MarketDataSnapshotFullRefreshMDEntriesArgs, 1> entries{entry_args};

    fixpp::v44::MarketDataSnapshotFullRefreshArgs args{};
    args.symbol = seed.symbol;
    args.md_req_id = seed.md_req_id;
    args.md_entries =
        std::span<const fixpp::v44::MarketDataSnapshotFullRefreshMDEntriesArgs>{entries};  // REQUIRED

    std::array<std::byte, 2048> out{};
    auto built = fixpp::v44::build_MarketDataSnapshotFullRefresh(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataSnapshotFullRefresh failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {55, 262, 268});
    // RC#1 delimiter pin: W's NoMDEntries(268) delimiter is MDEntryType(269),
    // NOT MDUpdateAction(279) (X's delimiter — see the X test below).
    EXPECT_NE(body.find("\x01" "269="), std::string::npos)
        << "W entry must open on MDEntryType(269)";

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::MarketDataSnapshotFullRefresh fw{mv};

    expect_eq_field(fw.symbol(), seed.symbol, "symbol");
    expect_eq_field(fw.md_req_id(), seed.md_req_id, "md_req_id");

    auto entries_r = fw.md_entries();
    ASSERT_EQ(entries_r.size(), 1U);
    auto entry0 = entries_r[0];
    expect_eq_field(entry0.md_entry_type(), seed.entry.md_entry_type, "entry.md_entry_type");
    expect_eq_decimal(entry0.md_entry_px(&read_arena), seed.entry.md_entry_px, &read_arena,
                       "entry.md_entry_px");
    expect_eq_decimal(entry0.md_entry_size(&read_arena), seed.entry.md_entry_size, &read_arena,
                       "entry.md_entry_size");
}

TEST(BuilderRoundtrip067, MarketDataIncrementalRefreshGrouped) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kMarketDataIncrementalRefreshSeed;
    std::pmr::monotonic_buffer_resource arena{4096};

    fixpp::v44::MarketDataIncrementalRefreshMDEntriesArgs entry_args{};
    entry_args.md_update_action = seed.entry.md_update_action;
    entry_args.md_entry_type = seed.entry.md_entry_type;
    entry_args.md_entry_px = make_decimal(seed.entry.md_entry_px, &arena);
    std::array<fixpp::v44::MarketDataIncrementalRefreshMDEntriesArgs, 1> entries{entry_args};

    fixpp::v44::MarketDataIncrementalRefreshArgs args{};
    args.md_req_id = seed.md_req_id;
    args.md_entries =
        std::span<const fixpp::v44::MarketDataIncrementalRefreshMDEntriesArgs>{entries};  // REQUIRED

    std::array<std::byte, 2048> out{};
    auto built = fixpp::v44::build_MarketDataIncrementalRefresh(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataIncrementalRefresh failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {262, 268});
    // RC#1 delimiter pin: X's NoMDEntries(268) delimiter is
    // MDUpdateAction(279) — SAME no_tag as W, DIFFERENT delimiter. The
    // discriminating assertion: 279 must appear STRICTLY BEFORE 269 in the
    // entry (a version-wide/tag-sorted plan would wrongly put 269 first,
    // since 269 < 279).
    auto const pos279 = body.find("\x01" "279=");
    auto const pos269 = body.find("\x01" "269=");
    ASSERT_NE(pos279, std::string::npos) << "X entry must contain MDUpdateAction(279)";
    ASSERT_NE(pos269, std::string::npos) << "X entry must contain MDEntryType(269)";
    EXPECT_LT(pos279, pos269)
        << "X's NoMDEntries delimiter must be MDUpdateAction(279), emitted BEFORE MDEntryType(269) "
           "(declaration order, NOT tag order which would wrongly put 269 first)";

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::MarketDataIncrementalRefresh fw{mv};

    expect_eq_field(fw.md_req_id(), seed.md_req_id, "md_req_id");

    auto entries_r = fw.md_entries();
    ASSERT_EQ(entries_r.size(), 1U);
    auto entry0 = entries_r[0];
    expect_eq_field(entry0.md_update_action(), seed.entry.md_update_action, "entry.md_update_action");
    expect_eq_field(entry0.md_entry_type(), seed.entry.md_entry_type, "entry.md_entry_type");
    expect_eq_decimal(entry0.md_entry_px(&read_arena), seed.entry.md_entry_px, &read_arena,
                       "entry.md_entry_px");
}

TEST(BuilderRoundtrip067, MassQuoteGrouped) {
    using namespace fixpp_test_support::seeds067;
    auto const& seed = kMassQuoteSeed;
    std::pmr::monotonic_buffer_resource arena{4096};

    fixpp::v44::MassQuoteQuoteSetsQuoteEntriesArgs entry_args{};
    entry_args.quote_entry_id = seed.set.entry.quote_entry_id;
    entry_args.bid_px = make_decimal(seed.set.entry.bid_px, &arena);
    entry_args.offer_px = make_decimal(seed.set.entry.offer_px, &arena);
    std::array<fixpp::v44::MassQuoteQuoteSetsQuoteEntriesArgs, 1> entries{entry_args};

    fixpp::v44::MassQuoteQuoteSetsArgs set_args{};
    set_args.quote_set_id = seed.set.quote_set_id;
    set_args.tot_no_quote_entries = seed.set.tot_no_quote_entries;
    set_args.quote_entries =
        std::span<const fixpp::v44::MassQuoteQuoteSetsQuoteEntriesArgs>{entries};  // REQUIRED
    std::array<fixpp::v44::MassQuoteQuoteSetsArgs, 1> sets{set_args};

    fixpp::v44::MassQuoteArgs args{};
    args.quote_id = seed.quote_id;
    args.quote_sets = std::span<const fixpp::v44::MassQuoteQuoteSetsArgs>{sets};  // REQUIRED

    std::array<std::byte, 2048> out{};
    auto built = fixpp::v44::build_MassQuote(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_MassQuote failed";
    std::string body = bytes_to_string(*built);
    assert_body_structure(body, {117, 296});
    // Nested-depth insurance: outer set delimiter QuoteSetID(302), nested
    // entry delimiter QuoteEntryID(299) — both present.
    EXPECT_NE(body.find("\x01" "302="), std::string::npos) << "outer set delimiter QuoteSetID(302)";
    EXPECT_NE(body.find("\x01" "299="), std::string::npos) << "nested entry delimiter QuoteEntryID(299)";

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);
    fixpp::v44::MassQuote fw{mv};

    expect_eq_field(fw.quote_id(), seed.quote_id, "quote_id");

    auto sets_r = fw.quote_sets();
    ASSERT_EQ(sets_r.size(), 1U);
    auto set0 = sets_r[0];
    expect_eq_field(set0.quote_set_id(), seed.set.quote_set_id, "set.quote_set_id");
    expect_eq_field(set0.tot_no_quote_entries(), seed.set.tot_no_quote_entries,
                     "set.tot_no_quote_entries");

    auto entries_r = set0.quote_entries();
    ASSERT_EQ(entries_r.size(), 1U);
    auto entry0 = entries_r[0];
    expect_eq_field(entry0.quote_entry_id(), seed.set.entry.quote_entry_id, "set.entry.quote_entry_id");
    expect_eq_decimal(entry0.bid_px(&read_arena), seed.set.entry.bid_px, &read_arena,
                       "set.entry.bid_px");
    expect_eq_decimal(entry0.offer_px(&read_arena), seed.set.entry.offer_px, &read_arena,
                       "set.entry.offer_px");
}
