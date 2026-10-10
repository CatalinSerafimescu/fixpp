// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_544_inbound_split_cancel_cells.cpp
//
// fixpp#544 (B35 Phase 4, `.specify/544-hot-path-zero-alloc.md` §2.2; orchestrator ruling
// B -> b3). Splitting Session::on_inbound_frame into per-state arms, sub-arms and reply
// leaves adds `co_await`s on coroutines. asio checks the chain's cancellation state at every
// such `co_await` (`awaitable_frame_base::await_transform`), so a naive boundary would throw
// `operation_aborted` earlier than today. The b3 sequence suppresses the check at each new
// boundary, so the first check is still the moved block's first original `co_await`.
//
// Each cell drives one reply block whose user callback (toAdmin / toApp) runs before the
// block's first original `co_await`, under a chain that the test cancels just before the
// block. It asserts what today's code does: the callback runs, then the first original
// `co_await` throws, and the reply is not transmitted. The LogonSent and too-high
// build-failure cells drive the path where the reply fails to build and today's block has no
// `co_await` at all: the frame completes with a value.
//
// The cells are a differential oracle: each must hold at base and after the split. Mutant:
// reduce FIXPP_INBOUND_SPLIT_AWAIT to `auto var = co_await (call)` and
// FIXPP_INBOUND_SPLIT_ENTRY to a no-op in src/session/session.cpp; each cell must then fail.

#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/system_error.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <expected>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <functional>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"

using namespace std::chrono_literals;
using fixpp::core::expected_t;
using fixpp::wire::access_mode;
using fixpp::wire::MessageView;

namespace fixpp::session::test {
namespace {

// The mock clock's UTC seed, and two SendingTime(52) values: one at the seed (fresh) and one
// a year before it (outside the default MaxLatency).
constexpr std::string_view kFresh52 = "20240101-00:00:00.000";
constexpr std::string_view kStale52 = "20230101-00:00:00.000";
constexpr std::string_view kLater122 = "20240101-00:00:01.000";

std::vector<std::byte> make_frame(std::string_view msg_type, std::uint32_t seq,
                                  std::string_view sender, std::string_view target,
                                  std::string_view sending_time, const std::string& extra = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=" + std::string(sending_time) + "\x01";
    body += "56=" + std::string(target) + "\x01";
    body += extra;
    std::string full = "8=FIX.4.2\x01";
    full += "9=" + std::to_string(body.size()) + "\x01";
    full += body;
    unsigned int cs = 0;
    for (unsigned char c : full) cs += c;
    char csbuf[8];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs & 0xFFU);
    full += "10=" + std::string(csbuf) + "\x01";
    std::vector<std::byte> out;
    out.reserve(full.size());
    for (char c : full) out.push_back(static_cast<std::byte>(c));
    return out;
}

std::string field_of(std::span<const std::byte> frame, std::string_view tag) {
    const std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    const std::string needle = "\x01" + std::string(tag) + "=";
    auto pos = wire.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    return wire.substr(pos, wire.find('\x01', pos) - pos);
}

// A Clock that forwards to a mock_clock and calls `on_now` on each now() while armed.
class HookedClock final : public fixpp::core::Clock {
public:
    explicit HookedClock(asio::any_io_executor ex)
        : inner_(std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200},
                 fixpp::core::steady_time_point{}, std::move(ex)) {}

    [[nodiscard]] fixpp::core::utc_time_point now() const noexcept override {
        if (armed && on_now) {
            ++armed_now_calls;
            on_now(armed_now_calls);
        }
        return inner_.now();
    }
    [[nodiscard]] fixpp::core::steady_time_point steady_now() const noexcept override {
        return inner_.steady_now();
    }
    [[nodiscard]] asio::awaitable<void> sleep_until(fixpp::core::steady_time_point d) override {
        return inner_.sleep_until(d);
    }
    void cancel_sleeps() noexcept override { inner_.cancel_sleeps(); }
    void forget_session(fixpp::session::Session* s) noexcept override { inner_.forget_session(s); }

