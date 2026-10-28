// SPDX-License-Identifier: AGPL-3.0-or-later
// test_096_tail_rows_roundtrip.cpp
//
// 096-fix-latest-tail-evidence, v50sp2 leg: one offline witness per FIX Latest tail MsgType (suite
// TailRows096), the census holding that suite to the tail table, and the walk's field leg
// compared with the QuickFIX-cpp golden (suite TailRows096Census). A source of the
// test_077_allversions_builder_roundtrip executable, beside 095's TU; the walk, the body
// enumeration, the presence check and check_witness are 095's, shared through
// support/tail_rows_witness.hpp (contracts W0). The tail table, the census and the field-leg
// comparison are shared with the vlatest tier through support/tail_rows_096.hpp.
//
// THE RULE (095 spec Terminology; 096 data-model §B.1), applied to dictionaries/FIX50SP2.xml at
// test runtime by the shared Walker:
//   - a field is required when every element on its path from the message (each enclosing
//     component reference, group and the field itself) is required='Y';
//   - a group is required when every element on its path is required, where a group that is the
//     ONLY child of a component (children counted on the component's definition as written, a
//     nested component reference counting as one) inherits the required flag of the reference that
//     wraps it;
//   - a group instance's required members follow the same rules, starting from the group;
//   - a group's delimiter is its first emitted tag in dictionary order with components expanded.
//     When it is a nested group's count tag, that nested group carries exactly one instance.
// The walk fails loudly, naming the element, on a required group inside a required group's
// instance, on a delimiter chain longer than one nested group, and on a required component
// reference whose component holds a group among siblings.
//
// No count, member list or per-MsgType outcome of the walk is recorded here (contracts W7): the
// walk re-derives them on every run. To re-derive one by hand:
//   grep -o "<message name='[^']*' msgtype='<MT>'" dictionaries/FIX50SP2.xml
//   sed -n "/<message name='<Name>'/,/<\/message>/p" dictionaries/FIX50SP2.xml
//   sed -n "/<component name='<Comp>'>/,/<\/component>/p" dictionaries/FIX50SP2.xml
// and apply the rule above to each component on the path. The field leg is compared with
//   grep '^FIX50SP2,<MT>,' tools/quickfix_required_golden/golden.csv
// (QuickFIX-cpp's message-level set; it does not carry the group rule).

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/v50sp2/groups/G_1676_1Args.hpp>
#include <fixpp/v50sp2/groups/G_1772_2Args.hpp>
#include <fixpp/v50sp2/groups/G_2428_1Args.hpp>
#include <fixpp/v50sp2/groups/G_2474_1Args.hpp>
#include <fixpp/v50sp2/groups/G_2474_2Args.hpp>
#include <fixpp/v50sp2/groups/G_453Args.hpp>
#include <fixpp/v50sp2/messages/CrossRequest.hpp>
#include <fixpp/v50sp2/messages/CrossRequestAck.hpp>
#include <fixpp/v50sp2/messages/MarketDataReport.hpp>
#include <fixpp/v50sp2/messages/MarketDataStatisticsReport.hpp>
#include <fixpp/v50sp2/messages/MarketDataStatisticsRequest.hpp>
#include <fixpp/v50sp2/messages/MassOrder.hpp>
#include <fixpp/v50sp2/messages/MassOrderAck.hpp>
#include <fixpp/v50sp2/messages/PartyActionReport.hpp>
#include <fixpp/v50sp2/messages/PartyActionRequest.hpp>
#include <fixpp/v50sp2/messages/PartyDetailsDefinitionRequest.hpp>
#include <fixpp/v50sp2/messages/PartyDetailsDefinitionRequestAck.hpp>
#include <fixpp/v50sp2/messages/PartyDetailsListReport.hpp>
#include <fixpp/v50sp2/messages/PartyDetailsListRequest.hpp>
#include <fixpp/v50sp2/messages/PartyDetailsListUpdateReport.hpp>
#include <fixpp/v50sp2/messages/PartyEntitlementsDefinitionRequest.hpp>
#include <fixpp/v50sp2/messages/PartyEntitlementsDefinitionRequestAck.hpp>
#include <fixpp/v50sp2/messages/PartyEntitlementsReport.hpp>
#include <fixpp/v50sp2/messages/PartyEntitlementsRequest.hpp>
#include <fixpp/v50sp2/messages/PartyEntitlementsUpdateReport.hpp>
#include <fixpp/v50sp2/messages/PartyRiskLimitCheckRequest.hpp>
#include <fixpp/v50sp2/messages/PartyRiskLimitCheckRequestAck.hpp>
#include <fixpp/v50sp2/messages/PartyRiskLimitsDefinitionRequest.hpp>
#include <fixpp/v50sp2/messages/PartyRiskLimitsDefinitionRequestAck.hpp>
#include <fixpp/v50sp2/messages/PartyRiskLimitsReport.hpp>
#include <fixpp/v50sp2/messages/PartyRiskLimitsReportAck.hpp>
#include <fixpp/v50sp2/messages/PartyRiskLimitsRequest.hpp>
#include <fixpp/v50sp2/messages/PartyRiskLimitsUpdateReport.hpp>
#include <fixpp/v50sp2/messages/QuoteAck.hpp>
#include <fixpp/v50sp2/messages/SecurityMassStatus.hpp>
#include <fixpp/v50sp2/messages/SecurityMassStatusRequest.hpp>
#include <fixpp/v50sp2/messages/TradeMatchReport.hpp>
#include <memory>
#include <memory_resource>
#include <optional>
#include <pugixml.hpp>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/app_message_read_scaffold.hpp"
#include "support/tail_rows_096.hpp"
#include "support/tail_rows_witness.hpp"

