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
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/pump_until_ready.hpp"

// ── #289: bounded pumps ──────────────────────────────────────────────
//
// Both census sites in this file (the two `/stop` windows) use `run_window_then_ready` plus a
// miss-branch drain (tests/support/pump_until_ready.hpp). The window is PRESERVED: the hazard #289
// names is the UNCONDITIONAL `get()`, not the fixed window.
//
// ⚠️ BOTH ARE NORMALISATIONS, NOT HAZARD FIXES. An
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
std::vector<std::byte> make_plain_logon_frame(std::string_view begin_str, std::string_view sender,
                                              std::string_view target) {
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
// sends a FIX Logon frame, holds the socket open (see kInitiatorHold), then closes.
asio::awaitable<void> run_plain_initiator(asio::io_context& ioc, uint16_t acceptor_port,
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

        // Build and send a Logon frame.
        auto logon = make_plain_logon_frame("FIX.4.2", sender, target);

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

// ── #518 probe: a close() the application posts while the acceptor's reply Logon
// is in flight ────────────────────────────────────────────────────────────────
//
// toAdmin fires synchronously just before the acceptor's reply Logon is stored and
// written. The application looks the session up through the public Engine::lookup()
// and posts Session::close(mode) onto its executor. asio::post, not co_spawn: the
// vendored co_spawn DISPATCHES, so on the session strand it would run close() inline
// inside toAdmin instead of queueing it behind the reply. No store is parked: the
// session runs the engine's default store over a real loopback socket.
namespace {

struct ReplyCloseProbe final : fixpp::session::Application {
    fixpp::session::Engine* engine = nullptr;
    fixpp::session::SessionId id;
    std::optional<fixpp::session::close_mode> mode;  // nullopt = control arm, no close
    std::shared_ptr<fixpp::session::Session> held;
    bool armed = false;
    bool lookup_non_null = false;
    bool close_started = false;
    bool close_returned = false;
    bool close_ok = false;
    std::optional<fixpp::session::fsm_state> state_at_close_return;
    int on_logon = 0;
    int on_logon_after_close_started = 0;

    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
                 const fixpp::session::SessionId& /*sid*/) override {
        // The acceptor's first admin emit is its reply Logon.
        if (armed) return;
        armed = true;
        held = engine->lookup(id);
        lookup_non_null = held != nullptr;
        if (!held || !mode) return;
        asio::any_io_executor ex = held->executor().underlying();
        asio::post(ex, [this, ex] {
            asio::co_spawn(
                ex,
                [this]() -> asio::awaitable<void> {
                    close_started = true;
                    auto r = co_await held->close(*mode);
                    close_returned = true;
                    close_ok = r.has_value();
                    state_at_close_return = held->state();
                },
                asio::detached);
        });
    }

    void onLogon(const fixpp::session::SessionId& /*sid*/) override {
        ++on_logon;
        if (close_started) ++on_logon_after_close_started;
    }
};

struct ReplyCloseOutcome {
    bool bound = false;
    bool settled = false;
    bool lookup_non_null = false;
    bool close_started = false;
    bool close_returned = false;
    bool close_ok = false;
    std::optional<fixpp::session::fsm_state> state_at_close_return;
    std::optional<fixpp::session::fsm_state> state_after_settle;
    std::vector<fixpp::session::fsm_state> ring;  // < 16 writes: physical order == write order
    int on_logon = 0;
    int on_logon_after_close_started = 0;
    std::size_t inflight_sleeps_after_settle = 0;
};

std::string ring_text(std::vector<fixpp::session::fsm_state> const& ring) {
    std::string s;
    for (auto st : ring) s += std::to_string(static_cast<int>(st)) + " ";
    return s;
}

ReplyCloseOutcome run_reply_close_probe(std::optional<fixpp::session::close_mode> mode) {
    ReplyCloseOutcome out;
    asio::io_context ioc;
    auto clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());
    auto app = std::make_shared<ReplyCloseProbe>();
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    eng_cfg.clock = clock;
    eng_cfg.application = app;
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

    app->engine = &engine;
    app->id = fixpp::session::SessionId::from_config(acc_cfg);
    app->mode = mode;
    if (!engine.register_session(std::move(acc_cfg)).has_value()) return out;
    if (!engine.start().has_value()) return out;

    uint16_t port = 0;
    (void)fixpp::test_support::pump_until(
        ioc,
        [&] {
            port = engine.acceptor_bound_endpoint(app->id).port;
            return port != 0;
        },
        kStateBudget, fixpp::test_support::kPumpSlice, "ReplyCloseProbe/bind");
    out.bound = port != 0;
    if (out.bound) {
        asio::co_spawn(ioc, run_plain_initiator(ioc, port, "PLAIN-INITIATOR", "PLAIN-ACCEPTOR"),
                       asio::detached);
        // Settled: the close (if any) returned, and the Logon exchange left
        // LogonReceived — so the reply arm has run to its end.
        out.settled = fixpp::test_support::pump_until(
            ioc,
            [&] {
                if (!app->held) return false;
                if (app->mode && !app->close_returned) return false;
                return app->held->state() != fixpp::session::fsm_state::LogonReceived &&
                       app->held->state() != fixpp::session::fsm_state::NotConnected;
            },
            kStateBudget, fixpp::test_support::kPumpSlice, "ReplyCloseProbe/settle");
        // A few more slices so anything queued behind the settle runs.
        ioc.run_for(50ms);
        ioc.restart();
    }

    out.lookup_non_null = app->lookup_non_null;
    out.close_started = app->close_started;
    out.close_returned = app->close_returned;
    out.close_ok = app->close_ok;
    out.state_at_close_return = app->state_at_close_return;
    out.on_logon = app->on_logon;
    out.on_logon_after_close_started = app->on_logon_after_close_started;
    out.inflight_sleeps_after_settle = clock->inflight_count();
    if (app->held) {
        out.state_after_settle = app->held->state();
        auto ring = app->held->fsm_visit_history();
        out.ring.assign(ring.begin(), ring.end());
    }

    // Wake any sleeper (an orphaned liveness loop would otherwise outlive the
    // Session that stop() frees), then release the lease and stop.
    clock->cancel_sleeps();
    ioc.run_for(100ms);
    ioc.restart();
    app->held.reset();
    auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(ioc, stop_fut, kStopWindow,
                                                    "ReplyCloseProbe/stop")) {
        fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "ReplyCloseProbe/stop");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << "ReplyCloseProbe/stop";
        return out;
    }
    stop_fut.get();
    return out;
}

