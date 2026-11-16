// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_103_resend_state_reconnect.cpp — test-only.
//
// Feature 103 (specs/103-resend-logon-reset-fidelity/), fixpp#578: the resend state ends with
// the connection. research.md "RED cells", "#578 — resend state across an in-place
// reconnect"; tasks T018 (arms A, B, E and the fresh-Session control), T039 (arm C), T110
// (arm D) and T111 to T120 (the drain's cells G1 to G9 and LO-6b).
//
// The vehicle is resend_gap_scene.hpp's Scene (a directly driven initiator on a mock clock,
// with a liveness loop) given a MockReconnectFactory, so `Session::drive_reconnect()` connects
// and logs on again with no socket: the make_reconnect_initiator shape, on a mock clock.
//
// Where the frames are. Before the reconnect the session writes through its transport
// callback (`Scene::sent`); after `install_reconnected_transport` its live writes go to the
// mock transport the factory made. So "after the reconnect mark" is `sent` from the mark plus
// every frame the new mock transport saw, and each arm first checks that the new Logon is
// among them (the positive control that the reading reaches the new connection's sink).
#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "session/test_103_support.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"

namespace {

namespace rg = fixpp::test_support::resend_gap;
namespace t103 = fixpp::test_support::t103;
using fixpp::session::fsm_state;
using fixpp::session::seqnum_t;

// An application that records, for each onLogout, the state the session is in when it runs.
// record_state_transition_ sets the new state before it calls onLogout, so the reading names
// the edge that fired it.
class ReconnectApp final : public rg::GapApp {
public:
    fixpp::session::Session* session = nullptr;
    std::vector<fsm_state> logout_states;
    // The callback depth each onLogout ran at.
    std::vector<std::uint32_t> logout_depths;
    // Run inside onLogout, after the readings.
    std::function<void()> on_logout;
    // Run inside fromApp and toApp, after the base's readings.
    std::function<void()> on_from_app;
    std::function<void()> on_to_app;

    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
        const fixpp::session::SessionId& id) override {
        auto r = rg::GapApp::fromApp(msg, id);
        if (on_from_app) on_from_app();
        return r;
    }
    fixpp::core::expected_t<void> toApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const fixpp::session::SessionId& /*id*/) override {
        if (on_to_app) on_to_app();
        return {};
    }

    void onLogout(const fixpp::session::SessionId& /*id*/) override {
        logout_states.push_back(session != nullptr ? session->state() : fsm_state::NotConnected);
        logout_depths.push_back(session != nullptr
                                    ? fixpp::session::session_test_access::callback_depth(*session)
                                    : 0U);
        if (on_logout) on_logout();
    }
};

// The frames of one byte stream the mock transport saw, split by each frame's BodyLength(9):
// `8=<v><SOH>9=<n><SOH>`, n body bytes, then `10=ddd<SOH>`. A stream that does not parse ends
// the split; the cell then misses the frame it looks for and fails by its own assertion.
std::vector<std::string> split_frames(std::vector<std::byte> const& bytes) {
    std::string const s(reinterpret_cast<char const*>(bytes.data()), bytes.size());
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at < s.size()) {
        auto const nine = s.find(
            "\x01"
            "9=",
            at);
        if (nine == std::string::npos) break;
        auto const len_end = s.find('\x01', nine + 3);
        if (len_end == std::string::npos) break;
        std::size_t body = 0;
        for (std::size_t i = nine + 3; i < len_end; ++i) {
            if (s[i] < '0' || s[i] > '9') return out;
            body = body * 10 + static_cast<std::size_t>(s[i] - '0');
        }
        std::size_t const trailer = len_end + 1 + body;
        constexpr std::size_t kTrailer = 7;  // "10=ddd<SOH>"
        if (trailer + kTrailer > s.size()) break;
        out.push_back(s.substr(at, trailer + kTrailer - at));
        at = trailer + kTrailer;
    }
    return out;
}

// HeartBtInt of every arm (Scene's), and the Logout timeout T of arm E.
constexpr std::uint32_t kLogoutTimeoutMs = 2000;

class Rig {
public:
    std::shared_ptr<ReconnectApp> app = std::make_shared<ReconnectApp>();
    std::shared_ptr<fixpp::test_support::MockReconnectFactory> factory =
        std::make_shared<fixpp::test_support::MockReconnectFactory>();
    rg::Scene s;
    std::size_t reconnect_mark = 0;

    // Scene's initiator (lenient, HeartBtInt 30 s, mock clock) with reset_on_logon and the
    // reconnect factory. The Scene's first session is replaced before it opens, so the cell's
    // session is built from the edited config.
    explicit Rig(fixpp::session::reset_seqnum_policy const policy =
                     fixpp::session::reset_seqnum_policy::bilateral_lenient,
                 bool const reset_on_logon = true, bool const recording_store = false)
        : s{rg::Options{.app = app, .recording_store = recording_store}} {
        s.cfg.reset_seqnum_policy_field = policy;
        s.cfg.reset_on_logon = reset_on_logon;
        s.cfg.transport_factory_override = factory;
        s.cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 19099};
        s.cfg.logout_disconnect_timeout_ms = kLogoutTimeoutMs;
        s.session = std::make_unique<fixpp::session::Session>(s.engine, s.cfg);
        app->session = s.session.get();
    }

    // The peer's Logon acknowledgement, with ResetSeqNumFlag(141)=Y when `reset`.
    [[nodiscard]] std::vector<std::byte> ack(seqnum_t const seq, bool const reset) const {
        std::string extra =
            "98=0\x01"
            "108=" +
            std::to_string(rg::kHeartBtInt.count()) + "\x01";
        if (reset) extra += "141=Y\x01";
        return s.frame("A", seq, extra);
    }

    // open(), then the peer's 141=Y acknowledgement at 34=1: Active, NextNumIn 2.
    [[nodiscard]] bool open_to_active() {
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                  s.session->open(), asio::use_future);
        if (!t103::pump_or_fail(
                s.ioc, [&] { return rg::Scene::ready(fut); }, fixpp::test_support::kPumpBudget,
                "t103_578/open") ||
            !fut.get().has_value()) {
            return false;
        }
        s.drain();
        (void)s.feed(ack(1, true));
        return s.state() == fsm_state::Active;
    }

    // Two in-sequence orders (NextNumIn 4), then an order at N+10: one ResendRequest from N, a
    // resend outstanding. NextNumIn is moved off 2 first so the old range's BeginSeqNo differs
    // from the one the new connection asks for.
    [[nodiscard]] bool open_gap() {
        (void)s.feed(s.order(s.next_in()));
        (void)s.feed(s.order(s.next_in()));
        seqnum_t const n = s.next_in();
        std::size_t const before = s.sent.size();
        (void)s.feed(s.order(n + 10));
        auto const rr = s.of_type("2", before);
        return rr.size() == 1U && rg::field(rr.front(), 7) == std::to_string(n) &&
               s.resend_outstanding() && s.state() == fsm_state::Active;
    }

    // A too-low NewOrderSingle without PossDupFlag(43): Arm B ends the session.
    void too_low_order() { (void)s.feed(s.order(1)); }

    // drive_reconnect(): the factory's mock transport is installed and the new Logon written
    // on it. Takes the reconnect mark first.
    [[nodiscard]] fixpp::core::expected_t<void> reconnect() {
        reconnect_mark = s.sent.size();
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                  s.session->drive_reconnect(), asio::use_future);
        if (!t103::pump_or_fail(
                s.ioc, [&] { return rg::Scene::ready(fut); }, fixpp::test_support::kPumpBudget,
                "t103_578/drive_reconnect")) {
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        auto r = fut.get();
        s.drain();
        return r;
    }

    // The frames the new connection's mock transport saw; none before an install.
    [[nodiscard]] std::vector<std::string> new_transport_frames() const {
        return factory->last_transport != nullptr
                   ? split_frames(factory->last_transport->outbound_bytes_seen())
                   : std::vector<std::string>{};
    }
    // MsgType/MsgSeqNum of each frame, for a failure message.
    [[nodiscard]] static std::string frames_text(std::vector<std::string> const& frames) {
        std::string out;
        for (auto const& f : frames) {
            auto const d = t103::decode(f);
            out += (out.empty() ? "" : " | ") + ("35=" + d.msg_type.value_or("?")) +
                   "/34=" + d.seq.value_or("?");
        }
        return out.empty() ? std::string{"nothing"} : out;
    }
    // drive_reconnect() spawned, not pumped: for a cell that holds a store call meanwhile.
    [[nodiscard]] std::future<fixpp::core::expected_t<void>> spawn_reconnect() {
        reconnect_mark = s.sent.size();
        return asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                              s.session->drive_reconnect(), asio::use_future);
    }

    // Every frame written after the reconnect mark: the callback's, then the new transport's.
    [[nodiscard]] std::vector<std::string> after_mark() const {
        std::vector<std::string> out(s.sent.begin() + static_cast<std::ptrdiff_t>(reconnect_mark),
                                     s.sent.end());
        if (factory->last_transport != nullptr) {
            for (auto& f : split_frames(factory->last_transport->outbound_bytes_seen())) {
                out.push_back(std::move(f));
            }
        }
        return out;
    }

    // "35=<t>/34=<n>/7=<b>/58=<text>" of each frame after the mark, for a failure message.
    [[nodiscard]] std::string after_mark_text() const {
        std::string out;
        for (auto const& f : after_mark()) {
            auto const d = t103::decode(f);
            out += (out.empty() ? "" : " | ") + ("35=" + d.msg_type.value_or("?")) +
                   "/34=" + d.seq.value_or("?");
            if (d.begin_seq_no) out += "/7=" + *d.begin_seq_no;
            if (d.text) out += "/58=" + *d.text;
        }
        return out.empty() ? std::string{"nothing"} : out;
    }

    // The frames of MsgType `type` written after the mark.
    [[nodiscard]] std::vector<t103::frame_fields> after_mark_of(std::string_view const type) const {
        std::vector<t103::frame_fields> out;
        for (auto const& f : after_mark()) {
            auto d = t103::decode(f);
            if (d.msg_type == type) out.push_back(std::move(d));
        }
        return out;
    }
};

