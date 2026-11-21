// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_105_logout_wait.cpp — 105-transport-liveness, User Story 1 part B
// (tasks.md T042 to T050, T053, T055): the R-2 wait after fixpp's ending Logout (research D2;
// contract C-2; data-model E-3).
//
// The oracle is the peer's (plain_rig::Peer records how its read ended, test_105_support.hpp
// classes it). Every cell runs on the plaintext Engine rig with the mock clock as the engine
// clock: the bound (logout_disconnect_timeout_ms, 500 ms in the rig's config) is on it, and no
// wall-clock timer of the rig can close the connection (no TLS handshake, the first frame and
// establishment deadlines on the mock, close()'s grace only under close(graceful), which a cell
// reaches only where its section says so). An "at once" reading holds the mock clock still and
// pumps with t105::pump_until_peer_read_ends, whose wall cap fails as an assertion.
//
// The routes (contract C-2.2): the peer's frames go at the numbers a real peer uses. The
// SendingTime-Reject route: a Heartbeat at NextNumIn with a stale SendingTime(52), which the
// Reject consumes (fixpp#423), so the peer's next number is NextNumIn. The OrigSendingTime
// route: a Heartbeat at NextNumIn with PossDupFlag(43)=Y and an OrigSendingTime(122) later than
// its SendingTime(52), consumed likewise. The too-low route: a second Heartbeat at a number
// already received, so the peer's next frames are below NextNumIn. The gap-deadline route:
// a Heartbeat above NextNumIn opens a gap, frames above it carry the session to the gap's
// deadline (twice HeartBtInt from the ResendRequest), and the first one there draws the
// deadline's Logout; the peer's next frames are above NextNumIn.
//
// Competing timeouts, per cell: the liveness instants (HeartBtInt 30 s unless a cell sets it;
// test_request_threshold = HeartBtInt in every cell that sets HeartBtInt), the gap deadline
// (none unless the route opens a gap) and the establishment deadline (passed once Active) are
// all past the bound, which the cells advance to at most.
//
// No ASSERT_* runs between a rig's start() and its stop(): every observation inside that
// window is recorded and asserted after stop() (tasks.md, Execution rules).
#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "session/plain_engine_rig.hpp"
#include "session/test_105_direct_rig.hpp"
#include "session/test_105_support.hpp"
#include "support/memory_store_test_access.hpp"
#include "support/seqnum_manager_test_access.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"

namespace {

using namespace std::chrono_literals;
using fixpp::session::fsm_state;
using fixpp::session::seqnum_t;
using fixpp::session::session_role;
using fixpp::session::session_test_access;
using fixpp::session::SessionConfig;
namespace pr = fixpp::test_support::plain_rig;
namespace t105 = fixpp::test_support::t105;

// The wall time an "at once" reading waits with the mock clock still. It bounds the cell's
// failure, not the close.
constexpr std::chrono::milliseconds kWallCap{1500};

// A SendingTime(52) far outside the session's MaxLatency of the mock clock's now.
constexpr std::string_view kStaleSendingTime = "20230101-00:00:00.000";
// An OrigSendingTime(122) later than any SendingTime(52) a cell stamps on the mock clock, whose
// UTC side stays within its first hour.
constexpr std::string_view kLateOrigSendingTime = "20240101-01:00:00.000";
// An OrigSendingTime(122) earlier than any SendingTime(52) a cell stamps: a valid one.
constexpr std::string_view kEarlyOrigSendingTime = "20231231-23:00:00.000";

// Counts the lifecycle callbacks and the inbound Logouts and application messages delivered.
class WaitApp final : public fixpp::session::Application {
public:
    int logons = 0;
    int logouts = 0;
    int from_admin_logouts = 0;
    int from_app = 0;

    void onLogon(const fixpp::session::SessionId& /*id*/) override { ++logons; }
    void onLogout(const fixpp::session::SessionId& /*id*/) override { ++logouts; }
    std::function<void(std::string_view)> to_admin_hook;
    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
                 const fixpp::session::SessionId& /*id*/) override {
        if (to_admin_hook) to_admin_hook(msg.msg_type());
    }
    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
        const fixpp::session::SessionId& /*id*/) override {
        if (msg.msg_type() == "5") ++from_admin_logouts;
        return {};
    }
    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const fixpp::session::SessionId& /*id*/) override {
        ++from_app;
        return {};
    }
};

// The store's next numbers (inbound, outbound), read synchronously from the MemoryStore under a
// GateStore: a MemoryStore call completes without suspending, so a private io_context runs it.
[[nodiscard]] std::pair<seqnum_t, seqnum_t> store_numbers(t105::gate_store_state& st) {
    asio::io_context tmp;
    auto in = asio::co_spawn(
        tmp, st.inner->next_seqnum(fixpp::session::direction_t::inbound, false), asio::use_future);
    auto out = asio::co_spawn(
        tmp, st.inner->next_seqnum(fixpp::session::direction_t::outbound, false), asio::use_future);
    tmp.run();
    auto const i = in.get();
    auto const o = out.get();
    return {i ? *i : 0U, o ? *o : 0U};
}

// One plaintext Engine session over a GateStore, reset_on_logout set.
struct WaitCell {
    std::shared_ptr<WaitApp> app = std::make_shared<WaitApp>();
    pr::Rig rig{app};
    std::shared_ptr<t105::gate_store_state> st = std::make_shared<t105::gate_store_state>();

    [[nodiscard]] SessionConfig cfg(session_role role) {
        auto c = rig.cfg(role);
        c.reset_on_logout = true;
        c.store_factory = std::make_shared<t105::GateStoreFactory>(st);
        return c;
    }
    // Active, and the session's Logon (the acceptor's reply) read by the peer.
    [[nodiscard]] bool start_active(SessionConfig c) {
        return rig.start(std::move(c)) && rig.to_active() && rig.run_until([this] {
            return !pr::frames_of_type(rig.peer.received, "A").empty();
        });
    }
    [[nodiscard]] std::string wire_since(std::size_t mark) const {
        return rig.peer.received.substr(std::min(mark, rig.peer.received.size()));
    }
    [[nodiscard]] std::size_t count_since(std::size_t mark, std::string_view type) const {
        return pr::frames_of_type(wire_since(mark), type).size();
    }
    [[nodiscard]] seqnum_t next_in() const {
        auto const s = rig.session();
        return s ? session_test_access::seqnum_mgr(*s).next_inbound_unsafe() : 0U;
    }
    // The inbound advances persisted since the call mark: next_seqnum(inbound, true) calls.
    [[nodiscard]] std::size_t inbound_persists_since(std::size_t call_mark) const {
        std::size_t n = 0;
        for (std::size_t i = call_mark; i < st->calls.size(); ++i) {
            if (st->calls[i] == t105::store_call::increment_inbound) ++n;
        }
        return n;
    }
    // The "at once" reading: the peer's read ends within the wall cap with the mock clock still,
    // in EOF; the store is read at the first pass that sees the read ended.
    [[nodiscard]] bool eof_at_once(const char* site,
                                   std::optional<std::pair<seqnum_t, seqnum_t>>* at_eof = nullptr) {
        bool const ended = t105::pump_until_peer_read_ends(
            rig.ioc,
            [&] {
                if (rig.peer.read_ended && at_eof != nullptr && !at_eof->has_value()) {
                    *at_eof = store_numbers(*st);
                }
                return rig.peer.read_ended;
            },
            kWallCap, site);
        return ended && t105::ending_of(rig.peer) == t105::peer_ending::eof;
    }
    // Steps the mock clock by step, then the bounded drive.
    void advance(std::chrono::nanoseconds step) {
        (void)t105::advance_and_drive(*rig.clock, rig.ioc, step, 256);
    }
    // The peer's read is still open after a bounded drive with the clock still.
    [[nodiscard]] bool still_open() {
        (void)t105::drive_bounded(rig.ioc, 256);
        rig.settle(std::chrono::milliseconds{20});
        return !rig.peer.read_ended;
    }
};

template <class Fut>
[[nodiscard]] bool rg_ready(Fut const& f) {
    return f.valid() && f.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
}

// ── The routes to an R-2 ending Logout after Active ──────────────────────────────────────

enum class route : std::uint8_t { too_low, gap_deadline, sending_time_reject, orig_sending_time };

[[nodiscard]] const char* route_name(route r) {
    switch (r) {
        case route::too_low:
            return "too-low";
        case route::gap_deadline:
            return "gap-deadline";
        case route::sending_time_reject:
            return "SendingTime-Reject";
        case route::orig_sending_time:
            return "OrigSendingTime-Reject";
    }
    return "?";
}

// The HeartBtInt of a route's cells: the gap-deadline route steps the mock clock to twice it.
[[nodiscard]] std::chrono::seconds heartbt_of(route r) {
    return r == route::gap_deadline ? std::chrono::seconds{10} : std::chrono::seconds{30};
}

[[nodiscard]] SessionConfig route_cfg(WaitCell& c, session_role role, route r) {
    auto cfg = c.cfg(role);
    cfg.heartbeat_interval = heartbt_of(r);
    cfg.test_request_threshold = std::chrono::milliseconds{heartbt_of(r)};
    return cfg;
}

// What the route left: the session's ending Logout on the wire, the mark before it, and the
// number the peer's next in-sequence frame would carry from its own point of view.
struct Ending {
    bool logout_on_wire = false;
    bool active_at_logout = false;  // the positive control: established after the Logout
    std::size_t mark = 0;           // the peer's wire mark before the stimulus that drew it
    seqnum_t peer_next = 0;         // the peer's next number by the route (contract C-2.2)
    std::string why;                // the setup miss, when logout_on_wire is false
};

// Drives c, Active, along r until the session's ending Logout is on the wire.
Ending drive_route(WaitCell& c, route r) {
    Ending e;
    auto& rig = c.rig;
    switch (r) {
        case route::too_low: {
            rig.peer.send(rig.heartbeat(2));
            if (!rig.run_until([&] { return c.next_in() == 3U; })) {
                e.why = "the Heartbeat at 2 did not advance NextNumIn";
                return e;
            }
            e.mark = rig.peer.received.size();
            rig.peer.send(rig.heartbeat(2));
            e.peer_next = 2;  // the peer is behind: its next frames are below NextNumIn
            break;
        }
        case route::sending_time_reject: {
            e.mark = rig.peer.received.size();
            rig.peer.send(pr::message(rig.begin_string, "0", 2, "TW", "ISLD", kStaleSendingTime));
            e.peer_next = 3;  // the Reject consumed 2
            break;
        }
        case route::orig_sending_time: {
            e.mark = rig.peer.received.size();
            rig.peer.send(rig.msg("0", 2,
                                  "43=Y\x01"
                                  "122=" +
                                      std::string{kLateOrigSendingTime} + "\x01"));
            e.peer_next = 3;  // the Reject consumed 2
            break;
        }
        case route::gap_deadline: {
            auto const mark = rig.peer.received.size();
            rig.peer.send(rig.heartbeat(5));
            if (!rig.run_until([&] { return c.count_since(mark, "2") == 1U; })) {
                e.why = "the Heartbeat at 5 drew no ResendRequest";
                return e;
            }
            // Frames above NextNumIn keep the peer alive and are ignored before the deadline.
            seqnum_t seq = 6;
            auto const step = heartbt_of(r) / 2;
            for (int i = 0; i < 3; ++i) {
                c.advance(step);
                // deliver() waits for the peer's write and settles, so the frame is read
                // before the clock moves again.
                if (!rig.deliver(rig.heartbeat(seq++))) {
                    e.why = "a peer write before the deadline did not complete";
                    return e;
                }
            }
            c.advance(step);  // the deadline: twice HeartBtInt from the ResendRequest
            e.mark = rig.peer.received.size();
            rig.peer.send(rig.heartbeat(seq++));
            e.peer_next = seq;  // the peer is ahead: its next frames are above NextNumIn
            break;
        }
    }
    e.logout_on_wire =
        rig.run_until([&] { return c.count_since(e.mark, "5") == 1U; }, std::chrono::seconds{3});
    if (!e.logout_on_wire) {
        e.why = std::string{"no ending Logout on the "} + route_name(r) +
                " route; the peer read: " + c.wire_since(e.mark);
        return e;
    }
    (void)t105::drive_bounded(rig.ioc, 256);
    e.active_at_logout = rig.state() == fsm_state::Active;
    return e;
}

