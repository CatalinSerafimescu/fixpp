// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/fixt_header_merge_test.cpp
//
// 081-strict-validation-residuals US1 (Concern A / #203 / L-041-2) T005/T006.
//
// STANDALONE binary (own executable + `add_test`), per `[const §VII.8]`: an
// exact-set completeness gate is isolation-sensitive
// ([[feedback_completeness_gate_exact_set_not_subset]]). quickstart.md
// Scenario A3 groups the census (T005) AND the parser-containment pin (T006)
// under the SAME standalone pin ("tests/dictionary/fixt_header_merge_*
// (new, standalone)") — both land in this one file/binary.
//
// T005 — NON-CIRCULAR CENSUS: the baked `fixpp::dict::detail::
// kFixtFramingTable` (src/dictionary/fixt_framing_table.hpp) must equal, tag
// AND datatype, both directions, the FIXT.1.1 `<header>` + `<trailer>` field
// set declared in the vendored `dictionaries/FIXT11.xml` — walked HERE
// directly via pugixml, independently of the production `XmlLoader`/
// `Dictionary` (research.md D-2). The walk recurses ONE level into
// `<header>`/`<trailer>` nested `<group>` elements (data-model.md E-1 — the
// `NoHops` group), including the group's own count field. The xml type
// string -> `field_data_type` step is a SMALL LOCAL table (independent of
// `xml_loader.cpp`'s private `kFieldTypeTable`); the `field_data_type` ->
// `field_type` reduction reuses the canonical public mapping
// (`field_type_from_data_type`, field_type.hpp) per data-model.md E-1's
// explicit instruction to go "through the canonical field_data_type ->
// field_type mapping" — only the FRAMING-SET SELECTION and the per-tag
// datatype-from-raw-XML are independent; the general reduction table is not
// itself under test here (it is exercised/tested elsewhere).
//
// T006 — PARSER-CONTAINMENT PIN (RC#1 / FR-009, asserted DIRECTLY, not via a
// blind unknown_fields() on/off compare — see research.md D-1/D-7): on
// `Dictionary::as_table_view()` output for FIX50/FIX50SP1/FIX50SP2,
// `table_view::field_valid_for`/`valid_tags_for` MUST stay FALSE for a
// curated set of framing tags not genuinely declared by NewOrderSingle(D),
// WHILE `table_view::is_fixt_framing_tag` (the mechanism the validator's
// Step-1 gate consults) is TRUE for those same tags. A re-widening of the
// shared `valid_` store inside `as_table_view()` flips `field_valid_for` to
// true -> RED.
//
// Anchors:
//   research:   specs/081-strict-validation-residuals/research.md D-2, D-7
//   data-model: specs/081-strict-validation-residuals/data-model.md E-1/E-2
//   contract:   specs/081-strict-validation-residuals/contracts/
//               validation-acceptance.md Invariants
//   quickstart: specs/081-strict-validation-residuals/quickstart.md
//               Scenario A3
//   tasks:      specs/081-strict-validation-residuals/tasks.md T005/T006

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/field_type.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <memory_resource>
#include <pugixml.hpp>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Cross-TU-visible-but-private production constant (src/dictionary/, seam
// via target_include_directories — mirrors the `ir.hpp` precedent in
// tests/dictionary/required_scope_census_test.cpp).
#include "fixt_framing_table.hpp"

