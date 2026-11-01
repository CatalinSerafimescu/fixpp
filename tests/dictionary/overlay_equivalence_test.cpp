// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/overlay_equivalence_test.cpp — W-1 and W-2 of 098-dialect-overlay
// (contracts/witnesses.md "Loading and merging"; research R-10).
//
// W-1: the small QuickFIX-XML and Orchestra triples (fixtures/098/qf, fixtures/098/
// orch), `load_with_overlay_from_string(base, overlay)` against
// `load_from_string(edited)`, and each format's `tolerant` triple.
// W-2: the vendored bases with an overlay and a hand-written insertion patch
// (fixtures/098/real), `load_with_overlay(base, overlay)` against
// `load_from_string(patched base)`; and every runtime base and the Orchestra file
// with an empty overlay against `load(base)`.
// W-4: `dict::load_any_with_overlay` against the loader called directly.
//
// Every row loads through research R-5's read seam and asserts no counter-origin
// refusal and step counter = step 6's prediction; every row has an in-test seeded
// mismatch that names, in advance, what it removes and the stores it expects the
// comparator to report. Compared and excluded census counts go to the report
// (stdout), never to a comment.
//
// The patch helper (overlay_patch.hpp) is line-based, not XML-aware (research R-10);
// its own rows are below.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/error.hpp>
#include <fixpp/dict/load_any.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <iostream>
#include <memory_resource>
#include <optional>
#include <pugixml.hpp>
#include <set>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#include "dictionary_equivalence.hpp"
#include "overlay_test_support.hpp"
#include "reused_tag_census.hpp"

namespace {

namespace eq = fixpp::dict::equivalence;
namespace ids = fixpp::dict::equivalence::id;
namespace ot = overlay_test;
namespace ov = fixpp::dict::detail::overlay;
using fixpp::dict::Dictionary;
using fixpp::dict::OrchestraLoader;
using fixpp::dict::unresolved_group_policy;
using fixpp::dict::XmlLoader;

// The format of a row's documents: it picks the loader, the seam and the census.
enum class fmt : std::uint8_t { quickfix_xml, orchestra };

std::pmr::memory_resource* mr() { return std::pmr::new_delete_resource(); }

// ── comparisons ──────────────────────────────────────────────────────────────

// `a` (the overlay side) against `b` (the reference), each checked against the
// census of the reference text.
eq::comparison_report compare_against(Dictionary const& a, Dictionary const& b,
                                      std::string_view reference_text,
                                      fmt format = fmt::quickfix_xml) {
    eq::raw_census const census = format == fmt::orchestra
                                      ? eq::census_from_orchestra(reference_text)
                                      : eq::census_from_quickfix_xml(reference_text);
    return eq::compare(eq::side{.dict = a, .census = census},
                       eq::side{.dict = b, .census = census});
}

void report_counts(std::string const& row, eq::comparison_report const& r) {
    std::cout << "[098 " << row << "] census compared " << r.census_compared << ", excluded "
              << r.census_excluded << '\n';
    std::size_t wraps = 0;
    for (auto const& n : r.notes) {
        wraps += n.find("wrap case") != std::string::npos ? 1 : 0;
    }
    if (wraps != 0) {
        std::cout << "[098 " << row << "] wrap-case notes " << wraps
                  << " (definitions past the wrap point compared side against side only)\n";
    }
}

void expect_equivalent(std::string const& row, eq::comparison_report const& r,
                       bool assert_no_exclusions) {
    report_counts(row, r);
    EXPECT_TRUE(r.differences.empty()) << row << ":\n" << r.describe();
    EXPECT_TRUE(r.invariant_violations.empty()) << row << ":\n" << r.describe();
    if (assert_no_exclusions) {
        EXPECT_EQ(r.census_excluded, 0U) << row << ":\n" << r.describe();
    }
}

void expect_reports(std::string const& row, eq::comparison_report const& r,
                    std::set<std::string_view> const& expected) {
    auto const got = r.reported_entries();
    std::cout << "[098 " << row << " seeded mismatch] reported stores:";
    for (auto const e : got) {
        std::cout << " [" << e << "]";
    }
    std::cout << '\n';
    for (auto const e : expected) {
        EXPECT_TRUE(got.contains(e)) << row << ": the seeded mismatch must report [" << e << "]:\n"
                                     << r.describe();
    }
}

// The text with the one line equal to `anchor` removed; the anchor must match
// exactly once.
std::string without_line(std::string_view text, std::string_view anchor) {
    auto lines = ot::lines_of(text);
    auto const hits = ot::find_line(lines, anchor);
    EXPECT_EQ(hits.size(), 1U) << "anchor '" << anchor << "' must match exactly once";
    if (hits.size() == 1) {
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(hits.front()));
    }
    return ot::join_lines(lines);
}

Dictionary load_text(std::string_view text,
                     unresolved_group_policy policy = unresolved_group_policy::fail_closed) {
    return XmlLoader{}.load_from_string(text, mr(), policy);
}

Dictionary load_orch_text(std::string_view text,
                          unresolved_group_policy policy = unresolved_group_policy::fail_closed) {
    return OrchestraLoader{}.load_from_string(text, mr(), policy);
}

Dictionary load_text_as(fmt format, std::string_view text) {
    return format == fmt::orchestra ? load_orch_text(text) : load_text(text);
}

// ── W-1, the small QuickFIX-XML triple ───────────────────────────────────────

std::filesystem::path qf(char const* name) { return ot::fixture_dir() / "qf" / name; }

