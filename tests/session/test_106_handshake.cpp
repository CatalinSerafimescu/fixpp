// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_106_handshake.cpp — 106-logon-guards-write-bounds (tasks.md T003).
//
// The handshake-bound cells (research D6; contract C-5) that compile on the base, including
// the TOML cells that mint a TLS transport (tasks.md T003: config_044_tests defines no
// FIXPP_TLS_FIXTURE_DIR, so those cells live here).
//
// The ctest entry registers this file whether or not it holds cells yet, so it compiles on its
// own, on the base as at the head; each cell task of tasks.md appends its cells here.
//
// tasks.md T131 (research D9 row FR-013): the bundle's factory mints a transport, and its
// async_handshake against a listener that accepts and never speaks TLS (t106::SilentListener)
// returns transport_handshake_timeout at the key. The factory's Transport::Config has no accessor
// (research D6, "What a cell can observe"), so the key is read through that public path. A
// minted transport with the 30 s default is still pending at the cell's budget, key + 2 s.
// tasks.md T133 (research D9 row FR-014): drive_reconnect() over a real TLS factory, one
// attempt, the bound set through Transport::Config::tls_handshake_timeout.

#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fixpp/config/config_bundle.hpp>
#include <fixpp/config/load_diagnostic.hpp>
#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/reconnect_policy.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "session/resend_gap_scene.hpp"
#include "session/test_106_support.hpp"
#include "support/pump_until_ready.hpp"
#include "support/session_executor_of.hpp"
#include "support/toml_106_bundle.hpp"

#ifndef FIXPP_CONFIG_FIXTURE_DIR
#error "FIXPP_CONFIG_FIXTURE_DIR must be set by CMake"
#endif
#ifndef FIXPP_TLS_FIXTURE_DIR
#error "FIXPP_TLS_FIXTURE_DIR must be set by CMake"
#endif

using namespace std::chrono_literals;

