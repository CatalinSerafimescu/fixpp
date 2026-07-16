// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_067_builder_shape_oracle.cpp
//
// 067-codegen-writer-emitter T020 [US2] [TESTS-FIRST] — the HEADLINE
// byte-equality pin (contracts/generated-builder.md G2). For each of the 5
// exemplar MsgTypes D/8/9/E/AS in v44, driven by the IDENTICAL 061 seed
// (tests/session/exemplar_seeds.hpp), assert:
//
//   1. fixpp::v44::build_<Msg>(generated) output bytes ==
//      fixpp::session::build_<msg>(the frozen 061 hand exemplar) output
//      bytes (DIRECT byte compare) -- INV-ORDER, the emitter's reason to
//      exist.
//   2. BOTH the generated and the hand output byte-match the QuickFIX-
//      authored golden (tests/session/golden/<msg>.fix) via
//      shape_oracle_profile() (excludes only framing tags {8,9,10,34,52};
//      every business tag, incl. TransactTime(60), matched verbatim;
//      decimals compared by value).
//   3. The >=1-decimal-per-exemplar direct byte compare (C2): the raw
//      canonical decimal field text appears verbatim in the GENERATED
//      output (D/8/E/AS carry a decimal; 9 is group-free/decimal-free, so
//      its C2 analog is a plain-integer verbatim check instead).
//   4. AS additionally asserts the multi-char MsgType path ("35=AS\x01").
//
// If any exemplar fails, the emitter is wrong by definition (G2).
//
// Anchors: specs/067-codegen-writer-emitter/contracts/generated-builder.md
//          G2/G3; tests/session/exemplar_seeds.hpp;
//          tests/session/golden/{new_order_single,execution_report,
//          order_cancel_reject,new_order_list,allocation_report}.fix;
//          tests/session/test_exemplar_roundtrip.cpp (shape_oracle_profile()
//          usage pattern).

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/session/business_messages.hpp>  // frozen 061 hand exemplars
#include <fixpp/v44/Builders.hpp>  // GENERATED (Phase 3b) — build_<Msg>/<Msg>Args
#include <fstream>
#include <memory_resource>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "exemplar_seeds.hpp"
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

// Golden diff a single built body against its checked-in golden via
// shape_oracle_profile() (G2). `label` distinguishes the hand vs generated
// leg in gtest failure output.
void assert_golden_match(std::string_view golden_relpath, std::span<const std::byte> body,
                          const char* label) {
    SCOPED_TRACE(label);
    std::string golden_path = std::string(FIXPP_REPO_ROOT) + "/" + std::string(golden_relpath);
    std::string golden_text = read_file(golden_path);
    std::vector<GoldenFrame> expected = parse_golden(golden_text);
    ASSERT_EQ(expected.size(), 1U);

    std::vector<GoldenFrame> actual{GoldenFrame{'>', std::vector<std::byte>{body.begin(), body.end()}}};
    auto diff = diff_transcripts(expected, actual, shape_oracle_profile());
    EXPECT_TRUE(static_cast<bool>(diff)) << "golden diff mismatch: " << diff.detail;
}

}  // namespace

