// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_exemplar_read.cpp
//
// 061-typed-app-messages (061-slim) US3 / T022 — independent inbound-read
// witnesses (FR-007). These parse HAND-AUTHORED wire bodies (literal string
// constants, NOT produced by the fixpp::session builders under test in
// test_exemplar_roundtrip.cpp) so the read path (dict-aware Parser<Index> +
// generated v44 flyweight accessors) is cross-checked independently of the
// write path. Every assertion is exact-value discriminating (not
// has_value()-only).
//
// Field tags/accessor names verified directly against the generated
// build/<preset>/_codegen/include/fixpp/v44/Messages.hpp (NOT assumed):
//   D  (NewOrderSingle):    11,38,40,44,54,55,60
//   8  (ExecutionReport):   6,14,17,37,39,54,55,150,151
//   9  (OrderCancelReject): 11,37,39,41,102,434
//   E  (NewOrderList):      66,68,73(group)->11,67,453(group)->448,447,452,
//                           802(group)->523,803; then 55,54,38; then 394
//   AS (AllocationReport):  6,53,54,55,71,75,87,453(group)->448,447,452,
//                           802(group)->523,803; then 755,794,857
// Field layout in each hand-authored body mirrors the dictionary-order shown
// in the QuickFIX-authored external goldens (tests/session/golden/*.fix) —
// read as reference only; the SOURCE of each body below is a literal string,
// independent of both the builders and the golden-diff mechanism.
//
// Anchors: specs/061-typed-app-messages/data-model.md §5 (FR-007);
//          contracts/builder-shape-oracle.md; tests/support/
//          app_message_read_scaffold.hpp; tests/session/test_exemplar_
//          roundtrip.cpp (proven dict-aware read pattern + accessor names).

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/v44/Messages.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/app_message_read_scaffold.hpp"

namespace {

using fixpp::decimal_t;

// Parse a decimal_t from an ASCII literal (test_exemplar_roundtrip.cpp
// precedent).
decimal_t make_decimal(std::string_view sv, std::pmr::memory_resource* mr) {
    auto bytes =
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
    auto r = decimal_t::parse(bytes, mr);
    EXPECT_TRUE(r.has_value()) << "make_decimal failed for: " << sv;
    return r.value_or(decimal_t{});
}

}  // namespace

// ── D (NewOrderSingle) — group-free ───────────────────────────────────────
TEST(ExemplarReadD, ReadD_Accessors) {
    std::pmr::monotonic_buffer_resource arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&arena);
    fixpp::dict::table_view tv = dict.as_table_view();

    // Hand-authored body (literal, NOT builder output): distinct values from
    // both the golden and the builder-driven roundtrip seed.
    std::string body =
        "35=D\x01"
        "11=RD-CLORD1\x01"
        "38=250.25\x01"
        "40=2\x01"
        "44=99.99\x01"
        "54=2\x01"
        "55=AAPL\x01"
        "60=20250101-08:30:00\x01";

    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &arena);

    fixpp::v44::NewOrderSingle nos{mv};

    auto cl_ord_id = nos.cl_ord_id();
    ASSERT_TRUE(cl_ord_id.has_value());
    EXPECT_EQ(*cl_ord_id, "RD-CLORD1");

    auto ord_type = nos.ord_type();
    ASSERT_TRUE(ord_type.has_value());
    EXPECT_EQ(*ord_type, '2');

    auto order_qty = nos.order_qty(&arena);
    ASSERT_TRUE(order_qty.has_value());
    EXPECT_EQ(*order_qty, make_decimal("250.25", &arena));

    auto price = nos.price(&arena);
    ASSERT_TRUE(price.has_value());
    EXPECT_EQ(*price, make_decimal("99.99", &arena));

    auto side = nos.side();
    ASSERT_TRUE(side.has_value());
    EXPECT_EQ(*side, '2');

    auto symbol = nos.symbol();
    ASSERT_TRUE(symbol.has_value());
    EXPECT_EQ(*symbol, "AAPL");

    auto transact_time = nos.transact_time();
    ASSERT_TRUE(transact_time.has_value());
    EXPECT_EQ(*transact_time, "20250101-08:30:00");
}

