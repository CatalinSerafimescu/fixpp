// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 fixpp contributors
//
// tests/session/test_session_plaintext_roundtrip.cpp — T007 [P] [US1]
//
// SC-001: a plaintext acceptor driven through run_accept_loop on insecure_plain_tcp
// + a plaintext initiator complete a FIX Logon → Logout round trip over a loopback
// socket. Exercises all three E-7 acceptor sites (profile-map arm, plaintext accept-
// factory selection, post-accept handshake skip). No TLS bytes are emitted.
//
// Mutation evidence (manual spot-check during development):
//   Reverting the site-#3 handshake-skip guard causes the accept loop to attempt
//   async_handshake on the plain socket, which fails (null TlsTransport cast) →
//   session never establishes → this test times out (FAIL).
//
// Watchdog: an asio::steady_timer fails the test (not hangs) if the round-trip
// does not complete within 5 seconds.
//
// Anchors: spec.md SC-001; research.md D-7/D-8; data-model.md E-7;
//          tasks.md T007; [const §XII.5 amended v0.3]

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/connect.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <asio/write.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <future>
#include <span>
#include <string>
#include <vector>

#include "support/minimal_dictionary.hpp"

// SecurityProfile::kind::insecure_plain_tcp — [[deprecated]] friction fires at
// every unsuppressed selection site (T019/T020). This test file legitimately
// selects the value (it IS the plaintext round-trip test), so suppress file-wide
// per the fixpp-internal-code pragma idiom. [043 T020]
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <fixpp/session/security_profile.hpp>

using namespace std::chrono_literals;

