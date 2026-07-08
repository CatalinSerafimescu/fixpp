// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_exemplar_roundtrip.cpp
//
// 061-typed-app-messages (061-slim) T020 (E rows) — the pivotal grouped/nested
// exemplar's round-trip + external-golden witness (contracts/builder-shape-
// oracle.md C5; data-model.md §4). Per the plan's "E-first vertical slice":
// T010 (E builder) -> T017 (E golden) -> this file's E rows -> STOP and
// VALIDATE before fanning out D/8/9/AS.
//
// Three legs, all discriminating (exact seeded values, not has_value()):
//   1. build_new_order_list(seed) -> byte-matches the QuickFIX-authored
//      external golden (tests/session/golden/new_order_list.fix) via
//      shape_oracle_profile() (framing tags only excluded).
//   2. Round-trip: builder output -> framed -> dict-aware parse ->
//      fixpp::v44::NewOrderList flyweight -> every seeded field reads back
//      exact, INCLUDING the 3-level 73->453->802 chain and the count-of-zero
//      NoPartyIDs=0 order (SC-002/003).
//   3. Byte-exact canonical decimal (FR-004/C2): the raw `38=150.75\x01`
//      bytes of order 1's OrderQty appear verbatim in the built body,
//      independent of the by-value golden diff and by-value parse compare.
//
// Anchors: specs/061-typed-app-messages/data-model.md §3.1 E / §4;
//          contracts/builder-shape-oracle.md C1/C3/C5;
//          tests/session/golden/new_order_list.fix + PROVENANCE.md.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/session/business_messages.hpp>
#include <fixpp/v44/Messages.hpp>
#include <fstream>
#include <memory_resource>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "exemplar_seeds.hpp"
#include "support/app_message_read_scaffold.hpp"
#include "support/golden_diff.hpp"

namespace {

using fixpp::decimal_t;
using fixpp::interop::diff_transcripts;
using fixpp::interop::GoldenFrame;
using fixpp::interop::parse_golden;
using fixpp::interop::shape_oracle_profile;
using fixpp::session::AllocationReportParams;
using fixpp::session::AllocationReportParty;
using fixpp::session::AllocationReportPartySubId;
using fixpp::session::NewOrderListOrder;
using fixpp::session::NewOrderListParams;
using fixpp::session::NewOrderListParty;
using fixpp::session::NewOrderListPartySubId;

// Parse a decimal_t from an ASCII literal (tests/wire/test_body_builder.cpp
// make_decimal() precedent).
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

std::string read_file(const std::string& path) {
    std::ifstream in{path, std::ios::binary};
    EXPECT_TRUE(in.good()) << "failed to open: " << path;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Build the fixpp::session::NewOrderListParams for the E seed
// (tests/session/exemplar_seeds.hpp::kNewOrderListSeed), converting the raw
// ASCII decimal literals into decimal_t via the supplied arena.
struct BuiltParams {
    std::vector<NewOrderListPartySubId> ord1_subs;
    std::vector<NewOrderListParty> ord1_parties;
    std::vector<NewOrderListParty> ord2_parties;  // empty -> NoPartyIDs=0
    std::vector<NewOrderListOrder> orders;
    NewOrderListParams params;
};

BuiltParams build_params_from_seed(std::pmr::memory_resource* mr) {
    const auto& seed = fixpp_test_support::kNewOrderListSeed;
    static_assert(fixpp_test_support::kNewOrderListOrders.size() == 2U);

    BuiltParams built;
    built.ord1_subs.push_back(NewOrderListPartySubId{
        seed.orders[0].parties[0].sub_ids[0].party_sub_id,
        seed.orders[0].parties[0].sub_ids[0].party_sub_id_type,
    });
    built.ord1_parties.push_back(NewOrderListParty{
        seed.orders[0].parties[0].party_id,
        seed.orders[0].parties[0].party_id_source,
        seed.orders[0].parties[0].party_role,
        std::span<const NewOrderListPartySubId>{built.ord1_subs},
    });

    built.orders.push_back(NewOrderListOrder{
        seed.orders[0].cl_ord_id,
        seed.orders[0].list_seq_no,
        seed.orders[0].side,
        seed.orders[0].symbol,
        make_decimal(seed.orders[0].order_qty, mr),
        std::span<const NewOrderListParty>{built.ord1_parties},
    });
    built.orders.push_back(NewOrderListOrder{
        seed.orders[1].cl_ord_id,
        seed.orders[1].list_seq_no,
        seed.orders[1].side,
        seed.orders[1].symbol,
        make_decimal(seed.orders[1].order_qty, mr),
        std::span<const NewOrderListParty>{built.ord2_parties},  // empty
    });

    built.params = NewOrderListParams{
        seed.list_id,
        seed.bid_type,
        seed.tot_no_orders,
        std::span<const NewOrderListOrder>{built.orders},
    };
    return built;
}

}  // namespace

// ── Leg 1 + 2: golden diff + round-trip read-back ────────────────────────────
TEST(ExemplarRoundtripE, BuildMatchesGoldenAndRoundTripsThreeLevel) {
    std::pmr::monotonic_buffer_resource arena{8192};
    BuiltParams built = build_params_from_seed(&arena);

    std::array<std::byte, 4096> out{};
    auto built_r = fixpp::session::build_new_order_list(std::span<std::byte>{out}, built.params);
    ASSERT_TRUE(built_r.has_value()) << "build_new_order_list failed";
    std::span<const std::byte> body = *built_r;

    // ── Leg 1: external golden diff (C5; shape_oracle_profile excludes only
    // framing tags {8,9,10,34,52} -- every seeded business field is matched
    // verbatim). ────────────────────────────────────────────────────────────
    const auto& seed = fixpp_test_support::kNewOrderListSeed;
    std::string golden_path = std::string(FIXPP_REPO_ROOT) + "/" + std::string(seed.golden_path);
    std::string golden_text = read_file(golden_path);
    std::vector<GoldenFrame> expected = parse_golden(golden_text);
    ASSERT_EQ(expected.size(), 1U);

    std::vector<GoldenFrame> actual{GoldenFrame{
        '>', std::vector<std::byte>{body.begin(), body.end()}}};
    auto diff = diff_transcripts(expected, actual, shape_oracle_profile());
    EXPECT_TRUE(static_cast<bool>(diff)) << "golden diff mismatch: " << diff.detail;

    // ── Leg 2: round-trip read-back, dict-aware, exact values (C5) ─────────
    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();

    std::vector<std::byte> frame =
        fixpp_test_support::make_frame(seed.begin_string, bytes_to_string(body));
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);

