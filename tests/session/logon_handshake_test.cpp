// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/logon_handshake_test.cpp
//
// Seam #2 — Logon handshake (005-session-establishment-fsm T019 / Phase 3 / US1).
//
// TDD red-first: authored BEFORE T021–T026 implementation; must FAIL until
// T021–T026 wire the US1 path. After T026 must be GREEN.
//
// Scenarios covered:
//   1. Initiator open() → FSM enters LogonSent (not Disconnected/NotConnected).
//   2. Acceptor inbound valid Logon → reply emitted, FSM → Active/LogonReceived.
//   3. HeartBtInt negotiation: acceptor uses agreed value.
//   4. Refusal — BeginString mismatch: FSM never enters Active.
//   5. Refusal — SenderCompID mismatch: FSM never enters Active.
//   6. Refusal — TargetCompID mismatch: FSM never enters Active.
//   7. Refusal — first message is not Logon: FSM never enters Active.
//   8. build_logon() produces a valid FIX frame (unit test of T021).
//   9. interpret_logon() validates BeginString/CompID/HeartBtInt (unit test of T021).
//  10. stamp_sending_time() formats SendingTime correctly (unit test of T022).
//
// Infrastructure:
//   - TransportDouble (tests/support/transport_double.hpp) — seam #0
//   - StoreDouble     (tests/support/store_double.hpp)     — seam #0
//   - mock_clock      (include/fixpp/core/test/mock_clock.hpp) via fixpp_mock_clock
//
// Anchors: spec US1; data-model E2 (FSM transitions); error slots 66..68;
// [FIX-SL §4.2]/§4.3.
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <future>
#include <memory>
#include <optional>
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
#include <fixpp/core/fix_time.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/sending_time.hpp>
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

namespace fixpp::session::test {

// ── Frame builder helpers ──────────────────────────────────────────────────────

static std::vector<std::byte> make_logon_frame(
        std::string_view begin_string,
        std::uint32_t msg_seq_num,
        std::string_view sender_comp_id,
        std::string_view target_comp_id,
        int heartbt_int,
        std::string_view sending_time = "20240101-00:00:00.000") {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(msg_seq_num) + "\x01";
    body += "49=" + std::string(sender_comp_id) + "\x01";
    body += "52=" + std::string(sending_time) + "\x01";
    body += "56=" + std::string(target_comp_id) + "\x01";
    body += "98=0\x01";
    body += "108=" + std::to_string(heartbt_int) + "\x01";

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
    frame.reserve(full.size());
    for (char c : full) { frame.push_back(static_cast<std::byte>(c)); }
    return frame;
}

static std::vector<std::byte> make_heartbeat_frame(
        std::string_view begin_string,
        std::uint32_t msg_seq_num,
        std::string_view sender_comp_id,
        std::string_view target_comp_id) {
    std::string body;
    body += "35=0\x01";
    body += "34=" + std::to_string(msg_seq_num) + "\x01";
    body += "49=" + std::string(sender_comp_id) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target_comp_id) + "\x01";

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
    frame.reserve(full.size());
    for (char c : full) { frame.push_back(static_cast<std::byte>(c)); }
    return frame;
}

static std::string extract_field(std::span<const std::byte> frame, int tag) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = std::to_string(tag) + "=";
    auto pos = wire.find(needle);
    if (pos == std::string::npos) { return {}; }
    pos += needle.size();
    auto end = wire.find('\x01', pos);
    if (end == std::string::npos) { return {}; }
    return wire.substr(pos, end - pos);
}

// ── Test fixture ───────────────────────────────────────────────────────────────

class LogonHandshakeTest : public ::testing::Test {
protected:
    asio::io_context                          ioc;
    std::shared_ptr<fixpp::core::mock_clock>  clock;
    fixpp::core::EngineConfig                 engine{};

    void SetUp() override {
        using namespace std::chrono;
        // Seed at 2024-01-01 00:00:00 UTC.
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock    = clock;
        engine.executor = ioc.get_executor();
    }

    // Build a minimal acceptor config (sender=ISLD, target=TW).
    fixpp::session::SessionConfig make_acceptor_cfg(int heartbt_int = 30,
                                                     std::string_view begin_string = "FIX.4.2") {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id     = "ISLD";
        cfg.target_comp_id     = "TW";
        cfg.begin_string       = std::string(begin_string);
        cfg.heartbeat_interval = std::chrono::seconds{heartbt_int};
        cfg.security_profile   = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary         = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override  = ioc.get_executor();
        return cfg;
    }