    bool armed = false;
    mutable int armed_now_calls = 0;
    std::function<void(int)> on_now;

private:
    fixpp::core::mock_clock inner_;
};

class RecordingApplication : public Application {
public:
    std::vector<std::string> to_admin;  // MsgType of each toAdmin call
    std::vector<std::string> to_app;    // MsgType of each toApp call
    std::function<expected_t<void>(std::string_view)> from_admin_hook;
    std::function<expected_t<void>(std::string_view)> from_app_hook;

    expected_t<void> fromAdmin(const MessageView<access_mode::Index>& mv,
                               const SessionId& /*id*/) override {
        return from_admin_hook ? from_admin_hook(mv.msg_type()) : expected_t<void>{};
    }
    expected_t<void> fromApp(const MessageView<access_mode::Index>& mv,
                             const SessionId& /*id*/) override {
        return from_app_hook ? from_app_hook(mv.msg_type()) : expected_t<void>{};
    }
    void toAdmin(const MessageView<access_mode::Index>& mv, const SessionId& /*id*/) override {
        to_admin.emplace_back(mv.msg_type());
    }
    expected_t<void> toApp(const MessageView<access_mode::Index>& mv,
                           const SessionId& /*id*/) override {
        to_app.emplace_back(mv.msg_type());
        return {};
    }

    [[nodiscard]] int to_admin_count(std::string_view t) const {
        int n = 0;
        for (auto const& s : to_admin) n += static_cast<int>(s == t);
        return n;
    }
};

// What one cancellable feed produced.
struct FeedOutcome {
    bool completed = false;  // the future became ready within the window
    bool aborted = false;    // it carried asio's operation_aborted
    bool value = false;      // it carried a value expected_t
};

struct CancelFixture {
    asio::io_context ioc;
    std::shared_ptr<HookedClock> clock = std::make_shared<HookedClock>(ioc.get_executor());
    std::shared_ptr<RecordingApplication> app = std::make_shared<RecordingApplication>();
    fixpp::core::EngineConfig engine;
    asio::cancellation_signal sig;
    bool emitted = false;
    std::vector<std::vector<std::byte>> sent;
    std::function<void(std::span<const std::byte>)> on_send;
    std::string sender = "ISLD";
    std::string target = "TW";

    CancelFixture() {
        engine.clock = clock;
        engine.executor = ioc.get_executor();
        engine.application = app;
    }

    // Emits terminal cancellation on the feed's chain once. Terminal, because each lock the
    // chain takes (FIXPP_DETAIL_CO_AWAIT_LOCK) resets the chain's filter to terminal-only, so
    // a total emission after the first lock would be filtered out.
    void cancel_chain() {
        if (!emitted) {
            emitted = true;
            sig.emit(asio::cancellation_type::terminal);
        }
    }

    SessionConfig cfg() {
        SessionConfig c;
        c.sender_comp_id = sender;
        c.target_comp_id = target;
        c.begin_string = "FIX.4.2";
        c.heartbeat_interval = 0s;
        c.security_profile = fixpp::test_support::make_minimal_security_profile();
        c.dictionary = fixpp::test_support::make_minimal_dictionary();
        c.executor_override = ioc.get_executor();
        c.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        c.transport_send = [this](std::span<const std::byte> f) {
            sent.emplace_back(f.begin(), f.end());
            if (on_send) on_send(f);
        };
        return c;
    }

