// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_executor_compat.cpp — [2d §9.3] Seam 3
//
// Executor-opt-out compatibility, 6 combos via the scripted test-double FSM
// (D-5; FR-007/FR-009/[2d §6.1]):
//   {asio::thread_pool, asio::system_executor, 1-thread io_context}
//     × {per_session_strand, direct_executor (attested)}
// Each must open() and drive the canonical Logon→NewOrderSingle→
// ExecutionReport→Logout SCRIPT LABEL sequence to completion in order.
// Asserts 2d-owned properties only (open success + serialised label order),
// NOT FIX FSM correctness (005's).
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/strand.hpp>
#include <asio/system_executor.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>

#include <future>
#include <thread>
#include <vector>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/scripted_fsm.hpp"

namespace {

using fixpp::core::EngineConfig;
using fixpp::session::Session;
using fixpp::session::SessionConfig;
using fixpp::session::threading_mode;
namespace ts = fixpp::testsupport;

// Replay the default script by posting each label callback in order onto the
// session's serialisation domain; returns the observed label order.
std::vector<ts::fsm_label>
drive_script(Session& s, const ts::fsm_script& script) {
    ts::observation_log log;
    std::promise<void> last;
    auto fut = last.get_future();
    const std::size_t n = script.size();
    for (std::size_t i = 0; i < n; ++i) {
        const auto lbl = script[i].label;
        const bool is_last = (i + 1 == n);
        s.dispatch_app_callback([&log, lbl, is_last, &last] {
            // The span MUST be fully destroyed (on_leave runs) BEFORE signalling
            // completion: drive_script tears down `log` the instant `fut` unblocks,
            // so set_value() inside the span scope races the destructor's on_leave
            // against `log`'s destruction on the main thread — a stack-use-after-
            // return on the last callback's pool thread (CI ASan, .github#936).
            { ts::observed_callback span{log, lbl}; }
            if (is_last) last.set_value();
        });
    }
    fut.wait();
    return log.label_order();
}

void run_combo(asio::any_io_executor ex, threading_mode mode, bool attested,
               asio::thread_pool* pool_to_join) {
    EngineConfig engine;
    engine.executor = std::move(ex);
    SessionConfig cfg;
    cfg.mode = mode;
    cfg.already_serialized_executor = attested;
    cfg.dictionary       = fixpp::test_support::make_minimal_dictionary(); // T050
    cfg.security_profile = fixpp::test_support::make_minimal_security_profile();  // RC#1
    Session s{engine, cfg};

    auto of = asio::co_spawn(engine.executor, s.open(), asio::use_future);
    ASSERT_TRUE(of.get().has_value()) << "open() failed for a valid combo";

    const auto script = ts::make_default_script();
    const auto observed = drive_script(s, script);

    ASSERT_EQ(observed.size(), script.size());
    for (std::size_t i = 0; i < script.size(); ++i)
        EXPECT_EQ(observed[i], script[i].label) << "label order broke at " << i;

    if (pool_to_join) pool_to_join->join();
}

TEST(SeamExecutorCompat, ThreadPoolPerSessionStrand) {
    asio::thread_pool pool{4};
    run_combo(pool.get_executor(), threading_mode::per_session_strand, false, &pool);
}
TEST(SeamExecutorCompat, ThreadPoolDirectExecutorAttested) {
    // direct_executor: the user supplies a GENUINELY-serialised executor —
    // a strand derived from the pool (the engine does NOT make_strand).
    asio::thread_pool pool{4};
    run_combo(asio::any_io_executor{asio::make_strand(pool.get_executor())},
              threading_mode::direct_executor, true, &pool);
}
TEST(SeamExecutorCompat, SystemExecutorPerSessionStrand) {
    run_combo(asio::any_io_executor{asio::system_executor{}},
              threading_mode::per_session_strand, false, nullptr);
}
TEST(SeamExecutorCompat, SystemExecutorDirectExecutorAttested) {
    // system_executor is a global CONCURRENT pool; a faithful direct_executor
    // attestation supplies a strand over it (the pre-serialised form).
    run_combo(asio::any_io_executor{asio::make_strand(asio::system_executor{})},
              threading_mode::direct_executor, true, nullptr);
}
TEST(SeamExecutorCompat, OneThreadIoContextPerSessionStrand) {
    asio::io_context ioc;
    auto wg = asio::make_work_guard(ioc);
    std::thread runner{[&] { ioc.run(); }};
    run_combo(ioc.get_executor(), threading_mode::per_session_strand, false, nullptr);
    wg.reset();
    runner.join();
}
TEST(SeamExecutorCompat, OneThreadIoContextDirectExecutorAttested) {
    asio::io_context ioc;
    auto wg = asio::make_work_guard(ioc);
    std::thread runner{[&] { ioc.run(); }};
    run_combo(ioc.get_executor(), threading_mode::direct_executor, true, nullptr);
    wg.reset();
    runner.join();
}

}  // namespace
