// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/transport/test_bounded_resolve.cpp — #361.
//
// Drives src/transport/bounded_resolve.hpp's CONTROL FLOW through
// `resolve_bounded_with`, whose operand is the operation's INITIATION.
//
// ⚠️ WHAT THIS FILE IS AND IS NOT EVIDENCE FOR. An initiation that never calls
// its completion is a faithful model of a wedged `getaddrinfo` AS SEEN BY THIS
// CODE — the helper cannot tell the two apart, because all it ever observes is
// "the completion has not run". So these cells are real witnesses for the
// deadline, the cancellation, the error mapping, the abandonment accounting and
// the backlog cap. They are NOT evidence that a real wedged `getaddrinfo`
// behaves that way, nor about what abandoning costs the io_context: no
// process-local way to wedge glibc's resolver exists, and wedging it needs a
// private mount namespace a gtest cell cannot enter. That half is
// `tools/probes/resolve_bound_probe.cpp`, run by hand.
//
// ⚠️ THE BACKLOG COUNTER IS PROCESS-WIDE, so every cell that abandons must
// return its budget or it poisons the cells after it. `BacklogReset` below is a
// fixture, not politeness — and the LAST cell asserts the counter is back to
// zero, which is what catches a leak in the accounting itself.
#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "transport/bounded_resolve.hpp"

