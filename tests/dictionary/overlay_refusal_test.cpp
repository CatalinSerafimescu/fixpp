// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/overlay_refusal_test.cpp — W-3 of 098-dialect-overlay, the
// QuickFIX-XML column (contracts/witnesses.md "W-3 Refusals"; research R-4, R-5).
//
// The kinds of refusal are the closed list `kCells` below, one entry per kind with a
// cell per format: a row (the test names that witness it), N/A with its reason, or
// pending for the Orchestra rows tasks T041 writes. `CellTable.HoldsEveryKindForEveryFormat`
// asserts every cell is one of those and every named row exists.
//
// Every refusal row loads through research R-5's read seam, catches the refusal as a
// `std::exception`, and asserts its exact dynamic type (`typeid` equality) and that
// its message names the rule, key, cap, table or counter origin. Rows that load
// assert counter = step 6's prediction.

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/error.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <memory_resource>
#include <pugixml.hpp>
#include <set>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#include "overlay_test_support.hpp"
#include "reused_tag_census.hpp"

namespace {

namespace ot = overlay_test;
namespace ov = fixpp::dict::detail::overlay;
using fixpp::dict::Dictionary;
using fixpp::dict::group_delimiter_collision_error;
using fixpp::dict::overlay_conflict_error;
using fixpp::dict::overlay_limit_error;
using fixpp::dict::xml_parse_error;
using fixpp::dict::XmlLoader;

std::pmr::memory_resource* mr() { return std::pmr::new_delete_resource(); }

constexpr std::string_view kOpen = "<fix type='FIX' major='4' minor='4' servicepack='0'>\n";

std::string overlay_doc(std::string_view body) {
    return std::string{kOpen} + std::string{body} + "</fix>\n";
}

std::string field_def(int number, std::string_view name, std::string_view type) {
    return "  <field number='" + std::to_string(number) + "' name='" + std::string{name} +
           "' type='" + std::string{type} + "' />\n";
}

// The general small base of the W-3 rows: NewOrderSingle reaches NoPartyIDs(453)
// through component Parties, ExecutionReport through Outer → Parties (a two-level
// chain); Instrument roots the doubling bomb.
std::string base_doc(std::string_view extra_messages = {}, std::string_view extra_components = {},
                     std::string_view extra_fields = {}, std::string_view d_extra = {}) {
    return std::string{kOpen} +
           " <header>\n"
           "  <field name='BeginString' required='Y' />\n"
           "  <field name='BodyLength' required='Y' />\n"
           "  <field name='MsgType' required='Y' />\n"
           " </header>\n"
           " <trailer>\n"
           "  <field name='CheckSum' required='Y' />\n"
           " </trailer>\n"
           " <messages>\n"
           "  <message name='Heartbeat' msgtype='0' msgcat='admin'>\n"
           "   <field name='TestReqID' required='N' />\n"
           "  </message>\n"
           "  <message name='NewOrderSingle' msgtype='D' msgcat='app'>\n"
           "   <field name='ClOrdID' required='Y' />\n"
           "   <field name='Account' required='N' />\n"
           "   <component name='Parties' required='N' />\n"
           "   <component name='Instrument' required='Y' />\n"
           "   <field name='Side' required='Y' />\n"
           "   <group name='NoAllocs' required='N'>\n"
           "    <field name='AllocAccount' required='N' />\n"
           "   </group>\n"
           "   <field name='Text' required='N' />\n" +
           std::string{d_extra} +
           "  </message>\n"
           "  <message name='ExecutionReport' msgtype='8' msgcat='app'>\n"
           "   <field name='OrderID' required='Y' />\n"
           "   <component name='Outer' required='N' />\n"
           "  </message>\n" +
           std::string{extra_messages} +
           " </messages>\n"
           " <components>\n"
           "  <component name='Instrument'>\n"
           "   <field name='Symbol' required='Y' />\n"
           "   <field name='SecurityID' required='N' />\n"
           "  </component>\n"
           "  <component name='Parties'>\n"
           "   <group name='NoPartyIDs' required='N'>\n"
           "    <field name='PartyID' required='N' />\n"
           "   </group>\n"
           "  </component>\n"
           "  <component name='Outer'>\n"
           "   <component name='Parties' required='N' />\n"
           "  </component>\n" +
           std::string{extra_components} +
           " </components>\n"
           " <fields>\n" +
           field_def(1, "Account", "STRING") + field_def(8, "BeginString", "STRING") +
           field_def(9, "BodyLength", "LENGTH") + field_def(10, "CheckSum", "STRING") +
           field_def(11, "ClOrdID", "STRING") + field_def(35, "MsgType", "STRING") +
           field_def(37, "OrderID", "STRING") + field_def(48, "SecurityID", "STRING") +
           "  <field number='54' name='Side' type='CHAR'>\n"
           "   <value enum='1' description='BUY' />\n"
           "   <value enum='2' description='SELL' />\n"
           "  </field>\n" +
           field_def(55, "Symbol", "STRING") + field_def(58, "Text", "STRING") +
           field_def(78, "NoAllocs", "NUMINGROUP") + field_def(79, "AllocAccount", "STRING") +
           field_def(112, "TestReqID", "STRING") + field_def(448, "PartyID", "STRING") +
           field_def(453, "NoPartyIDs", "NUMINGROUP") + std::string{extra_fields} +
           " </fields>\n"
           "</fix>\n";
}

ov::xml_overlay_probe probe(std::string_view base, std::string_view overlay) {
    return ov::probe_xml_overlay_from_string(base, overlay, mr());
}

// Exact type and every named part of the message.
template <class E>
void expect_refused(ov::xml_overlay_probe const& p, std::initializer_list<std::string_view> parts,
                    std::string const& row) {
    ot::refusal const r = ot::refusal_of(p);
    ASSERT_TRUE(r.refused) << row << ": expected a refusal; " << ot::describe(p);
    EXPECT_EQ(r.type, typeid(E).name()) << row << ": " << ot::describe(p);
    for (std::string_view const part : parts) {
        EXPECT_NE(r.what.find(part), std::string::npos)
            << row << ": the message must name '" << part << "': " << r.what;
    }
}

void expect_origin(ov::xml_overlay_probe const& p, ov::refusal_origin want,
                   std::string const& row) {
    EXPECT_EQ(p.origin, want) << row << ": origin " << ot::origin_name(p.origin) << ", expected "
                              << ot::origin_name(want) << "; " << ot::describe(p);
}

// ── R-4 conflicts ────────────────────────────────────────────────────────────

TEST(OverlayRefusalW3Xml, IdentifyingAttributeChangedFieldType) {
    auto const p = probe(
        base_doc(), overlay_doc(" <fields>\n" + field_def(11, "ClOrdID", "INT") + " </fields>\n"));
    expect_refused<overlay_conflict_error>(
        p, {"identifying attribute changed", "number=\"11\"", "type=\"INT\""}, "field type");
}

TEST(OverlayRefusalW3Xml, IdentifyingAttributeChangedFieldName) {
    auto const p = probe(
        base_doc(),
        overlay_doc(" <fields>\n" + field_def(11, "ClientOrderID", "STRING") + " </fields>\n"));
    expect_refused<overlay_conflict_error>(
        p, {"identifying attribute changed", "number=\"11\"", "name=\"ClientOrderID\""},
        "field name");
}

TEST(OverlayRefusalW3Xml, IdentifyingAttributeChangedMessageName) {
    auto const p = probe(base_doc(), overlay_doc(" <messages>\n"
                                                 "  <message name='Order' msgtype='D' "
                                                 "msgcat='app' />\n"
                                                 " </messages>\n"));
    expect_refused<overlay_conflict_error>(
        p, {"identifying attribute changed", "msgtype=\"D\"", "name=\"Order\""}, "message name");
}

TEST(OverlayRefusalW3Xml, FieldNameAlreadyUsedByAnotherNumber) {
    auto const p =
        probe(base_doc(),
              overlay_doc(" <fields>\n" + field_def(9001, "ClOrdID", "STRING") + " </fields>\n"));
    expect_refused<overlay_conflict_error>(
        p, {"field name already used by another number", "9001", "ClOrdID"}, "name reuse");
}

TEST(OverlayRefusalW3Xml, RequiredRelaxed) {
    auto const p = probe(base_doc(), overlay_doc(" <messages>\n"
                                                 "  <message name='NewOrderSingle' "
                                                 "msgtype='D' msgcat='app'>\n"
                                                 "   <field name='ClOrdID' required='N' />\n"
                                                 "  </message>\n"
                                                 " </messages>\n"));
    expect_refused<overlay_conflict_error>(
        p, {"required relaxed", "name=\"ClOrdID\"", "msgtype=\"D\""}, "required relaxed");
}

TEST(OverlayRefusalW3Xml, RedescribedEnumValue) {
    auto const p = probe(base_doc(), overlay_doc(" <fields>\n"
                                                 "  <field number='54' name='Side' type='CHAR'>\n"
                                                 "   <value enum='1' description='PURCHASE' />\n"
                                                 "  </field>\n"
                                                 " </fields>\n"));
    expect_refused<overlay_conflict_error>(p, {"redescribed enum value", "enum=\"1\"", "field 54"},
                                           "redescribed value");
}

void presence_row(std::string_view value) {
    auto const p = probe(base_doc(), overlay_doc(" <messages>\n"
                                                 "  <message name='NewOrderSingle' "
                                                 "msgtype='D' msgcat='app'>\n"
                                                 "   <field name='Account' required='" +
                                                 std::string{value} +
                                                 "' />\n"
                                                 "  </message>\n"
                                                 " </messages>\n"));
    expect_refused<overlay_conflict_error>(
        p, {"invalid required value", "name=\"Account\"", "required=\"" + std::string{value}},
        "required=" + std::string{value});
}

TEST(OverlayRefusalW3Xml, PresenceVocabularyLowercaseY) { presence_row("y"); }
TEST(OverlayRefusalW3Xml, PresenceVocabularyFalse) { presence_row("false"); }

TEST(OverlayRefusalW3Xml, DuplicateWithinTheOverlay) {
    auto const p =
        probe(base_doc(), overlay_doc(" <fields>\n" + field_def(9001, "VenueA", "STRING") +
                                      field_def(9001, "VenueA", "STRING") + " </fields>\n"));
    expect_refused<overlay_conflict_error>(p, {"duplicate within the overlay", "number=\"9001\""},
                                           "duplicate in overlay");
}

TEST(OverlayRefusalW3Xml, TwoRootContainersOfOneName) {
    auto const p = probe(base_doc(), overlay_doc(" <fields>\n </fields>\n <fields>\n </fields>\n"));
    expect_refused<overlay_conflict_error>(p, {"two root containers", "<fields>"},
                                           "two root containers");
}

TEST(OverlayRefusalW3Xml, VersionMismatch) {
    auto const p = probe(base_doc(),
                         "<fix type='FIX' major='4' minor='2' servicepack='0'>\n"
                         "</fix>\n");
    expect_refused<overlay_conflict_error>(p, {"version mismatch", "minor=\"2\""},
                                           "version mismatch");
}

TEST(OverlayRefusalW3Xml, OverlayOfTheOtherFormat) {
    auto const p = probe(base_doc(),
                         "<fixr:repository xmlns:fixr='http://fixprotocol.io/2020/orchestra/"
                         "repository' version='FIX.Latest_EP303'>\n"
                         "</fixr:repository>\n");
    expect_refused<overlay_conflict_error>(p, {"<fixr:repository>", "is not <fix>"},
                                           "other format");
}

TEST(OverlayRefusalW3Xml, RootChildOutsideTheList) {
    auto const p = probe(base_doc(), overlay_doc(" <datatypes>\n </datatypes>\n"));
    expect_refused<overlay_conflict_error>(p, {"root child <datatypes>", "is not allowed"},
                                           "root child");
}

// ── one tag twice at one level ───────────────────────────────────────────────

TEST(OverlayRefusalW3Xml, ChildRedeclaredThatTheParentReachesThroughAComponent) {
    auto const p = probe(base_doc(), overlay_doc(" <messages>\n"
                                                 "  <message name='NewOrderSingle' "
                                                 "msgtype='D' msgcat='app'>\n"
                                                 "   <group name='NoPartyIDs' required='N'>\n"
                                                 "    <field name='PartyID' required='N' />\n"
                                                 "   </group>\n"
                                                 "  </message>\n"
                                                 " </messages>\n"));
    expect_refused<overlay_conflict_error>(
        p, {"tag occurs twice at one level", "tag 453", "msgtype=\"D\""}, "redeclared direct");
}

TEST(OverlayRefusalW3Xml, ChildRedeclaredThroughATwoLevelComponentChain) {
    auto const p = probe(base_doc(), overlay_doc(" <messages>\n"
                                                 "  <message name='ExecutionReport' "
                                                 "msgtype='8' msgcat='app'>\n"
                                                 "   <field name='NoPartyIDs' required='N' />\n"
                                                 "  </message>\n"
                                                 " </messages>\n"));
    expect_refused<overlay_conflict_error>(
        p, {"tag occurs twice at one level", "tag 453", "msgtype=\"8\""}, "two-level chain");
}

TEST(OverlayRefusalW3Xml, NewComponentReferenceReachingADeclaredTag) {
    auto const p = probe(base_doc(), overlay_doc(" <messages>\n"
                                                 "  <message name='NewOrderSingle' "
                                                 "msgtype='D' msgcat='app'>\n"
                                                 "   <component name='VenueBlock' "
                                                 "required='N' />\n"
                                                 "  </message>\n"
                                                 " </messages>\n"
                                                 " <components>\n"
                                                 "  <component name='VenueBlock'>\n"
                                                 "   <field name='ClOrdID' required='N' />\n"
                                                 "  </component>\n"
                                                 " </components>\n"));
    expect_refused<overlay_conflict_error>(
        p, {"tag occurs twice at one level", "tag 11", "msgtype=\"D\""}, "new component ref");
}

TEST(OverlayRefusalW3Xml, HeaderAdditionWhoseTagAMessageBodyHas) {
    auto const p = probe(base_doc(), overlay_doc(" <header>\n"
                                                 "  <field name='Text' required='N' />\n"
                                                 " </header>\n"));
    expect_refused<overlay_conflict_error>(
        p, {"tag occurs twice at one level", "tag 58", "msgtype=\"D\""}, "header addition");
}

// Upward closure (research R-3's rule): NewOrderSingle declares Text(58) directly and
// inlines Instrument; the overlay adds Text to Instrument, so Text occurs twice in
// NewOrderSingle's level although NewOrderSingle itself is not extended.
TEST(OverlayRefusalW3Xml, AdditionToAnInlinedComponentReachingATagTheParentDeclares) {
    auto const p = probe(base_doc(), overlay_doc(" <components>\n"
                                                 "  <component name='Instrument'>\n"
                                                 "   <field name='Text' required='N' />\n"
                                                 "  </component>\n"
                                                 " </components>\n"));
    expect_refused<overlay_conflict_error>(
        p, {"tag occurs twice at one level", "tag 58", "msgtype=\"D\""}, "upward closure");
}

// ── repeated base keys ───────────────────────────────────────────────────────

constexpr std::string_view kSecondD =
    "  <message name='NewOrderSingle' msgtype='D' msgcat='app'>\n"
    "   <field name='ClOrdID' required='Y' />\n"
    "  </message>\n";

TEST(OverlayRefusalW3Xml, RepeatedBaseMsgTypeNamesBothOccurrences) {
    auto const p = probe(base_doc(kSecondD), overlay_doc(" <messages>\n"
                                                         "  <message name='NewOrderSingle' "
                                                         "msgtype='D' msgcat='app' />\n"
                                                         " </messages>\n"));
    expect_refused<overlay_conflict_error>(
        p,
        {"repeated base key", "msgtype=\"D\"", "(occurrence 2 of <message>",
         "(occurrence 4 of <message>"},
        "repeated msgtype");
}

TEST(OverlayRefusalW3Xml, RepeatedBaseFieldNameNamesBothOccurrences) {
    std::string const extra =
        field_def(9500, "VenueDup", "STRING") + field_def(9501, "VenueDup", "STRING");
    auto const p =
        probe(base_doc({}, {}, extra),
              overlay_doc(" <fields>\n" + field_def(9001, "VenueDup", "STRING") + " </fields>\n"));
    expect_refused<overlay_conflict_error>(
        p, {"repeated base key", "number=\"9500\"", "number=\"9501\""}, "repeated field name");
}

TEST(OverlayRefusalW3Xml, RepeatedBaseDirectChildNamesBothOccurrences) {
    auto const p = probe(base_doc({}, {}, {}, "   <field name='Text' required='N' />\n"),
                         overlay_doc(" <messages>\n"
                                     "  <message name='NewOrderSingle' msgtype='D' "
                                     "msgcat='app'>\n"
                                     "   <field name='Text' required='Y' />\n"
                                     "  </message>\n"
                                     " </messages>\n"));
    expect_refused<overlay_conflict_error>(
        p,
        {"repeated base key", "<field name=\"Text\"> (occurrence 4 of <field>",
         "<field name=\"Text\"> (occurrence 5 of <field>"},
        "repeated direct child");
}

std::string base_with_repeated_enum() {
    std::string b = base_doc();
    std::string const anchor = "   <value enum='2' description='SELL' />\n";
    b.insert(b.find(anchor) + anchor.size(), "   <value enum='1' description='BUY' />\n");
    return b;
}

TEST(OverlayRefusalW3Xml, RepeatedBaseEnumValueNamesBothOccurrences) {
    auto const p = probe(base_with_repeated_enum(),
                         overlay_doc(" <fields>\n"
                                     "  <field number='54' name='Side' type='CHAR'>\n"
                                     "   <value enum='1' description='BUY' />\n"
                                     "  </field>\n"
                                     " </fields>\n"));
    expect_refused<overlay_conflict_error>(
        p,
        {"repeated base key", "<value enum=\"1\"> (occurrence 1 of <value>",
         "<value enum=\"1\"> (occurrence 3 of <value>"},
        "repeated enum value");
}

// Controls: each repeated-key base loads with an empty overlay.
TEST(OverlayRefusalW3Xml, RepeatedKeyBasesLoadWithAnEmptyOverlay) {
    std::string const empty = overlay_doc({});
    for (std::string const& b :
         {base_doc(kSecondD),
          base_doc({}, {},
                   field_def(9500, "VenueDup", "STRING") + field_def(9501, "VenueDup", "STRING")),
          base_doc({}, {}, {}, "   <field name='Text' required='N' />\n"),
          base_with_repeated_enum()}) {
        ot::expect_loaded_at_prediction(probe(b, empty), "repeated-key control");
    }
}

// The loader itself refuses these keys repeated, so their cells are N/A; established
// by running the base loader alone.
TEST(OverlayRefusalW3Xml, LoaderRefusesARepeatedFieldNumber) {
    std::string const b = base_doc({}, {}, field_def(11, "ClOrdID2", "STRING"));
    try {
        static_cast<void>(XmlLoader{}.load_from_string(b, mr()));
        ADD_FAILURE() << "the base loader admitted a repeated field number";
    } catch (std::exception const& e) {
        EXPECT_EQ(typeid(e), typeid(xml_parse_error));
        EXPECT_NE(std::string_view{e.what()}.find("duplicate <field number=\"11\">"),
                  std::string_view::npos)
            << e.what();
    }
}

TEST(OverlayRefusalW3Xml, LoaderRefusesARepeatedComponentName) {
    std::string const b = base_doc({}, "  <component name='Outer'>\n  </component>\n");
    try {
        static_cast<void>(XmlLoader{}.load_from_string(b, mr()));
        ADD_FAILURE() << "the base loader admitted a repeated component name";
    } catch (std::exception const& e) {
        EXPECT_EQ(typeid(e), typeid(xml_parse_error));
        EXPECT_NE(std::string_view{e.what()}.find("duplicate <component name=\"Outer\">"),
                  std::string_view::npos)
            << e.what();
    }
}

// ── the Length+Data relation (step 8a) ───────────────────────────────────────
// Each row's base is a minimal dictionary built for its cell; "earlier" and "later"
// are `detect_length_pairs`'s visit order (fields, header, trailer, messages,
// components, groups). BaseLen(7010) LENGTH, BaseData(7011) DATA, VenueData(9100) a
// new DATA, VenueLen(9101) a new LENGTH.

struct ld_base {
    std::string messages;
    std::string components;
    std::string fields;  // after the common ones; the last must not be a LENGTH unless intended
};

std::string ld_doc(ld_base const& b) {
    return std::string{kOpen} +
           " <header>\n"
           "  <field name='BeginString' required='Y' />\n"
           "  <field name='MsgType' required='Y' />\n"
           " </header>\n"
           " <messages>\n" +
           b.messages + " </messages>\n <components>\n" + b.components +
           " </components>\n <fields>\n" + field_def(8, "BeginString", "STRING") +
           field_def(11, "ClOrdID", "STRING") + field_def(35, "MsgType", "STRING") +
           field_def(58, "Text", "STRING") + b.fields + " </fields>\n</fix>\n";
}

std::string const kNewData = field_def(9100, "VenueData", "DATA");
std::string const kNewLen = field_def(9101, "VenueLen", "LENGTH");
std::string const kVenueComp =
    " <components>\n  <component name='VenueComp'>\n   <field name='VenueTag' required='N' "
    "/>\n  </component>\n </components>\n";
std::string const kVenueTag = field_def(9102, "VenueTag", "STRING");

std::string msg_d(std::string_view body) {
    return "  <message name='NewOrderSingle' msgtype='D' msgcat='app'>\n"
           "   <field name='ClOrdID' required='Y' />\n" +
           std::string{body} + "  </message>\n";
}

std::string extend_d(std::string_view body, std::string_view more = {}) {
    return " <messages>\n  <message name='NewOrderSingle' msgtype='D' msgcat='app'>\n" +
           std::string{body} + "  </message>\n </messages>\n" + std::string{more};
}

std::string extend_c(std::string_view body, std::string_view more = {}) {
    return " <components>\n  <component name='C'>\n" + std::string{body} +
           "  </component>\n </components>\n" + std::string{more};
}

std::string fref(std::string_view name) {
    return "   <field name='" + std::string{name} + "' required='N' />\n";
}

void expect_relation_unchanged(std::string const& base, ov::xml_overlay_probe const& p,
                               std::string const& row) {
    ot::expect_loaded_at_prediction(p, row);
    ASSERT_TRUE(p.dictionary.has_value());
    Dictionary const b = XmlLoader{}.load_from_string(base, mr());
    auto const tb = b.as_table_view();
    auto const tm = p.dictionary->as_table_view();
    pugi::xml_document doc;
    ASSERT_TRUE(doc.load_buffer(base.data(), base.size()));
    ov::length_pair_relation const rel = ov::xml_length_pair_relation(doc);
    for (std::uint16_t const t : rel.declared_tags) {
        EXPECT_EQ(b.length_pair_data_tag(t), p.dictionary->length_pair_data_tag(t))
            << row << ": Dictionary::length_pair_data_tag(" << t << ")";
        EXPECT_EQ(tb.length_pair_data_tag(t), tm.length_pair_data_tag(t))
            << row << ": table_view::length_pair_data_tag(" << t << ")";
        EXPECT_EQ(tb.data_pair_length_tag(t), tm.data_pair_length_tag(t))
            << row << ": table_view::data_pair_length_tag(" << t << ")";
    }
}

void expect_pair_refused(ov::xml_overlay_probe const& p, std::string_view tag,
                         std::string const& row) {
    expect_refused<overlay_conflict_error>(p, {"Length+Data pair relation of base tag", tag}, row);
}

// <fields>: the base's last field is an unpaired LENGTH; a new DATA appended to
// <fields> pairs with it.
TEST(OverlayRefusalW3XmlLengthData, FieldsNewDataAfterAnUnpairedBaseLength) {
    std::string const base = ld_doc({.messages = msg_d(fref("BaseLen")),
                                     .components = {},
                                     .fields = field_def(7010, "BaseLen", "LENGTH")});
    auto const p = probe(base, overlay_doc(" <fields>\n" + kNewData + " </fields>\n"));
    expect_pair_refused(p, "7010", "fields / unpaired");
}

// <fields>: the base pairs BaseLen in a message (later); a new DATA appended right
// after BaseLen, last in <fields>, takes it first and the base pair is lost.
TEST(OverlayRefusalW3XmlLengthData, FieldsNewDataTakesALengthTheBasePairsLater) {
    std::string const base = ld_doc(
        {.messages = msg_d(fref("BaseLen") + fref("BaseData")),
         .components = {},
         .fields = field_def(7011, "BaseData", "DATA") + field_def(7010, "BaseLen", "LENGTH")});
    auto const p = probe(base, overlay_doc(" <fields>\n" + kNewData + " </fields>\n"));
    expect_pair_refused(p, "7010", "fields / paired later");
}

// strict container (component C)
TEST(OverlayRefusalW3XmlLengthData, ComponentNewDataAfterAnUnpairedBaseLength) {
    std::string const base = ld_doc(
        {.messages = msg_d("   <component name='C' />\n"),
         .components = "  <component name='C'>\n" + fref("BaseLen") + "  </component>\n",
         .fields = field_def(7010, "BaseLen", "LENGTH") + field_def(7020, "Tail", "STRING")});
    auto const p = probe(
        base, overlay_doc(extend_c(fref("VenueData"), " <fields>\n" + kNewData + " </fields>\n")));
    expect_pair_refused(p, "7010", "component / unpaired");
}

TEST(OverlayRefusalW3XmlLengthData, ComponentNewDataAfterALengthTheBasePairsEarlierLoads) {
    std::string const base =
        ld_doc({.messages = msg_d("   <component name='C' />\n"),
                .components = "  <component name='C'>\n" + fref("BaseLen") + "  </component>\n",
                .fields = field_def(7010, "BaseLen", "LENGTH") +
                          field_def(7011, "BaseData", "DATA") + field_def(7020, "Tail", "STRING")});
    auto const p = probe(
        base, overlay_doc(extend_c(fref("VenueData"), " <fields>\n" + kNewData + " </fields>\n")));
    expect_relation_unchanged(base, p, "component / paired earlier");
}

TEST(OverlayRefusalW3XmlLengthData, ComponentNewDataTakesALengthTheBasePairsLater) {
    std::string const base = ld_doc(
        {.messages = msg_d("   <component name='C' />\n"
                           "   <group name='NoBase' required='N'>\n" +
                           fref("BaseLen") + fref("BaseData") + "   </group>\n"),
         .components = "  <component name='C'>\n" + fref("BaseLen") + "  </component>\n",
         .fields = field_def(7010, "BaseLen", "LENGTH") + field_def(7020, "Tail", "STRING") +
                   field_def(7011, "BaseData", "DATA") + field_def(7100, "NoBase", "NUMINGROUP")});
    auto const p = probe(
        base, overlay_doc(extend_c(fref("VenueData"), " <fields>\n" + kNewData + " </fields>\n")));
    expect_pair_refused(p, "7010", "component / paired later");
}

TEST(OverlayRefusalW3XmlLengthData, ComponentNewLengthBeforeABaseData) {
    std::string const base =
        ld_doc({.messages = msg_d("   <component name='C' />\n"),
                .components = "  <component name='C'>\n" + fref("Text") + "  </component>\n",
                .fields = field_def(7011, "BaseData", "DATA") + field_def(7020, "Tail", "STRING")});
    auto const p = probe(base, overlay_doc(extend_c(fref("VenueLen") + fref("BaseData"),
                                                    " <fields>\n" + kNewLen + " </fields>\n")));
    expect_pair_refused(p, "7011", "component / new Length");
}

// non-strict container (message), a group between
std::string const kNoBaseGroup =
    "   <group name='NoBase' required='N'>\n" + fref("BaseTag") + "   </group>\n";
std::string const kNoBaseFields =
    field_def(7100, "NoBase", "NUMINGROUP") + field_def(7101, "BaseTag", "STRING");

TEST(OverlayRefusalW3XmlLengthData, MessageGroupBetweenNewDataAfterAnUnpairedBaseLength) {
    std::string const base =
        ld_doc({.messages = msg_d(fref("BaseLen") + kNoBaseGroup),
                .components = {},
                .fields = field_def(7010, "BaseLen", "LENGTH") + kNoBaseFields});
    auto const p = probe(
        base, overlay_doc(extend_d(fref("VenueData"), " <fields>\n" + kNewData + " </fields>\n")));
    expect_pair_refused(p, "7010", "message, group between / unpaired");
}

TEST(OverlayRefusalW3XmlLengthData, MessageGroupBetweenNewDataAfterALengthPairedEarlierLoads) {
    std::string const base =
        ld_doc({.messages = msg_d(fref("BaseLen") + kNoBaseGroup),
                .components = {},
                .fields = field_def(7010, "BaseLen", "LENGTH") +
                          field_def(7011, "BaseData", "DATA") + kNoBaseFields});
    auto const p = probe(
        base, overlay_doc(extend_d(fref("VenueData"), " <fields>\n" + kNewData + " </fields>\n")));
    expect_relation_unchanged(base, p, "message, group between / paired earlier");
}

std::string const kLaterPair =
    "  <message name='ExecutionReport' msgtype='8' msgcat='app'>\n"
    "   <component name='C2' />\n"
    "  </message>\n";
std::string const kLaterPairComp =
    "  <component name='C2'>\n" + fref("BaseLen") + fref("BaseData") + "  </component>\n";

TEST(OverlayRefusalW3XmlLengthData, MessageGroupBetweenNewDataTakesALengthPairedLater) {
    std::string const base =
        ld_doc({.messages = msg_d(fref("BaseLen") + kNoBaseGroup) + kLaterPair,
                .components = kLaterPairComp,
                .fields = field_def(7010, "BaseLen", "LENGTH") + field_def(7020, "Tail", "STRING") +
                          field_def(7011, "BaseData", "DATA") + kNoBaseFields});
    auto const p = probe(
        base, overlay_doc(extend_d(fref("VenueData"), " <fields>\n" + kNewData + " </fields>\n")));
    expect_pair_refused(p, "7010", "message, group between / paired later");
}

// An append-only overlay can put a group between a new Length and a base Data only
// by appending the group as well; its NumInGroup is a base field.
TEST(OverlayRefusalW3XmlLengthData, MessageGroupBetweenNewLengthBeforeABaseData) {
    std::string const base =
        ld_doc({.messages = msg_d(fref("Text")),
                .components = {},
                .fields = field_def(7011, "BaseData", "DATA") + kNoBaseFields});
    auto const p =
        probe(base, overlay_doc(extend_d(fref("VenueLen") + kNoBaseGroup + fref("BaseData"),
                                         " <fields>\n" + kNewLen + " </fields>\n")));
    expect_pair_refused(p, "7011", "message, group between / new Length");
}

// non-strict container (message), an overlay component between
TEST(OverlayRefusalW3XmlLengthData, MessageComponentBetweenNewDataAfterAnUnpairedBaseLength) {
    std::string const base = ld_doc(
        {.messages = msg_d(fref("BaseLen")),
         .components = {},
         .fields = field_def(7010, "BaseLen", "LENGTH") + field_def(7020, "Tail", "STRING")});
    auto const p = probe(
        base,
        overlay_doc(extend_d("   <component name='VenueComp' />\n" + fref("VenueData"),
                             kVenueComp + " <fields>\n" + kNewData + kVenueTag + " </fields>\n")));
    expect_pair_refused(p, "7010", "message, component between / unpaired");
}

TEST(OverlayRefusalW3XmlLengthData, MessageComponentBetweenNewDataAfterALengthPairedEarlierLoads) {
    std::string const base =
        ld_doc({.messages = msg_d(fref("BaseLen")),
                .components = {},
                .fields = field_def(7010, "BaseLen", "LENGTH") +
                          field_def(7011, "BaseData", "DATA") + field_def(7020, "Tail", "STRING")});
    auto const p = probe(
        base,
        overlay_doc(extend_d("   <component name='VenueComp' />\n" + fref("VenueData"),
                             kVenueComp + " <fields>\n" + kNewData + kVenueTag + " </fields>\n")));
    expect_relation_unchanged(base, p, "message, component between / paired earlier");
}

TEST(OverlayRefusalW3XmlLengthData, MessageComponentBetweenNewDataTakesALengthPairedLater) {
    std::string const base = ld_doc(
        {.messages = msg_d(fref("BaseLen")) + kLaterPair,
         .components = kLaterPairComp,
         .fields = field_def(7010, "BaseLen", "LENGTH") + field_def(7020, "Tail", "STRING") +
                   field_def(7011, "BaseData", "DATA") + field_def(7021, "Tail2", "STRING")});
    auto const p = probe(
        base,
        overlay_doc(extend_d("   <component name='VenueComp' />\n" + fref("VenueData"),
                             kVenueComp + " <fields>\n" + kNewData + kVenueTag + " </fields>\n")));
    expect_pair_refused(p, "7010", "message, component between / paired later");
}

TEST(OverlayRefusalW3XmlLengthData, MessageComponentBetweenNewLengthBeforeABaseData) {
    std::string const base =
        ld_doc({.messages = msg_d(fref("Text")),
                .components = {},
                .fields = field_def(7011, "BaseData", "DATA") + field_def(7020, "Tail", "STRING")});
    auto const p =
        probe(base, overlay_doc(extend_d(
                        fref("VenueLen") + "   <component name='VenueComp' />\n" + fref("BaseData"),
                        kVenueComp + " <fields>\n" + kNewLen + kVenueTag + " </fields>\n")));
    expect_pair_refused(p, "7011", "message, component between / new Length");
}

// ── R-5 guards: caps (each cap + 1 refused, each cap admitted) ───────────────

std::string padded_overlay(std::size_t total) {
    std::string const head{kOpen};
    std::string const tail = "</fix>\n";
    std::string s = head;
    s.append(total - head.size() - tail.size(), ' ');
    s += tail;
    return s;
}

TEST(OverlayRefusalW3XmlCaps, ByteCapPlusOneStringForm) {
    auto const p = probe(base_doc(), padded_overlay(ov::kMaxOverlayBytes + 1));
    expect_refused<overlay_limit_error>(p, {"byte cap", std::to_string(ov::kMaxOverlayBytes)},
                                        "bytes cap+1 (string)");
}

TEST(OverlayRefusalW3XmlCaps, ByteCapPlusOnePathForm) {
    ot::temp_file const b{base_doc()};
    ot::temp_file const o{padded_overlay(ov::kMaxOverlayBytes + 1)};
    auto const p = ov::probe_xml_overlay(b.path(), o.path(), mr());
    expect_refused<overlay_limit_error>(p, {"byte cap", std::to_string(ov::kMaxOverlayBytes)},
                                        "bytes cap+1 (path)");
}

TEST(OverlayRefusalW3XmlCaps, ByteCapAtCapLoadsThroughBothForms) {
    std::string const overlay = padded_overlay(ov::kMaxOverlayBytes);
    ASSERT_EQ(overlay.size(), ov::kMaxOverlayBytes);
    ot::expect_loaded_at_prediction(probe(base_doc(), overlay), "bytes at cap (string)");
    ot::temp_file const b{base_doc()};
    ot::temp_file const o{overlay};
    ot::expect_loaded_at_prediction(ov::probe_xml_overlay(b.path(), o.path(), mr()),
                                    "bytes at cap (path)");
}

// A new message whose body nests groups so that the deepest element sits at `depth`
// (the root is level 1).
std::string nested_overlay(std::size_t depth) {
    std::size_t const groups = depth - 4;  // fix, messages, message, ..., the innermost field
    std::string body = " <messages>\n  <message name='VenueDeep' msgtype='U9' msgcat='app'>\n";
    for (std::size_t i = 0; i < groups; ++i) {
        body += "<group name='NoDeep" + std::to_string(i) + "' required='N'>\n";
    }
    body += "<field name='VenueTag' required='N' />\n";
    for (std::size_t i = 0; i < groups; ++i) {
        body += "</group>\n";
    }
    body += "  </message>\n </messages>\n <fields>\n";
    for (std::size_t i = 0; i < groups; ++i) {
        body += field_def(20000 + static_cast<int>(i), "NoDeep" + std::to_string(i), "NUMINGROUP");
    }
    body += field_def(9102, "VenueTag", "STRING") + " </fields>\n";
    return overlay_doc(body);
}

TEST(OverlayRefusalW3XmlCaps, DepthCapPlusOne) {
    auto const p = probe(base_doc(), nested_overlay(ov::kMaxDepth + 1));
    expect_refused<overlay_limit_error>(p, {"depth cap", std::to_string(ov::kMaxDepth)},
                                        "depth cap+1");
}

TEST(OverlayRefusalW3XmlCaps, DepthAtCapLoads) {
    ot::expect_loaded_at_prediction(probe(base_doc(), nested_overlay(ov::kMaxDepth)),
                                    "depth at cap");
}

std::string new_fields_overlay(std::size_t n) {
    std::string body = " <fields>\n";
    for (std::size_t i = 0; i < n; ++i) {
        body += field_def(20000 + static_cast<int>(i), "VenueF" + std::to_string(i), "STRING");
    }
    return overlay_doc(body + " </fields>\n");
}

std::string new_components_overlay(std::size_t n) {
    std::string body = " <components>\n";
    for (std::size_t i = 0; i < n; ++i) {
        body += "  <component name='VenueC" + std::to_string(i) + "'>\n  </component>\n";
    }
    return overlay_doc(body + " </components>\n");
}

std::string new_messages_overlay(std::size_t n) {
    std::string body = " <messages>\n";
    for (std::size_t i = 0; i < n; ++i) {
        body += "  <message name='VenueM" + std::to_string(i) + "' msgtype='U" +
                std::to_string(100 + i) + "' msgcat='app'>\n  </message>\n";
    }
    return overlay_doc(body + " </messages>\n");
}

std::string new_values_overlay(std::size_t n) {
    std::string body = " <fields>\n  <field number='20000' name='VenueEnum' type='STRING'>\n";
    for (std::size_t i = 0; i < n; ++i) {
        body += "   <value enum='V" + std::to_string(i) + "' description='D" + std::to_string(i) +
                "' />\n";
    }
    return overlay_doc(body + "  </field>\n </fields>\n");
}

TEST(OverlayRefusalW3XmlCaps, NewFieldCapPlusOne) {
    expect_refused<overlay_limit_error>(
        probe(base_doc(), new_fields_overlay(ov::kMaxNewFields + 1)),
        {"new field definitions cap", "1024"}, "fields cap+1");
}
TEST(OverlayRefusalW3XmlCaps, NewFieldAtCapLoads) {
    ot::expect_loaded_at_prediction(probe(base_doc(), new_fields_overlay(ov::kMaxNewFields)),
                                    "fields at cap");
}
TEST(OverlayRefusalW3XmlCaps, NewComponentCapPlusOne) {
    expect_refused<overlay_limit_error>(
        probe(base_doc(), new_components_overlay(ov::kMaxNewComponents + 1)),
        {"new component definitions cap", "64"}, "components cap+1");
}
TEST(OverlayRefusalW3XmlCaps, NewComponentAtCapLoads) {
    ot::expect_loaded_at_prediction(
        probe(base_doc(), new_components_overlay(ov::kMaxNewComponents)), "components at cap");
}
TEST(OverlayRefusalW3XmlCaps, NewMessageCapPlusOne) {
    expect_refused<overlay_limit_error>(
        probe(base_doc(), new_messages_overlay(ov::kMaxNewMessages + 1)),
        {"new message definitions cap", "64"}, "messages cap+1");
}
TEST(OverlayRefusalW3XmlCaps, NewMessageAtCapLoads) {
    ot::expect_loaded_at_prediction(probe(base_doc(), new_messages_overlay(ov::kMaxNewMessages)),
                                    "messages at cap");
}
TEST(OverlayRefusalW3XmlCaps, EnumValueCapPlusOne) {
    expect_refused<overlay_limit_error>(
        probe(base_doc(), new_values_overlay(ov::kMaxNewEnumValues + 1)),
        {"added enum values cap", "4096"}, "enum values cap+1");
}
TEST(OverlayRefusalW3XmlCaps, EnumValueAtCapLoads) {
    ot::expect_loaded_at_prediction(probe(base_doc(), new_values_overlay(ov::kMaxNewEnumValues)),
                                    "enum values at cap");
}

// `appended` reference elements spread over new messages (each referencing distinct
// new fields), plus `merged` identical redeclarations or promotions of references
// NewOrderSingle already has.
std::string references_overlay(std::size_t appended, std::size_t merged) {
    // Enough per message that the appended-only cap + 1 row stays within the
    // new-message cap.
    constexpr std::size_t kPerMessage = 130;
    static constexpr std::string_view kMergeable[] = {
        "   <field name='ClOrdID' required='Y' />\n", "   <field name='Account' required='Y' />\n",
        "   <field name='Side' required='Y' />\n", "   <field name='Text' required='N' />\n",
        "   <component name='Instrument' required='Y' />\n"};
    EXPECT_LE(merged, std::size(kMergeable));
    std::string body = " <messages>\n  <message name='NewOrderSingle' msgtype='D' msgcat='app'>\n";
    for (std::size_t i = 0; i < merged; ++i) {
        body += kMergeable[i];
    }
    body += "  </message>\n";
    for (std::size_t m = 0; appended > m * kPerMessage; ++m) {
        body += "  <message name='VenueM" + std::to_string(m) + "' msgtype='U" +
                std::to_string(100 + m) + "' msgcat='app'>\n";
        for (std::size_t i = m * kPerMessage; i < std::min(appended, (m + 1) * kPerMessage); ++i) {
            body += fref("VenueF" + std::to_string(i % kPerMessage));
        }
        body += "  </message>\n";
    }
    body += " </messages>\n <fields>\n";
    for (std::size_t i = 0; i < kPerMessage; ++i) {
        body += field_def(20000 + static_cast<int>(i), "VenueF" + std::to_string(i), "STRING");
    }
    return overlay_doc(body + " </fields>\n");
}

TEST(OverlayRefusalW3XmlCaps, ReferenceCapPlusOne) {
    expect_refused<overlay_limit_error>(
        probe(base_doc(), references_overlay(ov::kMaxReferences + 1, 0)),
        {"reference elements cap", "8192"}, "references cap+1");
}
TEST(OverlayRefusalW3XmlCaps, ReferenceAtCapLoads) {
    ot::expect_loaded_at_prediction(probe(base_doc(), references_overlay(ov::kMaxReferences, 0)),
                                    "references at cap");
}

// (cap − k) appended plus (k + 1) merged in place reaches cap + 1 only if the
// merged-in-place references count.
constexpr std::size_t kMergedK = 3;
TEST(OverlayRefusalW3XmlCaps, MergedInPlaceReferencesCountTowardsTheCap) {
    expect_refused<overlay_limit_error>(
        probe(base_doc(), references_overlay(ov::kMaxReferences - kMergedK, kMergedK + 1)),
        {"reference elements cap", "8192"}, "merged-in-place cap+1");
}
TEST(OverlayRefusalW3XmlCaps, MergedInPlaceMixAtTheCapLoads) {
    ot::expect_loaded_at_prediction(
        probe(base_doc(), references_overlay(ov::kMaxReferences - kMergedK, kMergedK)),
        "merged-in-place at cap");
}

TEST(OverlayRefusalW3XmlCaps, Doctype) {
    auto const p = probe(base_doc(), "<!DOCTYPE fix [<!ENTITY x 'y'>]>\n" + overlay_doc({}));
    expect_refused<overlay_limit_error>(p, {"DOCTYPE"}, "doctype");
}

// ── R-5 guards: the merged-graph walk ────────────────────────────────────────

std::string const kCycleOverlay =
    " <components>\n"
    "  <component name='Instrument'>\n"
    "   <component name='VenueLoop' required='N' />\n"
    "  </component>\n"
    "  <component name='VenueLoop'>\n"
    "   <component name='Instrument' required='N' />\n"
    "  </component>\n"
    " </components>\n";

TEST(OverlayRefusalW3XmlWalk, CycleClosingThroughABaseComponent) {
    auto const p = probe(base_doc(), overlay_doc(kCycleOverlay));
    expect_refused<xml_parse_error>(
        p, {"component reference cycle", "component Instrument", "component VenueLoop"}, "cycle");
}

// The loader's own refusals after the splice: characterization pins.
TEST(OverlayRefusalW3XmlPins, UnknownReferenceIsTheLoadersError) {
    auto const p = probe(base_doc(), overlay_doc(extend_d(fref("Nope"))));
    expect_refused<xml_parse_error>(p, {"<field name=\"Nope\"> not declared"}, "unknown ref");
}

TEST(OverlayRefusalW3XmlPins, UnknownFieldTypeIsTheLoadersError) {
    auto const p =
        probe(base_doc(),
              overlay_doc(" <fields>\n" + field_def(9001, "VenueX", "BOGUS") + " </fields>\n"));
    expect_refused<xml_parse_error>(p, {"<field type=\"BOGUS\">"}, "unknown type");
}

TEST(OverlayRefusalW3XmlPins, GroupDelimiterCollisionTheMergeCreates) {
    auto const p =
        probe(base_doc(),
              overlay_doc(extend_d("   <group name='NoAllocs' required='N'>\n"
                                   "    <group name='NoVenueNested' required='N'>\n"
                                   "     <field name='AllocAccount' required='N' />\n"
                                   "    </group>\n"
                                   "   </group>\n",
                                   " <fields>\n" + field_def(9200, "NoVenueNested", "NUMINGROUP") +
                                       " </fields>\n")));
    expect_refused<group_delimiter_collision_error>(p, {"nested group 9200"}, "delim collision");
}

// The doubling bomb (A-001): C_n = { component C_{n-1}; group NoG_n { field F_n;
// component C_{n-1} } }, C_0 = the base's Instrument, as many levels as the new
// component cap allows.
std::string doubling_overlay() {
    std::string comps = " <components>\n";
    std::string fields = " <fields>\n";
    for (std::size_t n = 1; n < ov::kMaxNewComponents; ++n) {
        std::string const prev = n == 1 ? "Instrument" : "VenueC" + std::to_string(n - 1);
        comps += "  <component name='VenueC" + std::to_string(n) +
                 "'>\n"
                 "   <component name='" +
                 prev +
                 "' required='N' />\n"
                 "   <group name='NoVenueG" +
                 std::to_string(n) +
                 "' required='N'>\n"
                 "    <field name='VenueF" +
                 std::to_string(n) +
                 "' required='N' />\n"
                 "    <component name='" +
                 prev +
                 "' required='N' />\n"
                 "   </group>\n"
                 "  </component>\n";
        fields +=
            field_def(20000 + static_cast<int>(n), "NoVenueG" + std::to_string(n), "NUMINGROUP") +
            field_def(21000 + static_cast<int>(n), "VenueF" + std::to_string(n), "STRING");
    }
    return overlay_doc(comps + " </components>\n" + fields + " </fields>\n");
}

// Two empty-bodied chains, each level referencing both chains' previous level.
std::string empty_chains_overlay() {
    std::string comps =
        " <components>\n  <component name='VenueA0'>\n  </component>\n"
        "  <component name='VenueB0'>\n  </component>\n";
    for (std::size_t n = 1; 2 * (n + 1) <= ov::kMaxNewComponents; ++n) {
        for (char const c : {'A', 'B'}) {
            comps += std::string{"  <component name='Venue"} + c + std::to_string(n) +
                     "'>\n"
                     "   <component name='VenueA" +
                     std::to_string(n - 1) +
                     "' required='N' />\n"
                     "   <component name='VenueB" +
                     std::to_string(n - 1) +
                     "' required='N' />\n"
                     "  </component>\n";
        }
    }
    return overlay_doc(comps + " </components>\n");
}

void expect_base_fits(ov::xml_overlay_probe const& p, std::string const& row) {
    ASSERT_TRUE(p.guards.has_value()) << row << ": " << ot::describe(p);
    EXPECT_TRUE(p.guards->base_components_fit && p.guards->base_groups_fit)
        << row << ": the doubling rows need a base whose side tables fit";
}

TEST(OverlayRefusalW3XmlWalk, DoublingBombRefusedByTheBudget) {
    auto const p = probe(base_doc(), doubling_overlay());
    expect_base_fits(p, "doubling");
    expect_refused<overlay_limit_error>(p, {"expansion budget"}, "doubling");
    expect_origin(p, ov::refusal_origin::budget, "doubling");
}

TEST(OverlayRefusalW3XmlWalk, TwoEmptyChainsRefusedByTheBudget) {
    auto const p = probe(base_doc(), empty_chains_overlay());
    expect_base_fits(p, "empty chains");
    expect_refused<overlay_limit_error>(p, {"expansion budget"}, "empty chains");
    expect_origin(p, ov::refusal_origin::budget, "empty chains");
}

// Header expansion: a base with an empty <messages>, an overlay whose
// header holds nested groups that each reference one large component.
TEST(OverlayRefusalW3XmlWalk, HeaderExpansionLoadsAtThePrediction) {
    std::string const base = std::string{kOpen} +
                             " <header>\n  <field name='BeginString' required='Y' />\n"
                             " </header>\n <messages>\n </messages>\n <fields>\n" +
                             field_def(8, "BeginString", "STRING") + " </fields>\n</fix>\n";
    std::string big = "  <component name='VenueBig'>\n";
    std::string fields = " <fields>\n";
    for (int i = 0; i < 256; ++i) {
        big += fref("VenueBigF" + std::to_string(i));
        fields += field_def(20000 + i, "VenueBigF" + std::to_string(i), "STRING");
    }
    big += "  </component>\n";
    fields += field_def(21001, "NoVenueH1", "NUMINGROUP") +
              field_def(21002, "NoVenueH2", "NUMINGROUP") + " </fields>\n";
    std::string const overlay = overlay_doc(
        " <header>\n"
        "  <group name='NoVenueH1' required='N'>\n"
        "   <component name='VenueBig' required='N' />\n"
        "   <group name='NoVenueH2' required='N'>\n"
        "    <component name='VenueBig' required='N' />\n"
        "   </group>\n"
        "  </group>\n"
        " </header>\n <components>\n" +
        big + " </components>\n" + fields);
    ot::expect_loaded_at_prediction(probe(base, overlay), "header expansion");
}

// ── side-table fit (N-3) ─────────────────────────────────────────────────────
// A generated base whose side tables fit: message A1 holds `kGroups` groups NoSG_i
// of one member each (each the first-seen group of its tag), and three components
// declared in the order E0, E1, Late. VenueBig is a new component of `kBigFields`
// fields; a group NoSG_i { component VenueBig } adds 1 + kBigFields entries to its
// parent level and leaves the groups table alone, since NoSG_i is first seen in A1.

constexpr int kGroups = 700;
constexpr int kBigFields = 100;

std::string side_table_base() {
    std::string msg = "  <message name='SideTable' msgtype='A1' msgcat='app'>\n";
    std::string fields;
    for (int i = 0; i < kGroups; ++i) {
        msg += "   <group name='NoSG" + std::to_string(i) + "' required='N'>\n" +
               fref("SGF" + std::to_string(i)) + "   </group>\n";
        fields += field_def(10000 + i, "NoSG" + std::to_string(i), "NUMINGROUP") +
                  field_def(11000 + i, "SGF" + std::to_string(i), "STRING");
    }
    msg += "  </message>\n";
    return std::string{kOpen} +
           " <header>\n  <field name='BeginString' required='Y' />\n"
           " </header>\n <messages>\n" +
           msg +
           " </messages>\n <components>\n"
           "  <component name='E0'>\n" +
           fref("BeginString") +
           "  </component>\n"
           "  <component name='E1'>\n" +
           fref("BeginString") +
           "  </component>\n"
           "  <component name='Late'>\n" +
           fref("BeginString") + "  </component>\n </components>\n <fields>\n" +
           field_def(8, "BeginString", "STRING") + fields + " </fields>\n</fix>\n";
}

std::string big_component_and_fields(std::string_view extra_fields = {}) {
    std::string big = "  <component name='VenueBig'>\n";
    std::string fields = " <fields>\n";
    for (int i = 0; i < kBigFields; ++i) {
        big += fref("VenueBigF" + std::to_string(i));
        fields += field_def(20000 + i, "VenueBigF" + std::to_string(i), "STRING");
    }
    return big + "  </component>\n" + "@@FIELDS@@" + fields + std::string{extra_fields} +
           " </fields>\n";
}

// `count` groups NoSG_i { component VenueBig } starting at `first`.
std::string big_groups(int first, int count) {
    std::string s;
    for (int i = first; i < first + count; ++i) {
        s += "   <group name='NoSG" + std::to_string(i) +
             "' required='N'>\n"
             "    <component name='VenueBig' required='N' />\n   </group>\n";
    }
    return s;
}

// components = the overlay's <components> body (VenueBig is added), messages = its
// <messages> body, extra fields beyond VenueBig's.
std::string side_overlay(std::string const& components, std::string const& messages = {},
                         std::string const& extra_fields = {}) {
    std::string const bc = big_component_and_fields(extra_fields);
    std::size_t const cut = bc.find("@@FIELDS@@");
    std::string body;
    if (!messages.empty()) {
        body += " <messages>\n" + messages + " </messages>\n";
    }
    body += " <components>\n" + components + bc.substr(0, cut) + " </components>\n" +
            bc.substr(cut + std::string_view{"@@FIELDS@@"}.size());
    return overlay_doc(body);
}

// The entries one NoSG_i { component VenueBig } adds.
constexpr int kPerGroup = 1 + kBigFields;

std::string grow_e0_e1(int each) {
    return "  <component name='E0'>\n" + big_groups(0, each) +
           "  </component>\n"
           "  <component name='E1'>\n" +
           big_groups(each, each) + "  </component>\n";
}

TEST(OverlayRefusalW3XmlSideTable, EarlyComponentsPushALaterBaseStartPastTheTable) {
    // E0 and E1 each stay within a count of 65535; Late's start passes it.
    int const each = (65536 / 2) / kPerGroup + 1;
    auto const p = probe(side_table_base(), side_overlay(grow_e0_e1(each)));
    expect_refused<overlay_limit_error>(p, {"components side table"}, "components start");
    expect_origin(p, ov::refusal_origin::side_table, "components start");
}

TEST(OverlayRefusalW3XmlSideTable, EarlyComponentsJustInsideTheTableLoad) {
    // Every start, VenueBig's after Late included, stays within 65535.
    int const each = (65535 - 2 - kBigFields) / 2 / kPerGroup;
    ot::expect_loaded_at_prediction(probe(side_table_base(), side_overlay(grow_e0_e1(each))),
                                    "components start control");
}

TEST(OverlayRefusalW3XmlSideTable, ANewComponentWhoseCountPassesTheTable) {
    int const n = 65536 / kPerGroup + 1;
    auto const p = probe(side_table_base(), side_overlay("  <component name='VenueHuge'>\n" +
                                                         big_groups(0, n) + "  </component>\n"));
    expect_refused<overlay_limit_error>(p, {"components side table"}, "component count");
    expect_origin(p, ov::refusal_origin::side_table, "component count");
}

TEST(OverlayRefusalW3XmlSideTable, ANewComponentJustInsideTheTableLoads) {
    int const n = (65535 - 3 - kBigFields) / kPerGroup;
    ot::expect_loaded_at_prediction(
        probe(side_table_base(), side_overlay("  <component name='VenueHuge'>\n" +
                                              big_groups(0, n) + "  </component>\n")),
        "component count control");
}

// A large group NoVenueLow(5), lower than every base NumInGroup tag, in a new
// message that sorts after A1 (so every NoSG_i stays first seen in A1).
std::string low_group_overlay(int n) {
    return side_overlay({},
                        "  <message name='VenueLowMsg' msgtype='Z9' msgcat='app'>\n"
                        "   <group name='NoVenueLow' required='N'>\n" +
                            big_groups(0, n) + "   </group>\n  </message>\n",
                        field_def(5, "NoVenueLow", "NUMINGROUP"));
}

TEST(OverlayRefusalW3XmlSideTable, ALowTaggedGroupPushesBaseGroupsPastTheTable) {
    int const n = (65535 - kGroups / 2) / kPerGroup;  // its own count fits; later starts do not
    auto const p = probe(side_table_base(), low_group_overlay(n));
    expect_refused<overlay_limit_error>(p, {"groups side table"}, "groups start");
    expect_origin(p, ov::refusal_origin::side_table, "groups start");
}

TEST(OverlayRefusalW3XmlSideTable, ALowTaggedGroupJustInsideTheTableLoads) {
    int const n = (65535 - kGroups) / kPerGroup;
    ot::expect_loaded_at_prediction(probe(side_table_base(), low_group_overlay(n)),
                                    "groups start control");
}

// ── guard order (N-1) ────────────────────────────────────────────────────────

TEST(OverlayRefusalW3XmlOrder, CycleIsReportedBeforeARedeclareConflict) {
    // The cycle closes through Instrument, and NewOrderSingle redeclares Symbol, which
    // it reaches through Instrument.
    auto const p = probe(base_doc(), overlay_doc(extend_d(fref("Symbol")) + kCycleOverlay));
    expect_refused<xml_parse_error>(p, {"component reference cycle"}, "order: cycle");
}

TEST(OverlayRefusalW3XmlOrder, VersionMismatchIsReportedBeforeTheMessageCap) {
    std::string overlay = new_messages_overlay(ov::kMaxNewMessages + 1);
    overlay.replace(0, kOpen.size(), "<fix type='FIX' major='4' minor='2' servicepack='0'>\n");
    expect_refused<overlay_conflict_error>(probe(base_doc(), overlay), {"version mismatch"},
                                           "order: version");
}

TEST(OverlayRefusalW3XmlOrder, BudgetIsReportedBeforeTheSideTableFit) {
    auto const p = probe(base_doc(), doubling_overlay());
    ASSERT_TRUE(p.guards.has_value()) << ot::describe(p);
    EXPECT_FALSE(p.guards->merged_components_fit) << "the row needs both checks to fail";
    expect_refused<overlay_limit_error>(p, {"expansion budget"}, "order: budget");
    expect_origin(p, ov::refusal_origin::budget, "order: budget");
}

// ── the budget recipe and the longest chain ──────────────────────────────────

// Step 6's total over every vendored QuickFIX-XML base with an empty overlay; the
// budget is the next power of two at or above twice the largest (research R-5).
TEST(OverlayRefusalW3XmlBudget, ConstantEqualsTheRecipeOverTheVendoredBases) {
    std::uint64_t largest = 0;
    for (std::string_view const name : fixpp_test_support::kRuntimeDicts) {
        std::filesystem::path const base = ot::dict_dir() / std::string{name};
        std::string const text = ot::read_file(base);
        pugi::xml_document d;
        ASSERT_TRUE(d.load_buffer(text.data(), text.size()));
        pugi::xml_node const r = d.child("fix");
        ot::temp_file const empty{
            std::string{"<fix type='"} + r.attribute("type").as_string("FIX") + "' major='" +
            r.attribute("major").as_string("") + "' minor='" + r.attribute("minor").as_string("") +
            "' servicepack='" + r.attribute("servicepack").as_string("0") + "'/>\n"};
        auto const p = ov::probe_xml_overlay(base, empty.path(), mr());
        ASSERT_TRUE(p.guards.has_value()) << name << ": " << ot::describe(p);
        std::cout << "[098 budget recipe] " << name << " step-6 total " << p.guards->merged_total
                  << '\n';
        largest = std::max(largest, p.guards->merged_total);
    }
    std::uint64_t const recipe = std::bit_ceil(2 * largest);
    std::cout << "[098 budget recipe] largest " << largest << ", recipe " << recipe << ", constant "
              << ov::kExpansionBudget << '\n';
    EXPECT_EQ(ov::kExpansionBudget, recipe);
}

// The deepest chain the caps and the budget admit on the small base: new components
// linked one to the next through one new group each, the depth cap's nesting spent in
// the first. Its length is chosen through the seam's prediction.
std::string chain_overlay(std::size_t links) {
    std::string comps = " <components>\n";
    std::string fields = " <fields>\n";
    std::size_t const nest = ov::kMaxDepth - 4;  // fix, components, component, ..., field
    comps += "  <component name='VenueK0'>\n";
    for (std::size_t i = 0; i < nest; ++i) {
        comps += "<group name='NoVenueN" + std::to_string(i) + "' required='N'>\n";
        fields +=
            field_def(22000 + static_cast<int>(i), "NoVenueN" + std::to_string(i), "NUMINGROUP");
    }
    comps += "<field name='VenueTag' required='N' />\n";
    for (std::size_t i = 0; i < nest; ++i) {
        comps += "</group>\n";
    }
    comps += "  </component>\n";
    for (std::size_t k = 1; k < links; ++k) {
        comps += "  <component name='VenueK" + std::to_string(k) +
                 "'>\n"
                 "   <group name='NoVenueK" +
                 std::to_string(k) +
                 "' required='N'>\n"
                 "    <component name='VenueK" +
                 std::to_string(k - 1) +
                 "' required='N' />\n"
                 "   </group>\n  </component>\n";
        fields +=
            field_def(23000 + static_cast<int>(k), "NoVenueK" + std::to_string(k), "NUMINGROUP");
    }
    fields += field_def(9102, "VenueTag", "STRING");
    return overlay_doc(comps + " </components>\n" + fields + " </fields>\n");
}

TEST(OverlayRefusalW3XmlBudget, LongestChainLoadsAtThePrediction) {
    std::size_t links = ov::kMaxNewComponents;
    for (; links > 1; --links) {
        auto const p = probe(base_doc(), chain_overlay(links));
        if (p.guards && p.guards->merged_total - p.guards->base_total <= ov::kExpansionBudget) {
            break;
        }
    }
    std::cout << "[098 longest chain] links " << links << '\n';
    ot::expect_loaded_at_prediction(probe(base_doc(), chain_overlay(links)), "longest chain");
}

// ── the cell table ───────────────────────────────────────────────────────────

enum class cell_state : std::uint8_t { row, not_applicable, pending };

struct cell {
    cell_state state;
    std::string_view detail;  // row: the test names; N/A: the reason; pending: the task
};

struct kind {
    std::string_view name;
    cell xml;
    cell orchestra;
};

constexpr cell row(std::string_view tests) { return {.state = cell_state::row, .detail = tests}; }
constexpr cell na(std::string_view reason) {
    return {.state = cell_state::not_applicable, .detail = reason};
}
constexpr cell pending() { return {.state = cell_state::pending, .detail = "tasks T041"}; }

// One entry per refusal kind of contracts W-3 (each R-4 bullet, the level, repeated-key,
// Length+Data and R-5 kinds).
constexpr kind kCells[] = {
    {"identifying attribute changed",
     row("OverlayRefusalW3Xml.IdentifyingAttributeChangedFieldType "
         "OverlayRefusalW3Xml.IdentifyingAttributeChangedFieldName "
         "OverlayRefusalW3Xml.IdentifyingAttributeChangedMessageName"),
     pending()},
    {"field name already used by another number",
     row("OverlayRefusalW3Xml.FieldNameAlreadyUsedByAnotherNumber"), pending()},
    {"code set with a new id and an existing name (N-6)", na("QuickFIX XML has no code sets"),
     pending()},
    {"required relaxed", row("OverlayRefusalW3Xml.RequiredRelaxed"), pending()},
    {"redescribed enum value or code", row("OverlayRefusalW3Xml.RedescribedEnumValue"), pending()},
    {"presence vocabulary (A-013)",
     row("OverlayRefusalW3Xml.PresenceVocabularyLowercaseY "
         "OverlayRefusalW3Xml.PresenceVocabularyFalse"),
     pending()},
    {"duplicate within the overlay", row("OverlayRefusalW3Xml.DuplicateWithinTheOverlay"),
     pending()},
    {"two root containers of one name", row("OverlayRefusalW3Xml.TwoRootContainersOfOneName"),
     pending()},
    {"version mismatch", row("OverlayRefusalW3Xml.VersionMismatch"), pending()},
    {"overlay of the other format", row("OverlayRefusalW3Xml.OverlayOfTheOtherFormat"), pending()},
    {"root child outside data-model A.1's list", row("OverlayRefusalW3Xml.RootChildOutsideTheList"),
     pending()},
    {"group numInGroup id differs, or a new group with none or two (N-9)",
     na("a QuickFIX-XML group's NumInGroup is its name"), pending()},
    {"child redeclared that the parent reaches through a component",
     row("OverlayRefusalW3Xml.ChildRedeclaredThatTheParentReachesThroughAComponent"), pending()},
    {"child redeclared through a two-level component chain (N-2)",
     row("OverlayRefusalW3Xml.ChildRedeclaredThroughATwoLevelComponentChain"), pending()},
    {"new component reference reaching a declared tag (N-7)",
     row("OverlayRefusalW3Xml.NewComponentReferenceReachingADeclaredTag"), pending()},
    {"addition to an inlined component reaching a tag the parent declares (upward closure)",
     row("OverlayRefusalW3Xml.AdditionToAnInlinedComponentReachingATagTheParentDeclares"),
     pending()},
    {"header addition whose tag a message body has",
     row("OverlayRefusalW3Xml.HeaderAdditionWhoseTagAMessageBodyHas"),
     na("contracts W-3 names it for XML; Orchestra's header is component 1024, extended as "
        "any component")},
    {"repeated base key: MsgType",
     row("OverlayRefusalW3Xml.RepeatedBaseMsgTypeNamesBothOccurrences "
         "OverlayRefusalW3Xml.RepeatedKeyBasesLoadWithAnEmptyOverlay"),
     pending()},
    {"repeated base key: field name",
     row("OverlayRefusalW3Xml.RepeatedBaseFieldNameNamesBothOccurrences "
         "OverlayRefusalW3Xml.RepeatedKeyBasesLoadWithAnEmptyOverlay"),
     na("Orchestra matches a field by id, and the loader refuses a repeated field id "
        "(its backing row comes with tasks T041)")},
    {"repeated base key: direct child",
     row("OverlayRefusalW3Xml.RepeatedBaseDirectChildNamesBothOccurrences "
         "OverlayRefusalW3Xml.RepeatedKeyBasesLoadWithAnEmptyOverlay"),
     pending()},
    {"repeated base key: enum value",
     row("OverlayRefusalW3Xml.RepeatedBaseEnumValueNamesBothOccurrences "
         "OverlayRefusalW3Xml.RepeatedKeyBasesLoadWithAnEmptyOverlay"),
     pending()},
    {"repeated base key: field number",
     na("the loader refuses a repeated field number: OverlayRefusalW3Xml."
        "LoaderRefusesARepeatedFieldNumber"),
     na("Orchestra keys fields by id, not number")},
    {"repeated base key: component name",
     na("the loader refuses a repeated component name: OverlayRefusalW3Xml."
        "LoaderRefusesARepeatedComponentName"),
     na("Orchestra keys components by id, not name")},
    {"Length+Data, <fields>, new Data after an unpaired base Length",
     row("OverlayRefusalW3XmlLengthData.FieldsNewDataAfterAnUnpairedBaseLength"),
     na("<fields> adjacency: Orchestra has none")},
    {"Length+Data, <fields>, new Data after a base Length paired earlier",
     na("a Length <fields> pairs is followed by its Data, so it is never last"),
     na("<fields> adjacency: Orchestra has none")},
    {"Length+Data, <fields>, new Data after a base Length paired later",
     row("OverlayRefusalW3XmlLengthData.FieldsNewDataTakesALengthTheBasePairsLater"),
     na("<fields> adjacency: Orchestra has none")},
    {"Length+Data, <fields>, new Length before a base Data",
     na("a redeclared base Data is a match, not an append"),
     na("<fields> adjacency: Orchestra has none")},
    {"Length+Data, strict container, new Data after an unpaired base Length",
     row("OverlayRefusalW3XmlLengthData.ComponentNewDataAfterAnUnpairedBaseLength"),
     na("container adjacency: Orchestra pairs by lengthId")},
    {"Length+Data, strict container, new Data after a base Length paired earlier",
     row("OverlayRefusalW3XmlLengthData.ComponentNewDataAfterALengthTheBasePairsEarlierLoads"),
     na("container adjacency: Orchestra pairs by lengthId")},
    {"Length+Data, strict container, new Data after a base Length paired later",
     row("OverlayRefusalW3XmlLengthData.ComponentNewDataTakesALengthTheBasePairsLater"),
     na("container adjacency: Orchestra pairs by lengthId")},
    {"Length+Data, strict container, new Length before a base Data",
     row("OverlayRefusalW3XmlLengthData.ComponentNewLengthBeforeABaseData"),
     na("container adjacency: Orchestra pairs by lengthId")},
    {"Length+Data, non-strict container with a group between, new Data after an unpaired "
     "base Length",
     row("OverlayRefusalW3XmlLengthData.MessageGroupBetweenNewDataAfterAnUnpairedBaseLength"),
     na("container adjacency: Orchestra pairs by lengthId")},
    {"Length+Data, non-strict container with a group between, new Data after a base Length "
     "paired earlier",
     row("OverlayRefusalW3XmlLengthData.MessageGroupBetweenNewDataAfterALengthPairedEarlierLoads"),
     na("container adjacency: Orchestra pairs by lengthId")},
    {"Length+Data, non-strict container with a group between, new Data after a base Length "
     "paired later",
     row("OverlayRefusalW3XmlLengthData.MessageGroupBetweenNewDataTakesALengthPairedLater"),
     na("container adjacency: Orchestra pairs by lengthId")},
    {"Length+Data, non-strict container with a group between, new Length before a base Data",
     row("OverlayRefusalW3XmlLengthData.MessageGroupBetweenNewLengthBeforeABaseData"),
     na("container adjacency: Orchestra pairs by lengthId")},
    {"Length+Data, non-strict container with an overlay component between, new Data after "
     "an unpaired base Length",
     row("OverlayRefusalW3XmlLengthData.MessageComponentBetweenNewDataAfterAnUnpairedBaseLength"),
     na("container adjacency: Orchestra pairs by lengthId")},
    {"Length+Data, non-strict container with an overlay component between, new Data after a "
     "base Length paired earlier",
     row("OverlayRefusalW3XmlLengthData."
         "MessageComponentBetweenNewDataAfterALengthPairedEarlierLoads"),
     na("container adjacency: Orchestra pairs by lengthId")},
    {"Length+Data, non-strict container with an overlay component between, new Data after a "
     "base Length paired later",
     row("OverlayRefusalW3XmlLengthData.MessageComponentBetweenNewDataTakesALengthPairedLater"),
     na("container adjacency: Orchestra pairs by lengthId")},
    {"Length+Data, non-strict container with an overlay component between, new Length "
     "before a base Data",
     row("OverlayRefusalW3XmlLengthData.MessageComponentBetweenNewLengthBeforeABaseData"),
     na("container adjacency: Orchestra pairs by lengthId")},
    {"Length+Data, lengthId, new Data naming an unpaired base Length",
     na("QuickFIX XML has no lengthId"), pending()},
    {"Length+Data, lengthId, new Data naming a paired base Length",
     na("QuickFIX XML has no lengthId"), pending()},
    {"Length+Data, lengthId, new Length before a base Data", na("QuickFIX XML has no lengthId"),
     na("a base Data's lengthId is fixed by research R-3's equal-lengthId rule")},
    {"byte cap",
     row("OverlayRefusalW3XmlCaps.ByteCapPlusOneStringForm OverlayRefusalW3XmlCaps."
         "ByteCapPlusOnePathForm OverlayRefusalW3XmlCaps.ByteCapAtCapLoadsThroughBothForms"),
     pending()},
    {"depth cap",
     row("OverlayRefusalW3XmlCaps.DepthCapPlusOne OverlayRefusalW3XmlCaps.DepthAtCapLoads"),
     pending()},
    {"new field cap",
     row("OverlayRefusalW3XmlCaps.NewFieldCapPlusOne OverlayRefusalW3XmlCaps.NewFieldAtCapLoads"),
     pending()},
    {"new component cap",
     row("OverlayRefusalW3XmlCaps.NewComponentCapPlusOne "
         "OverlayRefusalW3XmlCaps.NewComponentAtCapLoads"),
     pending()},
    {"new message cap",
     row("OverlayRefusalW3XmlCaps.NewMessageCapPlusOne "
         "OverlayRefusalW3XmlCaps.NewMessageAtCapLoads"),
     pending()},
    {"new Orchestra group cap", na("QuickFIX XML has no group definitions"), pending()},
    {"new Orchestra code set cap", na("QuickFIX XML has no code sets"), pending()},
    {"enum value cap",
     row("OverlayRefusalW3XmlCaps.EnumValueCapPlusOne OverlayRefusalW3XmlCaps.EnumValueAtCapLoads"),
     pending()},
    {"reference cap",
     row("OverlayRefusalW3XmlCaps.ReferenceCapPlusOne OverlayRefusalW3XmlCaps.ReferenceAtCapLoads"),
     pending()},
    {"reference cap, merged-in-place references",
     row("OverlayRefusalW3XmlCaps.MergedInPlaceReferencesCountTowardsTheCap "
         "OverlayRefusalW3XmlCaps.MergedInPlaceMixAtTheCapLoads"),
     pending()},
    {"DOCTYPE", row("OverlayRefusalW3XmlCaps.Doctype"), pending()},
    {"cycle closing through a base component",
     row("OverlayRefusalW3XmlWalk.CycleClosingThroughABaseComponent"), pending()},
    {"unknown reference (pin)", row("OverlayRefusalW3XmlPins.UnknownReferenceIsTheLoadersError"),
     pending()},
    {"unknown field type (pin)", row("OverlayRefusalW3XmlPins.UnknownFieldTypeIsTheLoadersError"),
     pending()},
    {"group-delimiter collision the merge creates (pin)",
     row("OverlayRefusalW3XmlPins.GroupDelimiterCollisionTheMergeCreates"), pending()},
    {"expansion bomb, doubling", row("OverlayRefusalW3XmlWalk.DoublingBombRefusedByTheBudget"),
     pending()},
    {"expansion bomb, two empty chains",
     row("OverlayRefusalW3XmlWalk.TwoEmptyChainsRefusedByTheBudget"), pending()},
    {"header expansion", row("OverlayRefusalW3XmlWalk.HeaderExpansionLoadsAtThePrediction"),
     na("Orchestra's header is component 1024, charged standalone")},
    {"side-table fit",
     row("OverlayRefusalW3XmlSideTable.EarlyComponentsPushALaterBaseStartPastTheTable "
         "OverlayRefusalW3XmlSideTable.EarlyComponentsJustInsideTheTableLoad "
         "OverlayRefusalW3XmlSideTable.ANewComponentWhoseCountPassesTheTable "
         "OverlayRefusalW3XmlSideTable.ANewComponentJustInsideTheTableLoads "
         "OverlayRefusalW3XmlSideTable.ALowTaggedGroupPushesBaseGroupsPastTheTable "
         "OverlayRefusalW3XmlSideTable.ALowTaggedGroupJustInsideTheTableLoads"),
     pending()},
    {"guard order: cycle before redeclare",
     row("OverlayRefusalW3XmlOrder.CycleIsReportedBeforeARedeclareConflict"), pending()},
    {"guard order: version before the entry cap",
     row("OverlayRefusalW3XmlOrder.VersionMismatchIsReportedBeforeTheMessageCap"), pending()},
    {"guard order: budget before the side-table fit",
     row("OverlayRefusalW3XmlOrder.BudgetIsReportedBeforeTheSideTableFit"), pending()},
    {"budget recipe", row("OverlayRefusalW3XmlBudget.ConstantEqualsTheRecipeOverTheVendoredBases"),
     pending()},
    {"longest chain", row("OverlayRefusalW3XmlBudget.LongestChainLoadsAtThePrediction"), pending()},
};

TEST(CellTable, HoldsEveryKindForEveryFormat) {
    std::set<std::string> registered;
    auto const* unit = ::testing::UnitTest::GetInstance();
    for (int i = 0; i < unit->total_test_suite_count(); ++i) {
        auto const* suite = unit->GetTestSuite(i);
        for (int j = 0; j < suite->total_test_count(); ++j) {
            registered.insert(std::string{suite->name()} + "." + suite->GetTestInfo(j)->name());
        }
    }
    std::size_t pending_cells = 0;
    for (kind const& k : kCells) {
        for (auto const& [format, c] :
             {std::pair{"QuickFIX XML", k.xml}, std::pair{"Orchestra", k.orchestra}}) {
            EXPECT_FALSE(c.detail.empty()) << k.name << " / " << format << ": empty cell";
            if (c.state == cell_state::pending) {
                ++pending_cells;
                std::cout << "[098 W-3 cell] pending (" << c.detail << "): " << k.name << " / "
                          << format << '\n';
            } else if (c.state == cell_state::not_applicable) {
                std::cout << "[098 W-3 cell] N/A: " << k.name << " / " << format << ": " << c.detail
                          << '\n';
            } else {
                std::string_view rest = c.detail;
                while (!rest.empty()) {
                    std::size_t const sp = rest.find(' ');
                    std::string const name{rest.substr(0, sp)};
                    EXPECT_TRUE(registered.contains(name))
                        << k.name << " / " << format << ": row '" << name << "' is not a test";
                    rest = sp == std::string_view::npos ? std::string_view{} : rest.substr(sp + 1);
                }
            }
        }
    }
    std::cout << "[098 W-3 cell] pending cells, which tasks T041 replaces: " << pending_cells
              << '\n';
}

}  // namespace
