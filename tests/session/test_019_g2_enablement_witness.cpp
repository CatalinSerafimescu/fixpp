// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_019_g2_enablement_witness.cpp
//
// 019-app-callbacks T020 — G2 enablement witness.
//
// Proves SC-002: an opaque application-message round-trip
// (NewOrderSingle 35=D → ExecutionReport 35=8) is drivable through the
// PUBLIC 019 surface — `co_await engine.send(id, payload)` for origination
// and `fromApp(…)` for observation — without typed business-message schemas.
//
// Harness: tests/session/ live loopback pattern (mirrors engine_lifecycle_test).
// ONE engine, ONE acceptor + ONE initiator session, connected over real loopback
// TLS on a pre-reserved port. GTEST_SKIP if FIXPP_TLS_FIXTURE_DIR absent.
//
// Chain:
//   1. engine.send(initiator_id, nos_payload)
//        → acceptor's fromApp fires with MsgType "D"
//   2. engine.send(acceptor_id, er_payload)
//        → initiator's fromApp fires with MsgType "8"
//
// Both observations use the SAME WitnessApplication; the SessionId parameter
// passed to fromApp identifies which side observed the message.
//
// Self-deadline: all waits use run_for() with a bounded wall-clock loop.
// [[feedback_fail_placeholder_red_test]]
//
// Anchors: spec.md SC-002, US1/US2; FR-003/006; tasks.md T020;
//          engine_lifecycle_test.cpp (loopback-TLS + port-reservation pattern).

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstdlib>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "support/minimal_dictionary.hpp"

using namespace std::chrono_literals;
using fixpp::core::expected_t;
using fixpp::session::Application;
using fixpp::session::SessionId;
using fixpp::wire::MessageView;
using fixpp::wire::access_mode;

namespace {

// ── Helpers ───────────────────────────────────────────────────────────────────

const char* get_fixture_dir() {
    const char* env = std::getenv("FIXPP_TLS_FIXTURE_DIR");  // NOLINT(concurrency-mt-unsafe)
#ifdef FIXPP_TLS_FIXTURE_DIR
    static const char* kDir = FIXPP_TLS_FIXTURE_DIR;
#else
    static const char* kDir = nullptr;
#endif
    return (env && env[0] != '\0') ? env : kDir;
}

// Reserve a free loopback port so we can register the acceptor + initiator with
// the same port before start() (mirrors engine_lifecycle_test.cpp).
static uint16_t reserve_free_port(asio::io_context& ioc) {
    asio::ip::tcp::acceptor a{ioc};
    asio::ip::tcp::endpoint ep{asio::ip::make_address("127.0.0.1"), 0};
    a.open(ep.protocol());
    a.bind(ep);
    uint16_t port = a.local_endpoint().port();
    a.close();
    return port;
}

// ── WitnessApplication ────────────────────────────────────────────────────────
// Tracks each (SessionId, MsgType) pair observed via fromApp.
// Thread-safe: fromApp fires on the session strand; mutex-protected records.

struct FromAppRecord {
    SessionId session_id;
    std::string msg_type;
};

class WitnessApplication : public Application {
public:
    mutable std::mutex mu;
    std::vector<FromAppRecord> records;

    expected_t<void> fromApp(const MessageView<access_mode::Index>& msg,
                             const SessionId& id) override {
        auto fv = msg.get(35);
        std::string mt = fv ? std::string(fv->as_string()) : "<none>";
        {
            std::lock_guard<std::mutex> lk(mu);
            records.push_back({id, std::move(mt)});
        }
        return {};
    }

    int count_for(const SessionId& id) const {
        std::lock_guard<std::mutex> lk(mu);
        int n = 0;
        for (const auto& r : records)
            if (r.session_id == id) ++n;
        return n;
    }

