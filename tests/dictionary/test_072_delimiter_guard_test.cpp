// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/test_072_delimiter_guard_test.cpp — 072-nested-group-hardening [US1]
//
// Load-time nested==parent delimiter guard witness (FR-003 / SC-002) + the
// FR-004 asymmetry negative witness. A dialect XML whose nested group's
// delimiter (`first_field_tag`) equals its immediate parent group's delimiter
// feeds `OffsetTable::group_slices()`'s flat splitter a layout it mis-splits,
// silently corrupting the outer group's instance boundaries. This is a public
// API (dialect-XML load) reaching a silent wire-corruption bug, so the loader
// MUST reject it. The guard is delimiter-only by design (FR-004): a shared
// parent/child SCALAR member (L-062-3) is NOT load-enforced and must still load.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <fixpp/dict/error.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <memory_resource>
#include <string_view>

namespace {

// A minimal dialect: outer group NoOuter(100) delimiter SharedDelim(150); nested
// group NoInner(200) delimiter ALSO SharedDelim(150) — the collision the guard
// rejects.
constexpr std::string_view kCollidingXml =
    R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
    R"(<fields>)"
    R"(<field number='35' name='MsgType' type='STRING'/>)"
    R"(<field number='100' name='NoOuter' type='NUMINGROUP'/>)"
    R"(<field number='150' name='SharedDelim' type='INT'/>)"
    R"(<field number='200' name='NoInner' type='NUMINGROUP'/>)"
    R"(<field number='250' name='InnerData' type='STRING'/>)"
    R"(</fields>)"
    R"(<messages><message name='Bad' msgtype='U' msgcat='app'>)"
    R"(<field name='MsgType' required='N'/>)"
    R"(<group name='NoOuter' required='N'>)"
    R"(<field name='SharedDelim' required='N'/>)"      // outer delimiter = 150
    R"(<group name='NoInner' required='N'>)"
    R"(<field name='SharedDelim' required='N'/>)"      // inner delimiter = 150 == outer -> reject
    R"(<field name='InnerData' required='N'/>)"
    R"(</group></group></message></messages></fix>)";

// The conforming twin: inner group leads with a DISTINCT delimiter (250).
constexpr std::string_view kConformingXml =
    R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
    R"(<fields>)"
    R"(<field number='35' name='MsgType' type='STRING'/>)"
    R"(<field number='100' name='NoOuter' type='NUMINGROUP'/>)"
    R"(<field number='150' name='SharedDelim' type='INT'/>)"
    R"(<field number='200' name='NoInner' type='NUMINGROUP'/>)"
    R"(<field number='250' name='InnerData' type='STRING'/>)"
    R"(</fields>)"
    R"(<messages><message name='Ok' msgtype='U' msgcat='app'>)"
    R"(<field name='MsgType' required='N'/>)"
    R"(<group name='NoOuter' required='N'>)"
    R"(<field name='SharedDelim' required='N'/>)"      // outer delimiter = 150
    R"(<group name='NoInner' required='N'>)"
    R"(<field name='InnerData' required='N'/>)"        // inner delimiter = 250 != 150 -> loads
    R"(<field name='SharedDelim' required='N'/>)"
    R"(</group></group></message></messages></fix>)";

// FR-004 asymmetry: outer/inner delimiters are DISJOINT (150 vs 250) but both
// carry the SAME scalar member SharedScalar(300). The guard enforces the
// delimiter convention ONLY, so this dialect MUST load (the scalar collision is
// census-pinned, not load-enforced — L-062-3 / FR-004).
constexpr std::string_view kSharedScalarDisjointDelimXml =
    R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
    R"(<fields>)"
    R"(<field number='35' name='MsgType' type='STRING'/>)"
    R"(<field number='100' name='NoOuter' type='NUMINGROUP'/>)"
    R"(<field number='150' name='OuterDelim' type='INT'/>)"
    R"(<field number='300' name='SharedScalar' type='STRING'/>)"
    R"(<field number='200' name='NoInner' type='NUMINGROUP'/>)"
    R"(<field number='250' name='InnerDelim' type='STRING'/>)"
    R"(</fields>)"
    R"(<messages><message name='Ok' msgtype='U' msgcat='app'>)"
    R"(<field name='MsgType' required='N'/>)"
    R"(<group name='NoOuter' required='N'>)"
    R"(<field name='OuterDelim' required='N'/>)"       // outer delimiter = 150
    R"(<field name='SharedScalar' required='N'/>)"     // 300 in parent
    R"(<group name='NoInner' required='N'>)"
    R"(<field name='InnerDelim' required='N'/>)"        // inner delimiter = 250 (disjoint)
    R"(<field name='SharedScalar' required='N'/>)"     // 300 in child — shared scalar, NOT enforced
    R"(</group></group></message></messages></fix>)";

std::pmr::monotonic_buffer_resource make_arena(std::array<std::byte, (1UZ << 20)>& buf) {
    return std::pmr::monotonic_buffer_resource{buf.data(), buf.size()};
}

}  // namespace

// SC-002: a colliding dialect is rejected at load with the specific typed error,
// producing no usable view and not crashing.
TEST(Delimiter072Guard, CollidingDialectThrowsSpecificError) {
    std::array<std::byte, (1UZ << 20)> buf{};
    auto mr = make_arena(buf);
    EXPECT_THROW((void)fixpp::dict::XmlLoader{}.load_from_string(kCollidingXml, &mr),
                 fixpp::dict::group_delimiter_collision_error);
}

// The specific error is catchable as the base `dict::xml_parse_error` (existing
// bad-dictionary handlers still catch it — the derivation contract, D-A1).
TEST(Delimiter072Guard, CollidingDialectCatchableAsXmlParseError) {
    std::array<std::byte, (1UZ << 20)> buf{};
    auto mr = make_arena(buf);
    bool threw_as_base = false;
    try {
        (void)fixpp::dict::XmlLoader{}.load_from_string(kCollidingXml, &mr);
    } catch (fixpp::dict::xml_parse_error const& e) {
        threw_as_base = true;
        EXPECT_NE(std::string_view{e.what()}.find("group_delimiter_collision"), std::string_view::npos);
    }
    EXPECT_TRUE(threw_as_base) << "guard error must be catchable as dict::xml_parse_error";
}

// The conforming twin (distinct delimiters) loads normally — the guard is a
// no-op for a conforming dictionary.
TEST(Delimiter072Guard, ConformingDialectLoads) {
    std::array<std::byte, (1UZ << 20)> buf{};
    auto mr = make_arena(buf);
    auto dict = fixpp::dict::XmlLoader{}.load_from_string(kConformingXml, &mr);
    EXPECT_FALSE(dict.messages().empty());
}

// FR-004 asymmetry: a shared parent/child SCALAR member with DISJOINT delimiters
// loads successfully — the load guard enforces the delimiter convention ONLY.
TEST(Delimiter072Guard, SharedScalarWithDisjointDelimitersLoads) {
    std::array<std::byte, (1UZ << 20)> buf{};
    auto mr = make_arena(buf);
    auto dict = fixpp::dict::XmlLoader{}.load_from_string(kSharedScalarDisjointDelimXml, &mr);
    EXPECT_FALSE(dict.messages().empty())
        << "FR-004: a scalar-member collision with disjoint delimiters must NOT be load-rejected";
}
