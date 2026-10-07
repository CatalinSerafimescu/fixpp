// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/inbound_frame_dispositions_c7_witness_test.cpp — 093-inbound-frame-dispositions,
// quickstart Q-37 (tasks.md T102): the C-ABI witnesses for contract C-7 rows 1 to 6,
// driven through the C ABI.
//
// For each row's trigger, a C-ABI engine faces a raw TCP peer
// (inbound_frame_dispositions_capi_support.hpp): an initiator engine whose connection
// the peer accepts, or an acceptor engine the peer connects to. The cell asserts
// afterwards, through C:
//   - fixpp_session_is_established;
//   - the result of fixpp_session_send, and whether the toApp callback registered with
//     fixpp_session_register_send_callback fired for it;
//   - whether the receive callback registered with fixpp_session_register_callback
//     fired for the row's message;
//   - the result of fixpp_session_close, after the peer disconnects and the session
//     drains.
//
// This file and the support header name no symbol 093 adds, so they compile against
// the merge-base tree: to run the rows there, copy both into a copy of that tree and
// register this file alone (tasks.md T102). Row 5 therefore uses the default
// establishment timeout: the setter is new.
//
// Which observers discriminate depends on the trigger's state. fixpp_session_close
// returns FIXPP_ERR_OK for any session that was established once, so for a trigger
// in Active it reads the same whether the session carried on or ended; it
// discriminates only a trigger that comes before the session's first establishment.
// A frame in Active that is processed or disregarded with the session up either way
// is discriminated by the receive callback.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <thread>

#include "fix/c_api/session.h"
#include "inbound_frame_dispositions_capi_support.hpp"

using namespace std::chrono_literals;
using namespace fixpp::capi_test::ifd;

