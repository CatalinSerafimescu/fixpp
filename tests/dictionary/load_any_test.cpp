// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/load_any_test.cpp
// 080-orchestra-runtime-load Phase 2 (Foundational) — dict::load_any shared
// root-sniff dispatch helper. T003/T004/T005 per tasks.md; grouped whole-
// binary target dictionary_load_any_tests (LABELS "dictionary;orchestra;080").

#include <gtest/gtest.h>

#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/error.hpp>
#include <fixpp/dict/load_any.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fstream>
#include <iterator>
#include <memory_resource>
#include <string>
#include <string_view>

namespace {

std::filesystem::path orchestra_file() {
    return std::filesystem::path{FIXPP_ORCHESTRA_DATA_DIR} / "OrchestraFIXLatest.xml";
}

std::filesystem::path fix44_file() {
    return std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";
}

}  // namespace

// T003 — RED before T008: dict::load_any(OrchestraFIXLatest.xml, mr) returns
// a Dictionary whose parse/validate/read outcome equals dict::OrchestraLoader
// loaded directly. Structural equivalence: same session identity, same
// message count, same representative field_ref lookup (NewOrderSingle "D",
// ClOrdID tag 11, required).
TEST(LoadAny, OrchestraFileParityWithDirectOrchestraLoader) {
    std::pmr::monotonic_buffer_resource mr_any;
    auto via_any = fixpp::dict::load_any(orchestra_file(), &mr_any);

    std::pmr::monotonic_buffer_resource mr_direct;
    auto via_direct = fixpp::dict::OrchestraLoader{}.load(orchestra_file(), &mr_direct);

    EXPECT_EQ(via_any.which_session_version(), via_direct.which_session_version());
    EXPECT_EQ(via_any.which_session_version(), fixpp::dict::session_version::vlatest);
    EXPECT_EQ(via_any.messages().size(), via_direct.messages().size());
    EXPECT_EQ(via_any.messages().size(), 181U);

    auto const ref_any = via_any.field_ref("D", 11);
    auto const ref_direct = via_direct.field_ref("D", 11);
    EXPECT_EQ(ref_any.rule, ref_direct.rule);
    EXPECT_EQ(ref_any.rule, fixpp::dict::field_presence::Required);
}

// T004 — regression pin, green throughout: dict::load_any(FIX44.xml, mr)
// returns a Dictionary behaviorally identical to dict::XmlLoader{}.load(...).
// Bounded field/message lookups (not a whole-object EXPECT_EQ / memcmp —
// PMR pointers differ, host-OOM footgun per feedback_large_file_expect_eq).
TEST(LoadAny, ClassicFileParityWithDirectXmlLoader) {
    std::pmr::monotonic_buffer_resource mr_any;
    auto via_any = fixpp::dict::load_any(fix44_file(), &mr_any);

    std::pmr::monotonic_buffer_resource mr_direct;
    auto via_direct = fixpp::dict::XmlLoader{}.load(fix44_file(), &mr_direct);

    EXPECT_EQ(via_any.which_session_version(), via_direct.which_session_version());
    EXPECT_EQ(via_any.which_session_version(), fixpp::dict::session_version::v44);
    EXPECT_EQ(via_any.messages().size(), via_direct.messages().size());

    // NewOrderSingle "D" ClOrdID(11) is Required in both.
    auto const ref_any = via_any.field_ref("D", 11);
    auto const ref_direct = via_direct.field_ref("D", 11);
    EXPECT_EQ(ref_any.rule, ref_direct.rule);
    EXPECT_EQ(ref_any.rule, fixpp::dict::field_presence::Required);

    // A tag not declared for "D" (e.g. 4242, unused) is NotDeclared on both.
    EXPECT_EQ(via_any.field_valid_for("D", 4242), via_direct.field_valid_for("D", 4242));
    EXPECT_FALSE(via_any.field_valid_for("D", 4242));
}

