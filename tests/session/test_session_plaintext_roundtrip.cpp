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
// Watchdog: an asio::steady_timer fails the test (not hangs) if the round-trip
// does not complete within its establish/state pump plus the stop window.
//
// Anchors: spec.md SC-001; research.md D-7/D-8; data-model.md E-7;
//          tasks.md T007; [const §XII.5 amended v0.3]

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/connect.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/post.hpp>
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
#include <cstdio>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <functional>
#include <future>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/pump_until_ready.hpp"

// ── #289: bounded pumps ──────────────────────────────────────────────
//
// The round-trip tests' `/stop` windows use `run_window_then_ready` plus a
// miss-branch drain (tests/support/pump_until_ready.hpp). The window is PRESERVED: the hazard #289
// names is the UNCONDITIONAL `get()`, not the fixed window.
//
// ⚠️ THEY ARE NORMALISATIONS, NOT HAZARD FIXES. An
// `ASSERT_TRUE(stop_fut.wait_for(0s) == ready)` already stood between the window and
// the `get()` at each. What the migration buys is the shared report text and the
// FORCING SEAM. The census flags them because it is LEXICAL and cannot see an
// assertion standing between the two lines it matches.
//
// ⚠️ THE VERDICT IS CAPTURED BEFORE `watchdog.cancel()`, AND THE ORDER IS
// LOAD-BEARING IN BOTH DIRECTIONS. The window must stay INSIDE the armed watchdog --
// that is what the original `run_for(2s)` comment says it is for -- so the pump
// happens first. But the miss-branch DRAIN must run AFTER the cancel: its budget can
// reach the watchdog's deadline, so draining with the timer still armed would let a
// REAL `steady_timer`
// set `watchdog_fired` during failure handling and report a second, spurious defect.
// [[feedback_a_pump_budget_above_a_real_fallback_timer_turns_a_hang_into_a_false_pass]]
//
// The drain is the CLOCKED one, spelled `*engine.clock()`: each test installs a real
// `system_clock_source` into its `EngineConfig` and `std::move`s that config into the
// engine, so the accessor is the only live spelling. Non-nullness rests on the
// assignment two statements above each engine's construction, not on the accessor's
// "never null post-construction" comment, which is #289's standing known-false one.

// SecurityProfile::kind::insecure_plain_tcp — [[deprecated]] friction fires at
// every unsuppressed selection site (T019/T020). This test file legitimately
// selects the value (it IS the plaintext round-trip test), so suppress file-wide
// per the fixpp-internal-code pragma idiom. [043 T020]
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <fixpp/session/security_profile.hpp>

using namespace std::chrono_literals;

