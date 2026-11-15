// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_v42_builder_roundtrip.cpp
//
// FIX 4.2 per-message builder round-trip (records pass, REMAINING-WORK row 8; built on an
// owner ruling). The v42 sibling of test_077_allversions_builder_roundtrip.cpp (v50sp2) and
// its _vlatest twin: one cell per fixpp::v42 builder, each building a message, framing it
// as FIX.4.2, parsing it with an independent dictionary-driven Parser<Index> over
// dictionaries/FIX42.xml and reading each seeded field back by value, plus MsgType(35).
//
// GENERATED (host-side, not codegen product) by a one-off script and checked in as a
// static, reviewable .cpp, as the 077 suites are. How the cells were derived, so they can
// be re-derived:
//   - the message set is fixpp::v42::builder_registry, which the script required to equal
//     FIX42.xml's msgcat="app" set (BuilderCompleteness077Vt11V42 pins the same equality);
//   - per message, the scalar <Msg>Args members in declaration order; each member's tag is
//     the FIX42.xml top-level <field> of that message whose name, lower-cased, equals the
//     member name without underscores (the script stops unless exactly one matches). The
//     tag is not read from the generated builder;
//   - Length and Data fields, header fields the Args also carry (MessageEncoding(347)), and
//     repeating groups are left unset;
//   - up to 6 scalars, at most 2 per C++ category (string_view / char / bool / int64 /
//     decimal_t), as test_077_allversions_builder_roundtrip.cpp chooses them;
//   - values are chosen before any build: string "<Msg>_<member>", int64 = the field's own
//     tag, decimal "<tag>.5", bool true (wire "Y"), char = the first value of the field's
//     FIX42.xml enum list that no other seeded field of the cell carries (where none is left,
//     the first such digit or letter). The script stops unless every seeded value of a cell
//     is distinct on the wire, so a builder that writes one seeded member under another
//     seeded member's tag fails a value assertion.
// validate_<Msg> is not called: required groups are left empty, which it refuses by design
// (tests/codegen/test_082_v42_required_group_omission_test.cpp witnesses that refusal).
//
// V42BuilderRoundtripCensus reads this binary's registered BuilderRoundtripV42 cells and
// requires exactly one per builder_registry MsgType, mapping each cell name to its MsgType
// through an independent pugixml walk of FIX42.xml.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/v42/all.hpp>  // GENERATED -- build_<Msg>/<Msg>Args/builder_registry
#include <map>
#include <memory_resource>
#include <pugixml.hpp>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "support/app_message_read_scaffold.hpp"

namespace {

using fixpp_test_support::bytes_to_string;
using fixpp_test_support::expect_decimal;
using fixpp_test_support::expect_text;
using fixpp_test_support::make_decimal;
using fixpp_test_support::make_frame;
using fixpp_test_support::parse_dict;

std::string fix42_path() { return std::string(FIXPP_DICT_DATA_DIR) + "/FIX42.xml"; }

}  // namespace

class BuilderRoundtripV42 : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        dict_arena_ = new std::pmr::monotonic_buffer_resource(65536);
        fixpp::dict::XmlLoader loader;
        dict_ = new fixpp::dict::Dictionary(loader.load(fix42_path(), dict_arena_));
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

    std::array<std::byte, 16384> out{};
    std::pmr::monotonic_buffer_resource read_arena{65536};

    static std::pmr::monotonic_buffer_resource* dict_arena_;
    static fixpp::dict::Dictionary* dict_;
    static fixpp::dict::table_view* tv_;
};

std::pmr::monotonic_buffer_resource* BuilderRoundtripV42::dict_arena_ = nullptr;
fixpp::dict::Dictionary* BuilderRoundtripV42::dict_ = nullptr;
fixpp::dict::table_view* BuilderRoundtripV42::tv_ = nullptr;