    fixpp::v44::NewOrderList nol{mv};

    auto list_id = nol.list_id();
    ASSERT_TRUE(list_id.has_value());
    EXPECT_EQ(*list_id, "LIST1");

    auto bid_type = nol.bid_type();
    ASSERT_TRUE(bid_type.has_value());
    EXPECT_EQ(*bid_type, 3);

    auto tot_no_orders = nol.tot_no_orders();
    ASSERT_TRUE(tot_no_orders.has_value());
    EXPECT_EQ(*tot_no_orders, 2);

    auto orders = nol.orders();
    ASSERT_EQ(orders.size(), 2U);

    // Order 1 (ORD1): carries the 3-level 73->453->802 nested chain.
    {
        auto order0 = orders[0];
        auto cl_ord_id = order0.cl_ord_id();
        ASSERT_TRUE(cl_ord_id.has_value());
        EXPECT_EQ(*cl_ord_id, "ORD1");

        auto list_seq_no = order0.list_seq_no();
        ASSERT_TRUE(list_seq_no.has_value());
        EXPECT_EQ(*list_seq_no, 1);

        auto side = order0.side();
        ASSERT_TRUE(side.has_value());
        EXPECT_EQ(*side, '1');

        auto symbol = order0.symbol();
        ASSERT_TRUE(symbol.has_value());
        EXPECT_EQ(*symbol, "MSFT");

        auto order_qty = order0.order_qty(&read_arena);
        ASSERT_TRUE(order_qty.has_value());
        EXPECT_EQ(*order_qty, make_decimal("150.75", &read_arena));

        auto parties = order0.party_i_ds();
        ASSERT_EQ(parties.size(), 1U);

        auto party0 = parties[0];
        auto party_id = party0.party_id();
        ASSERT_TRUE(party_id.has_value());
        EXPECT_EQ(*party_id, "PARTY1");

        auto party_id_source = party0.party_id_source();
        ASSERT_TRUE(party_id_source.has_value());
        EXPECT_EQ(*party_id_source, 'D');

        auto party_role = party0.party_role();
        ASSERT_TRUE(party_role.has_value());
        EXPECT_EQ(*party_role, 1);

        auto subs = party0.party_sub_i_ds();
        ASSERT_EQ(subs.size(), 1U);

        auto sub0 = subs[0];
        auto party_sub_id = sub0.party_sub_id();
        ASSERT_TRUE(party_sub_id.has_value());
        EXPECT_EQ(*party_sub_id, "SUB1");

        auto party_sub_id_type = sub0.party_sub_id_type();
        ASSERT_TRUE(party_sub_id_type.has_value());
        EXPECT_EQ(*party_sub_id_type, 1);
    }