// ════════════════════════════════════════════════════════════════════════════════════════
// T042 — W-1 after Active (contract C-2.2, clarification D-a): the peer's Logout ends the
// wait. The oracle: the session Active after its Logout (the positive control), then EOF
// after the peer's Logout, the store reset under reset_on_logout by then, NextNumIn advanced
// (persisted) and fromAdmin called for the peer's Logout only at NextNumIn, onLogout once.
// ════════════════════════════════════════════════════════════════════════════════════════

class LogoutWait105 : public ::testing::TestWithParam<session_role> {};

struct ResponseReading {
    Ending e;
    bool eof = false;
    std::optional<std::pair<seqnum_t, seqnum_t>> store_at_eof;
    std::size_t persists_after_logout = 0;
    std::size_t rejects_after_logout = 0;
    std::size_t logouts_written_after = 0;
    int from_admin_logouts = 0;
    int logouts = 0;
    int logouts_at_wait = -1;
};

ResponseReading run_response(session_role role, route r, std::string_view extra = {},
                             std::optional<seqnum_t> peer_seq = std::nullopt) {
    WaitCell c;
    ResponseReading out;
    bool const up = c.start_active(route_cfg(c, role, r));
    if (up) out.e = drive_route(c, r);
    if (!up) out.e.why = "setup: the session did not reach Active";
    if (out.e.logout_on_wire && out.e.active_at_logout) {
        out.logouts_at_wait = c.app->logouts;
        auto const wire_mark = c.rig.peer.received.size();
        auto const call_mark = c.st->calls.size();
        c.rig.peer.send(c.rig.msg("5", peer_seq.value_or(out.e.peer_next), extra));
        out.eof = c.eof_at_once(route_name(r), &out.store_at_eof);
        out.persists_after_logout = c.inbound_persists_since(call_mark);
        out.rejects_after_logout = c.count_since(wire_mark, "3");
        out.logouts_written_after = c.count_since(wire_mark, "5");
    }
    c.rig.stop();
    out.from_admin_logouts = c.app->from_admin_logouts;
    out.logouts = c.app->logouts;
    return out;
}

void expect_response(ResponseReading const& o, bool at_next_num_in, const char* site) {
    EXPECT_TRUE(o.e.logout_on_wire) << site << ": " << o.e.why;
    EXPECT_TRUE(o.e.active_at_logout)
        << site << ": the positive control, the session was not established after its Logout";
    EXPECT_EQ(o.logouts_at_wait, 0) << site << ": onLogout fired before the wait ended";
    EXPECT_TRUE(o.eof) << site << ": the peer's read did not end in EOF after its Logout";
    EXPECT_EQ(o.store_at_eof, (std::optional<std::pair<seqnum_t, seqnum_t>>{{1U, 1U}}))
        << site << ": the receipt reset had not run when the peer read EOF";
    EXPECT_EQ(o.persists_after_logout, at_next_num_in ? 1U : 0U)
        << site << ": NextNumIn advanced for the peer's Logout";
    EXPECT_EQ(o.from_admin_logouts, at_next_num_in ? 1 : 0)
        << site << ": fromAdmin for the peer's Logout";
    EXPECT_EQ(o.logouts_written_after, 0U) << site << ": a reply Logout was written";
    EXPECT_EQ(o.logouts, 1) << site << ": onLogout";
}

TEST_P(LogoutWait105, TooLow_PeerLogoutBelowNextNumIn_EndsWaitAndResets) {
    expect_response(run_response(GetParam(), route::too_low), false, "TooLow");
}

TEST_P(LogoutWait105, GapDeadline_PeerLogoutAboveNextNumIn_EndsWaitAndResets) {
    expect_response(run_response(GetParam(), route::gap_deadline), false, "GapDeadline");
}

TEST_P(LogoutWait105, SendingTimeReject_PeerLogoutInSequence_EndsWait) {
    expect_response(run_response(GetParam(), route::sending_time_reject), true,
                    "SendingTimeReject");
}

TEST_P(LogoutWait105, OrigSendingTimeReject_PeerLogoutEndsWait) {
    expect_response(run_response(GetParam(), route::orig_sending_time), true,
                    "OrigSendingTimeReject");
}

// C-2.9 (research D2 item 3): the response test precedes the PossDup stage-1 arm, so a Logout
// carrying PossDupFlag(43)=Y with a valid OrigSendingTime(122) is the response, at NextNumIn
// and below it, and no Reject is written for it.
std::string poss_dup_extra() {
    return std::string{
               "43=Y\x01"
               "122="} +
           std::string{kEarlyOrigSendingTime} + "\x01";
}

TEST_P(LogoutWait105, PeerLogoutPossDupY_AtNextNumIn_EndsWait) {
    auto const o = run_response(GetParam(), route::sending_time_reject, poss_dup_extra());
    expect_response(o, true, "PossDupY_AtNextNumIn");
    EXPECT_EQ(o.rejects_after_logout, 0U) << "a Reject was written for the peer's Logout";
}

TEST_P(LogoutWait105, PeerLogoutPossDupY_BelowNextNumIn_EndsWait) {
    auto const o = run_response(GetParam(), route::too_low, poss_dup_extra());
    expect_response(o, false, "PossDupY_BelowNextNumIn");
    EXPECT_EQ(o.rejects_after_logout, 0U) << "a Reject was written for the peer's Logout";
}

// ════════════════════════════════════════════════════════════════════════════════════════
// T043 — the bound (W-12, C-2.5, C-1.d), on the SendingTime-Reject route: with no Logout from
// the peer the connection closes logout_disconnect_timeout_ms after the start of the Logout's
// write, on the mock clock, and onLogout fires then. The Logout's write starts in the strand
// turn of the stale frame, with the clock still, so its instant is the clock's when the cell
// sends that frame. Competing timeouts: HeartBtInt 30 s, no gap, all past the bound.
// ════════════════════════════════════════════════════════════════════════════════════════

struct BoundReading {
    Ending e;
    bool open_before = false;    // open at the bound - 1 ms
    bool active_before = false;  // still Active then
    int logouts_before = -1;     // onLogout before the bound
    bool eof_at_bound = false;   // EOF at the bound, the clock then still
    int logouts_at_bound = -1;   // onLogout once the read ended
    std::size_t answered_heartbeats = 0;
};

void expect_bound(BoundReading const& o, const char* site) {
    EXPECT_TRUE(o.e.logout_on_wire) << site << ": " << o.e.why;
    EXPECT_TRUE(o.e.active_at_logout)
        << site << ": the positive control, the session was not established after its Logout";
    EXPECT_TRUE(o.open_before) << site << ": the connection closed before the bound";
    EXPECT_TRUE(o.active_before) << site << ": the session left Active before the bound";
    EXPECT_EQ(o.logouts_before, 0) << site << ": onLogout before the bound";
    EXPECT_TRUE(o.eof_at_bound) << site << ": no EOF at the bound";
    EXPECT_EQ(o.logouts_at_bound, 1) << site << ": onLogout at the bound";
}

// Drives to the SendingTime-Reject Logout, then optionally runs mid(c) at mid_at after the
// Logout's start, then reads at the bound - 1 ms and at the bound.
BoundReading run_bound(session_role role,
                       std::function<std::size_t(WaitCell&)> const& mid = nullptr,
                       std::chrono::milliseconds mid_at = 0ms) {
    WaitCell c;
    BoundReading o;
    auto cfg = route_cfg(c, role, route::sending_time_reject);
    auto const bound = std::chrono::milliseconds{cfg.logout_disconnect_timeout_ms};
    bool const up = c.start_active(std::move(cfg));
    if (up) o.e = drive_route(c, route::sending_time_reject);
    if (!up) o.e.why = "setup: the session did not reach Active";
    if (o.e.logout_on_wire && o.e.active_at_logout) {
        std::chrono::milliseconds at{0};
        if (mid) {
            c.advance(mid_at);
            at = mid_at;
            o.answered_heartbeats = mid(c);
        }
        c.advance(bound - 1ms - at);
        o.open_before = c.still_open();
        o.active_before = c.rig.state() == fsm_state::Active;
        o.logouts_before = c.app->logouts;
        c.rig.clock->advance(1ms);
        o.eof_at_bound = c.eof_at_once("the bound");
        o.logouts_at_bound = c.app->logouts;
    }
    c.rig.stop();
    return o;
}

TEST_P(LogoutWait105, Bound_EofAtExactlyTheBound) { expect_bound(run_bound(GetParam()), "Bound"); }

// A Heartbeat answering the peer's TestRequest mid-wait does not move the bound.
TEST_P(LogoutWait105, Bound_DoesNotSlideWithAnAnswerSent) {
    auto const o = run_bound(
        GetParam(),
        [](WaitCell& c) {
            auto const mark = c.rig.peer.received.size();
            c.rig.peer.send(c.rig.msg("1", 3, "112=MID-WAIT\x01"));
            (void)c.rig.run_until([&] { return c.count_since(mark, "0") == 1U; });
            auto const hbs = pr::frames_of_type(c.wire_since(mark), "0");
            return hbs.size() == 1U && pr::field_value(hbs.front(), "112") == "MID-WAIT"
                       ? std::size_t{1}
                       : std::size_t{0};
        },
        250ms);
    EXPECT_EQ(o.answered_heartbeats, 1U) << "the TestRequest mid-wait was not answered";
    expect_bound(o, "Bound_DoesNotSlide");
}

// onLogout fires at the wait's end, not at the Logout's write.
TEST_P(LogoutWait105, OnLogoutFiresAtWaitEnd) {
    auto const o = run_response(GetParam(), route::sending_time_reject);
    EXPECT_EQ(o.logouts_at_wait, 0) << "onLogout fired at the Logout's write";
    EXPECT_EQ(o.logouts, 1) << "onLogout at the wait's end";
    EXPECT_TRUE(o.eof);
}

// ── The wait, entered on a route, with the cell's body running inside it ──────────────────

// One cell's session: Active with the route's config (tweaked), before(c) run once Active, then
// the route driven to its ending Logout. in_wait is the positive control: the Logout on the wire
// and the session still Active after it.
struct WaitRun {
    WaitCell c;
    Ending e;
    bool in_wait = false;

    WaitRun(session_role role, route r, std::function<void(SessionConfig&)> const& tweak = {},
            std::function<bool(WaitCell&)> const& before = {}) {
        auto cfg = route_cfg(c, role, r);
        if (tweak) tweak(cfg);
        if (!c.start_active(std::move(cfg))) {
            e.why = "setup: the session did not reach Active";
            return;
        }
        if (before && !before(c)) {
            e.why = "setup: the step before the route did not complete";
            return;
        }
        e = drive_route(c, r);
        in_wait = e.logout_on_wire && e.active_at_logout;
    }
    [[nodiscard]] fixpp::session::Session* s() const { return c.rig.session().get(); }
    // The peer's Logout at seq, then the "at once" reading.
    [[nodiscard]] bool peer_logout_eof(seqnum_t seq, const char* site,
                                       std::optional<std::pair<seqnum_t, seqnum_t>>* at = nullptr) {
        c.rig.peer.send(c.rig.msg("5", seq));
        return c.eof_at_once(site, at);
    }
};

void expect_in_wait(WaitRun const& w, const char* site) {
    EXPECT_TRUE(w.e.logout_on_wire) << site << ": " << w.e.why;
    EXPECT_TRUE(w.e.active_at_logout)
        << site << ": the positive control, the session was not established after its Logout";
}

// The liveness loop's TestRequest, drawn by the wire reader (HeartBtInt 10 s and
// test_request_threshold = HeartBtInt, tasks.md "Liveness instants"); its TestReqID is kept.
struct PendingTr {
    std::string id;
    bool operator()(WaitCell& c) {
        auto const tr = t105::advance_until_msg_type(
            *c.rig.clock, c.rig.ioc, [&c] { return c.rig.peer.received; }, "1", 0, 1s,
            t105::liveness_wire_cap(10s, 1s), "the pending TestRequest");
        if (!tr) return false;
        id = pr::field_value(tr->frame, "112");
        return !id.empty();
    }
};

