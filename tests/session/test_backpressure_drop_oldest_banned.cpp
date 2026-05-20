// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_backpressure_drop_oldest_banned.cpp — [2d §9.13] Seam 13
//
// Drop-oldest banned-on-app-path enforcement ([const §XV.15] / [2d §6.4] /
// I-14 / FR-010). Two halves:
//
// 1. COMPILE-TIME: a static_assert at every switch over
//    SessionConfig::backpressure_mode enumerates exactly two cases (block,
//    disconnect_and_recover). The static_assert is enforced via the
//    FIXPP_ASSERT_BACKPRESSURE_SWITCH_EXHAUSTIVE() macro from session_config.hpp.
//    This test has an inline helper that uses the macro — it must compile,
//    which proves the enum is closed and fully enumerated.
//
// 2. RUNTIME: attempting to call Session::open() with an out-of-range
//    backpressure_mode (cast from an int) returns error::invalid_session_config
//    (slot 53 / I-14 / [2d §6.4]). This is the defence-in-depth backstop for
//    FFI / SWIG paths that bypass the C++ type system.
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <cstdint>
#include <memory>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

namespace {

using fixpp::core::EngineConfig;
using fixpp::core::error;
using fixpp::session::Session;
using fixpp::session::SessionConfig;

// Helper: create a minimal mock_clock bound to the given executor.
static std::shared_ptr<fixpp::core::mock_clock> make_clock(
        asio::any_io_executor ex) {
    return std::make_shared<fixpp::core::mock_clock>(
        fixpp::core::utc_time_point{},
        fixpp::core::steady_time_point{},
        std::move(ex));
}

// ── Part 1: Compile-time exhaustiveness ────────────────────────────────────
// This function uses FIXPP_ASSERT_BACKPRESSURE_SWITCH_EXHAUSTIVE + a switch
// that covers both legal values. If a third enumerator were added to
// backpressure_mode without updating this switch, the macro's static_assert
// would fire at compile time.
[[maybe_unused]] static void check_switch_covers_both_legal_values(
        SessionConfig::backpressure_mode m) {
    // Macro verifies the enum has exactly 2 values (block + disconnect_and_recover).
    FIXPP_ASSERT_BACKPRESSURE_SWITCH_EXHAUSTIVE(SessionConfig::backpressure_mode);
    switch (m) {
        case SessionConfig::backpressure_mode::block:
            break;
        case SessionConfig::backpressure_mode::disconnect_and_recover:
            break;
    }
}

// ── Part 2: Runtime out-of-range cast rejected at Session::open() ──────────
TEST(SeamBackpressureDropOldestBanned, OutOfRangeCastRejectedAtOpen) {
    asio::io_context ioc;
    EngineConfig engine;
    engine.executor = ioc.get_executor();
    engine.clock    = make_clock(ioc.get_executor());

    SessionConfig cfg;
    // 2 is not block=0 or disconnect_and_recover=1 — simulates FFI/SWIG bypass
    cfg.app_backpressure = static_cast<SessionConfig::backpressure_mode>(2);

    Session s{engine, cfg};
    auto result = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run();
    auto val = result.get();

    EXPECT_FALSE(val.has_value())
        << "Session::open() must reject out-of-range backpressure_mode";
    ASSERT_FALSE(val.has_value());
    EXPECT_EQ(val.error(), error::invalid_session_config)
        << "Expected invalid_session_config (slot 53) for out-of-range "
           "backpressure_mode cast";
}

// Maximum unsigned out-of-range cast also rejected.
TEST(SeamBackpressureDropOldestBanned, MaxOutOfRangeCastRejectedAtOpen) {
    asio::io_context ioc;
    EngineConfig engine;
    engine.executor = ioc.get_executor();
    engine.clock    = make_clock(ioc.get_executor());

    SessionConfig cfg;
    cfg.app_backpressure =
        static_cast<SessionConfig::backpressure_mode>(
            std::numeric_limits<std::uint8_t>::max());

    Session s{engine, cfg};
    auto result = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run();
    auto val = result.get();

    EXPECT_FALSE(val.has_value());
    ASSERT_FALSE(val.has_value());
    EXPECT_EQ(val.error(), error::invalid_session_config);
}

// The two legal values MUST NOT be rejected.
TEST(SeamBackpressureDropOldestBanned, LegalValuesAcceptedAtOpen) {
    for (auto bp : {SessionConfig::backpressure_mode::block,
                    SessionConfig::backpressure_mode::disconnect_and_recover}) {
        asio::io_context ioc;
        EngineConfig engine;
        engine.executor = ioc.get_executor();
        engine.clock    = make_clock(ioc.get_executor());

        SessionConfig cfg;
        cfg.app_backpressure = bp;
        cfg.dictionary       = fixpp::test_support::make_minimal_dictionary(); // T050
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();  // RC#1

        Session s{engine, cfg};
        auto result = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run();
        auto val = result.get();

        EXPECT_TRUE(val.has_value())
            << "Legal backpressure_mode value must not be rejected at open";
    }
}

}  // namespace
