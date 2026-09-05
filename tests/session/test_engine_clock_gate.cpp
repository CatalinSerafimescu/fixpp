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
#include "support/pump_until_ready.hpp"

// ── #289: bounded pumps ──────────────────────────────────────────────────────
//
// This file's one census site is NOT the `run_window_then_ready` shape the rest of
// #289 migrates. It is a hand-rolled bounded poll loop, which is what
// `pump_until_ready` already is -- so it adopts THAT primitive, and reports with
// `kPumpBudgetMiss` (a budget was granted and exhausted) rather than `kWindowMiss`
// (a preserved window closed). See `stop_engine` below.
//
// ⚠️ CONSEQUENCE FOR VERIFICATION: NEITHER #289 DRIVER CAN FORCE THIS SITE.
// `pump_until_ready` carries no site label, so `FIXPP_FORCE_WINDOW_MISS` cannot reach
// it; and it is not a `run_window_then_ready` call, so `ci/pump-red-arm.sh` cannot
// rewrite it either. Forcing it means editing the budget by hand.

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
//
// (#289) The hand-rolled 5 s / 10 ms poll loop this replaces is `pump_until_ready` in
// SHAPE -- bounded budget, slice, poll for ready -- and the reason the site was in the
// census is a difference in OUTCOME: on exhaustion the old loop fell through SILENTLY,
// so a `stop()` that never completed left the test green with no verdict at all. The
// `if (ready) get()` guard made the hang unobservable rather than fixing it. The BUDGET
// IS UNCHANGED at 5 s, so the only new outcome is a report; no timing margin moves.
//
// ⚠️ "IS `pump_until_ready`" WOULD BE TOO STRONG -- three things differ, and one of them
// binds the CALLER rather than this helper:
//   - slice 10 ms -> `kPumpSlice` 1 ms, and `pump_until` holds a work guard across slices
//     (the old loop held none), so a slice no longer returns early when work runs out;
//   - the old loop restarted a stopped `ioc` BEFORE EACH SLICE; `pump_until` restarts ONCE
//     after the loop, relying on that work guard to keep the context runnable during it;
//   - ⚠️ therefore `pump_until_ready` DOES NOT RESTART AT ENTRY. Called with an already-
//     STOPPED `ioc`, every `run_for` is a silent no-op and the pump burns its whole budget
//     before reporting a miss. Both callers below restart-if-stopped immediately before
//     calling, so this is inert TODAY -- but that is a property of the callers, not of this
//     helper. A new caller must do the same. Check it; do not inherit this sentence.
//
// ⚠️ THE DRAIN IS `drain_or_report`, AND NOT BECAUSE NO CLOCK IS REACHABLE. One is:
// `Engine::clock()` returns `const shared_ptr<Clock>&`. But `NullClock_ReturnsClockNotSet_
// NotOperational` below constructs an `Engine` whose `EngineConfig::clock` IS null -- that
// is the cell it exists to witness -- and then calls this helper, so
// `cancel_and_drain_or_report(ioc, *engine.clock(), ...)` would dereference a null
// `shared_ptr` ON THE MISS PATH. A fault inside a failure handler is the one place it is
// least likely to be diagnosed correctly.
// (Also note `engine.hpp`'s comment on `clock()` -- *"Never null post-construction"* -- does
// not hold for that Engine: construction accepts a null clock and `start()` is what rejects
// it. Pre-existing; reported, not changed here.)
// A bare `*clock` would separately bind `::clock` from <ctime>.
static void stop_engine(fixpp::session::Engine& engine, asio::io_context& ioc) {
    auto fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    if (!fixpp::test_support::pump_until_ready(ioc, fut, 5s)) {
        fixpp::test_support::drain_or_report(ioc, "engine_clock_gate:stop_engine");
        ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << "engine_clock_gate:stop_engine";
        return;
    }
    fut.get();  // propagate any stop() exception
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
