// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/logon_received_observability_test.cpp
//
// 010-session-cfg-lifetime T012 / Phase 4 / US2.
//
// FR-004 acceptance scenario 1: fsm_visit_history() observability.
//   - AcceptorReplyLogonPath_VisitHistoryContainsLogonReceived: acceptor session
//     processes a valid peer Logon; afterwards fsm_visit_history() contains
//     fsm_state::LogonReceived somewhere in the ring.
//   - InitiatorOpen_VisitHistoryContainsLogonSent: initiator open() records
//     LogonSent in the ring.
//   - VisitHistoryEmptyOnFreshSession: a Session whose open() has not been
//     called returns an empty history span.
//
// Anchors: spec.md FR-004 / D-2; data-model.md §E1 ring-buffer invariant I-25.
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

#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace fixpp::session::test {

// ── Frame builder (mirrors logon_handshake_test.cpp helper) ──────────────────

static std::vector<std::byte> make_logon_frame_obs(
        std::string_view begin_string,
        std::uint32_t msg_seq_num,
        std::string_view sender_comp_id,
        std::string_view target_comp_id,
        int heartbt_int) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(msg_seq_num) + "\x01";
    body += "49=" + std::string(sender_comp_id) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target_comp_id) + "\x01";
    body += "98=0\x01";
    body += "108=" + std::to_string(heartbt_int) + "\x01";

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) { cs += c; }
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(full.size());
    for (char c : full) { frame.push_back(static_cast<std::byte>(c)); }
    return frame;
}

// ── Fixture ───────────────────────────────────────────────────────────────────

class LogonObservabilityTest : public ::testing::Test {
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

    // Acceptor config: sender=ISLD, target=TW, FIX.4.2.
    fixpp::session::SessionConfig make_acceptor_cfg() {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id     = "ISLD";
        cfg.target_comp_id     = "TW";
        cfg.begin_string       = "FIX.4.2";
        cfg.heartbeat_interval = 0s;  // disable liveness loop
        cfg.security_profile   = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary         = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override  = ioc.get_executor();
        cfg.role               = fixpp::session::session_role::acceptor;  // FR-004 / D-2
        return cfg;
    }

    // Initiator config: sender=TW, target=ISLD, FIX.4.2.
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

    fixpp::core::expected_t<void> open_sync(fixpp::session::Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }

    fixpp::core::expected_t<void> feed_sync(fixpp::session::Session& s,
                                            std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }
};

// ── Tests ─────────────────────────────────────────────────────────────────────

// FR-004 acceptance scenario 1: acceptor valid peer Logon → history contains
// LogonReceived (the intermediate state between NotConnected and Active).
// Anchors: spec.md FR-004; data-model.md §E1 I-25; D-2.
TEST_F(LogonObservabilityTest,
       AcceptorReplyLogonPath_VisitHistoryContainsLogonReceived) {
    auto cfg = make_acceptor_cfg();
    fixpp::session::Session sess(engine, cfg);

    auto open_r = open_sync(sess);
    ASSERT_TRUE(open_r.has_value()) << "open() failed";
    ASSERT_EQ(sess.state(), fsm_state::NotConnected)
        << "Acceptor must be NotConnected after open() (FR-004)";

    // Feed valid peer Logon (TW→ISLD, seq=1, HeartBtInt=30).
    auto logon_frame = make_logon_frame_obs("FIX.4.2", 1, "TW", "ISLD", 30);
    auto inbound_r = feed_sync(sess, std::span<const std::byte>{logon_frame});
    EXPECT_TRUE(inbound_r.has_value())
        << "on_inbound_frame() returned error for valid Logon";

    // After the awaitable completes, LogonReceived must appear in the ring.
    auto hist = sess.fsm_visit_history();
    EXPECT_THAT(hist, ::testing::Contains(fsm_state::LogonReceived))
        << "fsm_visit_history() must contain LogonReceived after acceptor "
        << "processes a valid peer Logon (FR-004 / D-2)";
}

// Initiator open() records LogonSent in the visit history.
// Anchors: spec.md FR-004; data-model.md §E1 I-25; D-2.
TEST_F(LogonObservabilityTest, InitiatorOpen_VisitHistoryContainsLogonSent) {
    auto cfg = make_initiator_cfg();
    fixpp::session::Session sess(engine, cfg);

    auto open_r = open_sync(sess);
    ASSERT_TRUE(open_r.has_value()) << "open() failed";

    EXPECT_EQ(sess.state(), fsm_state::LogonSent)
        << "Initiator must be in LogonSent after open()";

    auto hist = sess.fsm_visit_history();
    EXPECT_THAT(hist, ::testing::Contains(fsm_state::LogonSent))
        << "fsm_visit_history() must contain LogonSent after initiator open() "
        << "(FR-004 / D-2)";
}

// A session whose open() has not been called returns an empty history span.
// Anchors: data-model.md §E1 I-25 (ring starts empty, grows only via
// record_state_transition_); D-2.
TEST_F(LogonObservabilityTest, VisitHistoryEmptyOnFreshSession) {
    auto cfg = make_acceptor_cfg();
    fixpp::session::Session sess(engine, cfg);
    // Do NOT call open().

    auto hist = sess.fsm_visit_history();
    EXPECT_TRUE(hist.empty())
        << "fsm_visit_history() must be empty before any state transition "
        << "(no open() called); got " << hist.size() << " entries (D-2)";
}

}  // namespace fixpp::session::test
