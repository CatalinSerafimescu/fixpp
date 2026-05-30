// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/session/engine_acceptor_failclosed_test.cpp — T009 [P] [US1] Phase 3
//
// TDD RED: acceptor fail-closed paths via the Engine public API.
//
// Scenarios (SC-002 / US1 AC2 / C1 invariant / E-4):
//
//   Cell A: OFF-LIST identity → fail-CLOSED.
//     Once T011/T012/T013 land:
//       accept loop accepts, handshakes, resolves, attaches, delivers first
//       Logon → acceptor gate arm (1-live) fires → off-list → Disconnected +
//       session_event_compid_authorization_failed.
//     RED: stub never accepts → no authorization event emitted, session never
//       goes to Disconnected via the authorization path.
//
//   Cell B: ABSENT/DELAYED identity — happens-before regression (Gate A New-1).
//     The live_peer_id_ MUST be set strictly-happens-before the first
//     on_inbound_frame reaches the acceptor gate (C1 invariant / E-4).
//     Under mTLS without a live identity the gate MUST fall to arm (2)
//     fail-CLOSED — never admit.
//     RED: stub never accepts → the happens-before path is not exercised.
//
// Bounding: ioc.run_for(2s) — prevents hang.
// IMPORTANT: all session state/event captures MUST happen BEFORE calling stop().
//   stop() clears the registry (destroys Session objects) — raw pointers dangle.
// Anchors: tasks.md T009; spec.md SC-002 / US1 AC2; contracts C1/C3/E-4;
//          data-model E-4; [[feedback_simplify_pass_catches_9th_burn]].

#include <algorithm>
#include <chrono>
#include <future>
#include <variant>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <gtest/gtest.h>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "engine_loopback_harness.hpp"
#include "support/minimal_dictionary.hpp"

using namespace std::chrono_literals;
using fixpp::test_support::EngineLoopbackHarness;

namespace {

// Check whether a session has emitted a compid_authorization_failed event.
static bool has_authz_failed_event(const fixpp::session::Session& s) {
    auto evs = s.recent_events();
    return std::any_of(evs.begin(), evs.end(),
        [](const fixpp::session::SessionEvent& ev) {
            return std::holds_alternative<
                fixpp::session::session_event_compid_authorization_failed>(ev);
        });
}

}  // namespace

// ── Cell A: off-list identity → fail-CLOSED ───────────────────────────────────

TEST(EngineAcceptorFailClosedTest, OffListIdentityFailsClosed) {
    // Start engine with default harness (mTLS, loopback TLS fixture).
    // The default harness does NOT set an allow-list policy for the acceptor,
    // so any connecting peer is off-list.
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
    // RED: stub loop exits after open(), never calls async_accept(), so no
    //   peer is ever processed — no authorization event is ever emitted.
    ioc.run_for(2s);
    ioc.restart();

    fixpp::session::Session* acc = harness->engine().lookup(harness->acceptor_id());

    // Capture state BEFORE stop() frees the sessions.
    bool auth_failed_emitted = (acc != nullptr) && has_authz_failed_event(*acc);
    bool session_disconnected = (acc != nullptr) &&
        (acc->state() == fixpp::session::fsm_state::Disconnected);

    // Stop cleanly. NOTE: acc is dangling after stop_fut.get().
    auto stop_fut = asio::co_spawn(ioc, harness->engine().stop(), asio::use_future);
    ioc.run();
    stop_fut.get();

    // SC-002: when an off-list identity connects, the acceptor gate MUST emit
    // session_event_compid_authorization_failed AND the session MUST be in
    // Disconnected state.
    // RED: event was never emitted (stub loop never accepted a peer).
    EXPECT_TRUE(auth_failed_emitted)
        << "SC-002: session_event_compid_authorization_failed must be emitted "
        << "when an off-list identity tries to connect (C3 / FR-008). "
        << "RED: stub accept loop never accepts a peer — event never emitted. "
        << "GREEN after T011/T012/T013: live acceptor path wired with off-list "
        << "identity; gate arm (1-live) fires → fail-CLOSED → event emitted.";

    EXPECT_TRUE(session_disconnected)
        << "SC-002: acceptor session must reach Disconnected after off-list "
        << "identity fails authorization (FR-007 / C3). "
        << "RED: stub loop doesn't accept; session in non-Disconnected state. "
        << "GREEN after T011/T012/T013: gate emits Disconnected.";
}

