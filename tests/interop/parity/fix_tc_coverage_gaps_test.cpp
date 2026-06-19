// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/parity/fix_tc_coverage_gaps_test.cpp
//
// FIX-TC / QuickFIX reference-suite coverage-gap CHARACTERIZATION witnesses.
//
// Closes / documents the actionable rows from
// `research/G19-fix-fpml-iso20022/fix-tc-coverage-audit.md` that are testable
// against the current shipped Session behavior with no new production code:
//
//   gap #1/2b  TooHigh gap → ResendRequest emitted, stays Active (audit was
//              STALE: 013 FR-009 amended too-high from session-fatal to
//              AwaitingResend+ResendRequest; the prior `seqnum_gap_fatal_test`
//              cells only asserted state==Active, never the ResendRequest emit).
//   gap #9     ResendRequest EndSeqNo > our last stored outbound → GapFill the
//              clamped/unknown range, stays Active (QF parity: gap-fill, no error).
//   gap #7/2t  Header fields out of canonical order (MsgType not first body
//              field) → ACCEPTED, not Rejected. DIVERGENCE from QuickFIX (which
//              Rejects 373=14). Documented as B-cov-1; see the findings report.
//   gap #3/15  Non-header body fields in arbitrary order → ACCEPTED. Same
//              order-independent-parse divergence (B-cov-1).
//
// These are STANDALONE characterization cells (no counterparty / live transport):
// they drive crafted frames through Session::on_inbound_frame() and observe the
// outbound `transport_send_` capture + seqnum_mgr_test_access() (FIXPP_TEST_HOOKS),
// reusing the proven ParityAcceptorFixture (parity_support.hpp).
//
// [const §XV.9]: tests-only. spec_ref [FIX-SL §4.5 / §4.8 / FIX-TC 2b/2t/15].

#include "parity_support.hpp"

