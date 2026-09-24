// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/xml_loader_component_group_pairs_test.cpp — 091 (fixpp#418), C-2.5a
//
// `XmlLoader::detect_length_pairs` over a user-loaded dictionary: a LENGTH/DATA
// pair that is adjacent only inside a `<component>` definition or a `<group>`
// (at any depth, whatever its parent) is paired; a non-`<field>` child between
// the two breaks adjacency; conflicts are settled by the visit order
// `<fields>`, header/trailer/messages, component definitions, then groups, with
// the first writer winning (specs/091-data-field-bytes/contracts/codegen-builders.md
// C-2.5a, research.md R-11).
//
// Every arm loads its own document with its own tags, so the first-writer rule
// cannot leak between arms. In every arm the two fields are NOT adjacent in
// `<fields>` order (a filler field sits between them) and are NOT consecutive
// among the `<field>` children of any message, header or trailer with
// non-field children skipped: they never appear as direct `<field>` children
// of those containers at all. Pair-ness therefore comes only from the
// component/group containers each arm names.

#include <gtest/gtest.h>

#include <cstdint>
#include <exception>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>

namespace {

using fixpp::dict::Dictionary;

constexpr std::string_view kOpen = "<fix type='FIX' major='4' minor='4' servicepack='0'>";
constexpr std::string_view kClose = "</fix>";

// Fillers shared by every arm's <fields> block (each arm is its own document).
constexpr std::string_view kFillerFields =
    "<field number='11' name='ClOrdID' type='STRING'/>"
    "<field number='35' name='MsgType' type='STRING'/>"
    "<field number='58' name='Text' type='STRING'/>";

std::string doc(std::initializer_list<std::string_view> parts) {
    std::string s{kOpen};
    for (auto const p : parts) {
        s += p;
    }
    s += kClose;
    return s;
}

// Loads `xml`; on a loader exception records a failure naming it and returns
// nullopt, so an arm can never read a load failure as "not paired".
std::optional<Dictionary> load(std::string const& xml, std::pmr::memory_resource* mr) {
    try {
        return fixpp::dict::XmlLoader{}.load_from_string(xml, mr);
    } catch (std::exception const& e) {
        ADD_FAILURE() << "loader threw: " << e.what();
        return std::nullopt;
    }
}

}  // namespace

// (i) adjacent only inside a <component> definition.
TEST(XmlLoaderComponentGroupPairs, I_AdjacentInsideComponentIsPaired) {
    std::string const xml = doc({
        "<fields>",
        kFillerFields,
        "<field number='5101' name='LenI' type='LENGTH'/>",
        "<field number='5190' name='FillI' type='STRING'/>",
        "<field number='5102' name='DataI' type='DATA'/>",
        "</fields>",
        "<components>"
        "<component name='CI'><field name='LenI' required='N'/><field name='DataI' required='N'/>"
        "</component>"
        "</components>",
        "<messages><message name='MI' msgtype='D' msgcat='app'>"
        "<field name='ClOrdID' required='N'/><component name='CI' required='N'/>"
        "</message></messages>",
    });
    std::pmr::monotonic_buffer_resource mr;
    auto const d = load(xml, &mr);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->length_pair_data_tag(5101), 5102U);
    EXPECT_TRUE(d->as_table_view().has_nonstandard_pair());
}

// (ii) adjacent only inside a <group> nested in a component.
TEST(XmlLoaderComponentGroupPairs, II_AdjacentInsideGroupInComponentIsPaired) {
    std::string const xml = doc({
        "<fields>",
        kFillerFields,
        "<field number='5201' name='LenII' type='LENGTH'/>",
        "<field number='5290' name='FillII' type='STRING'/>",
        "<field number='5202' name='DataII' type='DATA'/>",
        "<field number='6201' name='NoII' type='NUMINGROUP'/>",
        "</fields>",
        "<components>"
        "<component name='CII'><group name='NoII' required='N'>"
        "<field name='ClOrdID' required='N'/><field name='LenII' required='N'/>"
        "<field name='DataII' required='N'/></group></component>"
        "</components>",
        "<messages><message name='MII' msgtype='D' msgcat='app'>"
        "<field name='Text' required='N'/><component name='CII' required='N'/>"
        "</message></messages>",
    });
    std::pmr::monotonic_buffer_resource mr;
    auto const d = load(xml, &mr);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->length_pair_data_tag(5201), 5202U);
}

