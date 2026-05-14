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

}  // namespace