namespace {

using fixpp_test_support::bytes_to_string;
// The walk, the body enumeration and check_witness live in support/tail_rows_witness.hpp.
using namespace fixpp_test_support::tail_rows;
// The tail table, the census and the field-leg comparison live in support/tail_rows_096.hpp.
using fixpp_test_support::tail_rows_096::census_findings;
using fixpp_test_support::tail_rows_096::golden_field_leg_findings;
using fixpp_test_support::tail_rows_096::tail_rows;
using fixpp_test_support::tail_rows_096::TailRow;

// ── Fixture: the dictionary, loaded once ─────────────────────────────────────

class Tail096Fixture : public ::testing::Test {
protected:
    struct Shared {
        pugi::xml_document doc;
        std::string load_error;
        std::pmr::monotonic_buffer_resource dict_arena{65536};
        std::optional<fixpp::dict::Dictionary> dict;
        std::optional<fixpp::dict::table_view> tv;
        WalkMap walk;
    };

    static void SetUpTestSuite() {
        if (shared_) return;
        shared_ = std::make_unique<Shared>();
        std::string const path = std::string(FIXPP_DICT_DATA_DIR) + "/FIX50SP2.xml";
        if (pugi::xml_parse_result const r = shared_->doc.load_file(path.c_str()); !r) {
            shared_->load_error = "pugixml could not load " + path + ": " + r.description();
            return;
        }
        shared_->walk = Walker{shared_->doc}.walk_all();
        fixpp::dict::XmlLoader loader;
        shared_->dict.emplace(loader.load(path, &shared_->dict_arena));
        shared_->tv.emplace(shared_->dict->as_table_view());
    }

    void SetUp() override { ASSERT_TRUE(shared_->load_error.empty()) << shared_->load_error; }

    static pugi::xml_document const& doc() { return shared_->doc; }
    static WalkMap const& walk() { return shared_->walk; }
    // tv is emplaced on every path that leaves load_error empty, and SetUp() fails the test
    // before a witness body runs otherwise.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    static fixpp::dict::table_view const& tv() { return *shared_->tv; }

    // W3–W5 for one witness: frame, independent parse, tag 35, both-direction readback,
    // presence check. The expected MsgType is the one the test's own name maps to.
    void check_witness(std::string const& body, Intent const& intent) {
        std::string const test_name =
            ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::string const expected_mt =
            msgtype_for_generated_name(doc(), split_test_name(test_name).second);
        fixpp_test_support::tail_rows::check_witness(walk(), tv(), &read_arena_, expected_mt, body,
                                                     intent);
    }

    std::array<std::byte, 131072> out_{};
    std::pmr::monotonic_buffer_resource read_arena_{65536};

    static std::unique_ptr<Shared> shared_;
};

std::unique_ptr<Tail096Fixture::Shared> Tail096Fixture::shared_;

class TailRows096 : public Tail096Fixture {};
class TailRows096Census : public Tail096Fixture {};

}  // namespace