TEST(V42BuilderRoundtripCensus, EveryRegistryMsgTypeHasExactlyOneCell) {
    pugi::xml_document doc;
    ASSERT_TRUE(doc.load_file(fix42_path().c_str())) << fix42_path();
    std::map<std::string, std::string> msgtype_of_name;
    for (auto const m : doc.child("fix").child("messages").children("message")) {
        msgtype_of_name[m.attribute("name").as_string()] = m.attribute("msgtype").as_string();
    }
    ASSERT_FALSE(msgtype_of_name.empty()) << "no <message> in " << fix42_path();

    std::map<std::string, int> multiplicity;
    for (auto const& e : fixpp::v42::builder_registry) {
        multiplicity[std::string{e.msg_type}] = 0;
    }
    ASSERT_FALSE(multiplicity.empty()) << "fixpp::v42::builder_registry is empty";

    std::vector<std::string> findings;
    ::testing::UnitTest const* const ut = ::testing::UnitTest::GetInstance();
    bool suite_seen = false;
    for (int s = 0; s < ut->total_test_suite_count(); ++s) {
        ::testing::TestSuite const* const suite = ut->GetTestSuite(s);
        if (std::string_view{suite->name()} != "BuilderRoundtripV42") continue;
        suite_seen = true;
        for (int t = 0; t < suite->total_test_count(); ++t) {
            std::string const name = suite->GetTestInfo(t)->name();
            auto const it = msgtype_of_name.find(name);
            if (it == msgtype_of_name.end()) {
                findings.push_back("BuilderRoundtripV42." + name + ": no <message name='" + name +
                                   "'> in FIX42.xml");
                continue;
            }
            auto const m = multiplicity.find(it->second);
            if (m == multiplicity.end()) {
                findings.push_back("BuilderRoundtripV42." + name + ": 35=" + it->second +
                                   " is not in fixpp::v42::builder_registry");
                continue;
            }
            ++m->second;
        }
    }
    ASSERT_TRUE(suite_seen) << "no BuilderRoundtripV42 suite is registered in this binary";
    for (auto const& [mt, n] : multiplicity) {
        if (n != 1) {
            findings.push_back("35=" + mt + " has " + std::to_string(n) +
                               " cells (exactly one required)");
        }
    }
    std::string joined;
    for (auto const& f : findings) {
        joined += "\n  ";
        joined += f;
    }
    EXPECT_TRUE(findings.empty()) << "census findings:" << joined;
}

TEST_F(BuilderRoundtripV42, IOI) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v42::IOIArgs args{};
    args.currency = "IOI_currency";
    args.id_source = "IOI_id_source";
    args.ioi_qlty_ind = 'H';
    args.ioi_trans_type = 'C';
    args.price = make_decimal("44.5", &arena);
    args.ioi_natural_flag = true;
    auto built = fixpp::v42::build_IOI(out, args);
    ASSERT_TRUE(built.has_value()) << "build_IOI failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built IOI frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "6", "msg_type");
    expect_text(mv, 15, "IOI_currency", "currency");
    expect_text(mv, 22, "IOI_id_source", "id_source");
    expect_text(mv, 25, "H", "ioi_qlty_ind");
    expect_text(mv, 28, "C", "ioi_trans_type");
    expect_decimal(mv, 44, "44.5", &read_arena, "price");
    expect_text(mv, 130, "Y", "ioi_natural_flag");
}

TEST_F(BuilderRoundtripV42, Advertisement) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v42::AdvertisementArgs args{};
    args.adv_id = "Advertisement_adv_id";
    args.adv_ref_id = "Advertisement_adv_ref_id";
    args.adv_side = 'B';
    args.price = make_decimal("44.5", &arena);
    args.shares = make_decimal("53.5", &arena);
    args.put_or_call = 201;
    auto built = fixpp::v42::build_Advertisement(out, args);
    ASSERT_TRUE(built.has_value()) << "build_Advertisement failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built Advertisement frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "7", "msg_type");
    expect_text(mv, 2, "Advertisement_adv_id", "adv_id");
    expect_text(mv, 3, "Advertisement_adv_ref_id", "adv_ref_id");
    expect_text(mv, 4, "B", "adv_side");
    expect_decimal(mv, 44, "44.5", &read_arena, "price");
    expect_decimal(mv, 53, "53.5", &read_arena, "shares");
    expect_text(mv, 201, "201", "put_or_call");
}

TEST_F(BuilderRoundtripV42, ExecutionReport) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v42::ExecutionReportArgs args{};
    args.account = "ExecutionReport_account";
    args.avg_px = make_decimal("6.5", &arena);
    args.cl_ord_id = "ExecutionReport_cl_ord_id";
    args.commission = make_decimal("12.5", &arena);
    args.comm_type = '1';
    args.exec_trans_type = '0';
    auto built = fixpp::v42::build_ExecutionReport(out, args);
    ASSERT_TRUE(built.has_value()) << "build_ExecutionReport failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built ExecutionReport frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "8", "msg_type");
    expect_text(mv, 1, "ExecutionReport_account", "account");
    expect_decimal(mv, 6, "6.5", &read_arena, "avg_px");
    expect_text(mv, 11, "ExecutionReport_cl_ord_id", "cl_ord_id");
    expect_decimal(mv, 12, "12.5", &read_arena, "commission");
    expect_text(mv, 13, "1", "comm_type");
    expect_text(mv, 20, "0", "exec_trans_type");
}

TEST_F(BuilderRoundtripV42, OrderCancelReject) {
    fixpp::v42::OrderCancelRejectArgs args{};
    args.account = "OrderCancelReject_account";
    args.cl_ord_id = "OrderCancelReject_cl_ord_id";
    args.ord_status = '0';
    args.cxl_rej_reason = 102;
    args.cxl_rej_response_to = '1';
    auto built = fixpp::v42::build_OrderCancelReject(out, args);
    ASSERT_TRUE(built.has_value()) << "build_OrderCancelReject failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built OrderCancelReject frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "9", "msg_type");
    expect_text(mv, 1, "OrderCancelReject_account", "account");
    expect_text(mv, 11, "OrderCancelReject_cl_ord_id", "cl_ord_id");
    expect_text(mv, 39, "0", "ord_status");
    expect_text(mv, 102, "102", "cxl_rej_reason");
    expect_text(mv, 434, "1", "cxl_rej_response_to");
}