// ── D (NewOrderSingle, 35=D) ─────────────────────────────────────────────────
TEST(BuilderShapeOracle067, NewOrderSingle) {
    const auto& seed = fixpp_test_support::kNewOrderSingleSeed;
    std::pmr::monotonic_buffer_resource arena{4096};
    decimal_t order_qty = make_decimal(seed.order_qty, &arena);
    decimal_t price = make_decimal(seed.price, &arena);

    std::array<std::byte, 1024> hand_out{};
    auto hand_r = fixpp::session::build_new_order_single(std::span<std::byte>{hand_out},
                                                           seed.cl_ord_id, seed.symbol, seed.side,
                                                           order_qty, price, seed.transact_time);
    ASSERT_TRUE(hand_r.has_value()) << "hand build_new_order_single failed";

    fixpp::v44::NewOrderSingleArgs args{};
    args.cl_ord_id = seed.cl_ord_id;
    args.symbol = seed.symbol;
    args.side = seed.side;
    args.order_qty = order_qty;
    // OrdType(40)="2" (Limit) is HARDCODED internal to the hand exemplar
    // (build_new_order_single has no ord_type parameter — "Limit-only"
    // per business_messages.hpp) rather than caller/seed-supplied; set it
    // explicitly here to reproduce the same output on the generated path.
    args.ord_type = '2';
    args.price = price;
    args.transact_time = seed.transact_time;

    std::array<std::byte, 1024> gen_out{};
    auto gen_r = fixpp::v44::build_NewOrderSingle(std::span<std::byte>{gen_out}, args);
    ASSERT_TRUE(gen_r.has_value()) << "generated build_NewOrderSingle failed";

    // 1. Direct byte equality (INV-ORDER, G2 headline).
    ASSERT_TRUE(std::ranges::equal(*hand_r, *gen_r))
        << "generated NewOrderSingle body diverges from the frozen 061 hand exemplar:\n"
        << "hand: " << bytes_to_string(*hand_r) << "\n"
        << "gen:  " << bytes_to_string(*gen_r);

    // 2. Both byte-match the QuickFIX golden.
    assert_golden_match(seed.golden_path, *hand_r, "hand");
    assert_golden_match(seed.golden_path, *gen_r, "generated");

    // 3. C2 — Price(44) canonical decimal bytes verbatim on the generated output.
    std::string gen_body = bytes_to_string(*gen_r);
    EXPECT_NE(gen_body.find("44=190.5\x01"), std::string::npos)
        << "expected canonical bytes '44=190.5\\x01' not found verbatim in generated body";
}

// ── 8 (ExecutionReport, 35=8) ─────────────────────────────────────────────────
TEST(BuilderShapeOracle067, ExecutionReport) {
    const auto& seed = fixpp_test_support::kExecutionReportSeed;
    std::pmr::monotonic_buffer_resource arena{4096};
    decimal_t leaves_qty = make_decimal(seed.leaves_qty, &arena);
    decimal_t cum_qty = make_decimal(seed.cum_qty, &arena);
    decimal_t avg_px = make_decimal(seed.avg_px, &arena);

    std::array<std::byte, 1024> hand_out{};
    auto hand_r = fixpp::session::build_execution_report(
        std::span<std::byte>{hand_out}, seed.order_id, seed.exec_id, seed.exec_type,
        seed.ord_status, seed.symbol, seed.side, leaves_qty, cum_qty, avg_px);
    ASSERT_TRUE(hand_r.has_value()) << "hand build_execution_report failed";

    fixpp::v44::ExecutionReportArgs args{};
    args.order_id = seed.order_id;
    args.exec_id = seed.exec_id;
    args.exec_type = seed.exec_type;
    args.ord_status = seed.ord_status;
    args.symbol = seed.symbol;
    args.side = seed.side;
    args.leaves_qty = leaves_qty;
    args.cum_qty = cum_qty;
    args.avg_px = avg_px;

    std::array<std::byte, 1024> gen_out{};
    auto gen_r = fixpp::v44::build_ExecutionReport(std::span<std::byte>{gen_out}, args);
    ASSERT_TRUE(gen_r.has_value()) << "generated build_ExecutionReport failed";

    ASSERT_TRUE(std::ranges::equal(*hand_r, *gen_r))
        << "generated ExecutionReport body diverges from the frozen 061 hand exemplar:\n"
        << "hand: " << bytes_to_string(*hand_r) << "\n"
        << "gen:  " << bytes_to_string(*gen_r);

    assert_golden_match(seed.golden_path, *hand_r, "hand");
    assert_golden_match(seed.golden_path, *gen_r, "generated");

    std::string gen_body = bytes_to_string(*gen_r);
    EXPECT_NE(gen_body.find("6=190.5\x01"), std::string::npos)
        << "expected canonical bytes '6=190.5\\x01' not found verbatim in generated body";
}

