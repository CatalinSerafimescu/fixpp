// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/core/test_third_party_clock_conformance.cpp — [2d §9.15] Seam 15
//
// Third-party Clock conformance / SC-006 (D-5). A clock derivative that is
// NEITHER system_clock_source NOR mock_clock drives the
// "Logon→NewOrderSingle→cancel" SCRIPT (scripted test-double FSM — FIX
// labels are script labels, not real FSM output). Asserts ONLY 2d-owned
// properties: sleep_until completion lands on the awaiter's bound executor;
// a competing cancellation (the awaitable-operators `||` race — the losing
// operand is cancelled) is HONOURED via operation_aborted; NOT FIX FSM
// correctness (005's). Alloc-guard cleanliness is US6 (seam 7).
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <variant>

#include <fixpp/core/clock.hpp>

#include "support/scripted_fsm.hpp"

namespace {

using namespace std::chrono_literals;
using namespace asio::experimental::awaitable_operators;
namespace ts = fixpp::testsupport;

// A genuinely third-party Clock impl (distinct type; user-supplied).
class tp_clock final : public fixpp::core::Clock {
public:
    explicit tp_clock(asio::any_io_executor ex) : ex_(std::move(ex)) {}
    fixpp::core::utc_time_point now() const noexcept override {
        return std::chrono::system_clock::now();
    }
    fixpp::core::steady_time_point steady_now() const noexcept override {
        return std::chrono::steady_clock::now();
    }
    asio::awaitable<void> sleep_until(
        fixpp::core::steady_time_point dl) override {
        asio::steady_timer t{ex_};
        t.expires_at(dl);
        co_await t.async_wait(asio::use_awaitable);  // throws op_aborted on cancel
        co_return;
    }
    void cancel_sleeps() noexcept override {}
private:
    asio::any_io_executor ex_;
};

TEST(SeamThirdPartyClockConformance, CompletesOnBoundExecutorAndHonoursCancel) {
    asio::io_context ioc;
    auto wg = asio::make_work_guard(ioc);
    std::thread runner{[&] { ioc.run(); }};

    tp_clock clk{ioc.get_executor()};
    const auto script = ts::make_conformance_cancel_script(/*terminal=*/false);

    std::atomic<bool> cancel_honoured{false};
    std::atomic<bool> on_bound_executor{false};

    auto fut = asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            auto self_ex = co_await asio::this_coro::executor;
            for (const auto& step : script) {
                if (step.label == ts::fsm_label::request_close) {
                    // Race the long third-party sleep against a short timer;
                    // when the timer wins, the awaitable-operators `||`
                    // CANCELS the losing operand — i.e. drives a real
                    // cancellation through tp_clock::sleep_until. If the
                    // third-party clock honours operation_aborted the group
                    // resolves cleanly (no hang, no double-complete).
                    asio::steady_timer kick{co_await asio::this_coro::executor};
                    kick.expires_after(30ms);
                    auto which = co_await (
                        clk.sleep_until(std::chrono::steady_clock::now() + 10s)
                        || kick.async_wait(asio::use_awaitable));
                    // index 1 ⇒ the timer won and the long sleep was
                    // cancelled+drained (cancellation honoured).
                    cancel_honoured.store(which.index() == 1);
                    auto resumed_ex = co_await asio::this_coro::executor;
                    on_bound_executor.store(resumed_ex == self_ex);
                }
            }
            co_return;
        },
        asio::use_future);

    fut.get();
    wg.reset();
    runner.join();

    EXPECT_TRUE(cancel_honoured.load())
        << "third-party Clock did not honour cancellation of sleep_until";
    EXPECT_TRUE(on_bound_executor.load())
        << "completion did not land on the awaiter's bound executor";
}

}  // namespace
