// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_105_connection_close.cpp — 105-transport-liveness, User Story 1 part A
// (tasks.md T018 to T022): every ending of an Engine-managed session closes its connection
// (contract C-1, C-4; research D1, D4).
//
// The oracle is the peer's: its read of the connection ends (plain_rig::Peer records the
// ending, test_105_support.hpp classes it). An "at once" cell holds the mock clock still and
// drives the io_context with t105::pump_until_peer_read_ends, whose wall cap fails as an
// assertion (contract C-1's preamble). The wall-clock timers that could also close a
// connection are named per section; on the plaintext rig the first frame's deadline and the
// establishment deadline run on the engine clock (the mock), there is no TLS handshake, and
// close()'s grace timer runs only under close(graceful), which these cells reach only where a
// section says so.
//
// No ASSERT_* runs between a rig's start() and its stop(): a fatal assertion there would leave
// the engine running past the test (tasks.md, Execution rules). Every observation inside that
// window is an EXPECT_* or a recorded value asserted after stop().
#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
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
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "session/mock_reconnect_factory.hpp"
#include "session/plain_engine_rig.hpp"
#include "session/resend_gap_scene.hpp"
#include "session/test_105_support.hpp"
#include "support/hooked_store.hpp"
#include "support/seqnum_manager_test_access.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"

namespace {

using namespace std::chrono_literals;
using fixpp::session::fsm_state;
using fixpp::session::seqnum_manager_test_access;
using fixpp::session::seqnum_t;
using fixpp::session::session_role;
using fixpp::session::session_test_access;
using fixpp::session::SessionConfig;
namespace pr = fixpp::test_support::plain_rig;
namespace t105 = fixpp::test_support::t105;

// The wall time an "at once" cell waits for the peer's read to end with the mock clock still.
// It bounds the cell's failure, not the close: with no timer of the mock clock due, nothing but
// the mechanism under test can end the read within it.
constexpr std::chrono::milliseconds kWallCap{1500};

// Counts the lifecycle callbacks; can throw from toAdmin for one MsgType once armed; can run a
// hook from onLogout.
class App105 final : public fixpp::session::Application {
public:
    int logons = 0;
    int logouts = 0;
    std::string throw_to_admin_on;  // MsgType(35); empty: never throws
    std::function<void()> on_logout_hook;

    void onLogon(const fixpp::session::SessionId& /*id*/) override { ++logons; }
    void onLogout(const fixpp::session::SessionId& /*id*/) override {
        ++logouts;
        if (on_logout_hook) on_logout_hook();
    }
    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
                 const fixpp::session::SessionId& /*id*/) override {
        if (!throw_to_admin_on.empty() && msg.msg_type() == throw_to_admin_on) {
            throw std::runtime_error("App105: toAdmin");
        }
    }
};

// One plaintext Engine session (tests/session/plain_engine_rig.hpp) and its application.
struct Cell {
    std::shared_ptr<App105> app;
    pr::Rig rig;

    explicit Cell(bool with_application = true)
        : app{with_application ? std::make_shared<App105>() : nullptr}, rig{app} {}

    [[nodiscard]] SessionConfig cfg(session_role role) { return rig.cfg(role); }
    // Active, and the session's Logon (the acceptor's reply) read by the peer, so a mark taken
    // after this call holds every byte the establishment wrote.
    [[nodiscard]] bool start_active(SessionConfig c) {
        return rig.start(std::move(c)) && rig.to_active() && rig.run_until([this] {
            return !pr::frames_of_type(rig.peer.received, "A").empty();
        });
    }

    [[nodiscard]] std::shared_ptr<fixpp::session::Session> session() const { return rig.session(); }

    // The "at once" reading: the peer's read ends within the wall cap with the mock clock
    // still, and it ended in EOF.
    [[nodiscard]] bool eof_at_once(const char* site) {
        bool const ended = t105::pump_until_peer_read_ends(
            rig.ioc, [this] { return rig.peer.read_ended; }, kWallCap, site);
        return ended && t105::ending_of(rig.peer) == t105::peer_ending::eof;
    }
    [[nodiscard]] std::string wire_since(std::size_t mark) const {
        return rig.peer.received.substr(std::min(mark, rig.peer.received.size()));
    }
};

[[nodiscard]] int count_establishment_timeouts(fixpp::session::Session const& s) {
    int n = 0;
    for (auto const& ev : s.recent_events()) {
        if (std::holds_alternative<fixpp::session::session_event_establishment_timeout>(ev)) ++n;
    }
    return n;
}

// The store's next numbers (inbound, outbound), read synchronously from the MemoryStore under a
// GateStore. A MemoryStore call completes without suspending, so a private io_context runs it.
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

// ════════════════════════════════════════════════════════════════════════════════════════
// T018 — class P silent endings after Active (contract C-1 row 1; FR-001, FR-002, FR-008).
// Each cell: Active (the positive control), one frame that reaches the named arm, then with
// the mock clock still the peer reads nothing more and its read ends in EOF. The path check is
// the silence: the arm writes no frame, so a Reject or a Logout on the wire means the frame
// reached another arm.
// ════════════════════════════════════════════════════════════════════════════════════════

class ConnectionClose105 : public ::testing::TestWithParam<session_role> {};

struct SilentReading {
    bool active = false;        // the session was Active before the stimulus
    bool eof = false;           // EOF at once
    bool disconnected = false;  // the session recorded Disconnected
    std::string after;          // what the peer read after the stimulus
    std::string state_before_close;
};

// Runs one silent-ending cell on c: prepare(c) after Active and before the mark, then the
// stimulus the peer sends. Everything is recorded; the caller asserts after stop().
SilentReading run_silent(Cell& c, SessionConfig cfg, std::function<bool(Cell&)> const& prepare,
                         std::function<std::string(Cell&)> const& stimulus, const char* site) {
    SilentReading r;
    r.active = c.start_active(std::move(cfg)) && c.rig.state() == fsm_state::Active;
    if (r.active && prepare) r.active = prepare(c);
    if (r.active) {
        auto const mark = c.rig.peer.received.size();
        c.rig.peer.send(stimulus(c));
        r.eof = c.eof_at_once(site);
        r.disconnected = c.rig.state() == fsm_state::Disconnected;
        r.after = c.wire_since(mark);
    }
    c.rig.stop();
    return r;
}

void expect_silent_eof(SilentReading const& r, const char* site) {
    EXPECT_TRUE(r.active) << site << ": setup, the session did not reach Active";
    EXPECT_TRUE(r.disconnected) << site << ": the arm did not end the session";
    EXPECT_TRUE(r.after.empty()) << site << ": the arm wrote a frame; the stimulus reached another "
                                 << "arm: " << r.after;
    EXPECT_TRUE(r.eof) << site << ": the peer's read did not end in EOF at once";
}

TEST_P(ConnectionClose105, PumpSilentEnding_BeginStringGuard_PeerSeesEofAtOnce) {
    Cell c;
    auto const r = run_silent(
        c, c.cfg(GetParam()), nullptr,
        [](Cell& x) { return pr::message("FIX.4.4", "0", 2, "TW", "ISLD", x.rig.sending_time()); },
        "BeginStringGuard");
    expect_silent_eof(r, "BeginStringGuard");
}