// ── 9 (OrderCancelReject, 35=9) ───────────────────────────────────────────────
// Group-free AND decimal-free exemplar: its C2 analog is a plain-integer
// verbatim check (CxlRejReason(102)=0), mirroring
// test_exemplar_roundtrip.cpp's "102=0\x01" pin for this exemplar.
TEST(BuilderShapeOracle067, OrderCancelReject) {
    const auto& seed = fixpp_test_support::kOrderCancelRejectSeed;

    std::array<std::byte, 1024> hand_out{};
    auto hand_r = fixpp::session::build_order_cancel_reject(
        std::span<std::byte>{hand_out}, seed.order_id, seed.cl_ord_id, seed.orig_cl_ord_id,
        seed.ord_status, seed.cxl_rej_response_to, seed.cxl_rej_reason);
    ASSERT_TRUE(hand_r.has_value()) << "hand build_order_cancel_reject failed";

    fixpp::v44::OrderCancelRejectArgs args{};
    args.order_id = seed.order_id;
    args.cl_ord_id = seed.cl_ord_id;
    args.orig_cl_ord_id = seed.orig_cl_ord_id;
    args.ord_status = seed.ord_status;
    args.cxl_rej_response_to = seed.cxl_rej_response_to;
    args.cxl_rej_reason = seed.cxl_rej_reason;

    std::array<std::byte, 1024> gen_out{};
    auto gen_r = fixpp::v44::build_OrderCancelReject(std::span<std::byte>{gen_out}, args);
    ASSERT_TRUE(gen_r.has_value()) << "generated build_OrderCancelReject failed";

    ASSERT_TRUE(std::ranges::equal(*hand_r, *gen_r))
        << "generated OrderCancelReject body diverges from the frozen 061 hand exemplar:\n"
        << "hand: " << bytes_to_string(*hand_r) << "\n"
        << "gen:  " << bytes_to_string(*gen_r);

    assert_golden_match(seed.golden_path, *hand_r, "hand");
    assert_golden_match(seed.golden_path, *gen_r, "generated");

    std::string gen_body = bytes_to_string(*gen_r);
    EXPECT_NE(gen_body.find("102=0\x01"), std::string::npos)
        << "expected canonical bytes '102=0\\x01' not found verbatim in generated body";
}

