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
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
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
#include <limits>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "support/hooked_store.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/session_test_access.hpp"
#include "support/temp_dir.hpp"
#include "support/validation_test_dictionary.hpp"

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
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
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

// A HookedStore hold (tests/support/hooked_store.hpp) is bounded below `kStateBudget`,
// so a hold that times out still lets the cell settle and report `hold_timed_out`
// instead of a settle miss.
static_assert(fixpp::test_support::kHoldBound < kStateBudget);

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
                                              std::string_view target, std::string_view extra = {},
                                              int seq = 1) {
    auto field = [](int tag, std::string_view v) -> std::string {
        return std::to_string(tag) + "=" + std::string(v) + "\x01";
    };
    std::string body;
    body += field(35, "A");                  // MsgType = Logon
    body += field(34, std::to_string(seq));  // MsgSeqNum
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
                                          std::string logon_extra = {},
                                          std::vector<std::byte> trailing = {}, int logon_seq = 1) {
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
        auto logon = make_plain_logon_frame("FIX.4.2", sender, target, logon_extra, logon_seq);

        // Capture the first byte for the no-TLS assertion.
        if (!logon.empty()) {
            g_first_byte_sent.store(logon[0], std::memory_order_release);
            g_first_byte_captured.store(true, std::memory_order_release);
        }

        // Frames coalesced behind the Logon go out in the same write.
        logon.insert(logon.end(), trailing.begin(), trailing.end());
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
// Each cell lets close(mode) land while a Logon arm is suspended. The close is posted
// from a callback the arm fires (toAdmin or onLogon), where the application reaches the
// session through the public Engine::lookup(), or from a store hook standing in for
// another thread. It is posted with asio::post, not co_spawn: the vendored co_spawn
// DISPATCHES, so on the session strand it would run close() inline inside the callback
// instead of queueing it behind the arm's next suspension. Each cell then checks what
// it names among the outcomes the rig records: the state ring, the event ring, onLogon,
// the admin frames passed to toAdmin, the clock's parked sleeps, and the store's counter
// writes and final counters.
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
        // Every admin frame passed to toAdmin.
        std::vector<std::string> to_admin_all;
        // The number of states in the session's state ring when the posted close began.
        std::optional<std::size_t> ring_at_close_start;
        // The clock's parked sleeps when close()'s own Logout reached toAdmin.
        std::optional<std::size_t> inflight_at_close_logout;
        // fromApp and fromAdmin calls made after the posted close began.
        int from_app_after_close_started = 0;
        int from_admin_after_close_started = 0;
        // The session's NextNumIn and the size of its event ring when the posted close
        // began.
        std::optional<sess::seqnum_t> next_in_at_close_start;
        std::optional<std::size_t> events_at_close_start;
    };

    sess::Engine* engine = nullptr;
    sess::SessionId id;
    std::optional<sess::close_mode> mode;  // nullopt = control, no close
    std::string arm_on;                    // toAdmin MsgType that posts the close; "" = none
    bool close_on_logon = false;           // onLogon posts the close
    fixpp::core::system_clock_source* clock = nullptr;
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
                    seen.ring_at_close_start = held->fsm_visit_history().size();
                    seen.next_in_at_close_start =
                        sess::session_test_access::seqnum_mgr(*held).next_inbound_unsafe();
                    seen.events_at_close_start = held->recent_events().size();
                    auto r = co_await held->close(*mode);
                    seen.close_ok = r.has_value();
                    seen.state_at_close_return = held->state();
                },
                asio::detached);
        });
    }

    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
                 const sess::SessionId& /*sid*/) override {
        seen.to_admin_all.emplace_back(msg.msg_type());
        if (seen.close_started) {
            seen.to_admin_after_close_started.emplace_back(msg.msg_type());
            if (msg.msg_type() == "5" && !seen.inflight_at_close_logout && clock) {
                seen.inflight_at_close_logout = clock->inflight_count();
            }
        }
        if (!held) held = engine->lookup(id);  // the control cell reads the state through it
        if (armed || arm_on.empty() || msg.msg_type() != arm_on) return;
        post_close();
    }

    void onLogon(const sess::SessionId& /*sid*/) override {
        ++seen.on_logon;
        if (close_on_logon && !armed) post_close();
    }

    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const sess::SessionId& /*sid*/) override {
        if (seen.close_started) ++seen.from_admin_after_close_started;
        return {};
    }

    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const sess::SessionId& /*sid*/) override {
        if (seen.close_started) ++seen.from_app_after_close_started;
        return {};
    }
};

using fixpp::test_support::HookedStore;
using fixpp::test_support::HookedStoreFactory;
using fixpp::test_support::StoreLog;

struct LogonCloseCase {
    std::optional<sess::close_mode> mode{};
    std::string arm_on = "A";
    std::string peer_logon_extra{};  // fields appended to the peer's Logon, SOH-terminated
    bool enable_789 = false;
    std::optional<sess::session_posture> posture{};
    bool reset_on_logon = false;
    bool reset_on_disconnect = false;
    sess::seqnum_t store_outbound_next = 0;  // 0 = no store_factory
    bool close_from_hydrate = false;
    bool close_from_reset = false;
    bool close_from_inbound_persist = false;
    bool close_from_outbound_persist = false;
    bool close_from_on_logon = false;
    bool hold_until_close_reset = false;  // see HookedStore::Hooks
    bool cancel_sleeps_before_stop = true;
    // Inbound validation on, over the validation test dictionary.
    bool validate = false;
    // Frames the peer writes in the same write as its Logon (or Logon-ack).
    std::vector<std::byte> trailing{};
    // close(graceful)'s store flush holds until the session has counted a garbled frame
    // (HookedStore::Hooks::flush_until), so a trailing frame that follows the frames under
    // test, and is garbled, shows the pump delivered them while close() was under way.
    bool flush_hold_until_garble = false;
    // #524 (093): the HookedStore's reset_to mode (tests/support/hooked_store.hpp).
    fixpp::test_support::reset_to_mode store_mode = fixpp::test_support::reset_to_mode::forward;
    // The hooked store operation holds until the posted close() has returned, or has
    // begun (HookedStore::Hooks::release_when), in place of hold_until_close_reset.
    bool hold_until_close_returned = false;
    bool hold_until_close_began = false;
    // A second connection over the store a first one left (its counters), with the
    // peer's Logon (or Logon-ack) at this MsgSeqNum.
    std::shared_ptr<StoreLog> reuse_store{};
    int peer_logon_seq = 1;
};