namespace {

// Budget for the pump that waits for the acceptor to reach the state a test
// asserts (#470). It replaces a fixed `run_for` window, which misses whenever the
// thread is descheduled past the window, so it is a wedge detector, not a latency
// claim.
constexpr auto kStateBudget = 3s;

// Window `engine.stop()` is given before the test reports a teardown miss.
constexpr auto kStopWindow = 2s;

// The Logon-only initiator must stay connected for longer than `kStateBudget`:
// once it closes, the acceptor leaves Active, so a pump still waiting at that point
// would miss a state it can no longer observe.
constexpr auto kInitiatorHold = kStateBudget + 1s;

// Current wall-clock UTC as a FIX UTCTimestamp "YYYYMMDD-HH:MM:SS.mmm".
// Required by the 038 acceptor first-Logon SendingTime(52) MaxLatency guard.
std::string utc_now_fix_timestamp() {
    std::array<char, 32> buf{};
    auto r = fixpp::core::utc_time_to_fix_string(std::chrono::system_clock::now(),
                                                 fixpp::core::fix_time_precision::millis,
                                                 std::span<char>{buf});
    return r ? std::string{r->data(), r->size()} : std::string{};
}

// Build a complete FIX frame from begin_string + a body string.
// The body must already contain all body fields (35=, 34=, 49=, 52=, 56=, etc.).
// Calculates BodyLength(9=) and CheckSum(10=) automatically.
std::vector<std::byte> make_fix_frame(std::string_view begin_str, std::string const& body) {
    std::string msg;
    msg += "8=" + std::string(begin_str) + "\x01";
    msg += "9=" + std::to_string(body.size()) + "\x01";
    msg += body;
    unsigned int cs = 0;
    for (unsigned char c : msg) cs += c;
    cs &= 0xFFU;
    char csbuf[5];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    msg += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> out;
    out.reserve(msg.size());
    for (char c : msg) out.push_back(static_cast<std::byte>(c));
    return out;
}

// Build a valid FIX Logon frame. EncryptMethod(98)=0 (plaintext-safe per FR-009).
// `extra` is appended after HeartBtInt(108): whole fields, each SOH-terminated.
std::vector<std::byte> make_plain_logon_frame(std::string_view begin_str, std::string_view sender,
                                              std::string_view target,
                                              std::string_view extra = {}) {
    auto field = [](int tag, std::string_view v) -> std::string {
        return std::to_string(tag) + "=" + std::string(v) + "\x01";
    };
    std::string body;
    body += field(35, "A");  // MsgType = Logon
    body += field(34, "1");  // MsgSeqNum
    body += field(49, sender);
    body += field(52, utc_now_fix_timestamp());  // SendingTime (038 guard)
    body += field(56, target);
    body += field(98, "0");    // EncryptMethod = none
    body += field(108, "30");  // HeartBtInt
    body += extra;
    return make_fix_frame(begin_str, body);
}

// Build a valid FIX Logout frame (35=5).
// seq MUST advance past the Logon's 34=1 (use 34=2 for the first Logout).
std::vector<std::byte> make_plain_logout_frame(std::string_view begin_str, std::string_view sender,
                                               std::string_view target, int seq) {
    auto field = [](int tag, std::string_view v) -> std::string {
        return std::to_string(tag) + "=" + std::string(v) + "\x01";
    };
    std::string body;
    body += field(35, "5");  // MsgType = Logout
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
std::atomic<bool> g_first_byte_captured{false};

// Standalone plaintext initiator coroutine (Logon-only).
// Connects to the acceptor's bound port via a raw TCP socket (no TLS),
// sends a FIX Logon frame (with `logon_extra` fields), holds the socket open (see
// kInitiatorHold), then closes.
asio::awaitable<void> run_plain_initiator(asio::io_context& ioc, uint16_t acceptor_port,
                                          std::string sender, std::string target,
                                          std::string logon_extra = {}) {
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
    try {
        asio::ip::tcp::socket sock{ioc};
        asio::ip::tcp::resolver resolver{ioc};

        auto eps = co_await resolver.async_resolve("127.0.0.1", std::to_string(acceptor_port),
                                                   asio::use_awaitable);

        asio::error_code ec;
        co_await asio::async_connect(sock, eps, asio::redirect_error(asio::use_awaitable, ec));
        if (ec) co_return;

        // Build and send a Logon frame.
        auto logon = make_plain_logon_frame("FIX.4.2", sender, target, logon_extra);

        // Capture the first byte for the no-TLS assertion.
        if (!logon.empty()) {
            g_first_byte_sent.store(logon[0], std::memory_order_release);
            g_first_byte_captured.store(true, std::memory_order_release);
        }

        co_await asio::async_write(sock, asio::buffer(logon.data(), logon.size()),
                                   asio::redirect_error(asio::use_awaitable, ec));

        // Stay connected so the acceptor's read-pump sees the socket as live while
        // the test waits for Active; see kInitiatorHold for the ordering it needs.
        asio::steady_timer t{ioc};
        t.expires_after(kInitiatorHold);
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
asio::awaitable<void> run_plain_initiator_with_logout(asio::io_context& ioc, uint16_t acceptor_port,
                                                      std::string sender, std::string target) {
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
    try {
        asio::ip::tcp::socket sock{ioc};
        asio::ip::tcp::resolver resolver{ioc};

        auto eps = co_await resolver.async_resolve("127.0.0.1", std::to_string(acceptor_port),
                                                   asio::use_awaitable);

        asio::error_code ec;
        co_await asio::async_connect(sock, eps, asio::redirect_error(asio::use_awaitable, ec));
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
    // Other tests in this binary also run run_plain_initiator; the capture must be this one's.
    g_first_byte_captured.store(false, std::memory_order_release);
    g_first_byte_sent.store(std::byte{0}, std::memory_order_release);
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

    // Let the accept loop bind the listener, not for a fixed window (#470): a fixed
    // window misses whenever the accept-loop thread is descheduled past it. No fatal
    // assertion runs until after engine.stop() below -- see the ASSERT_NE there.
    uint16_t bound_port = 0;
    (void)fixpp::test_support::pump_until(
        ioc,
        [&] {
            bound_port = engine.acceptor_bound_endpoint(acc_id).port;
            return bound_port != 0;
        },
        kStateBudget, fixpp::test_support::kPumpSlice,
        "PlainAcceptorAndInitiatorCompleteLogon/bind");

    // Watchdog: armed from the point the session is attempted, and longer than the
    // establish pump plus the stop window, so it can fire only when one of them
    // overran. It stays armed through the stop window to catch a wedged
    // engine.stop(). [spec brief: self-deadline that FAILs not hangs]
    std::atomic<bool> watchdog_fired{false};
    asio::steady_timer watchdog{ioc};
    watchdog.expires_after(kStateBudget + kStopWindow + 1s);
    watchdog.async_wait([&](asio::error_code ec) {
        if (!ec) watchdog_fired.store(true, std::memory_order_release);
    });

    // Pump until accept→(no handshake)→attach→Logon-admit is observed, not for a fixed
    // window (#470). The acceptor session is published to lookup() only after the
    // accept, so `null` below means the accept was not observed within the budget.
    //
    // Latched: the first observation of Active/LogonReceived ends the wait. Reading
    // state() here is safe only because this thread is the one driving `ioc`, and the
    // predicate runs between `run_for` slices, when no session-strand handler is
    // mid-flight.
    //
    // Only attempted when the bind succeeded: a bind miss leaves bound_port == 0, and
    // nothing would ever connect, so this pump would exist only to consume its own
    // budget before the fatal bind assertion below runs.
    bool established = false;
    std::string state_str = "null";
    bool establish_pumped = true;
    if (bound_port != 0) {
        // Spawn the standalone plaintext initiator.
        asio::co_spawn(ioc,
                       run_plain_initiator(ioc, bound_port,
                                           /*sender=*/"PLAIN-INITIATOR",
                                           /*target=*/"PLAIN-ACCEPTOR"),
                       asio::detached);

        const auto observe_established = [&] {
            if (established) return true;
            auto acc_session = engine.lookup(acc_id);
            if (acc_session == nullptr) return false;
            const auto st = acc_session->state();
            state_str = std::to_string(static_cast<int>(st));
            established = st == fixpp::session::fsm_state::Active ||
                          st == fixpp::session::fsm_state::LogonReceived;
            return established;
        };
        establish_pumped = fixpp::test_support::pump_until(
            ioc, observe_established, kStateBudget, fixpp::test_support::kPumpSlice,
            "PlainAcceptorAndInitiatorCompleteLogon/establish");
    }

    // Stop cleanly, with the watchdog still armed. We check stop_fut before assertions.
    auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    const bool stopped_in_window = fixpp::test_support::run_window_then_ready(
        ioc, stop_fut, kStopWindow, "PlainAcceptorAndInitiatorCompleteLogon/stop");
    // Cancel watchdog after cleanup so it doesn't fire during assertions — and, on the
    // miss branch, before the drain, whose budget reaches the watchdog's deadline.
    watchdog.cancel();
    if (!stopped_in_window) {
        fixpp::test_support::cancel_and_drain_or_report(
            ioc, *engine.clock(), "PlainAcceptorAndInitiatorCompleteLogon/stop");
        // A miss means engine.stop() did not complete within kStopWindow -- a potential
        // wedge in session teardown. Report text is the stem plus the label, nothing else.
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "PlainAcceptorAndInitiatorCompleteLogon/stop";
        return;
    }
    stop_fut.get();

    // Assert no watchdog fired during establish or cleanup.
    ASSERT_FALSE(watchdog_fired.load()) << "watchdog fired: plaintext round-trip overran the "
                                           "establish budget plus the stop window — potential "
                                           "hang in accept/handshake path";

    // Bind must have succeeded -- checked only now, after a completed engine.stop(),
    // so a bind miss never destroys a started-but-not-yet-stopped Engine.
    ASSERT_NE(bound_port, 0U) << fixpp::test_support::kPumpBudgetMiss
                              << "PlainAcceptorAndInitiatorCompleteLogon/bind"
                              << " -- acceptor did not bind";

    EXPECT_TRUE(establish_pumped) << fixpp::test_support::kPumpBudgetMiss
                                  << "PlainAcceptorAndInitiatorCompleteLogon/establish";

    // SC-001 core assertion: acceptor reached established state.
    EXPECT_TRUE(established)
        << "SC-001: plaintext acceptor must reach Active (or LogonReceived) after "
           "the initiator sends a valid Logon. state="
        << state_str
        << ". Exercises all three E-7 acceptor sites (profile-map arm, "
           "plaintext accept-factory, post-accept handshake skip).";

    // No-TLS assertion: the first byte sent over the wire must be '8' (start of
    // "8=FIX.4.2\x01"), NOT 0x16 (TLS Handshake) or 0x15 (TLS Alert).
    // This confirms no TLS ClientHello was emitted (SC-001 / FR-011).
    ASSERT_TRUE(g_first_byte_captured.load())
        << "No bytes were captured — the initiator may not have connected";
    const auto first_byte =
        static_cast<unsigned char>(g_first_byte_sent.load(std::memory_order_acquire));
    EXPECT_EQ(first_byte, static_cast<unsigned char>('8'))
        << "SC-001: first byte on the wire must be '8' (=0x38, start of '8=FIX.x.y\\x01'), "
           "not 0x16 (TLS Handshake) or 0x15 (TLS Alert). "
           "first_byte=0x"
        << std::hex << static_cast<unsigned>(first_byte);
}

// ── T042: SC-001 — full Logon → Logout round trip over plaintext ──────────────
//
// SC-001 (043's spec.md) defines the criterion as "Logon → Logout round trip".
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

    // Let the accept loop bind the listener, not for a fixed window (#470). No fatal
    // assertion runs until after engine.stop() below -- see the ASSERT_NE there.
    uint16_t bound_port = 0;
    (void)fixpp::test_support::pump_until(
        ioc,
        [&] {
            bound_port = engine.acceptor_bound_endpoint(acc_id).port;
            return bound_port != 0;
        },
        kStateBudget, fixpp::test_support::kPumpSlice,
        "PlainAcceptorAndInitiatorCompleteLogonLogout/bind");

    // Watchdog: longer than the state pump plus the stop window, so it fires only
    // when one of them overran.
    std::atomic<bool> watchdog_fired{false};
    asio::steady_timer watchdog{ioc};
    watchdog.expires_after(kStateBudget + kStopWindow + 1s);
    watchdog.async_wait([&](asio::error_code ec) {
        if (!ec) watchdog_fired.store(true, std::memory_order_release);
    });

    // Pump until the acceptor is observed Disconnected, not for a fixed window (#470).
    // Disconnected is also reachable by a raw socket close, which the initiator does
    // after its Logout; that is why assertion (2) below, not this wait, is what tells
    // the Logout path apart. Same threading condition as the Logon test's pump.
    //
    // Only attempted when the bind succeeded -- see the Logon test's twin pump for why.
    std::shared_ptr<fixpp::session::Session> acc_session;
    bool disconnected_pumped = true;
    if (bound_port != 0) {
        // Spawn the Logon+Logout initiator.
        asio::co_spawn(ioc,
                       run_plain_initiator_with_logout(ioc, bound_port,
                                                       /*sender=*/"PLAIN-INITIATOR",
                                                       /*target=*/"PLAIN-ACCEPTOR"),
                       asio::detached);

        disconnected_pumped = fixpp::test_support::pump_until(
            ioc,
            [&] {
                acc_session = engine.lookup(acc_id);
                return acc_session != nullptr &&
                       acc_session->state() == fixpp::session::fsm_state::Disconnected;
            },
            kStateBudget, fixpp::test_support::kPumpSlice,
            "PlainAcceptorAndInitiatorCompleteLogonLogout/disconnected");
    }

    // Capture state and events BEFORE stop() — the session is still in the registry
    // snapshot, and the strand is idle here (ioc is not being run). `found` and
    // `final_state` are recorded rather than asserted immediately: a fatal assertion
    // here, before engine.stop() runs, would tear down a started-but-not-yet-stopped
    // Engine.
    const bool found = acc_session != nullptr;
    std::optional<fixpp::session::fsm_state> final_state;
    bool logout_seqreset_event_found = false;
    if (found) {
        final_state = acc_session->state();

        // recent_events() is safe to call here: ioc is not being run (the pump
        // returned), so the session strand is idle — no concurrent writes to
        // recent_events_.
        for (const auto& ev : acc_session->recent_events()) {
            if (const auto* sr =
                    std::get_if<fixpp::session::session_event_sequence_numbers_reset>(&ev)) {
                if (!sr->by_peer_request) {
                    logout_seqreset_event_found = true;
                }
            }
        }
    }

    // Stop cleanly.
    auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    const bool stopped_in_window = fixpp::test_support::run_window_then_ready(
        ioc, stop_fut, kStopWindow, "PlainAcceptorAndInitiatorCompleteLogonLogout/stop");
    watchdog.cancel();
    if (!stopped_in_window) {
        fixpp::test_support::cancel_and_drain_or_report(
            ioc, *engine.clock(), "PlainAcceptorAndInitiatorCompleteLogonLogout/stop");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "PlainAcceptorAndInitiatorCompleteLogonLogout/stop";
        return;
    }
    stop_fut.get();

    ASSERT_FALSE(watchdog_fired.load())
        << "watchdog fired: plaintext Logon+Logout round-trip overran the state budget plus "
           "the stop window";

    // Bind must have succeeded -- checked only now, after a completed engine.stop().
    ASSERT_NE(bound_port, 0U) << fixpp::test_support::kPumpBudgetMiss
                              << "PlainAcceptorAndInitiatorCompleteLogonLogout/bind"
                              << " -- acceptor did not bind";

    EXPECT_TRUE(disconnected_pumped) << fixpp::test_support::kPumpBudgetMiss
                                     << "PlainAcceptorAndInitiatorCompleteLogonLogout/disconnected";

    ASSERT_TRUE(found) << "session not found in registry after Logout";

    // (1) Acceptor must have reached Disconnected (terminal) after the clean Logout.
    EXPECT_EQ(final_state, std::optional{fixpp::session::fsm_state::Disconnected})
        << "SC-001 / T-042: plaintext acceptor must reach Disconnected after a clean "
           "inbound Logout. final_state="
        << (final_state ? std::to_string(static_cast<int>(*final_state)) : std::string{"-1"});

    // (2) Discriminating signal: session_event_sequence_numbers_reset{by_peer_request=false}
    // is emitted ONLY on the inbound-Logout-in-Active path (session.cpp T046), not on
    // a raw socket-close. Its presence proves the Logout was processed in-sequence.
    EXPECT_TRUE(logout_seqreset_event_found)
        << "SC-001 / T-042: the inbound-Logout path must emit "
           "session_event_sequence_numbers_reset{by_peer_request=false}. "
           "Absence means the Logout was NOT processed via the Active→Logout transition "
           "(possibly the session disconnected for another reason before Logout).";
}

// ── fixpp#518: a close() that runs while a Logon arm is suspended ─────────────
//
// Each cell lets close(mode) land while a Logon arm is suspended, from one of two
// places: a toAdmin the arm fires, where the application reaches the session through
// the public Engine::lookup(), or a store hook standing in for another thread. It is
// posted with asio::post, not co_spawn: the vendored co_spawn DISPATCHES, so on the
// session strand it would run close() inline inside the callback instead of queueing
// it behind the arm's next suspension. Each cell then checks, after close() began:
// the state ring, onLogon, the admin frames passed to toAdmin, and the clock's parked
// sleeps. No store is parked: each cell runs over a real loopback socket, and no store
// operation waits on the test.
namespace {

namespace sess = fixpp::session;

struct CloseDuringLogonApp final : sess::Application {
    // What the application saw; the cells assert on it.
    struct Observed {
        bool close_started = false;
        std::optional<bool> close_ok;  // set when close() returned
        std::optional<sess::fsm_state> state_at_close_return;
        int on_logon = 0;
        std::vector<std::string> to_admin_after_close_started;
    };

    sess::Engine* engine = nullptr;
    sess::SessionId id;
    std::optional<sess::close_mode> mode;  // nullopt = control, no close
    std::string arm_on;                    // toAdmin MsgType that posts the close; "" = none
    std::shared_ptr<sess::Session> held;
    bool armed = false;
    Observed seen;

    // Look the session up and post close(mode) onto its executor.
    void post_close() {
        armed = true;
        held = engine->lookup(id);
        if (!held || !mode) return;
        asio::any_io_executor ex = held->executor().underlying();
        asio::post(ex, [this, ex] {
            asio::co_spawn(
                ex,
                [this]() -> asio::awaitable<void> {
                    seen.close_started = true;
                    auto r = co_await held->close(*mode);
                    seen.close_ok = r.has_value();
                    seen.state_at_close_return = held->state();
                },
                asio::detached);
        });
    }

    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
                 const sess::SessionId& /*sid*/) override {
        if (seen.close_started) seen.to_admin_after_close_started.emplace_back(msg.msg_type());
        if (!held) held = engine->lookup(id);  // the control cell reads the state through it
        if (armed || arm_on.empty() || msg.msg_type() != arm_on) return;
        post_close();
    }

    void onLogon(const sess::SessionId& /*sid*/) override { ++seen.on_logon; }
};

// A MemoryStore that reports itself persistent, so the session hydrates from it and
// persists into it. Its outbound counter can start past 1, it can run a hook on the
// session's first inbound hydrate read or on its first reset(), and it has a
// graceful-close flush. A hook stands in for a close() posted from another thread.
class HookedStore final : public sess::MessageStore {
public:
    HookedStore(sess::seqnum_t outbound_next, std::function<void()> on_hydrate,
                std::function<void()> on_reset)
        : sess::MessageStore(flush_thunk_for<HookedStore>()),
          inner_(sess::MemoryStore::Config{.policy = sess::capacity_policy::unbounded}),
          on_hydrate_(std::move(on_hydrate)),
          on_reset_(std::move(on_reset)) {
        asio::io_context seed_ioc;
        asio::co_spawn(
            seed_ioc,
            [this, outbound_next]() -> asio::awaitable<void> {
                for (sess::seqnum_t s = 1; s < outbound_next; ++s) {
                    (void)co_await inner_.next_seqnum(sess::direction_t::outbound, true);
                }
            },
            asio::detached);
        seed_ioc.run();
    }

    asio::awaitable<fixpp::core::expected_t<void>> store(sess::seqnum_t seq,
                                                         std::span<const std::byte> frame,
                                                         sess::direction_t dir) noexcept override {
        return inner_.store(seq, frame, dir);
    }
    asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        sess::seqnum_t begin, sess::seqnum_t end, sess::direction_t dir,
        sess::retrieve_visitor& visitor) noexcept override {
        return inner_.retrieve(begin, end, dir, visitor);
    }
    asio::awaitable<fixpp::core::expected_t<sess::seqnum_t>> next_seqnum(
        sess::direction_t dir, bool increment) noexcept override {
        if (on_hydrate_ && dir == sess::direction_t::inbound && !increment) {
            auto hook = std::move(on_hydrate_);
            on_hydrate_ = nullptr;
            hook();
            // Answered without yielding, as a store holding the counter in memory may:
            // nothing has been stored inbound yet. The close the hook posted then runs at
            // the next read's leading post.
            return ready(sess::seqnum_min);
        }
        return inner_.next_seqnum(dir, increment);
    }
    asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        if (on_reset_) {
            auto hook = std::move(on_reset_);
            on_reset_ = nullptr;
            hook();  // the close it posts runs at the reset's leading post
        }
        return inner_.reset();
    }

    // close(graceful) awaits this before it writes Disconnected. It yields the strand
    // a few times, as FileStore's flush does, so the session's other work can run
    // while close() is already under way.
    asio::awaitable<fixpp::core::expected_t<void>> flush_for_session_close() {
        for (int i = 0; i < 8; ++i) {
            co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        }
        co_return fixpp::core::expected_t<void>{};
    }

private:
    static asio::awaitable<fixpp::core::expected_t<sess::seqnum_t>> ready(sess::seqnum_t v) {
        co_return v;
    }

    sess::MemoryStore inner_;
    std::function<void()> on_hydrate_;
    std::function<void()> on_reset_;
};

class HookedStoreFactory final : public sess::MessageStoreFactory {
public:
    sess::seqnum_t outbound_next = 1;
    std::function<void()> on_hydrate;
    std::function<void()> on_reset;

    [[nodiscard]] bool yields_persistent_store() const noexcept override { return true; }
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<sess::MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/, std::pmr::memory_resource* /*mr*/,
        std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override {
        return fixpp::core::expected_t<std::unique_ptr<sess::MessageStore>>{
            std::make_unique<HookedStore>(outbound_next, std::move(on_hydrate),
                                          std::move(on_reset))};
    }
};

struct LogonCloseCase {
    std::optional<sess::close_mode> mode;
    std::string arm_on = "A";
    std::string peer_logon_extra;  // fields appended to the peer's Logon, SOH-terminated
    bool enable_789 = false;
    sess::seqnum_t store_outbound_next = 0;  // 0 = no store_factory
    bool close_from_hydrate = false;
    bool close_from_reset = false;
    bool cancel_sleeps_before_stop = true;
};

struct LogonCloseOutcome {
    bool bound = false;
    bool settled = false;
    CloseDuringLogonApp::Observed seen;
    std::optional<sess::fsm_state> state_after_settle;
    std::vector<sess::fsm_state> ring;  // < 16 writes: physical order == write order
    std::size_t inflight_sleeps_after_settle = 0;
    bool stop_completed = false;
    std::size_t inflight_sleeps_after_stop = 0;
};

// Space-separated; an FSM state prints as its enum value.
template <class T>
std::string joined(std::vector<T> const& items) {
    std::string s;
    for (auto const& item : items) {
        if constexpr (std::is_same_v<T, std::string>) {
            s += item;
        } else {
            s += std::to_string(static_cast<int>(item));
        }
        s += ' ';
    }
    return s;
}

// True iff a state other than Disconnected is written after a Disconnected.
bool written_after_disconnected(std::vector<sess::fsm_state> const& ring) {
    bool seen_disc = false;
    for (auto st : ring) {
        if (st == sess::fsm_state::Disconnected) {
            seen_disc = true;
        } else if (seen_disc) {
            return true;
        }
    }
    return false;
}

// Raw peer acceptor: accept one connection, read the initiator's Logon up to the end
// of its CheckSum field, answer with `reply`, hold the socket open.
asio::awaitable<void> run_raw_logon_acceptor(asio::io_context& ioc, asio::ip::tcp::acceptor& acc,
                                             std::vector<std::byte> reply) {
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
    try {
        asio::ip::tcp::socket sock{ioc};
        asio::error_code ec;
        co_await acc.async_accept(sock, asio::redirect_error(asio::use_awaitable, ec));
        if (ec) co_return;
        std::string got;
        std::array<char, 1024> buf{};
        const auto complete = [&got] {
            const auto cs = got.find(
                "\x01"
                "10=");
            return cs != std::string::npos && got.find('\x01', cs + 1) != std::string::npos;
        };
        while (!complete()) {
            const std::size_t n = co_await sock.async_read_some(
                asio::buffer(buf), asio::redirect_error(asio::use_awaitable, ec));
            if (ec) co_return;
            got.append(buf.data(), n);
        }
        co_await asio::async_write(sock, asio::buffer(reply.data(), reply.size()),
                                   asio::redirect_error(asio::use_awaitable, ec));
        asio::steady_timer t{ioc};
        t.expires_after(kInitiatorHold);
        co_await t.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        sock.close(ec);
    } catch (...) {
    }
}

// The io_context, clock, application and Engine one cell runs on.
struct CaseRig {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::system_clock_source> clock =
        std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());
    std::shared_ptr<CloseDuringLogonApp> app = std::make_shared<CloseDuringLogonApp>();
    sess::Engine engine{ioc.get_executor(), engine_config()};

    fixpp::core::EngineConfig engine_config() {
        fixpp::core::EngineConfig cfg;
        cfg.executor = ioc.get_executor();
        cfg.clock = clock;
        cfg.application = app;
        return cfg;
    }

    // Plain-TCP session config for `c`, with its store and the app wired in.
    [[nodiscard]] bool register_case(sess::session_role role, std::string sender,
                                     std::string target, uint16_t port, LogonCloseCase const& c) {
        sess::SessionConfig cfg;
        cfg.sender_comp_id = std::move(sender);
        cfg.target_comp_id = std::move(target);
        cfg.begin_string = "FIX.4.2";
        cfg.role = role;
        cfg.executor_override = ioc.get_executor();
        cfg.security_profile =
            sess::SecurityProfile{sess::SecurityProfile::kind::insecure_plain_tcp};
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.reset_seqnum_policy_field = sess::reset_seqnum_policy::bilateral_lenient;
        cfg.heartbeat_interval = std::chrono::seconds{30};
        cfg.logout_disconnect_timeout_ms = 500;
        cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", port};
        cfg.transport_send = [](std::span<const std::byte>) {};
        cfg.enable_next_expected_msg_seq_num = c.enable_789;
        if (c.store_outbound_next != 0) {
            auto factory = std::make_shared<HookedStoreFactory>();
            factory->outbound_next = c.store_outbound_next;
            if (c.close_from_hydrate) factory->on_hydrate = [a = app] { a->post_close(); };
            if (c.close_from_reset) factory->on_reset = [a = app] { a->post_close(); };
            cfg.store_factory = std::move(factory);
        }
        app->engine = &engine;
        app->id = sess::SessionId::from_config(cfg);
        app->mode = c.mode;
        app->arm_on = c.arm_on;
        return engine.register_session(std::move(cfg)).has_value();
    }

    // Settle, capture, then stop the engine. Settled: the close (if any) returned, or
    // the control session reached Active. The fixed dwell after it is the negative
    // witness's window: anything queued behind the settle point runs before capture.
    void settle_capture_and_stop(LogonCloseCase const& c, LogonCloseOutcome& out) {
        out.settled = fixpp::test_support::pump_until(
            ioc,
            [&] {
                if (!app->held) return false;
                if (c.mode) return app->seen.close_ok.has_value();
                return app->held->state() == sess::fsm_state::Active;
            },
            kStateBudget, fixpp::test_support::kPumpSlice, "LogonCloseDuringSuspension/settle");
        ioc.run_for(50ms);
        ioc.restart();

        out.seen = app->seen;
        out.inflight_sleeps_after_settle = clock->inflight_count();
        if (app->held) {
            out.state_after_settle = app->held->state();
            auto ring = app->held->fsm_visit_history();
            out.ring.assign(ring.begin(), ring.end());
        }

        if (c.cancel_sleeps_before_stop) {
            // Wake any sleeper so a loop parked past close() cannot outlive the Session
            // that stop() frees; the no-cancel cell leaves that to stop() alone.
            clock->cancel_sleeps();
            EXPECT_TRUE(fixpp::test_support::pump_until(
                ioc, [&] { return clock->inflight_count() == 0; }, kStateBudget,
                fixpp::test_support::kPumpSlice, "LogonCloseDuringSuspension/cancel"))
                << "a sleep outlived cancel_sleeps()";
        }
        app->held.reset();
        auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, stop_fut, kStopWindow,
                                                        "LogonCloseDuringSuspension/stop")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "LogonCloseDuringSuspension/stop");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "LogonCloseDuringSuspension/stop";
            return;
        }
        stop_fut.get();
        out.stop_completed = true;
        out.inflight_sleeps_after_stop = clock->inflight_count();
    }
};

