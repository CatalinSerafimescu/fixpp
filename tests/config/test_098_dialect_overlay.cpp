// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/config/test_098_dialect_overlay.cpp
//
// 098-dialect-overlay, contracts/witnesses.md W-8 (US3 AS-1..AS-5, SC-004): the
// TOML session key `dialect_overlay` (research R-9, data-model A.4). Fixtures are
// under tests/config/fixtures/098/: the TOML files and the QuickFIX-XML overlays
// they name by paths relative to the configuration file. The base dictionary is
// tests/config/fixtures/FIX44.xml, named as "../FIX44.xml".
//
// What each fixture's overlay declares:
//   venue_overlay.xml    field 5001 on NewOrderSingle (35=D);
//   venue_overlay_b.xml  field 5101 on NewOrderSingle;
//   conflict_overlay.xml SolicitedFlag(377) redeclared with a type other than
//                        the base's, so the merge refuses it.
//
// Every cell calls only the API that exists without 098, so it compiles on the
// unmodified tree. The last cell is a negative control: a deferred key other
// than dialect_overlay stays deferred at the session scopes.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/config/load_diagnostic.hpp>
#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#ifndef FIXPP_CONFIG_FIXTURE_DIR
#error "FIXPP_CONFIG_FIXTURE_DIR must be set by CMake"
#endif

namespace {

using RC = fixpp::config::reason_class;
using fixpp::config::LoadDiagnostic;

std::filesystem::path fixture(std::string_view name) {
    return std::filesystem::path{std::string{FIXPP_CONFIG_FIXTURE_DIR}} / "098" / name;
}

// The loaded bundle and the io_context its clock is bound to. The result is
// declared after the context, so it is destroyed first.
struct Loaded {
    asio::io_context ioc;
    fixpp::config::LoadResult result;

