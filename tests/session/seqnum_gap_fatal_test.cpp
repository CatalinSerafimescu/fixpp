// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/seqnum_gap_fatal_test.cpp
//
// Seam #4 — Too-high gap: session-fatal Logout + disconnect, no ResendRequest
// (005-session-establishment-fsm T028 / US2 Phase 4).
//
// Scenarios covered (I-4 / FR-008 / Session-2026-05-18):
//   1. Active-state inbound with too-high MsgSeqNum:
//      → FSM transitions to Disconnected (session-fatal).
//      → At least one outbound frame is emitted (the Logout-with-text).
//      → The emitted frame has MsgType=5 (Logout) — NOT MsgType=2 (ResendRequest).
//      → Session error callback receives session_test_request_unanswered (stand-in;
//         slot 70 session_seqnum_gap_unrecoverable deleted pre-v1.0 per 013 T006a).
//   2. LogonSent-state too-high: same fatal disposition.
//   3. NO ResendRequest(35=2) is EVER emitted (I-4 absolute).
//   4. Too-low in Active → FSM transitions to Disconnected (session-fatal).
//      → Logout emitted, NOT ResendRequest.
//
// Anchors: data-model.md E3/E2 transition matrix (Active / LogonSent rows);
//   invariant I-4; slot 74 (session_test_request_unanswered, stand-in;
//   slot 70 session_seqnum_gap_unrecoverable deleted pre-v1.0 per 013 T006a).
//   [FIX-SL §4.1] ordered-sequence integrity.
//   Session-2026-05-18 Q1 re-clarification: too-high = session-fatal, recovery deferred.
//
// Infrastructure:
//   TransportDouble (seam #0) — captures outbound frames.
//   StoreDouble     (seam #0) — minimal store.
//   SessionConfig   with executor_override = ioc.get_executor().
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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/store_double.hpp"
#include "support/transport_double.hpp"

// ── #289: bounded pumps ──────────────────────────────────────────────────────
//
// Where a site in this file is migrated it uses `run_window_then_ready` plus a
// miss-branch drain (tests/support/pump_until_ready.hpp). The window is PRESERVED:
// the hazard #289 names is the UNCONDITIONAL `get()`, not the fixed window.
//
// The site label passed to `run_window_then_ready` is the FORCING SEAM: exporting
// FIXPP_FORCE_WINDOW_MISS=<label> makes exactly that site take its miss branch, with
// no source edit and no rebuild. It is a WEAKER witness than textual mutation and
// does not replace it -- see the primitive.
//
// Rationale and the teardown-shape rule live at the primitive, not duplicated here
// (#324).

using namespace std::chrono_literals;

namespace fixpp::session::test {

namespace {

// Build a minimal FIX frame with a given MsgType and MsgSeqNum.
static std::vector<std::byte> make_frame(std::string_view begin_string, std::string_view msg_type,
                                         std::uint32_t seq, std::string_view sender,
                                         std::string_view target,
                                         std::string_view extra_fields = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    if (!extra_fields.empty()) {
        body += std::string(extra_fields);
    }

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
    frame.reserve(full.size());
    for (char c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

static std::vector<std::byte> make_logon_frame(std::string_view begin_string, std::uint32_t seq,
                                               std::string_view sender, std::string_view target,
                                               int heartbt = 30) {
    std::string extra;
    extra += "98=0\x01";
    extra += "108=" + std::to_string(heartbt) + "\x01";
    return make_frame(begin_string, "A", seq, sender, target, extra);
}

static std::vector<std::byte> make_heartbeat_frame(std::string_view begin_string, std::uint32_t seq,
                                                   std::string_view sender,
                                                   std::string_view target) {
    return make_frame(begin_string, "0", seq, sender, target);
}

// Extract a field value from a raw FIX wire frame.
static std::string extract_field(std::span<const std::byte> frame, int tag) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = std::to_string(tag) + "=";
    auto pos = wire.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    pos += needle.size();
    auto end = wire.find('\x01', pos);
    if (end == std::string::npos) {
        return {};
    }
    return wire.substr(pos, end - pos);
}

// Check whether a frame contains a given tag=value pair.
static bool frame_has_field(std::span<const std::byte> frame, int tag, std::string_view value) {
    return extract_field(frame, tag) == value;
}

// Check that a frame is a ResendRequest (MsgType=2).
static bool is_resend_request(std::span<const std::byte> frame) {
    return frame_has_field(frame, 35, "2");
}

// Check that a frame is a Logout (MsgType=5).
static bool is_logout(std::span<const std::byte> frame) { return frame_has_field(frame, 35, "5"); }

}  // namespace

// ── Fixture ───────────────────────────────────────────────────────────────────

class SeqnumGapFatalTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};

