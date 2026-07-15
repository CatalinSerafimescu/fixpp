// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/xml_enum_codeset_test.cpp
//
// 075-live-wire-enum-validation T028 [US3] — SC-002/FR-001 census + exposure
// witness. Bucket `dictionary_pure_tests`, LABELS "075;dictionary".
//
// GOVERNING RULE: assert against the SHIPPED dictionaries/*.xml at test
// time, never a hand-maintained expectation list. The per-dictionary counts
// below are DERIVED by a raw pugixml scan of the real file at test run;
// the fixed numbers in `kExpectedCensus` are the assertion TARGET (matching
// the T001 re-measurement, spec.md §"T001 RE-MEASUREMENT AUDIT"), not a
// second hand-maintained "actual" — the actual side is always computed
// fresh from the tree.
//
// SCOPE CARVE-OUT (stated explicitly, per
// `[[feedback_completeness_gate_exact_set_not_subset]]` — do not quietly
// widen or narrow it):
//   - The EXACT-COUNT leg is scoped to the NINE XmlLoader (QuickFIX-XML)
//     dictionaries — what 075 newly populates, and what the T001 census
//     table covers.
//   - The EXPOSURE leg (enum_values() returns real values+descriptions)
//     covers all TEN in spirit, but this file only drives the nine plus
//     the FIX44 Side(54) spot-check named by T028. The TENTH (Orchestra /
//     FIX Latest) carries 074's code sets — populated by `OrchestraLoader`,
//     UNTOUCHED by 075, and already pinned by
//     `tests/dictionary/orchestra_loader_test.cpp`'s
//     `OrchestraCodesets.PreservesValuesAndDescriptions` (an AdvSide(4)
//     spot-check). **074 has NO exact-count census, and this file does not
//     add one over the tenth** — 075 adds no new exact-count gate over
//     Orchestra.
//
// Anchors:
//   tasks: specs/075-live-wire-enum-validation/tasks.md T028
//   spec:  specs/075-live-wire-enum-validation/spec.md SC-002/FR-001,
//          "T001 RE-MEASUREMENT AUDIT" table (:184-195)

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <pugixml.hpp>
#include <set>
#include <string>
#include <string_view>

namespace {

using fixpp::dict::Dictionary;

constexpr std::size_t kArenaBytes = 32UZ * 1024UZ * 1024UZ;

// (dictionary file name, expected enum-backed-field count, expected
// total-declared-code count) — the NINE XmlLoader dictionaries, per the
// T001 re-measurement (spec.md :184-195), all independently re-measured
// 2026-07-14, 51/51 GREEN.
struct ExpectedCensusRow {
    std::string_view file;
    std::size_t enum_backed_fields;
    std::size_t total_codes;
};

constexpr std::array<ExpectedCensusRow, 9> kExpectedCensus{{
    {"FIX40.xml", 39, 235},
    {"FIX41.xml", 53, 342},
    {"FIX42.xml", 104, 629},
    {"FIX43.xml", 159, 1198},
    {"FIX44.xml", 245, 1708},
    {"FIX50.xml", 290, 2326},
    {"FIX50SP1.xml", 327, 2640},
    {"FIX50SP2.xml", 668, 5565},
    {"FIXT11.xml", 9, 56},
}};

// Raw pugixml scan of a QuickFIX-XML `<fields>` section: how many `<field>`
// elements declare at least one `<value>` child (enum-backed field count),
// and the total number of `<value>` children across all fields (total
// declared code count, raw — NOT deduped; SC-011's leg 1 in
// dict_enum_census_test.cpp independently pins zero duplicates across
// these same nine files, so raw == deduped here).
struct RawCensus {
    std::size_t enum_backed_fields = 0;
    std::size_t total_codes = 0;
};

RawCensus raw_scan(std::filesystem::path const& xml_path) {
    RawCensus rc;
    pugi::xml_document doc;
    auto const result = doc.load_file(xml_path.c_str());
    if (!result) {
        return rc;  // reported as zero; the Dictionary::load() below already
                    // exercises the same file and would fail loudly first.
    }
    auto const fields_node = doc.child("fix").child("fields");
    for (auto const& f : fields_node.children("field")) {
        std::size_t const value_count =
            static_cast<std::size_t>(std::distance(f.children("value").begin(), f.children("value").end()));
        if (value_count > 0) {
            ++rc.enum_backed_fields;
            rc.total_codes += value_count;
        }
    }
    return rc;
}

}  // namespace