// The reconnect's own Logon, on the new transport: one 35=A after the mark, 141=Y and 34=1
// (reset_on_logon). It is what proves the reading reaches the new connection's sink.
void expect_one_reset_logon_after_mark(Rig const& r) {
    auto const logons = r.after_mark_of("A");
    ASSERT_EQ(logons.size(), 1U) << "the reconnect wrote " << r.after_mark_text();
    EXPECT_EQ(logons.front().reset_seq_num, std::optional<std::string>{"Y"});
    EXPECT_EQ(logons.front().seq, std::optional<std::string>{"1"});
}

// Arms A and B, to the frame at 34=4 on the new connection. `downtime` is how far the mock
// clock moves while the session is Disconnected.
void run_arm_a_or_b(Rig& r, std::chrono::seconds const downtime) {
    ASSERT_TRUE(r.open_to_active()) << "setup: the session did not reach Active";
    ASSERT_TRUE(r.open_gap()) << "setup: no gap opened; the session wrote " << r.s.written();
    r.too_low_order();
    ASSERT_EQ(r.s.state(), fsm_state::Disconnected)
        << "setup: Arm B did not end the session; it is " << rg::state_name(r.s.state());
    if (downtime.count() != 0) r.s.pass(downtime);

    auto const rc = r.reconnect();
    ASSERT_TRUE(rc.has_value()) << "drive_reconnect returned " << static_cast<int>(rc.error());
    ASSERT_EQ(r.s.state(), fsm_state::LogonSent) << rg::state_name(r.s.state());
    expect_one_reset_logon_after_mark(r);

    (void)r.s.feed(r.ack(1, true));
    ASSERT_EQ(r.s.state(), fsm_state::Active)
        << "the reconnect's acknowledgement did not establish; after the mark: "
        << r.after_mark_text();
    EXPECT_FALSE(r.s.resend_outstanding())
        << "the old connection's resend is still outstanding on the new connection";

    ASSERT_EQ(r.s.next_in(), 2U);
    (void)r.s.feed(r.s.order(4));

    auto const rr = r.after_mark_of("2");
    EXPECT_EQ(rr.size(), 1U) << "after the mark: " << r.after_mark_text();
    if (rr.size() == 1U) {
        EXPECT_EQ(rr.front().begin_seq_no, std::optional<std::string>{"2"})
            << "the ResendRequest is not for the new gap; after the mark: " << r.after_mark_text();
    }
    EXPECT_EQ(r.after_mark_of("5").size(), 0U)
        << "a Logout after the reconnect; after the mark: " << r.after_mark_text();
    EXPECT_EQ(r.s.state(), fsm_state::Active) << rg::state_name(r.s.state());
}

// ── Arm A: the reconnect within 2 x HeartBtInt ──────────────────────────────────────────────
// The downtime is one HeartBtInt and a second, under the bound: the old connection's
// liveness loop wakes in Disconnected and ends before the install, so the new connection's
// Active spawns the only loop bound to the session's root cancellation slot. A reconnect
// that leaves the old loop parked is the drain's subject (research D1 step 3, cell G8).
TEST(ResendStateReconnect103, ArmA_ReconnectWithinTheBound_TooHighFrameDrawsAResendRequest) {
    Rig r;
    run_arm_a_or_b(r, rg::kHeartBtInt + std::chrono::seconds{1});
}

// ── Arm B: the clock passes 2 x HeartBtInt during the downtime ──────────────────────────────
TEST(ResendStateReconnect103, ArmB_ClockPastTheBoundDuringDowntime_NoStaleDeadline) {
    Rig r;
    run_arm_a_or_b(r, rg::kGapBound + std::chrono::seconds{1});
}

// ── Arm C: strict with no reset setting reaches Arm A's state (tasks T039) ──────────────────
// bilateral_strict resets before every Logon (103 D2.2), so the reconnect's Logon is a reset
// with no reset_on_logon set at all; Arm A's assertions and downtime.
TEST(ResendStateReconnect103, ArmC_StrictNoKnob_ReconnectResets_TooHighFrameDrawsAResendRequest) {
    Rig r{fixpp::session::reset_seqnum_policy::bilateral_strict, /*reset_on_logon=*/false};
    run_arm_a_or_b(r, rg::kHeartBtInt + std::chrono::seconds{1});
}

// ── Control: a fresh Session given steps 3 and 4 only writes the same ResendRequest ─────────
TEST(ResendStateReconnect103, Control_FreshSession_TooHighFrameDrawsTheSameResendRequest) {
    Rig r;
    ASSERT_TRUE(r.open_to_active()) << "setup: the session did not reach Active";
    ASSERT_EQ(r.s.next_in(), 2U);
    std::size_t const before = r.s.sent.size();
    (void)r.s.feed(r.s.order(4));
    auto const rr = r.s.of_type("2", before);
    ASSERT_EQ(rr.size(), 1U) << r.s.written(before);
    EXPECT_EQ(rg::field(rr.front(), 7), "2");
    EXPECT_EQ(r.s.count("5", before), 0U) << r.s.written(before);
}

// ── G8 (zero downtime): the old connection's liveness loop does not outlive the install ─────
// research D1 step 3 and cell G8 (tasks T118). As Arm A with no clock motion during the
// downtime, so the old connection's run_liveness_loop is still parked in its mock-clock sleep
// when drive_reconnect installs the new transport, and the new Active spawns a loop of its
// own. Only one loop may be bound to the session's root cancellation slot (D1, "The root
// slot's single-client invariant"); the drain retires the old one first (tasks T129, T131).
// After the new Active the clock moves one HeartBtInt: one liveness loop writes one
// Heartbeat.
TEST(ResendStateReconnect103, G8_ZeroDowntimeReconnect_OldLivenessLoopRetiredBeforeTheInstall) {
    Rig r;
    ASSERT_TRUE(r.open_to_active()) << "setup: the session did not reach Active";
    ASSERT_TRUE(r.open_gap()) << "setup: no gap opened; the session wrote " << r.s.written();
    r.too_low_order();
    ASSERT_EQ(r.s.state(), fsm_state::Disconnected) << rg::state_name(r.s.state());

    auto const rc = r.reconnect();
    ASSERT_TRUE(rc.has_value()) << "drive_reconnect returned " << static_cast<int>(rc.error());
    expect_one_reset_logon_after_mark(r);
    (void)r.s.feed(r.ack(1, true));
    ASSERT_EQ(r.s.state(), fsm_state::Active) << "after the mark: " << r.after_mark_text();

    r.s.pass(rg::kHeartBtInt + std::chrono::seconds{1});
    EXPECT_EQ(r.after_mark_of("0").size(), 1U)
        << "one liveness loop writes one Heartbeat per HeartBtInt; after the mark: "
        << r.after_mark_text();
}