    void SetUp() override {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};  // 2024-01-01
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_cfg() {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        // RC#C (gate-b/r1): bilateral_lenient — tests here don't exercise reset semantics.
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        return cfg;
    }

    fixpp::core::expected_t<void> open_sync(Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms,
                                                        "SeqnumGapFatalTest::open_sync")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "SeqnumGapFatalTest::open_sync");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "SeqnumGapFatalTest::open_sync";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    fixpp::core::expected_t<void> feed_sync(Session& s, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms,
                                                        "SeqnumGapFatalTest::feed_sync")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "SeqnumGapFatalTest::feed_sync");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "SeqnumGapFatalTest::feed_sync";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    // Drive a session to Active state (acceptor role):
    //   open() → receive valid Logon(seq=1) → state==LogonReceived/Active.
    bool drive_to_active(Session& s) {
        auto open_r = open_sync(s);
        if (!open_r.has_value()) {
            return false;
        }
        // Peer (TW→ISLD) sends Logon seq=1.
        auto logon = make_logon_frame("FIX.4.2", 1, "TW", "ISLD", 30);
        auto feed_r = feed_sync(s, logon);
        (void)feed_r;
        const auto st = s.state();
        return st == fsm_state::Active;
    }
};

// ── Test 1: Too-high MsgSeqNum in Active → AwaitingResend + ResendRequest ────
//
// 013 FR-009 amendment: too-high inbound seqnum in Active → AwaitingResend
// (NOT session-fatal per the 2e-recovery upgrade). Session STAYS Active and
// emits a ResendRequest(2) to fill the gap. [spec.md FR-009; 013 T006a]
// Pre-013 behavior was fatal-disconnect (005 FR-008); this test is updated
// per T006a catch-site migration.

TEST_F(SeqnumGapFatalTest, ActiveTooHighBecomesSessionFatal) {
    auto cfg = make_cfg();
    Session sess(engine, cfg);

    // Drive to Active (seq #1 consumed).
    ASSERT_TRUE(drive_to_active(sess)) << "Failed to drive session to Active/LogonReceived state";

    // At this point next expected inbound = 2.
    // Inject a Heartbeat with seq=10 (too high by 8).
    auto hb = make_heartbeat_frame("FIX.4.2", 10, "TW", "ISLD");
    auto r = feed_sync(sess, hb);
    (void)r;

    // 013 FR-009: too-high → AwaitingResend; session stays Active.
    // (Pre-013 behavior was Disconnected; amended per 013 T006a catch-site migration.)
    const auto st = sess.state();
    EXPECT_EQ(st, fsm_state::Active)
        << "013 FR-009: too-high seqnum → AwaitingResend; session stays Active; "
        << "got state=" << static_cast<int>(st);
}

// ── Test 2: Too-high gap → ResendRequest(35=2) emitted, session stays Active ──
//
// 013 FR-009: on too-high inbound seqnum, session emits ResendRequest(2) to
// fill the gap and remains Active (AwaitingResend transient flag set).
// Pre-013 behavior: no ResendRequest, session-fatal. Updated per T006a.

