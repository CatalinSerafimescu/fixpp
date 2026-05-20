// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/core/test_trace_context_engine_fallback.cpp
//
// gate-b/r1 RC#2 seam: FR-015 outside-session fallback path (P1.2→P2).
//
// Asserts that `co_await fixpp::current_trace_context()` on a plain
// `asio::io_context` executor (no `session_executor` wrap) returns a
// default-constructed `fixpp::otel::trace_context` and does NOT throw.
//
// Design context (D-17 / FR-015): 007 ships no `Engine` type; the engine-
// snapshot atomic read is the downstream Engine's wiring. The miss path on
// a non-session-executor correctly returns a default-constructed trace_context.
// This seam pins the "returns default when no session_executor" branch at the
// documented `D-17` scoping so the path is not accidentally broken by later
// Engine wiring.
#include <gtest/gtest.h>

#include <cstring>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <fixpp/core/trace_context.hpp>

namespace {

bool is_all_zero(const fixpp::otel::trace_context& ctx) {
    fixpp::otel::trace_context zero{};
    return std::memcmp(&ctx, &zero, sizeof(ctx)) == 0;
}

// Outside-session fallback: plain io_context executor (no session_executor
// wrap) → current_trace_context() returns a default-constructed trace_context.
// Non-throwing per [arch §5.3] (no exceptions across the parse→fromApp window;
// this path is equally exception-free because it never has a session to fail).
TEST(SeamTraceContextEngineFallback, PlainExecutorReturnsDefaultTraceContext) {
    asio::io_context ioc;

    auto fut = asio::co_spawn(
        ioc.get_executor(),
        []() -> asio::awaitable<fixpp::otel::trace_context> {
            // A plain io_context executor — NOT a session_executor.
            // current_trace_context() misses the session_executor target cast
            // and falls back to the D-17 default path.
            co_return co_await fixpp::current_trace_context();
        },
        asio::use_future);

    ioc.run();

    const auto ctx = fut.get();
    EXPECT_TRUE(is_all_zero(ctx))
        << "current_trace_context() outside any session domain (no "
           "session_executor on the awaiter) MUST return a default-constructed "
           "trace_context (D-17 / FR-015 / gate-b/r1 RC#2)";
}

// Verify the call does not throw even when the awaiter is a bare executor.
// Tested separately to make the no-exception property explicit.
TEST(SeamTraceContextEngineFallback, PlainExecutorDoesNotThrow) {
    asio::io_context ioc;
    bool threw = false;

    auto fut = asio::co_spawn(
        ioc.get_executor(),
        [&threw]() -> asio::awaitable<void> {
            try {
                [[maybe_unused]] auto ctx =
                    co_await fixpp::current_trace_context();
            } catch (...) {
                threw = true;
            }
            co_return;
        },
        asio::use_future);

    ioc.run();
    fut.get();  // propagate any exception from the coroutine infrastructure

    EXPECT_FALSE(threw)
        << "current_trace_context() must NOT throw on a plain executor";
}

}  // namespace
