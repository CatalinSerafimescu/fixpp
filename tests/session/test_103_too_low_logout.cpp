// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_103_too_low_logout.cpp — test-only.
//
// Feature 103 (specs/103-resend-logon-reset-fidelity/), fixpp#581, research D9 (FR-018) and
// D6's too-low Heartbeat (tasks T136 to T143): in Active, a frame below NextNumIn without
// PossDupFlag(43)=Y that reaches the sequence check draws QuickFIX's too-low Logout,
// "MsgSeqNum too low, expecting <N> but received <M>", then Disconnected. The cells B-1 to
// B-8.
//
// Each cell runs a directly driven Session (resend_gap_scene.hpp's Scene); the cells drive
// NextNumIn to 5 with in-sequence orders first. The cells B-1 to B-6 and B-8 read no 103
// state; B-7 reads the too-high Logout's mark and is last in the file.
#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "session/test_103_support.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"

namespace {

namespace rg = fixpp::test_support::resend_gap;
using namespace std::chrono_literals;
using fixpp::core::error;
using fixpp::session::fsm_state;
using fixpp::session::seqnum_t;
using fixpp::session::session_role;
using fixpp::session::session_test_access;
using fixpp::test_support::store_op;
using fixpp::test_support::StoreCall;

// NextNumIn once a cell has driven it.
constexpr seqnum_t kN = 5;

// Records, in order, each toAdmin ("toAdmin:<35>") and each onLogout. With
// `throw_on_logout`, toAdmin throws on a Logout(35=5).
class TooLowApp final : public rg::GapApp {
public:
    std::vector<std::string> events;
    bool throw_on_logout = false;

    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
                 const fixpp::session::SessionId& id) override {
        rg::GapApp::toAdmin(msg, id);
        events.push_back("toAdmin:" + std::string{msg.msg_type()});
        if (throw_on_logout && msg.msg_type() == "5") {
            throw std::runtime_error("TooLowApp: toAdmin");
        }
    }
    void onLogout(const fixpp::session::SessionId& /*id*/) override {
        events.emplace_back("onLogout");
    }
};

[[nodiscard]] std::string too_low_text(seqnum_t n, seqnum_t m) {
    return "MsgSeqNum too low, expecting " + std::to_string(n) + " but received " +
           std::to_string(m);
}

[[nodiscard]] bool is_inbound_persist(StoreCall const& c) {
    return c.op == store_op::next_seqnum && c.dir == fixpp::session::direction_t::inbound &&
           c.increment;
}

// Feeds in-sequence orders until NextNumIn is kN. With a recording store armed, the last
// one's inbound persist is the instrument's positive control: false if it is not logged.
[[nodiscard]] bool drive_to_n(rg::Scene& s) {
    std::size_t mark = 0;
    while (s.next_in() < kN) {
        if (s.log) mark = s.log->mark();
        if (!s.feed(s.order(s.next_in()))) return false;
    }
    if (s.log && s.log->first_after(mark, is_inbound_persist) == nullptr) return false;
    return s.next_in() == kN && s.state() == fsm_state::Active;
}

[[nodiscard]] rg::Options persisting(session_role role, std::shared_ptr<TooLowApp> app) {
    return rg::Options{.role = role,
                       .app = std::move(app),
                       .recording_store = true,
                       .recording_store_persistent = true};
}