namespace {

constexpr std::uint32_t kHeartBtIntSeconds = 30;

// What the cell reads through C once the row's trigger has run.
struct Observed {
    bool established = false;
    fixpp_error_t send_rc = -1;
    int to_app = 0;
    bool received = false;
    fixpp_error_t close_rc = -1;
};

// Reads the observers in order: fixpp_session_is_established, one fixpp_session_send
// and the toApp invocations it caused, whether the receive callback saw `row_id`; then
// disconnects the peer, waits for the session to drain and closes it.
Observed observe(CInitiator& c, RawAcceptor& peer, std::string_view row_id) {
    Observed o;
    o.established = c.established();
    int const before = c.rec.to_app.load();
    o.send_rc = c.send_order("SENT");
    o.to_app = c.rec.to_app.load() - before;
    o.received = c.rec.received_id(row_id);
    peer.disconnect();
    o.close_rc = c.drain_then_close();
    return o;
}

// The session carried on through the trigger.
void expect_kept_up(Observed const& o) {
    EXPECT_TRUE(o.established) << "fixpp_session_is_established";
    EXPECT_EQ(o.send_rc, FIXPP_ERR_OK) << "fixpp_session_send";
    EXPECT_EQ(o.to_app, 1) << "the toApp callback for that send";
    EXPECT_TRUE(o.received) << "the receive callback for the row's message";
    EXPECT_EQ(o.close_rc, FIXPP_ERR_OK) << "fixpp_session_close";
}

// ── The acceptor rig ────────────────────────────────────────────────────────
//
// A C-ABI acceptor engine (as in Row5_AnAcceptorWhoseLogonWasRefusedIsClosedAtTheDeadline)
// and a peer that connects to it. Every cell on it writes the session's first bytes
// itself, so the first-frame read sees them.

// An acceptor engine over the same configuration as the initiator rows.
struct CAcceptor : CInitiator {
    CAcceptor()
        : CInitiator{0, kHeartBtIntSeconds, {}, record_receive, nullptr, FIXPP_ROLE_ACCEPTOR} {}
};

// Starts `a` and connects `peer` to its bound port.
bool start_and_connect(CAcceptor& a, RawAcceptor& peer) {
    if (!a.start()) return false;
    std::uint16_t const port = a.bound_port();
    return port != 0 && peer.connect(port);
}

// The peer's Logon to the acceptor, MsgSeqNum 1, with `fields` between the header and
// the session-level fields.
std::string peer_logon(std::string const& fields = {},
                       std::uint32_t heartbeat_s = kHeartBtIntSeconds) {
    return frame44("35=A\x01" + peer_header(1) + fields +
                   fix_fields({{98, "0"}, {108, std::to_string(heartbeat_s)}}));
}

// Starts the acceptor engine `a`, connects `peer`, logs on with HeartBtInt(108)
// `heartbeat_s` and reads the acceptor's Logon reply; true once the session is
// established.
bool establish_acceptor(CInitiator& a, RawAcceptor& peer, std::uint32_t heartbeat_s) {
    if (!a.start()) return false;
    std::uint16_t const port = a.bound_port();
    return port != 0 && peer.connect(port) && peer.write(peer_logon({}, heartbeat_s)) &&
           peer.read_logon() && a.wait_established();
}

// A Logon whose third field is not MsgType(35), with `fields` between the header and
// the session-level fields.
std::string peer_logon_35_not_third(std::string const& fields = {}) {
    return frame44(
        fix_fields({{34, "1"}, {35, "A"}}) +
        fix_fields({{49, kPeerCompId}, {52, utc_now_sending_time()}, {56, kEngineCompId}}) +
        fields + fix_fields({{98, "0"}, {108, std::to_string(kHeartBtIntSeconds)}}));
}

// After the peer's first write: the acceptor's Logon reply reaches the peer and the
// session establishes, then an order and a fence are processed.
void expect_acceptor_established_then_kept_up(CAcceptor& a, RawAcceptor& peer,
                                              std::string_view row_id) {
    bool const answered = peer.read_logon();
    bool const est = answered && a.wait_established();
    bool const fenced = est && peer.write(order(2, row_id)) && peer.fence(3, "RA");
    EXPECT_TRUE(answered) << "the acceptor answers with its Logon";
    EXPECT_TRUE(est) << "the well-formed Logon establishes the session";
    EXPECT_TRUE(fenced);
    expect_kept_up(observe(a, peer, row_id));
}

// ── Row 1: a Framer-detected garbled frame in Active is disregarded ──────────

TEST(CapiC7Witness, Row1_AGarbledFrameInActiveIsDisregarded) {
    RawAcceptor peer;
    CInitiator c{peer.port(), kHeartBtIntSeconds};
    ASSERT_TRUE(establish(c, peer, kHeartBtIntSeconds)) << "setup";

    // Junk before a frame start (TC 2d), then an order.
    bool const fenced = peer.write("GARBLE" + order(2, "ROW1")) && peer.fence(3, "R1");
    EXPECT_TRUE(fenced) << "the session answers a TestRequest after the garble";
    expect_kept_up(observe(c, peer, "ROW1"));
}

// ── Row 2: a frame whose third field is not MsgType(35) is disregarded ───────

// A NewOrderSingle whose third field is MsgSeqNum(34).
std::string order_35_not_third(std::uint32_t seq, std::string_view cl_ord_id) {
    return frame44(
        fix_fields({{34, std::to_string(seq)}, {35, "D"}}) +
        fix_fields({{49, kPeerCompId}, {52, utc_now_sending_time()}, {56, kEngineCompId}}) +
        order_fields(cl_ord_id));
}

// In Active. The disregarded frame consumes no MsgSeqNum, so the fence carries the
// number it carried.
TEST(CapiC7Witness, Row2_A35NotThirdFrameInActiveIsDisregarded) {
    RawAcceptor peer;
    CInitiator c{peer.port(), kHeartBtIntSeconds};
    ASSERT_TRUE(establish(c, peer, kHeartBtIntSeconds)) << "setup";

    bool const fenced = peer.write(order_35_not_third(2, "ROW2")) && peer.fence(2, "R2");
    EXPECT_TRUE(fenced) << "the session answers a TestRequest carrying the disregarded number";
    Observed const o = observe(c, peer, "ROW2");
    EXPECT_TRUE(o.established);
    EXPECT_EQ(o.send_rc, FIXPP_ERR_OK);
    EXPECT_EQ(o.to_app, 1);
    EXPECT_FALSE(o.received) << "the receive callback is not invoked for the disregarded frame";
    EXPECT_EQ(o.close_rc, FIXPP_ERR_OK);
}

// A Logon reply whose third field is not MsgType(35) and which carries a malformed
// tag, then a well-formed Logon reply, in one write: the first is disregarded and the
// second establishes the session.
TEST(CapiC7Witness, Row2_A35NotThirdLogonIsDisregarded) {
    RawAcceptor peer;
    CInitiator c{peer.port(), kHeartBtIntSeconds};
    ASSERT_TRUE(c.start() && peer.accept() && peer.read_logon()) << "setup";

    std::string const bad_logon =
        frame44(fix_fields({{34, "1"}, {35, "A"}}) +
                fix_fields({{49, kPeerCompId}, {52, utc_now_sending_time()}, {56, kEngineCompId}}) +
                "9x9=1\x01" +
                fix_fields({{98, "0"}, {108, std::to_string(kHeartBtIntSeconds)}, {141, "Y"}}));
    ASSERT_TRUE(peer.write(bad_logon + logon_reply(kHeartBtIntSeconds)));
    bool const est = c.wait_established();
    bool const fenced = est && peer.write(order(2, "ROW2L")) && peer.fence(3, "R2L");
    EXPECT_TRUE(est) << "the well-formed Logon reply establishes the session";
    EXPECT_TRUE(fenced);
    expect_kept_up(observe(c, peer, "ROW2L"));
}

// ── Row 3: a frame of L split at the carry's edge is admitted ───────────────
//
// A Heartbeat padded with one Text(58) field to exactly L = 65536 bytes (few fields,
// so any parse holds it). The peer writes all but its last bytes, waits for the
// session to read them, then writes the rest with an order, which arrives as one
// read: the session holds most of the frame and must take the rest and the order in
// one feed.
// `c` is established with `peer`.
void row3_split_cell(CInitiator& c, RawAcceptor& peer, std::string_view row_id) {
    constexpr std::size_t kL = kDefaultLimit;
    // Any tail from one byte up to a read less the order: the rest and the order then
    // arrive as one read, which a carry of exactly L cannot take beside the bytes it holds.
    constexpr std::size_t kTail = 16;
    std::string const f = frame44_of_size("35=0\x01" + peer_header(2), kL, "58", Pad::one_field);
    ASSERT_EQ(f.size(), kL);
    bool const first = peer.write(f.substr(0, kL - kTail));
    std::this_thread::sleep_for(200ms);
    bool const fenced =
        first && peer.write(f.substr(kL - kTail) + order(3, row_id)) && peer.fence(4, "R3");
    EXPECT_TRUE(fenced) << "the session answers a TestRequest after the frame of L";
    expect_kept_up(observe(c, peer, row_id));
}

TEST(CapiC7Witness, Row3_AFrameOfLSplitAtTheCarryEdgeIsAdmitted) {
    RawAcceptor peer;
    CInitiator c{peer.port(), kHeartBtIntSeconds};
    ASSERT_TRUE(establish(c, peer, kHeartBtIntSeconds)) << "setup";
    row3_split_cell(c, peer, "ROW3");
}

// ── Row 4: a dense frame of L parses ────────────────────────────────────────
//
// An order of exactly L bytes whose padding is "2=<SOH>" fields (tag 2 is not a
// NewOrderSingle field, so each is an unknown field): the densest layout. It is
// written alone, so the carry holds nothing past it.
// `c` is established with `peer`.
void row4_dense_cell(CInitiator& c, RawAcceptor& peer, std::string_view row_id) {
    constexpr std::size_t kL = kDefaultLimit;
    std::string const f =
        frame44_of_size("35=D\x01" + peer_header(2) + order_fields(row_id), kL, "2", Pad::dense);
    ASSERT_EQ(f.size(), kL);
    bool const written = peer.write(f);
    std::this_thread::sleep_for(200ms);
    bool const fenced = written && peer.fence(3, "R4");
    EXPECT_TRUE(fenced) << "the session answers a TestRequest after the dense frame";
    expect_kept_up(observe(c, peer, row_id));
}

TEST(CapiC7Witness, Row4_ADenseFrameOfLIsDelivered) {
    RawAcceptor peer;
    CInitiator c{peer.port(), kHeartBtIntSeconds};
    ASSERT_TRUE(establish(c, peer, kHeartBtIntSeconds)) << "setup";
    row4_dense_cell(c, peer, "ROW4");
}

// ── Row 5: a pre-Active connection closes at the establishment deadline ──────
//
// The initiator's peer answers the Logon only after T, the default timeout (the
// setter is new, so this file does not use it). The connection is closed at T, so the
// late answer never establishes the session.
TEST(CapiC7Witness, Row5_ASlowPeerIsClosedAtTheDeadline) {
    constexpr std::chrono::milliseconds kLate = kDefaultLogonTimeout + 2000ms;
    RawAcceptor peer;
    CInitiator c{peer.port(), kHeartBtIntSeconds};
    auto const t0 = std::chrono::steady_clock::now();
    ASSERT_TRUE(c.start() && peer.accept() && peer.read_logon()) << "setup";

    bool const closed = peer.wait_eof(t0 + kLate);
    if (!closed) {
        (void)peer.write(logon_reply(kHeartBtIntSeconds));
        (void)c.wait_established();
        (void)(peer.write(order(2, "ROW5")) && peer.fence(3, "R5"));
    }
    EXPECT_TRUE(closed) << "the engine closed the connection before the late answer";
    Observed const o = observe(c, peer, "ROW5");
    EXPECT_FALSE(o.established);
    EXPECT_EQ(o.send_rc, FIXPP_ERR_SESSION_INVALID_STATE);
    EXPECT_EQ(o.to_app, 0);
    EXPECT_FALSE(o.received);
    EXPECT_EQ(o.close_rc, FIXPP_ERR_THREAD_SESSION_LIFECYCLE) << "the session never established";
}

// ── Row 6: liveness refreshes on more frames ────────────────────────────────
//
// HeartBtInt is 1 s. For several intervals the peer sends only frames of one class that
// takes an early return in the session's arm, and answers no TestRequest. The liveness
// loop sends a TestRequest after an interval without a refresh and ends the session
// after one more (run_liveness_loop, src/session/session.cpp); every one of these frames
// refreshes, so neither happens. `traffic(seq)` builds the class's frame; `advances`
// says whether it takes a MsgSeqNum (then the next frame carries the next one). `role` is
// the C-ABI engine's.
void row6_cell(std::function<std::string(std::uint32_t)> const& traffic, bool advances,
               std::string_view row_id, fixpp_session_role role = FIXPP_ROLE_INITIATOR) {
    constexpr std::uint32_t kShortHeartBtInt = 1;
    constexpr std::chrono::milliseconds kInterval{kShortHeartBtInt * 1000};
    constexpr std::chrono::milliseconds kTraffic = 4 * kInterval;
    constexpr std::chrono::milliseconds kEvery = kInterval / 4;
    bool const acceptor = role == FIXPP_ROLE_ACCEPTOR;
    RawAcceptor peer;
    CInitiator c{acceptor ? std::uint16_t{0} : peer.port(),
                 kShortHeartBtInt,
                 {},
                 record_receive,
                 nullptr,
                 role};
    ASSERT_TRUE(acceptor ? establish_acceptor(c, peer, kShortHeartBtInt)
                         : establish(c, peer, kShortHeartBtInt))
        << "setup";

    std::uint32_t seq = 2;
    auto const until = std::chrono::steady_clock::now() + kTraffic;
    bool written = true;
    while (written && std::chrono::steady_clock::now() < until) {
        written = peer.write(traffic(seq));
        if (advances) ++seq;
        std::this_thread::sleep_for(kEvery);
    }
    bool const fenced = written && peer.write(order(seq, row_id)) && peer.fence(seq + 1U, "R6");
    EXPECT_TRUE(written);
    EXPECT_TRUE(fenced) << "the session answers a TestRequest after the liveness-only traffic";
    expect_kept_up(observe(c, peer, row_id));
}

// Heartbeats below the expected MsgSeqNum (too low, tolerated for a Heartbeat).
std::string too_low_heartbeat(std::uint32_t /*seq*/) { return heartbeat(1); }

TEST(CapiC7Witness, Row6_LivenessOnlyTrafficKeepsTheSessionUp) {
    row6_cell(too_low_heartbeat, false, "ROW6");
}

// Orders below the expected MsgSeqNum carrying PossDupFlag(43)=Y (too low, ignored).
std::string too_low_possdup_order(std::uint32_t /*seq*/) {
    std::string const ts = utc_now_sending_time();
    return frame44(
        "35=D\x01" +
        fix_fields(
            {{34, "1"}, {43, "Y"}, {49, kPeerCompId}, {52, ts}, {56, kEngineCompId}, {122, ts}}) +
        order_fields("DUP6"));
}

TEST(CapiC7Witness, Row6_TooLowPossDupTrafficKeepsTheSessionUp) {
    row6_cell(too_low_possdup_order, false, "ROW6D");
}

// In-sequence session Rejects(35=3).
std::string inbound_reject(std::uint32_t seq) {
    return frame44("35=3\x01" + peer_header(seq) + fix_fields({{45, "1"}, {58, "x"}}));
}

TEST(CapiC7Witness, Row6_InboundRejectTrafficKeepsTheSessionUp) {
    row6_cell(inbound_reject, true, "ROW6R");
}

// In-sequence SequenceReset-GapFills, each to the next number.
std::string gap_fill(std::uint32_t seq) {
    return frame44("35=4\x01" + peer_header(seq) +
                   fix_fields({{123, "Y"}, {36, std::to_string(seq + 1U)}}));
}

TEST(CapiC7Witness, Row6_GapFillTrafficKeepsTheSessionUp) { row6_cell(gap_fill, true, "ROW6G"); }

// ── Row 1: the other garble shapes, and a garble before Active ──────────────
//
// `garbled` stands in for the peer's MsgSeqNum 2; a well-formed order with the same
// number follows it in one write, then a fence. The garble is disregarded, so the order
// is delivered and the session carries on.
void row1_cell(std::string const& garbled, std::string_view row_id) {
    RawAcceptor peer;
    CInitiator c{peer.port(), kHeartBtIntSeconds};
    ASSERT_TRUE(establish(c, peer, kHeartBtIntSeconds)) << "setup";
    ASSERT_FALSE(garbled.empty()) << "setup";
    bool const fenced = peer.write(garbled + order(2, row_id)) && peer.fence(3, "R1x");
    EXPECT_TRUE(fenced) << "the session answers a TestRequest after the garble";
    Observed const o = observe(c, peer, row_id);
    expect_kept_up(o);
    EXPECT_FALSE(c.rec.received_id("BAD")) << "the garbled frame reached the receive callback";
}

std::string bad_order_body() { return "35=D\x01" + peer_header(2) + order_fields("BAD"); }

TEST(CapiC7Witness, Row1_AWrongCheckSumIsDisregarded) {
    std::string f = order(2, "BAD");
    auto const at = f.rfind("10=");
    std::string garbled;
    if (at != std::string::npos) {
        auto const sum = static_cast<unsigned>(std::stoi(f.substr(at + 3, 3)));
        char wrong[4];
        std::snprintf(wrong, sizeof(wrong), "%03u", (sum + 1U) % 256U);
        garbled = f.replace(at + 3, 3, wrong, 3);
    }
    row1_cell(garbled, "ROW1C");
}

// A BodyLength(9) short of the body, so CheckSum(10) is not where it points.
TEST(CapiC7Witness, Row1_AWrongBodyLengthIsDisregarded) {
    std::string const body = bad_order_body();
    row1_cell(frame_raw("FIX.4.4", std::to_string(body.size() - 5U), body), "ROW1B");
}

// A BeginString(8) value longer than every supported one: it was framed and handled
// as a BeginString mismatch.
TEST(CapiC7Witness, Row1_ABeginStringOverItsCapIsDisregarded) {
    std::string const body = bad_order_body();
    row1_cell(frame_raw("FIX.4.4.LONGER.THAN.ANY.SUPPORTED", std::to_string(body.size()), body),
              "ROW1S");
}

// A BodyLength(9) whose digit run, zero-padded, is longer than the digit cap: it was
// framed and processed. The disregarded frame consumes no MsgSeqNum, so the fence
// carries the number it carried, and the receive callback is the discriminator.
TEST(CapiC7Witness, Row1_ABodyLengthDigitRunOverItsCapIsDisregarded) {
    RawAcceptor peer;
    CInitiator c{peer.port(), kHeartBtIntSeconds};
    ASSERT_TRUE(establish(c, peer, kHeartBtIntSeconds)) << "setup";
    std::string const body = "35=D\x01" + peer_header(2) + order_fields("ROW1D");
    std::string const length = std::to_string(body.size());
    std::string const padded = std::string(20U - length.size(), '0') + length;
    bool const fenced = peer.write(frame_raw("FIX.4.4", padded, body)) && peer.fence(2, "R1D");
    EXPECT_TRUE(fenced) << "the session answers a TestRequest carrying the disregarded number";
    Observed const o = observe(c, peer, "ROW1D");
    EXPECT_TRUE(o.established);
    EXPECT_EQ(o.send_rc, FIXPP_ERR_OK);
    EXPECT_EQ(o.to_app, 1);
    EXPECT_FALSE(o.received) << "the receive callback is not invoked for the disregarded frame";
    EXPECT_EQ(o.close_rc, FIXPP_ERR_OK);
}

// Before Active: junk ahead of the Logon reply, which then establishes the session.
TEST(CapiC7Witness, Row1_AGarbleBeforeTheLogonReplyIsDisregarded) {
    RawAcceptor peer;
    CInitiator c{peer.port(), kHeartBtIntSeconds};
    ASSERT_TRUE(c.start() && peer.accept() && peer.read_logon()) << "setup";
    ASSERT_TRUE(peer.write("GARBLE" + logon_reply(kHeartBtIntSeconds)));
    bool const est = c.wait_established();
    bool const fenced = est && peer.write(order(2, "ROW1L")) && peer.fence(3, "R1L");
    EXPECT_TRUE(est) << "the Logon reply after the garble establishes the session";
    EXPECT_TRUE(fenced);
    expect_kept_up(observe(c, peer, "ROW1L"));
}

// ── Row 2: the other 35-not-third Logon shapes ──────────────────────────────

// A well-formed Logon reply whose third field is not MsgType(35), alone: it was
// accepted; now nothing establishes the session.
TEST(CapiC7Witness, Row2_AWellFormed35NotThirdLogonReplyDoesNotEstablish) {
    constexpr std::chrono::milliseconds kSettle{1000};  // well below the default T
    RawAcceptor peer;
    CInitiator c{peer.port(), kHeartBtIntSeconds};
    ASSERT_TRUE(c.start() && peer.accept() && peer.read_logon()) << "setup";
    ASSERT_TRUE(peer.write(
        frame44(fix_fields({{34, "1"}, {35, "A"}}) +
                fix_fields({{49, kPeerCompId}, {52, utc_now_sending_time()}, {56, kEngineCompId}}) +
                fix_fields({{98, "0"}, {108, std::to_string(kHeartBtIntSeconds)}, {141, "Y"}}))));
    EXPECT_FALSE(c.wait_established(kSettle)) << "the 35-not-third Logon reply established it";
    Observed const o = observe(c, peer, "none");
    EXPECT_FALSE(o.established);
    EXPECT_EQ(o.send_rc, FIXPP_ERR_SESSION_INVALID_STATE);
    EXPECT_EQ(o.to_app, 0);
    EXPECT_EQ(o.close_rc, FIXPP_ERR_THREAD_SESSION_LIFECYCLE) << "the session never established";
}

// A 35-not-third Logon reply with 091's malformed Length+Data count (RawDataLength(95)
// counting past its RawData(96)), then a well-formed one: the first was refused.
TEST(CapiC7Witness, Row2_A35NotThirdLogonWithAMalformedCountIsDisregarded) {
    RawAcceptor peer;
    CInitiator c{peer.port(), kHeartBtIntSeconds};
    ASSERT_TRUE(c.start() && peer.accept() && peer.read_logon()) << "setup";
    std::string const bad_logon =
        frame44(fix_fields({{34, "1"}, {35, "A"}}) +
                fix_fields({{49, kPeerCompId}, {52, utc_now_sending_time()}, {56, kEngineCompId}}) +
                fix_fields({{95, "2"}, {96, "x"}}) +
                fix_fields({{98, "0"}, {108, std::to_string(kHeartBtIntSeconds)}, {141, "Y"}}));
    ASSERT_TRUE(peer.write(bad_logon + logon_reply(kHeartBtIntSeconds)));
    bool const est = c.wait_established();
    bool const fenced = est && peer.write(order(2, "ROW2C")) && peer.fence(3, "R2C");
    EXPECT_TRUE(est) << "the well-formed Logon reply establishes the session";
    EXPECT_TRUE(fenced);
    expect_kept_up(observe(c, peer, "ROW2C"));
}

// ── Row 3: a frame over the limit ends the connection at its header ─────────
//
// The peer writes only the header of a frame whose BodyLength(9) is over the 64 KiB
// limit. The connection is closed without the body; the bound sits far below the
// heartbeat interval and the establishment timeout, the other ways it can close.
constexpr std::chrono::milliseconds kOverLimitBound{2000};

std::string over_limit_header() {
    return "8=FIX.4.4\x01"
           "9=70000\x01"
           "35=0\x01"
           "34=2\x01";
}

TEST(CapiC7Witness, Row3_AnOverLimitBodyLengthInActiveEndsTheConnection) {
    RawAcceptor peer;
    CInitiator c{peer.port(), kHeartBtIntSeconds};
    ASSERT_TRUE(establish(c, peer, kHeartBtIntSeconds)) << "setup";
    auto const t0 = std::chrono::steady_clock::now();
    bool const closed = peer.write(over_limit_header()) && peer.wait_eof(t0 + kOverLimitBound);
    EXPECT_TRUE(closed) << "the connection stayed open after an over-limit BodyLength";
    Observed const o = observe(c, peer, "none");
    EXPECT_FALSE(o.established);
    EXPECT_EQ(o.send_rc, FIXPP_ERR_SESSION_INVALID_STATE);
    EXPECT_EQ(o.to_app, 0);
    EXPECT_EQ(o.close_rc, FIXPP_ERR_OK) << "established once";
}

TEST(CapiC7Witness, Row3_AnOverLimitBodyLengthBeforeActiveEndsTheConnection) {
    RawAcceptor peer;
    CInitiator c{peer.port(), kHeartBtIntSeconds};
    ASSERT_TRUE(c.start() && peer.accept() && peer.read_logon()) << "setup";
    auto const t0 = std::chrono::steady_clock::now();
    bool const closed = peer.write(over_limit_header()) && peer.wait_eof(t0 + kOverLimitBound);
    EXPECT_TRUE(closed) << "the connection stayed open after an over-limit BodyLength";
    Observed const o = observe(c, peer, "none");
    EXPECT_FALSE(o.established);
    EXPECT_EQ(o.send_rc, FIXPP_ERR_SESSION_INVALID_STATE);
    EXPECT_EQ(o.close_rc, FIXPP_ERR_THREAD_SESSION_LIFECYCLE);
}

// In LogoutSent: fixpp_session_close sends the Logout and blocks its caller until the
// close completes, so a helper thread makes the call and the peer never answers the
// Logout. The C ABI cannot set the logout timeout, so the session runs the default,
// spelled out here: a close that only the grace timer brings lands at it. The bound is
// a quarter of it, counted from the call. The cell then waits past the bound as well,
// so a late close reads as late rather than as none.
constexpr std::chrono::milliseconds kDefaultLogoutTimeout{2000};

// `c` is established with `peer`; the peer writes `bytes` once it has read the Logout.
// The helper thread stamps the band's origin immediately before its call and the time
// the call returned immediately after it; the peer stamps EOF in the completion handler
// of the read that returns it. The band is judged on those stamps: a late thread start
// moves the origin with the call, and a descheduled stamping thread can only make a
// completion stamp later, never earlier, so it cannot bring a late completion into the
// band.
// Waiting for the helper thread's start and for the Logout uses a setup budget under
// the session's HeartBtInt, not the band.
void logout_sent_cell(CInitiator& c, RawAcceptor& peer, std::string const& bytes) {
    using clock = std::chrono::steady_clock;
    auto const bound = kDefaultLogoutTimeout / 4;
    constexpr std::chrono::milliseconds kSetupBudget{10000};
    std::atomic<bool> calling{false};
    fixpp_error_t close_rc = FIXPP_ERR_OK;
    clock::time_point t0{};
    clock::time_point returned_at{};
    std::thread closer{[&] {
        t0 = clock::now();
        calling.store(true, std::memory_order_release);
        close_rc = fixpp_session_close(c.session);
        returned_at = clock::now();
    }};
    auto const start_deadline = clock::now() + kSetupBudget;
    while (!calling.load(std::memory_order_acquire) && clock::now() < start_deadline) {
        std::this_thread::sleep_for(1ms);
    }
    bool const closer_started = calling.load(std::memory_order_acquire);
    if (!closer_started) closer.join();
    ASSERT_TRUE(closer_started) << "the closing thread made no call within " << kSetupBudget.count()
                                << " ms";
    bool const logout =
        peer.read_until([](std::string const& f) { return RawAcceptor::has_field(f, "35=5"); },
                        kSetupBudget)
            .has_value();
    bool const eof = logout && peer.write(bytes) && peer.wait_eof(t0 + 2 * kDefaultLogoutTimeout);
    closer.join();
    auto const ms = [&](clock::time_point at) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(at - t0).count();
    };
    auto const eof_at = peer.eof_at();
    bool const closed = eof && eof_at.has_value() && *eof_at < t0 + bound;
    bool const returned_in_bound = returned_at < t0 + bound;
    EXPECT_TRUE(logout) << "the engine's Logout";
    EXPECT_TRUE(closed) << "the connection ends within a quarter of the logout timeout; EOF at "
                        << (eof_at ? ms(*eof_at) : -1) << " ms";
    EXPECT_TRUE(returned_in_bound)
        << "fixpp_session_close returns within a quarter of the logout timeout; returned at "
        << ms(returned_at) << " ms";
    EXPECT_TRUE(eof) << "the connection ends at the latest at the logout timeout";
    EXPECT_EQ(close_rc, FIXPP_ERR_OK) << "established once";
}