TEST_F(BuilderRoundtripV42, News) {
    fixpp::v42::NewsArgs args{};
    args.orig_time = "News_orig_time";
    args.urgency = '0';
    args.headline = "News_headline";
    auto built = fixpp::v42::build_News(out, args);
    ASSERT_TRUE(built.has_value()) << "build_News failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built News frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "B", "msg_type");
    expect_text(mv, 42, "News_orig_time", "orig_time");
    expect_text(mv, 61, "0", "urgency");
    expect_text(mv, 148, "News_headline", "headline");
}

TEST_F(BuilderRoundtripV42, Email) {
    fixpp::v42::EmailArgs args{};
    args.cl_ord_id = "Email_cl_ord_id";
    args.order_id = "Email_order_id";
    args.email_type = '0';
    auto built = fixpp::v42::build_Email(out, args);
    ASSERT_TRUE(built.has_value()) << "build_Email failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built Email frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "C", "msg_type");
    expect_text(mv, 11, "Email_cl_ord_id", "cl_ord_id");
    expect_text(mv, 37, "Email_order_id", "order_id");
    expect_text(mv, 94, "0", "email_type");
}

TEST_F(BuilderRoundtripV42, NewOrderSingle) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v42::NewOrderSingleArgs args{};
    args.account = "NewOrderSingle_account";
    args.cl_ord_id = "NewOrderSingle_cl_ord_id";
    args.commission = make_decimal("12.5", &arena);
    args.comm_type = '1';
    args.handl_inst = '2';
    args.order_qty = make_decimal("38.5", &arena);
    auto built = fixpp::v42::build_NewOrderSingle(out, args);
    ASSERT_TRUE(built.has_value()) << "build_NewOrderSingle failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built NewOrderSingle frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "D", "msg_type");
    expect_text(mv, 1, "NewOrderSingle_account", "account");
    expect_text(mv, 11, "NewOrderSingle_cl_ord_id", "cl_ord_id");
    expect_decimal(mv, 12, "12.5", &read_arena, "commission");
    expect_text(mv, 13, "1", "comm_type");
    expect_text(mv, 21, "2", "handl_inst");
    expect_decimal(mv, 38, "38.5", &read_arena, "order_qty");
}

TEST_F(BuilderRoundtripV42, NewOrderList) {
    fixpp::v42::NewOrderListArgs args{};
    args.list_id = "NewOrderList_list_id";
    args.tot_no_orders = 68;
    args.list_exec_inst = "NewOrderList_list_exec_inst";
    args.bid_type = 394;
    args.list_exec_inst_type = '1';
    auto built = fixpp::v42::build_NewOrderList(out, args);
    ASSERT_TRUE(built.has_value()) << "build_NewOrderList failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built NewOrderList frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "E", "msg_type");
    expect_text(mv, 66, "NewOrderList_list_id", "list_id");
    expect_text(mv, 68, "68", "tot_no_orders");
    expect_text(mv, 69, "NewOrderList_list_exec_inst", "list_exec_inst");
    expect_text(mv, 394, "394", "bid_type");
    expect_text(mv, 433, "1", "list_exec_inst_type");
}

TEST_F(BuilderRoundtripV42, OrderCancelRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v42::OrderCancelRequestArgs args{};
    args.account = "OrderCancelRequest_account";
    args.cl_ord_id = "OrderCancelRequest_cl_ord_id";
    args.order_qty = make_decimal("38.5", &arena);
    args.side = '1';
    args.cash_order_qty = make_decimal("152.5", &arena);
    args.put_or_call = 201;
    auto built = fixpp::v42::build_OrderCancelRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_OrderCancelRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built OrderCancelRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "F", "msg_type");
    expect_text(mv, 1, "OrderCancelRequest_account", "account");
    expect_text(mv, 11, "OrderCancelRequest_cl_ord_id", "cl_ord_id");
    expect_decimal(mv, 38, "38.5", &read_arena, "order_qty");
    expect_text(mv, 54, "1", "side");
    expect_decimal(mv, 152, "152.5", &read_arena, "cash_order_qty");
    expect_text(mv, 201, "201", "put_or_call");
}

