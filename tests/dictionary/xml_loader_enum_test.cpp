// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/xml_loader_enum_test.cpp
//
// 075-live-wire-enum-validation T012/T013 (FR-001/FR-017).
//
// The shipped QuickFIX dictionaries measure ZERO duplicate codes, ZERO
// missing `enum` attributes, and ZERO missing `description` attributes
// (research.md R-5 — "Measured 2026-07-14"), so T013's three strictness
// rules are UNTESTABLE against real dictionary data and must be driven with
// synthetic XML, or they ship unwitnessed.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/error.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <memory_resource>
#include <string_view>

namespace {

constexpr std::size_t kArenaSize = 64u * 1024u;

struct Arena {
    std::array<std::byte, kArenaSize> buf{};
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size(),
                                           std::pmr::null_memory_resource()};
};

}  // namespace

// ---------------------------------------------------------------------------
// T013 rule 1 — a duplicate `<value enum="X">` on one field is DEDUPED
// (union semantics), NOT a load failure. QuickFIX's `std::set<std::string>`
// membership is idempotent on a re-add (research.md R-5).
//
// Mutation: replace the dedupe `seen_codes.insert(...).second` guard in
// xml_loader.cpp with an unconditional push_back — this test's size-2
// assertion MUST go RED (enum_values(1).size() becomes 3).
// ---------------------------------------------------------------------------
TEST(XmlLoaderEnum, DuplicateEnumValueIsDedupedNotAnError) {
    Arena a;
    fixpp::dict::XmlLoader loader;
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields>)"
        R"(<field number='1' name='Account' type='STRING'>)"
        R"(<value enum='X' description='First'/>)"
        R"(<value enum='Y' description='Second'/>)"
        R"(<value enum='X' description='DuplicateIgnored'/>)"
        R"(</field>)"
        R"(</fields><messages/></fix>)";

    auto d = loader.load_from_string(kXml, &a.mr);

    auto const codes = d.enum_values(std::uint16_t{1});
    ASSERT_EQ(codes.size(), 2u) << "duplicate <value enum='X'> must be deduped, not appended";
    EXPECT_EQ(codes[0].value, "X");
    EXPECT_EQ(codes[0].description, "First")
        << "first occurrence's description must win over the duplicate's";
    EXPECT_EQ(codes[1].value, "Y");
    EXPECT_EQ(codes[1].description, "Second");
}

// ---------------------------------------------------------------------------
// T013 rule 2 — a `<value>` missing its `enum` attribute is a LOAD FAILURE
// (fail-closed, mirroring QuickFIX's ConfigError, DataDictionary.cpp:271-273).
//
// Mutation: remove the `if (!enum_attr) throw ...` guard — this test's
// EXPECT_THROW MUST go RED (load succeeds instead).
// ---------------------------------------------------------------------------
TEST(XmlLoaderEnum, ValueMissingEnumAttributeThrowsXmlParseError) {
    Arena a;
    fixpp::dict::XmlLoader loader;
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields>)"
        R"(<field number='1' name='Account' type='STRING'>)"
        R"(<value description='NoEnumAttribute'/>)"
        R"(</field>)"
        R"(</fields><messages/></fix>)";

    try {
        (void)loader.load_from_string(kXml, &a.mr);
        FAIL() << "expected dict::xml_parse_error";
    } catch (fixpp::dict::xml_parse_error const& e) {
        EXPECT_EQ(e.code(), fixpp::core::error::dict_xml_parse_failed);
    }
}

// ---------------------------------------------------------------------------
// T013 rule 3 — a `<value>` missing its `description` attribute is LEGAL and
// yields an EMPTY description view; it MUST NOT fail the load (description is
// diagnostics-only, QuickFIX reads it conditionally, DataDictionary.cpp:276).
//
// Mutation: make a missing `description` attribute throw (or default it to
// something non-empty) — either flavor turns this test RED.
// ---------------------------------------------------------------------------
TEST(XmlLoaderEnum, ValueMissingDescriptionIsLegalAndEmpty) {
    Arena a;
    fixpp::dict::XmlLoader loader;
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields>)"
        R"(<field number='1' name='Account' type='STRING'>)"
        R"(<value enum='X'/>)"
        R"(</field>)"
        R"(</fields><messages/></fix>)";

    auto d = loader.load_from_string(kXml, &a.mr);

    auto const codes = d.enum_values(std::uint16_t{1});
    ASSERT_EQ(codes.size(), 1u);
    EXPECT_EQ(codes[0].value, "X");
    EXPECT_TRUE(codes[0].description.empty())
        << "missing description must yield an empty view, not fail the load";
}

// ---------------------------------------------------------------------------
// T012 — a field with no <value> children at all carries an empty codeset
// (the FR-003 absent-codeset floor rests on this at the table_view layer;
// here it is the loader-level precondition: no run is emitted for tag 1).
// ---------------------------------------------------------------------------
TEST(XmlLoaderEnum, FieldWithNoValueChildrenHasEmptyEnumValues) {
    Arena a;
    fixpp::dict::XmlLoader loader;
    constexpr std::string_view kXml = R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
                                      R"(<fields>)"
                                      R"(<field number='1' name='Account' type='STRING'/>)"
                                      R"(</fields><messages/></fix>)";

    auto d = loader.load_from_string(kXml, &a.mr);

    EXPECT_TRUE(d.enum_values(std::uint16_t{1}).empty());
}