TEST(CapiC7Witness, Row3_AnOverLimitBodyLengthInLogoutSentEndsTheBlockingClose) {
    RawAcceptor peer;
    CInitiator c{peer.port(), kHeartBtIntSeconds};
    ASSERT_TRUE(establish(c, peer, kHeartBtIntSeconds)) << "setup";
    logout_sent_cell(c, peer, over_limit_header());
}

// ── Row 5: an acceptor whose Logon was refused is closed at the deadline ────
//
// The C-ABI engine is the acceptor; the peer connects and sends a Logon without
// HeartBtInt(108), which the session refuses, leaving it in Disconnected with its
// transport open. The connection is closed at T, the default timeout.
TEST(CapiC7Witness, Row5_AnAcceptorWhoseLogonWasRefusedIsClosedAtTheDeadline) {
    constexpr std::chrono::milliseconds kLate = kDefaultLogonTimeout + 2000ms;
    RawAcceptor peer;
    CInitiator a{0, kHeartBtIntSeconds, {}, record_receive, nullptr, FIXPP_ROLE_ACCEPTOR};
    ASSERT_TRUE(a.start()) << "setup";
    std::uint16_t const port = a.bound_port();
    ASSERT_NE(port, 0U) << "setup";
    ASSERT_TRUE(peer.connect(port)) << "setup";
    auto const t0 = std::chrono::steady_clock::now();
    ASSERT_TRUE(peer.write(frame44("35=A\x01" + peer_header(1) + fix_fields({{98, "0"}}))));
    bool const closed = peer.wait_eof(t0 + kLate);
    EXPECT_TRUE(closed) << "the refused session's connection stayed open past T";
    Observed const o = observe(a, peer, "none");
    EXPECT_FALSE(o.established);
    EXPECT_EQ(o.send_rc, FIXPP_ERR_SESSION_INVALID_STATE);
    EXPECT_EQ(o.close_rc, FIXPP_ERR_THREAD_SESSION_LIFECYCLE);
}

