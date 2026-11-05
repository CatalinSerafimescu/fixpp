// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/config/test_099_extra_paths.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-14 (tasks.md T095), FR-017, FR-018:
// `[dictionary] extra_paths` from TOML, contracts/surface.md's `## TOML`. Each document is written
// by the test, with absolute paths into the vendored dictionaries, so its text is here. The
// session witnesses' rig judges a real Session's route; EngineProbe gives a real Engine's
// register_session. Its live cell is tests/config/test_099_toml_live.cpp.

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/io_context.hpp>
#include <filesystem>
#include <fixpp/config/load_diagnostic.hpp>
#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "session/test_099_fixt_support.hpp"
#include "support/temp_dir.hpp"

#ifndef FIXPP_DICT_DATA_DIR
#error "FIXPP_DICT_DATA_DIR must be set by CMake"
#endif

namespace fixpp::session::test099 {
namespace {

using fixpp::config::LoadDiagnostic;

std::string vendored(std::string_view relative) {
    return (std::filesystem::path{FIXPP_DICT_DATA_DIR} / relative).generic_string();
}

// A TOML basic string of a path, whatever the separators.
std::string quoted(const std::string& s) { return "\"" + s + "\""; }

const std::string& kFix50Sp2() {
    static const std::string value = vendored("FIX50SP2.xml");
    return value;
}
const std::string& kFixt11() {
    static const std::string value = vendored("FIXT11.xml");
    return value;
}
const std::string& kFix44() {
    static const std::string value = vendored("FIX44.xml");
    return value;
}
const std::string& kLatest() {
    static const std::string value = vendored("orchestra/OrchestraFIXLatest.xml");
    return value;
}

// A document with one validating FIXT session. `extra_paths` is the key's whole line, or empty;
// `session_lines` goes into the session table.
std::string document(const std::string& extra_paths, const std::string& session_lines = {},
                     const std::string& kind = "path") {
    return "[clock]\nkind = \"system\"\n\n[store]\nkind = \"memory\"\n\n[dictionary]\nkind = " +
           quoted(kind) + "\npath = " + quoted(kFix50Sp2()) + "\n" + extra_paths +
           "\n\n[[session]]\n"
           "sender_comp_id = \"ISLD\"\n"
           "target_comp_id = \"TW\"\n"
           "begin_string = \"FIXT.1.1\"\n"
           "default_appl_ver_id = \"9\"\n"
           "role = \"initiator\"\n"
           "heartbeat_interval = \"30s\"\n"
           "validate_inbound_messages = true\n" +
           session_lines +
           "\n[session.transport]\nkind = \"plaintext\"\nhost = \"127.0.0.1\"\nport = 1\n"
           "\n[session.security_profile]\nkind = \"insecure_plain_tcp\"\n";
}

std::string list_of(const std::vector<std::string>& paths) {
    std::string out = "extra_paths = [";
    for (const std::string& p : paths) {
        out += quoted(p) + ", ";
    }
    return out + "]";
}

// The io_context the bundle's clock is bound to, and the bundle, destroyed first.
struct Loaded {
    std::filesystem::path dir;
    asio::io_context ioc;
    fixpp::config::LoadResult result;