    // Order 2 (ORD2): count-of-zero NoPartyIDs (SC-002/003) -- present-but-
    // empty, distinct from never-opened.
    {
        auto order1 = orders[1];
        auto cl_ord_id = order1.cl_ord_id();
        ASSERT_TRUE(cl_ord_id.has_value());
        EXPECT_EQ(*cl_ord_id, "ORD2");

        auto list_seq_no = order1.list_seq_no();
        ASSERT_TRUE(list_seq_no.has_value());
        EXPECT_EQ(*list_seq_no, 2);

        auto side = order1.side();
        ASSERT_TRUE(side.has_value());
        EXPECT_EQ(*side, '2');

        auto symbol = order1.symbol();
        ASSERT_TRUE(symbol.has_value());
        EXPECT_EQ(*symbol, "IBM");

        auto order_qty = order1.order_qty(&read_arena);
        ASSERT_TRUE(order_qty.has_value());
        EXPECT_EQ(*order_qty, make_decimal("50", &read_arena));

        // The discriminating count-of-zero assertion: size() == 0 (the group
        // was opened -- 453=0 is present on the wire, see the golden diff
        // above -- but has no entries).
        auto parties = order1.party_i_ds();
        EXPECT_EQ(parties.size(), 0U);
    }
}

// ── Leg 3: byte-exact canonical decimal (FR-004/C2) ──────────────────────────
TEST(ExemplarRoundtripE, ByteExactCanonicalDecimal) {
    std::pmr::monotonic_buffer_resource arena{8192};
    BuiltParams built = build_params_from_seed(&arena);

    std::array<std::byte, 4096> out{};
    auto built_r = fixpp::session::build_new_order_list(std::span<std::byte>{out}, built.params);
    ASSERT_TRUE(built_r.has_value()) << "build_new_order_list failed";

    std::string body = bytes_to_string(*built_r);
    // Order 1's OrderQty(38): raw "38=150.75\x01" bytes, independent of the
    // by-value golden diff and by-value parse compare (both normalize
    // decimals by value, per data-model.md §4).
    std::string expected_field = "38=150.75\x01";
    EXPECT_NE(body.find(expected_field), std::string::npos)
        << "expected canonical bytes '38=150.75\\x01' not found verbatim in body";
}

