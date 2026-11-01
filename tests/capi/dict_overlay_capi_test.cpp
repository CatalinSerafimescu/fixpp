// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/dict_overlay_capi_test.cpp — W-9's pure rows of 098-dialect-overlay
// (contracts/witnesses.md "C ABI and Python"; research R-6, R-12).
//
// `fixpp_dict_load_from_xml_with_overlay` over dictionaries/FIX44.xml with the
// fixtures under tests/dictionary/fixtures/098/capi/, and the exception-to-code
// mapping function its catch calls (`fixpp_capi::detail::dict_overlay_error_code`)
// fed each exception type directly. No row starts an engine.
//
// Each thunk row first checks its overlay's cause through the C++ loader
// (`dict::load_any_with_overlay`): the error type and the part of `what()` that
// names the cause (research R-6, "What each error carries"). A row whose fixture is
// missing, or is refused for another cause, fails there, not on the code.
// `*out_dict` starts at a non-null sentinel, so the NULL a row asserts is one the
// thunk wrote.

#include <gtest/gtest.h>

#include <exception>
#include <filesystem>
#include <memory_resource>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>

#include "capi_internal.hpp"
#include "fix/c_api/dict.h"
#include "fix/c_api/engine.h"
#include "fix/c_api/error.h"
#include "fix/c_api/session.h"
#include "fixpp/dict/error.hpp"
#include "fixpp/dict/load_any.hpp"