// (iii) a <component> reference between the two fields breaks adjacency.
TEST(XmlLoaderComponentGroupPairs, III_ComponentReferenceBetweenBreaksAdjacency) {
    std::string const xml = doc({
        "<fields>",
        kFillerFields,
        "<field number='5301' name='LenIII' type='LENGTH'/>",
        "<field number='5390' name='FillIII' type='STRING'/>",
        "<field number='5302' name='DataIII' type='DATA'/>",
        "</fields>",
        "<components>"
        "<component name='Between'><field name='FillIII' required='N'/></component>"
        "<component name='CIII'><field name='LenIII' required='N'/>"
        "<component name='Between' required='N'/>"
        "<field name='DataIII' required='N'/></component>"
        "</components>",
        "<messages><message name='MIII' msgtype='D' msgcat='app'>"
        "<field name='ClOrdID' required='N'/><component name='CIII' required='N'/>"
        "</message></messages>",
    });
    std::pmr::monotonic_buffer_resource mr;
    auto const d = load(xml, &mr);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->length_pair_data_tag(5301), 0U);
}

// (iv) a Length adjacent to Data A in one component and to Data B in a later
// one: the first writer (document order) wins.
TEST(XmlLoaderComponentGroupPairs, IV_FirstComponentInDocumentOrderWins) {
    std::string const xml = doc({
        "<fields>",
        kFillerFields,
        "<field number='5401' name='LenIV' type='LENGTH'/>",
        "<field number='5490' name='FillIV' type='STRING'/>",
        "<field number='5402' name='DataIVA' type='DATA'/>",
        "<field number='5491' name='FillIV2' type='STRING'/>",
        "<field number='5403' name='DataIVB' type='DATA'/>",
        "</fields>",
        "<components>"
        "<component name='CIVA'><field name='LenIV' required='N'/>"
        "<field name='DataIVA' required='N'/></component>"
        "<component name='CIVB'><field name='LenIV' required='N'/>"
        "<field name='DataIVB' required='N'/></component>"
        "</components>",
        "<messages>"
        "<message name='MIVA' msgtype='D' msgcat='app'>"
        "<field name='ClOrdID' required='N'/><component name='CIVA' required='N'/></message>"
        "<message name='MIVB' msgtype='E' msgcat='app'>"
        "<field name='ClOrdID' required='N'/><component name='CIVB' required='N'/></message>"
        "</messages>",
    });
    std::pmr::monotonic_buffer_resource mr;
    auto const d = load(xml, &mr);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->length_pair_data_tag(5401), 5402U);
}

// (v) adjacent only inside a <group> that is a direct child of a <message>.
TEST(XmlLoaderComponentGroupPairs, V_AdjacentInsideGroupDirectlyInMessageIsPaired) {
    std::string const xml = doc({
        "<fields>",
        kFillerFields,
        "<field number='5501' name='LenV' type='LENGTH'/>",
        "<field number='5590' name='FillV' type='STRING'/>",
        "<field number='5502' name='DataV' type='DATA'/>",
        "<field number='6501' name='NoV' type='NUMINGROUP'/>",
        "</fields>",
        "<components/>",
        "<messages><message name='MV' msgtype='D' msgcat='app'>"
        "<field name='Text' required='N'/>"
        "<group name='NoV' required='N'><field name='ClOrdID' required='N'/>"
        "<field name='LenV' required='N'/><field name='DataV' required='N'/></group>"
        "</message></messages>",
    });
    std::pmr::monotonic_buffer_resource mr;
    auto const d = load(xml, &mr);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->length_pair_data_tag(5501), 5502U);
}

