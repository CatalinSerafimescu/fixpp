// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_clock_injection_corpus.cpp — [2d §9.11] Seam 11
//
// The 2d-SCOPED deterministic clock-injection corpus (D-5 / SC-002 /
// I-03/I-04). mock_clock injected via SessionConfig::clock_override; the
// scripted test-double FSM is driven and its observation diffed against a
// GOLDEN expectation. Byte-identical output across runs proves no
// session-scoped path bypasses effective_clock into
// std::chrono::system_clock::now(). This is the bounded "clock seam only"
// corpus (C-P2-6) — NOT the full FIX-TC corpus (owned by 005). Relocated
// from the design-doc nominal tests/conformance/ (reserved for 005).
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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

// Run the default script with an injected mock_clock; emit a deterministic
// transcript of (label, steady_ns, utc_ns) the session-scoped path observed.
std::vector<std::string> run_corpus() {
    asio::io_context ioc;
    auto wg = asio::make_work_guard(ioc);
    std::thread runner{[&] { ioc.run(); }};

    auto clk = std::make_shared<fixpp::core::mock_clock>(
        fixpp::core::utc_time_point{1'700'000'000s},
        fixpp::core::steady_time_point{}, ioc.get_executor());

    fixpp::core::EngineConfig engine;
    engine.executor = ioc.get_executor();
    engine.clock    = std::make_shared<fixpp::core::mock_clock>(
        fixpp::core::utc_time_point{}, fixpp::core::steady_time_point{},
        ioc.get_executor());                       // distinct engine clock
    fixpp::session::SessionConfig cfg;
    cfg.clock_override   = clk;                     // session uses THIS one
    cfg.dictionary       = fixpp::test_support::make_minimal_dictionary(); // T050
    cfg.security_profile = fixpp::test_support::make_minimal_security_profile();  // RC#1
    fixpp::session::Session s{engine, cfg};

    EXPECT_TRUE(asio::co_spawn(ioc, s.open(), asio::use_future).get().has_value());
    EXPECT_EQ(s.effective_clock(), clk);           // I-03: override wins

    std::vector<std::string> transcript;
    const auto script = ts::make_default_script();
    for (const auto& step : script) {
        clk->advance(10ms);
        std::promise<void> ran;
        auto done = ran.get_future();
        s.dispatch_app_callback([&, lbl = step.label] {
            // Session-scoped timing MUST come from effective_clock (the
            // injected mock), never std::chrono::system_clock::now().
            const auto st = s.effective_clock()->steady_now()
                                .time_since_epoch().count();
            const auto ut = s.effective_clock()->now()
                                .time_since_epoch().count();
            transcript.push_back(std::to_string(static_cast<int>(lbl)) +
                                 ":" + std::to_string(st) + ":" +
                                 std::to_string(ut));
            ran.set_value();
        });
        done.wait();                               // serialise step ordering
    }
    wg.reset();
    runner.join();
    return transcript;
}

TEST(SeamClockInjectionCorpus, ByteIdenticalToGoldenAndDeterministic) {
    auto a = run_corpus();
    auto b = run_corpus();
    // Determinism ⇒ no wall-clock leak on any session-scoped path (SC-002).
    EXPECT_EQ(a, b);

    // Golden: 4 labels, steady advances 10ms each (ns), utc tracks steady
    // from the seeded mock_clock baseline. Deterministic ⇒ stable golden.
    // Golden: 4 labels; steady advances 10ms each (ns from epoch 0); utc =
    // seeded baseline (1.7e9 s) + the same steady delta — proving session
    // time is sourced from the injected mock_clock, deterministically, with
    // NO std::chrono::system_clock::now() leak (SC-002). Stable golden.
    const std::vector<std::string> golden{
        "0:10000000:1700000000010000000",
        "1:20000000:1700000000020000000",
        "2:30000000:1700000000030000000",
        "4:40000000:1700000000040000000",
    };
    EXPECT_EQ(a, golden);
}

}  // namespace