TEST_P(ConnectionClose105, PumpSilentEnding_CompIdGuard_PeerSeesEofAtOnce) {
    Cell c;
    auto const r = run_silent(
        c, c.cfg(GetParam()), nullptr,
        [](Cell& x) {
            return pr::message(x.rig.begin_string, "0", 2, "XX", "ISLD", x.rig.sending_time());
        },
        "CompIdGuard");
    expect_silent_eof(r, "CompIdGuard");
}

TEST_P(ConnectionClose105, PumpSilentEnding_MsgSeqNumZero_PeerSeesEofAtOnce) {
    Cell c;
    auto const r = run_silent(
        c, c.cfg(GetParam()), nullptr, [](Cell& x) { return x.rig.heartbeat(0); }, "MsgSeqNumZero");
    expect_silent_eof(r, "MsgSeqNumZero");
}

// D-3 of dispose_unparseable_: a faulty Logon in Active (field 3 is 35, 34 read before the
// malformed tag).
TEST_P(ConnectionClose105, PumpSilentEnding_D3FaultyLogon_PeerSeesEofAtOnce) {
    Cell c;
    auto const r = run_silent(
        c, c.cfg(GetParam()), nullptr,
        [](Cell& x) {
            return x.rig.msg("A", 2,
                             "98=0\x01"
                             "108=30\x01"
                             "9x9=1\x01");
        },
        "D3FaultyLogon");
    expect_silent_eof(r, "D3FaultyLogon");
}

// The TestReqID mismatch: a Heartbeat whose 112 is not the pending TestRequest's. The
// TestRequest is drawn by the wire reader with test_request_threshold = HeartBtInt (tasks.md,
// Execution rules, "Liveness instants"); the clock is still from the TestRequest on.
TEST_P(ConnectionClose105, PumpSilentEnding_TestReqIdMismatch_PeerSeesEofAtOnce) {
    Cell c;
    auto cfg = c.cfg(GetParam());
    cfg.heartbeat_interval = 10s;
    cfg.test_request_threshold = std::chrono::milliseconds{10s};
    auto const r = run_silent(
        c, std::move(cfg),
        [](Cell& x) {
            auto const tr = t105::advance_until_msg_type(
                *x.rig.clock, x.rig.ioc, [&x] { return x.rig.peer.received; }, "1", 0, 1s,
                t105::liveness_wire_cap(10s, 1s), "TestReqIdMismatch: the TestRequest");
            return tr.has_value();
        },
        [](Cell& x) { return x.rig.msg("0", 2, "112=NOT-THE-PENDING-ONE\x01"); },
        "TestReqIdMismatch");
    expect_silent_eof(r, "TestReqIdMismatch");
}

// A reply leaf whose number assignment fails and returns success: the ResendRequest a frame
// above NextNumIn draws, with the outbound counter at its maximum.
TEST_P(ConnectionClose105, PumpSilentEnding_ReplyLeafAssignFails_PeerSeesEofAtOnce) {
    Cell c;
    auto const r = run_silent(
        c, c.cfg(GetParam()),
        [](Cell& x) {
            auto s = x.session();
            if (!s) return false;
            auto& mgr = session_test_access::seqnum_mgr(*s);
            seqnum_manager_test_access::set_counters(mgr, mgr.next_inbound_unsafe(),
                                                     std::numeric_limits<seqnum_t>::max());
            return true;
        },
        [](Cell& x) { return x.rig.heartbeat(5); }, "ReplyLeafAssignFails");
    expect_silent_eof(r, "ReplyLeafAssignFails");
}

// A too-low frame whose Logout does not fit inbound_logout_leaf_'s buffer: the peer's
// CompID is long enough that the too-low Logout overflows it while the Logons fit
// (test_103_too_low_logout.cpp's B5 recipe).
namespace too_low_fit {
constexpr std::size_t kLeafBuffer = 256;  // inbound_logout_leaf_'s buffer
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
// The least peer CompID length at which the Logout overflows and the Logon fits, or 0.
[[nodiscard]] std::size_t overflowing_length(std::string const& st) {
    for (std::size_t len = 1; len < kLeafBuffer; ++len) {
        std::string const p(len, 'P');
        if (logout_size(p, st) > kLeafBuffer) return logon_size(p, st) <= kLeafBuffer ? len : 0U;
    }
    return 0U;
}
}  // namespace too_low_fit

TEST_P(ConnectionClose105, PumpSilentEnding_TooLowLogoutDoesNotFit_PeerSeesEofAtOnce) {
    Cell c;
    std::size_t const len = too_low_fit::overflowing_length(c.rig.sending_time());
    ASSERT_NE(len, 0U) << "setup: no CompID length overflows the Logout and fits the Logon";
    std::string const peer(len, 'P');
    auto cfg = c.cfg(GetParam());
    cfg.target_comp_id = peer;
    SilentReading r;
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
    if (up) {
        rig.peer.send(from_peer("A", 1,
                                "98=0\x01"
                                "108=30\x01"));
        rig.peer.send(from_peer("0", 2));
        up = rig.run_until([&] {
            auto const s = rig.session();
            return s && s->state() == fsm_state::Active &&
                   session_test_access::seqnum_mgr(*s).next_inbound_unsafe() == 3U &&
                   !pr::frames_of_type(rig.peer.received, "A").empty();
        });
    }
    r.active = up;
    if (up) {
        auto const mark = rig.peer.received.size();
        rig.peer.send(from_peer("0", 2));
        r.eof = c.eof_at_once("TooLowLogoutDoesNotFit");
        r.disconnected = rig.state() == fsm_state::Disconnected;
        r.after = c.wire_since(mark);
    }
    rig.stop();
    expect_silent_eof(r, "TooLowLogoutDoesNotFit");
}

// N-4: a session with no application attached (EngineConfig::application null) closes too.
TEST_P(ConnectionClose105, PumpSilentEnding_NoApplication_PeerSeesEofAtOnce) {
    Cell c{/*with_application=*/false};
    auto const r = run_silent(
        c, c.cfg(GetParam()), nullptr,
        [](Cell& x) { return pr::message("FIX.4.4", "0", 2, "TW", "ISLD", x.rig.sending_time()); },
        "NoApplication");
    expect_silent_eof(r, "NoApplication");
}

// ════════════════════════════════════════════════════════════════════════════════════════
// T019 — the peer's Logout (contract C-1 rows 3 and 5; FR-005). reset_on_logout on, the store
// a GateStore whose numbers the pump's predicate reads on its first pass that sees the peer's
// read ended: the reset unit runs before the close (B-104-1's order), so the store reads (1, 1)
// by then. The
// wall-clock timers: none (plain rig; no graceful close).
// ════════════════════════════════════════════════════════════════════════════════════════

struct LogoutReading {
    bool active = false;
    bool eof = false;
    std::optional<std::pair<seqnum_t, seqnum_t>> store_at_eof;
    std::size_t reply_logouts = 0;
    int logouts = 0;
    std::string after;
};

// Pumps with the mock clock still until the peer's read ends, reading the store at the first
// pass that sees it ended.
bool eof_reading_store(Cell& c, t105::gate_store_state& st,
                       std::optional<std::pair<seqnum_t, seqnum_t>>& at_eof, const char* site) {
    bool const ended = t105::pump_until_peer_read_ends(
        c.rig.ioc,
        [&] {
            if (c.rig.peer.read_ended && !at_eof) at_eof = store_numbers(st);
            return c.rig.peer.read_ended;
        },
        kWallCap, site);
    return ended && t105::ending_of(c.rig.peer) == t105::peer_ending::eof;
}

