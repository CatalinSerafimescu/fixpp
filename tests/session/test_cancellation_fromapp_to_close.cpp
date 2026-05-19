// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_cancellation_fromapp_to_close.cpp — [2d §9.5] Seam 5
//
// Cancellation-slot propagation fromApp→close ([2d §6.5]; I-07): a `fromApp`
// callback blocked inside a Clock::sleep_until is DRAINED with
// asio::error::operation_aborted (never a hang, never a thrown escape across
// the boundary — I-09) when close(terminal) fires the session root
// cancellation_type::total. Plus the 2d-owned ORDERING properties asserted
// against the D-16 phase-1 flush seam (D-5 scripted-test-double scoping — no
// real FileStore; the seam asserts only the call-site contract):
//   • close(graceful)  invokes the phase-1 flush hook EXACTLY ONCE;
//   • close(terminal)  NEVER invokes it (phase 1 skipped);
//   • phase 1 (flush) resolves BEFORE phase 2 (root total);
//   • idempotent THREE-STATE model (I-10): never-opened / already-closed →
//     error::session_already_closed; no side effects.
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <system_error>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_state.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_future.hpp>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>

#include "support/minimal_dictionary.hpp"

namespace {

using namespace std::chrono_literals;
using fixpp::core::EngineConfig;
using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::session::close_mode;
using fixpp::session::Session;
using fixpp::session::SessionConfig;

std::shared_ptr<fixpp::core::mock_clock> make_mock(asio::any_io_executor ex) {
    return std::make_shared<fixpp::core::mock_clock>(
        fixpp::core::utc_time_point{}, fixpp::core::steady_time_point{}, ex);
}

TEST(SeamCancellationFromAppToClose, FlushHookOnceUnderGracefulNeverUnderTerminal) {
    // graceful → hook invoked exactly once.
    {
        asio::io_context ctx;
        EngineConfig engine;
        engine.executor = ctx.get_executor();
        engine.clock    = make_mock(ctx.get_executor());
        SessionConfig cfg;
        cfg.dictionary  = fixpp::test_support::make_minimal_dictionary(); // T050
        Session s{engine, cfg};
        asio::co_spawn(ctx, s.open(), asio::detached);
        ctx.run();

        std::atomic<int> flush_calls{0};
        s.set_close_flush_hook([&]() -> expected_t<void> {
            flush_calls.fetch_add(1);
            return expected_t<void>{};
        });

        ctx.restart();
        expected_t<void> r;
        asio::co_spawn(ctx, s.close(close_mode::graceful),
                       [&](std::exception_ptr ep, expected_t<void> v) {
                           EXPECT_FALSE(ep);
                           r = v;
                       });
        ctx.run();
        EXPECT_TRUE(r.has_value());
        EXPECT_EQ(flush_calls.load(), 1);
    }
    // terminal → hook NEVER invoked (phase 1 skipped).
    {
        asio::io_context ctx;
        EngineConfig engine;
        engine.executor = ctx.get_executor();
        engine.clock    = make_mock(ctx.get_executor());
        SessionConfig cfg;
        cfg.dictionary  = fixpp::test_support::make_minimal_dictionary(); // T050
        Session s{engine, cfg};
        asio::co_spawn(ctx, s.open(), asio::detached);
        ctx.run();

        std::atomic<int> flush_calls{0};
        s.set_close_flush_hook([&]() -> expected_t<void> {
            flush_calls.fetch_add(1);
            return expected_t<void>{};
        });

        ctx.restart();
        expected_t<void> r;
        asio::co_spawn(ctx, s.close(close_mode::terminal),
                       [&](std::exception_ptr, expected_t<void> v) { r = v; });
        ctx.run();
        EXPECT_TRUE(r.has_value());
        EXPECT_EQ(flush_calls.load(), 0);
    }
}

TEST(SeamCancellationFromAppToClose, Phase1FlushResolvesBeforePhase2RootTotal) {
    asio::io_context ctx;
    EngineConfig engine;
    engine.executor = ctx.get_executor();
    engine.clock    = make_mock(ctx.get_executor());
    SessionConfig cfg;
    cfg.dictionary  = fixpp::test_support::make_minimal_dictionary(); // T050
    Session s{engine, cfg};
    asio::co_spawn(ctx, s.open(), asio::detached);
    ctx.run();

    std::atomic<int> seq{0};
    int flush_seq = -1;
    int cancel_seq = -1;

    s.set_close_flush_hook([&]() -> expected_t<void> {
        flush_seq = seq.fetch_add(1);
        return expected_t<void>{};
    });
    // A root-bound op observes phase-2 total; its cancellation must be
    // ordered strictly AFTER the phase-1 flush. The mock clock is frozen, so
    // the sleep can only resolve via the phase-2 root cancellation.
    asio::co_spawn(
        ctx,
        [&]() -> asio::awaitable<void> {
            // co_spawn defaults to terminal-only cancellation; phase-2 fires
            // cancellation_type::total ([2d §4.7]), so total must be enabled
            // for the root signal to tear this down (the real session wires
            // this — 005; D-5 scripted scope wires it test-side).
            co_await asio::this_coro::reset_cancellation_state(
                asio::enable_total_cancellation());
            try {
                co_await s.effective_clock()->sleep_until(
                    std::chrono::steady_clock::now() + 1h);
            } catch (const std::system_error&) {
                cancel_seq = seq.fetch_add(1);
            }
        },
        asio::bind_cancellation_slot(s.root_cancellation_slot(),
                                     asio::detached));

    ctx.restart();
    expected_t<void> r;
    asio::co_spawn(ctx, s.close(close_mode::graceful),
                   [&](std::exception_ptr, expected_t<void> v) { r = v; });
    ctx.run();

    EXPECT_TRUE(r.has_value());
    ASSERT_GE(flush_seq, 0);
    ASSERT_GE(cancel_seq, 0);
    EXPECT_LT(flush_seq, cancel_seq);   // phase 1 strictly before phase 2
}

TEST(SeamCancellationFromAppToClose, FromAppBlockedSleepDrainedByTerminalClose) {
    asio::io_context ctx;
    EngineConfig engine;
    engine.executor = ctx.get_executor();
    auto clk        = make_mock(ctx.get_executor());
    engine.clock    = clk;
    SessionConfig cfg;
    cfg.dictionary  = fixpp::test_support::make_minimal_dictionary(); // T050
    Session s{engine, cfg};
    asio::co_spawn(ctx, s.open(), asio::detached);
    ctx.run();

    std::atomic<bool> aborted{false};
    std::atomic<bool> finished{false};
    asio::co_spawn(
        ctx,
        [&]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(
                asio::enable_total_cancellation());   // phase-2 = total
            try {
                // mock clock is frozen → this only completes via cancellation.
                co_await s.effective_clock()->sleep_until(
                    std::chrono::steady_clock::now() + 1h);
            } catch (const std::system_error& e) {
                EXPECT_EQ(e.code(), asio::error::operation_aborted);  // I-09
                aborted.store(true);
            }
            finished.store(true);
        },
        asio::bind_cancellation_slot(s.root_cancellation_slot(),
                                     asio::detached));

