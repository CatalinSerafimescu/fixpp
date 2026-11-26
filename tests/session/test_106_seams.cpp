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
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fixpp/config/config_bundle.hpp>
#include <fixpp/config/load_diagnostic.hpp>
#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/session_config.hpp>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "session/test_099_fixt_support.hpp"
#include "session/test_106_support.hpp"
#include "support/extract_tag.hpp"
#include "support/hooked_store.hpp"
#include "support/memory_store_test_access.hpp"
#include "support/seqnum_manager_test_access.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"
#include "support/validation_test_dictionary.hpp"

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
// in-memory NextNumIn is exactly 1 after the refusal: the reset's value, unconsumed (106 D2). An
// acceptor that never hydrated also reads 1 in memory, so the cell shows that the reset ran by its
// store-reset-call and store-counter assertions (the store holds 5 before the feed). Its mutant:
// M6a (the acceptor commits the advance at the classification).
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

// ── T076: ConnectionBound106 (research D9 row C-4; data-model E-3) ─────────────────────────────
// [seam] The connection bound's cells whose rig needs no write limit, on t106::GiveUpRig (H = T =
// 10 s, B = 500 ms, a direct Session on a BlockingWriteTransport).
// ConnectionBound106.Teardown_Close_* and Teardown_EngineStop_* are covered by the Disconnected
// edge (data-model E-3's joiner table) and not written.

namespace {

namespace t106s = fixpp::test_support::t106;
using fixpp::session::session_role;

// open(), the connection, then the peer's Logon at 1, as t105::DirectRig::to_active() does, without
// its Active check (a cell whose bound fails at the loop's start ends at Active).
[[nodiscard]] bool connect_and_logon(t106s::GiveUpRig& r) {
    auto& s = r.d.s;
    auto const opened = r.d.run(s.session->open(), "open");
    if (!opened || !opened->has_value()) return false;
    if (r.d.role == session_role::initiator) {
        auto const c = r.d.run(s.session->drive_reconnect(), "drive_reconnect");
        if (!c || !c->has_value()) return false;
    } else {
        s.session->attach_accepted_transport(
            std::make_unique<fixpp::session::test::BlockingWriteTransport>(s.ioc.get_executor(),
                                                                           r.d.stream, r.d.bw),
            fixpp::transport::handshake_result{});
    }
    (void)s.feed(s.logon(1));
    return true;
}

class ConnectionBound106 : public ::testing::TestWithParam<session_role> {};

// The spawn refused at the loop's start fails closed: the loop takes the claim, Disconnected once,
// the transport closed, no give-up record, no Logout.
TEST_P(ConnectionBound106, SpawnFails_AtLoop) {
    t106s::GiveUpRig r{GetParam(), t106s::GiveUpParams{}};
    fixpp::session::session_test_access::set_connection_bound_spawn_fails(r.session(),
                                                                          [] { return true; });
    ASSERT_TRUE(connect_and_logon(r)) << "setup";
    r.d.s.drain();
    EXPECT_NE(fixpp::session::session_test_access::ending_claim(r.session()), 0) << "no claim";
    EXPECT_EQ(r.d.s.state(), fixpp::session::fsm_state::Disconnected);
    EXPECT_EQ(r.disconnected(), 1U) << "Disconnected entries";
    EXPECT_TRUE(r.d.closed()) << "the transport is open";
    EXPECT_EQ(r.records(), 0U) << "give-up records";
    EXPECT_EQ(r.completed_since("5", 0), 0U) << "a Logout";
}

// The bound's wait throws with the claim free: the bound takes it, Disconnected once, one close;
// no record, no Logout.
TEST_P(ConnectionBound106, WaitThrows) {
    t106s::GiveUpRig r{GetParam(), t106s::GiveUpParams{}};
    fixpp::session::session_test_access::set_connection_bound_wait_throws(r.session(),
                                                                          [] { return true; });
    ASSERT_TRUE(connect_and_logon(r)) << "setup";
    r.d.s.drain();
    EXPECT_NE(fixpp::session::session_test_access::ending_claim(r.session()), 0) << "no claim";
    EXPECT_EQ(r.d.s.state(), fixpp::session::fsm_state::Disconnected);
    EXPECT_EQ(r.disconnected(), 1U) << "Disconnected entries";
    EXPECT_TRUE(r.d.closed()) << "the transport is open";
    EXPECT_EQ(r.records(), 0U) << "give-up records";
    EXPECT_EQ(r.completed_since("5", 0), 0U) << "a Logout";
}

// A wake with nothing due ends nothing: the bound is still counted, the session Active, no claim.
TEST_P(ConnectionBound106, SpuriousWake_NoEnding) {
    t106s::GiveUpRig r{GetParam(), t106s::GiveUpParams{}};
    ASSERT_TRUE(r.d.to_active()) << "setup";
    ASSERT_EQ(fixpp::session::session_test_access::connection_bound_count(r.session()), 1)
        << "setup: the bound is not running";
    fixpp::session::session_test_access::wake_connection_bound(r.session());
    r.d.s.drain();
    EXPECT_EQ(fixpp::session::session_test_access::connection_bound_count(r.session()), 1)
        << "the wake ended the bound";
    EXPECT_EQ(r.d.s.state(), fixpp::session::fsm_state::Active);
    EXPECT_EQ(fixpp::session::session_test_access::ending_claim(r.session()), 0) << "a claim";
}

// The connection's end (the peer's Logout: the reply Logout, then Disconnected, with no close(),
// drain or stop) ends a parked bound: its counter reads 0 after a bounded pump.
TEST_P(ConnectionBound106, Teardown_ConnectionEnd_Parked) {
    t106s::GiveUpRig r{GetParam(), t106s::GiveUpParams{}};
    ASSERT_TRUE(r.d.to_active()) << "setup";
    ASSERT_EQ(fixpp::session::session_test_access::connection_bound_count(r.session()), 1)
        << "setup: the bound is not running";
    (void)r.d.s.feed(r.d.s.logout(2));
    ASSERT_EQ(r.d.s.state(), fixpp::session::fsm_state::Disconnected) << "setup";
    EXPECT_TRUE(r.d.s.pump_until(
        [&] {
            return fixpp::session::session_test_access::connection_bound_count(r.session()) == 0;
        },
        std::chrono::seconds{2}))
        << "the bound outlived the connection's end";
}

INSTANTIATE_TEST_SUITE_P(BothRoles, ConnectionBound106,
                         ::testing::Values(session_role::acceptor, session_role::initiator),
                         [](::testing::TestParamInfo<session_role> const& i) {
                             return i.param == session_role::acceptor ? "Acceptor" : "Initiator";
                         });

}  // namespace

// ── T100 to T103 and T106: the write limit (research D5; D9 rows FR-010, FR-011, C-4) ─────────
// [seam] Every cell here names SessionConfig::write_timeout_ms. A direct Session (t105::DirectRig:
// a BlockingWriteTransport whose next write can be held, the mock clock, both roles where the
// class allows), HeartBtInt 1 s, logout_disconnect_timeout_ms B = 500 ms unless named, and a
// LogCapture. S is the held write's start (the mock clock does not move while a cell pumps), W
// write_timeout_ms. Every instant is reached with step_to and a drain.
//
// Inequalities (research D9 rows FR-010 and "FR-010 spurious"): W = 300 ms; the cut at S + W
// precedes every competing instant of the cell. In Active, L is the instant of the last frame the
// peer sent, at most S, and S is at most L + 1 s; the TestRequest instant L + T (T = 1.2 s) can
// only queue a TestRequest behind the held write, and the bound's give-up row, L + 3T + 1 ns
// (L + 1.5 s + 1 ns in the TestRequest class, T = 500 ms), follows S + W. The spurious arm (the
// same staging with the limit off) reads at S + 1.3 s: an emit queued behind the held write
// moves its give-up to L + 3T + 1 ns, and a give-up decided there is closed only at its
// + B, so nothing closes by S + 1.3 s (in the TestRequest class G = L + 1.5 s + 1 ns, its close at
// G + B = L + 2 s, after S + 1.3 s = L + 1.8 s).

namespace {

namespace w106 {

using namespace std::chrono_literals;
using fixpp::session::fsm_state;
using fixpp::session::session_role;
using fixpp::session::session_test_access;
using fixpp::session::SessionConfig;
namespace t105 = fixpp::test_support::t105;
namespace t106 = fixpp::test_support::t106;
namespace rgs = fixpp::test_support::resend_gap;
using expected_void = fixpp::core::expected_t<void>;
using ns = std::chrono::nanoseconds;

constexpr std::uint32_t kW = 300;

// Counts onLogout; runs a hook from toAdmin with the frame's MsgType(35); optionally refuses one
// administrative MsgType in fromAdmin (on_inbound_active_from_admin_failed_'s two-argument
// Reject, SessionRejectReason 3) and every application message in fromApp (the
// BusinessMessageReject).
class App final : public rgs::GapApp {
public:
    int logouts = 0;
    std::string refuse_admin_type;  // empty: fromAdmin accepts every administrative message
    bool refuse_app = false;
    std::function<void(std::string_view)> to_admin_hook;