// ── Arm E: drive_reconnect from Active with a too-high Logout received ──────────────────────
// research D1, the drain and the commit point (tasks T129, T131), and D4's mark.
TEST(ResendStateReconnect103, ArmE_ReconnectFromActive_OldLogoutNeverAcknowledgedOnTheNewOne) {
    Rig r;
    ASSERT_TRUE(r.open_to_active()) << "setup: the session did not reach Active";
    ASSERT_TRUE(r.open_gap()) << "setup: no gap opened; the session wrote " << r.s.written();
    (void)r.s.feed(r.s.logout(r.s.next_in() + 20));
    auto const logout_at = r.s.mock->steady_now();
    // The mock transport is "dropped": the test records nothing, and the session is not told.

    auto const rc = r.reconnect();
    ASSERT_TRUE(rc.has_value()) << "drive_reconnect returned " << static_cast<int>(rc.error());
    expect_one_reset_logon_after_mark(r);
    (void)r.s.feed(r.ack(1, true));
    ASSERT_EQ(r.s.state(), fsm_state::Active) << "after the mark: " << r.after_mark_text();

    r.s.pass(std::chrono::milliseconds{kLogoutTimeoutMs} + std::chrono::seconds{1});
    ASSERT_GT(r.s.mock->steady_now(), logout_at + std::chrono::milliseconds{kLogoutTimeoutMs});

    ASSERT_EQ(r.app->logout_states.size(), 1U) << "onLogout must fire once";
    EXPECT_EQ(r.app->logout_states.front(), fsm_state::Disconnected)
        << "onLogout fired on the edge into " << rg::state_name(r.app->logout_states.front())
        << ", not the drain's edge into Disconnected";
    EXPECT_EQ(r.after_mark_of("5").size(), 0U)
        << "the old Logout was acknowledged on the new connection: " << r.after_mark_text();
    EXPECT_EQ(fixpp::session::session_test_access::peer_logout_bound_count(*r.s.session), 0)
        << "the old connection's bound outlived the reconnect";

    std::string const payload =
        "35=D\x01"
        "11=ORD-E\x01" +
        std::string{rg::kSideAndTime};
    auto fut =
        asio::co_spawn(fixpp::test_support::session_executor_of(*r.s.session, r.s.ioc),
                       r.s.session->send(std::span<const std::byte>{
                           reinterpret_cast<std::byte const*>(payload.data()), payload.size()}),
                       asio::use_future);
    ASSERT_TRUE(t103::pump_or_fail(
        r.s.ioc, [&] { return rg::Scene::ready(fut); }, fixpp::test_support::kPumpBudget,
        "t103_578/armE_send"));
    auto const sr = fut.get();
    EXPECT_TRUE(sr.has_value()) << "send returned " << static_cast<int>(sr.error());
    bool written = false;
    for (auto const& f : r.after_mark_of("D")) {
        written = written || f.seq.has_value();
    }
    EXPECT_TRUE(written) << "the send's frame is not written: " << r.after_mark_text();
}

// ── Phase 9 (tasks T110 to T120): drain, then reconnect ─────────────────────────────────────
// research D1 steps 0b to 5 (the owner's "drain, then reconnect" ruling). The cells
// below hold one store call of the old connection with the recording store
// (resend_gap_scene.hpp's Options::recording_store) while drive_reconnect runs, so they
// spawn it and pump with pump_until / pump_for while a hold is in flight. Frames on the old
// connection are the Scene's `sent` from the reconnect mark; frames on the new one are the
// new mock transport's.

namespace {

using fixpp::session::session_test_access;
using fixpp::test_support::store_op;
using fixpp::test_support::StoreCall;

// The next outbound number: the one the session's next frame takes.
seqnum_t next_out(Rig const& r) {
    return session_test_access::seqnum_mgr(*r.s.session).peek_outbound();
}
// Holds the store() of outbound number `seq`.
void hold_store_of(Rig& r, seqnum_t const seq) {
    r.s.log->hold_in = [seq](StoreCall const& c) {
        return c.op == store_op::store && c.dir == fixpp::session::direction_t::outbound &&
               c.first == seq;
    };
}
// A Session::send of `payload`, spawned; the payload lives in the send's own frame.
std::future<fixpp::core::expected_t<void>> spawn_send(Rig& r, std::string payload) {
    auto const p = std::make_shared<std::string>(std::move(payload));
    auto* const session = r.s.session.get();
    return asio::co_spawn(
        fixpp::test_support::session_executor_of(*session, r.s.ioc),
        [session, p]() -> asio::awaitable<fixpp::core::expected_t<void>> {
            co_return co_await session->send(std::span<const std::byte>{
                reinterpret_cast<std::byte const*>(p->data()), p->size()});
        },
        asio::use_future);
}
std::string order_payload(std::string const& id) {
    return "35=D\x01" + ("11=" + id + "\x01") + std::string{rg::kSideAndTime};
}
// The frames of MsgType `type` among `frames`, by their MsgSeqNum(34).
std::vector<std::string> seqs_of(std::vector<std::string> const& frames, std::string_view type) {
    std::vector<std::string> out;
    for (auto const& f : frames) {
        auto const d = t103::decode(f);
        if (d.msg_type == type) out.push_back(d.seq.value_or("?"));
    }
    return out;
}
std::vector<std::string> old_frames(Rig const& r) {
    return {r.s.sent.begin() + static_cast<std::ptrdiff_t>(r.reconnect_mark), r.s.sent.end()};
}
// The reconnect budget while a hold is in flight: below the drain's bound T, so the bound's
// force-close cannot be what ended the wait.
constexpr auto kHeldWindow = std::chrono::milliseconds{kLogoutTimeoutMs} / 8;
constexpr auto kAfterReleaseBudget = std::chrono::milliseconds{kLogoutTimeoutMs} / 4;

}  // namespace

// G1 (tasks T111): a send held in its store call, then drive_reconnect from Active.
TEST(Drain103, G1_SendHeldInItsStore_WrittenOnTheOldTransportNeverTheNew) {
    Rig r{fixpp::session::reset_seqnum_policy::bilateral_lenient, /*reset_on_logon=*/false,
          /*recording_store=*/true};
    ASSERT_TRUE(r.open_to_active()) << "setup";
    r.s.log->arm();
    seqnum_t const s_num = next_out(r);
    hold_store_of(r, s_num);
    auto send_fut = spawn_send(r, order_payload("ORD-G1"));
    ASSERT_TRUE(r.s.pump_until([&] { return r.s.log->holding(); }))
        << "setup: the send's store was not held";
    auto rc_fut = r.spawn_reconnect();
    r.s.pump_for(kHeldWindow);
    EXPECT_EQ(r.s.state(), fsm_state::Disconnected) << "the drain did not record Disconnected";
    EXPECT_EQ(r.factory->make_call_count.load(), 0) << "a connect was attempted during the drain";
    EXPECT_TRUE(r.new_transport_frames().empty())
        << "written on a new transport: " << Rig::frames_text(r.new_transport_frames());
    r.s.log->release_hold();
    EXPECT_TRUE(
        r.s.pump_until([&] { return rg::Scene::ready(rc_fut) && rg::Scene::ready(send_fut); },
                       kAfterReleaseBudget))
        << "the reconnect or the send did not complete";
    auto const old_d = seqs_of(old_frames(r), "D");
    EXPECT_EQ(old_d, (std::vector<std::string>{std::to_string(s_num)}))
        << "on the old transport: " << Rig::frames_text(old_frames(r));
    auto const fresh = r.new_transport_frames();
    EXPECT_TRUE(seqs_of(fresh, "D").empty()) << "on the new transport: " << Rig::frames_text(fresh);
    EXPECT_EQ(seqs_of(fresh, "A"), (std::vector<std::string>{std::to_string(s_num + 1)}))
        << "the new Logon does not carry S+1: " << Rig::frames_text(fresh);
}