TEST_F(BuilderRoundtripV42, OrderCancelReplaceRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v42::OrderCancelReplaceRequestArgs args{};
    args.account = "OrderCancelReplaceRequest_account";
    args.cl_ord_id = "OrderCancelReplaceRequest_cl_ord_id";
    args.commission = make_decimal("12.5", &arena);
    args.comm_type = '1';
    args.handl_inst = '2';
    args.order_qty = make_decimal("38.5", &arena);
    auto built = fixpp::v42::build_OrderCancelReplaceRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_OrderCancelReplaceRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built OrderCancelReplaceRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "G", "msg_type");
    expect_text(mv, 1, "OrderCancelReplaceRequest_account", "account");
    expect_text(mv, 11, "OrderCancelReplaceRequest_cl_ord_id", "cl_ord_id");
    expect_decimal(mv, 12, "12.5", &read_arena, "commission");
    expect_text(mv, 13, "1", "comm_type");
    expect_text(mv, 21, "2", "handl_inst");
    expect_decimal(mv, 38, "38.5", &read_arena, "order_qty");
}

TEST_F(BuilderRoundtripV42, OrderStatusRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v42::OrderStatusRequestArgs args{};
    args.account = "OrderStatusRequest_account";
    args.cl_ord_id = "OrderStatusRequest_cl_ord_id";
    args.side = '1';
    args.put_or_call = 201;
    args.strike_price = make_decimal("202.5", &arena);
    args.maturity_day = 205;
    auto built = fixpp::v42::build_OrderStatusRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_OrderStatusRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built OrderStatusRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "H", "msg_type");
    expect_text(mv, 1, "OrderStatusRequest_account", "account");
    expect_text(mv, 11, "OrderStatusRequest_cl_ord_id", "cl_ord_id");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "202.5", &read_arena, "strike_price");
    expect_text(mv, 205, "205", "maturity_day");
}

TEST_F(BuilderRoundtripV42, Allocation) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v42::AllocationArgs args{};
    args.avg_px = make_decimal("6.5", &arena);
    args.currency = "Allocation_currency";
    args.id_source = "Allocation_id_source";
    args.shares = make_decimal("53.5", &arena);
    args.side = '1';
    args.settlmnt_typ = '0';
    auto built = fixpp::v42::build_Allocation(out, args);
    ASSERT_TRUE(built.has_value()) << "build_Allocation failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built Allocation frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "J", "msg_type");
    expect_decimal(mv, 6, "6.5", &read_arena, "avg_px");
    expect_text(mv, 15, "Allocation_currency", "currency");
    expect_text(mv, 22, "Allocation_id_source", "id_source");
    expect_decimal(mv, 53, "53.5", &read_arena, "shares");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 63, "0", "settlmnt_typ");
}

TEST_F(BuilderRoundtripV42, ListCancelRequest) {
    fixpp::v42::ListCancelRequestArgs args{};
    args.text = "ListCancelRequest_text";
    args.transact_time = "ListCancelRequest_transact_time";
    auto built = fixpp::v42::build_ListCancelRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_ListCancelRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built ListCancelRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "K", "msg_type");
    expect_text(mv, 58, "ListCancelRequest_text", "text");
    expect_text(mv, 60, "ListCancelRequest_transact_time", "transact_time");
}

TEST_F(BuilderRoundtripV42, ListExecute) {
    fixpp::v42::ListExecuteArgs args{};
    args.text = "ListExecute_text";
    args.transact_time = "ListExecute_transact_time";
    auto built = fixpp::v42::build_ListExecute(out, args);
    ASSERT_TRUE(built.has_value()) << "build_ListExecute failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built ListExecute frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "L", "msg_type");
    expect_text(mv, 58, "ListExecute_text", "text");
    expect_text(mv, 60, "ListExecute_transact_time", "transact_time");
}

TEST_F(BuilderRoundtripV42, ListStatusRequest) {
    fixpp::v42::ListStatusRequestArgs args{};
    args.text = "ListStatusRequest_text";
    args.list_id = "ListStatusRequest_list_id";
    auto built = fixpp::v42::build_ListStatusRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_ListStatusRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built ListStatusRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "M", "msg_type");
    expect_text(mv, 58, "ListStatusRequest_text", "text");
    expect_text(mv, 66, "ListStatusRequest_list_id", "list_id");
}

TEST_F(BuilderRoundtripV42, ListStatus) {
    fixpp::v42::ListStatusArgs args{};
    args.transact_time = "ListStatus_transact_time";
    args.list_id = "ListStatus_list_id";
    args.tot_no_orders = 68;
    args.no_rpts = 82;
    auto built = fixpp::v42::build_ListStatus(out, args);
    ASSERT_TRUE(built.has_value()) << "build_ListStatus failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built ListStatus frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "N", "msg_type");
    expect_text(mv, 60, "ListStatus_transact_time", "transact_time");
    expect_text(mv, 66, "ListStatus_list_id", "list_id");
    expect_text(mv, 68, "68", "tot_no_orders");
    expect_text(mv, 82, "82", "no_rpts");
}

