// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/sending_time_test.cpp
//
// Seam #8 — SendingTime(52) MaxLatency enforcement (Q3).
// (005-session-establishment-fsm T051 / Phase 7 / US5)
//
// TDD red-first: authored BEFORE T055/T056 implementation. Must FAIL until
// those tasks wire the MaxLatency check and the guard-precedence ordering.
// After T057 must be GREEN.
//
// Scenarios (FR-013, SC-007, Clarification Q3, D-3/D-8):
//
//  1. check_sending_time: within MaxLatency → returns ok (expected_t<void>{}).
//
//  2. check_sending_time: exceeds MaxLatency (|delta| > 120 s default) →
//     returns unexpected(session_sending_time_accuracy) (slot 71).
//
//  3. check_sending_time: exact MaxLatency boundary → ok (≤ is allowed).
//
//  4. Q3 established-session path: a non-Logon message with stale SendingTime
//     arriving in Active state triggers:
//       - outbound Reject(35=3, RefTagID=52, SessionRejectReason=10)
//       - immediately followed by outbound Logout(35=5)
//       - FSM → Disconnected
//     The transport_send callback sees BOTH frames in order.
//
//  5. Q3 Logon-path special case (D-3, FR-013): a Logon(35=A) with a stale
//     SendingTime triggers a logout-with-error (outbound Logout only — NO
//     standalone Reject is emitted before the Logout on the Logon path).
//     The transport must see a Logout(35=5) as the outbound response, NOT a
//     Reject(35=3).
//
// Anchors: data-model.md E7/E8; research D-3/D-5/D-8; error slot 71;
// [FIX-SL §4.2.3]; spec FR-013; SC-007; tasks.md T051/T055/T056.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/sending_time.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/transport_double.hpp"

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace fixpp::session::test {
namespace {

using fixpp::core::utc_time_point;

// ── check_sending_time unit tests (T055) ──────────────────────────────────────

// Test 1: within MaxLatency → ok
TEST(SendingTimeCheck, WithinMaxLatency) {
    utc_time_point epoch{};
    auto inbound = epoch + std::chrono::seconds{1000};
    auto now     = epoch + std::chrono::seconds{1090};  // 90 s delta, < 120 s

    auto r = fixpp::session::check_sending_time(inbound, now, std::chrono::seconds{120});
    EXPECT_TRUE(r.has_value())
        << "90 s delta with 120 s MaxLatency must return ok";
}

// Test 2: exceeds MaxLatency → session_sending_time_accuracy
TEST(SendingTimeCheck, ExceedsMaxLatency) {
    utc_time_point epoch{};
    auto inbound = epoch + std::chrono::seconds{1000};
    auto now     = epoch + std::chrono::seconds{1200};  // 200 s delta, > 120 s

    auto r = fixpp::session::check_sending_time(inbound, now, std::chrono::seconds{120});
    ASSERT_FALSE(r.has_value())
        << "200 s delta with 120 s MaxLatency must return error";
    EXPECT_EQ(r.error(), fixpp::core::error::session_sending_time_accuracy)
        << "Error must be session_sending_time_accuracy (slot 71)";
}

// Test 3: exactly at MaxLatency → ok (|delta| <= max_latency)
TEST(SendingTimeCheck, ExactBoundaryIsOk) {
    utc_time_point epoch{};
    auto inbound = epoch + std::chrono::seconds{1000};
    auto now     = epoch + std::chrono::seconds{1120};  // exactly 120 s delta

    auto r = fixpp::session::check_sending_time(inbound, now, std::chrono::seconds{120});
    EXPECT_TRUE(r.has_value())
        << "Exactly 120 s delta with 120 s MaxLatency must return ok (boundary inclusive)";
}

// Test 3b: stale in the past (inbound < now - MaxLatency)
TEST(SendingTimeCheck, StaleInPast) {
    utc_time_point epoch{};
    auto inbound = epoch + std::chrono::seconds{1000};
    auto now     = epoch + std::chrono::seconds{1200};  // inbound is 200s in the past

    auto r = fixpp::session::check_sending_time(inbound, now, std::chrono::seconds{120});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), fixpp::core::error::session_sending_time_accuracy);
}