namespace {

namespace t106 = fixpp::test_support::t106;
namespace toml106 = fixpp::test_support::toml106;
namespace rg = fixpp::test_support::resend_gap;
using fixpp::core::error;

constexpr auto kKey = 300ms;          // the key the cells set
constexpr auto kMargin = 2s;          // the budget above the key (research D9 row FR-013)
constexpr auto kSpuriousAt = 2300ms;  // the spurious arm's instant, 27.7 s before the 30 s default

// ── The bundles ────────────────────────────────────────────────────────────────────────────────

struct HsSession {
    std::string sender;
    std::string target;
    bool acceptor = false;
    std::string transport_keys;  // extra [session.transport] keys
};

// The config suite's fixtures: an mTLS cert source, the FIX 4.4 dictionary. `default_transport» is
// the [default.transport] table's keys (no table when empty).
std::string hs_toml(std::vector<HsSession> const& sessions, std::string const& default_transport) {
    std::string const dir =
        std::filesystem::path{std::string{FIXPP_CONFIG_FIXTURE_DIR}}.generic_string();
    std::string t = "[clock]\nkind = \"system\"\n\n[store]\nkind = \"memory\"\n\n";
    t += "[cert_source]\nkind = \"file\"\ncert_file = '" + dir + "/leaf_ecdsa_p256.pem'\n";
    t += "key_file = '" + dir + "/leaf_ecdsa_p256.key'\nca_file = '" + dir + "/ca.pem'\n\n";
    t += "[dictionary]\nkind = \"path\"\npath = '" + dir + "/FIX44.xml'\n\n";
    if (!default_transport.empty()) t += "[default.transport]\n" + default_transport + "\n\n";
    for (auto const& s : sessions) {
        t += "[[session]]\nsender_comp_id = \"" + s.sender + "\"\ntarget_comp_id = \"" + s.target +
             "\"\nbegin_string = \"FIX.4.4\"\nrole = \"" +
             (s.acceptor ? std::string{"acceptor"} : std::string{"initiator"}) + "\"\n\n";
        t += "[session.transport]\nkind = \"tls\"\n";
        if (!s.acceptor) t += "host = \"fix.example.com\"\nport = 4321\n";
        t += s.transport_keys + "\n\n";
        t += "[session.security_profile]\nkind = \"mtls_ca\"\n\n";
    }
    return t;
}

// Loads `text» through a temporary file; the bundle's clock is bound to `ioc», which outlives it.
fixpp::config::LoadResult load_hs(std::string const& text, asio::io_context& ioc) {
    fixpp::config::LoadOptions opts;
    opts.engine_executor = ioc.get_executor();
    return toml106::load_text(text, opts);
}

// The factory a session's transports come from: its override, else the engine default.
std::shared_ptr<fixpp::transport::TransportFactory> factory_of(fixpp::config::ConfigBundle const& b,
                                                               std::size_t i) {
    auto const& o = b.sessions[i].config.transport_factory_override;
    return o ? o : b.engine.default_transport_factory;
}

// ── A minted transport's handshake against a silent listener ───────────────────────────────────

// Shared with the probe, so a completion after the cell's verdict writes into live storage.
struct hs_state {
    fixpp::tls::SslCtxConfig ssl;  // async_handshake's lifetimebound argument
    bool connected = false;
    bool returned = false;
    std::optional<error> err;
    std::chrono::steady_clock::duration elapsed{};
    bool done = false;
};

asio::awaitable<void> hs_probe(std::shared_ptr<fixpp::transport::Transport> t,
                               std::shared_ptr<hs_state> st, std::uint16_t port) {
    auto const c = co_await t->async_connect(fixpp::transport::Endpoint{"127.0.0.1", port, 0});
    if (c.has_value()) {
        st->connected = true;
        auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(t.get());
        if (tls != nullptr) {
            auto const t0 = std::chrono::steady_clock::now();
            auto const h = co_await tls->async_handshake(st->ssl);
            st->elapsed = std::chrono::steady_clock::now() - t0;
            st->returned = true;
            if (!h.has_value()) st->err = h.error();
        }
    }
    st->done = true;
}

struct HsReading {
    bool minted = false;
    bool connected = false;
    bool returned = false;
    std::optional<error> err;
    std::chrono::milliseconds elapsed{};
};

// Mints one transport from `f», connects it to a fresh silent listener and handshakes, pumping
// `ioc» until the handshake returns or `budget» passes. The verdict is read before the teardown,
// which closes the transport and so completes a pending handshake.
HsReading handshake_once(asio::io_context& ioc, fixpp::transport::TransportFactory& f,
                         std::shared_ptr<fixpp::tls::cert_source> cs,
                         std::chrono::milliseconds budget) {
    HsReading r;
    t106::SilentListener listener{ioc};
    auto st = std::make_shared<hs_state>();
    st->ssl.profile = fixpp::tls::SecurityProfile::mtls_ca;
    st->ssl.cs = std::move(cs);
    st->ssl.clock = nullptr;
    auto made = f.make(ioc.get_executor(), st->ssl, nullptr);
    if (!made.has_value()) return r;
    r.minted = true;
    std::shared_ptr<fixpp::transport::Transport> t{std::move(*made)};
    asio::co_spawn(ioc, hs_probe(t, st, listener.port()), asio::detached);
    (void)fixpp::test_support::pump_until(
        ioc, [&] { return st->returned || st->done; }, budget, 5ms, "the minted handshake");
    r.connected = st->connected;
    r.returned = st->returned;
    r.err = st->err;
    r.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(st->elapsed);
    (void)t->close();
    (void)fixpp::test_support::pump_until(
        ioc, [&] { return st->done; }, 5s, 5ms, "the minted handshake's teardown");
    return r;
}

// The key's reading: the handshake returned transport_handshake_timeout, no earlier than the key
// (the transport's timer starts inside async_handshake, after the probe's start instant) and
// within the budget.
void expect_times_out_at_key(HsReading const& r, char const* what) {
    ASSERT_TRUE(r.minted) << what << ": the factory minted no transport";
    ASSERT_TRUE(r.connected) << what << ": the TCP connect to the silent listener failed";
    ASSERT_TRUE(r.returned) << what << ": the handshake had not returned at key + 2 s";
    ASSERT_TRUE(r.err.has_value()) << what << ": the handshake succeeded against a silent peer";
    EXPECT_EQ(*r.err, error::transport_handshake_timeout)
        << what << ": the handshake's error " << static_cast<int>(*r.err);
    EXPECT_GE(r.elapsed, kKey) << what << ": returned after " << r.elapsed.count() << " ms";
}

void expect_pending(HsReading const& r, char const* what) {
    ASSERT_TRUE(r.minted) << what << ": the factory minted no transport";
    ASSERT_TRUE(r.connected) << what << ": the TCP connect to the silent listener failed";
    EXPECT_FALSE(r.returned) << what << ": the handshake returned after " << r.elapsed.count()
                             << " ms, before 2.3 s";
}

std::string key_line(std::chrono::milliseconds v) {
    return "tls_handshake_timeout_ms = " + std::to_string(v.count()) + "\n";
}

}  // namespace

// T131 (research D9 row FR-013; contract C-5.1): the key in [session.transport] of the one TLS
// initiator: the engine default factory, built from session[0], bounds its handshake at the key.
TEST(TomlHandshake106, SessionKey) {
    asio::io_context ioc;
    auto const r = load_hs(
        hs_toml({{.sender = "CLIENT1", .target = "SERVER1", .transport_keys = key_line(kKey)}}, {}),
        ioc);
    ASSERT_TRUE(r.has_value()) << "the bundle did not load:" << toml106::seen(r);
    ASSERT_EQ(r->sessions.size(), 1U);
    auto const f = factory_of(*r, 0);
    ASSERT_NE(f, nullptr) << "no TLS factory";
    expect_times_out_at_key(handshake_once(ioc, *f, r->engine.default_cert_source, kKey + kMargin),
                            "session[0]");
}