TEST_F(BuilderRoundtripV42, AllocationInstructionAck) {
    fixpp::v42::AllocationInstructionAckArgs args{};
    args.text = "AllocationInstructionAck_text";
    args.transact_time = "AllocationInstructionAck_transact_time";
    args.alloc_status = 87;
    args.alloc_rej_code = 88;
    auto built = fixpp::v42::build_AllocationInstructionAck(out, args);
    ASSERT_TRUE(built.has_value()) << "build_AllocationInstructionAck failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built AllocationInstructionAck frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "P", "msg_type");
    expect_text(mv, 58, "AllocationInstructionAck_text", "text");
    expect_text(mv, 60, "AllocationInstructionAck_transact_time", "transact_time");
    expect_text(mv, 87, "87", "alloc_status");
    expect_text(mv, 88, "88", "alloc_rej_code");
}

TEST_F(BuilderRoundtripV42, DontKnowTrade) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v42::DontKnowTradeArgs args{};
    args.exec_id = "DontKnowTrade_exec_id";
    args.id_source = "DontKnowTrade_id_source";
    args.last_px = make_decimal("31.5", &arena);
    args.last_shares = make_decimal("32.5", &arena);
    args.side = '1';
    args.dk_reason = 'A';
    auto built = fixpp::v42::build_DontKnowTrade(out, args);
    ASSERT_TRUE(built.has_value()) << "build_DontKnowTrade failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built DontKnowTrade frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "Q", "msg_type");
    expect_text(mv, 17, "DontKnowTrade_exec_id", "exec_id");
    expect_text(mv, 22, "DontKnowTrade_id_source", "id_source");
    expect_decimal(mv, 31, "31.5", &read_arena, "last_px");
    expect_decimal(mv, 32, "32.5", &read_arena, "last_shares");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 127, "A", "dk_reason");
}

TEST_F(BuilderRoundtripV42, QuoteRequest) {
    fixpp::v42::QuoteRequestArgs args{};
    args.quote_req_id = "QuoteRequest_quote_req_id";
    auto built = fixpp::v42::build_QuoteRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_QuoteRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built QuoteRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "R", "msg_type");
    expect_text(mv, 131, "QuoteRequest_quote_req_id", "quote_req_id");
}

TEST_F(BuilderRoundtripV42, Quote) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v42::QuoteArgs args{};
    args.currency = "Quote_currency";
    args.id_source = "Quote_id_source";
    args.ord_type = '1';
    args.bid_px = make_decimal("132.5", &arena);
    args.offer_px = make_decimal("133.5", &arena);
    args.put_or_call = 201;
    auto built = fixpp::v42::build_Quote(out, args);
    ASSERT_TRUE(built.has_value()) << "build_Quote failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built Quote frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "S", "msg_type");
    expect_text(mv, 15, "Quote_currency", "currency");
    expect_text(mv, 22, "Quote_id_source", "id_source");
    expect_text(mv, 40, "1", "ord_type");
    expect_decimal(mv, 132, "132.5", &read_arena, "bid_px");
    expect_decimal(mv, 133, "133.5", &read_arena, "offer_px");
    expect_text(mv, 201, "201", "put_or_call");
}

TEST_F(BuilderRoundtripV42, SettlementInstructions) {
    fixpp::v42::SettlementInstructionsArgs args{};
    args.last_mkt = "SettlementInstructions_last_mkt";
    args.side = '1';
    args.transact_time = "SettlementInstructions_transact_time";
    args.settl_inst_mode = '0';
    args.stand_inst_db_type = 169;
    args.settl_delivery_type = 172;
    auto built = fixpp::v42::build_SettlementInstructions(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SettlementInstructions failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SettlementInstructions frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "T", "msg_type");
    expect_text(mv, 30, "SettlementInstructions_last_mkt", "last_mkt");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 60, "SettlementInstructions_transact_time", "transact_time");
    expect_text(mv, 160, "0", "settl_inst_mode");
    expect_text(mv, 169, "169", "stand_inst_db_type");
    expect_text(mv, 172, "172", "settl_delivery_type");
}

TEST_F(BuilderRoundtripV42, MarketDataRequest) {
    fixpp::v42::MarketDataRequestArgs args{};
    args.md_req_id = "MarketDataRequest_md_req_id";
    args.subscription_request_type = '0';
    args.market_depth = 264;
    args.md_update_type = 265;
    args.aggregated_book = true;
    auto built = fixpp::v42::build_MarketDataRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MarketDataRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "V", "msg_type");
    expect_text(mv, 262, "MarketDataRequest_md_req_id", "md_req_id");
    expect_text(mv, 263, "0", "subscription_request_type");
    expect_text(mv, 264, "264", "market_depth");
    expect_text(mv, 265, "265", "md_update_type");
    expect_text(mv, 266, "Y", "aggregated_book");
}

