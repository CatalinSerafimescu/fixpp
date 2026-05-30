// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/session/engine_acceptor_test.cpp — T008 [P] [US1] Phase 3
//
// TDD RED: acceptor happy-path via the Engine public API.
//
// Scenario (SC-001 / US1 AC1 / C1/C3):
//   Start the engine with an acceptor + initiator registered. The engine's
//   accept loop listens for the initiator connection. Once T011/T012/T013 land:
//     run_accept_loop accepts the connection, completes the handshake, resolves
//     by reversed CompID, attaches via attach_accepted_transport, direct-delivers
//     the first Logon → acceptor gate admits with an on-list live identity →
//     session reaches Active.
//
// RED (stub): run_accept_loop exits immediately after open() without ever
//   calling listener.async_accept(). The acceptor session is opened (open()
//   succeeds on the acceptor path with NotConnected state) but never reaches
//   Active because no peer connection is accepted or processed.
//   Assertion "session reached established within 2s" FAILS.
//
// GREEN (T011/T012/T013): full accept→handshake→resolve→attach→admit wired.
//
// Bounding: ioc.run_for(2s) — no hang on stub path.
// Anchors: tasks.md T008; spec.md US1 AC1 / SC-001; contracts C1/C3;
//          data-model E-2/E-4; research R3/R4/R7.

#include <chrono>
#include <future>
#include <string>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <gtest/gtest.h>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "engine_loopback_harness.hpp"

using namespace std::chrono_literals;
using fixpp::test_support::EngineLoopbackHarness;

// ── SC-001: acceptor admits an on-list live identity to established state ─────

TEST(EngineAcceptorTest, OnListIdentityAdmitsToEstablished) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();

    auto harness = EngineLoopbackHarness::build(
        ioc.get_executor(), std::move(eng_cfg));
    if (!harness) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    harness->engine().start();

    // BOUNDED: run for at most 2s.
    // RED (stub): run_accept_loop exits after open() — it never calls
    //   listener.async_accept() and never accepts the initiator's connection.
    //   The acceptor session is in its initial NotConnected state.
    // GREEN (T011/T012/T013): accept loop runs, accepts, handshakes, resolves,
    //   attaches, delivers first Logon → gate admits → Active.
    ioc.run_for(2s);  // 2s gives T012 enough time to complete a handshake
    ioc.restart();

    // Capture state BEFORE calling stop() — stop() clears the registry,
    // which destroys Session objects, leaving any raw pointer dangling.
    fixpp::session::Session* acc = harness->engine().lookup(harness->acceptor_id());

    // SC-001: the acceptor session must reach established state.
    // RED: acc is non-null (open() runs in stub) but state is NotConnected —
    //   the accept loop returned without ever accepting a peer.
    // GREEN (T011/T012/T013): state is Active (or transitional LogonReceived).
    bool established = (acc != nullptr) &&
        (acc->state() == fixpp::session::fsm_state::Active ||
         acc->state() == fixpp::session::fsm_state::LogonReceived);

    // Capture the state string BEFORE stop() frees the session.
    std::string state_str = (acc != nullptr)
        ? std::to_string(static_cast<int>(acc->state()))
        : "null";

    // Stop cleanly (strict ~Engine() assert(stopped())).
    auto stop_fut = asio::co_spawn(ioc, harness->engine().stop(), asio::use_future);
    ioc.run();
    stop_fut.get();
    // NOTE: acc is now dangling — do NOT access it below this line.

    EXPECT_TRUE(established)
        << "SC-001: the acceptor session must reach Active (or LogonReceived) "
        << "within 2s after an on-list initiator connects over loopback-TLS. "
        << "RED: run_accept_loop stub exits after open() without calling "
        << "listener.async_accept(); session state=" << state_str
        << ". GREEN after T011/T012/T013: full accept→handshake→resolve→"
        << "attach→admit wired; session reaches Active.";
}

// ── Gate A New-3: lookup() returns null before start ─────────────────────────

TEST(EngineAcceptorTest, LookupNullBeforeStart) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();

    auto harness = EngineLoopbackHarness::build(
        ioc.get_executor(), std::move(eng_cfg));
    if (!harness) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    // Before start(): lazy construction — no Session exists yet (Gate A New-3).
    EXPECT_EQ(harness->engine().lookup(harness->acceptor_id()), nullptr)
        << "lookup() must return null for a registered-but-not-yet-started "
        << "session (lazy construction, Gate A New-3).";

    // Stop without start (no loops to join).
    auto stop_fut = asio::co_spawn(ioc, harness->engine().stop(), asio::use_future);
    ioc.run();
    stop_fut.get();
}

// ── C1: acceptor session remains in NotConnected (not Active) when stub runs ──

TEST(EngineAcceptorTest, AcceptorStaysNotConnectedWithStubLoop) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();

    auto harness = EngineLoopbackHarness::build(
        ioc.get_executor(), std::move(eng_cfg));
    if (!harness) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    harness->engine().start();

    // Brief run: only allow the stub to open the session (no peer connects).
    ioc.run_for(200ms);
    ioc.restart();

    fixpp::session::Session* acc = harness->engine().lookup(harness->acceptor_id());

    // Capture state before stop() frees the session.
    bool not_active = (acc == nullptr) ||
        (acc->state() != fixpp::session::fsm_state::Active &&
         acc->state() != fixpp::session::fsm_state::LogonReceived);

    auto stop_fut = asio::co_spawn(ioc, harness->engine().stop(), asio::use_future);
    ioc.run();
    stop_fut.get();

    EXPECT_TRUE(not_active)
        << "The acceptor stub loop must NOT reach Active without a real peer "
        << "connection (no false-admit). GREEN for stub: NotConnected or null.";
}