// ---------------------------------------------------------------------------
// Exposure witness: enum_values(54) on the FIX44 dictionary returns the
// declared Side codes WITH THEIR DESCRIPTIONS — a direct literal pin of the
// shipped FIX44.xml <field number='54'> block (16 codes, verified against
// the vendored file; not a synthetic/mock dictionary).
// ---------------------------------------------------------------------------
TEST(XmlEnumCodeset, Fix44SideEnumValuesReturnsCodesWithDescriptions) {
    auto storage = std::make_unique<std::byte[]>(kArenaBytes);
    std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";
    auto dict = fixpp::dict::XmlLoader{}.load(path, &mr);

    auto const codes = dict.enum_values(std::uint16_t{54});
    ASSERT_EQ(codes.size(), 16u) << "FIX44 Side(54) declares 16 codes in the shipped dictionary";

    struct Expected {
        std::string_view value;
        std::string_view description;
    };
    constexpr std::array<Expected, 16> kExpected{{
        {"1", "BUY"},
        {"2", "SELL"},
        {"3", "BUY_MINUS"},
        {"4", "SELL_PLUS"},
        {"5", "SELL_SHORT"},
        {"6", "SELL_SHORT_EXEMPT"},
        {"7", "UNDISCLOSED"},
        {"8", "CROSS"},
        {"9", "CROSS_SHORT"},
        {"A", "CROSS_SHORT_EXEMPT"},
        {"B", "AS_DEFINED"},
        {"C", "OPPOSITE"},
        {"D", "SUBSCRIBE"},
        {"E", "REDEEM"},
        {"F", "LEND"},
        {"G", "BORROW"},
    }};

    for (std::size_t i = 0; i < kExpected.size(); ++i) {
        EXPECT_EQ(codes[i].value, kExpected[i].value) << "index " << i;
        EXPECT_EQ(codes[i].description, kExpected[i].description)
            << "index " << i << " (value=" << codes[i].value << ")";
        EXPECT_FALSE(codes[i].description.empty())
            << "declared Side code " << codes[i].value << " must carry a non-empty description";
    }
}

// ---------------------------------------------------------------------------
// Per-dictionary exact-count census over the nine XmlLoader dictionaries.
// Both `Dictionary::enum_values()` (the exposure surface, loader-produced)
// and a raw pugixml scan (the shipped-XML ground truth) must agree with
// each other AND with the T001-measured expected numbers — so a dictionary
// refresh that adds/removes/edits a code, OR a loader regression that
// silently drops codes, fails the build.
// ---------------------------------------------------------------------------
TEST(XmlEnumCodeset, PerDictionaryEnumBackedFieldAndCodeCountsMatchCensus) {
    auto storage = std::make_unique<std::byte[]>(kArenaBytes);
    std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

    std::cout << "\n=== 075 T028 SC-002/FR-001: per-dictionary enum-backed-field / total-code census "
                 "(nine XmlLoader dicts) ===\n";

    for (auto const& row : kExpectedCensus) {
        auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / std::string{row.file};

        auto const raw = raw_scan(path);

        auto dict = fixpp::dict::XmlLoader{}.load(path, &mr);
        ASSERT_FALSE(dict.messages().empty()) << row.file << " failed to load or has no messages";

        // Loader-produced census: walk every raw field tag and ask the
        // loaded Dictionary whether it has a non-empty codeset (cross-checks
        // the raw scan against the actual runtime exposure surface).
        pugi::xml_document doc;
        ASSERT_TRUE(doc.load_file(path.c_str())) << row.file;
        auto const fields_node = doc.child("fix").child("fields");

        std::size_t loader_enum_backed_fields = 0;
        std::size_t loader_total_codes = 0;
        for (auto const& f : fields_node.children("field")) {
            auto const tag = static_cast<std::uint16_t>(f.attribute("number").as_uint());
            auto const codes = dict.enum_values(tag);
            if (!codes.empty()) {
                ++loader_enum_backed_fields;
                loader_total_codes += codes.size();
            }
        }

        std::cout << "  " << row.file << ": raw=" << raw.enum_backed_fields << "/" << raw.total_codes
                  << " loader=" << loader_enum_backed_fields << "/" << loader_total_codes
                  << " expected=" << row.enum_backed_fields << "/" << row.total_codes << "\n";

        EXPECT_EQ(raw.enum_backed_fields, row.enum_backed_fields)
            << row.file << ": raw-XML enum-backed field count drifted from the T001 census";
        EXPECT_EQ(raw.total_codes, row.total_codes)
            << row.file << ": raw-XML total declared code count drifted from the T001 census";
        EXPECT_EQ(loader_enum_backed_fields, row.enum_backed_fields)
            << row.file << ": Dictionary::enum_values() enum-backed field count diverges from the "
                            "raw shipped XML — the loader is silently dropping or adding codesets";
        EXPECT_EQ(loader_total_codes, row.total_codes)
            << row.file << ": Dictionary::enum_values() total code count diverges from the raw "
                            "shipped XML — the loader is silently dropping or adding codes";
    }
}
