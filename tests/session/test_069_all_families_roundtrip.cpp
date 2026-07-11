// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_069_all_families_roundtrip.cpp
//
// 069-v44-all-families T010/T013 [US2] -- differential round-trip over ALL
// 83 emitted application builders (build_<Msg> -> wire bytes -> independent
// dict-driven runtime-XML parse -> per-TAG readback, contracts/coverage-and-
// completeness.md C3) PLUS the new-family required-field fail-closed
// witnesses (FR-006, T013).
//
// Uniform GENERIC tag-based readback (NOT per-message typed flyweights,
// unlike test_067_builder_roundtrip.cpp): every top-level scalar is read via
// MessageView::get(tag)/get_decimal(tag, mr) against a hand-authored expected
// wire value (never captured post-hoc -- feedback_coverage_push_enshrines_bugs);
// every group-entry field is read via OffsetTable::group_slices(no_tag) + a
// local scan_slice_for_tag() byte scan (mirrors the C-ABI's own
// src/capi/message_read.cpp:scan_slice_for_tag mechanism -- fixpp_msg_get_group
// et al -- the group's root group_context is seeded unconditionally at
// MessageView construction, parser.hpp:144, so group_slices() works
// immediately post-parse with no typed group<>() call).
//
// Seeds: for each of the 83 registry MsgTypes, the message's own
// required='Y' scalar fields (dictionaries/FIX44.xml line cited per field
// below); messages with fewer than 2 own-required scalars are topped up with
// 1-2 adjacent body scalars (not required) so every row has >=2 fields.
// Required GROUPS are seeded/asserted only for the mandated nested exemplar
// (TradeCaptureReport/AE, TrdCapRptSideGrp/NoSides -- FIX44.xml:3536-3538);
// the other 82 messages leave their (possibly-required) groups empty --
// build_<Msg> never validates group cardinality (that is validate_<Msg>'s
// job, T013/US3, exercised separately below for exactly 2 discriminating
// cases) so an empty required group does not block THIS harness's build+
// parse+readback goal.
//
// Anchors: specs/069-v44-all-families/tasks.md T010/T013;
//          specs/069-v44-all-families/contracts/coverage-and-completeness.md
//          C3; tests/session/test_067_builder_roundtrip.cpp (host TU/scaffold
//          precedent); tests/session/test_067_builder_failclosed.cpp (T013
//          disposition precedent, wire_required_field_missing);
//          tests/session/test_067_completeness.cpp (set-equality diagnostic
//          precedent, lines 138-155); include/fixpp/wire/builder_validate.hpp
//          (validate_required, wire_required_field_missing at :77/:86);
//          src/capi/message_read.cpp:141 (scan_slice_for_tag precedent).

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/v44/Builders.hpp>  // GENERATED (Phase 3b) -- build_<Msg>/validate_<Msg>/<Msg>Args/builder_registry
#include <fixpp/v44/Messages.hpp>
#include <memory_resource>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/app_message_read_scaffold.hpp"

namespace {

using fixpp::decimal_t;
using IndexView = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;

decimal_t make_decimal(std::string_view sv, std::pmr::memory_resource* mr) {
    auto bytes =
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
    auto r = decimal_t::parse(bytes, mr);
    EXPECT_TRUE(r.has_value()) << "make_decimal failed for: " << sv;
    return r.value_or(decimal_t{});
}

std::string bytes_to_string(std::span<const std::byte> b) {
    return std::string{reinterpret_cast<const char*>(b.data()), b.size()};
}

// Generic top-level tag readback: compare the independent dict-driven parse's
// raw wire bytes for `tag` against a hand-authored expected string (String/
// Char verbatim; Bool as Y/N; Int as decimal digits) -- uniform across all 83,
// no per-message accessor.
void expect_wire_text(IndexView const& mv, std::uint16_t tag, std::string_view expected,
                      char const* label) {
    SCOPED_TRACE(label);
    auto fv = mv.get(tag);
    ASSERT_TRUE(fv.has_value()) << "tag " << tag << " not found in parsed frame";
    EXPECT_EQ(fv->as_string(), expected);
}

// Generic top-level DECIMAL tag readback: decode both sides via decimal_t and
// compare VALUES (not raw wire text) -- mirrors 067's expect_eq_decimal, so a
// re-canonicalizing decimal formatter cannot spuriously fail this genuinely
// differential check (build_<Msg> serializes via body_builder::field(tag,
// decimal_t); the independent dict-driven Parser<Index> + get_decimal decodes
// the SAME bytes through the SAME 2a decimal_t::parse boundary, from a
// completely separate call site).
void expect_wire_decimal(IndexView const& mv, std::uint16_t tag, std::string_view expected_ascii,
                         std::pmr::memory_resource* mr, char const* label) {
    SCOPED_TRACE(label);
    auto got = mv.get_decimal(tag, mr);
    ASSERT_TRUE(got.has_value()) << "tag " << tag << " not found/decodable in parsed frame";
    EXPECT_EQ(*got, make_decimal(expected_ascii, mr));
}

// Local group-entry scan (mirrors src/capi/message_read.cpp's TU-local
// scan_slice_for_tag -- not exported, so replicated here at test scope): walks
// a bounded {data,len} group-instance slice looking for `\x01<tag>=<value>`,
// returning the value up to the next SOH/end-of-slice. Uniform across every
// group-entry readback in this file (currently just the AE/NoSides exemplar).
std::optional<std::string_view> scan_slice_for_tag(std::span<const std::byte> slice,
                                                   std::uint16_t tag) {
    std::string_view sv{reinterpret_cast<char const*>(slice.data()), slice.size()};
    std::size_t pos = 0;
    while (pos < sv.size()) {
        std::size_t const eq = sv.find('=', pos);
        if (eq == std::string_view::npos) break;
        std::string_view const tag_sv = sv.substr(pos, eq - pos);
        std::size_t const soh = sv.find('\x01', eq);
        std::size_t const value_end = (soh == std::string_view::npos) ? sv.size() : soh;
        std::uint16_t parsed_tag = 0;
        bool ok = !tag_sv.empty();
        for (char c : tag_sv) {
            if (c < '0' || c > '9') {
                ok = false;
                break;
            }
            parsed_tag = static_cast<std::uint16_t>(parsed_tag * 10 + (c - '0'));
        }
        if (ok && parsed_tag == tag) {
            return sv.substr(eq + 1, value_end - (eq + 1));
        }
        if (soh == std::string_view::npos) break;
        pos = soh + 1;
    }
    return std::nullopt;
}

// Shared build->frame->dict-parse pipeline. `tv` is CALLER-constructed and
// must be a named local in the TEST body (NOT rebuilt inside this helper):
// MessageView's opaque_dict_ aliases the `table_view` OBJECT itself (not
// `dict`), so a `table_view` built as a temporary inside this function would
// dangle the instant it returned, and a later group_slices()/
// group_slices_reserve_bound() call (AE's nested-depth read below) would
// dereference freed stack memory -- caught empirically via a SIGSEGV in
// table_view::group_member_tags on the TradeCaptureReport case before this
// fix (033-era lifetime contract, table_view.hpp/app_message_read_scaffold.hpp).
// Returns the parsed MessageView (borrowing from `read_arena`/`tv`, both
// caller-owned and must outlive the returned view).
template <typename BuildFn>
std::optional<IndexView> build_and_parse(BuildFn&& build_fn, std::span<std::byte> out,
                                         std::string& body_out, fixpp::dict::table_view const& tv,
                                         std::pmr::memory_resource* read_arena,
                                         std::vector<std::byte>& frame_storage) {
    auto built = build_fn(out);
    if (!built.has_value()) {
        ADD_FAILURE() << "build_<Msg> failed";
        return std::nullopt;
    }
    body_out = bytes_to_string(*built);
    frame_storage = fixpp_test_support::make_frame("FIX.4.4", body_out);
    auto mv = fixpp_test_support::parse_dict(frame_storage, tv, read_arena);
    return mv;
}

}  // namespace

// Shared FIX44 dictionary + table_view, loaded ONCE for the whole suite (was:
// re-parsed from scratch in each of the 83 test bodies via load_fix44()).
// Lifetime: SetUpTestSuite() runs before any TestBody() in this suite and
// TearDownTestSuite() runs after every one of them, so `tv_` (which aliases
// the `table_view` OBJECT per the comment on build_and_parse() above) is
// guaranteed to outlive every per-test MessageView -- a stronger version of
// the same "table_view must outlive the borrowed view" contract, just moved
// from per-test-local to per-suite-static. Teardown order is the mirror of
// construction: tv_ (the view) is destroyed before dict_ (its backing
// dictionary) before dict_arena_ (dict_'s backing allocator).
class AllFamiliesRoundtrip069 : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        dict_arena_ = new std::pmr::monotonic_buffer_resource(16384);
        dict_ = new fixpp::dict::Dictionary(fixpp_test_support::load_fix44(dict_arena_));
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

    // Per-test build/parse state (fresh per TestBody(), same semantics as the
    // former per-test locals).
    std::array<std::byte, 4096> out{};
    std::string body;
    std::pmr::monotonic_buffer_resource read_arena{16384};
    std::vector<std::byte> frame_storage;

    template <typename BuildFn>
    std::optional<IndexView> parse(BuildFn&& build_fn) {
        return build_and_parse(std::forward<BuildFn>(build_fn), out, body, *tv_, &read_arena,
                               frame_storage);
    }

    static std::pmr::monotonic_buffer_resource* dict_arena_;
    static fixpp::dict::Dictionary* dict_;
    static fixpp::dict::table_view* tv_;
};

std::pmr::monotonic_buffer_resource* AllFamiliesRoundtrip069::dict_arena_ = nullptr;
fixpp::dict::Dictionary* AllFamiliesRoundtrip069::dict_ = nullptr;
fixpp::dict::table_view* AllFamiliesRoundtrip069::tv_ = nullptr;

