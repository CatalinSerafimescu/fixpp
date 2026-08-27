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
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/store_double.hpp"
#include "support/transport_double.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::conformance_test {
namespace {

// ── Frame builder helpers ──────────────────────────────────────────────────────

static std::vector<std::byte> make_logon_frame(std::string_view begin_string, std::uint32_t seq,
                                               std::string_view sender, std::string_view target,
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
    for (unsigned char c : full) {
        cs += c;
    }
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    for (char c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

static std::vector<std::byte> make_heartbeat_frame(std::string_view begin_string, std::uint32_t seq,
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
    for (unsigned char c : full) {
        cs += c;
    }
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    for (char c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

// Build a frame with an unknown MsgType (for 2q/2r).
static std::vector<std::byte> make_unknown_msgtype_frame(std::string_view begin_string,
                                                         std::uint32_t seq, std::string_view sender,
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
    for (unsigned char c : full) {
        cs += c;
    }
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    for (char c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

}  // namespace

// ── Fixture ───────────────────────────────────────────────────────────────────

//
// ── #289: the `run_for(W); restart(); fut.get()` migration ───────────────────
//
// The sites in this file use `run_window_then_ready`
// (tests/support/pump_until_ready.hpp). The window is PRESERVED: the hazard
// #289 names is the UNCONDITIONAL `get()`, not the fixed window. On a
// manually-driven io_context a `get()` the window did not satisfy blocks with
// nothing left to pump it -- a deadlock ctest reports as a timeout.
//
// Teardown is deliberately NOT a fixture-destructor drain, which is the shape
// PRs #301 and #307 used for fixtures that OWN their Session. Here every
// `Session` is a block-local declared AFTER the fixture, so it dies BEFORE a
// fixture destructor body could run -- and a drain is what RESUMES a suspended
// frame, so a destructor drain would resume it over the destroyed Session. The
// drain runs on the MISS branch instead, in the scope that still owns that
// storage, and it CANCELS THE MOCK CLOCK'S SLEEPS first: the first transition
// to Active co_spawns a detached `run_liveness_loop()` that parks on
// `sleep_until`, holding a work guard that `drain_or_report` cannot release
// (only a Clock can). `cancel_sleeps()` releases the waiters that exist WHEN IT
// RUNS and nothing more, so a miss whose drain itself performs the Active
// transition registers a NEW waiter afterwards and the drain then reports an
// honest residual. This is a documented limitation of the primitive
// (`pump_until_ready.hpp`), carried over from PR #313 unchanged.

// #289 harness sentinel: what the value-returning pump helpers below return
// when the preserved run window misses.
//
// It is deliberately NOT load-bearing under ordinary GoogleTest execution: the
// `ADD_FAILURE()` on the same branch records a nonfatal failure that the
// enclosing test retains, so a window miss cannot read as a pass whatever this
// value is. `--gtest_throw_on_failure` does not change that -- it throws AFTER
// reporting.
//
// The CONDITION that holds under, stated rather than counted: NO CALLER
// INTERCEPTS THE FAILURE. `EXPECT_NONFATAL_FAILURE` /
// `ScopedFakeTestPartResultReporter` (gtest-spi.h) install a fake reporter that
// absorbs the failure and lets the enclosing test pass. The first caller that
// does makes this value the ONLY remaining signal -- and `dispatch_aborted` is
// then ambiguous with a real `open()` / `on_inbound_frame()` outcome
// ([2d §6.5]; the `dispatch_aborted` returns in `Session::live_write_serialized_`),
// so an assertion on the error
// could be satisfied by this synthetic one. The remedy at that point is a
// distinct harness result (`std::optional<expected_t<void>>`), not a different
// production code.
//
// The sentinel exists for one narrower reason that holds either way: a caller
// that checks `has_value()` must not proceed on a fabricated success.
//
// Named rather than inlined so the other value-returning #289 sites adopt one
// greppable decision. Folding it into
// `tests/support/pump_until_ready.hpp` is deliberately deferred to the header PR
// that also closes the class-4 transport-teardown gap, so that header is touched
// once rather than twice.
inline constexpr auto kWindowMissSentinel = fixpp::core::error::dispatch_aborted;

class TcSeqnumTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};

    void SetUp() override {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_acceptor_cfg() {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        // RC#C (gate-b/r1): bilateral_lenient — conformance tests don't exercise reset.
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        return cfg;
    }

    fixpp::core::expected_t<void> open_sync(fixpp::session::Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "TcSeqnumTest::open_sync");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "TcSeqnumTest::open_sync";
            return std::unexpected(kWindowMissSentinel);
        }
        return fut.get();
    }

    fixpp::core::expected_t<void> feed_sync(fixpp::session::Session& s,
                                            std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "TcSeqnumTest::feed_sync");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "TcSeqnumTest::feed_sync";
            return std::unexpected(kWindowMissSentinel);
        }
        return fut.get();
    }

    // Drive a session to Active/LogonReceived (acceptor):
    // open() → receive Logon(seq=1) → state ∈ {Active, LogonReceived}.
    bool drive_to_active(fixpp::session::Session& s) {
        auto open_r = open_sync(s);
        if (!open_r.has_value()) {
            return false;
        }
        auto logon = make_logon_frame("FIX.4.2", 1, "TW", "ISLD", 30);
        auto feed_r = feed_sync(s, logon);
        (void)feed_r;
        const auto st = s.state();
        return st == fixpp::session::fsm_state::Active;
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
    EXPECT_EQ(st, fixpp::session::fsm_state::Active)
        << "2a: Session should remain Active after in-sequence message";
}

// ── 2c_MsgSeqNumTooLow ────────────────────────────────────────────────────────
//
// Oracle: a message with MsgSeqNum < next-expected and no PossDupFlag=Y →
// session-fatal ([FIX-SL §4.1]) for non-Heartbeat messages.
//
// T006a catch-site migration (013): Heartbeat(35=0) with too-low seqnum is
// silently ignored in Active state per 013 T020-A warmup compatibility.
// The general [FIX-SL §4.1] fatal rule applies to all other MsgTypes.
//
// 2c scenario: after reaching Active (next expected=2), send a Heartbeat with
// seq=1. With 013, too-low Heartbeat is silently ignored → session stays Active.

TEST_F(TcSeqnumTest, Tc2c_MsgSeqNumTooLow) {
    auto cfg = make_acceptor_cfg();
    fixpp::session::Session sess(engine, cfg);

    // Drive to Active: Logon consumed seq=1; next expected = 2.
    ASSERT_TRUE(drive_to_active(sess)) << "2c: Failed to drive to Active";

    // Send a Heartbeat with seq=1 (too low; no PossDupFlag).
    // 013 FR-009 / T020-A: too-low Heartbeat is silently ignored → stays Active.
    auto hb = make_heartbeat_frame("FIX.4.2", 1, "TW", "ISLD");
    feed_sync(sess, hb);

    // With 013, too-low Heartbeat → Active (not fatal).
    // The general [FIX-SL §4.1] fatal rule still applies to non-Heartbeat
    // messages; those paths are verified in fsm_matrix_witness_test.cpp.
    const auto st = sess.state();
    EXPECT_EQ(st, fixpp::session::fsm_state::Active)
        << "2c: Too-low Heartbeat must be silently ignored (Active) per 013 T020-A; "
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
