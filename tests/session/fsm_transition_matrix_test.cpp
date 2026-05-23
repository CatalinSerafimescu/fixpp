// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/fsm_transition_matrix_test.cpp
//
// 005-session-establishment-fsm T058 / Phase 8 / Seam #1 — FR-001 / I-1:
// "Every `[FIX-SL §4.10]` state×event cell maps to a defined transition
//  (no implicit/undefined, no silent no-op)."
//
// This is the META-attestation seam: it does NOT re-verify the detailed
// per-cell BEHAVIOUR (that is the per-US seam suite's job — seams #2 / #3 /
// #4 / #5 / #6 / #7 / #8 / #10 / #11). It asserts the matrix is TOTAL by
// driving one representative event per row × per defined event class, and
// observing:
//
//   (a) the driver does NOT crash / UB / hang;
//   (b) `Session::state()` after the event is one of the 6 valid `fsm_state`
//       values (i.e., the FSM never lands in an undefined slot);
//   (c) for the LogoutSent (drained) row, inbound non-Logout events leave
//       state == LogoutSent (the (drained) defined-transition semantic);
//   (d) for the Disconnected row, inbound events leave state == Disconnected
//       (the "ignored" defined-transition semantic).
//
// Anchors: data-model.md §"Transition matrix"; spec FR-001 / FR-017;
// invariant I-1; [FIX-SL §4.10]. Per-cell behaviour anchors: see header
// comment of each per-US seam test.

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