TEST(OverlayEquivalenceW1Qf, EveryAdditionKindEqualsTheEditedReference) {
    std::string const base = ot::read_file(qf("base.xml"));
    std::string const overlay = ot::read_file(qf("overlay.xml"));
    std::string const edited = ot::read_file(qf("edited.xml"));
    auto const p = ov::probe_xml_overlay_from_string(base, overlay, mr());
    ot::expect_loaded_at_prediction(p, "W-1 qf");
    ASSERT_TRUE(p.dictionary.has_value()) << ot::describe(p);
    Dictionary const reference = load_text(edited);
    expect_equivalent("W-1 qf", compare_against(*p.dictionary, reference, edited),
                      /*assert_no_exclusions=*/true);
}

// Seeded mismatch: the reference without the enum value Z the overlay adds to
// Side(54); the comparator must report the enum stores.
TEST(OverlayEquivalenceW1Qf, SeededMismatchWithoutAnEnumAdditionIsReported) {
    std::string const base = ot::read_file(qf("base.xml"));
    std::string const overlay = ot::read_file(qf("overlay.xml"));
    std::string const edited = ot::read_file(qf("edited.xml"));
    auto const p = ov::probe_xml_overlay_from_string(base, overlay, mr());
    ASSERT_TRUE(p.dictionary.has_value()) << ot::describe(p);
    std::string const seeded =
        without_line(edited, "   <value enum='Z' description='VENUE_CROSS' />");
    Dictionary const reference = load_text(seeded);
    expect_reports("W-1 qf", compare_against(*p.dictionary, reference, seeded),
                   {ids::kEnumValue, ids::kEnumCodes});
}

TEST(OverlayEquivalenceW1Qf, TolerantTripleEqualsItsReferenceUnderTolerant) {
    std::string const base = ot::read_file(qf("base_tolerant.xml"));
    std::string const overlay = ot::read_file(qf("overlay_tolerant.xml"));
    std::string const edited = ot::read_file(qf("edited_tolerant.xml"));
    auto const p =
        ov::probe_xml_overlay_from_string(base, overlay, mr(), unresolved_group_policy::tolerant);
    ot::expect_loaded_at_prediction(p, "W-1 qf tolerant");
    ASSERT_TRUE(p.dictionary.has_value()) << ot::describe(p);
    Dictionary const reference = load_text(edited, unresolved_group_policy::tolerant);
    expect_equivalent("W-1 qf tolerant", compare_against(*p.dictionary, reference, edited),
                      /*assert_no_exclusions=*/true);
}

// Seeded mismatch: the reference without the field VenueTag the overlay appends to
// NewOrderSingle; the comparator must report the message's field run and valid set.
TEST(OverlayEquivalenceW1Qf, TolerantSeededMismatchWithoutTheMessageAdditionIsReported) {
    std::string const base = ot::read_file(qf("base_tolerant.xml"));
    std::string const overlay = ot::read_file(qf("overlay_tolerant.xml"));
    std::string const edited = ot::read_file(qf("edited_tolerant.xml"));
    auto const p =
        ov::probe_xml_overlay_from_string(base, overlay, mr(), unresolved_group_policy::tolerant);
    ASSERT_TRUE(p.dictionary.has_value()) << ot::describe(p);
    std::string const seeded = without_line(edited, "   <field name='VenueTag' required='N' />");
    Dictionary const reference = load_text(seeded, unresolved_group_policy::tolerant);
    expect_reports("W-1 qf tolerant", compare_against(*p.dictionary, reference, seeded),
                   {ids::kMessageFields.tag, ids::kValid});
}

// The policy reaches the build: with the default policy both sides refuse.
TEST(OverlayEquivalenceW1Qf, TolerantTripleRefusesOnBothSidesUnderTheDefaultPolicy) {
    std::string const base = ot::read_file(qf("base_tolerant.xml"));
    std::string const overlay = ot::read_file(qf("overlay_tolerant.xml"));
    std::string const edited = ot::read_file(qf("edited_tolerant.xml"));
    auto const p = ov::probe_xml_overlay_from_string(base, overlay, mr());
    ot::refusal const r = ot::refusal_of(p);
    EXPECT_TRUE(r.refused) << ot::describe(p);
    EXPECT_EQ(r.type, typeid(fixpp::dict::xml_parse_error).name()) << ot::describe(p);
    EXPECT_NE(r.what.find("declares no first member"), std::string::npos) << r.what;
    EXPECT_THROW(static_cast<void>(load_text(edited)), fixpp::dict::xml_parse_error);
}

// ── W-1, the small Orchestra triple ──────────────────────────────────────────

std::filesystem::path orch(char const* name) { return ot::fixture_dir() / "orch" / name; }

TEST(OverlayEquivalenceW1Orch, EveryAdditionKindEqualsTheEditedReference) {
    std::string const base = ot::read_file(orch("base.xml"));
    std::string const overlay = ot::read_file(orch("overlay.xml"));
    std::string const edited = ot::read_file(orch("edited.xml"));
    auto const p = ov::probe_orchestra_overlay_from_string(base, overlay, mr());
    ot::expect_loaded_at_prediction(p, "W-1 orch");
    ASSERT_TRUE(p.dictionary.has_value()) << ot::describe(p);
    Dictionary const reference = load_orch_text(edited);
    expect_equivalent("W-1 orch", compare_against(*p.dictionary, reference, edited, fmt::orchestra),
                      /*assert_no_exclusions=*/true);
}