// ── Rows 1 to 3 on the acceptor's first frame, and row 3 in Disconnected ────

// Row 1: junk ahead of the peer's Logon, in the acceptor's first read.
TEST(CapiC7Witness, Row1_AGarbleBeforeTheAcceptorsLogonIsDisregarded) {
    RawAcceptor peer;
    CAcceptor a;
    ASSERT_TRUE(start_and_connect(a, peer)) << "setup";
    ASSERT_TRUE(peer.write("GARBLE" + peer_logon()));
    expect_acceptor_established_then_kept_up(a, peer, "ROW1A");
}

// Row 2: a Logon whose third field is not MsgType(35) and which carries a malformed
// tag, then a well-formed Logon, in one write.
TEST(CapiC7Witness, Row2_AnAcceptorDisregardsA35NotThirdLogon) {
    RawAcceptor peer;
    CAcceptor a;
    ASSERT_TRUE(start_and_connect(a, peer)) << "setup";
    ASSERT_TRUE(peer.write(peer_logon_35_not_third("9x9=1\x01") + peer_logon()));
    expect_acceptor_established_then_kept_up(a, peer, "ROW2A");
}

// Row 2: the same with 091's malformed Length+Data count (RawDataLength(95) counting
// past its RawData(96)) in place of the malformed tag.
TEST(CapiC7Witness, Row2_AnAcceptorDisregardsA35NotThirdLogonWithAMalformedCount) {
    RawAcceptor peer;
    CAcceptor a;
    ASSERT_TRUE(start_and_connect(a, peer)) << "setup";
    ASSERT_TRUE(
        peer.write(peer_logon_35_not_third(fix_fields({{95, "2"}, {96, "x"}})) + peer_logon()));
    expect_acceptor_established_then_kept_up(a, peer, "ROW2AC");
}