// ── Exemplar 9 (OrderCancelReject) — group-free ──────────────────────────────
// T011/T018/T019-T021 (9 rows).
TEST(ExemplarRoundtrip9, BuildMatchesGoldenAndRoundTrips) {
    const auto& seed = fixpp_test_support::kOrderCancelRejectSeed;

    std::array<std::byte, 1024> out{};
    auto built_r = fixpp::session::build_order_cancel_reject(
        std::span<std::byte>{out}, seed.order_id, seed.cl_ord_id, seed.orig_cl_ord_id,
        seed.ord_status, seed.cxl_rej_response_to, seed.cxl_rej_reason);
    ASSERT_TRUE(built_r.has_value()) << "build_order_cancel_reject failed";
    std::span<const std::byte> body = *built_r;

    // ── Leg 1: external golden diff ─────────────────────────────────────────
    std::string golden_path = std::string(FIXPP_REPO_ROOT) + "/" + std::string(seed.golden_path);
    std::string golden_text = read_file(golden_path);
    std::vector<GoldenFrame> expected = parse_golden(golden_text);
    ASSERT_EQ(expected.size(), 1U);

    std::vector<GoldenFrame> actual{GoldenFrame{
        '>', std::vector<std::byte>{body.begin(), body.end()}}};
    auto diff = diff_transcripts(expected, actual, shape_oracle_profile());
    EXPECT_TRUE(static_cast<bool>(diff)) << "golden diff mismatch: " << diff.detail;

    // ── Leg 2: round-trip read-back, dict-aware, exact values ───────────────
    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();

    std::vector<std::byte> frame =
        fixpp_test_support::make_frame(seed.begin_string, bytes_to_string(body));
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);

    fixpp::v44::OrderCancelReject ocr{mv};

    auto order_id = ocr.order_id();
    ASSERT_TRUE(order_id.has_value());
    EXPECT_EQ(*order_id, "ORDER1");

    auto cl_ord_id = ocr.cl_ord_id();
    ASSERT_TRUE(cl_ord_id.has_value());
    EXPECT_EQ(*cl_ord_id, "CLORD2");

    auto orig_cl_ord_id = ocr.orig_cl_ord_id();
    ASSERT_TRUE(orig_cl_ord_id.has_value());
    EXPECT_EQ(*orig_cl_ord_id, "CLORD1");

    auto ord_status = ocr.ord_status();
    ASSERT_TRUE(ord_status.has_value());
    EXPECT_EQ(*ord_status, '8');

    auto cxl_rej_response_to = ocr.field_value(434);
    ASSERT_TRUE(cxl_rej_response_to.has_value());
    ASSERT_EQ(cxl_rej_response_to->bytes().size(), 1U);
    EXPECT_EQ(static_cast<char>(cxl_rej_response_to->bytes()[0]), '1');

    auto cxl_rej_reason = ocr.field_value(102);
    ASSERT_TRUE(cxl_rej_reason.has_value());
    EXPECT_EQ(bytes_to_string(cxl_rej_reason->bytes()), "0");
}

TEST(ExemplarRoundtrip9, ByteExactIntField) {
    const auto& seed = fixpp_test_support::kOrderCancelRejectSeed;

    std::array<std::byte, 1024> out{};
    auto built_r = fixpp::session::build_order_cancel_reject(
        std::span<std::byte>{out}, seed.order_id, seed.cl_ord_id, seed.orig_cl_ord_id,
        seed.ord_status, seed.cxl_rej_response_to, seed.cxl_rej_reason);
    ASSERT_TRUE(built_r.has_value()) << "build_order_cancel_reject failed";

    std::string body = bytes_to_string(*built_r);
    // CxlRejReason(102) optional int, exercised at 0: raw "102=0\x01" bytes
    // verbatim (9 has no decimal field, so this is the byte-exact witness
    // instead of a canonical-decimal one, per data-model §4's spirit).
    std::string expected_field = "102=0\x01";
    EXPECT_NE(body.find(expected_field), std::string::npos)
        << "expected canonical bytes '102=0\\x01' not found verbatim in body";
}

// ── Exemplar AS (AllocationReport) — multi-char MsgType + 2-level nested ────
// T012/T018/T019-T021 (AS rows).
namespace {

struct BuiltAllocationReportParams {
    std::vector<AllocationReportPartySubId> subs;
    std::vector<AllocationReportParty> parties;
    AllocationReportParams params;
};

BuiltAllocationReportParams build_allocation_report_params_from_seed(std::pmr::memory_resource* mr) {
    const auto& seed = fixpp_test_support::kAllocationReportSeed;

    BuiltAllocationReportParams built;
    built.subs.push_back(AllocationReportPartySubId{
        seed.parties[0].sub_ids[0].party_sub_id,
        seed.parties[0].sub_ids[0].party_sub_id_type,
    });
    built.parties.push_back(AllocationReportParty{
        seed.parties[0].party_id,
        seed.parties[0].party_id_source,
        seed.parties[0].party_role,
        std::span<const AllocationReportPartySubId>{built.subs},
    });

    built.params = AllocationReportParams{
        seed.alloc_report_id,
        seed.alloc_trans_type,
        seed.alloc_report_type,
        seed.alloc_status,
        seed.alloc_no_orders_type,
        seed.side,
        make_decimal(seed.quantity, mr),
        make_decimal(seed.avg_px, mr),
        seed.trade_date,
        seed.symbol,
        std::span<const AllocationReportParty>{built.parties},
    };
    return built;
}

}  // namespace

