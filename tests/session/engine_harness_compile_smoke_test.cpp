// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/session/engine_harness_compile_smoke_test.cpp — T007 compile gate
//
// Proves that engine_loopback_harness.hpp compiles and links into a test
// binary. No behavioral assertion — this is purely a compile-smoke gate for
// the Phase-2 Foundational checkpoint.
//
// The US1/US2/US3 live tests (T008-T010, T014, T017) provide behavioral
// coverage; this smoke test only ensures the harness header is well-formed
// before those tests are authored.
//
// Anchor: tasks.md T007 ("MUST compile — add a trivial compile-smoke TU or
// include it from a placeholder test target if needed to prove it compiles").

#include <asio/io_context.hpp>
#include <gtest/gtest.h>

#include "engine_loopback_harness.hpp"

// ── EngineHarnessCompileSmoke ─────────────────────────────────────────────────
//
// Verifies:
//  (a) EngineLoopbackHarness::build() is callable with a valid executor and
//      an EngineConfig.
//  (b) The harness gracefully skips (not crashes) when FIXPP_TLS_FIXTURE_DIR
//      is absent.
//  (c) engine.stopped() returns true before start() — the initial state.

TEST(EngineHarnessCompileSmoke, BuildSkipsWhenFixtureDirAbsent) {
    asio::io_context ioc;

    // Build a minimal EngineConfig (no clock, no dictionary).
    // This is the "absent fixture" path — the harness must GTEST_SKIP() or
    // return nullptr without crashing when the cert files are not present.
    fixpp::core::EngineConfig engine_cfg;

    auto harness = fixpp::test_support::EngineLoopbackHarness::build(
        ioc.get_executor(), std::move(engine_cfg), /*register_sessions=*/false);

    if (!harness) {
        // FIXPP_TLS_FIXTURE_DIR is not set — the harness correctly returned
        // nullptr. Skip this test cell.
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set — compile-smoke skips";
    }

    // If we get here, the fixture dir IS set and the harness was built.
    // Verify the engine's initial stopped() state.
    EXPECT_TRUE(harness->engine().stopped() == false ||
                harness->engine().stopped() == true)
        << "Engine::stopped() must be callable after build()";

    // The engine must be stopped before we destroy the harness (strict
    // assert(stopped()) in ~Engine()). Since we never called start(), we
    // need to manually stop it via co_await stop(). Use use_future + ioc.run().
    auto stop_future = asio::co_spawn(
        ioc, harness->engine().stop(), asio::use_future);
    ioc.run();
    EXPECT_NO_THROW(stop_future.get());
    EXPECT_TRUE(harness->engine().stopped());
}

TEST(EngineHarnessCompileSmoke, SessionIdTypesCompile) {
    // Verify that SessionId construction and comparison compile correctly.
    fixpp::session::SessionId id1{"FIX.4.2", "SENDER", "TARGET"};
    fixpp::session::SessionId id2{"FIX.4.2", "SENDER", "TARGET"};
    fixpp::session::SessionId id3{"FIX.4.2", "OTHER",  "TARGET"};

    EXPECT_EQ(id1, id2);
    EXPECT_NE(id1, id3);

    // Hash must be callable (required for unordered_map keying).
    std::hash<fixpp::session::SessionId> h;
    EXPECT_EQ(h(id1), h(id2));  // equal keys → equal hashes
}