// Row 2: a well-formed Logon whose third field is not MsgType(35), alone: nothing
// establishes the session.
TEST(CapiC7Witness, Row2_AnAcceptorsWellFormed35NotThirdLogonDoesNotEstablish) {
    constexpr std::chrono::milliseconds kSettle{1000};  // well below the default T
    RawAcceptor peer;
    CAcceptor a;
    ASSERT_TRUE(start_and_connect(a, peer)) << "setup";
    ASSERT_TRUE(peer.write(peer_logon_35_not_third()));
    EXPECT_FALSE(a.wait_established(kSettle)) << "the 35-not-third Logon established it";
    Observed const o = observe(a, peer, "none");
    EXPECT_FALSE(o.established);
    EXPECT_EQ(o.send_rc, FIXPP_ERR_SESSION_INVALID_STATE);
    EXPECT_EQ(o.to_app, 0);
    EXPECT_EQ(o.close_rc, FIXPP_ERR_THREAD_SESSION_LIFECYCLE) << "the session never established";
}

// Row 3: the acceptor's first frame announces a BodyLength(9) over the 64 KiB limit; the
// peer writes only its header. kOverLimitBound sits below the acceptor's first-frame read
// deadline, so a reader that waits for the body instead closes outside it.
TEST(CapiC7Witness, Row3_AnOverLimitBodyLengthInTheAcceptorsFirstFrameEndsTheConnection) {
    RawAcceptor peer;
    CAcceptor a;
    ASSERT_TRUE(start_and_connect(a, peer)) << "setup";
    auto const t0 = std::chrono::steady_clock::now();
    bool const closed = peer.write(
                            "8=FIX.4.4\x01"
                            "9=70000\x01"
                            "35=A\x01"
                            "34=1\x01") &&
                        peer.wait_eof(t0 + kOverLimitBound);
    EXPECT_TRUE(closed) << "the connection stayed open after an over-limit BodyLength";
    // A connection refused at its first frame builds no session, so there is no drain to
    // wait for: the handle is closed directly once the peer has gone.
    EXPECT_FALSE(a.established());
    int const before = a.rec.to_app.load();
    EXPECT_EQ(a.send_order("SENT"), FIXPP_ERR_SESSION_INVALID_STATE);
    EXPECT_EQ(a.rec.to_app.load() - before, 0);
    peer.disconnect();
    EXPECT_EQ(fixpp_session_close(a.session), FIXPP_ERR_THREAD_SESSION_LIFECYCLE);
}

