// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_069_family_exemplar_golden.cpp
//
// 069-v44-all-families T012 [US2] — the 8 fixed external-golden exemplars
// (contracts/coverage-and-completeness.md C4). For each of the 8 newly-
// covered family classes, seed the generated fixpp::v44::<Msg>Args with the
// IDENTICAL values used in the paired tests/session/golden/gen/qf_*.cpp
// QuickFIX-cpp generator, build_<Msg>() the bytes, and assert byte-equality
// against the checked-in QuickFIX golden via shape_oracle_profile() (excludes
// only framing tags {8,9,10,34,52} — every business tag matched verbatim,
// decimals by value). External oracle so builder+parser cannot be co-wrong
// (FR-010/SC-006).
//
// Anchors: specs/069-v44-all-families/contracts/coverage-and-completeness.md
//          C4; tests/session/golden/069_*.fix + golden/gen/qf_*.cpp;
//          tests/session/test_067_builder_shape_oracle.cpp (mechanism
//          precedent: parse_golden + diff_transcripts(shape_oracle_profile())).

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/v44/Builders.hpp>  // GENERATED (Phase 3) — build_<Msg>/<Msg>Args
#include <fstream>
#include <memory_resource>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

#include "support/golden_diff.hpp"

namespace {

using fixpp::decimal_t;
using fixpp::interop::diff_transcripts;
using fixpp::interop::GoldenFrame;
using fixpp::interop::parse_golden;
using fixpp::interop::shape_oracle_profile;

decimal_t make_decimal(std::string_view sv, std::pmr::memory_resource* mr) {
    auto bytes =
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
    auto r = decimal_t::parse(bytes, mr);
    EXPECT_TRUE(r.has_value()) << "make_decimal failed for: " << sv;
    return r.value_or(decimal_t{});
}

std::string read_file(const std::string& path) {
    std::ifstream in{path, std::ios::binary};
    EXPECT_TRUE(in.good()) << "failed to open: " << path;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Golden diff a single built body against its checked-in golden via
// shape_oracle_profile() (C4). `golden_relpath` is relative to the repo root.
void assert_golden_match(std::string_view golden_relpath, std::span<const std::byte> body) {
    std::string golden_path = std::string(FIXPP_REPO_ROOT) + "/" + std::string(golden_relpath);
    std::string golden_text = read_file(golden_path);
    std::vector<GoldenFrame> expected = parse_golden(golden_text);
    ASSERT_EQ(expected.size(), 1U);

    std::vector<GoldenFrame> actual{
        GoldenFrame{'>', std::vector<std::byte>{body.begin(), body.end()}}};
    auto diff = diff_transcripts(expected, actual, shape_oracle_profile());
    EXPECT_TRUE(static_cast<bool>(diff)) << "golden diff mismatch: " << diff.detail;
}

}  // namespace

// ── j (BusinessMessageReject) — flat, ref-tag echo ──────────────────────────
// Seed identical to golden/gen/qf_businessmessagereject.cpp: RefMsgType(372)="D",
// BusinessRejectReason(380)=3 (UnsupportedMessageType).
TEST(FamilyGolden069, BusinessMessageReject_J) {
    fixpp::v44::BusinessMessageRejectArgs args{};
    args.ref_msg_type = "D";
    args.business_reject_reason = 3;

    std::array<std::byte, 1024> out{};
    auto r = fixpp::v44::build_BusinessMessageReject(std::span<std::byte>{out}, args);
    ASSERT_TRUE(r.has_value()) << "build_BusinessMessageReject failed";

    assert_golden_match("tests/session/golden/069_businessmessagereject_j.fix", *r);
}

// ── AE (TradeCaptureReport) — group-heavy/nested (NoSides / NoLegs) ─────────
// Seed identical to golden/gen/qf_tradecapturereport.cpp: TradeReportID=
// "TCR001", PreviouslyReported=false, LastQty=100, LastPx=50.25,
// TradeDate="20240101", TransactTime="20240101-00:00:00", Symbol="MSFT";
// NoSides: 1 entry (Side=Buy, OrderID="ORDER1") carrying a nested NoPartyIDs
// entry (PartyID="PARTY1", PartyIDSource='D', PartyRole=1); NoLegs: 1 entry
// (LegSymbol="IBM") — the required group-in-group/nested exemplar.
TEST(FamilyGolden069, TradeCaptureReport_AE) {
    fixpp::v44::groups::G_453Args party{};
    party.party_id = "PARTY1";
    party.party_id_source = 'D';
    party.party_role = 1;
    std::array<fixpp::v44::groups::G_453Args, 1> parties{party};

    fixpp::v44::groups::G_552_1Args side{};
    side.side = '1';
    side.order_id = "ORDER1";
    side.party_i_ds =
        std::optional<std::span<const fixpp::v44::groups::G_453Args>>{
            std::span<const fixpp::v44::groups::G_453Args>{parties}};
    std::array<fixpp::v44::groups::G_552_1Args, 1> sides{side};

    fixpp::v44::groups::G_555_3Args leg{};
    leg.leg_symbol = "IBM";
    std::array<fixpp::v44::groups::G_555_3Args, 1> legs{leg};

    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v44::TradeCaptureReportArgs args{};
    args.last_px = make_decimal("50.25", &arena);
    args.last_qty = make_decimal("100", &arena);
    args.symbol = "MSFT";
    args.transact_time = "20240101-00:00:00";
    args.trade_date = "20240101";
    args.sides = std::span<const fixpp::v44::groups::G_552_1Args>{sides};
    args.legs = std::optional<std::span<const fixpp::v44::groups::G_555_3Args>>{
        std::span<const fixpp::v44::groups::G_555_3Args>{legs}};
    args.previously_reported = false;
    args.trade_report_id = "TCR001";

    std::array<std::byte, 2048> out{};
    auto r = fixpp::v44::build_TradeCaptureReport(std::span<std::byte>{out}, args);
    ASSERT_TRUE(r.has_value()) << "build_TradeCaptureReport failed";

    assert_golden_match("tests/session/golden/069_tradecapturereport_ae.fix", *r);
}

// ── AP (PositionReport) — NoPositions group ─────────────────────────────────
// Seed identical to golden/gen/qf_positionreport.cpp.
TEST(FamilyGolden069, PositionReport_AP) {
    fixpp::v44::groups::G_702Args pos{};
    pos.pos_type = "TQ";
    std::pmr::monotonic_buffer_resource arena{4096};
    pos.long_qty = make_decimal("100", &arena);
    std::array<fixpp::v44::groups::G_702Args, 1> positions{pos};

    fixpp::v44::PositionReportArgs args{};
    args.account = "ACCT1";
    args.account_type = 1;
    args.positions = std::optional<std::span<const fixpp::v44::groups::G_702Args>>{
        std::span<const fixpp::v44::groups::G_702Args>{positions}};
    args.clearing_business_date = "20240101";
    args.pos_maint_rpt_id = "POSRPT1";
    args.pos_req_result = 0;
    args.settl_price = make_decimal("100.5", &arena);
    args.settl_price_type = 1;
    args.prior_settl_price = make_decimal("99.75", &arena);

    std::array<std::byte, 1024> out{};
    auto r = fixpp::v44::build_PositionReport(std::span<std::byte>{out}, args);
    ASSERT_TRUE(r.has_value()) << "build_PositionReport failed";

    assert_golden_match("tests/session/golden/069_positionreport_ap.fix", *r);
}

// ── BB (CollateralInquiry) — no required fields; NoCollInquiryQualifier ─────
// Seed identical to golden/gen/qf_collateralinquiry.cpp.
TEST(FamilyGolden069, CollateralInquiry_BB) {
    fixpp::v44::groups::G_938Args qual{};
    qual.coll_inquiry_qualifier = 0;
    std::array<fixpp::v44::groups::G_938Args, 1> quals{qual};

    fixpp::v44::CollateralInquiryArgs args{};
    args.account = "ACCT1";
    args.account_type = 1;
    args.coll_inquiry_id = "COLLINQ1";
    args.coll_inquiry_qualifier =
        std::optional<std::span<const fixpp::v44::groups::G_938Args>>{
            std::span<const fixpp::v44::groups::G_938Args>{quals}};

    std::array<std::byte, 1024> out{};
    auto r = fixpp::v44::build_CollateralInquiry(std::span<std::byte>{out}, args);
    ASSERT_TRUE(r.has_value()) << "build_CollateralInquiry failed";

    assert_golden_match("tests/session/golden/069_collateralinquiry_bb.fix", *r);
}

// ── y (SecurityList) — NoRelatedSym group ───────────────────────────────────
// Seed identical to golden/gen/qf_securitylist.cpp.
TEST(FamilyGolden069, SecurityList_Y) {
    fixpp::v44::groups::G_146_7Args entry{};
    entry.symbol = "MSFT";
    std::array<fixpp::v44::groups::G_146_7Args, 1> related{entry};

    fixpp::v44::SecurityListArgs args{};
    args.related_sym = std::optional<std::span<const fixpp::v44::groups::G_146_7Args>>{
        std::span<const fixpp::v44::groups::G_146_7Args>{related}};
    args.security_req_id = "SECREQ1";
    args.security_response_id = "SECRESP1";
    args.security_request_result = 0;

    std::array<std::byte, 1024> out{};
    auto r = fixpp::v44::build_SecurityList(std::span<std::byte>{out}, args);
    ASSERT_TRUE(r.has_value()) << "build_SecurityList failed";

    assert_golden_match("tests/session/golden/069_securitylist_y.fix", *r);
}

// ── AK (Confirmation) — required NoCapacities group ─────────────────────────
// Seed identical to golden/gen/qf_confirmation.cpp.
TEST(FamilyGolden069, Confirmation_AK) {
    fixpp::v44::groups::G_862Args cap{};
    cap.order_capacity = 'A';
    std::pmr::monotonic_buffer_resource arena{4096};
    cap.order_capacity_qty = make_decimal("100", &arena);
    std::array<fixpp::v44::groups::G_862Args, 1> capacities{cap};

    fixpp::v44::ConfirmationArgs args{};
    args.avg_px = make_decimal("50.25", &arena);
    args.side = '1';
    args.transact_time = "20240101-00:00:00";
    args.trade_date = "20240101";
    args.alloc_account = "ACCT1";
    args.alloc_qty = make_decimal("100", &arena);
    args.net_money = make_decimal("5025", &arena);
    args.gross_trade_amt = make_decimal("5025", &arena);
    args.confirm_id = "CONF1";
    args.confirm_status = 1;
    args.confirm_trans_type = 0;
    args.confirm_type = 1;
    args.capacities = std::span<const fixpp::v44::groups::G_862Args>{capacities};

    std::array<std::byte, 1024> out{};
    auto r = fixpp::v44::build_Confirmation(std::span<std::byte>{out}, args);
    ASSERT_TRUE(r.has_value()) << "build_Confirmation failed";

    assert_golden_match("tests/session/golden/069_confirmation_ak.fix", *r);
}

// ── o (RegistrationInstructions) — nested NoRegistDtls ──────────────────────
// Seed identical to golden/gen/qf_registrationinstructions.cpp.
TEST(FamilyGolden069, RegistrationInstructions_O) {
    fixpp::v44::groups::G_473Args dtl{};
    dtl.regist_dtls = "DETAILS1";
    std::array<fixpp::v44::groups::G_473Args, 1> dtls{dtl};

    fixpp::v44::RegistrationInstructionsArgs args{};
    args.regist_dtls =
        std::optional<std::span<const fixpp::v44::groups::G_473Args>>{
            std::span<const fixpp::v44::groups::G_473Args>{dtls}};
    args.regist_ref_id = "REGREF1";
    args.regist_id = "REGID1";
    args.regist_trans_type = '0';

    std::array<std::byte, 1024> out{};
    auto r = fixpp::v44::build_RegistrationInstructions(std::span<std::byte>{out}, args);
    ASSERT_TRUE(r.has_value()) << "build_RegistrationInstructions failed";

    assert_golden_match("tests/session/golden/069_registrationinstructions_o.fix", *r);
}

// ── N (ListStatus) — required NoOrders group ────────────────────────────────
// Seed identical to golden/gen/qf_liststatus.cpp.
TEST(FamilyGolden069, ListStatus_N) {
    fixpp::v44::groups::G_73_3Args order{};
    order.cl_ord_id = "ORD1";
    std::pmr::monotonic_buffer_resource arena{4096};
    order.cum_qty = make_decimal("100", &arena);
    order.ord_status = '2';
    order.leaves_qty = make_decimal("0", &arena);
    order.cxl_qty = make_decimal("0", &arena);
    std::array<fixpp::v44::groups::G_73_3Args, 1> orders{order};

    fixpp::v44::ListStatusArgs args{};
    args.list_id = "LIST1";
    args.tot_no_orders = 1;
    args.orders = std::span<const fixpp::v44::groups::G_73_3Args>{orders};
    args.no_rpts = 1;
    args.rpt_seq = 1;
    args.list_status_type = 1;
    args.list_order_status = 1;

    std::array<std::byte, 1024> out{};
    auto r = fixpp::v44::build_ListStatus(std::span<std::byte>{out}, args);
    ASSERT_TRUE(r.has_value()) << "build_ListStatus failed";

    assert_golden_match("tests/session/golden/069_liststatus_n.fix", *r);
}
