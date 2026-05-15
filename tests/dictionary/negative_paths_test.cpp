// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/negative_paths_test.cpp
//
// seam #7 — AC-L2..L8 / AC-L10
// Negative-path tests for fixpp::dict::XmlLoader::load and ::load_from_string.
// Each test drives one malformed or semantically-broken XML through
// load_from_string (or load for the filesystem path case) and asserts the
// correct typed exception with the matching fixpp::core::error code.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory_resource>

#include <fixpp/core/error.hpp>
#include <fixpp/dict/error.hpp>
#include <fixpp/dict/xml_loader.hpp>

// ---------------------------------------------------------------------------
// Helper: 256 KiB arena + monotonic_buffer_resource.
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kArenaSize = 256u * 1024u;

struct Arena {
    std::array<std::byte, kArenaSize> buf{};
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size(),
                                           std::pmr::null_memory_resource()};
};

}  // namespace

// ---------------------------------------------------------------------------
// AC-L2 — NonexistentPathThrowsXmlParseError
//
// load() with a path that does not exist must throw dict::xml_parse_error
// and report code() == dict_xml_parse_failed.
// ---------------------------------------------------------------------------
TEST(NegativePaths, AC_L2_NonexistentPathThrowsXmlParseError) {
    Arena a;
    fixpp::dict::XmlLoader loader;
    try {
        (void)loader.load("/nonexistent/path/FIX44.xml", &a.mr);
        FAIL() << "expected dict::xml_parse_error";
    } catch (fixpp::dict::xml_parse_error const& e) {
        EXPECT_EQ(e.code(), fixpp::core::error::dict_xml_parse_failed);
    }
}

// ---------------------------------------------------------------------------
// AC-L3 — MalformedXmlThrowsXmlParseError
//
// An unclosed tag produces a pugixml parse failure that the loader
// translates to dict::xml_parse_error.
// ---------------------------------------------------------------------------
TEST(NegativePaths, AC_L3_MalformedXmlThrowsXmlParseError) {
    Arena a;
    fixpp::dict::XmlLoader loader;
    constexpr std::string_view kBadXml = R"(<fix><unclosed)";
    try {
        (void)loader.load_from_string(kBadXml, &a.mr);
        FAIL() << "expected dict::xml_parse_error";
    } catch (fixpp::dict::xml_parse_error const& e) {
        EXPECT_EQ(e.code(), fixpp::core::error::dict_xml_parse_failed);
    }
}

// ---------------------------------------------------------------------------
// AC-L4 — UnknownFixVersionThrowsUnknownVersionError
//
// FIX major="6" minor="0" is outside the nine v1.0-supported versions;
// the loader must throw dict::unknown_version_error.
// ---------------------------------------------------------------------------
TEST(NegativePaths, AC_L4_UnknownFixVersionThrowsUnknownVersionError) {
    Arena a;
    fixpp::dict::XmlLoader loader;
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='6' minor='0' servicepack='0'>)"
        R"(<fields/><messages/></fix>)";
    try {
        (void)loader.load_from_string(kXml, &a.mr);
        FAIL() << "expected dict::unknown_version_error";
    } catch (fixpp::dict::unknown_version_error const& e) {
        EXPECT_EQ(e.code(), fixpp::core::error::dict_unknown_version);
    }
}

// ---------------------------------------------------------------------------
// AC-L5 — MissingFieldNumberThrowsXmlParseError
//
// A <field> element without a number attribute must throw dict::xml_parse_error.
// ---------------------------------------------------------------------------
TEST(NegativePaths, AC_L5_MissingFieldNumberThrowsXmlParseError) {
    Arena a;
    fixpp::dict::XmlLoader loader;
    // number attribute intentionally absent.
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields>)"
        R"(<field name='Foo' type='STRING'/>)"
        R"(</fields><messages/></fix>)";
    try {
        (void)loader.load_from_string(kXml, &a.mr);
        FAIL() << "expected dict::xml_parse_error";
    } catch (fixpp::dict::xml_parse_error const& e) {
        EXPECT_EQ(e.code(), fixpp::core::error::dict_xml_parse_failed);
    }
}

// ---------------------------------------------------------------------------
// AC-L5b — NonNumericFieldNumberThrowsXmlParseError
//
// A <field number='abc'> (non-numeric value) must throw dict::xml_parse_error.
// ---------------------------------------------------------------------------
TEST(NegativePaths, AC_L5b_NonNumericFieldNumberThrowsXmlParseError) {
    Arena a;
    fixpp::dict::XmlLoader loader;
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields>)"
        R"(<field number='abc' name='Foo' type='STRING'/>)"
        R"(</fields><messages/></fix>)";
    try {
        (void)loader.load_from_string(kXml, &a.mr);
        FAIL() << "expected dict::xml_parse_error";
    } catch (fixpp::dict::xml_parse_error const& e) {
        EXPECT_EQ(e.code(), fixpp::core::error::dict_xml_parse_failed);
    }
}

