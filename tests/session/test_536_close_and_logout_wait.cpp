// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_536_close_and_logout_wait.cpp
//
// fixpp#536 (.specify/536-537-538-recovery-liveness.md §3, decisions D-536-1 to D-536-4,
// and §5, the #536 table): what a session's close does to the clock, how close ends the
// session's own liveness loop, and what ends the wait of a graceful close for the peer's
// Logout.
//
// Each cell drives one Session directly, as an initiator, on a mock clock and on the
// callback transport (SessionConfig::transport_send). The callback transport matters to
// the liveness cells: its transmit is a plain call, so after an emit the liveness loop's
// cancellation filter is the one the sequence-number lock and the store left, and no
// transport write replaces it (note §3.5).
//
// Two clocks run in a graceful close. The logout wait's deadline is on the session's
// clock, which is the mock clock here. Session::close also races that wait against an
// asio::steady_timer of its own, armed for the same logout_disconnect_timeout_ms in real
// time, which no mock clock governs (brain/components/graceful-logout.md). A cell that
// must tell the two apart reads real time, and derives its bound from the session's own
// logout_disconnect_timeout_ms.
//
// The suites:
//   SessionNeverSweepsTheClock  D-536-1: no session calls Clock::cancel_sleeps().
//   CloseAfterTheLoopSent       D-536-2, the filter reset before each of the loop's sleeps.
//   CloseDuringTheLoopsEmit     D-536-2, the state test before the grace sleep.
//   LogoutWaitWake              D-536-3: the peer's Logout ends the logout wait through a
//                               wake of the session's own.
//   LogoutWaitLifetime          D-536-3, what the wake leaves behind when the wait ends
//                               another way, and two controls on traces D-536-3 keeps.
//   ExplicitSweep               D-536-4: a caller's own cancel_sleeps() still ends a
//                               liveness loop.

#include <gtest/gtest.h>

#include <array>
#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/session_test_access.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {

namespace {

using fixpp::test_support::kPumpBudget;
using fixpp::test_support::kPumpSlice;

// A Clock that forwards to a shipped clock and counts the calls of cancel_sleeps(). With
// `forward_sweeps` false a call is counted and does nothing more, as a third-party clock
// may implement it (tp_clock in tests/core/test_third_party_clock_conformance.cpp).
class CountingClock final : public fixpp::core::Clock {
public:
    CountingClock(std::shared_ptr<fixpp::core::Clock> inner, bool forward_sweeps)
        : inner_(std::move(inner)), forward_sweeps_(forward_sweeps) {}

    [[nodiscard]] fixpp::core::utc_time_point now() const noexcept override {
        return inner_->now();
    }
    [[nodiscard]] fixpp::core::steady_time_point steady_now() const noexcept override {
        return inner_->steady_now();
    }
    [[nodiscard]] asio::awaitable<void> sleep_until(
        fixpp::core::steady_time_point deadline) override {
        return inner_->sleep_until(deadline);
    }
    void cancel_sleeps() noexcept override {
        ++sweeps_;
        if (forward_sweeps_) inner_->cancel_sleeps();
    }
    void forget_session(Session* session) noexcept override { inner_->forget_session(session); }

    [[nodiscard]] int sweeps() const noexcept { return sweeps_; }

private:
    std::shared_ptr<fixpp::core::Clock> inner_;
    bool forward_sweeps_;
    int sweeps_ = 0;
};

// What a held store() does when a cancellation is recorded on its caller's coroutine
// thread while it holds.
//   yields_to_cancellation    HookedStore's hold (tests/support/hooked_store.hpp): the
//                             hold's next await meets the cancellation, and the call
//                             leaves as operation_aborted after the frame was stored.
//   outlasts_cancellation     the hold clears the thread's throw-if-cancelled flag for
//                             its own awaits and restores it before it returns, so the
//                             call returns the inner store's result with the
//                             cancellation still recorded (note §10, D-OUT-6, "With
//                             D-536-3").
enum class hold_kind : std::uint8_t { yields_to_cancellation, outlasts_cancellation };

// Bound on a hold. A cell releases its hold; this only ends one a cell's pumps missed,
// and a cell that reads `timed_out` then fails. Two bounded-pump budgets, so a cell takes
// no more than one bounded pump while a hold is in flight.
constexpr auto kHoldBound = 2 * kPumpBudget;

// One held outbound store(), shared by the cell and its HoldingStore.
struct StoreHold {
    // Set by the cell: the MsgType(35) of the outbound frame whose store() is held. The
    // first such store() takes the hold and clears this.
    std::string msg_type;
    hold_kind kind = hold_kind::yields_to_cancellation;