// Plain-TCP Engine acceptor; a raw peer sends a Logon carrying `c.peer_logon_extra`.
LogonCloseOutcome run_acceptor_case(LogonCloseCase const& c) {
    LogonCloseOutcome out;
    CaseRig rig;
    if (!rig.register_case(sess::session_role::acceptor, "PLAIN-ACCEPTOR", "PLAIN-INITIATOR", 0,
                           c)) {
        return out;
    }
    if (!rig.engine.start().has_value()) return out;

    uint16_t port = 0;
    (void)fixpp::test_support::pump_until(
        rig.ioc,
        [&] {
            port = rig.engine.acceptor_bound_endpoint(rig.app->id).port;
            return port != 0;
        },
        kStateBudget, fixpp::test_support::kPumpSlice, "LogonCloseDuringSuspension/bind");
    out.bound = port != 0;
    if (out.bound) {
        asio::co_spawn(rig.ioc,
                       run_plain_initiator(rig.ioc, port, "PLAIN-INITIATOR", "PLAIN-ACCEPTOR",
                                           c.peer_logon_extra),
                       asio::detached);
    }
    rig.settle_capture_and_stop(c, out);
    return out;
}

// Plain-TCP Engine initiator; a raw peer answers its Logon with a Logon-ack carrying
// `c.peer_logon_extra`.
LogonCloseOutcome run_initiator_case(LogonCloseCase const& c) {
    LogonCloseOutcome out;
    CaseRig rig;
    asio::ip::tcp::acceptor peer{rig.ioc, {asio::ip::make_address("127.0.0.1"), 0}};
    const uint16_t port = peer.local_endpoint().port();
    out.bound = port != 0;
    if (!rig.register_case(sess::session_role::initiator, "PLAIN-INITIATOR", "PLAIN-ACCEPTOR", port,
                           c)) {
        return out;
    }
    asio::co_spawn(
        rig.ioc,
        run_raw_logon_acceptor(rig.ioc, peer,
                               make_plain_logon_frame("FIX.4.2", "PLAIN-ACCEPTOR",
                                                      "PLAIN-INITIATOR", c.peer_logon_extra)),
        asio::detached);
    if (!rig.engine.start().has_value()) return out;
    rig.settle_capture_and_stop(c, out);
    return out;
}

