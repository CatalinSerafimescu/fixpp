// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_strand_serialisation.cpp — [2d §9.2] Seam 2
//
// Strand-serialisation property (TSan-MANDATORY; I-05 / FR-007 / FR-008 /
// [2d §6.1]). N user threads drive Session::dispatch_app_callback (a
// test-double "send" — the FIX FSM is 005's; D-5). Asserts the 2d-owned
// properties only:
//   • within a session no two app callbacks overlap (strictly serialised);
//   • fromApp(N+1) never begins before fromApp(N) returns;
//   • two sessions on the SAME engine executor may run concurrently across
//     sessions but never overlap within a session.
// RED before T019–T021 (the Phase-2 placeholder open() bound no
// session_executor, so dispatch had no serialisation domain).
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <thread>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/scripted_fsm.hpp"
#include "support/spin_window.hpp"
#include "support/wait_until.hpp"

namespace {

using fixpp::core::EngineConfig;
using fixpp::session::Session;
using fixpp::session::SessionConfig;
using fixpp::session::threading_mode;
using fixpp::testsupport::fsm_label;
using fixpp::testsupport::observation_log;
using fixpp::testsupport::observed_callback;

EngineConfig make_engine(asio::any_io_executor ex) {
    EngineConfig e;
    e.executor = std::move(ex);
    return e;  // clock null is fine — US1 does not resolve effective_clock
}

// Shared minimal dictionary for all open_session calls — T050 requires a
// non-null dictionary at Session::open (FR-018 / I-13 / seam 13).
static std::shared_ptr<const fixpp::dict::Dictionary> g_dict =
    fixpp::test_support::make_minimal_dictionary();

void open_session(Session& s, asio::thread_pool& pool) {
    auto fut = asio::co_spawn(pool, s.open(), asio::use_future);
    ASSERT_TRUE(fut.get().has_value());
}

// A precise sub-millisecond window, for widening the race an unserialised
// implementation would lose. `spin_for` rather than `sleep_for`, and the local
// reason is the strand: these callbacks are serialized, so any per-callback
// oversleep accumulates end to end and can exhaust the bounded drain below
// while the code under test is behaving correctly. (That is not hypothetical —
// it is what PR #326 was; the measured lane history lives there, not here.)
// Only one strand runs at a time, so exactly one thread spins.

TEST(SeamStrandSerialisation, NoOverlapWithinSessionUnderMultiThreadPool) {
    asio::thread_pool pool{8};
    EngineConfig engine = make_engine(pool.get_executor());
    SessionConfig cfg;        // default per_session_strand
    cfg.dictionary = g_dict;  // T050: non-null dict required at open
    cfg.security_profile = fixpp::test_support::make_minimal_security_profile();  // RC#1
    Session s{engine, cfg};
    open_session(s, pool);

    observation_log log;
    constexpr int kThreads = 16;
    constexpr int kPerThread = 200;
    std::vector<std::thread> drivers;
    std::atomic<int> done{0};
    for (int t = 0; t < kThreads; ++t) {
        drivers.emplace_back([&] {
            for (int i = 0; i < kPerThread; ++i) {
                s.dispatch_app_callback([&log, &done] {
                    observed_callback span{log, fsm_label::new_order_single};
                    // small window so an unserialised impl would overlap
                    fixpp::test_support::spin_for(std::chrono::microseconds{5});
                    done.fetch_add(1, std::memory_order_relaxed);
                });
            }
        });
    }
    for (auto& th : drivers) th.join();

    // Drain: wait until every posted callback ran. Bounded (#315) — this was an
    // unbounded spin, so a lost post wedged the test out to the ctest timeout.
    // Reported AFTER pool.join(), not via ASSERT_*: `pool` is declared BEFORE
    // `log` and `done`, so an early return would destroy them while the pool's
    // threads are still running callbacks that reference both.
    const bool drained = fixpp::test_support::wait_until_observed(
        [&done] { return done.load(std::memory_order_relaxed) >= kThreads * kPerThread; },
        std::chrono::seconds{10});
    pool.join();
    ASSERT_TRUE(drained) << fixpp::test_support::kWaitBudgetMiss
                         << "SingleSessionConcurrentDispatch: posted callbacks never all ran";

    EXPECT_TRUE(log.strictly_serialised())
        << "callbacks overlapped within a single session — strand contract broken";
    EXPECT_EQ(static_cast<int>(log.snapshot().size()), kThreads * kPerThread);
}

TEST(SeamStrandSerialisation, CrossSessionConcurrentSamEngineExecutor) {
    asio::thread_pool pool{8};
    EngineConfig engine = make_engine(pool.get_executor());
    SessionConfig cfg;
    cfg.dictionary = g_dict;  // T050: non-null dict required at open
    cfg.security_profile = fixpp::test_support::make_minimal_security_profile();  // RC#1
    Session a{engine, cfg};
    Session b{engine, cfg};
    open_session(a, pool);
    open_session(b, pool);

    observation_log la, lb;
    std::atomic<bool> a_inside{false};
    std::atomic<bool> overlap_across{false};
    std::atomic<int> done{0};
    constexpr int kEach = 400;
    for (int i = 0; i < kEach; ++i) {
        a.dispatch_app_callback([&] {
            observed_callback span{la, fsm_label::execution_report};
            a_inside.store(true, std::memory_order_release);
            fixpp::test_support::spin_for(std::chrono::microseconds{10});
            a_inside.store(false, std::memory_order_release);
            done.fetch_add(1, std::memory_order_relaxed);
        });
        b.dispatch_app_callback([&] {
            observed_callback span{lb, fsm_label::execution_report};
            if (a_inside.load(std::memory_order_acquire))
                overlap_across.store(true, std::memory_order_relaxed);
            done.fetch_add(1, std::memory_order_relaxed);
        });
    }
    // Bounded (#315), reported after join for the same destruction-order reason
    // as above: `pool` outlives `la`/`lb`/`done` only if we do not return early.
    const bool drained = fixpp::test_support::wait_until_observed(
        [&done] { return done.load(std::memory_order_relaxed) >= 2 * kEach; },
        std::chrono::seconds{10});
    pool.join();
    ASSERT_TRUE(drained) << fixpp::test_support::kWaitBudgetMiss
                         << "CrossSessionConcurrentSamEngineExecutor: posted callbacks";

    EXPECT_TRUE(la.strictly_serialised());
    EXPECT_TRUE(lb.strictly_serialised());
    // Cross-session concurrency is PERMITTED (not asserted to occur, but it
    // must not be forbidden by a global lock): both logs complete fully.
    EXPECT_EQ(static_cast<int>(la.snapshot().size()), kEach);
    EXPECT_EQ(static_cast<int>(lb.snapshot().size()), kEach);
    SUCCEED() << "cross-session overlap observed=" << overlap_across.load();
}

}  // namespace