// ── Census and the field leg ─────────────────────────────────────────────────

// Holds suite TailRows096 (selected by its exact name: TailRows096 is a prefix of other suites in
// this binary) to the tail table: every finding is collected before the one assertion.
TEST_F(TailRows096Census, EveryTailMsgTypeHasExactlyOneWitness) {
    std::vector<std::string> const findings = census_findings(
        "TailRows096", tail_rows(),
        [](std::string_view generated) { return msgtype_for_generated_name(doc(), generated); },
        "message", "FIX50SP2.xml");
    EXPECT_TRUE(findings.empty()) << "census findings: " << join(findings);
}

TEST_F(TailRows096Census, FieldLegMatchesQuickFixCppGolden) {
    std::vector<std::string> asserted;
    for (TailRow const& r : tail_rows()) asserted.emplace_back(r.msgtype);
    for (std::string const& f : golden_field_leg_findings(walk(), asserted, "walk")) {
        ADD_FAILURE() << f;
    }
}

// ── Witnesses: one per tail MsgType (contracts W1–W5) ─────────────────────────
//
// Each value is written twice, as the same literal: once in its intent entry, once in its Args
// setter (data-model §C). Neither statement reads the other.

TEST_F(TailRows096, A035_CrossRequest) {
    Intent intent;
    fixpp::v50sp2::CrossRequestArgs args{};
    intent.emplace_back("2672", "A035_CrossRequest_cross_request_id");
    args.cross_request_id = "A035_CrossRequest_cross_request_id";
    intent.emplace_back("376", "A035_CrossRequest_compliance_id");
    args.compliance_id = "A035_CrossRequest_compliance_id";
    auto const built = fixpp::v50sp2::build_CrossRequest(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_CrossRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A036_CrossRequestAck) {
    Intent intent;
    fixpp::v50sp2::CrossRequestAckArgs args{};
    intent.emplace_back("2672", "A036_CrossRequestAck_cross_request_id");
    args.cross_request_id = "A036_CrossRequestAck_cross_request_id";
    intent.emplace_back("376", "A036_CrossRequestAck_compliance_id");
    args.compliance_id = "A036_CrossRequestAck_compliance_id";
    auto const built = fixpp::v50sp2::build_CrossRequestAck(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_CrossRequestAck refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A037_MassOrder) {
    Intent intent;
    fixpp::v50sp2::MassOrderArgs args{};
    std::array<fixpp::v50sp2::groups::G_2428_1Args, 2> order_entries{};
    intent.emplace_back("2423", "A037_MassOrder_mass_order_request_id");
    args.mass_order_request_id = "A037_MassOrder_mass_order_request_id";
    intent.emplace_back("58", "A037_MassOrder_text");
    args.text = "A037_MassOrder_text";
    intent.emplace_back("2428", "2");
    intent.emplace_back("2428[0].2429", "1");
    order_entries[0].order_entry_action = '1';
    intent.emplace_back("2428[0].11", "A037_MassOrder_cl_ord_id_i0");
    order_entries[0].cl_ord_id = "A037_MassOrder_cl_ord_id_i0";
    intent.emplace_back("2428[1].2429", "1");
    order_entries[1].order_entry_action = '1';
    intent.emplace_back("2428[1].11", "A037_MassOrder_cl_ord_id_i1");
    order_entries[1].cl_ord_id = "A037_MassOrder_cl_ord_id_i1";
    args.order_entries = std::span<fixpp::v50sp2::groups::G_2428_1Args const>{order_entries};
    auto const built = fixpp::v50sp2::build_MassOrder(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_MassOrder refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A038_MassOrderAck) {
    Intent intent;
    fixpp::v50sp2::MassOrderAckArgs args{};
    intent.emplace_back("2425", "1");
    args.mass_order_request_status = 1;
    intent.emplace_back("1815", "1");
    args.trading_capacity = 1;
    auto const built = fixpp::v50sp2::build_MassOrderAck(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_MassOrderAck refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A039_TradeMatchReport) {
    Intent intent;
    fixpp::v50sp2::TradeMatchReportArgs args{};
    intent.emplace_back("880", "A039_TradeMatchReport_trd_match_id");
    args.trd_match_id = "A039_TradeMatchReport_trd_match_id";
    intent.emplace_back("1300", "A039_TradeMatchReport_market_segment_id");
    args.market_segment_id = "A039_TradeMatchReport_market_segment_id";
    auto const built = fixpp::v50sp2::build_TradeMatchReport(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_TradeMatchReport refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A040_MarketDataReport) {
    Intent intent;
    fixpp::v50sp2::MarketDataReportArgs args{};
    intent.emplace_back("2535", "1");
    args.md_report_event = 1;
    intent.emplace_back("2536", "2536");
    args.md_report_count = 2536;
    auto const built = fixpp::v50sp2::build_MarketDataReport(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataReport refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A041_MarketDataStatisticsRequest) {
    Intent intent;
    fixpp::v50sp2::MarketDataStatisticsRequestArgs args{};
    std::array<fixpp::v50sp2::groups::G_2474_1Args, 2> md_statistics{};
    intent.emplace_back("2452", "A041_MarketDataStatisticsRequest_md_statistic_req_id");
    args.md_statistic_req_id = "A041_MarketDataStatisticsRequest_md_statistic_req_id";
    intent.emplace_back("263", "0");
    args.subscription_request_type = '0';
    intent.emplace_back("2474", "2");
    intent.emplace_back("2474[0].2475", "A041_MarketDataStatisticsRequest_md_statistic_id_i0");
    md_statistics[0].md_statistic_id = "A041_MarketDataStatisticsRequest_md_statistic_id_i0";
    intent.emplace_back("2474[1].2475", "A041_MarketDataStatisticsRequest_md_statistic_id_i1");
    md_statistics[1].md_statistic_id = "A041_MarketDataStatisticsRequest_md_statistic_id_i1";
    args.md_statistics = std::span<fixpp::v50sp2::groups::G_2474_1Args const>{md_statistics};
    auto const built = fixpp::v50sp2::build_MarketDataStatisticsRequest(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataStatisticsRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A042_MarketDataStatisticsReport) {
    Intent intent;
    fixpp::v50sp2::MarketDataStatisticsReportArgs args{};
    std::array<fixpp::v50sp2::groups::G_2474_2Args, 2> md_statistics{};
    intent.emplace_back("2453", "A042_MarketDataStatisticsReport_md_statistic_rpt_id");
    args.md_statistic_rpt_id = "A042_MarketDataStatisticsReport_md_statistic_rpt_id";
    intent.emplace_back("2474", "2");
    intent.emplace_back("2474[0].2456", "1");
    md_statistics[0].md_statistic_type = 1;
    intent.emplace_back("2474[0].2475", "A042_MarketDataStatisticsReport_md_statistic_id_i0");
    md_statistics[0].md_statistic_id = "A042_MarketDataStatisticsReport_md_statistic_id_i0";
    intent.emplace_back("2474[1].2456", "1");
    md_statistics[1].md_statistic_type = 1;
    intent.emplace_back("2474[1].2475", "A042_MarketDataStatisticsReport_md_statistic_id_i1");
    md_statistics[1].md_statistic_id = "A042_MarketDataStatisticsReport_md_statistic_id_i1";
    args.md_statistics = std::span<fixpp::v50sp2::groups::G_2474_2Args const>{md_statistics};
    auto const built = fixpp::v50sp2::build_MarketDataStatisticsReport(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_MarketDataStatisticsReport refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A043_PartyDetailsListRequest) {
    Intent intent;
    fixpp::v50sp2::PartyDetailsListRequestArgs args{};
    intent.emplace_back("1505", "A043_PartyDetailsListRequest_party_details_list_request_id");
    args.party_details_list_request_id =
        "A043_PartyDetailsListRequest_party_details_list_request_id";
    intent.emplace_back("58", "A043_PartyDetailsListRequest_text");
    args.text = "A043_PartyDetailsListRequest_text";
    auto const built = fixpp::v50sp2::build_PartyDetailsListRequest(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyDetailsListRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A044_PartyDetailsListReport) {
    Intent intent;
    fixpp::v50sp2::PartyDetailsListReportArgs args{};
    intent.emplace_back("1510", "A044_PartyDetailsListReport_party_details_list_report_id");
    args.party_details_list_report_id = "A044_PartyDetailsListReport_party_details_list_report_id";
    intent.emplace_back("58", "A044_PartyDetailsListReport_text");
    args.text = "A044_PartyDetailsListReport_text";
    auto const built = fixpp::v50sp2::build_PartyDetailsListReport(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyDetailsListReport refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A045_PartyDetailsListUpdateReport) {
    Intent intent;
    fixpp::v50sp2::PartyDetailsListUpdateReportArgs args{};
    intent.emplace_back("1510", "A045_PartyDetailsListUpdateReport_party_details_list_report_id");
    args.party_details_list_report_id =
        "A045_PartyDetailsListUpdateReport_party_details_list_report_id";
    intent.emplace_back("58", "A045_PartyDetailsListUpdateReport_text");
    args.text = "A045_PartyDetailsListUpdateReport_text";
    auto const built = fixpp::v50sp2::build_PartyDetailsListUpdateReport(out_, args);
    ASSERT_TRUE(built.has_value())
        << "build_PartyDetailsListUpdateReport refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

// NoPartyUpdates' instances carry no string or int member: ListUpdateAction, the delimiter, takes
// in instance k the k-th value FIX50SP2.xml declares for it (data-model §C, implementation
// decision). Re-derive: sed -n "/<field number='1324'/,/<\/field>/p" dictionaries/FIX50SP2.xml
TEST_F(TailRows096, A046_PartyDetailsDefinitionRequest) {
    Intent intent;
    fixpp::v50sp2::PartyDetailsDefinitionRequestArgs args{};
    std::array<fixpp::v50sp2::groups::G_1676_1Args, 2> party_updates{};
    intent.emplace_back("1505", "A046_PartyDetailsDefinitionRequest_party_details_list_request_id");
    args.party_details_list_request_id =
        "A046_PartyDetailsDefinitionRequest_party_details_list_request_id";
    intent.emplace_back("1676", "2");
    intent.emplace_back("1676[0].1324", "A");
    party_updates[0].list_update_action = 'A';
    intent.emplace_back("1676[1].1324", "D");
    party_updates[1].list_update_action = 'D';
    args.party_updates = std::span<fixpp::v50sp2::groups::G_1676_1Args const>{party_updates};
    auto const built = fixpp::v50sp2::build_PartyDetailsDefinitionRequest(out_, args);
    ASSERT_TRUE(built.has_value())
        << "build_PartyDetailsDefinitionRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A047_PartyDetailsDefinitionRequestAck) {
    Intent intent;
    fixpp::v50sp2::PartyDetailsDefinitionRequestAckArgs args{};
    intent.emplace_back("1505",
                        "A047_PartyDetailsDefinitionRequestAck_party_details_list_request_id");
    args.party_details_list_request_id =
        "A047_PartyDetailsDefinitionRequestAck_party_details_list_request_id";
    intent.emplace_back("1878", "0");
    args.party_detail_request_status = 0;
    auto const built = fixpp::v50sp2::build_PartyDetailsDefinitionRequestAck(out_, args);
    ASSERT_TRUE(built.has_value())
        << "build_PartyDetailsDefinitionRequestAck refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A048_PartyEntitlementsRequest) {
    Intent intent;
    fixpp::v50sp2::PartyEntitlementsRequestArgs args{};
    intent.emplace_back("1770", "A048_PartyEntitlementsRequest_entitlement_request_id");
    args.entitlement_request_id = "A048_PartyEntitlementsRequest_entitlement_request_id";
    intent.emplace_back("58", "A048_PartyEntitlementsRequest_text");
    args.text = "A048_PartyEntitlementsRequest_text";
    auto const built = fixpp::v50sp2::build_PartyEntitlementsRequest(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyEntitlementsRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A049_PartyEntitlementsReport) {
    Intent intent;
    fixpp::v50sp2::PartyEntitlementsReportArgs args{};
    intent.emplace_back("1771", "A049_PartyEntitlementsReport_entitlement_report_id");
    args.entitlement_report_id = "A049_PartyEntitlementsReport_entitlement_report_id";
    intent.emplace_back("58", "A049_PartyEntitlementsReport_text");
    args.text = "A049_PartyEntitlementsReport_text";
    auto const built = fixpp::v50sp2::build_PartyEntitlementsReport(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyEntitlementsReport refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A050_PartyEntitlementsUpdateReport) {
    Intent intent;
    fixpp::v50sp2::PartyEntitlementsUpdateReportArgs args{};
    std::array<fixpp::v50sp2::groups::G_1772_2Args, 2> party_entitlements{};
    intent.emplace_back("1771", "A050_PartyEntitlementsUpdateReport_entitlement_report_id");
    args.entitlement_report_id = "A050_PartyEntitlementsUpdateReport_entitlement_report_id";
    intent.emplace_back("1772", "2");
    intent.emplace_back("1772[0].1324", "A");
    party_entitlements[0].list_update_action = 'A';
    intent.emplace_back("1772[0].1885", "A050_PartyEntitlementsUpdateReport_entitlement_ref_id_i0");
    party_entitlements[0].entitlement_ref_id =
        "A050_PartyEntitlementsUpdateReport_entitlement_ref_id_i0";
    intent.emplace_back("1772[1].1324", "A");
    party_entitlements[1].list_update_action = 'A';
    intent.emplace_back("1772[1].1885", "A050_PartyEntitlementsUpdateReport_entitlement_ref_id_i1");
    party_entitlements[1].entitlement_ref_id =
        "A050_PartyEntitlementsUpdateReport_entitlement_ref_id_i1";
    args.party_entitlements =
        std::span<fixpp::v50sp2::groups::G_1772_2Args const>{party_entitlements};
    auto const built = fixpp::v50sp2::build_PartyEntitlementsUpdateReport(out_, args);
    ASSERT_TRUE(built.has_value())
        << "build_PartyEntitlementsUpdateReport refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A051_PartyEntitlementsDefinitionRequest) {
    Intent intent;
    fixpp::v50sp2::PartyEntitlementsDefinitionRequestArgs args{};
    std::array<fixpp::v50sp2::groups::G_1772_2Args, 2> party_entitlements{};
    intent.emplace_back("1770", "A051_PartyEntitlementsDefinitionRequest_entitlement_request_id");
    args.entitlement_request_id = "A051_PartyEntitlementsDefinitionRequest_entitlement_request_id";
    intent.emplace_back("1772", "2");
    intent.emplace_back("1772[0].1324", "A");
    party_entitlements[0].list_update_action = 'A';
    intent.emplace_back("1772[0].1885",
                        "A051_PartyEntitlementsDefinitionRequest_entitlement_ref_id_i0");
    party_entitlements[0].entitlement_ref_id =
        "A051_PartyEntitlementsDefinitionRequest_entitlement_ref_id_i0";
    intent.emplace_back("1772[1].1324", "A");
    party_entitlements[1].list_update_action = 'A';
    intent.emplace_back("1772[1].1885",
                        "A051_PartyEntitlementsDefinitionRequest_entitlement_ref_id_i1");
    party_entitlements[1].entitlement_ref_id =
        "A051_PartyEntitlementsDefinitionRequest_entitlement_ref_id_i1";
    args.party_entitlements =
        std::span<fixpp::v50sp2::groups::G_1772_2Args const>{party_entitlements};
    auto const built = fixpp::v50sp2::build_PartyEntitlementsDefinitionRequest(out_, args);
    ASSERT_TRUE(built.has_value())
        << "build_PartyEntitlementsDefinitionRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A052_PartyEntitlementsDefinitionRequestAck) {
    Intent intent;
    fixpp::v50sp2::PartyEntitlementsDefinitionRequestAckArgs args{};
    intent.emplace_back("1770",
                        "A052_PartyEntitlementsDefinitionRequestAck_entitlement_request_id");
    args.entitlement_request_id =
        "A052_PartyEntitlementsDefinitionRequestAck_entitlement_request_id";
    intent.emplace_back("1882", "1882");
    args.entitlement_request_status = 1882;
    auto const built = fixpp::v50sp2::build_PartyEntitlementsDefinitionRequestAck(out_, args);
    ASSERT_TRUE(built.has_value())
        << "build_PartyEntitlementsDefinitionRequestAck refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A053_PartyRiskLimitsRequest) {
    Intent intent;
    fixpp::v50sp2::PartyRiskLimitsRequestArgs args{};
    intent.emplace_back("1666", "A053_PartyRiskLimitsRequest_risk_limit_request_id");
    args.risk_limit_request_id = "A053_PartyRiskLimitsRequest_risk_limit_request_id";
    intent.emplace_back("58", "A053_PartyRiskLimitsRequest_text");
    args.text = "A053_PartyRiskLimitsRequest_text";
    auto const built = fixpp::v50sp2::build_PartyRiskLimitsRequest(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyRiskLimitsRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A054_PartyRiskLimitsReport) {
    Intent intent;
    fixpp::v50sp2::PartyRiskLimitsReportArgs args{};
    intent.emplace_back("1667", "A054_PartyRiskLimitsReport_risk_limit_report_id");
    args.risk_limit_report_id = "A054_PartyRiskLimitsReport_risk_limit_report_id";
    intent.emplace_back("58", "A054_PartyRiskLimitsReport_text");
    args.text = "A054_PartyRiskLimitsReport_text";
    auto const built = fixpp::v50sp2::build_PartyRiskLimitsReport(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyRiskLimitsReport refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A055_PartyRiskLimitsUpdateReport) {
    Intent intent;
    fixpp::v50sp2::PartyRiskLimitsUpdateReportArgs args{};
    intent.emplace_back("1667", "A055_PartyRiskLimitsUpdateReport_risk_limit_report_id");
    args.risk_limit_report_id = "A055_PartyRiskLimitsUpdateReport_risk_limit_report_id";
    intent.emplace_back("58", "A055_PartyRiskLimitsUpdateReport_text");
    args.text = "A055_PartyRiskLimitsUpdateReport_text";
    auto const built = fixpp::v50sp2::build_PartyRiskLimitsUpdateReport(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyRiskLimitsUpdateReport refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A056_PartyRiskLimitsDefinitionRequest) {
    Intent intent;
    fixpp::v50sp2::PartyRiskLimitsDefinitionRequestArgs args{};
    intent.emplace_back("1666", "A056_PartyRiskLimitsDefinitionRequest_risk_limit_request_id");
    args.risk_limit_request_id = "A056_PartyRiskLimitsDefinitionRequest_risk_limit_request_id";
    intent.emplace_back("58", "A056_PartyRiskLimitsDefinitionRequest_text");
    args.text = "A056_PartyRiskLimitsDefinitionRequest_text";
    auto const built = fixpp::v50sp2::build_PartyRiskLimitsDefinitionRequest(out_, args);
    ASSERT_TRUE(built.has_value())
        << "build_PartyRiskLimitsDefinitionRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A057_PartyRiskLimitsDefinitionRequestAck) {
    Intent intent;
    fixpp::v50sp2::PartyRiskLimitsDefinitionRequestAckArgs args{};
    intent.emplace_back("1666", "A057_PartyRiskLimitsDefinitionRequestAck_risk_limit_request_id");
    args.risk_limit_request_id = "A057_PartyRiskLimitsDefinitionRequestAck_risk_limit_request_id";
    intent.emplace_back("1762", "1762");
    args.risk_limit_request_status = 1762;
    auto const built = fixpp::v50sp2::build_PartyRiskLimitsDefinitionRequestAck(out_, args);
    ASSERT_TRUE(built.has_value())
        << "build_PartyRiskLimitsDefinitionRequestAck refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A058_PartyRiskLimitsReportAck) {
    Intent intent;
    fixpp::v50sp2::PartyRiskLimitsReportAckArgs args{};
    intent.emplace_back("1667", "A058_PartyRiskLimitsReportAck_risk_limit_report_id");
    args.risk_limit_report_id = "A058_PartyRiskLimitsReportAck_risk_limit_report_id";
    intent.emplace_back("2316", "0");
    args.risk_limit_report_status = 0;
    auto const built = fixpp::v50sp2::build_PartyRiskLimitsReportAck(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyRiskLimitsReportAck refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A059_PartyActionRequest) {
    Intent intent;
    fixpp::v50sp2::PartyActionRequestArgs args{};
    std::array<fixpp::v50sp2::groups::G_453Args, 2> party_i_ds{};
    intent.emplace_back("2328", "A059_PartyActionRequest_party_action_request_id");
    args.party_action_request_id = "A059_PartyActionRequest_party_action_request_id";
    intent.emplace_back("2329", "0");
    args.party_action_type = 0;
    intent.emplace_back("453", "2");
    intent.emplace_back("453[0].448", "A059_PartyActionRequest_party_id_i0");
    party_i_ds[0].party_id = "A059_PartyActionRequest_party_id_i0";
    intent.emplace_back("453[1].448", "A059_PartyActionRequest_party_id_i1");
    party_i_ds[1].party_id = "A059_PartyActionRequest_party_id_i1";
    args.party_i_ds = std::span<fixpp::v50sp2::groups::G_453Args const>{party_i_ds};
    auto const built = fixpp::v50sp2::build_PartyActionRequest(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyActionRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A060_PartyActionReport) {
    Intent intent;
    fixpp::v50sp2::PartyActionReportArgs args{};
    std::array<fixpp::v50sp2::groups::G_453Args, 2> party_i_ds{};
    intent.emplace_back("2331", "A060_PartyActionReport_party_action_report_id");
    args.party_action_report_id = "A060_PartyActionReport_party_action_report_id";
    intent.emplace_back("2329", "0");
    args.party_action_type = 0;
    intent.emplace_back("2332", "0");
    args.party_action_response = 0;
    intent.emplace_back("453", "2");
    intent.emplace_back("453[0].448", "A060_PartyActionReport_party_id_i0");
    party_i_ds[0].party_id = "A060_PartyActionReport_party_id_i0";
    intent.emplace_back("453[1].448", "A060_PartyActionReport_party_id_i1");
    party_i_ds[1].party_id = "A060_PartyActionReport_party_id_i1";
    args.party_i_ds = std::span<fixpp::v50sp2::groups::G_453Args const>{party_i_ds};
    auto const built = fixpp::v50sp2::build_PartyActionReport(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyActionReport refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A061_PartyRiskLimitCheckRequest) {
    Intent intent;
    fixpp::v50sp2::PartyRiskLimitCheckRequestArgs args{};
    intent.emplace_back("2320", "0");
    args.risk_limit_check_trans_type = 0;
    intent.emplace_back("2321", "0");
    args.risk_limit_check_type = 0;
    auto const built = fixpp::v50sp2::build_PartyRiskLimitCheckRequest(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_PartyRiskLimitCheckRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A062_PartyRiskLimitCheckRequestAck) {
    Intent intent;
    fixpp::v50sp2::PartyRiskLimitCheckRequestAckArgs args{};
    intent.emplace_back("2325", "0");
    args.risk_limit_check_request_status = 0;
    intent.emplace_back("2320", "0");
    args.risk_limit_check_trans_type = 0;
    intent.emplace_back("2321", "0");
    args.risk_limit_check_type = 0;
    auto const built = fixpp::v50sp2::build_PartyRiskLimitCheckRequestAck(out_, args);
    ASSERT_TRUE(built.has_value())
        << "build_PartyRiskLimitCheckRequestAck refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A063_SecurityMassStatusRequest) {
    Intent intent;
    fixpp::v50sp2::SecurityMassStatusRequestArgs args{};
    intent.emplace_back("324", "A063_SecurityMassStatusRequest_security_status_req_id");
    args.security_status_req_id = "A063_SecurityMassStatusRequest_security_status_req_id";
    intent.emplace_back("263", "0");
    args.subscription_request_type = '0';
    auto const built = fixpp::v50sp2::build_SecurityMassStatusRequest(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityMassStatusRequest refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A064_SecurityMassStatus) {
    Intent intent;
    fixpp::v50sp2::SecurityMassStatusArgs args{};
    intent.emplace_back("324", "A064_SecurityMassStatus_security_status_req_id");
    args.security_status_req_id = "A064_SecurityMassStatus_security_status_req_id";
    intent.emplace_back("1300", "A064_SecurityMassStatus_market_segment_id");
    args.market_segment_id = "A064_SecurityMassStatus_market_segment_id";
    auto const built = fixpp::v50sp2::build_SecurityMassStatus(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_SecurityMassStatus refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}

TEST_F(TailRows096, A065_QuoteAck) {
    Intent intent;
    fixpp::v50sp2::QuoteAckArgs args{};
    intent.emplace_back("1865", "0");
    args.quote_ack_status = 0;
    intent.emplace_back("117", "A065_QuoteAck_quote_id");
    args.quote_id = "A065_QuoteAck_quote_id";
    intent.emplace_back("131", "A065_QuoteAck_quote_req_id");
    args.quote_req_id = "A065_QuoteAck_quote_req_id";
    auto const built = fixpp::v50sp2::build_QuoteAck(out_, args);
    ASSERT_TRUE(built.has_value()) << "build_QuoteAck refused the witness build";
    std::string const body = bytes_to_string(*built);
    check_witness(body, intent);
}