// What a too-low frame drew: B-1's assertions, for a frame at `m`, from `mark` (sent) and
// `log_mark` (the store log), with `events_before` application events and `delivered_before`
// fromApp calls before it.
void expect_too_low_logout(rg::Scene const& s, TooLowApp const& app, std::size_t mark,
                           std::size_t log_mark, std::size_t events_before,
                           std::size_t delivered_before, seqnum_t m,
                           fixpp::core::expected_t<void> const& r) {
    EXPECT_TRUE(r.has_value()) << "on_inbound_frame returned "
                               << (r ? 0 : static_cast<int>(r.error()));
    auto const logouts = s.of_type("5", mark);
    ASSERT_EQ(logouts.size(), 1U) << "the session wrote " << s.written(mark);
    EXPECT_EQ(rg::field(logouts.front(), 58), too_low_text(kN, m));
    EXPECT_EQ(s.state(), fsm_state::Disconnected) << rg::state_name(s.state());
    EXPECT_EQ(s.next_in(), kN) << "NextNumIn moved";
    if (s.log) {
        EXPECT_EQ(s.log->first_after(log_mark, is_inbound_persist), nullptr)
            << "an inbound number was persisted for the too-low frame";
    }
    std::vector<std::string> const after(
        app.events.begin() + static_cast<std::ptrdiff_t>(events_before), app.events.end());
    EXPECT_EQ(after, (std::vector<std::string>{"toAdmin:5", "onLogout"}))
        << "toAdmin once, on the Logout, then onLogout";
    EXPECT_EQ(app.delivered.size(), delivered_before) << "fromApp ran for the too-low frame";
}

class TooLowLogout103 : public ::testing::TestWithParam<session_role> {};

}  // namespace

// ── B-1 (T136): an application frame at 34=2 without 43=Y ──────────────────────────────
TEST_P(TooLowLogout103, B1_ApplicationFrameBelowNextNumIn_LogoutWithTheTextThenDisconnected) {
    auto app = std::make_shared<TooLowApp>();
    rg::Scene s{persisting(GetParam(), app)};
    ASSERT_TRUE(s.open_to_active()) << "setup";
    s.log->arm();
    ASSERT_TRUE(drive_to_n(s)) << "setup: NextNumIn " << s.next_in() << ", or no persist logged";
    auto const mark = s.sent.size();
    auto const log_mark = s.log->mark();
    auto const events_before = app->events.size();
    auto const delivered_before = app->delivered.size();
    auto const r = s.feed(s.order(2));
    expect_too_low_logout(s, *app, mark, log_mark, events_before, delivered_before, 2, r);
}

// ── B-2 (T137): a Heartbeat at 34=1 without 43=Y, with no gap and with a gap ────────────
TEST_P(TooLowLogout103, B2_HeartbeatBelowNextNumInNoGap_LogoutWithTheText) {
    auto app = std::make_shared<TooLowApp>();
    rg::Scene s{persisting(GetParam(), app)};
    ASSERT_TRUE(s.open_to_active()) << "setup";
    s.log->arm();
    ASSERT_TRUE(drive_to_n(s)) << "setup";
    auto const mark = s.sent.size();
    auto const log_mark = s.log->mark();
    auto const events_before = app->events.size();
    auto const r = s.feed(s.heartbeat(1));
    expect_too_low_logout(s, *app, mark, log_mark, events_before, app->delivered.size(), 1, r);
}

TEST_P(TooLowLogout103, B2_HeartbeatBelowNextNumInWithAGapBeforeTheDeadline_LogoutWithTheText) {
    auto app = std::make_shared<TooLowApp>();
    rg::Scene s{rg::Options{.role = GetParam(), .app = app}};
    ASSERT_TRUE(s.open_to_active()) << "setup";
    ASSERT_TRUE(drive_to_n(s)) << "setup";
    auto const before = s.sent.size();
    (void)s.feed(s.order(kN + rg::kGapWidth));
    ASSERT_EQ(s.count("2", before), 1U) << "setup: no gap opened: " << s.written(before);
    ASSERT_TRUE(s.resend_outstanding()) << "setup";
    s.pass(15s);
    auto const mark = s.sent.size();
    auto const events_before = app->events.size();
    auto const delivered_before = app->delivered.size();
    auto const r = s.feed(s.heartbeat(1));
    expect_too_low_logout(s, *app, mark, 0, events_before, delivered_before, 1, r);
}

