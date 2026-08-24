// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_quiesce_on_exit_residual.cpp
//
// #289(c) — checked-in RED/GREEN witness for the residual-work diagnostic
// branch of `fixpp::test_support::quiesce_on_exit`
// (tests/support/pump_until_ready.hpp). That branch — an `ADD_FAILURE()`
// when the io_context has not run out of work by the end of the quiesce
// window — has shipped since #284/#289(a) with no automated coverage: every
// existing caller's happy path never exercises it.
//
// Scope (restated from the #289 B2 design review, c-Q4): this witness
// proves ONLY the diagnostic branch's polarity — outstanding work at
// destruction time fires ADD_FAILURE, no outstanding work stays silent. It
// does NOT prove that every suspended coroutine is detected, that
// `stopped()==true` proves no dangling coroutine exists, that
// `quiesce_on_exit` successfully cancels all work, or that a reported
// residual was necessarily caused by a session frame.
// `mock_clock::sleep_until()` registers a new waiter unconditionally
// whenever the deadline is still in the future
// (src/core/test/mock_clock.cpp:119), while `cancel_sleeps()` is a
// one-shot drain of only the waiters present at the moment it runs
// (src/core/test/mock_clock.cpp:166) — a coroutine whose first run happens
// during the guard's own `run_for` drain (the session liveness loop's
// `sleep_until`, src/session/session.cpp:4816, is the concrete production
// case) can arm a sleep nothing will ever fire. This test does not exercise
// that gap; it is a documented residual of the guard, not something proven
// absent here.
//
// Anchors: tests/support/pump_until_ready.hpp (quiesce_on_exit); #289 B2
// design review c-Q1..c-Q4.

#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include <array>
#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/transport/transport.hpp>
#include <memory>
#include <span>

#include "support/pump_until_ready.hpp"

using namespace std::chrono_literals;
using fixpp::test_support::quiesce_on_exit;

namespace {

std::shared_ptr<fixpp::core::mock_clock> make_mock_clock(asio::io_context& ioc) {
    using namespace std::chrono;
    auto utc = system_clock::time_point{} + seconds{1704067200};
    auto stp = fixpp::core::steady_time_point{} + seconds{0};
    return std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
}

// Minimal Transport double for ClosesTransportBeforeDraining below: async_write
// blocks on a steady_timer until close() cancels it, mirroring the "blocked
// write" shape every live-transport hang in this suite takes.
class BlockedWriteTransport final : public fixpp::transport::Transport {
public:
    explicit BlockedWriteTransport(asio::any_io_executor exec) : timer_{std::move(exec)} {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::ConnectInfo>>
    async_connect(fixpp::transport::Endpoint const&) override {
        co_return fixpp::transport::ConnectInfo{};
    }
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_read_some(
        std::span<std::byte>) override {
        co_return std::unexpected{fixpp::core::error::transport_read_eof};
    }
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_write(
        std::span<const std::byte> buf) override {
        timer_.expires_after(std::chrono::seconds{30});
        asio::error_code ec;
        co_await timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        if (closed_) {
            co_return std::unexpected{fixpp::core::error::transport_already_closed};
        }
        co_return buf.size();
    }
    [[nodiscard]] fixpp::core::expected_t<void> cancel() noexcept override { return {}; }
    [[nodiscard]] fixpp::core::expected_t<void> close() noexcept override {
        closed_ = true;
        timer_.cancel();
        return {};
    }

private:
    asio::steady_timer timer_;
    bool closed_{false};
};

}  // namespace

// Branch fires: an explicit outstanding-work guard keeps `ioc.stopped()`
// false for the whole (short, 1ms) quiesce budget, so quiesce_on_exit
// reports the residual via ADD_FAILURE. The work guard, and quiesce_on_exit
// itself, must be scoped INSIDE the macro's captured statement — declaring
// quiesce_on_exit before EXPECT_NONFATAL_FAILURE would run its destructor
// after the macro stopped intercepting failures, silently passing.
TEST(QuiesceOnExitResidualWitness, ReportsWhenIocNeverDrains) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);

    EXPECT_NONFATAL_FAILURE(
        ([&] {
            auto keep_alive = asio::make_work_guard(ioc);
            quiesce_on_exit quiesce{ioc, *clock, 1ms};
        }()),
        "quiesce_on_exit: the io_context did not run out of work");
}

// Branch does not fire: an otherwise-empty io_context (no outstanding work,
// no scheduled sleeps) drains and stops within the budget. Any nonfatal
// failure here (i.e. an unexpected ADD_FAILURE) fails this test outright,
// which is exactly the assertion — the branch must stay silent.
TEST(QuiesceOnExitResidualWitness, SilentWhenIocDrainsNormally) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);

    quiesce_on_exit quiesce{ioc, *clock, 1ms};
}

// gate-b/r1 P1-4 (Art. VII §4): the two-argument {ioc, clock} aggregate
// init's DEFAULT budget (5s) had no falsifiable witness — every existing
// two-arg caller drains promptly regardless of what the default is, so
// changing the initializer 5s->0ms left the whole suite green. Assert the
// default directly. Costs no wall-clock: `quiesce` is never exercised past
// construction here (the io_context is never run), only its stored `budget`
// member is read, and the destructor's own drain runs against an empty,
// never-started context, which stops immediately regardless of the budget.
TEST(QuiesceOnExitResidualWitness, DefaultBudgetIsFiveSeconds) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);

    quiesce_on_exit quiesce{ioc, *clock};
    EXPECT_EQ(quiesce.budget, 5s);
}

// gate-b/r1 P1-1's mechanism, isolated: quiesce_on_exit.transport, when set,
// must be closed BEFORE the residual-work drain runs. Without it, a
// coroutine parked in async_write on a still-open transport never wakes, and
// this test's 50ms drain window would time out (ADD_FAILURE) even though
// nothing is leaked forever — it simply never quiesces within budget.
// Mutation: deleting the `quiesce.transport = &transport;` line below turns
// this RED (ADD_FAILURE fires, caught by gtest as a normal test failure —
// there is no SPI wrapper here because the passing path must be silent).
TEST(QuiesceOnExitResidualWitness, ClosesTransportBeforeDraining) {
    asio::io_context ioc;
    auto clock = make_mock_clock(ioc);
    BlockedWriteTransport transport{ioc.get_executor()};

    std::array<std::byte, 1> buf{};
    asio::co_spawn(ioc, transport.async_write(std::span<const std::byte>{buf}), asio::detached);
    ioc.run_for(10ms);  // let the write actually start blocking on the timer
    ioc.restart();

    quiesce_on_exit quiesce{ioc, *clock, 50ms};
    quiesce.transport = &transport;
}
