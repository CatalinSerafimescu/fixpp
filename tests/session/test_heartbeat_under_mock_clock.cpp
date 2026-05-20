// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_heartbeat_under_mock_clock.cpp — [2d §9.9] Seam 9
//
// Heartbeat-window simulation under mock_clock, driven by the scripted
// test-double FSM (D-5). Asserts 2d-owned properties only: the heartbeat
// label fires once per deterministic clock advance, in order, serialised on
// the session domain — NOT real FIX heartbeat-FSM correctness (005's). The
// heartbeat interval is a value 2d does not pin (fixture-chosen).
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/scripted_fsm.hpp"

namespace {

using namespace std::chrono_literals;
namespace ts = fixpp::testsupport;

TEST(SeamHeartbeatUnderMockClock, DeterministicHeartbeatWindowViaScriptedFsm) {
    asio::io_context ioc;
    auto wg = asio::make_work_guard(ioc);
    std::thread runner{[&] { ioc.run(); }};

    auto clk = std::make_shared<fixpp::core::mock_clock>(
        fixpp::core::utc_time_point{}, fixpp::core::steady_time_point{},
        ioc.get_executor());

    fixpp::core::EngineConfig engine;
    engine.executor = ioc.get_executor();
    engine.clock    = clk;
    fixpp::session::SessionConfig cfg;
    cfg.dictionary        = fixpp::test_support::make_minimal_dictionary(); // T050
    cfg.security_profile  = fixpp::test_support::make_minimal_security_profile();  // RC#1
    fixpp::session::Session s{engine, cfg};

    ASSERT_TRUE(asio::co_spawn(ioc, s.open(), asio::use_future).get().has_value());
    ASSERT_EQ(s.effective_clock(), clk);   // I-03: resolved to engine clock

    constexpr int kCycles = 8;
    const auto interval = 30ms;            // fixture-picked (2d does not pin)
    const auto script = ts::make_heartbeat_script(kCycles, interval);
    ts::observation_log log;

    for (const auto& step : script) {
        if (step.label == ts::fsm_label::advance_clock) {
            clk->advance(step.delta);
        } else {
            std::promise<void> ran;
            auto done = ran.get_future();
            s.dispatch_app_callback([&log, &ran, l = step.label] {
                ts::observed_callback span{log, l};
                ran.set_value();
            });
            done.wait();                   // serialise the scripted step order
        }
    }
    wg.reset();
    runner.join();

    auto order = log.label_order();
    int heartbeats = 0;
    for (auto l : order) if (l == ts::fsm_label::heartbeat) ++heartbeats;
    EXPECT_EQ(heartbeats, kCycles);
    EXPECT_TRUE(log.strictly_serialised());
    EXPECT_EQ(order.front(), ts::fsm_label::logon);
    EXPECT_EQ(order.back(), ts::fsm_label::logout);
}

}  // namespace
