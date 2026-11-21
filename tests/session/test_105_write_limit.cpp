// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_105_write_limit.cpp — 105-transport-liveness, User Story 1 part B (tasks.md
// T051): the write limit on every ending Logout (research D2 item 11; contract C-2.7; data-model
// E-3 rows 1 to 5).
//
// The rig is test_105_direct_rig.hpp's: a direct Session, both roles, whose Logout's write is
// held in the transport (BlockingWriteTransport) or queued at the write gate behind a held
// application send. The bound is logout_disconnect_timeout_ms (500 ms) from the start of the
// Logout's write, on the Scene's mock clock; a direct Session has no establishment deadline, so
// logon_timeout_ms plays no part, and the cells' liveness instants (HeartBtInt 30 s unless a cell
// sets it) are past the bound. Each LogoutBlocked and LogoutQueuedBehindBlockedSend cell asserts:
// open at the bound - 1 ms, no complete Logout at the peer, the transport closed at the bound,
// onLogout once, and no wait after it (a peer Logout fed after the bound changes nothing).
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <fixpp/log/logger.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "session/resend_gap_scene.hpp"
#include "session/test_105_direct_rig.hpp"
#include "session/test_105_support.hpp"
#include "support/log_capture.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"

namespace {

using namespace std::chrono_literals;
using fixpp::session::fsm_state;
using fixpp::session::seqnum_t;
using fixpp::session::session_role;
using fixpp::session::session_test_access;
using fixpp::session::SessionConfig;
namespace t105 = fixpp::test_support::t105;
namespace rgs = fixpp::test_support::resend_gap;

constexpr std::chrono::milliseconds kBound{500};

// The give-up's record (contract C-3.3), spelled out here as in test_105_liveness_timing.cpp.
constexpr char kGiveUpFormat[] =
    "session ended: TestRequest unanswered: test_req_id={} threshold_ms={} error={}";

class WriteLimit105 : public ::testing::TestWithParam<session_role> {};

// The R-2 Logout after Active: a Heartbeat at NextNumIn with a stale SendingTime(52).
[[nodiscard]] std::vector<std::byte> stale_heartbeat(rgs::Scene& s, seqnum_t seq) {
    return rgs::Scene::raw("0", seq, "TW", s.sending_time(std::chrono::seconds{-600}));
}

// The ending under test, started; its Logout's write held (or queued). Returns the frame's future
// when the ending is a pump-less frame delivery, empty otherwise.
enum class ending : std::uint8_t {
    r1,                    // the liveness loop's give-up
    r2_after_active,       // the SendingTime Reject's Logout
    r2_before_active,      // a Logon refusal with a Logout (the posture mismatch)
    r2_before_active_789,  // NextExpectedMsgSeqNum(789) invalid
    reply,                 // the reply to the peer's Logout
};

void tweak_for(ending e, SessionConfig& cfg) {
    if (e == ending::r1) {
        cfg.heartbeat_interval = 10s;
        cfg.test_request_threshold = std::chrono::milliseconds{10s};
    }
    if (e == ending::r2_before_active) cfg.posture = fixpp::session::session_posture::production;
    if (e == ending::r2_before_active_789) cfg.enable_next_expected_msg_seq_num = true;
}

struct Started {
    bool ok = false;
    std::string why;
    std::future<fixpp::core::expected_t<void>> frame;  // the delivery that writes the Logout
};

// Drives d to the point where the ending's Logout write has started and is held at the transport
// (queued = false), or is queued at the write gate behind a held send (queued = true).
Started start_ending(t105::DirectRig& d, ending e, bool queued) {
    Started st;
    auto& s = d.s;
    bool const before_active = e == ending::r2_before_active || e == ending::r2_before_active_789;
    if (before_active) {
        auto const opened = d.run(s.session->open(), "open");
        if (!opened || !opened->has_value()) {
            st.why = "setup: open()";
            return st;
        }
        if (d.role == session_role::initiator) {
            auto const c = d.run(s.session->drive_reconnect(), "drive_reconnect");
            if (!c || !c->has_value()) {
                st.why = "setup: drive_reconnect()";
                return st;
            }
        } else {
            s.session->attach_accepted_transport(
                std::make_unique<t105::bwt::BlockingWriteTransport>(s.ioc.get_executor(), d.stream,
                                                                    d.bw),
                fixpp::transport::handshake_result{});
        }
        d.hold_write_of("5");
        std::string const extra = e == ending::r2_before_active ? "464=Y\x01" : "789=abc\x01";
        st.frame = s.feed_spawn(s.frame("A", 1,
                                        "98=0\x01"
                                        "108=30\x01" +
                                            extra));
    } else {
        if (!d.to_active()) {
            st.why = "setup: not Active";
            return st;
        }
        if (e == ending::r1) {
            // The TestRequest, then the give-up after the grace (twice HeartBtInt and the guard).
            for (int i = 0; i < 11 && d.started("1") == 0U; ++i) d.pass(1s);
            if (d.started("1") == 0U) {
                st.why = "setup: no TestRequest";
                return st;
            }
        }
        if (queued && e == ending::r2_after_active) {
            // The too-low route, whose arm writes no Reject before its Logout: the Heartbeat at
            // 2 first, so a second one at 2 is below NextNumIn.
            (void)s.feed(s.heartbeat(2));
        }
        if (queued) {
            // An application send held in its write, holding the write gate.
            d.hold_next_write();
            static constexpr std::string_view kPayload =
                "35=D\x01"
                "11=Q105\x01";
            (void)asio::co_spawn(
                fixpp::test_support::session_executor_of(*s.session, s.ioc),
                s.session->send(std::as_bytes(std::span{kPayload.data(), kPayload.size()})),
                asio::detached);
            if (!s.pump_until([&] { return d.parked(); })) {
                st.why = "setup: the send's write was not held";
                return st;
            }
        } else {
            d.hold_write_of("5");
        }
        if (e == ending::r1) {
            for (int i = 0; i < 12 && d.started("5") == 0U && !queued; ++i) d.pass(1s);
            if (queued) {
                for (int i = 0; i < 12 && session_test_access::test_request_pending(*s.session);
                     ++i) {
                    d.pass(1s);
                    if (d.s.session->state() != fsm_state::Active) break;
                }
                // The give-up has started: its Logout is queued behind the held send.
                for (int i = 0; i < 12 && d.app->to_admin_count("5") == 0; ++i) d.pass(1s);
            }
        } else if (e == ending::r2_after_active) {
            st.frame = s.feed_spawn(queued ? s.heartbeat(2) : stale_heartbeat(s, 2));
        } else {
            st.frame = s.feed_spawn(s.logout(2));
        }
    }
    s.drain();
    bool const held_at_transport = d.parked() && d.started("5") == 1U;
    bool const queued_at_gate = queued && d.app->to_admin_count("5") >= 1 && d.started("5") == 0U;
    st.ok = queued ? queued_at_gate : held_at_transport;
    if (!st.ok) st.why = "setup: the Logout's write is not held where the cell needs it";
    return st;
}

struct LimitReading {
    Started st;
    std::size_t give_up_records_at_bound = 0;
    bool open_before = false;
    bool closed_at_bound = false;
    std::size_t completed_logouts = 0;
    int logouts_after = -1;
    bool no_wait_after = false;
};

LimitReading run_limit(session_role role, ending e, bool queued) {
    t105::LogCapture cap;
    t105::DirectRig d{role, [e, &cap](SessionConfig& c) {
                          tweak_for(e, c);
                          c.logger_override = cap.logger;
                      }};
    LimitReading r;
    r.st = start_ending(d, e, queued);
    if (r.st.ok) {
        d.pass(kBound - 1ms);
        r.open_before = !d.closed();
        d.pass(1ms);
        d.s.drain();
        r.closed_at_bound = d.closed();
        r.completed_logouts = d.completed("5");
        if (e == ending::r1)
            r.give_up_records_at_bound = cap.records_of(FIXPP_FORMAT_ID(kGiveUpFormat)).size();
        (void)d.s.pump_until(
            [&] {
                return d.app->logouts >= 1 || e == ending::r2_before_active ||
                       e == ending::r2_before_active_789;
            },
            std::chrono::milliseconds{500});
        r.logouts_after = d.app->logouts;
        // No wait after the limit: a peer Logout now changes nothing.
        if (d.s.session->state() == fsm_state::Disconnected) {
            (void)d.s.feed(d.s.logout(9));
            r.no_wait_after = d.completed("5") == r.completed_logouts;
        }
    }
    d.release();
    d.s.drain();
    auto const closed = d.run(d.s.session->close(fixpp::session::close_mode::terminal), "close");
    (void)closed;
    return r;
}

void expect_limit(LimitReading const& r, bool after_active, const char* site,
                  bool give_up = false) {
    if (give_up) {
        EXPECT_EQ(r.give_up_records_at_bound, 1U) << site << ": the slot-74 record at the bound";
    }
    EXPECT_TRUE(r.st.ok) << site << ": " << r.st.why;
    EXPECT_TRUE(r.open_before) << site << ": closed before the bound";
    EXPECT_TRUE(r.closed_at_bound) << site << ": the transport was not closed at the bound";
    EXPECT_EQ(r.completed_logouts, 0U) << site << ": a complete Logout reached the peer";
    if (after_active) EXPECT_EQ(r.logouts_after, 1) << site << ": onLogout";
    EXPECT_TRUE(r.no_wait_after) << site << ": a wait followed the limit";
}

TEST_P(WriteLimit105, R1_LogoutBlocked_ClosedAtTheBound) {
    expect_limit(run_limit(GetParam(), ending::r1, false), true, "R1", true);
}
TEST_P(WriteLimit105, R2AfterActive_LogoutBlocked_ClosedAtTheBound) {
    expect_limit(run_limit(GetParam(), ending::r2_after_active, false), true, "R2AfterActive");
}
TEST_P(WriteLimit105, R2BeforeActive_LogoutBlocked_ClosedAtTheBound) {
    expect_limit(run_limit(GetParam(), ending::r2_before_active, false), false, "R2BeforeActive");
}
TEST_P(WriteLimit105, R2BeforeActive789_LogoutBlocked_ClosedAtTheBound) {
    expect_limit(run_limit(GetParam(), ending::r2_before_active_789, false), false,
                 "R2BeforeActive789");
}
TEST_P(WriteLimit105, Reply_LogoutBlocked_ClosedAtTheBound) {
    expect_limit(run_limit(GetParam(), ending::reply, false), true, "Reply");
}
TEST_P(WriteLimit105, R1_LogoutQueuedBehindBlockedSend_ClosedAtTheBound) {
    expect_limit(run_limit(GetParam(), ending::r1, true), true, "R1Queued", true);
}
TEST_P(WriteLimit105, R2AfterActive_LogoutQueuedBehindBlockedSend_ClosedAtTheBound) {
    expect_limit(run_limit(GetParam(), ending::r2_after_active, true), true, "R2Queued");
}
TEST_P(WriteLimit105, Reply_LogoutQueuedBehindBlockedSend_ClosedAtTheBound) {
    expect_limit(run_limit(GetParam(), ending::reply, true), true, "ReplyQueued");
}

// Control: the Logout's write released at the bound - 1 ms completes; the wait then keeps the
// budget it started with, so the transport closes at the same bound.
TEST_P(WriteLimit105, WriteReleasedBeforeTheBound_LogoutOnWire_WaitKeepsTheSameBudget) {
    t105::DirectRig d{GetParam()};
    auto st = start_ending(d, ending::r2_after_active, false);
    EXPECT_TRUE(st.ok) << st.why;
    if (st.ok) {
        d.pass(kBound - 1ms);
        d.release();
        d.s.drain();
        EXPECT_EQ(d.completed("5"), 1U) << "the released Logout did not complete";
        EXPECT_FALSE(d.closed()) << "closed before the bound";
        EXPECT_EQ(d.s.state(), fsm_state::Active) << "no wait after the released Logout";
        d.pass(1ms);
        EXPECT_TRUE(d.closed()) << "the wait did not end at the same bound";
        EXPECT_EQ(d.app->logouts, 1);
    }
    d.release();
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "close");
}