TEST_F(BuilderRoundtripV42, MarketDataSnapshotFullRefresh) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v42::MarketDataSnapshotFullRefreshArgs args{};
    args.id_source = "MarketDataSnapshotFullRefresh_id_source";
    args.security_id = "MarketDataSnapshotFullRefresh_security_id";
    args.put_or_call = 201;
    args.strike_price = make_decimal("202.5", &arena);
    args.maturity_day = 205;
    args.opt_attribute = '0';
    auto built = fixpp::v42::build_MarketDataSnapshotFullRefresh(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataSnapshotFullRefresh failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MarketDataSnapshotFullRefresh frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "W", "msg_type");
    expect_text(mv, 22, "MarketDataSnapshotFullRefresh_id_source", "id_source");
    expect_text(mv, 48, "MarketDataSnapshotFullRefresh_security_id", "security_id");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "202.5", &read_arena, "strike_price");
    expect_text(mv, 205, "205", "maturity_day");
    expect_text(mv, 206, "0", "opt_attribute");
}

TEST_F(BuilderRoundtripV42, MarketDataIncrementalRefresh) {
    fixpp::v42::MarketDataIncrementalRefreshArgs args{};
    args.md_req_id = "MarketDataIncrementalRefresh_md_req_id";
    auto built = fixpp::v42::build_MarketDataIncrementalRefresh(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataIncrementalRefresh failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MarketDataIncrementalRefresh frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "X", "msg_type");
    expect_text(mv, 262, "MarketDataIncrementalRefresh_md_req_id", "md_req_id");
}

TEST_F(BuilderRoundtripV42, MarketDataRequestReject) {
    fixpp::v42::MarketDataRequestRejectArgs args{};
    args.text = "MarketDataRequestReject_text";
    args.md_req_id = "MarketDataRequestReject_md_req_id";
    args.md_req_rej_reason = '0';
    auto built = fixpp::v42::build_MarketDataRequestReject(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataRequestReject failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MarketDataRequestReject frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "Y", "msg_type");
    expect_text(mv, 58, "MarketDataRequestReject_text", "text");
    expect_text(mv, 262, "MarketDataRequestReject_md_req_id", "md_req_id");
    expect_text(mv, 281, "0", "md_req_rej_reason");
}

TEST_F(BuilderRoundtripV42, QuoteCancel) {
    fixpp::v42::QuoteCancelArgs args{};
    args.quote_id = "QuoteCancel_quote_id";
    args.quote_req_id = "QuoteCancel_quote_req_id";
    args.quote_cancel_type = 298;
    args.quote_response_level = 301;
    auto built = fixpp::v42::build_QuoteCancel(out, args);
    ASSERT_TRUE(built.has_value()) << "build_QuoteCancel failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built QuoteCancel frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "Z", "msg_type");
    expect_text(mv, 117, "QuoteCancel_quote_id", "quote_id");
    expect_text(mv, 131, "QuoteCancel_quote_req_id", "quote_req_id");
    expect_text(mv, 298, "298", "quote_cancel_type");
    expect_text(mv, 301, "301", "quote_response_level");
}

TEST_F(BuilderRoundtripV42, QuoteStatusRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v42::QuoteStatusRequestArgs args{};
    args.id_source = "QuoteStatusRequest_id_source";
    args.security_id = "QuoteStatusRequest_security_id";
    args.side = '1';
    args.put_or_call = 201;
    args.strike_price = make_decimal("202.5", &arena);
    args.maturity_day = 205;
    auto built = fixpp::v42::build_QuoteStatusRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_QuoteStatusRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built QuoteStatusRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "a", "msg_type");
    expect_text(mv, 22, "QuoteStatusRequest_id_source", "id_source");
    expect_text(mv, 48, "QuoteStatusRequest_security_id", "security_id");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "202.5", &read_arena, "strike_price");
    expect_text(mv, 205, "205", "maturity_day");
}

TEST_F(BuilderRoundtripV42, QuoteAcknowledgement) {
    fixpp::v42::QuoteAcknowledgementArgs args{};
    args.text = "QuoteAcknowledgement_text";
    args.quote_id = "QuoteAcknowledgement_quote_id";
    args.quote_status = 297;
    args.quote_reject_reason = 300;
    auto built = fixpp::v42::build_QuoteAcknowledgement(out, args);
    ASSERT_TRUE(built.has_value()) << "build_QuoteAcknowledgement failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built QuoteAcknowledgement frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "b", "msg_type");
    expect_text(mv, 58, "QuoteAcknowledgement_text", "text");
    expect_text(mv, 117, "QuoteAcknowledgement_quote_id", "quote_id");
    expect_text(mv, 297, "297", "quote_status");
    expect_text(mv, 300, "300", "quote_reject_reason");
}