// ── 8 (ExecutionReport) — group-free ──────────────────────────────────────
TEST(ExemplarRead8, Read8_Accessors) {
    std::pmr::monotonic_buffer_resource arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&arena);
    fixpp::dict::table_view tv = dict.as_table_view();

    std::string body =
        "35=8\x01"
        "6=0\x01"
        "14=0\x01"
        "17=XR-EXEC-9\x01"
        "37=XR-ORDID-9\x01"
        "39=0\x01"
        "54=1\x01"
        "55=GOOG\x01"
        "150=0\x01"
        "151=500\x01";

    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &arena);

    fixpp::v44::ExecutionReport er{mv};

    auto order_id = er.order_id();
    ASSERT_TRUE(order_id.has_value());
    EXPECT_EQ(*order_id, "XR-ORDID-9");

    auto exec_id = er.exec_id();
    ASSERT_TRUE(exec_id.has_value());
    EXPECT_EQ(*exec_id, "XR-EXEC-9");

    auto exec_type = er.exec_type();
    ASSERT_TRUE(exec_type.has_value());
    EXPECT_EQ(*exec_type, '0');

    auto ord_status = er.ord_status();
    ASSERT_TRUE(ord_status.has_value());
    EXPECT_EQ(*ord_status, '0');

    auto symbol = er.symbol();
    ASSERT_TRUE(symbol.has_value());
    EXPECT_EQ(*symbol, "GOOG");

    auto side = er.side();
    ASSERT_TRUE(side.has_value());
    EXPECT_EQ(*side, '1');

    auto leaves_qty = er.leaves_qty(&arena);
    ASSERT_TRUE(leaves_qty.has_value());
    EXPECT_EQ(*leaves_qty, make_decimal("500", &arena));

    auto cum_qty = er.cum_qty(&arena);
    ASSERT_TRUE(cum_qty.has_value());
    EXPECT_EQ(*cum_qty, make_decimal("0", &arena));

    auto avg_px = er.avg_px(&arena);
    ASSERT_TRUE(avg_px.has_value());
    EXPECT_EQ(*avg_px, make_decimal("0", &arena));
}

// ── 9 (OrderCancelReject) — group-free ────────────────────────────────────
TEST(ExemplarRead9, Read9_GroupFree) {
    std::pmr::monotonic_buffer_resource arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&arena);
    fixpp::dict::table_view tv = dict.as_table_view();

    std::string body =
        "35=9\x01"
        "11=RJ-CLORD-NEW\x01"
        "37=RJ-ORDID-1\x01"
        "39=8\x01"
        "41=RJ-CLORD-ORIG\x01"
        "102=1\x01"
        "434=2\x01";

    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &arena);

    fixpp::v44::OrderCancelReject ocr{mv};

    auto order_id = ocr.order_id();
    ASSERT_TRUE(order_id.has_value());
    EXPECT_EQ(*order_id, "RJ-ORDID-1");

    auto cl_ord_id = ocr.cl_ord_id();
    ASSERT_TRUE(cl_ord_id.has_value());
    EXPECT_EQ(*cl_ord_id, "RJ-CLORD-NEW");

    auto orig_cl_ord_id = ocr.orig_cl_ord_id();
    ASSERT_TRUE(orig_cl_ord_id.has_value());
    EXPECT_EQ(*orig_cl_ord_id, "RJ-CLORD-ORIG");

    auto ord_status = ocr.ord_status();
    ASSERT_TRUE(ord_status.has_value());
    EXPECT_EQ(*ord_status, '8');

    auto cxl_rej_response_to = ocr.cxl_rej_response_to();
    ASSERT_TRUE(cxl_rej_response_to.has_value());
    EXPECT_EQ(*cxl_rej_response_to, '2');

    auto cxl_rej_reason = ocr.cxl_rej_reason();
    ASSERT_TRUE(cxl_rej_reason.has_value());
    EXPECT_EQ(*cxl_rej_reason, 1);
}