void with_heartbt_10s(SessionConfig& cfg) {
    cfg.heartbeat_interval = 10s;
    cfg.test_request_threshold = std::chrono::milliseconds{10s};
}

// Opens a gap: a Heartbeat at 5 draws fixpp's ResendRequest for 2 onwards.
bool open_gap(WaitCell& c) {
    auto const mark = c.rig.peer.received.size();
    c.rig.peer.send(c.rig.heartbeat(5));
    return c.rig.run_until([&] { return c.count_since(mark, "2") == 1U; });
}

// ════════════════════════════════════════════════════════════════════════════════════════
// T044 — W-2 to W-7 on the SendingTime-Reject route (contract C-2.2): the frames of the wait
// are processed as in Active. The peer's next number is 3 after the route.
// ════════════════════════════════════════════════════════════════════════════════════════

TEST_P(LogoutWait105, AppMessageDuringWait_ReachesFromAppAndAdvances) {
    WaitRun w{GetParam(), route::sending_time_reject};
    expect_in_wait(w, "AppMessage");
    if (w.in_wait) {
        w.c.rig.peer.send(w.c.rig.msg("D", 3, "11=W2\x01"));
        EXPECT_TRUE(w.c.rig.run_until([&] { return w.c.app->from_app == 1; }))
            << "the application message did not reach fromApp";
        EXPECT_EQ(w.c.next_in(), 4U) << "NextNumIn did not advance";
        EXPECT_EQ(w.c.rig.state(), fsm_state::Active);
        EXPECT_TRUE(w.peer_logout_eof(4, "AppMessage"));
    }
    w.c.rig.stop();
}

TEST_P(LogoutWait105, TestRequestDuringWait_Answered) {
    WaitRun w{GetParam(), route::sending_time_reject};
    expect_in_wait(w, "TestRequest");
    if (w.in_wait) {
        auto const mark = w.c.rig.peer.received.size();
        w.c.rig.peer.send(w.c.rig.msg("1", 3, "112=W3\x01"));
        EXPECT_TRUE(w.c.rig.run_until([&] { return w.c.count_since(mark, "0") == 1U; }))
            << "the TestRequest was not answered";
        auto const hb = pr::frames_of_type(w.c.wire_since(mark), "0");
        EXPECT_TRUE(!hb.empty() && pr::field_value(hb.front(), "112") == "W3");
        EXPECT_TRUE(w.peer_logout_eof(4, "TestRequest"));
    }
    w.c.rig.stop();
}

TEST_P(LogoutWait105, ResendRequestDuringWait_Answered) {
    WaitRun w{GetParam(), route::sending_time_reject};
    expect_in_wait(w, "ResendRequest");
    if (w.in_wait) {
        auto const mark = w.c.rig.peer.received.size();
        w.c.rig.peer.send(w.c.rig.msg("2", 3,
                                      "7=1\x01"
                                      "16=0\x01"));
        EXPECT_TRUE(w.c.rig.run_until([&] { return w.c.count_since(mark, "4") >= 1U; }))
            << "the ResendRequest was not answered; the peer read: " << w.c.wire_since(mark);
        EXPECT_EQ(w.c.count_since(mark, "5"), 0U) << "a Logout was resent";
        EXPECT_TRUE(w.peer_logout_eof(4, "ResendRequest"));
    }
    w.c.rig.stop();
}

TEST_P(LogoutWait105, HeartbeatDuringWait_AnswersPendingTestRequest) {
    PendingTr tr;
    WaitRun w{GetParam(), route::sending_time_reject, with_heartbt_10s,
              [&tr](WaitCell& c) { return tr(c); }};
    expect_in_wait(w, "Heartbeat");
    if (w.in_wait) {
        EXPECT_TRUE(session_test_access::test_request_pending(*w.s()))
            << "setup: no TestRequest pending in the wait";
        auto const mark = w.c.rig.peer.received.size();
        w.c.rig.peer.send(w.c.rig.msg("0", 3, "112=" + tr.id + "\x01"));
        EXPECT_TRUE(w.c.rig.run_until([&] {
            return !session_test_access::test_request_pending(*w.s());
        })) << "the Heartbeat did not answer the pending TestRequest";
        EXPECT_EQ(w.c.count_since(mark, "5"), 0U);
        EXPECT_EQ(w.c.rig.state(), fsm_state::Active);
        EXPECT_TRUE(w.peer_logout_eof(4, "Heartbeat"));
    }
    w.c.rig.stop();
}

TEST_P(LogoutWait105, TooHighDuringWait_NotDeliveredResendRequestSent) {
    WaitRun w{GetParam(), route::sending_time_reject};
    expect_in_wait(w, "TooHigh");
    if (w.in_wait) {
        auto const mark = w.c.rig.peer.received.size();
        w.c.rig.peer.send(w.c.rig.msg("D", 6, "11=W6\x01"));
        EXPECT_TRUE(w.c.rig.run_until([&] { return w.c.count_since(mark, "2") == 1U; }))
            << "no ResendRequest for the gap; the peer read: " << w.c.wire_since(mark);
        auto const rr = pr::frames_of_type(w.c.wire_since(mark), "2");
        EXPECT_TRUE(!rr.empty() && pr::field_value(rr.front(), "7") == "3");
        EXPECT_EQ(w.c.app->from_app, 0) << "the frame above NextNumIn was delivered";
        EXPECT_EQ(w.c.next_in(), 3U);
        EXPECT_TRUE(w.peer_logout_eof(7, "TooHigh"));
    }
    w.c.rig.stop();
}

// A resend outstanding before the ending, the gap's deadline (twice HeartBtInt = 2 s) passed
// during the wait (the bound 5000 ms): the next frame above NextNumIn draws no second Logout.
TEST_P(LogoutWait105, GapDeadlinePassedDuringWait_NoSecondLogout) {
    WaitRun w{GetParam(), route::sending_time_reject,
              [](SessionConfig& cfg) {
                  cfg.heartbeat_interval = 1s;
                  cfg.test_request_threshold = std::chrono::milliseconds{1s};
                  cfg.logout_disconnect_timeout_ms = 5000;
              },
              open_gap};
    expect_in_wait(w, "GapDeadlinePassed");
    if (w.in_wait) {
        w.c.advance(2500ms);
        auto const mark = w.c.rig.peer.received.size();
        EXPECT_TRUE(w.c.rig.deliver(w.c.rig.heartbeat(6)));
        EXPECT_EQ(w.c.count_since(mark, "5"), 0U) << "the gap's deadline drew a second Logout";
        EXPECT_EQ(w.c.rig.state(), fsm_state::Active);
        EXPECT_TRUE(w.peer_logout_eof(7, "GapDeadlinePassed"));
    }
    w.c.rig.stop();
}

// ════════════════════════════════════════════════════════════════════════════════════════
// T045 — W-8, absorption: a peer-frame condition that ends the session in Active writes no
// second Logout and no Disconnected during the wait; a Reject its arm writes first is written.
// NextNumIn is asserted after each absorbed frame by W-8's rule for its cause and route; then
// an application message at NextNumIn reaches fromApp, and the peer's Logout ends the wait.
// ════════════════════════════════════════════════════════════════════════════════════════

struct Absorb {
    route r = route::sending_time_reject;
    std::function<void(SessionConfig&)> tweak;
    std::function<bool(WaitCell&)> before;
    std::function<std::string(WaitCell&)> frame;  // the absorbed frame
    seqnum_t next_in_after = 0;                   // NextNumIn after it, by W-8's rule
    std::size_t rejects = 0;                      // Rejects its arm writes first
};

void run_absorbed(session_role role, Absorb const& a, const char* site) {
    WaitRun w{role, a.r, a.tweak, a.before};
    expect_in_wait(w, site);
    if (w.in_wait) {
        auto const mark = w.c.rig.peer.received.size();
        EXPECT_TRUE(w.c.rig.deliver(a.frame(w.c)));
        EXPECT_EQ(w.c.count_since(mark, "5"), 0U) << site << ": a second Logout";
        EXPECT_EQ(w.c.count_since(mark, "3"), a.rejects) << site << ": the arm's Reject";
        EXPECT_EQ(w.c.rig.state(), fsm_state::Active) << site << ": the frame ended the session";
        EXPECT_EQ(w.c.app->logouts, 0) << site << ": onLogout fired at the absorbed frame";
        EXPECT_EQ(w.c.next_in(), a.next_in_after) << site << ": NextNumIn after the frame";
        auto const n = w.c.next_in();
        w.c.rig.peer.send(w.c.rig.msg("D", n, "11=AFTER\x01"));
        EXPECT_TRUE(w.c.rig.run_until([&] { return w.c.app->from_app == 1; }))
            << site << ": the application message after the absorbed frame";
        std::optional<std::pair<seqnum_t, seqnum_t>> at;
        EXPECT_TRUE(w.peer_logout_eof(n + 1, site, &at)) << site << ": EOF after the peer's Logout";
        EXPECT_EQ(at, (std::optional<std::pair<seqnum_t, seqnum_t>>{{1U, 1U}}))
            << site << ": the peer's Logout did not reset";
    }
    w.c.rig.stop();
    EXPECT_EQ(w.c.app->logouts, 1) << site << ": onLogout";
}

std::string stale_heartbeat(WaitCell& c, seqnum_t seq) {
    return pr::message(c.rig.begin_string, "0", seq, "TW", "ISLD", kStaleSendingTime);
}

TEST_P(LogoutWait105, Absorbed_TooLow_ThenAppMessageAndPeerLogout) {
    run_absorbed(GetParam(),
                 {.r = route::sending_time_reject,
                  .tweak = {},
                  .before = {},
                  .frame = [](WaitCell& c) { return c.rig.heartbeat(2); },
                  .next_in_after = 3,
                  .rejects = 0},
                 "TooLow");
}

TEST_P(LogoutWait105, Absorbed_SecondSendingTimeReject_ThenAppMessageAndPeerLogout) {
    run_absorbed(GetParam(),
                 {.r = route::sending_time_reject,
                  .tweak = {},
                  .before = {},
                  .frame = [](WaitCell& c) { return stale_heartbeat(c, 3); },
                  .next_in_after = 4,
                  .rejects = 1},
                 "SecondSendingTimeReject");
}

TEST_P(LogoutWait105, Absorbed_OrigSendingTimeReject_ThenAppMessageAndPeerLogout) {
    run_absorbed(GetParam(),
                 {.r = route::sending_time_reject,
                  .tweak = {},
                  .before = {},
                  .frame =
                      [](WaitCell& c) {
                          return c.rig.msg("0", 3,
                                           "43=Y\x01"
                                           "122=" +
                                               std::string{kLateOrigSendingTime} + "\x01");
                      },
                  .next_in_after = 4,
                  .rejects = 1},
                 "OrigSendingTimeReject");
}

TEST_P(LogoutWait105, Absorbed_TestReqIdMismatchInSequence_ThenAppMessageAndPeerLogout) {
    auto tr = std::make_shared<PendingTr>();
    run_absorbed(GetParam(),
                 {.r = route::sending_time_reject,
                  .tweak = with_heartbt_10s,
                  .before = [tr](WaitCell& c) { return (*tr)(c); },
                  .frame = [](WaitCell& c) { return c.rig.msg("0", 3, "112=NOT-PENDING\x01"); },
                  .next_in_after = 4,
                  .rejects = 0},
                 "TestReqIdMismatchInSequence");
}

// C-5.1's route: with validate_sequence_numbers off, an out-of-sequence Heartbeat.
TEST_P(LogoutWait105, Absorbed_TestReqIdMismatchKnobOffOutOfSequence_ThenAppMessageAndPeerLogout) {
    auto tr = std::make_shared<PendingTr>();
    run_absorbed(GetParam(),
                 {.tweak =
                      [](SessionConfig& cfg) {
                          with_heartbt_10s(cfg);
                          cfg.validate_sequence_numbers = false;
                      },
                  .before = [tr](WaitCell& c) { return (*tr)(c); },
                  .frame = [](WaitCell& c) { return c.rig.msg("0", 9, "112=NOT-PENDING\x01"); },
                  .next_in_after = 3,
                  .rejects = 0},
                 "TestReqIdMismatchKnobOffOutOfSequence");
}