// Control: a Heartbeat below NextNumIn with 43=Y is ignored.
TEST_P(TooLowLogout103, B2Control_HeartbeatBelowNextNumInWithPossDup_Ignored) {
    rg::Scene s{rg::Options{.role = GetParam()}};
    ASSERT_TRUE(s.open_to_active()) << "setup";
    ASSERT_TRUE(drive_to_n(s)) << "setup";
    auto const mark = s.sent.size();
    auto const r = s.feed(s.frame("0", 1,
                                  "43=Y\x01"
                                  "122=" +
                                      s.sending_time() + "\x01"));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(s.state(), fsm_state::Active) << rg::state_name(s.state());
    EXPECT_EQ(s.sent.size(), mark) << "the session wrote " << s.written(mark);
    EXPECT_EQ(s.next_in(), kN);
}

// Control: with validate_sequence_numbers off the Heartbeat is dropped.
TEST_P(TooLowLogout103, B2Control_KnobOff_HeartbeatBelowNextNumInDropped) {
    rg::Scene s{rg::Options{.role = GetParam(), .validate_sequence_numbers = false}};
    ASSERT_TRUE(s.open_to_active()) << "setup";
    ASSERT_TRUE(drive_to_n(s)) << "setup";
    auto const mark = s.sent.size();
    auto const r = s.feed(s.heartbeat(1));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(s.state(), fsm_state::Active) << rg::state_name(s.state());
    EXPECT_EQ(s.sent.size(), mark) << "the session wrote " << s.written(mark);
}

// ── B-3 (T138): a gap open past 2 × HB, then a too-low frame ────────────────────────────
// The traffic that carries the session past the bound: the loop's TestRequest after one
// quiet HeartBtInt, answered before the bound by a Heartbeat above NextNumIn (B-537-3).
TEST_P(TooLowLogout103, B3_GapPastTheDeadlineThenATooLowFrame_OneLogoutWithTheDeadlinesText) {
    rg::Scene s{rg::Options{.role = GetParam()}};
    ASSERT_TRUE(s.open_to_active()) << "setup";
    ASSERT_TRUE(drive_to_n(s)) << "setup";
    auto const gap_mark = s.sent.size();
    (void)s.feed(s.order(kN + rg::kGapWidth));
    ASSERT_EQ(s.count("2", gap_mark), 1U) << "setup: no gap opened: " << s.written(gap_mark);
    auto const e = s.mock->steady_now();
    s.pass(rg::kHeartBtInt);
    ASSERT_EQ(s.count("1", gap_mark), 1U) << "setup: " << s.written(gap_mark);
    (void)s.feed(s.heartbeat(kN + rg::kGapWidth + 1, s.last_test_req_id()));
    ASSERT_FALSE(s.test_request_pending()) << "setup: the TestRequest was not answered";
    s.pass(rg::kHeartBtInt + 1s);
    ASSERT_GE(s.mock->steady_now() - e, rg::kGapBound) << "setup: not past the bound";
    ASSERT_EQ(s.state(), fsm_state::Active) << "setup: " << s.written(gap_mark);
    ASSERT_EQ(s.next_in(), kN) << "setup";

    auto const mark = s.sent.size();
    auto const r = s.feed(s.order(2));
    EXPECT_TRUE(r.has_value());
    auto const logouts = s.of_type("5", mark);
    ASSERT_EQ(logouts.size(), 1U) << "the session wrote " << s.written(mark);
    EXPECT_EQ(rg::field(logouts.front(), 58),
              "ResendRequest unanswered, expecting MsgSeqNum " + std::to_string(kN));
    EXPECT_EQ(s.state(), fsm_state::Disconnected) << rg::state_name(s.state());
}