// Row 3 in Disconnected: the rig of Row5_AnAcceptorWhoseLogonWasRefusedIsClosedAtTheDeadline,
// whose refused Logon leaves the session in Disconnected with its transport open, until
// the establishment deadline T closes it. C cannot see the refusal land, so the peer
// waits a settle, during which the connection must stay open, then writes the header of
// a frame over the limit in a write of its own. Both windows count from before the
// connect, so from no later than the deadline's arming, and together they end before T:
// the deadline close cannot satisfy the cell.
TEST(CapiC7Witness, Row3_AnOverLimitBodyLengthInDisconnectedEndsTheConnection) {
    constexpr std::chrono::milliseconds kSettle = kDefaultLogonTimeout / 5;
    constexpr std::chrono::milliseconds kBound = kDefaultLogonTimeout / 5;
    static_assert(kSettle + kBound < kDefaultLogonTimeout,
                  "the bound must end before the deadline that would otherwise close it");
    RawAcceptor peer;
    CAcceptor a;
    auto const t0 = std::chrono::steady_clock::now();
    ASSERT_TRUE(start_and_connect(a, peer)) << "setup";
    ASSERT_TRUE(peer.write(frame44("35=A\x01" + peer_header(1) + fix_fields({{98, "0"}}))));
    bool const closed_early = peer.wait_eof(t0 + kSettle);
    bool const closed =
        !closed_early && peer.write(over_limit_header()) && peer.wait_eof(t0 + kSettle + kBound);
    EXPECT_FALSE(closed_early) << "the connection closed before the over-limit header";
    EXPECT_TRUE(closed) << "the connection stayed open after an over-limit BodyLength";
    Observed const o = observe(a, peer, "none");
    EXPECT_FALSE(o.established);
    EXPECT_EQ(o.send_rc, FIXPP_ERR_SESSION_INVALID_STATE);
    EXPECT_EQ(o.to_app, 0);
    EXPECT_EQ(o.close_rc, FIXPP_ERR_THREAD_SESSION_LIFECYCLE) << "the session never established";
}

// ── Row 6: the two PossDup Reject classes ───────────────────────────────────
//
// In-sequence orders carrying PossDupFlag(43)=Y whose OrigSendingTime(122) is absent or
// does not parse: each draws a session Reject and takes its MsgSeqNum. One cell per
// class, so a class that does not refresh is not covered by the other's refreshes.
std::string possdup_order(std::uint32_t seq, std::string const& orig_sending_time) {
    return frame44("35=D\x01" +
                   fix_fields({{34, std::to_string(seq)},
                               {43, "Y"},
                               {49, kPeerCompId},
                               {52, utc_now_sending_time()},
                               {56, kEngineCompId}}) +
                   orig_sending_time + order_fields("DUPR"));
}

TEST(CapiC7Witness, Row6_PossDupRejectNo122TrafficKeepsTheSessionUp) {
    row6_cell([](std::uint32_t seq) { return possdup_order(seq, {}); }, true, "ROW6N");
}

TEST(CapiC7Witness, Row6_PossDupRejectBad122TrafficKeepsTheSessionUp) {
    row6_cell([](std::uint32_t seq) { return possdup_order(seq, fix_fields({{122, "GARBAGE"}})); },
              true, "ROW6B");
}

// ── Rows 1, 2, 3 and 5 on an acceptor in NotConnected ───────────────────────
//
// The peer's first frame is a Logon whose third field is not MsgType(35). Its CompIDs
// match, so the acceptor builds the session and delivers the frame, which the session
// disregards: it stays in NotConnected with its read pump running. C cannot see that
// state, only a session that is not established on a connection that is open, so the
// peer waits a settle before it drives the row. Each window counts from before the
// connect, so from no later than the establishment deadline's arming, and the settle
// ends well before that deadline.
constexpr std::chrono::milliseconds kNotConnectedSettle = kDefaultLogonTimeout / 5;

struct NotConnectedStart {
    bool written = false;
    std::chrono::steady_clock::time_point connected_at;
    bool closed_in_settle = false;
    bool established_in_settle = false;
};

// Starts `a`, connects `peer` and writes the lone 35-not-third Logon, then waits until
// `t0` + kNotConnectedSettle.
NotConnectedStart start_not_connected(CAcceptor& a, RawAcceptor& peer,
                                      std::chrono::steady_clock::time_point t0) {
    NotConnectedStart r;
    if (!start_and_connect(a, peer)) return r;
    r.connected_at = std::chrono::steady_clock::now();
    r.written = peer.write(peer_logon_35_not_third());
    if (!r.written) return r;
    r.closed_in_settle = peer.wait_eof(t0 + kNotConnectedSettle);
    r.established_in_settle = a.established();
    return r;
}

