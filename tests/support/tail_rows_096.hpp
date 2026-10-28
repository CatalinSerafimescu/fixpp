// SPDX-License-Identifier: AGPL-3.0-or-later
// tail_rows_096.hpp
//
// 096-fix-latest-tail-evidence: what both tiers' tail TUs share so that they cannot drift apart:
//   - the one tail table (data-model §A; the catalogue row is the authority);
//   - the census (contracts W1): the findings for one suite, selected by its exact name from the
//     gtest registry, held to the tail table through the tier's own name → MsgType lookup;
//   - the walk's field leg against the QuickFIX-cpp golden (contracts W6), read at test time from
//     FIXPP_095_QUICKFIX_GOLDEN_CSV, which every including target defines.
// Each tier keeps its own walk, dictionary and witnesses; only the table and the two checks above
// live here. The namespace is distinct from fixpp_test_support::tail_rows because 095's TU, built
// into the same executable as the v50sp2 tier, defines its own TailRow and tail_rows() and imports
// that namespace whole.
//
// No count or per-MsgType outcome of either check is recorded here (contracts W7): each re-derives
// its findings on every run. To re-derive the field leg's golden side for one MsgType by hand:
//   grep '^FIX50SP2,<MT>,' tools/quickfix_required_golden/golden.csv

#pragma once

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/tail_rows_witness.hpp"