// The too-high Heartbeat that answers a pending TestRequest while a resend is outstanding: the
// gap opened first (so the TestRequest is not answered by the frame that opens it), then the
// TestRequest drawn.
TEST_P(LogoutWait105, Absorbed_TestReqIdMismatchTooHighAnswerPending_ThenAppMessageAndPeerLogout) {
    auto tr = std::make_shared<PendingTr>();
    run_absorbed(GetParam(),
                 {.r = route::sending_time_reject,
                  .tweak = with_heartbt_10s,
                  .before = [tr](WaitCell& c) { return open_gap(c) && (*tr)(c); },
                  .frame = [](WaitCell& c) { return c.rig.msg("0", 7, "112=NOT-PENDING\x01"); },
                  .next_in_after = 3,
                  .rejects = 0},
                 "TestReqIdMismatchTooHighAnswerPending");
}

TEST_P(LogoutWait105, Absorbed_CompIdMismatch_ThenAppMessageAndPeerLogout) {
    run_absorbed(GetParam(),
                 {.r = route::sending_time_reject,
                  .tweak = {},
                  .before = {},
                  .frame =
                      [](WaitCell& c) {
                          return pr::message(c.rig.begin_string, "0", 3, "XX", "ISLD",
                                             c.rig.sending_time());
                      },
                  .next_in_after = 3,
                  .rejects = 0},
                 "CompIdMismatch");
}

TEST_P(LogoutWait105, Absorbed_MsgSeqNumZero_ThenAppMessageAndPeerLogout) {
    run_absorbed(GetParam(),
                 {.r = route::sending_time_reject,
                  .tweak = {},
                  .before = {},
                  .frame = [](WaitCell& c) { return c.rig.heartbeat(0); },
                  .next_in_after = 3,
                  .rejects = 0},
                 "MsgSeqNumZero");
}

// On the too-low route the peer's next frames are below NextNumIn: each is absorbed, and none
// is delivered; the peer's Logout below NextNumIn then ends the wait.
TEST_P(LogoutWait105, TooLowRoute_PeerFramesBelowNextNumIn_Absorbed) {
    WaitRun w{GetParam(), route::too_low};
    expect_in_wait(w, "TooLowRoute");
    if (w.in_wait) {
        auto const mark = w.c.rig.peer.received.size();
        EXPECT_TRUE(w.c.rig.deliver(w.c.rig.heartbeat(2)));
        EXPECT_TRUE(w.c.rig.deliver(w.c.rig.msg("D", 2, "11=LOW\x01")));
        EXPECT_EQ(w.c.count_since(mark, "5"), 0U) << "a second Logout";
        EXPECT_EQ(w.c.app->from_app, 0) << "a frame below NextNumIn was delivered";
        EXPECT_EQ(w.c.rig.state(), fsm_state::Active);
        EXPECT_EQ(w.c.next_in(), 3U);
        EXPECT_TRUE(w.peer_logout_eof(2, "TooLowRoute"));
    }
    w.c.rig.stop();
    EXPECT_EQ(w.c.app->logouts, 1);
}

// ════════════════════════════════════════════════════════════════════════════════════════
// T046 — W-9: an internal failure that writes Disconnected during the wait ends the wait, from
// any root, and the connection closes at once: EOF with the clock still, onLogout once, no
// second Logout. The SendingTime-Reject route; the outbound numbers after it are the Logon 1,
// the Reject 2, the Logout 3.
// ════════════════════════════════════════════════════════════════════════════════════════

void expect_w9(WaitRun& w, std::size_t mark, const char* site) {
    EXPECT_TRUE(w.c.eof_at_once(site)) << site << ": no EOF at once";
    EXPECT_EQ(w.c.count_since(mark, "5"), 0U) << site << ": a second Logout";
    EXPECT_EQ(w.c.rig.state(), fsm_state::Disconnected) << site;
}

// The Heartbeat answering the peer's TestRequest fails its store call (error, Disconnected
// written).
TEST_P(LogoutWait105, StoreFailureDuringWait_EofAtOnce) {
    WaitRun w{GetParam(), route::sending_time_reject};
    expect_in_wait(w, "StoreFailure");
    if (w.in_wait) {
        w.c.st->fail_when = [](t105::store_call k, seqnum_t) {
            return k == t105::store_call::store_outbound;
        };
        auto const mark = w.c.rig.peer.received.size();
        w.c.rig.peer.send(w.c.rig.msg("1", 3, "112=W9\x01"));
        expect_w9(w, mark, "StoreFailure");
    }
    w.c.rig.stop();
    EXPECT_EQ(w.c.app->logouts, 1);
}

// The application message's inbound persist fails (error, none written).
TEST_P(LogoutWait105, PersistFailureDuringWait_EofAtOnce) {
    WaitRun w{GetParam(), route::sending_time_reject};
    expect_in_wait(w, "PersistFailure");
    if (w.in_wait) {
        w.c.st->fail_when = [](t105::store_call k, seqnum_t) {
            return k == t105::store_call::increment_inbound;
        };
        auto const mark = w.c.rig.peer.received.size();
        w.c.rig.peer.send(w.c.rig.msg("D", 3, "11=W9P\x01"));
        expect_w9(w, mark, "PersistFailure");
    }
    w.c.rig.stop();
    EXPECT_EQ(w.c.app->logouts, 1);
}

// A frame above NextNumIn draws a ResendRequest whose number assignment fails (the leaf records
// Disconnected and returns success).
TEST_P(LogoutWait105, TooHighDuringWait_ResendRequestAssignFails_EofAtOnce) {
    WaitRun w{GetParam(), route::sending_time_reject};
    expect_in_wait(w, "ResendRequestAssignFails");
    if (w.in_wait) {
        auto& mgr = session_test_access::seqnum_mgr(*w.s());
        fixpp::session::seqnum_manager_test_access::set_counters(
            mgr, mgr.next_inbound_unsafe(), std::numeric_limits<seqnum_t>::max());
        auto const mark = w.c.rig.peer.received.size();
        w.c.rig.peer.send(w.c.rig.msg("D", 6, "11=W9R\x01"));
        expect_w9(w, mark, "ResendRequestAssignFails");
        EXPECT_EQ(w.c.count_since(mark, "2"), 0U) << "the ResendRequest was written";
    }
    w.c.rig.stop();
    EXPECT_EQ(w.c.app->logouts, 1);
}

// An application payload for Engine::send.
constexpr std::string_view kOrderPayload =
    "35=D\x01"
    "11=SEND105\x01";

[[nodiscard]] std::future<fixpp::core::expected_t<void>> spawn_send(WaitCell& c) {
    return asio::co_spawn(
        c.rig.ioc,
        c.rig.engine->send(c.rig.id,
                           std::as_bytes(std::span{kOrderPayload.data(), kOrderPayload.size()})),
        asio::use_future);
}

// A send admitted before the ending (outbound 2), held in its store call across the ending's
// Reject (3) and Logout (4), then failed: a non-pump root ends the wait.
TEST_P(LogoutWait105, SendHeldInStoreAcrossTheEnding_StoreFails_EofAtOnce) {
    std::future<fixpp::core::expected_t<void>> send;
    WaitRun w{GetParam(), route::sending_time_reject, {}, [&send](WaitCell& c) {
                  // The send's frame reaches the inner MemoryStore first, which refuses a
                  // later number while an earlier one is missing; the call is held after it.
                  c.st->hold_after_inner = true;
                  c.st->hold_when = [](t105::store_call k, seqnum_t seq) {
                      return k == t105::store_call::store_outbound && seq == 2U;
                  };
                  send = spawn_send(c);
                  return c.rig.run_until([&c] { return c.st->parked == 1U; });
              }};
    expect_in_wait(w, "SendHeldStoreFails");
    if (w.in_wait) {
        auto const mark = w.c.rig.peer.received.size();
        w.c.st->fail_held = true;
        w.c.st->release_all();
        expect_w9(w, mark, "SendHeldStoreFails");
        EXPECT_TRUE(w.c.rig.run_until([&] { return rg_ready(send); }));
        if (rg_ready(send)) {
            EXPECT_FALSE(send.get().has_value()) << "the failed send succeeded";
        }
    }
    if (w.c.st) w.c.st->release_all();
    w.c.rig.stop();
    EXPECT_EQ(w.c.app->logouts, 1);
}

// ════════════════════════════════════════════════════════════════════════════════════════
// T047 — W-10 and W-11 (C-2.3, C-2.4): nothing is originated during the wait.
// ════════════════════════════════════════════════════════════════════════════════════════

TEST_P(LogoutWait105, SendDuringWait_Refused) {
    WaitRun w{GetParam(), route::sending_time_reject};
    expect_in_wait(w, "SendDuringWait");
    if (w.in_wait) {
        EXPECT_EQ(w.c.rig.state(), fsm_state::Active) << "the positive control at the send";
        auto const mark = w.c.rig.peer.received.size();
        auto f = spawn_send(w.c);
        EXPECT_TRUE(w.c.rig.run_until([&] { return rg_ready(f); }));
        if (rg_ready(f)) {
            auto const r = f.get();
            EXPECT_EQ(r.has_value() ? fixpp::core::error{} : r.error(),
                      fixpp::core::error::session_invalid_state_for_send);
        }
        EXPECT_EQ(w.c.count_since(mark, "D"), 0U) << "the refused send was written";
        EXPECT_TRUE(w.peer_logout_eof(3, "SendDuringWait"));
    }
    w.c.rig.stop();
}

// The held send's number (2) is below the Logout's (4); the wire order is not asserted
// (L-105-e).
TEST_P(LogoutWait105, SendHeldInStoreAcrossTheEnding_NumberedBeforeTheLogout) {
    std::future<fixpp::core::expected_t<void>> send;
    WaitRun w{GetParam(), route::sending_time_reject, {}, [&send](WaitCell& c) {
                  // The send's frame reaches the inner MemoryStore first, which refuses a
                  // later number while an earlier one is missing; the call is held after it.
                  c.st->hold_after_inner = true;
                  c.st->hold_when = [](t105::store_call k, seqnum_t seq) {
                      return k == t105::store_call::store_outbound && seq == 2U;
                  };
                  send = spawn_send(c);
                  return c.rig.run_until([&c] { return c.st->parked == 1U; });
              }};
    expect_in_wait(w, "SendHeldNumbered");
    if (w.in_wait) {
        w.c.st->release_all();
        EXPECT_TRUE(w.c.rig.run_until([&] { return rg_ready(send); }));
        EXPECT_TRUE(w.c.rig.run_until(
            [&] { return pr::frames_of_type(w.c.rig.peer.received, "D").size() == 1U; }));
        auto const d = pr::frames_of_type(w.c.rig.peer.received, "D");
        auto const lo = pr::frames_of_type(w.c.wire_since(w.e.mark), "5");
        EXPECT_TRUE(!d.empty() && !lo.empty() &&
                    std::stoul(pr::field_value(d.front(), "34")) <
                        std::stoul(pr::field_value(lo.front(), "34")))
            << "the send was numbered after the Logout";
        EXPECT_TRUE(w.peer_logout_eof(3, "SendHeldNumbered"));
    }
    if (w.c.st) w.c.st->release_all();
    w.c.rig.stop();
}

// The liveness loop originates nothing during the wait. HeartBtInt 10 s, test_request_threshold
// = HeartBtInt, the bound 30000 ms so the wait spans the instant tested; the wait armed one
// tick (1 s) before it. Positive control: Active at that instant.
void run_liveness_during_wait(session_role role, bool give_up, const char* site) {
    PendingTr tr;
    WaitRun w{role, route::sending_time_reject,
              [](SessionConfig& cfg) {
                  with_heartbt_10s(cfg);
                  cfg.logout_disconnect_timeout_ms = 30000;
              },
              [&](WaitCell& c) {
                  if (give_up) {
                      // The TestRequest at 10 s; the give-up at 20 s and the guard. The wait is
                      // armed at 19 s.
                      if (!tr(c)) return false;
                      c.advance(9s);
                  } else {
                      // The Heartbeat instant at 10 s from the Logon; the wait armed at 9 s,
                      // and the Logout's own write moves the next one to 19 s.
                      c.advance(9s);
                  }
                  return true;
              }};
    expect_in_wait(w, site);
    if (w.in_wait) {
        auto const mark = w.c.rig.peer.received.size();
        for (int i = 0; i < 12; ++i) w.c.advance(1s);
        EXPECT_EQ(w.c.rig.state(), fsm_state::Active) << site << ": the positive control";
        EXPECT_EQ(w.c.count_since(mark, "0"), 0U) << site << ": a Heartbeat was originated";
        EXPECT_EQ(w.c.count_since(mark, "1"), 0U) << site << ": a TestRequest was originated";
        EXPECT_EQ(w.c.count_since(mark, "5"), 0U) << site << ": a second Logout";
        EXPECT_TRUE(w.peer_logout_eof(3, site));
    }
    w.c.rig.stop();
}