// ── E (NewOrderList, 35=E) — the pivotal grouped/nested exemplar ─────────────
TEST(BuilderShapeOracle067, NewOrderListGrouped) {
    const auto& seed = fixpp_test_support::kNewOrderListSeed;
    std::pmr::monotonic_buffer_resource arena{8192};

    // ── Hand params (fixpp::session::NewOrderListParams) ──
    std::vector<fixpp::session::NewOrderListPartySubId> hand_ord1_subs;
    hand_ord1_subs.push_back(fixpp::session::NewOrderListPartySubId{
        seed.orders[0].parties[0].sub_ids[0].party_sub_id,
        seed.orders[0].parties[0].sub_ids[0].party_sub_id_type,
    });
    std::vector<fixpp::session::NewOrderListParty> hand_ord1_parties;
    hand_ord1_parties.push_back(fixpp::session::NewOrderListParty{
        seed.orders[0].parties[0].party_id,
        seed.orders[0].parties[0].party_id_source,
        seed.orders[0].parties[0].party_role,
        std::span<const fixpp::session::NewOrderListPartySubId>{hand_ord1_subs},
    });
    std::vector<fixpp::session::NewOrderListParty> hand_ord2_parties;  // empty -> NoPartyIDs=0

    std::vector<fixpp::session::NewOrderListOrder> hand_orders;
    hand_orders.push_back(fixpp::session::NewOrderListOrder{
        seed.orders[0].cl_ord_id, seed.orders[0].list_seq_no, seed.orders[0].side,
        seed.orders[0].symbol, make_decimal(seed.orders[0].order_qty, &arena),
        std::span<const fixpp::session::NewOrderListParty>{hand_ord1_parties},
    });
    hand_orders.push_back(fixpp::session::NewOrderListOrder{
        seed.orders[1].cl_ord_id, seed.orders[1].list_seq_no, seed.orders[1].side,
        seed.orders[1].symbol, make_decimal(seed.orders[1].order_qty, &arena),
        std::span<const fixpp::session::NewOrderListParty>{hand_ord2_parties},
    });

    fixpp::session::NewOrderListParams hand_params{
        seed.list_id, seed.bid_type, seed.tot_no_orders,
        std::span<const fixpp::session::NewOrderListOrder>{hand_orders}};

    std::array<std::byte, 4096> hand_out{};
    auto hand_r = fixpp::session::build_new_order_list(std::span<std::byte>{hand_out}, hand_params);
    ASSERT_TRUE(hand_r.has_value()) << "hand build_new_order_list failed";

    // ── Generated args (fixpp::v44::NewOrderListArgs) — SAME seed values ──
    fixpp::v44::groups::G_802Args gen_sub0{};
    gen_sub0.party_sub_id = seed.orders[0].parties[0].sub_ids[0].party_sub_id;
    gen_sub0.party_sub_id_type = seed.orders[0].parties[0].sub_ids[0].party_sub_id_type;
    std::array<fixpp::v44::groups::G_802Args, 1> gen_subs{gen_sub0};

    fixpp::v44::groups::G_453Args gen_party0{};
    gen_party0.party_id = seed.orders[0].parties[0].party_id;
    gen_party0.party_id_source = seed.orders[0].parties[0].party_id_source;
    gen_party0.party_role = seed.orders[0].parties[0].party_role;
    gen_party0.party_sub_i_ds = std::optional<
        std::span<const fixpp::v44::groups::G_802Args>>{
        std::span<const fixpp::v44::groups::G_802Args>{gen_subs}};
    std::array<fixpp::v44::groups::G_453Args, 1> gen_parties{gen_party0};

    // ORD2: engaged-but-EMPTY optional span (matches the hand builder's
    // "always opens NoPartyIDs, empty -> 453=0 present-but-empty" contract —
    // NOT nullopt, which would omit the group entirely).
    std::array<fixpp::v44::groups::G_453Args, 0> gen_empty_parties{};

    fixpp::v44::groups::G_73_2Args gen_order0{};
    gen_order0.cl_ord_id = seed.orders[0].cl_ord_id;
    gen_order0.list_seq_no = seed.orders[0].list_seq_no;
    gen_order0.side = seed.orders[0].side;
    gen_order0.symbol = seed.orders[0].symbol;
    gen_order0.order_qty = make_decimal(seed.orders[0].order_qty, &arena);
    gen_order0.party_i_ds = std::optional<std::span<const fixpp::v44::groups::G_453Args>>{
        std::span<const fixpp::v44::groups::G_453Args>{gen_parties}};

    fixpp::v44::groups::G_73_2Args gen_order1{};
    gen_order1.cl_ord_id = seed.orders[1].cl_ord_id;
    gen_order1.list_seq_no = seed.orders[1].list_seq_no;
    gen_order1.side = seed.orders[1].side;
    gen_order1.symbol = seed.orders[1].symbol;
    gen_order1.order_qty = make_decimal(seed.orders[1].order_qty, &arena);
    gen_order1.party_i_ds = std::optional<std::span<const fixpp::v44::groups::G_453Args>>{
        std::span<const fixpp::v44::groups::G_453Args>{gen_empty_parties}};

    std::array<fixpp::v44::groups::G_73_2Args, 2> gen_orders{gen_order0, gen_order1};

    fixpp::v44::NewOrderListArgs gen_args{};
    gen_args.list_id = seed.list_id;
    gen_args.tot_no_orders = seed.tot_no_orders;
    gen_args.bid_type = seed.bid_type;
    gen_args.orders = std::span<const fixpp::v44::groups::G_73_2Args>{gen_orders};  // REQUIRED

    std::array<std::byte, 4096> gen_out{};
    auto gen_r = fixpp::v44::build_NewOrderList(std::span<std::byte>{gen_out}, gen_args);
    ASSERT_TRUE(gen_r.has_value()) << "generated build_NewOrderList failed";

    // 1. Direct byte equality (the RC#7 group-order discriminator lives here).
    ASSERT_TRUE(std::ranges::equal(*hand_r, *gen_r))
        << "generated NewOrderList body diverges from the frozen 061 hand exemplar:\n"
        << "hand: " << bytes_to_string(*hand_r) << "\n"
        << "gen:  " << bytes_to_string(*gen_r);

    // 2. Both byte-match the QuickFIX golden.
    assert_golden_match(seed.golden_path, *hand_r, "hand");
    assert_golden_match(seed.golden_path, *gen_r, "generated");

    // 3. C2 — order 1's OrderQty(38) canonical decimal bytes verbatim.
    std::string gen_body = bytes_to_string(*gen_r);
    EXPECT_NE(gen_body.find("38=150.75\x01"), std::string::npos)
        << "expected canonical bytes '38=150.75\\x01' not found verbatim in generated body";
}