// ── E (NewOrderList) — 3-level nested (73->453->802) + count-of-zero ─────
TEST(ExemplarReadE, ReadE_ThreeLevelNested) {
    std::pmr::monotonic_buffer_resource arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&arena);
    fixpp::dict::table_view tv = dict.as_table_view();

    std::string body =
        "35=E\x01"
        "66=RDLIST9\x01"
        "68=2\x01"
        "73=2\x01"
        "11=RD-ORD-A\x01"
        "67=1\x01"
        "453=1\x01"
        "448=RD-PARTY-A\x01"
        "447=D\x01"
        "452=3\x01"
        "802=1\x01"
        "523=RD-SUB-A\x01"
        "803=2\x01"
        "55=TSLA\x01"
        "54=1\x01"
        "38=75\x01"
        "11=RD-ORD-B\x01"
        "67=2\x01"
        "453=0\x01"
        "55=NFLX\x01"
        "54=2\x01"
        "38=40\x01"
        "394=3\x01";

    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &arena);

    fixpp::v44::NewOrderList nol{mv};

    auto list_id = nol.list_id();
    ASSERT_TRUE(list_id.has_value());
    EXPECT_EQ(*list_id, "RDLIST9");

    auto tot_no_orders = nol.tot_no_orders();
    ASSERT_TRUE(tot_no_orders.has_value());
    EXPECT_EQ(*tot_no_orders, 2);

    auto bid_type = nol.bid_type();
    ASSERT_TRUE(bid_type.has_value());
    EXPECT_EQ(*bid_type, 3);

    auto orders = nol.orders();
    ASSERT_EQ(orders.size(), 2U);

    // Order 1: carries the 3-level 73->453->802 nested chain.
    {
        auto order0 = orders[0];
        auto cl_ord_id = order0.cl_ord_id();
        ASSERT_TRUE(cl_ord_id.has_value());
        EXPECT_EQ(*cl_ord_id, "RD-ORD-A");

        auto list_seq_no = order0.list_seq_no();
        ASSERT_TRUE(list_seq_no.has_value());
        EXPECT_EQ(*list_seq_no, 1);

        auto side = order0.side();
        ASSERT_TRUE(side.has_value());
        EXPECT_EQ(*side, '1');

        auto symbol = order0.symbol();
        ASSERT_TRUE(symbol.has_value());
        EXPECT_EQ(*symbol, "TSLA");

        auto order_qty = order0.order_qty(&arena);
        ASSERT_TRUE(order_qty.has_value());
        EXPECT_EQ(*order_qty, make_decimal("75", &arena));

        auto parties = order0.party_i_ds();
        ASSERT_EQ(parties.size(), 1U);

        auto party0 = parties[0];
        auto party_id = party0.party_id();
        ASSERT_TRUE(party_id.has_value());
        EXPECT_EQ(*party_id, "RD-PARTY-A");

        auto party_id_source = party0.party_id_source();
        ASSERT_TRUE(party_id_source.has_value());
        EXPECT_EQ(*party_id_source, 'D');

        auto party_role = party0.party_role();
        ASSERT_TRUE(party_role.has_value());
        EXPECT_EQ(*party_role, 3);

        auto subs = party0.party_sub_i_ds();
        ASSERT_EQ(subs.size(), 1U);

        auto sub0 = subs[0];
        auto party_sub_id = sub0.party_sub_id();
        ASSERT_TRUE(party_sub_id.has_value());
        EXPECT_EQ(*party_sub_id, "RD-SUB-A");

        auto party_sub_id_type = sub0.party_sub_id_type();
        ASSERT_TRUE(party_sub_id_type.has_value());
        EXPECT_EQ(*party_sub_id_type, 2);
    }

    // Order 2: count-of-zero NoPartyIDs (present-but-empty, SC-002/003).
    {
        auto order1 = orders[1];
        auto cl_ord_id = order1.cl_ord_id();
        ASSERT_TRUE(cl_ord_id.has_value());
        EXPECT_EQ(*cl_ord_id, "RD-ORD-B");

        auto list_seq_no = order1.list_seq_no();
        ASSERT_TRUE(list_seq_no.has_value());
        EXPECT_EQ(*list_seq_no, 2);

        auto side = order1.side();
        ASSERT_TRUE(side.has_value());
        EXPECT_EQ(*side, '2');

        auto symbol = order1.symbol();
        ASSERT_TRUE(symbol.has_value());
        EXPECT_EQ(*symbol, "NFLX");

        auto order_qty = order1.order_qty(&arena);
        ASSERT_TRUE(order_qty.has_value());
        EXPECT_EQ(*order_qty, make_decimal("40", &arena));

        auto parties = order1.party_i_ds();
        EXPECT_EQ(parties.size(), 0U);
    }
}

// Missing-required-field -> fail-closed typed error (FR-007). ListID(66) is
// required='Y' for NewOrderList (dictionaries/FIX44.xml:405). Omitting it
// from the wire and reading it back via the flyweight accessor must yield a
// typed error, not a crash or a silently-defaulted value. Verified against
// the real flyweight/parser behavior (get() on a missing tag ->
// err_required_field_missing -> core::error::wire_required_field_missing,
// include/fixpp/wire/parser.hpp:453-475) rather than assumed.
TEST(ExemplarReadE, ReadE_MissingRequired_TypedError) {
    std::pmr::monotonic_buffer_resource arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&arena);
    fixpp::dict::table_view tv = dict.as_table_view();

    // Same as ReadE_ThreeLevelNested's body but with the required ListID(66)
    // field OMITTED entirely.
    std::string body =
        "35=E\x01"
        "68=2\x01"
        "73=2\x01"
        "11=RD-ORD-A\x01"
        "67=1\x01"
        "453=1\x01"
        "448=RD-PARTY-A\x01"
        "447=D\x01"
        "452=3\x01"
        "802=1\x01"
        "523=RD-SUB-A\x01"
        "803=2\x01"
        "55=TSLA\x01"
        "54=1\x01"
        "38=75\x01"
        "11=RD-ORD-B\x01"
        "67=2\x01"
        "453=0\x01"
        "55=NFLX\x01"
        "54=2\x01"
        "38=40\x01"
        "394=3\x01";

    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &arena);

    fixpp::v44::NewOrderList nol{mv};

    auto list_id = nol.list_id();
    ASSERT_FALSE(list_id.has_value());
    EXPECT_EQ(list_id.error(), fixpp::core::error::wire_required_field_missing);

    // The rest of the frame parsed fine and remains readable -- the missing
    // required field fails closed at the SPECIFIC accessor, not the whole
    // message.
    auto tot_no_orders = nol.tot_no_orders();
    ASSERT_TRUE(tot_no_orders.has_value());
    EXPECT_EQ(*tot_no_orders, 2);
}