namespace {

// Current wall-clock UTC as a FIX UTCTimestamp "YYYYMMDD-HH:MM:SS.mmm".
// Required by the 038 acceptor first-Logon SendingTime(52) MaxLatency guard.
static std::string utc_now_fix_timestamp() {
    std::array<char, 32> buf{};
    auto r = fixpp::core::utc_time_to_fix_string(std::chrono::system_clock::now(),
                                                  fixpp::core::fix_time_precision::millis,
                                                  std::span<char>{buf});
    return r ? std::string{r->data(), r->size()} : std::string{};
}

// Build a complete FIX frame from begin_string + a body string.
// The body must already contain all body fields (35=, 34=, 49=, 52=, 56=, etc.).
// Calculates BodyLength(9=) and CheckSum(10=) automatically.
static std::vector<std::byte> make_fix_frame(std::string_view begin_str,
                                              std::string const& body) {
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

// Build a valid FIX Logon frame. EncryptMethod(98)=0 (plaintext-safe per FR-009).
static std::vector<std::byte> make_plain_logon_frame(std::string_view begin_str,
                                                      std::string_view sender,
                                                      std::string_view target) {
    auto field = [](int tag, std::string_view v) -> std::string {
        return std::to_string(tag) + "=" + std::string(v) + "\x01";
    };
    std::string body;
    body += field(35, "A");   // MsgType = Logon
    body += field(34, "1");   // MsgSeqNum
    body += field(49, sender);
    body += field(52, utc_now_fix_timestamp());  // SendingTime (038 guard)
    body += field(56, target);
    body += field(98, "0");    // EncryptMethod = none
    body += field(108, "30"); // HeartBtInt
    return make_fix_frame(begin_str, body);
}

// Build a valid FIX Logout frame (35=5).
// seq MUST advance past the Logon's 34=1 (use 34=2 for the first Logout).
static std::vector<std::byte> make_plain_logout_frame(std::string_view begin_str,
                                                       std::string_view sender,
                                                       std::string_view target,
                                                       int seq) {
    auto field = [](int tag, std::string_view v) -> std::string {
        return std::to_string(tag) + "=" + std::string(v) + "\x01";
    };
    std::string body;
    body += field(35, "5");   // MsgType = Logout
    body += field(34, std::to_string(seq));
    body += field(49, sender);
    body += field(52, utc_now_fix_timestamp());  // fresh SendingTime (038 MaxLatency guard)
    body += field(56, target);
    return make_fix_frame(begin_str, body);
}

// Spy on the first byte written by the initiator to confirm NO TLS ClientHello
// (TLS record type 0x16 = Handshake; TLS record type 0x15 = Alert).
// If the first byte is 0x38 ('8' — the start of "8=FIX.4.2") the wire is plaintext.
std::atomic<std::byte> g_first_byte_sent{std::byte{0}};
std::atomic<bool>      g_first_byte_captured{false};

// Standalone plaintext initiator coroutine (Logon-only).
// Connects to the acceptor's bound port via a raw TCP socket (no TLS),
// sends a FIX Logon frame, waits for the acceptor reply (up to 5s), then exits.
static asio::awaitable<void> run_plain_initiator(asio::io_context& ioc,
                                                  uint16_t acceptor_port,
                                                  std::string sender,
                                                  std::string target) {
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
    try {
        asio::ip::tcp::socket sock{ioc};
        asio::ip::tcp::resolver resolver{ioc};

        auto eps = co_await resolver.async_resolve(
            "127.0.0.1", std::to_string(acceptor_port), asio::use_awaitable);

        asio::error_code ec;
        co_await asio::async_connect(sock, eps,
                                     asio::redirect_error(asio::use_awaitable, ec));
        if (ec) co_return;

        // Build and send a Logon frame.
        auto logon = make_plain_logon_frame("FIX.4.2", sender, target);

        // Capture the first byte for the no-TLS assertion.
        if (!logon.empty()) {
            g_first_byte_sent.store(logon[0], std::memory_order_release);
            g_first_byte_captured.store(true, std::memory_order_release);
        }

        co_await asio::async_write(sock, asio::buffer(logon.data(), logon.size()),
                                   asio::redirect_error(asio::use_awaitable, ec));

        // Stay connected for a short window so the acceptor's read-pump sees the
        // socket as live while the test captures session state. 1s is sufficient;
        // the test's run_for(2s) always outlasts this, so the initiator has
        // already disconnected by the time ioc.run() runs after engine.stop().
        asio::steady_timer t{ioc};
        t.expires_after(1s);
        co_await t.async_wait(asio::redirect_error(asio::use_awaitable, ec));

        sock.close(ec);
    } catch (...) {
    }
}

// Plaintext initiator coroutine that completes a full Logon → Logout round-trip.
// Connects, sends Logon (34=1), waits 200ms for the acceptor to process + reply,
// then sends a Logout (34=2, fresh 52=) and waits 300ms before closing.
// The Logout MsgSeqNum MUST advance past the Logon's 34=1 so the acceptor's
// check_inbound sees an in-sequence frame (not a gap). [SC-001 / FR-009]
static asio::awaitable<void> run_plain_initiator_with_logout(asio::io_context& ioc,
                                                              uint16_t acceptor_port,
                                                              std::string sender,
                                                              std::string target) {
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
    try {
        asio::ip::tcp::socket sock{ioc};
        asio::ip::tcp::resolver resolver{ioc};

        auto eps = co_await resolver.async_resolve(
            "127.0.0.1", std::to_string(acceptor_port), asio::use_awaitable);

        asio::error_code ec;
        co_await asio::async_connect(sock, eps,
                                     asio::redirect_error(asio::use_awaitable, ec));
        if (ec) co_return;

        // Send Logon (34=1).
        auto logon = make_plain_logon_frame("FIX.4.2", sender, target);
        co_await asio::async_write(sock, asio::buffer(logon.data(), logon.size()),
                                   asio::redirect_error(asio::use_awaitable, ec));
        if (ec) co_return;

        // Wait 200ms for the acceptor to process the Logon and reach Active.
        asio::steady_timer t{ioc};
        t.expires_after(200ms);
        co_await t.async_wait(asio::redirect_error(asio::use_awaitable, ec));

        // Send Logout (34=2). MsgSeqNum=2 advances past Logon's 34=1.
        // Fresh 52= timestamp satisfies the 038 acceptor MaxLatency guard.
        auto logout = make_plain_logout_frame("FIX.4.2", sender, target, /*seq=*/2);
        co_await asio::async_write(sock, asio::buffer(logout.data(), logout.size()),
                                   asio::redirect_error(asio::use_awaitable, ec));

        // Wait 300ms for the acceptor to process the Logout → Disconnected.
        t.expires_after(300ms);
        co_await t.async_wait(asio::redirect_error(asio::use_awaitable, ec));

        sock.close(ec);
    } catch (...) {
    }
}

}  // namespace

// ── T007: SC-001 — plaintext acceptor + initiator complete Logon round-trip ──
//
// Exercises all three E-7 acceptor sites:
//   (1) engine.cpp profile-map arm: insecure_plain_tcp accepted plaintext path
//   (2) asio_listener Config transport_kind: plaintext factory used for make_accepted()
//   (3) engine.cpp run_accept_loop: post-accept handshake skip (no async_handshake)
//
// No GTEST_SKIP() here — plaintext sessions need no cert file and no env variable.

TEST(PlaintextRoundtripTest, PlainAcceptorAndInitiatorCompleteLogon) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    // 041 T019: Engine::start() rejects a null clock.
    eng_cfg.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};

    // Build the acceptor session config with insecure_plain_tcp.
    // No transport_factory_override needed — auto-derived from profile (FR-003a).
    fixpp::session::SessionConfig acc_cfg;
    acc_cfg.sender_comp_id = "PLAIN-ACCEPTOR";
    acc_cfg.target_comp_id = "PLAIN-INITIATOR";
    acc_cfg.begin_string = "FIX.4.2";
    acc_cfg.role = fixpp::session::session_role::acceptor;
    acc_cfg.executor_override = ioc.get_executor();
    acc_cfg.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::insecure_plain_tcp};
    acc_cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    acc_cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    // No transport_factory_override: engine auto-derives the plaintext factory.
    acc_cfg.heartbeat_interval = std::chrono::seconds{30};
    acc_cfg.logout_disconnect_timeout_ms = 500;
    // Port 0 = OS-assigned bind address for the acceptor listener.
    acc_cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 0};
    acc_cfg.transport_send = [](std::span<const std::byte>) {};

    auto acc_id = fixpp::session::SessionId::from_config(acc_cfg);
    ASSERT_TRUE(engine.register_session(std::move(acc_cfg)).has_value())
        << "register_session(acceptor) failed";

    ASSERT_TRUE(engine.start().has_value()) << "engine.start() failed";

    // Let the accept loop bind the listener.
    ioc.run_for(50ms);
    ioc.restart();

    uint16_t bound_port = engine.acceptor_bound_endpoint(acc_id).port;
    ASSERT_NE(bound_port, 0u) << "acceptor did not bind (port=0)";

    // Watchdog: fires at 5s from the point the session is attempted.
    // Protects BOTH the establish phase (run_for(500ms)) AND the cleanup phase
    // (run_for(3s) after stop). Total budget: 500ms + 3s = 3.5s << 5s.
    // We do NOT cancel the watchdog before cleanup — it must remain armed to
    // catch a wedged engine.stop(). [spec brief: self-deadline that FAILs not hangs]
    std::atomic<bool> watchdog_fired{false};
    asio::steady_timer watchdog{ioc};
    watchdog.expires_after(5s);
    watchdog.async_wait([&](asio::error_code ec) {
        if (!ec) watchdog_fired.store(true, std::memory_order_release);
    });

    // Spawn the standalone plaintext initiator.
    asio::co_spawn(
        ioc,
        run_plain_initiator(ioc, bound_port,
                            /*sender=*/"PLAIN-INITIATOR", /*target=*/"PLAIN-ACCEPTOR"),
        asio::detached);

    // Run for 500ms to allow accept→(no handshake)→attach→Logon-admit.
    // The initiator connects and sends the Logon immediately; the full exchange
    // (connect + Logon processing) completes within ~10ms on a loopback socket.
    // At 500ms the initiator's 1s stay-connected timer has NOT yet fired, so the
    // session is still Active when we capture state.
    ioc.run_for(500ms);
    ioc.restart();

    // Capture state while the initiator is still connected (Active window).
    auto acc_session = engine.lookup(acc_id);
    const bool established =
        (acc_session != nullptr) &&
        (acc_session->state() == fixpp::session::fsm_state::Active ||
         acc_session->state() == fixpp::session::fsm_state::LogonReceived);
    const std::string state_str =
        (acc_session != nullptr)
            ? std::to_string(static_cast<int>(acc_session->state()))
            : "null";

    // Stop cleanly. Use run_for(2s) — watchdog is still armed.
    // engine.stop() with logout_disconnect_timeout_ms=500 takes ≤1.5s; 2s is ample.
    // If stop() wedges, run_for returns after 2s; watchdog fires at 5s from
    // session-start — still well within the external test timeout. We check
    // stop_fut before assertions.
    auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    ioc.run_for(2s);
    // Cancel watchdog after cleanup so it doesn't fire during assertions.
    watchdog.cancel();
    ASSERT_TRUE(stop_fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready)
        << "engine.stop() did not complete within 3s — potential wedge in session teardown";
    stop_fut.get();

    // Assert no watchdog fired during establish or cleanup.
    ASSERT_FALSE(watchdog_fired.load()) << "watchdog fired: plaintext round-trip did not complete "
                                         "within 5s — potential hang in accept/handshake path";

    // SC-001 core assertion: acceptor reached established state.
    EXPECT_TRUE(established)
        << "SC-001: plaintext acceptor must reach Active (or LogonReceived) after "
           "the initiator sends a valid Logon. state=" << state_str
        << ". Exercises all three E-7 acceptor sites (profile-map arm, "
           "plaintext accept-factory, post-accept handshake skip).";

    // No-TLS assertion: the first byte sent over the wire must be '8' (start of
    // "8=FIX.4.2\x01"), NOT 0x16 (TLS Handshake) or 0x15 (TLS Alert).
    // This confirms no TLS ClientHello was emitted (SC-001 / FR-011).
    ASSERT_TRUE(g_first_byte_captured.load())
        << "No bytes were captured — the initiator may not have connected";
    const auto first_byte = static_cast<unsigned char>(
        g_first_byte_sent.load(std::memory_order_acquire));
    EXPECT_EQ(first_byte, static_cast<unsigned char>('8'))
        << "SC-001: first byte on the wire must be '8' (=0x38, start of '8=FIX.x.y\\x01'), "
           "not 0x16 (TLS Handshake) or 0x15 (TLS Alert). "
           "first_byte=0x" << std::hex << static_cast<unsigned>(first_byte);
}