// IOI (6) -- dictionaries/FIX44.xml:64-92
//   required 'IOIID' (FIX44.xml:65) -> ioiid(tag 23)
//   required 'IOITransType' (FIX44.xml:66) -> ioi_trans_type(tag 28)
//   required 'Side' (FIX44.xml:71) -> side(tag 54)
//   required 'IOIQty' (FIX44.xml:74) -> ioi_qty(tag 27)
TEST_F(AllFamiliesRoundtrip069, IOI) {
    fixpp::v44::IOIArgs args{};
    args.ioiid = "6_ioiid";
    args.ioi_trans_type = '1';
    args.side = '1';
    args.ioi_qty = "6_ioi_qty";
    auto mv_opt = parse([&](std::span<std::byte> o) { return fixpp::v44::build_IOI(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 23, "6_ioiid", "ioiid");
    expect_wire_text(mv, 28, "1", "ioi_trans_type");
    expect_wire_text(mv, 54, "1", "side");
    expect_wire_text(mv, 27, "6_ioi_qty", "ioi_qty");
}

// Advertisement (7) -- dictionaries/FIX44.xml:93-114
//   required 'AdvId' (FIX44.xml:94) -> adv_id(tag 2)
//   required 'AdvTransType' (FIX44.xml:95) -> adv_trans_type(tag 5)
//   required 'AdvSide' (FIX44.xml:100) -> adv_side(tag 4)
//   required 'Quantity' (FIX44.xml:101) -> quantity(tag 53)
TEST_F(AllFamiliesRoundtrip069, Advertisement) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v44::AdvertisementArgs args{};
    args.adv_id = "7_adv_id";
    args.adv_trans_type = "7_adv_trans_type";
    args.adv_side = '1';
    args.quantity = make_decimal("10.5", &arena);
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_Advertisement(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 2, "7_adv_id", "adv_id");
    expect_wire_text(mv, 5, "7_adv_trans_type", "adv_trans_type");
    expect_wire_text(mv, 4, "1", "adv_side");
    expect_wire_decimal(mv, 53, "10.5", &read_arena, "quantity");
}

// ExecutionReport (8) -- dictionaries/FIX44.xml:115-254
//   required 'OrderID' (FIX44.xml:116) -> order_id(tag 37)
//   required 'ExecID' (FIX44.xml:135) -> exec_id(tag 17)
//   required 'ExecType' (FIX44.xml:137) -> exec_type(tag 150)
//   required 'OrdStatus' (FIX44.xml:138) -> ord_status(tag 39)
//   required 'Side' (FIX44.xml:155) -> side(tag 54)
//   required 'LeavesQty' (FIX44.xml:194) -> leaves_qty(tag 151)
//   required 'CumQty' (FIX44.xml:195) -> cum_qty(tag 14)
//   required 'AvgPx' (FIX44.xml:196) -> avg_px(tag 6)
TEST_F(AllFamiliesRoundtrip069, ExecutionReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v44::ExecutionReportArgs args{};
    args.order_id = "8_order_id";
    args.exec_id = "8_exec_id";
    args.exec_type = '1';
    args.ord_status = '1';
    args.side = '1';
    args.leaves_qty = make_decimal("10.5", &arena);
    args.cum_qty = make_decimal("10.5", &arena);
    args.avg_px = make_decimal("10.5", &arena);
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_ExecutionReport(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 37, "8_order_id", "order_id");
    expect_wire_text(mv, 17, "8_exec_id", "exec_id");
    expect_wire_text(mv, 150, "1", "exec_type");
    expect_wire_text(mv, 39, "1", "ord_status");
    expect_wire_text(mv, 54, "1", "side");
    expect_wire_decimal(mv, 151, "10.5", &read_arena, "leaves_qty");
    expect_wire_decimal(mv, 14, "10.5", &read_arena, "cum_qty");
    expect_wire_decimal(mv, 6, "10.5", &read_arena, "avg_px");
}

// OrderCancelReject (9) -- dictionaries/FIX44.xml:255-277
//   required 'OrderID' (FIX44.xml:256) -> order_id(tag 37)
//   required 'ClOrdID' (FIX44.xml:259) -> cl_ord_id(tag 11)
//   required 'OrigClOrdID' (FIX44.xml:261) -> orig_cl_ord_id(tag 41)
//   required 'OrdStatus' (FIX44.xml:262) -> ord_status(tag 39)
//   required 'CxlRejResponseTo' (FIX44.xml:272) -> cxl_rej_response_to(tag 434)
TEST_F(AllFamiliesRoundtrip069, OrderCancelReject) {
    fixpp::v44::OrderCancelRejectArgs args{};
    args.order_id = "9_order_id";
    args.cl_ord_id = "9_cl_ord_id";
    args.orig_cl_ord_id = "9_orig_cl_ord_id";
    args.ord_status = '1';
    args.cxl_rej_response_to = '1';
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_OrderCancelReject(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 37, "9_order_id", "order_id");
    expect_wire_text(mv, 11, "9_cl_ord_id", "cl_ord_id");
    expect_wire_text(mv, 41, "9_orig_cl_ord_id", "orig_cl_ord_id");
    expect_wire_text(mv, 39, "1", "ord_status");
    expect_wire_text(mv, 434, "1", "cxl_rej_response_to");
}

// DerivativeSecurityList (AA) -- dictionaries/FIX44.xml:1239-1247
//   required 'SecurityReqID' (FIX44.xml:1240) -> security_req_id(tag 320)
//   required 'SecurityResponseID' (FIX44.xml:1241) -> security_response_id(tag 322)
//   required 'SecurityRequestResult' (FIX44.xml:1242) -> security_request_result(tag 560)
TEST_F(AllFamiliesRoundtrip069, DerivativeSecurityList) {
    fixpp::v44::DerivativeSecurityListArgs args{};
    args.security_req_id = "AA_security_req_id";
    args.security_response_id = "AA_security_response_id";
    args.security_request_result = 560;
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_DerivativeSecurityList(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 320, "AA_security_req_id", "security_req_id");
    expect_wire_text(mv, 322, "AA_security_response_id", "security_response_id");
    expect_wire_text(mv, 560, "560", "security_request_result");
}

// NewOrderMultileg (AB) -- dictionaries/FIX44.xml:1248-1320
//   required 'ClOrdID' (FIX44.xml:1249) -> cl_ord_id(tag 11)
//   required 'Side' (FIX44.xml:1274) -> side(tag 54)
//   required 'TransactTime' (FIX44.xml:1280) -> transact_time(tag 60)
//   required 'OrdType' (FIX44.xml:1283) -> ord_type(tag 40)
TEST_F(AllFamiliesRoundtrip069, NewOrderMultileg) {
    fixpp::v44::NewOrderMultilegArgs args{};
    args.cl_ord_id = "AB_cl_ord_id";
    args.side = '1';
    args.transact_time = "AB_transact_time";
    args.ord_type = '1';
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_NewOrderMultileg(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 11, "AB_cl_ord_id", "cl_ord_id");
    expect_wire_text(mv, 54, "1", "side");
    expect_wire_text(mv, 60, "AB_transact_time", "transact_time");
    expect_wire_text(mv, 40, "1", "ord_type");
}

// MultilegOrderCancelReplace (AC) -- dictionaries/FIX44.xml:1321-1396
//   required 'OrigClOrdID' (FIX44.xml:1323) -> orig_cl_ord_id(tag 41)
//   required 'ClOrdID' (FIX44.xml:1324) -> cl_ord_id(tag 11)
//   required 'Side' (FIX44.xml:1350) -> side(tag 54)
//   required 'TransactTime' (FIX44.xml:1356) -> transact_time(tag 60)
//   required 'OrdType' (FIX44.xml:1359) -> ord_type(tag 40)
TEST_F(AllFamiliesRoundtrip069, MultilegOrderCancelReplace) {
    fixpp::v44::MultilegOrderCancelReplaceArgs args{};
    args.orig_cl_ord_id = "AC_orig_cl_ord_id";
    args.cl_ord_id = "AC_cl_ord_id";
    args.side = '1';
    args.transact_time = "AC_transact_time";
    args.ord_type = '1';
    auto mv_opt = parse([&](std::span<std::byte> o) {
        return fixpp::v44::build_MultilegOrderCancelReplace(o, args);
    });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 41, "AC_orig_cl_ord_id", "orig_cl_ord_id");
    expect_wire_text(mv, 11, "AC_cl_ord_id", "cl_ord_id");
    expect_wire_text(mv, 54, "1", "side");
    expect_wire_text(mv, 60, "AC_transact_time", "transact_time");
    expect_wire_text(mv, 40, "1", "ord_type");
}

// TradeCaptureReportRequest (AD) -- dictionaries/FIX44.xml:1397-1434
//   required 'TradeRequestID' (FIX44.xml:1398) -> trade_request_id(tag 568)
//   required 'TradeRequestType' (FIX44.xml:1399) -> trade_request_type(tag 569)
TEST_F(AllFamiliesRoundtrip069, TradeCaptureReportRequest) {
    fixpp::v44::TradeCaptureReportRequestArgs args{};
    args.trade_request_id = "AD_trade_request_id";
    args.trade_request_type = 569;
    auto mv_opt = parse([&](std::span<std::byte> o) {
        return fixpp::v44::build_TradeCaptureReportRequest(o, args);
    });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 568, "AD_trade_request_id", "trade_request_id");
    expect_wire_text(mv, 569, "569", "trade_request_type");
}

// TradeCaptureReport (AE) -- dictionaries/FIX44.xml:1435-1493
//   required 'TradeReportID' (FIX44.xml:1436) -> trade_report_id(tag 571)
//   required 'PreviouslyReported' (FIX44.xml:1458) -> previously_reported(tag 570)
//   required 'LastQty' (FIX44.xml:1468) -> last_qty(tag 32)
//   required 'LastPx' (FIX44.xml:1469) -> last_px(tag 31)
//   required 'TradeDate' (FIX44.xml:1474) -> trade_date(tag 75)
//   required 'TransactTime' (FIX44.xml:1483) -> transact_time(tag 60)
TEST_F(AllFamiliesRoundtrip069, TradeCaptureReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v44::TradeCaptureReportArgs args{};
    args.trade_report_id = "AE_trade_report_id";
    args.previously_reported = true;
    args.last_qty = make_decimal("10.5", &arena);
    args.last_px = make_decimal("10.5", &arena);
    args.trade_date = "AE_trade_date";
    args.transact_time = "AE_transact_time";
    fixpp::v44::TradeCaptureReportSidesArgs side_entry{};
    side_entry.side = '1';
    side_entry.order_id = "AE_NoSides_OrderID";
    std::array<fixpp::v44::TradeCaptureReportSidesArgs, 1> sides_arr{side_entry};
    args.sides = std::span<const fixpp::v44::TradeCaptureReportSidesArgs>{sides_arr};
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_TradeCaptureReport(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 571, "AE_trade_report_id", "trade_report_id");
    expect_wire_text(mv, 570, "Y", "previously_reported");
    expect_wire_decimal(mv, 32, "10.5", &read_arena, "last_qty");
    expect_wire_decimal(mv, 31, "10.5", &read_arena, "last_px");
    expect_wire_text(mv, 75, "AE_trade_date", "trade_date");
    expect_wire_text(mv, 60, "AE_transact_time", "transact_time");
    // Nested depth (C3): NoSides(552) entry-level readback, not just top-level
    // scalars -- Side(54)/OrderID(37) both required per FIX44.xml:3537-3538.
    auto side_slices = mv.offsets().group_slices(552);
    ASSERT_EQ(side_slices.size(), 1u) << "NoSides(552) must carry exactly 1 entry";
    std::span<const std::byte> const entry0{side_slices[0].data, side_slices[0].len};
    auto side_val = scan_slice_for_tag(entry0, 54);
    ASSERT_TRUE(side_val.has_value()) << "Side(54) not found in NoSides entry";
    EXPECT_EQ(*side_val, "1");
    auto order_id_val = scan_slice_for_tag(entry0, 37);
    ASSERT_TRUE(order_id_val.has_value()) << "OrderID(37) not found in NoSides entry";
    EXPECT_EQ(*order_id_val, "AE_NoSides_OrderID");
}

// OrderMassStatusRequest (AF) -- dictionaries/FIX44.xml:1494-1505
//   required 'MassStatusReqID' (FIX44.xml:1495) -> mass_status_req_id(tag 584)
//   required 'MassStatusReqType' (FIX44.xml:1496) -> mass_status_req_type(tag 585)
TEST_F(AllFamiliesRoundtrip069, OrderMassStatusRequest) {
    fixpp::v44::OrderMassStatusRequestArgs args{};
    args.mass_status_req_id = "AF_mass_status_req_id";
    args.mass_status_req_type = 585;
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_OrderMassStatusRequest(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 584, "AF_mass_status_req_id", "mass_status_req_id");
    expect_wire_text(mv, 585, "585", "mass_status_req_type");
}

// QuoteRequestReject (AG) -- dictionaries/FIX44.xml:1506-1514
//   required 'QuoteReqID' (FIX44.xml:1507) -> quote_req_id(tag 131)
//   required 'QuoteRequestRejectReason' (FIX44.xml:1509) -> quote_request_reject_reason(tag 658)
TEST_F(AllFamiliesRoundtrip069, QuoteRequestReject) {
    fixpp::v44::QuoteRequestRejectArgs args{};
    args.quote_req_id = "AG_quote_req_id";
    args.quote_request_reject_reason = 658;
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_QuoteRequestReject(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 131, "AG_quote_req_id", "quote_req_id");
    expect_wire_text(mv, 658, "658", "quote_request_reject_reason");
}

// RFQRequest (AH) -- dictionaries/FIX44.xml:1515-1519
//   required 'RFQReqID' (FIX44.xml:1516) -> rfq_req_id(tag 644)
//   filler (not required) -> subscription_request_type(tag 263)
TEST_F(AllFamiliesRoundtrip069, RFQRequest) {
    fixpp::v44::RFQRequestArgs args{};
    args.rfq_req_id = "AH_rfq_req_id";
    args.subscription_request_type = '1';
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_RFQRequest(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 644, "AH_rfq_req_id", "rfq_req_id");
    expect_wire_text(mv, 263, "1", "subscription_request_type");
}

// QuoteStatusReport (AI) -- dictionaries/FIX44.xml:1520-1582
//   required 'QuoteID' (FIX44.xml:1523) -> quote_id(tag 117)
//   filler (not required) -> account(tag 1)
TEST_F(AllFamiliesRoundtrip069, QuoteStatusReport) {
    fixpp::v44::QuoteStatusReportArgs args{};
    args.quote_id = "AI_quote_id";
    args.account = "AI_account";
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_QuoteStatusReport(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 117, "AI_quote_id", "quote_id");
    expect_wire_text(mv, 1, "AI_account", "account");
}

// QuoteResponse (AJ) -- dictionaries/FIX44.xml:1583-1645
//   required 'QuoteRespID' (FIX44.xml:1584) -> quote_resp_id(tag 693)
//   required 'QuoteRespType' (FIX44.xml:1586) -> quote_resp_type(tag 694)
TEST_F(AllFamiliesRoundtrip069, QuoteResponse) {
    fixpp::v44::QuoteResponseArgs args{};
    args.quote_resp_id = "AJ_quote_resp_id";
    args.quote_resp_type = 694;
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_QuoteResponse(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 693, "AJ_quote_resp_id", "quote_resp_id");
    expect_wire_text(mv, 694, "694", "quote_resp_type");
}