void expect_reply_then_eof_after_reset(LogoutReading const& r, const char* site) {
    EXPECT_TRUE(r.active) << site << ": setup, the session did not reach Active";
    EXPECT_EQ(r.reply_logouts, 1U) << site << ": the reply Logout; the peer read: " << r.after;
    EXPECT_TRUE(r.eof) << site << ": the peer's read did not end in EOF after the reply";
    EXPECT_EQ(r.store_at_eof, (std::optional<std::pair<seqnum_t, seqnum_t>>{{1U, 1U}}))
        << site << ": the receipt reset unit had not run when the peer read EOF";
    EXPECT_EQ(r.logouts, 1) << site << ": onLogout";
}

TEST_P(ConnectionClose105, PeerLogoutInActive_EofAfterReplyAndReset) {
    Cell c;
    auto st = std::make_shared<t105::gate_store_state>();
    auto cfg = c.cfg(GetParam());
    cfg.reset_on_logout = true;
    cfg.store_factory = std::make_shared<t105::GateStoreFactory>(st);
    LogoutReading r;
    r.active = c.start_active(std::move(cfg));
    if (r.active) {
        auto const mark = c.rig.peer.received.size();
        c.rig.peer.send(c.rig.msg("5", 2));
        r.eof = eof_reading_store(c, *st, r.store_at_eof, "PeerLogoutInActive");
        r.after = c.wire_since(mark);
        r.reply_logouts = pr::frames_of_type(r.after, "5").size();
    }
    c.rig.stop();
    r.logouts = c.app->logouts;
    expect_reply_then_eof_after_reset(r, "PeerLogoutInActive");
}

// The too-high Logout acknowledged by its bound (the bound's route, class D): the mark set,
// fixpp's ResendRequest written, then the bound's deadline on the mock clock
// (logout_disconnect_timeout_ms after the mark), the reply Logout and the reset, then EOF.
TEST_P(ConnectionClose105, TooHighLogoutAcknowledged_Bound_EofAfterReset) {
    Cell c;
    auto st = std::make_shared<t105::gate_store_state>();
    auto cfg = c.cfg(GetParam());
    cfg.reset_on_logout = true;
    cfg.store_factory = std::make_shared<t105::GateStoreFactory>(st);
    auto const bound = std::chrono::milliseconds{cfg.logout_disconnect_timeout_ms};
    LogoutReading r;
    r.active = c.start_active(std::move(cfg));
    bool resend_request = false;
    if (r.active) {
        auto const mark = c.rig.peer.received.size();
        c.rig.peer.send(c.rig.msg("5", 5));
        resend_request =
            c.rig.run_until([&] { return !pr::frames_of_type(c.wire_since(mark), "2").empty(); });
        (void)t105::advance_and_drive(*c.rig.clock, c.rig.ioc, bound + 1ms, 256);
        r.eof = eof_reading_store(c, *st, r.store_at_eof, "TooHighLogoutAcknowledged_Bound");
        r.after = c.wire_since(mark);
        r.reply_logouts = pr::frames_of_type(r.after, "5").size();
    }
    c.rig.stop();
    r.logouts = c.app->logouts;
    EXPECT_TRUE(resend_request) << "setup: the too-high Logout drew no ResendRequest";
    expect_reply_then_eof_after_reset(r, "TooHighLogoutAcknowledged_Bound");
}

// The too-high Logout acknowledged at once, by the pump's frame (logout_disconnect_timeout_ms
// 0): the reply Logout and the reset, then EOF.
TEST_P(ConnectionClose105, TooHighLogoutAcknowledged_AtOnce_EofAfterReset) {
    Cell c;
    auto st = std::make_shared<t105::gate_store_state>();
    auto cfg = c.cfg(GetParam());
    cfg.reset_on_logout = true;
    cfg.logout_disconnect_timeout_ms = 0;
    cfg.store_factory = std::make_shared<t105::GateStoreFactory>(st);
    LogoutReading r;
    r.active = c.start_active(std::move(cfg));
    if (r.active) {
        auto const mark = c.rig.peer.received.size();
        c.rig.peer.send(c.rig.msg("5", 5));
        r.eof = eof_reading_store(c, *st, r.store_at_eof, "TooHighLogoutAcknowledged_AtOnce");
        r.after = c.wire_since(mark);
        r.reply_logouts = pr::frames_of_type(r.after, "5").size();
    }
    c.rig.stop();
    r.logouts = c.app->logouts;
    EXPECT_TRUE(pr::frames_of_type(r.after, "2").empty()) << "no ResendRequest at once";
    expect_reply_then_eof_after_reset(r, "TooHighLogoutAcknowledged_AtOnce");
}

// ════════════════════════════════════════════════════════════════════════════════════════
// T020 — the detached roots and application calls (contract C-1 rows 8, 9, 10; C-1.c).
// Liveness cells: HeartBtInt 10 s and test_request_threshold = HeartBtInt; the mock clock is
// stepped until the liveness loop has ended the session (its emission fails, so there is no
// frame for the wire reader to find), then held still. The wall-clock timers: none.
// ════════════════════════════════════════════════════════════════════════════════════════

// Steps the mock clock until done() or the cap; the cap is an assertion.
bool advance_until(Cell& c, std::function<bool()> const& done, std::chrono::nanoseconds cap,
                   const char* site) {
    std::chrono::nanoseconds advanced{0};
    for (;;) {
        (void)t105::drive_bounded(c.rig.ioc, 64);
        if (done()) return true;
        if (advanced + 1s > cap) {
            ADD_FAILURE() << site << ": not reached within the mock-clock cap";
            return false;
        }
        c.rig.clock->advance(1s);
        advanced += 1s;
    }
}

SessionConfig liveness_cfg(Cell& c, session_role role) {
    auto cfg = c.cfg(role);
    cfg.heartbeat_interval = 10s;
    cfg.test_request_threshold = std::chrono::milliseconds{10s};
    return cfg;
}

struct RootReading {
    bool active = false;
    bool ended = false;  // the root ended the session
    bool eof = false;
    std::string after;
};

RootReading run_liveness_failure(Cell& c, SessionConfig cfg, std::function<void(Cell&)> const& arm,
                                 const char* site) {
    RootReading r;
    r.active = c.start_active(std::move(cfg));
    if (r.active) {
        auto const mark = c.rig.peer.received.size();
        arm(c);
        r.ended = advance_until(
            c, [&] { return c.rig.state() != fsm_state::Active; }, t105::liveness_wire_cap(10s, 1s),
            site);
        r.eof = c.eof_at_once(site);
        r.after = c.wire_since(mark);
    }
    c.rig.stop();
    return r;
}

void expect_root_eof(RootReading const& r, const char* site) {
    EXPECT_TRUE(r.active) << site << ": setup, the session did not reach Active";
    EXPECT_TRUE(r.ended) << site << ": the root did not end the session";
    EXPECT_TRUE(r.eof) << site << ": the peer's read did not end in EOF once the root exited";
}

TEST_P(ConnectionClose105, LivenessToAdminThrows_EofAtOnce) {
    Cell c;
    auto const r = run_liveness_failure(
        c, liveness_cfg(c, GetParam()), [](Cell& x) { x.app->throw_to_admin_on = "0"; },
        "LivenessToAdminThrows");
    expect_root_eof(r, "LivenessToAdminThrows");
    EXPECT_TRUE(pr::frames_of_type(r.after, "0").empty()) << "the Heartbeat was sent";
}

