// SPDX-License-Identifier: AGPL-3.0-or-later
// test_077_allversions_builder_roundtrip_vlatest.cpp
//
// GENERATED (host-side, not codegen product) by a one-off T016 generator
// script from the regenerated fixpp::vlatest::Builders.hpp -- checked in as a
// static, reviewable .cpp (mirrors the shape of hand-authored
// tests/session/test_069_all_families_roundtrip.cpp, applied to every
// in-scope vlatest message so none is skipped). Values are DETERMINISTIC,
// CHOSEN-before-build literals (string "<Msg>_<field>", char '1', bool
// true->"Y", int64 = the field's own FIX tag, decimal "10.5") -- never
// captured post-hoc from a build (feedback_coverage_push_enshrines_bugs) --
// asserted byte/value-for-byte/value on an independent dict-driven readback
// (fixpp::wire::Parser<Index>). Per message: up to 6 top-level
// scalar fields, at most 2 per C++ scalar category
// (string_view/char/bool/int64/decimal_t) in declaration order -- type-
// diverse coverage per message, not exhaustive-field (069 precedent: a
// handful of fields per message, not every field). Repeating groups are
// left unset (optional spans default-empty; build_<Msg> never validates
// group cardinality) -- deep-group descent for vlatest is covered separately
// (vlatest: test_077_vlatest_builder_roundtrip.cpp T012; v44: test_069_all_
// families_roundtrip.cpp's TradeCaptureReport case).
//
// specs/077-builder-args-dedup/tasks.md T016 (SC-003: 100% of the vlatest
// in-scope application set, zero skips -- set-equality, not per-field
// exhaustiveness).

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/vlatest/all.hpp>  // GENERATED (077) -- build_<Msg>/<Msg>Args
#include <fixpp/vlatest/Messages.hpp>
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

// Local dict loader (mirrors test_077_vlatest_builder_roundtrip.cpp's
// load_vlatest() -- 074's native Orchestra reader). Consuming target
// defines FIXPP_ORCHESTRA_DATA_DIR.
fixpp::dict::Dictionary load_dict(std::pmr::memory_resource* mr) {
    fixpp::dict::OrchestraLoader loader;
    return loader.load(std::string(FIXPP_ORCHESTRA_DATA_DIR) + "/OrchestraFIXLatest.xml", mr);
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

class AllVersionsRoundtrip077Vlatest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        dict_arena_ = new std::pmr::monotonic_buffer_resource(65536);
        dict_ = new fixpp::dict::Dictionary(load_dict(dict_arena_));
        tv_ = new fixpp::dict::table_view(dict_->as_table_view());
    }

    static void TearDownTestSuite() {
        delete tv_;
        delete dict_;
        delete dict_arena_;
        tv_ = nullptr;
        dict_ = nullptr;
        dict_arena_ = nullptr;
    }

    std::array<std::byte, 131072> out{};
    std::pmr::monotonic_buffer_resource read_arena{65536};

    static std::pmr::monotonic_buffer_resource* dict_arena_;
    static fixpp::dict::Dictionary* dict_;
    static fixpp::dict::table_view* tv_;
};

std::pmr::monotonic_buffer_resource* AllVersionsRoundtrip077Vlatest::dict_arena_ = nullptr;
fixpp::dict::Dictionary* AllVersionsRoundtrip077Vlatest::dict_ = nullptr;
fixpp::dict::table_view* AllVersionsRoundtrip077Vlatest::tv_ = nullptr;

TEST_F(AllVersionsRoundtrip077Vlatest, IOI) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::IOIArgs args{};
    args.currency = "IOI_currency";
    args.security_id_source = "IOI_security_id_source";
    args.ioi_qlty_ind = '1';
    args.ioi_trans_type = '1';
    args.order_qty = make_decimal("10.5", &arena);
    args.price = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_IOI(out, args);
    ASSERT_TRUE(built.has_value()) << "build_IOI failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built IOI frame failed (see ADD_FAILURE above)";
    expect_text(mv, 15, "IOI_currency", "currency");
    expect_text(mv, 22, "IOI_security_id_source", "security_id_source");
    expect_text(mv, 25, "1", "ioi_qlty_ind");
    expect_text(mv, 28, "1", "ioi_trans_type");
    expect_decimal(mv, 38, "10.5", &read_arena, "order_qty");
    expect_decimal(mv, 44, "10.5", &read_arena, "price");
}

TEST_F(AllVersionsRoundtrip077Vlatest, Advertisement) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::AdvertisementArgs args{};
    args.adv_id = "Advertisement_adv_id";
    args.adv_ref_id = "Advertisement_adv_ref_id";
    args.adv_side = '1';
    args.price = make_decimal("10.5", &arena);
    args.quantity = make_decimal("10.5", &arena);
    args.put_or_call = 201;
    auto built = fixpp::vlatest::build_Advertisement(out, args);
    ASSERT_TRUE(built.has_value()) << "build_Advertisement failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built Advertisement frame failed (see ADD_FAILURE above)";
    expect_text(mv, 2, "Advertisement_adv_id", "adv_id");
    expect_text(mv, 3, "Advertisement_adv_ref_id", "adv_ref_id");
    expect_text(mv, 4, "1", "adv_side");
    expect_decimal(mv, 44, "10.5", &read_arena, "price");
    expect_decimal(mv, 53, "10.5", &read_arena, "quantity");
    expect_text(mv, 201, "201", "put_or_call");
}

TEST_F(AllVersionsRoundtrip077Vlatest, ExecutionReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::ExecutionReportArgs args{};
    args.account = "ExecutionReport_account";
    args.avg_px = make_decimal("10.5", &arena);
    args.cl_ord_id = "ExecutionReport_cl_ord_id";
    args.commission = make_decimal("10.5", &arena);
    args.comm_type = '1';
    args.exec_inst = '1';
    auto built = fixpp::vlatest::build_ExecutionReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_ExecutionReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built ExecutionReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "ExecutionReport_account", "account");
    expect_decimal(mv, 6, "10.5", &read_arena, "avg_px");
    expect_text(mv, 11, "ExecutionReport_cl_ord_id", "cl_ord_id");
    expect_decimal(mv, 12, "10.5", &read_arena, "commission");
    expect_text(mv, 13, "1", "comm_type");
    expect_text(mv, 18, "1", "exec_inst");
}

TEST_F(AllVersionsRoundtrip077Vlatest, OrderCancelReject) {
    fixpp::vlatest::OrderCancelRejectArgs args{};
    args.account = "OrderCancelReject_account";
    args.cl_ord_id = "OrderCancelReject_cl_ord_id";
    args.ord_status = '1';
    args.cxl_rej_reason = 102;
    args.cxl_rej_response_to = '1';
    args.account_type = 581;
    auto built = fixpp::vlatest::build_OrderCancelReject(out, args);
    ASSERT_TRUE(built.has_value()) << "build_OrderCancelReject failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built OrderCancelReject frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "OrderCancelReject_account", "account");
    expect_text(mv, 11, "OrderCancelReject_cl_ord_id", "cl_ord_id");
    expect_text(mv, 39, "1", "ord_status");
    expect_text(mv, 102, "102", "cxl_rej_reason");
    expect_text(mv, 434, "1", "cxl_rej_response_to");
    expect_text(mv, 581, "581", "account_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, DerivativeSecurityList) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::DerivativeSecurityListArgs args{};
    args.transact_time = "DerivativeSecurityList_transact_time";
    args.underlying_coupon_payment_date = "DerivativeSecurityList_underlying_coupon_payment_date";
    args.underlying_repurchase_term = 244;
    args.underlying_repurchase_rate = make_decimal("10.5", &arena);
    args.underlying_factor = make_decimal("10.5", &arena);
    args.underlying_put_or_call = 315;
    auto built = fixpp::vlatest::build_DerivativeSecurityList(out, args);
    ASSERT_TRUE(built.has_value()) << "build_DerivativeSecurityList failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built DerivativeSecurityList frame failed (see ADD_FAILURE above)";
    expect_text(mv, 60, "DerivativeSecurityList_transact_time", "transact_time");
    expect_text(mv, 241, "DerivativeSecurityList_underlying_coupon_payment_date", "underlying_coupon_payment_date");
    expect_text(mv, 244, "244", "underlying_repurchase_term");
    expect_decimal(mv, 245, "10.5", &read_arena, "underlying_repurchase_rate");
    expect_decimal(mv, 246, "10.5", &read_arena, "underlying_factor");
    expect_text(mv, 315, "315", "underlying_put_or_call");
}

TEST_F(AllVersionsRoundtrip077Vlatest, NewOrderMultileg) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::NewOrderMultilegArgs args{};
    args.account = "NewOrderMultileg_account";
    args.cl_ord_id = "NewOrderMultileg_cl_ord_id";
    args.commission = make_decimal("10.5", &arena);
    args.comm_type = '1';
    args.exec_inst = '1';
    args.order_qty = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_NewOrderMultileg(out, args);
    ASSERT_TRUE(built.has_value()) << "build_NewOrderMultileg failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built NewOrderMultileg frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "NewOrderMultileg_account", "account");
    expect_text(mv, 11, "NewOrderMultileg_cl_ord_id", "cl_ord_id");
    expect_decimal(mv, 12, "10.5", &read_arena, "commission");
    expect_text(mv, 13, "1", "comm_type");
    expect_text(mv, 18, "1", "exec_inst");
    expect_decimal(mv, 38, "10.5", &read_arena, "order_qty");
}

TEST_F(AllVersionsRoundtrip077Vlatest, MultilegOrderCancelReplace) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::MultilegOrderCancelReplaceArgs args{};
    args.account = "MultilegOrderCancelReplace_account";
    args.cl_ord_id = "MultilegOrderCancelReplace_cl_ord_id";
    args.commission = make_decimal("10.5", &arena);
    args.comm_type = '1';
    args.exec_inst = '1';
    args.order_qty = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_MultilegOrderCancelReplace(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MultilegOrderCancelReplace failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MultilegOrderCancelReplace frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "MultilegOrderCancelReplace_account", "account");
    expect_text(mv, 11, "MultilegOrderCancelReplace_cl_ord_id", "cl_ord_id");
    expect_decimal(mv, 12, "10.5", &read_arena, "commission");
    expect_text(mv, 13, "1", "comm_type");
    expect_text(mv, 18, "1", "exec_inst");
    expect_decimal(mv, 38, "10.5", &read_arena, "order_qty");
}

TEST_F(AllVersionsRoundtrip077Vlatest, TradeCaptureReportRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::TradeCaptureReportRequestArgs args{};
    args.cl_ord_id = "TradeCaptureReportRequest_cl_ord_id";
    args.exec_id = "TradeCaptureReportRequest_exec_id";
    args.side = '1';
    args.exec_type = '1';
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_TradeCaptureReportRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TradeCaptureReportRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TradeCaptureReportRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 11, "TradeCaptureReportRequest_cl_ord_id", "cl_ord_id");
    expect_text(mv, 17, "TradeCaptureReportRequest_exec_id", "exec_id");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 150, "1", "exec_type");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
}

TEST_F(AllVersionsRoundtrip077Vlatest, TradeCaptureReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::TradeCaptureReportArgs args{};
    args.avg_px = make_decimal("10.5", &arena);
    args.currency = "TradeCaptureReport_currency";
    args.exec_id = "TradeCaptureReport_exec_id";
    args.last_px = make_decimal("10.5", &arena);
    args.exec_type = '1';
    args.put_or_call = 201;
    auto built = fixpp::vlatest::build_TradeCaptureReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TradeCaptureReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TradeCaptureReport frame failed (see ADD_FAILURE above)";
    expect_decimal(mv, 6, "10.5", &read_arena, "avg_px");
    expect_text(mv, 15, "TradeCaptureReport_currency", "currency");
    expect_text(mv, 17, "TradeCaptureReport_exec_id", "exec_id");
    expect_decimal(mv, 31, "10.5", &read_arena, "last_px");
    expect_text(mv, 150, "1", "exec_type");
    expect_text(mv, 201, "201", "put_or_call");
}

TEST_F(AllVersionsRoundtrip077Vlatest, OrderMassStatusRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::OrderMassStatusRequestArgs args{};
    args.account = "OrderMassStatusRequest_account";
    args.security_id_source = "OrderMassStatusRequest_security_id_source";
    args.side = '1';
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    auto built = fixpp::vlatest::build_OrderMassStatusRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_OrderMassStatusRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built OrderMassStatusRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "OrderMassStatusRequest_account", "account");
    expect_text(mv, 22, "OrderMassStatusRequest_security_id_source", "security_id_source");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
}

TEST_F(AllVersionsRoundtrip077Vlatest, QuoteRequestReject) {
    fixpp::vlatest::QuoteRequestRejectArgs args{};
    args.text = "QuoteRequestReject_text";
    args.quote_req_id = "QuoteRequestReject_quote_req_id";
    args.quote_request_reject_reason = 658;
    args.pre_trade_anonymity = true;
    args.private_quote = true;
    args.respondent_type = 1172;
    auto built = fixpp::vlatest::build_QuoteRequestReject(out, args);
    ASSERT_TRUE(built.has_value()) << "build_QuoteRequestReject failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built QuoteRequestReject frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "QuoteRequestReject_text", "text");
    expect_text(mv, 131, "QuoteRequestReject_quote_req_id", "quote_req_id");
    expect_text(mv, 658, "658", "quote_request_reject_reason");
    expect_text(mv, 1091, "Y", "pre_trade_anonymity");
    expect_text(mv, 1171, "Y", "private_quote");
    expect_text(mv, 1172, "1172", "respondent_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, RFQRequest) {
    fixpp::vlatest::RFQRequestArgs args{};
    args.subscription_request_type = '1';
    args.rfq_req_id = "RFQRequest_rfq_req_id";
    args.private_quote = true;
    auto built = fixpp::vlatest::build_RFQRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_RFQRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built RFQRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 263, "1", "subscription_request_type");
    expect_text(mv, 644, "RFQRequest_rfq_req_id", "rfq_req_id");
    expect_text(mv, 1171, "Y", "private_quote");
}