// What every cell with a close asserts: the close ran and returned with the session
// Disconnected, nothing is written over close()'s Disconnected, onLogon never fires,
// and no liveness sleep is parked after close() returned.
void expect_close_owns_teardown(LogonCloseOutcome const& o) {
    EXPECT_TRUE(o.seen.close_started) << "the posted close never ran";
    EXPECT_EQ(o.seen.close_ok, std::optional{true});
    EXPECT_EQ(o.seen.state_at_close_return, std::optional{sess::fsm_state::Disconnected})
        << "state when close() returned; ring=" << joined(o.ring);
    EXPECT_EQ(o.state_after_settle, std::optional{sess::fsm_state::Disconnected})
        << "ring=" << joined(o.ring);
    EXPECT_FALSE(written_after_disconnected(o.ring))
        << "a state written after close()'s Disconnected; ring=" << joined(o.ring);
    EXPECT_EQ(o.seen.on_logon, 0) << "onLogon fired although close() began before Active";
    EXPECT_EQ(o.inflight_sleeps_after_settle, 0U)
        << "a liveness sleep is parked after close() returned";
    EXPECT_TRUE(o.stop_completed);
}

void expect_no_admin_after_close(LogonCloseOutcome const& o) {
    EXPECT_TRUE(o.seen.to_admin_after_close_started.empty())
        << "admin frames after close() began: " << joined(o.seen.to_admin_after_close_started);
}