TEST(ExemplarRoundtripAS, BuildMatchesGoldenAndRoundTripsTwoLevel) {
    std::pmr::monotonic_buffer_resource arena{8192};
    BuiltAllocationReportParams built = build_allocation_report_params_from_seed(&arena);

    std::array<std::byte, 2048> out{};
    auto built_r =
        fixpp::session::build_allocation_report(std::span<std::byte>{out}, built.params);
    ASSERT_TRUE(built_r.has_value()) << "build_allocation_report failed";
    std::span<const std::byte> body = *built_r;

    // ── Leg 1: external golden diff ─────────────────────────────────────────
    const auto& seed = fixpp_test_support::kAllocationReportSeed;
    std::string golden_path = std::string(FIXPP_REPO_ROOT) + "/" + std::string(seed.golden_path);
    std::string golden_text = read_file(golden_path);
    std::vector<GoldenFrame> expected = parse_golden(golden_text);
    ASSERT_EQ(expected.size(), 1U);

    std::vector<GoldenFrame> actual{GoldenFrame{
        '>', std::vector<std::byte>{body.begin(), body.end()}}};
    auto diff = diff_transcripts(expected, actual, shape_oracle_profile());
    EXPECT_TRUE(static_cast<bool>(diff)) << "golden diff mismatch: " << diff.detail;

    // ── Leg 2: round-trip read-back, dict-aware, exact values, incl. the
    // 2-level 453->802 chain (SC-003 AS leg) ─────────────────────────────────
    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();

    std::vector<std::byte> frame =
        fixpp_test_support::make_frame(seed.begin_string, bytes_to_string(body));
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);

    fixpp::v44::AllocationReport alloc{mv};

    // Multi-char MsgType(35) exemplar: "AS" parses/reads back exact (not
    // truncated to 'A' nor confused with a single-digit MsgType).
    auto msg_type = alloc.msg_type();
    ASSERT_TRUE(msg_type.has_value());
    EXPECT_EQ(*msg_type, "AS");

    auto alloc_report_id = alloc.alloc_report_id();
    ASSERT_TRUE(alloc_report_id.has_value());
    EXPECT_EQ(*alloc_report_id, "ALLOCRPT1");

    auto alloc_trans_type = alloc.alloc_trans_type();
    ASSERT_TRUE(alloc_trans_type.has_value());
    EXPECT_EQ(*alloc_trans_type, '0');

    auto alloc_report_type = alloc.alloc_report_type();
    ASSERT_TRUE(alloc_report_type.has_value());
    EXPECT_EQ(*alloc_report_type, 9);

    auto alloc_status = alloc.alloc_status();
    ASSERT_TRUE(alloc_status.has_value());
    EXPECT_EQ(*alloc_status, 0);

    auto alloc_no_orders_type = alloc.alloc_no_orders_type();
    ASSERT_TRUE(alloc_no_orders_type.has_value());
    EXPECT_EQ(*alloc_no_orders_type, 0);

    auto side = alloc.side();
    ASSERT_TRUE(side.has_value());
    EXPECT_EQ(*side, '1');

    auto quantity = alloc.quantity(&read_arena);
    ASSERT_TRUE(quantity.has_value());
    EXPECT_EQ(*quantity, make_decimal("1000", &read_arena));

    auto avg_px = alloc.avg_px(&read_arena);
    ASSERT_TRUE(avg_px.has_value());
    EXPECT_EQ(*avg_px, make_decimal("25.5", &read_arena));

    auto trade_date = alloc.trade_date();
    ASSERT_TRUE(trade_date.has_value());
    EXPECT_EQ(*trade_date, "20240101");

    auto symbol = alloc.symbol();
    ASSERT_TRUE(symbol.has_value());
    EXPECT_EQ(*symbol, "MSFT");

    // The discriminating 2-level read-back assertion (SC-003 AS leg): descend
    // 453 -> 802 and assert the exact seeded leaf value.
    auto parties = alloc.party_i_ds();
    ASSERT_EQ(parties.size(), 1U);

    auto party0 = parties[0];
    auto party_id = party0.party_id();
    ASSERT_TRUE(party_id.has_value());
    EXPECT_EQ(*party_id, "PARTY1");

    auto party_id_source = party0.party_id_source();
    ASSERT_TRUE(party_id_source.has_value());
    EXPECT_EQ(*party_id_source, 'D');

    auto party_role = party0.party_role();
    ASSERT_TRUE(party_role.has_value());
    EXPECT_EQ(*party_role, 1);

    auto subs = party0.party_sub_i_ds();
    ASSERT_EQ(subs.size(), 1U);

    auto sub0 = subs[0];
    auto party_sub_id = sub0.party_sub_id();
    ASSERT_TRUE(party_sub_id.has_value());
    EXPECT_EQ(*party_sub_id, "SUB1");

    auto party_sub_id_type = sub0.party_sub_id_type();
    ASSERT_TRUE(party_sub_id_type.has_value());
    EXPECT_EQ(*party_sub_id_type, 1);
}