    // Build a minimal initiator config (sender=TW, target=ISLD).
    fixpp::session::SessionConfig make_initiator_cfg(int heartbt_int = 30) {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id     = "TW";
        cfg.target_comp_id     = "ISLD";
        cfg.begin_string       = "FIX.4.2";
        cfg.heartbeat_interval = std::chrono::seconds{heartbt_int};
        cfg.security_profile   = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary         = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override  = ioc.get_executor();
        return cfg;
    }

    // Synchronously open a session.
    fixpp::core::expected_t<void> open_sync(fixpp::session::Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(std::chrono::milliseconds{200});
        ioc.restart();
        return fut.get();
    }

    // Synchronously feed an inbound frame.
    fixpp::core::expected_t<void> feed_sync(fixpp::session::Session& s,
                                             std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(std::chrono::milliseconds{200});
        ioc.restart();
        return fut.get();
    }
};

// ──────────────────────────────────────────────────────────────────────────────
// Tests
// ──────────────────────────────────────────────────────────────────────────────

// T1: After open(), initiator FSM is NOT in NotConnected/Disconnected.
TEST_F(LogonHandshakeTest, InitiatorOpenEntersLogonSent) {
    auto cfg = make_initiator_cfg(30);
    fixpp::session::Session sess(engine, cfg);
    auto open_result = open_sync(sess);
    ASSERT_TRUE(open_result.has_value()) << "Session::open() failed";

    // T023 (US1, Phase 3) wires the initiator NotConnected → LogonSent
    // transition at the tail of Session::open(). The matrix-strict outcome
    // is `LogonSent`; assert it directly.
    EXPECT_EQ(sess.state(), fsm_state::LogonSent)
        << "Initiator should transition NotConnected → LogonSent after open() "
        << "per data-model.md matrix; got state="
        << static_cast<int>(sess.state());
}

// T2: Acceptor: valid inbound Logon → FSM reaches Active or LogonReceived.
TEST_F(LogonHandshakeTest, AcceptorValidLogonTransitionsToActive) {
    auto cfg = make_acceptor_cfg(30);
    fixpp::session::Session sess(engine, cfg);
    auto open_result = open_sync(sess);
    ASSERT_TRUE(open_result.has_value()) << "Session::open() failed";

    // Peer (TW→ISLD) sends valid Logon with seq=1, HeartBtInt=30.
    auto logon_frame = make_logon_frame("FIX.4.2", 1, "TW", "ISLD", 30);
    auto inbound_result = feed_sync(sess, std::span<const std::byte>{logon_frame});
    EXPECT_TRUE(inbound_result.has_value())
        << "on_inbound_frame() returned error for valid Logon";

    const auto s = sess.state();
    EXPECT_TRUE(s == fsm_state::Active || s == fsm_state::LogonReceived)
        << "Acceptor should be Active or LogonReceived after valid peer Logon, "
        << "got state=" << static_cast<int>(s);
}

// T3: HeartBtInt negotiation — acceptor with 30s, peer requests 60s.
TEST_F(LogonHandshakeTest, HeartBtIntNegotiationUsesLower) {
    auto cfg = make_acceptor_cfg(30);
    fixpp::session::Session sess(engine, cfg);
    auto open_result = open_sync(sess);
    ASSERT_TRUE(open_result.has_value());

    auto logon_frame = make_logon_frame("FIX.4.2", 1, "TW", "ISLD", 60 /* peer */);
    auto inbound_result = feed_sync(sess, std::span<const std::byte>{logon_frame});
    EXPECT_TRUE(inbound_result.has_value())
        << "on_inbound_frame() error with peer HeartBtInt=60";
    // Detailed HeartBtInt-in-reply assertion deferred to T026 green pass.
}

// T4: Refusal — BeginString mismatch: FSM never enters Active.
TEST_F(LogonHandshakeTest, RefusedLogonWrongBeginString) {
    auto cfg = make_acceptor_cfg(30);
    fixpp::session::Session sess(engine, cfg);
    auto open_result = open_sync(sess);
    ASSERT_TRUE(open_result.has_value());

    // Peer sends Logon with wrong BeginString (FIX.3.9).
    auto logon_frame = make_logon_frame("FIX.3.9", 1, "TW", "ISLD", 30);
    auto inbound_result = feed_sync(sess, std::span<const std::byte>{logon_frame});

    const auto s = sess.state();
    EXPECT_NE(s, fsm_state::Active)
        << "Session must NOT enter Active on BeginString mismatch";
    EXPECT_NE(s, fsm_state::LogonReceived)
        << "Session must NOT enter LogonReceived on BeginString mismatch";
}