// ── AS (AllocationReport, 35=AS) — multi-char MsgType + 2-level nesting ──────
TEST(BuilderShapeOracle067, AllocationReportGrouped) {
    const auto& seed = fixpp_test_support::kAllocationReportSeed;
    std::pmr::monotonic_buffer_resource arena{8192};

    // ── Hand params (fixpp::session::AllocationReportParams) ──
    std::vector<fixpp::session::AllocationReportPartySubId> hand_subs;
    hand_subs.push_back(fixpp::session::AllocationReportPartySubId{
        seed.parties[0].sub_ids[0].party_sub_id,
        seed.parties[0].sub_ids[0].party_sub_id_type,
    });
    std::vector<fixpp::session::AllocationReportParty> hand_parties;
    hand_parties.push_back(fixpp::session::AllocationReportParty{
        seed.parties[0].party_id, seed.parties[0].party_id_source, seed.parties[0].party_role,
        std::span<const fixpp::session::AllocationReportPartySubId>{hand_subs},
    });

    fixpp::session::AllocationReportParams hand_params{};
    hand_params.alloc_report_id = seed.alloc_report_id;
    hand_params.alloc_trans_type = seed.alloc_trans_type;
    hand_params.alloc_report_type = seed.alloc_report_type;
    hand_params.alloc_status = seed.alloc_status;
    hand_params.alloc_no_orders_type = seed.alloc_no_orders_type;
    hand_params.side = seed.side;
    hand_params.quantity = make_decimal(seed.quantity, &arena);
    hand_params.avg_px = make_decimal(seed.avg_px, &arena);
    hand_params.trade_date = seed.trade_date;
    hand_params.symbol = seed.symbol;
    hand_params.parties = std::span<const fixpp::session::AllocationReportParty>{hand_parties};

    std::array<std::byte, 4096> hand_out{};
    auto hand_r =
        fixpp::session::build_allocation_report(std::span<std::byte>{hand_out}, hand_params);
    ASSERT_TRUE(hand_r.has_value()) << "hand build_allocation_report failed";

    // ── Generated args (fixpp::v44::AllocationReportArgs) — SAME seed values ──
    fixpp::v44::groups::G_802Args gen_sub0{};
    gen_sub0.party_sub_id = seed.parties[0].sub_ids[0].party_sub_id;
    gen_sub0.party_sub_id_type = seed.parties[0].sub_ids[0].party_sub_id_type;
    std::array<fixpp::v44::groups::G_802Args, 1> gen_subs{gen_sub0};

    fixpp::v44::groups::G_453Args gen_party0{};
    gen_party0.party_id = seed.parties[0].party_id;
    gen_party0.party_id_source = seed.parties[0].party_id_source;
    gen_party0.party_role = seed.parties[0].party_role;
    gen_party0.party_sub_i_ds =
        std::optional<std::span<const fixpp::v44::groups::G_802Args>>{
            std::span<const fixpp::v44::groups::G_802Args>{gen_subs}};
    std::array<fixpp::v44::groups::G_453Args, 1> gen_parties{gen_party0};

    fixpp::v44::AllocationReportArgs gen_args{};
    gen_args.alloc_report_id = seed.alloc_report_id;
    gen_args.alloc_trans_type = seed.alloc_trans_type;
    gen_args.alloc_report_type = seed.alloc_report_type;
    gen_args.alloc_status = seed.alloc_status;
    gen_args.alloc_no_orders_type = seed.alloc_no_orders_type;
    gen_args.side = seed.side;
    gen_args.quantity = make_decimal(seed.quantity, &arena);
    gen_args.avg_px = make_decimal(seed.avg_px, &arena);
    gen_args.trade_date = seed.trade_date;
    gen_args.symbol = seed.symbol;
    gen_args.party_i_ds = std::optional<std::span<const fixpp::v44::groups::G_453Args>>{
        std::span<const fixpp::v44::groups::G_453Args>{gen_parties}};

    std::array<std::byte, 4096> gen_out{};
    auto gen_r = fixpp::v44::build_AllocationReport(std::span<std::byte>{gen_out}, gen_args);
    ASSERT_TRUE(gen_r.has_value()) << "generated build_AllocationReport failed";

    // 1. Direct byte equality.
    ASSERT_TRUE(std::ranges::equal(*hand_r, *gen_r))
        << "generated AllocationReport body diverges from the frozen 061 hand exemplar:\n"
        << "hand: " << bytes_to_string(*hand_r) << "\n"
        << "gen:  " << bytes_to_string(*gen_r);

    // 2. Both byte-match the QuickFIX golden.
    assert_golden_match(seed.golden_path, *hand_r, "hand");
    assert_golden_match(seed.golden_path, *gen_r, "generated");

    // 3. C2 — AvgPx(6) canonical decimal bytes verbatim on the generated output.
    std::string gen_body = bytes_to_string(*gen_r);
    EXPECT_NE(gen_body.find("6=25.5\x01"), std::string::npos)
        << "expected canonical bytes '6=25.5\\x01' not found verbatim in generated body";

    // 4. Multi-char MsgType path: body leads with "35=AS\x01" (not the
    // single-char accident of D/8/9/E).
    EXPECT_EQ(gen_body.substr(0, 6), "35=AS\x01")
        << "generated AllocationReport body does not lead with 35=AS\\x01";
    EXPECT_EQ(bytes_to_string(*hand_r).substr(0, 6), "35=AS\x01")
        << "hand AllocationReport body does not lead with 35=AS\\x01";
}

