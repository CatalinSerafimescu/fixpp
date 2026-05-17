// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/parser_error_test.cpp
//
// seam #10 — AC-L3 pugixml→dict::xml_parse_error translation
//
// Verifies that every pugixml `xml_parse_result.status != status_ok` path
// through `XmlLoader::load_from_string` is translated into
// `dict::xml_parse_error` with the correct `.code()` and message prefix.
// No on-disk fixtures; each case drives the error via a malformed string_view.

#include <fixpp/dict/error.hpp>
#include <fixpp/dict/xml_loader.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory_resource>
#include <string>
#include <string_view>

namespace {

// Each test allocates its own monotonic buffer so PMR state never leaks.
constexpr std::size_t kBufSize = 64u * 1024u;

// Helper: assert that load_from_string with `xml` throws dict::xml_parse_error,
// has code() == fixpp::core::error::dict_xml_parse_failed, and that what()
// contains the "dict::xml_parse_error" prefix injected by the translation site.
void assert_parse_error(std::string_view xml) {
    std::array<std::byte, kBufSize> buf{};
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size(),
                                           std::pmr::null_memory_resource()};

    fixpp::dict::XmlLoader loader{};
    bool caught = false;
    try {
        (void)loader.load_from_string(xml, &mr);
    } catch (fixpp::dict::xml_parse_error const& e) {
        caught = true;
        EXPECT_EQ(e.code(), fixpp::core::error::dict_xml_parse_failed)
            << "wrong error code; what()=" << e.what();
        std::string const msg{e.what()};
        EXPECT_NE(msg.find("dict::xml_parse_error"), std::string::npos)
            << "message prefix missing; what()=" << msg;
    } catch (std::exception const& e) {
        FAIL() << "unexpected exception type; what()=" << e.what();
    } catch (...) {
        FAIL() << "unexpected non-std exception";
    }
    EXPECT_TRUE(caught) << "expected dict::xml_parse_error was not thrown";
}

void assert_parse_error_contains(std::string_view xml, std::string_view needle) {
    std::array<std::byte, kBufSize> buf{};
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size(),
                                           std::pmr::null_memory_resource()};

    try {
        (void)fixpp::dict::XmlLoader{}.load_from_string(xml, &mr);
        FAIL() << "expected dict::xml_parse_error";
    } catch (fixpp::dict::xml_parse_error const& e) {
        EXPECT_EQ(e.code(), fixpp::core::error::dict_xml_parse_failed);
        EXPECT_NE(std::string{e.what()}.find(needle), std::string::npos)
            << "what()=" << e.what();
    }
}

// ---------------------------------------------------------------------------
// TC-PE-01: Unclosed tag — truncated <field> element; pugixml status_end_element_mismatch
// or status_bad_end_element depending on pugixml version.
// ---------------------------------------------------------------------------
TEST(ParserError, UnclosedTag) {
    // The <fields> block is open and the <field> element is never closed;
    // the enclosing <fix> element is also never closed. Pugixml reports
    // xml_parse_result.status != status_ok.
    constexpr std::string_view kXml =
        "<fix major='4' minor='4'><fields>"
        "<field number='1' name='A' type='STRING'";
    assert_parse_error(kXml);
}

// ---------------------------------------------------------------------------
// TC-PE-02: Mismatched closing tag — </notfix> does not match <fix ...>.
// ---------------------------------------------------------------------------
TEST(ParserError, MismatchedClosingTag) {
    constexpr std::string_view kXml =
        "<fix major='4' minor='4'></notfix>";
    assert_parse_error(kXml);
}

// ---------------------------------------------------------------------------
// TC-PE-03: Invalid entity reference — &badentity; is not a recognised XML
// entity; pugixml yields status_bad_entity.
// ---------------------------------------------------------------------------
TEST(ParserError, InvalidEntity) {
    constexpr std::string_view kXml =
        "<fix major='4' minor='4'>&badentity;</fix>";
    assert_parse_error(kXml);
}

// ---------------------------------------------------------------------------
// TC-PE-04: Empty input — zero-length string_view. Pugixml treats this as
// a document with no root element and returns status != status_ok.
// ---------------------------------------------------------------------------
TEST(ParserError, EmptyInput) {
    assert_parse_error("");
}

// ---------------------------------------------------------------------------
// TC-PE-05: Non-XML garbage — no angle brackets; pugixml cannot find a root
// element and reports a parse failure.
// ---------------------------------------------------------------------------
TEST(ParserError, NonXmlGarbage) {
    constexpr std::string_view kXml = "this is not xml at all <<<";
    assert_parse_error(kXml);
}

// ---------------------------------------------------------------------------
// TC-PE-06..09 — defensive structural checks in `<fix>` header parsing
// (xml_loader.cpp:269/276/285/293; complements AC-L4 which exercises the
// resolve_version→Unknown path, not the from_chars-failure path).
// ---------------------------------------------------------------------------

// TC-PE-06: Root element is not <fix> — must throw xml_parse_error.
TEST(ParserError, RootElementNotFix) {
    constexpr std::string_view kXml = "<notfix major='4' minor='4'></notfix>";
    assert_parse_error(kXml);
}

// TC-PE-07: <fix> with no major/minor attributes — must throw xml_parse_error.
TEST(ParserError, FixMissingMajorMinor) {
    constexpr std::string_view kXml = "<fix><fields/><messages/></fix>";
    assert_parse_error(kXml);
}

// TC-PE-08: Non-numeric major — from_chars fails. unknown_version_error,
// not xml_parse_error, so don't use the assert_parse_error helper.
TEST(ParserError, NonNumericMajor) {
    std::array<std::byte, kBufSize> buf{};
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size(),
                                           std::pmr::null_memory_resource()};
    constexpr std::string_view kXml =
        "<fix major='abc' minor='0'><fields/><messages/></fix>";
    fixpp::dict::XmlLoader loader{};
    try {
        (void)loader.load_from_string(kXml, &mr);
        FAIL() << "expected dict::unknown_version_error";
    } catch (fixpp::dict::unknown_version_error const& e) {
        EXPECT_EQ(e.code(), fixpp::core::error::dict_unknown_version);
    }
}

// TC-PE-09: Non-numeric minor — symmetric to TC-PE-08.
TEST(ParserError, NonNumericMinor) {
    std::array<std::byte, kBufSize> buf{};
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size(),
                                           std::pmr::null_memory_resource()};
    constexpr std::string_view kXml =
        "<fix major='4' minor='xyz'><fields/><messages/></fix>";
    fixpp::dict::XmlLoader loader{};
    try {
        (void)loader.load_from_string(kXml, &mr);
        FAIL() << "expected dict::unknown_version_error";
    } catch (fixpp::dict::unknown_version_error const& e) {
        EXPECT_EQ(e.code(), fixpp::core::error::dict_unknown_version);
    }
}

TEST(ParserError, MalformedXmlIncludesPugixmlDescription) {
    constexpr std::string_view kXml = "<fix><unclosed>";
    assert_parse_error_contains(kXml, "Start-end tags mismatch");
}

}  // namespace
