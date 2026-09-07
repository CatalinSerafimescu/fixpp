// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 fixpp contributors
//
// tests/session/test_session_no_implicit_insecure_plain_tcp.cpp — T018 [043 US2]
//
// SC-002 / spec.md FR-006: insecure_plain_tcp is NEVER selected implicitly.
// A default-constructed SecurityProfile (kind::unset) is rejected at
// Session::open() with error::invalid_session_config — the session does NOT
// fall through to the plaintext path silently.
//
// Discrimination: the test seeds a valid EngineConfig with a
// make_asio_plain_transport_factory() as the default_transport_factory.
// If open() incorrectly auto-derived the plaintext path for an unset profile,
// it would succeed (not return invalid_session_config). The test therefore
// MUST return error::invalid_session_config in both cells — confirming the
// unset-reject fires BEFORE any factory/transport resolution.
//
// Cells:
//   Cell 1 — default-constructed SessionConfig (no security_profile set):
//     Session::open() MUST return error::invalid_session_config (not succeed).
//   Cell 2 — explicit kind::unset with a plaintext-capable default factory:
//     Session::open() MUST still return error::invalid_session_config, proving
//     the plaintext factory does NOT cause an implicit plaintext fallback.
//
// Anchors: spec.md SC-002 / FR-006; [const §XII.5]; research.md D-6; [043 T018]

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/transport/transport_factory.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/pump_until_ready.hpp"

namespace {

using fixpp::core::error;
using fixpp::session::SecurityProfile;
using fixpp::session::Session;
using fixpp::session::SessionConfig;

// ── Cell 1: default-constructed config (no security_profile set) ──────────
// SessionConfig{}.security_profile.k == kind::unset → open() MUST reject.
// The rejection MUST happen before any transport/factory resolution —
// proving insecure_plain_tcp is not selected implicitly. [SC-002 / D-6]
TEST(NoImplicitInsecurePlainTcp, DefaultConstructedConfigRejected) {
    asio::io_context ioc;
    fixpp::core::EngineConfig engine;
    engine.executor = ioc.get_executor();
    engine.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

    SessionConfig cfg;
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    // security_profile intentionally NOT set → kind::unset sentinel

    Session s{engine, cfg};
    auto result = asio::co_spawn(ioc, s.open(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, result, "NoImplicitInsecurePlainTcp::DefaultConstructedConfigRejected")) {
        return;
    }
    auto val = result.get();

    ASSERT_FALSE(val.has_value())
        << "SC-002: Session::open() must reject default-constructed (kind::unset) "
           "security_profile — insecure_plain_tcp must NOT be selected implicitly";
    EXPECT_EQ(val.error(), error::invalid_session_config)
        << "Expected error::invalid_session_config (slot 53) for kind::unset; "
           "if this succeeded, open() silently selected insecure_plain_tcp";
}

// ── Cell 2: explicit kind::unset + plaintext-capable default factory ───────
// Even when the engine's default_transport_factory is a plaintext factory,
// a kind::unset profile MUST still be rejected. There is no implicit fallback
// to the plaintext path. [SC-002 / D-6 step 1 (unset-reject fires before step 2)]
TEST(NoImplicitInsecurePlainTcp, ExplicitUnsetWithPlaintextFactoryRejected) {
    asio::io_context ioc;
    fixpp::core::EngineConfig engine;
    engine.executor = ioc.get_executor();
    engine.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());
    // Install a plaintext factory as the engine default. If open() resolved
    // this for an unset profile, it would succeed — but it must not.
    auto plain_factory = fixpp::transport::make_asio_plain_transport_factory(
        fixpp::transport::Transport::Config{});
    ASSERT_TRUE(plain_factory.has_value()) << "make_asio_plain_transport_factory failed";
    engine.default_transport_factory = std::move(*plain_factory);

    SessionConfig cfg;
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    cfg.security_profile = SecurityProfile{SecurityProfile::kind::unset};

    Session s{engine, cfg};
    auto result = asio::co_spawn(ioc, s.open(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, result, "NoImplicitInsecurePlainTcp::ExplicitUnsetWithPlaintextFactoryRejected")) {
        return;
    }
    auto val = result.get();

    ASSERT_FALSE(val.has_value())
        << "SC-002: kind::unset security_profile MUST be rejected even when the "
           "engine's default factory is a plaintext factory — there is no implicit "
           "fallback to insecure_plain_tcp";
    EXPECT_EQ(val.error(), error::invalid_session_config)
        << "Expected error::invalid_session_config (slot 53); "
           "a success here means open() silently selected the plaintext path";
}

}  // namespace
