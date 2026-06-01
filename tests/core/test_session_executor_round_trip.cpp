// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/core/test_session_executor_round_trip.cpp — [2d §9.19] Seam 19
//
// session_executor round-trip across BOTH threading modes via
// make_session_executor(...), INCLUDING:
//   • the SINGLE error::executor_not_serialised enforcement point
//     ([2d §4.8]:996 / FR-009 / I-06);
//   • the N5 open-path arm (data-model.md N5): a second Session::open() on
//     the same handle → error::session_already_open (slot 51 / FR-018).
//
// The open-time effective_clock resolution (I-03) and session_local-slot
// population (FR-014) arms of N5 are appended to THIS file by T030 (US2) and
// T045 (US4) respectively — they are not yet asserted here (US1 scope).
#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/session_executor.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

namespace {

using fixpp::core::EngineConfig;
using fixpp::core::error;
using fixpp::core::make_session_executor;
using fixpp::session::Session;
using fixpp::session::SessionConfig;
using fixpp::session::threading_mode;

TEST(SeamSessionExecutorRoundTrip, PerSessionStrandWrapsAndRoundTrips) {
    asio::thread_pool pool{2};
    auto se = make_session_executor(pool.get_executor(), threading_mode::per_session_strand,
                                    /*attested=*/false, nullptr);
    ASSERT_TRUE(se.has_value());
    EXPECT_TRUE(se->is_strand_wrapped());
    EXPECT_EQ(se->session_ptr(), nullptr);
    // round-trips through copy preserving the typed Session* + discriminator
    fixpp::core::session_executor copy = *se;
    EXPECT_EQ(copy.session_ptr(), se->session_ptr());
    EXPECT_TRUE(copy.is_strand_wrapped());
    pool.join();
}

TEST(SeamSessionExecutorRoundTrip, DirectExecutorAttestedIsBare) {
    asio::thread_pool pool{1};
    auto se = make_session_executor(pool.get_executor(), threading_mode::direct_executor,
                                    /*attested=*/true, nullptr);
    ASSERT_TRUE(se.has_value());
    EXPECT_FALSE(se->is_strand_wrapped());
    pool.join();
}

// THE single enforcement point: direct_executor && !attested →
// error::executor_not_serialised (slot 48).
TEST(SeamSessionExecutorRoundTrip, DirectExecutorWithoutAttestationRejected) {
    asio::thread_pool pool{1};
    auto se = make_session_executor(pool.get_executor(), threading_mode::direct_executor,
                                    /*attested=*/false, nullptr);
    ASSERT_FALSE(se.has_value());
    EXPECT_EQ(se.error(), error::executor_not_serialised);
    pool.join();
}

// N5 open-path arm — second open() → session_already_open (slot 51).
TEST(SeamSessionExecutorRoundTrip, SecondOpenRejectedSessionAlreadyOpen) {
    asio::thread_pool pool{2};
    EngineConfig engine;
    engine.executor = pool.get_executor();
    SessionConfig cfg;
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();              // T050
    cfg.security_profile = fixpp::test_support::make_minimal_security_profile();  // RC#1
    Session s{engine, cfg};

    auto first = asio::co_spawn(pool, s.open(), asio::use_future).get();
    ASSERT_TRUE(first.has_value());

    auto second = asio::co_spawn(pool, s.open(), asio::use_future).get();
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error(), error::session_already_open);
    pool.join();
}

// Open-path: null EngineConfig::executor (no override) →
// invalid_session_config (slot 53 / FR-018).
TEST(SeamSessionExecutorRoundTrip, NullEngineExecutorRejected) {
    asio::thread_pool pool{1};
    EngineConfig engine;  // engine.executor default = empty
    SessionConfig cfg;
    Session s{engine, cfg};
    auto r = asio::co_spawn(pool, s.open(), asio::use_future).get();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::invalid_session_config);
    pool.join();
}

// Open-path: direct_executor + lock_policy::spin rejected even when attested
// (slot 53 / I-06).
TEST(SeamSessionExecutorRoundTrip, DirectExecutorSpinRejectedEvenAttested) {
    asio::thread_pool pool{1};
    EngineConfig engine;
    engine.executor = pool.get_executor();
    SessionConfig cfg;
    cfg.mode = threading_mode::direct_executor;
    cfg.locks = fixpp::session::lock_policy::spin;
    cfg.already_serialized_executor = true;
    Session s{engine, cfg};
    auto r = asio::co_spawn(pool, s.open(), asio::use_future).get();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::invalid_session_config);
    pool.join();
}

}  // namespace