// True iff an Active write follows a Disconnected write in the ring.
bool active_after_disconnected(std::vector<fixpp::session::fsm_state> const& ring) {
    bool seen_disc = false;
    for (auto st : ring) {
        if (st == fixpp::session::fsm_state::Disconnected) seen_disc = true;
        if (seen_disc && st == fixpp::session::fsm_state::Active) return true;
    }
    return false;
}

}  // namespace

// Control arm: no close. The Logon exchange reaches Active with no Disconnected
// before it, and onLogon fires once — so the ring witness below can tell the arms apart.
TEST(ReplyLogonCloseProbe518, ControlNoCloseReachesActive) {
    auto o = run_reply_close_probe(std::nullopt);
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << ring_text(o.ring);
    EXPECT_TRUE(o.lookup_non_null) << "the session must be lookup-addressable in toAdmin";
    EXPECT_EQ(o.state_after_settle, std::optional{fixpp::session::fsm_state::Active});
    EXPECT_FALSE(active_after_disconnected(o.ring)) << "ring=" << ring_text(o.ring);
    EXPECT_EQ(o.on_logon, 1);
}

// #518: a close(terminal) posted from toAdmin runs while the reply Logon is in
// flight and writes Disconnected; the reply arm must not overwrite it with Active.
TEST(ReplyLogonCloseProbe518, TerminalCloseDuringReplyIsNotOverwrittenByActive) {
    auto o = run_reply_close_probe(fixpp::session::close_mode::terminal);
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << ring_text(o.ring);
    ASSERT_TRUE(o.lookup_non_null) << "Engine::lookup() must return the session in toAdmin";
    ASSERT_TRUE(o.close_started) << "the posted close never ran";
    EXPECT_TRUE(o.close_returned);
    EXPECT_TRUE(o.close_ok);
    EXPECT_FALSE(active_after_disconnected(o.ring))
        << "Active written after close's Disconnected; ring=" << ring_text(o.ring);
    EXPECT_EQ(o.state_at_close_return, std::optional{fixpp::session::fsm_state::Disconnected})
        << "state when close() returned";
    EXPECT_EQ(o.state_after_settle, std::optional{fixpp::session::fsm_state::Disconnected});
    EXPECT_EQ(o.on_logon_after_close_started, 0) << "onLogon fired after close() began";
    EXPECT_EQ(o.inflight_sleeps_after_settle, 0U)
        << "a liveness loop is parked after close() completed (spawned past its join)";
}

// Same, with close(graceful) — the mode fixpp_session_close() (C ABI) uses.
TEST(ReplyLogonCloseProbe518, GracefulCloseDuringReplyIsNotOverwrittenByActive) {
    auto o = run_reply_close_probe(fixpp::session::close_mode::graceful);
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << ring_text(o.ring);
    ASSERT_TRUE(o.lookup_non_null);
    ASSERT_TRUE(o.close_started) << "the posted close never ran";
    EXPECT_TRUE(o.close_returned);
    EXPECT_FALSE(active_after_disconnected(o.ring))
        << "Active written after close's Disconnected; ring=" << ring_text(o.ring);
    EXPECT_EQ(o.state_after_settle, std::optional{fixpp::session::fsm_state::Disconnected})
        << "ring=" << ring_text(o.ring);
    EXPECT_EQ(o.inflight_sleeps_after_settle, 0U)
        << "a liveness loop is parked after close() completed";
    // Informational: where onLogon fired relative to the close.
    std::printf("[graceful] ring=%s on_logon=%d after_close_started=%d\n",
                ring_text(o.ring).c_str(), o.on_logon, o.on_logon_after_close_started);
}
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop  // -Wdeprecated-declarations (insecure_plain_tcp, 043 T020)
#endif