TEST_F(AllVersionsRoundtrip077Vlatest, QuoteStatusReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::QuoteStatusReportArgs args{};
    args.account = "QuoteStatusReport_account";
    args.commission = make_decimal("10.5", &arena);
    args.comm_type = '1';
    args.currency = "QuoteStatusReport_currency";
    args.order_qty = make_decimal("10.5", &arena);
    args.ord_type = '1';
    auto built = fixpp::vlatest::build_QuoteStatusReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_QuoteStatusReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built QuoteStatusReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "QuoteStatusReport_account", "account");
    expect_decimal(mv, 12, "10.5", &read_arena, "commission");
    expect_text(mv, 13, "1", "comm_type");
    expect_text(mv, 15, "QuoteStatusReport_currency", "currency");
    expect_decimal(mv, 38, "10.5", &read_arena, "order_qty");
    expect_text(mv, 40, "1", "ord_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, QuoteResponse) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::QuoteResponseArgs args{};
    args.account = "QuoteResponse_account";
    args.cl_ord_id = "QuoteResponse_cl_ord_id";
    args.commission = make_decimal("10.5", &arena);
    args.comm_type = '1';
    args.order_qty = make_decimal("10.5", &arena);
    args.ord_type = '1';
    auto built = fixpp::vlatest::build_QuoteResponse(out, args);
    ASSERT_TRUE(built.has_value()) << "build_QuoteResponse failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built QuoteResponse frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "QuoteResponse_account", "account");
    expect_text(mv, 11, "QuoteResponse_cl_ord_id", "cl_ord_id");
    expect_decimal(mv, 12, "10.5", &read_arena, "commission");
    expect_text(mv, 13, "1", "comm_type");
    expect_decimal(mv, 38, "10.5", &read_arena, "order_qty");
    expect_text(mv, 40, "1", "ord_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, Confirmation) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::ConfirmationArgs args{};
    args.avg_px = make_decimal("10.5", &arena);
    args.commission = make_decimal("10.5", &arena);
    args.comm_type = '1';
    args.currency = "Confirmation_currency";
    args.security_id_source = "Confirmation_security_id_source";
    args.side = '1';
    auto built = fixpp::vlatest::build_Confirmation(out, args);
    ASSERT_TRUE(built.has_value()) << "build_Confirmation failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built Confirmation frame failed (see ADD_FAILURE above)";
    expect_decimal(mv, 6, "10.5", &read_arena, "avg_px");
    expect_decimal(mv, 12, "10.5", &read_arena, "commission");
    expect_text(mv, 13, "1", "comm_type");
    expect_text(mv, 15, "Confirmation_currency", "currency");
    expect_text(mv, 22, "Confirmation_security_id_source", "security_id_source");
    expect_text(mv, 54, "1", "side");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PositionMaintenanceRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::PositionMaintenanceRequestArgs args{};
    args.account = "PositionMaintenanceRequest_account";
    args.currency = "PositionMaintenanceRequest_currency";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_PositionMaintenanceRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PositionMaintenanceRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PositionMaintenanceRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "PositionMaintenanceRequest_account", "account");
    expect_text(mv, 15, "PositionMaintenanceRequest_currency", "currency");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PositionMaintenanceReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::PositionMaintenanceReportArgs args{};
    args.account = "PositionMaintenanceReport_account";
    args.currency = "PositionMaintenanceReport_currency";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_PositionMaintenanceReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PositionMaintenanceReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PositionMaintenanceReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "PositionMaintenanceReport_account", "account");
    expect_text(mv, 15, "PositionMaintenanceReport_currency", "currency");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, RequestForPositions) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::RequestForPositionsArgs args{};
    args.account = "RequestForPositions_account";
    args.currency = "RequestForPositions_currency";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_RequestForPositions(out, args);
    ASSERT_TRUE(built.has_value()) << "build_RequestForPositions failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built RequestForPositions frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "RequestForPositions_account", "account");
    expect_text(mv, 15, "RequestForPositions_currency", "currency");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, RequestForPositionsAck) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::RequestForPositionsAckArgs args{};
    args.account = "RequestForPositionsAck_account";
    args.currency = "RequestForPositionsAck_currency";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_RequestForPositionsAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_RequestForPositionsAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built RequestForPositionsAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "RequestForPositionsAck_account", "account");
    expect_text(mv, 15, "RequestForPositionsAck_currency", "currency");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PositionReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::PositionReportArgs args{};
    args.account = "PositionReport_account";
    args.currency = "PositionReport_currency";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_PositionReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PositionReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PositionReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "PositionReport_account", "account");
    expect_text(mv, 15, "PositionReport_currency", "currency");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, TradeCaptureReportRequestAck) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::TradeCaptureReportRequestAckArgs args{};
    args.security_id_source = "TradeCaptureReportRequestAck_security_id_source";
    args.security_id = "TradeCaptureReportRequestAck_security_id";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_TradeCaptureReportRequestAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TradeCaptureReportRequestAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TradeCaptureReportRequestAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 22, "TradeCaptureReportRequestAck_security_id_source", "security_id_source");
    expect_text(mv, 48, "TradeCaptureReportRequestAck_security_id", "security_id");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, TradeCaptureReportAck) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::TradeCaptureReportAckArgs args{};
    args.avg_px = make_decimal("10.5", &arena);
    args.currency = "TradeCaptureReportAck_currency";
    args.exec_id = "TradeCaptureReportAck_exec_id";
    args.last_px = make_decimal("10.5", &arena);
    args.exec_type = '1';
    args.put_or_call = 201;
    auto built = fixpp::vlatest::build_TradeCaptureReportAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TradeCaptureReportAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TradeCaptureReportAck frame failed (see ADD_FAILURE above)";
    expect_decimal(mv, 6, "10.5", &read_arena, "avg_px");
    expect_text(mv, 15, "TradeCaptureReportAck_currency", "currency");
    expect_text(mv, 17, "TradeCaptureReportAck_exec_id", "exec_id");
    expect_decimal(mv, 31, "10.5", &read_arena, "last_px");
    expect_text(mv, 150, "1", "exec_type");
    expect_text(mv, 201, "201", "put_or_call");
}

TEST_F(AllVersionsRoundtrip077Vlatest, AllocationReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::AllocationReportArgs args{};
    args.avg_px = make_decimal("10.5", &arena);
    args.currency = "AllocationReport_currency";
    args.security_id_source = "AllocationReport_security_id_source";
    args.quantity = make_decimal("10.5", &arena);
    args.side = '1';
    args.alloc_trans_type = '1';
    auto built = fixpp::vlatest::build_AllocationReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_AllocationReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built AllocationReport frame failed (see ADD_FAILURE above)";
    expect_decimal(mv, 6, "10.5", &read_arena, "avg_px");
    expect_text(mv, 15, "AllocationReport_currency", "currency");
    expect_text(mv, 22, "AllocationReport_security_id_source", "security_id_source");
    expect_decimal(mv, 53, "10.5", &read_arena, "quantity");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 71, "1", "alloc_trans_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, AllocationReportAck) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::AllocationReportAckArgs args{};
    args.security_id_source = "AllocationReportAck_security_id_source";
    args.security_id = "AllocationReportAck_security_id";
    args.quantity = make_decimal("10.5", &arena);
    args.alloc_trans_type = '1';
    args.alloc_status = 87;
    args.alloc_rej_code = 88;
    auto built = fixpp::vlatest::build_AllocationReportAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_AllocationReportAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built AllocationReportAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 22, "AllocationReportAck_security_id_source", "security_id_source");
    expect_text(mv, 48, "AllocationReportAck_security_id", "security_id");
    expect_decimal(mv, 53, "10.5", &read_arena, "quantity");
    expect_text(mv, 71, "1", "alloc_trans_type");
    expect_text(mv, 87, "87", "alloc_status");
    expect_text(mv, 88, "88", "alloc_rej_code");
}

TEST_F(AllVersionsRoundtrip077Vlatest, ConfirmationAck) {
    fixpp::vlatest::ConfirmationAckArgs args{};
    args.text = "ConfirmationAck_text";
    args.transact_time = "ConfirmationAck_transact_time";
    args.match_status = '1';
    args.confirm_rej_reason = 774;
    args.affirm_status = 940;
    auto built = fixpp::vlatest::build_ConfirmationAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_ConfirmationAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built ConfirmationAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "ConfirmationAck_text", "text");
    expect_text(mv, 60, "ConfirmationAck_transact_time", "transact_time");
    expect_text(mv, 573, "1", "match_status");
    expect_text(mv, 774, "774", "confirm_rej_reason");
    expect_text(mv, 940, "940", "affirm_status");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SettlementInstructionRequest) {
    fixpp::vlatest::SettlementInstructionRequestArgs args{};
    args.side = '1';
    args.transact_time = "SettlementInstructionRequest_transact_time";
    args.alloc_account = "SettlementInstructionRequest_alloc_account";
    args.stand_inst_db_type = 169;
    args.product = 460;
    auto built = fixpp::vlatest::build_SettlementInstructionRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SettlementInstructionRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SettlementInstructionRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 60, "SettlementInstructionRequest_transact_time", "transact_time");
    expect_text(mv, 79, "SettlementInstructionRequest_alloc_account", "alloc_account");
    expect_text(mv, 169, "169", "stand_inst_db_type");
    expect_text(mv, 460, "460", "product");
}

TEST_F(AllVersionsRoundtrip077Vlatest, AssignmentReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::AssignmentReportArgs args{};
    args.account = "AssignmentReport_account";
    args.currency = "AssignmentReport_currency";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_AssignmentReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_AssignmentReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built AssignmentReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "AssignmentReport_account", "account");
    expect_text(mv, 15, "AssignmentReport_currency", "currency");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, CollateralRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::CollateralRequestArgs args{};
    args.account = "CollateralRequest_account";
    args.cl_ord_id = "CollateralRequest_cl_ord_id";
    args.price = make_decimal("10.5", &arena);
    args.quantity = make_decimal("10.5", &arena);
    args.side = '1';
    args.put_or_call = 201;
    auto built = fixpp::vlatest::build_CollateralRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_CollateralRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built CollateralRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "CollateralRequest_account", "account");
    expect_text(mv, 11, "CollateralRequest_cl_ord_id", "cl_ord_id");
    expect_decimal(mv, 44, "10.5", &read_arena, "price");
    expect_decimal(mv, 53, "10.5", &read_arena, "quantity");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 201, "201", "put_or_call");
}

TEST_F(AllVersionsRoundtrip077Vlatest, CollateralAssignment) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::CollateralAssignmentArgs args{};
    args.account = "CollateralAssignment_account";
    args.cl_ord_id = "CollateralAssignment_cl_ord_id";
    args.price = make_decimal("10.5", &arena);
    args.quantity = make_decimal("10.5", &arena);
    args.side = '1';
    args.stand_inst_db_type = 169;
    auto built = fixpp::vlatest::build_CollateralAssignment(out, args);
    ASSERT_TRUE(built.has_value()) << "build_CollateralAssignment failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built CollateralAssignment frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "CollateralAssignment_account", "account");
    expect_text(mv, 11, "CollateralAssignment_cl_ord_id", "cl_ord_id");
    expect_decimal(mv, 44, "10.5", &read_arena, "price");
    expect_decimal(mv, 53, "10.5", &read_arena, "quantity");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 169, "169", "stand_inst_db_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, CollateralResponse) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::CollateralResponseArgs args{};
    args.account = "CollateralResponse_account";
    args.cl_ord_id = "CollateralResponse_cl_ord_id";
    args.price = make_decimal("10.5", &arena);
    args.quantity = make_decimal("10.5", &arena);
    args.side = '1';
    args.put_or_call = 201;
    auto built = fixpp::vlatest::build_CollateralResponse(out, args);
    ASSERT_TRUE(built.has_value()) << "build_CollateralResponse failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built CollateralResponse frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "CollateralResponse_account", "account");
    expect_text(mv, 11, "CollateralResponse_cl_ord_id", "cl_ord_id");
    expect_decimal(mv, 44, "10.5", &read_arena, "price");
    expect_decimal(mv, 53, "10.5", &read_arena, "quantity");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 201, "201", "put_or_call");
}

TEST_F(AllVersionsRoundtrip077Vlatest, News) {
    fixpp::vlatest::NewsArgs args{};
    args.orig_time = "News_orig_time";
    args.urgency = '1';
    args.raw_data = "News_raw_data";
    args.appl_seq_num = 1181;
    args.appl_last_seq_num = 1350;
    args.appl_resend_flag = true;
    auto built = fixpp::vlatest::build_News(out, args);
    ASSERT_TRUE(built.has_value()) << "build_News failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built News frame failed (see ADD_FAILURE above)";
    expect_text(mv, 42, "News_orig_time", "orig_time");
    expect_text(mv, 61, "1", "urgency");
    expect_text(mv, 96, "News_raw_data", "raw_data");
    expect_text(mv, 1181, "1181", "appl_seq_num");
    expect_text(mv, 1350, "1350", "appl_last_seq_num");
    expect_text(mv, 1352, "Y", "appl_resend_flag");
}

TEST_F(AllVersionsRoundtrip077Vlatest, CollateralReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::CollateralReportArgs args{};
    args.account = "CollateralReport_account";
    args.cl_ord_id = "CollateralReport_cl_ord_id";
    args.price = make_decimal("10.5", &arena);
    args.quantity = make_decimal("10.5", &arena);
    args.side = '1';
    args.stand_inst_db_type = 169;
    auto built = fixpp::vlatest::build_CollateralReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_CollateralReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built CollateralReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "CollateralReport_account", "account");
    expect_text(mv, 11, "CollateralReport_cl_ord_id", "cl_ord_id");
    expect_decimal(mv, 44, "10.5", &read_arena, "price");
    expect_decimal(mv, 53, "10.5", &read_arena, "quantity");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 169, "169", "stand_inst_db_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, CollateralInquiry) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::CollateralInquiryArgs args{};
    args.account = "CollateralInquiry_account";
    args.cl_ord_id = "CollateralInquiry_cl_ord_id";
    args.price = make_decimal("10.5", &arena);
    args.quantity = make_decimal("10.5", &arena);
    args.side = '1';
    args.stand_inst_db_type = 169;
    auto built = fixpp::vlatest::build_CollateralInquiry(out, args);
    ASSERT_TRUE(built.has_value()) << "build_CollateralInquiry failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built CollateralInquiry frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "CollateralInquiry_account", "account");
    expect_text(mv, 11, "CollateralInquiry_cl_ord_id", "cl_ord_id");
    expect_decimal(mv, 44, "10.5", &read_arena, "price");
    expect_decimal(mv, 53, "10.5", &read_arena, "quantity");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 169, "169", "stand_inst_db_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, NetworkCounterpartySystemStatusRequest) {
    fixpp::vlatest::NetworkCounterpartySystemStatusRequestArgs args{};
    args.network_request_id = "NetworkCounterpartySystemStatusRequest_network_request_id";
    args.network_request_type = 935;
    auto built = fixpp::vlatest::build_NetworkCounterpartySystemStatusRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_NetworkCounterpartySystemStatusRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built NetworkCounterpartySystemStatusRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 933, "NetworkCounterpartySystemStatusRequest_network_request_id", "network_request_id");
    expect_text(mv, 935, "935", "network_request_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, NetworkCounterpartySystemStatusResponse) {
    fixpp::vlatest::NetworkCounterpartySystemStatusResponseArgs args{};
    args.network_response_id = "NetworkCounterpartySystemStatusResponse_network_response_id";
    args.network_request_id = "NetworkCounterpartySystemStatusResponse_network_request_id";
    args.network_status_response_type = 937;
    auto built = fixpp::vlatest::build_NetworkCounterpartySystemStatusResponse(out, args);
    ASSERT_TRUE(built.has_value()) << "build_NetworkCounterpartySystemStatusResponse failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built NetworkCounterpartySystemStatusResponse frame failed (see ADD_FAILURE above)";
    expect_text(mv, 932, "NetworkCounterpartySystemStatusResponse_network_response_id", "network_response_id");
    expect_text(mv, 933, "NetworkCounterpartySystemStatusResponse_network_request_id", "network_request_id");
    expect_text(mv, 937, "937", "network_status_response_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, UserRequest) {
    fixpp::vlatest::UserRequestArgs args{};
    args.raw_data = "UserRequest_raw_data";
    args.username = "UserRequest_username";
    args.user_request_type = 924;
    args.encrypted_password_method = 1400;
    auto built = fixpp::vlatest::build_UserRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_UserRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built UserRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 96, "UserRequest_raw_data", "raw_data");
    expect_text(mv, 553, "UserRequest_username", "username");
    expect_text(mv, 924, "924", "user_request_type");
    expect_text(mv, 1400, "1400", "encrypted_password_method");
}

TEST_F(AllVersionsRoundtrip077Vlatest, UserResponse) {
    fixpp::vlatest::UserResponseArgs args{};
    args.username = "UserResponse_username";
    args.user_request_id = "UserResponse_user_request_id";
    args.user_status = 926;
    auto built = fixpp::vlatest::build_UserResponse(out, args);
    ASSERT_TRUE(built.has_value()) << "build_UserResponse failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built UserResponse frame failed (see ADD_FAILURE above)";
    expect_text(mv, 553, "UserResponse_username", "username");
    expect_text(mv, 923, "UserResponse_user_request_id", "user_request_id");
    expect_text(mv, 926, "926", "user_status");
}

TEST_F(AllVersionsRoundtrip077Vlatest, CollateralInquiryAck) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::CollateralInquiryAckArgs args{};
    args.account = "CollateralInquiryAck_account";
    args.cl_ord_id = "CollateralInquiryAck_cl_ord_id";
    args.quantity = make_decimal("10.5", &arena);
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    auto built = fixpp::vlatest::build_CollateralInquiryAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_CollateralInquiryAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built CollateralInquiryAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "CollateralInquiryAck_account", "account");
    expect_text(mv, 11, "CollateralInquiryAck_cl_ord_id", "cl_ord_id");
    expect_decimal(mv, 53, "10.5", &read_arena, "quantity");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
}

TEST_F(AllVersionsRoundtrip077Vlatest, ConfirmationRequest) {
    fixpp::vlatest::ConfirmationRequestArgs args{};
    args.text = "ConfirmationRequest_text";
    args.transact_time = "ConfirmationRequest_transact_time";
    args.alloc_acct_id_source = 661;
    args.confirm_type = 773;
    auto built = fixpp::vlatest::build_ConfirmationRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_ConfirmationRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built ConfirmationRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "ConfirmationRequest_text", "text");
    expect_text(mv, 60, "ConfirmationRequest_transact_time", "transact_time");
    expect_text(mv, 661, "661", "alloc_acct_id_source");
    expect_text(mv, 773, "773", "confirm_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, TradingSessionListRequest) {
    fixpp::vlatest::TradingSessionListRequestArgs args{};
    args.security_exchange = "TradingSessionListRequest_security_exchange";
    args.subscription_request_type = '1';
    args.trad_ses_req_id = "TradingSessionListRequest_trad_ses_req_id";
    args.trad_ses_method = 338;
    args.trad_ses_mode = 339;
    auto built = fixpp::vlatest::build_TradingSessionListRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TradingSessionListRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TradingSessionListRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 207, "TradingSessionListRequest_security_exchange", "security_exchange");
    expect_text(mv, 263, "1", "subscription_request_type");
    expect_text(mv, 335, "TradingSessionListRequest_trad_ses_req_id", "trad_ses_req_id");
    expect_text(mv, 338, "338", "trad_ses_method");
    expect_text(mv, 339, "339", "trad_ses_mode");
}