// Test 3c: from the future (inbound > now + MaxLatency)
TEST(SendingTimeCheck, FarFutureSendingTime) {
    utc_time_point epoch{};
    auto inbound = epoch + std::chrono::seconds{1300};  // 300s in the future
    auto now     = epoch + std::chrono::seconds{1000};

    auto r = fixpp::session::check_sending_time(inbound, now, std::chrono::seconds{120});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), fixpp::core::error::session_sending_time_accuracy);
}

// ── Frame builders for integration tests ─────────────────────────────────────

// Build a FIX frame with an explicit SendingTime value.
static std::vector<std::byte> make_frame_with_sending_time(
        std::string_view begin_string,
        std::string_view msg_type,
        std::uint32_t seq,
        std::string_view sender,
        std::string_view target,
        std::string_view sending_time,
        std::string_view extra_body = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=" + std::string(sending_time) + "\x01";
    body += "56=" + std::string(target) + "\x01";
    if (!extra_body.empty()) { body += extra_body; }

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs  = 0;
    for (unsigned char c : full) { cs += c; }
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    for (char c : full) { frame.push_back(static_cast<std::byte>(c)); }
    return frame;
}

static std::string extract_field(std::span<const std::byte> frame,
                                  std::uint32_t tag_wanted) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = std::to_string(tag_wanted) + "=";
    auto pos = wire.find(needle);
    if (pos == std::string::npos) { return {}; }
    pos += needle.size();
    auto end = wire.find('\x01', pos);
    if (end == std::string::npos) { return {}; }
    return wire.substr(pos, end - pos);
}

// ── Integration fixture ───────────────────────────────────────────────────────

struct SendingTimeFixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    TransportDouble transport;

    // Fixed "now" in the mock_clock: 2024-01-01 00:00:00 UTC
    // epoch seconds from unix = 1704067200
    static constexpr std::int64_t kNowSec = 1704067200;

    SendingTimeFixture() {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{kNowSec};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock    = clock;
        engine.executor = ioc.get_executor();
    }

    SessionConfig make_cfg(std::string_view begin_string = "FIX.4.2") {
        SessionConfig cfg;
        cfg.sender_comp_id     = "ISLD";
        cfg.target_comp_id     = "TW";
        cfg.begin_string       = std::string(begin_string);
        cfg.heartbeat_interval = 30s;
        // Use default MaxLatency (120 s) — no sending_time_threshold override.
        cfg.security_profile   = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary         = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override  = ioc.get_executor();
        cfg.transport_send     = [this](std::span<const std::byte> frame) {
            transport.capture_outbound(frame);
        };
        return cfg;
    }

    // Drive session to Active via initiator path.
    // Uses the mock clock's "now" (2024-01-01-00:00:00) as the valid SendingTime.
    void open_to_active(Session& sess,
                        std::string_view begin_string = "FIX.4.2") {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        // Feed a valid Logon with SendingTime matching mock clock now.
        // "now" = 2024-01-01 00:00:00 UTC → "20240101-00:00:00.000"
        auto logon = make_frame_with_sending_time(
            begin_string, "A", 1, "TW", "ISLD",
            "20240101-00:00:00.000",
            "98=0\x01""108=30\x01");
        auto fut2  = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        ASSERT_TRUE(fut2.get().has_value()) << "Logon-ack failed";
        ASSERT_EQ(sess.state(), fsm_state::Active);
    }

    void feed(Session& sess, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        (void)fut.get();
    }
};