    // A plain feed: no cancellation slot, as the session suites feed.
    bool feed_plain(Session& s, const std::vector<std::byte>& f) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(f), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "CancelFixture::plain");
            return false;
        }
        return fut.get().has_value();
    }

    // A feed on a chain that accepts terminal cancellation, bound to `sig`. The completion handler
    // is a plain lambda, so co_spawn's cancellation handler emits into the chain synchronously
    // (a handler with an associated executor, such as use_future's, has the emission
    // dispatched instead, which lands after a frame that never suspends).
    FeedOutcome feed_cancellable(Session& s, const std::vector<std::byte>& f) {
        std::promise<FeedOutcome> done;
        auto fut = done.get_future();
        asio::co_spawn(
            ioc,
            [&s, &f]() -> asio::awaitable<expected_t<void>> {
                co_await asio::this_coro::reset_cancellation_state(
                    asio::enable_terminal_cancellation());
                co_return co_await s.on_inbound_frame(f);
            },
            asio::bind_cancellation_slot(
                sig.slot(), [&done](std::exception_ptr ep, expected_t<void> r) {
                    FeedOutcome o;
                    o.completed = true;
                    if (ep) {
                        try {
                            std::rethrow_exception(ep);
                        } catch (const asio::system_error& e) {
                            o.aborted = e.code() == asio::error::operation_aborted;
                        } catch (...) {
                        }
                    } else {
                        o.value = r.has_value();
                    }
                    done.set_value(o);
                }));
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "CancelFixture::cancellable");
            return FeedOutcome{};
        }
        return fut.get();
    }

    // open() (initiator: emits Logon) and the peer's Logon-ack: Active, next inbound 2.
    bool open_to_active(Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "CancelFixture::open");
            return false;
        }
        if (!fut.get().has_value()) return false;
        if (!feed_plain(s, make_frame("A", 1, target, sender, kFresh52,
                                      "98=0\x01"
                                      "108=0\x01"))) {
            return false;
        }
        return s.state() == fsm_state::Active;
    }

    [[nodiscard]] int sent_count(std::string_view msg_type) const {
        int n = 0;
        for (auto const& f : sent) n += static_cast<int>(field_of(f, "35") == msg_type);
        return n;
    }
};

// The six reply blocks whose callback precedes their first original co_await. Each
// assertion set is the same: the callback ran, the frame threw operation_aborted, the reply
// was not sent, and the session stayed Active.
void expect_callback_then_abort(const CancelFixture& f, const FeedOutcome& o, Session& s,
                                int callbacks, std::string_view reply_type) {
    ASSERT_TRUE(f.emitted) << "the cell's cancellation point was never reached";
    ASSERT_TRUE(o.completed);
    EXPECT_TRUE(o.aborted) << "the reply block's first co_await must throw operation_aborted";
    EXPECT_EQ(callbacks, 1) << "the callback before the first co_await must run exactly once";
    EXPECT_EQ(f.sent_count(reply_type), 0) << "the reply must not be transmitted";
    EXPECT_EQ(s.state(), fsm_state::Active);
}

// Active SendingTime(52) guard: Reject, then Logout. The chain is cancelled by the Reject's
// transmit; toAdmin(Logout) runs before the Logout's assign_outbound.
TEST(B35InboundSplitCancel, SendingTimeGuardLogout_ToAdminRunsBeforeTheThrow) {
    CancelFixture f;
    Session s(f.engine, f.cfg());
    ASSERT_TRUE(f.open_to_active(s));
    f.on_send = [&](std::span<const std::byte> fr) {
        if (field_of(fr, "35") == "3") f.cancel_chain();
    };
    auto o = f.feed_cancellable(s, make_frame("0", 2, f.target, f.sender, kStale52));
    ASSERT_EQ(f.sent_count("3"), 1) << "the Reject must be sent before the cancellation";
    expect_callback_then_abort(f, o, s, f.app->to_admin_count("5"), "5");
}

// PossDup Arm D (OrigSendingTime(122) after SendingTime(52)): Reject, then Logout. Same
// cancellation point and assertions as the SendingTime guard cell.
TEST(B35InboundSplitCancel, PossDupArmDLogout_ToAdminRunsBeforeTheThrow) {
    CancelFixture f;
    Session s(f.engine, f.cfg());
    ASSERT_TRUE(f.open_to_active(s));
    f.on_send = [&](std::span<const std::byte> fr) {
        if (field_of(fr, "35") == "3") f.cancel_chain();
    };
    auto o = f.feed_cancellable(s, make_frame("0", 2, f.target, f.sender, kFresh52,
                                              "43=Y\x01"
                                              "122=" +
                                                  std::string(kLater122) + "\x01"));
    ASSERT_EQ(f.sent_count("3"), 1) << "the Reject must be sent before the cancellation";
    expect_callback_then_abort(f, o, s, f.app->to_admin_count("5"), "5");
}

