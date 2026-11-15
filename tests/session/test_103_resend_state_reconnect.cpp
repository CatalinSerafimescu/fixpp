// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_103_resend_state_reconnect.cpp — test-only.
//
// Feature 103 (specs/103-resend-logon-reset-fidelity/), fixpp#578: the resend state ends with
// the connection. research.md "RED cells", "#578 — resend state across an in-place
// reconnect"; tasks T018 (arms A, B, E and the fresh-Session control), T039 (arm C) and T118
// (G8's zero-downtime cell).
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

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "session/test_103_support.hpp"

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

    void onLogout(const fixpp::session::SessionId& /*id*/) override {
        logout_states.push_back(session != nullptr ? session->state() : fsm_state::NotConnected);
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
                 bool const reset_on_logon = true)
        : s{rg::Options{.app = app}} {
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
        auto fut = asio::co_spawn(s.ioc, s.session->open(), asio::use_future);
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
        auto fut = asio::co_spawn(s.ioc, s.session->drive_reconnect(), asio::use_future);
        if (!t103::pump_or_fail(
                s.ioc, [&] { return rg::Scene::ready(fut); }, fixpp::test_support::kPumpBudget,
                "t103_578/drive_reconnect")) {
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        auto r = fut.get();
        s.drain();
        return r;
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

// ── Arm E: drive_reconnect from Active with a too-high Logout received ──────────────────────
// research D1, the drain: GREEN only once the drain and the commit point land
// (tasks T129, T131); the mark's half needs D4.
// Disabled until then (tasks T131 drops the prefix); its RED is read with
// --gtest_also_run_disabled_tests.
TEST(ResendStateReconnect103,
     DISABLED_ArmE_ReconnectFromActive_OldLogoutNeverAcknowledgedOnTheNewOne) {
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

    std::string const payload =
        "35=D\x01"
        "11=ORD-E\x01" +
        std::string{rg::kSideAndTime};
    auto fut =
        asio::co_spawn(r.s.ioc,
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

}  // namespace
