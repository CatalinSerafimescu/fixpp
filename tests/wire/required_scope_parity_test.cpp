// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/required_scope_parity_test.cpp
//
// 079-required-presence-scope T019 (fixpp#201) — Contract 2: QuickFIX
// required-set parity. STANDALONE binary (own executable + `add_test(NAME
// required_scope_parity)`), per `[const §VII.8]`: an exact-set completeness
// gate is isolation-sensitive. Selected via `-R required_scope_parity`
// (Article VII §8 carve-out).
//
// Links NO QuickFIX: consumes the checked-in golden
// (tools/quickfix_required_golden/golden.csv, produced by T018 against a
// REAL, locally built QuickFIX v1.16.0) and compares it against the SAME
// independent pugixml oracle (`required_scope_oracle.hpp`) used by Contract
// 1's census (tests/dictionary/required_scope_census_test.cpp) — reused, not
// forked, per the task's single-oracle guarantee.
//
// SCOPE (body-only — see tools/quickfix_required_golden/main.cpp's header
// comment for the full justification): `FIX::DataDictionary::isRequiredField`
// has no header/trailer-required surface at all, so the golden carries only
// the message-BODY component-AND required set. This test therefore builds
// the oracle with `include_header_trailer=false`
// (`build_quickfix_oracle(path, false)`), corroborating ONLY the
// component-AND rule Contract 2 exists to corroborate ("confirms the
// independent walker encodes the AND-rule faithfully"). The
// StandardHeader/Trailer carve-out itself stays pinned by Contract 1's
// census only — contracts/census-and-agreement.md's Contract 2 text
// ("Header/trailer fields appear as ordinary required fields in the
// per-message set") is CONTRADICTED by the real QuickFIX source and is
// flagged as an escalation, not silently absorbed
// ([[feedback_parity_corpus_row_needs_a_surface_the_reference_engine_has]]).
//
// NO vlatest/Orchestra row: QuickFIX 1.16.0 cannot parse Orchestra — a
// parity row for an absent surface would go spuriously RED. vlatest is
// covered by Contract 1 only.
//
// Anchors:
//   tasks:     specs/079-required-presence-scope/tasks.md T018/T019
//   contracts: specs/079-required-presence-scope/contracts/census-and-agreement.md
//              Contract 2
//   spec:      specs/079-required-presence-scope/spec.md FR-010/SC-005

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "required_scope_oracle.hpp"  // the shared independent oracle (079 T018/T019)

namespace {

using fixpp_test::required_scope_oracle::build_quickfix_oracle;

// ── Minimal CSV parser matching the generator's own quoting scheme
// (RFC4180-style: a field wrapped in double quotes, internal quotes
// doubled) — mirrors tests/wire/enum_golden_manifest_test.cpp's
// split_csv_line, kept local rather than shared since it is a trivial,
// non-oracle utility. ──────────────────────────────────────────────────────
std::vector<std::string> split_csv_line(std::string const& line) {
    std::vector<std::string> fields;
    std::string cur;
    bool in_quotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    cur += '"';
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                cur += c;
            }
        } else {
            if (c == '"') {
                in_quotes = true;
            } else if (c == ',') {
                fields.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
    }
    fields.push_back(cur);
    return fields;
}

std::vector<std::string> read_lines(std::string const& path) {
    std::vector<std::string> lines;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        ADD_FAILURE() << "cannot open '" << path << "'";
        return lines;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

std::set<std::uint16_t> parse_tag_list(std::string const& s) {
    std::set<std::uint16_t> out;
    std::istringstream iss(s);
    std::uint32_t tag = 0;
    while (iss >> tag) {
        out.insert(static_cast<std::uint16_t>(tag));
    }
    return out;
}

// dictionary -> msg_type -> required tag set, as recorded in golden.csv.
using GoldenRows = std::map<std::string, std::map<std::string, std::set<std::uint16_t>>>;

GoldenRows load_golden(std::string const& csv_path) {
    GoldenRows rows;
    auto const lines = read_lines(csv_path);
    bool seen_header = false;
    for (auto const& line : lines) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (!seen_header) {
            // "dictionary,msg_type,required_tags" — skip it.
            seen_header = true;
            continue;
        }
        auto const fields = split_csv_line(line);
        if (fields.size() != 3) {
            ADD_FAILURE() << "malformed golden.csv data row (expected 3 fields): " << line;
            continue;
        }
        rows[fields[0]].emplace(fields[1], parse_tag_list(fields[2]));
    }
    return rows;
}

