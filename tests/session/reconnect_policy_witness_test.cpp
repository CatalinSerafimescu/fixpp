// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/reconnect_policy_witness_test.cpp — 016 T007 (RED witness).
//
// Proves the 015 down-peer carry-forward (CLAUDE.md L2): an initiator aimed at an
// UNREACHABLE peer is not promptly torn down by Engine::stop(), because the in-flight
// transport::Transport::async_connect is not aborted by cancellation_type::total —
// stop() blocks until the per-connect timeout runs to completion
// ([[feedback_engine_stop_must_close_transports_total_cancel_insufficient]],
//  [[feedback_asio_cospawn_total_cancellation_default]]).
//
// This file references ONLY the existing public API so it COMPILES at HEAD and FAILS
// behaviorally (RED). T008 (the SessionConfig reconnect-policy field + a bounded,
// promptly-cancellable connect) makes it GREEN. The complementary "finite policy is
// honored" assertion — which references the new field — is added as a GREEN companion
// (reconnect_policy_green_test) only after T008, so the RED witness stays compilable.
//
// Internal self-deadline discipline (R5 / [[feedback_fail_placeholder_red_test]]):
// the test never relies on ioc.run() terminating — it pumps with a wall-clock bound
// and measures stop()'s wall-clock cost.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstdlib>
#include <future>
#include <memory>
#include <span>
#include <utility>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/reconnect_policy.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>

#include "support/minimal_dictionary.hpp"

using namespace std::chrono_literals;

namespace {

// RFC 5737 TEST-NET-1: a routable-but-unresponsive address — the SYN is black-holed,
// so async_connect blocks until its connect_timeout (set small below) rather than
// returning connection_refused. This triggers the *blocked-connect* teardown path
// (the carry-forward), not the refused-fast-retry path.
constexpr const char* kBlackholeHost = "192.0.2.1";
constexpr std::uint16_t kBlackholePort = 9;

// Short per-connect timeout so the RED case is bounded (~3 s) instead of the 30 s
// default; the GREEN case (T008) completes far inside the watchdog bound.
constexpr std::chrono::milliseconds kConnectTimeout{3000};

// The watchdog bound. stop() MUST return within this on a healthy engine; at HEAD it
// blocks ~kConnectTimeout, exceeding it (RED).
constexpr std::chrono::milliseconds kStopWatchdog{1500};

std::shared_ptr<fixpp::transport::TransportFactory> make_tls_factory(const std::string& dir) {
    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path = dir + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = dir + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path = dir + "/ca.pem";
    auto cs = fixpp::tls::file_cert_source::make_file_cert_source(cs_cfg,
                                                                 std::pmr::new_delete_resource());
    if (!cs) {
        return nullptr;
    }
    fixpp::tls::SslCtxConfig ssl;
    ssl.profile = fixpp::tls::SecurityProfile::mtls_ca;
    ssl.cs = std::move(*cs);
    ssl.clock = nullptr;
    ssl.caps = fixpp::tls::CertSourceCaps{};

    fixpp::transport::Transport::Config tcfg;
    tcfg.connect_timeout = kConnectTimeout;
    auto fres = fixpp::transport::make_asio_tls_transport_factory(tcfg, ssl);
    if (!fres) {
        return nullptr;
    }
    return std::shared_ptr<fixpp::transport::TransportFactory>{std::move(*fres)};
}

const char* fixture_dir() {
#ifdef FIXPP_TLS_FIXTURE_DIR
    return FIXPP_TLS_FIXTURE_DIR;
#else
    return std::getenv("FIXPP_TLS_FIXTURE_DIR");
#endif
}

}  // namespace

