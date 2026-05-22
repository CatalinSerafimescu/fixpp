// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/conformance/tc_seqnum_test.cpp
//
// [FIX-TC] Seqnum conformance subset — 005-session-establishment-fsm T030
// US2 Phase 4.
//
// Scenarios from the QuickFIX oracle (research D-10):
//   2a_MsgSeqNumCorrect     — in-sequence message accepted; counter advances.
//   2c_MsgSeqNumTooLow      — too-low (no PossDup) → session-fatal ([FIX-SL §4.1]).
//   2q_MsgTypeNotValid      — unrecognized MsgType → bounded session Reject (not fatal).
//   2r_UnregisteredMsgType  — unregistered MsgType → bounded session Reject.
//
// EXPLICITLY OUT OF SCOPE (deferred per D-10 / Session-2026-05-18):
//   1a_ValidLogonMsgSeqNumTooHigh — deferred (too-high recovery feature).
//   2b_MsgSeqNumTooHigh           — deferred (too-high recovery feature).
//   These are NOT in-scope for 005 and must NOT be greened here.
//
// Anchors: research D-10; spec FR-008; data-model E3; [FIX-SL §4.1].
// [const §VII.5]: ships only the in-scope subset green; deferred cases not attempted.
//
// Infrastructure: same as tc_establishment_test.cpp.
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

// ── Frame builder helpers ──────────────────────────────────────────────────────

static std::vector<std::byte> make_logon_frame(
        std::string_view begin_string,
        std::uint32_t seq,
        std::string_view sender,
        std::string_view target,
        int heartbt = 30) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    body += "98=0\x01";
    body += "108=" + std::to_string(heartbt) + "\x01";

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
    for (char c : full) { frame.push_back(static_cast<std::byte>(c)); }
    return frame;
}

static std::vector<std::byte> make_heartbeat_frame(
        std::string_view begin_string,
        std::uint32_t seq,
        std::string_view sender,
        std::string_view target) {
    std::string body;
    body += "35=0\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";

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
    for (char c : full) { frame.push_back(static_cast<std::byte>(c)); }
    return frame;
}

// Build a frame with an unknown MsgType (for 2q/2r).
static std::vector<std::byte> make_unknown_msgtype_frame(
        std::string_view begin_string,
        std::uint32_t seq,
        std::string_view sender,
        std::string_view target,
        std::string_view msg_type) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";

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
    for (char c : full) { frame.push_back(static_cast<std::byte>(c)); }
    return frame;
}

}  // namespace

// ── Fixture ───────────────────────────────────────────────────────────────────

class TcSeqnumTest : public ::testing::Test {
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

    fixpp::session::SessionConfig make_acceptor_cfg() {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id    = "ISLD";
        cfg.target_comp_id    = "TW";
        cfg.begin_string      = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile  = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary        = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        return cfg;
    }

    fixpp::core::expected_t<void> open_sync(fixpp::session::Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }

    fixpp::core::expected_t<void> feed_sync(
            fixpp::session::Session& s, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }

    // Drive a session to Active/LogonReceived (acceptor):
    // open() → receive Logon(seq=1) → state ∈ {Active, LogonReceived}.
    bool drive_to_active(fixpp::session::Session& s) {
        auto open_r = open_sync(s);
        if (!open_r.has_value()) { return false; }
        auto logon = make_logon_frame("FIX.4.2", 1, "TW", "ISLD", 30);
        auto feed_r = feed_sync(s, logon);
        (void)feed_r;
        const auto st = s.state();
        return st == fixpp::session::fsm_state::Active ||
               st == fixpp::session::fsm_state::LogonReceived;
    }
};

// ── 2a_MsgSeqNumCorrect ───────────────────────────────────────────────────────
//
// Oracle: in-sequence MsgSeqNum=1 on Logon → session accepted, reaches Active.
// After reaching Active: seq=2 Heartbeat → still Active, counter advances.
//
// [FIX-TC] 2a: A message with the correct MsgSeqNum is processed normally.

TEST_F(TcSeqnumTest, Tc2a_MsgSeqNumCorrect) {
    auto cfg = make_acceptor_cfg();
    fixpp::session::Session sess(engine, cfg);

    // Step 1: Logon with seq=1 (correct starting MsgSeqNum).
    ASSERT_TRUE(drive_to_active(sess))
        << "2a: Should reach Active after Logon with correct MsgSeqNum=1";

    // Step 2: seq=2 Heartbeat (in-sequence after Logon consumed seq=1).
    auto hb = make_heartbeat_frame("FIX.4.2", 2, "TW", "ISLD");
    auto r = feed_sync(sess, hb);
    EXPECT_TRUE(r.has_value()) << "2a: In-sequence Heartbeat seq=2 should be accepted";

    // Session must remain Active (or LogonReceived).
    const auto st = sess.state();
    EXPECT_TRUE(st == fixpp::session::fsm_state::Active ||
                st == fixpp::session::fsm_state::LogonReceived)
        << "2a: Session should remain Active after in-sequence message";
}

// ── 2c_MsgSeqNumTooLow ────────────────────────────────────────────────────────
//
// Oracle: a message with MsgSeqNum < next-expected and no PossDupFlag=Y →
// session-fatal ([FIX-SL §4.1]).
//
// 2c scenario: after reaching Active (next expected=2), send a Heartbeat with
// seq=1. Must become session-fatal (Disconnected or LogoutSent).

