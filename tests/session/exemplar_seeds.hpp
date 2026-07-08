// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/exemplar_seeds.hpp
//
// 061-typed-app-messages (061-slim) T019 (E/9/AS rows) — ExemplarSeed-style
// seed record (data-model.md §3.2): typed scalars + nested group-shape
// description sufficient to (a) drive the builder call, (b) assert exact
// read-back values, (c) diff against the golden. The D/8 rows land alongside
// their own builder tasks (T013/T014).
//
// Decimal fields are stored as raw ASCII (matches the QuickFIX-authored
// golden's literal decimal text, tests/session/golden/new_order_list.fix /
// PROVENANCE.md); the consuming test converts to fixpp::decimal_t via
// decimal_t::parse() at runtime (decimal_t is not a constexpr-constructible
// literal type from a floating literal — mirrors
// tests/wire/test_body_builder.cpp's make_decimal() helper).
//
// Anchors: specs/061-typed-app-messages/data-model.md §3.1 E / §3.2;
//          tests/session/golden/new_order_list.fix + PROVENANCE.md.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace fixpp_test_support {

struct NewOrderListPartySubIdSeed {
    std::string_view party_sub_id;
    std::int64_t party_sub_id_type;
};

struct NewOrderListPartySeed {
    std::string_view party_id;
    char party_id_source;
    std::int64_t party_role;
    std::span<const NewOrderListPartySubIdSeed> sub_ids;
};

struct NewOrderListOrderSeed {
    std::string_view cl_ord_id;
    std::int64_t list_seq_no;
    char side;
    std::string_view symbol;
    std::string_view order_qty;  // raw ASCII decimal literal
    std::span<const NewOrderListPartySeed> parties;  // empty -> NoPartyIDs=0
};

// ── E (NewOrderList) seed — matches tests/session/golden/new_order_list.fix ──
// order 1 (ORD1): carries the nested 453->802 party chain.
inline constexpr std::array<NewOrderListPartySubIdSeed, 1> kNewOrderListOrd1Subs{{
    NewOrderListPartySubIdSeed{"SUB1", 1},
}};

inline constexpr std::array<NewOrderListPartySeed, 1> kNewOrderListOrd1Parties{{
    NewOrderListPartySeed{"PARTY1", 'D', 1, std::span<const NewOrderListPartySubIdSeed>{kNewOrderListOrd1Subs}},
}};

// order 2 (ORD2): NoPartyIDs=0 (present-but-empty) -> empty parties span.
inline constexpr std::array<NewOrderListPartySeed, 0> kNewOrderListOrd2Parties{};

inline constexpr std::array<NewOrderListOrderSeed, 2> kNewOrderListOrders{{
    NewOrderListOrderSeed{"ORD1", 1, '1', "MSFT", "150.75",
                          std::span<const NewOrderListPartySeed>{kNewOrderListOrd1Parties}},
    NewOrderListOrderSeed{"ORD2", 2, '2', "IBM", "50",
                          std::span<const NewOrderListPartySeed>{kNewOrderListOrd2Parties}},
}};

struct NewOrderListSeed {
    std::string_view msg_type = "E";
    std::string_view begin_string = "FIX.4.4";
    std::string_view list_id = "LIST1";
    std::int64_t bid_type = 3;
    std::int64_t tot_no_orders = 2;
    std::span<const NewOrderListOrderSeed> orders =
        std::span<const NewOrderListOrderSeed>{kNewOrderListOrders};
    std::string_view golden_path = "tests/session/golden/new_order_list.fix";
};

inline constexpr NewOrderListSeed kNewOrderListSeed{};

// ── 9 (OrderCancelReject) seed — matches tests/session/golden/order_cancel_reject.fix ──
// Group-free: no nested group-shape needed.
struct OrderCancelRejectSeed {
    std::string_view msg_type = "9";
    std::string_view begin_string = "FIX.4.4";
    std::string_view order_id = "ORDER1";
    std::string_view cl_ord_id = "CLORD2";
    std::string_view orig_cl_ord_id = "CLORD1";
    char ord_status = '8';           // Rejected
    char cxl_rej_response_to = '1';  // OrderCancelRequest
    std::int64_t cxl_rej_reason = 0;
    std::string_view golden_path = "tests/session/golden/order_cancel_reject.fix";
};