// G2 (tasks T112): the bound has claimed and its acknowledgement's store is held, then
// drive_reconnect from Active.
TEST(Drain103, G2_ClaimedAcknowledgementHeld_WrittenOnTheOldTransportNeverTheNew) {
    Rig r{fixpp::session::reset_seqnum_policy::bilateral_lenient, /*reset_on_logon=*/false,
          /*recording_store=*/true};
    ASSERT_TRUE(r.open_to_active()) << "setup";
    seqnum_t const n = r.s.next_in();
    (void)r.s.feed(r.s.logout(n + 3));
    ASSERT_EQ(session_test_access::peer_logout_phase(*r.s.session), 1) << "setup: no pending mark";
    r.s.log->arm();
    hold_store_of(r, next_out(r));
    auto gf = r.s.feed_spawn(r.s.gap_fill(n, n + 4));
    ASSERT_TRUE(r.s.pump_until([&] { return r.s.log->holding(); }))
        << "setup: the acknowledgement's store was not held";
    auto rc_fut = r.spawn_reconnect();
    r.s.pump_for(kHeldWindow);
    EXPECT_EQ(r.factory->make_call_count.load(), 0) << "a connect was attempted during the drain";
    r.s.log->release_hold();
    EXPECT_TRUE(r.s.pump_until([&] { return rg::Scene::ready(rc_fut) && rg::Scene::ready(gf); },
                               kAfterReleaseBudget))
        << "the reconnect or the delivery did not complete";
    EXPECT_EQ(seqs_of(old_frames(r), "5").size(), 1U)
        << "the acknowledgement on the old transport: " << Rig::frames_text(old_frames(r));
    (void)r.s.feed(r.ack(r.s.next_in(), false));
    EXPECT_EQ(r.s.state(), fsm_state::Active) << "the new connection did not reach Active: "
                                              << Rig::frames_text(r.new_transport_frames());
    EXPECT_TRUE(seqs_of(r.new_transport_frames(), "5").empty())
        << "a Logout on the new connection: " << Rig::frames_text(r.new_transport_frames());
}

// G3 (tasks T113): a TestRequest's Heartbeat reply held in its store, then drive_reconnect
// from Active.
TEST(Drain103, G3_InboundReplyHeld_NotWrittenOnTheNewTransport) {
    Rig r{fixpp::session::reset_seqnum_policy::bilateral_lenient, /*reset_on_logon=*/false,
          /*recording_store=*/true};
    ASSERT_TRUE(r.open_to_active()) << "setup";
    r.s.log->arm();
    hold_store_of(r, next_out(r));
    auto tr = r.s.feed_spawn(r.s.test_request(r.s.next_in()));
    ASSERT_TRUE(r.s.pump_until([&] { return r.s.log->holding(); }))
        << "setup: the Heartbeat reply's store was not held";
    auto rc_fut = r.spawn_reconnect();
    r.s.pump_for(kHeldWindow);
    r.s.log->release_hold();
    EXPECT_TRUE(r.s.pump_until([&] { return rg::Scene::ready(rc_fut) && rg::Scene::ready(tr); },
                               kAfterReleaseBudget))
        << "the reconnect or the delivery did not complete";
    EXPECT_TRUE(seqs_of(r.new_transport_frames(), "0").empty())
        << "the reply on the new transport: " << Rig::frames_text(r.new_transport_frames());
    EXPECT_FALSE(seqs_of(r.new_transport_frames(), "A").empty())
        << "the reconnect wrote no Logon: " << Rig::frames_text(r.new_transport_frames());
}

// G4 (tasks T114), with G1's hold: the drain against close() and cancellation.
// (1) close(terminal) while the drain waits.
TEST(Drain103, G4_1_CloseTerminalWhileTheDrainWaits_AlreadyClosedNothingInstalled) {
    Rig r{fixpp::session::reset_seqnum_policy::bilateral_lenient, false, true};
    ASSERT_TRUE(r.open_to_active()) << "setup";
    r.s.log->arm();
    hold_store_of(r, next_out(r));
    auto send_fut = spawn_send(r, order_payload("ORD-G4-1"));
    ASSERT_TRUE(r.s.pump_until([&] { return r.s.log->holding(); })) << "setup";
    auto rc_fut = r.spawn_reconnect();
    r.s.pump_for(kHeldWindow);
    auto close_fut =
        asio::co_spawn(fixpp::test_support::session_executor_of(*r.s.session, r.s.ioc),
                       r.s.session->close(fixpp::session::close_mode::terminal), asio::use_future);
    EXPECT_TRUE(r.s.pump_until([&] { return rg::Scene::ready(rc_fut); }, kHeldWindow))
        << "drive_reconnect did not return while the store was held";
    r.s.log->release_hold();
    (void)r.s.pump_until([&] { return rg::Scene::ready(close_fut) && rg::Scene::ready(send_fut); },
                         kAfterReleaseBudget);
    ASSERT_TRUE(rg::Scene::ready(rc_fut));
    auto const rc = rc_fut.get();
    ASSERT_FALSE(rc.has_value()) << "the reconnect succeeded after close()";
    EXPECT_EQ(rc.error(), fixpp::core::error::session_already_closed)
        << static_cast<int>(rc.error());
    EXPECT_EQ(r.factory->make_call_count.load(), 0) << "a transport was made";
}

// (2) the caller's own slot emits terminal while the drain waits.
TEST(Drain103, G4_2_CallersCancellation_DispatchAbortedPendingThenASecondReconnectWorks) {
    Rig r{fixpp::session::reset_seqnum_policy::bilateral_lenient, false, true};
    ASSERT_TRUE(r.open_to_active()) << "setup";
    r.s.log->arm();
    hold_store_of(r, next_out(r));
    auto send_fut = spawn_send(r, order_payload("ORD-G4-2"));
    ASSERT_TRUE(r.s.pump_until([&] { return r.s.log->holding(); })) << "setup";
    struct Outcome {
        std::optional<fixpp::core::expected_t<void>> rc;
        bool still_pending = false;
    };
    auto const out = std::make_shared<Outcome>();
    asio::cancellation_signal sig;
    auto* session = r.s.session.get();
    r.reconnect_mark = r.s.sent.size();
    asio::co_spawn(
        fixpp::test_support::session_executor_of(*session, r.s.ioc),
        [session, out]() -> asio::awaitable<void> {
            out->rc = co_await session->drive_reconnect();
            out->still_pending = (co_await asio::this_coro::cancellation_state).cancelled() !=
                                 asio::cancellation_type::none;
        },
        asio::bind_cancellation_slot(sig.slot(), asio::detached));
    r.s.pump_for(kHeldWindow);
    sig.emit(asio::cancellation_type::terminal);
    EXPECT_TRUE(r.s.pump_until([&] { return out->rc.has_value(); }, kHeldWindow))
        << "drive_reconnect did not return on the caller's cancellation";
    r.s.log->release_hold();
    (void)r.s.pump_until([&] { return rg::Scene::ready(send_fut); }, kAfterReleaseBudget);
    ASSERT_TRUE(out->rc.has_value());
    ASSERT_FALSE(out->rc->has_value()) << "the reconnect succeeded";
    EXPECT_EQ(out->rc->error(), fixpp::core::error::dispatch_aborted)
        << static_cast<int>(out->rc->error());
    EXPECT_TRUE(out->still_pending) << "the caller's cancellation was reset";
    EXPECT_EQ(r.factory->make_call_count.load(), 0) << "a transport was made";
    EXPECT_EQ(r.s.state(), fsm_state::Disconnected);
    auto const again = r.reconnect();
    EXPECT_TRUE(again.has_value())
        << "the second drive_reconnect returned " << static_cast<int>(again.error());
    EXPECT_FALSE(seqs_of(r.new_transport_frames(), "A").empty())
        << "the second reconnect wrote no Logon";
}

// (3) the old transport's write is blocked past T: the drain's bound force-closes it at T,
// the write fails there, and the drain completes. The old transport is the first reconnect's
// mock transport, the only one given the write hold.
TEST(Drain103, G4_3_WriteBlockedPastT_OldTransportForceClosedDrainCompletes) {
    Rig r{fixpp::session::reset_seqnum_policy::bilateral_lenient, false, false};
    auto const hold = std::make_shared<std::atomic<bool>>(false);
    auto const made = std::make_shared<int>(0);
    r.factory->tweak_script = [hold, made](fixpp::transport::test::Script& sc) {
        if ((*made)++ == 0) sc.write_hold = hold;
    };
    ASSERT_TRUE(r.open_to_active()) << "setup";
    ASSERT_TRUE(r.reconnect().has_value()) << "setup: the first reconnect";
    (void)r.s.feed(r.ack(r.s.next_in(), false));
    ASSERT_EQ(r.s.state(), fsm_state::Active) << "setup: the first reconnect did not establish";
    hold->store(true);
    auto send_fut = spawn_send(r, order_payload("ORD-G4-3"));
    r.s.pump_for(kHeldWindow);
    ASSERT_FALSE(rg::Scene::ready(send_fut)) << "setup: the send's write was not blocked";
    auto const t = std::chrono::milliseconds{kLogoutTimeoutMs};
    auto const began = std::chrono::steady_clock::now();
    auto rc_fut = r.spawn_reconnect();
    bool const done = r.s.pump_until([&] { return rg::Scene::ready(rc_fut); }, 2 * t);
    auto const took = std::chrono::steady_clock::now() - began;
    hold->store(false);
    (void)r.s.pump_until([&] { return rg::Scene::ready(send_fut); });
    ASSERT_TRUE(done) << "the drain did not complete within 2T";
    EXPECT_GE(took, t - std::chrono::milliseconds{50})
        << "the reconnect completed before the bound T; the write was blocked";
    auto const rc = rc_fut.get();
    EXPECT_TRUE(rc.has_value()) << static_cast<int>(rc.error());
    auto const sr = send_fut.get();
    EXPECT_FALSE(sr.has_value()) << "the blocked write succeeded";
    EXPECT_TRUE(seqs_of(r.new_transport_frames(), "D").empty())
        << "the send on the new transport: " << Rig::frames_text(r.new_transport_frames());
}