TEST_F(AllVersionsRoundtrip077Vlatest, TradingSessionList) {
    fixpp::vlatest::TradingSessionListArgs args{};
    args.trad_ses_req_id = "TradingSessionList_trad_ses_req_id";
    args.appl_id = "TradingSessionList_appl_id";
    args.appl_seq_num = 1181;
    args.appl_last_seq_num = 1350;
    args.appl_resend_flag = true;
    auto built = fixpp::vlatest::build_TradingSessionList(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TradingSessionList failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TradingSessionList frame failed (see ADD_FAILURE above)";
    expect_text(mv, 335, "TradingSessionList_trad_ses_req_id", "trad_ses_req_id");
    expect_text(mv, 1180, "TradingSessionList_appl_id", "appl_id");
    expect_text(mv, 1181, "1181", "appl_seq_num");
    expect_text(mv, 1350, "1350", "appl_last_seq_num");
    expect_text(mv, 1352, "Y", "appl_resend_flag");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SecurityListUpdateReport) {
    fixpp::vlatest::SecurityListUpdateReportArgs args{};
    args.transact_time = "SecurityListUpdateReport_transact_time";
    args.corporate_action = '1';
    args.security_req_id = "SecurityListUpdateReport_security_req_id";
    args.tot_no_related_sym = 393;
    args.security_request_result = 560;
    args.last_fragment = true;
    auto built = fixpp::vlatest::build_SecurityListUpdateReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityListUpdateReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SecurityListUpdateReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 60, "SecurityListUpdateReport_transact_time", "transact_time");
    expect_text(mv, 292, "1", "corporate_action");
    expect_text(mv, 320, "SecurityListUpdateReport_security_req_id", "security_req_id");
    expect_text(mv, 393, "393", "tot_no_related_sym");
    expect_text(mv, 560, "560", "security_request_result");
    expect_text(mv, 893, "Y", "last_fragment");
}

TEST_F(AllVersionsRoundtrip077Vlatest, AdjustedPositionReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::AdjustedPositionReportArgs args{};
    args.pos_maint_rpt_ref_id = "AdjustedPositionReport_pos_maint_rpt_ref_id";
    args.clearing_business_date = "AdjustedPositionReport_clearing_business_date";
    args.pos_req_type = 724;
    args.settl_price = make_decimal("10.5", &arena);
    args.prior_settl_price = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_AdjustedPositionReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_AdjustedPositionReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built AdjustedPositionReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 714, "AdjustedPositionReport_pos_maint_rpt_ref_id", "pos_maint_rpt_ref_id");
    expect_text(mv, 715, "AdjustedPositionReport_clearing_business_date", "clearing_business_date");
    expect_text(mv, 724, "724", "pos_req_type");
    expect_decimal(mv, 730, "10.5", &read_arena, "settl_price");
    expect_decimal(mv, 734, "10.5", &read_arena, "prior_settl_price");
}

TEST_F(AllVersionsRoundtrip077Vlatest, AllocationInstructionAlert) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::AllocationInstructionAlertArgs args{};
    args.avg_px = make_decimal("10.5", &arena);
    args.currency = "AllocationInstructionAlert_currency";
    args.security_id_source = "AllocationInstructionAlert_security_id_source";
    args.quantity = make_decimal("10.5", &arena);
    args.side = '1';
    args.alloc_trans_type = '1';
    auto built = fixpp::vlatest::build_AllocationInstructionAlert(out, args);
    ASSERT_TRUE(built.has_value()) << "build_AllocationInstructionAlert failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built AllocationInstructionAlert frame failed (see ADD_FAILURE above)";
    expect_decimal(mv, 6, "10.5", &read_arena, "avg_px");
    expect_text(mv, 15, "AllocationInstructionAlert_currency", "currency");
    expect_text(mv, 22, "AllocationInstructionAlert_security_id_source", "security_id_source");
    expect_decimal(mv, 53, "10.5", &read_arena, "quantity");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 71, "1", "alloc_trans_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, ExecutionAck) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::ExecutionAckArgs args{};
    args.avg_px = make_decimal("10.5", &arena);
    args.cl_ord_id = "ExecutionAck_cl_ord_id";
    args.cum_qty = make_decimal("10.5", &arena);
    args.exec_id = "ExecutionAck_exec_id";
    args.side = '1';
    args.dk_reason = '1';
    auto built = fixpp::vlatest::build_ExecutionAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_ExecutionAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built ExecutionAck frame failed (see ADD_FAILURE above)";
    expect_decimal(mv, 6, "10.5", &read_arena, "avg_px");
    expect_text(mv, 11, "ExecutionAck_cl_ord_id", "cl_ord_id");
    expect_decimal(mv, 14, "10.5", &read_arena, "cum_qty");
    expect_text(mv, 17, "ExecutionAck_exec_id", "exec_id");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 127, "1", "dk_reason");
}

TEST_F(AllVersionsRoundtrip077Vlatest, ContraryIntentionReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::ContraryIntentionReportArgs args{};
    args.security_id_source = "ContraryIntentionReport_security_id_source";
    args.security_id = "ContraryIntentionReport_security_id";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_ContraryIntentionReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_ContraryIntentionReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built ContraryIntentionReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 22, "ContraryIntentionReport_security_id_source", "security_id_source");
    expect_text(mv, 48, "ContraryIntentionReport_security_id", "security_id");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SecurityDefinitionUpdateReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::SecurityDefinitionUpdateReportArgs args{};
    args.currency = "SecurityDefinitionUpdateReport_currency";
    args.security_id_source = "SecurityDefinitionUpdateReport_security_id_source";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.spread = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_SecurityDefinitionUpdateReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityDefinitionUpdateReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SecurityDefinitionUpdateReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 15, "SecurityDefinitionUpdateReport_currency", "currency");
    expect_text(mv, 22, "SecurityDefinitionUpdateReport_security_id_source", "security_id_source");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 218, "10.5", &read_arena, "spread");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SettlementObligationReport) {
    fixpp::vlatest::SettlementObligationReportArgs args{};
    args.text = "SettlementObligationReport_text";
    args.transact_time = "SettlementObligationReport_transact_time";
    args.settlement_cycle_no = 1153;
    args.settl_oblig_mode = 1159;
    args.appl_resend_flag = true;
    auto built = fixpp::vlatest::build_SettlementObligationReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SettlementObligationReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SettlementObligationReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "SettlementObligationReport_text", "text");
    expect_text(mv, 60, "SettlementObligationReport_transact_time", "transact_time");
    expect_text(mv, 1153, "1153", "settlement_cycle_no");
    expect_text(mv, 1159, "1159", "settl_oblig_mode");
    expect_text(mv, 1352, "Y", "appl_resend_flag");
}

TEST_F(AllVersionsRoundtrip077Vlatest, DerivativeSecurityListUpdateReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::DerivativeSecurityListUpdateReportArgs args{};
    args.transact_time = "DerivativeSecurityListUpdateReport_transact_time";
    args.underlying_coupon_payment_date = "DerivativeSecurityListUpdateReport_underlying_coupon_payment_date";
    args.underlying_repurchase_term = 244;
    args.underlying_repurchase_rate = make_decimal("10.5", &arena);
    args.underlying_factor = make_decimal("10.5", &arena);
    args.underlying_put_or_call = 315;
    auto built = fixpp::vlatest::build_DerivativeSecurityListUpdateReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_DerivativeSecurityListUpdateReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built DerivativeSecurityListUpdateReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 60, "DerivativeSecurityListUpdateReport_transact_time", "transact_time");
    expect_text(mv, 241, "DerivativeSecurityListUpdateReport_underlying_coupon_payment_date", "underlying_coupon_payment_date");
    expect_text(mv, 244, "244", "underlying_repurchase_term");
    expect_decimal(mv, 245, "10.5", &read_arena, "underlying_repurchase_rate");
    expect_decimal(mv, 246, "10.5", &read_arena, "underlying_factor");
    expect_text(mv, 315, "315", "underlying_put_or_call");
}

TEST_F(AllVersionsRoundtrip077Vlatest, TradingSessionListUpdateReport) {
    fixpp::vlatest::TradingSessionListUpdateReportArgs args{};
    args.trad_ses_req_id = "TradingSessionListUpdateReport_trad_ses_req_id";
    args.appl_id = "TradingSessionListUpdateReport_appl_id";
    args.appl_seq_num = 1181;
    args.appl_last_seq_num = 1350;
    args.appl_resend_flag = true;
    auto built = fixpp::vlatest::build_TradingSessionListUpdateReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TradingSessionListUpdateReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TradingSessionListUpdateReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 335, "TradingSessionListUpdateReport_trad_ses_req_id", "trad_ses_req_id");
    expect_text(mv, 1180, "TradingSessionListUpdateReport_appl_id", "appl_id");
    expect_text(mv, 1181, "1181", "appl_seq_num");
    expect_text(mv, 1350, "1350", "appl_last_seq_num");
    expect_text(mv, 1352, "Y", "appl_resend_flag");
}

TEST_F(AllVersionsRoundtrip077Vlatest, MarketDefinitionRequest) {
    fixpp::vlatest::MarketDefinitionRequestArgs args{};
    args.subscription_request_type = '1';
    args.market_segment_id = "MarketDefinitionRequest_market_segment_id";
    args.market_id = "MarketDefinitionRequest_market_id";
    auto built = fixpp::vlatest::build_MarketDefinitionRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDefinitionRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MarketDefinitionRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 263, "1", "subscription_request_type");
    expect_text(mv, 1300, "MarketDefinitionRequest_market_segment_id", "market_segment_id");
    expect_text(mv, 1301, "MarketDefinitionRequest_market_id", "market_id");
}

TEST_F(AllVersionsRoundtrip077Vlatest, MarketDefinition) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::MarketDefinitionArgs args{};
    args.currency = "MarketDefinition_currency";
    args.text = "MarketDefinition_text";
    args.price_type = 423;
    args.round_lot = make_decimal("10.5", &arena);
    args.min_trade_vol = make_decimal("10.5", &arena);
    args.expiration_cycle = 827;
    auto built = fixpp::vlatest::build_MarketDefinition(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDefinition failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MarketDefinition frame failed (see ADD_FAILURE above)";
    expect_text(mv, 15, "MarketDefinition_currency", "currency");
    expect_text(mv, 58, "MarketDefinition_text", "text");
    expect_text(mv, 423, "423", "price_type");
    expect_decimal(mv, 561, "10.5", &read_arena, "round_lot");
    expect_decimal(mv, 562, "10.5", &read_arena, "min_trade_vol");
    expect_text(mv, 827, "827", "expiration_cycle");
}

TEST_F(AllVersionsRoundtrip077Vlatest, MarketDefinitionUpdateReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::MarketDefinitionUpdateReportArgs args{};
    args.currency = "MarketDefinitionUpdateReport_currency";
    args.text = "MarketDefinitionUpdateReport_text";
    args.price_type = 423;
    args.round_lot = make_decimal("10.5", &arena);
    args.min_trade_vol = make_decimal("10.5", &arena);
    args.expiration_cycle = 827;
    auto built = fixpp::vlatest::build_MarketDefinitionUpdateReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDefinitionUpdateReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MarketDefinitionUpdateReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 15, "MarketDefinitionUpdateReport_currency", "currency");
    expect_text(mv, 58, "MarketDefinitionUpdateReport_text", "text");
    expect_text(mv, 423, "423", "price_type");
    expect_decimal(mv, 561, "10.5", &read_arena, "round_lot");
    expect_decimal(mv, 562, "10.5", &read_arena, "min_trade_vol");
    expect_text(mv, 827, "827", "expiration_cycle");
}

TEST_F(AllVersionsRoundtrip077Vlatest, ApplicationMessageRequest) {
    fixpp::vlatest::ApplicationMessageRequestArgs args{};
    args.text = "ApplicationMessageRequest_text";
    args.encoded_text = "ApplicationMessageRequest_encoded_text";
    args.appl_req_type = 1347;
    auto built = fixpp::vlatest::build_ApplicationMessageRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_ApplicationMessageRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built ApplicationMessageRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "ApplicationMessageRequest_text", "text");
    expect_text(mv, 355, "ApplicationMessageRequest_encoded_text", "encoded_text");
    expect_text(mv, 1347, "1347", "appl_req_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, ApplicationMessageRequestAck) {
    fixpp::vlatest::ApplicationMessageRequestAckArgs args{};
    args.text = "ApplicationMessageRequestAck_text";
    args.encoded_text = "ApplicationMessageRequestAck_encoded_text";
    args.appl_req_type = 1347;
    args.appl_response_type = 1348;
    auto built = fixpp::vlatest::build_ApplicationMessageRequestAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_ApplicationMessageRequestAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built ApplicationMessageRequestAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "ApplicationMessageRequestAck_text", "text");
    expect_text(mv, 355, "ApplicationMessageRequestAck_encoded_text", "encoded_text");
    expect_text(mv, 1347, "1347", "appl_req_type");
    expect_text(mv, 1348, "1348", "appl_response_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, ApplicationMessageReport) {
    fixpp::vlatest::ApplicationMessageReportArgs args{};
    args.text = "ApplicationMessageReport_text";
    args.encoded_text = "ApplicationMessageReport_encoded_text";
    args.appl_report_type = 1426;
    auto built = fixpp::vlatest::build_ApplicationMessageReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_ApplicationMessageReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built ApplicationMessageReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "ApplicationMessageReport_text", "text");
    expect_text(mv, 355, "ApplicationMessageReport_encoded_text", "encoded_text");
    expect_text(mv, 1426, "1426", "appl_report_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, OrderMassActionReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::OrderMassActionReportArgs args{};
    args.cl_ord_id = "OrderMassActionReport_cl_ord_id";
    args.security_id_source = "OrderMassActionReport_security_id_source";
    args.price = make_decimal("10.5", &arena);
    args.side = '1';
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_OrderMassActionReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_OrderMassActionReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built OrderMassActionReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 11, "OrderMassActionReport_cl_ord_id", "cl_ord_id");
    expect_text(mv, 22, "OrderMassActionReport_security_id_source", "security_id_source");
    expect_decimal(mv, 44, "10.5", &read_arena, "price");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
}

TEST_F(AllVersionsRoundtrip077Vlatest, Email) {
    fixpp::vlatest::EmailArgs args{};
    args.cl_ord_id = "Email_cl_ord_id";
    args.order_id = "Email_order_id";
    args.email_type = '1';
    auto built = fixpp::vlatest::build_Email(out, args);
    ASSERT_TRUE(built.has_value()) << "build_Email failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built Email frame failed (see ADD_FAILURE above)";
    expect_text(mv, 11, "Email_cl_ord_id", "cl_ord_id");
    expect_text(mv, 37, "Email_order_id", "order_id");
    expect_text(mv, 94, "1", "email_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, OrderMassActionRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::OrderMassActionRequestArgs args{};
    args.cl_ord_id = "OrderMassActionRequest_cl_ord_id";
    args.security_id_source = "OrderMassActionRequest_security_id_source";
    args.price = make_decimal("10.5", &arena);
    args.side = '1';
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_OrderMassActionRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_OrderMassActionRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built OrderMassActionRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 11, "OrderMassActionRequest_cl_ord_id", "cl_ord_id");
    expect_text(mv, 22, "OrderMassActionRequest_security_id_source", "security_id_source");
    expect_decimal(mv, 44, "10.5", &read_arena, "price");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
}

TEST_F(AllVersionsRoundtrip077Vlatest, UserNotification) {
    fixpp::vlatest::UserNotificationArgs args{};
    args.text = "UserNotification_text";
    args.encoded_text = "UserNotification_encoded_text";
    args.user_status = 926;
    auto built = fixpp::vlatest::build_UserNotification(out, args);
    ASSERT_TRUE(built.has_value()) << "build_UserNotification failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built UserNotification frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "UserNotification_text", "text");
    expect_text(mv, 355, "UserNotification_encoded_text", "encoded_text");
    expect_text(mv, 926, "926", "user_status");
}