TEST_F(BuilderRoundtripV42, SecurityDefinitionRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v42::SecurityDefinitionRequestArgs args{};
    args.currency = "SecurityDefinitionRequest_currency";
    args.id_source = "SecurityDefinitionRequest_id_source";
    args.put_or_call = 201;
    args.strike_price = make_decimal("202.5", &arena);
    args.maturity_day = 205;
    args.opt_attribute = '0';
    auto built = fixpp::v42::build_SecurityDefinitionRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityDefinitionRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SecurityDefinitionRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "c", "msg_type");
    expect_text(mv, 15, "SecurityDefinitionRequest_currency", "currency");
    expect_text(mv, 22, "SecurityDefinitionRequest_id_source", "id_source");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "202.5", &read_arena, "strike_price");
    expect_text(mv, 205, "205", "maturity_day");
    expect_text(mv, 206, "0", "opt_attribute");
}

TEST_F(BuilderRoundtripV42, SecurityDefinition) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v42::SecurityDefinitionArgs args{};
    args.currency = "SecurityDefinition_currency";
    args.id_source = "SecurityDefinition_id_source";
    args.put_or_call = 201;
    args.strike_price = make_decimal("202.5", &arena);
    args.maturity_day = 205;
    args.opt_attribute = '0';
    auto built = fixpp::v42::build_SecurityDefinition(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityDefinition failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SecurityDefinition frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "d", "msg_type");
    expect_text(mv, 15, "SecurityDefinition_currency", "currency");
    expect_text(mv, 22, "SecurityDefinition_id_source", "id_source");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "202.5", &read_arena, "strike_price");
    expect_text(mv, 205, "205", "maturity_day");
    expect_text(mv, 206, "0", "opt_attribute");
}

TEST_F(BuilderRoundtripV42, SecurityStatusRequest) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v42::SecurityStatusRequestArgs args{};
    args.currency = "SecurityStatusRequest_currency";
    args.id_source = "SecurityStatusRequest_id_source";
    args.put_or_call = 201;
    args.strike_price = make_decimal("202.5", &arena);
    args.maturity_day = 205;
    args.opt_attribute = '0';
    auto built = fixpp::v42::build_SecurityStatusRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityStatusRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SecurityStatusRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "e", "msg_type");
    expect_text(mv, 15, "SecurityStatusRequest_currency", "currency");
    expect_text(mv, 22, "SecurityStatusRequest_id_source", "id_source");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "202.5", &read_arena, "strike_price");
    expect_text(mv, 205, "205", "maturity_day");
    expect_text(mv, 206, "0", "opt_attribute");
}

TEST_F(BuilderRoundtripV42, SecurityStatus) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v42::SecurityStatusArgs args{};
    args.currency = "SecurityStatus_currency";
    args.id_source = "SecurityStatus_id_source";
    args.last_px = make_decimal("31.5", &arena);
    args.put_or_call = 201;
    args.strike_price = make_decimal("202.5", &arena);
    args.maturity_day = 205;
    auto built = fixpp::v42::build_SecurityStatus(out, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityStatus failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built SecurityStatus frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "f", "msg_type");
    expect_text(mv, 15, "SecurityStatus_currency", "currency");
    expect_text(mv, 22, "SecurityStatus_id_source", "id_source");
    expect_decimal(mv, 31, "31.5", &read_arena, "last_px");
    expect_text(mv, 201, "201", "put_or_call");
    expect_decimal(mv, 202, "202.5", &read_arena, "strike_price");
    expect_text(mv, 205, "205", "maturity_day");
}

TEST_F(BuilderRoundtripV42, TradingSessionStatusRequest) {
    fixpp::v42::TradingSessionStatusRequestArgs args{};
    args.subscription_request_type = '0';
    args.trad_ses_req_id = "TradingSessionStatusRequest_trad_ses_req_id";
    args.trading_session_id = "TradingSessionStatusRequest_trading_session_id";
    args.trad_ses_method = 338;
    args.trad_ses_mode = 339;
    auto built = fixpp::v42::build_TradingSessionStatusRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TradingSessionStatusRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TradingSessionStatusRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "g", "msg_type");
    expect_text(mv, 263, "0", "subscription_request_type");
    expect_text(mv, 335, "TradingSessionStatusRequest_trad_ses_req_id", "trad_ses_req_id");
    expect_text(mv, 336, "TradingSessionStatusRequest_trading_session_id", "trading_session_id");
    expect_text(mv, 338, "338", "trad_ses_method");
    expect_text(mv, 339, "339", "trad_ses_mode");
}