// (vi) adjacent only inside a <group> nested directly in another <group>, the
// outer group inside a component.
TEST(XmlLoaderComponentGroupPairs, VI_AdjacentInsideGroupInGroupInComponentIsPaired) {
    std::string const xml = doc({
        "<fields>",
        kFillerFields,
        "<field number='5601' name='LenVI' type='LENGTH'/>",
        "<field number='5690' name='FillVI' type='STRING'/>",
        "<field number='5602' name='DataVI' type='DATA'/>",
        "<field number='6601' name='NoOuterVI' type='NUMINGROUP'/>",
        "<field number='6602' name='NoInnerVI' type='NUMINGROUP'/>",
        "</fields>",
        "<components>"
        "<component name='CVI'><group name='NoOuterVI' required='N'>"
        "<field name='ClOrdID' required='N'/>"
        "<group name='NoInnerVI' required='N'><field name='Text' required='N'/>"
        "<field name='LenVI' required='N'/><field name='DataVI' required='N'/></group>"
        "</group></component>"
        "</components>",
        "<messages><message name='MVI' msgtype='D' msgcat='app'>"
        "<field name='FillVI' required='N'/><component name='CVI' required='N'/>"
        "</message></messages>",
    });
    std::pmr::monotonic_buffer_resource mr;
    auto const d = load(xml, &mr);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->length_pair_data_tag(5601), 5602U);
}

// (vii)a visit order: a Length adjacent to Data A inside a <group> that is a
// direct child of a <message>, and to Data B as direct fields of a component
// that follows the group in document order. Component definitions are visited
// before any group, so B wins.
TEST(XmlLoaderComponentGroupPairs, VIIa_ComponentBeatsEarlierGroupInMessage) {
    std::string const xml = doc({
        "<fields>",
        kFillerFields,
        "<field number='5701' name='LenVIIa' type='LENGTH'/>",
        "<field number='5790' name='FillVIIa' type='STRING'/>",
        "<field number='5702' name='DataVIIaA' type='DATA'/>",
        "<field number='5791' name='FillVIIa2' type='STRING'/>",
        "<field number='5703' name='DataVIIaB' type='DATA'/>",
        "<field number='6701' name='NoVIIa' type='NUMINGROUP'/>",
        "</fields>",
        // <messages> precedes <components>: the group comes first in document order.
        "<messages>"
        "<message name='MVIIa1' msgtype='D' msgcat='app'>"
        "<field name='Text' required='N'/>"
        "<group name='NoVIIa' required='N'><field name='ClOrdID' required='N'/>"
        "<field name='LenVIIa' required='N'/><field name='DataVIIaA' required='N'/></group>"
        "</message>"
        "<message name='MVIIa2' msgtype='E' msgcat='app'>"
        "<field name='ClOrdID' required='N'/><component name='CVIIa' required='N'/></message>"
        "</messages>",
        "<components>"
        "<component name='CVIIa'><field name='LenVIIa' required='N'/>"
        "<field name='DataVIIaB' required='N'/></component>"
        "</components>",
    });
    std::pmr::monotonic_buffer_resource mr;
    auto const d = load(xml, &mr);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->length_pair_data_tag(5701), 5703U);
}

// (vii)b: the group sits in an earlier component C1, Data B in a later
// component C2.
TEST(XmlLoaderComponentGroupPairs, VIIb_LaterComponentBeatsGroupInEarlierComponent) {
    std::string const xml = doc({
        "<fields>",
        kFillerFields,
        "<field number='5801' name='LenVIIb' type='LENGTH'/>",
        "<field number='5890' name='FillVIIb' type='STRING'/>",
        "<field number='5802' name='DataVIIbA' type='DATA'/>",
        "<field number='5891' name='FillVIIb2' type='STRING'/>",
        "<field number='5803' name='DataVIIbB' type='DATA'/>",
        "<field number='6801' name='NoVIIb' type='NUMINGROUP'/>",
        "</fields>",
        "<components>"
        "<component name='C1VIIb'><group name='NoVIIb' required='N'>"
        "<field name='ClOrdID' required='N'/><field name='LenVIIb' required='N'/>"
        "<field name='DataVIIbA' required='N'/></group></component>"
        "<component name='C2VIIb'><field name='LenVIIb' required='N'/>"
        "<field name='DataVIIbB' required='N'/></component>"
        "</components>",
        "<messages>"
        "<message name='MVIIb1' msgtype='D' msgcat='app'>"
        "<field name='Text' required='N'/><component name='C1VIIb' required='N'/></message>"
        "<message name='MVIIb2' msgtype='E' msgcat='app'>"
        "<field name='ClOrdID' required='N'/><component name='C2VIIb' required='N'/></message>"
        "</messages>",
    });
    std::pmr::monotonic_buffer_resource mr;
    auto const d = load(xml, &mr);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->length_pair_data_tag(5801), 5803U);
}