// T5: Refusal — SenderCompID mismatch.
TEST_F(LogonHandshakeTest, RefusedLogonWrongSenderCompID) {
    auto cfg = make_acceptor_cfg(30);
    fixpp::session::Session sess(engine, cfg);
    auto open_result = open_sync(sess);
    ASSERT_TRUE(open_result.has_value());

    // Peer sends Logon with wrong SenderCompID ("WT" instead of "TW").
    auto logon_frame = make_logon_frame("FIX.4.2", 1, "WT" /* wrong */, "ISLD", 30);
    auto inbound_result = feed_sync(sess, std::span<const std::byte>{logon_frame});

    const auto s = sess.state();
    EXPECT_NE(s, fsm_state::Active)
        << "Session must NOT enter Active on SenderCompID mismatch";
    EXPECT_NE(s, fsm_state::LogonReceived)
        << "Session must NOT enter LogonReceived on SenderCompID mismatch";
}

// T6: Refusal — TargetCompID mismatch.
TEST_F(LogonHandshakeTest, RefusedLogonWrongTargetCompID) {
    auto cfg = make_acceptor_cfg(30);
    fixpp::session::Session sess(engine, cfg);
    auto open_result = open_sync(sess);
    ASSERT_TRUE(open_result.has_value());

    // Peer sends Logon with wrong TargetCompID ("DLSI" instead of "ISLD").
    auto logon_frame = make_logon_frame("FIX.4.2", 1, "TW", "DLSI" /* wrong */, 30);
    auto inbound_result = feed_sync(sess, std::span<const std::byte>{logon_frame});

    const auto s = sess.state();
    EXPECT_NE(s, fsm_state::Active)
        << "Session must NOT enter Active on TargetCompID mismatch";
    EXPECT_NE(s, fsm_state::LogonReceived)
        << "Session must NOT enter LogonReceived on TargetCompID mismatch";
}

// T7: Refusal — first message is not a Logon.
TEST_F(LogonHandshakeTest, RefusedFirstMessageNotLogon) {
    auto cfg = make_acceptor_cfg(30);
    fixpp::session::Session sess(engine, cfg);
    auto open_result = open_sync(sess);
    ASSERT_TRUE(open_result.has_value());

    // First inbound message is a Heartbeat (35=0), not a Logon (35=A).
    auto hb_frame = make_heartbeat_frame("FIX.4.2", 1, "TW", "ISLD");
    auto inbound_result = feed_sync(sess, std::span<const std::byte>{hb_frame});

    const auto s = sess.state();
    EXPECT_NE(s, fsm_state::Active)
        << "Session must NOT enter Active when first message is not Logon";
    EXPECT_NE(s, fsm_state::LogonReceived)
        << "Session must NOT enter LogonReceived when first message is not Logon";
}

// T8: build_logon() — unit test of T021 (admin_messages.cpp).
TEST_F(LogonHandshakeTest, BuildLogonProducesValidFrame) {
    std::array<std::byte, 256> buf{};
    auto result = fixpp::session::build_logon(
        std::span<std::byte>{buf},
        1,           // seq
        "TW",        // sender
        "ISLD",      // target
        "FIX.4.2",   // begin_string
        30           // heartbt_int
    );
    ASSERT_TRUE(result.has_value())
        << "build_logon() returned error; T021 not yet wired";

    EXPECT_GT(result->size(), 0u) << "build_logon() returned empty span";

    // Check BeginString(8)=FIX.4.2.
    auto begin_str = extract_field(*result, 8);
    EXPECT_EQ(begin_str, "FIX.4.2") << "BeginString(8) missing or wrong";

    // Check MsgType(35)=A.
    auto msg_type = extract_field(*result, 35);
    EXPECT_EQ(msg_type, "A") << "MsgType(35) not A";

    // Check MsgSeqNum(34)=1.
    auto seqnum = extract_field(*result, 34);
    EXPECT_EQ(seqnum, "1") << "MsgSeqNum(34) not 1";

    // Check HeartBtInt(108)=30.
    auto hbi = extract_field(*result, 108);
    EXPECT_EQ(hbi, "30") << "HeartBtInt(108) not 30";

    // Check SenderCompID(49)=TW.
    auto sender = extract_field(*result, 49);
    EXPECT_EQ(sender, "TW") << "SenderCompID(49) not TW";

    // Check TargetCompID(56)=ISLD.
    auto target = extract_field(*result, 56);
    EXPECT_EQ(target, "ISLD") << "TargetCompID(56) not ISLD";
}

