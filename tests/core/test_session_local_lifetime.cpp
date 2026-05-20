// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/core/test_session_local_lifetime.cpp — [2d §9.17] Seam 17
//
// session_local<T> lifetime under cancellation (TSan-clean; [2d §4.6];
// I-11/I-17): close(graceful) fired while a `fromApp` coroutine is blocked
// must NOT free the slot out from under an in-flight reader. The slot lives
// in the Session object (plain value ownership) — it is DRAINED (cleared) at
// close completion, never destroyed mid-read. Asserted: a read taken before
// close is the populated value; the in-flight coroutine is cancelled
// (operation_aborted, never UB) with the slot still valid; after close
// completes the slot reads the default (cleared, not a dangling read).
// Single io_context so the interleave is deterministic and TSan-clean.
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <memory>
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
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/core/trace_context.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

namespace {

using namespace std::chrono_literals;
using fixpp::core::EngineConfig;
using fixpp::session::close_mode;
using fixpp::session::Session;
using fixpp::session::SessionConfig;

bool eq(const fixpp::otel::trace_context& a,
        const fixpp::otel::trace_context& b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

TEST(SeamSessionLocalLifetime, SlotValidUntilCloseCompletesThenCleared) {
    asio::io_context ctx;
    EngineConfig engine;
    engine.executor = ctx.get_executor();
    engine.clock    = std::make_shared<fixpp::core::mock_clock>(
        fixpp::core::utc_time_point{}, fixpp::core::steady_time_point{},
        ctx.get_executor());

    SessionConfig cfg;
    cfg.dictionary       = fixpp::test_support::make_minimal_dictionary(); // T050
    cfg.security_profile = fixpp::test_support::make_minimal_security_profile();  // RC#1
    fixpp::otel::trace_context seed{};
    seed.trace_id[0] = std::byte{0xC7};
    seed.span_id[0]  = std::byte{0x5E};
    seed.flags       = 0x01;
    cfg.initial_trace_context = seed;

    Session sess{engine, cfg};
    asio::co_spawn(ctx, sess.open(), asio::detached);
    ctx.run();
    ctx.restart();

    std::atomic<bool> read_ok{false};
    std::atomic<bool> aborted{false};
    std::atomic<bool> finished{false};

    asio::co_spawn(
        sess.executor(),     // run UNDER the bound session_executor so
                             // current_trace_context() recovers Session*
        [&]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(
                asio::enable_total_cancellation());          // phase-2 = total
            const auto seen = co_await fixpp::current_trace_context();
            read_ok.store(eq(seen, seed));                    // slot live: seed
            try {
                // Frozen mock clock → only the phase-2 root total ends this;
                // the slot must remain valid for the whole wait.
                co_await sess.effective_clock()->sleep_until(
                    std::chrono::steady_clock::now() + 1h);
            } catch (const std::system_error& e) {
                EXPECT_EQ(e.code(), asio::error::operation_aborted);
                aborted.store(true);
            }
            finished.store(true);
        },
        asio::bind_cancellation_slot(sess.root_cancellation_slot(),
                                     asio::detached));

    ctx.poll();                                  // park the fromApp reader
    EXPECT_TRUE(read_ok.load());                 // slot valid mid-fromApp

    auto closed = asio::co_spawn(ctx, sess.close(close_mode::graceful),
                                 asio::use_future);
    ctx.run();

    EXPECT_TRUE(closed.get().has_value());
    EXPECT_TRUE(aborted.load());                 // cancelled, never UB
    EXPECT_TRUE(finished.load());                // no hang
    // Slot was DRAINED at close completion (T045), not destroyed: a fresh
    // read of the Session-owned slot is the default, not a dangling access.
    EXPECT_TRUE(eq(sess.trace_context_value(), fixpp::otel::trace_context{}));
}

}  // namespace