// ── AS (AllocationReport) — 2-level nested (453->802), multi-char MsgType ─
TEST(ExemplarReadAS, ReadAS_TwoLevelNested) {
    std::pmr::monotonic_buffer_resource arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&arena);
    fixpp::dict::table_view tv = dict.as_table_view();

    std::string body =
        "35=AS\x01"
        "6=12.75\x01"
        "53=333.5\x01"
        "54=2\x01"
        "55=AMZN\x01"
        "71=0\x01"
        "75=20250315\x01"
        "87=1\x01"
        "453=1\x01"
        "448=RD-PARTY-X\x01"
        "447=C\x01"
        "452=7\x01"
        "802=1\x01"
        "523=RD-SUB-X\x01"
        "803=4\x01"
        "755=RD-ALLOC-1\x01"
        "794=3\x01"
        "857=1\x01";

    std::vector<std::byte> frame = fixpp_test_support::make_frame("FIX.4.4", body);
    auto mv = fixpp_test_support::parse_dict(frame, tv, &arena);

    fixpp::v44::AllocationReport alloc{mv};

    auto msg_type = alloc.msg_type();
    ASSERT_TRUE(msg_type.has_value());
    EXPECT_EQ(*msg_type, "AS");

    auto alloc_report_id = alloc.alloc_report_id();
    ASSERT_TRUE(alloc_report_id.has_value());
    EXPECT_EQ(*alloc_report_id, "RD-ALLOC-1");

    auto alloc_trans_type = alloc.alloc_trans_type();
    ASSERT_TRUE(alloc_trans_type.has_value());
    EXPECT_EQ(*alloc_trans_type, '0');

    auto alloc_report_type = alloc.alloc_report_type();
    ASSERT_TRUE(alloc_report_type.has_value());
    EXPECT_EQ(*alloc_report_type, 3);

    auto alloc_status = alloc.alloc_status();
    ASSERT_TRUE(alloc_status.has_value());
    EXPECT_EQ(*alloc_status, 1);

    auto alloc_no_orders_type = alloc.alloc_no_orders_type();
    ASSERT_TRUE(alloc_no_orders_type.has_value());
    EXPECT_EQ(*alloc_no_orders_type, 1);

    auto side = alloc.side();
    ASSERT_TRUE(side.has_value());
    EXPECT_EQ(*side, '2');

    auto quantity = alloc.quantity(&arena);
    ASSERT_TRUE(quantity.has_value());
    EXPECT_EQ(*quantity, make_decimal("333.5", &arena));

    auto avg_px = alloc.avg_px(&arena);
    ASSERT_TRUE(avg_px.has_value());
    EXPECT_EQ(*avg_px, make_decimal("12.75", &arena));

    auto trade_date = alloc.trade_date();
    ASSERT_TRUE(trade_date.has_value());
    EXPECT_EQ(*trade_date, "20250315");

    auto symbol = alloc.symbol();
    ASSERT_TRUE(symbol.has_value());
    EXPECT_EQ(*symbol, "AMZN");

    // The discriminating 2-level chain: 453 -> 802.
    auto parties = alloc.party_i_ds();
    ASSERT_EQ(parties.size(), 1U);

    auto party0 = parties[0];
    auto party_id = party0.party_id();
    ASSERT_TRUE(party_id.has_value());
    EXPECT_EQ(*party_id, "RD-PARTY-X");

    auto party_id_source = party0.party_id_source();
    ASSERT_TRUE(party_id_source.has_value());
    EXPECT_EQ(*party_id_source, 'C');

    auto party_role = party0.party_role();
    ASSERT_TRUE(party_role.has_value());
    EXPECT_EQ(*party_role, 7);

    auto subs = party0.party_sub_i_ds();
    ASSERT_EQ(subs.size(), 1U);

    auto sub0 = subs[0];
    auto party_sub_id = sub0.party_sub_id();
    ASSERT_TRUE(party_sub_id.has_value());
    EXPECT_EQ(*party_sub_id, "RD-SUB-X");

    auto party_sub_id_type = sub0.party_sub_id_type();
    ASSERT_TRUE(party_sub_id_type.has_value());
    EXPECT_EQ(*party_sub_id_type, 4);
}