void expect_not_connected(NotConnectedStart const& r) {
    EXPECT_FALSE(r.closed_in_settle) << "the connection closed after the 35-not-third Logon";
    EXPECT_FALSE(r.established_in_settle) << "the 35-not-third Logon established the session";
}

// Row 1: junk, then a well-formed Logon, read by the pump.
TEST(CapiC7Witness, Row1_AGarbleInAnAcceptorsNotConnectedIsDisregarded) {
    RawAcceptor peer;
    CAcceptor a;
    auto const t0 = std::chrono::steady_clock::now();
    NotConnectedStart const r = start_not_connected(a, peer, t0);
    ASSERT_TRUE(r.written) << "setup";
    expect_not_connected(r);
    ASSERT_TRUE(peer.write("GARBLE" + peer_logon()));
    expect_acceptor_established_then_kept_up(a, peer, "ROW1N");
}

// Row 3: the header of a frame over the limit, in a write of its own. Settle and bound
// together end before T, so the deadline close cannot satisfy the cell.
TEST(CapiC7Witness, Row3_AnOverLimitBodyLengthInAnAcceptorsNotConnectedEndsTheConnection) {
    constexpr std::chrono::milliseconds kBound = kDefaultLogonTimeout / 5;
    static_assert(kNotConnectedSettle + kBound < kDefaultLogonTimeout,
                  "the bound must end before the deadline that would otherwise close it");
    RawAcceptor peer;
    CAcceptor a;
    auto const t0 = std::chrono::steady_clock::now();
    NotConnectedStart const r = start_not_connected(a, peer, t0);
    ASSERT_TRUE(r.written) << "setup";
    expect_not_connected(r);
    bool const closed = !r.closed_in_settle && peer.write(over_limit_header()) &&
                        peer.wait_eof(t0 + kNotConnectedSettle + kBound);
    EXPECT_TRUE(closed) << "the connection stayed open after an over-limit BodyLength";
    Observed const o = observe(a, peer, "none");
    EXPECT_FALSE(o.established);
    EXPECT_EQ(o.send_rc, FIXPP_ERR_SESSION_INVALID_STATE);
    EXPECT_EQ(o.to_app, 0);
    EXPECT_EQ(o.close_rc, FIXPP_ERR_THREAD_SESSION_LIFECYCLE) << "the session never established";
}

// Row 5: the peer sends nothing more. The connection stays open until a margin before T,
// counted from the connect, and is closed by T plus the same allowance as
// Row5_ASlowPeerIsClosedAtTheDeadline, counted from before it.
TEST(CapiC7Witness, Row5_AnAcceptorInNotConnectedIsClosedAtTheDeadline) {
    constexpr std::chrono::milliseconds kMargin = kDefaultLogonTimeout / 10;
    constexpr std::chrono::milliseconds kLate = kDefaultLogonTimeout + 2000ms;
    RawAcceptor peer;
    CAcceptor a;
    auto const t0 = std::chrono::steady_clock::now();
    NotConnectedStart const r = start_not_connected(a, peer, t0);
    ASSERT_TRUE(r.written) << "setup";
    expect_not_connected(r);
    bool const early =
        r.closed_in_settle || peer.wait_eof(r.connected_at + kDefaultLogonTimeout - kMargin);
    bool const closed = early || peer.wait_eof(t0 + kLate);
    EXPECT_FALSE(early) << "the connection closed before the deadline";
    EXPECT_TRUE(closed) << "the connection stayed open past T";
    Observed const o = observe(a, peer, "none");
    EXPECT_FALSE(o.established);
    EXPECT_EQ(o.send_rc, FIXPP_ERR_SESSION_INVALID_STATE);
    EXPECT_EQ(o.to_app, 0);
    EXPECT_EQ(o.close_rc, FIXPP_ERR_THREAD_SESSION_LIFECYCLE) << "the session never established";
}

// Row 2: a Logon whose third field is not MsgType(35) and which carries a malformed tag,
// then a well-formed Logon, read by the pump.
TEST(CapiC7Witness, Row2_AnAcceptorInNotConnectedDisregardsA35NotThirdLogon) {
    RawAcceptor peer;
    CAcceptor a;
    auto const t0 = std::chrono::steady_clock::now();
    NotConnectedStart const r = start_not_connected(a, peer, t0);
    ASSERT_TRUE(r.written) << "setup";
    expect_not_connected(r);
    ASSERT_TRUE(peer.write(peer_logon_35_not_third("9x9=1\x01") + peer_logon()));
    expect_acceptor_established_then_kept_up(a, peer, "ROW2N");
}

// ── Rows 1 to 4 and 6 on an acceptor in Active, and row 3 in LogoutSent ─────

// Row 1: junk before a frame start (TC 2d), then an order.
TEST(CapiC7Witness, Row1_AGarbledFrameInAnAcceptorsActiveIsDisregarded) {
    RawAcceptor peer;
    CAcceptor a;
    ASSERT_TRUE(establish_acceptor(a, peer, kHeartBtIntSeconds)) << "setup";
    bool const fenced = peer.write("GARBLE" + order(2, "ROW1AA")) && peer.fence(3, "R1AA");
    EXPECT_TRUE(fenced) << "the session answers a TestRequest after the garble";
    expect_kept_up(observe(a, peer, "ROW1AA"));
}

// Row 2: the disregarded frame consumes no MsgSeqNum, so the fence carries the number it
// carried.
TEST(CapiC7Witness, Row2_A35NotThirdFrameInAnAcceptorsActiveIsDisregarded) {
    RawAcceptor peer;
    CAcceptor a;
    ASSERT_TRUE(establish_acceptor(a, peer, kHeartBtIntSeconds)) << "setup";
    bool const fenced = peer.write(order_35_not_third(2, "ROW2AA")) && peer.fence(2, "R2AA");
    EXPECT_TRUE(fenced) << "the session answers a TestRequest carrying the disregarded number";
    Observed const o = observe(a, peer, "ROW2AA");
    EXPECT_TRUE(o.established);
    EXPECT_EQ(o.send_rc, FIXPP_ERR_OK);
    EXPECT_EQ(o.to_app, 1);
    EXPECT_FALSE(o.received) << "the receive callback is not invoked for the disregarded frame";
    EXPECT_EQ(o.close_rc, FIXPP_ERR_OK);
}

TEST(CapiC7Witness, Row3_AFrameOfLSplitAtTheCarryEdgeInAnAcceptorsActiveIsAdmitted) {
    RawAcceptor peer;
    CAcceptor a;
    ASSERT_TRUE(establish_acceptor(a, peer, kHeartBtIntSeconds)) << "setup";
    row3_split_cell(a, peer, "ROW3AA");
}

