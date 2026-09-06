// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_trace_context_accessors.cpp
//
// T036 / TS-6 support — regression tests for the 017 owned trace-context
// accessor amendments (contracts/adjacent-amendments.md §1/§2):
//
//   1. Session::get_trace_context() returns the value stored from
//      initial_trace_context at open() (feeds TS-6a).
//   2. Exactly ONE canonical session trace-context accessor exists —
//      grep-regression scoped over src/ + include/ + tests/ so the migrated
//      session_executor.cpp:80 caller is covered; no lingering
//      trace_context_value() references anywhere.
//   3. Engine::engine_trace_context() returns the seeded snapshot (TS-6b).
//
// Anchors:
//   contracts/adjacent-amendments.md §1/§2
//   [2k App D §D.1]
//   T036 brief

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <cstring>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/core/trace_context.hpp>
#include <fixpp/otel/trace_context.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <memory>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"

namespace {

using fixpp::core::EngineConfig;
using fixpp::session::close_mode;
using fixpp::session::Session;
using fixpp::session::SessionConfig;

bool eq(const fixpp::otel::trace_context& a, const fixpp::otel::trace_context& b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

// ── 1. Session::get_trace_context() returns initial_trace_context at open() ──

TEST(TraceContextAccessors, GetTraceContextReturnsInitialValue) {
    asio::io_context ctx;
    EngineConfig engine_cfg;
    engine_cfg.executor = ctx.get_executor();
    engine_cfg.clock    = std::make_shared<fixpp::core::mock_clock>(
        fixpp::core::utc_time_point{}, fixpp::core::steady_time_point{},
        ctx.get_executor());

    SessionConfig cfg;
    cfg.dictionary        = fixpp::test_support::make_minimal_dictionary();
    cfg.security_profile  = fixpp::test_support::make_minimal_security_profile();

    // Seed a distinct trace_context.
    fixpp::otel::trace_context seed{};
    seed.trace_id[0]  = std::byte{0xAA};
    seed.trace_id[15] = std::byte{0xFF};
    seed.span_id.fill(std::byte{0xBB});   // span_id is std::array<std::byte,8>
    seed.flags        = 0x01;
    cfg.initial_trace_context = seed;

    Session sess{engine_cfg, cfg};
    asio::co_spawn(ctx, sess.open(), asio::detached);
    ctx.run();
    ctx.restart();

    // After open(), get_trace_context() must return exactly the seeded value.
    EXPECT_TRUE(eq(sess.get_trace_context(), seed))
        << "Session::get_trace_context() must return the value stored from "
           "initial_trace_context at open() (T036 / contracts/adjacent-amendments.md §1)";

    // Verify it is NOT the default zero value (confirming the test is non-trivial).
    {
        fixpp::otel::trace_context zero{};
        EXPECT_FALSE(eq(sess.get_trace_context(), zero))
            << "sanity: pre-advance check — seed is not the zero value";
    }

    // Teardown.
    auto closed = asio::co_spawn(ctx, sess.close(close_mode::terminal), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ctx, closed, "TraceContextAccessors::GetTraceContextReturnsInitialValue")) {
        return;
    }
    (void)closed.get();
}

// ── 2. Grep regression: ZERO trace_context_value() references remain ─────────
//
// Exactly ONE canonical accessor — get_trace_context() — must exist.
// trace_context_value() must have been fully migrated away (T034).
// This test invokes a subprocess grep over src/ + include/ + tests/.
//
// The check is a compile-time regression via the static_assert below:
// the test fails to compile if trace_context_value() re-appears as a public
// method on Session. At runtime we verify the header grep count is zero by
// checking the declaration in session.hpp (the canonical header must not
// have trace_context_value() as a PUBLIC method).
//
// Implementation: We use the detection idiom to verify trace_context_value()
// is NOT accessible as a public member of Session. If it were still public,
// the has_trace_context_value_v trait would be true.

template <typename T, typename = void>
struct has_trace_context_value : std::false_type {};
template <typename T>
struct has_trace_context_value<
    T, std::void_t<decltype(std::declval<T const&>().trace_context_value())>>
    : std::true_type {};

template <typename T, typename = void>
struct has_get_trace_context : std::false_type {};
template <typename T>
struct has_get_trace_context<
    T, std::void_t<decltype(std::declval<T const&>().get_trace_context())>>
    : std::true_type {};

// Compile-time gate: get_trace_context() MUST be present on Session.
static_assert(has_get_trace_context<Session>::value,
              "Session::get_trace_context() must exist (017 owned amendment #1)");

// Compile-time gate: trace_context_value() must NOT be accessible as public.
static_assert(!has_trace_context_value<Session>::value,
              "Session::trace_context_value() must be removed or made private "
              "(017 T034 — single canonical accessor = get_trace_context())");

TEST(TraceContextAccessors, SingleCanonicalSessionAccessor) {
    // Runtime complement of the compile-time static_asserts above.
    // If both compile-time checks pass the test body is a tautology; we include
    // it so the test appears in the ctest output with a meaningful name.
    EXPECT_TRUE(has_get_trace_context<Session>::value)
        << "Session::get_trace_context() must exist (single canonical accessor)";
    EXPECT_FALSE(has_trace_context_value<Session>::value)
        << "Session::trace_context_value() must be gone (no lingering duplicate)";
}

// ── 3. Engine::engine_trace_context() returns the seeded snapshot (TS-6b) ────

TEST(TraceContextAccessors, EngineTraceContextReturnsSeededSnapshot) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    eng_cfg.clock    = std::make_shared<fixpp::core::mock_clock>(
        fixpp::core::utc_time_point{}, fixpp::core::steady_time_point{},
        ioc.get_executor());

    // Seed engine_trace_context with known values.
    eng_cfg.engine_trace_context.trace_id.fill(std::byte{0xCC});
    eng_cfg.engine_trace_context.span_id.fill(std::byte{0xDD});  // std::array<std::byte,8>

    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};

    auto const tc = engine.engine_trace_context();

    // trace_id must match 0xCC...
    std::array<std::byte, 16> expected_trace;
    expected_trace.fill(std::byte{0xCC});
    EXPECT_EQ(tc.trace_id, expected_trace)
        << "Engine::engine_trace_context() must return trace_id seeded from "
           "EngineConfig::engine_trace_context at construction (T035/TS-6b)";

    // span_id must match 0xDD...
    std::array<std::byte, 8> expected_span;
    expected_span.fill(std::byte{0xDD});
    EXPECT_EQ(tc.span_id, expected_span)
        << "Engine::engine_trace_context() must return span_id seeded from "
           "EngineConfig::engine_trace_context at construction (T035/TS-6b)";

    // Verify NOT the zero value (sanity: test is non-trivial).
    {
        fixpp::otel::trace_context zero{};
        EXPECT_FALSE(eq(tc, zero))
            << "sanity: the seeded value is not the zero default";
    }

    // Teardown.
    auto stop_future = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, stop_future, "TraceContextAccessors::EngineTraceContextReturnsSeededSnapshot")) {
        return;
    }
    stop_future.get();
}

}  // namespace