namespace fixpp::interop::parity {
namespace {

// Build a frame from a pre-ordered body (callers control field order, so these
// cells can craft out-of-canonical-order frames the make_fix_frame helper can't).
std::vector<std::byte> frame_from_body(std::string_view begin_string, const std::string& body) {
    std::string msg;
    msg += "8=" + std::string(begin_string) + "\x01";
    msg += "9=" + std::to_string(body.size()) + "\x01";
    msg += body;
    unsigned int cs = 0;
    for (unsigned char c : msg) {
        cs += c;
    }
    cs &= 0xFFU;
    char csbuf[5];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    msg += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(msg.size());
    for (char c : msg) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

// Does any captured frame contain a literal needle (e.g., "7=2\x01")?
bool any_frame_contains(const OutboundCapture& cap, std::string_view needle) {
    for (const auto& f : cap.frames) {
        std::string wire(reinterpret_cast<const char*>(f.data()), f.size());
        if (wire.find(std::string(needle)) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// Does ONE specific captured frame contain a literal needle? (Stronger than
// any_frame_contains: ties the asserted token to a single emitted frame so a
// witness can't be satisfied by the token appearing in some unrelated frame.)
bool frame_has(const std::vector<std::byte>& f, std::string_view needle) {
    std::string wire(reinterpret_cast<const char*>(f.data()), f.size());
    return wire.find(std::string(needle)) != std::string::npos;
}

using FixTcCoverageGaps = ParityAcceptorFixture;

// ── gap #1 / FIX-TC 2b — too-high inbound seqnum → ResendRequest, stays Active ─
//
// AUDIT WAS STALE. The fix-tc-coverage-audit lists 2b / 1a / logonSeqTooHigh as
// "deferred — no ResendRequest on too-high gap". 013 FR-009 amended this: a
// too-high gap now enters AwaitingResend and EMITS a ResendRequest(35=2) to fill
// the gap, staying Active. The existing seqnum_gap_fatal_test cells assert only
// state==Active and never the ResendRequest emission — so the emit was unwitnessed.
TEST_F(FixTcCoverageGaps, TooHighInboundSeqnum_EmitsResendRequest_StaysActive) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(drive_to_active(s));
    ASSERT_EQ(next_inbound(s), 2U) << "precondition: next-expected-inbound = 2";

    const std::size_t before = capture.frames.size();

    // Peer skips [2..9] and sends a Heartbeat at seq=10 (too high).
    (void)feed(s, make_fix_frame("FIX.4.2", "0", /*seq=*/10, "TW", "ISLD"));

    EXPECT_EQ(s.state(), fixpp::session::fsm_state::Active)
        << "013 FR-009: too-high gap → AwaitingResend, session stays Active";
    ASSERT_EQ(capture.frames.size(), before + 1U)
        << "exactly one frame (the ResendRequest) must be emitted in response";
    const auto& rr = capture.frames.back();
    EXPECT_TRUE(frame_has(rr, "35=2\x01")) << "the emitted frame must be a ResendRequest(35=2)";
    // Pin the FULL resend range on the SAME frame: BeginSeqNo(7) = first missing
    // seqnum (2), EndSeqNo(16) = 0 ("through current" — open-ended). Asserting
    // only 7=2 (or asserting across all frames) would tolerate a mutated EndSeqNo
    // that silently narrows the recovery window.
    EXPECT_TRUE(frame_has(rr, "7=2\x01"))
        << "ResendRequest BeginSeqNo(7) must start at the first missing seqnum (2)";
    EXPECT_TRUE(frame_has(rr, "16=0\x01"))
        << "ResendRequest EndSeqNo(16) must be 0 (through-current / open-ended)";
}

// ── gap #9 — ResendRequest EndSeqNo larger than messages we have → GapFill ────
//
// FIX-TC resendRequest_EndSeqNumberLargerThanMessages. A peer asks us to resend
// a range [1..100] whose EndSeqNo far exceeds anything we have emitted. This
// fixture attaches NO message store, so replay_outbound_range_ takes the
// store-absent branch (session.cpp:4838): the entire requested range collapses
// into a SINGLE SequenceReset-GapFill(35=4) whose NewSeqNo(36)=requested_end+1
// (here 101, session.cpp:4840). The session stays Active — no error/disconnect.
// (A store WITH stored app messages would instead clamp the effective end to the
// last stored outbound and walk per-slot — a different cell; not exercised here.)
TEST_F(FixTcCoverageGaps, ResendRequestEndSeqNoBeyondLastOutbound_GapFills_StaysActive) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(drive_to_active(s));

    const std::size_t before = capture.frames.size();

    // We (acceptor) have emitted only our Logon reply (outbound seq 1) and keep no
    // store. The peer asks for [1..100] — far beyond anything we can replay.
    (void)feed(s, make_resend_request("FIX.4.2", /*seq=*/2, "TW", "ISLD",
                                      /*begin=*/1, /*end=*/100));

    EXPECT_EQ(s.state(), fixpp::session::fsm_state::Active)
        << "over-range ResendRequest must not disconnect the session";
    // Enforce the "SINGLE GapFill" post-condition: exactly one frame emitted, and
    // it is a SequenceReset (35=4). Asserting count>=1 across all frames would
    // tolerate multiple resets or fail to bind the range fields to this frame.
    ASSERT_EQ(capture.frames.size(), before + 1U)
        << "the over-range request must collapse into exactly ONE reply frame";
    const auto& gf = capture.frames.back();
    EXPECT_TRUE(frame_has(gf, "35=4\x01")) << "the single reply must be a SequenceReset(35=4)";
    // DISCRIMINATING the over-range coverage on that SAME frame: it must be a
    // GAP-FILL (123=Y), not a hard reset (123=N), and honor the full requested
    // EndSeqNo so NewSeqNo(36)=requested_end+1=101. A witness asserting only
    // "a 35=4 exists" would also pass on a [1..1] gap-fill (36=2), a reset-mode
    // 35=4, or an unrelated SequenceReset — 36=101 + 123=Y pin the exact behavior.
    EXPECT_TRUE(frame_has(gf, "123=Y\x01"))
        << "the reply must be a SequenceReset-GapFill (123=Y), not a hard reset";
    EXPECT_TRUE(frame_has(gf, "36=101\x01"))
        << "store-absent over-range GapFill must carry NewSeqNo=EndSeqNo+1 (101)";
}

// ── gap #7 / FIX-TC 2t — header fields out of canonical order → ACCEPTED ───────
//
// DIVERGENCE (documented, B-cov-1). FIX-SL §4.5 mandates the first three fields
// be 8=BeginString, 9=BodyLength, 35=MsgType. The framer enforces 8 then 9, but
// the session/parser scans the remaining fields ORDER-INDEPENDENTLY, so a frame
// where MsgType(35) is NOT the first body field is ACCEPTED — not Rejected.
// QuickFIX would emit Reject(373=14). This cell pins fixpp's lenient behavior.
TEST_F(FixTcCoverageGaps, HeaderFieldsOutOfOrder_MsgTypeNotFirst_Accepted_DivergesFromQuickFix) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(drive_to_active(s));
    ASSERT_EQ(next_inbound(s), 2U);

    // A Heartbeat with MsgSeqNum(34) placed BEFORE MsgType(35) — header order violation.
    std::string body;
    body += field(34, "2");   // 34 before 35 (canonical order is 35 first)
    body += field(35, "0");
    body += field(49, "TW");
    body += field(52, "20240101-00:00:00.000");
    body += field(56, "ISLD");
    (void)feed(s, frame_from_body("FIX.4.2", body));

    EXPECT_EQ(s.state(), fixpp::session::fsm_state::Active)
        << "DIVERGENCE: out-of-order header fields are accepted, session stays Active";
    EXPECT_EQ(next_inbound(s), 3U)
        << "the out-of-order Heartbeat is processed in-sequence (counter advances)";
    EXPECT_EQ(capture.count_msg_type("3"), 0U)
        << "DIVERGENCE: fixpp emits NO Reject for field-order violations (QF emits 373=14)";
}

// ── gap #3 / FIX-TC 15 — non-header BODY fields in arbitrary order → ACCEPTED ──
//
// Same order-independent-parse divergence (B-cov-1). This must shuffle real
// BODY fields, not header/session fields: a Heartbeat carries no body fields, so
// shuffling 49/52/56 (all HEADER fields) would only re-test the header-order cell
// above. Instead use a ResendRequest, whose body fields are BeginSeqNo(7) and
// EndSeqNo(16), and place EndSeqNo(16) BEFORE BeginSeqNo(7). fixpp parses them
// order-independently: the resend range is read correctly and honored — proven by
// the GapFill reply carrying NewSeqNo(36)=101 (= EndSeqNo 100 + 1). A parser that
// enforced body-field order would Reject(35=3) and emit no GapFill.
TEST_F(FixTcCoverageGaps, BodyFieldsArbitraryOrder_Accepted) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(drive_to_active(s));
    ASSERT_EQ(next_inbound(s), 2U);
    const std::size_t before = capture.frames.size();

    // A ResendRequest(35=2, seq=2) with EndSeqNo(16) placed BEFORE BeginSeqNo(7) —
    // body fields in non-canonical order.
    std::string body;
    body += field(35, "2");
    body += field(34, "2");
    body += field(49, "TW");
    body += field(52, "20240101-00:00:00.000");
    body += field(56, "ISLD");
    body += field(16, "100");  // EndSeqNo before BeginSeqNo (canonical is 7 then 16)
    body += field(7, "1");
    (void)feed(s, frame_from_body("FIX.4.2", body));

    EXPECT_EQ(s.state(), fixpp::session::fsm_state::Active)
        << "out-of-order body fields are accepted, session stays Active";
    EXPECT_EQ(next_inbound(s), 3U) << "processed in-sequence (counter advances)";
    EXPECT_EQ(capture.count_msg_type("3"), 0U)
        << "DIVERGENCE: no Reject for out-of-order body fields (QF emits 373=14)";
    // Prove BOTH reversed body fields were actually PARSED (not ignored): the
    // resend range [1..100] was read from the reversed 16/7 and honored → a single
    // GapFill whose gap starts at BeginSeqNo=1 (MsgSeqNum 34=1) and whose
    // NewSeqNo(36)=EndSeqNo+1=101. 34=1 pins BeginSeqNo(7), 36=101 pins EndSeqNo(16).
    ASSERT_EQ(capture.frames.size(), before + 1U) << "exactly one GapFill reply expected";
    const auto& gf = capture.frames.back();
    EXPECT_TRUE(frame_has(gf, "34=1\x01"))
        << "reversed-order BeginSeqNo(7)=1 must be parsed (GapFill starts at seq 1)";
    EXPECT_TRUE(frame_has(gf, "36=101\x01"))
        << "reversed-order EndSeqNo(16)=100 must be parsed (NewSeqNo=101)";
}

// ── gap #10 / FIX-TC validLogonState_MsgTypeLogoutAndLogonAlreadySent ─────────
//
// After WE initiate a graceful logout (state LogoutSent, our Logout emitted,
// awaiting the peer's Logout confirmation), an inbound Logon — or any non-Logout
// frame — is DRAINED: the FSM stays LogoutSent, the inbound seqnum does not
// advance, and no Reject / no new Logon is emitted. Only the peer's Logout(35=5)
// is acted upon (→ Disconnected).
//
// DIVERGENCE (documented, B-cov-2). QuickFIX's
// `validLogonState_MsgTypeLogoutAndLogonAlreadySent_Valid` treats a second Logon
// in this state as "valid" (processed). fixpp silently drains it: a graceful
// logout in progress is not aborted to re-establish. Not a Reject either, so the
// wire is not polluted — the frame is simply ignored.
TEST_F(FixTcCoverageGaps, InboundLogonWhileLogoutSent_Drained_StaysLogoutSent) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(drive_to_active(s));
    const std::uint32_t inbound_before = next_inbound(s);
    const std::size_t logons_before = capture.count_msg_type("A");

