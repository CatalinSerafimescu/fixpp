// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/vlatest_reify_roundtrip_test.cpp
//
// 076-fix-latest-typed-codegen T009(b) [US1] -- V-2 typed round-trip,
// universal reify/read-back leg (specs/076-fix-latest-typed-codegen/
// tasks.md T009; contracts V-2): for all 181 fixpp::vlatest MsgTypes, parse
// a minimal raw wire frame (dict-free Parser<Index>, no dictionary needed --
// see below) and drive it through the SAME generic
// owning_<Msg>::from_view() -> view() -> msg_type() pipeline that every
// fixpp::vlatest::Reify.hpp class exposes with an IDENTICAL signature
// (build/.../vlatest/Reify.hpp:52-53/204-214 et al, mirrored verbatim across
// all 181 owning_<Msg> classes) -- "universal" = one uniform mechanism
// (check_one<Owning>()), not 181 hand-written bodies. The 181-entry
// enumeration below is mechanically extracted from the generated Reify.hpp
// (regex over `class owning_<Name> { public: static constexpr
// ::std::string_view msg_type_v = "<Code>";`), not hand-transcribed from
// memory; count cross-checked against the independent raw-XML census in
// vlatest_boundary_test.cpp (181 <fixr:message> elements).
//
// SCOPE NOTE -- T009(a) (the typed *build_<Msg>* application-subset leg via
// fixpp::vlatest::Builders.hpp) is NOT implemented in this TU or anywhere in
// this phase. Builders.hpp is 131MB / ~2.07M lines for the 173-message
// app-subset -- its deeply-nested-group Args structs are NOT deduplicated
// across messages the way the read-side group flyweights are (unlike
// v44/Builders.hpp: 83 messages / 3.7MB), so it grows combinatorially with
// nesting depth. A single TU #including it was empirically measured (this
// implementer, 076 phase-3) to exceed 22GB RSS and 6m51s wall-clock without
// finishing compilation and had to be SIGKILL'd to protect the 24GB build
// host (feedback_build_resource_cap_oom). Escalated to the orchestrator
// rather than attempted here; see the phase-3 report.
//
// V-2 CAVEAT (carried per contract/brief): a green round-trip here is BLIND
// to absent fields -- only MsgType(35) is asserted per message (a minimal,
// otherwise-empty frame) -- this is NOT a completeness proof. Completeness
// (V-1 / V-1b) is Phase 4 / US2, out of scope here.
//
// Dict-free rationale: Parser<Index>{} default-constructs without a
// dict::table_view (include/fixpp/wire/parser.hpp:565), and
// owning_<Msg>::from_view() only deep-copies the already-framed byte span
// (Reify.hpp:204-214) -- it does not read or validate any field beyond what
// the accessor the caller invokes decodes. No dictionary/OrchestraLoader
// dependency is needed to exercise this pipeline.
//
// Anchors: specs/076-fix-latest-typed-codegen/tasks.md T009;
//          build/linux-clang-debug/_codegen/include/fixpp/vlatest/Reify.hpp
//          (owning_<Msg>::from_view/view()/msg_type() uniform shape);
//          tests/support/app_message_read_scaffold.hpp (Framer/
//          pmr_carry_buffer usage precedent).

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fixpp/vlatest/Reify.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Minimal raw FIX frame carrying only 35=<code>: "8=FIXT.1.1|9=<len>|
// 35=<code>|10=<chk>". FIXT.1.1 is the real-world FIX-Latest session
// BeginString (EP-based, ApplVerID-differentiated transport); the value is
// inert here since neither the dict-free Parser<Index> nor from_view()
// inspects it.
std::vector<std::byte> make_minimal_frame(std::string_view msg_type_code) {
    std::string body = "35=" + std::string(msg_type_code) + "\x01";
    std::string pre =
        "8=FIXT.1.1\x01" "9=" + std::to_string(body.size()) + "\x01" + body;
    unsigned sum = 0;
    for (unsigned char c : pre) sum += c;
    char chk[16]{};
    std::snprintf(chk, sizeof(chk), "10=%03u\x01", sum % 256U);
    std::string full = pre + chk;
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// Parses `code`'s minimal frame dict-free and drives it through
// Owning::from_view() -> view() -> msg_type(); asserts the round-trip
// succeeds and MsgType(35) survives the reify deep-copy verbatim. Uniform
// across every fixpp::vlatest::owning_<Msg> (identical from_view/view()/
// msg_type() signatures -- Reify.hpp).
template <typename Owning>
void check_one(std::string_view code) {
    SCOPED_TRACE(code);
    auto frame = make_minimal_frame(code);
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::wire::pmr_carry_buffer carry{frame.size(), &arena};
    fixpp::wire::Framer framer{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = framer.feed(std::span<const std::byte>{frame.data(), frame.size()}, carry,
                              std::span<fixpp::wire::frame_view>{fvs, 1});
    ASSERT_TRUE(framed.has_value() && !framed->empty()) << "Framer::feed failed for " << code;

    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{};  // dict-free (parser.hpp:565)
    auto src = parser.parse(fvs[0], &arena);
    ASSERT_TRUE(src.has_value()) << "dict-free Parser<Index>::parse failed for " << code;

    auto owned = Owning::from_view(*src, &arena);
    ASSERT_TRUE(owned.has_value()) << "owning_<Msg>::from_view failed for " << code;
    auto mt = owned->msg_type();
    ASSERT_TRUE(mt.has_value()) << "msg_type() read-back failed for " << code;
    EXPECT_EQ(*mt, code) << "MsgType(35) must survive the reify round-trip verbatim";
    EXPECT_EQ(Owning::msg_type_v, code);
}

}  // namespace

// Mechanically extracted from build/.../vlatest/Reify.hpp (see file header
// comment for the extraction regex); NOT hand-transcribed. 181 entries.
#define FIXPP_076_VLATEST_MESSAGES(X) \
    X(Heartbeat,"0") X(TestRequest,"1") X(ResendRequest,"2") \
    X(Reject,"3") X(SequenceReset,"4") X(Logout,"5") \
    X(IOI,"6") X(Advertisement,"7") X(ExecutionReport,"8") \
    X(OrderCancelReject,"9") X(Logon,"A") X(DerivativeSecurityList,"AA") \
    X(NewOrderMultileg,"AB") X(MultilegOrderCancelReplace,"AC") X(TradeCaptureReportRequest,"AD") \
    X(TradeCaptureReport,"AE") X(OrderMassStatusRequest,"AF") X(QuoteRequestReject,"AG") \
    X(RFQRequest,"AH") X(QuoteStatusReport,"AI") X(QuoteResponse,"AJ") \
    X(Confirmation,"AK") X(PositionMaintenanceRequest,"AL") X(PositionMaintenanceReport,"AM") \
    X(RequestForPositions,"AN") X(RequestForPositionsAck,"AO") X(PositionReport,"AP") \
    X(TradeCaptureReportRequestAck,"AQ") X(TradeCaptureReportAck,"AR") X(AllocationReport,"AS") \
    X(AllocationReportAck,"AT") X(ConfirmationAck,"AU") X(SettlementInstructionRequest,"AV") \
    X(AssignmentReport,"AW") X(CollateralRequest,"AX") X(CollateralAssignment,"AY") \
    X(CollateralResponse,"AZ") X(News,"B") X(CollateralReport,"BA") \
    X(CollateralInquiry,"BB") X(NetworkCounterpartySystemStatusRequest,"BC") X(NetworkCounterpartySystemStatusResponse,"BD") \
    X(UserRequest,"BE") X(UserResponse,"BF") X(CollateralInquiryAck,"BG") \
    X(ConfirmationRequest,"BH") X(TradingSessionListRequest,"BI") X(TradingSessionList,"BJ") \
    X(SecurityListUpdateReport,"BK") X(AdjustedPositionReport,"BL") X(AllocationInstructionAlert,"BM") \
    X(ExecutionAck,"BN") X(ContraryIntentionReport,"BO") X(SecurityDefinitionUpdateReport,"BP") \
    X(SettlementObligationReport,"BQ") X(DerivativeSecurityListUpdateReport,"BR") X(TradingSessionListUpdateReport,"BS") \
    X(MarketDefinitionRequest,"BT") X(MarketDefinition,"BU") X(MarketDefinitionUpdateReport,"BV") \
    X(ApplicationMessageRequest,"BW") X(ApplicationMessageRequestAck,"BX") X(ApplicationMessageReport,"BY") \
    X(OrderMassActionReport,"BZ") X(Email,"C") X(OrderMassActionRequest,"CA") \
    X(UserNotification,"CB") X(StreamAssignmentRequest,"CC") X(StreamAssignmentReport,"CD") \
    X(StreamAssignmentReportACK,"CE") X(PartyDetailsListRequest,"CF") X(PartyDetailsListReport,"CG") \
    X(MarginRequirementInquiry,"CH") X(MarginRequirementInquiryAck,"CI") X(MarginRequirementReport,"CJ") \
    X(PartyDetailsListUpdateReport,"CK") X(PartyRiskLimitsRequest,"CL") X(PartyRiskLimitsReport,"CM") \
    X(SecurityMassStatusRequest,"CN") X(SecurityMassStatus,"CO") X(AccountSummaryReport,"CQ") \
    X(PartyRiskLimitsUpdateReport,"CR") X(PartyRiskLimitsDefinitionRequest,"CS") X(PartyRiskLimitsDefinitionRequestAck,"CT") \
    X(PartyEntitlementsRequest,"CU") X(PartyEntitlementsReport,"CV") X(QuoteAck,"CW") \
    X(PartyDetailsDefinitionRequest,"CX") X(PartyDetailsDefinitionRequestAck,"CY") X(PartyEntitlementsUpdateReport,"CZ") \
    X(NewOrderSingle,"D") X(PartyEntitlementsDefinitionRequest,"DA") X(PartyEntitlementsDefinitionRequestAck,"DB") \
    X(TradeMatchReport,"DC") X(TradeMatchReportAck,"DD") X(PartyRiskLimitsReportAck,"DE") \
    X(PartyRiskLimitCheckRequest,"DF") X(PartyRiskLimitCheckRequestAck,"DG") X(PartyActionRequest,"DH") \
    X(PartyActionReport,"DI") X(MassOrder,"DJ") X(MassOrderAck,"DK") \
    X(PositionTransferInstruction,"DL") X(PositionTransferInstructionAck,"DM") X(PositionTransferReport,"DN") \
    X(MarketDataStatisticsRequest,"DO") X(MarketDataStatisticsReport,"DP") X(CollateralReportAck,"DQ") \
    X(MarketDataReport,"DR") X(CrossRequest,"DS") X(CrossRequestAck,"DT") \
    X(AllocationInstructionAlertRequest,"DU") X(AllocationInstructionAlertRequestAck,"DV") X(TradeAggregationRequest,"DW") \
    X(TradeAggregationReport,"DX") X(PayManagementRequest,"DY") X(PayManagementRequestAck,"DZ") \
    X(NewOrderList,"E") X(PayManagementReport,"EA") X(PayManagementReportAck,"EB") \
    X(SettlementStatusRequest,"EC") X(SettlementStatusRequestAck,"ED") X(SettlementStatusReport,"EE") \
    X(SettlementStatusReportAck,"EF") X(SecurityRiskMetricsReport,"EG") X(AlgoCertificateRequest,"EH") \
    X(AlgoCertificateRequestAck,"EI") X(AlgoCertificateReport,"EJ") X(AlgoCertificateReportAck,"EK") \
    X(TestSuiteDefinitionRequest,"EL") X(TestSuiteDefinitionRequestAck,"EM") X(TestActionRequest,"EN") \
    X(TestActionRequestAck,"EO") X(TestActionReport,"EP") X(MarketDataAck,"EQ") \
    X(SecurityStatusAck,"ER") X(TradingSessionStatusAck,"ES") X(OrderCancelRequest,"F") \
    X(OrderCancelReplaceRequest,"G") X(OrderStatusRequest,"H") X(AllocationInstruction,"J") \
    X(ListCancelRequest,"K") X(ListExecute,"L") X(ListStatusRequest,"M") \
    X(ListStatus,"N") X(AllocationInstructionAck,"P") X(DontKnowTrade,"Q") \
    X(QuoteRequest,"R") X(Quote,"S") X(SettlementInstructions,"T") \
    X(MarketDataRequest,"V") X(MarketDataSnapshotFullRefresh,"W") X(MarketDataIncrementalRefresh,"X") \
    X(MarketDataRequestReject,"Y") X(QuoteCancel,"Z") X(QuoteStatusRequest,"a") \
    X(MassQuoteAck,"b") X(SecurityDefinitionRequest,"c") X(SecurityDefinition,"d") \
    X(SecurityStatusRequest,"e") X(SecurityStatus,"f") X(TradingSessionStatusRequest,"g") \
    X(TradingSessionStatus,"h") X(MassQuote,"i") X(BusinessMessageReject,"j") \
    X(BidRequest,"k") X(BidResponse,"l") X(ListStrikePrice,"m") \
    X(XMLnonFIX,"n") X(RegistrationInstructions,"o") X(RegistrationInstructionsResponse,"p") \
    X(OrderMassCancelRequest,"q") X(OrderMassCancelReport,"r") X(NewOrderCross,"s") \
    X(CrossOrderCancelReplaceRequest,"t") X(CrossOrderCancelRequest,"u") X(SecurityTypeRequest,"v") \
    X(SecurityTypes,"w") X(SecurityListRequest,"x") X(SecurityList,"y") \
    X(DerivativeSecurityListRequest,"z")

TEST(VlatestReifyRoundtrip076, All181MessagesUniversalReifyReadback) {
#define FIXPP_076_CHECK_ONE(Name, Code) check_one<fixpp::vlatest::owning_##Name>(Code);
    FIXPP_076_VLATEST_MESSAGES(FIXPP_076_CHECK_ONE)
#undef FIXPP_076_CHECK_ONE
}

// Non-tautological cardinality pin: the enumeration itself must carry
// exactly 181 entries (a truncated/duplicated X-macro list would otherwise
// silently under/over-cover while the loop above still passes on whatever
// subset it happens to enumerate).
TEST(VlatestReifyRoundtrip076, EnumeratedCountIs181) {
    std::size_t count = 0;
#define FIXPP_076_COUNT_ONE(Name, Code) ++count;
    FIXPP_076_VLATEST_MESSAGES(FIXPP_076_COUNT_ONE)
#undef FIXPP_076_COUNT_ONE
    EXPECT_EQ(count, 181U);
}