inline constexpr OrderCancelRejectSeed kOrderCancelRejectSeed{};

// ── AS (AllocationReport) seed — matches tests/session/golden/allocation_report.fix ──
// One top-level 453->802 party chain (2-level nested).
struct AllocationReportPartySubIdSeed {
    std::string_view party_sub_id;
    std::int64_t party_sub_id_type;
};

struct AllocationReportPartySeed {
    std::string_view party_id;
    char party_id_source;
    std::int64_t party_role;
    std::span<const AllocationReportPartySubIdSeed> sub_ids;
};

inline constexpr std::array<AllocationReportPartySubIdSeed, 1> kAllocationReportSubs{{
    AllocationReportPartySubIdSeed{"SUB1", 1},
}};

inline constexpr std::array<AllocationReportPartySeed, 1> kAllocationReportParties{{
    AllocationReportPartySeed{"PARTY1", 'D', 1,
                              std::span<const AllocationReportPartySubIdSeed>{kAllocationReportSubs}},
}};

struct AllocationReportSeed {
    std::string_view msg_type = "AS";
    std::string_view begin_string = "FIX.4.4";
    std::string_view alloc_report_id = "ALLOCRPT1";
    char alloc_trans_type = '0';       // New
    std::int64_t alloc_report_type = 9;  // Accept
    std::int64_t alloc_status = 0;       // Accepted
    std::int64_t alloc_no_orders_type = 0;  // NotSpecified (data-model §3.1 AS note)
    char side = '1';                   // Buy
    std::string_view quantity = "1000";  // raw ASCII decimal literal
    std::string_view avg_px = "25.5";    // raw ASCII decimal literal
    std::string_view trade_date = "20240101";
    std::string_view symbol = "MSFT";
    std::span<const AllocationReportPartySeed> parties =
        std::span<const AllocationReportPartySeed>{kAllocationReportParties};
    std::string_view golden_path = "tests/session/golden/allocation_report.fix";
};

inline constexpr AllocationReportSeed kAllocationReportSeed{};

// ── D (NewOrderSingle) seed — matches tests/session/golden/new_order_single.fix ──
// Group-free. Same seed values as the pre-existing 020
// AS1_NOS_HappyPath_ParseBack test (PROVENANCE.md).
struct NewOrderSingleSeed {
    std::string_view msg_type = "D";
    std::string_view begin_string = "FIX.4.4";
    std::string_view cl_ord_id = "ORD-001";
    std::string_view symbol = "MSFT";
    char side = '1';  // Buy
    std::string_view order_qty = "100";  // raw ASCII decimal literal
    std::string_view price = "190.5";    // raw ASCII decimal literal
    std::string_view transact_time = "20240101-10:00:00";
    std::string_view golden_path = "tests/session/golden/new_order_single.fix";
};

inline constexpr NewOrderSingleSeed kNewOrderSingleSeed{};

// ── 8 (ExecutionReport) seed — matches tests/session/golden/execution_report.fix ──
// Group-free. Same seed values as the pre-existing 020
// AS2_ExecRpt_HappyPath_ParseBack test (PROVENANCE.md).
struct ExecutionReportSeed {
    std::string_view msg_type = "8";
    std::string_view begin_string = "FIX.4.4";
    std::string_view order_id = "ORD-XCH-001";
    std::string_view exec_id = "EXEC-001";
    char exec_type = 'F';   // Trade
    char ord_status = '2';  // Filled
    std::string_view symbol = "MSFT";
    char side = '1';                    // Buy
    std::string_view leaves_qty = "0";  // raw ASCII decimal literal
    std::string_view cum_qty = "100";   // raw ASCII decimal literal
    std::string_view avg_px = "190.5";  // raw ASCII decimal literal
    std::string_view golden_path = "tests/session/golden/execution_report.fix";
};

inline constexpr ExecutionReportSeed kExecutionReportSeed{};

}  // namespace fixpp_test_support