TEST_P(LogoutWait105, LivenessDuringWait_ArmedOneTickBeforeHeartbeat_SendsNothing) {
    run_liveness_during_wait(GetParam(), false, "BeforeHeartbeat");
}

TEST_P(LogoutWait105, LivenessDuringWait_ArmedOneTickBeforeGiveUp_SendsNothing) {
    run_liveness_during_wait(GetParam(), true, "BeforeGiveUp");
}

// ════════════════════════════════════════════════════════════════════════════════════════
// T048 — no wait is armed, and the closers (C-2 "No wait is armed", W-13, W-14, C-2.8).
// ════════════════════════════════════════════════════════════════════════════════════════

// logout_disconnect_timeout_ms 0: no wait, the connection closes after the Logout's write.
TEST_P(LogoutWait105, TimeoutZero_NoWaitEofAfterWrite) {
    WaitCell c;
    auto cfg = route_cfg(c, GetParam(), route::sending_time_reject);
    cfg.logout_disconnect_timeout_ms = 0;
    bool const up = c.start_active(std::move(cfg));
    Ending e;
    bool eof = false;
    if (up) {
        e = drive_route(c, route::sending_time_reject);
        eof = c.eof_at_once("TimeoutZero");
    }
    c.rig.stop();
    EXPECT_TRUE(up && e.logout_on_wire) << e.why;
    EXPECT_TRUE(eof) << "no EOF after the Logout's write with timeout 0";
    EXPECT_EQ(c.app->logouts, 1);
}

// close(terminal), and Engine::stop(), during the wait: one close by the closer.
TEST_P(LogoutWait105, CloseTerminalDuringWait_OneClose) {
    WaitRun w{GetParam(), route::sending_time_reject};
    expect_in_wait(w, "CloseTerminal");
    if (w.in_wait) {
        auto const mark = w.c.rig.peer.received.size();
        auto sess = w.c.rig.session();
        auto f =
            asio::co_spawn(sess->executor().underlying(),
                           sess->close(fixpp::session::close_mode::terminal), asio::use_future);
        EXPECT_TRUE(w.c.eof_at_once("CloseTerminal"));
        EXPECT_TRUE(w.c.rig.run_until([&] { return rg_ready(f); }));
        EXPECT_EQ(w.c.count_since(mark, "5"), 0U) << "close(terminal) wrote a Logout";
        EXPECT_EQ(w.c.app->logouts, 1);
    }
    w.c.rig.stop();
    EXPECT_EQ(w.c.app->logouts, 1);
}

TEST_P(LogoutWait105, EngineStopDuringWait_OneClose) {
    WaitRun w{GetParam(), route::sending_time_reject};
    expect_in_wait(w, "EngineStop");
    std::size_t mark = 0;
    if (w.in_wait) mark = w.c.rig.peer.received.size();
    w.c.rig.stop();
    if (w.in_wait) {
        EXPECT_TRUE(w.c.rig.run_until([&] { return w.c.rig.peer.read_ended; }));
        EXPECT_EQ(t105::ending_of(w.c.rig.peer), t105::peer_ending::eof);
        EXPECT_EQ(w.c.count_since(mark, "5"), 0U);
    }
    EXPECT_EQ(w.c.app->logouts, 1);
}

// The too-low Logout that does not fit inbound_logout_leaf_'s buffer is not transmitted: no wait,
// EOF at once (test_105_connection_close.cpp's too_low_fit recipe: the least peer CompID length
// at which that Logout overflows and the Logons fit).
namespace too_low_fit {
constexpr std::size_t kLeafBuffer = 256;
[[nodiscard]] std::size_t logout_size(std::string const& peer, std::string const& st) {
    std::array<std::byte, 4096> big{};
    auto const r = fixpp::session::build_logout(
        big, 2, "ISLD", peer, "MsgSeqNum too low, expecting 3 but received 2", "FIX.4.2", st);
    return r ? r->size() : 0U;
}
[[nodiscard]] std::size_t logon_size(std::string const& peer, std::string const& st) {
    std::array<std::byte, 4096> big{};
    auto const r = fixpp::session::build_logon(big, 1, "ISLD", peer, "FIX.4.2", 30, st);
    return r ? r->size() : 0U;
}
[[nodiscard]] std::size_t overflowing_length(std::string const& st) {
    for (std::size_t len = 1; len < kLeafBuffer; ++len) {
        std::string const p(len, 'P');
        if (logout_size(p, st) > kLeafBuffer) return logon_size(p, st) <= kLeafBuffer ? len : 0U;
    }
    return 0U;
}
}  // namespace too_low_fit

TEST_P(LogoutWait105, LogoutNotTransmitted_ClosesAtOnce) {
    WaitCell c;
    std::size_t const len = too_low_fit::overflowing_length(c.rig.sending_time());
    EXPECT_NE(len, 0U) << "setup: no CompID length overflows the Logout and fits the Logon";
    std::string const peer(len == 0U ? 1U : len, 'P');
    auto cfg = c.cfg(GetParam());
    cfg.target_comp_id = peer;
    auto& rig = c.rig;
    auto from_peer = [&](std::string_view type, std::uint32_t seq, std::string_view extra = {}) {
        return pr::message(rig.begin_string, type, seq, peer, "ISLD", rig.sending_time(), extra);
    };
    bool up = rig.start(std::move(cfg));
    if (up && rig.role_is_initiator()) {
        up = rig.run_until([&] { return !pr::frames_of_type(rig.peer.received, "A").empty(); });
    } else if (up) {
        up = rig.connect_peer();
    }
    bool eof = false;
    std::string after;
    if (up) {
        rig.peer.send(from_peer("A", 1,
                                "98=0\x01"
                                "108=30\x01"));
        rig.peer.send(from_peer("0", 2));
        up = rig.run_until([&] {
            return rig.state() == fsm_state::Active && c.next_in() == 3U &&
                   !pr::frames_of_type(rig.peer.received, "A").empty();
        });
    }
    if (up) {
        auto const mark = rig.peer.received.size();
        rig.peer.send(from_peer("0", 2));
        eof = c.eof_at_once("LogoutNotTransmitted");
        after = c.wire_since(mark);
    }
    rig.stop();
    EXPECT_TRUE(up) << "setup";
    EXPECT_TRUE(eof) << "no EOF at once for a Logout not transmitted";
    EXPECT_TRUE(pr::frames_of_type(after, "5").empty()) << "the overflowing Logout was written";
    EXPECT_EQ(c.app->logouts, 1);
}

// C-2.8: a too-high Logout while a resend is outstanding sets the pending mark; a frame after
// the gap's deadline draws the deadline's Logout, which claims the mark and arms no wait: EOF
// after its write, with the clock still. HeartBtInt 10 s (the deadline at 20 s), the bound 30 s
// (the 103 mark's own bound past the deadline).
TEST_P(LogoutWait105, PendingTooHighMark_GapDeadlineLogout_EofAfterWrite) {
    WaitCell c;
    auto cfg = route_cfg(c, GetParam(), route::gap_deadline);
    cfg.logout_disconnect_timeout_ms = 30000;
    bool up = c.start_active(std::move(cfg)) && open_gap(c);
    if (up) up = c.rig.deliver(c.rig.msg("5", 6));
    bool logout = false;
    bool eof = false;
    if (up) {
        seqnum_t seq = 7;
        for (int i = 0; i < 3; ++i) {
            c.advance(5s);
            up = up && c.rig.deliver(c.rig.heartbeat(seq++));
        }
        c.advance(5s);
        auto const mark = c.rig.peer.received.size();
        c.rig.peer.send(c.rig.heartbeat(seq++));
        logout = c.rig.run_until([&] { return c.count_since(mark, "5") == 1U; });
        eof = c.eof_at_once("PendingTooHighMark");
    }
    c.rig.stop();
    EXPECT_TRUE(up) << "setup";
    EXPECT_TRUE(logout) << "the frame at the deadline drew no Logout";
    EXPECT_TRUE(eof) << "no EOF after the deadline's Logout with the mark claimed";
}

// ════════════════════════════════════════════════════════════════════════════════════════
// The direct-Session forms (test_105_direct_rig.hpp): no pump, so an ending reached from an
// inbound frame closes at the exit of a later, separately started root or call (L-105-b).
// ════════════════════════════════════════════════════════════════════════════════════════

[[nodiscard]] std::vector<std::byte> stale_frame(t105::rgs::Scene& s, seqnum_t seq) {
    return t105::rgs::Scene::raw("0", seq, "TW", s.sending_time(std::chrono::seconds{-600}));
}

// T046, W-9 on a direct Session: the Heartbeat answering a TestRequest fails its store call
// during the wait; the connection is closed by the next send's exit at the latest.
TEST(LogoutWait105Direct, DirectSession_StoreFailureDuringWait_ClosesAtNextSend) {
    auto gst = std::make_shared<t105::gate_store_state>();
    t105::DirectRig d{session_role::acceptor, [&](SessionConfig& c) {
                          c.store_factory = std::make_shared<t105::GateStoreFactory>(gst);
                      }};
    ASSERT_TRUE(d.to_active()) << "setup";
    (void)d.s.feed(stale_frame(d.s, 2));
    EXPECT_EQ(d.completed("5"), 1U) << "setup: no ending Logout";
    EXPECT_EQ(d.s.state(), fsm_state::Active) << "the positive control: established in the wait";
    gst->fail_when = [](t105::store_call k, seqnum_t) {
        return k == t105::store_call::store_outbound;
    };
    (void)d.s.feed(d.s.test_request(3));
    EXPECT_EQ(d.s.state(), fsm_state::Disconnected) << "the store failure did not end the wait";
    static constexpr std::string_view kPayload = "35=D\x01";
    auto const sent = d.run(
        d.s.session->send(std::as_bytes(std::span{kPayload.data(), kPayload.size()})), "send");
    EXPECT_TRUE(sent && !sent->has_value()) << "the ended session admitted the send";
    EXPECT_TRUE(d.closed()) << "the connection is open after the next send returned";
    EXPECT_EQ(d.app->logouts, 1);
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "close");
}

// T048: no clock, so no wait is armed and the write is not limited; the too-low Logout is
// written, and the next call closes the connection.
TEST(LogoutWait105Direct, NoClock_NoWait_ClosesAtNextCall) {
    t105::DirectRig d{session_role::acceptor, {}, /*with_clock=*/false};
    ASSERT_TRUE(d.to_active()) << "setup";
    (void)d.s.feed(d.s.heartbeat(2));
    (void)d.s.feed(d.s.heartbeat(2));
    EXPECT_EQ(d.completed("5"), 1U) << "setup: no too-low Logout";
    EXPECT_EQ(d.s.state(), fsm_state::Disconnected) << "a wait was armed with no clock";
    static constexpr std::string_view kPayload = "35=D\x01";
    (void)d.run(d.s.session->send(std::as_bytes(std::span{kPayload.data(), kPayload.size()})),
                "send");
    EXPECT_TRUE(d.closed()) << "the connection is open after the next call";
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "close");
}