// Too-high MsgSeqNum: ResendRequest. now() runs once for the SendingTime guard and once for
// the ResendRequest's stamp; the second cancels the chain, so toAdmin(ResendRequest) runs
// before its assign_outbound.
TEST(B35InboundSplitCancel, TooHighResendRequest_ToAdminRunsBeforeTheThrow) {
    CancelFixture f;
    Session s(f.engine, f.cfg());
    ASSERT_TRUE(f.open_to_active(s));
    f.clock->on_now = [&](int n) {
        if (n == 2) f.cancel_chain();
    };
    f.clock->armed = true;
    auto o = f.feed_cancellable(s, make_frame("0", 9, f.target, f.sender, kFresh52));
    f.clock->armed = false;
    expect_callback_then_abort(f, o, s, f.app->to_admin_count("2"), "2");
}

// In-sequence Logout: the confirming Logout. Logout is exempt from the SendingTime guard, so
// the first now() is the confirming Logout's stamp; it cancels the chain.
TEST(B35InboundSplitCancel, ConfirmingLogout_ToAdminRunsBeforeTheThrow) {
    CancelFixture f;
    Session s(f.engine, f.cfg());
    ASSERT_TRUE(f.open_to_active(s));
    f.clock->on_now = [&](int n) {
        if (n == 1) f.cancel_chain();
    };
    f.clock->armed = true;
    auto o = f.feed_cancellable(s, make_frame("5", 2, f.target, f.sender, kFresh52));
    f.clock->armed = false;
    expect_callback_then_abort(f, o, s, f.app->to_admin_count("5"), "5");
}

// TestRequest: the Heartbeat reply. fromAdmin(TestRequest) cancels the chain; toAdmin
// (Heartbeat) runs before the reply's assign_outbound.
TEST(B35InboundSplitCancel, TestRequestHeartbeatReply_ToAdminRunsBeforeTheThrow) {
    CancelFixture f;
    Session s(f.engine, f.cfg());
    ASSERT_TRUE(f.open_to_active(s));
    f.app->from_admin_hook = [&](std::string_view t) -> expected_t<void> {
        if (t == "1") f.cancel_chain();
        return {};
    };
    auto o = f.feed_cancellable(s, make_frame("1", 2, f.target, f.sender, kFresh52, "112=TR1\x01"));
    expect_callback_then_abort(f, o, s, f.app->to_admin_count("0"), "0");
}

// fromApp reject: the BusinessMessageReject. fromApp cancels the chain and rejects; toApp
// (BusinessMessageReject) runs before its assign_outbound.
TEST(B35InboundSplitCancel, BusinessMessageReject_ToAppRunsBeforeTheThrow) {
    CancelFixture f;
    Session s(f.engine, f.cfg());
    ASSERT_TRUE(f.open_to_active(s));
    f.app->from_app_hook = [&](std::string_view /*t*/) -> expected_t<void> {
        f.cancel_chain();
        return std::unexpected(fixpp::core::error::app_do_not_send);
    };
    auto o = f.feed_cancellable(s, make_frame("D", 2, f.target, f.sender, kFresh52));
    int bmr_to_app = 0;
    for (auto const& t : f.app->to_app) bmr_to_app += static_cast<int>(t == "j");
    expect_callback_then_abort(f, o, s, bmr_to_app, "j");
}

