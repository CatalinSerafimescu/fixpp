// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/core/test_trace_context_resume.cpp — [2d §9.6] Seam 6
//
// Session-domain trace context survives coroutine resume on a DIFFERENT
// thread ([2d §4.6]; I-11): bind a session over a 4-thread io_context,
// populate the session_local<trace_context> slot at open() from
// SessionConfig::initial_trace_context, run a `fromApp` coroutine UNDER the
// bound session_executor, capture `co_await fixpp::current_trace_context()`,
// co_await real sleeps until the coroutine resumes on a different worker
// thread, then re-read and assert BYTE-EQUALITY. The value is recovered
// through the borrowed stable Session* (typed session_executor::session_ptr,
// round-3 RC#1) — NOT thread_local — so a thread hop must not change it.
//
// io_context + manual std::threads (NOT asio::thread_pool) so teardown is
// fully deterministic: the worker threads exit on ioc.stop() before any
// asio internal any_executor type-erasure shared-target ref-counts are
// torn down. The thread_pool teardown path races (TSan) on the
// shared_target_executor<session_executor> ref-count vs the still-running
// worker — a known asio internal type-erasure noise specific to a custom
// executor target. The 2d-owned property (session_local survives cross-
// thread resume) is unaffected; this just keeps the harness TSan-clean.
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <thread>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/core/trace_context.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>

#include "support/minimal_dictionary.hpp"

namespace {

using namespace std::chrono_literals;
using fixpp::core::EngineConfig;
using fixpp::session::Session;
using fixpp::session::SessionConfig;

bool eq(const fixpp::otel::trace_context& a,
        const fixpp::otel::trace_context& b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

TEST(SeamTraceContextResume, SurvivesResumeOnDifferentThread) {
    asio::io_context ioc;
    auto work = asio::make_work_guard(ioc);
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&] { ioc.run(); });
    }

    EngineConfig engine;
    engine.executor = ioc.get_executor();
    engine.clock    = std::make_shared<fixpp::core::system_clock_source>(
        ioc.get_executor());

    SessionConfig cfg;
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary(); // T050
    fixpp::otel::trace_context seed{};
    for (std::size_t i = 0; i < seed.trace_id.size(); ++i)
        seed.trace_id[i] = std::byte(0xA0 + i);
    for (std::size_t i = 0; i < seed.span_id.size(); ++i)
        seed.span_id[i] = std::byte(0x10 + i);
    seed.flags = 0x01;
    cfg.initial_trace_context = seed;

    Session sess{engine, cfg};
    auto opened = asio::co_spawn(ioc, sess.open(), asio::use_future);
    ASSERT_TRUE(opened.get().has_value());

    {
        auto done = asio::co_spawn(
            sess.executor(),    // run UNDER the bound session_executor
            [&]() -> asio::awaitable<bool> {
                const auto captured = co_await fixpp::current_trace_context();
                EXPECT_TRUE(eq(captured, seed));

                const auto t0 = std::this_thread::get_id();
                bool thread_changed = false;
                // Bounded: a strand over 4 worker threads reschedules across
                // them; well under this many 1ms hops it lands elsewhere.
                for (int i = 0; i < 200 && !thread_changed; ++i) {
                    co_await sess.effective_clock()->sleep_until(
                        std::chrono::steady_clock::now() + 1ms);
                    const auto again = co_await fixpp::current_trace_context();
                    EXPECT_TRUE(eq(again, seed));   // identical across resume
                    if (std::this_thread::get_id() != t0) thread_changed = true;
                }
                co_return thread_changed;
            },
            asio::use_future);

        EXPECT_TRUE(done.get());
    }

    // Drain deterministically: release the work guard, stop the io_context,
    // join the worker threads BEFORE any asio internal ref-counts are torn
    // down by main-scope destructors.
    work.reset();
    ioc.stop();
    for (auto& t : threads) t.join();
}

}  // namespace