// T048, W-14: drive_reconnect's drain during the wait ends it at once, with one close by the
// drain; the reconnect installs a new transport.
TEST(LogoutWait105Direct, DriveReconnectDrainDuringWait_OneClose) {
    t105::DirectRig d{session_role::initiator};
    ASSERT_TRUE(d.to_active()) << "setup";
    (void)d.s.feed(stale_frame(d.s, 2));
    EXPECT_EQ(d.completed("5"), 1U) << "setup: no ending Logout";
    EXPECT_EQ(d.s.state(), fsm_state::Active) << "the positive control: established in the wait";
    auto const r = d.run(d.s.session->drive_reconnect(), "drive_reconnect");
    EXPECT_TRUE(r.has_value()) << "drive_reconnect did not return";
    EXPECT_TRUE(r && r->has_value()) << "the reconnect after the drain was refused";
    EXPECT_EQ(d.app->logouts, 1);
    EXPECT_EQ(d.started("5"), 1U) << "a second Logout";
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "close");
}

// T049: a direct Session has no establishment deadline, so its pre-Active wait ends at the
// bound alone. The acceptor's posture refusal (TestMessageIndicator(464)=Y on a production
// session).
TEST(LogoutWait105Direct, PreActive_DirectSession_BoundAloneEndsWait) {
    t105::DirectRig d{session_role::acceptor, [](SessionConfig& c) {
                          c.posture = fixpp::session::session_posture::production;
                      }};
    auto const opened = d.run(d.s.session->open(), "open");
    ASSERT_TRUE(opened && opened->has_value()) << "setup: open()";
    d.s.session->attach_accepted_transport(
        std::make_unique<t105::bwt::BlockingWriteTransport>(d.s.ioc.get_executor(), d.stream, d.bw),
        fixpp::transport::handshake_result{});
    (void)d.s.feed(d.s.frame("A", 1,
                             "98=0\x01"
                             "108=30\x01"
                             "464=Y\x01"));
    EXPECT_EQ(d.completed("5"), 1U) << "setup: no refusal Logout";
    EXPECT_EQ(d.s.state(), fsm_state::Disconnected);
    d.pass(499ms);
    EXPECT_FALSE(d.closed()) << "closed before the bound";
    d.pass(1ms);
    EXPECT_TRUE(d.closed()) << "the pre-Active wait did not end at the bound";
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "close");
}

// The strict-reset refusal's exemption on a direct Session, where no pump closes on the arm's
// error: no wait follows its Logout, so the bound's exit releases the close at once, with the
// clock frozen. A wait armed there would hold the connection until the bound
// (PreActive_DirectSession_BoundAloneEndsWait).
TEST(LogoutWait105Direct, PreActive_DirectSession_StrictReset_NoWaitClosedAtOnce) {
    t105::DirectRig d{session_role::acceptor, [](SessionConfig& c) {
                          c.reset_seqnum_policy_field =
                              fixpp::session::reset_seqnum_policy::bilateral_strict;
                      }};
    auto const opened = d.run(d.s.session->open(), "open");
    ASSERT_TRUE(opened && opened->has_value()) << "setup: open()";
    d.s.session->attach_accepted_transport(
        std::make_unique<t105::bwt::BlockingWriteTransport>(d.s.ioc.get_executor(), d.stream, d.bw),
        fixpp::transport::handshake_result{});
    (void)d.s.feed(d.s.frame("A", 1,
                             "98=0\x01"
                             "108=30\x01"));
    d.s.drain();
    EXPECT_EQ(d.completed("5"), 1U) << "setup: no refusal Logout";
    EXPECT_EQ(d.s.state(), fsm_state::Disconnected);
    EXPECT_TRUE(d.closed()) << "the strict-reset refusal's connection was not closed at once";
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "close");
}

// T050, rows 10 and 11, N-1: the bound expires while the answer to the peer's ResendRequest is
// blocked in its write behind a peer that stopped reading; the transport closes at the bound.
TEST(LogoutWait105Direct, Bound_ExpiresWhileResendAnswerBlocked_EofAtTheBound) {
    t105::DirectRig d{session_role::acceptor};
    ASSERT_TRUE(d.to_active()) << "setup";
    (void)d.s.feed(stale_frame(d.s, 2));
    EXPECT_EQ(d.completed("5"), 1U) << "setup: no ending Logout";
    EXPECT_EQ(d.s.state(), fsm_state::Active) << "the positive control: established in the wait";
    d.hold_next_write();
    auto answer = d.s.feed_spawn(d.s.resend_request(3));
    d.s.drain();
    EXPECT_TRUE(d.parked()) << "setup: the resend answer's write is not held";
    d.pass(499ms);
    EXPECT_FALSE(d.closed()) << "closed before the bound";
    d.pass(1ms);
    EXPECT_TRUE(d.closed()) << "the bound did not close the connection under the blocked answer";
    d.release();
    d.s.drain();
    EXPECT_EQ(d.app->logouts, 1);
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "close");
}

// ════════════════════════════════════════════════════════════════════════════════════════
// T049 — before Active (contract C-2 W-1 to W-15's before-Active column, C-2.6; data-model
// E-3 rows 6, 9, 11), on the plaintext Engine rig. The session is Disconnected after its
// refusal Logout, the connection open; the peer's Logout ends the wait, any other frame is
// drained; the bound, or the establishment deadline when earlier, closes. logon_timeout_ms
// 10 s unless a cell sets it (the establishment deadline on the engine clock, from the
// connection's start); the bound 500 ms.
// ════════════════════════════════════════════════════════════════════════════════════════

[[nodiscard]] int establishment_timeouts(fixpp::session::Session const& s) {
    int n = 0;
    for (auto const& ev : s.recent_events()) {
        if (std::holds_alternative<fixpp::session::session_event_establishment_timeout>(ev)) ++n;
    }
    return n;
}

// Connects (acceptor) or waits for the session's Logon (initiator), then sends the peer's Logon
// with extra; true once the refusal's Logout is on the wire. The mark is taken before that Logon.
bool refuse_before_active(WaitCell& c, std::string const& extra, std::size_t& mark,
                          seqnum_t logon_seq = 1) {
    auto& rig = c.rig;
    if (rig.role_is_initiator()) {
        if (!rig.run_until([&] { return !pr::frames_of_type(rig.peer.received, "A").empty(); })) {
            return false;
        }
    } else if (!rig.connect_peer()) {
        return false;
    }
    mark = rig.peer.received.size();
    rig.peer.send(rig.msg("A", logon_seq,
                          "98=0\x01"
                          "108=30\x01" +
                              extra));
    return rig.run_until([&] { return c.count_since(mark, "5") == 1U; }, std::chrono::seconds{3});
}

// The positive control of every pre-Active cell: the refusal's Logout written, the session
// Disconnected, the connection open.
struct PreActiveWait {
    WaitCell c;
    std::size_t mark = 0;
    bool refused = false;
    bool open_after = false;

    PreActiveWait(session_role role, std::function<void(SessionConfig&)> const& tweak,
                  std::string const& extra, seqnum_t logon_seq = 1) {
        auto cfg = c.cfg(role);
        if (tweak) tweak(cfg);
        if (!c.rig.start(std::move(cfg))) return;
        refused = refuse_before_active(c, extra, mark, logon_seq);
        if (refused) {
            open_after = c.still_open() && c.rig.state() == fsm_state::Disconnected;
        }
    }
};

void production_posture(SessionConfig& cfg) {
    cfg.posture = fixpp::session::session_posture::production;
}

TEST_P(LogoutWait105, PreActive_BoundSurvivesTheRefusalsOwnEdge) {
    PreActiveWait w{GetParam(), production_posture, "464=Y\x01"};
    EXPECT_TRUE(w.refused) << "setup: no refusal Logout";
    EXPECT_TRUE(w.open_after) << "the refusal's own Disconnected closed the connection";
    if (w.refused) {
        w.c.advance(499ms);
        EXPECT_TRUE(w.c.still_open()) << "closed before the bound";
        w.c.rig.clock->advance(1ms);
        EXPECT_TRUE(w.c.eof_at_once("PreActive_BoundSurvives")) << "no EOF at the bound";
    }
    w.c.rig.stop();
}

TEST_P(LogoutWait105, PreActive_FramesDrained) {
    PreActiveWait w{GetParam(), production_posture, "464=Y\x01"};
    EXPECT_TRUE(w.refused && w.open_after) << "setup";
    if (w.refused) {
        auto const mark = w.c.rig.peer.received.size();
        EXPECT_TRUE(w.c.rig.deliver(w.c.rig.heartbeat(2)));
        EXPECT_TRUE(w.c.rig.deliver(w.c.rig.msg("D", 3, "11=PRE\x01")));
        EXPECT_TRUE(w.c.rig.deliver(w.c.rig.msg("1", 4, "112=PRE\x01")));
        EXPECT_TRUE(w.c.wire_since(mark).empty())
            << "a frame drew a reply: " << w.c.wire_since(mark);
        EXPECT_EQ(w.c.app->from_app, 0);
        EXPECT_TRUE(w.c.still_open()) << "a drained frame closed the connection";
        w.c.advance(500ms);
        EXPECT_TRUE(w.c.eof_at_once("PreActive_FramesDrained"));
    }
    w.c.rig.stop();
}

// OQ-1: the peer's Logout ends a pre-Active wait and resets nothing. The acceptor's store holds
// NextNumIn 5, so the peer's Logon at 1 is too low (B-104-6).
TEST(LogoutWait105Acceptor, PreActiveTooLowLogon_PeerLogoutEndsWaitNoReset) {
    WaitCell c;
    fixpp::session::memory_store_test_access::set_counter(*c.st->inner,
                                                          fixpp::session::direction_t::inbound, 5);
    auto cfg = c.cfg(session_role::acceptor);
    bool const up = c.rig.start(std::move(cfg));
    std::size_t mark = 0;
    bool const refused = up && refuse_before_active(c, "", mark);
    bool open_after = false;
    bool eof = false;
    std::optional<std::pair<seqnum_t, seqnum_t>> at;
    if (refused) {
        open_after = c.still_open();
        c.rig.peer.send(c.rig.msg("5", 2));
        eof = c.eof_at_once("PreActiveTooLowLogon", &at);
    }
    c.rig.stop();
    EXPECT_TRUE(refused) << "setup: no too-low Logon refusal";
    EXPECT_TRUE(open_after) << "the refusal closed the connection at once";
    EXPECT_TRUE(eof) << "the peer's Logout did not end the pre-Active wait";
    EXPECT_TRUE(at.has_value() && at->first == 5U) << "the inbound number was reset or moved";
}

// The initiator's LogonSent refusal with a Logout-with-Text (the reply Logon's stale
// SendingTime): the bound's expiry closes.
TEST(LogoutWait105Initiator, PreActive_BoundExpiryCloses) {
    WaitCell c;
    bool const up = c.rig.start(c.cfg(session_role::initiator)) && c.rig.run_until([&] {
        return !pr::frames_of_type(c.rig.peer.received, "A").empty();
    });
    std::size_t mark = 0;
    bool refused = false;
    if (up) {
        mark = c.rig.peer.received.size();
        c.rig.peer.send(pr::message(c.rig.begin_string, "A", 1, "TW", "ISLD", kStaleSendingTime,
                                    "98=0\x01"
                                    "108=30\x01"));
        refused = c.rig.run_until([&] { return c.count_since(mark, "5") == 1U; });
    }
    bool open_before = false;
    bool eof = false;
    if (refused) {
        c.advance(499ms);
        open_before = c.still_open();
        c.rig.clock->advance(1ms);
        eof = c.eof_at_once("PreActive_BoundExpiryCloses");
    }
    c.rig.stop();
    EXPECT_TRUE(refused) << "setup: no LogonSent Logout-with-Text";
    EXPECT_TRUE(open_before) << "closed before the bound";
    EXPECT_TRUE(eof) << "no EOF at the bound";
}

// E-5: the wait armed when the establishment deadline passes; the connection closes at the
// deadline, with no establishment-timeout event (the session recorded its own ending).
TEST(LogoutWait105Acceptor, PreActive_WaitArmedAtEstablishmentDeadline_ClosedNoEvent) {
    PreActiveWait w{session_role::acceptor,
                    [](SessionConfig& cfg) {
                        production_posture(cfg);
                        cfg.logon_timeout_ms = 1000;
                        cfg.logout_disconnect_timeout_ms = 5000;
                    },
                    "464=Y\x01"};
    EXPECT_TRUE(w.refused && w.open_after) << "setup";
    int events = -1;
    if (w.refused) {
        w.c.advance(999ms);
        EXPECT_TRUE(w.c.still_open()) << "closed before the establishment deadline";
        w.c.rig.clock->advance(1ms);
        EXPECT_TRUE(w.c.eof_at_once("PreActive_WaitArmedAtEstablishmentDeadline"));
        if (auto s = w.c.rig.session()) events = establishment_timeouts(*s);
    }
    w.c.rig.stop();
    EXPECT_EQ(events, 0) << "an establishment-timeout event at the wait's close";
}