    bool holding = false;    // the inner store() has returned and the call is held
    bool released = false;   // set by the cell
    bool timed_out = false;  // the hold reached kHoldBound
    // A cancellation was recorded on the caller's coroutine thread while the call was held.
    bool cancelled_while_holding = false;
    std::chrono::steady_clock::time_point cancelled_at;
};

[[nodiscard]] std::string_view msg_type_of(std::span<const std::byte> frame) {
    const std::string_view text{reinterpret_cast<const char*>(frame.data()), frame.size()};
    constexpr std::string_view tag{
        "\x01"
        "35="};
    const auto at = text.find(tag);
    if (at == std::string_view::npos) return {};
    const auto begin = at + tag.size();
    const auto end = text.find('\x01', begin);
    return text.substr(begin, end == std::string_view::npos ? end : end - begin);
}

// An unbounded MemoryStore behind a wrapper that holds one outbound store(): the inner
// store completes the call, and the wrapper then yields the session's executor until the
// cell releases it. HookedStore's on_outbound_store hold is the precedent; this one names
// the held call by its MsgType and has the second kind of hold (hold_kind).
class HoldingStore final : public MessageStore {
public:
    explicit HoldingStore(std::shared_ptr<StoreHold> hold)
        : inner_(MemoryStore::Config{.policy = capacity_policy::unbounded}),
          hold_(std::move(hold)) {}

    asio::awaitable<fixpp::core::expected_t<void>> store(seqnum_t seq,
                                                         std::span<const std::byte> frame,
                                                         direction_t dir) noexcept override {
        if (dir == direction_t::outbound && !hold_->msg_type.empty() &&
            msg_type_of(frame) == hold_->msg_type) {
            hold_->msg_type.clear();
            return held_store(seq, frame, dir);
        }
        return inner_.store(seq, frame, dir);
    }
    asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t begin, seqnum_t end, direction_t dir,
        retrieve_visitor& visitor) noexcept override {
        return inner_.retrieve(begin, end, dir, visitor);
    }
    asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool increment) noexcept override {
        return inner_.next_seqnum(dir, increment);
    }
    asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        return inner_.reset();
    }
    asio::awaitable<fixpp::core::expected_t<void>> reset_to(seqnum_t next_in,
                                                            seqnum_t next_out) noexcept override {
        return inner_.reset_to(next_in, next_out);
    }

private:
    asio::awaitable<fixpp::core::expected_t<void>> held_store(seqnum_t seq,
                                                              std::span<const std::byte> frame,
                                                              direction_t dir) {
        auto r = co_await inner_.store(seq, frame, dir);
        const auto ex = co_await asio::this_coro::executor;
        const bool outlast = hold_->kind == hold_kind::outlasts_cancellation;
        const bool throws = co_await asio::this_coro::throw_if_cancelled();
        if (outlast) co_await asio::this_coro::throw_if_cancelled(false);
        // `holding` is cleared on every exit, the throw of a yielding hold included.
        struct holding_flag {
            bool& flag;
            ~holding_flag() { flag = false; }
        } const holding{hold_->holding};
        hold_->holding = true;
        const auto deadline = std::chrono::steady_clock::now() + kHoldBound;
        while (!hold_->released) {
            if (std::chrono::steady_clock::now() >= deadline) {
                hold_->timed_out = true;
                break;
            }
            if (!hold_->cancelled_while_holding &&
                (co_await asio::this_coro::cancellation_state).cancelled() !=
                    asio::cancellation_type::none) {
                hold_->cancelled_while_holding = true;
                hold_->cancelled_at = std::chrono::steady_clock::now();
            }
            co_await asio::post(ex, asio::use_awaitable);
        }
        if (outlast) co_await asio::this_coro::throw_if_cancelled(throws);
        co_return r;
    }

    MemoryStore inner_;
    std::shared_ptr<StoreHold> hold_;
};

// Volatile, as MemoryStoreFactory is: the session reads no counter from the store.
class HoldingStoreFactory final : public MessageStoreFactory {
public:
    std::shared_ptr<StoreHold> hold = std::make_shared<StoreHold>();

    [[nodiscard]] bool yields_persistent_store() const noexcept override { return false; }
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/, std::pmr::memory_resource* /*mr*/,
        std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override {
        return fixpp::core::expected_t<std::unique_ptr<MessageStore>>{
            std::make_unique<HoldingStore>(hold)};
    }
};

enum class clock_kind : std::uint8_t {
    mock,                // the session's clock is the mock clock
    counting,            // a CountingClock over it, which forwards cancel_sleeps()
    sweep_does_nothing,  // a CountingClock over it, whose cancel_sleeps() does nothing
};