namespace {

std::filesystem::path base_path() {
    return std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";
}
std::filesystem::path fixture(std::string_view name) {
    return std::filesystem::path{FIXPP_098_FIXTURE_DIR} / name;
}

// A non-null value no load returns, so a NULL read back was written by the thunk.
fixpp_dict_t* sentinel() {
    static int storage = 0;
    return reinterpret_cast<fixpp_dict_t*>(&storage);
}

// The C++ load of `overlay` throws `Expected` with `cause` in its message.
template <class Expected>
void expect_cause(std::filesystem::path const& overlay, std::string_view cause) {
    ASSERT_TRUE(std::filesystem::exists(base_path())) << base_path();
    ASSERT_TRUE(std::filesystem::exists(overlay)) << overlay;
    try {
        (void)fixpp::dict::load_any_with_overlay(base_path(), overlay,
                                                 std::pmr::new_delete_resource());
        ADD_FAILURE() << overlay << " loaded; the row needs it refused";
    } catch (std::exception const& e) {
        EXPECT_TRUE(typeid(e) == typeid(Expected))
            << overlay << " threw " << typeid(e).name() << ": " << e.what();
        EXPECT_NE(std::string_view{e.what()}.find(cause), std::string_view::npos)
            << overlay << ": " << e.what();
    }
}

// The thunk's code for `overlay` over FIX44.xml; `*out` holds what it left.
fixpp_error_t load(std::filesystem::path const& overlay, fixpp_dict_t** out) {
    *out = sentinel();
    return fixpp_dict_load_from_xml_with_overlay(base_path().c_str(), overlay.c_str(), out);
}

// ── The thunk ────────────────────────────────────────────────────────────────

TEST(DictOverlayCapiW9, EntryCapOverlayIsLimitExceededAndNull) {
    auto const overlay = fixture("capi/FIX44_entry_cap_overlay.xml");
    expect_cause<fixpp::dict::overlay_limit_error>(overlay, "new message definitions cap");
    fixpp_dict_t* out = nullptr;
    EXPECT_EQ(load(overlay, &out), FIXPP_ERR_DICT_LIMIT_EXCEEDED);
    EXPECT_EQ(out, nullptr);
}

TEST(DictOverlayCapiW9, DoctypeOverlayIsLimitExceededAndNull) {
    auto const overlay = fixture("capi/FIX44_doctype_overlay.xml");
    expect_cause<fixpp::dict::overlay_limit_error>(overlay, "DOCTYPE");
    fixpp_dict_t* out = nullptr;
    EXPECT_EQ(load(overlay, &out), FIXPP_ERR_DICT_LIMIT_EXCEEDED);
    EXPECT_EQ(out, nullptr);
}

TEST(DictOverlayCapiW9, ConflictingOverlayIsDictConfigAndNull) {
    auto const overlay = fixture("capi/FIX44_conflict_overlay.xml");
    expect_cause<fixpp::dict::overlay_conflict_error>(overlay, "identifying attribute changed");
    fixpp_dict_t* out = nullptr;
    EXPECT_EQ(load(overlay, &out), FIXPP_ERR_DICT_CONFIG);
    EXPECT_EQ(out, nullptr);
}

// Control: each NULL argument is refused before any load.
TEST(DictOverlayCapiW9, NullArgumentsAreNullHandle) {
    auto const base = base_path().string();
    auto const overlay = fixture("real/FIX44_overlay.xml").string();
    fixpp_dict_t* out = sentinel();
    EXPECT_EQ(fixpp_dict_load_from_xml_with_overlay(nullptr, overlay.c_str(), &out),
              FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(out, nullptr);
    out = sentinel();
    EXPECT_EQ(fixpp_dict_load_from_xml_with_overlay(base.c_str(), nullptr, &out),
              FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(out, nullptr);
    EXPECT_EQ(fixpp_dict_load_from_xml_with_overlay(base.c_str(), overlay.c_str(), nullptr),
              FIXPP_ERR_NULL_HANDLE);
}

// Control: the handle the FIX44 overlay yields is accepted by
// fixpp_session_config_set_dictionary and by fixpp_session_open on an engine that is
// never started. A base-only handle passes this too; the live arm discriminates.
TEST(DictOverlayCapiW9, HandleIsAcceptedBySessionConfigAndOpen) {
    auto const overlay = fixture("real/FIX44_overlay.xml");
    ASSERT_TRUE(std::filesystem::exists(overlay)) << overlay;
    fixpp_dict_t* dict = nullptr;
    ASSERT_EQ(load(overlay, &dict), FIXPP_ERR_OK);
    ASSERT_NE(dict, nullptr);

    fixpp_engine_config_t* ec = nullptr;
    ASSERT_EQ(fixpp_engine_config_create(&ec), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_config_set_realtime_clock(ec), FIXPP_ERR_OK);
    fixpp_engine_t* eng = nullptr;
    ASSERT_EQ(fixpp_engine_create(ec, 1, 0, &eng), FIXPP_ERR_OK);  // consumes ec

    fixpp_session_config_t* sc = nullptr;
    EXPECT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_comp_ids(sc, "INI-098", "ACC-098"), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_begin_string(sc, "FIX.4.4"), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_role(sc, FIXPP_ROLE_INITIATOR), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_heartbeat_seconds(sc, 30), FIXPP_ERR_OK);
    EXPECT_EQ(
        fixpp_session_config_set_security(sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr, nullptr),
        FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_dictionary(sc, dict), FIXPP_ERR_OK);
    fixpp_dict_destroy(dict);  // the setter copied it

    fixpp_session_t* sess = nullptr;
    EXPECT_EQ(fixpp_session_open(eng, sc, &sess), FIXPP_ERR_OK);  // consumes sc on success
    EXPECT_NE(sess, nullptr);
    if (sess == nullptr) {
        fixpp_session_config_destroy(sc);
    }
    fixpp_engine_destroy(eng);
}

// ── The mapping function, fed each type (research R-6's table) ───────────────

fixpp_error_t code_for(std::exception_ptr const& ep) {
    return fixpp_capi::detail::dict_overlay_error_code(ep);
}

TEST(DictOverlayCapiW9Mapping, OverlayLimitErrorIsLimitExceeded) {
    EXPECT_EQ(code_for(std::make_exception_ptr(fixpp::dict::overlay_limit_error("limit"))),
              FIXPP_ERR_DICT_LIMIT_EXCEEDED);
}

TEST(DictOverlayCapiW9Mapping, XmlOomErrorIsDictOom) {
    EXPECT_EQ(code_for(std::make_exception_ptr(fixpp::dict::xml_oom_error{})), FIXPP_ERR_DICT_OOM);
}

TEST(DictOverlayCapiW9Mapping, BareBadAllocIsDictOom) {
    EXPECT_EQ(code_for(std::make_exception_ptr(std::bad_alloc{})), FIXPP_ERR_DICT_OOM);
}

TEST(DictOverlayCapiW9Mapping, XmlParseErrorIsDictConfig) {
    EXPECT_EQ(code_for(std::make_exception_ptr(fixpp::dict::xml_parse_error("parse"))),
              FIXPP_ERR_DICT_CONFIG);
}

TEST(DictOverlayCapiW9Mapping, OverlayConflictErrorIsDictConfig) {
    EXPECT_EQ(code_for(std::make_exception_ptr(fixpp::dict::overlay_conflict_error("conflict"))),
              FIXPP_ERR_DICT_CONFIG);
}

TEST(DictOverlayCapiW9Mapping, RuntimeErrorIsDictConfig) {
    EXPECT_EQ(code_for(std::make_exception_ptr(std::runtime_error("other"))),
              FIXPP_ERR_DICT_CONFIG);
}

TEST(DictOverlayCapiW9Mapping, NonStdExceptionIsDictConfig) {
    EXPECT_EQ(code_for(std::make_exception_ptr(42)), FIXPP_ERR_DICT_CONFIG);
}

}  // namespace