TEST_P(ConnectionClose105, LivenessAssignFails_EofAtOnce) {
    Cell c;
    auto const r = run_liveness_failure(
        c, liveness_cfg(c, GetParam()),
        [](Cell& x) {
            auto s = x.session();
            if (!s) return;
            auto& mgr = session_test_access::seqnum_mgr(*s);
            seqnum_manager_test_access::set_counters(mgr, mgr.next_inbound_unsafe(),
                                                     std::numeric_limits<seqnum_t>::max());
        },
        "LivenessAssignFails");
    expect_root_eof(r, "LivenessAssignFails");
    EXPECT_TRUE(pr::frames_of_type(r.after, "0").empty()) << "the Heartbeat was sent";
}

TEST_P(ConnectionClose105, LivenessStoreFails_EofAtOnce) {
    Cell c;
    auto st = std::make_shared<t105::gate_store_state>();
    auto cfg = liveness_cfg(c, GetParam());
    cfg.store_factory = std::make_shared<t105::GateStoreFactory>(st);
    auto const r = run_liveness_failure(
        c, std::move(cfg),
        [st](Cell&) {
            st->fail_when = [](t105::store_call k, seqnum_t) {
                return k == t105::store_call::store_outbound;
            };
        },
        "LivenessStoreFails");
    expect_root_eof(r, "LivenessStoreFails");
    EXPECT_GE(st->calls_failed, 1U) << "setup: the store call did not fail";
    EXPECT_TRUE(pr::frames_of_type(r.after, "0").empty()) << "the Heartbeat was sent";
}

// Session::send on an Engine session whose store fails the send's store call (a persistent
// store: nothing transmitted, Disconnected): EOF once the call has returned.
TEST_P(ConnectionClose105, SendStoreFailure_EofAfterSendReturns) {
    Cell c;
    auto st = std::make_shared<t105::gate_store_state>();
    auto cfg = c.cfg(GetParam());
    cfg.store_factory = std::make_shared<t105::GateStoreFactory>(st);
    RootReading r;
    bool returned = false;
    bool failed = false;
    r.active = c.start_active(std::move(cfg));
    if (r.active) {
        auto const mark = c.rig.peer.received.size();
        st->fail_when = [](t105::store_call k, seqnum_t) {
            return k == t105::store_call::store_outbound;
        };
        auto s = c.session();
        static constexpr char kPayload[] = "35=D\x01";
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s, c.rig.ioc),
                                  s->send(std::as_bytes(std::span{kPayload, sizeof(kPayload) - 1})),
                                  asio::use_future);
        returned = c.rig.run_until([&] { return fut.wait_for(0s) == std::future_status::ready; });
        if (returned) failed = !fut.get().has_value();
        r.ended = c.rig.state() == fsm_state::Disconnected;
        r.eof = c.eof_at_once("SendStoreFailure");
        r.after = c.wire_since(mark);
    }
    c.rig.stop();
    EXPECT_TRUE(returned) << "setup: the send did not return";
    EXPECT_TRUE(failed) << "setup: the send did not fail";
    expect_root_eof(r, "SendStoreFailure");
    EXPECT_TRUE(pr::frames_of_type(r.after, "D").empty()) << "the message was transmitted";
}

// An Engine-managed initiator on the engine's connect loop, ended in Active by toAdmin
// throwing on the liveness Heartbeat (as LivenessToAdminThrows): EOF with the clock still,
// the session ended terminally (the connect loop's single cycle ends, spec Assumptions) and
// no second connection to the peer's listener.
TEST(ConnectionClose105Engine, EngineInitiator_EndingOnConnectLoop_NoHeldTransport_CycleEnds) {
    Cell c;
    auto cfg = liveness_cfg(c, session_role::initiator);
    RootReading r;
    r.active = c.start_active(std::move(cfg));
    bool second_connection = false;
    bool closed = false;
    if (r.active) {
        c.app->throw_to_admin_on = "0";
        r.ended = advance_until(
            c, [&] { return c.rig.state() != fsm_state::Active; }, t105::liveness_wire_cap(10s, 1s),
            "EngineInitiator");
        r.eof = c.eof_at_once("EngineInitiator");
        auto s = c.session();
        closed = s && session_test_access::is_drained(*s);
        // A second connection would be accepted on the rig's listener.
        asio::ip::tcp::socket second{c.rig.ioc};
        c.rig.listener->async_accept(second, [&](std::error_code const& ec) {
            if (!ec) second_connection = true;
        });
        c.rig.settle(std::chrono::milliseconds{300});
        std::error_code ec;
        c.rig.listener->cancel(ec);
        c.rig.settle(std::chrono::milliseconds{20});
    }
    c.rig.stop();
    expect_root_eof(r, "EngineInitiator");
    EXPECT_TRUE(closed) << "the session was not closed terminally once the connection ended";
    EXPECT_FALSE(second_connection) << "the connect loop connected a second time";
}

// ════════════════════════════════════════════════════════════════════════════════════════
// T021 — before Active, and the acceptor's first frame (contract C-4; FR-006, FR-007).
// logon_timeout_ms is short on the mock clock; the clock is still while the close is awaited,
// so the establishment deadline cannot close the connection. Then the clock is advanced past
// the deadline: no session_event_establishment_timeout and no establishment-timeout record
// may follow, since the refusal was not a timeout. The wall-clock timers: none on this rig
// (the first frame's deadline is on the engine clock).
// ════════════════════════════════════════════════════════════════════════════════════════

constexpr std::uint32_t kShortLogonTimeoutMs = 2000;
constexpr char kTimeoutRecordFormat[] = "session not established within logon_timeout_ms={}";

struct PreActiveReading {
    bool refused = false;  // the session recorded Disconnected
    bool eof = false;
    int timeout_events = 0;
    std::size_t timeout_records = 0;
    std::string after;
};

// The acceptor's peer connects and sends frames(c); the initiator's peer waits for the engine's
// Logon and answers with frames(c).
PreActiveReading run_pre_active(session_role role, std::function<std::string(Cell&)> const& frames,
                                const char* site,
                                std::function<void(SessionConfig&)> const& tweak = {}) {
    Cell c;
    t105::LogCapture log;
    auto cfg = c.cfg(role);
    cfg.logon_timeout_ms = kShortLogonTimeoutMs;
    cfg.logger_override = log.logger;
    if (tweak) tweak(cfg);
    PreActiveReading r;
    bool up = c.rig.start(std::move(cfg));
    if (up && c.rig.role_is_initiator()) {
        up = c.rig.run_until([&] {
            return !pr::frames_of_type(c.rig.peer.received, "A").empty() &&
                   c.rig.state() == fsm_state::LogonSent;
        });
    } else if (up) {
        up = c.rig.connect_peer();
    }
    if (up) {
        auto const mark = c.rig.peer.received.size();
        c.rig.peer.send(frames(c));
        r.eof = c.eof_at_once(site);
        r.refused = c.rig.state() == fsm_state::Disconnected;
        r.after = c.wire_since(mark);
        (void)t105::advance_and_drive(*c.rig.clock, c.rig.ioc,
                                      std::chrono::milliseconds{kShortLogonTimeoutMs} + 1s, 256);
        c.rig.settle(std::chrono::milliseconds{50});
        if (auto s = c.session()) r.timeout_events = count_establishment_timeouts(*s);
    }
    c.rig.stop();
    r.timeout_records = log.records_of(FIXPP_FORMAT_ID(kTimeoutRecordFormat)).size();
    EXPECT_TRUE(up) << site << ": setup, the peer did not reach the session";
    return r;
}