struct Options {
    clock_kind clock = clock_kind::mock;
    // close(graceful)'s logout wait, on the session's clock, and its grace timer, in real
    // time. Zero keeps SessionConfig's default.
    std::uint32_t logout_disconnect_timeout_ms = 0;
    bool holding_store = false;
};

constexpr auto kHeartBtInt = std::chrono::seconds{30};

// One initiator Session, TW to ISLD, with HeartBtInt = kHeartBtInt, its clock and its
// transport callback.
class Scene {
public:
    // First, so destroyed last: the session's executor is on it.
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> mock;
    std::shared_ptr<CountingClock> counting;  // null for clock_kind::mock
    fixpp::core::EngineConfig engine;
    SessionConfig cfg;
    std::shared_ptr<StoreHold> hold;  // null without Options::holding_store
    std::vector<std::string> sent;    // each frame the session gave its transport callback
    std::unique_ptr<Session> session;

    explicit Scene(Options const& o = {}) {
        using namespace std::chrono;
        mock = std::make_shared<fixpp::core::mock_clock>(
            system_clock::time_point{} + seconds{1704067200}, fixpp::core::steady_time_point{},
            ioc.get_executor());
        if (o.clock == clock_kind::mock) {
            engine.clock = mock;
        } else {
            counting = std::make_shared<CountingClock>(mock, o.clock == clock_kind::counting);
            engine.clock = counting;
        }
        engine.executor = ioc.get_executor();

        cfg.sender_comp_id = "TW";
        cfg.target_comp_id = "ISLD";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = kHeartBtInt;
        if (o.logout_disconnect_timeout_ms != 0) {
            cfg.logout_disconnect_timeout_ms = o.logout_disconnect_timeout_ms;
        }
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.transport_send = [this](std::span<const std::byte> f) {
            sent.emplace_back(reinterpret_cast<const char*>(f.data()), f.size());
        };
        if (o.holding_store) {
            auto factory = std::make_shared<HoldingStoreFactory>();
            hold = factory->hold;
            cfg.store_factory = std::move(factory);
        }
        session = std::make_unique<Session>(engine, cfg);
    }

    Scene(Scene const&) = delete;
    Scene& operator=(Scene const&) = delete;

    // A destructor's body runs while every member is alive. A hold still in flight is
    // released, the session is closed if a cell left it open, and whatever is still
    // suspended on the io_context is resumed here, over live storage. The drain sweeps
    // the mock clock itself, so it also releases a sleep under a CountingClock whose
    // cancel_sleeps() does nothing. cancel_and_drain_or_report carries its own catch.
    ~Scene() {
        try {
            if (hold) hold->released = true;
            asio::co_spawn(ioc, session->close(close_mode::terminal), asio::detached);
        } catch (...) {  // NOLINT(bugprone-empty-catch) — the drain below still runs.
        }
        fixpp::test_support::cancel_and_drain_or_report(ioc, *mock, "536 Scene");
    }

    template <class Fut>
    [[nodiscard]] static bool ready(Fut const& fut) {
        return fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
    }
    template <class Pred>
    [[nodiscard]] bool run_until(Pred pred,
                                 std::chrono::steady_clock::duration budget = kPumpBudget) {
        return fixpp::test_support::pump_until(ioc, std::move(pred), budget, kPumpSlice, nullptr);
    }
    // Runs the handlers that become ready during `window`, in real time.
    void settle(std::chrono::steady_clock::duration window = 20ms) {
        (void)run_until([] { return false; }, window);
    }

    [[nodiscard]] std::chrono::milliseconds logout_timeout() const {
        return std::chrono::milliseconds{cfg.logout_disconnect_timeout_ms};
    }

    // SendingTime(52) for a frame stamped now on the session's clock.
    [[nodiscard]] std::string sending_time() const {
        std::array<char, 32> buf{};
        auto r = fixpp::core::utc_time_to_fix_string(
            mock->now(), fixpp::core::fix_time_precision::millis, std::span<char>{buf});
        return r ? std::string{r->data(), r->size()} : std::string{};
    }

    // A frame from the peer.
    [[nodiscard]] std::vector<std::byte> peer_frame(std::string_view msg_type, std::uint32_t seq,
                                                    std::string_view extra = {}) const {
        std::string body;
        body += "35=" + std::string(msg_type) + "\x01";
        body += "34=" + std::to_string(seq) + "\x01";
        body += "49=ISLD\x01";
        body += "52=" + sending_time() + "\x01";
        body += "56=TW\x01";
        body += std::string(extra);
        std::string full =
            "8=FIX.4.2\x01"
            "9=" +
            std::to_string(body.size()) + "\x01" + body;
        unsigned int cs = 0;
        for (unsigned char c : full) cs += c;
        std::array<char, 8> csbuf{};
        std::snprintf(csbuf.data(), csbuf.size(), "%03u", cs & 0xFFU);
        full += "10=" + std::string(csbuf.data()) + "\x01";
        std::vector<std::byte> out;
        out.reserve(full.size());
        for (char c : full) out.push_back(static_cast<std::byte>(c));
        return out;
    }