// ── 077-builder-args-dedup T020 [US3] — frozen-corpus extension ────────────
//
// W/X/i were NOT previously wired to the generated builder path (per the
// T002 corpus inventory, tests/session/golden/gen/README.md: consumed only
// by pre-061 hand-builder tests) — extend the differential to them here.
// research.md R5 flagged both as the sharpest available discriminators
// against a mis-shared dedup key:
//   - W (MarketDataSnapshotFullRefresh) / X (MarketDataIncrementalRefresh):
//     SAME no_tag NoMDEntries(268), DIFFERENT delimiter -- MDEntryType(269)
//     in W vs MDUpdateAction(279) in X -- dedup to groups::G_268_1Args /
//     groups::G_268_2Args respectively. A no_tag-only key would wrongly
//     collapse these to one plan.
//   - i (MassQuote): 2-level-nested QuotSetGrp(296)/QuotEntryGrp(295), both
//     Required at their own level -- groups::G_296_2Args / G_295_3Args.
// Goldens: tests/session/golden/{market_data_snapshot,market_data_incremental,
// mass_quote}.fix (frozen, QuickFIX-authored, tests/session/golden/gen/
// qf_market_data.cpp / qf_mass_quote.cpp).

// ── W (MarketDataSnapshotFullRefresh, 35=W) ─────────────────────────────────
TEST(BuilderShapeOracle067, MarketDataSnapshotFullRefresh_W) {
    std::pmr::monotonic_buffer_resource arena{4096};

    fixpp::v44::groups::G_268_1Args entry{};
    entry.md_entry_type = '0';  // FIX::MDEntryType_BID
    entry.md_entry_px = make_decimal("99.5", &arena);
    entry.md_entry_size = make_decimal("100", &arena);
    std::array<fixpp::v44::groups::G_268_1Args, 1> entries{entry};

    fixpp::v44::MarketDataSnapshotFullRefreshArgs args{};
    args.md_req_id = "MDR-W1";
    args.symbol = "MSFT";
    args.md_entries = std::span<const fixpp::v44::groups::G_268_1Args>{entries};

    std::array<std::byte, 1024> out{};
    auto r = fixpp::v44::build_MarketDataSnapshotFullRefresh(std::span<std::byte>{out}, args);
    ASSERT_TRUE(r.has_value()) << "build_MarketDataSnapshotFullRefresh failed";

    assert_golden_match("tests/session/golden/market_data_snapshot.fix", *r, "generated");
}