void expect_refused_eof_no_timeout(PreActiveReading const& r, const char* site) {
    EXPECT_TRUE(r.refused) << site << ": the refusal did not end the session";
    EXPECT_TRUE(r.after.empty()) << site << ": the refusal wrote a frame: " << r.after;
    EXPECT_TRUE(r.eof) << site << ": the peer's read did not end in EOF before the deadline";
    EXPECT_EQ(r.timeout_events, 0) << site << ": a session_event_establishment_timeout";
    EXPECT_EQ(r.timeout_records, 0U) << site << ": an establishment-timeout record";
}

std::string peer_logon(Cell& c, std::uint32_t seq = 1,
                       std::string_view extra =
                           "98=0\x01"
                           "108=30\x01") {
    return c.rig.msg("A", seq, extra);
}

// A frame whose third field is not MsgType(35): NotConnected disregards it as garbled.
std::string disregarded_frame(Cell& c) {
    std::string const body =
        "49=TW\x01"
        "35=0\x01"
        "34=1\x01"
        "52=" +
        c.rig.sending_time() +
        "\x01"
        "56=ISLD\x01";
    return pr::frame(c.rig.begin_string, body);
}

class PreActive105 : public ::testing::TestWithParam<session_role> {};

// 092's D-1 (acceptor, NotConnected) and D-2 (initiator, LogonSent): a faulty Logon.
TEST_P(PreActive105, PreActiveRefusal_D1D2_FaultyLogon_EofBeforeDeadlineNoTimeoutEvent) {
    char const* const site = GetParam() == session_role::acceptor ? "D-1" : "D-2";
    auto const r = run_pre_active(
        GetParam(),
        [](Cell& c) {
            return peer_logon(c, 1,
                              "98=0\x01"
                              "108=30\x01"
                              "9x9=1\x01");
        },
        site);
    expect_refused_eof_no_timeout(r, site);
}

// The NotConnected refusal (acceptor) and the LogonSent refusal (initiator): a Heartbeat where
// a Logon is due.
TEST_P(PreActive105,
       PreActiveRefusal_NotConnectedOrLogonSent_NotALogon_EofBeforeDeadlineNoTimeoutEvent) {
    char const* const site = GetParam() == session_role::acceptor ? "NotConnected" : "LogonSent";
    auto const r = run_pre_active(GetParam(), [](Cell& c) { return c.rig.heartbeat(1); }, site);
    expect_refused_eof_no_timeout(r, site);
}

// MsgSeqNum 0 on the Logon.
TEST_P(PreActive105, PreActiveRefusal_SeqNum0_EofBeforeDeadlineNoTimeoutEvent) {
    auto const r = run_pre_active(GetParam(), [](Cell& c) { return peer_logon(c, 0); }, "SeqNum0");
    expect_refused_eof_no_timeout(r, "SeqNum0");
}

// [guard: expire() closes without its event] C-4.3 (B-093-4): a peer that does not complete a
// Logon is closed at logon_timeout_ms with the event and the record. Initiator: silent after
// the engine's Logon. Acceptor: a first frame NotConnected disregards, then silence. Open at
// the deadline - 1 ms; EOF at the deadline.
TEST_P(PreActive105, SilentPeer_StillClosedAtEstablishmentDeadlineWithEvent) {
    Cell c;
    t105::LogCapture log;
    auto cfg = c.cfg(GetParam());
    cfg.logon_timeout_ms = kShortLogonTimeoutMs;
    cfg.logger_override = log.logger;
    bool up = c.rig.start(std::move(cfg));
    if (up && c.rig.role_is_initiator()) {
        up = c.rig.run_until([&] { return c.rig.state() == fsm_state::LogonSent; });
    } else if (up) {
        up = c.rig.connect_peer() && c.rig.deliver(disregarded_frame(c));
    }
    bool open_before = false;
    bool eof = false;
    int events = 0;
    if (up) {
        // The deadline counts from the connection's start, and the clock has not moved since.
        (void)t105::advance_and_drive(*c.rig.clock, c.rig.ioc,
                                      std::chrono::milliseconds{kShortLogonTimeoutMs - 1}, 256);
        c.rig.settle(std::chrono::milliseconds{50});
        open_before = !c.rig.peer.read_ended;
        (void)t105::advance_and_drive(*c.rig.clock, c.rig.ioc, 1ms, 256);
        eof = c.eof_at_once("SilentPeer");
        if (auto s = c.session()) events = count_establishment_timeouts(*s);
    }
    c.rig.stop();
    auto const records = log.records_of(FIXPP_FORMAT_ID(kTimeoutRecordFormat)).size();
    EXPECT_TRUE(up) << "setup";
    EXPECT_TRUE(open_before) << "closed before the establishment deadline";
    EXPECT_TRUE(eof) << "not closed at the establishment deadline";
    EXPECT_EQ(events, 1) << "session_event_establishment_timeout";
    EXPECT_EQ(records, 1U) << "the establishment-timeout record";
}

FIXPP_105_BOTH_ROLES(PreActive105);

// The identity guard: the Logon's BeginString differs. Initiator only: an acceptor's first frame
// with another BeginString matches no registered session and makes none.
TEST(PreActive105Initiator, PreActiveRefusal_IdGuard_EofBeforeDeadlineNoTimeoutEvent) {
    auto const r = run_pre_active(
        session_role::initiator,
        [](Cell& c) {
            return pr::message("FIX.4.4", "A", 1, "TW", "ISLD", c.rig.sending_time(),
                               "98=0\x01"
                               "108=30\x01");
        },
        "IdGuard");
    expect_refused_eof_no_timeout(r, "IdGuard");
}

// The acceptor's first frame whose arm returns an error: the reply Logon's store call fails on a
// persistent store.
TEST(AcceptorFirstFrame105, AcceptorFirstFrameError_EofAtOnce) {
    auto st = std::make_shared<t105::gate_store_state>();
    st->fail_when = [](t105::store_call k, seqnum_t) {
        return k == t105::store_call::store_outbound;
    };
    auto const r = run_pre_active(
        session_role::acceptor, [](Cell& c) { return peer_logon(c); }, "AcceptorFirstFrameError",
        [st](SessionConfig& cfg) {
            cfg.store_factory = std::make_shared<t105::GateStoreFactory>(st);
        });
    EXPECT_GE(st->calls_failed, 1U) << "setup: the reply Logon's store call did not fail";
    EXPECT_TRUE(r.eof) << "the peer's read did not end in EOF at once";
    EXPECT_EQ(r.timeout_events, 0) << "a session_event_establishment_timeout";
    EXPECT_EQ(r.timeout_records, 0U) << "an establishment-timeout record";
}

// The acceptor's first frame refused: a Logon with no HeartBtInt(108).
TEST(AcceptorFirstFrame105, AcceptorFirstFrameRefused_EofAtOnceNoTimeoutEvent) {
    auto const r = run_pre_active(
        session_role::acceptor, [](Cell& c) { return peer_logon(c, 1, "98=0\x01"); },
        "AcceptorFirstFrameRefused");
    expect_refused_eof_no_timeout(r, "AcceptorFirstFrameRefused");
}