namespace fixpp_test_support::tail_rows_096 {

struct TailRow {
    char const* row;
    char const* msgtype;
};

inline std::vector<TailRow> const& tail_rows() {
    static std::vector<TailRow> const rows = {
        {.row = "A-035", .msgtype = "DS"},  // CrossRequest
        {.row = "A-036", .msgtype = "DT"},  // CrossRequestAck
        {.row = "A-037", .msgtype = "DJ"},  // MassOrder
        {.row = "A-038", .msgtype = "DK"},  // MassOrderAck
        {.row = "A-039", .msgtype = "DC"},  // TradeMatchReport
        {.row = "A-040", .msgtype = "DR"},  // MarketDataReport
        {.row = "A-041", .msgtype = "DO"},  // MarketDataStatisticsRequest
        {.row = "A-042", .msgtype = "DP"},  // MarketDataStatisticsReport
        {.row = "A-043", .msgtype = "CF"},  // PartyDetailsListRequest
        {.row = "A-044", .msgtype = "CG"},  // PartyDetailsListReport
        {.row = "A-045", .msgtype = "CK"},  // PartyDetailsListUpdateReport
        {.row = "A-046", .msgtype = "CX"},  // PartyDetailsDefinitionRequest
        {.row = "A-047", .msgtype = "CY"},  // PartyDetailsDefinitionRequestAck
        {.row = "A-048", .msgtype = "CU"},  // PartyEntitlementsRequest
        {.row = "A-049", .msgtype = "CV"},  // PartyEntitlementsReport
        {.row = "A-050", .msgtype = "CZ"},  // PartyEntitlementsUpdateReport
        {.row = "A-051", .msgtype = "DA"},  // PartyEntitlementsDefinitionRequest
        {.row = "A-052", .msgtype = "DB"},  // PartyEntitlementsDefinitionRequestAck
        {.row = "A-053", .msgtype = "CL"},  // PartyRiskLimitsRequest
        {.row = "A-054", .msgtype = "CM"},  // PartyRiskLimitsReport
        {.row = "A-055", .msgtype = "CR"},  // PartyRiskLimitsUpdateReport
        {.row = "A-056", .msgtype = "CS"},  // PartyRiskLimitsDefinitionRequest
        {.row = "A-057", .msgtype = "CT"},  // PartyRiskLimitsDefinitionRequestAck
        {.row = "A-058", .msgtype = "DE"},  // PartyRiskLimitsReportAck
        {.row = "A-059", .msgtype = "DH"},  // PartyActionRequest
        {.row = "A-060", .msgtype = "DI"},  // PartyActionReport
        {.row = "A-061", .msgtype = "DF"},  // PartyRiskLimitCheckRequest
        {.row = "A-062", .msgtype = "DG"},  // PartyRiskLimitCheckRequestAck
        {.row = "A-063", .msgtype = "CN"},  // SecurityMassStatusRequest
        {.row = "A-064", .msgtype = "CO"},  // SecurityMassStatus
        {.row = "A-065", .msgtype = "CW"},  // QuoteAck
    };
    return rows;
}

// The census findings for suite `suite` (selected by its exact name, never by prefix) against
// `rows`: every registered test of that suite is mapped to a MsgType through `msgtype_of`, and
// every finding is collected before returning. The kinds: an unmapped name (no `<element name=…>`
// in `dict_file`), an extra MsgType, a row mismatch, and a multiplicity other than one per MsgType.
inline std::vector<std::string> census_findings(
    std::string_view suite, std::vector<TailRow> const& rows,
    std::function<std::string(std::string_view)> const& msgtype_of, std::string_view element,
    std::string_view dict_file) {
    struct Expected {
        std::string row;
        int multiplicity = 0;
    };
    std::map<std::string, Expected> by_msgtype;
    for (TailRow const& r : rows) by_msgtype[r.msgtype] = {.row = r.row, .multiplicity = 0};
    std::vector<std::string> findings;
    ::testing::UnitTest const* const ut = ::testing::UnitTest::GetInstance();
    for (int s = 0; s < ut->total_test_suite_count(); ++s) {
        ::testing::TestSuite const* const ts = ut->GetTestSuite(s);
        if (std::string_view{ts->name()} != suite) continue;
        for (int t = 0; t < ts->total_test_count(); ++t) {
            std::string const name = ts->GetTestInfo(t)->name();
            auto const [row, generated] = ::fixpp_test_support::tail_rows::split_test_name(name);
            std::string const mt = msgtype_of(generated);
            if (mt.empty()) {
                std::string f = "unmapped name ";
                f += suite;
                f += ".";
                f += name;
                f += ": no <";
                f += element;
                f += " name='";
                f += generated;
                f += "'> in ";
                f += dict_file;
                findings.push_back(std::move(f));
                continue;
            }
            auto const it = by_msgtype.find(mt);
            if (it == by_msgtype.end()) {
                std::string f = "extra MsgType 35=";
                f += mt;
                f += " (";
                f += suite;
                f += ".";
                f += name;
                f += ")";
                findings.push_back(std::move(f));
                continue;
            }
            ++it->second.multiplicity;
            if (::fixpp_test_support::tail_rows::drop_hyphen(it->second.row) != row) {
                std::string f = "row mismatch: ";
                f += suite;
                f += ".";
                f += name;
                f += " names row ";
                f += row;
                f += ", 35=";
                f += mt;
                f += " is row ";
                f += it->second.row;
                findings.push_back(std::move(f));
            }
        }
    }
    for (auto const& [mt, e] : by_msgtype) {
        if (e.multiplicity != 1) {
            findings.push_back("35=" + mt + " has multiplicity " + std::to_string(e.multiplicity) +
                               " (exactly one witness required)");
        }
    }
    return findings;
}

// The field leg against golden.csv for `msg_types`: first the precondition (exactly one
// `FIX50SP2,<MT>,` row per MsgType), and only when it holds for every MsgType, set equality between
// each MsgType's message-level required fields in `walk` and its golden row. `walk_label` names the
// walk in the findings. Returns at the first finding that leaves nothing further to compare.
inline std::vector<std::string> golden_field_leg_findings(
    ::fixpp_test_support::tail_rows::WalkMap const& walk, std::vector<std::string> const& msg_types,
    std::string_view walk_label) {
    std::vector<std::string> findings;
    std::ifstream in{FIXPP_095_QUICKFIX_GOLDEN_CSV};
    if (!in) {
        findings.push_back(std::string{"cannot open "} + FIXPP_095_QUICKFIX_GOLDEN_CSV);
        return findings;
    }
    std::map<std::string, std::vector<std::set<std::uint16_t>>> rows;
    for (std::string line; std::getline(in, line);) {
        if (!line.starts_with("FIX50SP2,")) continue;
        std::size_t const c2 = line.find(',', 9);
        if (c2 == std::string::npos) continue;
        std::string const mt = line.substr(9, c2 - 9);
        std::string tags = line.substr(c2 + 1);
        std::erase(tags, '"');
        std::set<std::uint16_t> set;
        std::istringstream ts{tags};
        for (unsigned t = 0; ts >> t;) set.insert(static_cast<std::uint16_t>(t));
        rows[mt].push_back(std::move(set));
    }
    for (std::string const& mt : msg_types) {
        std::size_t const n = rows.contains(mt) ? rows[mt].size() : 0;
        if (n != 1) {
            findings.push_back("golden.csv holds " + std::to_string(n) + " rows FIX50SP2," + mt +
                               ", (exactly one required)");
        }
    }
    if (!findings.empty()) return findings;
    for (std::string const& mt : msg_types) {
        auto const it = walk.find(mt);
        if (it == walk.end()) {
            findings.push_back("35=" + mt + " is not in the " + std::string{walk_label});
            return findings;
        }
        std::set<std::uint16_t> const walked(it->second.fields.begin(), it->second.fields.end());
        std::set<std::uint16_t> const& golden = rows[mt].front();
        if (walked != golden) {
            findings.push_back("35=" + mt + ": " + std::string{walk_label} + " " +
                               ::fixpp_test_support::tail_rows::join_tags(walked) +
                               ", golden.csv " +
                               ::fixpp_test_support::tail_rows::join_tags(golden));
        }
    }
    return findings;
}

}  // namespace fixpp_test_support::tail_rows_096