    void onLogout(const fixpp::session::SessionId& /*id*/) override { ++logouts; }
    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
                 const fixpp::session::SessionId& id) override {
        rgs::GapApp::toAdmin(msg, id);
        if (to_admin_hook) to_admin_hook(msg.msg_type());
    }
    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
        const fixpp::session::SessionId& /*id*/) override {
        if (!refuse_admin_type.empty() && msg.msg_type() == refuse_admin_type) {
            return std::unexpected(fixpp::core::error::app_do_not_send);
        }
        return {};
    }
    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
        const fixpp::session::SessionId& id) override {
        if (refuse_app) return std::unexpected(fixpp::core::error::app_do_not_send);
        return rgs::GapApp::fromApp(msg, id);
    }
};

struct Params {
    std::uint32_t write_ms = kW;
    std::chrono::seconds heartbeat{1};
    std::optional<std::chrono::milliseconds> threshold;
    std::uint32_t logout_ms = 500;
};

class Rig {
public:
    t106::LogCapture log;
    std::shared_ptr<App> app = std::make_shared<App>();
    t105::DirectRig d;

    Rig(session_role role, Params p, std::function<void(SessionConfig&)> const& tweak = {})
        : d{role, [this, p, &tweak](SessionConfig& c) {
                c.heartbeat_interval = p.heartbeat;
                if (p.threshold) c.test_request_threshold = *p.threshold;
                c.logout_disconnect_timeout_ms = p.logout_ms;
                c.write_timeout_ms = p.write_ms;
                c.logger_override = log.logger;
                if (tweak) tweak(c);
            }} {
        d.s.engine.application = app;
        d.s.session = std::make_unique<fixpp::session::Session>(d.s.engine, d.s.cfg);
    }
    Rig(Rig const&) = delete;
    Rig& operator=(Rig const&) = delete;
    ~Rig() {
        d.release();
        d.s.drain();
    }

    fixpp::session::Session& session() { return *d.s.session; }
    [[nodiscard]] t106::steady_time_point now() const { return d.s.mock->steady_now(); }
    [[nodiscard]] t106::steady_time_point last_inbound() const {
        return session_test_access::last_inbound_steady(*d.s.session);
    }
    void at(t106::steady_time_point t) {
        d.s.mock->step_to(t);
        d.s.drain();
    }
    // The write of the next frame of MsgType type is held: armed at its toAdmin.
    void hold_write_of(std::string type) {
        app->to_admin_hook = [b = d.bw.get(), t = std::move(type),
                              armed = false](std::string_view m) mutable {
            if (!armed && m == t) {
                armed = true;
                b->arm_block();
            }
        };
    }
    [[nodiscard]] std::size_t write_records() {
        return t106::records_now(log, FIXPP_FORMAT_ID(t106::kWriteTimeoutRecordFormat));
    }
    [[nodiscard]] std::size_t give_up_records() { return t106::give_up_records_now(log); }
    [[nodiscard]] std::size_t disconnected() const {
        return t106::count_disconnected(*d.s.session);
    }
    // An application send, started and not awaited.
    std::future<expected_void> spawn_send(std::string_view clordid) {
        payloads_.push_back(
            "35=D\x01"
            "11=" +
            std::string{clordid} + "\x01");
        auto const& pl = payloads_.back();
        return asio::co_spawn(fixpp::test_support::session_executor_of(*d.s.session, d.s.ioc),
                              d.s.session->send(std::as_bytes(std::span{pl.data(), pl.size()})),
                              asio::use_future);
    }
    template <class Awaitable>
    std::future<expected_void> spawn(Awaitable a) {
        return asio::co_spawn(fixpp::test_support::session_executor_of(*d.s.session, d.s.ioc),
                              std::move(a), asio::use_future);
    }

private:
    std::deque<std::string> payloads_;
};

enum class wclass : std::uint8_t {
    send,
    heartbeat,
    test_request,
    resend_frame,
    sequence_reset,
    reject,
    resend_request,
    heartbeat_reply,
    business_reject,
    initiator_logon,
    reply_logon,
    honor789_frame,
};

[[nodiscard]] const char* class_name(wclass c) {
    switch (c) {
        case wclass::send:
            return "Send";
        case wclass::heartbeat:
            return "Heartbeat";
        case wclass::test_request:
            return "TestRequest";
        case wclass::resend_frame:
            return "ResendFrame";
        case wclass::sequence_reset:
            return "SequenceReset";
        case wclass::reject:
            return "Reject";
        case wclass::resend_request:
            return "ResendRequest";
        case wclass::heartbeat_reply:
            return "HeartbeatReply";
        case wclass::business_reject:
            return "BusinessReject";
        case wclass::initiator_logon:
            return "InitiatorLogon";
        case wclass::reply_logon:
            return "ReplyLogon";
        case wclass::honor789_frame:
            return "Honor789Frame";
    }
    return "?";
}

[[nodiscard]] Params params_for(wclass c, std::uint32_t write_ms) {
    Params p;
    p.write_ms = write_ms;
    if (c == wclass::test_request) p.threshold = 500ms;
    return p;
}

void tweak_for(wclass c, SessionConfig& cfg) {
    if (c == wclass::honor789_frame) cfg.enable_next_expected_msg_seq_num = true;
    if (c == wclass::resend_frame || c == wclass::honor789_frame) {
        cfg.store_factory = std::make_shared<fixpp::session::MemoryStoreFactory>(
            fixpp::session::MemoryStore::Config{.policy =
                                                    fixpp::session::capacity_policy::unbounded});
    }
}

struct Staged {
    bool ok = false;
    std::string why;
    t106::steady_time_point s{};                       // the held write's start
    std::optional<std::future<expected_void>> cut;     // the call the cut write returns through
    std::optional<std::future<expected_void>> queued;  // a send queued behind it at the gate
    std::string type;                                  // the held write's MsgType(35)
    bool active = true;
};

[[nodiscard]] bool parked_on(Rig& r, std::string_view type) {
    if (!r.d.parked() || r.d.bw->recorded.empty()) return false;
    return fixpp::test_support::extract_tag(r.d.bw->recorded.back().bytes, 35) == type;
}

// Brings the session to the class's held write; the mock clock stays at S when it returns.
Staged stage(Rig& r, wclass c) {
    Staged st;
    auto& sc = r.d.s;
    if (c == wclass::reject) r.app->refuse_admin_type = "0";
    if (c == wclass::business_reject) r.app->refuse_app = true;
    if (c == wclass::initiator_logon) {
        st.active = false;
        st.type = "A";
        auto const opened = r.d.run(sc.session->open(), "open");
        if (!opened || !opened->has_value()) {
            st.why = "open() failed";
            return st;
        }
        r.d.hold_next_write();
        st.cut.emplace(r.spawn(sc.session->drive_reconnect()));
        if (!sc.pump_until([&] { return parked_on(r, "A"); })) {
            st.why = "the Logon's write was not held";
            return st;
        }
        st.s = r.now();
        st.ok = true;
        return st;
    }
    // The pre-Active classes on the direct rig (proposed substitutes: research D9 names an Engine
    // rig, whose acceptor's reply Logon no seam can hold; on a direct Session no establishment
    // deadline runs, so none competes with the cut).
    if (c == wclass::reply_logon || c == wclass::honor789_frame) {
        st.active = false;
        auto const opened = r.d.run(sc.session->open(), "open");
        if (!opened || !opened->has_value()) {
            st.why = "open() failed";
            return st;
        }
        if (c == wclass::reply_logon) {
            sc.session->attach_accepted_transport(
                std::make_unique<fixpp::session::test::BlockingWriteTransport>(
                    sc.ioc.get_executor(), r.d.stream, r.d.bw),
                fixpp::transport::handshake_result{});
            r.d.hold_next_write();
            st.type = "A";
            st.cut.emplace(sc.feed_spawn(sc.logon(1)));
        } else {
            auto const c2 = r.d.run(sc.session->drive_reconnect(), "drive_reconnect");
            if (!c2 || !c2->has_value()) {
                st.why = "drive_reconnect() failed";
                return st;
            }
            // The acknowledgement's NextExpectedMsgSeqNum(789) 1 is below the next outbound
            // number, so the honour resends from 1: the Logon's GapFill.
            r.hold_write_of("4");
            st.type = "4";
            st.cut.emplace(sc.feed_spawn(sc.frame("A", 1,
                                                  "98=0\x01"
                                                  "108=30\x01"
                                                  "789=1\x01")));
        }
        if (!sc.pump_until([&] { return parked_on(r, st.type); })) {
            st.why = std::string{"the "} + class_name(c) + " write was not held";
            return st;
        }
        st.s = r.now();
        st.ok = true;
        return st;
    }
    if (!r.d.to_active()) {
        st.why = "not Active";
        return st;
    }
    auto const t0 = r.now();
    auto const held_feed = [&](std::vector<std::byte> f, std::string type) {
        st.type = std::move(type);
        st.cut.emplace(sc.feed_spawn(std::move(f)));
        return sc.pump_until([&] { return parked_on(r, st.type); });
    };
    bool held = false;
    switch (c) {
        case wclass::send:
            r.d.hold_next_write();
            st.type = "D";
            st.cut.emplace(r.spawn_send("W106A"));
            held = sc.pump_until([&] { return parked_on(r, "D"); });
            break;
        case wclass::heartbeat:
            r.hold_write_of("0");
            st.type = "0";
            r.at(t0 + 1s);
            held = parked_on(r, "0");
            break;
        case wclass::test_request:
            r.hold_write_of("1");
            st.type = "1";
            r.at(r.last_inbound() + 500ms);
            held = parked_on(r, "1");
            break;
        case wclass::resend_frame: {
            auto sent = r.spawn_send("W106R");
            if (!sc.pump_until([&] { return rgs::Scene::ready(sent); }) || !sent.get()) {
                st.why = "the application send before the ResendRequest failed";
                return st;
            }
            // The ResendRequest asks from the send's own MsgSeqNum, so its first answer is the
            // send's replay.
            std::string const from{
                fixpp::test_support::extract_tag(r.d.bw->recorded.back().bytes, 34)};
            r.d.hold_next_write();
            held = held_feed(sc.frame("2", 2, "7=" + from + "\x01" + "16=0\x01"), "D");
            break;
        }
        case wclass::sequence_reset:
            r.hold_write_of("4");
            held = held_feed(sc.resend_request(2), "4");
            break;
        case wclass::reject:
            r.hold_write_of("3");
            held = held_feed(sc.heartbeat(2), "3") &&
                   fixpp::test_support::extract_tag(r.d.bw->recorded.back().bytes, 373) == "3";
            break;
        case wclass::resend_request:
            r.hold_write_of("2");
            held = held_feed(sc.heartbeat(5), "2");
            break;
        case wclass::heartbeat_reply:
            r.hold_write_of("0");
            held = held_feed(sc.test_request(2), "0");
            break;
        case wclass::business_reject:
            r.d.hold_next_write();
            held = held_feed(sc.order(2), "j");
            break;
        case wclass::initiator_logon:
        case wclass::reply_logon:
        case wclass::honor789_frame:
            break;
    }
    if (!held) {
        st.why = std::string{"the "} + class_name(c) + " write was not held";
        return st;
    }
    st.s = r.now();
    if (c == wclass::send || c == wclass::heartbeat || c == wclass::test_request ||
        c == wclass::heartbeat_reply || c == wclass::business_reject || c == wclass::reject ||
        c == wclass::resend_request) {
        st.queued.emplace(r.spawn_send("W106Q"));
        sc.drain();
    }
    st.ok = true;
    return st;
}