// ---------------------------------------------------------------------------
// AC-L6 — DuplicateFieldNumberThrowsXmlParseError
//
// Two <field> rows sharing number='1' must throw dict::xml_parse_error.
// ---------------------------------------------------------------------------
TEST(NegativePaths, AC_L6_DuplicateFieldNumberThrowsXmlParseError) {
    Arena a;
    fixpp::dict::XmlLoader loader;
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields>)"
        R"(<field number='1' name='Account' type='STRING'/>)"
        R"(<field number='1' name='AccountDup' type='STRING'/>)"
        R"(</fields><messages/></fix>)";
    try {
        (void)loader.load_from_string(kXml, &a.mr);
        FAIL() << "expected dict::xml_parse_error";
    } catch (fixpp::dict::xml_parse_error const& e) {
        EXPECT_EQ(e.code(), fixpp::core::error::dict_xml_parse_failed);
    }
}

// ---------------------------------------------------------------------------
// AC-L7 — DanglingComponentReferenceThrowsXmlParseError
//
// A <message> that references <component name='NotDeclared'> without a
// matching declaration in <components> must throw dict::xml_parse_error.
// ---------------------------------------------------------------------------
TEST(NegativePaths, AC_L7_DanglingComponentReferenceThrowsXmlParseError) {
    Arena a;
    fixpp::dict::XmlLoader loader;
    // The component "Instrument" is referenced inside the message body but
    // has no corresponding <component name='Instrument'> declaration.
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields>)"
        R"(<field number='35' name='MsgType' type='STRING'/>)"
        R"(</fields>)"
        R"(<components/>)"
        R"(<messages>)"
        R"(<message name='NewOrderSingle' msgtype='D' msgcat='app'>)"
        R"(<component name='NotDeclared' required='Y'/>)"
        R"(</message>)"
        R"(</messages>)"
        R"(</fix>)";
    try {
        (void)loader.load_from_string(kXml, &a.mr);
        FAIL() << "expected dict::xml_parse_error";
    } catch (fixpp::dict::xml_parse_error const& e) {
        EXPECT_EQ(e.code(), fixpp::core::error::dict_xml_parse_failed);
    }
}

// ---------------------------------------------------------------------------
// AC-L8 — UnknownFieldTypeThrowsXmlParseError
//
// A <field type='UNKNOWN_TYPE'> outside the FIX-type vocabulary must throw
// dict::xml_parse_error.
// ---------------------------------------------------------------------------
TEST(NegativePaths, AC_L8_UnknownFieldTypeThrowsXmlParseError) {
    Arena a;
    fixpp::dict::XmlLoader loader;
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields>)"
        R"(<field number='1' name='Foo' type='UNKNOWN_TYPE'/>)"
        R"(</fields><messages/></fix>)";
    try {
        (void)loader.load_from_string(kXml, &a.mr);
        FAIL() << "expected dict::xml_parse_error";
    } catch (fixpp::dict::xml_parse_error const& e) {
        EXPECT_EQ(e.code(), fixpp::core::error::dict_xml_parse_failed);
    }
}

// ---------------------------------------------------------------------------
// AC-L10 — LoadFromStringEquivalentForMalformed
//
// load_from_string with the same malformed XML that load(path, ...) would
// reject (open-file failure triggers a different code path, so we use clearly
// malformed XML that both entry points share via pugixml's parse step).
// Confirms both entry points produce the same dict::xml_parse_error type.
// ---------------------------------------------------------------------------
TEST(NegativePaths, AC_L10_LoadFromStringEquivalentForMalformed) {
    Arena a_str;
    Arena a_file;
    fixpp::dict::XmlLoader loader;

    // Malformed XML — exercise the load_from_string path.
    constexpr std::string_view kBadXml = R"(<fix><broken_tag attr=)";

    bool string_threw = false;
    try {
        (void)loader.load_from_string(kBadXml, &a_str.mr);
        FAIL() << "expected dict::xml_parse_error from load_from_string";
    } catch (fixpp::dict::xml_parse_error const& e) {
        EXPECT_EQ(e.code(), fixpp::core::error::dict_xml_parse_failed);
        string_threw = true;
    }
    EXPECT_TRUE(string_threw);

    // The load(path, mr) path for a missing file also throws xml_parse_error
    // (filesystem-open failure is translated the same way per xml_loader.cpp).
    bool file_threw = false;
    try {
        (void)loader.load("/nonexistent/path/bad.xml", &a_file.mr);
        FAIL() << "expected dict::xml_parse_error from load";
    } catch (fixpp::dict::xml_parse_error const& e) {
        EXPECT_EQ(e.code(), fixpp::core::error::dict_xml_parse_failed);
        file_threw = true;
    }
    EXPECT_TRUE(file_threw);
}