// ── B-4 (T139): toAdmin throws on the Logout ─────────────────────────────────────────────
TEST_P(TooLowLogout103, B4_ToAdminThrowsOnTheLogout_AppCallbackThrewDisconnected) {
    auto app = std::make_shared<TooLowApp>();
    app->throw_on_logout = true;
    rg::Scene s{rg::Options{.role = GetParam(), .app = app}};
    ASSERT_TRUE(s.open_to_active()) << "setup";
    ASSERT_TRUE(drive_to_n(s)) << "setup";
    auto const r = s.feed(s.order(2));
    ASSERT_FALSE(r.has_value()) << "on_inbound_frame succeeded";
    EXPECT_EQ(r.error(), error::app_callback_threw) << static_cast<int>(r.error());
    EXPECT_EQ(s.state(), fsm_state::Disconnected) << rg::state_name(s.state());
}

// ── B-5 (T140): CompIDs long enough that the leaf's Logout does not fit ─────────────────
// An initiator whose TargetCompID is `length` characters, which the peer's frames carry as
// their SenderCompID(49). The length is computed with the builders over a large buffer:
// the least one at which the too-low Logout no longer fits the leaf's buffer, while the
// Logon does. The arm one shorter is the near miss: the Logout fits and is written.
namespace {

constexpr std::size_t kLeafBuffer = 256;  // inbound_logout_leaf_'s buffer and the Logon's

[[nodiscard]] std::size_t logout_size(std::string const& target, seqnum_t seq,
                                      std::string const& st) {
    std::array<std::byte, 4096> big{};
    auto const r =
        fixpp::session::build_logout(big, seq, "ISLD", target, too_low_text(kN, 2), "FIX.4.2", st);
    return r ? r->size() : 0U;
}
[[nodiscard]] std::size_t logon_size(std::string const& target, std::string const& st) {
    std::array<std::byte, 4096> big{};
    auto const r = fixpp::session::build_logon(big, 1, "ISLD", target, "FIX.4.2",
                                               static_cast<int>(rg::kHeartBtInt.count()), st);
    return r ? r->size() : 0U;
}

struct LongIds {
    std::shared_ptr<TooLowApp> app = std::make_shared<TooLowApp>();
    rg::Scene s{rg::Options{.app = app}};
    std::string peer;

    explicit LongIds(std::size_t length) : peer(length, 'P') {
        s.cfg.target_comp_id = peer;
        s.session = std::make_unique<fixpp::session::Session>(s.engine, s.cfg);
    }
    [[nodiscard]] std::vector<std::byte> from_peer(char const* type, seqnum_t seq,
                                                   std::string const& extra) const {
        return rg::Scene::raw(type, seq, peer, s.sending_time(), extra);
    }
    [[nodiscard]] bool to_n() {
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                  s.session->open(), asio::use_future);
        if (!s.pump_until([&] { return rg::Scene::ready(fut); }) || !fut.get()) return false;
        (void)s.feed(from_peer("A", 1,
                               "98=0\x01"
                               "108=" +
                                   std::to_string(rg::kHeartBtInt.count()) + "\x01"));
        for (seqnum_t q = 2; q < kN && s.state() == fsm_state::Active; ++q) {
            (void)s.feed(from_peer("D", q, rg::Scene::order_fields(q)));
        }
        return s.state() == fsm_state::Active && s.next_in() == kN;
    }
};

// The least TargetCompID length whose too-low Logout does not fit, or 0 if no length fits
// the Logon but not the Logout.
[[nodiscard]] std::size_t overflowing_length(seqnum_t logout_seq, std::string const& st) {
    for (std::size_t len = 1; len < kLeafBuffer; ++len) {
        std::string const t(len, 'P');
        if (logout_size(t, logout_seq, st) > kLeafBuffer) {
            return logon_size(t, st) <= kLeafBuffer ? len : 0U;
        }
    }
    return 0U;
}

}  // namespace