// C-4.5: a first frame the acceptor takes without refusing (well framed, its third field not
// MsgType, so NotConnected disregards it), then a later frame refused before Active.
TEST(AcceptorFirstFrame105, AcceptorFirstFrameAcceptedLaterFrameRefused_EofAtOnce) {
    auto const r = run_pre_active(
        session_role::acceptor, [](Cell& c) { return disregarded_frame(c) + c.rig.heartbeat(2); },
        "AcceptorFirstFrameAcceptedLaterFrameRefused");
    expect_refused_eof_no_timeout(r, "AcceptorFirstFrameAcceptedLaterFrameRefused");
}

// ════════════════════════════════════════════════════════════════════════════════════════
// T022 — one close per ending (contract C-1.a; class X). The wall-clock timers: close()'s grace
// timer runs only in InsideClose_NoSecondClose, whose graceful close ends on the peer's Logout
// well inside it.
// ════════════════════════════════════════════════════════════════════════════════════════

// onLogout starts close(): the edge's owe and close()'s clear give one close. The close is at
// once with the mock clock still, onLogout fires once and the close returns.
TEST_P(ConnectionClose105, OnLogoutCallsClose_OneClose) {
    Cell c;
    std::optional<std::future<fixpp::core::expected_t<void>>> close_fut;
    c.app->on_logout_hook = [&] {
        auto s = c.session();
        if (!s) return;
        close_fut =
            asio::co_spawn(fixpp::test_support::session_executor_of(*s, c.rig.ioc),
                           s->close(fixpp::session::close_mode::terminal), asio::use_future);
    };
    bool const active = c.start_active(c.cfg(GetParam()));
    bool eof = false;
    bool close_done = false;
    if (active) {
        c.rig.peer.send(pr::message("FIX.4.4", "0", 2, "TW", "ISLD", c.rig.sending_time()));
        eof = c.eof_at_once("OnLogoutCallsClose");
        close_done = close_fut.has_value() && c.rig.run_until([&] {
            return close_fut->wait_for(0s) == std::future_status::ready;
        });
        if (close_done) (void)close_fut->get();
    }
    c.rig.stop();
    EXPECT_TRUE(active) << "setup";
    EXPECT_TRUE(eof) << "the peer's read did not end in EOF at once";
    EXPECT_TRUE(close_done) << "the close() onLogout started did not return";
    EXPECT_EQ(c.app->logouts, 1) << "onLogout";
}

// Engine::stop() started as soon as the ending is recorded, racing its close: the stop returns
// and the peer reads EOF.
TEST_P(ConnectionClose105, EngineStopRacesEndingClose_OneClose) {
    Cell c;
    bool const active = c.start_active(c.cfg(GetParam()));
    bool eof = false;
    bool stopped = false;
    if (active) {
        c.rig.peer.send(pr::message("FIX.4.4", "0", 2, "TW", "ISLD", c.rig.sending_time()));
        (void)c.rig.run_until([&] { return c.rig.state() == fsm_state::Disconnected; });
        auto fut = asio::co_spawn(c.rig.ioc, c.rig.engine->stop(), asio::use_future);
        eof = c.eof_at_once("EngineStopRacesEndingClose");
        stopped = fixpp::test_support::pump_until_ready(c.rig.ioc, fut, kWallCap,
                                                        "EngineStopRacesEndingClose");
        if (stopped) fut.get();
    }
    if (!stopped) c.rig.stop();
    EXPECT_TRUE(active) << "setup";
    EXPECT_TRUE(eof) << "the peer's read did not end in EOF";
    EXPECT_TRUE(stopped) << "Engine::stop() did not return";
}

// [guard: the latch's lifecycle term deleted] a graceful close() in Active: its Logout, the
// peer's Logout, then the close() that owns the connection closes it, and returns success.
TEST_P(ConnectionClose105, InsideClose_NoSecondClose) {
    Cell c;
    bool const active = c.start_active(c.cfg(GetParam()));
    bool eof = false;
    bool close_ok = false;
    std::string after;
    if (active) {
        auto const mark = c.rig.peer.received.size();
        auto s = c.session();
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s, c.rig.ioc),
                                  s->close(fixpp::session::close_mode::graceful), asio::use_future);
        (void)c.rig.run_until([&] { return !pr::frames_of_type(c.wire_since(mark), "5").empty(); });
        c.rig.peer.send(c.rig.msg("5", 2));
        eof = c.eof_at_once("InsideClose");
        bool const done =
            c.rig.run_until([&] { return fut.wait_for(0s) == std::future_status::ready; });
        close_ok = done && fut.get().has_value();
        after = c.wire_since(mark);
    }
    c.rig.stop();
    EXPECT_TRUE(active) << "setup";
    EXPECT_EQ(pr::frames_of_type(after, "5").size(), 1U) << "close()'s Logout: " << after;
    EXPECT_TRUE(eof) << "the peer's read did not end in EOF";
    EXPECT_TRUE(close_ok) << "close(graceful) did not return success";
    EXPECT_EQ(c.app->logouts, 1) << "onLogout";
}

// [guard: the latch's lifecycle term deleted] A class P ending (the BeginString guard) recorded
// while a graceful close() is in its Logout's store call. close() owns the connection from its
// commit of closing, so the ending owes nothing and the pump does not close the session: the
// Logout's store call runs to its end (FR-008), the Logout is written, and the close() that owns
// the connection closes it and returns success. A second consumer of the ending would close the
// session terminally inside the graceful close, which escalates it (093 OD-29) and cancels its
// phase 1 in the held store call, so the Logout is not written. The wall-clock timers: close()'s
// grace timer, set here above every wait of the cell, and none other (the GateStore hold is
// released only by the cell).
TEST_P(ConnectionClose105, InsideClose_EndingDuringItsLogoutStore_NoSecondClose) {
    Cell c;
    auto st = std::make_shared<t105::gate_store_state>();
    auto cfg = c.cfg(GetParam());
    cfg.store_factory = std::make_shared<t105::GateStoreFactory>(st);
    cfg.logout_disconnect_timeout_ms = 30000;
    bool const active = c.start_active(std::move(cfg));
    bool held = false;
    bool ended = false;
    bool eof = false;
    bool close_ok = false;
    std::string after;
    if (active) {
        auto const mark = c.rig.peer.received.size();
        auto s = c.session();
        st->hold_when = [](t105::store_call k, seqnum_t) {
            return k == t105::store_call::store_outbound;
        };
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s, c.rig.ioc),
                                  s->close(fixpp::session::close_mode::graceful), asio::use_future);
        held = c.rig.run_until([&] { return st->parked == 1U; });
        st->hold_when = nullptr;
        c.rig.peer.send(pr::message("FIX.4.4", "0", 2, "TW", "ISLD", c.rig.sending_time()));
        ended = c.rig.run_until([&] { return c.rig.state() == fsm_state::Disconnected; });
        // The pump has returned from the ending's frame before the release, so a consumer of a
        // latch the ending set has run by then.
        c.rig.settle(std::chrono::milliseconds{50});
        st->release_all();
        eof = c.eof_at_once("InsideCloseEndingDuringItsLogoutStore");
        bool const done =
            c.rig.run_until([&] { return fut.wait_for(0s) == std::future_status::ready; });
        close_ok = done && fut.get().has_value();
        after = c.wire_since(mark);
    }
    st->release_all();
    c.rig.stop();
    EXPECT_TRUE(active) << "setup";
    EXPECT_TRUE(held) << "setup: close()'s Logout was not held in its store call";
    EXPECT_TRUE(ended) << "setup: the BeginString guard did not end the session";
    EXPECT_EQ(pr::frames_of_type(after, "5").size(), 1U)
        << "close()'s Logout, whose store call the ending did not cut: " << after;
    EXPECT_TRUE(eof) << "the peer's read did not end in EOF";
    EXPECT_TRUE(close_ok) << "close(graceful) did not return success";
    EXPECT_EQ(c.app->logouts, 1) << "onLogout";
}