// (4) onLogout, fired by the drain's own edge, spawns close(terminal) (which takes its R-4
// post) while the store is still held.
TEST(Drain103, G4_4_CloseFromTheDrainsOnLogout_AlreadyClosedWhileTheStoreIsHeld) {
    Rig r{fixpp::session::reset_seqnum_policy::bilateral_lenient, false, true};
    ASSERT_TRUE(r.open_to_active()) << "setup";
    std::future<fixpp::core::expected_t<void>> close_fut;
    r.app->on_logout = [&] {
        close_fut = asio::co_spawn(fixpp::test_support::session_executor_of(*r.s.session, r.s.ioc),
                                   r.s.session->close(fixpp::session::close_mode::terminal),
                                   asio::use_future);
    };
    r.s.log->arm();
    hold_store_of(r, next_out(r));
    auto send_fut = spawn_send(r, order_payload("ORD-G4-4"));
    ASSERT_TRUE(r.s.pump_until([&] { return r.s.log->holding(); })) << "setup";
    auto rc_fut = r.spawn_reconnect();
    bool const returned = r.s.pump_until([&] { return rg::Scene::ready(rc_fut); }, kHeldWindow);
    bool const held = r.s.log->holding();
    r.s.log->release_hold();
    (void)r.s.pump_until(
        [&] {
            return rg::Scene::ready(send_fut) &&
                   (!close_fut.valid() || rg::Scene::ready(close_fut));
        },
        kAfterReleaseBudget);
    EXPECT_TRUE(returned && held) << "drive_reconnect did not return while the store was held";
    ASSERT_TRUE(close_fut.valid()) << "onLogout did not run";
    ASSERT_TRUE(rg::Scene::ready(rc_fut));
    auto const rc = rc_fut.get();
    ASSERT_FALSE(rc.has_value()) << "the reconnect succeeded after close()";
    EXPECT_EQ(rc.error(), fixpp::core::error::session_already_closed)
        << static_cast<int>(rc.error());
    EXPECT_EQ(r.factory->make_call_count.load(), 0) << "a transport was made";
}

namespace {
// A connect hold for the mock factory's transports: released by the cell.
struct ConnectHold {
    bool entered = false;
    bool released = false;
    std::function<asio::awaitable<void>()> fn() {
        return [this]() -> asio::awaitable<void> {
            entered = true;
            auto const ex = co_await asio::this_coro::executor;
            while (!released) co_await asio::post(ex, asio::use_awaitable);
        };
    }
};
}  // namespace

// G5 (tasks T115): single flight. A second drive_reconnect while the first's connect is held.
TEST(Drain103, G5_SecondReconnectWhileTheConnectIsHeld_AlreadyOpenOneConnect) {
    ConnectHold hold;
    Rig r{fixpp::session::reset_seqnum_policy::bilateral_lenient, true, false};
    struct release_on_exit {
        ConnectHold& h;
        ~release_on_exit() { h.released = true; }
    } const release{hold};
    r.factory->tweak_script = [&hold](fixpp::transport::test::Script& sc) {
        sc.connect_hold = hold.fn();
    };
    ASSERT_TRUE(r.open_to_active()) << "setup";
    r.too_low_order();
    ASSERT_EQ(r.s.state(), fsm_state::Disconnected) << "setup";
    auto first = r.spawn_reconnect();
    ASSERT_TRUE(r.s.pump_until([&] { return hold.entered; })) << "setup: the connect was not held";
    auto second = asio::co_spawn(fixpp::test_support::session_executor_of(*r.s.session, r.s.ioc),
                                 r.s.session->drive_reconnect(), asio::use_future);
    EXPECT_TRUE(r.s.pump_until([&] { return rg::Scene::ready(second); }, kHeldWindow))
        << "the second drive_reconnect did not return at once";
    hold.released = true;
    EXPECT_TRUE(r.s.pump_until([&] { return rg::Scene::ready(first); }))
        << "the first did not complete";
    ASSERT_TRUE(rg::Scene::ready(second));
    auto const sr = second.get();
    ASSERT_FALSE(sr.has_value()) << "the second drive_reconnect succeeded";
    EXPECT_EQ(sr.error(), fixpp::core::error::session_already_open) << static_cast<int>(sr.error());
    EXPECT_EQ(r.factory->make_call_count.load(), 1) << "transports made";
    EXPECT_TRUE(first.get().has_value());
    EXPECT_EQ(seqs_of(r.new_transport_frames(), "A").size(), 1U)
        << Rig::frames_text(r.new_transport_frames());
}

// G6 (tasks T116): the commit point. After a quiescent drain the connect is held; the
// session is closed (twin: Engine::stop's note); release.
class Drain103CommitPoint : public ::testing::TestWithParam<bool> {};

TEST_P(Drain103CommitPoint, G6_CloseDuringTheConnect_NothingInstalledNewTransportClosed) {
    bool const engine_stop = GetParam();
    ConnectHold hold;
    Rig r{fixpp::session::reset_seqnum_policy::bilateral_lenient, true, false};
    struct release_on_exit {
        ConnectHold& h;
        ~release_on_exit() { h.released = true; }
    } const release{hold};
    auto const closed = std::make_shared<std::atomic<bool>>(false);
    r.factory->tweak_script = [&hold, closed](fixpp::transport::test::Script& sc) {
        sc.connect_hold = hold.fn();
        sc.closed_flag = closed;
    };
    ASSERT_TRUE(r.open_to_active()) << "setup";
    r.too_low_order();
    ASSERT_EQ(r.s.state(), fsm_state::Disconnected) << "setup";
    auto const visits = r.s.session->fsm_visit_history().size();
    auto rc_fut = r.spawn_reconnect();
    ASSERT_TRUE(r.s.pump_until([&] { return hold.entered; })) << "setup: the connect was not held";
    std::future<fixpp::core::expected_t<void>> close_fut;
    if (engine_stop) {
        session_test_access::note_engine_stop(*r.s.session);
    } else {
        close_fut = asio::co_spawn(fixpp::test_support::session_executor_of(*r.s.session, r.s.ioc),
                                   r.s.session->close(fixpp::session::close_mode::terminal),
                                   asio::use_future);
        (void)r.s.pump_until([&] { return rg::Scene::ready(close_fut); }, kHeldWindow);
    }
    hold.released = true;
    EXPECT_TRUE(r.s.pump_until([&] { return rg::Scene::ready(rc_fut); }))
        << "drive_reconnect did not return";
    ASSERT_TRUE(rg::Scene::ready(rc_fut));
    auto const rc = rc_fut.get();
    ASSERT_FALSE(rc.has_value()) << "the reconnect succeeded on a closed session";
    EXPECT_EQ(rc.error(), fixpp::core::error::session_already_closed)
        << static_cast<int>(rc.error());
    EXPECT_TRUE(closed->load()) << "the new transport was not closed";
    EXPECT_EQ(r.s.session->fsm_visit_history().size(), visits) << "a transition was recorded";
    EXPECT_NE(r.s.state(), fsm_state::LogonSent);
}

INSTANTIATE_TEST_SUITE_P(CloseAndStop, Drain103CommitPoint, ::testing::Bool(),
                         [](auto const& info) {
                             return std::string{info.param ? "EngineStop" : "CloseTerminal"};
                         });

// G7 (tasks T117): drive_reconnect spawned from fromApp (false) and from toApp (true), on
// an Active session. onLogout runs in its own callback scope (depth 1), no assertion fires,
// and the reconnect completes after the callback (for toApp, after its emit) returns.
class Drain103CallbackEntry : public ::testing::TestWithParam<bool> {};