// The classes whose cut write's error reaches the call that started it: the application's send,
// drive_reconnect() (the initiator's Logon) and the inbound delivery for the leaves that return
// their emit's error. The loop's emits have no caller; the walk's frames return through the walk's
// own result, which keeps dispatch_aborted (replay_outbound_range_'s contract).
[[nodiscard]] bool has_cut_call(wclass c) {
    return c == wclass::send || c == wclass::initiator_logon || c == wclass::reject ||
           c == wclass::resend_request || c == wclass::heartbeat_reply ||
           c == wclass::business_reject || c == wclass::reply_logon;
}

// T100: open at S + W − 1 ns, closed at S + W; one write-timeout record; the cut call's
// session_write_timeout; dispatch_aborted for the send queued behind it; no Logout; FR-017.
void expect_cut_at_s_plus_w(session_role role, wclass c) {
    Rig r{role, params_for(c, kW), [c](SessionConfig& cfg) { tweak_for(c, cfg); }};
    Staged st = stage(r, c);
    ASSERT_TRUE(st.ok) << class_name(c) << ": setup: " << st.why;
    auto const disconnected_before = r.disconnected();
    auto const logouts_before = r.app->logouts;
    auto const w = std::chrono::milliseconds{kW};

    r.at(st.s + w - ns{1});
    EXPECT_EQ(r.write_records(), 0U) << class_name(c) << ": a write-timeout record before S + W";
    EXPECT_FALSE(r.d.closed()) << class_name(c) << ": closed before S + W";
    r.at(st.s + w);
    EXPECT_EQ(r.write_records(), 1U) << class_name(c) << ": write-timeout records at S + W";
    EXPECT_TRUE(r.d.closed()) << class_name(c) << ": not closed at S + W";

    if (st.cut) {
        ASSERT_TRUE(rgs::Scene::ready(*st.cut)) << class_name(c) << ": the cut call did not return";
        auto const cr = st.cut->get();
        if (has_cut_call(c)) {
            EXPECT_FALSE(cr.has_value()) << class_name(c) << ": the cut call succeeded";
            if (!cr) {
                EXPECT_EQ(cr.error(), fixpp::core::error::session_write_timeout)
                    << class_name(c) << ": the cut call's error " << static_cast<int>(cr.error());
            }
        }
    }
    if (st.queued) {
        ASSERT_TRUE(rgs::Scene::ready(*st.queued)) << class_name(c) << ": the queued send";
        auto const qr = st.queued->get();
        EXPECT_FALSE(qr.has_value()) << class_name(c) << ": the queued send succeeded";
        if (!qr) {
            EXPECT_EQ(qr.error(), fixpp::core::error::dispatch_aborted)
                << class_name(c) << ": the queued send's error " << static_cast<int>(qr.error());
        }
    }
    EXPECT_EQ(r.d.started("5"), 0U) << class_name(c) << ": a Logout";
    EXPECT_EQ(r.d.s.state(), fsm_state::Disconnected) << class_name(c);
    EXPECT_EQ(r.disconnected() - disconnected_before, 1U)
        << class_name(c) << ": new Disconnected entries";
    EXPECT_EQ(r.app->logouts - logouts_before, st.active ? 1 : 0)
        << class_name(c) << ": onLogout calls";
    EXPECT_EQ(r.give_up_records(), 0U) << class_name(c) << ": give-up records";
    EXPECT_EQ(r.write_records(), 1U) << class_name(c) << ": write-timeout records";
}

// T101, the spurious arm: the same staging with the limit off; still open at S + 1.3 s, with no
// write-timeout record.
void expect_open_with_limit_off(session_role role, wclass c) {
    Rig r{role, params_for(c, 0), [c](SessionConfig& cfg) { tweak_for(c, cfg); }};
    Staged st = stage(r, c);
    ASSERT_TRUE(st.ok) << class_name(c) << ": setup: " << st.why;
    r.at(st.s + 1300ms);
    EXPECT_FALSE(r.d.closed()) << class_name(c) << ": closed at S + 1.3 s with the limit off";
    EXPECT_EQ(r.write_records(), 0U) << class_name(c) << ": a write-timeout record";
}

struct ClassCase {
    wclass c;
    session_role role;
};

[[nodiscard]] std::string case_name(::testing::TestParamInfo<ClassCase> const& i) {
    return std::string{class_name(i.param.c)} +
           (i.param.role == session_role::acceptor ? "_Acceptor" : "_Initiator");
}

class WriteTimeout106 : public ::testing::TestWithParam<ClassCase> {};

TEST_P(WriteTimeout106, CutAtTheLimit) { expect_cut_at_s_plus_w(GetParam().role, GetParam().c); }
TEST_P(WriteTimeout106, LimitOff_StaysOpen) {
    expect_open_with_limit_off(GetParam().role, GetParam().c);
}

[[nodiscard]] std::vector<ClassCase> all_cases() {
    std::vector<ClassCase> v;
    for (auto c : {wclass::send, wclass::heartbeat, wclass::test_request, wclass::resend_frame,
                   wclass::sequence_reset, wclass::reject, wclass::resend_request,
                   wclass::heartbeat_reply, wclass::business_reject}) {
        v.push_back({c, session_role::acceptor});
        v.push_back({c, session_role::initiator});
    }
    v.push_back({wclass::initiator_logon, session_role::initiator});
    v.push_back({wclass::reply_logon, session_role::acceptor});
    v.push_back({wclass::honor789_frame, session_role::initiator});
    return v;
}

INSTANTIATE_TEST_SUITE_P(Classes, WriteTimeout106, ::testing::ValuesIn(all_cases()), case_name);

class WriteLimit106 : public ::testing::TestWithParam<session_role> {};

[[nodiscard]] std::string role_name(::testing::TestParamInfo<session_role> const& i) {
    return i.param == session_role::acceptor ? "Acceptor" : "Initiator";
}

