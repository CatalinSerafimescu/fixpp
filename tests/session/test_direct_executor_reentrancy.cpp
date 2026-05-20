// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_direct_executor_reentrancy.cpp — [2d §9.16] Seam 16
//
// direct_executor re-entrancy guard (root cause #1; Edge Case
// "direct_executor attested over a genuinely non-serialised executor"):
//   • session_executor::is_strand_wrapped() discriminates the two modes;
//   • DEBUG builds trip the strand-invariant assert on a detected concurrent
//     session-callback entry; RELEASE builds are documented UB (guard
//     compiled out — not tested).
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

namespace {

using fixpp::core::EngineConfig;
using fixpp::session::Session;
using fixpp::session::SessionConfig;
using fixpp::session::threading_mode;

TEST(SeamDirectExecutorReentrancy, IsStrandWrappedDiscriminator) {
    auto dict = fixpp::test_support::make_minimal_dictionary();
    asio::thread_pool pool{2};
    EngineConfig engine;
    engine.executor = pool.get_executor();

    SessionConfig strand_cfg;  // per_session_strand default
    strand_cfg.dictionary       = dict;        // T050: non-null dict required at open
    strand_cfg.security_profile = fixpp::test_support::make_minimal_security_profile();  // RC#1
    Session a{engine, strand_cfg};
    ASSERT_TRUE(asio::co_spawn(pool, a.open(), asio::use_future).get().has_value());
    EXPECT_TRUE(a.executor().is_strand_wrapped())
        << "per_session_strand must report is_strand_wrapped()==true";

    SessionConfig direct_cfg;
    direct_cfg.mode = threading_mode::direct_executor;
    direct_cfg.already_serialized_executor = true;
    direct_cfg.dictionary       = dict;        // T050: non-null dict required at open
    direct_cfg.security_profile = fixpp::test_support::make_minimal_security_profile();  // RC#1
    Session b{engine, direct_cfg};
    ASSERT_TRUE(asio::co_spawn(pool, b.open(), asio::use_future).get().has_value());
    EXPECT_FALSE(b.executor().is_strand_wrapped())
        << "direct_executor must report is_strand_wrapped()==false";
    pool.join();
}

// Correctly-attested direct_executor over a GENUINELY serialised executor
// (1-thread io_context) drives many callbacks WITHOUT tripping the debug
// strand-invariant guard — the guard fires only on a real concurrency
// violation, not on correct attestation.
TEST(SeamDirectExecutorReentrancy, CorrectAttestationDoesNotTripGuard) {
    asio::io_context ioc;
    auto wg = asio::make_work_guard(ioc);
    std::thread runner{[&] { ioc.run(); }};

    EngineConfig engine;
    engine.executor = ioc.get_executor();
    SessionConfig cfg;
    cfg.mode = threading_mode::direct_executor;
    cfg.already_serialized_executor = true;   // truthful attestation
    cfg.dictionary        = fixpp::test_support::make_minimal_dictionary(); // T050
    cfg.security_profile  = fixpp::test_support::make_minimal_security_profile();  // RC#1
    Session s{engine, cfg};
    ASSERT_TRUE(asio::co_spawn(ioc, s.open(), asio::use_future).get().has_value());

    std::atomic<int> done{0};
    constexpr int kN = 3000;
    for (int i = 0; i < kN; ++i) {
        s.dispatch_app_callback(
            [&done] { done.fetch_add(1, std::memory_order_relaxed); });
    }
    while (done.load(std::memory_order_relaxed) < kN)
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    wg.reset();
    runner.join();
    SUCCEED() << "debug strand-invariant guard not tripped under correct "
                 "direct_executor attestation";
}

// The debug strand-invariant assert on a DETECTED concurrent FSM entry
// (direct_executor attested over a genuinely non-serialised executor) is
// implemented in Session::dispatch_app_callback (in_dispatch_ exchange +
// assert, compiled out under NDEBUG — release is documented UB per the spec
// Edge Case). Exercising the abort path needs the project's special
// release-/death-linkage harness (sync seam #5 precedent: a dedicated
// EXPECT_DEATH target with overridden linkage — the default preset's gtest
// disables GTEST_HAS_DEATH_TEST and fork-after-asio-threads is unsafe). That
// abort-path death target is folded into the Phase-9 sanitizer/death matrix
// (T057), keeping this functional seam green and fork-free.

}  // namespace