TEST_F(AllVersionsRoundtrip077Vlatest, StreamAssignmentRequest) {
    fixpp::vlatest::StreamAssignmentRequestArgs args{};
    args.stream_asgn_req_id = "StreamAssignmentRequest_stream_asgn_req_id";
    args.stream_asgn_req_type = 1498;
    auto built = fixpp::vlatest::build_StreamAssignmentRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_StreamAssignmentRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built StreamAssignmentRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1497, "StreamAssignmentRequest_stream_asgn_req_id", "stream_asgn_req_id");
    expect_text(mv, 1498, "1498", "stream_asgn_req_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, StreamAssignmentReport) {
    fixpp::vlatest::StreamAssignmentReportArgs args{};
    args.stream_asgn_req_id = "StreamAssignmentReport_stream_asgn_req_id";
    args.stream_asgn_req_type = 1498;
    args.stream_asgn_rpt_id = "StreamAssignmentReport_stream_asgn_rpt_id";
    auto built = fixpp::vlatest::build_StreamAssignmentReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_StreamAssignmentReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built StreamAssignmentReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1497, "StreamAssignmentReport_stream_asgn_req_id", "stream_asgn_req_id");
    expect_text(mv, 1498, "1498", "stream_asgn_req_type");
    expect_text(mv, 1501, "StreamAssignmentReport_stream_asgn_rpt_id", "stream_asgn_rpt_id");
}

TEST_F(AllVersionsRoundtrip077Vlatest, StreamAssignmentReportACK) {
    fixpp::vlatest::StreamAssignmentReportACKArgs args{};
    args.text = "StreamAssignmentReportACK_text";
    args.encoded_text = "StreamAssignmentReportACK_encoded_text";
    args.stream_asgn_rej_reason = 1502;
    args.stream_asgn_ack_type = 1503;
    auto built = fixpp::vlatest::build_StreamAssignmentReportACK(out, args);
    ASSERT_TRUE(built.has_value()) << "build_StreamAssignmentReportACK failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built StreamAssignmentReportACK frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "StreamAssignmentReportACK_text", "text");
    expect_text(mv, 355, "StreamAssignmentReportACK_encoded_text", "encoded_text");
    expect_text(mv, 1502, "1502", "stream_asgn_rej_reason");
    expect_text(mv, 1503, "1503", "stream_asgn_ack_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PartyDetailsListRequest) {
    fixpp::vlatest::PartyDetailsListRequestArgs args{};
    args.text = "PartyDetailsListRequest_text";
    args.subscription_request_type = '1';
    args.encoded_text = "PartyDetailsListRequest_encoded_text";
    auto built = fixpp::vlatest::build_PartyDetailsListRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyDetailsListRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PartyDetailsListRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "PartyDetailsListRequest_text", "text");
    expect_text(mv, 263, "1", "subscription_request_type");
    expect_text(mv, 355, "PartyDetailsListRequest_encoded_text", "encoded_text");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PartyDetailsListReport) {
    fixpp::vlatest::PartyDetailsListReportArgs args{};
    args.text = "PartyDetailsListReport_text";
    args.transact_time = "PartyDetailsListReport_transact_time";
    args.last_fragment = true;
    args.appl_seq_num = 1181;
    args.appl_last_seq_num = 1350;
    args.appl_resend_flag = true;
    auto built = fixpp::vlatest::build_PartyDetailsListReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyDetailsListReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PartyDetailsListReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "PartyDetailsListReport_text", "text");
    expect_text(mv, 60, "PartyDetailsListReport_transact_time", "transact_time");
    expect_text(mv, 893, "Y", "last_fragment");
    expect_text(mv, 1181, "1181", "appl_seq_num");
    expect_text(mv, 1350, "1350", "appl_last_seq_num");
    expect_text(mv, 1352, "Y", "appl_resend_flag");
}

TEST_F(AllVersionsRoundtrip077Vlatest, MarginRequirementInquiry) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::MarginRequirementInquiryArgs args{};
    args.security_id_source = "MarginRequirementInquiry_security_id_source";
    args.security_id = "MarginRequirementInquiry_security_id";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_MarginRequirementInquiry(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MarginRequirementInquiry failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MarginRequirementInquiry frame failed (see ADD_FAILURE above)";
    expect_text(mv, 22, "MarginRequirementInquiry_security_id_source", "security_id_source");
    expect_text(mv, 48, "MarginRequirementInquiry_security_id", "security_id");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, MarginRequirementInquiryAck) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::MarginRequirementInquiryAckArgs args{};
    args.security_id_source = "MarginRequirementInquiryAck_security_id_source";
    args.security_id = "MarginRequirementInquiryAck_security_id";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_MarginRequirementInquiryAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MarginRequirementInquiryAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MarginRequirementInquiryAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 22, "MarginRequirementInquiryAck_security_id_source", "security_id_source");
    expect_text(mv, 48, "MarginRequirementInquiryAck_security_id", "security_id");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, MarginRequirementReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::MarginRequirementReportArgs args{};
    args.currency = "MarginRequirementReport_currency";
    args.security_id_source = "MarginRequirementReport_security_id_source";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_MarginRequirementReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MarginRequirementReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MarginRequirementReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 15, "MarginRequirementReport_currency", "currency");
    expect_text(mv, 22, "MarginRequirementReport_security_id_source", "security_id_source");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PartyDetailsListUpdateReport) {
    fixpp::vlatest::PartyDetailsListUpdateReportArgs args{};
    args.text = "PartyDetailsListUpdateReport_text";
    args.transact_time = "PartyDetailsListUpdateReport_transact_time";
    args.last_fragment = true;
    args.appl_seq_num = 1181;
    args.appl_last_seq_num = 1350;
    args.appl_resend_flag = true;
    auto built = fixpp::vlatest::build_PartyDetailsListUpdateReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyDetailsListUpdateReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PartyDetailsListUpdateReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "PartyDetailsListUpdateReport_text", "text");
    expect_text(mv, 60, "PartyDetailsListUpdateReport_transact_time", "transact_time");
    expect_text(mv, 893, "Y", "last_fragment");
    expect_text(mv, 1181, "1181", "appl_seq_num");
    expect_text(mv, 1350, "1350", "appl_last_seq_num");
    expect_text(mv, 1352, "Y", "appl_resend_flag");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PartyRiskLimitsRequest) {
    fixpp::vlatest::PartyRiskLimitsRequestArgs args{};
    args.text = "PartyRiskLimitsRequest_text";
    args.subscription_request_type = '1';
    args.encoded_text = "PartyRiskLimitsRequest_encoded_text";
    args.risk_limit_request_type = 1760;
    auto built = fixpp::vlatest::build_PartyRiskLimitsRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyRiskLimitsRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PartyRiskLimitsRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "PartyRiskLimitsRequest_text", "text");
    expect_text(mv, 263, "1", "subscription_request_type");
    expect_text(mv, 355, "PartyRiskLimitsRequest_encoded_text", "encoded_text");
    expect_text(mv, 1760, "1760", "risk_limit_request_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PartyRiskLimitsReport) {
    fixpp::vlatest::PartyRiskLimitsReportArgs args{};
    args.text = "PartyRiskLimitsReport_text";
    args.transact_time = "PartyRiskLimitsReport_transact_time";
    args.unsolicited_indicator = true;
    args.last_fragment = true;
    args.appl_seq_num = 1181;
    args.appl_last_seq_num = 1350;
    auto built = fixpp::vlatest::build_PartyRiskLimitsReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyRiskLimitsReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PartyRiskLimitsReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "PartyRiskLimitsReport_text", "text");
    expect_text(mv, 60, "PartyRiskLimitsReport_transact_time", "transact_time");
    expect_text(mv, 325, "Y", "unsolicited_indicator");
    expect_text(mv, 893, "Y", "last_fragment");
    expect_text(mv, 1181, "1181", "appl_seq_num");
    expect_text(mv, 1350, "1350", "appl_last_seq_num");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SecurityMassStatusRequest) {
    fixpp::vlatest::SecurityMassStatusRequestArgs args{};
    args.subscription_request_type = '1';
    args.security_status_req_id = "SecurityMassStatusRequest_security_status_req_id";
    args.trading_session_id = "SecurityMassStatusRequest_trading_session_id";
    args.instrument_scope_product = 1543;
    args.instrument_scope_put_or_call = 1553;
    args.instrument_scope_flexible_indicator = true;
    auto built = fixpp::vlatest::build_SecurityMassStatusRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityMassStatusRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SecurityMassStatusRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 263, "1", "subscription_request_type");
    expect_text(mv, 324, "SecurityMassStatusRequest_security_status_req_id", "security_status_req_id");
    expect_text(mv, 336, "SecurityMassStatusRequest_trading_session_id", "trading_session_id");
    expect_text(mv, 1543, "1543", "instrument_scope_product");
    expect_text(mv, 1553, "1553", "instrument_scope_put_or_call");
    expect_text(mv, 1554, "Y", "instrument_scope_flexible_indicator");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SecurityMassStatus) {
    fixpp::vlatest::SecurityMassStatusArgs args{};
    args.transact_time = "SecurityMassStatus_transact_time";
    args.trade_date = "SecurityMassStatus_trade_date";
    args.market_depth = 264;
    args.unsolicited_indicator = true;
    args.adjustment = 334;
    args.appl_resend_flag = true;
    auto built = fixpp::vlatest::build_SecurityMassStatus(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityMassStatus failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SecurityMassStatus frame failed (see ADD_FAILURE above)";
    expect_text(mv, 60, "SecurityMassStatus_transact_time", "transact_time");
    expect_text(mv, 75, "SecurityMassStatus_trade_date", "trade_date");
    expect_text(mv, 264, "264", "market_depth");
    expect_text(mv, 325, "Y", "unsolicited_indicator");
    expect_text(mv, 334, "334", "adjustment");
    expect_text(mv, 1352, "Y", "appl_resend_flag");
}

TEST_F(AllVersionsRoundtrip077Vlatest, AccountSummaryReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::AccountSummaryReportArgs args{};
    args.currency = "AccountSummaryReport_currency";
    args.transact_time = "AccountSummaryReport_transact_time";
    args.margin_excess = make_decimal("10.5", &arena);
    args.total_net_value = make_decimal("10.5", &arena);
    args.appl_seq_num = 1181;
    args.appl_last_seq_num = 1350;
    auto built = fixpp::vlatest::build_AccountSummaryReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_AccountSummaryReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built AccountSummaryReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 15, "AccountSummaryReport_currency", "currency");
    expect_text(mv, 60, "AccountSummaryReport_transact_time", "transact_time");
    expect_decimal(mv, 899, "10.5", &read_arena, "margin_excess");
    expect_decimal(mv, 900, "10.5", &read_arena, "total_net_value");
    expect_text(mv, 1181, "1181", "appl_seq_num");
    expect_text(mv, 1350, "1350", "appl_last_seq_num");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PartyRiskLimitsUpdateReport) {
    fixpp::vlatest::PartyRiskLimitsUpdateReportArgs args{};
    args.text = "PartyRiskLimitsUpdateReport_text";
    args.transact_time = "PartyRiskLimitsUpdateReport_transact_time";
    args.last_fragment = true;
    args.appl_seq_num = 1181;
    args.appl_last_seq_num = 1350;
    args.appl_resend_flag = true;
    auto built = fixpp::vlatest::build_PartyRiskLimitsUpdateReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyRiskLimitsUpdateReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PartyRiskLimitsUpdateReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "PartyRiskLimitsUpdateReport_text", "text");
    expect_text(mv, 60, "PartyRiskLimitsUpdateReport_transact_time", "transact_time");
    expect_text(mv, 893, "Y", "last_fragment");
    expect_text(mv, 1181, "1181", "appl_seq_num");
    expect_text(mv, 1350, "1350", "appl_last_seq_num");
    expect_text(mv, 1352, "Y", "appl_resend_flag");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PartyRiskLimitsDefinitionRequest) {
    fixpp::vlatest::PartyRiskLimitsDefinitionRequestArgs args{};
    args.text = "PartyRiskLimitsDefinitionRequest_text";
    args.encoded_text = "PartyRiskLimitsDefinitionRequest_encoded_text";
    auto built = fixpp::vlatest::build_PartyRiskLimitsDefinitionRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyRiskLimitsDefinitionRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PartyRiskLimitsDefinitionRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "PartyRiskLimitsDefinitionRequest_text", "text");
    expect_text(mv, 355, "PartyRiskLimitsDefinitionRequest_encoded_text", "encoded_text");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PartyRiskLimitsDefinitionRequestAck) {
    fixpp::vlatest::PartyRiskLimitsDefinitionRequestAckArgs args{};
    args.text = "PartyRiskLimitsDefinitionRequestAck_text";
    args.encoded_text = "PartyRiskLimitsDefinitionRequestAck_encoded_text";
    args.risk_limit_request_result = 1761;
    args.risk_limit_request_status = 1762;
    auto built = fixpp::vlatest::build_PartyRiskLimitsDefinitionRequestAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyRiskLimitsDefinitionRequestAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PartyRiskLimitsDefinitionRequestAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "PartyRiskLimitsDefinitionRequestAck_text", "text");
    expect_text(mv, 355, "PartyRiskLimitsDefinitionRequestAck_encoded_text", "encoded_text");
    expect_text(mv, 1761, "1761", "risk_limit_request_result");
    expect_text(mv, 1762, "1762", "risk_limit_request_status");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PartyEntitlementsRequest) {
    fixpp::vlatest::PartyEntitlementsRequestArgs args{};
    args.text = "PartyEntitlementsRequest_text";
    args.subscription_request_type = '1';
    args.encoded_text = "PartyEntitlementsRequest_encoded_text";
    args.entitlement_status = 1883;
    auto built = fixpp::vlatest::build_PartyEntitlementsRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyEntitlementsRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PartyEntitlementsRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "PartyEntitlementsRequest_text", "text");
    expect_text(mv, 263, "1", "subscription_request_type");
    expect_text(mv, 355, "PartyEntitlementsRequest_encoded_text", "encoded_text");
    expect_text(mv, 1883, "1883", "entitlement_status");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PartyEntitlementsReport) {
    fixpp::vlatest::PartyEntitlementsReportArgs args{};
    args.text = "PartyEntitlementsReport_text";
    args.transact_time = "PartyEntitlementsReport_transact_time";
    args.last_fragment = true;
    args.appl_seq_num = 1181;
    args.appl_last_seq_num = 1350;
    args.appl_resend_flag = true;
    auto built = fixpp::vlatest::build_PartyEntitlementsReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyEntitlementsReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PartyEntitlementsReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "PartyEntitlementsReport_text", "text");
    expect_text(mv, 60, "PartyEntitlementsReport_transact_time", "transact_time");
    expect_text(mv, 893, "Y", "last_fragment");
    expect_text(mv, 1181, "1181", "appl_seq_num");
    expect_text(mv, 1350, "1350", "appl_last_seq_num");
    expect_text(mv, 1352, "Y", "appl_resend_flag");
}

TEST_F(AllVersionsRoundtrip077Vlatest, QuoteAck) {
    fixpp::vlatest::QuoteAckArgs args{};
    args.text = "QuoteAck_text";
    args.quote_id = "QuoteAck_quote_id";
    args.quote_cancel_type = 298;
    args.quote_reject_reason = 300;
    auto built = fixpp::vlatest::build_QuoteAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_QuoteAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built QuoteAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "QuoteAck_text", "text");
    expect_text(mv, 117, "QuoteAck_quote_id", "quote_id");
    expect_text(mv, 298, "298", "quote_cancel_type");
    expect_text(mv, 300, "300", "quote_reject_reason");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PartyDetailsDefinitionRequest) {
    fixpp::vlatest::PartyDetailsDefinitionRequestArgs args{};
    args.text = "PartyDetailsDefinitionRequest_text";
    args.encoded_text = "PartyDetailsDefinitionRequest_encoded_text";
    auto built = fixpp::vlatest::build_PartyDetailsDefinitionRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyDetailsDefinitionRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PartyDetailsDefinitionRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "PartyDetailsDefinitionRequest_text", "text");
    expect_text(mv, 355, "PartyDetailsDefinitionRequest_encoded_text", "encoded_text");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PartyDetailsDefinitionRequestAck) {
    fixpp::vlatest::PartyDetailsDefinitionRequestAckArgs args{};
    args.text = "PartyDetailsDefinitionRequestAck_text";
    args.encoded_text = "PartyDetailsDefinitionRequestAck_encoded_text";
    args.party_detail_request_result = 1877;
    args.party_detail_request_status = 1878;
    auto built = fixpp::vlatest::build_PartyDetailsDefinitionRequestAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyDetailsDefinitionRequestAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PartyDetailsDefinitionRequestAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "PartyDetailsDefinitionRequestAck_text", "text");
    expect_text(mv, 355, "PartyDetailsDefinitionRequestAck_encoded_text", "encoded_text");
    expect_text(mv, 1877, "1877", "party_detail_request_result");
    expect_text(mv, 1878, "1878", "party_detail_request_status");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PartyEntitlementsUpdateReport) {
    fixpp::vlatest::PartyEntitlementsUpdateReportArgs args{};
    args.text = "PartyEntitlementsUpdateReport_text";
    args.transact_time = "PartyEntitlementsUpdateReport_transact_time";
    args.last_fragment = true;
    args.appl_seq_num = 1181;
    args.appl_last_seq_num = 1350;
    args.appl_resend_flag = true;
    auto built = fixpp::vlatest::build_PartyEntitlementsUpdateReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyEntitlementsUpdateReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PartyEntitlementsUpdateReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "PartyEntitlementsUpdateReport_text", "text");
    expect_text(mv, 60, "PartyEntitlementsUpdateReport_transact_time", "transact_time");
    expect_text(mv, 893, "Y", "last_fragment");
    expect_text(mv, 1181, "1181", "appl_seq_num");
    expect_text(mv, 1350, "1350", "appl_last_seq_num");
    expect_text(mv, 1352, "Y", "appl_resend_flag");
}