std::string describe_diff(std::set<std::uint16_t> const& expected, std::set<std::uint16_t> const& actual) {
    std::vector<std::uint16_t> missing;
    std::vector<std::uint16_t> extra;
    std::ranges::set_difference(expected, actual, std::back_inserter(missing));
    std::ranges::set_difference(actual, expected, std::back_inserter(extra));
    std::ostringstream oss;
    if (!missing.empty()) {
        oss << "missing-from-golden{";
        for (auto t : missing) {
            oss << t << ",";
        }
        oss << "} ";
    }
    if (!extra.empty()) {
        oss << "extra-in-golden{";
        for (auto t : extra) {
            oss << t << ",";
        }
        oss << "}";
    }
    return oss.str();
}

struct DictCase {
    std::string label;
    std::string filename;
};

std::vector<DictCase> const kQuickfixDicts{
    {"FIX40", "FIX40.xml"},     {"FIX41", "FIX41.xml"},     {"FIX42", "FIX42.xml"},
    {"FIX43", "FIX43.xml"},     {"FIX44", "FIX44.xml"},     {"FIX50", "FIX50.xml"},
    {"FIX50SP1", "FIX50SP1.xml"}, {"FIX50SP2", "FIX50SP2.xml"}, {"FIXT11", "FIXT11.xml"},
};

}  // namespace

// ============================================================================
// T019 — Contract 2: quickfix_required_set(dict, msg) == expected(dict, msg)
// (the census oracle), exact set-equality per message, across the 9
// QuickFIX-schema dicts.
// ============================================================================
TEST(RequiredScopeParity, QuickFixGoldenMatchesOracleAcrossNineDicts) {
#ifndef FIXPP_REQUIRED_GOLDEN_CSV
#error "FIXPP_REQUIRED_GOLDEN_CSV must be defined by CMake"
#endif
#ifndef FIXPP_DICT_DATA_DIR
#error "FIXPP_DICT_DATA_DIR must be defined by CMake"
#endif
    std::cout << "\n=== 079 T019: QuickFIX required-set parity (9 dicts, Contract 2) ===\n";

    GoldenRows const golden = load_golden(FIXPP_REQUIRED_GOLDEN_CSV);
    ASSERT_FALSE(golden.empty()) << "golden.csv produced zero data rows — parse failure?";

    std::size_t total_messages = 0;
    for (auto const& dc : kQuickfixDicts) {
        auto const xml_path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / dc.filename;
        auto const oracle = build_quickfix_oracle(xml_path, /*include_header_trailer=*/false);

        auto const git = golden.find(dc.label);
        ASSERT_NE(git, golden.end()) << "golden.csv has no rows for dictionary " << dc.label;
        auto const& golden_msgs = git->second;

        // Both-directions message-type coverage: the golden's message set
        // must equal the oracle's message set for this dict (a missing or
        // extra message row is itself a completeness defect, not just a
        // per-tag mismatch).
        std::set<std::string> oracle_msg_types;
        for (auto const& [msg_type, req] : oracle.message_required) {
            (void)req;
            oracle_msg_types.insert(msg_type);
        }
        std::set<std::string> golden_msg_types;
        for (auto const& [msg_type, req] : golden_msgs) {
            (void)req;
            golden_msg_types.insert(msg_type);
        }
        EXPECT_EQ(oracle_msg_types, golden_msg_types)
            << dc.label << ": golden.csv message-type set differs from the oracle's — "
               "regenerate the golden (quickfix_required_golden_regen_diff) or investigate a "
               "genuine dictionary drift";

        std::size_t checked = 0;
        for (auto const& [msg_type, expected] : oracle.message_required) {
            auto const mit = golden_msgs.find(msg_type);
            if (mit == golden_msgs.end()) {
                continue;  // already flagged by the set-equality check above
            }
            EXPECT_EQ(expected, mit->second)
                << dc.label << " " << msg_type << ": " << describe_diff(expected, mit->second);
            ++checked;
        }
        std::cout << "  " << dc.label << ": " << checked << " message(s) checked\n";
        total_messages += checked;
    }

    std::cout << "  total messages checked across 9 QuickFIX dicts: " << total_messages << "\n";
    EXPECT_GT(total_messages, 0u);
}