// T102 (research D9 row "FR-010 start"): the second send queued at the first's start S_A; the
// first released at S_A + W − 1 ms, so the second starts there and is cut at its own start + W,
// not at S_A + W (where a start taken at the gate, M3, would cut it, 299 ms earlier).
TEST_P(WriteLimit106, QueuedWrite_CountsFromItsOwnStart) {
    Rig r{GetParam(), Params{}};
    ASSERT_TRUE(r.d.to_active()) << "setup";
    r.d.hold_next_write();
    auto a = r.spawn_send("W106A");
    ASSERT_TRUE(r.d.s.pump_until([&] { return parked_on(r, "D"); })) << "setup: A not held";
    auto const s_a = r.now();
    auto b = r.spawn_send("W106B");
    r.d.s.drain();
    auto const w = std::chrono::milliseconds{kW};
    r.at(s_a + w - 1ms);
    r.d.release();
    r.d.hold_next_write();
    r.d.s.drain();
    ASSERT_TRUE(rgs::Scene::ready(a)) << "A did not return after its release";
    EXPECT_TRUE(a.get().has_value()) << "A failed";
    ASSERT_TRUE(parked_on(r, "D") && r.d.started("D") == 2U) << "setup: B not held";
    auto const s_b = r.now();
    r.at(s_a + w);
    EXPECT_FALSE(r.d.closed()) << "B cut at A's start + W";
    EXPECT_EQ(r.write_records(), 0U) << "a write-timeout record at A's start + W";
    r.at(s_b + w - ns{1});
    EXPECT_FALSE(r.d.closed()) << "closed before B's start + W";
    r.at(s_b + w);
    EXPECT_TRUE(r.d.closed()) << "not closed at B's start + W";
    EXPECT_EQ(r.write_records(), 1U) << "write-timeout records";
    ASSERT_TRUE(rgs::Scene::ready(b)) << "B did not return";
    auto const br = b.get();
    ASSERT_FALSE(br.has_value()) << "B succeeded";
    EXPECT_EQ(br.error(), fixpp::core::error::session_write_timeout);
}

// T102 (research D9 row "FR-010 walk"): a resend walk of three frames, each released at its own
// start + W − 1 ms: the walk completes, nothing is cut (897 ms in all, below T = 1.2 s).
TEST_P(WriteLimit106, ResendWalk_ProgressNotCut) {
    Rig r{GetParam(), Params{}, [](SessionConfig& c) { tweak_for(wclass::resend_frame, c); }};
    ASSERT_TRUE(r.d.to_active()) << "setup";
    std::string from;
    for (auto const* id : {"W106R1", "W106R2", "W106R3"}) {
        auto f = r.spawn_send(id);
        ASSERT_TRUE(r.d.s.pump_until([&] { return rgs::Scene::ready(f); }) && f.get())
            << "setup: send " << id;
        if (from.empty()) {
            from = std::string{fixpp::test_support::extract_tag(r.d.bw->recorded.back().bytes, 34)};
        }
    }
    auto const before = r.d.bw->recorded.size();
    r.d.hold_next_write();
    // From the first send's MsgSeqNum: the walk is the three sends' replays.
    auto walk = r.d.s.feed_spawn(r.d.s.frame("2", 2, "7=" + from + "\x01" + "16=0\x01"));
    auto const w = std::chrono::milliseconds{kW};
    for (int k = 0; k < 3; ++k) {
        ASSERT_TRUE(r.d.s.pump_until([&] { return r.d.parked(); }))
            << "frame " << k << " of the walk was not held";
        auto const s = r.now();
        r.at(s + w - 1ms);
        EXPECT_FALSE(r.d.closed()) << "frame " << k << " cut before its own start + W";
        r.d.release();
        if (k < 2) r.d.hold_next_write();
        r.d.s.drain();
    }
    ASSERT_TRUE(rgs::Scene::ready(walk)) << "the walk did not complete";
    EXPECT_TRUE(walk.get().has_value()) << "the ResendRequest's delivery failed";
    EXPECT_EQ(r.d.bw->recorded.size() - before, 3U) << "frames written by the walk";
    EXPECT_FALSE(r.d.closed()) << "closed";
    EXPECT_EQ(r.write_records(), 0U) << "write-timeout records";
    EXPECT_EQ(r.d.s.state(), fsm_state::Active);
}

// T103 (research D9 row "FR-011 close"): close()'s Logout write held from S, W = 300 ms,
// B = 5000 ms; the mock step to S + W + 1 ms issued at once after close(): no write-timeout
// record, still open (close()'s grace, a wall-clock timer of 5 s, cannot expire first).
TEST_P(WriteLimit106, CloseLogout_KeepsGrace) {
    Params p;
    p.logout_ms = 5000;
    Rig r{GetParam(), p};
    ASSERT_TRUE(r.d.to_active()) << "setup";
    r.hold_write_of("5");
    auto closing = r.spawn(r.session().close(fixpp::session::close_mode::graceful));
    ASSERT_TRUE(r.d.s.pump_until([&] { return parked_on(r, "5"); }, 2s))
        << "setup: close()'s Logout not held";
    auto const s = r.now();
    r.at(s + std::chrono::milliseconds{kW} + 1ms);
    EXPECT_EQ(r.write_records(), 0U) << "write-timeout records";
    EXPECT_FALSE(r.d.closed()) << "closed at S + W + 1 ms";
    r.d.release();
    (void)r.d.s.pump_until([&] { return rgs::Scene::ready(closing); }, 10s);
}

// T103 (research D9 row FR-011): the give-up's Logout (H = T = 10 s, its TestRequest written)
// held from S = G, W = 300 ms, B = 500 ms: open at S + W and at S + B − 1 ns, closed at S + B
// (105's ending limit); S + B follows S + W by 200 ms, so an ordinary-class Logout would be cut
// first.
TEST_P(WriteLimit106, EndingLogout_Keeps105Limit) {
    Params p;
    p.heartbeat = 10s;
    p.threshold = 10000ms;
    Rig r{GetParam(), p};
    ASSERT_TRUE(r.d.to_active()) << "setup";
    auto const l = r.last_inbound();
    r.at(l + 10s);
    ASSERT_EQ(r.d.completed("1"), 1U) << "setup: no TestRequest at T";
    auto const g = t106::give_up_instant_written(l, std::chrono::milliseconds{10000}, r.now());
    r.hold_write_of("5");
    r.at(g);
    ASSERT_TRUE(parked_on(r, "5")) << "setup: the give-up's Logout not held at G";
    auto const s = r.now();
    r.at(s + std::chrono::milliseconds{kW});
    EXPECT_FALSE(r.d.closed()) << "closed at S + W";
    EXPECT_EQ(r.write_records(), 0U) << "write-timeout records";
    r.at(s + 500ms - ns{1});
    EXPECT_FALSE(r.d.closed()) << "closed before S + B";
    r.at(s + 500ms);
    EXPECT_TRUE(r.d.closed()) << "not closed at S + B";
    EXPECT_EQ(r.write_records(), 0U) << "write-timeout records";
    EXPECT_EQ(r.give_up_records(), 1U) << "give-up records";
}

// Orchestrator ruling D (proposed name): with the liveness loop off (HeartBtInt 0) the loop leaves
// at its guard, before its spawn point, so the bound the first limited write spawned (the
// initiator's Logon, the acceptor's reply Logon) keeps timing the connection's writes: an
// application send held at S is cut at S + W. A loop exit that ended the bound unconditionally
// leaves the send uncut (the bound is spawned once per connection).
TEST_P(WriteLimit106, LivenessOff_BoundKeepsTimingWrites) {
    Params p;
    p.heartbeat = 0s;
    Rig r{GetParam(), p};
    ASSERT_TRUE(r.d.to_active()) << "setup";
    r.d.hold_next_write();
    auto cut = r.spawn_send("W106L");
    ASSERT_TRUE(r.d.s.pump_until([&] { return parked_on(r, "D"); })) << "setup: send not held";
    auto const s = r.now();
    r.at(s + std::chrono::milliseconds{kW} - ns{1});
    EXPECT_FALSE(r.d.closed()) << "closed before S + W";
    r.at(s + std::chrono::milliseconds{kW});
    EXPECT_TRUE(r.d.closed()) << "not closed at S + W";
    EXPECT_EQ(r.write_records(), 1U) << "write-timeout records";
    ASSERT_TRUE(rgs::Scene::ready(cut)) << "the cut send did not return";
    auto const cr = cut.get();
    ASSERT_FALSE(cr.has_value()) << "the cut send succeeded";
    EXPECT_EQ(cr.error(), fixpp::core::error::session_write_timeout);
}

// T106 (research D9 row C-4): the bound's spawn refused at a limited write's start (HeartBtInt 0,
// so no loop spawns it first, and the seam set before the first write): the write is not
// started and returns dispatch_aborted; the claim taken, Disconnected once, closed once, zero
// reason records. The first limited write is the initiator's Logon (drive_reconnect() returns its
// error) or the acceptor's reply Logon.
TEST_P(WriteLimit106, ConnectionBound_SpawnFails_AtWrite) {
    Params p;
    p.heartbeat = 0s;
    Rig r{GetParam(), p};
    session_test_access::set_connection_bound_spawn_fails(r.session(), [] { return true; });
    auto const opened = r.d.run(r.session().open(), "open");
    ASSERT_TRUE(opened && opened->has_value()) << "setup: open()";
    auto const disconnected_before = r.disconnected();
    std::optional<expected_void> first;
    if (GetParam() == session_role::initiator) {
        first = r.d.run(r.session().drive_reconnect(), "drive_reconnect");
    } else {
        r.session().attach_accepted_transport(
            std::make_unique<fixpp::session::test::BlockingWriteTransport>(r.d.s.ioc.get_executor(),
                                                                           r.d.stream, r.d.bw),
            fixpp::transport::handshake_result{});
        first = r.d.s.feed(r.d.s.logon(1));
    }
    r.d.s.drain();
    EXPECT_EQ(r.d.bw->starts, 0U) << "a write was started";
    if (GetParam() == session_role::initiator) {
        ASSERT_TRUE(first.has_value()) << "drive_reconnect() did not return";
        EXPECT_FALSE(first->has_value()) << "drive_reconnect() succeeded";
        if (!first->has_value()) {
            EXPECT_EQ(first->error(), fixpp::core::error::dispatch_aborted);
        }
    }
    EXPECT_NE(session_test_access::ending_claim(r.session()), 0) << "no claim";
    EXPECT_EQ(r.d.s.state(), fsm_state::Disconnected);
    EXPECT_EQ(r.disconnected() - disconnected_before, 1U) << "new Disconnected entries";
    EXPECT_TRUE(r.d.closed()) << "the transport is open";
    EXPECT_EQ(r.d.bw->closes, 1U) << "closes";
    EXPECT_EQ(r.write_records(), 0U) << "write-timeout records";
    EXPECT_EQ(r.give_up_records(), 0U) << "give-up records";
}