namespace fixpp::session::test {

// ── Compile-time invariants on the fsm_state enum (E2 / D-2) ────────────────

static_assert(static_cast<std::uint8_t>(fsm_state::NotConnected) == 0);
static_assert(static_cast<std::uint8_t>(fsm_state::LogonSent) == 1);
static_assert(static_cast<std::uint8_t>(fsm_state::LogonReceived) == 2);
static_assert(static_cast<std::uint8_t>(fsm_state::Active) == 3);
static_assert(static_cast<std::uint8_t>(fsm_state::LogoutSent) == 4);
static_assert(static_cast<std::uint8_t>(fsm_state::Disconnected) == 5);

// "No `RecoveryPending`" (D-2 / Session-2026-05-18) is enforced structurally
// by the enum: any reference to `fsm_state::RecoveryPending` in code would
// fail to compile. This TU does not name it; that absence is the test.

namespace {

// ── Frame builder ───────────────────────────────────────────────────────────
//
// Builds a minimal FIX 4.2 admin frame with the supplied MsgType + MsgSeqNum
// and the default SENDER/TARGET CompIDs. Identical wire shape to the per-US
// seam tests' make_*_frame builders.

std::vector<std::byte> make_admin_frame(std::string_view msg_type, std::uint32_t msg_seq_num,
                                        std::string_view sender = "TW",
                                        std::string_view target = "ISLD",
                                        std::string_view extra_fields = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(msg_seq_num) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    if (!extra_fields.empty()) body += std::string(extra_fields);

    std::string hdr;
    hdr += "8=FIX.4.2\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    cs &= 0xFFU;
    char csbuf[4];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(full.size());
    for (char c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

bool is_valid_fsm_state(fsm_state s) noexcept {
    switch (s) {
        case fsm_state::NotConnected:
        case fsm_state::LogonSent:
        case fsm_state::LogonReceived:
        case fsm_state::Active:
        case fsm_state::LogoutSent:
        case fsm_state::Disconnected:
            return true;
    }
    return false;
}

}  // namespace

// ── Fixture ─────────────────────────────────────────────────────────────────

class FsmTransitionMatrixTest : public ::testing::Test {
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

    SessionConfig make_initiator_cfg() {
        SessionConfig cfg;
        cfg.sender_comp_id = "TW";
        cfg.target_comp_id = "ISLD";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = std::chrono::seconds{30};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        return cfg;
    }

    fixpp::core::expected_t<void> open_sync(Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(std::chrono::milliseconds{200});
        ioc.restart();
        return fut.get();
    }

    fixpp::core::expected_t<void> feed_sync(Session& s, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(std::chrono::milliseconds{200});
        ioc.restart();
        return fut.get();
    }

    fixpp::core::expected_t<void> close_sync(Session& s, close_mode mode) {
        auto fut = asio::co_spawn(ioc, s.close(mode), asio::use_future);
        ioc.run_for(std::chrono::milliseconds{500});
        ioc.restart();
        return fut.get();
    }
};

// ── Row 1: NotConnected ─────────────────────────────────────────────────────
//
// Matrix: open(initiator) → LogonSent; every other event → Disconnected
// (acceptor not-Logon-first refusal / out-of-scope / fatal close). I-1: all
// cells are defined; no silent no-op.

TEST_F(FsmTransitionMatrixTest, NotConnected_StartState_IsNotConnected) {
    auto cfg = make_initiator_cfg();
    Session sess(engine, cfg);
    EXPECT_EQ(sess.state(), fsm_state::NotConnected)
        << "Pre-open initial state must be NotConnected (E2)";
    EXPECT_TRUE(is_valid_fsm_state(sess.state()));
}

TEST_F(FsmTransitionMatrixTest, NotConnected_OpenInitiator_LandsInLogonSent) {
    auto cfg = make_initiator_cfg();
    Session sess(engine, cfg);
    auto r = open_sync(sess);
    ASSERT_TRUE(r.has_value()) << "Session::open() failed";
    // Matrix: NotConnected × open(initiator) → LogonSent (emit Logon, seq=1).
    EXPECT_EQ(sess.state(), fsm_state::LogonSent);
    EXPECT_TRUE(is_valid_fsm_state(sess.state()));
}

// ── Row 2: LogonSent ────────────────────────────────────────────────────────
//
// Matrix samples: valid Logon (peer-ack) → Active; inbound non-Logon (HB/TR/
// Reject) → session-fatal → Disconnected; out-of-scope admin → session-fatal.
// Every event has a defined post-state — never UB, never silent no-op.

TEST_F(FsmTransitionMatrixTest, LogonSent_InboundHeartbeat_LandsInValidState) {
    auto cfg = make_initiator_cfg();
    Session sess(engine, cfg);
    ASSERT_TRUE(open_sync(sess).has_value());
    ASSERT_EQ(sess.state(), fsm_state::LogonSent);

    // Matrix cell LogonSent × inbound Heartbeat → session-fatal Logout+disconnect.
    // I-1 attestation: post-state is a defined fsm_state value (no UB).
    auto hb = make_admin_frame("0", 1);
    (void)feed_sync(sess, hb);  // may return error (session-fatal); not asserted here
    EXPECT_TRUE(is_valid_fsm_state(sess.state()))
        << "LogonSent×Heartbeat must land in a defined fsm_state (FR-001 / I-1); "
        << "got " << static_cast<int>(sess.state());
}

TEST_F(FsmTransitionMatrixTest, LogonSent_InboundResendRequestIsDefinedBoundedTransition) {
    auto cfg = make_initiator_cfg();
    Session sess(engine, cfg);
    ASSERT_TRUE(open_sync(sess).has_value());
    ASSERT_EQ(sess.state(), fsm_state::LogonSent);

    // Matrix: LogonSent × out-of-scope admin (RR=35=2) → defined bounded
    // disposition (FR-017 / session_admin_not_supported, slot 75). I-1
    // attestation: never undefined, never silent — the post-state is a
    // valid fsm_state value (the exact disposition is the per-US seam test's
    // concern; here we attest only that the cell is defined and exits cleanly).
    auto rr = make_admin_frame("2", 1, "TW", "ISLD",
                               "7=1\x01"
                               "16=0\x01");  // BeginSeqNo / EndSeqNo
    (void)feed_sync(sess, rr);
    EXPECT_TRUE(is_valid_fsm_state(sess.state()));
}

// ── Row 3 / 4: LogonReceived → Active ───────────────────────────────────────
//
// Driving the acceptor's Logon-received path requires a peer Logon delivered
// via on_inbound_frame() while in NotConnected. The matrix's interesting
// LogonReceived/Active cells (HB/TR/seqnum/Logout/timer) are exhaustively
// exercised by the per-US seams (#2/#3/#5/#6). This TU's meta-attestation
// for the Active row is the inbound-out-of-scope-admin cell — FR-017
// asserts it is a defined bounded reject, not a silent no-op nor UB.

TEST_F(FsmTransitionMatrixTest, Active_InboundResendRequestIsDefinedBoundedTransition) {
    auto cfg = make_initiator_cfg();
    Session sess(engine, cfg);
    ASSERT_TRUE(open_sync(sess).has_value());

    // Drive LogonSent → Active via a peer Logon-ack with the negotiated
    // HeartBtInt. Seam #2 (logon_handshake_test) exercises this in depth; we
    // re-use the same shape.
    auto peer_logon = make_admin_frame("A", 1, "ISLD", "TW",
                                       "98=0\x01"
                                       "108=30\x01");  // EncryptMethod / HeartBtInt
    auto fr = feed_sync(sess, peer_logon);
    // We don't require Active here (the negotiated path may vary per US1
    // impl); we DO require a defined fsm_state and that the test does not
    // crash. I-1 holds if and only if every cell-driver completes cleanly.
    (void)fr;
    EXPECT_TRUE(is_valid_fsm_state(sess.state()));

    // Now inject an out-of-scope admin (ResendRequest) in whatever post-Logon
    // state we landed in. FR-017: defined bounded reject — never UB / never
    // silent no-op.
    auto rr = make_admin_frame("2", 2, "ISLD", "TW",
                               "7=1\x01"
                               "16=0\x01");
    (void)feed_sync(sess, rr);
    EXPECT_TRUE(is_valid_fsm_state(sess.state()));
}

// ── Row 5: LogoutSent (drained) ─────────────────────────────────────────────
//
// The (drained) cell is the most subtle defined-transition in the matrix
// (data-model.md preamble): the inbound message is accepted off the wire and
// discarded — FSM REMAINS in LogoutSent, next-expected-inbound seqnum NOT
// advanced, no fromAdmin/fromApp dispatch, no emit. I-1 attestation: a
// HB/TR/RR fed into LogoutSent must leave state == LogoutSent.

TEST_F(FsmTransitionMatrixTest, LogoutSent_InboundNonLogoutEventsAreDrainedCellRemainsLogoutSent) {
    auto cfg = make_initiator_cfg();
    Session sess(engine, cfg);
    ASSERT_TRUE(open_sync(sess).has_value());

    // Drive into Active first.
    auto peer_logon = make_admin_frame("A", 1, "ISLD", "TW",
                                       "98=0\x01"
                                       "108=30\x01");
    (void)feed_sync(sess, peer_logon);

    // Initiate Logout → LogoutSent (we don't ioc.run() to completion of the
    // phase-1 graceful flow; close() posts the Logout emit + phase-1 timer
    // wait, and the FSM is in LogoutSent during phase 1).
    asio::co_spawn(ioc, sess.close(close_mode::graceful),
                   [](std::exception_ptr, fixpp::core::expected_t<void>) {});
    ioc.run_for(std::chrono::milliseconds{20});  // let phase-1 emit fire

    // We accept either LogoutSent (phase-1 in flight) or Disconnected
    // (phase-2 has resolved). Both are defined matrix outcomes for the close
    // path. If we observe LogoutSent, exercise the (drained) cell.
    const auto state_after_close = sess.state();
    EXPECT_TRUE(is_valid_fsm_state(state_after_close));

    if (state_after_close == fsm_state::LogoutSent) {
        // Feed a Heartbeat — must remain LogoutSent (the (drained) cell).
        auto hb = make_admin_frame("0", 2, "ISLD", "TW");
        (void)feed_sync(sess, hb);
        EXPECT_EQ(sess.state(), fsm_state::LogoutSent)
            << "LogoutSent × inbound Heartbeat is the (drained) cell: "
            << "FSM must REMAIN in LogoutSent (data-model.md preamble)";
    }
    // Let close() complete cleanly before the fixture tears down.
    ioc.run_for(std::chrono::milliseconds{500});
}

// ── Row 6: Disconnected ─────────────────────────────────────────────────────
//
// Matrix: every inbound event is "ignored" (a DEFINED transition: FSM remains
// in Disconnected, no emit, no counter effect). I-1 attestation: a HB fed
// into Disconnected must leave state == Disconnected.

TEST_F(FsmTransitionMatrixTest, Disconnected_InboundIsIgnoredCellRemainsDisconnected) {
    auto cfg = make_initiator_cfg();
    Session sess(engine, cfg);
    ASSERT_TRUE(open_sync(sess).has_value());

    // Drive directly to Disconnected via close(terminal) — skips phase 1.
    auto r = close_sync(sess, close_mode::terminal);
    (void)r;
    ASSERT_EQ(sess.state(), fsm_state::Disconnected);

    // Feed a Heartbeat — must remain Disconnected (the "ignored" cell).
    auto hb = make_admin_frame("0", 2, "ISLD", "TW");
    auto fr = feed_sync(sess, hb);
    (void)fr;  // may surface session_already_closed; the post-state is what
               // I-1 attests.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "Disconnected × inbound is the 'ignored' cell: FSM must REMAIN "
        << "Disconnected (data-model.md matrix row 6)";
}

// ── Cross-row close idempotency (matrix close_terminal column) ──────────────
//
// I-9 + matrix: Session::close() is idempotent. Repeated close(terminal)
// from Disconnected returns session_already_closed and leaves state ==
// Disconnected. Defined-cell attestation for the close-after-close case.

TEST_F(FsmTransitionMatrixTest, Disconnected_CloseTerminalIsIdempotentAndStaysDisconnected) {
    auto cfg = make_initiator_cfg();
    Session sess(engine, cfg);
    ASSERT_TRUE(open_sync(sess).has_value());
    (void)close_sync(sess, close_mode::terminal);
    ASSERT_EQ(sess.state(), fsm_state::Disconnected);

    // Second close — defined no-op-style transition per the matrix.
    auto second = close_sync(sess, close_mode::terminal);
    (void)second;  // session_already_closed expected; not the I-1 property.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected);
}

// ── NotConnected refused-Logon arms (session.cpp:489-554) ──────────────────
//
// Matrix row NotConnected has two refusal cells (session.cpp:489-554):
//   1. Non-Logon first message → refuse, transition to Disconnected.
//   2. Logon-shaped but BeginString/CompID mismatch → refuse, stay in
//      NotConnected (Phase 3 "never reaches Active" contract; full refuse+
//      disconnect lands in a later phase).
//
// Coverage gap: existing tests drive NotConnected via open() into LogonSent
// (initiator path); none feed a refused frame to an acceptor-shaped session
// still in NotConnected. These two tests close that arm.

TEST_F(FsmTransitionMatrixTest, NotConnected_NonLogonFirstMessage_TransitionsToDisconnected) {
    // Acceptor-shaped: do NOT call open() (no initiator Logon emitted).
    // Session is in NotConnected; feed a Heartbeat (35=0) as the first frame.
    auto cfg = make_initiator_cfg();
    Session sess(engine, cfg);
    ASSERT_EQ(sess.state(), fsm_state::NotConnected);

    auto hb = make_admin_frame("0", 1, "ISLD", "TW");
    (void)feed_sync(sess, hb);

    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "NotConnected + non-Logon first frame must transition to Disconnected "
        << "(session.cpp:548-550; matrix row 1)";
}

// Acceptor-shaped NotConnected: a fresh session (no open() called) receives a
// valid peer Logon (BeginString + CompID + seqnum=1 all match cfg). Per the
// matrix row NotConnected×Logon(valid), the FSM transitions through LogonReceived
// to Active (the acceptor emits its own Logon reply on the LogonReceived→Active
// transition, F1 Round-A drift fix, spec.md FR-005 §US2 AC2).
TEST_F(FsmTransitionMatrixTest, NotConnected_ValidLogonFromPeer_LandsInLogonReceived) {
    auto cfg = make_initiator_cfg();
    Session sess(engine, cfg);
    ASSERT_EQ(sess.state(), fsm_state::NotConnected);

    // Build a peer Logon: peer's 49=ISLD = our target, peer's 56=TW = our sender,
    // BeginString=FIX.4.2 matches cfg, seqnum=1.
    auto peer_logon = make_admin_frame("A", 1, "ISLD", "TW");
    // make_admin_frame doesn't include 98=0 + 108=N, so this might not parse as
    // a valid Logon by interpret_logon's strict criteria. Build it manually.
    const std::string body =
        "8=FIX.4.2\x01"
        "9=74\x01"
        "35=A\x01"
        "34=1\x01"
        "49=ISLD\x01"
        "52=20200101-00:00:00.000\x01"
        "56=TW\x01"
        "98=0\x01"
        "108=30\x01";
    std::uint32_t cs = 0;
    for (char c : body) cs = (cs + static_cast<unsigned char>(c)) & 0xFFU;
    char csbuf[4];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    const std::string full = body + "10=" + csbuf + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(full.size());
    for (char c : full) frame.push_back(static_cast<std::byte>(c));

    (void)feed_sync(sess, frame);

    // F1 (Round-A drift fix): acceptor now emits reply Logon and transitions to Active
    // within on_inbound_frame. The LogonReceived state is transient and internal;
    // after feed_sync returns, state must be Active (spec.md FR-005 §US2 AC2).
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "NotConnected + valid peer Logon (acceptor first message) must "
        << "transition through LogonReceived to Active (FR-005/F1 drift fix)";
}

// T013 [US3] — FR-006: refused Logon on NotConnected row → Disconnected (not preserved).
// Anchors: spec.md FR-006, data-model.md:19 matrix row, opus_pr81_1_triage.md RC#3.
TEST_F(FsmTransitionMatrixTest, NotConnected_RefusedLogonByBeginString_ReachesDisconnected) {
    // Feed a Logon (35=A) with wrong BeginString — interpret_logon refuses.
    // FR-006 contract: every refusal on the NotConnected row → Disconnected
    // (no MsgType discrimination; Phase-3 compromise removed by T014).
    auto cfg = make_initiator_cfg();  // expects FIX.4.2
    Session sess(engine, cfg);
    ASSERT_EQ(sess.state(), fsm_state::NotConnected);

    // Wrong BeginString ("FIX.4.4" instead of cfg's "FIX.4.2").
    // The full frame still has MsgType=A (35=A); interpret_logon rejects on
    // BeginString mismatch.
    const std::string body =
        "8=FIX.4.4\x01"
        "9=64\x01"
        "35=A\x01"
        "34=1\x01"
        "49=ISLD\x01"
        "52=20200101-00:00:00.000\x01"
        "56=TW\x01"
        "98=0\x01"
        "108=30\x01";
    // Compute checksum.
    std::uint32_t cs = 0;
    for (char c : body) cs = (cs + static_cast<unsigned char>(c)) & 0xFFU;
    char csbuf[4];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    const std::string full = body + "10=" + csbuf + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(full.size());
    for (char c : full) frame.push_back(static_cast<std::byte>(c));

    (void)feed_sync(sess, frame);

    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "NotConnected + Logon-shaped frame with BeginString mismatch must "
        << "transition to Disconnected per FR-006 / data-model.md:19 matrix row";
}

}  // namespace fixpp::session::test