    std::string last_msg_type_for(const SessionId& id) const {
        std::lock_guard<std::mutex> lk(mu);
        for (auto it = records.rbegin(); it != records.rend(); ++it)
            if (it->session_id == id) return it->msg_type;
        return {};
    }
};

// ── Opaque payload builders ───────────────────────────────────────────────────

static std::vector<std::byte> make_nos_payload() {
    // NewOrderSingle body fields. MsgType (35=D) MUST be included in the
    // payload so the receiver's frame-scanner extracts it for fromApp dispatch.
    // Session::send_impl writes 8=/9=/34=/49=/52=/56= then appends app_payload;
    // it does NOT stamp 35=. The payload must carry it. [send_impl lines 2775-2822]
    static const char k[] =
        "35=D\x01""11=ORD001\x01""54=1\x01""55=AAPL\x01""40=2\x01""44=100.0\x01";
    std::vector<std::byte> v;
    for (const char* p = k; *p; ++p) v.push_back(static_cast<std::byte>(*p));
    return v;
}

static std::vector<std::byte> make_exec_report_payload() {
    // ExecutionReport body fields. MsgType (35=8) included for the same reason.
    static const char k[] =
        "35=8\x01""17=EXEC001\x01""37=ORD001\x01""39=2\x01""150=2\x01""151=0\x01";
    std::vector<std::byte> v;
    for (const char* p = k; *p; ++p) v.push_back(static_cast<std::byte>(*p));
    return v;
}

// ── Test ──────────────────────────────────────────────────────────────────────

TEST(G2EnablementWitness, OpaqueRoundTripViaEngineLoopback) {
    const char* fixture_dir = get_fixture_dir();
    if (!fixture_dir || fixture_dir[0] == '\0')
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set — skipping live loopback witness";

    asio::io_context ioc;

    // ── Build TLS factory (mTLS-CA, mirrors engine_lifecycle_test) ───────────
    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path = std::string(fixture_dir) + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = std::string(fixture_dir) + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path = std::string(fixture_dir) + "/ca.pem";
    auto cs_r = fixpp::tls::file_cert_source::make_file_cert_source(
        cs_cfg, std::pmr::new_delete_resource());
    ASSERT_TRUE(cs_r.has_value()) << "cert_source build failed";

    fixpp::tls::SslCtxConfig ssl;
    ssl.profile = fixpp::tls::SecurityProfile::mtls_ca;
    ssl.cs = std::move(*cs_r);
    ssl.clock = nullptr;
    ssl.caps = fixpp::tls::CertSourceCaps{};

    auto fac_r = fixpp::transport::make_asio_tls_transport_factory(
        fixpp::transport::Transport::Config{}, ssl);
    ASSERT_TRUE(fac_r.has_value()) << "transport factory build failed";
    std::shared_ptr<fixpp::transport::TransportFactory> fac{std::move(*fac_r)};

    // ── Reserve a port so both sessions can be registered with the same port ──
    const uint16_t port = reserve_free_port(ioc);

    // ── Build the application and engine ─────────────────────────────────────
    auto app = std::make_shared<WitnessApplication>();

    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.application = app;

    fixpp::session::Engine engine{ioc.get_executor(), std::move(ecfg)};

    // ── Register acceptor + initiator ─────────────────────────────────────────
    // The leaf cert CN is "fixpp-leaf-rsa2048" (loopback fixture convention).
    auto make_cfg = [&](const char* sender, const char* target,
                        fixpp::session::session_role role, const char* peer_compid) {
        fixpp::session::SessionConfig c;
        c.sender_comp_id = sender;
        c.target_comp_id = target;
        c.begin_string = "FIX.4.2";
        c.role = role;
        c.executor_override = ioc.get_executor();
        c.security_profile =
            fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
        c.compid_authorization_policy.add_binding("fixpp-leaf-rsa2048", peer_compid);
        c.dictionary = fixpp::test_support::make_minimal_dictionary();
        c.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        c.transport_factory_override = fac;
        c.heartbeat_interval = std::chrono::seconds{30};
        c.logout_disconnect_timeout_ms = 2000;
        c.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", port};
        c.transport_send = [](std::span<const std::byte>) {};  // rebound on attach
        return c;
    };

    auto acc_cfg = make_cfg("ACCEPTOR", "INITIATOR",
                            fixpp::session::session_role::acceptor, "INITIATOR");
    auto ini_cfg = make_cfg("INITIATOR", "ACCEPTOR",
                            fixpp::session::session_role::initiator, "ACCEPTOR");
    const auto acc_id = SessionId::from_config(acc_cfg);
    const auto ini_id = SessionId::from_config(ini_cfg);

    ASSERT_TRUE(engine.register_session(std::move(acc_cfg)).has_value())
        << "register acceptor failed";
    ASSERT_TRUE(engine.register_session(std::move(ini_cfg)).has_value())
        << "register initiator failed";

    // ── Start engine and wait for both sessions to reach Active ──────────────
    engine.start();

    bool acc_active = false, ini_active = false;
    auto deadline_logon = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline_logon &&
           (!acc_active || !ini_active)) {
        ioc.run_for(100ms);
        ioc.restart();
        const fixpp::session::Session* acc_s = engine.lookup(acc_id);
        const fixpp::session::Session* ini_s = engine.lookup(ini_id);
        acc_active = acc_s && acc_s->state() == fixpp::session::fsm_state::Active;
        ini_active = ini_s && ini_s->state() == fixpp::session::fsm_state::Active;
    }

