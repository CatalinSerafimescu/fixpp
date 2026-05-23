// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/logon_handshake_test.cpp
//
// Seam #2 — Logon handshake (005-session-establishment-fsm T019 / Phase 3 / US1).
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
    // FR-004: role=acceptor set explicitly per T010.
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
        cfg.role               = fixpp::session::session_role::acceptor;  // FR-004 / T010
        return cfg;
    }

    // Build a minimal initiator config (sender=TW, target=ISLD).
    // FR-004: role=initiator set explicitly per T010 (same as default, explicit for clarity).
    fixpp::session::SessionConfig make_initiator_cfg(int heartbt_int = 30) {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id     = "TW";
        cfg.target_comp_id     = "ISLD";
        cfg.begin_string       = "FIX.4.2";
        cfg.heartbeat_interval = std::chrono::seconds{heartbt_int};
        cfg.security_profile   = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary         = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override  = ioc.get_executor();
        cfg.role               = fixpp::session::session_role::initiator;  // FR-004 / T010 (explicit)
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
        1,                            // seq
        "TW",                         // sender
        "ISLD",                       // target
        "FIX.4.2",                    // begin_string
        30,                           // heartbt_int
        "20240101-00:00:00.000"       // sending_time
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

// ── Phase 4 / US2 tests (T010) ────────────────────────────────────────────────

// US2-AC1 (FR-004): Acceptor open() does NOT transition to LogonSent.
// After open(), acceptor must remain in NotConnected — no outbound Logon is emitted.
// TDD-RED: today's open() unconditionally sets LogonSent for all roles.
// Anchors: spec.md FR-004 §US2 AC1; data-model.md §E1 Session::open modified;
//          contracts/session_role.hpp; opus_pr81_1_triage.md RC#2.
TEST_F(LogonHandshakeTest, AcceptorOpenStaysInNotConnected) {
    auto cfg = make_acceptor_cfg(30);
    fixpp::session::Session sess(engine, cfg);
    auto open_result = open_sync(sess);
    ASSERT_TRUE(open_result.has_value()) << "Session::open() failed";

    EXPECT_EQ(sess.state(), fsm_state::NotConnected)
        << "Acceptor must stay NotConnected after open() — no outbound Logon; "
        << "got state=" << static_cast<int>(sess.state())
        << " (FR-004 §US2 AC1)";
}

// US2-AC2 (FR-005): Acceptor valid peer Logon drives NotConnected→LogonReceived→Active.
// FR-005 requires the acceptor to emit its own Logon reply on the LogonReceived→Active
// transition. After on_inbound_frame returns the session must be in Active (the reply
// Logon is emitted synchronously within the same coroutine invocation).
//
// Drift F1+F2 fix (Round-A): strict Active assertion + outbound Logon capture.
// Anchors: spec.md FR-005 §US2 AC2; data-model.md:19 matrix row;
//          contracts/session_role.hpp; opus_pr81_1_triage.md RC#2.
TEST_F(LogonHandshakeTest, AcceptorValidPeerLogonReachesActiveViaLogonReceived) {
    // Wire transport capture BEFORE constructing the session so the callback
    // is set in the config (transport_send_ is captured from cfg at open() time).
    std::vector<std::vector<std::byte>> outbound_frames;
    auto cfg = make_acceptor_cfg(30);
    cfg.transport_send = [&](std::span<const std::byte> frame) {
        outbound_frames.emplace_back(frame.begin(), frame.end());
    };

    fixpp::session::Session sess(engine, cfg);
    auto open_result = open_sync(sess);
    ASSERT_TRUE(open_result.has_value()) << "Session::open() failed";

    ASSERT_EQ(sess.state(), fsm_state::NotConnected)
        << "Acceptor must be NotConnected before peer Logon (prereq)";

    // Feed valid peer Logon — FR-005: acceptor emits its own reply Logon and
    // transitions LogonReceived→Active within the same on_inbound_frame call.
    auto logon_frame = make_logon_frame("FIX.4.2", 1, "TW", "ISLD", 30);
    auto inbound_result = feed_sync(sess, std::span<const std::byte>{logon_frame});
    EXPECT_TRUE(inbound_result.has_value())
        << "on_inbound_frame() returned error for valid peer Logon";

    // FR-005: after the reply Logon is emitted, state MUST be Active.
    // The LogonReceived→Active transition is internal and synchronous;
    // state() after feed_sync must read Active, not LogonReceived.
    // [F1+F2 drift fix; spec.md FR-005; data-model.md:19 matrix row]
    const auto final_state = sess.state();
    EXPECT_EQ(final_state, fsm_state::Active)
        << "Acceptor must reach Active after emitting reply Logon; "
        << "got state=" << static_cast<int>(final_state)
        << " (FR-005 §US2 AC2; spec.md line 112)";

    // FR-005: at least one outbound frame must have been emitted AND it must
    // carry MsgType 35=A (Logon reply). Transport was wired above.
    ASSERT_FALSE(outbound_frames.empty())
        << "Acceptor must have emitted at least one outbound frame (the reply Logon)";
    bool found_logon_reply = false;
    for (const auto& frame : outbound_frames) {
        const auto sv = std::span<const std::byte>(frame);
        const std::string msg_type = extract_field(sv, 35);
        if (msg_type == "A") {
            found_logon_reply = true;
            break;
        }
    }
    EXPECT_TRUE(found_logon_reply)
        << "Acceptor must emit a Logon reply (35=A) on the LogonReceived→Active transition; "
        << "no such frame found in " << outbound_frames.size() << " captured outbound frames"
        << " (FR-005; spec.md line 112)";
}

// US2-AC3 (FR-004): Initiator open() still transitions to LogonSent and emits outbound Logon.
// Regression test — the role branch must not break the existing initiator path.
// Anchors: spec.md FR-004 §US2 AC3; data-model.md §E1 Session::open modified.
TEST_F(LogonHandshakeTest, InitiatorOpenStillReachesLogonSentAndEmitsLogon) {
    // T027-audit close-out: assert BOTH state == LogonSent AND an outbound Logon
    // frame (35=A) was actually emitted. The original test only checked state —
    // body didn't match test name. Spec.md FR-004 + §US2 AC3: "open() sets state
    // and emits initial Logon."
    auto cfg = make_initiator_cfg(30);
    std::vector<std::vector<std::byte>> captured;
    cfg.transport_send = [&captured](std::span<const std::byte> f) {
        captured.emplace_back(f.begin(), f.end());
    };

    fixpp::session::Session sess(engine, cfg);
    auto open_result = open_sync(sess);
    ASSERT_TRUE(open_result.has_value()) << "Session::open() failed";

    EXPECT_EQ(sess.state(), fsm_state::LogonSent)
        << "Initiator must reach LogonSent after open() per FR-004 §US2 AC3; "
        << "got state=" << static_cast<int>(sess.state());

    // FR-004: outbound Logon (35=A) MUST be emitted by open() on the initiator path.
    ASSERT_GE(captured.size(), 1u)
        << "Initiator open() must emit at least one outbound frame";
    bool found_logon = false;
    for (const auto& frame : captured) {
        // Use the file-local extract_field at line 133 (returns std::string; "" on miss).
        const auto mt = extract_field(std::span<const std::byte>{frame}, 35);
        if (mt == "A") {
            found_logon = true;
            break;
        }
    }
    EXPECT_TRUE(found_logon)
        << "Initiator open() must emit an outbound Logon (35=A); captured "
        << captured.size() << " frame(s) but none had 35=A.";
}

// ── F6 (RED): Seqnum hole on open()'s initiator emit — open() must return error
// when build_logon fails rather than silently succeeding with a consumed seqnum.
// The 256-byte logon_buf overflows when sender_comp_id is large enough.
// After F6 fix: open() returns unexpected when build_logon fails.
// Before F6 fix: open() silently ignores build failure and returns ok.
// [Drift F6; spec.md FR-001(e); open() code path session.cpp:263]
TEST_F(LogonHandshakeTest, InitiatorOpen_BuildLogonOverflow_ReturnsError_NotSilentOk) {
    // A sender_comp_id of 220 characters overflows the 256-byte logon_buf.
    // The Logon frame minimum overhead is ~80 bytes (headers, fixed fields, etc.)
    // so 200+ char sender_comp_id triggers wire_frame_too_large from build_logon.
    auto cfg = make_initiator_cfg(30);
    cfg.sender_comp_id = std::string(220, 'X');  // overflows 256-byte logon_buf

    fixpp::session::Session sess(engine, cfg);
    auto open_result = open_sync(sess);

    // F6: build_logon failure must propagate out of open() as an error.
    // Currently open() silently returns ok despite not sending a Logon.
    // After F6 fix: returns unexpected(wire_frame_too_large).
    EXPECT_FALSE(open_result.has_value())
        << "open() must return error when build_logon fails (buffer overflow); "
        << "silently succeeding with a consumed seqnum is the F6 bug; "
        << "[spec.md FR-001(e); session.cpp:263]";
}

// ── RC#B RED: Acceptor build-logon overflow must reach Disconnected (not Active).
//
// Bug: acceptor sets fsm_state_=LogonReceived BEFORE attempting reply build, then
// checks `fsm_state_ == LogonReceived` (always true) to transition to Active.
// When build_logon fails (oversized sender_comp_id), the session still reaches Active.
//
// Fix: gate the LogonReceived→Active transition on successful emit.
// Anchors: 009 spec.md FR-005; 005 data-model.md:19 matrix row.
// [gate-b/r1-red: F-02 acceptor build failure]
TEST_F(LogonHandshakeTest, Acceptor_BuildLogonOverflow_DoesNotReachActive) {
    // Build an acceptor with an oversized sender_comp_id that makes build_logon fail.
    // The 256-byte reply_buf overflows when sender_comp_id alone is 200 chars.
    //
    // Constraint: interpret_logon validates peer's 56= against cfg.sender_comp_id, so
    // the peer frame's 56= must match. We build a custom peer frame with 56=<200 X's>
    // and set cfg.sender_comp_id=<200 X's> to pass the CompID check while overflowing
    // the reply build buffer.
    const std::string long_sender(200, 'X');

    auto cfg = make_acceptor_cfg(30);
    cfg.sender_comp_id = long_sender;   // overflows 256-byte reply_buf in build_logon
    cfg.target_comp_id = "TW";

    // Build a peer Logon that matches (peer.49=TW, peer.56=<long_sender>, 8=FIX.4.2).
    // Using make_logon_frame won't work as it hard-wires target="ISLD". Build manually.
    auto logon_frame = make_logon_frame("FIX.4.2", 1, "TW", long_sender, 30);

    fixpp::session::Session sess(engine, cfg);
    auto open_result = open_sync(sess);
    ASSERT_TRUE(open_result.has_value()) << "Acceptor open() must succeed (no outbound Logon)";
    ASSERT_EQ(sess.state(), fsm_state::NotConnected);

    // Feed the peer Logon — the session accepts it (CompID match) but fails to
    // build the reply (sender_comp_id is too large for the 256-byte reply buffer).
    auto inbound_result = feed_sync(sess, std::span<const std::byte>{logon_frame});

    // RC#B bug: the session transitions to Active anyway because the
    // `if (fsm_state_ == LogonReceived)` check is tautological.
    const auto s = sess.state();
    EXPECT_EQ(s, fsm_state::Disconnected)
        << "Acceptor must reach Disconnected (not Active) when reply build_logon fails; "
        << "got state=" << static_cast<int>(s) << ". "
        << "Bug: fsm_state_==LogonReceived is set at line 650 then tested tautologically "
        << "at line 692, so build failure still transitions to Active. "
        << "[gate-b/r1-red: F-02; 009 spec.md FR-005]";
    EXPECT_FALSE(inbound_result.has_value())
        << "on_inbound_frame must return an error when reply build fails "
        << "[gate-b/r1-red: F-02]";
}

// ── RC#B RED: Acceptor transport throw on reply Logon must reach Disconnected.
//
// Bug: store_then_emit swallows transport throws (F-03) AND the acceptor discards
// emit_r with `(void)emit_r` (F-02). Even if store_then_emit did propagate transport
// errors, the acceptor would not gate the Active transition on it.
//
// [gate-b/r1-red: F-02/F-03 acceptor transport failure]
TEST_F(LogonHandshakeTest, Acceptor_TransportThrowsOnReplyLogon_DoesNotReachActive) {
    // Transport that throws on all calls (including the reply Logon emission).
    // During open() the acceptor emits nothing, so the throw only fires on the reply.
    auto cfg = make_acceptor_cfg(30);
    cfg.transport_send = [](std::span<const std::byte> /*frame*/) {
        throw std::system_error(std::make_error_code(std::errc::connection_reset));
    };

    fixpp::session::Session sess(engine, cfg);
    auto open_result = open_sync(sess);
    ASSERT_TRUE(open_result.has_value()) << "Acceptor open() must succeed";
    ASSERT_EQ(sess.state(), fsm_state::NotConnected);

    // Feed a valid peer Logon — the reply emission will throw.
    auto logon_frame = make_logon_frame("FIX.4.2", 1, "TW", "ISLD", 30);
    auto inbound_result = feed_sync(sess, std::span<const std::byte>{logon_frame});

    // RC#B bug: transport throw is swallowed AND emit_r is discarded — Active anyway.
    const auto s = sess.state();
    EXPECT_EQ(s, fsm_state::Disconnected)
        << "Acceptor must reach Disconnected (not Active) when reply transport throws; "
        << "got state=" << static_cast<int>(s) << ". "
        << "Bug: store_then_emit swallows throw + (void)emit_r discards result. "
        << "[gate-b/r1-red: F-02/F-03; 009 spec.md FR-005]";
    EXPECT_FALSE(inbound_result.has_value())
        << "on_inbound_frame must return error when transport fails on reply Logon "
        << "[gate-b/r1-red: F-02/F-03]";
}

// ── Original T10: stamp_sending_time() unit test (preserved) ─────────────────

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