TEST_P(Drain103CallbackEntry, G7_ReconnectFromACallback_OnLogoutInItsOwnScope) {
    bool const from_to_app = GetParam();
    Rig r{fixpp::session::reset_seqnum_policy::bilateral_lenient, true, false};
    ASSERT_TRUE(r.open_to_active()) << "setup";
    std::future<fixpp::core::expected_t<void>> rc_fut;
    bool spawned = false;
    auto spawn = [&] {
        if (spawned) return;
        spawned = true;
        rc_fut = r.spawn_reconnect();
    };
    std::future<fixpp::core::expected_t<void>> send_fut;
    if (from_to_app) {
        r.app->on_to_app = spawn;
        send_fut = spawn_send(r, order_payload("ORD-G7"));
    } else {
        r.app->on_from_app = spawn;
        (void)r.s.feed_spawn(r.s.order(r.s.next_in()));
    }
    EXPECT_TRUE(r.s.pump_until([&] { return spawned && rg::Scene::ready(rc_fut); }))
        << "the reconnect did not complete";
    ASSERT_TRUE(spawned) << "setup: the callback did not run";
    ASSERT_TRUE(rg::Scene::ready(rc_fut));
    auto const rc = rc_fut.get();
    EXPECT_TRUE(rc.has_value()) << static_cast<int>(rc.error());
    ASSERT_EQ(r.app->logout_depths.size(), 1U) << "onLogout must fire once";
    EXPECT_EQ(r.app->logout_depths.front(), 1U) << "onLogout ran nested in the callback";
    EXPECT_EQ(r.app->logout_states.front(), fsm_state::Disconnected)
        << "onLogout fired on the edge into " << rg::state_name(r.app->logout_states.front());
    if (from_to_app) {
        ASSERT_TRUE(rg::Scene::ready(send_fut));
        EXPECT_TRUE(send_fut.get().has_value()) << "the send whose toApp started the reconnect";
        EXPECT_FALSE(seqs_of(old_frames(r), "D").empty())
            << "the send was not written on the old transport: " << Rig::frames_text(old_frames(r));
    }
}

INSTANTIATE_TEST_SUITE_P(FromAppAndToApp, Drain103CallbackEntry, ::testing::Bool(),
                         [](auto const& info) {
                             return std::string{info.param ? "ToApp" : "FromApp"};
                         });

// G8 (tasks T118): the old connection's detached coroutines. Active, a liveness loop and a
// pending mark (a bound alive); drive_reconnect from Active. After the install neither the
// old loop nor the old bound exists; the new Active spawns the one loop. Twin: a too-high
// Logout at once on the new connection is acknowledged at its own mark's T, never the old's.
TEST(Drain103, G8_OldLoopAndBoundRetired_NewMarkAcknowledgedAtItsOwnT) {
    Rig r;
    ASSERT_TRUE(r.open_to_active()) << "setup";
    ASSERT_EQ(session_test_access::liveness_count(*r.s.session), 1) << "setup: no liveness loop";
    (void)r.s.feed(r.s.logout(r.s.next_in() + 3));
    ASSERT_EQ(session_test_access::peer_logout_phase(*r.s.session), 1) << "setup: no pending mark";
    auto const old_mark_at = r.s.mock->steady_now();
    auto const rc = r.reconnect();
    ASSERT_TRUE(rc.has_value()) << static_cast<int>(rc.error());
    EXPECT_EQ(session_test_access::liveness_count(*r.s.session), 0)
        << "the old liveness loop outlived the install";
    EXPECT_EQ(session_test_access::peer_logout_bound_count(*r.s.session), 0)
        << "the old bound outlived the install";
    (void)r.s.feed(r.ack(1, true));
    ASSERT_EQ(r.s.state(), fsm_state::Active) << r.after_mark_text();
    EXPECT_EQ(session_test_access::liveness_count(*r.s.session), 1);
    // The new mark, half a bound after the old one.
    auto const t = std::chrono::milliseconds{kLogoutTimeoutMs};
    r.s.pass(t / 2);
    (void)r.s.feed(r.s.logout(r.s.next_in() + 3));
    ASSERT_EQ(session_test_access::peer_logout_phase(*r.s.session), 1) << "setup: no new mark";
    r.s.pass(t / 2 + std::chrono::milliseconds{1});  // past the old mark's T, before the new's
    ASSERT_GT(r.s.mock->steady_now(), old_mark_at + t);
    EXPECT_TRUE(r.after_mark_of("5").empty())
        << "acknowledged at the old mark's T: " << r.after_mark_text();
    r.s.pass(t / 2);  // past the new mark's T
    EXPECT_EQ(r.after_mark_of("5").size(), 1U) << r.after_mark_text();
}

// LO-6b (tasks T120): a standalone initiator; a SendingTime-fatal frame whose Reject's store
// is held; drive_reconnect from Active (the drain records Disconnected and waits on the
// inbound chain); release; the reconnect completes; then an in-sequence peer Logout on the
// new connection.
TEST(Drain103, LO6b_FatalChainHeldThroughTheDrain_NoLogoutAfterItsDisconnected) {
    Rig r{fixpp::session::reset_seqnum_policy::bilateral_lenient, true, true};
    ASSERT_TRUE(r.open_to_active()) << "setup";
    r.s.log->arm();
    hold_store_of(r, next_out(r));  // the Reject, before the chain's Logout
    auto chain = r.s.feed_spawn(r.s.order_with_stale_sending_time(r.s.next_in()));
    ASSERT_TRUE(r.s.pump_until([&] { return r.s.log->holding(); }))
        << "setup: the Reject's store was not held";
    auto rc_fut = r.spawn_reconnect();
    r.s.pump_for(kHeldWindow);
    r.s.log->release_hold();
    EXPECT_TRUE(r.s.pump_until([&] { return rg::Scene::ready(rc_fut) && rg::Scene::ready(chain); },
                               kAfterReleaseBudget))
        << "the reconnect or the chain did not complete";
    EXPECT_TRUE(seqs_of(old_frames(r), "5").empty())
        << "a Logout after the drain's Disconnected: " << Rig::frames_text(old_frames(r));
    ASSERT_TRUE(rg::Scene::ready(rc_fut));
    auto const rc = rc_fut.get();
    ASSERT_TRUE(rc.has_value()) << static_cast<int>(rc.error());
    (void)r.s.feed(r.ack(1, true));
    ASSERT_EQ(r.s.state(), fsm_state::Active) << Rig::frames_text(r.new_transport_frames());
    (void)r.s.feed(r.s.logout(r.s.next_in()));
    EXPECT_EQ(seqs_of(r.new_transport_frames(), "5").size(), 1U)
        << Rig::frames_text(r.new_transport_frames());
}

// Arm D (tasks T110): the mark cleared across a reconnect that passes through Disconnected
// first. A too-high Logout sets the mark; a wrong-CompID frame records Disconnected;
// drive_reconnect; Active; the clock past the old mark's arrival + T.
TEST(ResendStateReconnect103, ArmD_MarkClearedAcrossAReconnectThroughDisconnected) {
    Rig r;
    ASSERT_TRUE(r.open_to_active()) << "setup";
    (void)r.s.feed(r.s.logout(r.s.next_in() + 3));
    auto const mark_at = r.s.mock->steady_now();
    (void)r.s.feed(r.s.order_from_other_comp_id(r.s.next_in()));
    ASSERT_EQ(r.s.state(), fsm_state::Disconnected) << "setup";
    auto const rc = r.reconnect();
    ASSERT_TRUE(rc.has_value()) << static_cast<int>(rc.error());
    (void)r.s.feed(r.ack(1, true));
    ASSERT_EQ(r.s.state(), fsm_state::Active) << r.after_mark_text();
    r.s.pass(std::chrono::milliseconds{kLogoutTimeoutMs} + std::chrono::seconds{1});
    ASSERT_GT(r.s.mock->steady_now(), mark_at + std::chrono::milliseconds{kLogoutTimeoutMs});
    EXPECT_TRUE(r.after_mark_of("5").empty()) << r.after_mark_text();
    auto fut = spawn_send(r, order_payload("ORD-ARM-D"));
    ASSERT_TRUE(t103::pump_or_fail(
        r.s.ioc, [&] { return rg::Scene::ready(fut); }, fixpp::test_support::kPumpBudget,
        "t103_578/armD_send"));
    auto const sr = fut.get();
    EXPECT_TRUE(sr.has_value()) << "the send was refused: " << static_cast<int>(sr.error());
}

// ── F1 and F2 (tasks T088, T089): the bound's late start and a lost wake ─────────────

