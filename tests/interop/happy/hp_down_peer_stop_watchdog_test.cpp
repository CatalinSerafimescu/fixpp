// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_down_peer_stop_watchdog_test.cpp — 016 T016 [US1].
//
// FR-028 down-peer regression cell — a SEPARATE scenario, deliberately NOT in the
// happy-path matrix and NOT counterparty-paired: it proves Engine::stop() returns
// within a stated bound when a fixpp initiator is aimed at a peer that never
// accepts. This is the interop-suite guard for the 015 down-peer L2 carry-forward
// (CLAUDE.md L2), discharged by the Phase-2 T008 fix (SessionConfig reconnect
// policy + promptly-cancellable in-flight connect).
//
// Unlike the matrix cells this runs WITHOUT a counterparty (the point is a peer
// that never answers), so it executes green locally. The watchdog IS the
// assertion — it must never silently pass on a hang ([[feedback_fail_placeholder_red_test]]:
// stop_within() has an internal wall-clock bound, never relies on ioc.run() ending).
//
// spec_ref [FIX-SL §4.4 reconnect] + FR-004/FR-028.
//
// [const §XV.9]: tests/-only.

#include <gtest/gtest.h>

#include <chrono>

#include <fixpp/session/engine.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/reconnect_policy.hpp>

#include "hp_support.hpp"

using namespace std::chrono_literals;
using fixpp::interop::Role;

namespace {

// RFC 5737 TEST-NET-1: routable but unresponsive — the SYN is black-holed, so the
// connect blocks until its timeout rather than returning connection_refused. This
// is the blocked-connect teardown path the carry-forward is about.
constexpr const char* kBlackholeHost = "192.0.2.1";
constexpr std::uint16_t kBlackholePort = 9;

// The watchdog bound. With the T008 fix, total-cancel promptly tears down the
// in-flight connect, so stop() returns well inside this.
constexpr std::chrono::milliseconds kStopWatchdog{1500};

TEST(DownPeerWatchdog, StopReturnsBoundedOnNeverAcceptingPeer) {
    namespace hp = fixpp::interop::hp;
    const char* dir = hp::tls_fixture_dir();
    if (dir == nullptr || dir[0] == '\0') {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }
    auto factory = hp::make_interop_tls_factory(dir);
    ASSERT_NE(factory, nullptr) << "baseline TLS factory build failed";

    fixpp::interop::InteropEngineFixture fx;
    auto cfg = hp::make_session_config(
        Role::fixpp_initiator, "FIX.4.4", factory, fx.ioc().get_executor(),
        fixpp::transport::Endpoint{kBlackholeHost, kBlackholePort});

    // Finite reconnect policy (FR-004): bounded attempts + non-zero backoff so a
    // repeated connect failure cannot busy-spin (the cause-1 half of the L2 bug).
    auto policy = fixpp::transport::ReconnectPolicy::defaults_quickfix_compat(nullptr);
    policy.max_attempts = 3;
    cfg.reconnect_policy = policy;

    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value());

    fx.start();
    // Let the connect loop reach the in-flight async_connect (the predicate never
    // becomes true → pumps the full 200 ms window).
    fx.run_until([] { return false; }, 200ms);

    // The assertion IS the watchdog (FR-028): stop() must complete within the
    // bound. stop_within() returns the measured wall-clock; >= bound means a hang.
    const auto elapsed = fx.stop_within(kStopWatchdog);
    EXPECT_LT(elapsed, kStopWatchdog)
        << "Engine::stop() took " << elapsed.count()
        << " ms on a never-accepting peer (watchdog " << kStopWatchdog.count()
        << " ms) — 015 down-peer L2 carry-forward / T008 fix regression.";
    EXPECT_TRUE(fx.stopped()) << "engine did not reach stopped()";
}

}  // namespace