// The R-1 Logout held; the peer's Logout arrives during the write: its arm writes nothing (the
// owner is set) and records Disconnected, and the close defers to the writer, so the Logout
// completes on the wire, then the transport closes.
TEST_P(WriteLimit105, R1WriteHeld_PeerLogoutArrives_LogoutCompleteThenEof) {
    t105::DirectRig d{GetParam(), [](SessionConfig& c) { tweak_for(ending::r1, c); }};
    auto st = start_ending(d, ending::r1, false);
    EXPECT_TRUE(st.ok) << st.why;
    if (st.ok) {
        (void)d.s.feed(d.s.logout(2));
        EXPECT_FALSE(d.closed()) << "the peer's Logout cut the R-1 Logout's write";
        d.pass(kBound / 2);
        d.release();
        d.s.drain();
        EXPECT_EQ(d.completed("5"), 1U) << "the R-1 Logout did not complete";
        EXPECT_TRUE(d.closed()) << "the transport stayed open after the R-1 Logout";
        EXPECT_EQ(d.app->logouts, 1);
    }
    d.release();
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "close");
}

// The R-1 Logout's store call held at the bound: the transport closes at the bound; onLogout
// fires when the store call returns.
TEST_P(WriteLimit105, R1WriteHeld_LimitFiresInsideStoreCall_EofAtBoundOnLogoutAfterStore) {
    auto gst = std::make_shared<t105::gate_store_state>();
    t105::DirectRig d{GetParam(), [&](SessionConfig& c) {
                          tweak_for(ending::r1, c);
                          c.store_factory = std::make_shared<t105::GateStoreFactory>(gst);
                      }};
    bool up = d.to_active();
    EXPECT_TRUE(up) << "setup";
    if (up) {
        for (int i = 0; i < 11 && d.started("1") == 0U; ++i) d.pass(1s);
        gst->hold_after_inner = true;
        gst->hold_when = [&d](t105::store_call k, seqnum_t) {
            return k == t105::store_call::store_outbound && d.app->to_admin_count("5") >= 1;
        };
        for (int i = 0; i < 12 && gst->parked == 0U; ++i) d.pass(1s);
        EXPECT_EQ(gst->parked, 1U) << "setup: the R-1 Logout's store call is not held";
        d.pass(kBound - 1ms);
        EXPECT_FALSE(d.closed()) << "closed before the bound";
        d.pass(1ms);
        EXPECT_TRUE(d.closed()) << "the transport was not closed at the bound";
        EXPECT_EQ(d.app->logouts, 0) << "onLogout before the store call returned";
        gst->release_all();
        d.s.drain();
        EXPECT_EQ(d.app->logouts, 1) << "onLogout after the store call returned";
    }
    gst->release_all();
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "close");
}

// [guard: a 0 ms limit read as "close at once"] With logout_disconnect_timeout_ms 0 the Logout's
// write is not limited: it stays held, and the transport open, past any bound.
TEST_P(WriteLimit105, TimeoutZero_WriteUnbounded) {
    t105::DirectRig d{GetParam(), [](SessionConfig& c) { c.logout_disconnect_timeout_ms = 0; }};
    auto st = start_ending(d, ending::r2_after_active, false);
    EXPECT_TRUE(st.ok) << st.why;
    if (st.ok) {
        d.pass(10s);
        EXPECT_FALSE(d.closed()) << "the unlimited write was cut";
        EXPECT_TRUE(d.parked()) << "the write is no longer held";
    }
    d.release();
    d.s.drain();
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "close");
}

FIXPP_105_BOTH_ROLES(WriteLimit105);

}  // namespace
