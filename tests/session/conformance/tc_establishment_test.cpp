// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/conformance/tc_establishment_test.cpp
//
// [FIX-TC] Establishment subset — conformance scenarios (005 T020 / Phase 3 / US1).
//
// TDD red-first: authored BEFORE T021–T026. Must FAIL until those tasks wire
// the US1 path. After T026 must be GREEN.
//
// Scenarios from the QuickFIX oracle (research D-10, fix42/fix44):
//   1a_ValidLogonWithCorrectMsgSeqNum   — both sides reach Active (fix42/fix44)
//   1c_InvalidSenderCompID              — bad SenderCompID → disconnect (fix42)
//   1c_InvalidTargetCompID              — bad TargetCompID → disconnect (fix42)
//   1d_InvalidLogonWrongBeginString     — bad BeginString → disconnect (fix42)
//   2i_BeginStringValueUnexpected       — wrong BeginString post-logon → Logout (fix42)
//   2k_CompIDDoesNotMatchProfile        — post-active CompID mismatch → Reject+Logout (fix42)
//   1e_NotLogonMessage                  — first message not Logon → disconnect (fix42)
//
// Anchors: spec FR-002/003/004/011/016/018; data-model E2; research D-10;
// [FIX-SL §4.2]–§4.3.
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <future>
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
#include <fixpp/session/security_profile.hpp>
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
        int heartbt) {
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

// ── Conformance test harness ───────────────────────────────────────────────────

struct Harness {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};

    explicit Harness() {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock    = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_cfg(
            std::string sender, std::string target,
            std::string begin_string = "FIX.4.2",
            int heartbt = 30) {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id     = std::move(sender);
        cfg.target_comp_id     = std::move(target);
        cfg.begin_string       = std::move(begin_string);
        cfg.heartbeat_interval = std::chrono::seconds{heartbt};
        cfg.security_profile   = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary         = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override  = ioc.get_executor();
        return cfg;
    }

    fixpp::core::expected_t<void> open_session(fixpp::session::Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(std::chrono::milliseconds{200});
        ioc.restart();
        return fut.get();
    }

    fixpp::core::expected_t<void> feed_frame(fixpp::session::Session& s,
                                              std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(std::chrono::milliseconds{200});
        ioc.restart();
        return fut.get();
    }
};

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// 1a_ValidLogonWithCorrectMsgSeqNum (fix42)
// Oracle: acceptor receives valid Logon(seq=1, HBI=30) → replies → Active.
// T012 rewrite per FR-005 §US2 AC2: configure role=acceptor; open() stays in
// NotConnected; on_inbound_frame(peer_logon) drives NotConnected→LogonReceived.
// The test MUST observe LogonReceived as an intermediate state.
// Anchors: spec.md FR-005 §US2 AC2; data-model.md:19 matrix row;
//          opus_pr81_1_triage.md RC#2; contracts/session_role.hpp.
// ══════════════════════════════════════════════════════════════════════════════

TEST(TcEstablishment, Scenario1a_ValidLogon_fix42) {
    Harness h;
    auto cfg = h.make_cfg("ISLD", "TW", "FIX.4.2", 30);
    cfg.role = fixpp::session::session_role::acceptor;  // FR-005 / T012
    fixpp::session::Session sess(h.engine, cfg);

    // open() with role=acceptor: lifecycle transitions to open, FSM stays NotConnected.
    ASSERT_TRUE(h.open_session(sess).has_value()) << "open() failed";
    ASSERT_EQ(sess.state(), fsm_state::NotConnected)
        << "1a_fix42: acceptor must be NotConnected after open()";

    // Feed valid peer Logon — drives NotConnected → LogonReceived (data-model.md:19).
    auto frame = make_logon_frame("FIX.4.2", 1, "TW", "ISLD", 30);
    auto r = h.feed_frame(sess, std::span<const std::byte>{frame});
    EXPECT_TRUE(r.has_value()) << "1a_fix42: valid Logon rejected";

    // Deterministic snapshot: ioc is single-threaded; state() is read from the
    // same thread that ran the strand, so this is a stable intermediate observation.
    const auto state_after_logon = sess.state();

    // FR-005: the intermediate LogonReceived state MUST be observed (matrix row
    // NotConnected × valid Logon → LogonReceived). This is not end-state-only.
    EXPECT_EQ(state_after_logon, fsm_state::LogonReceived)
        << "1a_fix42: acceptor must be in LogonReceived after peer Logon; "
        << "got state=" << static_cast<int>(state_after_logon)
        << " (FR-005 §US2 AC2; data-model.md:19 matrix row)";
}

// ══════════════════════════════════════════════════════════════════════════════
// 1a_ValidLogonWithCorrectMsgSeqNum (fix44) — same, FIX.4.4
// T012 rewrite per FR-005 §US2 AC2 (same as fix42 above, FIX.4.4 variant).
// ══════════════════════════════════════════════════════════════════════════════

TEST(TcEstablishment, Scenario1a_ValidLogon_fix44) {
    Harness h;
    auto cfg = h.make_cfg("ISLD", "TW", "FIX.4.4", 30);
    cfg.role = fixpp::session::session_role::acceptor;  // FR-005 / T012
    fixpp::session::Session sess(h.engine, cfg);

    ASSERT_TRUE(h.open_session(sess).has_value()) << "open() failed";
    ASSERT_EQ(sess.state(), fsm_state::NotConnected)
        << "1a_fix44: acceptor must be NotConnected after open()";

    auto frame = make_logon_frame("FIX.4.4", 1, "TW", "ISLD", 30);
    auto r = h.feed_frame(sess, std::span<const std::byte>{frame});
    EXPECT_TRUE(r.has_value()) << "1a_fix44: valid Logon rejected";

    const auto state_after_logon = sess.state();

    EXPECT_EQ(state_after_logon, fsm_state::LogonReceived)
        << "1a_fix44: acceptor must be in LogonReceived after peer Logon; "
        << "got state=" << static_cast<int>(state_after_logon)
        << " (FR-005 §US2 AC2; data-model.md:19 matrix row)";
}

// ══════════════════════════════════════════════════════════════════════════════
// 1c_InvalidSenderCompID (fix42)
// Oracle: peer sends Logon with wrong SenderCompID → server disconnects.
// ══════════════════════════════════════════════════════════════════════════════

TEST(TcEstablishment, Scenario1c_InvalidSenderCompID) {
    Harness h;
    auto cfg = h.make_cfg("ISLD", "TW", "FIX.4.2", 30);
    fixpp::session::Session sess(h.engine, cfg);
    ASSERT_TRUE(h.open_session(sess).has_value());

    // SenderCompID "WT" ≠ expected "TW".
    auto frame = make_logon_frame("FIX.4.2", 1, "WT", "ISLD", 30);
    h.feed_frame(sess, std::span<const std::byte>{frame});

    const auto s = sess.state();
    EXPECT_NE(s, fsm_state::Active)       << "1c_InvalidSenderCompID: must not enter Active";
    EXPECT_NE(s, fsm_state::LogonReceived) << "1c_InvalidSenderCompID: must not enter LogonReceived";
}

// ══════════════════════════════════════════════════════════════════════════════
// 1c_InvalidTargetCompID (fix42)
// ══════════════════════════════════════════════════════════════════════════════

TEST(TcEstablishment, Scenario1c_InvalidTargetCompID) {
    Harness h;
    auto cfg = h.make_cfg("ISLD", "TW", "FIX.4.2", 30);
    fixpp::session::Session sess(h.engine, cfg);
    ASSERT_TRUE(h.open_session(sess).has_value());

    // TargetCompID "DLSI" ≠ expected "ISLD".
    auto frame = make_logon_frame("FIX.4.2", 1, "TW", "DLSI", 30);
    h.feed_frame(sess, std::span<const std::byte>{frame});

    const auto s = sess.state();
    EXPECT_NE(s, fsm_state::Active)       << "1c_InvalidTargetCompID: must not enter Active";
    EXPECT_NE(s, fsm_state::LogonReceived) << "1c_InvalidTargetCompID: must not enter LogonReceived";
}

// ══════════════════════════════════════════════════════════════════════════════
// 1d_InvalidLogonWrongBeginString (fix42)
// Oracle: BeginString=FIX.3.9 → server disconnects.
// ══════════════════════════════════════════════════════════════════════════════

TEST(TcEstablishment, Scenario1d_WrongBeginString) {
    Harness h;
    auto cfg = h.make_cfg("ISLD", "TW", "FIX.4.2", 30);
    fixpp::session::Session sess(h.engine, cfg);
    ASSERT_TRUE(h.open_session(sess).has_value());

    auto frame = make_logon_frame("FIX.3.9", 1, "TW", "ISLD", 30);
    h.feed_frame(sess, std::span<const std::byte>{frame});

    const auto s = sess.state();
    EXPECT_NE(s, fsm_state::Active)       << "1d_WrongBeginString: must not enter Active";
    EXPECT_NE(s, fsm_state::LogonReceived) << "1d_WrongBeginString: must not enter LogonReceived";
}

// ══════════════════════════════════════════════════════════════════════════════
// 1e_NotLogonMessage (fix42)
// Oracle: first inbound is Heartbeat (35=0) → server disconnects.
// ══════════════════════════════════════════════════════════════════════════════

TEST(TcEstablishment, Scenario1e_NotLogonMessage) {
    Harness h;
    auto cfg = h.make_cfg("ISLD", "TW", "FIX.4.2", 30);
    fixpp::session::Session sess(h.engine, cfg);
    ASSERT_TRUE(h.open_session(sess).has_value());

    auto frame = make_heartbeat_frame("FIX.4.2", 1, "TW", "ISLD");
    h.feed_frame(sess, std::span<const std::byte>{frame});

    const auto s = sess.state();
    EXPECT_NE(s, fsm_state::Active)       << "1e_NotLogonMessage: must not enter Active";
    EXPECT_NE(s, fsm_state::LogonReceived) << "1e_NotLogonMessage: must not enter LogonReceived";
}

// ══════════════════════════════════════════════════════════════════════════════
// 2i_BeginStringValueUnexpected (fix42)
// Oracle: post-logon message with wrong BeginString → Logout + disconnect.
// ══════════════════════════════════════════════════════════════════════════════

TEST(TcEstablishment, Scenario2i_BeginStringValueUnexpected) {
    Harness h;
    auto cfg = h.make_cfg("ISLD", "TW", "FIX.4.2", 30);
    fixpp::session::Session sess(h.engine, cfg);
    ASSERT_TRUE(h.open_session(sess).has_value());

    // Step 1: valid Logon → reach Active.
    auto logon = make_logon_frame("FIX.4.2", 1, "TW", "ISLD", 30);
    auto r1 = h.feed_frame(sess, std::span<const std::byte>{logon});
    EXPECT_TRUE(r1.has_value()) << "2i: initial Logon should succeed";

    // Step 2: post-logon Heartbeat with BeginString=FIX.4.1 (wrong version).
    auto bad = make_heartbeat_frame("FIX.4.1", 2, "TW", "ISLD");
    h.feed_frame(sess, std::span<const std::byte>{bad});

    // The session must disconnect (no longer Active) on unexpected BeginString.
    const auto s = sess.state();
    EXPECT_NE(s, fsm_state::Active)
        << "2i: session must not remain Active after wrong BeginString";
}

// ══════════════════════════════════════════════════════════════════════════════
// 2k_CompIDDoesNotMatchProfile (fix42)
// Oracle: post-active message with wrong CompID → Reject + Logout + disconnect.
// ══════════════════════════════════════════════════════════════════════════════

TEST(TcEstablishment, Scenario2k_CompIDDoesNotMatchProfile) {
    Harness h;
    auto cfg = h.make_cfg("ISLD", "TW", "FIX.4.2", 30);
    fixpp::session::Session sess(h.engine, cfg);
    ASSERT_TRUE(h.open_session(sess).has_value());

    // Step 1: valid Logon → reach Active.
    auto logon = make_logon_frame("FIX.4.2", 1, "TW", "ISLD", 30);
    auto r1 = h.feed_frame(sess, std::span<const std::byte>{logon});
    EXPECT_TRUE(r1.has_value()) << "2k: initial Logon should succeed";

    // Step 2: duplicate Logon with wrong SenderCompID ("WT" ≠ "TW").
    // This exercises the Active-state CompID violation path.
    auto bad = make_logon_frame("FIX.4.2", 2, "WT" /* wrong */, "ISLD", 30);
    h.feed_frame(sess, std::span<const std::byte>{bad});

    // The session should not remain Active.
    const auto s = sess.state();
    EXPECT_NE(s, fsm_state::Active)
        << "2k: session must not remain Active after CompID mismatch on active session";
}

}  // namespace fixpp::session::conformance_test