// close(graceful) from LogonReceived runs its own phase-1 Logout.
void expect_only_close_logout_after_close(LogonCloseOutcome const& o) {
    EXPECT_EQ(o.seen.to_admin_after_close_started, std::vector<std::string>{"5"})
        << "admin frames after close() began: " << joined(o.seen.to_admin_after_close_started);
}

}  // namespace

// Control: no close. Asserts the session reaches Active with no Disconnected in the
// ring and one onLogon, the outcome the close cells are told apart from.
TEST(LogonCloseDuringSuspension, ControlNoCloseReachesActive) {
    auto o = run_acceptor_case({.mode = std::nullopt});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    EXPECT_EQ(o.state_after_settle, std::optional{sess::fsm_state::Active});
    EXPECT_FALSE(written_after_disconnected(o.ring)) << "ring=" << joined(o.ring);
    EXPECT_EQ(o.seen.on_logon, 1);
    EXPECT_TRUE(o.stop_completed);
}

// close(terminal) posted from the reply Logon's toAdmin, so it runs during the reply's
// write.
TEST(LogonCloseDuringSuspension, AcceptorTerminalCloseDuringReply) {
    auto o = run_acceptor_case({.mode = sess::close_mode::terminal});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    expect_no_admin_after_close(o);
}

// Same, and stop() runs without the test first waking the clock's sleepers; asserts
// no sleep is parked after stop() either.
TEST(LogonCloseDuringSuspension, AcceptorTerminalCloseDuringReplyThenStopWithoutCancel) {
    auto o =
        run_acceptor_case({.mode = sess::close_mode::terminal, .cancel_sleeps_before_stop = false});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    EXPECT_EQ(o.inflight_sleeps_after_stop, 0U) << "a sleep is still parked after stop()";
}