// Seeded mismatch: the reference without the code Z the overlay adds to the base
// code set SideCodeSet; the comparator must report the enum stores.
TEST(OverlayEquivalenceW1Orch, SeededMismatchWithoutACodeAdditionIsReported) {
    std::string const base = ot::read_file(orch("base.xml"));
    std::string const overlay = ot::read_file(orch("overlay.xml"));
    std::string const edited = ot::read_file(orch("edited.xml"));
    auto const p = ov::probe_orchestra_overlay_from_string(base, overlay, mr());
    ASSERT_TRUE(p.dictionary.has_value()) << ot::describe(p);
    std::string const seeded =
        without_line(edited, "      <fixr:code id=\"54003\" name=\"VenueCross\" value=\"Z\" />");
    Dictionary const reference = load_orch_text(seeded);
    expect_reports("W-1 orch", compare_against(*p.dictionary, reference, seeded, fmt::orchestra),
                   {ids::kEnumValue, ids::kEnumCodes});
}

TEST(OverlayEquivalenceW1Orch, TolerantTripleEqualsItsReferenceUnderTolerant) {
    std::string const base = ot::read_file(orch("base_tolerant.xml"));
    std::string const overlay = ot::read_file(orch("overlay_tolerant.xml"));
    std::string const edited = ot::read_file(orch("edited_tolerant.xml"));
    auto const p = ov::probe_orchestra_overlay_from_string(base, overlay, mr(),
                                                           unresolved_group_policy::tolerant);
    ot::expect_loaded_at_prediction(p, "W-1 orch tolerant");
    ASSERT_TRUE(p.dictionary.has_value()) << ot::describe(p);
    Dictionary const reference = load_orch_text(edited, unresolved_group_policy::tolerant);
    expect_equivalent("W-1 orch tolerant",
                      compare_against(*p.dictionary, reference, edited, fmt::orchestra),
                      /*assert_no_exclusions=*/true);
}

// Seeded mismatch: the reference without the field VenueTag the overlay appends to
// NewOrderSingle's structure; the comparator must report the message's field run
// and valid set.
TEST(OverlayEquivalenceW1Orch, TolerantSeededMismatchWithoutTheMessageAdditionIsReported) {
    std::string const base = ot::read_file(orch("base_tolerant.xml"));
    std::string const overlay = ot::read_file(orch("overlay_tolerant.xml"));
    std::string const edited = ot::read_file(orch("edited_tolerant.xml"));
    auto const p = ov::probe_orchestra_overlay_from_string(base, overlay, mr(),
                                                           unresolved_group_policy::tolerant);
    ASSERT_TRUE(p.dictionary.has_value()) << ot::describe(p);
    std::string const seeded = without_line(edited, "        <fixr:fieldRef id=\"9101\" />");
    Dictionary const reference = load_orch_text(seeded, unresolved_group_policy::tolerant);
    expect_reports("W-1 orch tolerant",
                   compare_against(*p.dictionary, reference, seeded, fmt::orchestra),
                   {ids::kMessageFields.tag, ids::kValid});
}

// The policy reaches the build: with the default policy both sides refuse.
TEST(OverlayEquivalenceW1Orch, TolerantTripleRefusesOnBothSidesUnderTheDefaultPolicy) {
    std::string const base = ot::read_file(orch("base_tolerant.xml"));
    std::string const overlay = ot::read_file(orch("overlay_tolerant.xml"));
    std::string const edited = ot::read_file(orch("edited_tolerant.xml"));
    auto const p = ov::probe_orchestra_overlay_from_string(base, overlay, mr());
    ot::refusal const r = ot::refusal_of(p);
    EXPECT_TRUE(r.refused) << ot::describe(p);
    EXPECT_EQ(r.type, typeid(fixpp::dict::orchestra_parse_error).name()) << ot::describe(p);
    EXPECT_NE(r.what.find("declares no first member"), std::string::npos) << r.what;
    EXPECT_THROW(static_cast<void>(load_orch_text(edited)), fixpp::dict::orchestra_parse_error);
}

// ── the patch helper's own rows ──────────────────────────────────────────────

constexpr std::string_view kHelperBase =
    "<a>\n"
    " <b>\n"
    "  <c x='1' />\n"
    " </b>\n"
    " <b>\n"
    "  <d />\n"
    " </b>\n"
    "</a>\n";

TEST(OverlayPatchHelper, InsertsTheRunExactlyBeforeTheBlocksLastLine) {
    auto const p = ot::parse_patch(
        "@@ append to the second b\n"
        "   <d />\n"
        "+  <e />\n"
        "+  <f />\n"
        "  </b>\n");
    ASSERT_TRUE(p.error.empty()) << p.error;
    auto const a = ot::apply_patch(kHelperBase, p.hunks);
    ASSERT_TRUE(a.error.empty()) << a.error;
    EXPECT_EQ(a.text,
              "<a>\n"
              " <b>\n"
              "  <c x='1' />\n"
              " </b>\n"
              " <b>\n"
              "  <d />\n"
              "  <e />\n"
              "  <f />\n"
              " </b>\n"
              "</a>\n");
}

TEST(OverlayPatchHelper, AContextBlockMatchingZeroPlacesFailsTheCell) {
    auto const p = ot::parse_patch("@@ nowhere\n  <zzz />\n+  <e />\n  </b>\n");
    ASSERT_TRUE(p.error.empty()) << p.error;
    auto const a = ot::apply_patch(kHelperBase, p.hunks);
    EXPECT_NE(a.error.find("matches 0 places"), std::string::npos) << a.error;
}

TEST(OverlayPatchHelper, AContextBlockMatchingTwoPlacesFailsTheCell) {
    auto const p = ot::parse_patch("@@ twice\n+  <e />\n  </b>\n");
    ASSERT_TRUE(p.error.empty()) << p.error;
    auto const a = ot::apply_patch(kHelperBase, p.hunks);
    EXPECT_NE(a.error.find("matches 2 places"), std::string::npos) << a.error;
}

