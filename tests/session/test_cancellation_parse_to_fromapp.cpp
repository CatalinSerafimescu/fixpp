// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_cancellation_parse_to_fromapp.cpp — [2d §9.4] Seam 4
//
// Cancellation-slot propagation parse→fromApp ([2d §6.5]; I-08/I-09). The
// 2d-owned property decomposes into two deterministically-assertable halves
// (D-5 scripted scoping — no real FIX FSM):
//   • cancellable_dispatch REAPS a handler whose slot was signalled BEFORE
//     pickup → expected_t<void>{ unexpect, error::dispatch_aborted }, the
//     handler NEVER invoked, never a thrown exception (I-09); not-signalled
//     runs the handler exactly once (case-3);
//   • close(graceful) phase-2 fires cancellation_type::total on the SESSION
//     ROOT slot — the single propagation point a pending fromApp dispatch is
//     bound to (T039).
// Together: a fromApp dispatch bound to the root slot is reaped at phase-2.
//
// Determinism for the reap window: the session strand is backed by a SECOND
// io_context that is NOT run until after the cancellation is emitted, so the
// asio::dispatch hand-off genuinely defers (same-io_context dispatch would
// run inline — no reap window, which is correct case-3 behaviour but not the
// scenario under test). Session::session_arena() is valid right after the
// ctor (I-18) — open() only needed where close()'s two-phase body runs.
#include <gtest/gtest.h>

#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <exception>
#include <fixpp/core/cancellable_dispatch.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/session_executor.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <optional>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

namespace {

using fixpp::core::cancellable_dispatch;
using fixpp::core::EngineConfig;
using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::core::make_session_executor;
using fixpp::session::close_mode;
using fixpp::session::Session;
using fixpp::session::SessionConfig;
using fixpp::session::threading_mode;

TEST(SeamCancellationParseToFromApp, SignalledBeforePickupIsReapedDispatchAborted) {
    // `work` backs the session strand; `drive` runs the fromApp coroutine.
    // Manual non-blocking poll() pumping (NOT run() — co_spawn's work guard
    // would block run() while the coroutine is parked cross-context) gives a
    // deterministic reap window: drive.poll() → coroutine parks at the
    // asio::dispatch hand-off (posted to `work`); emit; work.poll() runs the
    // queued continuation which observes the cancellation and reaps;
    // drive.poll() delivers the co_spawn completion.
    asio::io_context work;
    asio::io_context drive;
    EngineConfig engine;
    engine.executor = work.get_executor();
    SessionConfig cfg;
    Session sess{engine, cfg};
    auto se = *make_session_executor(work.get_executor(), threading_mode::per_session_strand,
                                     /*attested=*/false, &sess);
    asio::cancellation_signal sig;
    std::atomic<bool> handler_ran{false};
    bool done = false;
    expected_t<void> r{};

    asio::co_spawn(
        drive,
        [&]() -> asio::awaitable<expected_t<void>> {
            co_return co_await cancellable_dispatch(se, sig.slot(),
                                                    [&] { handler_ran.store(true); });
        },
        [&](std::exception_ptr ep, expected_t<void> v) {
            EXPECT_FALSE(ep);  // I-09: never thrown
            r = v;
            done = true;
        });

    drive.poll();                              // park at the hand-off
    sig.emit(asio::cancellation_type::total);  // signalled BEFORE pickup
    work.poll();                               // continuation reaps
    drive.poll();                              // co_spawn completion

    EXPECT_TRUE(done);
    EXPECT_FALSE(handler_ran.load());  // handler REAPED
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::dispatch_aborted);
}

TEST(SeamCancellationParseToFromApp, NotSignalledRunsHandlerExactlyOnce) {
    asio::io_context ctx;
    EngineConfig engine;
    engine.executor = ctx.get_executor();
    SessionConfig cfg;
    Session sess{engine, cfg};
    auto se = *make_session_executor(ctx.get_executor(), threading_mode::per_session_strand,
                                     /*attested=*/false, &sess);
    asio::cancellation_signal sig;
    std::atomic<int> calls{0};

    auto fut = asio::co_spawn(
        ctx,
        [&]() -> asio::awaitable<expected_t<void>> {
            co_return co_await cancellable_dispatch(se, sig.slot(), [&] { calls.fetch_add(1); });
        },
        asio::use_future);

    ctx.run();  // slot never emitted → case 3

    auto r = fut.get();
    EXPECT_EQ(calls.load(), 1);
    EXPECT_TRUE(r.has_value());
}

// close(graceful) phase-2 fires cancellation_type::total on the root slot —
// the propagation point a pending fromApp dispatch binds to (T039 / I-07).
TEST(SeamCancellationParseToFromApp, CloseGracefulPhase2FiresRootTotal) {
    asio::io_context ctx;
    EngineConfig engine;
    engine.executor = ctx.get_executor();
    SessionConfig cfg;
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();              // T050
    cfg.security_profile = fixpp::test_support::make_minimal_security_profile();  // RC#1
    Session sess{engine, cfg};

    auto opened = asio::co_spawn(ctx, sess.open(), asio::use_future);
    ctx.run();
    ASSERT_TRUE(opened.get().has_value());
    ctx.restart();

    // Bind an observer to the root slot exactly as a pending fromApp
    // dispatch would (this is what cancellable_dispatch assigns internally).
    std::atomic<bool> root_total_fired{false};
    auto root = sess.root_cancellation_slot();
    root.assign([&](asio::cancellation_type t) {
        if ((t & asio::cancellation_type::total) != asio::cancellation_type::none) {
            root_total_fired.store(true);
        }
    });

    auto closed = asio::co_spawn(ctx, sess.close(close_mode::graceful), asio::use_future);
    ctx.run();

    EXPECT_TRUE(closed.get().has_value());
    EXPECT_TRUE(root_total_fired.load());  // phase-2 propagation point
}

}  // namespace
