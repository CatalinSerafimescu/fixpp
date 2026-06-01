// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/core/test_trace_context_resume.cpp — [2d §9.6] Seam 6
//
// Two tests covering "session-domain trace_context survives coroutine
// resume across different threads" ([2d §4.6]; I-11; E8; FR-015):
//
//   1. SeamTraceContextResume.SurvivesExplicitCrossThreadHop —
//      deterministic structural test. Constructs a SECOND session_executor
//      over a disjoint single-thread io_context (Pattern B from the
//      `current_trace_context()` docstring); posts to it via asio::post
//      use_awaitable; asserts the cross-thread hop happens by std::thread
//      identity (t1 != t0) AND that current_trace_context() re-reads the
//      same seed on the other side. Hops back to the primary
//      session_executor for clean future-completion.
//
//   2. SeamTraceContextResume.SurvivesSchedulingResumes —
//      realistic-scheduling soft test. The "natural client code" shape: a
//      coroutine on sess.executor() loops sleep_until(now + 1ms) and
//      re-reads current_trace_context() each iteration, asserting only the
//      contract (byte-equality with seed). Whether a thread hop is
//      observed in a given run depends on asio's worker routing — this
//      test does NOT assert on that scheduling property (it is not a
//      library contract; see gh#79).
//
// The deterministic test alone proves the [2d §4.6] guarantee; the soft
// test additionally exercises the natural-client-code path so a regression
// in real-world scheduling-dependent flow would still surface as a
// byte-equality failure (rather than the previous false-positive
// EXPECT_TRUE(done.get()) flake on 2-vCPU GHA runners).
//
// Recovery mechanism (under test): the value is recovered through the
// borrowed stable Session* (typed session_executor::session_ptr, round-3
// RC#1) — NOT thread_local — so it is correct on every thread the
// coroutine resumes on, as long as it remains on a session_executor for
// the right Session*.
//
// io_context + manual std::threads (NOT asio::thread_pool) so teardown is
// fully deterministic: the worker threads exit on ioc.stop() before any
// asio internal any_executor type-erasure shared-target ref-counts are
// torn down. The thread_pool teardown path races (TSan) on the
// shared_target_executor<session_executor> ref-count vs the still-running
// worker — a known asio internal type-erasure noise specific to a custom
// executor target. The 2d-owned property (session_local survives
// cross-thread resume) is unaffected; this just keeps the harness
// TSan-clean.
//
// Motivating issue: https://github.com/CatalinSerafimescu/fixpp/issues/79
#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/core/trace_context.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <thread>
#include <vector>

#include "support/disjoint_session_executor.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