    // WE initiate graceful logout → emits Logout, transitions to LogoutSent, then
    // parks awaiting the peer's Logout confirmation (2 s clock-bound timeout).
    auto close_fut = asio::co_spawn(ioc, s.close(fixpp::session::close_mode::graceful),
                                    asio::use_future);
    ioc.run_for(100ms);
    ASSERT_EQ(s.state(), fixpp::session::fsm_state::LogoutSent)
        << "precondition: graceful close emits Logout and enters LogoutSent";

    // Peer sends a Logon while we await its Logout confirmation. NOTE: the frame
    // MUST be a named local that outlives run_for — on_inbound_frame takes a
    // non-owning span and reads it only when the coroutine resumes inside
    // run_for (the caller owns the buffer across the await, as the read-pump does).
    {
        auto logon = make_logon("FIX.4.2", inbound_before, "TW", "ISLD");
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(logon), asio::use_future);
        ioc.run_for(100ms);
        (void)fut.get();
    }

    EXPECT_EQ(s.state(), fixpp::session::fsm_state::LogoutSent)
        << "DIVERGENCE: inbound Logon in LogoutSent is drained — graceful logout not aborted";
    EXPECT_EQ(next_inbound(s), inbound_before)
        << "a drained frame must NOT advance the inbound seqnum";
    EXPECT_EQ(capture.count_msg_type("A"), logons_before)
        << "no new Logon emitted in response (session is NOT re-established)";
    EXPECT_EQ(capture.count_msg_type("3"), 0U) << "no Reject emitted (drained, not rejected)";

