// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_106_seams.cpp — 106-logon-guards-write-bounds (tasks.md T003).
//
// Every cell that names a symbol 106 adds (tasks.md, Execution rules, "[seam: Mn] cells"): the
// base build lacks the symbol, so each cell's RED is read under its named mutant at the
// implementation head, in the mutant tree.
//
// The ctest entry registers this file whether or not it holds cells yet, so it compiles on its
// own; each cell task of tasks.md appends its cells here.

#include <gtest/gtest.h>

#include <asio/io_context.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fixpp/config/config_bundle.hpp>
#include <fixpp/config/load_diagnostic.hpp>
#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/session/session_config.hpp>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

#ifndef FIXPP_CONFIG_FIXTURE_DIR
#error "FIXPP_CONFIG_FIXTURE_DIR must be set by CMake"
#endif

using namespace std::chrono_literals;

// ── TOML: write_timeout_ms and the acceptor key's default (tasks.md T026) ─────────────────────
// A bundle built from one template and loaded through a temporary file; the template's relative
// paths name the config suite's fixture directory absolutely.

namespace {

struct Toml106Parts {
    std::string defaults;  // the [default] table's keys (no table when empty)
    std::string session;   // extra [[session]] keys
};

Toml106Parts in_defaults(std::string s) {
    Toml106Parts p;
    p.defaults = std::move(s);
    return p;
}
Toml106Parts in_session(std::string s) {
    Toml106Parts p;
    p.session = std::move(s);
    return p;
}

std::string toml_106(Toml106Parts const& p) {
    std::string const dir =
        std::filesystem::path{std::string{FIXPP_CONFIG_FIXTURE_DIR}}.generic_string();
    std::string t = "[clock]\nkind = \"system\"\n\n[store]\nkind = \"memory\"\n\n";
    t += "[cert_source]\nkind = \"file\"\ncert_file = '" + dir + "/leaf_ecdsa_p256.pem'\n";
    t += "key_file = '" + dir + "/leaf_ecdsa_p256.key'\nca_file = '" + dir + "/ca.pem'\n\n";
    t += "[dictionary]\nkind = \"path\"\npath = '" + dir + "/FIX44.xml'\n\n";
    if (!p.defaults.empty()) t += "[default]\n" + p.defaults + "\n\n";
    t += "[[session]]\nsender_comp_id = \"CLIENT1\"\ntarget_comp_id = \"SERVER1\"\n";
    t += "begin_string = \"FIX.4.4\"\nrole = \"initiator\"\n" + p.session + "\n\n";
    t += "[session.transport]\nkind = \"tls\"\nhost = \"fix.example.com\"\nport = 4321\n\n";
    t += "[session.security_profile]\nkind = \"mtls_ca\"\n";
    return t;
}

fixpp::config::LoadResult load_text_106(std::string const& text) {
    static std::atomic<unsigned> n{0};
    auto const* info = ::testing::UnitTest::GetInstance()->current_test_info();
    std::string const name = std::string{"fixpp_106_seam_"} + (info ? info->name() : "cell") + "_" +
                             std::to_string(n.fetch_add(1)) + ".toml";
    auto const path = std::filesystem::temp_directory_path() / name;
    {
        std::ofstream out{path, std::ios::binary | std::ios::trunc};
        out << text;
    }
    asio::io_context ctx;
    fixpp::config::LoadOptions opts;
    opts.engine_executor = ctx.get_executor();
    auto result = fixpp::config::load_toml_config(path, opts);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return result;
}

std::string seen_106(fixpp::config::LoadResult const& r) {
    std::string seen;
    if (r.has_value()) return {};
    for (auto const& d : r.error()) seen += " [" + d.key_path + ": " + d.message + "]";
    return seen;
}

}  // namespace

// [seam: M11] The resolved SessionConfig::write_timeout_ms equals the key, set in a [[session]]
// table and, separately, in [default] (contract C-4.7). The two values differ from each other and
// from 0, so a mapper that drops the value, or reads one scope only, reads another number.
TEST(TomlWriteTimeout106, Set) {
    {
        auto const r = load_text_106(toml_106(in_session("write_timeout_ms = 300")));
        ASSERT_TRUE(r.has_value()) << "the [[session]] bundle did not load:" << seen_106(r);
        ASSERT_EQ(r->sessions.size(), 1U);
        EXPECT_EQ(r->sessions[0].config.write_timeout_ms, std::uint32_t{300})
            << "write_timeout_ms set in [[session]]";
    }
    {
        auto const r = load_text_106(toml_106(in_defaults("write_timeout_ms = 4294967295")));
        ASSERT_TRUE(r.has_value()) << "the [default] bundle did not load:" << seen_106(r);
        ASSERT_EQ(r->sessions.size(), 1U);
        EXPECT_EQ(r->sessions[0].config.write_timeout_ms, std::uint32_t{4294967295U})
            << "write_timeout_ms set in [default]";
    }
}

// [seam: M11'] No key: the limit is off, in the loaded bundle and in a default SessionConfig.
// Exact equality: no timing cell loads the key absent.
TEST(TomlWriteTimeout106, KeyAbsent_Off) {
    auto const r = load_text_106(toml_106({}));
    ASSERT_TRUE(r.has_value()) << "the bundle did not load:" << seen_106(r);
    ASSERT_EQ(r->sessions.size(), 1U);
    EXPECT_EQ(r->sessions[0].config.write_timeout_ms, std::uint32_t{0}) << "the loaded bundle";
    EXPECT_EQ(fixpp::session::SessionConfig{}.write_timeout_ms, std::uint32_t{0})
        << "a default SessionConfig";
}

// [seam: M7''''] No acceptor_tls_handshake_timeout_ms key: 1500 ms in the bundle, in the
// EngineConfig a host copies from it (as it copies the clock, contract C-2), and in both structs'
// defaults. Exact equality: EngineFirstFramePhaseATls.Q17_* builds EngineConfig directly and
// accepts a close in a window, and no other cell loads the key absent.
TEST(TomlHandshake106, AcceptorKeyAbsent_Default1500) {
    auto const r = load_text_106(toml_106({}));
    ASSERT_TRUE(r.has_value()) << "the bundle did not load:" << seen_106(r);
    EXPECT_EQ(r->engine.acceptor_tls_handshake_timeout, 1500ms) << "the loaded bundle";
    fixpp::core::EngineConfig ec;
    ec.clock = r->engine.clock;
    ec.acceptor_tls_handshake_timeout = r->engine.acceptor_tls_handshake_timeout;
    EXPECT_EQ(ec.acceptor_tls_handshake_timeout, 1500ms) << "the EngineConfig copied from it";
    EXPECT_EQ(fixpp::core::EngineConfig{}.acceptor_tls_handshake_timeout, 1500ms)
        << "a default EngineConfig";
    EXPECT_EQ(fixpp::config::EngineEstablishment{}.acceptor_tls_handshake_timeout, 1500ms)
        << "a default EngineEstablishment";
}
