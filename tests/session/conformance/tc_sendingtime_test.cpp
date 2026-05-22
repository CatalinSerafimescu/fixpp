// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/conformance/tc_sendingtime_test.cpp
//
// [FIX-TC] SendingTime conformance subset — 005-session-establishment-fsm T053
// Phase 7 / US5.
//
// Scenarios from the QuickFIX oracle (research D-10, version-coverage ledger):
//
//   1d_InvalidLogonBadSendingTime (fix42 + fix44):
//     "If a Logon is received with a bad SendingTime, send a Logout."
//     Oracle: inbound Logon with SendingTime > MaxLatency from now →
//             outbound Logout(35=5) (no standalone Reject), → disconnect.
//     (D-3: the offending message IS the Logon → logout-with-error, no Reject)
//
//   2o_SendingTimeValueOutOfRange (fix42 + fix44):
//     "If an established message has SendingTime out of range, send Reject then Logout."
//     Oracle: inbound message in Active with stale SendingTime →
//             outbound Reject(35=3, 373=10, 371=52) → outbound Logout(35=5) → disconnect.
//
// EXPLICITLY DEFERRED (per D-10, tasks.md T053 note):
//   FIXT.1.1 / FIX 5.0 SP2 variants — no oracle dir; deferred per D-10.
//
// Anchors: research D-3/D-8/D-10; spec FR-013; SC-007; [FIX-SL §4.2.3];
// tasks.md T053; error slot 71.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/session/seqnum.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/transport_double.hpp"
#include "support/store_double.hpp"

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace fixpp::session::conformance_test {
namespace {

// ── Frame builders ────────────────────────────────────────────────────────────

// Build a frame with an explicit SendingTime value.
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

// ── Test fixture ──────────────────────────────────────────────────────────────

struct SendingTimeConformanceFixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    fixpp::session::test::TransportDouble transport;

    // Mock "now" = 2024-01-01 00:00:00 UTC = unix 1704067200.
    static constexpr std::int64_t kNowSec = 1704067200;

    SendingTimeConformanceFixture() {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{kNowSec};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock    = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_cfg(std::string_view begin_string = "FIX.4.2") {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id     = "ISLD";
        cfg.target_comp_id     = "TW";
        cfg.begin_string       = std::string(begin_string);
        cfg.heartbeat_interval = 30s;
        // Use default MaxLatency (120 s).
        cfg.security_profile   = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary         = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override  = ioc.get_executor();
        cfg.transport_send     = [this](std::span<const std::byte> frame) {
            transport.capture_outbound(frame);
        };
        return cfg;
    }

    // Drive session to Active with a valid (fresh) SendingTime.
    void open_to_active(fixpp::session::Session& sess,
                        std::string_view begin_string = "FIX.4.2") {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        // Valid Logon with fresh SendingTime matching mock clock now.
        auto logon = make_frame_with_sending_time(
            begin_string, "A", 1, "TW", "ISLD",
            "20240101-00:00:00.000",
            "98=0\x01""108=30\x01");
        auto fut2  = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        ASSERT_TRUE(fut2.get().has_value()) << "Logon-ack failed";
        ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);
    }

    void feed(fixpp::session::Session& sess, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        (void)fut.get();
    }
};

}  // namespace

// ── 1d_InvalidLogonBadSendingTime (fix42) ────────────────────────────────────
//
// Oracle: "If a Logon is received with a bad SendingTime, send a Logout."
// Scenario:
//   CONNECT
//   open() → LogonSent
//   I:Logon(stale SendingTime 300 s in the past) →
//   E:Logout(35=5)   ← engine emits logout-with-error, NO Reject
//   eDISCONNECT
//
// D-3: the offending message IS the Logon → logout-with-error only.
// The transport must see Logout(35=5), NOT Reject(35=3).
TEST(TCSendingTime, Fix42_1d_InvalidLogonBadSendingTime) {
    SendingTimeConformanceFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    fixpp::session::Session sess(f.engine, cfg);

    // open() → LogonSent
    auto fut = asio::co_spawn(f.ioc, sess.open(), asio::use_future);
    f.ioc.run_for(200ms);
    f.ioc.restart();
    ASSERT_TRUE(fut.get().has_value());
    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::LogonSent);

    const std::size_t before = f.transport.sent_count();

    // Stale Logon: SendingTime = now − 300 s = "20231231-23:55:00.000"
    auto stale_logon = make_frame_with_sending_time(
        "FIX.4.2", "A", 1, "TW", "ISLD",
        "20231231-23:55:00.000",
        "98=0\x01""108=30\x01");
    f.feed(sess, stale_logon);

    // Must emit a Logout(35=5) — NOT a Reject(35=3).
    bool found_reject = false;
    bool found_logout = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        auto mt = extract_field(f.transport.sent(i), 35);
        if (mt == "3") { found_reject = true; }
        if (mt == "5") { found_logout = true; }
    }
    EXPECT_FALSE(found_reject)
        << "1d fix42: stale Logon SendingTime must NOT trigger Reject(35=3) (D-3)";
    EXPECT_TRUE(found_logout)
        << "1d fix42: stale Logon SendingTime must trigger Logout(35=5)";
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "1d fix42: session must be Disconnected after logout-with-error";
}