TEST(ExemplarRoundtripAS, ByteExactCanonicalDecimal) {
    std::pmr::monotonic_buffer_resource arena{8192};
    BuiltAllocationReportParams built = build_allocation_report_params_from_seed(&arena);

    std::array<std::byte, 2048> out{};
    auto built_r =
        fixpp::session::build_allocation_report(std::span<std::byte>{out}, built.params);
    ASSERT_TRUE(built_r.has_value()) << "build_allocation_report failed";

    std::string body = bytes_to_string(*built_r);
    // AvgPx(6): raw "6=25.5\x01" bytes, independent of the by-value golden
    // diff and by-value parse compare (both normalize decimals by value).
    std::string expected_field = "6=25.5\x01";
    EXPECT_NE(body.find(expected_field), std::string::npos)
        << "expected canonical bytes '6=25.5\\x01' not found verbatim in body";
}

// ── Exemplar D (NewOrderSingle) — group-free (020 refactor, T013) ──────────
TEST(ExemplarRoundtripD, BuildMatchesGoldenAndRoundTrips) {
    std::pmr::monotonic_buffer_resource arena{4096};
    const auto& seed = fixpp_test_support::kNewOrderSingleSeed;
    auto order_qty = make_decimal(seed.order_qty, &arena);
    auto price = make_decimal(seed.price, &arena);

    std::array<std::byte, 1024> out{};
    auto built_r = fixpp::session::build_new_order_single(std::span<std::byte>{out}, seed.cl_ord_id,
                                                           seed.symbol, seed.side, order_qty, price,
                                                           seed.transact_time);
    ASSERT_TRUE(built_r.has_value()) << "build_new_order_single failed";
    std::span<const std::byte> body = *built_r;

    // ── Leg 1: external golden diff ─────────────────────────────────────────
    std::string golden_path = std::string(FIXPP_REPO_ROOT) + "/" + std::string(seed.golden_path);
    std::string golden_text = read_file(golden_path);
    std::vector<GoldenFrame> expected = parse_golden(golden_text);
    ASSERT_EQ(expected.size(), 1U);

    std::vector<GoldenFrame> actual{GoldenFrame{
        '>', std::vector<std::byte>{body.begin(), body.end()}}};
    auto diff = diff_transcripts(expected, actual, shape_oracle_profile());
    EXPECT_TRUE(static_cast<bool>(diff)) << "golden diff mismatch: " << diff.detail;

    // ── Leg 2: round-trip read-back, dict-aware, exact values ───────────────
    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();

    std::vector<std::byte> frame =
        fixpp_test_support::make_frame(seed.begin_string, bytes_to_string(body));
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);

    fixpp::v44::NewOrderSingle nos{mv};

    auto cl_ord_id = nos.cl_ord_id();
    ASSERT_TRUE(cl_ord_id.has_value());
    EXPECT_EQ(*cl_ord_id, "ORD-001");

    auto symbol = nos.symbol();
    ASSERT_TRUE(symbol.has_value());
    EXPECT_EQ(*symbol, "MSFT");

    auto side = nos.side();
    ASSERT_TRUE(side.has_value());
    EXPECT_EQ(*side, '1');

    auto ord_type = nos.ord_type();
    ASSERT_TRUE(ord_type.has_value());
    EXPECT_EQ(*ord_type, '2');

    auto transact_time = nos.transact_time();
    ASSERT_TRUE(transact_time.has_value());
    EXPECT_EQ(*transact_time, "20240101-10:00:00");

    auto read_order_qty = nos.order_qty(&read_arena);
    ASSERT_TRUE(read_order_qty.has_value());
    EXPECT_EQ(*read_order_qty, make_decimal("100", &read_arena));

    auto read_price = nos.price(&read_arena);
    ASSERT_TRUE(read_price.has_value());
    EXPECT_EQ(*read_price, make_decimal("190.5", &read_arena));
}