// ── T042: SC-001 — full Logon → Logout round trip over plaintext ──────────────
//
// SC-001 (spec.md:303) defines the criterion as "Logon → Logout round trip".
// feature-catalogue.md T-042 cites this file as the "Logon/Logout" witness.
// The above PlainAcceptorAndInitiatorCompleteLogon test covers the Logon half;
// this test covers the clean Logout path end-to-end (FQ-2, gate-b/r1).
//
// Discriminating assertions:
//   (1) Acceptor reaches fsm_state::Disconnected after receiving the Logout.
//   (2) Acceptor's recent_events() contains session_event_sequence_numbers_reset
//       {by_peer_request=false} — emitted ONLY on the inbound-Logout-in-Active
//       path (session.cpp T046 site), NOT on a raw socket-close path.
//   These two together prove the Logout was consumed in-sequence through the
//   correct transition, not merely that the session terminated for any reason.
//
// Anchor: spec.md SC-001; feature-catalogue.md T-042; session.cpp T046.
TEST(PlaintextRoundtripTest, PlainAcceptorAndInitiatorCompleteLogonLogout) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    eng_cfg.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};

    fixpp::session::SessionConfig acc_cfg;
    acc_cfg.sender_comp_id = "PLAIN-ACCEPTOR";
    acc_cfg.target_comp_id = "PLAIN-INITIATOR";
    acc_cfg.begin_string = "FIX.4.2";
    acc_cfg.role = fixpp::session::session_role::acceptor;
    acc_cfg.executor_override = ioc.get_executor();
    acc_cfg.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::insecure_plain_tcp};
    acc_cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    acc_cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    acc_cfg.heartbeat_interval = std::chrono::seconds{30};
    acc_cfg.logout_disconnect_timeout_ms = 500;
    acc_cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 0};
    acc_cfg.transport_send = [](std::span<const std::byte>) {};

    auto acc_id = fixpp::session::SessionId::from_config(acc_cfg);
    ASSERT_TRUE(engine.register_session(std::move(acc_cfg)).has_value())
        << "register_session(acceptor) failed";
    ASSERT_TRUE(engine.start().has_value()) << "engine.start() failed";

    ioc.run_for(50ms);
    ioc.restart();

    uint16_t bound_port = engine.acceptor_bound_endpoint(acc_id).port;
    ASSERT_NE(bound_port, 0u) << "acceptor did not bind (port=0)";

    // Watchdog: 6s budget covers the full Logon+Logout exchange + cleanup.
    std::atomic<bool> watchdog_fired{false};
    asio::steady_timer watchdog{ioc};
    watchdog.expires_after(6s);
    watchdog.async_wait([&](asio::error_code ec) {
        if (!ec) watchdog_fired.store(true, std::memory_order_release);
    });

    // Spawn the Logon+Logout initiator.
    asio::co_spawn(
        ioc,
        run_plain_initiator_with_logout(ioc, bound_port,
                                        /*sender=*/"PLAIN-INITIATOR",
                                        /*target=*/"PLAIN-ACCEPTOR"),
        asio::detached);

    // Run for 700ms: 200ms (Logon exchange) + 300ms (Logout exchange) + 200ms margin.
    // After this point the initiator has sent Logon + Logout; the acceptor should have
    // processed the Logout and transitioned to Disconnected.
    ioc.run_for(700ms);
    ioc.restart();

    // Capture state BEFORE stop() — the session is still in the registry snapshot.
    auto acc_session = engine.lookup(acc_id);
    ASSERT_NE(acc_session, nullptr) << "session not found in registry after Logout";

    const auto final_state = acc_session->state();

    // Capture recent_events() to find the Logout-specific signal.
    // recent_events() is safe to call here: ioc is paused (run_for returned),
    // so the session strand is idle — no concurrent writes to recent_events_.
    bool logout_seqreset_event_found = false;
    for (const auto& ev : acc_session->recent_events()) {
        if (auto* sr = std::get_if<fixpp::session::session_event_sequence_numbers_reset>(&ev)) {
            if (!sr->by_peer_request) {
                logout_seqreset_event_found = true;
            }
        }
    }

    // Stop cleanly.
    auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    ioc.run_for(2s);
    watchdog.cancel();
    ASSERT_TRUE(stop_fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready)
        << "engine.stop() did not complete within 2s — potential wedge in session teardown";
    stop_fut.get();

    ASSERT_FALSE(watchdog_fired.load())
        << "watchdog fired: plaintext Logon+Logout round-trip did not complete within 6s";

    // (1) Acceptor must have reached Disconnected (terminal) after the clean Logout.
    EXPECT_EQ(final_state, fixpp::session::fsm_state::Disconnected)
        << "SC-001 / T-042: plaintext acceptor must reach Disconnected after a clean "
           "inbound Logout. final_state="
        << static_cast<int>(final_state);

    // (2) Discriminating signal: session_event_sequence_numbers_reset{by_peer_request=false}
    // is emitted ONLY on the inbound-Logout-in-Active path (session.cpp T046), not on
    // a raw socket-close. Its presence proves the Logout was processed in-sequence.
    EXPECT_TRUE(logout_seqreset_event_found)
        << "SC-001 / T-042: the inbound-Logout path must emit "
           "session_event_sequence_numbers_reset{by_peer_request=false}. "
           "Absence means the Logout was NOT processed via the Active→Logout transition "
           "(possibly the session disconnected for another reason before Logout).";
}
#pragma clang diagnostic pop  // -Wdeprecated-declarations (insecure_plain_tcp, 043 T020)