// ── 1d_InvalidLogonBadSendingTime (fix44) ────────────────────────────────────
TEST(TCSendingTime, Fix44_1d_InvalidLogonBadSendingTime) {
    SendingTimeConformanceFixture f;
    auto cfg = f.make_cfg("FIX.4.4");
    fixpp::session::Session sess(f.engine, cfg);

    auto fut = asio::co_spawn(f.ioc, sess.open(), asio::use_future);
    f.ioc.run_for(200ms);
    f.ioc.restart();
    ASSERT_TRUE(fut.get().has_value());
    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::LogonSent);

    const std::size_t before = f.transport.sent_count();

    auto stale_logon = make_frame_with_sending_time(
        "FIX.4.4", "A", 1, "TW", "ISLD",
        "20231231-23:55:00.000",
        "98=0\x01""108=30\x01");
    f.feed(sess, stale_logon);

    bool found_reject = false;
    bool found_logout = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        auto mt = extract_field(f.transport.sent(i), 35);
        if (mt == "3") { found_reject = true; }
        if (mt == "5") { found_logout = true; }
    }
    EXPECT_FALSE(found_reject) << "1d fix44: stale Logon must NOT trigger Reject";
    EXPECT_TRUE(found_logout)  << "1d fix44: stale Logon must trigger Logout";
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected);
}

// ── 2o_SendingTimeValueOutOfRange (fix42) ────────────────────────────────────
//
// Oracle: "If an established message has SendingTime out of range, send Reject then Logout."
// Scenario:
//   CONNECT + full Logon exchange → Active
//   I:Heartbeat(stale SendingTime) →
//   E:Reject(35=3, 373=10, 371=52)   ← Reject with SendingTime reason
//   E:Logout(35=5)                    ← then Logout
//   eDISCONNECT
TEST(TCSendingTime, Fix42_2o_SendingTimeValueOutOfRange) {
    SendingTimeConformanceFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_to_active(sess, "FIX.4.2");
    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);

    const std::size_t before = f.transport.sent_count();

    // Send a Heartbeat with stale SendingTime (300 s ago).
    auto stale_hb = make_frame_with_sending_time(
        "FIX.4.2", "0", 2, "TW", "ISLD",
        "20231231-23:55:00.000");
    f.feed(sess, stale_hb);

    // Must emit Reject(35=3, 373=10, 371=52).
    bool found_reject = false;
    bool found_logout = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        auto mt = extract_field(f.transport.sent(i), 35);
        if (mt == "3") {
            found_reject = true;
            EXPECT_EQ(extract_field(f.transport.sent(i), 373), "10")
                << "2o fix42: SessionRejectReason(373) must be 10 (SendingTime accuracy)";
            EXPECT_EQ(extract_field(f.transport.sent(i), 371), "52")
                << "2o fix42: RefTagID(371) must be 52 (SendingTime tag)";
        }
        if (mt == "5") { found_logout = true; }
    }
    EXPECT_TRUE(found_reject)  << "2o fix42: stale established SendingTime must emit Reject";
    EXPECT_TRUE(found_logout)  << "2o fix42: stale established SendingTime must emit Logout";
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "2o fix42: session must be Disconnected after Reject+Logout";
}

// ── 2o_SendingTimeValueOutOfRange (fix44) ────────────────────────────────────
TEST(TCSendingTime, Fix44_2o_SendingTimeValueOutOfRange) {
    SendingTimeConformanceFixture f;
    auto cfg = f.make_cfg("FIX.4.4");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_to_active(sess, "FIX.4.4");
    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);

    const std::size_t before = f.transport.sent_count();

    auto stale_hb = make_frame_with_sending_time(
        "FIX.4.4", "0", 2, "TW", "ISLD",
        "20231231-23:55:00.000");
    f.feed(sess, stale_hb);

    bool found_reject = false;
    bool found_logout = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        auto mt = extract_field(f.transport.sent(i), 35);
        if (mt == "3") {
            found_reject = true;
            EXPECT_EQ(extract_field(f.transport.sent(i), 373), "10");
            EXPECT_EQ(extract_field(f.transport.sent(i), 371), "52");
        }
        if (mt == "5") { found_logout = true; }
    }
    EXPECT_TRUE(found_reject)  << "2o fix44: stale established SendingTime must emit Reject";
    EXPECT_TRUE(found_logout)  << "2o fix44: stale established SendingTime must emit Logout";
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected);
}

// ── DEFERRED: FIXT/5.0SP2 variants ───────────────────────────────────────────
// FIXT.1.1 / FIX 5.0 SP2 SendingTime scenarios are deferred per D-10.
// No oracle dir for fixt11/fix50sp2 in the QFJ reference tree.

}  // namespace fixpp::session::conformance_test