// ── Cell B: absent/delayed identity — happens-before regression (Gate A New-1)

TEST(EngineAcceptorFailClosedTest, AbsentIdentityNeverAdmits) {
    // The acceptor live-identity gate arm (C3) MUST use the live_peer_id_
    // set by attach_accepted_transport (T011). The happens-before invariant
    // (C1 / E-4) requires live_peer_id_ to be set BEFORE the first
    // on_inbound_frame reaches the gate. If live_peer_id_ is absent under mTLS,
    // the gate MUST fall to arm (2) fail-CLOSED — never admit.
    //
    // RED behavioral anchor: with stub loop, no connection is accepted, so no
    // identity binding can happen. We assert that the acceptor session (if open)
    // does NOT reach Active state — it must fail CLOSED, not silently admit.
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();

    auto harness = EngineLoopbackHarness::build(
        ioc.get_executor(), std::move(eng_cfg));
    if (!harness) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    harness->engine().start();

    // BOUNDED: 2s deadline.
    ioc.run_for(2s);
    ioc.restart();

    fixpp::session::Session* acc = harness->engine().lookup(harness->acceptor_id());

    // Capture state BEFORE stop().
    // Under mTLS with no peer identity delivered (absent/delayed), the acceptor
    // gate MUST NOT admit. The session must never reach Active without a
    // live_peer_id_ being set first (E-4).
    bool never_admitted = (acc == nullptr) ||
        (acc->state() != fixpp::session::fsm_state::Active);

    // Capture auth-failed event BEFORE stop().
    // In a GREEN world with T012 wired but identity deliberately delayed:
    // the gate falls to arm (2) → session_event_compid_authorization_failed.
    bool auth_failed = (acc != nullptr) && has_authz_failed_event(*acc);

    // Stop cleanly.
    auto stop_fut = asio::co_spawn(ioc, harness->engine().stop(), asio::use_future);
    ioc.run();
    stop_fut.get();

    // Absent identity must not silently admit (happens-before invariant).
    // RED: with stub loop, session is not active (open() leaves it NotConnected),
    //   which satisfies never_admitted = true — but this cell's TRUE RED fires
    //   once T012 wires the accept loop with a delayed identity injection test.
    // GREEN (T011/T012/T013): the full live-identity accept path is wired;
    //   injecting a withheld identity asserts Disconnected + event.
    EXPECT_TRUE(never_admitted)
        << "Happens-before invariant (Gate A New-1 / E-4): an acceptor session "
        << "under mTLS with no live_peer_id_ set MUST NOT reach Active. "
        << "RED (T011/T012/T013 not wired): no connection accepted by stub; "
        << "once T012 wires the accept loop, a test with delayed peer_id must "
        << "verify the gate falls to arm (2) fail-CLOSED, never arm (1-live). "
        << "GREEN after T011/T012/T013: the full live-identity accept path is "
        << "wired; inject a withheld identity → assert Disconnected + event.";

    // Informational: the happens-before regression gate must also emit the
    // authorization_failed event when the absent-identity arm (2) fires.
    // For now this is RED because stub never accepts; TRUE RED fires in T012.
    EXPECT_TRUE(auth_failed)
        << "Happens-before regression (Gate A New-1): when live_peer_id_ is "
        << "absent under mTLS, arm (2) fail-CLOSED must emit "
        << "session_event_compid_authorization_failed. "
        << "RED: stub never accepts → event never emitted. "
        << "GREEN (T012+T013): accept loop runs; absent-identity → arm (2) "
        << "fires → Disconnected + event.";
}