// LogonSent SendingTime(52) guard, reply-build-failure path. The CompIDs are sized so the
// initiator's Logon fits the Logon buffer and the Logout carrying the guard's Text(58) does
// not fit the Logout buffer. The guard's now() cancels the chain. Today the failed build
// reaches no co_await, so the session disconnects and the frame completes with a value.
TEST(B35InboundSplitCancel, LogonSentLogoutBuildFailure_DisconnectsWithoutThrow) {
    CancelFixture f;
    f.sender = std::string(86, 'S');
    f.target = std::string(86, 'T');
    Session s(f.engine, f.cfg());
    auto fut = asio::co_spawn(f.ioc, s.open(), asio::use_future);
    // On a miss, drain while `s` is alive: it is declared after `f`, so it dies before `f.ioc`.
    const bool opened = fixpp::test_support::run_window_then_ready(f.ioc, fut, 200ms);
    if (!opened) {
        fixpp::test_support::cancel_and_drain_or_report(f.ioc, *f.clock,
                                                        "LogonSentLogoutBuildFailure.open");
    }
    ASSERT_TRUE(opened);
    ASSERT_TRUE(fut.get().has_value()) << "the initiator Logon must fit its buffer";
    ASSERT_EQ(s.state(), fsm_state::LogonSent);
    ASSERT_EQ(f.sent_count("A"), 1);

    f.clock->on_now = [&](int n) {
        if (n == 1) f.cancel_chain();
    };
    f.clock->armed = true;
    auto o = f.feed_cancellable(s, make_frame("A", 1, f.target, f.sender, kStale52,
                                              "98=0\x01"
                                              "108=0\x01"));
    f.clock->armed = false;

    ASSERT_TRUE(f.emitted) << "the cell's cancellation point was never reached";
    ASSERT_TRUE(o.completed);
    EXPECT_FALSE(o.aborted) << "a failed build reaches no co_await, so nothing may throw";
    EXPECT_TRUE(o.value);
    EXPECT_EQ(s.state(), fsm_state::Disconnected);
    EXPECT_EQ(f.app->to_admin_count("5"), 0) << "the Logout must have failed to build";
    EXPECT_EQ(f.sent_count("5"), 0);
}

// Too-high MsgSeqNum, reply-build-failure path. The CompIDs are sized so the initiator's Logon
// fits its buffer and a ResendRequest whose BeginSeqNo has ten digits does not fit its own: a
// Reset-mode SequenceReset moves NextNumIn to a ten-digit value first. now()'s second call (the
// ResendRequest's stamp) cancels the chain. Today the failed build reaches no co_await, so the
// session stays Active and the frame completes with a value.
TEST(B35InboundSplitCancel, TooHighResendRequestBuildFailure_StaysActiveWithoutThrow) {
    CancelFixture f;
    f.sender = std::string(88, 'S');
    f.target = std::string(88, 'T');
    Session s(f.engine, f.cfg());
    ASSERT_TRUE(f.open_to_active(s)) << "the initiator Logon must fit its buffer";
    ASSERT_TRUE(
        f.feed_plain(s, make_frame("4", 2, f.target, f.sender, kFresh52, "36=4000000000\x01")));
    ASSERT_EQ(s.state(), fsm_state::Active);

    f.clock->on_now = [&](int n) {
        if (n == 2) f.cancel_chain();
    };
    f.clock->armed = true;
    auto o = f.feed_cancellable(s, make_frame("0", 4000000005U, f.target, f.sender, kFresh52));
    f.clock->armed = false;

    ASSERT_TRUE(f.emitted) << "the cell's cancellation point was never reached";
    ASSERT_TRUE(o.completed);
    EXPECT_FALSE(o.aborted) << "a failed build reaches no co_await, so nothing may throw";
    EXPECT_TRUE(o.value);
    EXPECT_EQ(s.state(), fsm_state::Active);
    EXPECT_EQ(f.app->to_admin_count("2"), 0) << "the ResendRequest must have failed to build";
    EXPECT_EQ(f.sent_count("2"), 0);
}

}  // namespace
}  // namespace fixpp::session::test