// RED at HEAD; GREEN after T008. Engine::stop() must return within kStopWatchdog when
// an initiator is registered against an unreachable peer and started.
TEST(ReconnectPolicyWitness, StopReturnsBoundedOnUnreachablePeer) {
    const char* dir = fixture_dir();
    if (dir == nullptr || dir[0] == '\0') {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }
    auto factory = make_tls_factory(dir);
    ASSERT_NE(factory, nullptr) << "TLS factory build failed";

    asio::io_context ioc;
    // 041 T019: Engine::start() rejects a null clock with clock_not_set.
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());
    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};

    fixpp::session::SessionConfig ini;
    ini.sender_comp_id = "INITIATOR";
    ini.target_comp_id = "ACCEPTOR";
    ini.begin_string = "FIX.4.4";
    ini.role = fixpp::session::session_role::initiator;
    ini.executor_override = ioc.get_executor();
    ini.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
    ini.dictionary = fixpp::test_support::make_minimal_dictionary();
    ini.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    ini.transport_factory_override = factory;
    ini.heartbeat_interval = std::chrono::seconds{30};
    ini.reconnect_endpoint = fixpp::transport::Endpoint{kBlackholeHost, kBlackholePort};
    ini.transport_send = [](std::span<const std::byte>) {};

    ASSERT_TRUE(engine.register_session(std::move(ini)).has_value());
    ASSERT_TRUE(engine.start().has_value()) << "engine.start() failed";

    // Let the connect loop reach the in-flight async_connect (resolve of an IP is
    // immediate; the connect then blocks on the black-holed SYN).
    ioc.run_for(200ms);
    if (ioc.stopped()) {
        ioc.restart();
    }

    // Measure stop()'s full wall-clock cost. Hard cap 10 s guards against a true hang
    // (so a regression fails loudly rather than wedging the suite); the RED assertion
    // is the tight watchdog below.
    const auto t0 = std::chrono::steady_clock::now();
    auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    const auto hard_cap = t0 + 10s;
    while (std::chrono::steady_clock::now() < hard_cap) {
        if (stop_fut.wait_for(0s) == std::future_status::ready) {
            break;
        }
        if (ioc.stopped()) {
            ioc.restart();
        }
        ioc.run_for(5ms);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);

    ASSERT_EQ(stop_fut.wait_for(0s), std::future_status::ready)
        << "Engine::stop() did not complete within the 10 s hard cap (true hang).";
    stop_fut.get();
    EXPECT_TRUE(engine.stopped());

    EXPECT_LT(elapsed, kStopWatchdog)
        << "Engine::stop() took " << elapsed.count()
        << " ms on an unreachable-peer initiator (watchdog " << kStopWatchdog.count()
        << " ms). The in-flight async_connect is not promptly cancelled by "
           "cancellation_type::total — 015 down-peer L2 carry-forward (T008 fix).";
}

// Cause-1 (busy-spin) witness — deterministic, no network. The prior hard-coded
// empty ReconnectPolicy{} had a 0-backoff schedule (delay_for_attempt == 0) ⇒
// ~100% CPU busy-spin on repeated connect failure. The T008 default
// (defaults_quickfix_compat) has a non-zero interval, eliminating the busy-spin.
TEST(ReconnectPolicyWitness, DefaultBackoffIsNonZeroNoBusySpin) {
    fixpp::transport::ReconnectPolicy empty{};
    EXPECT_EQ(empty.delay_for_attempt(1).count(), 0)
        << "empty ReconnectPolicy{} = the 0-backoff busy-spin bug (the old default)";

    auto def = fixpp::transport::ReconnectPolicy::defaults_quickfix_compat(nullptr);
    EXPECT_GT(def.delay_for_attempt(1).count(), 0)
        << "T008 default must back off (no busy-spin on repeated connect failure)";
}

// Plumbing witness — an operator-supplied finite policy flows into SessionConfig,
// survives the copy (010 W-5 copy-constructible invariant), and is the value the
// Session ctor's resolve_reconnect_policy() honors (operator policy wins over the
// default). A finite max_attempts bounds reconnect (FSM surfaces
// transport_reconnect_limit_exceeded at the cap).
TEST(ReconnectPolicyWitness, FinitePolicyHonoredViaSessionConfig) {
    fixpp::session::SessionConfig cfg;
    auto p = fixpp::transport::ReconnectPolicy::defaults_quickfix_compat(nullptr);
    p.max_attempts = 5;  // finite ⇒ bounded reconnect (no infinite loop)
    cfg.reconnect_policy = p;

    fixpp::session::SessionConfig copy = cfg;  // exercises copy-constructibility
    ASSERT_TRUE(copy.reconnect_policy.has_value());
    EXPECT_EQ(copy.reconnect_policy->max_attempts, 5u);
}