namespace {

using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::transport::detail::abandoned_resolve_backlog;
using fixpp::transport::detail::bounded_resolve_state;
using fixpp::transport::detail::kMaxAbandonedResolves;
using fixpp::transport::detail::resolve_bounded_with;
using results_type = asio::ip::tcp::resolver::results_type;
using namespace std::chrono_literals;

// The counter outlives a cell, so a cell that abandons must hand its budget
// back. Every abandoned state is kept alive here for the duration of the cell
// (that is what "abandoned" MEANS — the completion still owns it), then released.
struct BacklogGuard {
    BacklogGuard() {
        EXPECT_EQ(abandoned_resolve_backlog().load(), 0) << "leaked from a prior cell";
    }
    ~BacklogGuard() { EXPECT_EQ(abandoned_resolve_backlog().load(), 0) << "this cell leaked"; }
};

// An initiation that never completes — the model of a wedged getaddrinfo. It
// keeps the state alive exactly as a real pending operation would, in `kept`.
struct WedgedInitiation {
    std::vector<std::shared_ptr<bounded_resolve_state>>* kept;
    void operator()(const std::shared_ptr<bounded_resolve_state>& st, auto /*completion*/) const {
        kept->push_back(st);  // stand-in for asio's op queue holding the handler
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// The deadline actually ends the wait — the property #361 is about.
// ─────────────────────────────────────────────────────────────────────────────
TEST(BoundedResolve, DeadlineRetiresTheCallerWhileTheOperationIsStillOutstanding) {
    BacklogGuard guard;
    asio::io_context ioc;
    std::vector<std::shared_ptr<bounded_resolve_state>> kept;

    std::optional<expected_t<results_type>> out;
    asio::co_spawn(
        asio::make_strand(ioc),
        [&]() -> asio::awaitable<void> {
            out = co_await resolve_bounded_with(std::chrono::steady_clock::now() + 80ms,
                                                WedgedInitiation{&kept});
        },
        asio::detached);

    const auto t0 = std::chrono::steady_clock::now();
    ioc.run();
    const auto elapsed = std::chrono::steady_clock::now() - t0;

    ASSERT_TRUE(out.has_value()) << "the caller never retired — the deadline did not end the wait";
    ASSERT_FALSE(out->has_value());
    EXPECT_EQ(out->error(), error::transport_connect_timeout);
    EXPECT_LT(elapsed, 5s) << "retired, but not at anything like the deadline";

    // The operation is STILL outstanding and the state is still alive for it:
    // that is what abandonment means, and it is charged to the backlog.
    ASSERT_EQ(kept.size(), 1u);
    EXPECT_EQ(abandoned_resolve_backlog().load(), 1);
    kept.clear();  // the "op completed / was destroyed" moment
    EXPECT_EQ(abandoned_resolve_backlog().load(), 0) << "the destructor must return the budget";
}

// ─────────────────────────────────────────────────────────────────────────────
// A cancellation ends it too, and reports the cancelled variant — this is the
// half Engine::stop() needs and the half asio's resolver cannot provide.
// ─────────────────────────────────────────────────────────────────────────────
TEST(BoundedResolve, CancellationRetiresTheCallerAndReportsCancelled) {
    BacklogGuard guard;
    asio::io_context ioc;
    std::vector<std::shared_ptr<bounded_resolve_state>> kept;
    asio::cancellation_signal sig;

    std::optional<expected_t<results_type>> out;
    auto strand = asio::make_strand(ioc);
    asio::co_spawn(
        strand,
        [&]() -> asio::awaitable<void> {
            // The transports do this at the top of async_connect; without it the
            // co_spawn default filter is terminal-only and stop()'s total is
            // dropped — which is the pre-#361 behaviour this cell would then
            // silently re-measure.
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            out = co_await resolve_bounded_with(std::chrono::steady_clock::now() + 60s,
                                                WedgedInitiation{&kept});
        },
        asio::bind_cancellation_slot(sig.slot(), asio::detached));

    // Let the frame reach the gate, then cancel. A 60 s deadline means only the
    // cancellation can end this cell — if it does not, the run below hangs and
    // ctest's timeout reports it, rather than the cell passing for a wrong reason.
    ioc.run_for(100ms);
    asio::post(strand, [&sig] { sig.emit(asio::cancellation_type::total); });
    ioc.run();

    ASSERT_TRUE(out.has_value());
    ASSERT_FALSE(out->has_value());
    EXPECT_EQ(out->error(), error::transport_connect_cancelled)
        << "a cancellation must not be reported as a timeout — the reconnect FSM treats both as "
           "attempt failures, but the C API maps them to DIFFERENT categories";
    ASSERT_EQ(kept.size(), 1u);
    kept.clear();
    EXPECT_EQ(abandoned_resolve_backlog().load(), 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// The completion's own outcomes: success, failure, and operation_aborted, which
// maps to cancelled rather than to a resolve failure.
// ─────────────────────────────────────────────────────────────────────────────
TEST(BoundedResolve, CompletionOutcomesMapToTheDocumentedErrors) {
    BacklogGuard guard;

    struct Arm {
        asio::error_code ec;
        error expected;
        const char* what;
    };
    const Arm arms[] = {
        {asio::error::host_not_found, error::transport_resolve_failed, "resolver said no"},
        {asio::error::operation_aborted, error::transport_connect_cancelled, "op aborted"},
    };

    for (const auto& arm : arms) {
        asio::io_context ioc;
        std::optional<expected_t<results_type>> out;
        asio::co_spawn(
            asio::make_strand(ioc),
            [&]() -> asio::awaitable<void> {
                out = co_await resolve_bounded_with(
                    std::chrono::steady_clock::now() + 60s,
                    [ec = arm.ec](const std::shared_ptr<bounded_resolve_state>& st,
                                  auto completion) {
                        // Post rather than call inline: a real op never completes
                        // inside its initiating function, and completing inline
                        // would test an ordering asio does not produce.
                        asio::post(st->gate.get_executor(),
                                   [completion = std::move(completion), ec]() mutable {
                                       completion(ec, results_type{});
                                   });
                    });
            },
            asio::detached);
        ioc.run();

        ASSERT_TRUE(out.has_value()) << arm.what;
        ASSERT_FALSE(out->has_value()) << arm.what;
        EXPECT_EQ(out->error(), arm.expected) << arm.what;
    }
    EXPECT_EQ(abandoned_resolve_backlog().load(), 0)
        << "a completed resolve must never be charged to the backlog";
}

// ─────────────────────────────────────────────────────────────────────────────
// The cap — the reason it exists is the reconnect loop, so the cell drives the
// reconnect loop's shape: attempt, abandon, attempt again, N times.
//
// Before the cap, every abandoned lookup stacked behind the previous one on
// asio's SINGLE resolver work thread and nothing bounded the queue.
// ─────────────────────────────────────────────────────────────────────────────
TEST(BoundedResolve, TheBacklogCapRefusesRatherThanStackingAbandonedLookups) {
    BacklogGuard guard;
    asio::io_context ioc;
    std::vector<std::shared_ptr<bounded_resolve_state>> kept;
    std::vector<expected_t<results_type>> outcomes;

    // kMaxAbandonedResolves attempts that each abandon, then two more.
    for (int i = 0; i < kMaxAbandonedResolves + 2; ++i) {
        asio::co_spawn(
            asio::make_strand(ioc),
            [&]() -> asio::awaitable<void> {
                auto r = co_await resolve_bounded_with(std::chrono::steady_clock::now() + 30ms,
                                                       WedgedInitiation{&kept});
                outcomes.push_back(std::move(r));
            },
            asio::detached);
        ioc.run();
        ioc.restart();
    }

    ASSERT_EQ(outcomes.size(), static_cast<std::size_t>(kMaxAbandonedResolves + 2));
    for (int i = 0; i < kMaxAbandonedResolves; ++i) {
        ASSERT_FALSE(outcomes[static_cast<std::size_t>(i)].has_value());
        EXPECT_EQ(outcomes[static_cast<std::size_t>(i)].error(), error::transport_connect_timeout)
            << "attempt " << i << " should have waited out its deadline";
    }
    for (int i = kMaxAbandonedResolves; i < kMaxAbandonedResolves + 2; ++i) {
        ASSERT_FALSE(outcomes[static_cast<std::size_t>(i)].has_value());
        EXPECT_EQ(outcomes[static_cast<std::size_t>(i)].error(), error::transport_resolve_failed)
            << "attempt " << i << " is past the cap and must be REFUSED, not queued";
    }

    // Exactly the capped number of operations were ever started.
    EXPECT_EQ(kept.size(), static_cast<std::size_t>(kMaxAbandonedResolves))
        << "a refused attempt must not have started an operation — if it did, the cap bounds the "
           "error code and not the resource, which is the whole point";
    EXPECT_EQ(abandoned_resolve_backlog().load(), kMaxAbandonedResolves);

    kept.clear();
    EXPECT_EQ(abandoned_resolve_backlog().load(), 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// A refusal must not be sticky: once the wedged lookups drain, resolves work.
// ─────────────────────────────────────────────────────────────────────────────
TEST(BoundedResolve, TheCapLiftsWhenTheAbandonedLookupsDrain) {
    BacklogGuard guard;
    asio::io_context ioc;
    std::vector<std::shared_ptr<bounded_resolve_state>> kept;

    for (int i = 0; i < kMaxAbandonedResolves; ++i) {
        asio::co_spawn(
            asio::make_strand(ioc),
            [&]() -> asio::awaitable<void> {
                (void)co_await resolve_bounded_with(std::chrono::steady_clock::now() + 20ms,
                                                    WedgedInitiation{&kept});
            },
            asio::detached);
        ioc.run();
        ioc.restart();
    }
    ASSERT_EQ(abandoned_resolve_backlog().load(), kMaxAbandonedResolves);

    kept.clear();  // the wedged lookups finally return
    ASSERT_EQ(abandoned_resolve_backlog().load(), 0);

    std::optional<expected_t<results_type>> out;
    asio::co_spawn(
        asio::make_strand(ioc),
        [&]() -> asio::awaitable<void> {
            out = co_await resolve_bounded_with(
                std::chrono::steady_clock::now() + 60s,
                [](const std::shared_ptr<bounded_resolve_state>& st, auto completion) {
                    asio::post(st->gate.get_executor(),
                               [completion = std::move(completion)]() mutable {
                                   completion(asio::error::host_not_found, results_type{});
                               });
                });
        },
        asio::detached);
    ioc.run();

    ASSERT_TRUE(out.has_value());
    ASSERT_FALSE(out->has_value());
    EXPECT_EQ(out->error(), error::transport_resolve_failed)
        << "the resolver answered, so this must be the resolver's failure — not a refusal";
}

}  // namespace