// [guard: the bound's pre-Active live predicate without its state_ == lifecycle::open term]: the
// base already closes once here; under the mutant close() waits on the bound's join with the
// mock clock still.
TEST_P(LogoutWait105, PreActive_CloseDuringWait_OneClose) {
    PreActiveWait w{GetParam(), production_posture, "464=Y\x01"};
    EXPECT_TRUE(w.refused && w.open_after) << "setup";
    if (w.refused) {
        auto const mark = w.c.rig.peer.received.size();
        auto sess = w.c.rig.session();
        auto f =
            asio::co_spawn(sess->executor().underlying(),
                           sess->close(fixpp::session::close_mode::terminal), asio::use_future);
        EXPECT_TRUE(w.c.eof_at_once("PreActive_CloseDuringWait"));
        EXPECT_TRUE(w.c.rig.run_until([&] { return rg_ready(f); }));
        EXPECT_EQ(w.c.count_since(mark, "5"), 0U) << "a second Logout";
    }
    w.c.rig.stop();
}

// One cell per Logon refusal with a Logout: the refusal's Logout, the connection open after it,
// then the peer's Logout ends the wait.
void expect_refusal_wait_then_eof(PreActiveWait& w, const char* site) {
    EXPECT_TRUE(w.refused) << site << ": setup, no refusal Logout";
    EXPECT_TRUE(w.open_after) << site << ": the refusal closed the connection at once";
    if (w.refused) {
        w.c.rig.peer.send(w.c.rig.msg("5", 2));
        EXPECT_TRUE(w.c.eof_at_once(site)) << site << ": the peer's Logout did not end the wait";
    }
    w.c.rig.stop();
}

TEST_P(LogoutWait105, PreActiveRefusal_PostureMismatch_WaitThenEof) {
    PreActiveWait w{GetParam(), production_posture, "464=Y\x01"};
    expect_refusal_wait_then_eof(w, "PostureMismatch");
}

// The strict-reset refusal is exempt from the R-2 wait (spec.md Clarifications, the Phase 4
// implementation session; contract C-2 "Which Logouts"): its arm returns
// session_seqnum_reset_mismatch, and an arm's error closes the connection at once (data-model E-3
// row 5b). The Logout, then EOF with the clock frozen.
TEST(LogoutWait105Acceptor, PreActiveRefusal_StrictReset_NoWaitEofAtOnce) {
    WaitCell c;
    auto cfg = c.cfg(session_role::acceptor);
    cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_strict;
    bool const up = c.rig.start(std::move(cfg));
    std::size_t mark = 0;
    bool const refused = up && refuse_before_active(c, "", mark);
    bool const eof = refused && c.eof_at_once("StrictReset");
    c.rig.stop();
    EXPECT_TRUE(refused) << "setup: no refusal Logout";
    EXPECT_TRUE(eof) << "the strict-reset refusal did not close the connection at once";
}

TEST(LogoutWait105Acceptor, PreActiveRefusal_TooLowLogon_WaitThenEof) {
    auto seed = [](WaitCell& c) {
        fixpp::session::memory_store_test_access::set_counter(
            *c.st->inner, fixpp::session::direction_t::inbound, 5);
    };
    WaitCell c;
    seed(c);
    bool const up = c.rig.start(c.cfg(session_role::acceptor));
    std::size_t mark = 0;
    bool const refused = up && refuse_before_active(c, "", mark);
    bool const open_after = refused && c.still_open();
    bool eof = false;
    if (refused) {
        c.rig.peer.send(c.rig.msg("5", 2));
        eof = c.eof_at_once("TooLowLogon");
    }
    c.rig.stop();
    EXPECT_TRUE(refused) << "setup";
    EXPECT_TRUE(open_after) << "the refusal closed the connection at once";
    EXPECT_TRUE(eof) << "the peer's Logout did not end the wait";
}

TEST(LogoutWait105Initiator, PreActiveRefusal_LogonSentLogoutWithText_WaitThenEof) {
    WaitCell c;
    bool const up = c.rig.start(c.cfg(session_role::initiator)) && c.rig.run_until([&] {
        return !pr::frames_of_type(c.rig.peer.received, "A").empty();
    });
    std::size_t mark = 0;
    bool refused = false;
    if (up) {
        mark = c.rig.peer.received.size();
        c.rig.peer.send(pr::message(c.rig.begin_string, "A", 1, "TW", "ISLD", kStaleSendingTime,
                                    "98=0\x01"
                                    "108=30\x01"));
        refused = c.rig.run_until([&] { return c.count_since(mark, "5") == 1U; });
    }
    bool const open_after = refused && c.still_open();
    bool eof = false;
    if (refused) {
        c.rig.peer.send(c.rig.msg("5", 2));
        eof = c.eof_at_once("LogonSentLogoutWithText");
    }
    c.rig.stop();
    EXPECT_TRUE(refused) << "setup";
    EXPECT_TRUE(open_after) << "the refusal closed the connection at once";
    EXPECT_TRUE(eof) << "the peer's Logout did not end the wait";
}

// Ruling R-789: the two NextExpectedMsgSeqNum(789) Logouts of honor_peer_next_expected_ are R-2
// endings before Active (LogonReceived for the acceptor, LogonSent for the initiator).
void with_789(SessionConfig& cfg) { cfg.enable_next_expected_msg_seq_num = true; }

TEST_P(LogoutWait105, PreActive789_Invalid_WaitThenEof) {
    PreActiveWait w{GetParam(), with_789, "789=abc\x01"};
    expect_refusal_wait_then_eof(w, "789Invalid");
}

TEST_P(LogoutWait105, PreActive789_TooHigh_WaitThenEof) {
    PreActiveWait w{GetParam(), with_789, "789=999\x01"};
    expect_refusal_wait_then_eof(w, "789TooHigh");
}

// The establishment cap (E-3, "The establishment cap"; research D10): the refusal Logout held in
// its store call when the establishment deadline passes, the establishment time left (1000 ms)
// below the bound (5000 ms): open at the deadline - 1 ms, EOF at it, no event.
TEST(LogoutWait105Acceptor, PreActive_LogoutHeldInStore_ClosedAtEstablishmentDeadlineNoEvent) {
    WaitCell c;
    c.app->to_admin_hook = [st = c.st.get()](std::string_view t) {
        if (t == "5") {
            st->hold_after_inner = true;
            st->hold_when = [](t105::store_call k, seqnum_t) {
                return k == t105::store_call::store_outbound;
            };
        }
    };
    auto cfg = c.cfg(session_role::acceptor);
    production_posture(cfg);
    cfg.logon_timeout_ms = 1000;
    cfg.logout_disconnect_timeout_ms = 5000;
    bool const up = c.rig.start(std::move(cfg)) && c.rig.connect_peer();
    bool held = false;
    bool open_before = false;
    bool eof = false;
    int events = -1;
    if (up) {
        c.rig.peer.send(c.rig.msg("A", 1,
                                  "98=0\x01"
                                  "108=30\x01"
                                  "464=Y\x01"));
        held = c.rig.run_until([&] { return c.st->parked == 1U; });
        c.advance(999ms);
        open_before = c.still_open();
        c.rig.clock->advance(1ms);
        eof = c.eof_at_once("LogoutHeldInStore");
        if (auto s = c.rig.session()) events = establishment_timeouts(*s);
    }
    c.st->release_all();
    c.rig.stop();
    EXPECT_TRUE(up && held) << "setup: the refusal Logout's store call is not held";
    EXPECT_TRUE(open_before) << "closed before the establishment deadline";
    EXPECT_TRUE(eof) << "no EOF at the establishment deadline";
    EXPECT_EQ(events, 0) << "an establishment-timeout event";
}

// The establishment cap with the refusal Logout blocked in its write: an Engine initiator whose
// connection is a BlockingWriteTransport made by its factory (the connect loop's transport), the
// peer's frames pushed into its ScriptedStream. Establishment time left 1000 ms, the bound
// 5000 ms: open at the deadline - 1 ms, closed at it, no establishment-timeout event.
TEST(LogoutWait105Initiator, PreActive_LogoutBlockedInWrite_ClosedAtEstablishmentDeadlineNoEvent) {
    auto app = std::make_shared<WaitApp>();
    pr::Rig rig{app};
    // Declared after the rig: the held write's timer holds an executor of its io_context.
    auto bw = std::make_shared<t105::bwt::blocking_write_state>();
    auto stream = std::make_shared<t105::bwt::ScriptedStream>();
    bw->record_bytes = true;
    stream->hold_open = true;
    auto cfg = rig.cfg(session_role::initiator);
    cfg.transport_factory_override =
        std::make_shared<t105::bwt::BlockingWriteTransportFactory>(stream, bw);
    production_posture(cfg);
    cfg.logon_timeout_ms = 1000;
    cfg.logout_disconnect_timeout_ms = 5000;
    app->to_admin_hook = [b = bw.get()](std::string_view t) {
        if (t == "5") b->arm_block();
    };
    auto started = [&](std::string_view type) {
        std::size_t n = 0;
        for (auto const& w : bw->recorded) {
            if (fixpp::test_support::extract_tag(w.bytes, 35) == type) ++n;
        }
        return n;
    };
    bool up = rig.start(std::move(cfg)) && rig.run_until([&] { return started("A") == 1U; });
    bool held = false;
    bool open_before = false;
    bool closed = false;
    int events = -1;
    if (up) {
        auto const reply = pr::message(rig.begin_string, "A", 1, "TW", "ISLD", rig.sending_time(),
                                       "98=0\x01"
                                       "108=30\x01"
                                       "464=Y\x01");
        stream->push(pr::to_bytes(reply));
        held = rig.run_until([&] { return bw->parked && started("5") == 1U; });
        (void)t105::advance_and_drive(*rig.clock, rig.ioc, 999ms, 256);
        open_before = !stream->closed;
        (void)t105::advance_and_drive(*rig.clock, rig.ioc, 1ms, 256);
        closed = rig.run_until([&] { return stream->closed; }, std::chrono::seconds{2});
        if (auto s = rig.session()) events = establishment_timeouts(*s);
    }
    bw->release();
    rig.stop();
    app->to_admin_hook = nullptr;
    EXPECT_TRUE(up && held) << "setup: the refusal Logout's write is not held";
    EXPECT_TRUE(open_before) << "closed before the establishment deadline";
    EXPECT_TRUE(closed) << "not closed at the establishment deadline";
    EXPECT_EQ(events, 0) << "an establishment-timeout event";
}

// The same with a second mock clock as SessionConfig::clock_override: only the engine clock
// reaches the establishment deadline, so the close comes at it with the override still.
TEST(LogoutWait105Acceptor, PreActive_ClockOverride_CapOnEngineClock) {
    WaitCell c;
    auto override_clock = t105::make_mock_clock(c.rig.ioc.get_executor());
    auto cfg = c.cfg(session_role::acceptor);
    production_posture(cfg);
    cfg.logon_timeout_ms = 1000;
    cfg.logout_disconnect_timeout_ms = 5000;
    cfg.clock_override = override_clock;
    bool const up = c.rig.start(std::move(cfg));
    std::size_t mark = 0;
    bool const refused = up && refuse_before_active(c, "464=Y\x01", mark);
    bool open_before = false;
    bool eof = false;
    int events = -1;
    if (refused) {
        c.advance(999ms);
        open_before = c.still_open();
        c.rig.clock->advance(1ms);
        eof = c.eof_at_once("ClockOverride");
        if (auto s = c.rig.session()) events = establishment_timeouts(*s);
    }
    c.rig.stop();
    EXPECT_TRUE(refused) << "setup";
    EXPECT_TRUE(open_before) << "closed before the establishment deadline";
    EXPECT_TRUE(eof) << "the cap on the engine clock did not close the connection";
    EXPECT_EQ(events, 0)
        << "an establishment-timeout event: the pump's deadline closed it, not the cap";
}