    ctx.restart();
    ctx.poll();   // park the fromApp sleep

    expected_t<void> r;
    asio::co_spawn(ctx, s.close(close_mode::terminal),
                   [&](std::exception_ptr, expected_t<void> v) { r = v; });
    ctx.run();

    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(aborted.load());     // drained with operation_aborted
    EXPECT_TRUE(finished.load());    // no hang
}

TEST(SeamCancellationFromAppToClose, IdempotentThreeStateModel) {
    asio::io_context ctx;
    EngineConfig engine;
    engine.executor = ctx.get_executor();
    engine.clock    = make_mock(ctx.get_executor());
    SessionConfig cfg;
    cfg.dictionary  = fixpp::test_support::make_minimal_dictionary(); // T050

    // never-opened → session_already_closed, no side effects.
    {
        Session s{engine, cfg};
        auto r = asio::co_spawn(ctx, s.close(close_mode::graceful),
                                asio::use_future);
        ctx.run();
        auto v = r.get();
        ASSERT_FALSE(v.has_value());
        EXPECT_EQ(v.error(), error::session_already_closed);
    }
    // open → close → close again (drained) → session_already_closed; the
    // phase-1 hook ran exactly once (first close only), no side effects on
    // the redundant call.
    {
        ctx.restart();
        Session s{engine, cfg};
        asio::co_spawn(ctx, s.open(), asio::detached);
        ctx.run();
        std::atomic<int> flush_calls{0};
        s.set_close_flush_hook([&]() -> expected_t<void> {
            flush_calls.fetch_add(1);
            return expected_t<void>{};
        });

        ctx.restart();
        auto r1 = asio::co_spawn(ctx, s.close(close_mode::graceful),
                                 asio::use_future);
        ctx.run();
        EXPECT_TRUE(r1.get().has_value());

        ctx.restart();
        auto r2 = asio::co_spawn(ctx, s.close(close_mode::graceful),
                                 asio::use_future);
        ctx.run();
        auto v2 = r2.get();
        ASSERT_FALSE(v2.has_value());
        EXPECT_EQ(v2.error(), error::session_already_closed);
        EXPECT_EQ(flush_calls.load(), 1);   // no second-call side effects
    }
}

}  // namespace
