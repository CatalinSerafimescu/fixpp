// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/negative_paths_test.cpp
//
// seam #7 — AC-L2..L8 / AC-L10
// Negative-path tests for fixpp::dict::XmlLoader::load and ::load_from_string.
// Each test drives one malformed or semantically-broken XML through
// load_from_string (or load for the filesystem path case) and asserts the
// correct typed exception with the matching fixpp::core::error code.

#include <gtest/gtest.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/error.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fstream>
#include <memory_resource>
#include <string>
#include <string_view>

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

void expect_xml_parse_error_contains(std::string_view xml, std::string_view needle) {
    auto* mr = std::pmr::new_delete_resource();
    try {
        (void)fixpp::dict::XmlLoader{}.load_from_string(xml, mr);
        FAIL() << "expected dict::xml_parse_error";
    } catch (fixpp::dict::xml_parse_error const& e) {
        EXPECT_EQ(e.code(), fixpp::core::error::dict_xml_parse_failed);
        EXPECT_NE(std::string{e.what()}.find(needle), std::string::npos) << "what()=" << e.what();
    }
}

void expect_unknown_version_error_contains(std::string_view xml, std::string_view needle) {
    auto* mr = std::pmr::new_delete_resource();
    try {
        (void)fixpp::dict::XmlLoader{}.load_from_string(xml, mr);
        FAIL() << "expected dict::unknown_version_error";
    } catch (fixpp::dict::unknown_version_error const& e) {
        EXPECT_EQ(e.code(), fixpp::core::error::dict_unknown_version);
        EXPECT_NE(std::string{e.what()}.find(needle), std::string::npos) << "what()=" << e.what();
    }
}

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
    constexpr std::string_view kXml = R"(<fix type='FIX' major='6' minor='0' servicepack='0'>)"
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
    constexpr std::string_view kXml = R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
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
    constexpr std::string_view kXml = R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
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
    constexpr std::string_view kXml = R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
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
    constexpr std::string_view kXml = R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
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
    constexpr std::string_view kXml = R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
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

TEST(NegativePaths, RejectsMissingFieldNameAttribute) {
    constexpr std::string_view kXml = R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
                                      R"(<fields><field number='11' type='STRING'/></fields>)"
                                      R"(<messages/></fix>)";
    expect_xml_parse_error_contains(kXml, "missing name attribute");
}

TEST(NegativePaths, RejectsNonNumericServicepack) {
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='x'>)"
        R"(<fields><field number='11' name='ClOrdID' type='STRING'/></fields>)"
        R"(<messages/></fix>)";
    expect_unknown_version_error_contains(kXml, "non-numeric servicepack");
}

// Pins the !s.empty() short-circuit branch in xml_loader.cpp: servicepack=''
// (attribute present but empty) must be treated identically to servicepack='0'
// — i.e. SP0 — and resolve to v44.
TEST(NegativePaths, EmptyServicepackResolvesAsSP0) {
    auto* mr = std::pmr::new_delete_resource();
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack=''>)"
        R"(<fields><field number='35' name='MsgType' type='STRING'/></fields>)"
        R"(<messages/></fix>)";
    EXPECT_NO_THROW({
        auto dict = fixpp::dict::XmlLoader{}.load_from_string(kXml, mr);
        EXPECT_EQ(dict.which_session_version(), fixpp::dict::session_version::v44);
    });
}

TEST(NegativePaths, AllowsDictionaryWithoutComponentsBlock) {
    auto* mr = std::pmr::new_delete_resource();
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields>)"
        R"(<field number='35' name='MsgType' type='STRING'/>)"
        R"(<field number='49' name='SenderCompID' type='STRING'/>)"
        R"(</fields>)"
        R"(<messages>)"
        R"(<message name='Heartbeat' msgtype='0' msgcat='admin'>)"
        R"(<field name='MsgType' required='Y'/><field name='SenderCompID' required='N'/>)"
        R"(</message>)"
        R"(</messages>)"
        R"(</fix>)";

    EXPECT_NO_THROW({
        auto dict = fixpp::dict::XmlLoader{}.load_from_string(kXml, mr);
        EXPECT_EQ(dict.which_session_version(), fixpp::dict::session_version::v44);
    });
}