// Confirmation (AK) -- dictionaries/FIX44.xml:1646-1712
//   required 'ConfirmID' (FIX44.xml:1647) -> confirm_id(tag 664)
//   required 'ConfirmTransType' (FIX44.xml:1650) -> confirm_trans_type(tag 666)
//   required 'ConfirmType' (FIX44.xml:1651) -> confirm_type(tag 773)
//   required 'ConfirmStatus' (FIX44.xml:1654) -> confirm_status(tag 665)
//   required 'TransactTime' (FIX44.xml:1660) -> transact_time(tag 60)
//   required 'TradeDate' (FIX44.xml:1661) -> trade_date(tag 75)
//   required 'AllocQty' (FIX44.xml:1669) -> alloc_qty(tag 80)
//   required 'Side' (FIX44.xml:1671) -> side(tag 54)
//   required 'AllocAccount' (FIX44.xml:1675) -> alloc_account(tag 79)
//   required 'AvgPx' (FIX44.xml:1678) -> avg_px(tag 6)
//   required 'GrossTradeAmt' (FIX44.xml:1688) -> gross_trade_amt(tag 381)
//   required 'NetMoney' (FIX44.xml:1699) -> net_money(tag 118)
TEST_F(AllFamiliesRoundtrip069, Confirmation) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v44::ConfirmationArgs args{};
    args.confirm_id = "AK_confirm_id";
    args.confirm_trans_type = 666;
    args.confirm_type = 773;
    args.confirm_status = 665;
    args.transact_time = "AK_transact_time";
    args.trade_date = "AK_trade_date";
    args.alloc_qty = make_decimal("10.5", &arena);
    args.side = '1';
    args.alloc_account = "AK_alloc_account";
    args.avg_px = make_decimal("10.5", &arena);
    args.gross_trade_amt = make_decimal("10.5", &arena);
    args.net_money = make_decimal("10.5", &arena);
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_Confirmation(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 664, "AK_confirm_id", "confirm_id");
    expect_wire_text(mv, 666, "666", "confirm_trans_type");
    expect_wire_text(mv, 773, "773", "confirm_type");
    expect_wire_text(mv, 665, "665", "confirm_status");
    expect_wire_text(mv, 60, "AK_transact_time", "transact_time");
    expect_wire_text(mv, 75, "AK_trade_date", "trade_date");
    expect_wire_decimal(mv, 80, "10.5", &read_arena, "alloc_qty");
    expect_wire_text(mv, 54, "1", "side");
    expect_wire_text(mv, 79, "AK_alloc_account", "alloc_account");
    expect_wire_decimal(mv, 6, "10.5", &read_arena, "avg_px");
    expect_wire_decimal(mv, 381, "10.5", &read_arena, "gross_trade_amt");
    expect_wire_decimal(mv, 118, "10.5", &read_arena, "net_money");
}

// PositionMaintenanceRequest (AL) -- dictionaries/FIX44.xml:1713-1740
//   required 'PosReqID' (FIX44.xml:1714) -> pos_req_id(tag 710)
//   required 'PosTransType' (FIX44.xml:1715) -> pos_trans_type(tag 709)
//   required 'PosMaintAction' (FIX44.xml:1716) -> pos_maint_action(tag 712)
//   required 'ClearingBusinessDate' (FIX44.xml:1719) -> clearing_business_date(tag 715)
//   required 'Account' (FIX44.xml:1723) -> account(tag 1)
//   required 'AccountType' (FIX44.xml:1725) -> account_type(tag 581)
//   required 'TransactTime' (FIX44.xml:1731) -> transact_time(tag 60)
TEST_F(AllFamiliesRoundtrip069, PositionMaintenanceRequest) {
    fixpp::v44::PositionMaintenanceRequestArgs args{};
    args.pos_req_id = "AL_pos_req_id";
    args.pos_trans_type = 709;
    args.pos_maint_action = 712;
    args.clearing_business_date = "AL_clearing_business_date";
    args.account = "AL_account";
    args.account_type = 581;
    args.transact_time = "AL_transact_time";
    auto mv_opt = parse([&](std::span<std::byte> o) {
        return fixpp::v44::build_PositionMaintenanceRequest(o, args);
    });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 710, "AL_pos_req_id", "pos_req_id");
    expect_wire_text(mv, 709, "709", "pos_trans_type");
    expect_wire_text(mv, 712, "712", "pos_maint_action");
    expect_wire_text(mv, 715, "AL_clearing_business_date", "clearing_business_date");
    expect_wire_text(mv, 1, "AL_account", "account");
    expect_wire_text(mv, 581, "581", "account_type");
    expect_wire_text(mv, 60, "AL_transact_time", "transact_time");
}

// PositionMaintenanceReport (AM) -- dictionaries/FIX44.xml:1741-1769
//   required 'PosMaintRptID' (FIX44.xml:1742) -> pos_maint_rpt_id(tag 721)
//   required 'PosTransType' (FIX44.xml:1743) -> pos_trans_type(tag 709)
//   required 'PosMaintAction' (FIX44.xml:1745) -> pos_maint_action(tag 712)
//   required 'OrigPosReqRefID' (FIX44.xml:1746) -> orig_pos_req_ref_id(tag 713)
//   required 'PosMaintStatus' (FIX44.xml:1747) -> pos_maint_status(tag 722)
//   required 'ClearingBusinessDate' (FIX44.xml:1749) -> clearing_business_date(tag 715)
//   required 'Account' (FIX44.xml:1753) -> account(tag 1)
//   required 'AccountType' (FIX44.xml:1755) -> account_type(tag 581)
//   required 'TransactTime' (FIX44.xml:1761) -> transact_time(tag 60)
TEST_F(AllFamiliesRoundtrip069, PositionMaintenanceReport) {
    fixpp::v44::PositionMaintenanceReportArgs args{};
    args.pos_maint_rpt_id = "AM_pos_maint_rpt_id";
    args.pos_trans_type = 709;
    args.pos_maint_action = 712;
    args.orig_pos_req_ref_id = "AM_orig_pos_req_ref_id";
    args.pos_maint_status = 722;
    args.clearing_business_date = "AM_clearing_business_date";
    args.account = "AM_account";
    args.account_type = 581;
    args.transact_time = "AM_transact_time";
    auto mv_opt = parse([&](std::span<std::byte> o) {
        return fixpp::v44::build_PositionMaintenanceReport(o, args);
    });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 721, "AM_pos_maint_rpt_id", "pos_maint_rpt_id");
    expect_wire_text(mv, 709, "709", "pos_trans_type");
    expect_wire_text(mv, 712, "712", "pos_maint_action");
    expect_wire_text(mv, 713, "AM_orig_pos_req_ref_id", "orig_pos_req_ref_id");
    expect_wire_text(mv, 722, "722", "pos_maint_status");
    expect_wire_text(mv, 715, "AM_clearing_business_date", "clearing_business_date");
    expect_wire_text(mv, 1, "AM_account", "account");
    expect_wire_text(mv, 581, "581", "account_type");
    expect_wire_text(mv, 60, "AM_transact_time", "transact_time");
}

// RequestForPositions (AN) -- dictionaries/FIX44.xml:1770-1793
//   required 'PosReqID' (FIX44.xml:1771) -> pos_req_id(tag 710)
//   required 'PosReqType' (FIX44.xml:1772) -> pos_req_type(tag 724)
//   required 'Account' (FIX44.xml:1776) -> account(tag 1)
//   required 'AccountType' (FIX44.xml:1778) -> account_type(tag 581)
//   required 'ClearingBusinessDate' (FIX44.xml:1783) -> clearing_business_date(tag 715)
//   required 'TransactTime' (FIX44.xml:1787) -> transact_time(tag 60)
TEST_F(AllFamiliesRoundtrip069, RequestForPositions) {
    fixpp::v44::RequestForPositionsArgs args{};
    args.pos_req_id = "AN_pos_req_id";
    args.pos_req_type = 724;
    args.account = "AN_account";
    args.account_type = 581;
    args.clearing_business_date = "AN_clearing_business_date";
    args.transact_time = "AN_transact_time";
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_RequestForPositions(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 710, "AN_pos_req_id", "pos_req_id");
    expect_wire_text(mv, 724, "724", "pos_req_type");
    expect_wire_text(mv, 1, "AN_account", "account");
    expect_wire_text(mv, 581, "581", "account_type");
    expect_wire_text(mv, 715, "AN_clearing_business_date", "clearing_business_date");
    expect_wire_text(mv, 60, "AN_transact_time", "transact_time");
}

// RequestForPositionsAck (AO) -- dictionaries/FIX44.xml:1794-1814
//   required 'PosMaintRptID' (FIX44.xml:1795) -> pos_maint_rpt_id(tag 721)
//   required 'PosReqResult' (FIX44.xml:1799) -> pos_req_result(tag 728)
//   required 'PosReqStatus' (FIX44.xml:1800) -> pos_req_status(tag 729)
//   required 'Account' (FIX44.xml:1802) -> account(tag 1)
//   required 'AccountType' (FIX44.xml:1804) -> account_type(tag 581)
TEST_F(AllFamiliesRoundtrip069, RequestForPositionsAck) {
    fixpp::v44::RequestForPositionsAckArgs args{};
    args.pos_maint_rpt_id = "AO_pos_maint_rpt_id";
    args.pos_req_result = 728;
    args.pos_req_status = 729;
    args.account = "AO_account";
    args.account_type = 581;
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_RequestForPositionsAck(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 721, "AO_pos_maint_rpt_id", "pos_maint_rpt_id");
    expect_wire_text(mv, 728, "728", "pos_req_result");
    expect_wire_text(mv, 729, "729", "pos_req_status");
    expect_wire_text(mv, 1, "AO_account", "account");
    expect_wire_text(mv, 581, "581", "account_type");
}

// PositionReport (AP) -- dictionaries/FIX44.xml:1815-1844
//   required 'PosMaintRptID' (FIX44.xml:1816) -> pos_maint_rpt_id(tag 721)
//   required 'PosReqResult' (FIX44.xml:1822) -> pos_req_result(tag 728)
//   required 'ClearingBusinessDate' (FIX44.xml:1823) -> clearing_business_date(tag 715)
//   required 'Account' (FIX44.xml:1827) -> account(tag 1)
//   required 'AccountType' (FIX44.xml:1829) -> account_type(tag 581)
//   required 'SettlPrice' (FIX44.xml:1832) -> settl_price(tag 730)
//   required 'SettlPriceType' (FIX44.xml:1833) -> settl_price_type(tag 731)
//   required 'PriorSettlPrice' (FIX44.xml:1834) -> prior_settl_price(tag 734)
TEST_F(AllFamiliesRoundtrip069, PositionReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v44::PositionReportArgs args{};
    args.pos_maint_rpt_id = "AP_pos_maint_rpt_id";
    args.pos_req_result = 728;
    args.clearing_business_date = "AP_clearing_business_date";
    args.account = "AP_account";
    args.account_type = 581;
    args.settl_price = make_decimal("10.5", &arena);
    args.settl_price_type = 731;
    args.prior_settl_price = make_decimal("10.5", &arena);
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_PositionReport(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 721, "AP_pos_maint_rpt_id", "pos_maint_rpt_id");
    expect_wire_text(mv, 728, "728", "pos_req_result");
    expect_wire_text(mv, 715, "AP_clearing_business_date", "clearing_business_date");
    expect_wire_text(mv, 1, "AP_account", "account");
    expect_wire_text(mv, 581, "581", "account_type");
    expect_wire_decimal(mv, 730, "10.5", &read_arena, "settl_price");
    expect_wire_text(mv, 731, "731", "settl_price_type");
    expect_wire_decimal(mv, 734, "10.5", &read_arena, "prior_settl_price");
}

// TradeCaptureReportRequestAck (AQ) -- dictionaries/FIX44.xml:1845-1861
//   required 'TradeRequestID' (FIX44.xml:1846) -> trade_request_id(tag 568)
//   required 'TradeRequestType' (FIX44.xml:1847) -> trade_request_type(tag 569)
//   required 'TradeRequestResult' (FIX44.xml:1850) -> trade_request_result(tag 749)
//   required 'TradeRequestStatus' (FIX44.xml:1851) -> trade_request_status(tag 750)
TEST_F(AllFamiliesRoundtrip069, TradeCaptureReportRequestAck) {
    fixpp::v44::TradeCaptureReportRequestAckArgs args{};
    args.trade_request_id = "AQ_trade_request_id";
    args.trade_request_type = 569;
    args.trade_request_result = 749;
    args.trade_request_status = 750;
    auto mv_opt = parse([&](std::span<std::byte> o) {
        return fixpp::v44::build_TradeCaptureReportRequestAck(o, args);
    });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 568, "AQ_trade_request_id", "trade_request_id");
    expect_wire_text(mv, 569, "569", "trade_request_type");
    expect_wire_text(mv, 749, "749", "trade_request_result");
    expect_wire_text(mv, 750, "750", "trade_request_status");
}