TEST(TooLowLogoutLongCompIds103, B5_LogoutDoesNotFit_NoLogoutDisconnected_NearMissWritesIt) {
    // The Logout's number: an initiator that sent only its Logon numbers the Logout 2.
    seqnum_t const logout_seq = 2;
    std::size_t length = 0;
    {
        LongIds probe{1};
        length = overflowing_length(logout_seq, probe.s.sending_time());
    }
    ASSERT_NE(length, 0U) << "no TargetCompID length fits the Logon and not the Logout";

    LongIds over{length};
    ASSERT_TRUE(over.to_n()) << "setup: the session at length " << length << " is "
                             << rg::state_name(over.s.state());
    ASSERT_EQ(session_test_access::seqnum_mgr(*over.s.session).peek_outbound(), logout_seq);
    auto mark = over.s.sent.size();
    auto r = over.s.feed(over.from_peer("D", 2, rg::Scene::order_fields(2)));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(over.s.count("5", mark), 0U) << "the session wrote " << over.s.written(mark);
    EXPECT_EQ(over.s.state(), fsm_state::Disconnected) << rg::state_name(over.s.state());

    LongIds near{length - 1};
    ASSERT_TRUE(near.to_n()) << "setup: the near-miss session is "
                             << rg::state_name(near.s.state());
    mark = near.s.sent.size();
    r = near.s.feed(near.from_peer("D", 2, rg::Scene::order_fields(2)));
    EXPECT_TRUE(r.has_value());
    auto const logouts = near.s.of_type("5", mark);
    ASSERT_EQ(logouts.size(), 1U) << "the near miss wrote " << near.s.written(mark);
    EXPECT_EQ(rg::field(logouts.front(), 58), too_low_text(kN, 2));
    EXPECT_EQ(near.s.state(), fsm_state::Disconnected);
}

// ── B-6 (T141): a frame at 34=N with the seqnum lock drained ────────────────────────────
// The sequence check refuses it for a reason other than "too low": no Logout.
TEST_P(TooLowLogout103, B6_FrameAtNextNumInWithTheSeqnumLockDrained_NoLogout) {
    rg::Scene s{rg::Options{.role = GetParam()}};
    ASSERT_TRUE(s.open_to_active()) << "setup";
    ASSERT_TRUE(drive_to_n(s)) << "setup";
    auto drained =
        asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                       session_test_access::seqnum_mgr(*s.session).drain(), asio::use_future);
    ASSERT_TRUE(s.pump_until([&] { return rg::Scene::ready(drained); })) << "setup: drain";
    auto const mark = s.sent.size();
    (void)s.feed(s.order(kN));
    EXPECT_EQ(s.count("5", mark), 0U) << "the session wrote " << s.written(mark);
    EXPECT_EQ(s.state(), fsm_state::Disconnected) << rg::state_name(s.state());
}

// ── B-8 (T143): the carve-outs ───────────────────────────────────────────────────────────
// Validation on: a too-low frame that fails the dictionary is Rejected at the validate
// gate and the session stays Active (QuickFIX C++ validates before verify).
TEST_P(TooLowLogout103, B8_ValidationOnDictionaryInvalidTooLowFrame_RejectActiveNoLogout) {
    rg::Scene s{rg::Options{.role = GetParam(), .validate = true}};
    ASSERT_TRUE(s.open_to_active()) << "setup";
    ASSERT_TRUE(drive_to_n(s)) << "setup";
    auto const mark = s.sent.size();
    (void)s.feed(s.invalid_order(2));
    EXPECT_EQ(s.count("3", mark), 1U) << "the session wrote " << s.written(mark);
    EXPECT_EQ(s.count("5", mark), 0U) << "the session wrote " << s.written(mark);
    EXPECT_EQ(s.state(), fsm_state::Active) << rg::state_name(s.state());
}

// The control of the carve-out: validation on, a dictionary-valid too-low frame draws
// B-1's Logout.
TEST_P(TooLowLogout103, B8Control_ValidationOnDictionaryValidTooLowFrame_Logout) {
    auto app = std::make_shared<TooLowApp>();
    rg::Scene s{rg::Options{.role = GetParam(), .validate = true, .app = app}};
    ASSERT_TRUE(s.open_to_active()) << "setup";
    ASSERT_TRUE(drive_to_n(s)) << "setup";
    auto const mark = s.sent.size();
    auto const events_before = app->events.size();
    auto const delivered_before = app->delivered.size();
    auto const r = s.feed(s.order(2));
    expect_too_low_logout(s, *app, mark, 0, events_before, delivered_before, 2, r);
}