// ── Test 4: Q3 established-session — stale SendingTime → Reject+Logout+Disconnect
TEST(SendingTimeIntegration, StaleSendingTimeInActiveTriggersRejectThenLogout) {
    SendingTimeFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    Session sess(f.engine, cfg);
    f.open_to_active(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active);

    const std::size_t before = f.transport.sent_count();

    // Feed a Heartbeat(35=0) with SendingTime 300 s in the past
    // (well beyond the 120 s MaxLatency).
    // "20231231-23:55:00.000" = 2024-01-01 00:00:00 − 300 s
    auto stale_hb = make_frame_with_sending_time(
        "FIX.4.2", "0", 2, "TW", "ISLD",
        "20231231-23:55:00.000");
    f.feed(sess, stale_hb);

    // Must emit a Reject(35=3) with RefTagID=52, SessionRejectReason=10.
    bool found_reject = false;
    bool found_logout = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        auto mt = extract_field(f.transport.sent(i), 35);
        if (mt == "3") {
            found_reject = true;
            EXPECT_EQ(extract_field(f.transport.sent(i), 371), "52")
                << "RefTagID(371) must be 52 (SendingTime tag)";
            EXPECT_EQ(extract_field(f.transport.sent(i), 373), "10")
                << "SessionRejectReason(373) must be 10 (SendingTime accuracy)";
        } else if (mt == "5") {
            found_logout = true;
        }
    }
    EXPECT_TRUE(found_reject)
        << "Q3: stale SendingTime in Active must emit Reject(35=3, reason=10, refTag=52)";
    EXPECT_TRUE(found_logout)
        << "Q3: stale SendingTime in Active must emit Logout(35=5) after Reject";

    // Session must be Disconnected after Reject+Logout.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "Q3: session must be Disconnected after stale-SendingTime Reject+Logout";
}

// ── Test 5: Q3 Logon-path — stale Logon SendingTime → Logout ONLY (no Reject)
//
// Per D-3/FR-013: if the offending message IS the Logon itself, there is no
// standalone Reject. The response is a logout-with-error (Logout only).
// The transport must see a Logout(35=5) but NO Reject(35=3).
//
// The session is in LogonSent state (initiator just called open()).
// The peer's Logon reply arrives with a stale SendingTime.
TEST(SendingTimeIntegration, StaleLogonSendingTimeTriggersLogoutOnlyNoReject) {
    SendingTimeFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    Session sess(f.engine, cfg);

    // Call open() — session goes to LogonSent.
    auto fut = asio::co_spawn(f.ioc, sess.open(), asio::use_future);
    f.ioc.run_for(200ms);
    f.ioc.restart();
    ASSERT_TRUE(fut.get().has_value()) << "open() failed";
    ASSERT_EQ(sess.state(), fsm_state::LogonSent);

    const std::size_t before = f.transport.sent_count();

    // Feed a Logon with SendingTime 300 s in the past.
    // "20231231-23:55:00.000" = now − 300 s (well beyond 120 s MaxLatency).
    auto stale_logon = make_frame_with_sending_time(
        "FIX.4.2", "A", 1, "TW", "ISLD",
        "20231231-23:55:00.000",
        "98=0\x01""108=30\x01");
    f.feed(sess, stale_logon);

    // Must emit a Logout(35=5) as the error response.
    bool found_reject = false;
    bool found_logout = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        auto mt = extract_field(f.transport.sent(i), 35);
        if (mt == "3") { found_reject = true; }
        if (mt == "5") { found_logout = true; }
    }

    EXPECT_FALSE(found_reject)
        << "Q3 Logon-path: stale Logon SendingTime must NOT emit Reject(35=3) (D-3)";
    EXPECT_TRUE(found_logout)
        << "Q3 Logon-path: stale Logon SendingTime must emit Logout(35=5) (logout-with-error)";

    // Session must be Disconnected.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "Q3 Logon-path: session must be Disconnected after stale-Logon logout-with-error";
}

}  // namespace (anonymous)
}  // namespace fixpp::session::test