// TradeCaptureReportAck (AR) -- dictionaries/FIX44.xml:1862-1900
//   required 'TradeReportID' (FIX44.xml:1863) -> trade_report_id(tag 571)
//   required 'ExecType' (FIX44.xml:1870) -> exec_type(tag 150)
TEST_F(AllFamiliesRoundtrip069, TradeCaptureReportAck) {
    fixpp::v44::TradeCaptureReportAckArgs args{};
    args.trade_report_id = "AR_trade_report_id";
    args.exec_type = '1';
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_TradeCaptureReportAck(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 571, "AR_trade_report_id", "trade_report_id");
    expect_wire_text(mv, 150, "1", "exec_type");
}

// AllocationReport (AS) -- dictionaries/FIX44.xml:1901-1969
//   required 'AllocReportID' (FIX44.xml:1902) -> alloc_report_id(tag 755)
//   required 'AllocTransType' (FIX44.xml:1904) -> alloc_trans_type(tag 71)
//   required 'AllocReportType' (FIX44.xml:1908) -> alloc_report_type(tag 794)
//   required 'AllocStatus' (FIX44.xml:1909) -> alloc_status(tag 87)
//   required 'AllocNoOrdersType' (FIX44.xml:1916) -> alloc_no_orders_type(tag 857)
//   required 'Side' (FIX44.xml:1922) -> side(tag 54)
//   required 'Quantity' (FIX44.xml:1928) -> quantity(tag 53)
//   required 'AvgPx' (FIX44.xml:1935) -> avg_px(tag 6)
//   required 'TradeDate' (FIX44.xml:1941) -> trade_date(tag 75)
TEST_F(AllFamiliesRoundtrip069, AllocationReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v44::AllocationReportArgs args{};
    args.alloc_report_id = "AS_alloc_report_id";
    args.alloc_trans_type = '1';
    args.alloc_report_type = 794;
    args.alloc_status = 87;
    args.alloc_no_orders_type = 857;
    args.side = '1';
    args.quantity = make_decimal("10.5", &arena);
    args.avg_px = make_decimal("10.5", &arena);
    args.trade_date = "AS_trade_date";
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_AllocationReport(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 755, "AS_alloc_report_id", "alloc_report_id");
    expect_wire_text(mv, 71, "1", "alloc_trans_type");
    expect_wire_text(mv, 794, "794", "alloc_report_type");
    expect_wire_text(mv, 87, "87", "alloc_status");
    expect_wire_text(mv, 857, "857", "alloc_no_orders_type");
    expect_wire_text(mv, 54, "1", "side");
    expect_wire_decimal(mv, 53, "10.5", &read_arena, "quantity");
    expect_wire_decimal(mv, 6, "10.5", &read_arena, "avg_px");
    expect_wire_text(mv, 75, "AS_trade_date", "trade_date");
}

// AllocationReportAck (AT) -- dictionaries/FIX44.xml:1970-1988
//   required 'AllocReportID' (FIX44.xml:1971) -> alloc_report_id(tag 755)
//   required 'AllocID' (FIX44.xml:1972) -> alloc_id(tag 70)
//   required 'TransactTime' (FIX44.xml:1976) -> transact_time(tag 60)
//   required 'AllocStatus' (FIX44.xml:1977) -> alloc_status(tag 87)
TEST_F(AllFamiliesRoundtrip069, AllocationReportAck) {
    fixpp::v44::AllocationReportAckArgs args{};
    args.alloc_report_id = "AT_alloc_report_id";
    args.alloc_id = "AT_alloc_id";
    args.transact_time = "AT_transact_time";
    args.alloc_status = 87;
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_AllocationReportAck(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 755, "AT_alloc_report_id", "alloc_report_id");
    expect_wire_text(mv, 70, "AT_alloc_id", "alloc_id");
    expect_wire_text(mv, 60, "AT_transact_time", "transact_time");
    expect_wire_text(mv, 87, "87", "alloc_status");
}

// ConfirmationAck (AU) -- dictionaries/FIX44.xml:1989-1999
//   required 'ConfirmID' (FIX44.xml:1990) -> confirm_id(tag 664)
//   required 'TradeDate' (FIX44.xml:1991) -> trade_date(tag 75)
//   required 'TransactTime' (FIX44.xml:1992) -> transact_time(tag 60)
//   required 'AffirmStatus' (FIX44.xml:1993) -> affirm_status(tag 940)
TEST_F(AllFamiliesRoundtrip069, ConfirmationAck) {
    fixpp::v44::ConfirmationAckArgs args{};
    args.confirm_id = "AU_confirm_id";
    args.trade_date = "AU_trade_date";
    args.transact_time = "AU_transact_time";
    args.affirm_status = 940;
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_ConfirmationAck(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 664, "AU_confirm_id", "confirm_id");
    expect_wire_text(mv, 75, "AU_trade_date", "trade_date");
    expect_wire_text(mv, 60, "AU_transact_time", "transact_time");
    expect_wire_text(mv, 940, "940", "affirm_status");
}

// SettlementInstructionRequest (AV) -- dictionaries/FIX44.xml:2000-2016
//   required 'SettlInstReqID' (FIX44.xml:2001) -> settl_inst_req_id(tag 791)
//   required 'TransactTime' (FIX44.xml:2002) -> transact_time(tag 60)
TEST_F(AllFamiliesRoundtrip069, SettlementInstructionRequest) {
    fixpp::v44::SettlementInstructionRequestArgs args{};
    args.settl_inst_req_id = "AV_settl_inst_req_id";
    args.transact_time = "AV_transact_time";
    auto mv_opt = parse([&](std::span<std::byte> o) {
        return fixpp::v44::build_SettlementInstructionRequest(o, args);
    });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 791, "AV_settl_inst_req_id", "settl_inst_req_id");
    expect_wire_text(mv, 60, "AV_transact_time", "transact_time");
}

// AssignmentReport (AW) -- dictionaries/FIX44.xml:2017-2045
//   required 'AsgnRptID' (FIX44.xml:2018) -> asgn_rpt_id(tag 833)
//   required 'AccountType' (FIX44.xml:2023) -> account_type(tag 581)
//   required 'SettlPrice' (FIX44.xml:2031) -> settl_price(tag 730)
//   required 'SettlPriceType' (FIX44.xml:2032) -> settl_price_type(tag 731)
//   required 'UnderlyingSettlPrice' (FIX44.xml:2033) -> underlying_settl_price(tag 732)
//   required 'AssignmentMethod' (FIX44.xml:2035) -> assignment_method(tag 744)
//   required 'OpenInterest' (FIX44.xml:2037) -> open_interest(tag 746)
//   required 'ExerciseMethod' (FIX44.xml:2038) -> exercise_method(tag 747)
//   required 'SettlSessID' (FIX44.xml:2039) -> settl_sess_id(tag 716)
//   required 'SettlSessSubID' (FIX44.xml:2040) -> settl_sess_sub_id(tag 717)
//   required 'ClearingBusinessDate' (FIX44.xml:2041) -> clearing_business_date(tag 715)
TEST_F(AllFamiliesRoundtrip069, AssignmentReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v44::AssignmentReportArgs args{};
    args.asgn_rpt_id = "AW_asgn_rpt_id";
    args.account_type = 581;
    args.settl_price = make_decimal("10.5", &arena);
    args.settl_price_type = 731;
    args.underlying_settl_price = make_decimal("10.5", &arena);
    args.assignment_method = '1';
    args.open_interest = make_decimal("10.5", &arena);
    args.exercise_method = '1';
    args.settl_sess_id = "AW_settl_sess_id";
    args.settl_sess_sub_id = "AW_settl_sess_sub_id";
    args.clearing_business_date = "AW_clearing_business_date";
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_AssignmentReport(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 833, "AW_asgn_rpt_id", "asgn_rpt_id");
    expect_wire_text(mv, 581, "581", "account_type");
    expect_wire_decimal(mv, 730, "10.5", &read_arena, "settl_price");
    expect_wire_text(mv, 731, "731", "settl_price_type");
    expect_wire_decimal(mv, 732, "10.5", &read_arena, "underlying_settl_price");
    expect_wire_text(mv, 744, "1", "assignment_method");
    expect_wire_decimal(mv, 746, "10.5", &read_arena, "open_interest");
    expect_wire_text(mv, 747, "1", "exercise_method");
    expect_wire_text(mv, 716, "AW_settl_sess_id", "settl_sess_id");
    expect_wire_text(mv, 717, "AW_settl_sess_sub_id", "settl_sess_sub_id");
    expect_wire_text(mv, 715, "AW_clearing_business_date", "clearing_business_date");
}

// CollateralRequest (AX) -- dictionaries/FIX44.xml:2046-2090
//   required 'CollReqID' (FIX44.xml:2047) -> coll_req_id(tag 894)
//   required 'CollAsgnReason' (FIX44.xml:2048) -> coll_asgn_reason(tag 895)
//   required 'TransactTime' (FIX44.xml:2049) -> transact_time(tag 60)
TEST_F(AllFamiliesRoundtrip069, CollateralRequest) {
    fixpp::v44::CollateralRequestArgs args{};
    args.coll_req_id = "AX_coll_req_id";
    args.coll_asgn_reason = 895;
    args.transact_time = "AX_transact_time";
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_CollateralRequest(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 894, "AX_coll_req_id", "coll_req_id");
    expect_wire_text(mv, 895, "895", "coll_asgn_reason");
    expect_wire_text(mv, 60, "AX_transact_time", "transact_time");
}

// CollateralAssignment (AY) -- dictionaries/FIX44.xml:2091-2139
//   required 'CollAsgnID' (FIX44.xml:2092) -> coll_asgn_id(tag 902)
//   required 'CollAsgnReason' (FIX44.xml:2094) -> coll_asgn_reason(tag 895)
//   required 'CollAsgnTransType' (FIX44.xml:2095) -> coll_asgn_trans_type(tag 903)
//   required 'TransactTime' (FIX44.xml:2097) -> transact_time(tag 60)
TEST_F(AllFamiliesRoundtrip069, CollateralAssignment) {
    fixpp::v44::CollateralAssignmentArgs args{};
    args.coll_asgn_id = "AY_coll_asgn_id";
    args.coll_asgn_reason = 895;
    args.coll_asgn_trans_type = 903;
    args.transact_time = "AY_transact_time";
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_CollateralAssignment(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 902, "AY_coll_asgn_id", "coll_asgn_id");
    expect_wire_text(mv, 895, "895", "coll_asgn_reason");
    expect_wire_text(mv, 903, "903", "coll_asgn_trans_type");
    expect_wire_text(mv, 60, "AY_transact_time", "transact_time");
}

// CollateralResponse (AZ) -- dictionaries/FIX44.xml:2140-2183
//   required 'CollRespID' (FIX44.xml:2141) -> coll_resp_id(tag 904)
//   required 'CollAsgnID' (FIX44.xml:2142) -> coll_asgn_id(tag 902)
//   required 'CollAsgnReason' (FIX44.xml:2144) -> coll_asgn_reason(tag 895)
//   required 'CollAsgnRespType' (FIX44.xml:2146) -> coll_asgn_resp_type(tag 905)
//   required 'TransactTime' (FIX44.xml:2148) -> transact_time(tag 60)
TEST_F(AllFamiliesRoundtrip069, CollateralResponse) {
    fixpp::v44::CollateralResponseArgs args{};
    args.coll_resp_id = "AZ_coll_resp_id";
    args.coll_asgn_id = "AZ_coll_asgn_id";
    args.coll_asgn_reason = 895;
    args.coll_asgn_resp_type = 905;
    args.transact_time = "AZ_transact_time";
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_CollateralResponse(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 904, "AZ_coll_resp_id", "coll_resp_id");
    expect_wire_text(mv, 902, "AZ_coll_asgn_id", "coll_asgn_id");
    expect_wire_text(mv, 895, "895", "coll_asgn_reason");
    expect_wire_text(mv, 905, "905", "coll_asgn_resp_type");
    expect_wire_text(mv, 60, "AZ_transact_time", "transact_time");
}