// The bound T of F1 and F2, in milliseconds.
constexpr std::uint32_t kBoundT = 2000;

// A test seam the bound awaits: it records that it ran, then yields until released.
struct BoundHold {
    // Releases the hold when the cell's scope ends, whatever path ended it.
    struct release_on_exit {
        BoundHold& h;
        ~release_on_exit() { h.released = true; }
    };
    bool entered = false;
    bool released = false;
    std::function<asio::awaitable<void>()> fn() {
        return [this]() -> asio::awaitable<void> {
            entered = true;
            auto const ex = co_await asio::this_coro::executor;
            while (!released) co_await asio::post(ex, asio::use_awaitable);
        };
    }
};

// Teardown on a failure path: a bound still running is ended at its own deadline, so the
// Session is not destroyed with it (~Session asserts its join counter is 0) and the cell
// ends as a test failure, not an abort.
void end_a_bound_still_running(rg::Scene& s, std::chrono::milliseconds bound_t,
                               std::future<fixpp::core::expected_t<void>>& close_fut) {
    if (fixpp::session::session_test_access::peer_logout_bound_count(*s.session) == 0) return;
    s.mock->advance(bound_t);
    (void)s.pump_until([&] {
        return rg::Scene::ready(close_fut) &&
               fixpp::session::session_test_access::peer_logout_bound_count(*s.session) == 0;
    });
}

std::future<fixpp::core::expected_t<void>> spawn_terminal_close(rg::Scene& s) {
    return asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                          s.session->close(fixpp::session::close_mode::terminal), asio::use_future);
}

// F1 (research D4.4, "Publish, then re-test"): the hook holds the bound before its first run
// until close(terminal) has begun; released, the bound finds the session closing and exits.
// close() completes within a bounded pump, and the join counter is 0. The mock clock is not
// moved, so the bound's T cannot end any wait: the cell's forced spurious-hit arm.
TEST(BoundProtocol103, F1_BoundStartsAfterCloseBegan_CloseCompletesCounterZero) {
    // Declared before the Scene and released by `release` after it, so a failure path's
    // teardown resumes the held bound over a live BoundHold.
    BoundHold hold;
    rg::Scene s{rg::Options{.logout_timeout_ms = kBoundT}};
    BoundHold::release_on_exit const release{hold};
    ASSERT_TRUE(s.open_to_active());
    fixpp::session::session_test_access::set_peer_logout_bound_hold_first_run(*s.session,
                                                                              hold.fn());
    seqnum_t const n = s.next_in();
    auto feed_fut = s.feed_spawn(s.logout(n + 3));
    ASSERT_TRUE(s.pump_until([&] { return hold.entered && rg::Scene::ready(feed_fut); }))
        << "setup: the bound did not reach its first-run seam";
    auto close_fut = spawn_terminal_close(s);
    s.pump_for(std::chrono::milliseconds{20});
    hold.released = true;
    EXPECT_TRUE(s.pump_until([&] { return rg::Scene::ready(close_fut); }))
        << "close() did not complete";
    EXPECT_EQ(fixpp::session::session_test_access::peer_logout_bound_count(*s.session), 0);
    end_a_bound_still_running(s, std::chrono::milliseconds{kBoundT}, close_fut);
}

// F2 (research D4.4, "Wake sources"): the hook holds the bound after its publish-and-re-test,
// before its race's wait; a wrong-SenderCompID frame records Disconnected, whose wake lands
// on a timer with no wait pending; released, the bound waits. close() completes within a
// bounded pump through its unconditional wake, and the join counter is 0. The mock clock is
// not moved.
TEST(BoundProtocol103, F2_EdgeWakeLostBeforeTheRace_CloseCompletesCounterZero) {
    // Declared before the Scene and released by `release` after it, so a failure path's
    // teardown resumes the held bound over a live BoundHold.
    BoundHold hold;
    rg::Scene s{rg::Options{.logout_timeout_ms = kBoundT}};
    BoundHold::release_on_exit const release{hold};
    ASSERT_TRUE(s.open_to_active());
    fixpp::session::session_test_access::set_peer_logout_bound_hold_before_race(*s.session,
                                                                                hold.fn());
    seqnum_t const n = s.next_in();
    auto feed_fut = s.feed_spawn(s.logout(n + 3));
    ASSERT_TRUE(s.pump_until([&] { return hold.entered && rg::Scene::ready(feed_fut); }))
        << "setup: the bound did not reach its before-race seam";
    auto wrong = s.feed_spawn(s.order_from_other_comp_id(n));
    ASSERT_TRUE(s.pump_until([&] { return rg::Scene::ready(wrong); }));
    ASSERT_EQ(s.state(), fsm_state::Disconnected) << "setup";
    hold.released = true;
    s.pump_for(std::chrono::milliseconds{20});
    auto close_fut = spawn_terminal_close(s);
    EXPECT_TRUE(s.pump_until([&] { return rg::Scene::ready(close_fut); }))
        << "close() did not complete";
    EXPECT_EQ(fixpp::session::session_test_access::peer_logout_bound_count(*s.session), 0);
    end_a_bound_still_running(s, std::chrono::milliseconds{kBoundT}, close_fut);
}

// F3 (research D4.3, D4.4): N passes S while the bound is held at its first-run seam, so the
// frame path's wake lands before the bound has published its own. Released, the bound reads N
// past S at its first test and acknowledges with no race, the mock clock not moved.
TEST(BoundProtocol103, F3_NPassedSBeforeTheFirstTest_AckWithNoClockMove) {
    BoundHold hold;
    rg::Scene s{rg::Options{.logout_timeout_ms = kBoundT}};
    BoundHold::release_on_exit const release{hold};
    ASSERT_TRUE(s.open_to_active());
    fixpp::session::session_test_access::set_peer_logout_bound_hold_first_run(*s.session,
                                                                              hold.fn());
    seqnum_t const n = s.next_in();
    auto const mark = s.sent.size();
    auto feed_fut = s.feed_spawn(s.logout(n + 3));
    ASSERT_TRUE(s.pump_until([&] { return hold.entered && rg::Scene::ready(feed_fut); }))
        << "setup: the bound did not reach its first-run seam";
    auto gf = s.feed_spawn(s.gap_fill(n, n + 4));
    ASSERT_TRUE(s.pump_until([&] { return rg::Scene::ready(gf); })) << "setup: the GapFill";
    ASSERT_EQ(s.count("5", mark), 0U) << "setup: acknowledged before the bound ran";
    hold.released = true;
    EXPECT_TRUE(s.pump_until([&] { return s.count("5", mark) == 1U; },
                             std::chrono::milliseconds{kBoundT} / 4))
        << "no acknowledgement with N past S at the bound's first test: " << s.written(mark);
    if (fixpp::session::session_test_access::peer_logout_bound_count(*s.session) != 0) {
        // A bound still running is ended at its own deadline (see end_a_bound_still_running).
        s.mock->advance(std::chrono::milliseconds{kBoundT});
        (void)s.pump_until([&] {
            return fixpp::session::session_test_access::peer_logout_bound_count(*s.session) == 0;
        });
    }
    EXPECT_EQ(fixpp::session::session_test_access::peer_logout_bound_count(*s.session), 0);
}

// The drain's bound reached on a session with no live transport (research D1 step 3): a send's
// store call is held past T on a session whose sink is the config-time one, so at T there is
// no old transport to force-close; the drain goes on waiting, and once the hold is released
// the reconnect completes.
TEST(Drain103, G4_7_BoundReachedWithNoLiveTransport_DrainCompletesAfterTheRelease) {
    Rig r{fixpp::session::reset_seqnum_policy::bilateral_lenient, false, true};
    ASSERT_TRUE(r.open_to_active()) << "setup";
    r.s.log->arm();
    hold_store_of(r, next_out(r));
    auto send_fut = spawn_send(r, order_payload("ORD-G4-7"));
    ASSERT_TRUE(r.s.pump_until([&] { return r.s.log->holding(); })) << "setup";
    auto rc_fut = r.spawn_reconnect();
    auto const t = std::chrono::milliseconds{kLogoutTimeoutMs};
    EXPECT_FALSE(r.s.pump_until([&] { return rg::Scene::ready(rc_fut); }, t + t / 4))
        << "the drain ended while the send's store was held";
    r.s.log->release_hold();
    EXPECT_TRUE(
        r.s.pump_until([&] { return rg::Scene::ready(rc_fut) && rg::Scene::ready(send_fut); },
                       kAfterReleaseBudget))
        << "the reconnect or the send did not complete";
    ASSERT_TRUE(rg::Scene::ready(rc_fut));
    auto const rc = rc_fut.get();
    EXPECT_TRUE(rc.has_value()) << static_cast<int>(rc.error());
    EXPECT_EQ(r.factory->make_call_count.load(), 1) << "transports made";
}