// ── X (MarketDataIncrementalRefresh, 35=X) ──────────────────────────────────
TEST(BuilderShapeOracle067, MarketDataIncrementalRefresh_X) {
    std::pmr::monotonic_buffer_resource arena{4096};

    fixpp::v44::groups::G_268_2Args entry{};
    entry.md_update_action = '0';  // FIX::MDUpdateAction_NEW
    entry.md_entry_type = '0';     // FIX::MDEntryType_BID
    entry.md_entry_px = make_decimal("100.25", &arena);
    std::array<fixpp::v44::groups::G_268_2Args, 1> entries{entry};

    fixpp::v44::MarketDataIncrementalRefreshArgs args{};
    args.md_req_id = "MDR-X1";
    args.md_entries = std::span<const fixpp::v44::groups::G_268_2Args>{entries};

    std::array<std::byte, 1024> out{};
    auto r = fixpp::v44::build_MarketDataIncrementalRefresh(std::span<std::byte>{out}, args);
    ASSERT_TRUE(r.has_value()) << "build_MarketDataIncrementalRefresh failed";

    assert_golden_match("tests/session/golden/market_data_incremental.fix", *r, "generated");
}

// ── i (MassQuote, 35=i) — deep-nested-group insurance ───────────────────────
TEST(BuilderShapeOracle067, MassQuote_I) {
    std::pmr::monotonic_buffer_resource arena{4096};

    fixpp::v44::groups::G_295_3Args entry{};
    entry.quote_entry_id = "QE1";
    entry.bid_px = make_decimal("10.5", &arena);
    entry.offer_px = make_decimal("10.75", &arena);
    std::array<fixpp::v44::groups::G_295_3Args, 1> entries{entry};

    fixpp::v44::groups::G_296_2Args set{};
    set.quote_set_id = "QS1";
    set.tot_no_quote_entries = 1;
    set.quote_entries = std::span<const fixpp::v44::groups::G_295_3Args>{entries};
    std::array<fixpp::v44::groups::G_296_2Args, 1> sets{set};

    fixpp::v44::MassQuoteArgs args{};
    args.quote_id = "QID-100";
    args.quote_sets = std::span<const fixpp::v44::groups::G_296_2Args>{sets};

    std::array<std::byte, 1024> out{};
    auto r = fixpp::v44::build_MassQuote(std::span<std::byte>{out}, args);
    ASSERT_TRUE(r.has_value()) << "build_MassQuote failed";

    assert_golden_match("tests/session/golden/mass_quote.fix", *r, "generated");
}