// News (B) -- dictionaries/FIX44.xml:294-308
//   required 'Headline' (FIX44.xml:297) -> headline(tag 148)
//   filler (not required) -> orig_time(tag 42)
TEST_F(AllFamiliesRoundtrip069, News) {
    fixpp::v44::NewsArgs args{};
    args.headline = "B_headline";
    args.orig_time = "B_orig_time";
    auto mv_opt = parse([&](std::span<std::byte> o) { return fixpp::v44::build_News(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 148, "B_headline", "headline");
    expect_wire_text(mv, 42, "B_orig_time", "orig_time");
}

// CollateralReport (BA) -- dictionaries/FIX44.xml:2184-2230
//   required 'CollRptID' (FIX44.xml:2185) -> coll_rpt_id(tag 908)
//   required 'CollStatus' (FIX44.xml:2187) -> coll_status(tag 910)
TEST_F(AllFamiliesRoundtrip069, CollateralReport) {
    fixpp::v44::CollateralReportArgs args{};
    args.coll_rpt_id = "BA_coll_rpt_id";
    args.coll_status = 910;
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_CollateralReport(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 908, "BA_coll_rpt_id", "coll_rpt_id");
    expect_wire_text(mv, 910, "910", "coll_status");
}

// CollateralInquiry (BB) -- dictionaries/FIX44.xml:2231-2276
//   filler (not required) -> account(tag 1)
//   filler (not required) -> cl_ord_id(tag 11)
TEST_F(AllFamiliesRoundtrip069, CollateralInquiry) {
    fixpp::v44::CollateralInquiryArgs args{};
    args.account = "BB_account";
    args.cl_ord_id = "BB_cl_ord_id";
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_CollateralInquiry(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 1, "BB_account", "account");
    expect_wire_text(mv, 11, "BB_cl_ord_id", "cl_ord_id");
}

// NetworkCounterpartySystemStatusRequest (BC) -- dictionaries/FIX44.xml:2277-2281
//   required 'NetworkRequestType' (FIX44.xml:2278) -> network_request_type(tag 935)
//   required 'NetworkRequestID' (FIX44.xml:2279) -> network_request_id(tag 933)
TEST_F(AllFamiliesRoundtrip069, NetworkCounterpartySystemStatusRequest) {
    fixpp::v44::NetworkCounterpartySystemStatusRequestArgs args{};
    args.network_request_type = 935;
    args.network_request_id = "BC_network_request_id";
    auto mv_opt = parse([&](std::span<std::byte> o) {
        return fixpp::v44::build_NetworkCounterpartySystemStatusRequest(o, args);
    });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 935, "935", "network_request_type");
    expect_wire_text(mv, 933, "BC_network_request_id", "network_request_id");
}

// NetworkCounterpartySystemStatusResponse (BD) -- dictionaries/FIX44.xml:2282-2288
//   required 'NetworkStatusResponseType' (FIX44.xml:2283) -> network_status_response_type(tag 937)
//   required 'NetworkResponseID' (FIX44.xml:2285) -> network_response_id(tag 932)
TEST_F(AllFamiliesRoundtrip069, NetworkCounterpartySystemStatusResponse) {
    fixpp::v44::NetworkCounterpartySystemStatusResponseArgs args{};
    args.network_status_response_type = 937;
    args.network_response_id = "BD_network_response_id";
    auto mv_opt = parse([&](std::span<std::byte> o) {
        return fixpp::v44::build_NetworkCounterpartySystemStatusResponse(o, args);
    });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 937, "937", "network_status_response_type");
    expect_wire_text(mv, 932, "BD_network_response_id", "network_response_id");
}

// CollateralInquiryAck (BG) -- dictionaries/FIX44.xml:2304-2337
//   required 'CollInquiryID' (FIX44.xml:2305) -> coll_inquiry_id(tag 909)
//   required 'CollInquiryStatus' (FIX44.xml:2306) -> coll_inquiry_status(tag 945)
TEST_F(AllFamiliesRoundtrip069, CollateralInquiryAck) {
    fixpp::v44::CollateralInquiryAckArgs args{};
    args.coll_inquiry_id = "BG_coll_inquiry_id";
    args.coll_inquiry_status = 945;
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_CollateralInquiryAck(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 909, "BG_coll_inquiry_id", "coll_inquiry_id");
    expect_wire_text(mv, 945, "945", "coll_inquiry_status");
}

// ConfirmationRequest (BH) -- dictionaries/FIX44.xml:2338-2352
//   required 'ConfirmReqID' (FIX44.xml:2339) -> confirm_req_id(tag 859)
//   required 'ConfirmType' (FIX44.xml:2340) -> confirm_type(tag 773)
//   required 'TransactTime' (FIX44.xml:2345) -> transact_time(tag 60)
TEST_F(AllFamiliesRoundtrip069, ConfirmationRequest) {
    fixpp::v44::ConfirmationRequestArgs args{};
    args.confirm_req_id = "BH_confirm_req_id";
    args.confirm_type = 773;
    args.transact_time = "BH_transact_time";
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_ConfirmationRequest(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 859, "BH_confirm_req_id", "confirm_req_id");
    expect_wire_text(mv, 773, "773", "confirm_type");
    expect_wire_text(mv, 60, "BH_transact_time", "transact_time");
}

// Email (C) -- dictionaries/FIX44.xml:309-325
//   required 'EmailThreadID' (FIX44.xml:310) -> email_thread_id(tag 164)
//   required 'EmailType' (FIX44.xml:311) -> email_type(tag 94)
//   required 'Subject' (FIX44.xml:313) -> subject(tag 147)
TEST_F(AllFamiliesRoundtrip069, Email) {
    fixpp::v44::EmailArgs args{};
    args.email_thread_id = "C_email_thread_id";
    args.email_type = '1';
    args.subject = "C_subject";
    auto mv_opt = parse([&](std::span<std::byte> o) { return fixpp::v44::build_Email(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 164, "C_email_thread_id", "email_thread_id");
    expect_wire_text(mv, 94, "1", "email_type");
    expect_wire_text(mv, 147, "C_subject", "subject");
}

// NewOrderSingle (D) -- dictionaries/FIX44.xml:326-403
//   required 'ClOrdID' (FIX44.xml:327) -> cl_ord_id(tag 11)
//   required 'Side' (FIX44.xml:356) -> side(tag 54)
//   required 'TransactTime' (FIX44.xml:358) -> transact_time(tag 60)
//   required 'OrdType' (FIX44.xml:362) -> ord_type(tag 40)
TEST_F(AllFamiliesRoundtrip069, NewOrderSingle) {
    fixpp::v44::NewOrderSingleArgs args{};
    args.cl_ord_id = "D_cl_ord_id";
    args.side = '1';
    args.transact_time = "D_transact_time";
    args.ord_type = '1';
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_NewOrderSingle(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 11, "D_cl_ord_id", "cl_ord_id");
    expect_wire_text(mv, 54, "1", "side");
    expect_wire_text(mv, 60, "D_transact_time", "transact_time");
    expect_wire_text(mv, 40, "1", "ord_type");
}

// NewOrderList (E) -- dictionaries/FIX44.xml:404-424
//   required 'ListID' (FIX44.xml:405) -> list_id(tag 66)
//   required 'BidType' (FIX44.xml:409) -> bid_type(tag 394)
//   required 'TotNoOrders' (FIX44.xml:421) -> tot_no_orders(tag 68)
TEST_F(AllFamiliesRoundtrip069, NewOrderList) {
    fixpp::v44::NewOrderListArgs args{};
    args.list_id = "E_list_id";
    args.bid_type = 394;
    args.tot_no_orders = 68;
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_NewOrderList(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 66, "E_list_id", "list_id");
    expect_wire_text(mv, 394, "394", "bid_type");
    expect_wire_text(mv, 68, "68", "tot_no_orders");
}

// OrderCancelRequest (F) -- dictionaries/FIX44.xml:425-447
//   required 'OrigClOrdID' (FIX44.xml:426) -> orig_cl_ord_id(tag 41)
//   required 'ClOrdID' (FIX44.xml:428) -> cl_ord_id(tag 11)
//   required 'Side' (FIX44.xml:440) -> side(tag 54)
//   required 'TransactTime' (FIX44.xml:441) -> transact_time(tag 60)
TEST_F(AllFamiliesRoundtrip069, OrderCancelRequest) {
    fixpp::v44::OrderCancelRequestArgs args{};
    args.orig_cl_ord_id = "F_orig_cl_ord_id";
    args.cl_ord_id = "F_cl_ord_id";
    args.side = '1';
    args.transact_time = "F_transact_time";
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_OrderCancelRequest(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 41, "F_orig_cl_ord_id", "orig_cl_ord_id");
    expect_wire_text(mv, 11, "F_cl_ord_id", "cl_ord_id");
    expect_wire_text(mv, 54, "1", "side");
    expect_wire_text(mv, 60, "F_transact_time", "transact_time");
}

// OrderCancelReplaceRequest (G) -- dictionaries/FIX44.xml:448-524
//   required 'OrigClOrdID' (FIX44.xml:453) -> orig_cl_ord_id(tag 41)
//   required 'ClOrdID' (FIX44.xml:454) -> cl_ord_id(tag 11)
//   required 'Side' (FIX44.xml:480) -> side(tag 54)
//   required 'TransactTime' (FIX44.xml:481) -> transact_time(tag 60)
//   required 'OrdType' (FIX44.xml:484) -> ord_type(tag 40)
TEST_F(AllFamiliesRoundtrip069, OrderCancelReplaceRequest) {
    fixpp::v44::OrderCancelReplaceRequestArgs args{};
    args.orig_cl_ord_id = "G_orig_cl_ord_id";
    args.cl_ord_id = "G_cl_ord_id";
    args.side = '1';
    args.transact_time = "G_transact_time";
    args.ord_type = '1';
    auto mv_opt = parse([&](std::span<std::byte> o) {
        return fixpp::v44::build_OrderCancelReplaceRequest(o, args);
    });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 41, "G_orig_cl_ord_id", "orig_cl_ord_id");
    expect_wire_text(mv, 11, "G_cl_ord_id", "cl_ord_id");
    expect_wire_text(mv, 54, "1", "side");
    expect_wire_text(mv, 60, "G_transact_time", "transact_time");
    expect_wire_text(mv, 40, "1", "ord_type");
}

// OrderStatusRequest (H) -- dictionaries/FIX44.xml:525-538
//   required 'ClOrdID' (FIX44.xml:527) -> cl_ord_id(tag 11)
//   required 'Side' (FIX44.xml:537) -> side(tag 54)
TEST_F(AllFamiliesRoundtrip069, OrderStatusRequest) {
    fixpp::v44::OrderStatusRequestArgs args{};
    args.cl_ord_id = "H_cl_ord_id";
    args.side = '1';
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_OrderStatusRequest(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 11, "H_cl_ord_id", "cl_ord_id");
    expect_wire_text(mv, 54, "1", "side");
}

// AllocationInstruction (J) -- dictionaries/FIX44.xml:539-603
//   required 'AllocID' (FIX44.xml:540) -> alloc_id(tag 70)
//   required 'AllocTransType' (FIX44.xml:541) -> alloc_trans_type(tag 71)
//   required 'AllocType' (FIX44.xml:542) -> alloc_type(tag 626)
//   required 'AllocNoOrdersType' (FIX44.xml:550) -> alloc_no_orders_type(tag 857)
//   required 'Side' (FIX44.xml:556) -> side(tag 54)
//   required 'Quantity' (FIX44.xml:562) -> quantity(tag 53)
//   required 'AvgPx' (FIX44.xml:569) -> avg_px(tag 6)
//   required 'TradeDate' (FIX44.xml:575) -> trade_date(tag 75)
TEST_F(AllFamiliesRoundtrip069, AllocationInstruction) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v44::AllocationInstructionArgs args{};
    args.alloc_id = "J_alloc_id";
    args.alloc_trans_type = '1';
    args.alloc_type = 626;
    args.alloc_no_orders_type = 857;
    args.side = '1';
    args.quantity = make_decimal("10.5", &arena);
    args.avg_px = make_decimal("10.5", &arena);
    args.trade_date = "J_trade_date";
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_AllocationInstruction(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 70, "J_alloc_id", "alloc_id");
    expect_wire_text(mv, 71, "1", "alloc_trans_type");
    expect_wire_text(mv, 626, "626", "alloc_type");
    expect_wire_text(mv, 857, "857", "alloc_no_orders_type");
    expect_wire_text(mv, 54, "1", "side");
    expect_wire_decimal(mv, 53, "10.5", &read_arena, "quantity");
    expect_wire_decimal(mv, 6, "10.5", &read_arena, "avg_px");
    expect_wire_text(mv, 75, "J_trade_date", "trade_date");
}