TEST(OverlayPatchHelper, RewritesARequiredValueInPlace) {
    constexpr std::string_view base = "<m>\n <field name='A' required='N' />\n</m>\n";
    auto const p = ot::parse_patch(
        "@@ promote A\n"
        " <m>\n"
        "- <field name='A' required='N' />\n"
        "+ <field name='A' required='Y' />\n"
        " </m>\n");
    ASSERT_TRUE(p.error.empty()) << p.error;
    auto const a = ot::apply_patch(base, p.hunks);
    ASSERT_TRUE(a.error.empty()) << a.error;
    EXPECT_EQ(a.text, "<m>\n <field name='A' required='Y' />\n</m>\n");
}

TEST(OverlayPatchHelper, AddsARequiredAttributeInTheLinesQuoteStyle) {
    constexpr std::string_view base = "<m>\n <field name=\"A\" />\n</m>\n";
    auto const p = ot::parse_patch(
        "@@ promote A\n"
        " <m>\n"
        "- <field name=\"A\" />\n"
        "+ <field name=\"A\" required=\"Y\" />\n"
        " </m>\n");
    ASSERT_TRUE(p.error.empty()) << p.error;
    auto const a = ot::apply_patch(base, p.hunks);
    ASSERT_TRUE(a.error.empty()) << a.error;
    EXPECT_EQ(a.text, "<m>\n <field name=\"A\" required=\"Y\" />\n</m>\n");
}

TEST(OverlayPatchHelper, ARequiredAttributeAddedInsideAQuotedValueFailsTheCell) {
    // Inserted right after the opening quote of name="A", so it lands in the value.
    auto const p = ot::parse_patch(
        "@@ promote A\n"
        " <m>\n"
        "- <field name=\"A\" />\n"
        "+ <field name=\" required=\"Y\"A\" />\n"
        " </m>\n");
    EXPECT_NE(p.error.find("neither a Required promotion nor a self-closing opener"),
              std::string::npos)
        << p.error;
}

TEST(OverlayPatchHelper, OpensASelfClosingElement) {
    constexpr std::string_view base = "<fix>\n <header />\n <messages>\n</fix>\n";
    auto const p = ot::parse_patch(
        "@@ open header\n"
        "- <header />\n"
        "+ <header>\n"
        "+  <field name='H' required='N' />\n"
        "+ </header>\n"
        "  <messages>\n");
    ASSERT_TRUE(p.error.empty()) << p.error;
    auto const a = ot::apply_patch(base, p.hunks);
    ASSERT_TRUE(a.error.empty()) << a.error;
    EXPECT_EQ(a.text,
              "<fix>\n <header>\n  <field name='H' required='N' />\n </header>\n"
              " <messages>\n</fix>\n");
}

TEST(OverlayPatchHelper, ARewriteOfAnyOtherShapeFailsTheCell) {
    auto const p = ot::parse_patch(
        "@@ rename\n"
        "- <field name='A' required='N' />\n"
        "+ <group name='A' required='N' />\n"
        " </m>\n");
    EXPECT_NE(p.error.find("neither a Required promotion nor a self-closing opener"),
              std::string::npos)
        << p.error;
}

TEST(OverlayPatchHelper, StripsOneTrailingCrAndWritesLf) {
    constexpr std::string_view base = "<a>\r\n <b>\r\n </b>\r\n</a>\r\n";
    auto const p = ot::parse_patch("@@ crlf\r\n  <b>\r\n+  <c />\r\n  </b>\r\n");
    ASSERT_TRUE(p.error.empty()) << p.error;
    auto const a = ot::apply_patch(base, p.hunks);
    ASSERT_TRUE(a.error.empty()) << a.error;
    EXPECT_EQ(a.text, "<a>\n <b>\n  <c />\n </b>\n</a>\n");
}

// The overlap rule: hunks may share context lines, but not a rewritten line or an
// insertion position.
TEST(OverlayPatchHelper, OverlappingContextsApply) {
    auto const p = ot::parse_patch(
        "@@ first\n   <c x='1' />\n+  <e />\n  </b>\n"
        "@@ second\n  </b>\n+ <g />\n  <b>\n");
    ASSERT_TRUE(p.error.empty()) << p.error;
    auto const a = ot::apply_patch(kHelperBase, p.hunks);
    ASSERT_TRUE(a.error.empty()) << a.error;
    EXPECT_EQ(a.text,
              "<a>\n <b>\n  <c x='1' />\n  <e />\n </b>\n <g />\n <b>\n  <d />\n </b>\n</a>\n");
}

constexpr std::string_view kRewriteBase =
    "<m>\n <field name='A' required='N' />\n <field name='B' required='N' />\n</m>\n";

TEST(OverlayPatchHelper, TwoHunksRewritingOneLineFailTheCell) {
    auto const p = ot::parse_patch(
        "@@ one\n <m>\n- <field name='A' required='N' />\n+ <field name='A' required='Y' />\n"
        "  <field name='B' required='N' />\n"
        "@@ two\n- <field name='A' required='N' />\n+ <field name='A' required='Y' />\n"
        "  <field name='B' required='N' />\n </m>\n");
    ASSERT_TRUE(p.error.empty()) << p.error;
    auto const a = ot::apply_patch(kRewriteBase, p.hunks);
    EXPECT_NE(a.error.find("rewrite one line"), std::string::npos) << a.error;
}

TEST(OverlayPatchHelper, ARewrittenLineAnotherHunksContextNamesFailsTheCell) {
    auto const p = ot::parse_patch(
        "@@ rewrite\n <m>\n- <field name='A' required='N' />\n+ <field name='A' required='Y' />\n"
        "  <field name='B' required='N' />\n"
        "@@ context\n  <field name='A' required='N' />\n+ <field name='C' required='N' />\n"
        "  <field name='B' required='N' />\n");
    ASSERT_TRUE(p.error.empty()) << p.error;
    auto const a = ot::apply_patch(kRewriteBase, p.hunks);
    EXPECT_NE(a.error.find("rewrites a line the context of hunk"), std::string::npos) << a.error;
}