// close(graceful), the mode fixpp_session_close() (C ABI) uses, posted from the reply's
// toAdmin. Its phase 1 waits on the write gate the reply holds, so the FSM is still
// LogonReceived when the arm resumes. Asserts no Active in the ring.
TEST(LogonCloseDuringSuspension, AcceptorGracefulCloseDuringReplyNeverReachesActive) {
    auto o = run_acceptor_case({.mode = sess::close_mode::graceful});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    EXPECT_EQ(std::count(o.ring.begin(), o.ring.end(), sess::fsm_state::Active), 0)
        << "ring=" << joined(o.ring);
    expect_only_close_logout_after_close(o);
}

// The peer's Logon carries NextExpectedMsgSeqNum(789) above the acceptor's next
// outbound, which the 789 honour after the reply answers with a Logout. The close is
// posted from the reply's toAdmin.
TEST(LogonCloseDuringSuspension, AcceptorCloseDuringReplyBuildsNoHonourLogout) {
    auto o = run_acceptor_case(
        {.mode = sess::close_mode::terminal, .peer_logon_extra = "789=5\x01", .enable_789 = true});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    expect_no_admin_after_close(o);
}

// The acceptor's store starts its outbound counter past 1 and the peer's 789 is below
// it, so the 789 honour after the reply gap-fills. The close is posted from the
// GapFill's toAdmin, so it runs during the GapFill's write.
TEST(LogonCloseDuringSuspension, AcceptorCloseDuringHonourGapFill) {
    auto o = run_acceptor_case({.mode = sess::close_mode::terminal,
                                .arm_on = "4",
                                .peer_logon_extra = "789=2\x01",
                                .enable_789 = true,
                                .store_outbound_next = 4});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    expect_no_admin_after_close(o);
}

