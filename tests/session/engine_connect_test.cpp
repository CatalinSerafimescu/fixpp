// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/session/engine_connect_test.cpp — T016(a) [RED] [US2] Phase 4
//
// Anchors:
//   tasks.md T016(a)
//   specs/015-runtime-engine/contracts/realized-behavior.md C2i
//   specs/015-runtime-engine/data-model.md E-1a
//   feature spec FR-003 (connect-then-Logon ordering)
//   SC-010 deltas (7)/(8): Session::drive_reconnect() + Session::live_transport()
//
// TDD RED: verify that the initiator live connect path (run_connect_loop) is
// a STUB and the real connect-then-Logon-then-read-pump flow does NOT happen yet.
//
// Scenario (C2i / US2 / FR-003):
//   1. A standalone TLS server runs on LoopbackTlsFixture's listener.
//   2. An initiator session is registered in the Engine with
//      reconnect_endpoint = fixture.server_endpoint() (the server's bound port).
//   3. Engine::start() spawns run_connect_loop for the initiator.
//   4. Server driver coroutine: co_await fixture.listener().async_accept() →
//      TLS handshake → receive initiator's post-connect Logon → send Logon-ack
//      + N heartbeats → set server_received_logon = true.
//
// RED today (STUB at engine.cpp ~line 550-566):
//   run_connect_loop does open() (which calls Session::open() for the initiator;
//   open() transitions to LogonSent and emits the Logon into the no-op
//   transport_send_) then co_returns.  It NEVER calls async_connect to the server.
//   Therefore:
//     - server driver's async_accept() never completes (no TCP connection arrives);
//       the server coroutine times out via its self-deadline.
//     - server_received_logon stays false.
//     - initiator session state stays LogonSent (NOT Active).
//     - next_inbound_unsafe() stays 1 (no inbound frames delivered).
//
// GREEN (T016(b)-(e)):
//   run_connect_loop: open() (no Logon at open time) → drive_reconnect() →
//   install_reconnected_transport (rebinds transport_send_) → emit Logon
//   POST-connect → run_read_pump(session.live_transport(), session, cfg);
//   server receives the Logon, replies with Logon-ack + N heartbeats;
//   read-pump delivers them → LogonSent→Active, next_inbound advances.
//
// Anti-hang: every co_spawn carries a self-deadline (steady_timer). All
//   ioc.run_for() calls are explicitly bounded. Whole test <= 5s.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/seqnum_manager.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <future>
#include <string>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "transport/loopback_tls_fixture.hpp"

using namespace std::chrono_literals;
using fixpp::session::fsm_state;