struct LogonCloseOutcome {
    bool bound = false;
    bool settled = false;
    CloseDuringLogonApp::Observed seen;
    std::optional<sess::fsm_state> state_after_settle;
    // Physical order is write order only while the ring has not wrapped; the capture
    // checks it.
    std::vector<sess::fsm_state> ring;
    // A session_event_sequence_numbers_reset is in the event ring at settle.
    bool reset_event = false;
    std::size_t inflight_sleeps_after_settle = 0;
    bool stop_completed = false;
    std::size_t inflight_sleeps_after_stop = 0;
    // Set when the case has a store: its log, and its counters read after stop().
    std::shared_ptr<StoreLog> store_log;
    std::optional<sess::seqnum_t> store_next_inbound;
    std::optional<sess::seqnum_t> store_next_outbound;
    // The session's garbled_frame_count() at settle.
    std::uint64_t garbled = 0;
    // The session's NextNumIn at settle.
    std::optional<sess::seqnum_t> next_in_after_settle;
    // The event ring at settle, in emission order while it has not wrapped: for each
    // event, whether it is a session_event_garbled_frame.
    std::vector<bool> events_are_garbles;
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

// The validation test dictionary with ResetSeqNumFlag(141) declared on the Logon, so a
// Logon carrying 141=Y validates and only the frames a cell makes invalid are rejected.
std::shared_ptr<const fixpp::dict::Dictionary> make_validation_dictionary_with_141() {
    std::string xml{fixpp::test_support::kValidationTestFix42Xml};
    auto insert_after = [&xml](std::string_view anchor, std::string_view text) {
        auto const at = xml.find(anchor);
        if (at == std::string::npos) return false;
        xml.insert(at + anchor.size(), text);
        return true;
    };
    if (!insert_after(R"(<field number="108" name="HeartBtInt"    required="Y"/>)",
                      R"(<field number="141" name="ResetSeqNumFlag" required="N"/>)") ||
        !insert_after(R"(<field number="112" name="TestReqID"    type="STRING"/>)",
                      R"(<field number="141" name="ResetSeqNumFlag" type="BOOLEAN"/>)")) {
        return nullptr;
    }
    constexpr std::size_t kBufSize = 128U * 1024U;
    auto buf = std::make_unique<std::array<std::byte, kBufSize>>();
    // The shared_ptr's deleter owns the dictionary, its resource and its buffer, and
    // releases them in that order.
    // NOLINTBEGIN(cppcoreguidelines-owning-memory)
    auto* mr = new std::pmr::monotonic_buffer_resource{buf->data(), buf->size()};
    auto* raw_dict =
        new fixpp::dict::Dictionary{fixpp::dict::XmlLoader{}.load_from_string(xml, mr)};
    auto* raw_buf = buf.release();
    return std::shared_ptr<const fixpp::dict::Dictionary>{
        raw_dict, [mr, raw_buf](const fixpp::dict::Dictionary* p) {
            delete p;
            delete mr;
            delete raw_buf;
        }};
    // NOLINTEND(cppcoreguidelines-owning-memory)
}

// The io_context, clock, application and Engine one cell runs on.
struct CaseRig {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::system_clock_source> clock =
        std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());
    std::shared_ptr<CloseDuringLogonApp> app = std::make_shared<CloseDuringLogonApp>();
    sess::Engine engine{ioc.get_executor(), engine_config()};
    std::shared_ptr<StoreLog> store_log;  // set by register_case when the case has a store

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
        cfg.dictionary = c.validate ? make_validation_dictionary_with_141()
                                    : fixpp::test_support::make_minimal_dictionary();
        cfg.validate_inbound_messages = c.validate;
        cfg.reset_seqnum_policy_field = sess::reset_seqnum_policy::bilateral_lenient;
        cfg.heartbeat_interval = std::chrono::seconds{30};
        cfg.logout_disconnect_timeout_ms = 500;
        cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", port};
        cfg.transport_send = [](std::span<const std::byte>) {};
        cfg.enable_next_expected_msg_seq_num = c.enable_789;
        cfg.posture = c.posture;
        cfg.reset_on_logon = c.reset_on_logon;
        cfg.reset_on_disconnect = c.reset_on_disconnect;
        if (c.store_outbound_next != 0) {
            auto factory = std::make_shared<HookedStoreFactory>();
            factory->outbound_next = c.store_outbound_next;
            factory->mode = c.store_mode;
            if (c.reuse_store) factory->log = c.reuse_store;
            factory->log->close_began = [a = app] { return a->seen.close_started; };
            if (c.close_from_hydrate) factory->hooks.on_hydrate = [a = app] { a->post_close(); };
            if (c.close_from_reset) factory->hooks.on_reset = [a = app] { a->post_close(); };
            if (c.close_from_inbound_persist) {
                factory->hooks.on_inbound_persist = [a = app] { a->post_close(); };
            }
            if (c.close_from_outbound_persist) {
                factory->hooks.on_outbound_persist = [a = app] { a->post_close(); };
            }
            factory->hooks.hold_until_close_reset = c.hold_until_close_reset;
            if (c.hold_until_close_returned) {
                factory->hooks.release_when = [a = app] { return a->seen.close_ok.has_value(); };
            } else if (c.hold_until_close_began) {
                factory->hooks.release_when = [a = app] { return a->seen.close_started; };
            }
            if (c.flush_hold_until_garble) {
                factory->hooks.flush_until = [a = app] {
                    return a->held && a->held->garbled_frame_count() >= 1U;
                };
            }
            store_log = factory->log;
            cfg.store_factory = std::move(factory);
        }
        app->engine = &engine;
        app->id = sess::SessionId::from_config(cfg);
        app->mode = c.mode;
        app->arm_on = c.arm_on;
        app->close_on_logon = c.close_from_on_logon;
        app->clock = clock.get();
        return engine.register_session(std::move(cfg)).has_value();
    }

    // Settle, capture, then stop the engine. Settled: the close (if any) returned, or
    // the control session reached Active. The fixed dwell after it is the negative
    // witness's window: anything queued behind the settle point runs before capture.
    void settle_capture_and_stop(LogonCloseCase const& c, LogonCloseOutcome& out) {
        out.settled = fixpp::test_support::pump_until(
            ioc,
            [&] {
                if (c.mode) return app->seen.close_ok.has_value();
                // The control initiator fires no toAdmin after it is published.
                if (!app->held) app->held = engine.lookup(app->id);
                if (!app->held) return false;
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
            EXPECT_LT(out.ring.size(), 16U)
                << "the state ring wrapped, so its physical order is not write order";
            out.reset_event = std::ranges::any_of(app->held->recent_events(), [](auto const& ev) {
                return std::holds_alternative<sess::session_event_sequence_numbers_reset>(ev);
            });
            out.garbled = app->held->garbled_frame_count();
            out.next_in_after_settle =
                sess::session_test_access::seqnum_mgr(*app->held).next_inbound_unsafe();
            for (auto const& ev : app->held->recent_events()) {
                out.events_are_garbles.push_back(
                    std::holds_alternative<sess::session_event_garbled_frame>(ev));
            }
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

        out.store_log = store_log;
        if (store_log && store_log->inner) {
            asio::io_context read_ioc;
            asio::co_spawn(
                read_ioc,
                [&]() -> asio::awaitable<void> {
                    auto in =
                        co_await store_log->inner->next_seqnum(sess::direction_t::inbound, false);
                    auto ob =
                        co_await store_log->inner->next_seqnum(sess::direction_t::outbound, false);
                    if (in) out.store_next_inbound = *in;
                    if (ob) out.store_next_outbound = *ob;
                },
                asio::detached);
            read_ioc.run();
        }
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
                                           c.peer_logon_extra, c.trailing, c.peer_logon_seq),
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
    auto reply = make_plain_logon_frame("FIX.4.2", "PLAIN-ACCEPTOR", "PLAIN-INITIATOR",
                                        c.peer_logon_extra, c.peer_logon_seq);
    reply.insert(reply.end(), c.trailing.begin(), c.trailing.end());
    asio::co_spawn(rig.ioc, run_raw_logon_acceptor(rig.ioc, peer, std::move(reply)),
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

// Every state written after the posted close began is close()'s Disconnected.
void expect_no_state_but_disconnected_after_close(LogonCloseOutcome const& o) {
    ASSERT_TRUE(o.seen.ring_at_close_start.has_value()) << "the posted close never ran";
    // The ASSERT_TRUE above returns on an empty optional; the check does not model it.
    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    ASSERT_LE(*o.seen.ring_at_close_start, o.ring.size()) << "ring=" << joined(o.ring);
    for (std::size_t i = *o.seen.ring_at_close_start; i < o.ring.size(); ++i) {
        EXPECT_EQ(o.ring[i], sess::fsm_state::Disconnected)
            << "a state other than Disconnected written after close() began; ring="
            << joined(o.ring) << " (close began at " << *o.seen.ring_at_close_start << ")";
    }
    // NOLINTEND(bugprone-unchecked-optional-access)
}

// Two frames the peer coalesces behind its Logon (or Logon-ack), from `sender` to
// `target`: a NewOrderSingle at 34=2 that the validation test dictionary rejects (no
// ClOrdID(11)), then a Heartbeat at 34=3 whose third field is not MsgType(35), which the
// arm disregards as garbled and counts whatever the state.
std::vector<std::byte> invalid_then_garbled(std::string_view sender, std::string_view target) {
    auto field = [](int tag, std::string_view v) {
        return std::to_string(tag) + "=" + std::string(v) + "\x01";
    };
    auto const ts = utc_now_fix_timestamp();
    auto invalid = make_fix_frame("FIX.4.2", field(35, "D") + field(34, "2") + field(49, sender) +
                                                 field(52, ts) + field(56, target) +
                                                 field(54, "1") + field(60, ts));
    auto const garbled =
        make_fix_frame("FIX.4.2", field(34, "3") + field(35, "0") + field(49, sender) +
                                      field(52, ts) + field(56, target));
    invalid.insert(invalid.end(), garbled.begin(), garbled.end());
    return invalid;
}

// #523 (093 quickstart Q-22): the frames coalesced behind the Logon reach the arm while
// close() is under way, and the arm acts on none of them. The garbled one is counted
// (the positive control: the pump delivered them before the flush released), the
// invalid one draws no Reject, no admin frame reaches toAdmin, no state other than
// close()'s Disconnected is written after close() began, no fromApp or fromAdmin runs,
// NextNumIn keeps the value it had when close() began, and every event emitted after
// that is a garbled-frame event (FR-030 carves garbled-frame accounting out).
void expect_coalesced_frames_inert(LogonCloseOutcome const& o) {
    ASSERT_TRUE(o.store_log);
    EXPECT_EQ(o.store_log->flushes_begun, 1) << "close(graceful)'s flush";
    EXPECT_FALSE(o.store_log->flush_hold_timed_out)
        << "the garbled trailing frame was not counted within the flush hold's bound";
    EXPECT_EQ(o.garbled, 1U) << "garbled_frame_count(): the trailing 35-not-third frame";
    expect_no_admin_after_close(o);
    expect_no_state_but_disconnected_after_close(o);
    EXPECT_EQ(o.seen.from_app_after_close_started, 0) << "fromApp after close() began";
    EXPECT_EQ(o.seen.from_admin_after_close_started, 0) << "fromAdmin after close() began";
    ASSERT_TRUE(o.seen.next_in_at_close_start.has_value()) << "the posted close never ran";
    EXPECT_EQ(o.next_in_after_settle, o.seen.next_in_at_close_start)
        << "NextNumIn moved after close() began";
    ASSERT_TRUE(o.seen.events_at_close_start.has_value()) << "the posted close never ran";
    // The ASSERT_TRUE above returns on an empty optional; the check does not model it.
    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    ASSERT_LT(o.events_are_garbles.size(), sess::kSessionEventRingCapacity)
        << "the event ring wrapped, so its physical order is not emission order";
    ASSERT_LE(*o.seen.events_at_close_start, o.events_are_garbles.size());
    for (std::size_t i = *o.seen.events_at_close_start; i < o.events_are_garbles.size(); ++i) {
        EXPECT_TRUE(o.events_are_garbles[i])
            << "event " << i << " after close() began is not a garbled-frame event";
    }
    // NOLINTEND(bugprone-unchecked-optional-access)
}

// close(graceful) from LogonReceived runs its own phase-1 Logout.
void expect_only_close_logout_after_close(LogonCloseOutcome const& o) {
    EXPECT_EQ(o.seen.to_admin_after_close_started, std::vector<std::string>{"5"})
        << "admin frames after close() began: " << joined(o.seen.to_admin_after_close_started);
}

// The store's counter writes in completion order; `*` marks one completed after close()
// began.
std::string store_writes(LogonCloseOutcome const& o) {
    std::string s;
    if (!o.store_log) return s;
    for (auto const& w : o.store_log->writes) {
        s += w.op;
        s += w.after_close_began ? "* " : " ";
    }
    return s;
}

// close() with reset_on_disconnect resets the store at teardown. The store must end at
// that reset's post-state, whatever the arm wrote around it. In the cells that use this,
// the arm issues its own resets before close() runs, so a reset() issued after close()
// began is the teardown's; without one the counters prove nothing.
void expect_store_ends_at_teardown_reset(LogonCloseOutcome const& o) {
    ASSERT_TRUE(o.store_log);
    EXPECT_EQ(o.store_log->stores_made, 1);
    EXPECT_FALSE(o.store_log->hold_timed_out)
        << "no reset() was issued after close() began within the hold's bound";
    EXPECT_GE(o.store_log->resets_issued_after_close_began, 1)
        << "store writes: " << store_writes(o);
    EXPECT_EQ(o.store_next_inbound, std::optional{sess::seqnum_min})
        << "store writes: " << store_writes(o);
    EXPECT_EQ(o.store_next_outbound, std::optional{sess::seqnum_min})
        << "store writes: " << store_writes(o);
}

// close(graceful) posted from onLogon, which the arm fires when it writes Active. The
// arm's inbound persist after that yields while close()'s store flush keeps the FSM at
// Active, and close()'s phase-1 Logout follows the flush. Asserts the arm completed its
// persist and started no liveness loop: no sleep is parked when that Logout reaches
// toAdmin.
void expect_close_from_on_logon_starts_no_liveness(LogonCloseOutcome const& o) {
    EXPECT_TRUE(o.seen.close_started) << "the posted close never ran";
    EXPECT_EQ(o.seen.on_logon, 1);
    EXPECT_EQ(o.seen.close_ok, std::optional{true});
    EXPECT_EQ(o.state_after_settle, std::optional{sess::fsm_state::Disconnected})
        << "ring=" << joined(o.ring);
    EXPECT_FALSE(written_after_disconnected(o.ring))
        << "a state written after close()'s Disconnected; ring=" << joined(o.ring);
    expect_only_close_logout_after_close(o);
    ASSERT_TRUE(o.seen.inflight_at_close_logout.has_value())
        << "close()'s Logout never reached toAdmin";
    EXPECT_EQ(*o.seen.inflight_at_close_logout, 0U)
        << "a liveness sleep was parked when close()'s Logout reached toAdmin";
    EXPECT_EQ(o.inflight_sleeps_after_settle, 0U)
        << "a liveness sleep is parked after close() returned";
    EXPECT_EQ(o.store_next_inbound, std::optional{sess::seqnum_min + 1})
        << "store writes: " << store_writes(o);
    EXPECT_TRUE(o.stop_completed);
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

// The acceptor's posture is production and the peer's Logon carries
// TestMessageIndicator(464)=Y, so the arm would refuse it with a Logout right after
// hydrate. close(graceful) is posted from the store's first hydrate read; the store's
// flush keeps it under way while the arm resumes. Asserts no refusal reaches toAdmin.
TEST(LogonCloseDuringSuspension, AcceptorCloseDuringHydrateBuildsNoRefusal) {
    auto o = run_acceptor_case({.mode = sess::close_mode::graceful,
                                .arm_on = "",
                                .peer_logon_extra = "464=Y\x01",
                                .posture = sess::session_posture::production,
                                .store_outbound_next = 1,
                                .close_from_hydrate = true});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    expect_no_admin_after_close(o);
}

// reset_on_logon is set, so the acceptor resets its store before it checks the Logon's
// sequence number. close(graceful) is posted from that reset(); the store's flush keeps
// it under way while the arm resumes. Asserts no LogonReceived in the ring.
TEST(LogonCloseDuringSuspension, AcceptorCloseDuringKnobResetNeverReceivesLogon) {
    auto o = run_acceptor_case({.mode = sess::close_mode::graceful,
                                .arm_on = "",
                                .reset_on_logon = true,
                                .store_outbound_next = 1,
                                .close_from_reset = true});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    EXPECT_EQ(std::count(o.ring.begin(), o.ring.end(), sess::fsm_state::LogonReceived), 0)
        << "ring=" << joined(o.ring);
    expect_no_admin_after_close(o);
}

// The peer's Logon carries ResetSeqNumFlag(141)=Y and reset_on_disconnect is set.
// close(terminal) is posted from the arm's peer reset(), so close()'s teardown reset
// and the arm's counter writes after its reset share the store. Asserts the store ends
// at the teardown reset's post-state.
TEST(LogonCloseDuringSuspension, AcceptorCloseDuringPeerResetStoreEndsAtTeardownReset) {
    auto o = run_acceptor_case({.mode = sess::close_mode::terminal,
                                .arm_on = "",
                                .peer_logon_extra = "141=Y\x01",
                                .reset_on_disconnect = true,
                                .store_outbound_next = 1,
                                .close_from_reset = true});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    expect_store_ends_at_teardown_reset(o);
}

// The peer's Logon carries ResetSeqNumFlag(141)=Y, so after writing LogonReceived the
// acceptor runs its 141=Y reset unit (093 contract C-6, superseding 030 T010/T011's
// reset, then restore, then persist). close(graceful) is posted from the unit's store
// operation (the forward reset_to's on_reset hook); the store's flush keeps it under way
// while the arm resumes, and no teardown reset is configured. Asserts the unit
// completes, keeping the consumed Logon's advance, and no reply follows it.
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
    EXPECT_EQ(o.store_next_inbound, std::optional{sess::seqnum_min + 1})
        << "store writes: " << store_writes(o);
}

// Same peer Logon; close(graceful) is posted from the inbound persist that follows the
// 141=Y reset, so the arm resumes after its last counter write, before the reply.
// 093 (tasks.md T083): the persist is part of the unit's store operation only in a
// default-body store (MessageStore::reset_to's default body), so the store runs in that
// mode.
TEST(LogonCloseDuringSuspension, AcceptorCloseDuringPeerResetPersistBuildsNoReply) {
    auto o = run_acceptor_case({.mode = sess::close_mode::graceful,
                                .arm_on = "",
                                .peer_logon_extra = "141=Y\x01",
                                .store_outbound_next = 1,
                                .close_from_inbound_persist = true,
                                .store_mode = fixpp::test_support::reset_to_mode::default_body});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    expect_only_close_logout_after_close(o);
    EXPECT_FALSE(o.reset_event) << "the arm emitted its reset event after close() began";
}

// The acceptor's store is persistent, so the arm persists the Logon's inbound advance
// after it writes Active. close(graceful) is posted from onLogon.
TEST(LogonCloseDuringSuspension, AcceptorCloseFromOnLogonStartsNoLiveness) {
    auto o = run_acceptor_case({.mode = sess::close_mode::graceful,
                                .arm_on = "",
                                .store_outbound_next = 1,
                                .close_from_on_logon = true});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_from_on_logon_starts_no_liveness(o);
}

// #523 (093 tasks.md T075; quickstart Q-22; contract C-2 step 2): validation is on, and
// the peer writes a dictionary-invalid NewOrderSingle and a garbled Heartbeat in the
// same write as its Logon. close(graceful) is posted from the store's first hydrate
// read, and its store flush holds until the garbled Heartbeat is counted, so the invalid
// frame reaches the NotConnected arm while close() is under way. The arm acts on it in
// no way: no Reject, no admin frame to toAdmin, no state but close()'s Disconnected.
TEST(LogonCloseDuringSuspension, AcceptorCloseDuringHydrateActsOnNoCoalescedFrame) {
    auto o =
        run_acceptor_case({.mode = sess::close_mode::graceful,
                           .arm_on = "",
                           .store_outbound_next = 1,
                           .close_from_hydrate = true,
                           .validate = true,
                           .trailing = invalid_then_garbled("PLAIN-INITIATOR", "PLAIN-ACCEPTOR"),
                           .flush_hold_until_garble = true});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    expect_coalesced_frames_inert(o);
}

// Initiator control: no close. Asserts the initiator reaches Active with no
// Disconnected in the ring and one onLogon.
TEST(LogonCloseDuringSuspension, InitiatorControlNoCloseReachesActive) {
    auto o = run_initiator_case({.mode = std::nullopt});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    EXPECT_EQ(o.state_after_settle, std::optional{sess::fsm_state::Active});
    EXPECT_FALSE(written_after_disconnected(o.ring)) << "ring=" << joined(o.ring);
    EXPECT_EQ(o.seen.on_logon, 1);
    EXPECT_TRUE(o.stop_completed);
}

// Initiator: the peer's Logon-ack carries 141=Y and a 789 above the initiator's next
// outbound, so the initiator resets its store, restores and persists its inbound
// counter, and then answers the 789 with a Logout. close(graceful) is posted from that
// reset(); the store's flush keeps it under way, and no teardown reset is configured.
// Asserts the unit completes, keeping the consumed Logon-ack's advance, and no 789
// Logout follows it.
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
    EXPECT_EQ(o.store_next_inbound, std::optional{sess::seqnum_min + 1})
        << "store writes: " << store_writes(o);
}

// #523 (093 tasks.md T075; quickstart Q-22; contract C-2 step 2): validation is on, and
// the peer writes a dictionary-invalid NewOrderSingle and a garbled Heartbeat in the
// same write as its Logon-ack, which carries 141=Y. close(graceful) is posted from the
// arm's peer reset(), and its store flush holds until the garbled Heartbeat is counted,
// so the invalid frame reaches the LogonSent arm while close() is under way. The arm
// acts on it in no way: no Reject, no admin frame to toAdmin, no state but close()'s
// Disconnected.
TEST(LogonCloseDuringSuspension, InitiatorCloseDuringPeerResetActsOnNoCoalescedFrame) {
    auto o =
        run_initiator_case({.mode = sess::close_mode::graceful,
                            .arm_on = "",
                            .peer_logon_extra = "141=Y\x01",
                            .store_outbound_next = 1,
                            .close_from_reset = true,
                            .validate = true,
                            .trailing = invalid_then_garbled("PLAIN-ACCEPTOR", "PLAIN-INITIATOR"),
                            .flush_hold_until_garble = true});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    expect_coalesced_frames_inert(o);
}

// Initiator: reset_on_logon is set, so the initiator's own Logon carries 141=Y at
// seq 1, and the peer's Logon-ack carries 141=Y. After its reset the initiator
// restores and persists its inbound counter, then its outbound one. close(graceful) is
// posted from the inbound persist, and no teardown reset is configured. Asserts the
// outbound restore completes too. 093 (tasks.md T083): the restores are the unit's
// store operation's own writes only in a default-body store, so the store runs in that
// mode.
TEST(LogonCloseDuringSuspension, InitiatorCloseDuringInboundRestoreCompletesOutboundRestore) {
    auto o = run_initiator_case({.mode = sess::close_mode::graceful,
                                 .arm_on = "",
                                 .peer_logon_extra = "141=Y\x01",
                                 .reset_on_logon = true,
                                 .store_outbound_next = 1,
                                 .close_from_inbound_persist = true,
                                 .store_mode = fixpp::test_support::reset_to_mode::default_body});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    expect_no_admin_after_close(o);
    EXPECT_EQ(o.store_next_inbound, std::optional{sess::seqnum_min + 1})
        << "store writes: " << store_writes(o);
    EXPECT_EQ(o.store_next_outbound, std::optional{sess::seqnum_min + 1})
        << "store writes: " << store_writes(o);
}

// Same, and the peer's Logon-ack also carries a 789 above the initiator's next
// outbound, which the 789 honour answers with a Logout. close(graceful) is posted from
// the outbound persist, so the arm resumes after its last counter write. 093 (tasks.md
// T083): a default-body store, as above.
TEST(LogonCloseDuringSuspension, InitiatorCloseDuringOutboundRestoreBuildsNoHonourFrame) {
    auto o = run_initiator_case({.mode = sess::close_mode::graceful,
                                 .arm_on = "",
                                 .peer_logon_extra = "141=Y\x01"
                                                     "789=5\x01",
                                 .enable_789 = true,
                                 .reset_on_logon = true,
                                 .store_outbound_next = 1,
                                 .close_from_outbound_persist = true,
                                 .store_mode = fixpp::test_support::reset_to_mode::default_body});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    expect_no_admin_after_close(o);
    EXPECT_FALSE(o.reset_event) << "the arm emitted its reset event after close() began";
}

// Initiator: the peer's Logon-ack carries 141=Y and reset_on_disconnect is set.
// close(terminal) is posted from the arm's peer reset(), so close()'s teardown reset
// and the arm's counter writes after its reset share the store. Asserts the store ends
// at the teardown reset's post-state.
TEST(LogonCloseDuringSuspension, InitiatorCloseDuringPeerResetStoreEndsAtTeardownReset) {
    auto o = run_initiator_case({.mode = sess::close_mode::terminal,
                                 .arm_on = "",
                                 .peer_logon_extra = "141=Y\x01",
                                 .reset_on_disconnect = true,
                                 .store_outbound_next = 1,
                                 .close_from_reset = true});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    expect_store_ends_at_teardown_reset(o);
}

// Initiator: reset_on_logon and reset_on_disconnect are set, and the peer's Logon-ack
// carries 141=Y. close(terminal) is posted from the inbound persist after the arm's
// reset, and the store holds that persist's return until close()'s teardown reset has
// been issued. Asserts the store ends at the teardown reset's post-state.
// 093 (tasks.md T083; contract C-6): a default-body store, as above. close() now waits
// for the unit's store operation before its teardown reset, so that reset is never
// issued inside the unit: the hold waits out its bound, the unit completes (both
// restores), and the teardown reset then leaves (1, 1).
TEST(LogonCloseDuringSuspension, InitiatorCloseDuringInboundRestoreStoreEndsAtTeardownReset) {
    auto o = run_initiator_case({.mode = sess::close_mode::terminal,
                                 .arm_on = "",
                                 .peer_logon_extra = "141=Y\x01",
                                 .reset_on_logon = true,
                                 .reset_on_disconnect = true,
                                 .store_outbound_next = 1,
                                 .close_from_inbound_persist = true,
                                 .hold_until_close_reset = true,
                                 .store_mode = fixpp::test_support::reset_to_mode::default_body});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    ASSERT_TRUE(o.store_log);
    EXPECT_TRUE(o.store_log->hold_timed_out)
        << "close() issued its teardown reset while the unit's persist was held";
    EXPECT_GE(o.store_log->resets_issued_after_close_began, 1)
        << "no teardown reset; store writes: " << store_writes(o);
    EXPECT_EQ(o.store_next_inbound, std::optional{sess::seqnum_min})
        << "store writes: " << store_writes(o);
    EXPECT_EQ(o.store_next_outbound, std::optional{sess::seqnum_min})
        << "store writes: " << store_writes(o);
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

// Initiator: its store is persistent, so the arm persists the Logon-ack's inbound
// advance after it writes Active. close(graceful) is posted from onLogon.
TEST(LogonCloseDuringSuspension, InitiatorCloseFromOnLogonStartsNoLiveness) {
    auto o = run_initiator_case({.mode = sess::close_mode::graceful,
                                 .arm_on = "",
                                 .store_outbound_next = 1,
                                 .close_from_on_logon = true});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_from_on_logon_starts_no_liveness(o);
}
// ── #524 (093 tasks.md T080, T081; quickstart Q-23 to Q-25; contract C-6) ──────
//
// The peer's Logon (acceptor) or Logon-ack (initiator) carries ResetSeqNumFlag(141)=Y,
// so the arm runs its 141=Y reset unit, whose store operation is the HookedStore's
// reset_to: in forward mode the inner MemoryStore's one-step reset_to, in default-body
// mode MessageStore's default body over the store's own reset() and next_seqnum().
// close(terminal) is posted from that operation's hook (on_reset), and the operation
// holds as each cell states, so close() begins inside the unit. The durable counters are
// the inner MemoryStore's, read after stop(). The unit's targets (research R-6): next-in
// 2, because the Logon was consumed; next-out 1, because the session sent no 141=Y.
namespace {

constexpr sess::seqnum_t kUnitIn = sess::seqnum_min + 1;
constexpr sess::seqnum_t kUnitOut = sess::seqnum_min;

void expect_store_counters(LogonCloseOutcome const& o, sess::seqnum_t in, sess::seqnum_t out) {
    EXPECT_EQ(o.store_next_inbound, std::optional{in}) << "store writes: " << store_writes(o);
    EXPECT_EQ(o.store_next_outbound, std::optional{out}) << "store writes: " << store_writes(o);
}

// The 141=Y peer case for `role`, the unit's store operation hooked.
LogonCloseOutcome run_unit_case(sess::session_role role, LogonCloseCase c) {
    c.mode = sess::close_mode::terminal;
    c.arm_on = "";
    c.peer_logon_extra = "141=Y\x01";
    c.store_outbound_next = 1;
    c.close_from_reset = true;
    return role == sess::session_role::acceptor ? run_acceptor_case(c) : run_initiator_case(c);
}

// A second connection over the store `first` left, with no close: the peer's Logon (or
// Logon-ack) at 34=2 without 141=Y.
LogonCloseOutcome run_next_logon(sess::session_role role, LogonCloseOutcome const& first) {
    LogonCloseCase c{.mode = std::nullopt, .arm_on = "", .store_outbound_next = 1};
    c.reuse_store = first.store_log;
    c.peer_logon_seq = 2;
    return role == sess::session_role::acceptor ? run_acceptor_case(c) : run_initiator_case(c);
}

// Q-23: no teardown reset. close() returns while the unit's store operation is held, so
// its seqnum drain runs inside the unit. The durable counters are the unit's targets,
// and the peer's next Logon at 34=2 without 141=Y is accepted (789 off): Active, with no
// ResendRequest.
void run_q23(sess::session_role role) {
    auto o = run_unit_case(role, {.hold_until_close_returned = true});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    ASSERT_TRUE(o.store_log);
    EXPECT_FALSE(o.store_log->hold_timed_out)
        << "close() did not return while the unit's store operation was held";
    expect_store_counters(o, kUnitIn, kUnitOut);

    auto next = run_next_logon(role, o);
    ASSERT_TRUE(next.bound);
    EXPECT_TRUE(next.settled) << "the next Logon at 34=2 did not reach Active; ring="
                              << joined(next.ring);
    EXPECT_EQ(next.state_after_settle, std::optional{sess::fsm_state::Active})
        << "ring=" << joined(next.ring);
    EXPECT_EQ(std::ranges::count(next.seen.to_admin_all, std::string{"2"}), 0)
        << "a ResendRequest answered the Logon at 34=2; admin frames: "
        << joined(next.seen.to_admin_all);
}

// Q-24: teardown reset (reset_on_disconnect). The unit's store operation holds until
// close() has begun. The final durable state is (1, 1), and close()'s teardown reset
// was issued.
void run_q24(sess::session_role role) {
    auto o = run_unit_case(role, {.reset_on_disconnect = true, .hold_until_close_began = true});
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    ASSERT_TRUE(o.store_log);
    EXPECT_FALSE(o.store_log->hold_timed_out) << "close() never began";
    EXPECT_GE(o.store_log->resets_issued_after_close_began, 1)
        << "no teardown reset; store writes: " << store_writes(o);
    expect_store_counters(o, sess::seqnum_min, sess::seqnum_min);
}

// Q-25: a default-body store. With a teardown reset, the unit's reset() holds until a
// reset() is issued after close() began: close() waits for the unit instead, so the
// hold waits out its bound, the unit completes, and the teardown reset then leaves
// (1, 1). Without one, the hold ends when close() returns, and the durable counters are
// the unit's targets.
void run_q25(sess::session_role role, bool teardown) {
    LogonCloseCase c{.reset_on_disconnect = teardown,
                     .store_mode = fixpp::test_support::reset_to_mode::default_body};
    if (teardown) {
        c.hold_until_close_reset = true;
    } else {
        c.hold_until_close_returned = true;
    }
    auto o = run_unit_case(role, c);
    ASSERT_TRUE(o.bound);
    ASSERT_TRUE(o.settled) << "ring=" << joined(o.ring);
    expect_close_owns_teardown(o);
    ASSERT_TRUE(o.store_log);
    if (teardown) {
        EXPECT_TRUE(o.store_log->hold_timed_out)
            << "close() issued its teardown reset while the unit's reset() was held";
        EXPECT_GE(o.store_log->resets_issued_after_close_began, 1)
            << "no teardown reset; store writes: " << store_writes(o);
        expect_store_counters(o, sess::seqnum_min, sess::seqnum_min);
    } else {
        EXPECT_FALSE(o.store_log->hold_timed_out)
            << "close() did not return while the unit's reset() was held";
        expect_store_counters(o, kUnitIn, kUnitOut);
    }
}

}  // namespace

TEST(LogonCloseDuringSuspension, Q23_AcceptorCloseDrainsInsideTheUnitAndTheNextLogonAt2IsAccepted) {
    run_q23(sess::session_role::acceptor);
}
TEST(LogonCloseDuringSuspension,
     Q23_InitiatorCloseDrainsInsideTheUnitAndTheNextLogonAt2IsAccepted) {
    run_q23(sess::session_role::initiator);
}
TEST(LogonCloseDuringSuspension, Q24_AcceptorTeardownResetLeavesOneOne) {
    run_q24(sess::session_role::acceptor);
}
TEST(LogonCloseDuringSuspension, Q24_InitiatorTeardownResetLeavesOneOne) {
    run_q24(sess::session_role::initiator);
}
TEST(LogonCloseDuringSuspension, Q25_AcceptorDefaultBodyWithTeardownResetLeavesOneOne) {
    run_q25(sess::session_role::acceptor, /*teardown=*/true);
}
TEST(LogonCloseDuringSuspension, Q25_InitiatorDefaultBodyWithTeardownResetLeavesOneOne) {
    run_q25(sess::session_role::initiator, /*teardown=*/true);
}
TEST(LogonCloseDuringSuspension, Q25_AcceptorDefaultBodyWithoutTeardownResetLeavesTheTargets) {
    run_q25(sess::session_role::acceptor, /*teardown=*/false);
}
TEST(LogonCloseDuringSuspension, Q25_InitiatorDefaultBodyWithoutTeardownResetLeavesTheTargets) {
    run_q25(sess::session_role::initiator, /*teardown=*/false);
}

// HookedStore in forward mode (093 tasks.md T080; quickstart §2): reset_to reaches the
// inner MemoryStore's reset_to and fires the on_reset hook there. In default-body mode
// the same call is the store's own reset() and next_seqnum(). The log tells them apart,
// so this cell fails if forward mode stops forwarding.
TEST(HookedStoreResetTo, ForwardModeForwardsToTheInnerStoreAndFiresItsHook) {
    for (auto const mode : {fixpp::test_support::reset_to_mode::forward,
                            fixpp::test_support::reset_to_mode::default_body}) {
        auto log = std::make_shared<StoreLog>();
        bool hook_fired = false;
        HookedStore::Hooks hooks;
        hooks.on_reset = [&hook_fired] { hook_fired = true; };
        HookedStore store{/*outbound_next=*/1, std::move(hooks), log, mode};
        asio::io_context ioc;
        std::optional<bool> ok;
        std::optional<sess::seqnum_t> in;
        std::optional<sess::seqnum_t> out;
        asio::co_spawn(
            ioc,
            [&]() -> asio::awaitable<void> {
                auto r = co_await store.reset_to(kUnitIn, kUnitOut);
                ok = r.has_value();
                auto i = co_await log->inner->next_seqnum(sess::direction_t::inbound, false);
                auto o = co_await log->inner->next_seqnum(sess::direction_t::outbound, false);
                if (i) in = *i;
                if (o) out = *o;
            },
            asio::detached);
        ioc.run();
        bool const forward = mode == fixpp::test_support::reset_to_mode::forward;
        char const* const name = forward ? "forward" : "default body";
        EXPECT_EQ(ok, std::optional{true}) << name;
        EXPECT_TRUE(hook_fired) << name << ": on_reset";
        EXPECT_EQ(in, std::optional{kUnitIn}) << name;
        EXPECT_EQ(out, std::optional{kUnitOut}) << name;
        std::vector<std::string> ops;
        for (auto const& w : log->writes) ops.push_back(w.op);
        std::vector<std::string> const want_ops = forward
                                                      ? std::vector<std::string>{"reset_to 2 1"}
                                                      : std::vector<std::string>{"reset", "in+1"};
        EXPECT_EQ(ops, want_ops) << name << ": the store operations the call made";
    }
}

// ── Q-27 (093 tasks.md T081; contract C-6 "close()"; data-model E-10) ──────────
//
// close()'s bounded wait for an in-flight reset unit, on a mock clock. An acceptor
// Session over a FileStore whose file-I/O executor is an io_context the cell runs only
// when it chooses, so the unit's reset_to is held inside the store, with the store's
// writer lock taken, for as long as the cell wants. reset_on_disconnect is set, so
// close() is about to issue its teardown reset with the unit in flight and waits:
// its completion signal raced against await_deadline on the session's clock, bounded by
// logon_timeout_ms.
//   - Expiry: once the clock reaches the bound, close() records
//     session_event_close_reset_wait_expired and proceeds; its teardown reset queues on
//     the FileStore's writer lock behind the unit, so a restart reads (1, 1).
//   - Re-arm: a clock-wide cancel_sleeps() during the wait (close()'s own sweep comes
//     before its teardown reset, so only another sweep reaches it) records no expiry,
//     and close() is still waiting, until the clock reaches the bound.
// Time moves only by the cell's advance; drain_ready runs what an advance made ready.
namespace {

void drain_ready_q27(asio::io_context& ioc) {
    ioc.restart();
    while (ioc.poll() > 0) {
        ioc.restart();
    }
    ioc.restart();
}

bool has_wait_expired_event(sess::Session const& s) {
    return std::ranges::any_of(s.recent_events(), [](auto const& ev) {
        return std::holds_alternative<sess::session_event_close_reset_wait_expired>(ev);
    });
}

constexpr std::uint32_t kQ27Bound = 1000;  // logon_timeout_ms

struct Q27Rig {
    asio::io_context ioc;
    asio::io_context fio;  // the FileStore's file-I/O executor; run only by the cell
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    std::filesystem::path dir = fixpp::test_support::unique_temp_dir("q27");
    // The session's role, and whether close() issues a teardown reset
    // (reset_on_disconnect).
    sess::session_role role = sess::session_role::acceptor;
    bool teardown = true;
    // MsgType(35) of every frame the session sent.
    std::shared_ptr<std::vector<std::string>> sent = std::make_shared<std::vector<std::string>>();

    // `steady_seed` is the mock clock's initial steady_now(); the UTC side is seeded
    // independently.
    explicit Q27Rig(fixpp::core::steady_time_point steady_seed = {})
        : clock{std::make_shared<fixpp::core::mock_clock>(
              std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200},
              steady_seed, ioc.get_executor())} {
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }
    ~Q27Rig() { (void)fixpp::test_support::try_remove_temp_dir(dir); }
    Q27Rig(Q27Rig const&) = delete;
    Q27Rig& operator=(Q27Rig const&) = delete;

    sess::FileStore::Config store_config() {
        sess::FileStore::Config c;
        c.directory = dir;
        c.sender_comp_id = "ISLD";
        c.target_comp_id = "TW";
        c.max_frame_bytes = 4096;
        c.file_io_executor = fio.get_executor();
        return c;
    }

    sess::SessionConfig cfg() {
        sess::SessionConfig c;
        c.sender_comp_id = "ISLD";
        c.target_comp_id = "TW";
        c.begin_string = "FIX.4.2";
        c.role = role;
        c.heartbeat_interval = std::chrono::seconds{30};
        c.security_profile = fixpp::test_support::make_minimal_security_profile();
        c.dictionary = fixpp::test_support::make_minimal_dictionary();
        c.executor_override = ioc.get_executor();
        c.reset_seqnum_policy_field = sess::reset_seqnum_policy::bilateral_lenient;
        c.transport_send = [out = sent](std::span<const std::byte> frame) {
            std::string_view const f{reinterpret_cast<char const*>(frame.data()), frame.size()};
            auto const at = f.find(
                "\x01"
                "35=");
            if (at == std::string_view::npos) return;
            auto const end = f.find('\x01', at + 4);
            out->emplace_back(f.substr(at + 4, end == std::string_view::npos ? 0 : end - at - 4));
        };
        c.reset_on_disconnect = teardown;
        c.logon_timeout_ms = kQ27Bound;
        c.store_factory = std::make_shared<sess::FileStoreFactory>(store_config());
        return c;
    }

    // Runs both io_contexts until `ready` holds or neither has work left.
    template <class Ready>
    bool run_both_until(Ready ready) {
        for (int i = 0; i < 100000 && !ready(); ++i) {
            fio.restart();
            std::size_t const n = fio.poll();
            ioc.restart();
            std::size_t const m = ioc.poll();
            if (n == 0 && m == 0 && !ready()) return false;
        }
        return ready();
    }

    // The counters a restart over the store directory reads.
    std::pair<sess::seqnum_t, sess::seqnum_t> restart_counters() {
        sess::FileStoreFactory factory{store_config()};
        auto minted = factory.make("ISLD", "TW", nullptr, 1024 * 1024 * 1024, fio.get_executor());
        if (!minted) return {0, 0};
        auto& store = **minted;
        std::pair<sess::seqnum_t, sess::seqnum_t> out{0, 0};
        auto fut = asio::co_spawn(
            ioc,
            [&]() -> asio::awaitable<void> {
                auto in = co_await store.next_seqnum(sess::direction_t::inbound, false);
                auto ob = co_await store.next_seqnum(sess::direction_t::outbound, false);
                out = {in.value_or(0), ob.value_or(0)};
            },
            asio::use_future);
        (void)run_both_until([&] { return fut.wait_for(0s) == std::future_status::ready; });
        return out;
    }
};

std::vector<std::byte> q27_logon_141() {
    return make_fix_frame("FIX.4.2",
                          "35=A\x01"
                          "34=1\x01"
                          "49=TW\x01"
                          "52=20240101-00:00:00.000\x01"
                          "56=ISLD\x01"
                          "98=0\x01"
                          "108=30\x01"
                          "141=Y\x01");
}

// Opens the session, feeds the 141=Y Logon until the unit's reset_to is held in the
// FileStore, then starts close(terminal) and runs until it waits for the unit.
struct Q27Run {
    std::future<fixpp::core::expected_t<void>> feed;
    std::future<fixpp::core::expected_t<void>> close;
};

void q27_start(Q27Rig& r, sess::Session& s, Q27Run& run) {
    auto open = asio::co_spawn(r.ioc, s.open(), asio::use_future);
    ASSERT_TRUE(r.run_both_until([&] { return open.wait_for(0s) == std::future_status::ready; }))
        << "open()";
    ASSERT_TRUE(open.get().has_value()) << "open()";
    auto const logon = q27_logon_141();
    run.feed = asio::co_spawn(r.ioc, s.on_inbound_frame(logon), asio::use_future);
    drain_ready_q27(r.ioc);
    ASSERT_NE(run.feed.wait_for(0s), std::future_status::ready)
        << "the Logon completed: the unit's store operation was not held in the FileStore";
    run.close = asio::co_spawn(r.ioc, s.close(sess::close_mode::terminal), asio::use_future);
    drain_ready_q27(r.ioc);
    ASSERT_NE(run.close.wait_for(0s), std::future_status::ready)
        << "close() completed with the unit in flight: it did not wait";
}

// After the cell: let the unit and close() finish, so nothing outlives the rig.
void q27_finish(Q27Rig& r, Q27Run& run) {
    if (!run.close.valid()) return;
    r.clock->advance(std::chrono::milliseconds{kQ27Bound});
    EXPECT_TRUE(r.run_both_until([&] {
        return run.close.wait_for(0s) == std::future_status::ready &&
               (!run.feed.valid() || run.feed.wait_for(0s) == std::future_status::ready);
    })) << "close() or the Logon never completed";
}

}  // namespace

TEST(LogonCloseDuringSuspension, Q27_TheWaitExpiresIsRecordedAndFileStoreStillEndsAtOneOne) {
    Q27Rig r;
    Q27Run run;
    {
        sess::Session s{r.engine, r.cfg()};
        q27_start(r, s, run);
        if (!::testing::Test::HasFatalFailure()) {
            EXPECT_FALSE(has_wait_expired_event(s)) << "expired before the clock moved";
            // KIND D (ci/mock-clock-staging-sweep.sh): close()'s wait deadline is a stored
            // anchor, taken when q27_start's drain left close() parked on it, so it predates
            // the advance; nothing must fire at the bound minus 1 ms.
            r.clock->advance(std::chrono::milliseconds{kQ27Bound - 1});
            drain_ready_q27(r.ioc);
            EXPECT_FALSE(has_wait_expired_event(s)) << "expired one millisecond before the bound";
            EXPECT_NE(run.close.wait_for(0s), std::future_status::ready);
            // KIND D (ci/mock-clock-staging-sweep.sh): the same stored anchor, so a late arm
            // fires at once.
            r.clock->advance(std::chrono::milliseconds{1});
            drain_ready_q27(r.ioc);
            EXPECT_TRUE(has_wait_expired_event(s))
                << "session_event_close_reset_wait_expired at the bound";
            EXPECT_TRUE(r.run_both_until([&] {
                return run.close.wait_for(0s) == std::future_status::ready;
            })) << "close() did not complete after the wait expired";
        }
        q27_finish(r, run);
        if (run.close.valid() && run.close.wait_for(0s) == std::future_status::ready) {
            EXPECT_TRUE(run.close.get().has_value()) << "close()";
        }
    }
    EXPECT_EQ(r.restart_counters(), (std::pair{sess::seqnum_min, sess::seqnum_min}))
        << "the teardown reset, queued behind the unit on the writer lock, must end at (1, 1)";
}

TEST(LogonCloseDuringSuspension, Q27_AClockWideSweepDuringTheWaitDoesNotEndIt) {
    Q27Rig r;
    Q27Run run;
    {
        sess::Session s{r.engine, r.cfg()};
        q27_start(r, s, run);
        if (!::testing::Test::HasFatalFailure()) {
            r.clock->cancel_sleeps();
            drain_ready_q27(r.ioc);
            EXPECT_FALSE(has_wait_expired_event(s))
                << "a clock-wide sweep ended close()'s wait as an expiry";
            EXPECT_NE(run.close.wait_for(0s), std::future_status::ready)
                << "a clock-wide sweep ended close()'s wait";
            // KIND D (ci/mock-clock-staging-sweep.sh): the re-armed wait sleeps to the stored
            // anchor taken before the sweep, so it predates the advance; nothing must fire
            // at the bound minus 1 ms.
            r.clock->advance(std::chrono::milliseconds{kQ27Bound - 1});
            drain_ready_q27(r.ioc);
            EXPECT_FALSE(has_wait_expired_event(s)) << "expired before the bound after the sweep";
            // KIND D (ci/mock-clock-staging-sweep.sh): the same stored anchor, so a late arm
            // fires at once.
            r.clock->advance(std::chrono::milliseconds{1});
            drain_ready_q27(r.ioc);
            EXPECT_TRUE(has_wait_expired_event(s)) << "the re-armed wait expires at the bound";
        }
        q27_finish(r, run);
    }
}

// The bound near the clock's representation limit: logon_timeout_ms at its largest
// accepted value, with the session clock seeded one hour short of
// steady_time_point::max(). close()'s bound is not representable, so it saturates at
// max(): the wait has not expired one millisecond before the step to max(), and expires
// at max(). The close is terminal, so it crosses no logout bound; the hour is above
// every other duration the cell crosses.
TEST(LogonCloseDuringSuspension, Q27_TheBoundSaturatesAtTheClockMax) {
    constexpr auto kHeadroom = std::chrono::hours{1};
    auto const max = fixpp::core::steady_time_point::max();
    Q27Rig r{max - kHeadroom};
    Q27Run run;
    {
        auto cfg = r.cfg();
        cfg.logon_timeout_ms = std::numeric_limits<std::uint32_t>::max();
        sess::Session s{r.engine, cfg};
        q27_start(r, s, run);
        if (!::testing::Test::HasFatalFailure()) {
            EXPECT_FALSE(has_wait_expired_event(s)) << "expired before the clock moved";
            // KIND C (ci/mock-clock-staging-sweep.sh): short of max(), nothing may fire;
            // that is the oracle.
            r.clock->advance(kHeadroom - std::chrono::milliseconds{1});
            drain_ready_q27(r.ioc);
            EXPECT_FALSE(has_wait_expired_event(s)) << "expired 1 ms short of max()";
            EXPECT_NE(run.close.wait_for(0s), std::future_status::ready);
            // KIND D (ci/mock-clock-staging-sweep.sh): close()'s wait deadline is a stored
            // anchor predating the step, so a late arm fires at once.
            r.clock->step_to(max);
            drain_ready_q27(r.ioc);
            EXPECT_TRUE(has_wait_expired_event(s)) << "the wait did not expire at max()";
        }
        // The clock is at max(): finish without advancing it further.
        if (run.close.valid()) {
            EXPECT_TRUE(r.run_both_until([&] {
                return run.close.wait_for(0s) == std::future_status::ready &&
                       (!run.feed.valid() || run.feed.wait_for(0s) == std::future_status::ready);
            })) << "close() or the Logon never completed";
        }
    }
}

// ── Q-23 and Q-24 over a FileStore (contract C-6's outcome table, "every store") ──
//
// Q27Rig's FileStore holds the unit's reset_to, with its writer lock taken, while the
// cell does not run the file-I/O executor; close(terminal) begins there. The clock never
// moves, so close()'s bounded wait cannot expire:
//   - Q-23, no teardown reset: close() issues no teardown reset, so it does not wait for
//     the unit and returns while the unit is held; a restart reads the unit's targets,
//     and a second session over the directory accepts the peer's next Logon at 34=2
//     without 141=Y: Active, with no ResendRequest;
//   - Q-24, a teardown reset: close() waits for the unit, then resets; a restart reads
//     (1, 1). close() cannot return while the unit holds the writer lock with or without
//     that wait, since its teardown reset queues on the lock, so the cell asserts the
//     counters and not the wait.
// The initiator's own Logon carries no 141=Y, so the unit's targets are (2, 1) in both
// roles.
namespace {

// The frame that answers the session's Logon, or opens the acceptor's: MsgSeqNum
// `seq`, without 141=Y.
std::vector<std::byte> q2324_logon(int seq) {
    return make_fix_frame("FIX.4.2",
                          "35=A\x01"
                          "34=" +
                              std::to_string(seq) +
                              "\x01"
                              "49=TW\x01"
                              "52=20240101-00:00:00.000\x01"
                              "56=ISLD\x01"
                              "98=0\x01"
                              "108=30\x01");
}

void run_q2324_file_store(sess::session_role role, bool teardown) {
    Q27Rig r;
    r.role = role;
    r.teardown = teardown;
    {
        sess::Session s{r.engine, r.cfg()};
        auto open = asio::co_spawn(r.ioc, s.open(), asio::use_future);
        ASSERT_TRUE(r.run_both_until([&] {
            return open.wait_for(0s) == std::future_status::ready;
        })) << "open()";
        ASSERT_TRUE(open.get().has_value()) << "open()";
        auto const logon = q27_logon_141();
        auto feed = asio::co_spawn(r.ioc, s.on_inbound_frame(logon), asio::use_future);
        drain_ready_q27(r.ioc);
        bool const held = feed.wait_for(0s) != std::future_status::ready &&
                          sess::session_test_access::reset_unit_in_flight(s);
        auto close = asio::co_spawn(r.ioc, s.close(sess::close_mode::terminal), asio::use_future);
        drain_ready_q27(r.ioc);
        bool const close_returned_while_held = close.wait_for(0s) == std::future_status::ready &&
                                               sess::session_test_access::reset_unit_in_flight(s);
        bool const done = r.run_both_until([&] {
            return close.wait_for(0s) == std::future_status::ready &&
                   feed.wait_for(0s) == std::future_status::ready;
        });
        if (!done) {
            // Let the unit and close() finish, so nothing outlives the rig.
            // KIND G (ci/mock-clock-staging-sweep.sh): this advance runs only when `done` is
            // false, and the ASSERT_TRUE(done) below then returns before any read of the
            // wait-expired event or the restart counters, so nothing the cell asserts depends
            // on it.
            r.clock->advance(std::chrono::milliseconds{kQ27Bound});
            (void)r.run_both_until([&] {
                return close.wait_for(0s) == std::future_status::ready &&
                       feed.wait_for(0s) == std::future_status::ready;
            });
        }
        ASSERT_TRUE(held) << "the unit's store operation was not held in the FileStore";
        ASSERT_TRUE(done) << "close() or the Logon never completed";
        EXPECT_TRUE(close.get().has_value()) << "close()";
        EXPECT_FALSE(has_wait_expired_event(s)) << "the wait expired, though the clock never moved";
        if (!teardown) {
            EXPECT_TRUE(close_returned_while_held)
                << "close() waited for the unit, though it issues no teardown reset";
        }
    }
    auto const want =
        teardown ? std::pair{sess::seqnum_min, sess::seqnum_min} : std::pair{kUnitIn, kUnitOut};
    EXPECT_EQ(r.restart_counters(), want)
        << "the counters a restart reads; want the unit's targets without a teardown reset, "
           "(1, 1) with one";
    if (teardown) return;

    // The peer's next Logon at 34=2, without 141=Y, over the directory the first left.
    r.sent->clear();
    sess::Session s2{r.engine, r.cfg()};
    auto open = asio::co_spawn(r.ioc, s2.open(), asio::use_future);
    ASSERT_TRUE(r.run_both_until([&] { return open.wait_for(0s) == std::future_status::ready; }))
        << "open()";
    ASSERT_TRUE(open.get().has_value()) << "open()";
    auto const next_logon = q2324_logon(2);
    auto feed = asio::co_spawn(r.ioc, s2.on_inbound_frame(next_logon), asio::use_future);
    EXPECT_TRUE(r.run_both_until([&] { return feed.wait_for(0s) == std::future_status::ready; }))
        << "the next Logon";
    EXPECT_EQ(s2.state(), sess::fsm_state::Active) << "the next Logon at 34=2 was not accepted";
    EXPECT_EQ(std::ranges::count(*r.sent, std::string{"2"}), 0)
        << "a ResendRequest answered the Logon at 34=2; frames sent: " << joined(*r.sent);
    auto close = asio::co_spawn(r.ioc, s2.close(sess::close_mode::terminal), asio::use_future);
    EXPECT_TRUE(r.run_both_until([&] { return close.wait_for(0s) == std::future_status::ready; }))
        << "the second session's close()";
}

}  // namespace

TEST(LogonCloseDuringSuspension,
     Q23_FileStoreAcceptorCloseDrainsInsideTheUnitAndTheNextLogonAt2IsAccepted) {
    run_q2324_file_store(sess::session_role::acceptor, /*teardown=*/false);
}
TEST(LogonCloseDuringSuspension,
     Q23_FileStoreInitiatorCloseDrainsInsideTheUnitAndTheNextLogonAt2IsAccepted) {
    run_q2324_file_store(sess::session_role::initiator, /*teardown=*/false);
}
TEST(LogonCloseDuringSuspension, Q24_FileStoreAcceptorTeardownResetLeavesOneOne) {
    run_q2324_file_store(sess::session_role::acceptor, /*teardown=*/true);
}
TEST(LogonCloseDuringSuspension, Q24_FileStoreInitiatorTeardownResetLeavesOneOne) {
    run_q2324_file_store(sess::session_role::initiator, /*teardown=*/true);
}

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop  // -Wdeprecated-declarations (insecure_plain_tcp, 043 T020)
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