// close(graceful) posted from the store's first hydrate read, before any callback
// fires; the store's flush keeps it under way while the arm resumes. Asserts no
// LogonReceived in the ring.
TEST(LogonCloseDuringSuspension, AcceptorCloseDuringHydrate) {
    auto o = run_acceptor_case({.mode = sess::close_mode::graceful,
                                .arm_on = "",
                                .store_outbound_next = 1,
                                .close_from_hydrate = true});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    EXPECT_EQ(std::count(o.ring.begin(), o.ring.end(), sess::fsm_state::LogonReceived), 0)
        << "ring=" << joined(o.ring);
    expect_no_admin_after_close(o);
}

// The peer's Logon carries ResetSeqNumFlag(141)=Y, so the acceptor resets its store
// after writing LogonReceived. close(graceful) is posted from that reset(); the store's
// flush keeps it under way while the arm resumes.
TEST(LogonCloseDuringSuspension, AcceptorCloseDuringPeerResetBuildsNoReply) {
    auto o = run_acceptor_case({.mode = sess::close_mode::graceful,
                                .arm_on = "",
                                .peer_logon_extra = "141=Y\x01",
                                .store_outbound_next = 1,
                                .close_from_reset = true});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    expect_only_close_logout_after_close(o);
}

// Initiator: the peer's Logon-ack carries 141=Y and a 789 above the initiator's next
// outbound, so the initiator resets its store and then answers the 789 with a Logout.
// close(graceful) is posted from that reset(); the store's flush keeps it under way.
TEST(LogonCloseDuringSuspension, InitiatorCloseDuringPeerResetBuildsNoHonourFrame) {
    auto o = run_initiator_case({.mode = sess::close_mode::graceful,
                                 .arm_on = "",
                                 .peer_logon_extra = "141=Y\x01"
                                                     "789=5\x01",
                                 .enable_789 = true,
                                 .store_outbound_next = 1,
                                 .close_from_reset = true});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    expect_no_admin_after_close(o);
}

// Initiator: the peer's Logon-ack carries 789=1, below the initiator's next outbound,
// so the initiator gap-fills before going Active. The close is posted from the
// GapFill's toAdmin, so it runs during the GapFill's write.
TEST(LogonCloseDuringSuspension, InitiatorCloseDuringHonourGapFill) {
    auto o = run_initiator_case({.mode = sess::close_mode::terminal,
                                 .arm_on = "4",
                                 .peer_logon_extra = "789=1\x01",
                                 .enable_789 = true});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    expect_no_admin_after_close(o);
}
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop  // -Wdeprecated-declarations (insecure_plain_tcp, 043 T020)
#endif