    // Drain the parked close() coroutine cleanly: let the 2 s logout timeout fire.
    clock->advance(std::chrono::seconds{3});
    ioc.run_for(200ms);
    (void)close_fut.get();
}

// ── gap #4 / FIX-TC RejectResentMessage (QF C++ resendRequest_AResentMessage…, ─
//    QFJ testResendSeqWithReject) — a malformed RESENT frame during the
//    resend-receive window → Reject(35=3), session stays Active, recovery
//    continues. CONFORMANT (no divergence). Characterized 2026-06-19.
//
// This is the resend-RECEIVE counterpart to test_inbound_poss_dup_validation
// ArmC (which rejects a missing-122 PossDup in steady state): here the bad frame
// arrives while WE are in AwaitingResend (we sent a ResendRequest and the peer is
// replaying our requested range). The question gap #4 asks is whether a Reject on
// a resent frame ABORTS recovery or lets it continue. Observed: it continues.
//
// Frame choice (discriminating): the malformed resent frame is at seq=2 — exactly
// the gap-begin / first missing seqnum, INSIDE the requested range — and carries
// PossDupFlag(43)=Y. Its only defect is the MISSING OrigSendingTime(122). So it is
// a legitimate-as-a-resend frame whose lone fault is the format error under test:
// it is NOT rejected for being out-of-sequence or a duplicate (which would measure
// the wrong path) — it reaches the PossDup validation and earns 373=1/371=122.
TEST_F(FixTcCoverageGaps, RejectResentMessage_DuringResend_RejectsAndContinues) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(drive_to_active(s));
    ASSERT_EQ(next_inbound(s), 2U) << "precondition: next-expected-inbound = 2";

    // Drive into AwaitingResend: peer skips [2..4] and sends a Heartbeat at seq=5
    // (too high) → fixpp emits a ResendRequest(35=2) for the gap and stays Active.
    (void)feed(s, make_fix_frame("FIX.4.2", "0", /*seq=*/5, "TW", "ISLD"));
    ASSERT_EQ(s.state(), fixpp::session::fsm_state::Active);
    ASSERT_EQ(next_inbound(s), 2U) << "the too-high gap does not advance the inbound seqnum";
    ASSERT_GE(capture.count_msg_type("2"), 1U) << "precondition: a ResendRequest(35=2) was sent";

    // The peer begins replaying. The FIRST resent frame (seq=2, 43=Y) is MALFORMED:
    // it is missing the required OrigSendingTime(122).
    const std::size_t before = capture.frames.size();
    (void)feed(s, make_fix_frame("FIX.4.2", "0", /*seq=*/2, "TW", "ISLD", "43=Y\x01"));

    // (a) Exactly ONE frame emitted in response, and it is a Reject(35=3) — bind
    // the asserted reason fields to THAT single frame so the witness can't be
    // satisfied by an unrelated emitted frame.
    ASSERT_EQ(capture.frames.size(), before + 1U)
        << "a malformed resent frame must elicit exactly one reply (the Reject)";
    const auto& rj = capture.frames.back();
    EXPECT_TRUE(frame_has(rj, "35=3\x01")) << "the reply must be a session-level Reject(35=3)";
    EXPECT_TRUE(frame_has(rj, "371=122\x01")) << "Reject RefTagID(371) must point at OrigSendingTime";
    EXPECT_TRUE(frame_has(rj, "373=1\x01")) << "Reject SessionRejectReason(373) must be RequiredTagMissing";
    EXPECT_TRUE(frame_has(rj, "45=2\x01")) << "Reject RefSeqNum(45) must be the bad resent frame's seq (2)";

    // (b) The session does NOT abort recovery: it stays Active (not Disconnected),
    // and the rejected frame did NOT fill the gap (seqnum not advanced — strictly
    // in-order; fixpp will not skip the still-missing seqnum).
    EXPECT_EQ(s.state(), fixpp::session::fsm_state::Active)
        << "RejectResentMessage: a bad resent frame must not tear the session down";
    EXPECT_EQ(next_inbound(s), 2U)
        << "the rejected resent frame must not advance past the still-missing seqnum";

    // (c) Recovery CONTINUES: a corrected re-send of the same seqnum (43=Y, 122
    // present) is accepted and the gap progresses — the earlier Reject did not
    // poison the resend window.
    const std::size_t after_reject = capture.frames.size();
    (void)feed(s, make_fix_frame("FIX.4.2", "0", /*seq=*/2, "TW", "ISLD",
                                 "43=Y\x01""122=20240101-00:00:00.000\x01"));
    EXPECT_EQ(next_inbound(s), 3U)
        << "a corrected resent frame is accepted after the Reject — recovery continues";
    EXPECT_EQ(capture.frames.size(), after_reject)
        << "the accepted resent frame is silent (no Reject, no admin emit)";
}

