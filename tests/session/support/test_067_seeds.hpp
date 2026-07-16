// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/support/test_067_seeds.hpp
//
// 067-codegen-writer-emitter T012 [US1] [TESTS-FIRST] — the round-trip SEED
// TABLE: one row per OFFICIAL v44 MsgType (33 rows), each row a plain
// (non-generated-type) struct of raw seed values keyed by the SAME accessor
// names `to_accessor`/`kind_of` will produce on the generated `<Msg>Args`
// (verified against the ALREADY-GENERATED read-side `fixpp::v44::<Msg>`
// flyweight accessors in build/<preset>/_codegen/include/fixpp/v44/
// Messages.hpp, which shares `to_accessor`/`kind_of` verbatim with the write
// emitter per research.md R7 — so these names are the real, checked
// contract, not a guess).
//
// Deliberately standalone (no #include of the not-yet-generated
// <fixpp/v44/Builders.hpp>): this header must compile/link on its own so the
// RED failure lands cleanly in the CONSUMER (test_067_builder_roundtrip.cpp),
// not smeared across the seed data itself.
//
// Field selection: for the 28 group-free messages, 2-6 clean BODY (non-
// header/non-trailer) scalar fields spanning String/Char/Int32/Decimal/Bool
// kinds. For the 5 group-bearing messages singled out by contracts/
// generated-builder.md G6/research.md R5 (E, AS, W, X, i — the mandatory W/
// X paired NoMDEntries(268) discriminator + MassQuote nested-group
// insurance), full group population including nested depth (E: 73->453->802;
// AS: 453->802; i: 296->295).
//
// Decimal-kind fields are carried as ASCII string_view literals (parsed via
// the shared make_decimal() helper in the consuming test, matching the
// tests/session/test_exemplar_roundtrip.cpp precedent) since decimal_t needs
// a memory_resource to construct.
//
// ── Nested-group Args type naming (077-builder-args-dedup) ─────────────────
// Nested repeating-group Args types are emitted DEDUPED per-plan as
// `fixpp::v44::groups::G_<no_tag>Args` (single plan) or
// `G_<no_tag>_<ordinal>Args` (≥2 distinct member-set plans sharing the same
// no_tag — e.g. W's and X's NoMDEntries(268) have different member sets and
// so land on different ordinals). This SUPERSEDES the earlier per-message-
// qualified naming this comment used to describe (`NewOrderListOrdersArgs`
// etc.); see specs/077-builder-args-dedup/contracts/golden/
// v44_Builders_all.golden.hpp for the authoritative name map.
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace fixpp_test_support::seeds067 {

// ── 28 group-free messages ──────────────────────────────────────────────

struct NewOrderSingleSeed {
    std::string_view cl_ord_id = "ORD-100";
    std::string_view symbol = "MSFT";
    char side = '1';
    std::string_view order_qty = "10";   // decimal ASCII
    std::string_view price = "99.5";     // decimal ASCII
    bool locate_reqd = true;
    std::string_view encoded_text = "hello world";  // Length+Data pair (FR-007a)
};
inline constexpr NewOrderSingleSeed kNewOrderSingleSeed{};

struct OrderCancelRequestSeed {
    std::string_view cl_ord_id = "ORD-200";
    std::string_view order_id = "OID-1";
    std::string_view orig_cl_ord_id = "ORD-199";
    char side = '2';
    std::string_view symbol = "IBM";
    std::string_view transact_time = "20240102-11:00:00";
};
inline constexpr OrderCancelRequestSeed kOrderCancelRequestSeed{};

struct OrderCancelReplaceRequestSeed {
    std::string_view cl_ord_id = "ORD-300";
    std::string_view order_id = "OID-2";
    std::string_view orig_cl_ord_id = "ORD-299";
    std::string_view price = "101.25";
    char side = '1';
    std::string_view symbol = "GOOG";
};
inline constexpr OrderCancelReplaceRequestSeed kOrderCancelReplaceRequestSeed{};

struct OrderStatusRequestSeed {
    std::string_view cl_ord_id = "ORD-400";
    std::string_view order_id = "OID-3";
    char side = '2';
    std::string_view symbol = "AAPL";
};
inline constexpr OrderStatusRequestSeed kOrderStatusRequestSeed{};

struct ExecutionReportSeed {
    std::string_view avg_px = "150.25";
    std::string_view cl_ord_id = "ORD-500";
    std::string_view exec_id = "EXEC-9";
    std::string_view order_id = "OID-4";
    char ord_status = '0';
    char side = '1';
    std::string_view symbol = "TSLA";
    std::string_view leaves_qty = "5";
};
inline constexpr ExecutionReportSeed kExecutionReportSeed{};

struct OrderCancelRejectSeed {
    std::string_view cl_ord_id = "ORD-600";
    std::string_view order_id = "OID-5";
    char ord_status = '8';
    std::string_view orig_cl_ord_id = "ORD-599";
    std::int64_t cxl_rej_reason = 0;
    char cxl_rej_response_to = '1';
};
inline constexpr OrderCancelRejectSeed kOrderCancelRejectSeed{};

struct OrderMassCancelRequestSeed {
    std::string_view cl_ord_id = "MC-1";
    char side = '1';
    std::string_view symbol = "MSFT";
    std::string_view transact_time = "20240103-09:00:00";
    char mass_cancel_request_type = '7';
};
inline constexpr OrderMassCancelRequestSeed kOrderMassCancelRequestSeed{};

struct OrderMassCancelReportSeed {
    std::string_view cl_ord_id = "MC-2";
    std::string_view order_id = "OID-6";
    char side = '2';
    std::string_view symbol = "IBM";
    char mass_cancel_request_type = '7';
    char mass_cancel_response = '0';
};
inline constexpr OrderMassCancelReportSeed kOrderMassCancelReportSeed{};

struct OrderMassStatusRequestSeed {
    std::string_view symbol = "MSFT";
    char side = '1';
    std::string_view mass_status_req_id = "MSR-1";
    std::int64_t mass_status_req_type = 1;
};
inline constexpr OrderMassStatusRequestSeed kOrderMassStatusRequestSeed{};

struct MultilegOrderCancelReplaceSeed {
    std::string_view cl_ord_id = "ML-1";
    std::string_view order_id = "OID-7";
    std::string_view orig_cl_ord_id = "ML-0";
    std::string_view price = "50.5";
    char side = '1';
    std::string_view symbol = "SPX";
};
inline constexpr MultilegOrderCancelReplaceSeed kMultilegOrderCancelReplaceSeed{};

struct CrossOrderCancelReplaceRequestSeed {
    std::string_view order_id = "OID-8";
    std::string_view price = "75.25";
    std::string_view symbol = "EUR/USD";
    std::string_view cross_id = "CR-1";
    std::int64_t cross_type = 1;
};
inline constexpr CrossOrderCancelReplaceRequestSeed kCrossOrderCancelReplaceRequestSeed{};

struct CrossOrderCancelRequestSeed {
    std::string_view order_id = "OID-9";
    std::string_view symbol = "EUR/USD";
    std::string_view cross_id = "CR-2";
    std::int64_t cross_type = 1;
};
inline constexpr CrossOrderCancelRequestSeed kCrossOrderCancelRequestSeed{};

struct MarketDataRequestSeed {
    std::string_view md_req_id = "MDR-1";
    char subscription_request_type = '1';
    std::int64_t market_depth = 0;
    bool aggregated_book = true;
};
inline constexpr MarketDataRequestSeed kMarketDataRequestSeed{};

struct MarketDataRequestRejectSeed {
    std::string_view md_req_id = "MDR-2";
    char md_req_rej_reason = '0';
    std::string_view text = "unknown symbol";
};
inline constexpr MarketDataRequestRejectSeed kMarketDataRequestRejectSeed{};

struct SecurityDefinitionRequestSeed {
    std::string_view symbol = "MSFT";
    std::string_view security_req_id = "SDR-1";
    std::int64_t security_request_type = 0;
};
inline constexpr SecurityDefinitionRequestSeed kSecurityDefinitionRequestSeed{};

struct SecurityDefinitionSeed {
    std::string_view symbol = "MSFT";
    std::string_view security_req_id = "SDR-2";
    std::string_view security_response_id = "SDR-2-RESP";
    std::int64_t security_response_type = 1;
};
inline constexpr SecurityDefinitionSeed kSecurityDefinitionSeed{};

struct SecurityStatusRequestSeed {
    std::string_view symbol = "MSFT";
    std::string_view security_status_req_id = "SSR-1";
    char subscription_request_type = '1';
};
inline constexpr SecurityStatusRequestSeed kSecurityStatusRequestSeed{};

struct SecurityStatusSeed {
    std::string_view symbol = "MSFT";
    std::string_view security_status_req_id = "SSR-2";
    std::int64_t security_trading_status = 17;
    bool unsolicited_indicator = false;
};
inline constexpr SecurityStatusSeed kSecurityStatusSeed{};

struct TradingSessionStatusRequestSeed {
    std::string_view trad_ses_req_id = "TSR-1";
    std::string_view trading_session_id = "SESSION1";
    char subscription_request_type = '1';
};
inline constexpr TradingSessionStatusRequestSeed kTradingSessionStatusRequestSeed{};

struct TradingSessionStatusSeed {
    std::string_view trading_session_id = "SESSION1";
    std::int64_t trad_ses_status = 2;
    bool unsolicited_indicator = true;
};
inline constexpr TradingSessionStatusSeed kTradingSessionStatusSeed{};

struct MassQuoteAcknowledgementSeed {
    std::string_view quote_id = "QID-1";
    std::int64_t quote_status = 0;
    std::int64_t quote_response_level = 1;
};
inline constexpr MassQuoteAcknowledgementSeed kMassQuoteAcknowledgementSeed{};

struct QuoteSeed {
    std::string_view quote_id = "QID-2";
    std::string_view symbol = "MSFT";
    std::string_view bid_px = "99.5";
    std::string_view offer_px = "100.5";
    char side = '1';
};
inline constexpr QuoteSeed kQuoteSeed{};

struct QuoteRequestSeed {
    std::string_view quote_req_id = "QR-1";
    std::string_view cl_ord_id = "QCL-1";
};
inline constexpr QuoteRequestSeed kQuoteRequestSeed{};

struct QuoteRequestRejectSeed {
    std::string_view quote_req_id = "QR-2";
    std::int64_t quote_request_reject_reason = 1;
};
inline constexpr QuoteRequestRejectSeed kQuoteRequestRejectSeed{};

struct QuoteCancelSeed {
    std::string_view quote_id = "QID-3";
    std::int64_t quote_cancel_type = 1;
    std::int64_t quote_response_level = 0;
};
inline constexpr QuoteCancelSeed kQuoteCancelSeed{};

struct QuoteStatusRequestSeed {
    std::string_view symbol = "MSFT";
    std::string_view quote_id = "QID-4";
    std::string_view quote_status_req_id = "QSR-1";
};
inline constexpr QuoteStatusRequestSeed kQuoteStatusRequestSeed{};

struct AllocationInstructionSeed {
    std::string_view alloc_id = "ALLOC-1";
    char alloc_trans_type = '0';
    char side = '1';
    std::string_view symbol = "MSFT";
    std::string_view quantity = "500";
    std::string_view avg_px = "99.75";
};
inline constexpr AllocationInstructionSeed kAllocationInstructionSeed{};

struct AllocationInstructionAckSeed {
    std::string_view alloc_id = "ALLOC-2";
    std::int64_t alloc_status = 0;
    std::string_view transact_time = "20240104-08:00:00";
};
inline constexpr AllocationInstructionAckSeed kAllocationInstructionAckSeed{};

// ── 5 group-bearing messages (R5/G6 mandatory insurance) ───────────────────

// E NewOrderList: ListOrdGrp NoOrders(73) REQUIRED (FIX44.xml:2944);
// order-level Parties NoPartyIDs(453) OPTIONAL (FIX44.xml:2508); entry-level
// PtysSubGrp NoPartySubIDs(802) OPTIONAL (FIX44.xml:3701). 3-level nest.
struct NewOrderListPartySubIdSeed {
    std::string_view party_sub_id = "SUB1";
    std::int64_t party_sub_id_type = 1;
};
struct NewOrderListPartySeed {
    std::string_view party_id = "PARTY1";
    char party_id_source = 'D';
    std::int64_t party_role = 1;
    NewOrderListPartySubIdSeed sub{};  // one instance
};
struct NewOrderListOrderSeed {
    std::string_view cl_ord_id = "ORD1";
    std::int64_t list_seq_no = 1;
    char side = '1';
    std::string_view symbol = "MSFT";
    std::string_view order_qty = "150.75";
    NewOrderListPartySeed party{};  // one instance
};
struct NewOrderListSeed {
    std::string_view list_id = "LIST-100";
    std::int64_t bid_type = 3;
    std::int64_t tot_no_orders = 1;
    NewOrderListOrderSeed order{};  // one instance (NoOrders required, size()>0)
};
inline constexpr NewOrderListSeed kNewOrderListSeed{};

// AS AllocationReport: Parties NoPartyIDs(453) OPTIONAL; nested
// NoPartySubIDs(802) OPTIONAL. 2-level nest.
struct AllocationReportPartySubIdSeed {
    std::string_view party_sub_id = "SUB2";
    std::int64_t party_sub_id_type = 1;
};
struct AllocationReportPartySeed {
    std::string_view party_id = "PARTY2";
    char party_id_source = 'D';
    std::int64_t party_role = 1;
    AllocationReportPartySubIdSeed sub{};
};
struct AllocationReportSeed {
    std::string_view alloc_report_id = "ALLOCRPT-100";
    char alloc_trans_type = '0';
    std::int64_t alloc_report_type = 9;
    std::int64_t alloc_status = 0;
    char side = '1';
    std::string_view quantity = "1000";
    std::string_view avg_px = "25.5";
    std::string_view trade_date = "20240101";
    std::string_view symbol = "MSFT";
    AllocationReportPartySeed party{};
};
inline constexpr AllocationReportSeed kAllocationReportSeed{};

// W MarketDataSnapshotFullRefresh: MDFullGrp NoMDEntries(268) REQUIRED,
// delimiter MDEntryType(269) (FIX44.xml:3022-3024) — RC#1 pin, W half.
struct MarketDataEntrySeedW {
    char md_entry_type = '0';           // Bid
    std::string_view md_entry_px = "99.5";
    std::string_view md_entry_size = "100";
};
struct MarketDataSnapshotFullRefreshSeed {
    std::string_view symbol = "MSFT";
    std::string_view md_req_id = "MDR-W1";
    MarketDataEntrySeedW entry{};  // one instance (NoMDEntries required)
};
inline constexpr MarketDataSnapshotFullRefreshSeed kMarketDataSnapshotFullRefreshSeed{};

// X MarketDataIncrementalRefresh: MDIncGrp NoMDEntries(268) REQUIRED,
// delimiter MDUpdateAction(279) (FIX44.xml:3059-3061) — RC#1 pin, X half.
// SAME no_tag(268) as W, DIFFERENT delimiter/member set (the discriminator).
struct MarketDataEntrySeedX {
    char md_update_action = '0';        // New
    char md_entry_type = '0';           // optional in X, set anyway to differ from W's px
    std::string_view md_entry_px = "100.25";
};
struct MarketDataIncrementalRefreshSeed {
    std::string_view md_req_id = "MDR-X1";
    MarketDataEntrySeedX entry{};  // one instance (NoMDEntries required)
};
inline constexpr MarketDataIncrementalRefreshSeed kMarketDataIncrementalRefreshSeed{};

// i MassQuote: QuotSetGrp NoQuoteSets(296) REQUIRED, delimiter
// QuoteSetID(302); nested QuotEntryGrp NoQuoteEntries(295) REQUIRED,
// delimiter QuoteEntryID(299) (FIX44.xml:3219-3221,3350-3352). 2-level nest,
// BOTH levels required (deep-nested insurance, research R5).
struct MassQuoteEntrySeed {
    std::string_view quote_entry_id = "QE1";
    std::string_view bid_px = "10.5";
    std::string_view offer_px = "10.75";
};
struct MassQuoteSetSeed {
    std::string_view quote_set_id = "QS1";
    std::int64_t tot_no_quote_entries = 1;
    MassQuoteEntrySeed entry{};  // one instance (NoQuoteEntries required)
};
struct MassQuoteSeed {
    std::string_view quote_id = "QID-100";
    MassQuoteSetSeed set{};  // one instance (NoQuoteSets required)
};
inline constexpr MassQuoteSeed kMassQuoteSeed{};

}  // namespace fixpp_test_support::seeds067