// The drain's wait for the old connection's detached coroutines (research D1 step 3): the
// bound is held at its first-run seam, so the drain polls for its join counter. During that
// poll a close(terminal) runs (G4_5) or the caller's own slot emits (G4_6). Released, the
// drain returns session_already_closed (G4_5) or dispatch_aborted (G4_6), and no transport is
// made. The mock clock is not moved, so the bound's T ends no wait.
void start_drain_behind_a_held_bound(Rig& r, BoundHold& hold) {
    ASSERT_TRUE(r.open_to_active()) << "setup";
    fixpp::session::session_test_access::set_peer_logout_bound_hold_first_run(*r.s.session,
                                                                              hold.fn());
    seqnum_t const n = r.s.next_in();
    auto feed_fut = r.s.feed_spawn(r.s.logout(n + 3));
    ASSERT_TRUE(r.s.pump_until([&] { return hold.entered && rg::Scene::ready(feed_fut); }))
        << "setup: the bound did not reach its first-run seam";
}

TEST(Drain103, G4_5_CloseWhileTheDrainWaitsForTheBound_AlreadyClosedNoTransport) {
    BoundHold hold;
    Rig r{fixpp::session::reset_seqnum_policy::bilateral_lenient, false, false};
    BoundHold::release_on_exit const release{hold};
    ASSERT_NO_FATAL_FAILURE(start_drain_behind_a_held_bound(r, hold));
    auto rc_fut = r.spawn_reconnect();
    r.s.pump_for(std::chrono::milliseconds{20});
    ASSERT_FALSE(rg::Scene::ready(rc_fut)) << "setup: the drain did not wait for the bound";
    auto close_fut = spawn_terminal_close(r.s);
    r.s.pump_for(std::chrono::milliseconds{20});
    hold.released = true;
    EXPECT_TRUE(r.s.pump_until(
        [&] { return rg::Scene::ready(rc_fut) && rg::Scene::ready(close_fut); }, kHeldWindow))
        << "the reconnect or the close did not complete";
    ASSERT_TRUE(rg::Scene::ready(rc_fut));
    auto const rc = rc_fut.get();
    ASSERT_FALSE(rc.has_value()) << "the reconnect succeeded after close()";
    EXPECT_EQ(rc.error(), fixpp::core::error::session_already_closed)
        << static_cast<int>(rc.error());
    EXPECT_EQ(r.factory->make_call_count.load(), 0) << "a transport was made";
    end_a_bound_still_running(r.s, std::chrono::milliseconds{kLogoutTimeoutMs}, close_fut);
}

TEST(Drain103, G4_6_CallersCancellationWhileTheDrainWaitsForTheBound_DispatchAborted) {
    BoundHold hold;
    Rig r{fixpp::session::reset_seqnum_policy::bilateral_lenient, false, false};
    BoundHold::release_on_exit const release{hold};
    ASSERT_NO_FATAL_FAILURE(start_drain_behind_a_held_bound(r, hold));
    auto const out = std::make_shared<std::optional<fixpp::core::expected_t<void>>>();
    asio::cancellation_signal sig;
    auto* session = r.s.session.get();
    r.reconnect_mark = r.s.sent.size();
    asio::co_spawn(
        fixpp::test_support::session_executor_of(*session, r.s.ioc),
        [session, out]() -> asio::awaitable<void> { *out = co_await session->drive_reconnect(); },
        asio::bind_cancellation_slot(sig.slot(), asio::detached));
    r.s.pump_for(std::chrono::milliseconds{20});
    ASSERT_FALSE(out->has_value()) << "setup: the drain did not wait for the bound";
    sig.emit(asio::cancellation_type::terminal);
    EXPECT_TRUE(r.s.pump_until([&] { return out->has_value(); }, kHeldWindow))
        << "drive_reconnect did not return on the caller's cancellation";
    hold.released = true;
    (void)r.s.pump_until([&] {
        return fixpp::session::session_test_access::peer_logout_bound_count(*r.s.session) == 0;
    });
    ASSERT_TRUE(out->has_value());
    ASSERT_FALSE((*out)->has_value()) << "the reconnect succeeded";
    EXPECT_EQ((*out)->error(), fixpp::core::error::dispatch_aborted)
        << static_cast<int>((*out)->error());
    EXPECT_EQ(r.factory->make_call_count.load(), 0) << "a transport was made";
    sig.slot().clear();
}

// A plaintext twin of MockReconnectFactory: its kind() is plaintext, so a session on the
// insecure_plain_tcp profile opens with it and the reconnect takes the plaintext branch.
class PlainMockReconnectFactory final : public fixpp::transport::TransportFactory {
public:
    std::atomic<int> make_call_count{0};
    std::function<void(fixpp::transport::test::Script&)> tweak_script;

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>> make(
        asio::any_io_executor exec, fixpp::tls::SslCtxConfig /*ssl_cfg*/,
        std::pmr::memory_resource* /*mr*/) noexcept override {
        ++make_call_count;
        fixpp::transport::test::Script script;
        if (tweak_script) tweak_script(script);
        return std::make_unique<fixpp::transport::test::mock_transport>(std::move(exec),
                                                                        std::move(script));
    }
    [[nodiscard]] fixpp::core::expected_t<void> reload_credentials(
        std::shared_ptr<fixpp::tls::cert_source> /*new_source*/) noexcept override {
        return {};
    }
    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source> cert_source_snapshot()
        const noexcept override {
        return nullptr;
    }
    [[nodiscard]] fixpp::transport::transport_security_kind kind() const noexcept override {
        return fixpp::transport::transport_security_kind::plaintext;
    }
};

// G6 on the plaintext branch (research D1 step 4; reconnect_fsm.cpp's plaintext fast path):
// close(terminal) during the held connect; released, the install refuses at the commit point,
// the new transport is closed, and drive_reconnect returns session_already_closed.
TEST(Drain103CommitPointPlain, G6_CloseDuringTheConnect_AlreadyClosedTransportClosed) {
    ConnectHold hold;
    Rig r{fixpp::session::reset_seqnum_policy::bilateral_lenient, true, false};
    struct release_on_exit {
        ConnectHold& h;
        ~release_on_exit() { h.released = true; }
    } const release{hold};
    auto const plain = std::make_shared<PlainMockReconnectFactory>();
    auto const closed = std::make_shared<std::atomic<bool>>(false);
    plain->tweak_script = [&hold, closed](fixpp::transport::test::Script& sc) {
        sc.connect_hold = hold.fn();
        sc.closed_flag = closed;
    };
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    r.s.cfg.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::insecure_plain_tcp};
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
    r.s.cfg.transport_factory_override = plain;
    r.s.session = std::make_unique<fixpp::session::Session>(r.s.engine, r.s.cfg);
    r.app->session = r.s.session.get();
    ASSERT_TRUE(r.open_to_active()) << "setup";
    r.too_low_order();
    ASSERT_EQ(r.s.state(), fsm_state::Disconnected) << "setup";
    auto const out_before = next_out(r);
    auto rc_fut = r.spawn_reconnect();
    ASSERT_TRUE(r.s.pump_until([&] { return hold.entered; })) << "setup: the connect was not held";
    auto close_fut = spawn_terminal_close(r.s);
    (void)r.s.pump_until([&] { return rg::Scene::ready(close_fut); }, kHeldWindow);
    hold.released = true;
    EXPECT_TRUE(r.s.pump_until([&] { return rg::Scene::ready(rc_fut); }))
        << "drive_reconnect did not return";
    ASSERT_TRUE(rg::Scene::ready(rc_fut));
    auto const rc = rc_fut.get();
    ASSERT_FALSE(rc.has_value()) << "the reconnect succeeded on a closed session";
    EXPECT_EQ(rc.error(), fixpp::core::error::session_already_closed)
        << static_cast<int>(rc.error());
    EXPECT_EQ(plain->make_call_count.load(), 1) << "transports made";
    EXPECT_TRUE(closed->load()) << "the new transport was not closed";
    EXPECT_NE(r.s.state(), fsm_state::LogonSent);
    EXPECT_TRUE(r.after_mark_of("A").empty())
        << "a Logon was written after the refused install: " << r.after_mark_text();
    EXPECT_EQ(next_out(r), out_before) << "a number was taken after the refused install";
}

}  // namespace