TEST_F(AllVersionsRoundtrip077Vlatest, NewOrderSingle) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::NewOrderSingleArgs args{};
    args.account = "NewOrderSingle_account";
    args.cl_ord_id = "NewOrderSingle_cl_ord_id";
    args.commission = make_decimal("10.5", &arena);
    args.comm_type = '1';
    args.exec_inst = '1';
    args.order_qty = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_NewOrderSingle(out, args);
    ASSERT_TRUE(built.has_value()) << "build_NewOrderSingle failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built NewOrderSingle frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "NewOrderSingle_account", "account");
    expect_text(mv, 11, "NewOrderSingle_cl_ord_id", "cl_ord_id");
    expect_decimal(mv, 12, "10.5", &read_arena, "commission");
    expect_text(mv, 13, "1", "comm_type");
    expect_text(mv, 18, "1", "exec_inst");
    expect_decimal(mv, 38, "10.5", &read_arena, "order_qty");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PartyEntitlementsDefinitionRequest) {
    fixpp::vlatest::PartyEntitlementsDefinitionRequestArgs args{};
    args.text = "PartyEntitlementsDefinitionRequest_text";
    args.encoded_text = "PartyEntitlementsDefinitionRequest_encoded_text";
    auto built = fixpp::vlatest::build_PartyEntitlementsDefinitionRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyEntitlementsDefinitionRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PartyEntitlementsDefinitionRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "PartyEntitlementsDefinitionRequest_text", "text");
    expect_text(mv, 355, "PartyEntitlementsDefinitionRequest_encoded_text", "encoded_text");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PartyEntitlementsDefinitionRequestAck) {
    fixpp::vlatest::PartyEntitlementsDefinitionRequestAckArgs args{};
    args.text = "PartyEntitlementsDefinitionRequestAck_text";
    args.encoded_text = "PartyEntitlementsDefinitionRequestAck_encoded_text";
    args.entitlement_request_result = 1881;
    args.entitlement_request_status = 1882;
    auto built = fixpp::vlatest::build_PartyEntitlementsDefinitionRequestAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyEntitlementsDefinitionRequestAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PartyEntitlementsDefinitionRequestAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "PartyEntitlementsDefinitionRequestAck_text", "text");
    expect_text(mv, 355, "PartyEntitlementsDefinitionRequestAck_encoded_text", "encoded_text");
    expect_text(mv, 1881, "1881", "entitlement_request_result");
    expect_text(mv, 1882, "1882", "entitlement_request_status");
}

TEST_F(AllVersionsRoundtrip077Vlatest, TradeMatchReport) {
    fixpp::vlatest::TradeMatchReportArgs args{};
    args.transact_time = "TradeMatchReport_transact_time";
    args.trade_date = "TradeMatchReport_trade_date";
    args.multi_leg_reporting_type = '1';
    args.trd_type = 828;
    args.trd_sub_type = 829;
    args.appl_resend_flag = true;
    auto built = fixpp::vlatest::build_TradeMatchReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TradeMatchReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TradeMatchReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 60, "TradeMatchReport_transact_time", "transact_time");
    expect_text(mv, 75, "TradeMatchReport_trade_date", "trade_date");
    expect_text(mv, 442, "1", "multi_leg_reporting_type");
    expect_text(mv, 828, "828", "trd_type");
    expect_text(mv, 829, "829", "trd_sub_type");
    expect_text(mv, 1352, "Y", "appl_resend_flag");
}

TEST_F(AllVersionsRoundtrip077Vlatest, TradeMatchReportAck) {
    fixpp::vlatest::TradeMatchReportAckArgs args{};
    args.text = "TradeMatchReportAck_text";
    args.encoded_text = "TradeMatchReportAck_encoded_text";
    args.appl_seq_num = 1181;
    args.appl_last_seq_num = 1350;
    args.appl_resend_flag = true;
    auto built = fixpp::vlatest::build_TradeMatchReportAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TradeMatchReportAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TradeMatchReportAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "TradeMatchReportAck_text", "text");
    expect_text(mv, 355, "TradeMatchReportAck_encoded_text", "encoded_text");
    expect_text(mv, 1181, "1181", "appl_seq_num");
    expect_text(mv, 1350, "1350", "appl_last_seq_num");
    expect_text(mv, 1352, "Y", "appl_resend_flag");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PartyRiskLimitsReportAck) {
    fixpp::vlatest::PartyRiskLimitsReportAckArgs args{};
    args.text = "PartyRiskLimitsReportAck_text";
    args.transact_time = "PartyRiskLimitsReportAck_transact_time";
    args.risk_limit_report_status = 2316;
    args.risk_limit_report_reject_reason = 2317;
    auto built = fixpp::vlatest::build_PartyRiskLimitsReportAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyRiskLimitsReportAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PartyRiskLimitsReportAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "PartyRiskLimitsReportAck_text", "text");
    expect_text(mv, 60, "PartyRiskLimitsReportAck_transact_time", "transact_time");
    expect_text(mv, 2316, "2316", "risk_limit_report_status");
    expect_text(mv, 2317, "2317", "risk_limit_report_reject_reason");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PartyRiskLimitCheckRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::PartyRiskLimitCheckRequestArgs args{};
    args.currency = "PartyRiskLimitCheckRequest_currency";
    args.security_id_source = "PartyRiskLimitCheckRequest_security_id_source";
    args.side = '1';
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    auto built = fixpp::vlatest::build_PartyRiskLimitCheckRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyRiskLimitCheckRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PartyRiskLimitCheckRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 15, "PartyRiskLimitCheckRequest_currency", "currency");
    expect_text(mv, 22, "PartyRiskLimitCheckRequest_security_id_source", "security_id_source");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PartyRiskLimitCheckRequestAck) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::PartyRiskLimitCheckRequestAckArgs args{};
    args.currency = "PartyRiskLimitCheckRequestAck_currency";
    args.security_id_source = "PartyRiskLimitCheckRequestAck_security_id_source";
    args.side = '1';
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    auto built = fixpp::vlatest::build_PartyRiskLimitCheckRequestAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyRiskLimitCheckRequestAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PartyRiskLimitCheckRequestAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 15, "PartyRiskLimitCheckRequestAck_currency", "currency");
    expect_text(mv, 22, "PartyRiskLimitCheckRequestAck_security_id_source", "security_id_source");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PartyActionRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::PartyActionRequestArgs args{};
    args.text = "PartyActionRequest_text";
    args.transact_time = "PartyActionRequest_transact_time";
    args.instrument_scope_product = 1543;
    args.instrument_scope_put_or_call = 1553;
    args.instrument_scope_flexible_indicator = true;
    args.instrument_scope_coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_PartyActionRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyActionRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PartyActionRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "PartyActionRequest_text", "text");
    expect_text(mv, 60, "PartyActionRequest_transact_time", "transact_time");
    expect_text(mv, 1543, "1543", "instrument_scope_product");
    expect_text(mv, 1553, "1553", "instrument_scope_put_or_call");
    expect_text(mv, 1554, "Y", "instrument_scope_flexible_indicator");
    expect_decimal(mv, 1555, "10.5", &read_arena, "instrument_scope_coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PartyActionReport) {
    fixpp::vlatest::PartyActionReportArgs args{};
    args.text = "PartyActionReport_text";
    args.transact_time = "PartyActionReport_transact_time";
    args.copy_msg_indicator = true;
    args.instrument_scope_product = 1543;
    args.instrument_scope_put_or_call = 1553;
    args.instrument_scope_flexible_indicator = true;
    auto built = fixpp::vlatest::build_PartyActionReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyActionReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PartyActionReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "PartyActionReport_text", "text");
    expect_text(mv, 60, "PartyActionReport_transact_time", "transact_time");
    expect_text(mv, 797, "Y", "copy_msg_indicator");
    expect_text(mv, 1543, "1543", "instrument_scope_product");
    expect_text(mv, 1553, "1553", "instrument_scope_put_or_call");
    expect_text(mv, 1554, "Y", "instrument_scope_flexible_indicator");
}

TEST_F(AllVersionsRoundtrip077Vlatest, MassOrder) {
    fixpp::vlatest::MassOrderArgs args{};
    args.account = "MassOrder_account";
    args.text = "MassOrder_text";
    args.order_capacity = '1';
    args.order_restrictions = '1';
    args.account_type = 581;
    args.cust_order_capacity = 582;
    auto built = fixpp::vlatest::build_MassOrder(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MassOrder failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MassOrder frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "MassOrder_account", "account");
    expect_text(mv, 58, "MassOrder_text", "text");
    expect_text(mv, 528, "1", "order_capacity");
    expect_text(mv, 529, "1", "order_restrictions");
    expect_text(mv, 581, "581", "account_type");
    expect_text(mv, 582, "582", "cust_order_capacity");
}

TEST_F(AllVersionsRoundtrip077Vlatest, MassOrderAck) {
    fixpp::vlatest::MassOrderAckArgs args{};
    args.account = "MassOrderAck_account";
    args.text = "MassOrderAck_text";
    args.order_capacity = '1';
    args.order_restrictions = '1';
    args.account_type = 581;
    args.cust_order_capacity = 582;
    auto built = fixpp::vlatest::build_MassOrderAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MassOrderAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MassOrderAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "MassOrderAck_account", "account");
    expect_text(mv, 58, "MassOrderAck_text", "text");
    expect_text(mv, 528, "1", "order_capacity");
    expect_text(mv, 529, "1", "order_restrictions");
    expect_text(mv, 581, "581", "account_type");
    expect_text(mv, 582, "582", "cust_order_capacity");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PositionTransferInstruction) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::PositionTransferInstructionArgs args{};
    args.currency = "PositionTransferInstruction_currency";
    args.security_id_source = "PositionTransferInstruction_security_id_source";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_PositionTransferInstruction(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PositionTransferInstruction failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PositionTransferInstruction frame failed (see ADD_FAILURE above)";
    expect_text(mv, 15, "PositionTransferInstruction_currency", "currency");
    expect_text(mv, 22, "PositionTransferInstruction_security_id_source", "security_id_source");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PositionTransferInstructionAck) {
    fixpp::vlatest::PositionTransferInstructionAckArgs args{};
    args.text = "PositionTransferInstructionAck_text";
    args.transact_time = "PositionTransferInstructionAck_transact_time";
    args.transfer_trans_type = 2439;
    args.transfer_type = 2440;
    auto built = fixpp::vlatest::build_PositionTransferInstructionAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PositionTransferInstructionAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PositionTransferInstructionAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "PositionTransferInstructionAck_text", "text");
    expect_text(mv, 60, "PositionTransferInstructionAck_transact_time", "transact_time");
    expect_text(mv, 2439, "2439", "transfer_trans_type");
    expect_text(mv, 2440, "2440", "transfer_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PositionTransferReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::PositionTransferReportArgs args{};
    args.currency = "PositionTransferReport_currency";
    args.security_id_source = "PositionTransferReport_security_id_source";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_PositionTransferReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PositionTransferReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PositionTransferReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 15, "PositionTransferReport_currency", "currency");
    expect_text(mv, 22, "PositionTransferReport_security_id_source", "security_id_source");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, MarketDataStatisticsRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::MarketDataStatisticsRequestArgs args{};
    args.security_id_source = "MarketDataStatisticsRequest_security_id_source";
    args.security_id = "MarketDataStatisticsRequest_security_id";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_MarketDataStatisticsRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataStatisticsRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MarketDataStatisticsRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 22, "MarketDataStatisticsRequest_security_id_source", "security_id_source");
    expect_text(mv, 48, "MarketDataStatisticsRequest_security_id", "security_id");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, MarketDataStatisticsReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::MarketDataStatisticsReportArgs args{};
    args.currency = "MarketDataStatisticsReport_currency";
    args.security_id_source = "MarketDataStatisticsReport_security_id_source";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_MarketDataStatisticsReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataStatisticsReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MarketDataStatisticsReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 15, "MarketDataStatisticsReport_currency", "currency");
    expect_text(mv, 22, "MarketDataStatisticsReport_security_id_source", "security_id_source");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, CollateralReportAck) {
    fixpp::vlatest::CollateralReportAckArgs args{};
    args.text = "CollateralReportAck_text";
    args.transact_time = "CollateralReportAck_transact_time";
    args.coll_rpt_reject_reason = 2487;
    args.coll_rpt_status = 2488;
    auto built = fixpp::vlatest::build_CollateralReportAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_CollateralReportAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built CollateralReportAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "CollateralReportAck_text", "text");
    expect_text(mv, 60, "CollateralReportAck_transact_time", "transact_time");
    expect_text(mv, 2487, "2487", "coll_rpt_reject_reason");
    expect_text(mv, 2488, "2488", "coll_rpt_status");
}

TEST_F(AllVersionsRoundtrip077Vlatest, MarketDataReport) {
    fixpp::vlatest::MarketDataReportArgs args{};
    args.transact_time = "MarketDataReport_transact_time";
    args.tot_num_reports = 911;
    args.md_report_id = 963;
    args.appl_id = "MarketDataReport_appl_id";
    args.appl_resend_flag = true;
    auto built = fixpp::vlatest::build_MarketDataReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MarketDataReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 60, "MarketDataReport_transact_time", "transact_time");
    expect_text(mv, 911, "911", "tot_num_reports");
    expect_text(mv, 963, "963", "md_report_id");
    expect_text(mv, 1180, "MarketDataReport_appl_id", "appl_id");
    expect_text(mv, 1352, "Y", "appl_resend_flag");
}

TEST_F(AllVersionsRoundtrip077Vlatest, CrossRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::CrossRequestArgs args{};
    args.security_id_source = "CrossRequest_security_id_source";
    args.order_qty = make_decimal("10.5", &arena);
    args.security_id = "CrossRequest_security_id";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    auto built = fixpp::vlatest::build_CrossRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_CrossRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built CrossRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 22, "CrossRequest_security_id_source", "security_id_source");
    expect_decimal(mv, 38, "10.5", &read_arena, "order_qty");
    expect_text(mv, 48, "CrossRequest_security_id", "security_id");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
}

TEST_F(AllVersionsRoundtrip077Vlatest, CrossRequestAck) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::CrossRequestAckArgs args{};
    args.security_id_source = "CrossRequestAck_security_id_source";
    args.order_qty = make_decimal("10.5", &arena);
    args.security_id = "CrossRequestAck_security_id";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    auto built = fixpp::vlatest::build_CrossRequestAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_CrossRequestAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built CrossRequestAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 22, "CrossRequestAck_security_id_source", "security_id_source");
    expect_decimal(mv, 38, "10.5", &read_arena, "order_qty");
    expect_text(mv, 48, "CrossRequestAck_security_id", "security_id");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
}

TEST_F(AllVersionsRoundtrip077Vlatest, AllocationInstructionAlertRequest) {
    fixpp::vlatest::AllocationInstructionAlertRequestArgs args{};
    args.trade_date = "AllocationInstructionAlertRequest_trade_date";
    args.alloc_group_id = "AllocationInstructionAlertRequest_alloc_group_id";
    auto built = fixpp::vlatest::build_AllocationInstructionAlertRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_AllocationInstructionAlertRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built AllocationInstructionAlertRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 75, "AllocationInstructionAlertRequest_trade_date", "trade_date");
    expect_text(mv, 1730, "AllocationInstructionAlertRequest_alloc_group_id", "alloc_group_id");
}