// T106 (research D9 row C-4, data-model E-3's joiner row "the Disconnected edge"): the bound armed
// on a send held at S, the connection's end at S + 100 ms (the peer's Logout, with no close(),
// drain or stop), the clock not advanced after it, so the armed deadline cannot end the bound:
// its counter reads 0 after a bounded pump, through the edge's wake.
TEST_P(WriteLimit106, ConnectionBound_Teardown_ConnectionEnd_Timing) {
    Rig r{GetParam(), Params{}};
    ASSERT_TRUE(r.d.to_active()) << "setup";
    r.d.hold_next_write();
    auto held = r.spawn_send("W106T");
    ASSERT_TRUE(r.d.s.pump_until([&] { return parked_on(r, "D"); })) << "setup: send not held";
    ASSERT_EQ(session_test_access::connection_bound_count(r.session()), 1) << "setup: no bound";
    r.at(r.now() + 100ms);
    // The edge with no write before it: the reply Logout's toAdmin throws, which records
    // Disconnected before that Logout reaches the write gate (held behind the send).
    r.app->to_admin_hook = [](std::string_view m) {
        if (m == "5") throw std::runtime_error{"WriteLimit106: toAdmin"};
    };
    auto lo = r.d.s.feed_spawn(r.d.s.logout(2));
    EXPECT_TRUE(r.d.s.pump_until(
        [&] {
            return r.d.s.state() == fsm_state::Disconnected &&
                   session_test_access::connection_bound_count(r.session()) == 0;
        },
        2s))
        << "the bound outlived the connection's end";
    r.d.release();
    r.d.s.drain();
}

// T104 (plan "Deferred to tasks.md", M4 at the other Logout sites): each remaining Logout site's
// Logout held from S, W = 300 ms < B = 500 ms: no write-timeout record at S + W, still open, and
// closed at S + B by 105's ending limit (counted from the leaf's entry, S on the mock clock).
enum class logout_site : std::uint8_t { r2_leaf, refuse_logon, logon_sent_leaf, honor789 };

[[nodiscard]] const char* site_name(logout_site s) {
    switch (s) {
        case logout_site::r2_leaf:
            return "R2Leaf";
        case logout_site::refuse_logon:
            return "RefuseLogon";
        case logout_site::logon_sent_leaf:
            return "LogonSentLeaf";
        case logout_site::honor789:
            return "Honor789";
    }
    return "?";
}

void expect_logout_keeps_its_limit(logout_site site) {
    auto const role = site == logout_site::refuse_logon || site == logout_site::r2_leaf
                          ? session_role::acceptor
                          : session_role::initiator;
    Rig r{role, Params{}, [site](SessionConfig& c) {
              if (site == logout_site::refuse_logon) {
                  c.posture = fixpp::session::session_posture::production;
              }
              if (site == logout_site::honor789) c.enable_next_expected_msg_seq_num = true;
          }};
    auto& sc = r.d.s;
    std::optional<std::future<expected_void>> fed;
    if (site == logout_site::r2_leaf) {
        ASSERT_TRUE(r.d.to_active()) << "setup";
        r.hold_write_of("5");
        // MsgSeqNum 1 again, below NextNumIn 2, without PossDupFlag: the R-2 Logout.
        fed.emplace(sc.feed_spawn(sc.heartbeat(1)));
    } else {
        auto const opened = r.d.run(sc.session->open(), "open");
        ASSERT_TRUE(opened && opened->has_value()) << "setup: open()";
        if (role == session_role::acceptor) {
            sc.session->attach_accepted_transport(
                std::make_unique<fixpp::session::test::BlockingWriteTransport>(
                    sc.ioc.get_executor(), r.d.stream, r.d.bw),
                fixpp::transport::handshake_result{});
        } else {
            auto const c2 = r.d.run(sc.session->drive_reconnect(), "drive_reconnect");
            ASSERT_TRUE(c2 && c2->has_value()) << "setup: drive_reconnect()";
        }
        r.hold_write_of("5");
        switch (site) {
            case logout_site::refuse_logon:
                // TestMessageIndicator(464) Y against the production posture.
                fed.emplace(sc.feed_spawn(sc.frame("A", 1,
                                                   "98=0\x01"
                                                   "108=30\x01"
                                                   "464=Y\x01")));
                break;
            case logout_site::logon_sent_leaf:
                // SendingTime(52) ten minutes behind the clock: the LogonSent Logout.
                fed.emplace(sc.feed_spawn(
                    rgs::Scene::raw("A", 1, "TW", sc.sending_time(std::chrono::seconds{-600}),
                                    "98=0\x01"
                                    "108=30\x01")));
                break;
            case logout_site::honor789:
                // NextExpectedMsgSeqNum(789) above the next outbound number: the too-high Logout.
                fed.emplace(sc.feed_spawn(sc.frame("A", 1,
                                                   "98=0\x01"
                                                   "108=30\x01"
                                                   "789=99\x01")));
                break;
            case logout_site::r2_leaf:
                break;
        }
    }
    ASSERT_TRUE(sc.pump_until([&] { return parked_on(r, "5"); }))
        << site_name(site) << ": setup: the Logout was not held";
    auto const s = r.now();
    r.at(s + std::chrono::milliseconds{kW});
    EXPECT_EQ(r.write_records(), 0U) << site_name(site) << ": a write-timeout record at S + W";
    EXPECT_FALSE(r.d.closed()) << site_name(site) << ": closed at S + W";
    r.at(s + 500ms - ns{1});
    EXPECT_FALSE(r.d.closed()) << site_name(site) << ": closed before S + B";
    r.at(s + 500ms);
    EXPECT_TRUE(r.d.closed()) << site_name(site) << ": not closed at S + B";
    EXPECT_EQ(r.write_records(), 0U) << site_name(site) << ": write-timeout records";
}

TEST(WriteLimitLogoutSite106, EndingLogout_R2Leaf) {
    expect_logout_keeps_its_limit(logout_site::r2_leaf);
}
TEST(WriteLimitLogoutSite106, EndingLogout_RefuseLogon) {
    expect_logout_keeps_its_limit(logout_site::refuse_logon);
}
TEST(WriteLimitLogoutSite106, EndingLogout_LogonSentLeaf) {
    expect_logout_keeps_its_limit(logout_site::logon_sent_leaf);
}
TEST(WriteLimitLogoutSite106, EndingLogout_Honor789) {
    expect_logout_keeps_its_limit(logout_site::honor789);
}

// T105 (research D9 row FR-017; contract C-4.6): the OneEnding106 pairs with the limit set,
// GiveUp106's H = T = 10 s, W = 300 ms, B = 500 ms unless named. Each asserts FR-017: one new
// Disconnected entry, onLogout at most once, the transport closed, and the reason records of the
// first claimant (one for the give-up or the write limit, none for an ending Logout or close()).
struct PairReading {
    std::size_t disconnected = 0;
    int logouts = 0;
    bool closed = false;  // the transport's close (its count is not the reading: 105's close
                          // frame and a later close of the same transport both count)
    std::size_t give_up = 0;
    std::size_t write_timeout = 0;
};

class PairRig {
public:
    explicit PairRig(session_role role, std::uint32_t write_ms = kW, std::uint32_t logout_ms = 500)
        : r{role, [&] {
                Params p;
                p.heartbeat = 10s;
                p.threshold = 10000ms;
                p.write_ms = write_ms;
                p.logout_ms = logout_ms;
                return p;
            }()} {}
    Rig r;
    std::size_t disconnected_before = 0;
    int logouts_before = 0;

    [[nodiscard]] bool active() {
        if (!r.d.to_active()) return false;
        disconnected_before = r.disconnected();
        logouts_before = r.app->logouts;
        return true;
    }
    [[nodiscard]] PairReading read() {
        return {.disconnected = r.disconnected() - disconnected_before,
                .logouts = r.app->logouts - logouts_before,
                .closed = r.d.closed(),
                .give_up = r.give_up_records(),
                .write_timeout = r.write_records()};
    }
    // A send held from now (S).
    [[nodiscard]] std::optional<std::future<expected_void>> hold_send(std::string_view id) {
        r.d.hold_next_write();
        auto f = r.spawn_send(id);
        if (!r.d.s.pump_until([&] { return parked_on(r, "D"); })) return std::nullopt;
        return f;
    }
};