FIXPP_105_BOTH_ROLES(ConnectionClose105);

// ════════════════════════════════════════════════════════════════════════════════════════
// T020, the direct initiator (contract C-1 row 10; C-1.c): a Session with no Engine, its
// connection a mock transport made by MockReconnectFactory
// (tests/session/mock_reconnect_factory.hpp) at drive_reconnect(), as
// test_103_resend_state_reconnect.cpp drives it, on resend_gap::Scene's mock clock. The
// peer-visible close is the transport's close (Script::closed_flag). The wall-clock timers:
// drain_for_reconnect_'s bound runs only while a frame or an emitter is in flight at the drain,
// which these cells never leave.
// ════════════════════════════════════════════════════════════════════════════════════════

namespace rg = fixpp::test_support::resend_gap;

// resend_gap's application, whose toAdmin can throw on one MsgType once armed.
class ThrowingGapApp final : public rg::GapApp {
public:
    std::string throw_on;
    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
                 const fixpp::session::SessionId& id) override {
        rg::GapApp::toAdmin(msg, id);
        if (!throw_on.empty() && msg.msg_type() == throw_on) {
            throw std::runtime_error("ThrowingGapApp: toAdmin");
        }
    }
};

// A direct initiator whose connections are mock transports; each made transport's close is
// recorded in closed[i].
struct DirectInitiator {
    std::shared_ptr<ThrowingGapApp> app = std::make_shared<ThrowingGapApp>();
    std::shared_ptr<fixpp::test_support::MockReconnectFactory> factory =
        std::make_shared<fixpp::test_support::MockReconnectFactory>();
    std::shared_ptr<std::vector<std::shared_ptr<std::atomic<bool>>>> closed =
        std::make_shared<std::vector<std::shared_ptr<std::atomic<bool>>>>();
    rg::Scene s;

    explicit DirectInitiator(std::function<void(SessionConfig&)> const& tweak = {})
        : s{rg::Options{.app = app}} {
        factory->tweak_script = [c = closed](fixpp::transport::test::Script& sc) {
            c->push_back(std::make_shared<std::atomic<bool>>(false));
            sc.closed_flag = c->back();
        };
        s.cfg.transport_factory_override = factory;
        s.cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 19099};
        if (tweak) tweak(s.cfg);
        s.session = std::make_unique<fixpp::session::Session>(s.engine, s.cfg);
    }

    template <class Awaitable>
    std::optional<fixpp::core::expected_t<void>> run(Awaitable a, const char* site) {
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                  std::move(a), asio::use_future);
        if (!fixpp::test_support::pump_until(
                s.ioc, [&] { return rg::Scene::ready(fut); }, fixpp::test_support::kPumpBudget,
                fixpp::test_support::kPumpSlice, site)) {
            ADD_FAILURE() << site << ": the call did not return";
            return std::nullopt;
        }
        auto r = fut.get();
        s.drain();
        return r;
    }
    [[nodiscard]] bool transport_closed(std::size_t i) const {
        return i < closed->size() && (*closed)[i]->load();
    }
    [[nodiscard]] std::string transport_wire(std::size_t i) const {
        (void)i;
        if (factory->last_transport == nullptr) return {};
        auto const b = factory->last_transport->outbound_bytes_seen();
        return std::string{reinterpret_cast<const char*>(b.data()), b.size()};
    }
};

// The Logon of drive_reconnect() fails (toAdmin throws on it): the transport drive_reconnect()
// installed is closed once the call has returned, with no later call and the clock still.
TEST(ConnectionClose105Direct,
     DirectInitiatorLogonEmitFails_DriveReconnect_EofAfterTheCallReturns) {
    DirectInitiator d;
    auto const opened = d.run(d.s.session->open(), "open");
    ASSERT_TRUE(opened && opened->has_value()) << "setup: open()";
    d.app->throw_on = "A";
    auto const r = d.run(d.s.session->drive_reconnect(), "drive_reconnect");
    ASSERT_TRUE(r.has_value()) << "setup: drive_reconnect() did not return";
    EXPECT_FALSE(r->has_value()) << "setup: the Logon's toAdmin throw did not fail the call";
    EXPECT_EQ(d.factory->make_call_count.load(), 1) << "setup: no transport was made";
    EXPECT_EQ(d.s.state(), fsm_state::Disconnected) << "setup: the session did not end";
    EXPECT_TRUE(d.transport_closed(0))
        << "the installed transport is still open after the call returned";
}

// An ending on a direct initiator's connection, then a later application call (a send the ended
// session refuses), then drive_reconnect(): the ended connection is closed by the later call's
// exit, the Session stays open in Disconnected, and drive_reconnect() installs a new transport
// that logs on.
TEST(ConnectionClose105Direct, DirectInitiator_EndingThenDriveReconnect_Reconnects) {
    DirectInitiator d;
    auto const opened = d.run(d.s.session->open(), "open");
    ASSERT_TRUE(opened && opened->has_value()) << "setup: open()";
    auto const first = d.run(d.s.session->drive_reconnect(), "drive_reconnect 1");
    ASSERT_TRUE(first && first->has_value()) << "setup: the first drive_reconnect()";
    (void)d.s.feed(d.s.logon(1));
    ASSERT_EQ(d.s.state(), fsm_state::Active) << "setup: not Active on the first connection";
    // The CompID guard ends the session (a class P ending; a direct Session has no pump).
    (void)d.s.feed(rg::Scene::raw("0", 2, "XX", d.s.sending_time()));
    EXPECT_EQ(d.s.state(), fsm_state::Disconnected) << "setup: the frame did not end the session";
    static constexpr char kPayload[] = "35=D\x01";
    auto const sent =
        d.run(d.s.session->send(std::as_bytes(std::span{kPayload, sizeof(kPayload) - 1})), "send");
    EXPECT_TRUE(sent && !sent->has_value()) << "setup: the ended session admitted the send";
    EXPECT_TRUE(d.transport_closed(0)) << "the ended connection is still open after the later call";
    EXPECT_FALSE(session_test_access::is_closing(*d.s.session) ||
                 session_test_access::is_drained(*d.s.session))
        << "the Session was closed, not only its connection";
    auto const second = d.run(d.s.session->drive_reconnect(), "drive_reconnect 2");
    EXPECT_TRUE(second && second->has_value()) << "the reconnect after the ending was refused";
    EXPECT_EQ(d.factory->make_call_count.load(), 2) << "no second transport was made";
    EXPECT_FALSE(pr::frames_of_type(d.transport_wire(1), "A").empty())
        << "the new transport carries no Logon";
}