TEST(ExemplarRoundtripD, ByteExactCanonicalDecimal) {
    std::pmr::monotonic_buffer_resource arena{4096};
    const auto& seed = fixpp_test_support::kNewOrderSingleSeed;
    auto order_qty = make_decimal(seed.order_qty, &arena);
    auto price = make_decimal(seed.price, &arena);

    std::array<std::byte, 1024> out{};
    auto built_r = fixpp::session::build_new_order_single(std::span<std::byte>{out}, seed.cl_ord_id,
                                                           seed.symbol, seed.side, order_qty, price,
                                                           seed.transact_time);
    ASSERT_TRUE(built_r.has_value()) << "build_new_order_single failed";

    std::string body = bytes_to_string(*built_r);
    // Price(44): raw "44=190.5\x01" bytes verbatim, independent of the
    // by-value golden diff and by-value parse compare.
    std::string expected_field = "44=190.5\x01";
    EXPECT_NE(body.find(expected_field), std::string::npos)
        << "expected canonical bytes '44=190.5\\x01' not found verbatim in body";
}

// ── Exemplar 8 (ExecutionReport) — group-free (020 refactor, T014) ─────────
TEST(ExemplarRoundtrip8, BuildMatchesGoldenAndRoundTrips) {
    std::pmr::monotonic_buffer_resource arena{4096};
    const auto& seed = fixpp_test_support::kExecutionReportSeed;
    auto leaves_qty = make_decimal(seed.leaves_qty, &arena);
    auto cum_qty = make_decimal(seed.cum_qty, &arena);
    auto avg_px = make_decimal(seed.avg_px, &arena);

    std::array<std::byte, 1024> out{};
    auto built_r = fixpp::session::build_execution_report(
        std::span<std::byte>{out}, seed.order_id, seed.exec_id, seed.exec_type, seed.ord_status,
        seed.symbol, seed.side, leaves_qty, cum_qty, avg_px);
    ASSERT_TRUE(built_r.has_value()) << "build_execution_report failed";
    std::span<const std::byte> body = *built_r;

    // ── Leg 1: external golden diff ─────────────────────────────────────────
    std::string golden_path = std::string(FIXPP_REPO_ROOT) + "/" + std::string(seed.golden_path);
    std::string golden_text = read_file(golden_path);
    std::vector<GoldenFrame> expected = parse_golden(golden_text);
    ASSERT_EQ(expected.size(), 1U);

    std::vector<GoldenFrame> actual{GoldenFrame{
        '>', std::vector<std::byte>{body.begin(), body.end()}}};
    auto diff = diff_transcripts(expected, actual, shape_oracle_profile());
    EXPECT_TRUE(static_cast<bool>(diff)) << "golden diff mismatch: " << diff.detail;

    // ── Leg 2: round-trip read-back, dict-aware, exact values ───────────────
    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();

    std::vector<std::byte> frame =
        fixpp_test_support::make_frame(seed.begin_string, bytes_to_string(body));
    auto mv = fixpp_test_support::parse_dict(frame, tv, &read_arena);

    fixpp::v44::ExecutionReport er{mv};

    auto order_id = er.order_id();
    ASSERT_TRUE(order_id.has_value());
    EXPECT_EQ(*order_id, "ORD-XCH-001");

    auto exec_id = er.exec_id();
    ASSERT_TRUE(exec_id.has_value());
    EXPECT_EQ(*exec_id, "EXEC-001");

    auto exec_type = er.exec_type();
    ASSERT_TRUE(exec_type.has_value());
    EXPECT_EQ(*exec_type, 'F');

    auto ord_status = er.ord_status();
    ASSERT_TRUE(ord_status.has_value());
    EXPECT_EQ(*ord_status, '2');

    auto symbol = er.symbol();
    ASSERT_TRUE(symbol.has_value());
    EXPECT_EQ(*symbol, "MSFT");

    auto side = er.side();
    ASSERT_TRUE(side.has_value());
    EXPECT_EQ(*side, '1');

    auto read_leaves_qty = er.leaves_qty(&read_arena);
    ASSERT_TRUE(read_leaves_qty.has_value());
    EXPECT_EQ(*read_leaves_qty, make_decimal("0", &read_arena));

    auto read_cum_qty = er.cum_qty(&read_arena);
    ASSERT_TRUE(read_cum_qty.has_value());
    EXPECT_EQ(*read_cum_qty, make_decimal("100", &read_arena));

    auto read_avg_px = er.avg_px(&read_arena);
    ASSERT_TRUE(read_avg_px.has_value());
    EXPECT_EQ(*read_avg_px, make_decimal("190.5", &read_arena));
}