namespace {

// Build a fresh SendingTime(52) string so the Q3 guard (038/041 T019) admits
// this frame when the engine has a real clock.
static std::string utc_now_fix_timestamp() {
    std::array<char, 32> buf{};
    auto r = fixpp::core::utc_time_to_fix_string(std::chrono::system_clock::now(),
                                                 fixpp::core::fix_time_precision::millis,
                                                 std::span<char>{buf});
    return r ? std::string{r->data(), r->size()} : std::string{};
}

// ── Frame builder helpers ─────────────────────────────────────────────────────

static std::vector<std::byte> make_fix_frame(std::string_view begin_str, std::string_view msg_type,
                                             int seq_num, std::string_view sender,
                                             std::string_view target, std::string extra_body = "") {
    auto field = [](int tag, std::string_view v) -> std::string {
        return std::to_string(tag) + "=" + std::string(v) + "\x01";
    };
    std::string body;
    body += field(35, msg_type);
    body += field(34, std::to_string(seq_num));
    body += field(49, sender);
    body += field(52, utc_now_fix_timestamp());  // SendingTime (fresh; 041 T019 clock gate)
    body += field(56, target);
    body += extra_body;

    std::string msg;
    msg += "8=" + std::string(begin_str) + "\x01";
    msg += "9=" + std::to_string(body.size()) + "\x01";
    msg += body;
    unsigned int cs = 0;
    for (unsigned char c : msg) cs += c;
    cs &= 0xFFu;
    char csbuf[5];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    msg += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> out;
    out.reserve(msg.size());
    for (char c : msg) out.push_back(static_cast<std::byte>(c));
    return out;
}

// Logon-ack frame (server sends seq=1 back to initiator).
static std::vector<std::byte> make_logon_ack_frame(std::string_view begin_str,
                                                   std::string_view sender,
                                                   std::string_view target) {
    return make_fix_frame(begin_str, "A", 1, sender, target,
                          "98=0\x01"
                          "108=30\x01");
}

// Heartbeat frame.
static std::vector<std::byte> make_heartbeat_frame(std::string_view begin_str, int seq_num,
                                                   std::string_view sender,
                                                   std::string_view target) {
    return make_fix_frame(begin_str, "0", seq_num, sender, target);
}

// ── Build-harness helper ─────────────────────────────────────────────────────
// Returns the fixture_dir string or nullptr if absent.
static const char* get_fixture_dir() {
    const char* dir = std::getenv("FIXPP_TLS_FIXTURE_DIR");
#ifdef FIXPP_TLS_FIXTURE_DIR
    static const char* kDir = FIXPP_TLS_FIXTURE_DIR;
#else
    static const char* kDir = nullptr;
#endif
    return (dir && dir[0] != '\0') ? dir : kDir;
}

// ── Standalone TLS server driver coroutine ────────────────────────────────────
// Models the "server" counterpart to the engine's initiator.
// Waits for ONE TCP connection from the initiator (up to `deadline`), completes
// TLS, reads the initiator's Logon frame (searches for "35=A"), then sends
// Logon-ack (seq=1) + N heartbeats (seq=2..N+1).
// Sets server_received_logon = true if the received data contains "35=A".
//
// Self-deadline anti-hang:
//   A timer co_spawn cancels the fixture's listener after `deadline`.
//   When the listener is cancelled, async_accept() returns an error value
//   (transport_accept_cancelled), this coroutine detects it and co_returns.
//   The fixture's cancel_listener() method closes the listen socket (FR-025
//   Option-A contract).
//
// Caller must ensure server_received_logon and fixture outlive this coroutine.

static asio::awaitable<void> run_server_driver(fixpp::transport::test::LoopbackTlsFixture& fixture,
                                               std::atomic<bool>& server_received_logon,
                                               int n_heartbeats,
                                               std::chrono::milliseconds deadline) {
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

    auto executor = co_await asio::this_coro::executor;

    // Spawn a timer that cancels the listener after `deadline`.
    // If async_accept completes first, we cancel the timer; if the timer fires
    // first, async_accept() returns an error value and we co_return.
    // Using a shared cancellation flag to prevent the timer callback firing
    // after the accept already succeeded.
    auto cancel_timer = std::make_shared<asio::steady_timer>(executor);
    cancel_timer->expires_after(deadline);
    cancel_timer->async_wait([&fixture, cancel_timer](const std::error_code& ec) {
        if (!ec) {
            // Timer fired — cancel the listener so async_accept unblocks.
            fixture.cancel_listener();
        }
    });

    try {
        // co_await the accept. On the stub path: the initiator never connects;
        // the timer fires and cancels the listener → async_accept returns error.
        auto accept_r = co_await fixture.listener().async_accept();
        // Timer no longer needed regardless of outcome.
        cancel_timer->cancel();

        if (!accept_r.has_value()) {
            // Timed out (stub path) or error — server never receives logon.
            co_return;
        }

        // Got a TCP connection. Drive the TLS handshake.
        std::unique_ptr<fixpp::transport::Transport> transport = std::move(*accept_r);
        auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(transport.get());
        if (!tls) co_return;

        auto hs_r = co_await tls->async_handshake(fixture.ssl_cfg());
        if (!hs_r.has_value()) co_return;

        // Read the initiator's Logon frame.
        // Buffer: 4096 bytes (well above any Logon frame).
        std::vector<std::byte> buf(4096);
        std::vector<std::byte> received;
        while (received.size() < 4096) {
            auto read_r = co_await transport->async_read_some(std::span<std::byte>{buf});
            if (!read_r.has_value()) break;
            auto n = *read_r;
            received.insert(received.end(), buf.begin(), buf.begin() + n);

            // Check for "35=A" in received bytes (FIX Logon MsgType).
            // The needle's begin and end MUST point into the SAME array: two
            // distinct `"35=A"` literals are not guaranteed to be pooled (MSVC
            // debug keeps them separate), so `(const std::byte*)"35=A"` and a
            // second `(const std::byte*)"35=A" + 4` would be pointers into
            // different objects → a transposed/invalid range (UB; MSVC debug's
            // _Adl_verify_range aborts). Use one named array for both ends.
            static constexpr char kLogonMsgType[] = "35=A";
            const auto* needle = reinterpret_cast<const std::byte*>(kLogonMsgType);
            auto it = std::search(received.begin(), received.end(), needle, needle + 4);
            if (it != received.end()) {
                server_received_logon.store(true, std::memory_order_release);
                break;
            }
        }

        if (!server_received_logon.load(std::memory_order_acquire)) co_return;

        // Send Logon-ack (seq=1, sender=ACCEPTOR, target=INITIATOR).
        auto logon_ack = make_logon_ack_frame("FIX.4.2", "ACCEPTOR", "INITIATOR");
        (void)co_await transport->async_write(std::span<const std::byte>{logon_ack});

        // Send N heartbeats (seq=2..N+1).
        for (int i = 0; i < n_heartbeats; ++i) {
            auto hb = make_heartbeat_frame("FIX.4.2", /*seq=*/2 + i, "ACCEPTOR", "INITIATOR");
            (void)co_await transport->async_write(std::span<const std::byte>{hb});
        }

        // Stay connected past the test's state-capture window (mirror of the US1
        // engine_acceptor_test client: a BOUNDED timer hold, not a fixed-duration
        // hold sized shorter than the capture). The initiator's read-pump drives
        // the session to Disconnected on peer EOF; if the server closed before the
        // capture, the test would observe Disconnected instead of Active. The hold
        // (5s) comfortably exceeds the capture run_for (3s); engine.stop() tears
        // the session down at end-of-test, and the teardown ioc.run() simply waits
        // out the remaining hold (bounded — cannot hang). [engine_acceptor_test L142]
        asio::steady_timer hold{executor};
        hold.expires_after(5s);
        co_await hold.async_wait(asio::use_awaitable);

    } catch (...) {
        // Any exception (including operation_aborted) is silently absorbed.
        // server_received_logon stays false — the correct RED outcome.
    }
}

}  // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Test: InitiatorConnectThenLogon [LOAD-BEARING RED]
//
// Registers ONE initiator in the engine. The fixture's server-side listener
// acts as the counterparty. Asserts three conditions that the current stub
// CANNOT satisfy:
//
//   1. server_received_logon == true
//      RED: stub never connects → server's async_accept times out → false.
//
//   2. initiator session state == fsm_state::Active
//      RED: open() transitions to LogonSent (initiator-role open() emits Logon
//      into the no-op transport_send_); stub never drives drive_reconnect() so
//      the peer Logon-ack is never received → state stays LogonSent.
//
//   3. next_inbound_unsafe() >= 2  (Logon-ack == seq 1 → advances 1→2)
//      RED: no peer frames delivered → next_inbound stays 1.
//
//   The EXPECTED_RED_REASON for each assertion is documented in the EXPECT
//   failure message.
// ─────────────────────────────────────────────────────────────────────────────
TEST(EngineConnectTest, InitiatorConnectThenLogon) {
    const char* fixture_dir = get_fixture_dir();
    if (!fixture_dir || fixture_dir[0] == '\0') GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    // ── Build TLS factory ────────────────────────────────────────────────────
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

    // ── Build the loopback TLS fixture (provides the server-side listener) ───
    asio::io_context ioc;
    fixpp::transport::test::LoopbackTlsFixture fixture{std::string(fixture_dir),
                                                       ioc.get_executor()};

    // The server's bound port — the initiator's reconnect_endpoint.
    uint16_t server_port = fixture.bound_port();
    ASSERT_NE(server_port, 0u) << "fixture listener did not bind";

    // ── Register ONE initiator in the engine ─────────────────────────────────
    // reconnect_endpoint targets the fixture's server listener port.
    // transport_send is the no-op rebindable slot (E-1 / E-1a).
    // CompIdAuthorizationPolicy: permissive — the server here is test code;
    // we only need the server's identity check to pass.  Use a policy that
    // allows the leaf cert CN ("fixpp-leaf-rsa2048") → "ACCEPTOR" binding.
    fixpp::session::CompIdAuthorizationPolicy authz;
    authz.add_binding("fixpp-leaf-rsa2048", "ACCEPTOR");

    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    // 041 T019: Engine::start() rejects a null clock with clock_not_set.
    eng_cfg.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};

    fixpp::session::SessionConfig ini;
    ini.sender_comp_id = "INITIATOR";
    ini.target_comp_id = "ACCEPTOR";
    ini.begin_string = "FIX.4.2";
    ini.role = fixpp::session::session_role::initiator;
    ini.executor_override = ioc.get_executor();
    ini.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
    ini.compid_authorization_policy = authz;
    ini.dictionary = fixpp::test_support::make_minimal_dictionary();
    ini.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    ini.transport_factory_override = fac;
    ini.heartbeat_interval = std::chrono::seconds{30};
    ini.logout_disconnect_timeout_ms = 2000;
    ini.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", server_port};
    // Rebindable no-op send slot (E-1 / E-1a): captured at open(), rebound to
    // live transport_send_ after drive_reconnect() succeeds.
    ini.transport_send = [](std::span<const std::byte>) {};

    auto ini_id = fixpp::session::SessionId::from_config(ini);
    ASSERT_TRUE(engine.register_session(std::move(ini)).has_value()) << "register_session failed";

    // ── Server driver: observability flag ────────────────────────────────────
    // std::atomic<bool> so the coroutine and the test body can share it safely.
    std::atomic<bool> server_received_logon{false};

    // Spawn the server driver coroutine BEFORE engine.start() so the listener
    // is already waiting when the initiator's connect loop fires.
    constexpr int kN = 2;  // N heartbeats sent after Logon-ack
    asio::co_spawn(ioc,
                   run_server_driver(fixture, server_received_logon, kN,
                                     /*deadline=*/3500ms),
                   asio::detached);

    // ── Start the engine ─────────────────────────────────────────────────────
    ASSERT_TRUE(engine.start().has_value()) << "engine.start() failed";

    // Run the executor for up to 4 seconds to allow:
    //   GREEN path: connect → handshake → Logon emission → Logon-ack + kN HBs
    //               → read-pump delivers all frames → state==Active
    //   RED path (stub):  run_connect_loop co_returns immediately;
    //               server_driver times out after 3.5s.
    ioc.run_for(4s);
    ioc.restart();

    // ── Capture state BEFORE stop() frees the session ────────────────────────
    auto ini_session = engine.lookup(ini_id);

    // Session should exist (it's constructed and open()ed in run_connect_loop).
    // If it's null the stub didn't even build the session — that's also a RED.
    fsm_state state = fsm_state::NotConnected;
    int next_inbound = 0;
    if (ini_session != nullptr) {
        state = ini_session->state();
#ifdef FIXPP_TEST_HOOKS
        next_inbound =
            static_cast<int>(ini_session->seqnum_mgr_test_access().next_inbound_unsafe());
#endif
    }

    // ── Stop the engine ───────────────────────────────────────────────────────
    auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    ioc.run();
    stop_fut.get();

    // ── Load-bearing RED assertions ───────────────────────────────────────────

    // Assertion 1: The server must have received the initiator's Logon on the
    //   wire (proving connect-then-Logon FR-003 ordering).
    //   RED: stub never calls async_connect → server's async_accept never fires →
    //        server_received_logon stays false.
    EXPECT_TRUE(server_received_logon.load(std::memory_order_acquire))
        << "FR-003 / C2i: the server must receive the initiator's Logon on the "
        << "wire after the transport is live (connect-then-Logon order). "
        << "RED: run_connect_loop stub never calls async_connect to the server; "
        << "server async_accept times out → server_received_logon == false. "
        << "GREEN (T016b-e): drive_reconnect() connects + handshakes; Logon is "
        << "emitted POST-connect over the live transport_send_.";

    // Assertion 2: The initiator session must have reached Active state.
    //   RED: open() transitions to LogonSent; stub co_returns without connecting;
    //        the server Logon-ack is never delivered → state stays LogonSent.
    EXPECT_EQ(state, fsm_state::Active)
        << "C2i / US2 AC1: the initiator session must reach Active after "
        << "connect + Logon + peer Logon-ack delivered by the read-pump. "
        << "RED: run_connect_loop stub: open() → LogonSent; no drive_reconnect() "
        << "→ no connection → no Logon-ack delivered → state stays LogonSent "
        << "(state=" << static_cast<int>(state) << "). "
        << "GREEN (T016b-e): drive_reconnect() + Logon emission + read-pump "
        << "delivers Logon-ack → LogonSent→Active.";

    // Assertion 3: next_inbound_unsafe() must have advanced past the initial
    //   value of 1 (Logon-ack seq=1 advances 1→2; each heartbeat adds 1 more).
    //   Minimum expected: 2 (Logon-ack delivered).
    //   RED: no frames delivered by the read-pump → next_inbound stays 1.
    EXPECT_EQ(next_inbound, 2 + kN)
        << "SC-010 / C2i: next_inbound_unsafe() must be exactly 2 + kN after the "
        << "Logon-ack (seq=1, 1→2) and the " << kN << " heartbeats (seq=2.." << (1 + kN)
        << ", each +1) are delivered IN ORDER by the read-pump. "
        << "RED: no drive_reconnect() → no live transport → no read-pump → "
        << "no frames delivered → next_inbound stays 1 "
        << "(actual=" << next_inbound << "). "
        << "GREEN (T016b-e): real read-pump delivers Logon-ack + " << kN
        << " heartbeats → next_inbound == " << (2 + kN) << ".";
}