TEST(OverlayPatchHelper, TwoHunksInsertingAtOnePositionFailTheCell) {
    auto const p = ot::parse_patch(
        "@@ one\n   <c x='1' />\n+  <e />\n  </b>\n"
        "@@ two\n  <b>\n   <c x='1' />\n+  <f />\n  </b>\n");
    ASSERT_TRUE(p.error.empty()) << p.error;
    auto const a = ot::apply_patch(kHelperBase, p.hunks);
    EXPECT_NE(a.error.find("insert at one position"), std::string::npos) << a.error;
}

// ── W-2, the vendored bases ──────────────────────────────────────────────────

struct vendored_row {
    char const* name;
    char const* base;           // under dictionaries/
    char const* overlay;        // under fixtures/098/real/
    char const* patch;          // under fixtures/098/real/
    bool assert_no_exclusions;  // the small bases and FIXT11
    char const* seeded_hunk;    // the hunk the seeded mismatch drops (its label)
    std::set<std::string_view> seeded_expect;
    fmt format = fmt::quickfix_xml;
};

std::vector<vendored_row> const& vendored_rows() {
    static std::vector<vendored_row> const rows{
        {.name = "FIX44",
         .base = "FIX44.xml",
         .overlay = "FIX44_overlay.xml",
         .patch = "FIX44.patch",
         .assert_no_exclusions = true,
         .seeded_hunk = "field 40: value Z appended",
         .seeded_expect = {ids::kEnumValue, ids::kEnumCodes}},
        {.name = "FIX50SP2",
         .base = "FIX50SP2.xml",
         .overlay = "FIX50SP2_overlay.xml",
         .patch = "FIX50SP2.patch",
         .assert_no_exclusions = false,
         .seeded_hunk = "field 40: value Z appended",
         .seeded_expect = {ids::kEnumValue, ids::kEnumCodes}},
        {.name = "FIXT11",
         .base = "FIXT11.xml",
         .overlay = "FIXT11_overlay.xml",
         .patch = "FIXT11.patch",
         .assert_no_exclusions = true,
         .seeded_hunk = "header: Custom098HeaderTag (5020) appended to <header>",
         .seeded_expect = {ids::kMessageFields.tag, ids::kValid}},
        {.name = "Orchestra",
         .base = "orchestra/OrchestraFIXLatest.xml",
         .overlay = "Orchestra_overlay.xml",
         .patch = "Orchestra.patch",
         .assert_no_exclusions = false,
         .seeded_hunk = "codeSet MsgTypeCodeSet (35): code U1 appended",
         .seeded_expect = {ids::kEnumValue, ids::kEnumCodes},
         .format = fmt::orchestra},
    };
    return rows;
}

class OverlayEquivalenceW2 : public ::testing::TestWithParam<std::size_t> {};

std::string patched_text(vendored_row const& row, char const* drop_label) {
    std::string const base = ot::read_file(ot::dict_dir() / row.base);
    auto parsed = ot::parse_patch(ot::read_file(ot::fixture_dir() / "real" / row.patch));
    EXPECT_TRUE(parsed.error.empty()) << row.name << ": " << parsed.error;
    if (drop_label != nullptr) {
        auto const before = parsed.hunks.size();
        std::erase_if(parsed.hunks, [&](ot::hunk const& h) { return h.label == drop_label; });
        EXPECT_EQ(parsed.hunks.size() + 1, before)
            << row.name << ": the seeded hunk '" << drop_label << "' must exist exactly once";
    }
    auto const a = ot::apply_patch(base, parsed.hunks);
    EXPECT_TRUE(a.error.empty()) << row.name << ": " << a.error;
    return a.text;
}

ov::xml_overlay_probe probe_vendored(vendored_row const& row) {
    std::filesystem::path const base = ot::dict_dir() / row.base;
    std::filesystem::path const overlay = ot::fixture_dir() / "real" / row.overlay;
    return row.format == fmt::orchestra ? ov::probe_orchestra_overlay(base, overlay, mr())
                                        : ov::probe_xml_overlay(base, overlay, mr());
}

TEST_P(OverlayEquivalenceW2, OverlayEqualsThePatchedBase) {
    vendored_row const& row = vendored_rows().at(GetParam());
    std::string const patched = patched_text(row, nullptr);
    ASSERT_FALSE(patched.empty()) << row.name << ": the patch did not apply";
    auto const p = probe_vendored(row);
    ot::expect_loaded_at_prediction(p, std::string{"W-2 "} + row.name);
    ASSERT_TRUE(p.dictionary.has_value()) << ot::describe(p);
    Dictionary const reference = load_text_as(row.format, patched);
    expect_equivalent(std::string{"W-2 "} + row.name,
                      compare_against(*p.dictionary, reference, patched, row.format),
                      row.assert_no_exclusions);
}

TEST_P(OverlayEquivalenceW2, SeededMismatchWithOneHunkDroppedIsReported) {
    vendored_row const& row = vendored_rows().at(GetParam());
    std::string const seeded = patched_text(row, row.seeded_hunk);
    ASSERT_FALSE(seeded.empty()) << row.name << ": the patch did not apply";
    auto const p = probe_vendored(row);
    ASSERT_TRUE(p.dictionary.has_value()) << ot::describe(p);
    Dictionary const reference = load_text_as(row.format, seeded);
    expect_reports(std::string{"W-2 "} + row.name,
                   compare_against(*p.dictionary, reference, seeded, row.format),
                   row.seeded_expect);
}

