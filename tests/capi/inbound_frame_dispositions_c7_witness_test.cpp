// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/inbound_frame_dispositions_c7_witness_test.cpp — 093-inbound-frame-dispositions,
// quickstart Q-37 (tasks.md T102): the C-ABI witnesses for contract C-7 rows 1 to 6,
// driven through the C ABI.
//
// For each row's trigger, a C-ABI initiator engine faces a raw TCP acceptor peer
// (inbound_frame_dispositions_capi_support.hpp), and the cell asserts afterwards,
// through C:
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
// in Active (rows 1, 2's frame, 3, 4, 6) it reads the same whether the session
// carried on or ended; it discriminates the pre-Active triggers (row 2's Logon, row 5).
// Row 2's frame in Active is processed or disregarded with the session up either way,
// so the receive callback is its discriminator.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
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

bool establish(CInitiator& c, RawAcceptor& peer, std::uint32_t heartbeat_s) {
    return c.start() && peer.accept() && peer.read_logon() &&
           peer.write(logon_reply(heartbeat_s)) && c.wait_established();
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
TEST(CapiC7Witness, Row3_AFrameOfLSplitAtTheCarryEdgeIsAdmitted) {
    constexpr std::size_t kL = 65536;
    constexpr std::size_t kTail = 16;
    RawAcceptor peer;
    CInitiator c{peer.port(), kHeartBtIntSeconds};
    ASSERT_TRUE(establish(c, peer, kHeartBtIntSeconds)) << "setup";

    std::string const f = frame44_of_size("35=0\x01" + peer_header(2), kL, "58", Pad::one_field);
    ASSERT_EQ(f.size(), kL);
    bool const first = peer.write(f.substr(0, kL - kTail));
    std::this_thread::sleep_for(200ms);
    bool const fenced =
        first && peer.write(f.substr(kL - kTail) + order(3, "ROW3")) && peer.fence(4, "R3");
    EXPECT_TRUE(fenced) << "the session answers a TestRequest after the frame of L";
    expect_kept_up(observe(c, peer, "ROW3"));
}

// ── Row 4: a dense frame of L parses ────────────────────────────────────────
//
// An order of exactly L bytes whose padding is "2=<SOH>" fields (tag 2 is not a
// NewOrderSingle field, so each is an unknown field): the densest layout. It is
// written alone, so the carry holds nothing past it.
TEST(CapiC7Witness, Row4_ADenseFrameOfLIsDelivered) {
    constexpr std::size_t kL = 65536;
    RawAcceptor peer;
    CInitiator c{peer.port(), kHeartBtIntSeconds};
    ASSERT_TRUE(establish(c, peer, kHeartBtIntSeconds)) << "setup";

    std::string const f =
        frame44_of_size("35=D\x01" + peer_header(2) + order_fields("ROW4"), kL, "2", Pad::dense);
    ASSERT_EQ(f.size(), kL);
    bool const written = peer.write(f);
    std::this_thread::sleep_for(200ms);
    bool const fenced = written && peer.fence(3, "R4");
    EXPECT_TRUE(fenced) << "the session answers a TestRequest after the dense frame";
    expect_kept_up(observe(c, peer, "ROW4"));
}

// ── Row 5: a pre-Active connection closes at the establishment deadline ──────
//
// The initiator's peer answers the Logon only after T, the default timeout (the
// setter is new, so this file does not use it). The connection is closed at T, so the
// late answer never establishes the session.
TEST(CapiC7Witness, Row5_ASlowPeerIsClosedAtTheDeadline) {
    constexpr std::chrono::milliseconds kDefaultT{10000};  // SessionConfig's default T
    constexpr std::chrono::milliseconds kLate = kDefaultT + 2000ms;
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
// HeartBtInt is 1 s. For several intervals the peer sends only Heartbeats below the
// expected MsgSeqNum (too low, tolerated for a Heartbeat) and answers no TestRequest.
// The liveness loop sends a TestRequest after an interval without a refresh and ends
// the session after one more (run_liveness_loop, src/session/session.cpp); every
// one of these Heartbeats refreshes, so neither happens.
TEST(CapiC7Witness, Row6_LivenessOnlyTrafficKeepsTheSessionUp) {
    constexpr std::uint32_t kShortHeartBtInt = 1;
    constexpr std::chrono::milliseconds kInterval{kShortHeartBtInt * 1000};
    constexpr std::chrono::milliseconds kTraffic = 4 * kInterval;
    constexpr std::chrono::milliseconds kEvery = kInterval / 4;
    RawAcceptor peer;
    CInitiator c{peer.port(), kShortHeartBtInt};
    ASSERT_TRUE(establish(c, peer, kShortHeartBtInt)) << "setup";

    auto const until = std::chrono::steady_clock::now() + kTraffic;
    bool written = true;
    while (written && std::chrono::steady_clock::now() < until) {
        written = peer.write(heartbeat(1));
        std::this_thread::sleep_for(kEvery);
    }
    bool const fenced = written && peer.write(order(2, "ROW6")) && peer.fence(3, "R6");
    EXPECT_TRUE(written);
    EXPECT_TRUE(fenced) << "the session answers a TestRequest after the liveness-only traffic";
    expect_kept_up(observe(c, peer, "ROW6"));
}

}  // namespace