// A too-low application frame with 43=Y is ignored (validation off: the validation test
// dictionary has no PossDupFlag(43) in its header).
TEST_P(TooLowLogout103, B8_TooLowPossDupApplicationFrame_IgnoredActive) {
    rg::Scene s{rg::Options{.role = GetParam()}};
    ASSERT_TRUE(s.open_to_active()) << "setup";
    ASSERT_TRUE(drive_to_n(s)) << "setup";
    auto const mark = s.sent.size();
    (void)s.feed(s.poss_dup_order(2, rg::Scene::orig_time::valid));
    EXPECT_EQ(s.count("5", mark), 0U) << "the session wrote " << s.written(mark);
    EXPECT_EQ(s.state(), fsm_state::Active) << rg::state_name(s.state());
    EXPECT_EQ(s.next_in(), kN);
}

INSTANTIATE_TEST_SUITE_P(BothRoles, TooLowLogout103,
                         ::testing::Values(session_role::initiator, session_role::acceptor),
                         [](auto const& info) { return std::string{rg::role_name(info.param)}; });

// ── B-7 (T142): the mark pending, a too-low frame's Logout store held past T ────────────
// Arm B claims the pending mark, so its Logout is the one: the bound, at T, writes none.
namespace {
class TooLowLogoutMark103 : public ::testing::TestWithParam<session_role> {};
}  // namespace

TEST_P(TooLowLogoutMark103, B7_MarkPendingTooLowLogoutHeldPastT_ExactlyOneLogout) {
    constexpr std::uint32_t kT = 2000;
    rg::Scene s{rg::Options{.role = GetParam(),
                            .logout_timeout_ms = kT,
                            .recording_store = true,
                            .recording_store_persistent = true}};
    ASSERT_TRUE(s.open_to_active()) << "setup";
    s.log->arm();
    ASSERT_TRUE(drive_to_n(s)) << "setup";
    (void)s.feed(s.logout(kN + 3));
    ASSERT_EQ(session_test_access::peer_logout_phase(*s.session), 1) << "setup: no pending mark";
    seqnum_t const logout_seq = session_test_access::seqnum_mgr(*s.session).peek_outbound();
    s.log->hold_in = [logout_seq](StoreCall const& c) {
        return c.op == store_op::store && c.dir == fixpp::session::direction_t::outbound &&
               c.first == logout_seq;
    };
    auto const mark = s.sent.size();
    auto fut = s.feed_spawn(s.order(2));
    ASSERT_TRUE(s.pump_until([&] { return s.log->holding(); }))
        << "setup: the too-low frame's Logout store was not held: " << s.written(mark);
    s.mock->advance(std::chrono::milliseconds{kT} + 1s);
    s.pump_for(std::chrono::milliseconds{kT} / 8);
    s.log->release_hold();
    EXPECT_TRUE(
        s.pump_until([&] { return rg::Scene::ready(fut); }, std::chrono::milliseconds{kT} / 4))
        << "the delivery did not complete";
    s.pump_for(std::chrono::milliseconds{kT} / 8);
    auto const logouts = s.of_type("5", mark);
    ASSERT_EQ(logouts.size(), 1U) << "the session wrote " << s.written(mark);
    EXPECT_EQ(rg::field(logouts.front(), 58), too_low_text(kN, 2));
    EXPECT_EQ(s.state(), fsm_state::Disconnected) << rg::state_name(s.state());
}

INSTANTIATE_TEST_SUITE_P(BothRoles, TooLowLogoutMark103,
                         ::testing::Values(session_role::initiator, session_role::acceptor),
                         [](auto const& info) { return std::string{rg::role_name(info.param)}; });