// (vii)c: the group is a direct child of <header>; Data B sits as direct
// fields of a component that follows it in document order.
TEST(XmlLoaderComponentGroupPairs, VIIc_ComponentBeatsGroupInHeader) {
    std::string const xml = doc({
        "<header><field name='MsgType' required='N'/>"
        "<group name='NoVIIc' required='N'><field name='ClOrdID' required='N'/>"
        "<field name='LenVIIc' required='N'/><field name='DataVIIcA' required='N'/></group>"
        "</header>",
        "<trailer/>",
        "<messages><message name='MVIIc' msgtype='D' msgcat='app'>"
        "<field name='Text' required='N'/><component name='CVIIc' required='N'/>"
        "</message></messages>",
        "<components>"
        "<component name='CVIIc'><field name='LenVIIc' required='N'/>"
        "<field name='DataVIIcB' required='N'/></component>"
        "</components>",
        "<fields>",
        kFillerFields,
        "<field number='5901' name='LenVIIc' type='LENGTH'/>",
        "<field number='5990' name='FillVIIc' type='STRING'/>",
        "<field number='5902' name='DataVIIcA' type='DATA'/>",
        "<field number='5991' name='FillVIIc2' type='STRING'/>",
        "<field number='5903' name='DataVIIcB' type='DATA'/>",
        "<field number='6901' name='NoVIIc' type='NUMINGROUP'/>",
        "</fields>",
    });
    std::pmr::monotonic_buffer_resource mr;
    auto const d = load(xml, &mr);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->length_pair_data_tag(5901), 5903U);
}

// (viii)a adjacent only inside a <group> that is a direct child of <header>.
TEST(XmlLoaderComponentGroupPairs, VIIIa_AdjacentInsideGroupInHeaderIsPaired) {
    std::string const xml = doc({
        "<header><field name='MsgType' required='N'/>"
        "<group name='NoVIIIa' required='N'><field name='ClOrdID' required='N'/>"
        "<field name='LenVIIIa' required='N'/><field name='DataVIIIa' required='N'/></group>"
        "</header>",
        "<trailer/>",
        "<messages><message name='MVIIIa' msgtype='D' msgcat='app'>"
        "<field name='Text' required='N'/></message></messages>",
        "<components/>",
        "<fields>",
        kFillerFields,
        "<field number='6011' name='LenVIIIa' type='LENGTH'/>",
        "<field number='6090' name='FillVIIIa' type='STRING'/>",
        "<field number='6012' name='DataVIIIa' type='DATA'/>",
        "<field number='7011' name='NoVIIIa' type='NUMINGROUP'/>",
        "</fields>",
    });
    std::pmr::monotonic_buffer_resource mr;
    auto const d = load(xml, &mr);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->length_pair_data_tag(6011), 6012U);
}

// (viii)b adjacent only inside a <group> that is a direct child of <trailer>.
TEST(XmlLoaderComponentGroupPairs, VIIIb_AdjacentInsideGroupInTrailerIsPaired) {
    std::string const xml = doc({
        "<header><field name='MsgType' required='N'/></header>",
        "<trailer>"
        "<group name='NoVIIIb' required='N'><field name='ClOrdID' required='N'/>"
        "<field name='LenVIIIb' required='N'/><field name='DataVIIIb' required='N'/></group>"
        "</trailer>",
        "<messages><message name='MVIIIb' msgtype='D' msgcat='app'>"
        "<field name='Text' required='N'/></message></messages>",
        "<components/>",
        "<fields>",
        kFillerFields,
        "<field number='6021' name='LenVIIIb' type='LENGTH'/>",
        "<field number='6091' name='FillVIIIb' type='STRING'/>",
        "<field number='6022' name='DataVIIIb' type='DATA'/>",
        "<field number='7021' name='NoVIIIb' type='NUMINGROUP'/>",
        "</fields>",
    });
    std::pmr::monotonic_buffer_resource mr;
    auto const d = load(xml, &mr);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->length_pair_data_tag(6021), 6022U);
}
