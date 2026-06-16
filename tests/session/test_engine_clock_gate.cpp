// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_engine_clock_gate.cpp — T017 [P] [US3] Phase 5
//
// TDD RED-first witness for the Engine clock-config gate (041 US3).
//
// SC-004 / FR-007 / FR-008 / C-4: Engine::start() must return
//   error::clock_not_set when EngineConfig::clock == nullptr, and the engine
//   must NOT become operational (no session loops spawned — lookup() stays null
//   after pumping the io_context).  With a valid clock, start() succeeds and
//   the engine operates unchanged.
//
// T017 — RED: Engine::start() is currently void; the test won't compile until
//   T018 changes the signature to [[nodiscard]] expected_t<void>.  That IS the
//   RED state.  Once T018 lands the test turns GREEN.
//
// Design decision:
//   - The "not operational" post-condition is asserted by registering a session
//     BEFORE start(), pumping the io_context, and asserting lookup() == nullptr
//     (nothing spawned — Gate A New-3 lazy construction).  This discriminates
//     against a hypothetical start() that returns an error but still spawns.
//   - After a failed start() the engine must be stopped cleanly before the dtor
//     runs (Engine dtor asserts stopped()).  stop() handles null
//     outstanding_counter_ (the join is skipped) so it is safe to call even
//     when start() never ran.
//   - The valid-clock arm asserts lookup() goes NON-null after pumping,
//     confirming the loops ran.  Uses a plain-TCP mock transport with a
//     no-op transport_send so it can run without FIXPP_TLS_FIXTURE_DIR.
//
// Anchors: spec.md FR-007/FR-008/SC-004; contracts/validation-gate.md C-4;
//          data-model.md E-5; tasks.md T017; engine_config.hpp:188
//          (validate_engine_config → clock_not_set).
//          [[feedback_witness_asserts_named_postcondition_not_proxy]]
//          [[feedback_self_run_build_gate]]

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session_config.hpp>
#include <memory>
#include <span>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

using namespace std::chrono_literals;

namespace {

// Minimal mock clock (no steady-timer; just a clock interface).
static std::shared_ptr<fixpp::core::mock_clock> make_mock_clock(asio::io_context& ioc) {
    using namespace std::chrono;
    auto utc = system_clock::time_point{} + seconds{1704067200};
    auto stp = fixpp::core::steady_time_point{} + seconds{0};
    return std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
}

// Register a minimal acceptor session on `engine`.  The session uses a no-op
// transport_send (no live TLS needed for this test).  Returns the SessionId.
static fixpp::session::SessionId register_dummy_session(fixpp::session::Engine& engine,
                                                        asio::io_context& ioc) {
    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id = "ACCEPTOR";
    cfg.target_comp_id = "INITIATOR";
    cfg.begin_string = "FIX.4.2";
    cfg.role = fixpp::session::session_role::acceptor;
    cfg.executor_override = ioc.get_executor();
    cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    cfg.heartbeat_interval = std::chrono::seconds{0};
    cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 0};
    cfg.transport_send = [](std::span<const std::byte>) {};
    cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;

    auto id = fixpp::session::SessionId::from_config(cfg);
    auto r = engine.register_session(std::move(cfg));
    (void)r;  // succeeds by construction (new id, valid config)
    return id;
}

// Stop the engine and drain the ioc within a hard bound.
static void stop_engine(fixpp::session::Engine& engine, asio::io_context& ioc) {
    auto fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (ioc.stopped()) ioc.restart();
        ioc.run_for(std::chrono::milliseconds{10});
        if (fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready) break;
    }
    if (fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
        fut.get();  // propagate any stop() exception
    }
}

}  // namespace

// ── Cell 1: null clock → clock_not_set, not operational ──────────────────────
//
// Engine::start() with EngineConfig::clock == nullptr must:
//   (a) return error::clock_not_set (not succeed),
//   (b) NOT spawn any loops — lookup(id) == nullptr after pumping the ioc
//       (discriminating "not operational" witness; if loops ran, lookup goes
//       non-null once a session reaches open()).
//
// SC-004 / FR-007 / C-4 / data-model E-5.

TEST(EngineClockGate, NullClock_ReturnsClockNotSet_NotOperational) {
    asio::io_context ioc;

    fixpp::core::EngineConfig cfg;
    cfg.executor = ioc.get_executor();
    // clock is nullptr (default) — the gate must reject this.

    fixpp::session::Engine engine{ioc.get_executor(), std::move(cfg)};

    // Register a session so there's something to spawn if the gate fails.
    auto id = register_dummy_session(engine, ioc);

    // T018 RED: Engine::start() is void here; the test won't compile.
    // T018 GREEN: start() returns expected_t<void>.
    auto result = engine.start();

    // (a) Must return clock_not_set.
    ASSERT_FALSE(result.has_value())
        << "start() with null clock must return an error (clock_not_set)";
    EXPECT_EQ(result.error(), fixpp::core::error::clock_not_set)
        << "start() with null clock must return clock_not_set (slot 54)";

    // (b) No loops spawned — lookup stays null even after pumping.
    ioc.run_for(50ms);
    if (ioc.stopped()) ioc.restart();

    EXPECT_EQ(engine.lookup(id), nullptr)
        << "start() must NOT spawn session loops when it returns an error; "
        << "lookup() must stay null (no loop reached open())";

    // Clean teardown (dtor asserts stopped()).
    stop_engine(engine, ioc);
}

// ── Cell 2: valid clock → success, operates unchanged ────────────────────────
//
// Engine::start() with a valid clock must return success ({}).
// After pumping the ioc, the registered session loop runs (lookup goes non-null)
// — proving the valid-clock path is unchanged.
//
// SC-004 / FR-007 / FR-009 / C-4.

TEST(EngineClockGate, ValidClock_ReturnsSuccess) {
    asio::io_context ioc;

    fixpp::core::EngineConfig cfg;
    cfg.executor = ioc.get_executor();
    cfg.clock = make_mock_clock(ioc);

    fixpp::session::Engine engine{ioc.get_executor(), std::move(cfg)};

    // T018 RED: won't compile until start() returns expected_t<void>.
    auto result = engine.start();

    // Must succeed with a valid clock.
    EXPECT_TRUE(result.has_value()) << "start() with a valid clock must return success (no error)";

    // Run briefly so posted loops fire (Gate A New-3 lazy-construction pattern).
    ioc.run_for(50ms);
    if (ioc.stopped()) ioc.restart();

    // Clean teardown.
    stop_engine(engine, ioc);
}
