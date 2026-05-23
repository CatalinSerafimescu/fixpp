// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/session_send_invalid_state_test.cpp
//
// 010-session-cfg-lifetime T013+T014 / Phase 4 / US2.
//
// FR-005 acceptance scenarios 1+2: Session::send returns
// error::session_invalid_state_for_send when the FSM is NOT Active.
//
// Tests:
//   SendPreLogon_ReturnsInvalidStateForSend — never-opened session (NotConnected).
//   SendInLogonSent_ReturnsInvalidStateForSend — initiator after open(), frozen in LogonSent.
//   SendInDisconnected_ReturnsInvalidStateForSend — session driven to Disconnected.
//   SendInActive_DoesNotReturnInvalidStateForSend — positive control: Active session.
//
// TDD: first three tests are authored RED here (pre-T014 they return
// session_invalid_logon, not session_invalid_state_for_send). T014 makes them GREEN.
// The positive control passes both before and after T014.
//
// Anchors: spec.md FR-005 / US2 AC1/AC2/AC3; data-model.md §E1 Session::send;
//          data-model.md §E3 error::session_invalid_state_for_send = 77; D-3.
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace fixpp::session::test {

// ── Frame builder ─────────────────────────────────────────────────────────────

static std::vector<std::byte> make_peer_logon_frame(
        std::string_view begin_string,
        std::uint32_t seq,
        std::string_view sender,
        std::string_view target) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    body += "98=0\x01";
    body += "108=30\x01";

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) { cs += c; }
    cs &= 0xFFu;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    for (char c : full) { frame.push_back(static_cast<std::byte>(c)); }
    return frame;
}

// ── Fixture ───────────────────────────────────────────────────────────────────

class SendInvalidStateTest : public ::testing::Test {
protected:
    asio::io_context                         ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig                engine{};

    void SetUp() override {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock    = clock;
        engine.executor = ioc.get_executor();
    }

    // Initiator config: sender=TW, target=ISLD, FIX.4.2, heartbeat disabled.
    fixpp::session::SessionConfig make_initiator_cfg() {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id     = "TW";
        cfg.target_comp_id     = "ISLD";
        cfg.begin_string       = "FIX.4.2";
        cfg.heartbeat_interval = 0s;  // disable liveness loop
        cfg.security_profile   = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary         = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override  = ioc.get_executor();
        cfg.role               = fixpp::session::session_role::initiator;
        return cfg;
    }

    // Open and run, returning the result.
    fixpp::core::expected_t<void> open_sync(fixpp::session::Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }

    // Feed an inbound frame.
    fixpp::core::expected_t<void> feed_sync(fixpp::session::Session& s,
                                            std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }

    // Call send() with a dummy payload, return the result.
    fixpp::core::expected_t<void> send_sync(fixpp::session::Session& s) {
        std::array<std::byte, 4> payload{};
        auto fut = asio::co_spawn(ioc, s.send(std::span<const std::byte>(payload)),
                                  asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }

    // Drive initiator to Active by feeding a valid peer Logon-ack.
    void drive_to_active(fixpp::session::Session& s) {
        auto r = open_sync(s);
        ASSERT_TRUE(r.has_value()) << "open() failed";
        ASSERT_EQ(s.state(), fsm_state::LogonSent) << "Initiator must be in LogonSent after open()";

        auto logon = make_peer_logon_frame("FIX.4.2", 1, "ISLD", "TW");
        auto feed_r = feed_sync(s, std::span<const std::byte>{logon});
        ASSERT_TRUE(feed_r.has_value()) << "Logon-ack feed failed";
        ASSERT_EQ(s.state(), fsm_state::Active) << "Must reach Active after Logon-ack";
    }
};

// ── Tests ─────────────────────────────────────────────────────────────────────

// AC1 (FR-005): send() on a never-opened session (NotConnected) returns
// session_invalid_state_for_send. Pre-T014: returns session_invalid_logon.
// Post-T014 (T014 changes that site): returns session_invalid_state_for_send.
// Anchors: spec.md FR-005 §US2 AC1; D-3; data-model.md §E3.
TEST_F(SendInvalidStateTest, SendPreLogon_ReturnsInvalidStateForSend) {
    auto cfg = make_initiator_cfg();
    fixpp::session::Session sess(engine, cfg);
    // Do NOT call open(). FSM is in NotConnected.

    EXPECT_EQ(sess.state(), fsm_state::NotConnected);

    auto result = send_sync(sess);
    ASSERT_FALSE(result.has_value())
        << "send() in NotConnected must return an error (FR-005 §US2 AC1)";
    EXPECT_EQ(result.error(), fixpp::core::error::session_invalid_state_for_send)
        << "Expected session_invalid_state_for_send (=77) but got "
        << static_cast<int>(result.error())
        << " (FR-005 / D-3)";
}

// AC2 (FR-005): send() while in LogonSent returns session_invalid_state_for_send.
// Freeze the FSM in LogonSent: open() an initiator, do NOT feed the peer Logon-ack.
// Anchors: spec.md FR-005 §US2 AC2; D-3.
TEST_F(SendInvalidStateTest, SendInLogonSent_ReturnsInvalidStateForSend) {
    auto cfg = make_initiator_cfg();
    fixpp::session::Session sess(engine, cfg);

    auto open_r = open_sync(sess);
    ASSERT_TRUE(open_r.has_value()) << "open() failed";
    // After open(), initiator is in LogonSent (no peer Logon-ack fed).
    ASSERT_EQ(sess.state(), fsm_state::LogonSent)
        << "Initiator must be in LogonSent (no Logon-ack fed)";

    auto result = send_sync(sess);
    ASSERT_FALSE(result.has_value())
        << "send() in LogonSent must return an error (FR-005 §US2 AC2)";
    EXPECT_EQ(result.error(), fixpp::core::error::session_invalid_state_for_send)
        << "Expected session_invalid_state_for_send (=77) but got "
        << static_cast<int>(result.error())
        << " (FR-005 / D-3)";
}

// AC3 (FR-005): send() in Disconnected returns session_invalid_state_for_send.
// Drive to Active, then close(terminal) → Disconnected, then send().
// Anchors: spec.md FR-005 §US2 AC3; D-3.
TEST_F(SendInvalidStateTest, SendInDisconnected_ReturnsInvalidStateForSend) {
    auto cfg = make_initiator_cfg();
    fixpp::session::Session sess(engine, cfg);

    drive_to_active(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // close(terminal) → Disconnected.
    {
        auto close_fut = asio::co_spawn(ioc, sess.close(close_mode::terminal), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        (void)close_fut.get();
    }
    ASSERT_EQ(sess.state(), fsm_state::Disconnected)
        << "close(terminal) must leave session in Disconnected";

    auto result = send_sync(sess);
    ASSERT_FALSE(result.has_value())
        << "send() in Disconnected must return an error (FR-005 §US2 AC3)";
    EXPECT_EQ(result.error(), fixpp::core::error::session_invalid_state_for_send)
        << "Expected session_invalid_state_for_send (=77) but got "
        << static_cast<int>(result.error())
        << " (FR-005 / D-3)";
}

// Positive control: send() in Active does NOT return session_invalid_state_for_send.
// The transport capture is empty (no store_factory), so send() will likely fail at
// the SeqnumManager or store step — but it must NOT return invalid_state_for_send.
// Anchors: spec.md FR-005 §US2; D-3.
TEST_F(SendInvalidStateTest, SendInActive_DoesNotReturnInvalidStateForSend) {
    auto cfg = make_initiator_cfg();
    // Wire a no-op transport so the send pipeline's transport_send step doesn't crash.
    cfg.transport_send = [](std::span<const std::byte>) {};

    fixpp::session::Session sess(engine, cfg);
    drive_to_active(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active);

    auto result = send_sync(sess);
    // The positive control only asserts the FSM gate did NOT fire.
    if (!result.has_value()) {
        EXPECT_NE(result.error(), fixpp::core::error::session_invalid_state_for_send)
            << "Active session: send() must NOT return session_invalid_state_for_send "
            << "(error=" << static_cast<int>(result.error()) << "); "
            << "any other error (e.g. no store, etc.) is acceptable here (FR-005 / D-3)";
    }
    // If result has a value: that's fine (success).
}

}  // namespace fixpp::session::test