TEST(CapiC7Witness, Row3_AnOverLimitBodyLengthInAnAcceptorsActiveEndsTheConnection) {
    RawAcceptor peer;
    CAcceptor a;
    ASSERT_TRUE(establish_acceptor(a, peer, kHeartBtIntSeconds)) << "setup";
    auto const t0 = std::chrono::steady_clock::now();
    bool const closed = peer.write(over_limit_header()) && peer.wait_eof(t0 + kOverLimitBound);
    EXPECT_TRUE(closed) << "the connection stayed open after an over-limit BodyLength";
    Observed const o = observe(a, peer, "none");
    EXPECT_FALSE(o.established);
    EXPECT_EQ(o.send_rc, FIXPP_ERR_SESSION_INVALID_STATE);
    EXPECT_EQ(o.to_app, 0);
    EXPECT_EQ(o.close_rc, FIXPP_ERR_OK) << "established once";
}

TEST(CapiC7Witness, Row4_ADenseFrameOfLInAnAcceptorsActiveIsDelivered) {
    RawAcceptor peer;
    CAcceptor a;
    ASSERT_TRUE(establish_acceptor(a, peer, kHeartBtIntSeconds)) << "setup";
    row4_dense_cell(a, peer, "ROW4AA");
}

TEST(CapiC7Witness, Row6_LivenessOnlyTrafficKeepsAnAcceptorUp) {
    row6_cell(too_low_heartbeat, false, "ROW6A", FIXPP_ROLE_ACCEPTOR);
}

TEST(CapiC7Witness, Row6_TooLowPossDupTrafficKeepsAnAcceptorUp) {
    row6_cell(too_low_possdup_order, false, "ROW6AD", FIXPP_ROLE_ACCEPTOR);
}

TEST(CapiC7Witness, Row6_InboundRejectTrafficKeepsAnAcceptorUp) {
    row6_cell(inbound_reject, true, "ROW6AR", FIXPP_ROLE_ACCEPTOR);
}

TEST(CapiC7Witness, Row6_GapFillTrafficKeepsAnAcceptorUp) {
    row6_cell(gap_fill, true, "ROW6AG", FIXPP_ROLE_ACCEPTOR);
}

TEST(CapiC7Witness, Row6_PossDupRejectNo122TrafficKeepsAnAcceptorUp) {
    row6_cell([](std::uint32_t seq) { return possdup_order(seq, {}); }, true, "ROW6AN",
              FIXPP_ROLE_ACCEPTOR);
}

TEST(CapiC7Witness, Row6_PossDupRejectBad122TrafficKeepsAnAcceptorUp) {
    row6_cell([](std::uint32_t seq) { return possdup_order(seq, fix_fields({{122, "GARBAGE"}})); },
              true, "ROW6AB", FIXPP_ROLE_ACCEPTOR);
}

// Row 3 in LogoutSent, the acceptor's blocking close.
TEST(CapiC7Witness, Row3_AnOverLimitBodyLengthInAnAcceptorsLogoutSentEndsTheBlockingClose) {
    RawAcceptor peer;
    CAcceptor a;
    ASSERT_TRUE(establish_acceptor(a, peer, kHeartBtIntSeconds)) << "setup";
    logout_sent_cell(a, peer, over_limit_header());
}

// ── Rows 3 and 5 on an initiator in Disconnected ────────────────────────────
//
// The peer answers the initiator's Logon with a Logon reply without HeartBtInt(108),
// which the session refuses, leaving it in Disconnected with its transport open until the
// establishment deadline T. As for the acceptor in Disconnected, C cannot see the refusal
// land, so the peer waits a settle during which the connection must stay open and the
// session not established. Windows count from before the engine starts, so from no later
// than the deadline's arming.
std::string refused_logon_reply() {
    return frame44("35=A\x01" + peer_header(1) + fix_fields({{98, "0"}}));
}

TEST(CapiC7Witness, Row3_AnOverLimitBodyLengthInAnInitiatorsDisconnectedEndsTheConnection) {
    constexpr std::chrono::milliseconds kSettle = kDefaultLogonTimeout / 5;
    constexpr std::chrono::milliseconds kBound = kDefaultLogonTimeout / 5;
    static_assert(kSettle + kBound < kDefaultLogonTimeout,
                  "the bound must end before the deadline that would otherwise close it");
    RawAcceptor peer;
    CInitiator c{peer.port(), kHeartBtIntSeconds};
    auto const t0 = std::chrono::steady_clock::now();
    ASSERT_TRUE(c.start() && peer.accept() && peer.read_logon()) << "setup";
    ASSERT_TRUE(peer.write(refused_logon_reply()));
    bool const closed_early = peer.wait_eof(t0 + kSettle);
    bool const established_early = c.established();
    bool const closed =
        !closed_early && peer.write(over_limit_header()) && peer.wait_eof(t0 + kSettle + kBound);
    EXPECT_FALSE(closed_early) << "the connection closed before the over-limit header";
    EXPECT_FALSE(established_early) << "the refused Logon reply established the session";
    EXPECT_TRUE(closed) << "the connection stayed open after an over-limit BodyLength";
    Observed const o = observe(c, peer, "none");
    EXPECT_FALSE(o.established);
    EXPECT_EQ(o.send_rc, FIXPP_ERR_SESSION_INVALID_STATE);
    EXPECT_EQ(o.to_app, 0);
    EXPECT_EQ(o.close_rc, FIXPP_ERR_THREAD_SESSION_LIFECYCLE) << "the session never established";
}

TEST(CapiC7Witness, Row5_AnInitiatorWhoseLogonReplyWasRefusedIsClosedAtTheDeadline) {
    constexpr std::chrono::milliseconds kSettle = kDefaultLogonTimeout / 5;
    constexpr std::chrono::milliseconds kLate = kDefaultLogonTimeout + 2000ms;
    RawAcceptor peer;
    CInitiator c{peer.port(), kHeartBtIntSeconds};
    auto const t0 = std::chrono::steady_clock::now();
    ASSERT_TRUE(c.start() && peer.accept() && peer.read_logon()) << "setup";
    ASSERT_TRUE(peer.write(refused_logon_reply()));
    bool const closed_early = peer.wait_eof(t0 + kSettle);
    bool const established_early = c.established();
    bool const closed = closed_early || peer.wait_eof(t0 + kLate);
    EXPECT_FALSE(closed_early) << "the connection closed at the refusal, not at the deadline";
    EXPECT_FALSE(established_early) << "the refused Logon reply established the session";
    EXPECT_TRUE(closed) << "the refused session's connection stayed open past T";
    Observed const o = observe(c, peer, "none");
    EXPECT_FALSE(o.established);
    EXPECT_EQ(o.send_rc, FIXPP_ERR_SESSION_INVALID_STATE);
    EXPECT_EQ(o.close_rc, FIXPP_ERR_THREAD_SESSION_LIFECYCLE);
}

}  // namespace