TEST_F(BuilderRoundtripV42, TradingSessionStatus) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v42::TradingSessionStatusArgs args{};
    args.text = "TradingSessionStatus_text";
    args.unsolicited_indicator = true;
    args.trad_ses_req_id = "TradingSessionStatus_trad_ses_req_id";
    args.trad_ses_method = 338;
    args.trad_ses_mode = 339;
    args.total_volume_traded = make_decimal("387.5", &arena);
    auto built = fixpp::v42::build_TradingSessionStatus(out, args);
    ASSERT_TRUE(built.has_value()) << "build_TradingSessionStatus failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TradingSessionStatus frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "h", "msg_type");
    expect_text(mv, 58, "TradingSessionStatus_text", "text");
    expect_text(mv, 325, "Y", "unsolicited_indicator");
    expect_text(mv, 335, "TradingSessionStatus_trad_ses_req_id", "trad_ses_req_id");
    expect_text(mv, 338, "338", "trad_ses_method");
    expect_text(mv, 339, "339", "trad_ses_mode");
    expect_decimal(mv, 387, "387.5", &read_arena, "total_volume_traded");
}

TEST_F(BuilderRoundtripV42, MassQuote) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v42::MassQuoteArgs args{};
    args.quote_id = "MassQuote_quote_id";
    args.quote_req_id = "MassQuote_quote_req_id";
    args.def_bid_size = make_decimal("293.5", &arena);
    args.def_offer_size = make_decimal("294.5", &arena);
    args.quote_response_level = 301;
    auto built = fixpp::v42::build_MassQuote(out, args);
    ASSERT_TRUE(built.has_value()) << "build_MassQuote failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built MassQuote frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "i", "msg_type");
    expect_text(mv, 117, "MassQuote_quote_id", "quote_id");
    expect_text(mv, 131, "MassQuote_quote_req_id", "quote_req_id");
    expect_decimal(mv, 293, "293.5", &read_arena, "def_bid_size");
    expect_decimal(mv, 294, "294.5", &read_arena, "def_offer_size");
    expect_text(mv, 301, "301", "quote_response_level");
}

TEST_F(BuilderRoundtripV42, BusinessMessageReject) {
    fixpp::v42::BusinessMessageRejectArgs args{};
    args.ref_seq_num = 45;
    args.text = "BusinessMessageReject_text";
    args.ref_msg_type = "BusinessMessageReject_ref_msg_type";
    args.business_reject_reason = 380;
    auto built = fixpp::v42::build_BusinessMessageReject(out, args);
    ASSERT_TRUE(built.has_value()) << "build_BusinessMessageReject failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built BusinessMessageReject frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "j", "msg_type");
    expect_text(mv, 45, "45", "ref_seq_num");
    expect_text(mv, 58, "BusinessMessageReject_text", "text");
    expect_text(mv, 372, "BusinessMessageReject_ref_msg_type", "ref_msg_type");
    expect_text(mv, 380, "380", "business_reject_reason");
}

TEST_F(BuilderRoundtripV42, BidRequest) {
    fixpp::v42::BidRequestArgs args{};
    args.currency = "BidRequest_currency";
    args.text = "BidRequest_text";
    args.forex_req = true;
    args.bid_request_trans_type = 'C';
    args.total_num_securities = 393;
    args.bid_type = 394;
    auto built = fixpp::v42::build_BidRequest(out, args);
    ASSERT_TRUE(built.has_value()) << "build_BidRequest failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built BidRequest frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "k", "msg_type");
    expect_text(mv, 15, "BidRequest_currency", "currency");
    expect_text(mv, 58, "BidRequest_text", "text");
    expect_text(mv, 121, "Y", "forex_req");
    expect_text(mv, 374, "C", "bid_request_trans_type");
    expect_text(mv, 393, "393", "total_num_securities");
    expect_text(mv, 394, "394", "bid_type");
}

TEST_F(BuilderRoundtripV42, BidResponse) {
    fixpp::v42::BidResponseArgs args{};
    args.bid_id = "BidResponse_bid_id";
    args.client_bid_id = "BidResponse_client_bid_id";
    auto built = fixpp::v42::build_BidResponse(out, args);
    ASSERT_TRUE(built.has_value()) << "build_BidResponse failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built BidResponse frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "l", "msg_type");
    expect_text(mv, 390, "BidResponse_bid_id", "bid_id");
    expect_text(mv, 391, "BidResponse_client_bid_id", "client_bid_id");
}

TEST_F(BuilderRoundtripV42, ListStrikePrice) {
    fixpp::v42::ListStrikePriceArgs args{};
    args.list_id = "ListStrikePrice_list_id";
    args.tot_no_strikes = 422;
    auto built = fixpp::v42::build_ListStrikePrice(out, args);
    ASSERT_TRUE(built.has_value()) << "build_ListStrikePrice failed";
    std::string const body = bytes_to_string(*built);
    std::vector<std::byte> const frame = make_frame("FIX.4.2", body);
    auto const mv = parse_dict(frame, *tv_, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built ListStrikePrice frame failed (see ADD_FAILURE above)";
    expect_text(mv, 35, "m", "msg_type");
    expect_text(mv, 66, "ListStrikePrice_list_id", "list_id");
    expect_text(mv, 422, "422", "tot_no_strikes");
}
