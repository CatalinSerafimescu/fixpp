// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/session/engine_lifecycle_test.cpp — T017 [US3] Phase 5
//
// Proves the public multi-session lifecycle of the runtime Engine (SC-004/005):
//   register two distinct sessions (one initiator, one acceptor) → non-blocking
//   start → both loops run + both addressable via lookup(SessionId) → clean
//   idempotent stop → duplicate-identity rejection.
//
// The two sessions form a real in-engine loopback pair: the initiator connects
// to the co-registered acceptor over loopback-TLS, so BOTH loops genuinely run
// (connect+handshake+authorize on the initiator; accept+resolve+attach+admit on
// the acceptor) and reach an established session before stop() tears them down.
// This is the strongest SC-004/005 witness and avoids leaning on a doomed
// down-peer connect (whose teardown latency depends on transport-layer connect-
// cancellation — out of 015 scope; see the Gate-B note in tasks.md T018).
//
// Sub-scenarios (tasks.md T017 a–d):
//   (a) register initiator + acceptor → start() → both loops run + both
//       lookup()-retrievable; lookup() is null before the loop reaches open()
//       and non-null after (Gate A New-3 lazy construction).
//   (b) co_await stop() → clean teardown (construct→start→accept/connect→stop);
//       no leaked work / UAF / cancellation hang (asserted under the sanitizer
//       presets via the ASan/UBSan/TSan ctest runs).
//   (c) a second stop() is a no-op.
//   (d) a SessionConfig resolving to an already-registered SessionId →
//       register_session returns session_invalid_argument (=119).
//
// Anti-hang: every ioc.run_for() is bounded; stop() total-cancels both
//   established read-pumps (cancellation wired + tested in US2). Whole test < 5s.
//
// Anchors: tasks.md T017; spec.md US3 AC1/2/3 / SC-004/005; contracts C5/C6;
//          data-model E-5/E-7; research R9; [const §XI.2/§XV.9];
//          [[feedback_asio_cospawn_total_cancellation_default]].

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <memory>
#include <span>
#include <string>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_future.hpp>

#include <gtest/gtest.h>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/transport_factory.hpp>

#include "support/minimal_dictionary.hpp"

using namespace std::chrono_literals;

namespace {

const char* get_fixture_dir() {
    const char* dir = std::getenv("FIXPP_TLS_FIXTURE_DIR");
#ifdef FIXPP_TLS_FIXTURE_DIR
    static const char* kDir = FIXPP_TLS_FIXTURE_DIR;
#else
    static const char* kDir = nullptr;
#endif
    return (dir && dir[0] != '\0') ? dir : kDir;
}

// Reserve a free loopback port: bind a throwaway acceptor to port 0, read the
// OS-assigned port, then release it. The engine acceptor re-binds it at start().
uint16_t reserve_free_port(asio::io_context& ioc) {
    asio::ip::tcp::acceptor a{ioc};
    asio::ip::tcp::endpoint ep{asio::ip::make_address("127.0.0.1"), 0};
    a.open(ep.protocol());
    a.bind(ep);
    uint16_t port = a.local_endpoint().port();
    a.close();
    return port;
}

bool is_established(fixpp::session::Session* s) {
    return s != nullptr &&
        (s->state() == fixpp::session::fsm_state::Active ||
         s->state() == fixpp::session::fsm_state::LogonReceived);
}

}  // namespace

// ── (a)+(b)+(c): register → start → lookup → stop, real in-engine loopback ────