// ── gap #3 / FIX-TC 20_SimultaneousResendRequest (QFJ testSimultaneousResend… ──
//    Requests) — both sides exchange ResendRequests concurrently. CONFORMANT
//    (no divergence). Characterized 2026-06-19.
//
// The interleaving under test: fixpp has sent ITS OWN ResendRequest (it is in
// AwaitingResend, awaiting the peer's replay of our gap) when the peer ALSO sends
// US a ResendRequest (the peer has its own gap). fixpp must service the peer's
// ResendRequest (replay / GapFill our outbound) WHILE its own resend is still
// outstanding — the ResendState carries an outbound_replay_cursor for exactly this.
//
// Observed: fixpp services the counter-request (emits the storeless GapFill reply),
// stays Active, and BOTH recovery directions complete — no Reject, no Logout, no
// deadlock/confusion between the two outstanding resends.
TEST_F(FixTcCoverageGaps, SimultaneousResendRequest_ServicesPeerWhileAwaitingOwn) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(drive_to_active(s));
    ASSERT_EQ(next_inbound(s), 2U) << "precondition: next-expected-inbound = 2";

    // Step 1 — OUR resend goes outstanding: a too-high inbound (seq=5) makes fixpp
    // emit ITS OWN ResendRequest(35=2) for the [2..] gap and enter AwaitingResend.
    (void)feed(s, make_fix_frame("FIX.4.2", "0", /*seq=*/5, "TW", "ISLD"));
    ASSERT_EQ(s.state(), fixpp::session::fsm_state::Active);
    ASSERT_EQ(next_inbound(s), 2U) << "our gap is open; inbound seqnum not advanced";
    ASSERT_GE(capture.count_msg_type("2"), 1U) << "precondition: OUR ResendRequest is outstanding";

    // Step 2 — the peer counter-requests WHILE our resend is outstanding. The frame
    // arrives at seq=2 (the peer's in-sequence next send = our gap-begin), so it both
    // fills our first gap slot AND carries a ResendRequest([1..3]) payload fixpp must
    // service. (Storeless fixture → the reply collapses to a SequenceReset-GapFill.)
    const std::size_t before = capture.frames.size();
    (void)feed(s, make_resend_request("FIX.4.2", /*seq=*/2, "TW", "ISLD", /*begin=*/1, /*end=*/3));

    // fixpp services the counter-request even though its OWN resend is still open:
    // exactly one reply, a SequenceReset-GapFill(35=4, 123=Y) with NewSeqNo(36)=end+1=4.
    ASSERT_EQ(capture.frames.size(), before + 1U)
        << "the peer ResendRequest must be serviced with exactly one reply mid-resend";
    const auto& reply = capture.frames.back();
    EXPECT_TRUE(frame_has(reply, "35=4\x01")) << "reply to the counter-request is a SequenceReset(35=4)";
    EXPECT_TRUE(frame_has(reply, "123=Y\x01")) << "storeless reply is a GapFill (123=Y)";
    EXPECT_TRUE(frame_has(reply, "36=4\x01")) << "GapFill NewSeqNo(36)=EndSeqNo+1=4 ties it to [1..3]";
    EXPECT_EQ(s.state(), fixpp::session::fsm_state::Active)
        << "servicing a counter-ResendRequest mid-resend must not tear the session down";
    EXPECT_EQ(next_inbound(s), 3U)
        << "the seq=2 frame also filled our first gap slot — our recovery progressed";

    // Step 3 — the peer finishes replaying OUR requested gap (seq 3,4 PossDup) → our
    // own recovery completes too. Both outstanding resends resolve; still Active.
    const std::size_t after_reply = capture.frames.size();
    (void)feed(s, make_fix_frame("FIX.4.2", "0", 3, "TW", "ISLD",
                                 "43=Y\x01""122=20240101-00:00:00.000\x01"));
    (void)feed(s, make_fix_frame("FIX.4.2", "0", 4, "TW", "ISLD",
                                 "43=Y\x01""122=20240101-00:00:00.000\x01"));
    EXPECT_EQ(next_inbound(s), 5U) << "our gap [2..4] is fully filled — both directions recovered";
    EXPECT_EQ(s.state(), fixpp::session::fsm_state::Active) << "session stays Active throughout";
    EXPECT_EQ(capture.count_msg_type("3"), 0U) << "no Reject anywhere in the bidirectional exchange";
    EXPECT_EQ(capture.frames.size(), after_reply)
        << "the closing PossDup replays are silent (no further admin emit)";
}

}  // namespace
}  // namespace fixpp::interop::parity
