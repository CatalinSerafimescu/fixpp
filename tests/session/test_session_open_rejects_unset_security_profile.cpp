// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_session_open_rejects_unset_security_profile.cpp
//
// gate-b/r1 RC#1 seam: Session::open() MUST reject the default-constructed
// SecurityProfile sentinel (kind::unset) with error::invalid_session_config
// (slot 53 / FR-018 / N-P2-3 / [const §XII.5] / [2d §4.5]).
//
// The minimal-stub pattern (D-15 / D-21 amended): SecurityProfile ships as a
// complete value type with a sentinel discriminant (kind::unset). 2g extends
// with the concrete TLS binding; this seam pins the constitutional no-implicit-
// default rule shipped in this PR.
//
// Two halves:
//   1. Default-constructed SecurityProfile (kind::unset) + valid dictionary →
//      Session::open() returns error::invalid_session_config.
//   2. Non-sentinel SecurityProfile (kind::mtls_ca) + valid dictionary →
//      Session::open() succeeds.
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/security_profile.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

namespace {

using fixpp::core::EngineConfig;
using fixpp::core::error;
using fixpp::session::Session;
using fixpp::session::SessionConfig;

static std::shared_ptr<fixpp::core::mock_clock> make_clock(
        asio::any_io_executor ex) {
    return std::make_shared<fixpp::core::mock_clock>(
        fixpp::core::utc_time_point{},
        fixpp::core::steady_time_point{},
        std::move(ex));
}

// ── Part 1: Default-constructed sentinel rejected ──────────────────────────
// SessionConfig{} has security_profile.k == kind::unset.
// Session::open() MUST return error::invalid_session_config (slot 53).
TEST(SeamSessionOpenRejectsUnsetSecurityProfile, DefaultSentinelRejected) {
    asio::io_context ioc;
    EngineConfig engine;
    engine.executor = ioc.get_executor();
    engine.clock    = make_clock(ioc.get_executor());

    SessionConfig cfg;
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary(); // T050
    // security_profile NOT set → default kind::unset sentinel

    Session s{engine, cfg};
    auto result = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run();
    auto val = result.get();

    ASSERT_FALSE(val.has_value())
        << "Session::open() must reject default-constructed SecurityProfile "
           "(kind::unset) per [const §XII.5] / FR-018 / N-P2-3";
    EXPECT_EQ(val.error(), error::invalid_session_config)
        << "Expected error::invalid_session_config (slot 53) for "
           "kind::unset security_profile";
}

// Explicit kind::unset also rejected.
TEST(SeamSessionOpenRejectsUnsetSecurityProfile, ExplicitUnsetRejected) {
    asio::io_context ioc;
    EngineConfig engine;
    engine.executor = ioc.get_executor();
    engine.clock    = make_clock(ioc.get_executor());

    SessionConfig cfg;
    cfg.dictionary       = fixpp::test_support::make_minimal_dictionary();
    cfg.security_profile = fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::unset};

    Session s{engine, cfg};
    auto result = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run();
    auto val = result.get();

    ASSERT_FALSE(val.has_value());
    EXPECT_EQ(val.error(), error::invalid_session_config);
}

// ── Part 2: Non-sentinel SecurityProfile accepted ─────────────────────────
// kind::mtls_ca (and other non-unset variants) must NOT be rejected.
TEST(SeamSessionOpenRejectsUnsetSecurityProfile, NonSentinelAccepted) {
    for (auto k : {fixpp::session::SecurityProfile::kind::mtls_ca,
                   fixpp::session::SecurityProfile::kind::mtls_pinned,
                   fixpp::session::SecurityProfile::kind::one_way_ca}) {
        asio::io_context ioc;
        EngineConfig engine;
        engine.executor = ioc.get_executor();
        engine.clock    = make_clock(ioc.get_executor());

        SessionConfig cfg;
        cfg.dictionary       = fixpp::test_support::make_minimal_dictionary();
        cfg.security_profile = fixpp::session::SecurityProfile{k};

        Session s{engine, cfg};
        auto result = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run();
        auto val = result.get();

        EXPECT_TRUE(val.has_value())
            << "Session::open() must NOT reject SecurityProfile with "
               "non-sentinel kind value " << static_cast<int>(k);
    }
}

}  // namespace