// ListCancelRequest (K) -- dictionaries/FIX44.xml:604-612
//   required 'ListID' (FIX44.xml:605) -> list_id(tag 66)
//   required 'TransactTime' (FIX44.xml:606) -> transact_time(tag 60)
TEST_F(AllFamiliesRoundtrip069, ListCancelRequest) {
    fixpp::v44::ListCancelRequestArgs args{};
    args.list_id = "K_list_id";
    args.transact_time = "K_transact_time";
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_ListCancelRequest(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 66, "K_list_id", "list_id");
    expect_wire_text(mv, 60, "K_transact_time", "transact_time");
}

// ListExecute (L) -- dictionaries/FIX44.xml:613-621
//   required 'ListID' (FIX44.xml:614) -> list_id(tag 66)
//   required 'TransactTime' (FIX44.xml:617) -> transact_time(tag 60)
TEST_F(AllFamiliesRoundtrip069, ListExecute) {
    fixpp::v44::ListExecuteArgs args{};
    args.list_id = "L_list_id";
    args.transact_time = "L_transact_time";
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_ListExecute(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 66, "L_list_id", "list_id");
    expect_wire_text(mv, 60, "L_transact_time", "transact_time");
}

// ListStatusRequest (M) -- dictionaries/FIX44.xml:622-627
//   required 'ListID' (FIX44.xml:623) -> list_id(tag 66)
//   filler (not required) -> text(tag 58)
TEST_F(AllFamiliesRoundtrip069, ListStatusRequest) {
    fixpp::v44::ListStatusRequestArgs args{};
    args.list_id = "M_list_id";
    args.text = "M_text";
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_ListStatusRequest(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 66, "M_list_id", "list_id");
    expect_wire_text(mv, 58, "M_text", "text");
}

// ListStatus (N) -- dictionaries/FIX44.xml:628-641
//   required 'ListID' (FIX44.xml:629) -> list_id(tag 66)
//   required 'ListStatusType' (FIX44.xml:630) -> list_status_type(tag 429)
//   required 'NoRpts' (FIX44.xml:631) -> no_rpts(tag 82)
//   required 'ListOrderStatus' (FIX44.xml:632) -> list_order_status(tag 431)
//   required 'RptSeq' (FIX44.xml:633) -> rpt_seq(tag 83)
//   required 'TotNoOrders' (FIX44.xml:638) -> tot_no_orders(tag 68)
TEST_F(AllFamiliesRoundtrip069, ListStatus) {
    fixpp::v44::ListStatusArgs args{};
    args.list_id = "N_list_id";
    args.list_status_type = 429;
    args.no_rpts = 82;
    args.list_order_status = 431;
    args.rpt_seq = 83;
    args.tot_no_orders = 68;
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_ListStatus(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 66, "N_list_id", "list_id");
    expect_wire_text(mv, 429, "429", "list_status_type");
    expect_wire_text(mv, 82, "82", "no_rpts");
    expect_wire_text(mv, 431, "431", "list_order_status");
    expect_wire_text(mv, 83, "83", "rpt_seq");
    expect_wire_text(mv, 68, "68", "tot_no_orders");
}

// AllocationInstructionAck (P) -- dictionaries/FIX44.xml:642-659
//   required 'AllocID' (FIX44.xml:643) -> alloc_id(tag 70)
//   required 'TransactTime' (FIX44.xml:647) -> transact_time(tag 60)
//   required 'AllocStatus' (FIX44.xml:648) -> alloc_status(tag 87)
TEST_F(AllFamiliesRoundtrip069, AllocationInstructionAck) {
    fixpp::v44::AllocationInstructionAckArgs args{};
    args.alloc_id = "P_alloc_id";
    args.transact_time = "P_transact_time";
    args.alloc_status = 87;
    auto mv_opt = parse([&](std::span<std::byte> o) {
        return fixpp::v44::build_AllocationInstructionAck(o, args);
    });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 70, "P_alloc_id", "alloc_id");
    expect_wire_text(mv, 60, "P_transact_time", "transact_time");
    expect_wire_text(mv, 87, "87", "alloc_status");
}

// DontKnowTrade (Q) -- dictionaries/FIX44.xml:660-675
//   required 'OrderID' (FIX44.xml:661) -> order_id(tag 37)
//   required 'ExecID' (FIX44.xml:663) -> exec_id(tag 17)
//   required 'DKReason' (FIX44.xml:664) -> dk_reason(tag 127)
//   required 'Side' (FIX44.xml:668) -> side(tag 54)
TEST_F(AllFamiliesRoundtrip069, DontKnowTrade) {
    fixpp::v44::DontKnowTradeArgs args{};
    args.order_id = "Q_order_id";
    args.exec_id = "Q_exec_id";
    args.dk_reason = '1';
    args.side = '1';
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_DontKnowTrade(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 37, "Q_order_id", "order_id");
    expect_wire_text(mv, 17, "Q_exec_id", "exec_id");
    expect_wire_text(mv, 127, "1", "dk_reason");
    expect_wire_text(mv, 54, "1", "side");
}

// QuoteRequest (R) -- dictionaries/FIX44.xml:676-685
//   required 'QuoteReqID' (FIX44.xml:677) -> quote_req_id(tag 131)
//   filler (not required) -> cl_ord_id(tag 11)
TEST_F(AllFamiliesRoundtrip069, QuoteRequest) {
    fixpp::v44::QuoteRequestArgs args{};
    args.quote_req_id = "R_quote_req_id";
    args.cl_ord_id = "R_cl_ord_id";
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_QuoteRequest(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 131, "R_quote_req_id", "quote_req_id");
    expect_wire_text(mv, 11, "R_cl_ord_id", "cl_ord_id");
}

// Quote (S) -- dictionaries/FIX44.xml:686-746
//   required 'QuoteID' (FIX44.xml:688) -> quote_id(tag 117)
//   filler (not required) -> account(tag 1)
TEST_F(AllFamiliesRoundtrip069, Quote) {
    fixpp::v44::QuoteArgs args{};
    args.quote_id = "S_quote_id";
    args.account = "S_account";
    auto mv_opt = parse([&](std::span<std::byte> o) { return fixpp::v44::build_Quote(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 117, "S_quote_id", "quote_id");
    expect_wire_text(mv, 1, "S_account", "account");
}

// SettlementInstructions (T) -- dictionaries/FIX44.xml:747-758
//   required 'SettlInstMsgID' (FIX44.xml:748) -> settl_inst_msg_id(tag 777)
//   required 'SettlInstMode' (FIX44.xml:750) -> settl_inst_mode(tag 160)
//   required 'TransactTime' (FIX44.xml:756) -> transact_time(tag 60)
TEST_F(AllFamiliesRoundtrip069, SettlementInstructions) {
    fixpp::v44::SettlementInstructionsArgs args{};
    args.settl_inst_msg_id = "T_settl_inst_msg_id";
    args.settl_inst_mode = '1';
    args.transact_time = "T_transact_time";
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_SettlementInstructions(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 777, "T_settl_inst_msg_id", "settl_inst_msg_id");
    expect_wire_text(mv, 160, "1", "settl_inst_mode");
    expect_wire_text(mv, 60, "T_transact_time", "transact_time");
}

// MarketDataRequest (V) -- dictionaries/FIX44.xml:759-773
//   required 'MDReqID' (FIX44.xml:760) -> md_req_id(tag 262)
//   required 'SubscriptionRequestType' (FIX44.xml:761) -> subscription_request_type(tag 263)
//   required 'MarketDepth' (FIX44.xml:762) -> market_depth(tag 264)
TEST_F(AllFamiliesRoundtrip069, MarketDataRequest) {
    fixpp::v44::MarketDataRequestArgs args{};
    args.md_req_id = "V_md_req_id";
    args.subscription_request_type = '1';
    args.market_depth = 264;
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_MarketDataRequest(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 262, "V_md_req_id", "md_req_id");
    expect_wire_text(mv, 263, "1", "subscription_request_type");
    expect_wire_text(mv, 264, "264", "market_depth");
}

// MarketDataSnapshotFullRefresh (W) -- dictionaries/FIX44.xml:774-785
//   filler (not required) -> security_id_source(tag 22)
//   filler (not required) -> security_id(tag 48)
TEST_F(AllFamiliesRoundtrip069, MarketDataSnapshotFullRefresh) {
    fixpp::v44::MarketDataSnapshotFullRefreshArgs args{};
    args.security_id_source = "W_security_id_source";
    args.security_id = "W_security_id";
    auto mv_opt = parse([&](std::span<std::byte> o) {
        return fixpp::v44::build_MarketDataSnapshotFullRefresh(o, args);
    });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 22, "W_security_id_source", "security_id_source");
    expect_wire_text(mv, 48, "W_security_id", "security_id");
}

// MarketDataIncrementalRefresh (X) -- dictionaries/FIX44.xml:786-791
//   filler (not required) -> md_req_id(tag 262)
//   filler (not required) -> appl_queue_depth(tag 813)
TEST_F(AllFamiliesRoundtrip069, MarketDataIncrementalRefresh) {
    fixpp::v44::MarketDataIncrementalRefreshArgs args{};
    args.md_req_id = "X_md_req_id";
    args.appl_queue_depth = 813;
    auto mv_opt = parse([&](std::span<std::byte> o) {
        return fixpp::v44::build_MarketDataIncrementalRefresh(o, args);
    });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 262, "X_md_req_id", "md_req_id");
    expect_wire_text(mv, 813, "813", "appl_queue_depth");
}

// MarketDataRequestReject (Y) -- dictionaries/FIX44.xml:792-799
//   required 'MDReqID' (FIX44.xml:793) -> md_req_id(tag 262)
//   filler (not required) -> text(tag 58)
TEST_F(AllFamiliesRoundtrip069, MarketDataRequestReject) {
    fixpp::v44::MarketDataRequestRejectArgs args{};
    args.md_req_id = "Y_md_req_id";
    args.text = "Y_text";
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_MarketDataRequestReject(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 262, "Y_md_req_id", "md_req_id");
    expect_wire_text(mv, 58, "Y_text", "text");
}

// QuoteCancel (Z) -- dictionaries/FIX44.xml:800-812
//   required 'QuoteID' (FIX44.xml:802) -> quote_id(tag 117)
//   required 'QuoteCancelType' (FIX44.xml:803) -> quote_cancel_type(tag 298)
TEST_F(AllFamiliesRoundtrip069, QuoteCancel) {
    fixpp::v44::QuoteCancelArgs args{};
    args.quote_id = "Z_quote_id";
    args.quote_cancel_type = 298;
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_QuoteCancel(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 117, "Z_quote_id", "quote_id");
    expect_wire_text(mv, 298, "298", "quote_cancel_type");
}

// QuoteStatusRequest (a) -- dictionaries/FIX44.xml:813-827
//   filler (not required) -> account(tag 1)
//   filler (not required) -> security_id_source(tag 22)
TEST_F(AllFamiliesRoundtrip069, QuoteStatusRequest) {
    fixpp::v44::QuoteStatusRequestArgs args{};
    args.account = "a_account";
    args.security_id_source = "a_security_id_source";
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_QuoteStatusRequest(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 1, "a_account", "account");
    expect_wire_text(mv, 22, "a_security_id_source", "security_id_source");
}

// MassQuoteAcknowledgement (b) -- dictionaries/FIX44.xml:828-843
//   required 'QuoteStatus' (FIX44.xml:831) -> quote_status(tag 297)
//   filler (not required) -> account(tag 1)
TEST_F(AllFamiliesRoundtrip069, MassQuoteAcknowledgement) {
    fixpp::v44::MassQuoteAcknowledgementArgs args{};
    args.quote_status = 297;
    args.account = "b_account";
    auto mv_opt = parse([&](std::span<std::byte> o) {
        return fixpp::v44::build_MassQuoteAcknowledgement(o, args);
    });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 297, "297", "quote_status");
    expect_wire_text(mv, 1, "b_account", "account");
}

// SecurityDefinitionRequest (c) -- dictionaries/FIX44.xml:844-859
//   required 'SecurityReqID' (FIX44.xml:845) -> security_req_id(tag 320)
//   required 'SecurityRequestType' (FIX44.xml:846) -> security_request_type(tag 321)
TEST_F(AllFamiliesRoundtrip069, SecurityDefinitionRequest) {
    fixpp::v44::SecurityDefinitionRequestArgs args{};
    args.security_req_id = "c_security_req_id";
    args.security_request_type = 321;
    auto mv_opt = parse([&](std::span<std::byte> o) {
        return fixpp::v44::build_SecurityDefinitionRequest(o, args);
    });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 320, "c_security_req_id", "security_req_id");
    expect_wire_text(mv, 321, "321", "security_request_type");
}