TEST(EngineLifecycleTest, TwoSessionRegisterStartLookupStop) {
    const char* fixture_dir = get_fixture_dir();
    if (!fixture_dir || fixture_dir[0] == '\0')
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    asio::io_context ioc;

    // Shared TLS factory + cert_source (same leaf cert for both ends — mTLS-CA).
    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path        = std::string(fixture_dir) + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = std::string(fixture_dir) + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path   = std::string(fixture_dir) + "/ca.pem";
    auto cs_r = fixpp::tls::file_cert_source::make_file_cert_source(
        cs_cfg, std::pmr::new_delete_resource());
    ASSERT_TRUE(cs_r.has_value()) << "cert_source build failed";

    fixpp::tls::SslCtxConfig ssl;
    ssl.profile = fixpp::tls::SecurityProfile::mtls_ca;
    ssl.cs      = std::move(*cs_r);
    ssl.clock   = nullptr;
    ssl.caps    = fixpp::tls::CertSourceCaps{};

    auto fac_r = fixpp::transport::make_asio_tls_transport_factory(
        fixpp::transport::Transport::Config{}, ssl);
    ASSERT_TRUE(fac_r.has_value()) << "transport factory build failed";
    std::shared_ptr<fixpp::transport::TransportFactory> fac{std::move(*fac_r)};

    const uint16_t port = reserve_free_port(ioc);

    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};

    // The leaf cert CN is "fixpp-leaf-rsa2048" (loopback fixture convention).
    auto make_cfg = [&](const char* sender, const char* target,
                        fixpp::session::session_role role,
                        const char* peer_compid) {
        fixpp::session::SessionConfig c;
        c.sender_comp_id    = sender;
        c.target_comp_id    = target;
        c.begin_string      = "FIX.4.2";
        c.role              = role;
        c.executor_override = ioc.get_executor();
        c.security_profile  = fixpp::session::SecurityProfile{
            fixpp::session::SecurityProfile::kind::mtls_ca};
        // Authorize the peer's live cert identity (CN) → its expected CompID.
        c.compid_authorization_policy.add_binding("fixpp-leaf-rsa2048", peer_compid);
        c.dictionary        = fixpp::test_support::make_minimal_dictionary();
        c.reset_seqnum_policy_field =
            fixpp::session::reset_seqnum_policy::bilateral_lenient;
        c.transport_factory_override = fac;
        c.heartbeat_interval = std::chrono::seconds{30};
        c.logout_disconnect_timeout_ms = 2000;
        // Acceptor: bind endpoint. Initiator: connect target. Same fixed port.
        c.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", port};
        c.transport_send = [](std::span<const std::byte>) {};
        return c;
    };

    auto acc = make_cfg("ACCEPTOR", "INITIATOR",
                        fixpp::session::session_role::acceptor, "INITIATOR");
    auto ini = make_cfg("INITIATOR", "ACCEPTOR",
                        fixpp::session::session_role::initiator, "ACCEPTOR");
    const auto acc_id = fixpp::session::SessionId::from_config(acc);
    const auto ini_id = fixpp::session::SessionId::from_config(ini);
    ASSERT_TRUE(engine.register_session(std::move(acc)).has_value());
    ASSERT_TRUE(engine.register_session(std::move(ini)).has_value());

    // ── (a) lazy construction: null before each loop reaches open() ───────────
    // start() only co_spawns the loops (posts them); nothing runs until the
    // io_context is polled, so lookup() is null for both right after start().
    engine.start();
    EXPECT_EQ(engine.lookup(acc_id), nullptr)
        << "acceptor lookup() must be null after start() but before its accept "
        << "loop reaches the lazy ctor + open() (Gate A New-3).";
    EXPECT_EQ(engine.lookup(ini_id), nullptr)
        << "initiator lookup() must be null after start() but before its connect "
        << "loop reaches the lazy ctor + open() (Gate A New-3).";

    // Run the executor so both loops construct + open + connect/accept + admit.
    ioc.run_for(2s);
    ioc.restart();

    // ── (a) non-null after open(): both sessions addressable by SessionId ─────
    fixpp::session::Session* acc_s = engine.lookup(acc_id);
    fixpp::session::Session* ini_s = engine.lookup(ini_id);
    EXPECT_NE(acc_s, nullptr) << "acceptor lookup() must be non-null after open().";
    EXPECT_NE(ini_s, nullptr) << "initiator lookup() must be non-null after open().";

    // Both loops genuinely ran end-to-end: the acceptor only advances past
    // NotConnected if the initiator connected, handshook, and sent a Logon that
    // resolved + authorized; the initiator only reaches Active on the acceptor's
    // reply Logon. (SC-004/005 — "both run their loops".)
    int acc_state = (acc_s != nullptr) ? static_cast<int>(acc_s->state()) : -1;
    int ini_state = (ini_s != nullptr) ? static_cast<int>(ini_s->state()) : -1;
    EXPECT_TRUE(is_established(acc_s))
        << "acceptor must reach an established state (the initiator connected, "
        << "handshook, and its Logon was resolved + admitted). state=" << acc_state;
    EXPECT_TRUE(is_established(ini_s))
        << "initiator must reach an established state (it connected and received "
        << "the acceptor's reply Logon via its read-pump). state=" << ini_state;

    // ── (b) clean idempotent stop: total-cancel + close transports + join ─────
    // stop() must complete promptly: it closes both established (idle) read-pumps'
    // transports so the in-flight async_read_some fails and the pumps unwind, then
    // joins-before-clear. An unbounded ioc.run() that returns proves no hang.
    {
        auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
        ioc.run();
        stop_fut.get();
    }
    EXPECT_TRUE(engine.stopped()) << "engine must be stopped() after stop().";

    // After stop() cleared the registry, lookup() returns null for both.
    EXPECT_EQ(engine.lookup(acc_id), nullptr);
    EXPECT_EQ(engine.lookup(ini_id), nullptr);

    // ── (c) a second stop() is a no-op ────────────────────────────────────────
    {
        ioc.restart();
        auto stop_fut2 = asio::co_spawn(ioc, engine.stop(), asio::use_future);
        ioc.run();
        stop_fut2.get();  // must complete promptly (no work to join)
    }
    EXPECT_TRUE(engine.stopped())
        << "a second stop() must remain a no-op and leave the engine stopped().";
}

// ── (d): duplicate-identity registration is rejected (SC-004 / FR-002) ────────
//
// No TLS fixture needed: register_session only derives SessionId::from_config
// and stores config + role (lazy construction — no Session built, no transport).

TEST(EngineLifecycleTest, DuplicateIdentityRejected) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();

    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};

    auto make_cfg = [&] {
        fixpp::session::SessionConfig c;
        c.sender_comp_id    = "ACCEPTOR";
        c.target_comp_id    = "INITIATOR";
        c.begin_string      = "FIX.4.2";
        c.role              = fixpp::session::session_role::acceptor;
        c.executor_override = ioc.get_executor();
        return c;
    };

    // First registration of a unique SessionId succeeds.
    ASSERT_TRUE(engine.register_session(make_cfg()).has_value())
        << "first registration of a unique SessionId must succeed.";

    // A config resolving to the same {begin_string, sender, target} → same
    // SessionId → duplicate → session_invalid_argument (=119).
    auto dup_r = engine.register_session(make_cfg());
    ASSERT_FALSE(dup_r.has_value())
        << "FR-002 / SC-004: a config resolving to an already-registered "
        << "SessionId must be rejected.";
    EXPECT_EQ(dup_r.error(), fixpp::core::error::session_invalid_argument)
        << "duplicate registration must fail with session_invalid_argument (119).";

    // The engine was never started, but the strict dtor still requires stop()
    // (a never-started engine has stopped_ == false). stop() with a null
    // outstanding counter skips the join and just clears the registry.
    auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    ioc.run();
    stop_fut.get();
    EXPECT_TRUE(engine.stopped());
}