INSTANTIATE_TEST_SUITE_P(Vendored, OverlayEquivalenceW2, ::testing::Values(0U, 1U, 2U, 3U),
                         [](auto const& info) {
                             return std::string{vendored_rows().at(info.param).name};
                         });

// SC-005's at-cap overlay (T017) loads with no refusal.
TEST(OverlayEquivalenceW2AtCap, Fix50Sp2AtCapOverlayLoads) {
    auto const p =
        ov::probe_xml_overlay(ot::dict_dir() / "FIX50SP2.xml",
                              ot::fixture_dir() / "real" / "FIX50SP2_at_cap_overlay.xml", mr());
    ot::expect_loaded_at_prediction(p, "W-2 FIX50SP2 at-cap");
    if (p.guards) {
        std::cout << "[098 W-2 FIX50SP2 at-cap] predicted " << p.guards->merged_total
                  << ", base total " << p.guards->base_total << '\n';
    }
}

// ── W-2, the empty overlay on every runtime base ─────────────────────────────

// An empty `<fix>` overlay naming the base's version.
std::string empty_overlay_for(std::string_view base_text) {
    pugi::xml_document doc;
    EXPECT_TRUE(doc.load_buffer(base_text.data(), base_text.size()));
    pugi::xml_node const r = doc.child("fix");
    return std::string{"<fix type='"} + r.attribute("type").as_string("FIX") + "' major='" +
           r.attribute("major").as_string("") + "' minor='" + r.attribute("minor").as_string("") +
           "' servicepack='" + r.attribute("servicepack").as_string("0") + "'/>\n";
}

// The seeded removal (contracts W-2): the field number of the first `<field>`
// definition in document order that lists two or more `<value>`s. Empty when none.
std::string first_multi_valued_field(std::string_view base_text) {
    pugi::xml_document doc;
    EXPECT_TRUE(doc.load_buffer(base_text.data(), base_text.size()));
    for (pugi::xml_node f = doc.child("fix").child("fields").child("field"); f;
         f = f.next_sibling("field")) {
        pugi::xml_node const v = f.child("value");
        if (v && v.next_sibling("value")) {
            return f.attribute("number").as_string("");
        }
    }
    return {};
}

// The base text without that definition's last `<value>`. The anchor is the
// definition's opening `<field number='N'` line, which must match exactly once;
// the removed line is the value line right before the definition's `</field>`.
// Empty on failure.
std::string without_last_value(std::string_view base_text, std::string const& number) {
    auto lines = ot::lines_of(base_text);
    auto const trimmed = [](std::string const& l) {
        auto const lead = l.find_first_not_of(" \t");
        return lead == std::string::npos ? std::string_view{} : std::string_view{l}.substr(lead);
    };
    std::vector<std::size_t> hits;
    for (char const q : {'\'', '"'}) {
        std::string const head = std::string{"<field number="} + q + number + q;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            std::string_view const t = trimmed(lines[i]);
            if (t.starts_with(head) && t.size() > head.size() &&
                (t[head.size()] == ' ' || t[head.size()] == '>')) {
                hits.push_back(i);
            }
        }
    }
    EXPECT_EQ(hits.size(), 1U) << "the anchor of field " << number << " must match exactly once";
    if (hits.size() != 1) {
        return {};
    }
    std::size_t close = hits.front() + 1;
    while (close < lines.size() && trimmed(lines[close]) != "</field>") {
        ++close;
    }
    EXPECT_LT(close, lines.size()) << "field " << number << " has no closing line";
    if (close == lines.size()) {
        return {};
    }
    std::string_view const last = trimmed(lines[close - 1]);
    EXPECT_TRUE(last.starts_with("<value ") && last.ends_with("/>"))
        << "field " << number << ": the line before </field> is not one value element: " << last;
    if (!last.starts_with("<value ") || !last.ends_with("/>")) {
        return {};
    }
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(close) - 1);
    return ot::join_lines(lines);
}

class OverlayEquivalenceW2Empty : public ::testing::TestWithParam<std::string_view> {};

TEST_P(OverlayEquivalenceW2Empty, EmptyOverlayEqualsTheBase) {
    std::filesystem::path const base_path = ot::dict_dir() / std::string{GetParam()};
    std::string const base_text = ot::read_file(base_path);
    ot::temp_file const overlay{empty_overlay_for(base_text)};
    auto const p = ov::probe_xml_overlay(base_path, overlay.path(), mr());
    std::string const row = "W-2 empty " + std::string{GetParam()};
    ot::expect_loaded_at_prediction(p, row);
    ASSERT_TRUE(p.dictionary.has_value()) << ot::describe(p);
    Dictionary const reference = XmlLoader{}.load(base_path, mr());
    expect_equivalent(row, compare_against(*p.dictionary, reference, base_text),
                      GetParam() == "FIXT11.xml");
}

// Seeded mismatch: `load(base)` against a copy of the base without one enum value
// (the recipe above); the comparator must report the enum stores.
TEST_P(OverlayEquivalenceW2Empty, SeededMismatchWithoutOneEnumValueIsReported) {
    std::filesystem::path const base_path = ot::dict_dir() / std::string{GetParam()};
    std::string const base_text = ot::read_file(base_path);
    std::string const number = first_multi_valued_field(base_text);
    ASSERT_FALSE(number.empty()) << GetParam() << " has no field definition with two values";
    std::string const copy = without_last_value(base_text, number);
    ASSERT_FALSE(copy.empty());
    Dictionary const base = XmlLoader{}.load(base_path, mr());
    std::optional<Dictionary> seeded;
    ASSERT_NO_THROW(seeded.emplace(load_text(copy)))
        << "the copy without one value of field " << number << " must load";
    eq::raw_census const census_base = eq::census_from_quickfix_xml(base_text);
    eq::raw_census const census_copy = eq::census_from_quickfix_xml(copy);
    auto const r = eq::compare(eq::side{.dict = base, .census = census_base},
                               eq::side{.dict = *seeded, .census = census_copy});
    std::cout << "[098 W-2 empty " << GetParam() << "] seeded removal: the last value of field "
              << number << '\n';
    expect_reports("W-2 empty " + std::string{GetParam()}, r, {ids::kEnumValue, ids::kEnumCodes});
}