TEST_F(AllVersionsRoundtrip077Vlatest, AllocationInstructionAlertRequestAck) {
    fixpp::vlatest::AllocationInstructionAlertRequestAckArgs args{};
    args.reject_text = "AllocationInstructionAlertRequestAck_reject_text";
    args.encoded_reject_text = "AllocationInstructionAlertRequestAck_encoded_reject_text";
    args.alloc_request_status = 2768;
    auto built = fixpp::vlatest::build_AllocationInstructionAlertRequestAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_AllocationInstructionAlertRequestAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built AllocationInstructionAlertRequestAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1328, "AllocationInstructionAlertRequestAck_reject_text", "reject_text");
    expect_text(mv, 1665, "AllocationInstructionAlertRequestAck_encoded_reject_text", "encoded_reject_text");
    expect_text(mv, 2768, "2768", "alloc_request_status");
}

TEST_F(AllVersionsRoundtrip077Vlatest, TradeAggregationRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::TradeAggregationRequestArgs args{};
    args.account = "TradeAggregationRequest_account";
    args.avg_px = make_decimal("10.5", &arena);
    args.currency = "TradeAggregationRequest_currency";
    args.side = '1';
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_TradeAggregationRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TradeAggregationRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TradeAggregationRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "TradeAggregationRequest_account", "account");
    expect_decimal(mv, 6, "10.5", &read_arena, "avg_px");
    expect_text(mv, 15, "TradeAggregationRequest_currency", "currency");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
}

TEST_F(AllVersionsRoundtrip077Vlatest, TradeAggregationReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::TradeAggregationReportArgs args{};
    args.avg_px = make_decimal("10.5", &arena);
    args.security_id_source = "TradeAggregationReport_security_id_source";
    args.security_id = "TradeAggregationReport_security_id";
    args.side = '1';
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_TradeAggregationReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TradeAggregationReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TradeAggregationReport frame failed (see ADD_FAILURE above)";
    expect_decimal(mv, 6, "10.5", &read_arena, "avg_px");
    expect_text(mv, 22, "TradeAggregationReport_security_id_source", "security_id_source");
    expect_text(mv, 48, "TradeAggregationReport_security_id", "security_id");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PayManagementRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::PayManagementRequestArgs args{};
    args.security_id_source = "PayManagementRequest_security_id_source";
    args.security_id = "PayManagementRequest_security_id";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_PayManagementRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PayManagementRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PayManagementRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 22, "PayManagementRequest_security_id_source", "security_id_source");
    expect_text(mv, 48, "PayManagementRequest_security_id", "security_id");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PayManagementRequestAck) {
    fixpp::vlatest::PayManagementRequestAckArgs args{};
    args.pay_request_id = "PayManagementRequestAck_pay_request_id";
    args.pay_request_status = 2813;
    auto built = fixpp::vlatest::build_PayManagementRequestAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PayManagementRequestAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PayManagementRequestAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 2812, "PayManagementRequestAck_pay_request_id", "pay_request_id");
    expect_text(mv, 2813, "2813", "pay_request_status");
}

TEST_F(AllVersionsRoundtrip077Vlatest, NewOrderList) {
    fixpp::vlatest::NewOrderListArgs args{};
    args.list_id = "NewOrderList_list_id";
    args.tot_no_orders = 68;
    args.list_exec_inst = "NewOrderList_list_exec_inst";
    args.bid_type = 394;
    args.list_exec_inst_type = '1';
    args.cancellation_rights = '1';
    auto built = fixpp::vlatest::build_NewOrderList(out, args);
    ASSERT_TRUE(built.has_value()) << "build_NewOrderList failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built NewOrderList frame failed (see ADD_FAILURE above)";
    expect_text(mv, 66, "NewOrderList_list_id", "list_id");
    expect_text(mv, 68, "68", "tot_no_orders");
    expect_text(mv, 69, "NewOrderList_list_exec_inst", "list_exec_inst");
    expect_text(mv, 394, "394", "bid_type");
    expect_text(mv, 433, "1", "list_exec_inst_type");
    expect_text(mv, 480, "1", "cancellation_rights");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PayManagementReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::PayManagementReportArgs args{};
    args.security_id_source = "PayManagementReport_security_id_source";
    args.security_id = "PayManagementReport_security_id";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_PayManagementReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PayManagementReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PayManagementReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 22, "PayManagementReport_security_id_source", "security_id_source");
    expect_text(mv, 48, "PayManagementReport_security_id", "security_id");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, PayManagementReportAck) {
    fixpp::vlatest::PayManagementReportAckArgs args{};
    args.reject_text = "PayManagementReportAck_reject_text";
    args.encoded_reject_text = "PayManagementReportAck_encoded_reject_text";
    args.pay_dispute_reason = 2800;
    args.pay_report_status = 2806;
    auto built = fixpp::vlatest::build_PayManagementReportAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_PayManagementReportAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built PayManagementReportAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1328, "PayManagementReportAck_reject_text", "reject_text");
    expect_text(mv, 1665, "PayManagementReportAck_encoded_reject_text", "encoded_reject_text");
    expect_text(mv, 2800, "2800", "pay_dispute_reason");
    expect_text(mv, 2806, "2806", "pay_report_status");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SettlementStatusRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::SettlementStatusRequestArgs args{};
    args.avg_px = make_decimal("10.5", &arena);
    args.currency = "SettlementStatusRequest_currency";
    args.security_id_source = "SettlementStatusRequest_security_id_source";
    args.side = '1';
    args.alloc_qty = make_decimal("10.5", &arena);
    args.stand_inst_db_type = 169;
    auto built = fixpp::vlatest::build_SettlementStatusRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SettlementStatusRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SettlementStatusRequest frame failed (see ADD_FAILURE above)";
    expect_decimal(mv, 6, "10.5", &read_arena, "avg_px");
    expect_text(mv, 15, "SettlementStatusRequest_currency", "currency");
    expect_text(mv, 22, "SettlementStatusRequest_security_id_source", "security_id_source");
    expect_text(mv, 54, "1", "side");
    expect_decimal(mv, 80, "10.5", &read_arena, "alloc_qty");
    expect_text(mv, 169, "169", "stand_inst_db_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SettlementStatusRequestAck) {
    fixpp::vlatest::SettlementStatusRequestAckArgs args{};
    args.reject_text = "SettlementStatusRequestAck_reject_text";
    args.encoded_reject_text = "SettlementStatusRequestAck_encoded_reject_text";
    args.settl_status_request_status = 2966;
    auto built = fixpp::vlatest::build_SettlementStatusRequestAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SettlementStatusRequestAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SettlementStatusRequestAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1328, "SettlementStatusRequestAck_reject_text", "reject_text");
    expect_text(mv, 1665, "SettlementStatusRequestAck_encoded_reject_text", "encoded_reject_text");
    expect_text(mv, 2966, "2966", "settl_status_request_status");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SettlementStatusReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::SettlementStatusReportArgs args{};
    args.avg_px = make_decimal("10.5", &arena);
    args.currency = "SettlementStatusReport_currency";
    args.security_id_source = "SettlementStatusReport_security_id_source";
    args.side = '1';
    args.alloc_qty = make_decimal("10.5", &arena);
    args.stand_inst_db_type = 169;
    auto built = fixpp::vlatest::build_SettlementStatusReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SettlementStatusReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SettlementStatusReport frame failed (see ADD_FAILURE above)";
    expect_decimal(mv, 6, "10.5", &read_arena, "avg_px");
    expect_text(mv, 15, "SettlementStatusReport_currency", "currency");
    expect_text(mv, 22, "SettlementStatusReport_security_id_source", "security_id_source");
    expect_text(mv, 54, "1", "side");
    expect_decimal(mv, 80, "10.5", &read_arena, "alloc_qty");
    expect_text(mv, 169, "169", "stand_inst_db_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SettlementStatusReportAck) {
    fixpp::vlatest::SettlementStatusReportAckArgs args{};
    args.reject_text = "SettlementStatusReportAck_reject_text";
    args.encoded_reject_text = "SettlementStatusReportAck_encoded_reject_text";
    args.settl_status_report_status = 2973;
    auto built = fixpp::vlatest::build_SettlementStatusReportAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SettlementStatusReportAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SettlementStatusReportAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1328, "SettlementStatusReportAck_reject_text", "reject_text");
    expect_text(mv, 1665, "SettlementStatusReportAck_encoded_reject_text", "encoded_reject_text");
    expect_text(mv, 2973, "2973", "settl_status_report_status");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SecurityRiskMetricsReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::SecurityRiskMetricsReportArgs args{};
    args.transact_time = "SecurityRiskMetricsReport_transact_time";
    args.underlying_coupon_payment_date = "SecurityRiskMetricsReport_underlying_coupon_payment_date";
    args.underlying_repurchase_term = 244;
    args.underlying_repurchase_rate = make_decimal("10.5", &arena);
    args.underlying_factor = make_decimal("10.5", &arena);
    args.underlying_put_or_call = 315;
    auto built = fixpp::vlatest::build_SecurityRiskMetricsReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityRiskMetricsReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SecurityRiskMetricsReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 60, "SecurityRiskMetricsReport_transact_time", "transact_time");
    expect_text(mv, 241, "SecurityRiskMetricsReport_underlying_coupon_payment_date", "underlying_coupon_payment_date");
    expect_text(mv, 244, "244", "underlying_repurchase_term");
    expect_decimal(mv, 245, "10.5", &read_arena, "underlying_repurchase_rate");
    expect_decimal(mv, 246, "10.5", &read_arena, "underlying_factor");
    expect_text(mv, 315, "315", "underlying_put_or_call");
}

TEST_F(AllVersionsRoundtrip077Vlatest, AlgoCertificateRequest) {
    fixpp::vlatest::AlgoCertificateRequestArgs args{};
    args.text = "AlgoCertificateRequest_text";
    args.transact_time = "AlgoCertificateRequest_transact_time";
    args.algo_certificate_request_trans_type = 3016;
    args.algo_certificate_status = 3022;
    auto built = fixpp::vlatest::build_AlgoCertificateRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_AlgoCertificateRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built AlgoCertificateRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "AlgoCertificateRequest_text", "text");
    expect_text(mv, 60, "AlgoCertificateRequest_transact_time", "transact_time");
    expect_text(mv, 3016, "3016", "algo_certificate_request_trans_type");
    expect_text(mv, 3022, "3022", "algo_certificate_status");
}

TEST_F(AllVersionsRoundtrip077Vlatest, AlgoCertificateRequestAck) {
    fixpp::vlatest::AlgoCertificateRequestAckArgs args{};
    args.reject_text = "AlgoCertificateRequestAck_reject_text";
    args.encoded_reject_text = "AlgoCertificateRequestAck_encoded_reject_text";
    args.algo_certificate_request_status = 3017;
    args.algo_certificate_request_type = 3077;
    auto built = fixpp::vlatest::build_AlgoCertificateRequestAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_AlgoCertificateRequestAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built AlgoCertificateRequestAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1328, "AlgoCertificateRequestAck_reject_text", "reject_text");
    expect_text(mv, 1665, "AlgoCertificateRequestAck_encoded_reject_text", "encoded_reject_text");
    expect_text(mv, 3017, "3017", "algo_certificate_request_status");
    expect_text(mv, 3077, "3077", "algo_certificate_request_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, AlgoCertificateReport) {
    fixpp::vlatest::AlgoCertificateReportArgs args{};
    args.text = "AlgoCertificateReport_text";
    args.transact_time = "AlgoCertificateReport_transact_time";
    args.algo_certificate_report_trans_type = 3020;
    args.algo_certificate_status = 3022;
    auto built = fixpp::vlatest::build_AlgoCertificateReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_AlgoCertificateReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built AlgoCertificateReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "AlgoCertificateReport_text", "text");
    expect_text(mv, 60, "AlgoCertificateReport_transact_time", "transact_time");
    expect_text(mv, 3020, "3020", "algo_certificate_report_trans_type");
    expect_text(mv, 3022, "3022", "algo_certificate_status");
}

TEST_F(AllVersionsRoundtrip077Vlatest, AlgoCertificateReportAck) {
    fixpp::vlatest::AlgoCertificateReportAckArgs args{};
    args.reject_text = "AlgoCertificateReportAck_reject_text";
    args.encoded_reject_text = "AlgoCertificateReportAck_encoded_reject_text";
    args.algo_certificate_report_status = 3021;
    args.algo_certificate_report_type = 3078;
    auto built = fixpp::vlatest::build_AlgoCertificateReportAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_AlgoCertificateReportAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built AlgoCertificateReportAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1328, "AlgoCertificateReportAck_reject_text", "reject_text");
    expect_text(mv, 1665, "AlgoCertificateReportAck_encoded_reject_text", "encoded_reject_text");
    expect_text(mv, 3021, "3021", "algo_certificate_report_status");
    expect_text(mv, 3078, "3078", "algo_certificate_report_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, TestSuiteDefinitionRequest) {
    fixpp::vlatest::TestSuiteDefinitionRequestArgs args{};
    args.text = "TestSuiteDefinitionRequest_text";
    args.transact_time = "TestSuiteDefinitionRequest_transact_time";
    args.test_suite_request_trans_type = 3064;
    auto built = fixpp::vlatest::build_TestSuiteDefinitionRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TestSuiteDefinitionRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TestSuiteDefinitionRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "TestSuiteDefinitionRequest_text", "text");
    expect_text(mv, 60, "TestSuiteDefinitionRequest_transact_time", "transact_time");
    expect_text(mv, 3064, "3064", "test_suite_request_trans_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, TestSuiteDefinitionRequestAck) {
    fixpp::vlatest::TestSuiteDefinitionRequestAckArgs args{};
    args.reject_text = "TestSuiteDefinitionRequestAck_reject_text";
    args.encoded_reject_text = "TestSuiteDefinitionRequestAck_encoded_reject_text";
    args.test_suite_request_status = 3065;
    auto built = fixpp::vlatest::build_TestSuiteDefinitionRequestAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TestSuiteDefinitionRequestAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TestSuiteDefinitionRequestAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1328, "TestSuiteDefinitionRequestAck_reject_text", "reject_text");
    expect_text(mv, 1665, "TestSuiteDefinitionRequestAck_encoded_reject_text", "encoded_reject_text");
    expect_text(mv, 3065, "3065", "test_suite_request_status");
}

TEST_F(AllVersionsRoundtrip077Vlatest, TestActionRequest) {
    fixpp::vlatest::TestActionRequestArgs args{};
    args.text = "TestActionRequest_text";
    args.transact_time = "TestActionRequest_transact_time";
    args.test_action_type = 3067;
    auto built = fixpp::vlatest::build_TestActionRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TestActionRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TestActionRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "TestActionRequest_text", "text");
    expect_text(mv, 60, "TestActionRequest_transact_time", "transact_time");
    expect_text(mv, 3067, "3067", "test_action_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, TestActionRequestAck) {
    fixpp::vlatest::TestActionRequestAckArgs args{};
    args.reject_text = "TestActionRequestAck_reject_text";
    args.encoded_reject_text = "TestActionRequestAck_encoded_reject_text";
    args.test_action_request_status = 3068;
    args.test_suite_activity_state = 3069;
    auto built = fixpp::vlatest::build_TestActionRequestAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TestActionRequestAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TestActionRequestAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1328, "TestActionRequestAck_reject_text", "reject_text");
    expect_text(mv, 1665, "TestActionRequestAck_encoded_reject_text", "encoded_reject_text");
    expect_text(mv, 3068, "3068", "test_action_request_status");
    expect_text(mv, 3069, "3069", "test_suite_activity_state");
}

TEST_F(AllVersionsRoundtrip077Vlatest, TestActionReport) {
    fixpp::vlatest::TestActionReportArgs args{};
    args.text = "TestActionReport_text";
    args.encoded_text = "TestActionReport_encoded_text";
    args.test_suite_status = 3070;
    auto built = fixpp::vlatest::build_TestActionReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TestActionReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TestActionReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "TestActionReport_text", "text");
    expect_text(mv, 355, "TestActionReport_encoded_text", "encoded_text");
    expect_text(mv, 3070, "3070", "test_suite_status");
}

TEST_F(AllVersionsRoundtrip077Vlatest, MarketDataAck) {
    fixpp::vlatest::MarketDataAckArgs args{};
    args.transact_time = "MarketDataAck_transact_time";
    args.reject_text = "MarketDataAck_reject_text";
    args.report_status = 3113;
    auto built = fixpp::vlatest::build_MarketDataAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MarketDataAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 60, "MarketDataAck_transact_time", "transact_time");
    expect_text(mv, 1328, "MarketDataAck_reject_text", "reject_text");
    expect_text(mv, 3113, "3113", "report_status");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SecurityStatusAck) {
    fixpp::vlatest::SecurityStatusAckArgs args{};
    args.transact_time = "SecurityStatusAck_transact_time";
    args.reject_text = "SecurityStatusAck_reject_text";
    args.report_status = 3113;
    auto built = fixpp::vlatest::build_SecurityStatusAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityStatusAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SecurityStatusAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 60, "SecurityStatusAck_transact_time", "transact_time");
    expect_text(mv, 1328, "SecurityStatusAck_reject_text", "reject_text");
    expect_text(mv, 3113, "3113", "report_status");
}

