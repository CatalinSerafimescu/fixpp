// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/support/toml_106_bundle.hpp — 106-logon-guards-write-bounds: the TOML cells' one-session
// bundle template and the load of a TOML text through a temporary file, shared by
// tests/config/test_load_negative_battery.cpp, tests/session/test_106_seams.cpp and
// tests/session/test_106_handshake.cpp.
//
// CONDITIONS this header keeps, checked by reading:
//   - it names no production symbol 106 adds, so it compiles on the base;
//   - a temporary file's name holds the process id, the running test's name and a per-process
//     counter, so two processes that run the same cell at once (two build trees' ctest, a sharded
//     run) write different files;
//   - the includer's target defines FIXPP_CONFIG_FIXTURE_DIR (the config suite's fixture
//     directory), which the template names absolutely.
#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/io_context.hpp>
#include <atomic>
#include <filesystem>
#include <fixpp/config/load_diagnostic.hpp>
#include <fixpp/config/toml_config_loader.hpp>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

#include "temp_dir.hpp"  // current_pid()

#ifndef FIXPP_CONFIG_FIXTURE_DIR
#error "FIXPP_CONFIG_FIXTURE_DIR must be set by CMake"
#endif

namespace fixpp::test_support::toml106 {

struct Parts {
    std::string root;       // root keys, before the first table
    std::string defaults;   // the [default] table's keys (no table when empty)
    std::string session;    // extra [[session]] keys
    std::string transport;  // extra [session.transport] keys
};

inline Parts in_root(std::string s) {
    Parts p;
    p.root = std::move(s);
    return p;
}
inline Parts in_defaults(std::string s) {
    Parts p;
    p.defaults = std::move(s);
    return p;
}
inline Parts in_session(std::string s) {
    Parts p;
    p.session = std::move(s);
    return p;
}
inline Parts in_transport(std::string s) {
    Parts p;
    p.transport = std::move(s);
    return p;
}

// One initiator [[session]] over TLS with the mtls_ca profile, the config suite's file cert
// source and FIX 4.4 dictionary, a system clock and a memory store; p's keys go into their tables.
inline std::string bundle(Parts const& p) {
    std::string const dir =
        std::filesystem::path{std::string{FIXPP_CONFIG_FIXTURE_DIR}}.generic_string();
    std::string t = p.root + "\n";
    t += "[clock]\nkind = \"system\"\n\n[store]\nkind = \"memory\"\n\n";
    t += "[cert_source]\nkind = \"file\"\ncert_file = '" + dir + "/leaf_ecdsa_p256.pem'\n";
    t += "key_file = '" + dir + "/leaf_ecdsa_p256.key'\nca_file = '" + dir + "/ca.pem'\n\n";
    t += "[dictionary]\nkind = \"path\"\npath = '" + dir + "/FIX44.xml'\n\n";
    if (!p.defaults.empty()) t += "[default]\n" + p.defaults + "\n\n";
    t += "[[session]]\nsender_comp_id = \"CLIENT1\"\ntarget_comp_id = \"SERVER1\"\n";
    t += "begin_string = \"FIX.4.4\"\nrole = \"initiator\"\n" + p.session + "\n\n";
    t += "[session.transport]\nkind = \"tls\"\nhost = \"fix.example.com\"\nport = 4321\n";
    t += p.transport + "\n\n[session.security_profile]\nkind = \"mtls_ca\"\n";
    return t;
}

// Loads text through a temporary file, removed before return. The bundle's clock is bound to
// opts.engine_executor's context, which must outlive every use of the clock.
inline fixpp::config::LoadResult load_text(std::string const& text,
                                           fixpp::config::LoadOptions const& opts) {
    static std::atomic<unsigned> n{0};
    auto const* info = ::testing::UnitTest::GetInstance()->current_test_info();
    std::string cell = info ? info->name() : "cell";
    std::ranges::replace(cell, '/', '_');  // a parameterised cell's name holds a '/'
    std::string const name = "fixpp_106_toml_" + std::to_string(current_pid()) + "_" + cell + "_" +
                             std::to_string(n.fetch_add(1)) + ".toml";
    auto const path = std::filesystem::temp_directory_path() / name;
    {
        std::ofstream out{path, std::ios::binary | std::ios::trunc};
        out << text;
    }
    auto result = fixpp::config::load_toml_config(path, opts);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return result;
}

// The same, for a bundle whose clock no cell uses (bound to a context that ends with the call).
inline fixpp::config::LoadResult load_text(std::string const& text) {
    asio::io_context ctx;
    fixpp::config::LoadOptions opts;
    opts.engine_executor = ctx.get_executor();
    return load_text(text, opts);
}

// A refused load's diagnostics, for a failure message; " (the bundle loaded)" when it loaded.
inline std::string seen(fixpp::config::LoadResult const& r) {
    if (r.has_value()) return " (the bundle loaded)";
    std::string s;
    for (auto const& d : r.error()) s += " [" + d.key_path + ": " + d.message + "]";
    return s;
}

}  // namespace fixpp::test_support::toml106
