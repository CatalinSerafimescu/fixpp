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

#include <algorithm>
#include <asio/io_context.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fixpp/config/config_bundle.hpp>
#include <fixpp/config/load_diagnostic.hpp>
#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/session_config.hpp>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#include "session/test_099_fixt_support.hpp"
#include "session/test_106_support.hpp"
#include "support/hooked_store.hpp"
#include "support/memory_store_test_access.hpp"
#include "support/seqnum_manager_test_access.hpp"
#include "support/session_test_access.hpp"

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
// Exact equality, not a window, so a changed default cannot pass inside one.
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
// defaults. Exact equality, not a timing window, so a changed default cannot pass inside one
// (a cell that reads the bound by timing accepts a window around it).
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

// ── T058: NextNumIn106.CommitRefused_Disconnected (research D9 row "FR-006 commit", N-4) ───────
// [seam: set_commit_freeze_hook] The manager is frozen between the classification and the commit,
// so the commit is refused: the arm ends Disconnected once and silently, nothing is written after
// the refusal, the arm never reaches LogonReceived, and NextNumIn is unconsumed in memory and in
// the store. Its mutant: the commit's result ignored (tasks.md T069).

TEST(NextNumIn106, CommitRefused_Disconnected) {
    namespace t99 = fixpp::session::test099;
    using fixpp::session::fsm_state;
    using fixpp::session::memory_store_test_access;
    using fixpp::session::seqnum_manager_test_access;
    using fixpp::session::session_test_access;
    auto store = std::make_shared<fixpp::test_support::HookedStoreFactory>();
    t99::RigOptions opts;
    opts.validate = true;
    opts.role = fixpp::session::session_role::acceptor;
    opts.tweak = [store](fixpp::session::SessionConfig& c) {
        c.store_factory = store;
        c.reset_on_disconnect = false;
    };
    t99::Rig rig{t99::shape_fixture(t99::Shape::T), std::move(opts)};
    auto const opened = rig.open();
    ASSERT_FALSE(opened.has_value()) << *opened;
    auto& mgr = session_test_access::seqnum_mgr(*rig.sess);
    bool hook_ran = false;
    session_test_access::set_commit_freeze_hook(*rig.sess, [&mgr, &hook_ran] {
        hook_ran = true;
        seqnum_manager_test_access::set_frozen(mgr, true);
    });
    auto const in_store = [&store] {
        return memory_store_test_access::counter(*store->log->inner,
                                                 fixpp::session::direction_t::inbound);
    };
    auto const written_before = rig.out.size();
    auto const disconnected_before = fixpp::test_support::t106::count_disconnected(*rig.sess);
    auto const store_in_before = in_store();
    std::string err;
    rig.feed(t99::input("Logon 1137=9").make(rig.peer_ctx(1)), &err);
    seqnum_manager_test_access::set_frozen(mgr, false);
    session_test_access::set_commit_freeze_hook(*rig.sess, nullptr);

    ASSERT_TRUE(hook_ran) << "the path was not exercised: the commit point was not reached";
    EXPECT_EQ(err, "") << "the refused commit's return";
    EXPECT_EQ(rig.sess->state(), fsm_state::Disconnected) << t99::state_name(rig.sess->state());
    EXPECT_EQ(fixpp::test_support::t106::count_disconnected(*rig.sess) - disconnected_before, 1U)
        << "new Disconnected entries";
    auto const history = rig.sess->fsm_visit_history();
    EXPECT_EQ(std::count(history.begin(), history.end(), fsm_state::LogonReceived), 0)
        << "the arm went past its commit";
    EXPECT_EQ(rig.out.size(), written_before) << "frames written after the refused commit";
    EXPECT_EQ(mgr.next_inbound_unsafe(), 1U) << "NextNumIn in memory";
    EXPECT_EQ(in_store(), store_in_before) << "NextNumIn in the store";
}

// ── NextNumIn106.Acceptor_ResetOnLogon_S3_InMemoryOne (the Phase 4 report's ruling) ────────────
// [seam: M6a] A head-only pin beside the guard
// NextNumIn106.Acceptor_ResetOnLogon_S3_KeepsTheReset (test_106_identity_first.cpp), which keeps
// what the base also reads: with reset_on_logon on an acceptor whose store holds NextNumIn 5, the
// knob's reset runs before the classification, the Logon at MsgSeqNum 1 is refused at S3, and the
// in-memory NextNumIn is exactly 1 after the refusal: the reset's value, unconsumed (106 D2). The
// store holds a value other than 1 before the feed, so a NextNumIn of 1 after it is the reset's,
// not the hydrated one. Its mutant: M6a (the acceptor commits the advance at the classification).
TEST(NextNumIn106, Acceptor_ResetOnLogon_S3_InMemoryOne) {
    namespace t99 = fixpp::session::test099;
    using fixpp::session::direction_t;
    using fixpp::session::fsm_state;
    using fixpp::session::memory_store_test_access;
    using fixpp::session::session_test_access;
    constexpr fixpp::session::seqnum_t kStoredIn = 5;
    auto inner = std::make_shared<fixpp::session::MemoryStore>(
        fixpp::session::MemoryStore::Config{.policy = fixpp::session::capacity_policy::unbounded});
    memory_store_test_access::set_counter(*inner, direction_t::inbound, kStoredIn);
    memory_store_test_access::set_counter(*inner, direction_t::outbound, 1);
    auto store = std::make_shared<fixpp::test_support::HookedStoreFactory>();
    store->log->inner = inner;
    t99::RigOptions opts;
    opts.validate = true;
    opts.role = fixpp::session::session_role::acceptor;
    opts.tweak = [store](fixpp::session::SessionConfig& c) {
        c.store_factory = store;
        c.reset_on_logon = true;
        c.reset_on_disconnect = false;
    };
    t99::Rig rig{t99::shape_fixture(t99::Shape::T), std::move(opts)};
    auto const opened = rig.open();
    ASSERT_FALSE(opened.has_value()) << *opened;
    auto const resets_before = store->log->calls.size();
    std::string err;
    rig.feed(t99::input("Logon 1137=99").make(rig.peer_ctx(1)), &err);

    EXPECT_GT(store->log->calls.size(), resets_before) << "store reset calls";
    EXPECT_EQ(memory_store_test_access::counter(*inner, direction_t::inbound), 1U)
        << "NextNumIn in the store";
    EXPECT_EQ(session_test_access::seqnum_mgr(*rig.sess).next_inbound_unsafe(), 1U)
        << "NextNumIn in memory";
    EXPECT_EQ(rig.sess->state(), fsm_state::Disconnected) << t99::state_name(rig.sess->state());
}