TEST_F(AllVersionsRoundtrip077Vlatest, TradingSessionStatusAck) {
    fixpp::vlatest::TradingSessionStatusAckArgs args{};
    args.transact_time = "TradingSessionStatusAck_transact_time";
    args.reject_text = "TradingSessionStatusAck_reject_text";
    args.report_status = 3113;
    auto built = fixpp::vlatest::build_TradingSessionStatusAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TradingSessionStatusAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TradingSessionStatusAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 60, "TradingSessionStatusAck_transact_time", "transact_time");
    expect_text(mv, 1328, "TradingSessionStatusAck_reject_text", "reject_text");
    expect_text(mv, 3113, "3113", "report_status");
}

TEST_F(AllVersionsRoundtrip077Vlatest, OrderCancelRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::OrderCancelRequestArgs args{};
    args.account = "OrderCancelRequest_account";
    args.cl_ord_id = "OrderCancelRequest_cl_ord_id";
    args.order_qty = make_decimal("10.5", &arena);
    args.side = '1';
    args.cash_order_qty = make_decimal("10.5", &arena);
    args.put_or_call = 201;
    auto built = fixpp::vlatest::build_OrderCancelRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_OrderCancelRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built OrderCancelRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "OrderCancelRequest_account", "account");
    expect_text(mv, 11, "OrderCancelRequest_cl_ord_id", "cl_ord_id");
    expect_decimal(mv, 38, "10.5", &read_arena, "order_qty");
    expect_text(mv, 54, "1", "side");
    expect_decimal(mv, 152, "10.5", &read_arena, "cash_order_qty");
    expect_text(mv, 201, "201", "put_or_call");
}

TEST_F(AllVersionsRoundtrip077Vlatest, OrderCancelReplaceRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::OrderCancelReplaceRequestArgs args{};
    args.account = "OrderCancelReplaceRequest_account";
    args.cl_ord_id = "OrderCancelReplaceRequest_cl_ord_id";
    args.commission = make_decimal("10.5", &arena);
    args.comm_type = '1';
    args.exec_inst = '1';
    args.order_qty = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_OrderCancelReplaceRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_OrderCancelReplaceRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built OrderCancelReplaceRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "OrderCancelReplaceRequest_account", "account");
    expect_text(mv, 11, "OrderCancelReplaceRequest_cl_ord_id", "cl_ord_id");
    expect_decimal(mv, 12, "10.5", &read_arena, "commission");
    expect_text(mv, 13, "1", "comm_type");
    expect_text(mv, 18, "1", "exec_inst");
    expect_decimal(mv, 38, "10.5", &read_arena, "order_qty");
}

TEST_F(AllVersionsRoundtrip077Vlatest, OrderStatusRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::OrderStatusRequestArgs args{};
    args.account = "OrderStatusRequest_account";
    args.cl_ord_id = "OrderStatusRequest_cl_ord_id";
    args.side = '1';
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    auto built = fixpp::vlatest::build_OrderStatusRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_OrderStatusRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built OrderStatusRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "OrderStatusRequest_account", "account");
    expect_text(mv, 11, "OrderStatusRequest_cl_ord_id", "cl_ord_id");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
}

TEST_F(AllVersionsRoundtrip077Vlatest, AllocationInstruction) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::AllocationInstructionArgs args{};
    args.avg_px = make_decimal("10.5", &arena);
    args.currency = "AllocationInstruction_currency";
    args.security_id_source = "AllocationInstruction_security_id_source";
    args.quantity = make_decimal("10.5", &arena);
    args.side = '1';
    args.alloc_trans_type = '1';
    auto built = fixpp::vlatest::build_AllocationInstruction(out, args);
    ASSERT_TRUE(built.has_value()) << "build_AllocationInstruction failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built AllocationInstruction frame failed (see ADD_FAILURE above)";
    expect_decimal(mv, 6, "10.5", &read_arena, "avg_px");
    expect_text(mv, 15, "AllocationInstruction_currency", "currency");
    expect_text(mv, 22, "AllocationInstruction_security_id_source", "security_id_source");
    expect_decimal(mv, 53, "10.5", &read_arena, "quantity");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 71, "1", "alloc_trans_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, ListCancelRequest) {
    fixpp::vlatest::ListCancelRequestArgs args{};
    args.text = "ListCancelRequest_text";
    args.transact_time = "ListCancelRequest_transact_time";
    auto built = fixpp::vlatest::build_ListCancelRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_ListCancelRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built ListCancelRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "ListCancelRequest_text", "text");
    expect_text(mv, 60, "ListCancelRequest_transact_time", "transact_time");
}

TEST_F(AllVersionsRoundtrip077Vlatest, ListExecute) {
    fixpp::vlatest::ListExecuteArgs args{};
    args.text = "ListExecute_text";
    args.transact_time = "ListExecute_transact_time";
    auto built = fixpp::vlatest::build_ListExecute(out, args);
    ASSERT_TRUE(built.has_value()) << "build_ListExecute failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built ListExecute frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "ListExecute_text", "text");
    expect_text(mv, 60, "ListExecute_transact_time", "transact_time");
}

TEST_F(AllVersionsRoundtrip077Vlatest, ListStatusRequest) {
    fixpp::vlatest::ListStatusRequestArgs args{};
    args.text = "ListStatusRequest_text";
    args.list_id = "ListStatusRequest_list_id";
    auto built = fixpp::vlatest::build_ListStatusRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_ListStatusRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built ListStatusRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "ListStatusRequest_text", "text");
    expect_text(mv, 66, "ListStatusRequest_list_id", "list_id");
}

TEST_F(AllVersionsRoundtrip077Vlatest, ListStatus) {
    fixpp::vlatest::ListStatusArgs args{};
    args.transact_time = "ListStatus_transact_time";
    args.list_id = "ListStatus_list_id";
    args.tot_no_orders = 68;
    args.no_rpts = 82;
    args.last_fragment = true;
    auto built = fixpp::vlatest::build_ListStatus(out, args);
    ASSERT_TRUE(built.has_value()) << "build_ListStatus failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built ListStatus frame failed (see ADD_FAILURE above)";
    expect_text(mv, 60, "ListStatus_transact_time", "transact_time");
    expect_text(mv, 66, "ListStatus_list_id", "list_id");
    expect_text(mv, 68, "68", "tot_no_orders");
    expect_text(mv, 82, "82", "no_rpts");
    expect_text(mv, 893, "Y", "last_fragment");
}

TEST_F(AllVersionsRoundtrip077Vlatest, AllocationInstructionAck) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::AllocationInstructionAckArgs args{};
    args.security_id_source = "AllocationInstructionAck_security_id_source";
    args.security_id = "AllocationInstructionAck_security_id";
    args.alloc_status = 87;
    args.alloc_rej_code = 88;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    auto built = fixpp::vlatest::build_AllocationInstructionAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_AllocationInstructionAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built AllocationInstructionAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 22, "AllocationInstructionAck_security_id_source", "security_id_source");
    expect_text(mv, 48, "AllocationInstructionAck_security_id", "security_id");
    expect_text(mv, 87, "87", "alloc_status");
    expect_text(mv, 88, "88", "alloc_rej_code");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
}

TEST_F(AllVersionsRoundtrip077Vlatest, DontKnowTrade) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::DontKnowTradeArgs args{};
    args.exec_id = "DontKnowTrade_exec_id";
    args.security_id_source = "DontKnowTrade_security_id_source";
    args.last_px = make_decimal("10.5", &arena);
    args.last_qty = make_decimal("10.5", &arena);
    args.side = '1';
    args.dk_reason = '1';
    auto built = fixpp::vlatest::build_DontKnowTrade(out, args);
    ASSERT_TRUE(built.has_value()) << "build_DontKnowTrade failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built DontKnowTrade frame failed (see ADD_FAILURE above)";
    expect_text(mv, 17, "DontKnowTrade_exec_id", "exec_id");
    expect_text(mv, 22, "DontKnowTrade_security_id_source", "security_id_source");
    expect_decimal(mv, 31, "10.5", &read_arena, "last_px");
    expect_decimal(mv, 32, "10.5", &read_arena, "last_qty");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 127, "1", "dk_reason");
}

TEST_F(AllVersionsRoundtrip077Vlatest, QuoteRequest) {
    fixpp::vlatest::QuoteRequestArgs args{};
    args.cl_ord_id = "QuoteRequest_cl_ord_id";
    args.text = "QuoteRequest_text";
    args.order_capacity = '1';
    args.order_restrictions = '1';
    args.booking_type = 775;
    args.pre_trade_anonymity = true;
    auto built = fixpp::vlatest::build_QuoteRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_QuoteRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built QuoteRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 11, "QuoteRequest_cl_ord_id", "cl_ord_id");
    expect_text(mv, 58, "QuoteRequest_text", "text");
    expect_text(mv, 528, "1", "order_capacity");
    expect_text(mv, 529, "1", "order_restrictions");
    expect_text(mv, 775, "775", "booking_type");
    expect_text(mv, 1091, "Y", "pre_trade_anonymity");
}

TEST_F(AllVersionsRoundtrip077Vlatest, Quote) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::QuoteArgs args{};
    args.account = "Quote_account";
    args.commission = make_decimal("10.5", &arena);
    args.comm_type = '1';
    args.currency = "Quote_currency";
    args.order_qty = make_decimal("10.5", &arena);
    args.ord_type = '1';
    auto built = fixpp::vlatest::build_Quote(out, args);
    ASSERT_TRUE(built.has_value()) << "build_Quote failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built Quote frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "Quote_account", "account");
    expect_decimal(mv, 12, "10.5", &read_arena, "commission");
    expect_text(mv, 13, "1", "comm_type");
    expect_text(mv, 15, "Quote_currency", "currency");
    expect_decimal(mv, 38, "10.5", &read_arena, "order_qty");
    expect_text(mv, 40, "1", "ord_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SettlementInstructions) {
    fixpp::vlatest::SettlementInstructionsArgs args{};
    args.cl_ord_id = "SettlementInstructions_cl_ord_id";
    args.text = "SettlementInstructions_text";
    args.settl_inst_mode = '1';
    args.settl_inst_req_rej_code = 792;
    auto built = fixpp::vlatest::build_SettlementInstructions(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SettlementInstructions failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SettlementInstructions frame failed (see ADD_FAILURE above)";
    expect_text(mv, 11, "SettlementInstructions_cl_ord_id", "cl_ord_id");
    expect_text(mv, 58, "SettlementInstructions_text", "text");
    expect_text(mv, 160, "1", "settl_inst_mode");
    expect_text(mv, 792, "792", "settl_inst_req_rej_code");
}

TEST_F(AllVersionsRoundtrip077Vlatest, MarketDataRequest) {
    fixpp::vlatest::MarketDataRequestArgs args{};
    args.md_req_id = "MarketDataRequest_md_req_id";
    args.subscription_request_type = '1';
    args.market_depth = 264;
    args.md_update_type = 265;
    args.aggregated_book = true;
    args.open_close_settl_flag = '1';
    auto built = fixpp::vlatest::build_MarketDataRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MarketDataRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 262, "MarketDataRequest_md_req_id", "md_req_id");
    expect_text(mv, 263, "1", "subscription_request_type");
    expect_text(mv, 264, "264", "market_depth");
    expect_text(mv, 265, "265", "md_update_type");
    expect_text(mv, 266, "Y", "aggregated_book");
    expect_text(mv, 286, "1", "open_close_settl_flag");
}

TEST_F(AllVersionsRoundtrip077Vlatest, MarketDataSnapshotFullRefresh) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::MarketDataSnapshotFullRefreshArgs args{};
    args.security_id_source = "MarketDataSnapshotFullRefresh_security_id_source";
    args.security_id = "MarketDataSnapshotFullRefresh_security_id";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_MarketDataSnapshotFullRefresh(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataSnapshotFullRefresh failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MarketDataSnapshotFullRefresh frame failed (see ADD_FAILURE above)";
    expect_text(mv, 22, "MarketDataSnapshotFullRefresh_security_id_source", "security_id_source");
    expect_text(mv, 48, "MarketDataSnapshotFullRefresh_security_id", "security_id");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, MarketDataIncrementalRefresh) {
    fixpp::vlatest::MarketDataIncrementalRefreshArgs args{};
    args.trade_date = "MarketDataIncrementalRefresh_trade_date";
    args.md_req_id = "MarketDataIncrementalRefresh_md_req_id";
    args.appl_queue_depth = 813;
    args.appl_queue_resolution = 814;
    args.appl_resend_flag = true;
    auto built = fixpp::vlatest::build_MarketDataIncrementalRefresh(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataIncrementalRefresh failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MarketDataIncrementalRefresh frame failed (see ADD_FAILURE above)";
    expect_text(mv, 75, "MarketDataIncrementalRefresh_trade_date", "trade_date");
    expect_text(mv, 262, "MarketDataIncrementalRefresh_md_req_id", "md_req_id");
    expect_text(mv, 813, "813", "appl_queue_depth");
    expect_text(mv, 814, "814", "appl_queue_resolution");
    expect_text(mv, 1352, "Y", "appl_resend_flag");
}

TEST_F(AllVersionsRoundtrip077Vlatest, MarketDataRequestReject) {
    fixpp::vlatest::MarketDataRequestRejectArgs args{};
    args.text = "MarketDataRequestReject_text";
    args.md_req_id = "MarketDataRequestReject_md_req_id";
    args.md_req_rej_reason = '1';
    auto built = fixpp::vlatest::build_MarketDataRequestReject(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataRequestReject failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MarketDataRequestReject frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "MarketDataRequestReject_text", "text");
    expect_text(mv, 262, "MarketDataRequestReject_md_req_id", "md_req_id");
    expect_text(mv, 281, "1", "md_req_rej_reason");
}

TEST_F(AllVersionsRoundtrip077Vlatest, QuoteCancel) {
    fixpp::vlatest::QuoteCancelArgs args{};
    args.account = "QuoteCancel_account";
    args.quote_id = "QuoteCancel_quote_id";
    args.quote_cancel_type = 298;
    args.quote_response_level = 301;
    auto built = fixpp::vlatest::build_QuoteCancel(out, args);
    ASSERT_TRUE(built.has_value()) << "build_QuoteCancel failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built QuoteCancel frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "QuoteCancel_account", "account");
    expect_text(mv, 117, "QuoteCancel_quote_id", "quote_id");
    expect_text(mv, 298, "298", "quote_cancel_type");
    expect_text(mv, 301, "301", "quote_response_level");
}

TEST_F(AllVersionsRoundtrip077Vlatest, QuoteStatusRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::QuoteStatusRequestArgs args{};
    args.account = "QuoteStatusRequest_account";
    args.security_id_source = "QuoteStatusRequest_security_id_source";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_QuoteStatusRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_QuoteStatusRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built QuoteStatusRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "QuoteStatusRequest_account", "account");
    expect_text(mv, 22, "QuoteStatusRequest_security_id_source", "security_id_source");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, MassQuoteAck) {
    fixpp::vlatest::MassQuoteAckArgs args{};
    args.account = "MassQuoteAck_account";
    args.text = "MassQuoteAck_text";
    args.quote_status = 297;
    args.quote_cancel_type = 298;
    auto built = fixpp::vlatest::build_MassQuoteAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MassQuoteAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MassQuoteAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "MassQuoteAck_account", "account");
    expect_text(mv, 58, "MassQuoteAck_text", "text");
    expect_text(mv, 297, "297", "quote_status");
    expect_text(mv, 298, "298", "quote_cancel_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SecurityDefinitionRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::SecurityDefinitionRequestArgs args{};
    args.currency = "SecurityDefinitionRequest_currency";
    args.security_id_source = "SecurityDefinitionRequest_security_id_source";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.spread = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_SecurityDefinitionRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityDefinitionRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SecurityDefinitionRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 15, "SecurityDefinitionRequest_currency", "currency");
    expect_text(mv, 22, "SecurityDefinitionRequest_security_id_source", "security_id_source");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 218, "10.5", &read_arena, "spread");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SecurityDefinition) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::SecurityDefinitionArgs args{};
    args.currency = "SecurityDefinition_currency";
    args.security_id_source = "SecurityDefinition_security_id_source";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.spread = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_SecurityDefinition(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityDefinition failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SecurityDefinition frame failed (see ADD_FAILURE above)";
    expect_text(mv, 15, "SecurityDefinition_currency", "currency");
    expect_text(mv, 22, "SecurityDefinition_security_id_source", "security_id_source");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 218, "10.5", &read_arena, "spread");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SecurityStatusRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::SecurityStatusRequestArgs args{};
    args.currency = "SecurityStatusRequest_currency";
    args.security_id_source = "SecurityStatusRequest_security_id_source";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_SecurityStatusRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityStatusRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SecurityStatusRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 15, "SecurityStatusRequest_currency", "currency");
    expect_text(mv, 22, "SecurityStatusRequest_security_id_source", "security_id_source");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SecurityStatus) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::SecurityStatusArgs args{};
    args.currency = "SecurityStatus_currency";
    args.security_id_source = "SecurityStatus_security_id_source";
    args.last_px = make_decimal("10.5", &arena);
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    auto built = fixpp::vlatest::build_SecurityStatus(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityStatus failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SecurityStatus frame failed (see ADD_FAILURE above)";
    expect_text(mv, 15, "SecurityStatus_currency", "currency");
    expect_text(mv, 22, "SecurityStatus_security_id_source", "security_id_source");
    expect_decimal(mv, 31, "10.5", &read_arena, "last_px");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
}