namespace {

using namespace std::chrono_literals;
using fixpp::core::EngineConfig;
using fixpp::session::Session;
using fixpp::session::SessionConfig;

bool eq(const fixpp::otel::trace_context& a, const fixpp::otel::trace_context& b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

// Shared test fixture: 4-thread io_context backing a session, populated
// trace_context seed, opened session. Both tests share this prelude.
struct SeamTraceContextResume : ::testing::Test {
    asio::io_context ioc;
    asio::executor_work_guard<asio::io_context::executor_type> work{asio::make_work_guard(ioc)};
    std::vector<std::thread> threads;
    EngineConfig engine;
    SessionConfig cfg;
    fixpp::otel::trace_context seed{};

    SeamTraceContextResume() {
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back([this] { ioc.run(); });
        }
        engine.executor = ioc.get_executor();
        engine.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        for (std::size_t i = 0; i < seed.trace_id.size(); ++i)
            seed.trace_id[i] = std::byte(0xA0 + i);
        for (std::size_t i = 0; i < seed.span_id.size(); ++i) seed.span_id[i] = std::byte(0x10 + i);
        seed.flags = 0x01;
        cfg.initial_trace_context = seed;
    }

    ~SeamTraceContextResume() override {
        work.reset();
        ioc.stop();
        for (auto& t : threads) t.join();
    }
};

// Test 1 — deterministic structural test (Pattern B from the docstring).
//
// Spawns a NESTED coroutine on a session_executor that wraps a disjoint
// single-thread io_context (via the test_support helper). The nested
// coroutine's body runs entirely on side_thread by construction — single-
// thread io_context, single thread, no scheduling luck — and there
// captures both std::this_thread::get_id() (compared to t0 below) and
// co_await fixpp::current_trace_context() (compared to the outer seed).
// On nested-coroutine completion the outer coroutine resumes back on its
// own associated executor (sess.executor()) and re-reads.
//
// NOTE on why this shape is required: `co_await asio::post(executor,
// use_awaitable)` runs the post completion handler on `executor`, but the
// resume of the awaiting coroutine dispatches via the AWAITABLE's
// associated executor (the one bound at outer co_spawn time, i.e.,
// sess.executor()). So a single `co_await asio::post(side.executor(),...)`
// inside the outer coroutine bounces the resume back to a main-pool
// worker — the trace_context read after that post still happens on a main
// thread, defeating the structural cross-thread assertion. Spawning a
// SEPARATE inner coroutine on side.executor() pins the inner body to
// side_thread because that's the inner awaitable's associated executor.
TEST_F(SeamTraceContextResume, SurvivesExplicitCrossThreadHop) {
    Session sess{engine, cfg};
    auto opened = asio::co_spawn(ioc, sess.open(), asio::use_future);
    ASSERT_TRUE(opened.get().has_value());

    fixpp::test_support::disjoint_session_executor side(sess);
    ASSERT_TRUE(side.valid());

    auto done = asio::co_spawn(
        sess.executor(),
        [&]() -> asio::awaitable<bool> {
            const auto c0 = co_await fixpp::current_trace_context();
            EXPECT_TRUE(eq(c0, seed));
            const auto t0 = std::this_thread::get_id();

            // Nested coroutine on side.executor() — runs entirely on
            // side_thread. The captures (thread id, trace context) cross
            // the cross-thread boundary back as the inner's return value.
            const auto side_tid = co_await asio::co_spawn(
                side.executor(),
                [&]() -> asio::awaitable<std::thread::id> {
                    const auto t = std::this_thread::get_id();
                    // current_trace_context() runs on side_thread, under a
                    // session_executor for &sess → recovers session state.
                    const auto c1 = co_await fixpp::current_trace_context();
                    EXPECT_TRUE(eq(c1, seed));  // contract: survives hop
                    co_return t;
                },
                asio::use_awaitable);

            // Deterministic cross-thread proof: side.thread_id() is a
            // private std::thread of the side single-thread io_context, by
            // construction disjoint from any main pool worker.
            EXPECT_NE(t0, side_tid);
            EXPECT_EQ(side_tid, side.thread_id());

            // Outer coroutine is back on sess.executor() — confirm the
            // session's trace_context still reads correctly here too.
            const auto c2 = co_await fixpp::current_trace_context();
            EXPECT_TRUE(eq(c2, seed));
            EXPECT_TRUE(eq(c2, c0));

            co_return true;
        },
        asio::use_future);

    EXPECT_TRUE(done.get());
}

// Test 2 — realistic-scheduling soft test (Pattern A natural-client-code
// flavor).
//
// Polls current_trace_context() in a sleep_until loop on a 4-thread
// io_context. The asserted property is the only one the library actually
// guarantees: every read is byte-equal to seed. Whether any iteration
// happens to land on a different worker thread depends on asio's worker
// routing AND the host's CPU topology — that is NOT a library contract
// (see gh#79: 2-vCPU GHA runners under gcc-release can route all 200
// 1-ms-tick resumes to a single warm worker), so the test does not assert
// on it.
//
// A `thread_changed` observation is still recorded (telemetry only) via
// RecordProperty so a future operator inspecting per-platform behavior can
// see the distribution without re-running.
TEST_F(SeamTraceContextResume, SurvivesSchedulingResumes) {
    Session sess{engine, cfg};
    auto opened = asio::co_spawn(ioc, sess.open(), asio::use_future);
    ASSERT_TRUE(opened.get().has_value());

    auto thread_changed_f = asio::co_spawn(
        sess.executor(),
        [&]() -> asio::awaitable<bool> {
            const auto captured = co_await fixpp::current_trace_context();
            EXPECT_TRUE(eq(captured, seed));

            const auto t0 = std::this_thread::get_id();
            bool thread_changed = false;
            for (int i = 0; i < 200; ++i) {
                co_await sess.effective_clock()->sleep_until(std::chrono::steady_clock::now() +
                                                             1ms);
                const auto again = co_await fixpp::current_trace_context();
                EXPECT_TRUE(eq(again, seed));  // contract — every iter
                if (std::this_thread::get_id() != t0) thread_changed = true;
            }
            co_return thread_changed;
        },
        asio::use_future);

    // Telemetry only — NOT an assertion. The contract above (per-iter
    // byte-equality) is what matters; the cross-thread observation is a
    // function of asio's routing on the host and is not guaranteed.
    const bool observed = thread_changed_f.get();
    ::testing::Test::RecordProperty("thread_changed_observed", observed ? "true" : "false");
}

}  // namespace