TEST(ExemplarRoundtrip8, ByteExactCanonicalDecimal) {
    std::pmr::monotonic_buffer_resource arena{4096};
    const auto& seed = fixpp_test_support::kExecutionReportSeed;
    auto leaves_qty = make_decimal(seed.leaves_qty, &arena);
    auto cum_qty = make_decimal(seed.cum_qty, &arena);
    auto avg_px = make_decimal(seed.avg_px, &arena);

    std::array<std::byte, 1024> out{};
    auto built_r = fixpp::session::build_execution_report(
        std::span<std::byte>{out}, seed.order_id, seed.exec_id, seed.exec_type, seed.ord_status,
        seed.symbol, seed.side, leaves_qty, cum_qty, avg_px);
    ASSERT_TRUE(built_r.has_value()) << "build_execution_report failed";

    std::string body = bytes_to_string(*built_r);
    // AvgPx(6): raw "6=190.5\x01" bytes verbatim, independent of the by-value
    // golden diff and by-value parse compare.
    std::string expected_field = "6=190.5\x01";
    EXPECT_NE(body.find(expected_field), std::string::npos)
        << "expected canonical bytes '6=190.5\\x01' not found verbatim in body";
}

// Mutation-proof (data-model §4 spirit / [[feedback_witness_asserts_named_
// postcondition_not_proxy]]): swap the emitted order of the sub-entry level
// (523/803) in an in-memory COPY of the golden text, and confirm
// diff_transcripts flags it as a mismatch against the correctly-ordered
// builder output. Proves the AS golden diff is genuinely order-sensitive at
// the 2-level nest -- not a tautology that would pass regardless of the
// builder's field order (mirrors the E golden's non-tautological ordering
// claim, data-model §4 "Field order matches the QuickFIX-authored golden").
TEST(ExemplarRoundtripAS, OrderMutationDetectsSubEntryReorder) {
    std::pmr::monotonic_buffer_resource arena{8192};
    BuiltAllocationReportParams built = build_allocation_report_params_from_seed(&arena);

    std::array<std::byte, 2048> out{};
    auto built_r =
        fixpp::session::build_allocation_report(std::span<std::byte>{out}, built.params);
    ASSERT_TRUE(built_r.has_value()) << "build_allocation_report failed";
    std::span<const std::byte> body = *built_r;

    const auto& seed = fixpp_test_support::kAllocationReportSeed;
    std::string golden_path = std::string(FIXPP_REPO_ROOT) + "/" + std::string(seed.golden_path);
    std::string golden_text = read_file(golden_path);

    // Swap the sub-entry level fields in the golden's RAW TEXT (the file
    // stores literal "\x01" 4-char sequences, decoded to SOH bytes only by
    // parse_golden -- so the mutation must match that literal form):
    // "523=SUB1\x01803=1\x01" -> "803=1\x01523=SUB1\x01" (the
    // correctly-ordered golden has 523 before 803, matching NoPartySubIDs'
    // dictionary delimiter-first order).
    const std::string correct_order = "523=SUB1\\x01803=1\\x01";
    const std::string swapped_order = "803=1\\x01523=SUB1\\x01";
    auto pos = golden_text.find(correct_order);
    ASSERT_NE(pos, std::string::npos) << "golden text does not contain the expected sub-entry order";
    std::string mutated_golden = golden_text;
    mutated_golden.replace(pos, correct_order.size(), swapped_order);

    std::vector<GoldenFrame> mutated_expected = parse_golden(mutated_golden);
    ASSERT_EQ(mutated_expected.size(), 1U);

    std::vector<GoldenFrame> actual{GoldenFrame{
        '>', std::vector<std::byte>{body.begin(), body.end()}}};
    auto diff = diff_transcripts(mutated_expected, actual, shape_oracle_profile());
    EXPECT_FALSE(static_cast<bool>(diff))
        << "golden diff should have detected the sub-entry reorder mutation but reported match";
}