TEST_F(AllVersionsRoundtrip077Vlatest, TradingSessionStatusRequest) {
    fixpp::vlatest::TradingSessionStatusRequestArgs args{};
    args.security_exchange = "TradingSessionStatusRequest_security_exchange";
    args.subscription_request_type = '1';
    args.trad_ses_req_id = "TradingSessionStatusRequest_trad_ses_req_id";
    args.trad_ses_method = 338;
    args.trad_ses_mode = 339;
    auto built = fixpp::vlatest::build_TradingSessionStatusRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TradingSessionStatusRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TradingSessionStatusRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 207, "TradingSessionStatusRequest_security_exchange", "security_exchange");
    expect_text(mv, 263, "1", "subscription_request_type");
    expect_text(mv, 335, "TradingSessionStatusRequest_trad_ses_req_id", "trad_ses_req_id");
    expect_text(mv, 338, "338", "trad_ses_method");
    expect_text(mv, 339, "339", "trad_ses_mode");
}

TEST_F(AllVersionsRoundtrip077Vlatest, TradingSessionStatus) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::TradingSessionStatusArgs args{};
    args.security_id_source = "TradingSessionStatus_security_id_source";
    args.security_id = "TradingSessionStatus_security_id";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_TradingSessionStatus(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TradingSessionStatus failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TradingSessionStatus frame failed (see ADD_FAILURE above)";
    expect_text(mv, 22, "TradingSessionStatus_security_id_source", "security_id_source");
    expect_text(mv, 48, "TradingSessionStatus_security_id", "security_id");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, MassQuote) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::MassQuoteArgs args{};
    args.account = "MassQuote_account";
    args.quote_id = "MassQuote_quote_id";
    args.def_bid_size = make_decimal("10.5", &arena);
    args.def_offer_size = make_decimal("10.5", &arena);
    args.quote_response_level = 301;
    args.quote_type = 537;
    auto built = fixpp::vlatest::build_MassQuote(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MassQuote failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MassQuote frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "MassQuote_account", "account");
    expect_text(mv, 117, "MassQuote_quote_id", "quote_id");
    expect_decimal(mv, 293, "10.5", &read_arena, "def_bid_size");
    expect_decimal(mv, 294, "10.5", &read_arena, "def_offer_size");
    expect_text(mv, 301, "301", "quote_response_level");
    expect_text(mv, 537, "537", "quote_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, BusinessMessageReject) {
    fixpp::vlatest::BusinessMessageRejectArgs args{};
    args.ref_seq_num = 45;
    args.text = "BusinessMessageReject_text";
    args.encoded_text = "BusinessMessageReject_encoded_text";
    args.business_reject_reason = 380;
    auto built = fixpp::vlatest::build_BusinessMessageReject(out, args);
    ASSERT_TRUE(built.has_value()) << "build_BusinessMessageReject failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built BusinessMessageReject frame failed (see ADD_FAILURE above)";
    expect_text(mv, 45, "45", "ref_seq_num");
    expect_text(mv, 58, "BusinessMessageReject_text", "text");
    expect_text(mv, 355, "BusinessMessageReject_encoded_text", "encoded_text");
    expect_text(mv, 380, "380", "business_reject_reason");
}

TEST_F(AllVersionsRoundtrip077Vlatest, BidRequest) {
    fixpp::vlatest::BidRequestArgs args{};
    args.currency = "BidRequest_currency";
    args.text = "BidRequest_text";
    args.forex_req = true;
    args.bid_request_trans_type = '1';
    args.tot_no_related_sym = 393;
    args.bid_type = 394;
    auto built = fixpp::vlatest::build_BidRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_BidRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built BidRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 15, "BidRequest_currency", "currency");
    expect_text(mv, 58, "BidRequest_text", "text");
    expect_text(mv, 121, "Y", "forex_req");
    expect_text(mv, 374, "1", "bid_request_trans_type");
    expect_text(mv, 393, "393", "tot_no_related_sym");
    expect_text(mv, 394, "394", "bid_type");
}

TEST_F(AllVersionsRoundtrip077Vlatest, BidResponse) {
    fixpp::vlatest::BidResponseArgs args{};
    args.bid_id = "BidResponse_bid_id";
    args.client_bid_id = "BidResponse_client_bid_id";
    auto built = fixpp::vlatest::build_BidResponse(out, args);
    ASSERT_TRUE(built.has_value()) << "build_BidResponse failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built BidResponse frame failed (see ADD_FAILURE above)";
    expect_text(mv, 390, "BidResponse_bid_id", "bid_id");
    expect_text(mv, 391, "BidResponse_client_bid_id", "client_bid_id");
}

TEST_F(AllVersionsRoundtrip077Vlatest, ListStrikePrice) {
    fixpp::vlatest::ListStrikePriceArgs args{};
    args.list_id = "ListStrikePrice_list_id";
    args.tot_no_strikes = 422;
    args.last_fragment = true;
    auto built = fixpp::vlatest::build_ListStrikePrice(out, args);
    ASSERT_TRUE(built.has_value()) << "build_ListStrikePrice failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built ListStrikePrice frame failed (see ADD_FAILURE above)";
    expect_text(mv, 66, "ListStrikePrice_list_id", "list_id");
    expect_text(mv, 422, "422", "tot_no_strikes");
    expect_text(mv, 893, "Y", "last_fragment");
}

TEST_F(AllVersionsRoundtrip077Vlatest, RegistrationInstructions) {
    fixpp::vlatest::RegistrationInstructionsArgs args{};
    args.account = "RegistrationInstructions_account";
    args.cl_ord_id = "RegistrationInstructions_cl_ord_id";
    args.tax_advantage_type = 495;
    args.regist_trans_type = '1';
    args.ownership_type = '1';
    args.acct_id_source = 660;
    auto built = fixpp::vlatest::build_RegistrationInstructions(out, args);
    ASSERT_TRUE(built.has_value()) << "build_RegistrationInstructions failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built RegistrationInstructions frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "RegistrationInstructions_account", "account");
    expect_text(mv, 11, "RegistrationInstructions_cl_ord_id", "cl_ord_id");
    expect_text(mv, 495, "495", "tax_advantage_type");
    expect_text(mv, 514, "1", "regist_trans_type");
    expect_text(mv, 517, "1", "ownership_type");
    expect_text(mv, 660, "660", "acct_id_source");
}

TEST_F(AllVersionsRoundtrip077Vlatest, RegistrationInstructionsResponse) {
    fixpp::vlatest::RegistrationInstructionsResponseArgs args{};
    args.account = "RegistrationInstructionsResponse_account";
    args.cl_ord_id = "RegistrationInstructionsResponse_cl_ord_id";
    args.regist_status = '1';
    args.regist_rej_reason_code = 507;
    args.regist_trans_type = '1';
    args.acct_id_source = 660;
    auto built = fixpp::vlatest::build_RegistrationInstructionsResponse(out, args);
    ASSERT_TRUE(built.has_value()) << "build_RegistrationInstructionsResponse failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built RegistrationInstructionsResponse frame failed (see ADD_FAILURE above)";
    expect_text(mv, 1, "RegistrationInstructionsResponse_account", "account");
    expect_text(mv, 11, "RegistrationInstructionsResponse_cl_ord_id", "cl_ord_id");
    expect_text(mv, 506, "1", "regist_status");
    expect_text(mv, 507, "507", "regist_rej_reason_code");
    expect_text(mv, 514, "1", "regist_trans_type");
    expect_text(mv, 660, "660", "acct_id_source");
}

TEST_F(AllVersionsRoundtrip077Vlatest, OrderMassCancelRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::OrderMassCancelRequestArgs args{};
    args.cl_ord_id = "OrderMassCancelRequest_cl_ord_id";
    args.security_id_source = "OrderMassCancelRequest_security_id_source";
    args.side = '1';
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    auto built = fixpp::vlatest::build_OrderMassCancelRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_OrderMassCancelRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built OrderMassCancelRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 11, "OrderMassCancelRequest_cl_ord_id", "cl_ord_id");
    expect_text(mv, 22, "OrderMassCancelRequest_security_id_source", "security_id_source");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
}

TEST_F(AllVersionsRoundtrip077Vlatest, OrderMassCancelReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::OrderMassCancelReportArgs args{};
    args.cl_ord_id = "OrderMassCancelReport_cl_ord_id";
    args.security_id_source = "OrderMassCancelReport_security_id_source";
    args.side = '1';
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    auto built = fixpp::vlatest::build_OrderMassCancelReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_OrderMassCancelReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built OrderMassCancelReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 11, "OrderMassCancelReport_cl_ord_id", "cl_ord_id");
    expect_text(mv, 22, "OrderMassCancelReport_security_id_source", "security_id_source");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
}

TEST_F(AllVersionsRoundtrip077Vlatest, NewOrderCross) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::NewOrderCrossArgs args{};
    args.currency = "NewOrderCross_currency";
    args.exec_inst = '1';
    args.handl_inst = '1';
    args.security_id_source = "NewOrderCross_security_id_source";
    args.price = make_decimal("10.5", &arena);
    args.stop_px = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_NewOrderCross(out, args);
    ASSERT_TRUE(built.has_value()) << "build_NewOrderCross failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built NewOrderCross frame failed (see ADD_FAILURE above)";
    expect_text(mv, 15, "NewOrderCross_currency", "currency");
    expect_text(mv, 18, "1", "exec_inst");
    expect_text(mv, 21, "1", "handl_inst");
    expect_text(mv, 22, "NewOrderCross_security_id_source", "security_id_source");
    expect_decimal(mv, 44, "10.5", &read_arena, "price");
    expect_decimal(mv, 99, "10.5", &read_arena, "stop_px");
}

TEST_F(AllVersionsRoundtrip077Vlatest, CrossOrderCancelReplaceRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::CrossOrderCancelReplaceRequestArgs args{};
    args.currency = "CrossOrderCancelReplaceRequest_currency";
    args.exec_inst = '1';
    args.handl_inst = '1';
    args.security_id_source = "CrossOrderCancelReplaceRequest_security_id_source";
    args.price = make_decimal("10.5", &arena);
    args.stop_px = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_CrossOrderCancelReplaceRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_CrossOrderCancelReplaceRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built CrossOrderCancelReplaceRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 15, "CrossOrderCancelReplaceRequest_currency", "currency");
    expect_text(mv, 18, "1", "exec_inst");
    expect_text(mv, 21, "1", "handl_inst");
    expect_text(mv, 22, "CrossOrderCancelReplaceRequest_security_id_source", "security_id_source");
    expect_decimal(mv, 44, "10.5", &read_arena, "price");
    expect_decimal(mv, 99, "10.5", &read_arena, "stop_px");
}

TEST_F(AllVersionsRoundtrip077Vlatest, CrossOrderCancelRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::CrossOrderCancelRequestArgs args{};
    args.security_id_source = "CrossOrderCancelRequest_security_id_source";
    args.order_id = "CrossOrderCancelRequest_order_id";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_CrossOrderCancelRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_CrossOrderCancelRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built CrossOrderCancelRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 22, "CrossOrderCancelRequest_security_id_source", "security_id_source");
    expect_text(mv, 37, "CrossOrderCancelRequest_order_id", "order_id");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SecurityTypeRequest) {
    fixpp::vlatest::SecurityTypeRequestArgs args{};
    args.text = "SecurityTypeRequest_text";
    args.security_type = "SecurityTypeRequest_security_type";
    args.product = 460;
    auto built = fixpp::vlatest::build_SecurityTypeRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityTypeRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SecurityTypeRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "SecurityTypeRequest_text", "text");
    expect_text(mv, 167, "SecurityTypeRequest_security_type", "security_type");
    expect_text(mv, 460, "460", "product");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SecurityTypes) {
    fixpp::vlatest::SecurityTypesArgs args{};
    args.text = "SecurityTypes_text";
    args.subscription_request_type = '1';
    args.security_req_id = "SecurityTypes_security_req_id";
    args.security_response_type = 323;
    args.tot_no_security_types = 557;
    args.last_fragment = true;
    auto built = fixpp::vlatest::build_SecurityTypes(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityTypes failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SecurityTypes frame failed (see ADD_FAILURE above)";
    expect_text(mv, 58, "SecurityTypes_text", "text");
    expect_text(mv, 263, "1", "subscription_request_type");
    expect_text(mv, 320, "SecurityTypes_security_req_id", "security_req_id");
    expect_text(mv, 323, "323", "security_response_type");
    expect_text(mv, 557, "557", "tot_no_security_types");
    expect_text(mv, 893, "Y", "last_fragment");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SecurityListRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::SecurityListRequestArgs args{};
    args.currency = "SecurityListRequest_currency";
    args.security_id_source = "SecurityListRequest_security_id_source";
    args.put_or_call = 201;
    args.strike_price = make_decimal("10.5", &arena);
    args.opt_attribute = '1';
    args.coupon_rate = make_decimal("10.5", &arena);
    auto built = fixpp::vlatest::build_SecurityListRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityListRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SecurityListRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 15, "SecurityListRequest_currency", "currency");
    expect_text(mv, 22, "SecurityListRequest_security_id_source", "security_id_source");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "10.5", &read_arena, "strike_price");
    expect_text(mv, 206, "1", "opt_attribute");
    expect_decimal(mv, 223, "10.5", &read_arena, "coupon_rate");
}

TEST_F(AllVersionsRoundtrip077Vlatest, SecurityList) {
    fixpp::vlatest::SecurityListArgs args{};
    args.transact_time = "SecurityList_transact_time";
    args.security_req_id = "SecurityList_security_req_id";
    args.tot_no_related_sym = 393;
    args.security_request_result = 560;
    args.last_fragment = true;
    args.appl_resend_flag = true;
    auto built = fixpp::vlatest::build_SecurityList(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityList failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SecurityList frame failed (see ADD_FAILURE above)";
    expect_text(mv, 60, "SecurityList_transact_time", "transact_time");
    expect_text(mv, 320, "SecurityList_security_req_id", "security_req_id");
    expect_text(mv, 393, "393", "tot_no_related_sym");
    expect_text(mv, 560, "560", "security_request_result");
    expect_text(mv, 893, "Y", "last_fragment");
    expect_text(mv, 1352, "Y", "appl_resend_flag");
}

TEST_F(AllVersionsRoundtrip077Vlatest, DerivativeSecurityListRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::vlatest::DerivativeSecurityListRequestArgs args{};
    args.currency = "DerivativeSecurityListRequest_currency";
    args.text = "DerivativeSecurityListRequest_text";
    args.underlying_repurchase_term = 244;
    args.underlying_repurchase_rate = make_decimal("10.5", &arena);
    args.underlying_factor = make_decimal("10.5", &arena);
    args.subscription_request_type = '1';
    auto built = fixpp::vlatest::build_DerivativeSecurityListRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_DerivativeSecurityListRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIXT.1.1", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built DerivativeSecurityListRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 15, "DerivativeSecurityListRequest_currency", "currency");
    expect_text(mv, 58, "DerivativeSecurityListRequest_text", "text");
    expect_text(mv, 244, "244", "underlying_repurchase_term");
    expect_decimal(mv, 245, "10.5", &read_arena, "underlying_repurchase_rate");
    expect_decimal(mv, 246, "10.5", &read_arena, "underlying_factor");
    expect_text(mv, 263, "1", "subscription_request_type");
}