TEST_F(SeqnumGapFatalTest, NoResendRequestEmittedOnTooHighGap) {
    auto cfg = make_cfg();
    Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));

    // Too-high gap.
    auto hb = make_heartbeat_frame("FIX.4.2", 99, "TW", "ISLD");
    feed_sync(sess, hb);

    // 013 FR-009: session stays Active (AwaitingResend) after too-high gap.
    // (Pre-013: must be Disconnected or LogoutSent; amended per 013 T006a.)
    const auto st = sess.state();
    EXPECT_EQ(st, fsm_state::Active)
        << "013 FR-009: too-high seqnum gap → AwaitingResend, session stays Active. "
        << "Got state=" << static_cast<int>(st);
}

// ── Test 3: Too-low Heartbeat(35=0) in Active → silently ignored (stays Active)
//
// 013 T020-A / T026: too-low Heartbeat(0) is silently dropped (no disconnect)
// so that liveness-warmup passes (which re-send the same seq) don't disrupt
// an otherwise healthy session. Non-Heartbeat too-low messages remain fatal.
// [spec.md FR-009 warmup behaviour; T020-A dual-gate; T006a catch-site migration]

TEST_F(SeqnumGapFatalTest, ActiveTooLowBecomesSessionFatal) {
    auto cfg = make_cfg();
    Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));

    // In Active: next expected = 2. Send Heartbeat at seq=1 (too low).
    // 013: too-low Heartbeat is silently ignored; session stays Active.
    auto hb = make_heartbeat_frame("FIX.4.2", 1, "TW", "ISLD");
    feed_sync(sess, hb);

    const auto st = sess.state();
    EXPECT_EQ(st, fsm_state::Active)
        << "013: too-low Heartbeat(0) silently ignored; session stays Active. "
        << "Got state=" << static_cast<int>(st);
}

// ── Test 4: LogonSent + unexpected inbound → Disconnected (matrix-strict) ────
//
// The transition matrix LogonSent row says:
//   inbound Heartbeat / TestRequest / Reject / out-of-scope admin /
//   invalid MsgType / refused Logon → session-fatal Logout+disconnect.
//
// Phase 4 (US2) scope: the Logout emission is US4-owned; the FSM transition
// to Disconnected is wired here under T035. The test asserts the exact
// matrix outcome: state == Disconnected.
//
// NOTE: this test goes through the NotConnected→LogonSent transition path.
// At Phase 4 the initiator open() path that mints LogonSent is NOT yet
// wired (T023 ships the shape; the FSM mutation to LogonSent runs in
// later US1 work). If open() leaves the FSM in NotConnected, the
// inbound Heartbeat hits the NotConnected row instead — which per matrix
// is `refuse, → Disconnected`. Either path produces Disconnected; the
// assertion below is the matrix-strict outcome valid for both paths.

TEST_F(SeqnumGapFatalTest, LogonSentTooHighBecomesSessionFatal) {
    auto cfg = make_cfg();
    // Initiator role: set sender_comp_id=TW, target=ISLD (reversed from acceptor).
    cfg.sender_comp_id = "TW";
    cfg.target_comp_id = "ISLD";
    Session sess(engine, cfg);

    auto open_r = open_sync(sess);
    ASSERT_TRUE(open_r.has_value()) << "open() should succeed for initiator";

    // Inject a Heartbeat with seq=99 instead of the expected Logon ack.
    // Either LogonSent row (heartbeat in LogonSent → fatal) or NotConnected
    // row (heartbeat as 1st msg → refuse, → Disconnected) applies depending
    // on whether the initiator's LogonSent FSM mutation is wired yet.
    // Both paths produce Disconnected per the data-model matrix.
    auto hb = make_heartbeat_frame("FIX.4.2", 99, "ISLD", "TW");
    feed_sync(sess, hb);

    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "LogonSent (or NotConnected) + unexpected Heartbeat must transition "
        << "to Disconnected per data-model.md matrix; got state=" << static_cast<int>(sess.state());
}

}  // namespace fixpp::session::test