void expect_fr017(PairReading const& p, std::size_t give_up, std::size_t write_timeout,
                  char const* cell) {
    EXPECT_EQ(p.disconnected, 1U) << cell << ": new Disconnected entries";
    EXPECT_EQ(p.logouts, 1) << cell << ": onLogout calls";
    EXPECT_TRUE(p.closed) << cell << ": the transport is open";
    EXPECT_EQ(p.give_up, give_up) << cell << ": give-up records";
    EXPECT_EQ(p.write_timeout, write_timeout) << cell << ": write-timeout records";
}

class OneEnding106 : public ::testing::TestWithParam<session_role> {};

// GiveUp/Watchdog: the TestRequest written at L + T, G = L + 2T + 1 ns; a send held from
// S = G − 100 ms; the loop decides the give-up at G, its Logout queued behind the send; the
// bound at S + W = G + 200 ms finds the claim taken; 105's limit cuts the Logout at G + B.
TEST_P(OneEnding106, GiveUpThenWatchdog) {
    PairRig p{GetParam()};
    ASSERT_TRUE(p.active()) << "setup";
    auto const l = p.r.last_inbound();
    p.r.at(l + 10s);
    ASSERT_EQ(p.r.d.completed("1"), 1U) << "setup: no TestRequest at T";
    auto const g = t106::give_up_instant_written(l, std::chrono::milliseconds{10000}, p.r.now());
    p.r.at(g - 100ms);
    auto held = p.hold_send("W106P");
    ASSERT_TRUE(held) << "setup: the send was not held";
    p.r.at(g);
    EXPECT_EQ(p.r.give_up_records(), 1U) << "no give-up record at G";
    p.r.at(g + 200ms);
    EXPECT_EQ(p.r.write_records(), 0U) << "a write-timeout record at S + W";
    p.r.at(g + 500ms);
    EXPECT_TRUE(p.r.d.closed()) << "not closed at G + B";
    expect_fr017(p.read(), 1, 0, "GiveUpThenWatchdog");
}

// Watchdog/GiveUp: the send held from S = G − 500 ms, cut at S + W = G − 200 ms; the loop, waking
// at G, finds the session no longer Active. [guard: the loop's liveness_may_originate_() test]
TEST_P(OneEnding106, WatchdogThenGiveUp) {
    PairRig p{GetParam()};
    ASSERT_TRUE(p.active()) << "setup";
    auto const l = p.r.last_inbound();
    p.r.at(l + 10s);
    ASSERT_EQ(p.r.d.completed("1"), 1U) << "setup: no TestRequest at T";
    auto const g = t106::give_up_instant_written(l, std::chrono::milliseconds{10000}, p.r.now());
    p.r.at(g - 500ms);
    auto held = p.hold_send("W106P");
    ASSERT_TRUE(held) << "setup: the send was not held";
    p.r.at(g - 200ms);
    EXPECT_TRUE(p.r.d.closed()) << "not closed at S + W";
    p.r.at(g + 1s);
    expect_fr017(p.read(), 0, 1, "WatchdogThenGiveUp");
}

// Watchdog/EndingLimit and EndingLimit/Watchdog: a send held from S; at D = S + 100 ms the peer's
// MsgSeqNum 1 again (below NextNumIn) draws the R-2 Logout, which takes the ending claim at its
// leaf's entry and queues behind the send. With W = 300 ms the bound's instant S + W precedes
// 105's cut at D + B = S + 600 ms and finds the claim taken; with W = 1 s the cut at D + B comes
// first. Either way the ending is the R-2 Logout's: no reason record.
void ending_limit_pair(session_role role, std::uint32_t write_ms, char const* cell) {
    PairRig p{role, write_ms};
    ASSERT_TRUE(p.active()) << "setup";
    auto held = p.hold_send("W106P");
    ASSERT_TRUE(held) << "setup: the send was not held";
    auto const s = p.r.now();
    p.r.at(s + 100ms);
    auto fed = p.r.d.s.feed_spawn(p.r.d.s.heartbeat(1));
    p.r.d.s.drain();
    ASSERT_NE(session_test_access::ending_claim(p.r.session()), 0) << "setup: no R-2 claim at D";
    p.r.at(s + std::chrono::milliseconds{write_ms} < s + 600ms
               ? s + std::chrono::milliseconds{write_ms}
               : s + 300ms);
    EXPECT_EQ(p.r.write_records(), 0U) << cell << ": a write-timeout record";
    p.r.at(s + 600ms);
    EXPECT_TRUE(p.r.d.closed()) << cell << ": not closed at D + B";
    p.r.at(s + std::chrono::milliseconds{write_ms} + 100ms);
    expect_fr017(p.read(), 0, 0, cell);
}

TEST_P(OneEnding106, WatchdogThenEndingLimit) {
    ending_limit_pair(GetParam(), kW, "WatchdogThenEndingLimit");
}
TEST_P(OneEnding106, EndingLimitThenWatchdog) {
    ending_limit_pair(GetParam(), 1000, "EndingLimitThenWatchdog");
}

// Watchdog/Close: the send cut at S + W; close() at C = S + W + 100 ms finds the ending recorded.
// [guard: close() takes no claim]
TEST_P(OneEnding106, WatchdogThenClose) {
    PairRig p{GetParam(), kW, 5000};
    ASSERT_TRUE(p.active()) << "setup";
    auto held = p.hold_send("W106P");
    ASSERT_TRUE(held) << "setup: the send was not held";
    auto const s = p.r.now();
    p.r.at(s + std::chrono::milliseconds{kW});
    ASSERT_TRUE(p.r.d.closed()) << "not closed at S + W";
    p.r.at(s + std::chrono::milliseconds{kW} + 100ms);
    auto closed = p.r.d.run(p.r.session().close(fixpp::session::close_mode::graceful), "close");
    EXPECT_TRUE(closed.has_value()) << "close() did not return";
    expect_fr017(p.read(), 0, 1, "WatchdogThenClose");
}

// Close/Watchdog: close() at C = S + 100 ms, its Logout queued behind the held send; the mock step
// to S + W issued at once after close() (close()'s grace is a wall-clock timer of B = 5 s): the
// bound leaves on closing, so no write-timeout record. The held send is then released.
TEST_P(OneEnding106, CloseThenWatchdog) {
    PairRig p{GetParam(), kW, 5000};
    ASSERT_TRUE(p.active()) << "setup";
    auto held = p.hold_send("W106P");
    ASSERT_TRUE(held) << "setup: the send was not held";
    auto const s = p.r.now();
    p.r.at(s + 100ms);
    auto closing = p.r.spawn(p.r.session().close(fixpp::session::close_mode::graceful));
    p.r.at(s + std::chrono::milliseconds{kW});
    EXPECT_EQ(p.r.write_records(), 0U) << "a write-timeout record";
    p.r.d.release();
    EXPECT_TRUE(p.r.d.s.pump_until([&] { return rgs::Scene::ready(closing); }, 10s))
        << "close() did not return";
    auto const rd = p.read();
    EXPECT_EQ(rd.disconnected, 1U) << "new Disconnected entries";
    EXPECT_EQ(rd.logouts, 1) << "onLogout calls";
    EXPECT_EQ(rd.give_up, 0U) << "give-up records";
    EXPECT_EQ(rd.write_timeout, 0U) << "write-timeout records";
}

// BothDueAtOneWake (data-model E-3, the rows' order at one re-test): the loop's TestRequest held in
// its store call until S = G − W, then its write held, so the bound is armed on both G = L + 3T + 1
// ns and S + W = G; one step_to from G − 1 ms to G (the Instants rule's one exception): the give-up
// row is evaluated first and claims; the write row finds the claim taken.
TEST_P(OneEnding106, BothDueAtOneWake) {
    auto gate = std::make_shared<t105::gate_store_state>();
    Rig r{GetParam(),
          [] {
              Params p;
              p.heartbeat = 10s;
              p.threshold = 10000ms;
              return p;
          }(),
          [gate](SessionConfig& c) {
              c.store_factory = std::make_shared<t105::GateStoreFactory>(gate);
          }};
    ASSERT_TRUE(r.d.to_active()) << "setup";
    auto const l = r.last_inbound();
    gate->hold_when = [](t105::store_call k, fixpp::session::seqnum_t) {
        return k == t105::store_call::store_outbound;
    };
    r.hold_write_of("1");
    r.at(l + 10s);
    ASSERT_EQ(gate->parked, 1U) << "setup: the TestRequest's store call is not held";
    auto const g = t106::give_up_instant_emit_not_returned(l, std::chrono::milliseconds{10000});
    auto const w = std::chrono::milliseconds{kW};
    r.at(g - w);
    gate->hold_when = nullptr;
    gate->release_all();
    ASSERT_TRUE(r.d.s.pump_until([&] { return parked_on(r, "1"); })) << "setup: TestRequest write";
    r.at(g - 1ms);
    ASSERT_EQ(r.give_up_records(), 0U) << "setup: a give-up record before G";
    r.at(g);
    EXPECT_EQ(r.give_up_records(), 1U) << "give-up records";
    EXPECT_EQ(r.write_records(), 0U) << "write-timeout records";
    EXPECT_EQ(session_test_access::ending_claim(r.session()), 1)
        << "the claim is not the give-up's";
}