// [guard: the reply admitted in LogonReceived] T019, C-1 row 3's LogonReceived case (FR-005;
// the spec clarification of the analyze re-run), acceptor only. The acceptor's Logon
// arm is held at its reply's store call; a graceful close() starts there and is held at its
// flush step; the arm is released and its supersede return leaves the session LogonReceived;
// the peer's Logout is delivered and processed; then the flush is released. Oracle: no 35=5
// written by fixpp, the store reads (1, 1) once the reset unit has run, the connection closed
// by the running close(). The wall-clock timers: HookedStore's own hold bound (kHoldBound),
// which a held call meets only if the cell stalls.
TEST(ConnectionClose105Acceptor,
     PeerLogoutInLogonReceivedDuringClose_NoReplyLogout_ResetFollows104) {
    Cell c;
    auto factory = std::make_shared<fixpp::test_support::HookedStoreFactory>();
    bool hold_store = true;
    bool hold_flush = true;
    factory->hooks.on_outbound_store = [&hold_store] { return hold_store; };
    factory->hooks.release_when = [&hold_store] { return !hold_store; };
    factory->hooks.flush_until = [&hold_flush] { return !hold_flush; };
    auto cfg = c.cfg(session_role::acceptor);
    cfg.reset_on_logout = true;
    cfg.store_factory = factory;
    bool up = c.rig.start(std::move(cfg)) && c.rig.connect_peer();
    bool held = false;
    bool flushing = false;
    bool logon_received = false;
    bool eof = false;
    bool close_ok = false;
    std::optional<std::pair<seqnum_t, seqnum_t>> store_after;
    std::optional<std::future<fixpp::core::expected_t<void>>> close_fut;
    if (up) {
        c.rig.peer.send(c.rig.logon(1));
        held = c.rig.run_until([&] {
            auto s = c.session();
            return s && s->state() == fsm_state::LogonReceived;
        });
        auto s = c.session();
        if (held && s) {
            close_fut =
                asio::co_spawn(fixpp::test_support::session_executor_of(*s, c.rig.ioc),
                               s->close(fixpp::session::close_mode::graceful), asio::use_future);
            flushing = c.rig.run_until([&] { return factory->log->flushes_begun == 1; });
            hold_store = false;
            (void)c.rig.run_until([&] { return !factory->log->hold_timed_out; });
            c.rig.settle(std::chrono::milliseconds{50});
            logon_received = s->state() == fsm_state::LogonReceived;
            c.rig.peer.send(c.rig.msg("5", 2));
            c.rig.settle(std::chrono::milliseconds{100});
            hold_flush = false;
            eof = c.eof_at_once("PeerLogoutInLogonReceivedDuringClose");
            bool const done = c.rig.run_until(
                [&] { return close_fut->wait_for(0s) == std::future_status::ready; });
            close_ok = done && close_fut->get().has_value();
            if (factory->log->inner) {
                asio::io_context tmp;
                auto in = asio::co_spawn(
                    tmp,
                    factory->log->inner->next_seqnum(fixpp::session::direction_t::inbound, false),
                    asio::use_future);
                auto out = asio::co_spawn(
                    tmp,
                    factory->log->inner->next_seqnum(fixpp::session::direction_t::outbound, false),
                    asio::use_future);
                tmp.run();
                auto const i = in.get();
                auto const o = out.get();
                store_after = std::pair<seqnum_t, seqnum_t>{i ? *i : 0U, o ? *o : 0U};
            }
        }
    }
    std::string const wire = c.rig.peer.received;
    c.rig.stop();
    EXPECT_TRUE(up && held) << "setup: the Logon arm was not held in LogonReceived";
    EXPECT_TRUE(flushing) << "setup: close(graceful) did not reach its flush";
    EXPECT_TRUE(logon_received)
        << "setup: the released arm did not leave the session LogonReceived";
    EXPECT_TRUE(pr::frames_of_type(wire, "5").empty()) << "fixpp wrote a Logout: " << wire;
    EXPECT_EQ(store_after, (std::optional<std::pair<seqnum_t, seqnum_t>>{{1U, 1U}}))
        << "the reset 104's reset point runs";
    EXPECT_TRUE(eof) << "the running close() did not close the connection";
    EXPECT_TRUE(close_ok) << "close(graceful) did not return success";
    EXPECT_EQ(c.app->logouts, 0)
        << "onLogout fires on leaving Active, which this session never was";
}

// [guard: the drain flag deleted] T022, B-103-9's bound: a direct initiator's drive_reconnect()
// drain, with a send held in its store call across the drain's step 2 (its Disconnected write).
// The drain owns the old connection: the send's exit closes nothing, and the drain installs the
// new connection.
TEST(ConnectionClose105Direct, InsideReconnectDrain_DrainKeepsItsBound) {
    auto st = std::make_shared<t105::gate_store_state>();
    DirectInitiator d{[st](SessionConfig& cfg) {
        cfg.store_factory = std::make_shared<t105::GateStoreFactory>(st);
    }};
    auto const opened = d.run(d.s.session->open(), "open");
    ASSERT_TRUE(opened && opened->has_value()) << "setup: open()";
    auto const first = d.run(d.s.session->drive_reconnect(), "drive_reconnect 1");
    ASSERT_TRUE(first && first->has_value()) << "setup: the first drive_reconnect()";
    (void)d.s.feed(d.s.logon(1));
    ASSERT_EQ(d.s.state(), fsm_state::Active) << "setup";
    st->hold_when = [](t105::store_call k, seqnum_t) {
        return k == t105::store_call::store_outbound;
    };
    static constexpr char kPayload[] = "35=D\x01";
    auto send_fut =
        asio::co_spawn(fixpp::test_support::session_executor_of(*d.s.session, d.s.ioc),
                       d.s.session->send(std::as_bytes(std::span{kPayload, sizeof(kPayload) - 1})),
                       asio::use_future);
    ASSERT_TRUE(fixpp::test_support::pump_until(
        d.s.ioc, [&] { return st->parked == 1U; }, fixpp::test_support::kPumpBudget,
        fixpp::test_support::kPumpSlice, "send held"))
        << "setup: the send's store call was not held";
    st->hold_when = nullptr;
    auto rfut = asio::co_spawn(fixpp::test_support::session_executor_of(*d.s.session, d.s.ioc),
                               d.s.session->drive_reconnect(), asio::use_future);
    (void)fixpp::test_support::pump_until(
        d.s.ioc, [&] { return d.s.state() == fsm_state::Disconnected; },
        fixpp::test_support::kPumpBudget, fixpp::test_support::kPumpSlice, "drain step 2");
    // The reach proof: the drain is still waiting for the held send when it is released.
    EXPECT_FALSE(rg::Scene::ready(rfut)) << "setup: the drain did not wait for the held send";
    st->release_all();
    bool const reconnected = fixpp::test_support::pump_until(
        d.s.ioc, [&] { return rg::Scene::ready(rfut) && rg::Scene::ready(send_fut); },
        fixpp::test_support::kPumpBudget, fixpp::test_support::kPumpSlice, "drain");
    d.s.drain();
    EXPECT_TRUE(reconnected) << "the drain did not complete";
    if (reconnected) {
        (void)send_fut.get();
        EXPECT_TRUE(rfut.get().has_value()) << "the reconnect failed";
    }
    EXPECT_FALSE(d.transport_closed(0))
        << "the old connection was closed during the drain by another root";
    EXPECT_EQ(d.factory->make_call_count.load(), 2) << "no new connection was installed";
}

}  // namespace