    explicit Loaded(std::string_view name) {
        fixpp::config::LoadOptions opts;
        opts.engine_executor = ioc.get_executor();
        result = fixpp::config::load_toml_config(fixture(name), opts);
    }
};

std::string diag_string(std::vector<LoadDiagnostic> const& diags) {
    std::string s;
    for (auto const& d : diags) {
        s += "[reason=" + std::to_string(static_cast<int>(d.reason)) + " key=" + d.key_path +
             " at " + std::to_string(d.location.line) + ":" + std::to_string(d.location.col) +
             "] " + d.message + "\n";
    }
    return s;
}

std::string diag_string(fixpp::config::LoadResult const& r) {
    return r.has_value() ? std::string{"(the load succeeded)"} : diag_string(r.error());
}

bool has_diag(std::vector<LoadDiagnostic> const& diags, RC reason, std::string_view key_path) {
    for (auto const& d : diags) {
        if (d.reason == reason && d.key_path == key_path) {
            return true;
        }
    }
    return false;
}

std::size_t count_diag(std::vector<LoadDiagnostic> const& diags, RC reason,
                       std::string_view key_path) {
    std::size_t n = 0;
    for (auto const& d : diags) {
        if (d.reason == reason && d.key_path == key_path) {
            ++n;
        }
    }
    return n;
}

// The diagnostics written at `key_path`, whatever their reason class.
std::vector<LoadDiagnostic> diags_at(std::vector<LoadDiagnostic> const& diags,
                                     std::string_view key_path) {
    std::vector<LoadDiagnostic> out;
    for (auto const& d : diags) {
        if (d.key_path == key_path) {
            out.push_back(d);
        }
    }
    return out;
}

bool declares(std::shared_ptr<const fixpp::dict::Dictionary> const& dict, std::uint16_t tag) {
    return dict && dict->as_table_view().field_valid_for("D", tag);
}

constexpr std::string_view kDeferredText = "is recognized but not yet supported";

// ── AS-1 (SC-004): no overlay ────────────────────────────────────────────────

TEST(DialectOverlay098, AS1_NoOverlay_SessionGetsTheEngineDictionaryAndOpens) {
    Loaded l{"as1_no_overlay.toml"};
    ASSERT_TRUE(l.result.has_value()) << diag_string(l.result);
    auto const& bundle = *l.result;
    ASSERT_EQ(bundle.engine.dictionaries.size(), 1U);
    ASSERT_EQ(bundle.sessions.size(), 1U);
    fixpp::session::SessionConfig const cfg = bundle.sessions[0].config;
    EXPECT_EQ(cfg.dictionary, bundle.engine.dictionaries.front())
        << "the session's config.dictionary must be engine.dictionaries.front()";

    // No host code touches the session's dictionary; the engine slice is the
    // bundle's, on this test's io_context.
    fixpp::core::EngineConfig engine;
    engine.executor = l.ioc.get_executor();
    engine.clock = bundle.engine.clock;
    engine.default_store_factory = bundle.engine.default_store_factory;
    engine.default_transport_factory = bundle.engine.default_transport_factory;
    fixpp::session::Session sess{engine, cfg};
    auto fut = asio::co_spawn(l.ioc, sess.open(), asio::use_future);
    l.ioc.run();
    ASSERT_EQ(fut.wait_for(std::chrono::seconds{0}), std::future_status::ready)
        << "open() did not complete when the io_context ran out of work";
    auto const r = fut.get();
    EXPECT_TRUE(r.has_value()) << "open() returned error code " << static_cast<int>(r.error())
                               << " (invalid_session_config is "
                               << static_cast<int>(fixpp::core::error::invalid_session_config)
                               << ")";
}

// ── AS-2: an overlay session gets the merged dictionary ──────────────────────

TEST(DialectOverlay098, AS2_OverlaySession_GetsTheMergedDictionary) {
    Loaded l{"as2_overlay.toml"};
    ASSERT_TRUE(l.result.has_value()) << diag_string(l.result);
    auto const& bundle = *l.result;
    ASSERT_EQ(bundle.sessions.size(), 3U);
    ASSERT_EQ(bundle.engine.dictionaries.size(), 1U);
    auto const& base = bundle.engine.dictionaries.front();
    auto const& merged = bundle.sessions[1].config.dictionary;
    ASSERT_NE(merged, nullptr);
    EXPECT_NE(merged, base) << "the overlay session must not get the base dictionary";
    EXPECT_TRUE(declares(merged, 5001)) << "5001 must be valid on 35=D in the merged dictionary";
    EXPECT_FALSE(declares(base, 5001)) << "the base dictionary must not declare 5001";
}

TEST(DialectOverlay098, AS2_TwoSessionsNamingOnePath_ShareOnePointer) {
    Loaded l{"as2_overlay.toml"};
    ASSERT_TRUE(l.result.has_value()) << diag_string(l.result);
    auto const& bundle = *l.result;
    ASSERT_EQ(bundle.sessions.size(), 3U);
    ASSERT_NE(bundle.sessions[1].config.dictionary, nullptr);
    EXPECT_EQ(bundle.sessions[1].config.dictionary, bundle.sessions[2].config.dictionary);
}

TEST(DialectOverlay098, AS2_EngineDictionaries_HoldOnlyTheBase) {
    Loaded l{"as2_overlay.toml"};
    ASSERT_TRUE(l.result.has_value()) << diag_string(l.result);
    auto const& bundle = *l.result;
    ASSERT_EQ(bundle.sessions.size(), 3U);
    ASSERT_EQ(bundle.engine.dictionaries.size(), 1U);
    auto const& base = bundle.engine.dictionaries.front();
    EXPECT_EQ(bundle.sessions[0].config.dictionary, base);
    EXPECT_NE(bundle.sessions[1].config.dictionary, base);
    EXPECT_NE(bundle.sessions[2].config.dictionary, base);
}

// ── AS-5: [default] inheritance and override ─────────────────────────────────

TEST(DialectOverlay098, AS5_DefaultOverlay_InheritedAndOverridden) {
    Loaded l{"as5_default.toml"};
    ASSERT_TRUE(l.result.has_value()) << diag_string(l.result);
    auto const& bundle = *l.result;
    ASSERT_EQ(bundle.sessions.size(), 2U);
    ASSERT_EQ(bundle.engine.dictionaries.size(), 1U);
    auto const& base = bundle.engine.dictionaries.front();
    auto const& inherited = bundle.sessions[0].config.dictionary;
    auto const& overridden = bundle.sessions[1].config.dictionary;
    ASSERT_NE(inherited, nullptr);
    ASSERT_NE(overridden, nullptr);
    EXPECT_NE(inherited, base);
    EXPECT_NE(overridden, base);
    EXPECT_NE(inherited, overridden);
    EXPECT_TRUE(declares(inherited, 5001)) << "session[0] inherits [default]'s overlay";
    EXPECT_FALSE(declares(inherited, 5101));
    EXPECT_TRUE(declares(overridden, 5101)) << "session[1] overrides [default]'s overlay";
    EXPECT_FALSE(declares(overridden, 5001));
}

// ── AS-3: load failures at the key path where the value was written ──────────

TEST(DialectOverlay098, AS3_MissingOverlayFile_ReportedAtItsKeyPath) {
    Loaded l{"as3_missing.toml"};
    ASSERT_FALSE(l.result.has_value()) << "an absent overlay file must fail the load";
    auto const& diags = l.result.error();
    EXPECT_TRUE(
        has_diag(diags, RC::invalid_or_contradictory_selector, "session[0].dialect_overlay"))
        << diag_string(diags);
    EXPECT_FALSE(
        has_diag(diags, RC::recognized_not_yet_supported_step2, "session[0].dialect_overlay"))
        << diag_string(diags);
}

TEST(DialectOverlay098, AS3_ConflictingOverlay_ReportedAtItsKeyPath) {
    Loaded l{"as3_conflict.toml"};
    ASSERT_FALSE(l.result.has_value()) << "a conflicting overlay must fail the load";
    auto const& diags = l.result.error();
    EXPECT_TRUE(
        has_diag(diags, RC::invalid_or_contradictory_selector, "session[0].dialect_overlay"))
        << diag_string(diags);
    EXPECT_FALSE(
        has_diag(diags, RC::recognized_not_yet_supported_step2, "session[0].dialect_overlay"))
        << diag_string(diags);
}

// ── AS-4: root scope ─────────────────────────────────────────────────────────

TEST(DialectOverlay098, AS4_RootScope_Refused) {
    Loaded l{"as4_root.toml"};
    ASSERT_FALSE(l.result.has_value()) << "dialect_overlay at root must fail the load";
    auto const& diags = l.result.error();
    auto const at = diags_at(diags, "dialect_overlay");
    ASSERT_EQ(at.size(), 1U) << diag_string(diags);
    EXPECT_EQ(at.front().reason, RC::invalid_or_contradictory_selector) << diag_string(diags);
    EXPECT_EQ(at.front().message,
              "dialect_overlay is a session key; set it in [default] or [[session]]");
}

// ── Value checks ─────────────────────────────────────────────────────────────

TEST(DialectOverlay098, Value_NotAString_MalformedValue) {
    Loaded l{"value_not_string.toml"};
    ASSERT_FALSE(l.result.has_value());
    auto const& diags = l.result.error();
    EXPECT_TRUE(has_diag(diags, RC::malformed_value, "session[0].dialect_overlay"))
        << diag_string(diags);
}

TEST(DialectOverlay098, Value_EmptyString_EmptyRequired) {
    Loaded l{"value_empty.toml"};
    ASSERT_FALSE(l.result.has_value());
    auto const& diags = l.result.error();
    EXPECT_TRUE(has_diag(diags, RC::empty_required, "session[0].dialect_overlay"))
        << diag_string(diags);
}

// ── Origins: one diagnostic per place the failing value was written ──────────

TEST(DialectOverlay098, TwoOrigins_OneFailingFile_OneDiagnosticAtEach) {
    Loaded l{"two_origins.toml"};
    ASSERT_FALSE(l.result.has_value());
    auto const& diags = l.result.error();
    EXPECT_EQ(count_diag(diags, RC::invalid_or_contradictory_selector, "default.dialect_overlay"),
              1U)
        << diag_string(diags);
    EXPECT_EQ(
        count_diag(diags, RC::invalid_or_contradictory_selector, "session[1].dialect_overlay"), 1U)
        << diag_string(diags);
    EXPECT_TRUE(diags_at(diags, "session[0].dialect_overlay").empty())
        << "session[0] writes no dialect_overlay; its inherited value is reported at "
           "default.dialect_overlay\n"
        << diag_string(diags);
}

// ── No [dictionary] ──────────────────────────────────────────────────────────

TEST(DialectOverlay098, NoDictionary_OverlayReportsRequiresDictionary) {
    Loaded l{"no_dictionary.toml"};
    ASSERT_FALSE(l.result.has_value());
    auto const& diags = l.result.error();
    auto const at = diags_at(diags, "session[0].dialect_overlay");
    ASSERT_EQ(at.size(), 1U) << diag_string(diags);
    EXPECT_EQ(at.front().reason, RC::invalid_or_contradictory_selector) << diag_string(diags);
    EXPECT_EQ(at.front().message, "dialect_overlay requires [dictionary]");
    EXPECT_TRUE(has_diag(diags, RC::missing_required, "dictionary")) << diag_string(diags);
}

// ── Location: the value's position in the raw table where it was written ─────
//
// The literals are the line and column (1-based) of each `dialect_overlay`
// value's first character in two_origins.toml. Re-derive them with
// `grep -n dialect_overlay tests/config/fixtures/098/two_origins.toml` and the
// column of the opening quote on each line.

TEST(DialectOverlay098, Location_IsTheValuePositionWhereItWasWritten) {
    Loaded l{"two_origins.toml"};
    ASSERT_FALSE(l.result.has_value());
    auto const& diags = l.result.error();

    auto const in_default = diags_at(diags, "default.dialect_overlay");
    ASSERT_EQ(in_default.size(), 1U) << diag_string(diags);
    EXPECT_EQ(in_default.front().reason, RC::invalid_or_contradictory_selector)
        << diag_string(diags);
    EXPECT_EQ(in_default.front().location.line, 18U) << diag_string(diags);
    EXPECT_EQ(in_default.front().location.col, 19U) << diag_string(diags);

    auto const in_session = diags_at(diags, "session[1].dialect_overlay");
    ASSERT_EQ(in_session.size(), 1U) << diag_string(diags);
    EXPECT_EQ(in_session.front().reason, RC::invalid_or_contradictory_selector)
        << diag_string(diags);
    EXPECT_EQ(in_session.front().location.line, 41U) << diag_string(diags);
    EXPECT_EQ(in_session.front().location.col, 19U) << diag_string(diags);
}

// ── Message: a load failure carries the loader's text ────────────────────────
//
// A conflict's message names the element kind and its key (research R-6), so it
// names the tag 377; the deferred-key text names neither.

TEST(DialectOverlay098, Message_LoadFailureCarriesTheLoadersText) {
    Loaded l{"as3_conflict.toml"};
    ASSERT_FALSE(l.result.has_value());
    auto const& diags = l.result.error();
    auto const at = diags_at(diags, "session[0].dialect_overlay");
    ASSERT_EQ(at.size(), 1U) << diag_string(diags);
    EXPECT_EQ(at.front().reason, RC::invalid_or_contradictory_selector) << diag_string(diags);
    EXPECT_NE(at.front().message.find("377"), std::string::npos)
        << "message: " << at.front().message;
    EXPECT_EQ(at.front().message.find(kDeferredText), std::string::npos)
        << "message: " << at.front().message;
}

// ── Raw session index: a non-table element before the session ────────────────
//
// inline_session_array.toml's `session` array holds a non-table element and then
// the one session, so that session is the first merged session table but the
// second raw element. Its overlay diagnostic is reported at the raw index, as
// the loader's other session diagnostics are, and is located at the value in
// that raw element. The literals are the value's line and column (1-based):
// re-derive them with `grep -n dialect_overlay
// tests/config/fixtures/098/inline_session_array.toml` and the column of the
// value's opening quote on that line.

TEST(DialectOverlay098, RawSessionIndex_NonTableElementBeforeTheSession) {
    Loaded l{"inline_session_array.toml"};
    ASSERT_FALSE(l.result.has_value());
    auto const& diags = l.result.error();
    auto const at = diags_at(diags, "session[1].dialect_overlay");
    ASSERT_EQ(at.size(), 1U) << diag_string(diags);
    EXPECT_EQ(at.front().reason, RC::invalid_or_contradictory_selector) << diag_string(diags);
    EXPECT_EQ(at.front().location.line, 9U) << diag_string(diags);
    EXPECT_EQ(at.front().location.col, 153U) << diag_string(diags);
    EXPECT_TRUE(diags_at(diags, "session[0].dialect_overlay").empty())
        << "the overlay is written in raw element 1, not 0\n"
        << diag_string(diags);
}

// ── Negative control: another deferred key at the session scopes ─────────────

TEST(DialectOverlay098, DeferredKeyAtSessionScopes_StaysDeferred) {
    Loaded l{"deferred_session_scope.toml"};
    ASSERT_FALSE(l.result.has_value());
    auto const& diags = l.result.error();
    EXPECT_TRUE(has_diag(diags, RC::recognized_not_yet_supported_step2, "default.tracer"))
        << diag_string(diags);
    EXPECT_TRUE(has_diag(diags, RC::recognized_not_yet_supported_step2, "session[0].tracer"))
        << diag_string(diags);
}

}  // namespace