INSTANTIATE_TEST_SUITE_P(RuntimeDicts, OverlayEquivalenceW2Empty,
                         ::testing::ValuesIn(fixpp_test_support::kRuntimeDicts),
                         [](auto const& info) {
                             std::string n{info.param};
                             std::erase(n, '.');
                             return n;
                         });

// ── W-2, the empty overlay on the Orchestra file ─────────────────────────────

std::filesystem::path orchestra_base() {
    return ot::dict_dir() / "orchestra" / "OrchestraFIXLatest.xml";
}

// An empty `fixr:repository` overlay naming the base's version.
std::string empty_orch_overlay_for(std::string_view base_text) {
    pugi::xml_document doc;
    EXPECT_TRUE(doc.load_buffer(base_text.data(), base_text.size()));
    return std::string{
               "<fixr:repository xmlns:fixr=\"http://fixprotocol.io/2020/orchestra/"
               "repository\" version=\""} +
           doc.child("fixr:repository").attribute("version").as_string("") + "\"/>\n";
}

// The seeded removal (contracts W-2), Orchestra form: the name of the first
// `fixr:codeSet` in document order that lists two or more `fixr:code`s. Empty when
// none.
std::string first_multi_code_codeset(std::string_view base_text) {
    pugi::xml_document doc;
    EXPECT_TRUE(doc.load_buffer(base_text.data(), base_text.size()));
    for (pugi::xml_node cs =
             doc.child("fixr:repository").child("fixr:codeSets").child("fixr:codeSet");
         cs; cs = cs.next_sibling("fixr:codeSet")) {
        pugi::xml_node const c = cs.child("fixr:code");
        if (c && c.next_sibling("fixr:code")) {
            return cs.attribute("name").as_string("");
        }
    }
    return {};
}

// The base text without that code set's last `fixr:code` element, which may span
// several lines. The anchor is the code set's opening `<fixr:codeSet` line carrying
// its name, which must match exactly once. Empty on failure.
std::string without_last_code(std::string_view base_text, std::string const& name) {
    auto lines = ot::lines_of(base_text);
    auto const trimmed = [](std::string const& l) {
        auto const lead = l.find_first_not_of(" \t");
        return lead == std::string::npos ? std::string_view{} : std::string_view{l}.substr(lead);
    };
    std::string const named = " name=\"" + name + "\"";
    std::vector<std::size_t> hits;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string_view const t = trimmed(lines[i]);
        if (t.starts_with("<fixr:codeSet ") && t.find(named) != std::string_view::npos) {
            hits.push_back(i);
        }
    }
    EXPECT_EQ(hits.size(), 1U) << "the anchor of code set " << name << " must match exactly once";
    if (hits.size() != 1) {
        return {};
    }
    std::size_t close = hits.front() + 1;
    std::size_t last_code = lines.size();
    while (close < lines.size() && trimmed(lines[close]) != "</fixr:codeSet>") {
        if (trimmed(lines[close]).starts_with("<fixr:code ")) {
            last_code = close;
        }
        ++close;
    }
    EXPECT_LT(close, lines.size()) << "code set " << name << " has no closing line";
    EXPECT_LT(last_code, close) << "code set " << name << " has no fixr:code line";
    if (close == lines.size() || last_code >= close) {
        return {};
    }
    std::size_t end = last_code;
    if (!trimmed(lines[last_code]).ends_with("/>")) {
        while (end < close && trimmed(lines[end]) != "</fixr:code>") {
            ++end;
        }
        EXPECT_LT(end, close) << "code set " << name << ": its last fixr:code is not closed";
        if (end >= close) {
            return {};
        }
    }
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(last_code),
                lines.begin() + static_cast<std::ptrdiff_t>(end) + 1);
    return ot::join_lines(lines);
}

TEST(OverlayEquivalenceW2EmptyOrchestra, EmptyOverlayEqualsTheBase) {
    std::string const base_text = ot::read_file(orchestra_base());
    ot::temp_file const overlay{empty_orch_overlay_for(base_text)};
    auto const p = ov::probe_orchestra_overlay(orchestra_base(), overlay.path(), mr());
    ot::expect_loaded_at_prediction(p, "W-2 empty Orchestra");
    ASSERT_TRUE(p.dictionary.has_value()) << ot::describe(p);
    Dictionary const reference = OrchestraLoader{}.load(orchestra_base(), mr());
    expect_equivalent("W-2 empty Orchestra",
                      compare_against(*p.dictionary, reference, base_text, fmt::orchestra),
                      /*assert_no_exclusions=*/false);
}