// T005(a) — RED before T008: an XML whose root is neither `fix` nor
// `fixr:repository` throws dict::xml_parse_error (fail-closed, G2). Written
// to a temp file at test runtime — no checked-in fixture.
TEST(LoadAny, UnrecognizedRootThrowsXmlParseError) {
    auto const path = std::filesystem::temp_directory_path() / "load_any_test_unrecognized_root.xml";
    {
        std::ofstream out(path, std::ios::binary);
        out << "<foo><bar/></foo>";
    }
    std::pmr::monotonic_buffer_resource mr;
    try {
        (void)fixpp::dict::load_any(path, &mr);
        FAIL() << "expected dict::xml_parse_error";
    } catch (fixpp::dict::xml_parse_error const& e) {
        // Discriminates against an else-branch mutant that silently routes an
        // unrecognized root to XmlLoader/OrchestraLoader instead of throwing
        // load_any's own unrecognized-root error.
        EXPECT_NE(std::string_view{e.what()}.find("unrecognized"), std::string_view::npos);
    }
    std::filesystem::remove(path);
}

// T005(b) — RED before T008: an unreadable/nonexistent path likewise throws
// dict::xml_parse_error.
TEST(LoadAny, UnreadablePathThrowsXmlParseError) {
    auto const path = std::filesystem::path{FIXPP_ORCHESTRA_DATA_DIR} / "does_not_exist_080.xml";
    std::pmr::monotonic_buffer_resource mr;
    EXPECT_THROW((void)fixpp::dict::load_any(path, &mr), fixpp::dict::xml_parse_error);
}

// T005(c) — malformed (truncated) XML likewise throws dict::xml_parse_error.
TEST(LoadAny, MalformedXmlThrowsXmlParseError) {
    auto const path = std::filesystem::temp_directory_path() / "load_any_test_malformed.xml";
    {
        std::ofstream out(path, std::ios::binary);
        out << "<fixr:repository><fixr:fiel";
    }
    std::pmr::monotonic_buffer_resource mr;
    EXPECT_THROW((void)fixpp::dict::load_any(path, &mr), fixpp::dict::xml_parse_error);
    std::filesystem::remove(path);
}

namespace {

std::string slurp(std::filesystem::path const& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

// T015 — FR-004 single-dispatch source-inspection gate (quickstart Scenario 6).
// A SOURCE-inspection assertion, not a behavior test: the two call sites each
// delegate to dict::load_any and neither inlines its own root-element sniff, and
// exactly one runtime root-sniff implementation exists (src/dictionary/load_any.cpp).
// The discriminant token is pugixml's `document_element()` — load_any's sole
// root-read primitive; if a call site re-grew an inline sniff it would need this
// same primitive (or first_child()), so its ABSENCE at the call sites proves the
// dispatch rule is defined once. Behavior Scenarios 1/3 would still pass if each
// site sniffed independently; this gate is what pins "defined once, not duplicated".
TEST(LoadAny, FR004_SingleSharedDispatchSourceInspection) {
    std::filesystem::path const src{FIXPP_SRC_DIR};

    std::string const sniff_tu = slurp(src / "dictionary" / "load_any.cpp");
    std::string const capi_tu = slurp(src / "capi" / "dictionary.cpp");
    std::string const toml_tu = slurp(src / "config" / "selector_resolver.cpp");

    ASSERT_FALSE(sniff_tu.empty());
    ASSERT_FALSE(capi_tu.empty());
    ASSERT_FALSE(toml_tu.empty());

    // The sole runtime root-sniff lives in load_any.cpp and reads the root via
    // document_element() (N-2 hardening).
    EXPECT_NE(sniff_tu.find("document_element()"), std::string::npos)
        << "src/dictionary/load_any.cpp must be the one runtime root-sniff";

    // Both call sites delegate to dict::load_any …
    EXPECT_NE(capi_tu.find("load_any("), std::string::npos)
        << "C-API fixpp_dict_load_from_xml must call dict::load_any";
    EXPECT_NE(toml_tu.find("load_any("), std::string::npos)
        << "TOML dictionary.path resolver must call dict::load_any";

    // … and neither re-grows its own root-element sniff (the discriminant
    // primitive appears at NO call site — only inside load_any.cpp).
    EXPECT_EQ(capi_tu.find("document_element("), std::string::npos)
        << "C-API thunk must NOT inline a root-element sniff (FR-004)";
    EXPECT_EQ(toml_tu.find("document_element("), std::string::npos)
        << "TOML resolver must NOT inline a root-element sniff (FR-004)";
}