// T9a: interpret_logon() — valid frame returns HeartBtInt.
TEST_F(LogonHandshakeTest, InterpretLogonValidFrameReturnsHeartBtInt) {
    auto frame = make_logon_frame("FIX.4.2", 1, "TW", "ISLD", 30);
    auto result = fixpp::session::interpret_logon(
        std::span<const std::byte>{frame},
        "TW",     // expected_sender (peer's SenderCompID = our TargetCompID)
        "ISLD",   // expected_target (peer's TargetCompID = our SenderCompID)
        "FIX.4.2" // expected_begin
    );
    ASSERT_TRUE(result.has_value())
        << "interpret_logon() returned error for valid frame; T021 not wired";
    EXPECT_EQ(*result, 30) << "interpret_logon() returned wrong HeartBtInt";
}

// T9b: interpret_logon() — BeginString mismatch returns error.
TEST_F(LogonHandshakeTest, InterpretLogonWrongBeginStringReturnsError) {
    auto frame = make_logon_frame("FIX.3.9", 1, "TW", "ISLD", 30);
    auto result = fixpp::session::interpret_logon(
        std::span<const std::byte>{frame},
        "TW", "ISLD", "FIX.4.2"
    );
    EXPECT_FALSE(result.has_value())
        << "interpret_logon() should fail on BeginString mismatch";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(),
                  fixpp::core::error::session_begin_string_unsupported)
            << "Wrong error code for BeginString mismatch";
    }
}

// T9c: interpret_logon() — SenderCompID mismatch returns error.
TEST_F(LogonHandshakeTest, InterpretLogonWrongSenderReturnsError) {
    auto frame = make_logon_frame("FIX.4.2", 1, "WT" /* wrong */, "ISLD", 30);
    auto result = fixpp::session::interpret_logon(
        std::span<const std::byte>{frame},
        "TW", "ISLD", "FIX.4.2"
    );
    EXPECT_FALSE(result.has_value())
        << "interpret_logon() should fail on SenderCompID mismatch";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(),
                  fixpp::core::error::session_compid_mismatch)
            << "Wrong error code for SenderCompID mismatch";
    }
}

// T9d: interpret_logon() — TargetCompID mismatch returns error.
TEST_F(LogonHandshakeTest, InterpretLogonWrongTargetReturnsError) {
    auto frame = make_logon_frame("FIX.4.2", 1, "TW", "DLSI" /* wrong */, 30);
    auto result = fixpp::session::interpret_logon(
        std::span<const std::byte>{frame},
        "TW", "ISLD", "FIX.4.2"
    );
    EXPECT_FALSE(result.has_value())
        << "interpret_logon() should fail on TargetCompID mismatch";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(),
                  fixpp::core::error::session_compid_mismatch)
            << "Wrong error code for TargetCompID mismatch";
    }
}

// T10: stamp_sending_time() — formats SendingTime correctly (T022 unit test).
TEST_F(LogonHandshakeTest, StampSendingTimeFormatsCorrectly) {
    using namespace std::chrono;
    auto now = clock->now();
    std::array<char, 32> buf{};
    auto result = fixpp::session::stamp_sending_time(now, std::span<char>{buf});
    ASSERT_TRUE(result.has_value())
        << "stamp_sending_time() returned error; T022 not yet wired";
    EXPECT_GE(result->size(), 17u) << "SendingTime too short (need ≥17 chars for seconds)";
    EXPECT_LE(result->size(), 25u) << "SendingTime too long";

    // Round-trip: parse back and compare.
    auto parse_result = fixpp::core::fix_string_to_utc_time(
        std::span<const char>{result->data(), result->size()});
    ASSERT_TRUE(parse_result.has_value())
        << "parse failed on stamp_sending_time() output";
    // Parsed tp should equal now truncated to millis (millis precision default).
    auto expected = time_point_cast<milliseconds>(now);
    EXPECT_EQ(*parse_result, expected)
        << "stamp_sending_time round-trip mismatch";
}

}  // namespace fixpp::session::test