// SecurityDefinition (d) -- dictionaries/FIX44.xml:860-877
//   required 'SecurityReqID' (FIX44.xml:861) -> security_req_id(tag 320)
//   required 'SecurityResponseID' (FIX44.xml:862) -> security_response_id(tag 322)
//   required 'SecurityResponseType' (FIX44.xml:863) -> security_response_type(tag 323)
TEST_F(AllFamiliesRoundtrip069, SecurityDefinition) {
    fixpp::v44::SecurityDefinitionArgs args{};
    args.security_req_id = "d_security_req_id";
    args.security_response_id = "d_security_response_id";
    args.security_response_type = 323;
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_SecurityDefinition(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 320, "d_security_req_id", "security_req_id");
    expect_wire_text(mv, 322, "d_security_response_id", "security_response_id");
    expect_wire_text(mv, 323, "323", "security_response_type");
}

// SecurityStatusRequest (e) -- dictionaries/FIX44.xml:878-888
//   required 'SecurityStatusReqID' (FIX44.xml:879) -> security_status_req_id(tag 324)
//   required 'SubscriptionRequestType' (FIX44.xml:885) -> subscription_request_type(tag 263)
TEST_F(AllFamiliesRoundtrip069, SecurityStatusRequest) {
    fixpp::v44::SecurityStatusRequestArgs args{};
    args.security_status_req_id = "e_security_status_req_id";
    args.subscription_request_type = '1';
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_SecurityStatusRequest(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 324, "e_security_status_req_id", "security_status_req_id");
    expect_wire_text(mv, 263, "1", "subscription_request_type");
}

// SecurityStatus (f) -- dictionaries/FIX44.xml:889-915
//   filler (not required) -> currency(tag 15)
//   filler (not required) -> security_id_source(tag 22)
TEST_F(AllFamiliesRoundtrip069, SecurityStatus) {
    fixpp::v44::SecurityStatusArgs args{};
    args.currency = "f_currency";
    args.security_id_source = "f_security_id_source";
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_SecurityStatus(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 15, "f_currency", "currency");
    expect_wire_text(mv, 22, "f_security_id_source", "security_id_source");
}

// TradingSessionStatusRequest (g) -- dictionaries/FIX44.xml:916-923
//   required 'TradSesReqID' (FIX44.xml:917) -> trad_ses_req_id(tag 335)
//   required 'SubscriptionRequestType' (FIX44.xml:922) -> subscription_request_type(tag 263)
TEST_F(AllFamiliesRoundtrip069, TradingSessionStatusRequest) {
    fixpp::v44::TradingSessionStatusRequestArgs args{};
    args.trad_ses_req_id = "g_trad_ses_req_id";
    args.subscription_request_type = '1';
    auto mv_opt = parse([&](std::span<std::byte> o) {
        return fixpp::v44::build_TradingSessionStatusRequest(o, args);
    });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 335, "g_trad_ses_req_id", "trad_ses_req_id");
    expect_wire_text(mv, 263, "1", "subscription_request_type");
}

// TradingSessionStatus (h) -- dictionaries/FIX44.xml:924-942
//   required 'TradingSessionID' (FIX44.xml:926) -> trading_session_id(tag 336)
//   required 'TradSesStatus' (FIX44.xml:931) -> trad_ses_status(tag 340)
TEST_F(AllFamiliesRoundtrip069, TradingSessionStatus) {
    fixpp::v44::TradingSessionStatusArgs args{};
    args.trading_session_id = "h_trading_session_id";
    args.trad_ses_status = 340;
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_TradingSessionStatus(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 336, "h_trading_session_id", "trading_session_id");
    expect_wire_text(mv, 340, "340", "trad_ses_status");
}

// MassQuote (i) -- dictionaries/FIX44.xml:943-955
//   required 'QuoteID' (FIX44.xml:945) -> quote_id(tag 117)
//   filler (not required) -> account(tag 1)
TEST_F(AllFamiliesRoundtrip069, MassQuote) {
    fixpp::v44::MassQuoteArgs args{};
    args.quote_id = "i_quote_id";
    args.account = "i_account";
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_MassQuote(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 117, "i_quote_id", "quote_id");
    expect_wire_text(mv, 1, "i_account", "account");
}

// BusinessMessageReject (j) -- dictionaries/FIX44.xml:956-964
//   required 'RefMsgType' (FIX44.xml:958) -> ref_msg_type(tag 372)
//   required 'BusinessRejectReason' (FIX44.xml:960) -> business_reject_reason(tag 380)
TEST_F(AllFamiliesRoundtrip069, BusinessMessageReject) {
    fixpp::v44::BusinessMessageRejectArgs args{};
    args.ref_msg_type = "j_ref_msg_type";
    args.business_reject_reason = 380;
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_BusinessMessageReject(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 372, "j_ref_msg_type", "ref_msg_type");
    expect_wire_text(mv, 380, "380", "business_reject_reason");
}

// BidRequest (k) -- dictionaries/FIX44.xml:965-995
//   required 'ClientBidID' (FIX44.xml:967) -> client_bid_id(tag 391)
//   required 'BidRequestTransType' (FIX44.xml:968) -> bid_request_trans_type(tag 374)
//   required 'TotNoRelatedSym' (FIX44.xml:970) -> tot_no_related_sym(tag 393)
//   required 'BidType' (FIX44.xml:971) -> bid_type(tag 394)
//   required 'BidTradeType' (FIX44.xml:989) -> bid_trade_type(tag 418)
//   required 'BasisPxType' (FIX44.xml:990) -> basis_px_type(tag 419)
TEST_F(AllFamiliesRoundtrip069, BidRequest) {
    fixpp::v44::BidRequestArgs args{};
    args.client_bid_id = "k_client_bid_id";
    args.bid_request_trans_type = '1';
    args.tot_no_related_sym = 393;
    args.bid_type = 394;
    args.bid_trade_type = '1';
    args.basis_px_type = '1';
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_BidRequest(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 391, "k_client_bid_id", "client_bid_id");
    expect_wire_text(mv, 374, "1", "bid_request_trans_type");
    expect_wire_text(mv, 393, "393", "tot_no_related_sym");
    expect_wire_text(mv, 394, "394", "bid_type");
    expect_wire_text(mv, 418, "1", "bid_trade_type");
    expect_wire_text(mv, 419, "1", "basis_px_type");
}

// BidResponse (l) -- dictionaries/FIX44.xml:996-1000
//   filler (not required) -> bid_id(tag 390)
//   filler (not required) -> client_bid_id(tag 391)
TEST_F(AllFamiliesRoundtrip069, BidResponse) {
    fixpp::v44::BidResponseArgs args{};
    args.bid_id = "l_bid_id";
    args.client_bid_id = "l_client_bid_id";
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_BidResponse(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 390, "l_bid_id", "bid_id");
    expect_wire_text(mv, 391, "l_client_bid_id", "client_bid_id");
}

// ListStrikePrice (m) -- dictionaries/FIX44.xml:1001-1007
//   required 'ListID' (FIX44.xml:1002) -> list_id(tag 66)
//   required 'TotNoStrikes' (FIX44.xml:1003) -> tot_no_strikes(tag 422)
TEST_F(AllFamiliesRoundtrip069, ListStrikePrice) {
    fixpp::v44::ListStrikePriceArgs args{};
    args.list_id = "m_list_id";
    args.tot_no_strikes = 422;
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_ListStrikePrice(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 66, "m_list_id", "list_id");
    expect_wire_text(mv, 422, "422", "tot_no_strikes");
}

// RegistrationInstructions (o) -- dictionaries/FIX44.xml:1009-1022
//   required 'RegistID' (FIX44.xml:1010) -> regist_id(tag 513)
//   required 'RegistTransType' (FIX44.xml:1011) -> regist_trans_type(tag 514)
//   required 'RegistRefID' (FIX44.xml:1012) -> regist_ref_id(tag 508)
TEST_F(AllFamiliesRoundtrip069, RegistrationInstructions) {
    fixpp::v44::RegistrationInstructionsArgs args{};
    args.regist_id = "o_regist_id";
    args.regist_trans_type = '1';
    args.regist_ref_id = "o_regist_ref_id";
    auto mv_opt = parse([&](std::span<std::byte> o) {
        return fixpp::v44::build_RegistrationInstructions(o, args);
    });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 513, "o_regist_id", "regist_id");
    expect_wire_text(mv, 514, "1", "regist_trans_type");
    expect_wire_text(mv, 508, "o_regist_ref_id", "regist_ref_id");
}

// RegistrationInstructionsResponse (p) -- dictionaries/FIX44.xml:1023-1034
//   required 'RegistID' (FIX44.xml:1024) -> regist_id(tag 513)
//   required 'RegistTransType' (FIX44.xml:1025) -> regist_trans_type(tag 514)
//   required 'RegistRefID' (FIX44.xml:1026) -> regist_ref_id(tag 508)
//   required 'RegistStatus' (FIX44.xml:1031) -> regist_status(tag 506)
TEST_F(AllFamiliesRoundtrip069, RegistrationInstructionsResponse) {
    fixpp::v44::RegistrationInstructionsResponseArgs args{};
    args.regist_id = "p_regist_id";
    args.regist_trans_type = '1';
    args.regist_ref_id = "p_regist_ref_id";
    args.regist_status = '1';
    auto mv_opt = parse([&](std::span<std::byte> o) {
        return fixpp::v44::build_RegistrationInstructionsResponse(o, args);
    });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 513, "p_regist_id", "regist_id");
    expect_wire_text(mv, 514, "1", "regist_trans_type");
    expect_wire_text(mv, 508, "p_regist_ref_id", "regist_ref_id");
    expect_wire_text(mv, 506, "1", "regist_status");
}

// OrderMassCancelRequest (q) -- dictionaries/FIX44.xml:1035-1048
//   required 'ClOrdID' (FIX44.xml:1036) -> cl_ord_id(tag 11)
//   required 'MassCancelRequestType' (FIX44.xml:1038) -> mass_cancel_request_type(tag 530)
//   required 'TransactTime' (FIX44.xml:1044) -> transact_time(tag 60)
TEST_F(AllFamiliesRoundtrip069, OrderMassCancelRequest) {
    fixpp::v44::OrderMassCancelRequestArgs args{};
    args.cl_ord_id = "q_cl_ord_id";
    args.mass_cancel_request_type = '1';
    args.transact_time = "q_transact_time";
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_OrderMassCancelRequest(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 11, "q_cl_ord_id", "cl_ord_id");
    expect_wire_text(mv, 530, "1", "mass_cancel_request_type");
    expect_wire_text(mv, 60, "q_transact_time", "transact_time");
}

// OrderMassCancelReport (r) -- dictionaries/FIX44.xml:1049-1068
//   required 'OrderID' (FIX44.xml:1052) -> order_id(tag 37)
//   required 'MassCancelRequestType' (FIX44.xml:1054) -> mass_cancel_request_type(tag 530)
//   required 'MassCancelResponse' (FIX44.xml:1055) -> mass_cancel_response(tag 531)
TEST_F(AllFamiliesRoundtrip069, OrderMassCancelReport) {
    fixpp::v44::OrderMassCancelReportArgs args{};
    args.order_id = "r_order_id";
    args.mass_cancel_request_type = '1';
    args.mass_cancel_response = '1';
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_OrderMassCancelReport(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 37, "r_order_id", "order_id");
    expect_wire_text(mv, 530, "1", "mass_cancel_request_type");
    expect_wire_text(mv, 531, "1", "mass_cancel_response");
}

// NewOrderCross (s) -- dictionaries/FIX44.xml:1069-1115
//   required 'CrossID' (FIX44.xml:1070) -> cross_id(tag 548)
//   required 'CrossType' (FIX44.xml:1071) -> cross_type(tag 549)
//   required 'CrossPrioritization' (FIX44.xml:1072) -> cross_prioritization(tag 550)
//   required 'TransactTime' (FIX44.xml:1088) -> transact_time(tag 60)
//   required 'OrdType' (FIX44.xml:1090) -> ord_type(tag 40)
TEST_F(AllFamiliesRoundtrip069, NewOrderCross) {
    fixpp::v44::NewOrderCrossArgs args{};
    args.cross_id = "s_cross_id";
    args.cross_type = 549;
    args.cross_prioritization = 550;
    args.transact_time = "s_transact_time";
    args.ord_type = '1';
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_NewOrderCross(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 548, "s_cross_id", "cross_id");
    expect_wire_text(mv, 549, "549", "cross_type");
    expect_wire_text(mv, 550, "550", "cross_prioritization");
    expect_wire_text(mv, 60, "s_transact_time", "transact_time");
    expect_wire_text(mv, 40, "1", "ord_type");
}