INSTANTIATE_TEST_SUITE_P(BothRoles, OneEnding106,
                         ::testing::Values(session_role::acceptor, session_role::initiator),
                         role_name);

// T106 (research D9 row C-4; data-model E-3's joiner row drain_for_reconnect_ and its respawn): a
// direct initiator in LogonSent (its reconnected Logon written, drive_reconnect() returned),
// inbound validation on (the validation test dictionary), W = 300 ms, logout_disconnect_timeout_ms
// 5000 ms (the drain's step-3 wall-clock bound, a competing ending: the cells read long before it).
// An acknowledgement without its required EncryptMethod(98) draws the validate-first Reject, whose
// write is held at S, so inbound_depth_ is 1; a second drive_reconnect() then enters the drain,
// which records no Disconnected from LogonSent and parks at its step 3.
class DrainRig {
public:
    Rig r{session_role::initiator,
          [] {
              Params p;
              p.logout_ms = 5000;
              return p;
          }(),
          [](SessionConfig& c) {
              c.dictionary = fixpp::test_support::make_validation_test_dictionary();
              c.validate_inbound_messages = true;
          }};
    std::optional<std::future<expected_void>> delivery;
    t106::steady_time_point s{};
    // A drive_reconnect() bound to sig's slot. Members after the rig, so destroyed before it; the
    // teardown below waits for the call to return before either is destroyed, so no completion
    // touches the signal after it dies.
    asio::cancellation_signal sig;
    std::optional<std::future<expected_void>> cancellable;
    std::future<expected_void>& cancellable_drive_reconnect() {
        cancellable.emplace(
            asio::co_spawn(fixpp::test_support::session_executor_of(r.session(), r.d.s.ioc),
                           r.session().drive_reconnect(),
                           asio::bind_cancellation_slot(sig.slot(), asio::use_future)));
        return *cancellable;
    }

    [[nodiscard]] bool logon_sent() {
        auto const opened = r.d.run(r.session().open(), "open");
        if (!opened || !opened->has_value()) return false;
        auto const c = r.d.run(r.session().drive_reconnect(), "drive_reconnect");
        return c && c->has_value() && r.d.s.state() == fsm_state::LogonSent;
    }
    [[nodiscard]] bool reject_held() {
        r.hold_write_of("3");
        delivery.emplace(r.d.s.feed_spawn(r.d.s.frame("A", 1, "108=30\x01")));
        if (!r.d.s.pump_until([&] { return parked_on(r, "3"); })) return false;
        s = r.now();
        return r.d.s.state() == fsm_state::LogonSent;
    }
    [[nodiscard]] int bound_count() {
        return session_test_access::connection_bound_count(r.session());
    }
    // A bounded teardown: the held write released, then close(), which wakes the bound (it leaves
    // on closing), so a bound a mutant leaves running cannot keep a drain's join spinning past
    // the cell; the rig's unbounded drain runs only after that.
    ~DrainRig() {
        r.d.release();
        if (delivery) (void)r.d.s.pump_until([&] { return rgs::Scene::ready(*delivery); }, 10s);
        auto closing = r.spawn(r.session().close(fixpp::session::close_mode::terminal));
        (void)r.d.s.pump_until(
            [&] {
                return rgs::Scene::ready(closing) &&
                       (!cancellable || !cancellable->valid() || rgs::Scene::ready(*cancellable));
            },
            10s);
    }
};

// _Parked: no write in flight (the acknowledgement withheld), so the drain's step 3 does not park;
// the second drive_reconnect() returns, and at the new connection's first spawn of the bound (its
// Logon's write) the old one's count was 0: the drain's entry wake and term ended it before the
// join and the install.
TEST(ConnectionBound106, Teardown_Drain_Parked) {
    DrainRig d;
    ASSERT_TRUE(d.logon_sent()) << "setup: not in LogonSent";
    ASSERT_EQ(d.bound_count(), 1) << "setup: the Logon's write spawned no bound";
    std::vector<int> old_at_spawn;
    session_test_access::set_connection_bound_spawn_fails(d.r.session(), [&] {
        old_at_spawn.push_back(d.bound_count() - 1);  // less the new spawn's own join
        return false;
    });
    auto second = d.r.spawn(d.r.session().drive_reconnect());
    EXPECT_TRUE(d.r.d.s.pump_until([&] { return rgs::Scene::ready(second); }, 2s))
        << "the second drive_reconnect() did not return";
    ASSERT_EQ(old_at_spawn.size(), 1U) << "spawns of the bound after the second drive_reconnect()";
    EXPECT_EQ(old_at_spawn.front(), 0) << "the old bound outlived the drain's join";
    session_test_access::set_connection_bound_spawn_fails(d.r.session(), nullptr);
}

// _Timing: the armed write a Logon-arm Reject held in LogonSent; the teardown at S + 100 ms, the
// clock not advanced after it, so only the drain's entry wake ends the bound before the join (the
// drain parks at step 3): the counter reads 0 after a bounded pump.
TEST(ConnectionBound106, Teardown_Drain_Timing) {
    DrainRig d;
    ASSERT_TRUE(d.logon_sent()) << "setup: not in LogonSent";
    ASSERT_TRUE(d.reject_held()) << "setup: the Reject's write was not held";
    d.r.at(d.s + 100ms);
    ASSERT_EQ(d.bound_count(), 1) << "setup: no bound armed on the Reject's write";
    auto second = d.r.spawn(d.r.session().drive_reconnect());
    EXPECT_TRUE(d.r.d.s.pump_until([&] { return d.bound_count() == 0; }, 2s))
        << "the bound outlived the drain's entry";
    EXPECT_FALSE(rgs::Scene::ready(second)) << "the drain did not park at its step 3";
    d.r.d.release();
    EXPECT_TRUE(d.r.d.s.pump_until([&] { return rgs::Scene::ready(second); }, 10s))
        << "the second drive_reconnect() did not return after the release";
}

// WriteExpiryDuringDrain_NoRecord: the same rig, step_to(S + W) issued as soon as the drain parks:
// no write-timeout record, no Disconnected from the bound, the counter 0; the Reject's write is
// ended by the drain's own wall-clock bound (its force-close), within 5 s + 2 s.
TEST(ConnectionBound106, WriteExpiryDuringDrain_NoRecord) {
    DrainRig d;
    ASSERT_TRUE(d.logon_sent()) << "setup: not in LogonSent";
    ASSERT_TRUE(d.reject_held()) << "setup: the Reject's write was not held";
    auto const disconnected_before = d.r.disconnected();
    auto second = d.r.spawn(d.r.session().drive_reconnect());
    ASSERT_TRUE(d.r.d.s.pump_until([&] { return d.bound_count() == 0; }, 2s))
        << "setup: the bound outlived the drain's entry";
    d.r.at(d.s + std::chrono::milliseconds{kW});
    EXPECT_EQ(d.r.write_records(), 0U) << "a write-timeout record during the drain";
    EXPECT_EQ(d.r.disconnected(), disconnected_before) << "Disconnected during the drain";
    EXPECT_EQ(d.bound_count(), 0) << "a bound during the drain";
    EXPECT_TRUE(d.r.d.s.pump_until([&] { return rgs::Scene::ready(*d.delivery); }, 7s))
        << "the Reject's write was not ended by the drain's bound";
    EXPECT_EQ(d.r.write_records(), 0U) << "write-timeout records";
    (void)d.r.d.s.pump_until([&] { return rgs::Scene::ready(second); }, 10s);
}

// DrainCancelled_RespawnsOnBlockedWrite (N-6): the same rig, the second drive_reconnect()
// cancelled through its cancellation slot while parked, so it returns dispatch_aborted; the bound
// is spawned again on the still-held write, and step_to(S + W) cuts it: one write-timeout record,
// Disconnected, the transport closed.
TEST(ConnectionBound106, DrainCancelled_RespawnsOnBlockedWrite) {
    DrainRig d;
    ASSERT_TRUE(d.logon_sent()) << "setup: not in LogonSent";
    ASSERT_TRUE(d.reject_held()) << "setup: the Reject's write was not held";
    auto const disconnected_before = d.r.disconnected();
    auto& second = d.cancellable_drive_reconnect();
    auto& sig = d.sig;
    ASSERT_TRUE(d.r.d.s.pump_until([&] { return d.bound_count() == 0; }, 2s))
        << "setup: the bound outlived the drain's entry";
    ASSERT_FALSE(rgs::Scene::ready(second)) << "setup: the drain did not park";
    sig.emit(asio::cancellation_type::terminal);
    ASSERT_TRUE(d.r.d.s.pump_until([&] { return rgs::Scene::ready(second); }, 2s))
        << "the cancelled drive_reconnect() did not return";
    auto const sr = second.get();
    ASSERT_FALSE(sr.has_value()) << "the cancelled drive_reconnect() succeeded";
    EXPECT_EQ(sr.error(), fixpp::core::error::dispatch_aborted);
    ASSERT_FALSE(d.r.d.closed()) << "setup: the cancellation closed the transport";
    EXPECT_EQ(d.bound_count(), 1) << "no bound spawned again on the held write";
    d.r.at(d.s + std::chrono::milliseconds{kW} - ns{1});
    EXPECT_FALSE(d.r.d.closed()) << "closed before S + W";
    d.r.at(d.s + std::chrono::milliseconds{kW});
    EXPECT_EQ(d.r.write_records(), 1U) << "write-timeout records";
    EXPECT_TRUE(d.r.d.closed()) << "not closed at S + W";
    EXPECT_EQ(d.r.d.s.state(), fsm_state::Disconnected);
    EXPECT_EQ(d.r.disconnected() - disconnected_before, 1U) << "new Disconnected entries";
    ASSERT_TRUE(rgs::Scene::ready(*d.delivery)) << "the Reject's delivery did not return";
    auto const dr = d.delivery->get();
    d.delivery.reset();
    ASSERT_FALSE(dr.has_value()) << "the Reject's delivery succeeded";
    EXPECT_EQ(dr.error(), fixpp::core::error::session_write_timeout);
}