// Seeded mismatch: `load(base)` against a copy of the base without one code (the
// recipe above); the comparator must report the enum stores.
TEST(OverlayEquivalenceW2EmptyOrchestra, SeededMismatchWithoutOneCodeIsReported) {
    std::string const base_text = ot::read_file(orchestra_base());
    std::string const name = first_multi_code_codeset(base_text);
    ASSERT_FALSE(name.empty()) << "the Orchestra file has no code set with two codes";
    std::string const copy = without_last_code(base_text, name);
    ASSERT_FALSE(copy.empty());
    Dictionary const base = OrchestraLoader{}.load(orchestra_base(), mr());
    std::optional<Dictionary> seeded;
    ASSERT_NO_THROW(seeded.emplace(load_orch_text(copy)))
        << "the copy without one code of code set " << name << " must load";
    eq::raw_census const census_base = eq::census_from_orchestra(base_text);
    eq::raw_census const census_copy = eq::census_from_orchestra(copy);
    auto const r = eq::compare(eq::side{.dict = base, .census = census_base},
                               eq::side{.dict = *seeded, .census = census_copy});
    std::cout << "[098 W-2 empty Orchestra] seeded removal: the last code of code set " << name
              << '\n';
    expect_reports("W-2 empty Orchestra", r, {ids::kEnumValue, ids::kEnumCodes});
}

// ── W-4, load_any_with_overlay ───────────────────────────────────────────────

struct triple_paths {
    std::filesystem::path base;
    std::filesystem::path overlay;
    std::filesystem::path edited;  // its census keys both sides
    fmt format;
};

Dictionary direct_load(triple_paths const& t, unresolved_group_policy policy) {
    return t.format == fmt::orchestra
               ? OrchestraLoader{}.load_with_overlay(t.base, t.overlay, mr(), policy)
               : XmlLoader{}.load_with_overlay(t.base, t.overlay, mr(), policy);
}

void expect_dispatch_matches(triple_paths const& t, unresolved_group_policy policy,
                             std::string const& row) {
    Dictionary const any = fixpp::dict::load_any_with_overlay(t.base, t.overlay, mr(), policy);
    Dictionary const direct = direct_load(t, policy);
    expect_equivalent(row, compare_against(any, direct, ot::read_file(t.edited), t.format),
                      /*assert_no_exclusions=*/true);
}

TEST(OverlayLoadAnyW4, QuickFixXmlMatchesTheDirectLoader) {
    expect_dispatch_matches(
        {qf("base.xml"), qf("overlay.xml"), qf("edited.xml"), fmt::quickfix_xml},
        unresolved_group_policy::fail_closed, "W-4 qf");
}

TEST(OverlayLoadAnyW4, OrchestraMatchesTheDirectLoader) {
    expect_dispatch_matches(
        {orch("base.xml"), orch("overlay.xml"), orch("edited.xml"), fmt::orchestra},
        unresolved_group_policy::fail_closed, "W-4 orch");
}

// W-1's tolerant triples, dispatched with `tolerant`, against the direct loader
// called with `tolerant`.
TEST(OverlayLoadAnyW4, TolerantMatchesTheDirectLoaderCalledWithTolerant) {
    expect_dispatch_matches({qf("base_tolerant.xml"), qf("overlay_tolerant.xml"),
                             qf("edited_tolerant.xml"), fmt::quickfix_xml},
                            unresolved_group_policy::tolerant, "W-4 qf tolerant");
    expect_dispatch_matches({orch("base_tolerant.xml"), orch("overlay_tolerant.xml"),
                             orch("edited_tolerant.xml"), fmt::orchestra},
                            unresolved_group_policy::tolerant, "W-4 orch tolerant");
}

void expect_mixed_refused(std::filesystem::path const& base, std::filesystem::path const& overlay,
                          std::string_view root, std::string const& row) {
    try {
        static_cast<void>(fixpp::dict::load_any_with_overlay(base, overlay, mr()));
        ADD_FAILURE() << row << ": a mixed-format pair loaded";
    } catch (std::exception const& e) {
        EXPECT_EQ(typeid(e), typeid(fixpp::dict::overlay_conflict_error))
            << row << ": " << e.what();
        EXPECT_NE(std::string_view{e.what()}.find(root), std::string_view::npos)
            << row << ": the message must name the overlay root " << root << ": " << e.what();
    }
}

TEST(OverlayLoadAnyW4, AMixedFormatPairIsRefused) {
    expect_mixed_refused(qf("base.xml"), orch("overlay.xml"), "<fixr:repository>",
                         "W-4 qf base, orch overlay");
    expect_mixed_refused(orch("base.xml"), qf("overlay.xml"), "<fix>", "W-4 orch base, qf overlay");
}

// ── the FIX50SP2 wrap probe (research R-10): reported, never asserted ─────────

TEST(OverlayWrapProbe, Fix50Sp2FirstComponentPastTheWrapPoint) {
    std::string const text = ot::read_file(ot::dict_dir() / "FIX50SP2.xml");
    eq::raw_census const census = eq::census_from_quickfix_xml(text);
    Dictionary const d = XmlLoader{}.load(ot::dict_dir() / "FIX50SP2.xml", mr());
    std::size_t start = 0;
    for (auto const& [name, members] : census.components) {
        if (start > 0xFFFFU || members.size() > 0xFFFFU) {
            std::vector<std::uint16_t> got;
            for (auto const& f : d.component_fields(name)) {
                got.push_back(f.tag);
            }
            auto const cr = d.component(name);
            std::cout << "[098 wrap probe] FIX50SP2 component \"" << name << "\": census start "
                      << start << ", count " << members.size() << "; loader first_field_index "
                      << (cr ? std::to_string(cr->first_field_index) : std::string{"(none)"})
                      << ", component_fields " << (got == members ? "EQUAL to" : "DIFFERENT from")
                      << " its raw members (wrap " << (got == members ? "not shown" : "shown")
                      << ")\n";
            return;
        }
        start += members.size();
    }
    std::cout << "[098 wrap probe] FIX50SP2: no component past the wrap point (the component "
                 "side table fits)\n";
}

}  // namespace