// CrossOrderCancelReplaceRequest (t) -- dictionaries/FIX44.xml:1116-1164
//   required 'CrossID' (FIX44.xml:1118) -> cross_id(tag 548)
//   required 'OrigCrossID' (FIX44.xml:1119) -> orig_cross_id(tag 551)
//   required 'CrossType' (FIX44.xml:1120) -> cross_type(tag 549)
//   required 'CrossPrioritization' (FIX44.xml:1121) -> cross_prioritization(tag 550)
//   required 'TransactTime' (FIX44.xml:1137) -> transact_time(tag 60)
//   required 'OrdType' (FIX44.xml:1139) -> ord_type(tag 40)
TEST_F(AllFamiliesRoundtrip069, CrossOrderCancelReplaceRequest) {
    fixpp::v44::CrossOrderCancelReplaceRequestArgs args{};
    args.cross_id = "t_cross_id";
    args.orig_cross_id = "t_orig_cross_id";
    args.cross_type = 549;
    args.cross_prioritization = 550;
    args.transact_time = "t_transact_time";
    args.ord_type = '1';
    auto mv_opt = parse([&](std::span<std::byte> o) {
        return fixpp::v44::build_CrossOrderCancelReplaceRequest(o, args);
    });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 548, "t_cross_id", "cross_id");
    expect_wire_text(mv, 551, "t_orig_cross_id", "orig_cross_id");
    expect_wire_text(mv, 549, "549", "cross_type");
    expect_wire_text(mv, 550, "550", "cross_prioritization");
    expect_wire_text(mv, 60, "t_transact_time", "transact_time");
    expect_wire_text(mv, 40, "1", "ord_type");
}

// CrossOrderCancelRequest (u) -- dictionaries/FIX44.xml:1165-1176
//   required 'CrossID' (FIX44.xml:1167) -> cross_id(tag 548)
//   required 'OrigCrossID' (FIX44.xml:1168) -> orig_cross_id(tag 551)
//   required 'CrossType' (FIX44.xml:1169) -> cross_type(tag 549)
//   required 'CrossPrioritization' (FIX44.xml:1170) -> cross_prioritization(tag 550)
//   required 'TransactTime' (FIX44.xml:1175) -> transact_time(tag 60)
TEST_F(AllFamiliesRoundtrip069, CrossOrderCancelRequest) {
    fixpp::v44::CrossOrderCancelRequestArgs args{};
    args.cross_id = "u_cross_id";
    args.orig_cross_id = "u_orig_cross_id";
    args.cross_type = 549;
    args.cross_prioritization = 550;
    args.transact_time = "u_transact_time";
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_CrossOrderCancelRequest(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 548, "u_cross_id", "cross_id");
    expect_wire_text(mv, 551, "u_orig_cross_id", "orig_cross_id");
    expect_wire_text(mv, 549, "549", "cross_type");
    expect_wire_text(mv, 550, "550", "cross_prioritization");
    expect_wire_text(mv, 60, "u_transact_time", "transact_time");
}

// SecurityTypeRequest (v) -- dictionaries/FIX44.xml:1177-1187
//   required 'SecurityReqID' (FIX44.xml:1178) -> security_req_id(tag 320)
//   filler (not required) -> text(tag 58)
TEST_F(AllFamiliesRoundtrip069, SecurityTypeRequest) {
    fixpp::v44::SecurityTypeRequestArgs args{};
    args.security_req_id = "v_security_req_id";
    args.text = "v_text";
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_SecurityTypeRequest(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 320, "v_security_req_id", "security_req_id");
    expect_wire_text(mv, 58, "v_text", "text");
}

// SecurityTypes (w) -- dictionaries/FIX44.xml:1188-1201
//   required 'SecurityReqID' (FIX44.xml:1189) -> security_req_id(tag 320)
//   required 'SecurityResponseID' (FIX44.xml:1190) -> security_response_id(tag 322)
//   required 'SecurityResponseType' (FIX44.xml:1191) -> security_response_type(tag 323)
TEST_F(AllFamiliesRoundtrip069, SecurityTypes) {
    fixpp::v44::SecurityTypesArgs args{};
    args.security_req_id = "w_security_req_id";
    args.security_response_id = "w_security_response_id";
    args.security_response_type = 323;
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_SecurityTypes(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 320, "w_security_req_id", "security_req_id");
    expect_wire_text(mv, 322, "w_security_response_id", "security_response_id");
    expect_wire_text(mv, 323, "323", "security_response_type");
}

// SecurityListRequest (x) -- dictionaries/FIX44.xml:1202-1217
//   required 'SecurityReqID' (FIX44.xml:1203) -> security_req_id(tag 320)
//   required 'SecurityListRequestType' (FIX44.xml:1204) -> security_list_request_type(tag 559)
TEST_F(AllFamiliesRoundtrip069, SecurityListRequest) {
    fixpp::v44::SecurityListRequestArgs args{};
    args.security_req_id = "x_security_req_id";
    args.security_list_request_type = 559;
    auto mv_opt = parse(
        [&](std::span<std::byte> o) { return fixpp::v44::build_SecurityListRequest(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 320, "x_security_req_id", "security_req_id");
    expect_wire_text(mv, 559, "559", "security_list_request_type");
}

// SecurityList (y) -- dictionaries/FIX44.xml:1218-1225
//   required 'SecurityReqID' (FIX44.xml:1219) -> security_req_id(tag 320)
//   required 'SecurityResponseID' (FIX44.xml:1220) -> security_response_id(tag 322)
//   required 'SecurityRequestResult' (FIX44.xml:1221) -> security_request_result(tag 560)
TEST_F(AllFamiliesRoundtrip069, SecurityList) {
    fixpp::v44::SecurityListArgs args{};
    args.security_req_id = "y_security_req_id";
    args.security_response_id = "y_security_response_id";
    args.security_request_result = 560;
    auto mv_opt =
        parse([&](std::span<std::byte> o) { return fixpp::v44::build_SecurityList(o, args); });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 320, "y_security_req_id", "security_req_id");
    expect_wire_text(mv, 322, "y_security_response_id", "security_response_id");
    expect_wire_text(mv, 560, "560", "security_request_result");
}

// DerivativeSecurityListRequest (z) -- dictionaries/FIX44.xml:1226-1238
//   required 'SecurityReqID' (FIX44.xml:1227) -> security_req_id(tag 320)
//   required 'SecurityListRequestType' (FIX44.xml:1228) -> security_list_request_type(tag 559)
TEST_F(AllFamiliesRoundtrip069, DerivativeSecurityListRequest) {
    fixpp::v44::DerivativeSecurityListRequestArgs args{};
    args.security_req_id = "z_security_req_id";
    args.security_list_request_type = 559;
    auto mv_opt = parse([&](std::span<std::byte> o) {
        return fixpp::v44::build_DerivativeSecurityListRequest(o, args);
    });
    ASSERT_TRUE(mv_opt.has_value()) << "build/parse pipeline failed";
    auto const& mv = *mv_opt;
    expect_wire_text(mv, 320, "z_security_req_id", "security_req_id");
    expect_wire_text(mv, 559, "559", "security_list_request_type");
}

// Coverage set-equality self-check (mandatory guard, mirrors
// test_067_completeness.cpp's set_difference diagnostic, lines 138-155):
// this file's own hand-written MsgType table MUST equal, set-for-set, the
// emitted fixpp::v44::builder_registry. Deleting/forgetting a row here is a
// LOUD failure (both-direction set_difference), never a silent under-cover
// (e.g. 70/83 green) -- feedback_completeness_gate_exact_set_not_subset.
TEST_F(AllFamiliesRoundtrip069, CoverageSetEqualityOverAllEmittedBuilders) {
    static constexpr std::array<std::string_view, 83> kSeedMsgTypes = {
        "6",  "7",  "8",  "9",  "AA", "AB", "AC", "AD", "AE", "AF", "AG", "AH", "AI", "AJ",
        "AK", "AL", "AM", "AN", "AO", "AP", "AQ", "AR", "AS", "AT", "AU", "AV", "AW", "AX",
        "AY", "AZ", "B",  "BA", "BB", "BC", "BD", "BG", "BH", "C",  "D",  "E",  "F",  "G",
        "H",  "J",  "K",  "L",  "M",  "N",  "P",  "Q",  "R",  "S",  "T",  "V",  "W",  "X",
        "Y",  "Z",  "a",  "b",  "c",  "d",  "e",  "f",  "g",  "h",  "i",  "j",  "k",  "l",
        "m",  "o",  "p",  "q",  "r",  "s",  "t",  "u",  "v",  "w",  "x",  "y",  "z",
    };

    std::set<std::string> seeded;
    for (auto mt : kSeedMsgTypes) seeded.insert(std::string{mt});
    ASSERT_EQ(seeded.size(), kSeedMsgTypes.size()) << "duplicate MsgType in kSeedMsgTypes";

    std::set<std::string> emitted;
    for (auto const& entry : fixpp::v44::builder_registry)
        emitted.insert(std::string{entry.msg_type});

    std::vector<std::string> missing;
    std::set_difference(emitted.begin(), emitted.end(), seeded.begin(), seeded.end(),
                        std::back_inserter(missing));
    std::vector<std::string> extra;
    std::set_difference(seeded.begin(), seeded.end(), emitted.begin(), emitted.end(),
                        std::back_inserter(extra));

    auto join = [](std::vector<std::string> const& v) {
        std::string s;
        for (auto const& x : v) {
            s += x;
            s += ' ';
        }
        return s;
    };
    EXPECT_TRUE(missing.empty())
        << "harness is MISSING emitted MsgTypes (under-covers builder_registry): " << join(missing);
    EXPECT_TRUE(extra.empty())
        << "harness seeds MsgTypes NOT in builder_registry (stale/typo row): " << join(extra);
    EXPECT_EQ(seeded, emitted);
    EXPECT_EQ(fixpp::v44::builder_registry.size(), 83u);
}

// ── T013 [US2] new-family required-field fail-closed witnesses ──────────
// Disposition: fixpp::core::error::wire_required_field_missing (the SAME
// enum test_067_builder_failclosed.cpp's RequiredGroupZero_ValidateRejects
// asserts -- builder_validate.hpp:77 (missing scalar) and :86 (empty
// required group) both return it). Each witness seeds every OTHER required
// field and omits EXACTLY the one under test, so the reject is attributable
// to that field (feedback_witness_asserts_named_postcondition_not_proxy (d)).

// Nested: TradeCaptureReport(AE)'s TrdCapRptSideGrp/NoSides is REQUIRED
// (dictionaries/FIX44.xml:1489/3536) with a required entry field Side(54)
// (FIX44.xml:3537). Seed everything else (incl. OrderID(37), FIX44.xml:3538,
// present) but omit Side on the one entry.
TEST(AllFamiliesFailClosed069, TradeCaptureReport_NoSidesEntry_MissingSide) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v44::TradeCaptureReportArgs args{};
    args.trade_report_id = "AE_trade_report_id";
    args.previously_reported = true;
    args.last_qty = make_decimal("10.5", &arena);
    args.last_px = make_decimal("10.5", &arena);
    args.trade_date = "AE_trade_date";
    args.transact_time = "AE_transact_time";
    fixpp::v44::TradeCaptureReportSidesArgs side_entry{};
    // side_entry.side intentionally left unset (nullopt) -- the ONE omitted
    // required entry field.
    side_entry.order_id =
        "AE_NoSides_OrderID";  // present, so the reject is attributable to Side alone
    std::array<fixpp::v44::TradeCaptureReportSidesArgs, 1> sides_arr{side_entry};
    args.sides = std::span<const fixpp::v44::TradeCaptureReportSidesArgs>{sides_arr};

    auto r = fixpp::v44::validate_TradeCaptureReport(args);
    ASSERT_FALSE(r.has_value()) << "a NoSides entry missing required Side(54) must fail validate_";
    EXPECT_EQ(r.error(), fixpp::core::error::wire_required_field_missing);
}

// Flat: BusinessMessageReject(j) requires RefMsgType(372) (FIX44.xml:958)
// AND BusinessRejectReason(380) (FIX44.xml:960). Seed BusinessRejectReason,
// omit RefMsgType alone.
TEST(AllFamiliesFailClosed069, BusinessMessageReject_MissingRefMsgType) {
    fixpp::v44::BusinessMessageRejectArgs args{};
    // args.ref_msg_type intentionally left unset (nullopt) -- the ONE omitted
    // required field.
    args.business_reject_reason =
        380;  // present, so the reject is attributable to RefMsgType alone

    auto r = fixpp::v44::validate_BusinessMessageReject(args);
    ASSERT_FALSE(r.has_value()) << "omitting required RefMsgType(372) must fail validate_";
    EXPECT_EQ(r.error(), fixpp::core::error::wire_required_field_missing);
}
