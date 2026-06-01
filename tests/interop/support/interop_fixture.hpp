// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/interop_fixture.hpp
//
// fixpp::interop — foundational Engine-lifecycle fixture (016 T004, data-model E1).
//
// Owns an asio::io_context + a public fixpp Engine and provides the reusable
// pump/teardown machinery every interop cell needs:
//   - start()       — Engine::start() (co_spawns the role loops).
//   - run_until()    — pump the io_context until a predicate holds or a wall-clock
//                      deadline elapses (the internal self-deadline, E1/R5 — never
//                      rely on ioc.run() to terminate, [[feedback_fail_placeholder_red_test]]).
//   - stop_within()  — co_spawn Engine::stop() and pump until it completes or a
//                      bound elapses, returning the measured wall-clock duration.
//                      This IS the down-peer / reconnect watchdog (FR-004/FR-028).
//
// The fixture is deliberately role/security-agnostic: a driver registers its own
// session(s) on engine() with the SessionConfig + transport factory appropriate
// to its cell (plain-tcp vs tls-logon), then drives via these helpers. The fixture
// guarantees Engine::stop() runs before ~Engine (the Engine dtor asserts stopped()).
//
// [const §XV.9]: tests/-only; the concrete Engine header is safe to include here.
#pragma once

#include <asio/io_context.hpp>
#include <chrono>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/session/engine.hpp>
#include <functional>

namespace fixpp::interop {

class InteropEngineFixture {
public:
    explicit InteropEngineFixture(fixpp::core::EngineConfig cfg = {});

    // Ensures Engine::stop() has completed before the Engine is destroyed
    // (the Engine dtor asserts stopped()). Idempotent with explicit stop_within().
    ~InteropEngineFixture();

    InteropEngineFixture(const InteropEngineFixture&) = delete;
    InteropEngineFixture& operator=(const InteropEngineFixture&) = delete;

    asio::io_context& ioc() noexcept { return ioc_; }
    fixpp::session::Engine& engine() noexcept { return engine_; }

    // Engine::start() — non-blocking; co_spawns one loop per registered session.
    void start();

    // Pump the io_context until `ready()` returns true or `deadline` (wall-clock
    // from now) elapses. Returns true iff `ready()` became true in time. Restarts
    // a drained io_context so a quiescent-then-active session keeps progressing.
    bool run_until(const std::function<bool()>& ready, std::chrono::milliseconds deadline);

    // Co-spawn Engine::stop() and pump the io_context until it completes or `bound`
    // (wall-clock) elapses. Returns the measured wall-clock duration stop() took.
    // A return value >= bound means stop() did NOT complete within the bound (the
    // FR-028 down-peer watchdog failure condition). Idempotent: a second call after
    // a completed stop() returns immediately.
    std::chrono::milliseconds stop_within(std::chrono::milliseconds bound);

    [[nodiscard]] bool stopped() const noexcept { return engine_.stopped(); }

private:
    asio::io_context ioc_;
    fixpp::session::Engine engine_;
    bool stop_spawned_ = false;
};

}  // namespace fixpp::interop