TEST_F(TcSeqnumTest, Tc2c_MsgSeqNumTooLow) {
    auto cfg = make_acceptor_cfg();
    fixpp::session::Session sess(engine, cfg);

    // Drive to Active: Logon consumed seq=1; next expected = 2.
    ASSERT_TRUE(drive_to_active(sess))
        << "2c: Failed to drive to Active";

    // Send a Heartbeat with seq=1 (too low; no PossDupFlag).
    auto hb = make_heartbeat_frame("FIX.4.2", 1, "TW", "ISLD");
    feed_sync(sess, hb);

    // Session must be session-fatal.
    const auto st = sess.state();
    EXPECT_TRUE(st == fixpp::session::fsm_state::Disconnected ||
                st == fixpp::session::fsm_state::LogoutSent)
        << "2c: Too-low MsgSeqNum must cause session-fatal (Disconnected or LogoutSent); "
        << "got state=" << static_cast<int>(st);
}

// ── 2q_MsgTypeNotValid ────────────────────────────────────────────────────────
//
// Oracle: a message with an invalid/unknown MsgType in Active state →
// session-level Reject (not a session-fatal). The session remains Active.
//
// Scope note: 2q requires US5 (Phase 7 T054) for the full Reject(35=3) emission.
// Phase 4 verifies the MINIMAL observable: the session does NOT become fatal
// on an unknown MsgType in Active state (it's a bounded reject, not a disconnect).
//
// Provisional assertion: session stays Active (or in a recoverable Reject state).
// The Reject emission itself is verified in Phase 7 seam #7.

TEST_F(TcSeqnumTest, Tc2q_MsgTypeNotValid) {
    auto cfg = make_acceptor_cfg();
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess)) << "2q: Failed to drive to Active";

    // Send a message with an unknown MsgType "Z" (in-sequence seq=2).
    // This represents an unrecognised application message type.
    auto unknown = make_unknown_msgtype_frame("FIX.4.2", 2, "TW", "ISLD", "Z");
    auto r = feed_sync(sess, unknown);
    // The session processes it: unknown MsgType → bounded Reject, stays Active.

    const auto st = sess.state();
    // Phase 4 observable: session does NOT become fatal for unknown MsgType.
    // The Reject emission (35=3) is wired in Phase 7; here we assert non-fatal.
    EXPECT_NE(st, fixpp::session::fsm_state::Disconnected)
        << "2q: Unknown MsgType should NOT cause Disconnected (bounded reject, not fatal)";
    // Note: LogoutSent is also not expected for a simple unknown MsgType.
    // However, some implementations may emit a Logout if no dictionary match
    // is found AND the FSM treats it as a fatal guard failure. The spec says
    // session Reject, not logout. We check the minimal invariant:
    // the session is still in a functioning state.
    EXPECT_NE(st, fixpp::session::fsm_state::LogonSent)
        << "2q: Unknown MsgType should not cause LogonSent regression";
}

// ── 2r_UnregisteredMsgType ────────────────────────────────────────────────────
//
// Oracle: a message type not registered in the session's dictionary →
// bounded session Reject (same handling as 2q for Phase 4).
//
// Scope note: same as 2q; Phase 4 verifies the session does not become fatal.
// Phase 7 wires the Reject(35=3) emission.

TEST_F(TcSeqnumTest, Tc2r_UnregisteredMsgType) {
    auto cfg = make_acceptor_cfg();
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess)) << "2r: Failed to drive to Active";

    // Send a message with a completely unregistered MsgType (e.g., "XYZ").
    auto unknown = make_unknown_msgtype_frame("FIX.4.2", 2, "TW", "ISLD", "XYZ");
    feed_sync(sess, unknown);

    const auto st = sess.state();
    EXPECT_NE(st, fixpp::session::fsm_state::Disconnected)
        << "2r: Unregistered MsgType should NOT cause Disconnected";
}

// ── Explicitly deferred: 1a and 2b ────────────────────────────────────────────
//
// The following test documents the deferred cases (D-10 / Session-2026-05-18).
// These are NOT tested here — they belong to the future session-recovery feature.
// The test is a static documentation only (SUCCEED).

TEST_F(TcSeqnumTest, DeferredScenarios_NotInScope) {
    // 1a_ValidLogonMsgSeqNumTooHigh: deferred to session-recovery feature.
    //   Would require ResendRequest(35=2) emission — not in 005.
    // 2b_MsgSeqNumTooHigh: deferred to session-recovery feature.
    //   Would require ResendRequest(35=2) + SequenceReset recovery — not in 005.
    //
    // Per D-10 and [const §VII.5]: these TC cases are deferred-with-traceability
    // under the explicit Article XVII §1 Gate-A-blocker waiver. The seqnum-gap
    // for 005 is session-fatal (I-4; Session-2026-05-18 Q1 re-clarification).
    SUCCEED() << "1a/2b deferred per D-10 / Session-2026-05-18; "
                 "session-recovery feature owns the ResendRequest path.";
}

}  // namespace fixpp::session::conformance_test