    ASSERT_TRUE(acc_active) << "Acceptor did not reach Active within 5s";
    ASSERT_TRUE(ini_active) << "Initiator did not reach Active within 5s";

    // ── Step 1: initiator sends NOS (35=D) via engine.send ───────────────────
    // fromApp on the ACCEPTOR session must fire with MsgType "D".
    {
        auto nos = make_nos_payload();
        auto send_fut = asio::co_spawn(
            ioc, engine.send(ini_id, std::span<const std::byte>(nos)), asio::use_future);

        // Drive until acceptor's fromApp fires with "D" (bounded 3s).
        auto dl = std::chrono::steady_clock::now() + 3s;
        while (std::chrono::steady_clock::now() < dl && app->count_for(acc_id) == 0) {
            ioc.run_for(100ms);
            ioc.restart();
        }

        auto result = send_fut.get();
        ASSERT_TRUE(result.has_value())
            << "engine.send(NOS) failed: error="
            << (result.has_value() ? 0 : static_cast<int>(result.error()));

        ASSERT_GE(app->count_for(acc_id), 1)
            << "Acceptor: fromApp did not fire after NOS send (MsgType 'D')";
        EXPECT_EQ(app->last_msg_type_for(acc_id), "D")
            << "Acceptor: expected fromApp MsgType 'D' (NOS) but got '"
            << app->last_msg_type_for(acc_id) << "'";
    }

    // ── Step 2: acceptor sends ExecutionReport (35=8) via engine.send ────────
    // fromApp on the INITIATOR session must fire with MsgType "8".
    {
        auto er = make_exec_report_payload();
        auto send_fut = asio::co_spawn(
            ioc, engine.send(acc_id, std::span<const std::byte>(er)), asio::use_future);

        // Drive until initiator's fromApp fires with "8" (bounded 3s).
        auto dl = std::chrono::steady_clock::now() + 3s;
        while (std::chrono::steady_clock::now() < dl && app->count_for(ini_id) == 0) {
            ioc.run_for(100ms);
            ioc.restart();
        }

        auto result = send_fut.get();
        ASSERT_TRUE(result.has_value())
            << "engine.send(ER) failed: error="
            << (result.has_value() ? 0 : static_cast<int>(result.error()));

        ASSERT_GE(app->count_for(ini_id), 1)
            << "Initiator: fromApp did not fire after ExecutionReport send (MsgType '8')";
        EXPECT_EQ(app->last_msg_type_for(ini_id), "8")
            << "Initiator: expected fromApp MsgType '8' (ER) but got '"
            << app->last_msg_type_for(ini_id) << "'";
    }

    // ── Teardown ──────────────────────────────────────────────────────────────
    {
        auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
        ioc.run();
        stop_fut.get();
    }
    EXPECT_TRUE(engine.stopped()) << "engine must be stopped() after stop()";
}

}  // namespace