namespace {

using fixpp::dict::Dictionary;
using fixpp::dict::field_data_type;
using fixpp::dict::field_type;
using fixpp::dict::field_type_from_data_type;

// ── T005: independent raw-XML oracle ────────────────────────────────────────

// Small LOCAL xml-type-string -> field_data_type table, independent of
// xml_loader.cpp's private kFieldTypeTable. Covers exactly the type strings
// FIXT11.xml's <header>/<trailer>/NoHops fields use.
field_data_type xml_type_to_data_type(std::string_view s) {
    if (s == "STRING") return field_data_type::String;
    if (s == "LENGTH") return field_data_type::Length;
    if (s == "SEQNUM") return field_data_type::SeqNum;
    if (s == "NUMINGROUP") return field_data_type::NumInGroup;
    if (s == "UTCTIMESTAMP") return field_data_type::UtcTimestamp;
    if (s == "BOOLEAN") return field_data_type::Boolean;
    if (s == "DATA") return field_data_type::Data;
    if (s == "INT") return field_data_type::Int;
    ADD_FAILURE() << "xml_type_to_data_type: unrecognized FIXT11.xml type string '" << s
                  << "' — the census's local type table needs a new row";
    return field_data_type::String;
}

struct ExpectedEntry {
    std::uint16_t tag;
    field_type type;
    friend bool operator<(ExpectedEntry const& a, ExpectedEntry const& b) {
        return a.tag < b.tag;
    }
    friend bool operator==(ExpectedEntry const& a, ExpectedEntry const& b) {
        return a.tag == b.tag && a.type == b.type;
    }
};

std::string describe(std::set<ExpectedEntry> const& s) {
    std::ostringstream oss;
    for (auto const& e : s) {
        oss << e.tag << ":" << static_cast<int>(e.type) << " ";
    }
    return oss.str();
}

// Walks dictionaries/FIXT11.xml directly (pugixml) and returns the
// independently-derived <header>+<trailer> tag/type set, recursing one level
// into nested <group> elements (the NoHops group), per data-model.md E-1.
std::set<ExpectedEntry> walk_fixt11_framing_set() {
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIXT11.xml";
    pugi::xml_document doc;
    auto const result = doc.load_file(path.c_str());
    if (!result) {
        ADD_FAILURE() << "failed to load " << path;
        return {};
    }
    auto const fix = doc.child("fix");

    // name -> (tag, xml type string), from the <fields> catalog.
    std::vector<std::pair<std::string, std::pair<std::uint16_t, std::string>>> by_name;
    for (auto const& f : fix.child("fields").children("field")) {
        by_name.emplace_back(
            f.attribute("name").value(),
            std::make_pair(static_cast<std::uint16_t>(f.attribute("number").as_uint()),
                           std::string{f.attribute("type").value()}));
    }
    auto lookup = [&](std::string_view name) -> std::pair<std::uint16_t, std::string> const& {
        for (auto const& [n, entry] : by_name) {
            if (n == name) {
                return entry;
            }
        }
        static std::pair<std::uint16_t, std::string> const kMissing{0, ""};
        ADD_FAILURE() << "walk_fixt11_framing_set: field name '" << name
                      << "' not found in FIXT11.xml <fields>";
        return kMissing;
    };

    std::set<ExpectedEntry> out;
    auto add_field_name = [&](std::string_view name) {
        auto const& [tag, type_str] = lookup(name);
        if (tag == 0) {
            return;
        }
        out.insert(ExpectedEntry{tag, field_type_from_data_type(xml_type_to_data_type(type_str))});
    };

    for (auto const& section : {fix.child("header"), fix.child("trailer")}) {
        for (auto const& child : section.children()) {
            std::string_view const child_name = child.name();
            if (child_name == "field") {
                add_field_name(child.attribute("name").value());
            } else if (child_name == "group") {
                // One level of recursion: the group's OWN count field, plus
                // its direct <field> children (data-model.md E-1 disposition
                // — the group's nested members are NOT recursed further).
                add_field_name(child.attribute("name").value());
                for (auto const& gf : child.children("field")) {
                    add_field_name(gf.attribute("name").value());
                }
            }
        }
    }
    return out;
}

std::set<ExpectedEntry> baked_framing_set() {
    std::set<ExpectedEntry> out;
    for (auto const& e : fixpp::dict::detail::kFixtFramingTable) {
        out.insert(ExpectedEntry{e.tag, e.type});
    }
    return out;
}

TEST(FixtHeaderMerge, BakedTableEqualsFixt11HeaderTrailerCensusExactSet) {
    auto const expected = walk_fixt11_framing_set();
    auto const actual = baked_framing_set();
    EXPECT_EQ(expected, actual) << "expected(FIXT11.xml walk)={ " << describe(expected)
                                << "} actual(kFixtFramingTable)={ " << describe(actual) << "}";
}

// Direct spot-pin for the flat-recursed NoHops group (data-model.md E-1 —
// the specific residual this feature closes: excluding these would leave a
// SC-003 false-reject of routed FIXT traffic).
TEST(FixtHeaderMerge, NoHopsGroupTagsPresentFlatWithCorrectTypes) {
    auto const actual = baked_framing_set();
    EXPECT_TRUE(actual.contains(ExpectedEntry{627, field_type::Int}))   // NoHops
        << "627 NoHops must be present (Int)";
    EXPECT_TRUE(actual.contains(ExpectedEntry{628, field_type::String}))  // HopCompID
        << "628 HopCompID must be present (String)";
    EXPECT_TRUE(actual.contains(ExpectedEntry{629, field_type::String}))  // HopSendingTime
        << "629 HopSendingTime must be present (String)";
    EXPECT_TRUE(actual.contains(ExpectedEntry{630, field_type::Int}))   // HopRefID
        << "630 HopRefID must be present (Int)";
}

// ── T006: parser-containment pin (RC#1 / FR-009, direct, not blind on/off) ─

struct FixtHeaderContainmentTest : ::testing::TestWithParam<char const*> {};

TEST_P(FixtHeaderContainmentTest, FramingTagsAcceptedByValidatorNeverLeakIntoSharedValidStore) {
    std::pmr::monotonic_buffer_resource mr;
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / GetParam();
    Dictionary dict = fixpp::dict::XmlLoader{}.load(path, &mr);
    auto const tv = dict.as_table_view();

    // NewOrderSingle(D) genuinely declares none of these tags as body fields
    // (verified: empty <header/>, and none of {8,9,10,34,49,52,56,1128,1156}
    // appear as scalar message fields of D in any FIX50/FIX50SP1/FIX50SP2
    // dictionary) — "not genuinely message-declared" per the contract.
    constexpr std::array<std::uint16_t, 9> kCuratedFramingTags{8,   9,    10,  34,  49,
                                                               52,  56,   1128, 1156};
    constexpr std::string_view kMsgType = "D";

    for (auto const tag : kCuratedFramingTags) {
        EXPECT_TRUE(tv.is_fixt_framing_tag(tag))
            << GetParam() << ": tag " << tag
            << " must be a FIXT framing tag (the mechanism the validator's Step-1 gate uses)";
        EXPECT_FALSE(tv.field_valid_for(kMsgType, tag))
            << GetParam() << ": tag " << tag
            << " must NOT leak into the shared valid_ store via field_valid_for — a re-widening "
               "of valid_ inside as_table_view() would flip this to true";
        EXPECT_FALSE(tv.valid_tags_for(kMsgType).contains(tag))
            << GetParam() << ": tag " << tag << " must NOT leak into valid_tags_for either";
    }
}

INSTANTIATE_TEST_SUITE_P(Fix50Family, FixtHeaderContainmentTest,
                        ::testing::Values("FIX50.xml", "FIX50SP1.xml", "FIX50SP2.xml"));

}  // namespace
