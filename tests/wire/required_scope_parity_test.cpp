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
using fixpp_test::required_scope_oracle::GroupContextKey;

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

// ── 081 T020: per-group golden (tools/quickfix_required_golden/golden_groups.csv)
// -- SEPARATE file from golden.csv (5-column row shape: dictionary, msg_type,
// group_path, no_tag, required_tags). See main.cpp's T020 header-comment
// banner for why it is not appended to golden.csv. ──────────────────────────

std::vector<std::uint16_t> parse_path_list(std::string const& s) {
    std::vector<std::uint16_t> out;
    std::istringstream iss(s);
    std::uint32_t tag = 0;
    while (iss >> tag) {
        out.push_back(static_cast<std::uint16_t>(tag));
    }
    return out;
}

// dictionary -> GroupContextKey -> required tag set, as recorded in
// golden_groups.csv.
using GoldenGroupRows = std::map<std::string, std::map<GroupContextKey, std::set<std::uint16_t>>>;

GoldenGroupRows load_golden_groups(std::string const& csv_path) {
    GoldenGroupRows rows;
    auto const lines = read_lines(csv_path);
    bool seen_header = false;
    for (auto const& line : lines) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (!seen_header) {
            // "dictionary,msg_type,group_path,no_tag,required_tags" — skip it.
            seen_header = true;
            continue;
        }
        auto const fields = split_csv_line(line);
        if (fields.size() != 5) {
            ADD_FAILURE() << "malformed golden_groups.csv data row (expected 5 fields): " << line;
            continue;
        }
        GroupContextKey key{fields[1], parse_path_list(fields[2]),
                             static_cast<std::uint16_t>(std::stoul(fields[3]))};
        rows[fields[0]].emplace(std::move(key), parse_tag_list(fields[4]));
    }
    return rows;
}

std::string describe_context(GroupContextKey const& key) {
    std::ostringstream oss;
    oss << "msg=" << key.msg_type << " path={";
    for (auto t : key.path) {
        oss << t << ",";
    }
    oss << "} no_tag=" << key.no_tag;
    return oss.str();
}

// ── 081-strict-validation-residuals T020 — named, source-verified
// stricter-superset carve-out (W-204-1 lineage; orchestrator/user-approved
// waiver, 2026-07-19). See the TEST()'s own header-comment banner below for
// the full root-cause writeup; this table is the load-bearing PIN, not a
// blanket skip — for each named context it asserts `oracle == golden ∪
// extra_tags` EXACTLY, so it goes RED if the divergence grows to a 4th
// context, if the extra tag set differs, or if the oracle-side required set
// at one of these 3 contexts changes for any other reason. ──────────────────
struct KnownSupersetContext {
    std::string dict;
    GroupContextKey key;
    std::set<std::uint16_t> extra_tags;  // required per the oracle (D-3), NOT required per real
                                          // QuickFIX (golden) — fixpp is the stricter superset here.
};

std::vector<KnownSupersetContext> const kKnownSupersetContexts{
    // FIX44/FIX50/FIX50SP1 MassQuote('i') / NoQuoteSets(296): QuickFIX
    // `DataDictionary::addXMLGroup` (DataDictionary.cpp:562-563) hardcodes
    // `addXMLComponentFields(pDoc, node, msgtype, groupDD,
    // /*componentRequired=*/false)` for ANY <component> sitting DIRECTLY
    // inside a <group> — ignoring both the component's own `required=` and
    // the enclosing group's own `required=`. Here NoQuoteSets(296,
    // required='Y') directly contains <component name="QuotEntryGrp"
    // required="Y">, whose sole child is <group name="NoQuoteEntries"(295)
    // required="Y">. QuickFIX still DISCOVERS 295 as a nested group on
    // NoQuoteSets's own sub-DD (an ungated `addGroup` call,
    // DataDictionary.cpp:576/585 — the context-KEY set matches exactly, see
    // the exhaustive check above) but never marks it required at that level
    // (the `addRequiredField` gate IS conditioned on the hardcoded-false
    // `componentRequired`). D-3's immediate-enclosing-group rule never
    // modeled this component-in-group quirk; fixpp/the oracle stay
    // spec-faithful and correctly require 295 here (a required direct member
    // of a required group) — a STRICTER SUPERSET of QuickFIX, safe direction
    // (no false-accept), same lineage as 079's waived W-204-1. FIX50SP2's
    // `QuotSetGrp` has `NoQuoteSets required="N"` (not "Y"), so the whole
    // subtree is optional either way and the quirk never manifests there —
    // no 4th context.
    {"FIX44", GroupContextKey{"i", {}, 296}, {295}},
    {"FIX50", GroupContextKey{"i", {}, 296}, {295}},
    {"FIX50SP1", GroupContextKey{"i", {}, 296}, {295}},
};