// T131's spurious arm: no key, the 30 s default: still pending at 2.3 s.
TEST(TomlHandshake106, SessionKeyAbsent_Pending) {
    asio::io_context ioc;
    auto const r = load_hs(
        hs_toml({{.sender = "CLIENT1", .target = "SERVER1", .transport_keys = {}}}, {}), ioc);
    ASSERT_TRUE(r.has_value()) << "the bundle did not load:" << toml106::seen(r);
    ASSERT_EQ(r->sessions.size(), 1U);
    auto const f = factory_of(*r, 0);
    ASSERT_NE(f, nullptr) << "no TLS factory";
    expect_pending(handshake_once(ioc, *f, r->engine.default_cert_source, kSpuriousAt),
                   "session[0], no key");
}

// T131: two TLS initiators, session[0]'s key 60 s and session[1]'s the key. Only the value
// differs, so the divergence term is the only reason session[1] gets its own factory (contract
// C-5.1); its handshake times out at the key, and session[0]'s factory is still pending at 2.3 s.
TEST(TomlHandshake106, SecondSessionDiverges) {
    asio::io_context ioc;
    auto const r = load_hs(
        hs_toml({{.sender = "CLIENT1", .target = "SERVER1", .transport_keys = key_line(60000ms)},
                 {.sender = "CLIENT2", .target = "SERVER2", .transport_keys = key_line(kKey)}},
                {}),
        ioc);
    ASSERT_TRUE(r.has_value()) << "the bundle did not load:" << toml106::seen(r);
    ASSERT_EQ(r->sessions.size(), 2U);
    EXPECT_NE(r->sessions[1].config.transport_factory_override, nullptr)
        << "session[1] got no factory of its own";
    auto const f1 = factory_of(*r, 1);
    ASSERT_NE(f1, nullptr) << "no TLS factory for session[1]";
    expect_times_out_at_key(handshake_once(ioc, *f1, r->engine.default_cert_source, kKey + kMargin),
                            "session[1]");
    auto const f0 = factory_of(*r, 0);
    ASSERT_NE(f0, nullptr) << "no TLS factory for session[0]";
    expect_pending(handshake_once(ioc, *f0, r->engine.default_cert_source, kSpuriousAt),
                   "session[0], key 60 s");
}

// T131 (L-106-d; contract C-5.1): the key in [default.transport], shared by a TLS acceptor
// (session[0]) and a TLS initiator: the bundle loads, and the initiator's minted transport times
// out at the key.
TEST(TomlHandshake106, DefaultTransport_MixedRoles_Accepted) {
    asio::io_context ioc;
    auto const r = load_hs(
        hs_toml({{.sender = "SERVER1", .target = "CLIENT1", .acceptor = true, .transport_keys = {}},
                 {.sender = "CLIENT2", .target = "SERVER2", .transport_keys = {}}},
                key_line(kKey)),
        ioc);
    ASSERT_TRUE(r.has_value()) << "the mixed-role bundle did not load:" << toml106::seen(r);
    ASSERT_EQ(r->sessions.size(), 2U);
    auto const f = factory_of(*r, 1);
    ASSERT_NE(f, nullptr) << "no TLS factory for the initiator";
    expect_times_out_at_key(handshake_once(ioc, *f, r->engine.default_cert_source, kKey + kMargin),
                            "the initiator, key in [default.transport]");
}

// ── T133 (research D9 row FR-014; contract C-5.3): one bounded attempt ─────────────────────────