// DrainCancelled_RespawnFails_ClosedAtOnce: the same rig, the spawn seam set after the bound's
// first spawn and before the cancellation, so the respawn is refused: at that return, with the
// clock not advanced, the claim taken, Disconnected, the transport closed, zero reason records;
// the Reject's write returns dispatch_aborted, its epoch not cut.
TEST(ConnectionBound106, DrainCancelled_RespawnFails_ClosedAtOnce) {
    DrainRig d;
    ASSERT_TRUE(d.logon_sent()) << "setup: not in LogonSent";
    ASSERT_TRUE(d.reject_held()) << "setup: the Reject's write was not held";
    auto const disconnected_before = d.r.disconnected();
    auto& second = d.cancellable_drive_reconnect();
    auto& sig = d.sig;
    ASSERT_TRUE(d.r.d.s.pump_until([&] { return d.bound_count() == 0; }, 2s))
        << "setup: the bound outlived the drain's entry";
    session_test_access::set_connection_bound_spawn_fails(d.r.session(), [] { return true; });
    sig.emit(asio::cancellation_type::terminal);
    ASSERT_TRUE(d.r.d.s.pump_until([&] { return rgs::Scene::ready(second); }, 2s))
        << "the cancelled drive_reconnect() did not return";
    d.r.d.s.drain();
    EXPECT_NE(session_test_access::ending_claim(d.r.session()), 0) << "no claim";
    EXPECT_EQ(d.r.d.s.state(), fsm_state::Disconnected);
    EXPECT_EQ(d.r.disconnected() - disconnected_before, 1U) << "new Disconnected entries";
    EXPECT_TRUE(d.r.d.closed()) << "the transport is open";
    EXPECT_EQ(d.r.write_records(), 0U) << "write-timeout records";
    EXPECT_EQ(d.r.give_up_records(), 0U) << "give-up records";
    ASSERT_TRUE(rgs::Scene::ready(*d.delivery)) << "the Reject's delivery did not return";
    auto const dr = d.delivery->get();
    d.delivery.reset();
    ASSERT_FALSE(dr.has_value()) << "the Reject's delivery succeeded";
    EXPECT_EQ(dr.error(), fixpp::core::error::dispatch_aborted)
        << "the Reject's error " << static_cast<int>(dr.error());
    session_test_access::set_connection_bound_spawn_fails(d.r.session(), nullptr);
}

// T107 (research D9, the shipping-clock row; its mutant M8): the shipping clock.
// system_clock_source, the coroutines on the session's executor, HeartBtInt 2 s (T = 2.4 s, the
// default threshold), W = 200 ms, B = 2000 ms; the application sends every 300 ms and the peer
// sends nothing. Every send completes until the TestRequest is seen at the peer, in [T, 2T) of wall
// time from the Logon; the next send is then held, at S, and cut within W + 1 s of S: one
// write-timeout record, no slot-74 record, session_write_timeout from the cut send. Inequality: the
// competing give-up is at the TestRequest's write + T or later, and 300 ms + W + 1 s = 1.5 s < T
// leaves 0.9 s of scheduling margin. The peer's frames are stamped from the Scene's mock clock, so
// sending_time_threshold is widened to accept them.
TEST_P(WriteLimit106, ShippingClock106_DirectDriver_BoundBesideLiveness_TestRequestAtT) {
    Params p;
    p.heartbeat = 2s;
    p.write_ms = 200;
    p.logout_ms = 2000;
    Rig r{GetParam(), p,
          [](SessionConfig& c) { c.sending_time_threshold = std::chrono::hours{24 * 365 * 10}; }};
    auto& sc = r.d.s;
    sc.engine.clock = std::make_shared<fixpp::core::system_clock_source>(sc.ioc.get_executor());
    sc.session = std::make_unique<fixpp::session::Session>(sc.engine, sc.cfg);
    auto& sess = r.session();
    // After open(), every call runs on the session's own executor (session->executor(), the
    // session_executor the clock's per-session timer slot is keyed by), so the liveness loop's
    // sleep and the bound's wait run where they would share that slot.
    auto const on_session = [&](auto awaitable, char const* what) {
        auto f = asio::co_spawn(sess.executor(), std::move(awaitable), asio::use_future);
        if (!sc.pump_until([&] { return rgs::Scene::ready(f); }, 5s)) {
            ADD_FAILURE() << what << " did not return";
            return false;
        }
        auto const v = f.get();
        if (!v) ADD_FAILURE() << what << " failed: " << static_cast<int>(v.error());
        return v.has_value();
    };
    auto const opened = r.d.run(sess.open(), "open");
    ASSERT_TRUE(opened && opened->has_value()) << "setup: open()";
    // Read before the peer's Logon, so it does not follow the session's last inbound instant.
    auto const logon_at = std::chrono::steady_clock::now();
    if (GetParam() == session_role::initiator) {
        ASSERT_TRUE(on_session(sess.drive_reconnect(), "drive_reconnect()")) << "setup";
    } else {
        sess.attach_accepted_transport(
            std::make_unique<fixpp::session::test::BlockingWriteTransport>(sc.ioc.get_executor(),
                                                                           r.d.stream, r.d.bw),
            fixpp::transport::handshake_result{});
    }
    auto const logon = sc.logon(1);
    ASSERT_TRUE(on_session(sess.on_inbound_frame(logon), "the peer's Logon")) << "setup";
    ASSERT_EQ(sc.state(), fsm_state::Active) << "setup";
    auto const t = 2400ms;
    std::deque<std::string> payloads;
    auto const send_on_session = [&](std::string id) {
        payloads.push_back(
            "35=D\x01"
            "11=" +
            id + "\x01");
        auto const& pl = payloads.back();
        return asio::co_spawn(sess.executor(),
                              sess.send(std::as_bytes(std::span{pl.data(), pl.size()})),
                              asio::use_future);
    };
    std::optional<std::chrono::steady_clock::time_point> tr_seen;
    std::deque<std::future<expected_void>> sends;
    int n = 0;
    while (!tr_seen && std::chrono::steady_clock::now() - logon_at < 2 * t) {
        sends.push_back(send_on_session("W106S" + std::to_string(++n)));
        (void)sc.pump_until([&] { return r.d.completed("1") > 0; }, 300ms);
        if (r.d.completed("1") > 0) tr_seen = std::chrono::steady_clock::now();
    }
    ASSERT_TRUE(tr_seen.has_value()) << "no TestRequest by 2T";
    EXPECT_GE(*tr_seen - logon_at, t) << "the TestRequest came before T";
    for (auto& f : sends) {
        ASSERT_TRUE(sc.pump_until([&] { return rgs::Scene::ready(f); }, 1s)) << "a send";
        EXPECT_TRUE(f.get().has_value()) << "a send before the TestRequest failed";
    }
    r.d.hold_next_write();
    // Read before the send starts, so it does not follow the held write's start S; the reading
    // after the park follows S.
    auto const before_s = std::chrono::steady_clock::now();
    auto cut = send_on_session("W106SCUT");
    ASSERT_TRUE(sc.pump_until([&] { return parked_on(r, "D"); }, 1s)) << "the send not held";
    auto const after_s = std::chrono::steady_clock::now();
    EXPECT_TRUE(sc.pump_until([&] { return r.d.closed(); }, 200ms + 1s))
        << "not cut within W + 1 s";
    EXPECT_LT(std::chrono::steady_clock::now() - after_s, 200ms + 1s) << "cut after W + 1 s";
    EXPECT_GE(std::chrono::steady_clock::now() - before_s, 200ms) << "cut before W";
    ASSERT_TRUE(sc.pump_until([&] { return rgs::Scene::ready(cut); }, 1s))
        << "the cut send did not return";
    auto const cr = cut.get();
    ASSERT_FALSE(cr.has_value()) << "the cut send succeeded";
    EXPECT_EQ(cr.error(), fixpp::core::error::session_write_timeout);
    EXPECT_EQ(r.write_records(), 1U) << "write-timeout records";
    EXPECT_EQ(r.give_up_records(), 0U) << "give-up records";
}

INSTANTIATE_TEST_SUITE_P(BothRoles, WriteLimit106,
                         ::testing::Values(session_role::acceptor, session_role::initiator),
                         role_name);

}  // namespace w106

}  // namespace