std::set<std::uint16_t> const* find_known_superset_extra(std::string const& dict, GroupContextKey const& key) {
    for (auto const& c : kKnownSupersetContexts) {
        if (c.dict == dict && !(c.key < key) && !(key < c.key)) {  // GroupContextKey has only operator<
            return &c.extra_tags;
        }
    }
    return nullptr;
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

// ============================================================================
// 081-strict-validation-residuals T020 — Concern B per-group parity:
// quickfix_group_required_set(dict, msg, path, no_tag) ==
// oracle.group_required(dict, msg, path, no_tag), exact set-equality, for
// EVERY real group context across the 9 QuickFIX-schema dicts, BOTH
// directions on the context-KEY set (not just tag-content once matched) —
// [[feedback_completeness_gate_exact_set_not_subset]].
//
// Non-circularity triangle (see contracts/census-and-parity.md "Parity
// golden contract" + `required_scope_census_test.cpp` T017 leg 1): the
// census already pins oracle == the loaded `table_view` store
// (`PerGroupContextStoreMatchesWalkerExceptL0661GroupBlindDicts`); this test
// pins golden == oracle; so golden == the loaded store TRANSITIVELY — that
// is why this binary rightly links no fixpp dictionary loader and compares
// golden directly against the SAME independent oracle (`required_scope_oracle.hpp`)
// rather than re-loading a `Dictionary` here.
//
// The golden's group CONTEXT DISCOVERY (isGroup/getGroup) and REQUIRED-NESS
// (isRequiredField on the returned sub-DataDictionary) are both real,
// measured QuickFIX v1.16.0 API calls (tools/quickfix_required_golden/main.cpp
// T020 banner) — independent of the oracle's own raw-XML walk
// (required_scope_oracle.hpp T012 rework, D-3 immediate-enclosing
// group-gating). Body-only surface (same rationale as the message-level test
// above): `include_header_trailer=false`.
//
// ⚠️ NAMED STRICTER-SUPERSET CARVE-OUT (081, W-204-1 lineage; orchestrator +
// user-approved 2026-07-19 — NOT a blanket skip):
// exhaustive across all 29,301 real body group contexts in the 9 QuickFIX
// dicts, the context-KEY SET matches exactly both directions (QuickFIX and
// the oracle agree on WHICH group contexts exist everywhere) — but 3
// contexts disagree on required-CONTENT: FIX44/FIX50/FIX50SP1 MassQuote('i')
// / NoQuoteSets(296) — oracle says {295,302,304} required, golden (real
// QuickFIX) says {302,304} (295 = NoQuoteEntries, missing).
//
// ROOT CAUSE (verified against `reference-engines/quickfix-cpp/src/C++/
// DataDictionary.cpp:562-563,569,584-586`): `DataDictionary::addXMLGroup`
// hardcodes `componentRequired=false` for ANY `<component>` sitting DIRECTLY
// inside a `<group>` (`addXMLComponentFields(pDoc, node, msgtype, groupDD,
// /*componentRequired=*/false)` — ignores BOTH the component's own
// `required=` AND the enclosing group's own `required=` entirely for the
// gate on `addRequiredField`). Here `QuotSetGrp`'s `<group name="NoQuoteSets"
// required="Y">` directly contains `<component name="QuotEntryGrp"
// required="Y">`, whose sole child is `<group name="NoQuoteEntries"
// required="Y">` (FIX44.xml `<components>`). QuickFIX's addXMLComponentFields
// STILL registers NoQuoteEntries as a discoverable nested group on
// NoQuoteSets's own sub-DD (`DD.addGroup(...)`, an UNGATED code path,
// DataDictionary.cpp:576/585) — so `isGroup` agrees on the context existing
// — but never calls `addRequiredField(msgtype, 295)` on that sub-DD (the
// FIRST `if(name=="field"||name=="group")` block's `addRequiredField` call
// IS gated on `componentRequired`, which is unconditionally false for a
// group-nested component). FIX50SP2's `QuotSetGrp` has `NoQuoteSets
// required="N"` (not "Y") — the whole subtree is optional either way, so
// this quirk never manifests there; that is WHY FIX50SP2 is silent while
// FIX44/FIX50/FIX50SP1 (all `NoQuoteSets required="Y"`) are not.
//
// NOT a D-3 bug: D-3 ("immediate enclosing group's own required=") is a
// group-DIRECT-member rule; NoQuoteEntries(295) genuinely IS a direct member
// of NoQuoteSets(296) whose own `required='Y'` AND whose immediate enclosing
// group NoQuoteSets is ALSO `required='Y'` — the oracle (and, transitively
// via the T017 census pin, the loaded `table_view` store) is CORRECTLY
// spec-defensible per D-3's own stated rule. This is fixpp being a STRICTER
// SUPERSET of QuickFIX at exactly 3 contexts — same direction/shape as
// W-204-1 (079's waived 24-context superset), but on an AXIS ORTHOGONAL to
// the anticipated N3 "required member inside an optional group" table (that
// table is all optional-outer-group over-requiring; this is a
// required-nested-group-via-required-component QuickFIX quirk D-3 never
// modeled) — SC-002's "24→0" claim is CONTRADICTED by this residual. Runtime
// impact (via the T017 transitive pin): on the opt-in strict
// `validate_inbound_messages` path, fixpp will `wire_required_field_missing`
// (ref_tag=295) a MassQuote whose NoQuoteSets instance omits the
// NoQuoteEntries subgroup entirely, where real QuickFIX accepts it.
//
// DECISION (orchestrator + user, 2026-07-19): do NOT extend D-3/the
// loaders/the oracle to replicate QuickFIX's component-in-group
// `componentRequired=false` quirk (fixpp stays spec-faithful — MORE correct
// than QuickFIX here). WAIVE these 3 as a named, source-verified
// stricter-superset residual instead (safe direction — no false-accept),
// same lineage as 079's W-204-1. `kKnownSupersetContexts` above is the
// PIN, not a blanket skip: for exactly these 3 named (dict,context) pairs
// the per-context assertion below requires `oracle == golden ∪ extra_tags`
// EXACTLY — a mutation-test-able pin that goes RED if the divergence grows
// to a 4th context, if the extra tag differs, or if the oracle-side set at
// one of these 3 contexts changes for any other reason; every OTHER context
// keeps the plain exact-set assertion unchanged
// ([[feedback_coverage_push_enshrines_bugs]]-adjacent care was taken NOT to
// silently force agreement anywhere else). B&L record lands in T024
// (spec/behaviors-and-limitations.md, orchestrator-owned).
// ============================================================================
TEST(RequiredScopeParity, QuickFixGroupGoldenMatchesOracleAcrossNineDicts) {
#ifndef FIXPP_REQUIRED_GOLDEN_GROUPS_CSV
#error "FIXPP_REQUIRED_GOLDEN_GROUPS_CSV must be defined by CMake"
#endif
#ifndef FIXPP_DICT_DATA_DIR
#error "FIXPP_DICT_DATA_DIR must be defined by CMake"
#endif
    std::cout << "\n=== 081 T020: QuickFIX per-group required-set parity (9 dicts, Concern B) ===\n";

    GoldenGroupRows const golden = load_golden_groups(FIXPP_REQUIRED_GOLDEN_GROUPS_CSV);
    ASSERT_FALSE(golden.empty()) << "golden_groups.csv produced zero data rows — parse failure?";

    std::size_t total_contexts = 0;
    std::size_t carve_out_hits = 0;
    for (auto const& dc : kQuickfixDicts) {
        auto const xml_path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / dc.filename;
        auto const oracle = build_quickfix_oracle(xml_path, /*include_header_trailer=*/false);

        // Both-directions context-KEY set equality (not just tag-content once
        // matched): a context on one side only is itself the defect.
        std::set<GroupContextKey> oracle_keys;
        for (auto const& [key, members] : oracle.group_members) {
            (void)members;
            oracle_keys.insert(key);
        }

        auto const git = golden.find(dc.label);
        // A dict with ZERO real body-level group contexts (e.g. FIXT11 — all
        // admin messages, no <group> in the message body) legitimately has no
        // row at all in golden_groups.csv (the generator never emits a row
        // for a dict/message pair with zero groups discovered). Only require
        // the dict to be present when the oracle actually found contexts for
        // it — an absent dict WITH non-empty oracle contexts is a genuine
        // completeness defect (all its contexts silently missing).
        if (oracle_keys.empty()) {
            EXPECT_TRUE(git == golden.end() || git->second.empty())
                << dc.label << ": oracle found zero body group contexts, but golden_groups.csv has "
                << (git != golden.end() ? git->second.size() : 0) << " row(s) for it";
            std::cout << "  " << dc.label << ": 0 group context(s) (oracle+golden agree — dict has no "
                                              "body-level groups)\n";
            continue;
        }
        ASSERT_NE(git, golden.end())
            << "golden_groups.csv has no rows for dictionary " << dc.label << " but the oracle found "
            << oracle_keys.size() << " real group context(s) — all silently missing";
        auto const& golden_ctx = git->second;

        std::set<GroupContextKey> golden_keys;
        for (auto const& [key, req] : golden_ctx) {
            (void)req;
            golden_keys.insert(key);
        }
        // `GroupContextKey` has only `operator<` (no `operator==`), so it
        // does not satisfy `std::ranges::less`'s `totally_ordered_with`
        // constraint — use the classic iterator-pair `std::set_difference`
        // overload, which only needs a strict-weak-ordering `<`.
        std::vector<GroupContextKey> missing_from_golden;
        std::vector<GroupContextKey> extra_in_golden;
        std::set_difference(oracle_keys.begin(), oracle_keys.end(), golden_keys.begin(), golden_keys.end(),
                             std::back_inserter(missing_from_golden));
        std::set_difference(golden_keys.begin(), golden_keys.end(), oracle_keys.begin(), oracle_keys.end(),
                             std::back_inserter(extra_in_golden));
        EXPECT_TRUE(missing_from_golden.empty())
            << dc.label << ": " << missing_from_golden.size()
            << " context(s) in the oracle but missing from golden_groups.csv — e.g. "
            << (missing_from_golden.empty() ? "" : describe_context(missing_from_golden.front()));
        EXPECT_TRUE(extra_in_golden.empty())
            << dc.label << ": " << extra_in_golden.size()
            << " context(s) in golden_groups.csv but not found by the oracle — e.g. "
            << (extra_in_golden.empty() ? "" : describe_context(extra_in_golden.front()));

        std::size_t checked = 0;
        for (auto const& [key, members] : oracle.group_members) {
            (void)members;
            auto const git2 = golden_ctx.find(key);
            if (git2 == golden_ctx.end()) {
                continue;  // already flagged by the context-key-set check above
            }
            auto const rit = oracle.group_required.find(key);
            std::set<std::uint16_t> const expected =
                (rit != oracle.group_required.end()) ? rit->second : std::set<std::uint16_t>{};

            auto const* carve_out_extra = find_known_superset_extra(dc.label, key);
            if (carve_out_extra != nullptr) {
                // 081 named stricter-superset carve-out (W-204-1 lineage,
                // kKnownSupersetContexts above): oracle must equal EXACTLY
                // golden ∪ the named extra tag(s) — real pin, not a skip.
                std::set<std::uint16_t> golden_plus_extra = git2->second;
                golden_plus_extra.insert(carve_out_extra->begin(), carve_out_extra->end());
                EXPECT_EQ(expected, golden_plus_extra)
                    << dc.label << " " << describe_context(key)
                    << " (081 W-204-1-lineage stricter-superset carve-out): expected oracle == golden ∪ "
                       "named-extra-tags: "
                    << describe_diff(expected, golden_plus_extra);
                ++carve_out_hits;
            } else {
                EXPECT_EQ(expected, git2->second) << dc.label << " " << describe_context(key) << ": "
                                                   << describe_diff(expected, git2->second);
            }
            ++checked;
        }
        std::cout << "  " << dc.label << ": " << checked << " group context(s) checked\n";
        total_contexts += checked;
    }

    std::cout << "  total group contexts checked across 9 QuickFIX dicts: " << total_contexts << "\n";
    std::cout << "  named stricter-superset carve-out contexts exercised: " << carve_out_hits << " (expect "
              << kKnownSupersetContexts.size() << ")\n";
    EXPECT_GT(total_contexts, 0u);
    // The carve-out itself stays a real pin: if a declared context vanishes
    // (dictionary edit, oracle rework, etc.), or MORE contexts than declared
    // start hitting the carve-out lookup, that is itself a finding to
    // re-examine — not silently absorbed.
    EXPECT_EQ(carve_out_hits, kKnownSupersetContexts.size())
        << "the named stricter-superset carve-out contexts were not exercised exactly as declared — "
           "re-examine kKnownSupersetContexts against the current oracle/golden";
}