TEST(NegativePaths, RejectsComponentWithoutNameAttribute) {
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields><field number='11' name='ClOrdID' type='STRING'/></fields>)"
        R"(<components><component><field name='ClOrdID' required='Y'/></component></components>)"
        R"(<messages><message name='NewOrderSingle' msgtype='D' msgcat='app'/></messages>)"
        R"(</fix>)";
    expect_xml_parse_error_contains(kXml, "<component> missing name attribute");
}

TEST(NegativePaths, RejectsDuplicateComponentNames) {
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields><field number='11' name='ClOrdID' type='STRING'/></fields>)"
        R"(<components><component name='Instrument'/><component name='Instrument'/></components>)"
        R"(<messages><message name='NewOrderSingle' msgtype='D' msgcat='app'/></messages>)"
        R"(</fix>)";
    expect_xml_parse_error_contains(kXml, "duplicate <component name=\"Instrument\">");
}

TEST(NegativePaths, RejectsMissingMessagesBlockWhenFieldsArePresent) {
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields><field number='11' name='ClOrdID' type='STRING'/></fields>)"
        R"(</fix>)";
    expect_xml_parse_error_contains(kXml, "missing <messages> block");
}

TEST(NegativePaths, RejectsMessageWithoutMsgTypeAttribute) {
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields><field number='11' name='ClOrdID' type='STRING'/></fields>)"
        R"(<messages><message name='NewOrderSingle' msgcat='app'/></messages>)"
        R"(</fix>)";
    expect_xml_parse_error_contains(kXml, "missing msgtype attribute");
}

TEST(NegativePaths, RejectsMessageWithoutNameAttribute) {
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields><field number='11' name='ClOrdID' type='STRING'/></fields>)"
        R"(<messages><message msgtype='D' msgcat='app'/></messages>)"
        R"(</fix>)";
    expect_xml_parse_error_contains(kXml, "missing name attribute");
}

TEST(NegativePaths, RejectsFieldReferenceWithoutNameAttribute) {
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields><field number='35' name='MsgType' type='STRING'/></fields>)"
        R"(<messages><message name='Heartbeat' msgtype='0' msgcat='admin'>)"
        R"(<field required='Y'/>)"
        R"(</message></messages></fix>)";
    expect_xml_parse_error_contains(kXml, "<field> reference missing name");
}

TEST(NegativePaths, RejectsUnknownFieldReferenceInMessage) {
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields><field number='35' name='MsgType' type='STRING'/></fields>)"
        R"(<messages><message name='Heartbeat' msgtype='0' msgcat='admin'>)"
        R"(<field name='UnknownField' required='Y'/>)"
        R"(</message></messages></fix>)";
    expect_xml_parse_error_contains(kXml, "not declared in <fields> block");
}

TEST(NegativePaths, RejectsGroupWithoutMatchingNoFieldDeclaration) {
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields><field number='35' name='MsgType' type='STRING'/></fields>)"
        R"(<messages><message name='Heartbeat' msgtype='0' msgcat='admin'>)"
        R"(<group name='NoUnderlyings' required='N'><field name='MsgType' required='Y'/></group>)"
        R"(</message></messages></fix>)";
    expect_xml_parse_error_contains(kXml, "has no matching <field> declaration");
}

TEST(NegativePaths, LoadRejectsMalformedXmlFileWithPugixmlDescription) {
    auto* mr = std::pmr::new_delete_resource();
    // Per-process-unique filename avoids clobber under sharded/parallel reruns.
    auto const path = std::filesystem::temp_directory_path() /
                      ("fixpp_dictionary_malformed_input_" + std::to_string(::getpid()) + ".xml");
    // RAII guard: remove the temp file on every exit path (throw or return).
    struct FileGuard {
        std::filesystem::path const& p;
        ~FileGuard() { std::filesystem::remove(p); }
    } guard{path};

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << "<fix><unclosed>";
    }

    try {
        (void)fixpp::dict::XmlLoader{}.load(path, mr);
        FAIL() << "expected dict::xml_parse_error";
    } catch (fixpp::dict::xml_parse_error const& e) {
        EXPECT_EQ(e.code(), fixpp::core::error::dict_xml_parse_failed);
        EXPECT_NE(std::string{e.what()}.find("Start-end tags mismatch"), std::string::npos)
            << "what()=" << e.what();
    }
}