namespace {

struct ReconnectReading {
    bool built = false;
    bool opened = false;
    bool returned = false;
    std::optional<fixpp::core::expected_t<void>> result;
    std::chrono::milliseconds elapsed{};
};

// A direct initiator (resend_gap::Scene's Session) whose one connect attempt goes through a real
// TLS factory, its handshake bound `bound», to a listener that accepts and never speaks TLS.
// drive_reconnect() is awaited for `budget»; the verdict is read before close(), which ends a
// pending attempt.
ReconnectReading drive_one_attempt(std::chrono::milliseconds bound,
                                   std::chrono::milliseconds budget) {
    ReconnectReading out;
    char const* env = std::getenv("FIXPP_TLS_FIXTURE_DIR");
    std::string const dir = env != nullptr ? env : FIXPP_TLS_FIXTURE_DIR;
    if (dir.empty()) return out;
    rg::Scene s;
    std::optional<t106::SilentListener> listener;
    listener.emplace(s.ioc);
    fixpp::tls::file_cert_source::Config cs;
    cs.leaf_path = dir + "/leaf_rsa2048.pem";
    cs.private_key_path = dir + "/leaf_rsa2048.key";
    cs.ca_bundle_path = dir + "/ca.pem";
    auto source =
        fixpp::tls::file_cert_source::make_file_cert_source(cs, std::pmr::new_delete_resource());
    if (!source.has_value()) return out;
    fixpp::tls::SslCtxConfig ssl;
    ssl.profile = fixpp::tls::SecurityProfile::mtls_ca;
    ssl.cs = std::move(*source);
    ssl.clock = nullptr;
    ssl.caps = fixpp::tls::CertSourceCaps{};
    fixpp::transport::Transport::Config tcfg;
    tcfg.tls_handshake_timeout = bound;
    auto factory = fixpp::transport::make_asio_tls_transport_factory(tcfg, ssl);
    if (!factory.has_value()) return out;
    s.cfg.transport_factory_override =
        std::shared_ptr<fixpp::transport::TransportFactory>{std::move(*factory)};
    s.cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", listener->port(), 0};
    s.cfg.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::one_way_ca};
    fixpp::transport::ReconnectPolicy policy;
    policy.max_attempts = 1;
    policy.schedule = std::pmr::vector<std::chrono::milliseconds>{std::pmr::get_default_resource()};
    policy.schedule.push_back(0ms);
    policy.jitter = 0.0;
    s.cfg.reconnect_policy = policy;
    s.session = std::make_unique<fixpp::session::Session>(s.engine, s.cfg);
    out.built = true;

    auto const ex = fixpp::test_support::session_executor_of(*s.session, s.ioc);
    auto opened = asio::co_spawn(ex, s.session->open(), asio::use_future);
    if (!fixpp::test_support::pump_until(
            s.ioc, [&] { return rg::Scene::ready(opened); }, 5s, 5ms, "open") ||
        !opened.get().has_value()) {
        return out;
    }
    out.opened = true;
    auto const sx = fixpp::test_support::session_executor_of(*s.session, s.ioc);  // bound by open()
    auto const t0 = std::chrono::steady_clock::now();
    auto driven = asio::co_spawn(sx, s.session->drive_reconnect(), asio::use_future);
    out.returned = fixpp::test_support::pump_until(
        s.ioc, [&] { return rg::Scene::ready(driven); }, budget, 5ms, "drive_reconnect");
    if (out.returned) {
        out.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0);
        out.result = driven.get();
    }
    // The verdict is read. The listener's close ends a pending handshake (the peer's EOF), so
    // drive_reconnect() returns before close() runs.
    listener.reset();
    if (!out.returned) {
        (void)fixpp::test_support::pump_until(
            s.ioc, [&] { return rg::Scene::ready(driven); }, 10s, 5ms, "drive_reconnect's end");
    }
    auto closed = asio::co_spawn(sx, s.session->close(fixpp::session::close_mode::terminal),
                                 asio::use_future);
    (void)fixpp::test_support::pump_until(
        s.ioc, [&] { return rg::Scene::ready(closed); }, 10s, 5ms, "close");
    return out;
}

}  // namespace

// The bound 300 ms through Transport::Config::tls_handshake_timeout, max_attempts 1:
// drive_reconnect() returns transport_reconnect_limit_exceeded within 300 ms + 2 s. [guard: M7″,
// the factory's bound replaced by 60 s (tasks.md T143)]
TEST(HandshakeBound106, DriveReconnect_OneAttempt) {
    auto const r = drive_one_attempt(kKey, kKey + kMargin);
    if (!r.built) GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    ASSERT_TRUE(r.opened) << "open() failed";
    ASSERT_TRUE(r.returned) << "drive_reconnect() had not returned at 300 ms + 2 s";
    ASSERT_TRUE(r.result.has_value());
    ASSERT_FALSE(r.result->has_value()) << "drive_reconnect() succeeded against a silent peer";
    EXPECT_EQ(r.result->error(), error::transport_reconnect_limit_exceeded)
        << "drive_reconnect()'s error " << static_cast<int>(r.result->error());
    EXPECT_GE(r.elapsed, kKey) << "returned after " << r.elapsed.count() << " ms";
}

// Its spurious arm: the bound 60 s, so drive_reconnect() has not returned at 2.3 s.
TEST(HandshakeBound106, DriveReconnect_LongBound_NotReturned) {
    auto const r = drive_one_attempt(60000ms, kSpuriousAt);
    if (!r.built) GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    ASSERT_TRUE(r.opened) << "open() failed";
    EXPECT_FALSE(r.returned) << "drive_reconnect() returned after " << r.elapsed.count()
                             << " ms, before 2.3 s";
}