// ════════════════════════════════════════════════════════════════════════════════════════
// T053 — the bound's absorbed exception (data-model E-3 row 16): t105::CountingClock as
// clock_override throws std::bad_alloc from sleep_until for the bound's instant only, which
// detail::await_deadline absorbs and completes early. The bound's instant is the mock clock's
// steady_now() at the frame that draws the Logout (the clock still) plus the bound. Each cell:
// the connection closed at once with the clock still, by the bounded drive.
// ════════════════════════════════════════════════════════════════════════════════════════

struct CountingDirect {
    t105::DirectRig d;
    std::shared_ptr<t105::CountingClock> cc;
    explicit CountingDirect(session_role role,
                            std::function<void(SessionConfig&)> const& tweak = {})
        : d{role, tweak} {
        cc = std::make_shared<t105::CountingClock>(d.s.mock);
        d.s.cfg.clock_override = cc;
        d.s.session = std::make_unique<fixpp::session::Session>(d.s.engine, d.s.cfg);
    }
    [[nodiscard]] fixpp::core::steady_time_point bound_instant() const {
        return d.s.mock->steady_now() + 500ms;
    }
    // Runs handlers one at a time until fut is ready, at most max_handlers.
    template <class Fut>
    bool run_until_ready(Fut const& fut, std::size_t max_handlers = 4096) {
        for (std::size_t i = 0; i < max_handlers && !rg_ready(fut); ++i) {
            if (d.s.ioc.run_one_for(std::chrono::milliseconds{1}) == 0) d.s.ioc.restart();
        }
        return rg_ready(fut);
    }
};

TEST(LogoutWait105Direct, EndingBound_DeadlineArmAbsorbsException_Writing_EofAtOnce) {
    CountingDirect c{session_role::acceptor};
    ASSERT_TRUE(c.d.to_active()) << "setup";
    c.cc->throw_once_at(c.bound_instant());
    c.d.hold_write_of("5");
    auto f = c.d.s.feed_spawn(stale_frame(c.d.s, 2));
    (void)t105::drive_bounded(c.d.s.ioc, 4096);
    EXPECT_FALSE(c.cc->throw_pending()) << "the bound's sleep was never requested";
    EXPECT_TRUE(c.d.closed()) << "the bound's failure during the write did not close at once";
    c.d.release();
    (void)t105::drive_bounded(c.d.s.ioc, 4096);
    EXPECT_EQ(c.d.app->logouts, 1);
    (void)c.d.run(c.d.s.session->close(fixpp::session::close_mode::terminal), "close");
}

TEST(LogoutWait105Direct, EndingBound_DeadlineArmAbsorbsException_Armed_EofAtOnce) {
    CountingDirect c{session_role::acceptor};
    ASSERT_TRUE(c.d.to_active()) << "setup";
    auto const at = c.bound_instant();
    auto f = c.d.s.feed_spawn(stale_frame(c.d.s, 2));
    EXPECT_TRUE(c.run_until_ready(f)) << "setup: the frame did not return";
    EXPECT_EQ(c.d.completed("5"), 1U) << "setup: the Logout is not on the wire";
    // Armed only now: the first race, built while the Logout was written, has not met it.
    c.cc->throw_once_at(at);
    (void)t105::drive_bounded(c.d.s.ioc, 4096);
    EXPECT_FALSE(c.cc->throw_pending()) << "the armed bound's sleep was never requested";
    EXPECT_TRUE(c.d.closed()) << "the bound's failure in the wait did not close at once";
    EXPECT_EQ(c.d.app->logouts, 1);
    (void)c.d.run(c.d.s.session->close(fixpp::session::close_mode::terminal), "close");
}

TEST(LogoutWait105Direct, EndingBound_DeadlineArmAbsorbsException_ArmedBeforeActive_EofAtOnce) {
    CountingDirect c{session_role::acceptor, [](SessionConfig& cfg) {
                         cfg.posture = fixpp::session::session_posture::production;
                     }};
    auto const opened = c.d.run(c.d.s.session->open(), "open");
    ASSERT_TRUE(opened && opened->has_value()) << "setup: open()";
    c.d.s.session->attach_accepted_transport(std::make_unique<t105::bwt::BlockingWriteTransport>(
                                                 c.d.s.ioc.get_executor(), c.d.stream, c.d.bw),
                                             fixpp::transport::handshake_result{});
    auto const at = c.bound_instant();
    auto f = c.d.s.feed_spawn(c.d.s.frame("A", 1,
                                          "98=0\x01"
                                          "108=30\x01"
                                          "464=Y\x01"));
    EXPECT_TRUE(c.run_until_ready(f)) << "setup: the Logon did not return";
    EXPECT_EQ(c.d.completed("5"), 1U) << "setup: no refusal Logout";
    c.cc->throw_once_at(at);
    (void)t105::drive_bounded(c.d.s.ioc, 4096);
    EXPECT_FALSE(c.cc->throw_pending()) << "the armed bound's sleep was never requested";
    EXPECT_TRUE(c.d.closed()) << "the bound's failure in the pre-Active wait did not close at once";
    (void)c.d.run(c.d.s.session->close(fixpp::session::close_mode::terminal), "close");
}

// ════════════════════════════════════════════════════════════════════════════════════════
// T055 — lifetime (ASan lane; B-103-13): close() joins the ending bound and the close frame, so
// the Session can be destroyed once close() has returned. Each cell destroys the Session after
// close() returns and then runs every handler left, so a frame that outlived its join would
// touch a freed Session under ASan.
// ════════════════════════════════════════════════════════════════════════════════════════

void destroy_then_drain(t105::DirectRig& d) {
    d.s.session.reset();
    (void)t105::drive_bounded(d.s.ioc, 4096);
    // The Scene's own teardown reads its Session: a fresh, unopened one.
    d.s.session = std::make_unique<fixpp::session::Session>(d.s.engine, d.s.cfg);
}

// The write limit's close frame spawned while a graceful close() waits on the Logout owner: the
// mock clock reaches the bound well inside close_grace's wall time (logout_disconnect_timeout_ms
// 30000, so close_grace is 30 s of wall time and the bound 30 s of mock time).
TEST(Lifetime105, CloseFrame_SpawnedByWriteLimitDuringGracefulClose) {
    t105::DirectRig d{session_role::acceptor,
                      [](SessionConfig& c) { c.logout_disconnect_timeout_ms = 30000; }};
    ASSERT_TRUE(d.to_active()) << "setup";
    d.hold_write_of("5");
    auto frame = d.s.feed_spawn(stale_frame(d.s, 2));
    d.s.drain();
    EXPECT_TRUE(d.parked()) << "setup: the Logout's write is not held";
    auto closing =
        asio::co_spawn(fixpp::test_support::session_executor_of(*d.s.session, d.s.ioc),
                       d.s.session->close(fixpp::session::close_mode::graceful), asio::use_future);
    // close()'s phase 1 poll-joins the Logout owner by post, so the io_context never goes idle
    // until it ends: the bounded drive, never Scene::drain(), runs the cell from here.
    (void)t105::advance_and_drive(*d.s.mock, d.s.ioc, 29999ms, 20000);
    EXPECT_FALSE(d.closed()) << "closed before the bound";
    (void)t105::advance_and_drive(*d.s.mock, d.s.ioc, 1ms, 20000);
    EXPECT_TRUE(d.closed()) << "no close at the bound";
    bool const returned = d.s.pump_until([&] { return rg_ready(closing); }, 3s);
    EXPECT_TRUE(returned) << "close() did not return after the bound";
    if (!returned) {
        d.release();
        (void)d.s.pump_until([&] { return rg_ready(closing); }, 35s);
    }
    destroy_then_drain(d);
}

// [guard: the bound's join deleted] A wait armed, close(terminal), the Session destroyed.
TEST(Lifetime105, EndingLogoutBound_SessionDestroyedAfterClose) {
    t105::DirectRig d{session_role::acceptor};
    ASSERT_TRUE(d.to_active()) << "setup";
    (void)d.s.feed(stale_frame(d.s, 2));
    auto const closed = d.run(d.s.session->close(fixpp::session::close_mode::terminal), "close");
    EXPECT_TRUE(closed.has_value());
    destroy_then_drain(d);
}

// [guard: the close frame's first join deleted] A class P ending's close frame, spawned at the
// next call's exit, in flight when close() starts; the Session destroyed after it returns.
TEST(Lifetime105, CloseFrame_SessionDestroyedAfterClose) {
    t105::DirectRig d{session_role::acceptor};
    ASSERT_TRUE(d.to_active()) << "setup";
    (void)d.s.feed(t105::rgs::Scene::raw("0", 2, "XX", d.s.sending_time()));
    static constexpr std::string_view kPayload = "35=D\x01";
    auto sent = asio::co_spawn(
        fixpp::test_support::session_executor_of(*d.s.session, d.s.ioc),
        d.s.session->send(std::as_bytes(std::span{kPayload.data(), kPayload.size()})),
        asio::use_future);
    auto closing =
        asio::co_spawn(fixpp::test_support::session_executor_of(*d.s.session, d.s.ioc),
                       d.s.session->close(fixpp::session::close_mode::terminal), asio::use_future);
    EXPECT_TRUE(d.s.pump_until([&] { return rg_ready(closing) && rg_ready(sent); }));
    destroy_then_drain(d);
}

// The Engine twin of WriteLimit105.R1WriteHeld_PeerLogoutArrives_LogoutCompleteThenEof (data-model
// E-3, "Other roots during `writing`"; T077's mutant "the latch consumers' `writing` deferral
// deleted" reads it): the give-up's Logout is held in its store call; the peer's Logout arrives,
// its arm writes nothing (the owner is set) and records Disconnected, and the pump's after-frame
// test leaves the latch to the writer, so the connection stays open, the Logout completes on the
// wire when the store call returns, and then EOF. The mock clock stops at the step that reached
// the give-up, short of the write limit.
TEST_P(LogoutWait105, R1WriteHeld_PeerLogoutArrives_Engine_LogoutCompleteThenEof) {
    WaitCell c;
    auto cfg = c.cfg(GetParam());
    cfg.reset_on_logout = false;
    cfg.heartbeat_interval = 10s;
    cfg.test_request_threshold = std::chrono::milliseconds{10s};
    bool r1_logout_to_admin = false;
    c.app->to_admin_hook = [&r1_logout_to_admin](std::string_view t) {
        if (t == "5") r1_logout_to_admin = true;
    };
    bool const up = c.start_active(std::move(cfg));
    auto const mark = c.rig.peer.received.size();
    bool test_request = false;
    for (int i = 0; up && i < 11 && !test_request; ++i) {
        c.advance(1s);
        test_request = c.count_since(mark, "1") != 0U;
    }
    c.st->hold_after_inner = true;
    c.st->hold_when = [&r1_logout_to_admin](t105::store_call k, seqnum_t) {
        return k == t105::store_call::store_outbound && r1_logout_to_admin;
    };
    for (int i = 0; test_request && i < 12 && c.st->parked == 0U; ++i) c.advance(1s);
    bool const held = c.st->parked == 1U;
    bool open_after_peer_logout = false;
    bool eof = false;
    if (held) {
        c.rig.peer.send(c.rig.msg("5", 2));
        open_after_peer_logout = c.still_open();
        c.st->release_all();
        eof = c.eof_at_once("R1WriteHeld_PeerLogoutArrives_Engine");
    }
    c.st->release_all();
    c.rig.stop();
    EXPECT_TRUE(up && test_request) << "setup: no TestRequest";
    EXPECT_TRUE(held) << "setup: the give-up's Logout was not held in its store call";
    EXPECT_TRUE(open_after_peer_logout) << "the peer's Logout cut the give-up's Logout";
    EXPECT_TRUE(eof) << "no EOF after the give-up's Logout";
    EXPECT_EQ(c.count_since(mark, "5"), 1U) << "the give-up's Logout did not complete on the wire";
}

FIXPP_105_BOTH_ROLES(LogoutWait105);

}  // namespace