    explicit Loaded(const std::string& text, const std::string& overlay_xml = {})
        : dir(fixpp::test_support::unique_temp_dir("w14")) {
        if (!overlay_xml.empty()) {
            std::ofstream(dir / "overlay.xml") << overlay_xml;
        }
        std::ofstream(dir / "bundle.toml") << text;
        fixpp::config::LoadOptions opts;
        opts.engine_executor = ioc.get_executor();
        result = fixpp::config::load_toml_config(dir / "bundle.toml", opts);
    }
    Loaded(const Loaded&) = delete;
    Loaded& operator=(const Loaded&) = delete;
    Loaded(Loaded&&) = delete;
    Loaded& operator=(Loaded&&) = delete;
    ~Loaded() { fixpp::test_support::try_remove_temp_dir(dir); }
};

constexpr std::string_view kKey = "dictionary.extra_paths";

std::vector<LoadDiagnostic> at_key(const fixpp::config::LoadResult& r, std::string_view key) {
    std::vector<LoadDiagnostic> out;
    if (!r.has_value()) {
        std::ranges::copy_if(r.error(), std::back_inserter(out),
                             [&](const LoadDiagnostic& d) { return d.key_path == key; });
    }
    return out;
}

std::string all_diagnostics(const fixpp::config::LoadResult& r) {
    std::string s;
    if (!r.has_value()) {
        for (const LoadDiagnostic& d : r.error()) {
            s += d.key_path + ": " + d.message + "\n";
        }
    }
    return s;
}

std::vector<dd::session_version> versions_of(const fixpp::config::ConfigBundle& b) {
    std::vector<dd::session_version> out;
    out.reserve(b.engine.dictionaries.size());
    for (const auto& d : b.engine.dictionaries) {
        out.push_back(d->which_session_version());
    }
    return out;
}

// ── extra_paths reaches the engine ──────────────────────────────────────────────────────

TEST(W14ExtraPaths, ReachesTheEngineInOrder) {
    Loaded l{document(list_of({kFixt11(), kLatest()}))};
    ASSERT_TRUE(l.result.has_value()) << all_diagnostics(l.result);
    const std::vector<dd::session_version> expected = {
        dd::session_version::v50sp2, dd::session_version::vt11, dd::session_version::vlatest};
    EXPECT_EQ(versions_of(*l.result), expected);
}

TEST(W14ExtraPaths, FrontIsThePathFile_AndTheSessionsDefault) {
    Loaded l{document(list_of({kFixt11(), kLatest()}))};
    ASSERT_TRUE(l.result.has_value()) << all_diagnostics(l.result);
    ASSERT_FALSE(l.result->engine.dictionaries.empty());
    const auto& front = l.result->engine.dictionaries.front();
    EXPECT_EQ(front->which_session_version(), dd::session_version::v50sp2)
        << "the front is the path file";
    ASSERT_EQ(l.result->sessions.size(), 1U);
    EXPECT_EQ(l.result->sessions[0].config.dictionary, front);
}

TEST(W14ExtraPaths, TheSessionValidates_ARealEngineRegistersIt) {
    Loaded l{document(list_of({kFixt11(), kLatest()}))};
    ASSERT_TRUE(l.result.has_value()) << all_diagnostics(l.result);
    const SessionConfig cfg = l.result->sessions[0].config;
    EXPECT_TRUE(cfg.validate_inbound_messages);
    EngineProbe probe{l.result->engine.dictionaries};
    const auto registered = probe.engine->register_session(cfg);
    EXPECT_TRUE(registered.has_value());
}

TEST(W14ExtraPaths, ARepeatedFileIsLoadedAgain) {
    Loaded l{document(list_of({kFixt11(), kFixt11()}))};
    ASSERT_TRUE(l.result.has_value()) << all_diagnostics(l.result);
    ASSERT_EQ(l.result->engine.dictionaries.size(), 3U);
    EXPECT_NE(l.result->engine.dictionaries[1], l.result->engine.dictionaries[2]);
}

// ── Absent and empty: the path file alone ───────────────────────────────────────────────

TEST(W14Absent, OneEngineDictionary_ThePathFile) {
    Loaded l{document("")};
    ASSERT_TRUE(l.result.has_value()) << all_diagnostics(l.result);
    ASSERT_EQ(l.result->engine.dictionaries.size(), 1U);
    EXPECT_EQ(l.result->engine.dictionaries.front()->which_session_version(),
              dd::session_version::v50sp2);
    EXPECT_EQ(l.result->sessions[0].config.dictionary, l.result->engine.dictionaries.front());
}

TEST(W14Absent, AnEmptyListIsAnAbsentKey) {
    Loaded l{document("extra_paths = []")};
    ASSERT_TRUE(l.result.has_value()) << all_diagnostics(l.result);
    ASSERT_EQ(l.result->engine.dictionaries.size(), 1U);
    EXPECT_EQ(l.result->engine.dictionaries.front()->which_session_version(),
              dd::session_version::v50sp2);
}

// ── Diagnostics at the key ──────────────────────────────────────────────────────────────

TEST(W14Bad, AFileThatDoesNotLoad_NamesItsIndex) {
    Loaded l{document(list_of({kFixt11(), vendored("no_such_file.xml")}))};
    EXPECT_FALSE(l.result.has_value()) << "a bad path loads";
    const auto d = at_key(l.result, kKey);
    ASSERT_EQ(d.size(), 1U) << all_diagnostics(l.result);
    EXPECT_NE(d[0].message.find("[1]"), std::string::npos) << d[0].message;
}

TEST(W14Bad, NotAnArray) {
    Loaded l{document("extra_paths = " + quoted(kFixt11()))};
    EXPECT_FALSE(l.result.has_value()) << "a string in place of the list loads";
    EXPECT_EQ(at_key(l.result, kKey).size(), 1U) << all_diagnostics(l.result);
}

TEST(W14Bad, ANonStringEntry_NamesItsIndex) {
    Loaded l{document("extra_paths = [" + quoted(kFixt11()) + ", 7]")};
    EXPECT_FALSE(l.result.has_value()) << "a number in the list loads";
    const auto d = at_key(l.result, kKey);
    ASSERT_EQ(d.size(), 1U) << all_diagnostics(l.result);
    EXPECT_NE(d[0].message.find("[1]"), std::string::npos) << d[0].message;
}

TEST(W14Bad, AnEmptyEntry_NamesItsIndex) {
    Loaded l{document("extra_paths = [\"\", " + quoted(kFixt11()) + "]")};
    EXPECT_FALSE(l.result.has_value()) << "an empty entry loads";
    const auto d = at_key(l.result, kKey);
    ASSERT_EQ(d.size(), 1U) << all_diagnostics(l.result);
    EXPECT_NE(d[0].message.find("[0]"), std::string::npos) << d[0].message;
}

TEST(W14Bad, EachBadEntryAddsItsOwn) {
    Loaded l{document("extra_paths = [7, " + quoted(kFixt11()) + ", " +
                      quoted(vendored("no_such_file.xml")) + "]")};
    EXPECT_FALSE(l.result.has_value()) << "two bad entries load";
    const auto d = at_key(l.result, kKey);
    ASSERT_EQ(d.size(), 2U) << all_diagnostics(l.result);
    EXPECT_NE(d[0].message.find("[0]"), std::string::npos) << d[0].message;
    EXPECT_NE(d[1].message.find("[2]"), std::string::npos) << d[1].message;
}

// A kind other than `path` is refused at the kind, as before this feature, and the key is not read.
TEST(W14Bad, AnotherKindIsRefusedAtTheKind_TheKeyIsNotRead) {
    Loaded l{document("extra_paths = 7", {}, "version")};
    EXPECT_FALSE(l.result.has_value());
    EXPECT_EQ(at_key(l.result, "dictionary.kind").size(), 1U) << all_diagnostics(l.result);
    EXPECT_TRUE(at_key(l.result, kKey).empty()) << all_diagnostics(l.result);
}

// ── FR-018: the overlay ─────────────────────────────────────────────────────────────────

const std::string& kOverlay() {
    static const std::string value =
        "<fix type='FIX' major='5' minor='0' servicepack='2'>\n"
        " <messages>\n"
        "  <message name='NewOrderSingle' msgtype='D' msgcat='app'>\n"
        "   <field name='Custom099Field' required='N' />\n"
        "  </message>\n"
        " </messages>\n"
        " <fields>\n"
        "  <field number='5001' name='Custom099Field' type='STRING' />\n"
        " </fields>\n"
        "</fix>\n";
    return value;
}

// The session's own-version overlay survives, on a frame S answers. A non-own extra stays plain,
// so the overlay's field is refused on a frame routed to the extra.
TEST(W14Overlay, TheOwnVersionSurvives_ANonOwnExtraStaysPlain) {
    Loaded l{document(list_of({kFixt11(), kFix44()}), "dialect_overlay = \"overlay.xml\"\n"),
             kOverlay()};
    ASSERT_TRUE(l.result.has_value()) << all_diagnostics(l.result);
    const fixpp::config::ConfigBundle& b = *l.result;
    ASSERT_EQ(b.engine.dictionaries.size(), 3U);
    const DictPtr s = b.sessions[0].config.dictionary;
    ASSERT_NE(s, nullptr);
    EXPECT_NE(s, b.engine.dictionaries.front()) << "the session holds the merged dictionary";

    const Fixture fixture{.registry = make_registry(b.engine.dictionaries), .session_dict = s};
    const std::string custom = "5001=x\x01";
    {
        CellRun run = enter_arm(fixture, Arm::Active, {});
        ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
        run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), kNos() + custom));
        expect_ending(run, "- | stays | c | app=1 adm=0");
    }
    {
        CellRun run = enter_arm(fixture, Arm::Active, {});
        ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
        run.ending =
            run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), kNos() + custom, "1128=6\x01"));
        expect_ending(run, "R 2/5001 D 45=2 | stays | c | app=0 adm=0");
    }
}

// ── The refusal arm ─────────────────────────────────────────────────────────────────────

// load_toml_config builds no engine and runs no FIXT transport check.
TEST(W14Refusal, NoTransportDictionary_LoadsAndRegisterSessionRefuses) {
    Loaded l{document(list_of({kLatest()}))};
    ASSERT_TRUE(l.result.has_value()) << all_diagnostics(l.result);
    EngineProbe probe{l.result->engine.dictionaries};
    const auto registered = probe.engine->register_session(l.result->sessions[0].config);
    ASSERT_FALSE(registered.has_value()) << "registered with no transport dictionary";
    EXPECT_EQ(registered.error(), fixpp::core::error::invalid_session_config);
}

}  // namespace
}  // namespace fixpp::session::test099