    // Delivers one frame. The bytes are kept until the Scene dies, so a delivery that
    // a pump missed is still resumed over live storage by the destructor's drain.
    fixpp::core::expected_t<void> feed(std::vector<std::byte> frame) {
        fed_.push_back(std::move(frame));
        auto fut = asio::co_spawn(ioc, session->on_inbound_frame(fed_.back()), asio::use_future);
        if (!run_until([&] { return ready(fut); })) {
            ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << "536 Scene::feed";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    // open(), then the peer's Logon: the session is Active and its liveness loop is
    // parked in its first sleep.
    [[nodiscard]] bool open_to_active() {
        auto fut = asio::co_spawn(ioc, session->open(), asio::use_future);
        if (!run_until([&] { return ready(fut); }) || !fut.get().has_value()) return false;
        const std::string extra =
            "98=0\x01"
            "108=" +
            std::to_string(kHeartBtInt.count()) + "\x01";
        (void)feed(peer_frame("A", 1, extra));
        return session->state() == fsm_state::Active;
    }

    [[nodiscard]] std::future<fixpp::core::expected_t<void>> spawn_close(close_mode mode) {
        return asio::co_spawn(ioc, session->close(mode), asio::use_future);
    }

    // How many frames of this MsgType the session has given its transport callback.
    [[nodiscard]] int count(std::string_view msg_type) const {
        int n = 0;
        for (auto const& f : sent) {
            const auto* const data = reinterpret_cast<const std::byte*>(f.data());
            if (msg_type_of(std::span<const std::byte>{data, f.size()}) == msg_type) ++n;
        }
        return n;
    }

    // The TestReqID(112) of the first TestRequest sent, or empty.
    [[nodiscard]] std::string first_test_req_id() const {
        for (auto const& f : sent) {
            const auto* const data = reinterpret_cast<const std::byte*>(f.data());
            if (msg_type_of(std::span<const std::byte>{data, f.size()}) != "1") continue;
            constexpr std::string_view tag{
                "\x01"
                "112="};
            const auto at = f.find(tag);
            if (at == std::string::npos) return {};
            const auto begin = at + tag.size();
            return f.substr(begin, f.find('\x01', begin) - begin);
        }
        return {};
    }

private:
    std::deque<std::vector<std::byte>> fed_;
};

[[nodiscard]] long long millis(std::chrono::steady_clock::duration d) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

}  // namespace

// ── D-536-1: no session issues a cancel_sleeps() call ───────────────────────────────
//
// The count is the CountingClock's, so it holds the calls the session made through its
// Clock and none the mock clock makes on itself. Each cell reads it before the Scene's
// teardown.

TEST(SessionNeverSweepsTheClock, Open) {
    Scene s{Options{.clock = clock_kind::counting}};
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    EXPECT_EQ(s.counting->sweeps(), 0) << "open() and the Logon exchange swept the clock";
}

TEST(SessionNeverSweepsTheClock, Traffic) {
    Scene s{Options{.clock = clock_kind::counting}};
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    s.sent.clear();

    // The peer's TestRequest, answered by a Heartbeat.
    (void)s.feed(s.peer_frame("1", 2, "112=PEER-1\x01"));
    ASSERT_EQ(s.count("0"), 1) << "setup: the peer's TestRequest drew no Heartbeat";
    // One pass of the liveness loop: a Heartbeat and a TestRequest of its own.
    s.mock->advance(kHeartBtInt + 1s);
    ASSERT_TRUE(s.run_until([&] { return s.count("1") == 1; }))
        << "setup: the liveness loop sent no TestRequest";
    ASSERT_EQ(s.count("0"), 2) << "setup: the liveness loop sent no Heartbeat";
    // The peer's answer to it.
    const std::string id = s.first_test_req_id();
    ASSERT_FALSE(id.empty()) << "setup: the TestRequest carries no TestReqID(112)";
    (void)s.feed(s.peer_frame("0", 3, "112=" + id + "\x01"));
    ASSERT_EQ(s.session->state(), fsm_state::Active) << "setup: the session left Active";

    EXPECT_EQ(s.counting->sweeps(), 0) << "inbound and liveness traffic swept the clock";
}

TEST(SessionNeverSweepsTheClock, GracefulCloseThePeerConfirms) {
    Scene s{Options{.clock = clock_kind::counting}};
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";

    auto closed = s.spawn_close(close_mode::graceful);
    ASSERT_TRUE(s.run_until([&] { return s.session->state() == fsm_state::LogoutSent; }))
        << "setup: the session did not reach LogoutSent";
    // The peer's Logout is delivered in LogoutSent, so it meets the LogoutSent arm.
    (void)s.feed(s.peer_frame("5", 2));
    ASSERT_EQ(s.session->state(), fsm_state::Disconnected)
        << "setup: the LogoutSent arm did not take the peer's Logout as the confirmation";
    ASSERT_TRUE(s.run_until([&] { return Scene::ready(closed); }))
        << "setup: close(graceful) did not complete";

    EXPECT_EQ(s.counting->sweeps(), 0)
        << "a graceful close that the peer confirmed swept the clock, " << s.counting->sweeps()
        << " time(s)";
}

TEST(SessionNeverSweepsTheClock, TerminalClose) {
    Scene s{Options{.clock = clock_kind::counting}};
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";

    auto closed = s.spawn_close(close_mode::terminal);
    ASSERT_TRUE(s.run_until([&] { return Scene::ready(closed); }))
        << "setup: close(terminal) did not complete";

    EXPECT_EQ(s.counting->sweeps(), 0)
        << "a terminal close swept the clock, " << s.counting->sweeps() << " time(s)";
}

// ── D-536-2, the reset: close ends its own liveness loop after the loop has sent ────
//
// A liveness emit takes the sequence-number lock, and the lock sequence leaves the
// loop's coroutine thread with a terminal cancellation filter. close emits total. Each
// cell parks the loop in one of its two sleeps after an emit, and close must complete
// with the mock clock not advanced: nothing but close's own cancellation can end the
// sleep.

// The loop sent a Heartbeat and is parked in its main sleep.
TEST(CloseAfterTheLoopSent, AHeartbeat_CloseCompletesWithoutTheClock) {
    Scene s;
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    s.sent.clear();

    // Inbound traffic at 20 s keeps the inbound side fresh, so at HeartBtInt the loop
    // owes a Heartbeat and no TestRequest.
    s.mock->advance(20s);
    (void)s.feed(s.peer_frame("0", 2));
    s.mock->advance(11s);
    ASSERT_TRUE(s.run_until([&] { return s.count("0") == 1; }))
        << "setup: the liveness loop sent no Heartbeat";
    s.settle();
    ASSERT_EQ(s.count("1"), 0)
        << "setup: the loop also sent a TestRequest, so it is not parked in its main sleep";

    auto closed = s.spawn_close(close_mode::terminal);
    EXPECT_TRUE(s.run_until([&] { return Scene::ready(closed); }))
        << "close(terminal) did not complete, with the clock not advanced, for a session "
           "whose liveness loop had sent a Heartbeat: close did not end the loop's main sleep";
}

// The loop sent a Heartbeat and a TestRequest and is parked in its grace sleep.
TEST(CloseAfterTheLoopSent, ATestRequest_CloseCompletesWithoutTheClock) {
    Scene s;
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    s.sent.clear();

    s.mock->advance(kHeartBtInt + 1s);
    ASSERT_TRUE(s.run_until([&] { return s.count("1") == 1; }))
        << "setup: the liveness loop sent no TestRequest";

    auto closed = s.spawn_close(close_mode::terminal);
    EXPECT_TRUE(s.run_until([&] { return Scene::ready(closed); }))
        << "close(terminal) did not complete, with the clock not advanced, for a session "
           "whose liveness loop had sent a TestRequest: close did not end the loop's grace "
           "sleep";
}

// ── D-536-2, the state test: close runs while the loop's emit is suspended ──────────
//
// The loop's TestRequest is held in the store, once the store has the frame. close(terminal)
// runs to its wait for the loop; the hold is then released. On the callback transport
// the released emit succeeds, so the loop goes on to its grace sleep with close's
// cancellation already emitted. The grace deadline is past the clock (the clock stands
// one second after HeartBtInt, and the grace sleep ends at twice HeartBtInt), so the loop
// can only leave by itself.
TEST(CloseDuringTheLoopsEmit, TestRequestHeldInTheStore_CloseCompletesWithoutTheClock) {
    Scene s{Options{.holding_store = true}};
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    s.sent.clear();

    s.hold->msg_type = "1";
    s.mock->advance(kHeartBtInt + 1s);
    ASSERT_TRUE(s.run_until([&] { return s.hold->holding; }))
        << "setup: no TestRequest store() was held";
    ASSERT_EQ(s.count("1"), 0) << "setup: the TestRequest was transmitted before its store()";

    auto closed = s.spawn_close(close_mode::terminal);
    ASSERT_TRUE(s.run_until([&] {
        return session_test_access::is_closing(*s.session) &&
               s.session->state() == fsm_state::Disconnected;
    })) << "setup: close(terminal) did not run to its wait for the liveness loop";
    s.settle();
    ASSERT_TRUE(s.hold->holding) << "setup: the hold ended before it was released";
    ASSERT_FALSE(Scene::ready(closed)) << "setup: close completed while the emit was held";

    s.hold->released = true;
    const bool completed = s.run_until([&] { return Scene::ready(closed); });

    EXPECT_FALSE(s.hold->timed_out) << "the hold reached its bound";
    // The note reads that close's total emit does not pass the loop's filter during the
    // hold. A cancellation recorded there would end this kind of hold by an exception.
    EXPECT_FALSE(s.hold->cancelled_while_holding)
        << "a cancellation was recorded on the liveness loop's thread during the hold";
    ASSERT_EQ(s.count("1"), 1)
        << "the released TestRequest was not transmitted, so the loop did not reach its "
           "grace sleep and the cell says nothing about the state test before it";
    EXPECT_TRUE(completed)
        << "close(terminal) did not complete, with the clock not advanced, after the "
           "liveness loop's held TestRequest emit was released: the loop went into its "
           "grace sleep although the session was no longer Active";
}

// ── D-536-3: the peer's Logout ends the logout wait through the session's own wake ──
//
// The session's clock ignores cancel_sleeps(), as a third-party clock may. What then
// remains to end the wait early is the session's own wake. Without it close returns
// when its real grace timer fires, logout_disconnect_timeout_ms after it began: the
// bound is half of that timeout, counted from the start of close.
TEST(LogoutWaitWake, ClockWhoseSweepDoesNothing_GracefulCloseReturnsAtThePeersLogout) {
    Scene s{Options{.clock = clock_kind::sweep_does_nothing}};
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    const auto timeout = s.logout_timeout();

    const auto began = std::chrono::steady_clock::now();
    auto closed = s.spawn_close(close_mode::graceful);
    ASSERT_TRUE(s.run_until([&] { return s.session->state() == fsm_state::LogoutSent; }))
        << "setup: the session did not reach LogoutSent";
    s.settle();
    (void)s.feed(s.peer_frame("5", 2));
    ASSERT_EQ(s.session->state(), fsm_state::Disconnected)
        << "setup: the LogoutSent arm did not take the peer's Logout as the confirmation";
    ASSERT_TRUE(s.run_until([&] { return Scene::ready(closed); }))
        << "close(graceful) did not complete";
    const auto took = std::chrono::steady_clock::now() - began;

    EXPECT_LT(took, timeout / 2) << "close(graceful) returned " << millis(took)
                                 << " ms after it began, with a logout "
                                 << "timeout of " << timeout.count()
                                 << " ms and the peer's Logout delivered at once: "
                                 << "the peer's Logout did not end the logout wait";
}

// ── D-536-3, lifetime: the wait ends another way ────────────────────────────────────

// The peer never answers and the mock clock never advances, so only close's real grace
// timer can end the wait: close cannot return before it.
TEST(LogoutWaitLifetime, GraceTimerWinsWhileTheWaitIsPending_CloseCompletes) {
    Scene s{Options{.logout_disconnect_timeout_ms = 200}};
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    s.sent.clear();
    const auto timeout = s.logout_timeout();

    const auto began = std::chrono::steady_clock::now();
    auto closed = s.spawn_close(close_mode::graceful);
    ASSERT_TRUE(s.run_until([&] { return s.session->state() == fsm_state::LogoutSent; }))
        << "setup: the session did not reach LogoutSent";
    ASSERT_TRUE(s.run_until([&] { return Scene::ready(closed); }))
        << "close(graceful) did not complete when its grace timer expired";
    const auto took = std::chrono::steady_clock::now() - began;

    EXPECT_GE(took, timeout) << "close(graceful) returned " << millis(took)
                             << " ms after it began, before its grace timer (" << timeout.count()
                             << " ms): something else ended the wait";
    EXPECT_EQ(s.session->state(), fsm_state::Disconnected);
    EXPECT_EQ(s.count("5"), 1) << "the session's Logout was not written exactly once";
}

// A terminal close escalates the graceful one during its wait. Both return, well inside
// the grace timer's period, so the escalation and not the timer ended the wait.
TEST(LogoutWaitLifetime, TerminalCloseEscalatesDuringTheWait_BothClosesComplete) {
    Scene s;
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    s.sent.clear();
    const auto timeout = s.logout_timeout();

    const auto began = std::chrono::steady_clock::now();
    auto graceful = s.spawn_close(close_mode::graceful);
    ASSERT_TRUE(s.run_until([&] { return s.session->state() == fsm_state::LogoutSent; }))
        << "setup: the session did not reach LogoutSent";
    s.settle();
    ASSERT_FALSE(Scene::ready(graceful)) << "setup: the graceful close did not wait";

    auto terminal = s.spawn_close(close_mode::terminal);
    ASSERT_TRUE(s.run_until([&] { return Scene::ready(graceful) && Scene::ready(terminal); }))
        << "the two closes did not both complete after the escalation";
    const auto took = std::chrono::steady_clock::now() - began;

    EXPECT_LT(took, timeout / 2) << "the closes returned " << millis(took)
                                 << " ms after the graceful one began; its "
                                 << "grace timer is " << timeout.count()
                                 << " ms, so the timer may have ended the wait";
    EXPECT_EQ(s.session->state(), fsm_state::Disconnected);
    EXPECT_EQ(s.count("5"), 1) << "the session's Logout was not written exactly once";
}

// Control. The peer's Logout arrives while the session's own Logout emit is still held
// in the store. It meets the Active arm, which answers it and records Disconnected.
// Phase 1 then resumes, records LogoutSent over that, and waits for its deadline: no
// confirmation is recorded on this trace. D-536-3 leaves the trace as it is. The cell
// advances the mock clock to the deadline and reads real time, so that the deadline and
// not close's real grace timer is what ended the wait.
TEST(LogoutWaitLifetime, Control_PeersLogoutWhileTheLogoutEmitIsHeld_WaitRunsToItsDeadline) {
    Scene s{Options{.holding_store = true}};
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    s.sent.clear();
    const auto timeout = s.logout_timeout();

    s.hold->msg_type = "5";
    const auto began = std::chrono::steady_clock::now();
    auto closed = s.spawn_close(close_mode::graceful);
    ASSERT_TRUE(s.run_until([&] { return s.hold->holding; }))
        << "setup: the store() of the session's Logout was not held";
    ASSERT_EQ(s.count("5"), 0) << "setup: the Logout was transmitted before its store()";

    (void)s.feed(s.peer_frame("5", 2));
    ASSERT_EQ(s.session->state(), fsm_state::Disconnected)
        << "setup: the Active arm did not record Disconnected for the peer's Logout";
    ASSERT_EQ(s.count("5"), 1) << "setup: the Active arm did not answer the peer's Logout";
    ASSERT_TRUE(s.hold->holding) << "setup: the hold ended before it was released";
    ASSERT_FALSE(Scene::ready(closed)) << "close completed before the store was released";

    s.hold->released = true;
    ASSERT_TRUE(s.run_until([&] { return s.session->state() == fsm_state::LogoutSent; }))
        << "phase 1 did not record LogoutSent after the store was released";
    EXPECT_EQ(s.count("5"), 2) << "the two Logouts were not both written";
    EXPECT_FALSE(s.hold->timed_out) << "the hold reached its bound";

    // No confirmation was recorded, so the wait stands until its deadline.
    s.settle(timeout / 8);
    ASSERT_FALSE(Scene::ready(closed))
        << "close completed before the logout deadline, with the mock clock not advanced";

    s.mock->advance(timeout);
    ASSERT_TRUE(s.run_until([&] { return Scene::ready(closed); }))
        << "close did not complete at the logout deadline";
    const auto took = std::chrono::steady_clock::now() - began;
    ASSERT_LT(took, timeout) << "inconclusive: " << millis(took)
                             << " ms of real time passed, so close's real grace timer ("
                             << timeout.count() << " ms) may have ended the wait";
    EXPECT_EQ(s.session->state(), fsm_state::Disconnected);
}

// Controls for the `try` around the wait. Phase 1 is cancelled while its Logout emit is
// held, by a hold that a cancellation does not end: the store then returns success with
// the cancellation recorded, the callback transport's transmit is a plain call, and the
// phase goes on to its wait, whose co_await throws at once. close must then complete with
// the mock clock not advanced. A wait that was entered would stand until a deadline the
// mock clock never reaches, and the cell would fail at its last pump.

// The cancellation comes from close's grace timer.
TEST(LogoutWaitLifetime, Control_GraceTimerWinsWhileTheLogoutEmitIsHeld_CloseCompletes) {
    Scene s{Options{.logout_disconnect_timeout_ms = 200, .holding_store = true}};
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    s.sent.clear();

    s.hold->msg_type = "5";
    s.hold->kind = hold_kind::outlasts_cancellation;
    auto closed = s.spawn_close(close_mode::graceful);
    ASSERT_TRUE(s.run_until([&] { return s.hold->holding; }))
        << "setup: the store() of the session's Logout was not held";
    ASSERT_TRUE(s.run_until([&] { return s.hold->cancelled_while_holding; }))
        << "setup: the grace timer's expiry did not cancel phase 1 while its emit was held";
    ASSERT_TRUE(s.hold->holding) << "setup: the cancellation ended the hold";
    ASSERT_EQ(s.count("5"), 0) << "setup: the Logout was transmitted during the hold";
    ASSERT_FALSE(Scene::ready(closed)) << "setup: close completed while the emit was held";

    s.hold->released = true;
    const bool completed = s.run_until([&] { return Scene::ready(closed); });

    EXPECT_FALSE(s.hold->timed_out) << "the hold reached its bound";
    ASSERT_EQ(s.count("5"), 1)
        << "the Logout was not transmitted after the release: the emit did not succeed with "
           "the cancellation recorded, so phase 1 did not reach its wait";
    EXPECT_TRUE(completed) << "close(graceful) did not complete with the clock not advanced: "
                              "phase 1 entered its wait although it was already cancelled";
    EXPECT_EQ(s.session->state(), fsm_state::Disconnected);
}

// The cancellation comes from a terminal close that escalates.
TEST(LogoutWaitLifetime, Control_TerminalCloseEscalatesWhileTheLogoutEmitIsHeld_ClosesComplete) {
    Scene s{Options{.holding_store = true}};
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    s.sent.clear();
    const auto timeout = s.logout_timeout();

    s.hold->msg_type = "5";
    s.hold->kind = hold_kind::outlasts_cancellation;
    const auto began = std::chrono::steady_clock::now();
    auto graceful = s.spawn_close(close_mode::graceful);
    ASSERT_TRUE(s.run_until([&] { return s.hold->holding; }))
        << "setup: the store() of the session's Logout was not held";

    auto terminal = s.spawn_close(close_mode::terminal);
    ASSERT_TRUE(s.run_until([&] { return s.hold->cancelled_while_holding; }))
        << "setup: the escalation did not cancel phase 1 while its emit was held";
    ASSERT_LT(s.hold->cancelled_at - began, timeout)
        << "setup: phase 1 was cancelled " << millis(s.hold->cancelled_at - began)
        << " ms after the graceful close began, so its grace timer (" << timeout.count()
        << " ms) and not the escalation may have cancelled it";
    ASSERT_TRUE(s.hold->holding) << "setup: the cancellation ended the hold";
    ASSERT_EQ(s.count("5"), 0) << "setup: the Logout was transmitted during the hold";

    s.hold->released = true;
    const bool completed =
        s.run_until([&] { return Scene::ready(graceful) && Scene::ready(terminal); });

    EXPECT_FALSE(s.hold->timed_out) << "the hold reached its bound";
    ASSERT_EQ(s.count("5"), 1)
        << "the Logout was not transmitted after the release: the emit did not succeed with "
           "the cancellation recorded, so phase 1 did not reach its wait";
    EXPECT_TRUE(completed) << "the closes did not both complete with the clock not advanced: "
                              "phase 1 entered its wait although it was already cancelled";
    EXPECT_EQ(s.session->state(), fsm_state::Disconnected);
}

// ── D-536-4: a caller's own cancel_sleeps() still ends a liveness loop ──────────────
//
// Control. Two sessions, each on a mock clock of its own; one clock is swept. Both
// clocks then pass HeartBtInt twice. The session whose clock was not swept shows what a
// running loop does in the same windows: a Heartbeat and a TestRequest at the first, and
// Disconnected for the unanswered TestRequest at the second. The swept session sends
// nothing and stays Active.
TEST(ExplicitSweep, Control_EndsAnUnclosedSessionsLivenessLoop) {
    Scene swept;
    Scene other;
    ASSERT_TRUE(swept.open_to_active()) << "setup: the swept session did not reach Active";
    ASSERT_TRUE(other.open_to_active()) << "setup: the other session did not reach Active";
    swept.sent.clear();
    other.sent.clear();

    swept.mock->cancel_sleeps();
    swept.settle();

    for (Scene* s : {&swept, &other}) {
        s->mock->advance(kHeartBtInt + 1s);
        s->settle();
    }
    ASSERT_EQ(other.count("0"), 1) << "control: a running loop sent no Heartbeat in the window";
    ASSERT_EQ(other.count("1"), 1) << "control: a running loop sent no TestRequest in the window";
    for (Scene* s : {&swept, &other}) {
        s->mock->advance(kHeartBtInt);
        s->settle();
    }
    ASSERT_EQ(other.session->state(), fsm_state::Disconnected)
        << "control: a running loop did not end the session for its unanswered TestRequest";

    EXPECT_EQ(swept.count("0"), 0) << "the swept session sent a Heartbeat";
    EXPECT_EQ(swept.count("1"), 0) << "the swept session sent a TestRequest";
    EXPECT_EQ(swept.session->state(), fsm_state::Active);
}

}  // namespace fixpp::session::test
