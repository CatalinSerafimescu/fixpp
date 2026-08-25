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
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/session/engine.hpp>
#include <functional>
#include <future>
#include <memory>

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
    fixpp::session::Engine& engine() noexcept { return *engine_; }

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
    // [[nodiscard]] (#292): the destructor used to CALL this and drop the answer,
    // so a stop() that never completed was invisible. Every one of the 14 call
    // sites already binds the result; the attribute makes a recurrence a compile
    // error rather than a review question.
    [[nodiscard]] std::chrono::milliseconds stop_within(std::chrono::milliseconds bound);

    // NOTE (#292): this forwards Engine::stopped(), which is NOT a completion
    // predicate — Engine::stop() stores stopped_=true at STEP 1 of its teardown
    // (src/session/engine.cpp:1196) and then goes on to cancel loops, join them,
    // close sessions and clear the registry (through engine.cpp:1343). So
    // stopped()==true means "stop was ADMITTED", not "stop has FINISHED", and a
    // stop suspended anywhere in steps 2-5 reports true. The fixture must not
    // use it to decide teardown safety; see stop_completed() below.
    [[nodiscard]] bool stopped() const noexcept { return engine_->stopped(); }

    // (#292) The predicate teardown actually needs: the ONE co_spawned stop()
    // operation this fixture owns has resolved and its get() returned. Unlike
    // stopped(), this cannot be true while a teardown frame is still suspended.
    [[nodiscard]] bool stop_completed() const noexcept { return stop_completed_; }

    // Test-only seam (#292). The destructor's teardown bound is 30 s — generous
    // on purpose — which makes the not-completed branch untestable in practice
    // (a witness would have to hang for 30 s). A test sets this to 0 ms to take
    // that branch deterministically and in no time at all.
    void set_teardown_bound_for_test(std::chrono::milliseconds b) noexcept {
        teardown_bound_ = b;
    }

private:
    asio::io_context ioc_;
    // A real wall-clock source so outbound admin messages carry a populated
    // SendingTime(52). Without it effective_clock_ is null and the session emits an
    // EMPTY 52= — tolerated by mock peers (test_reconnect_live_happy_path) but REJECTED
    // by a real QuickFIX counterparty ("Tag specified without a value:52"), so live
    // interop logon never completes. Injected into EngineConfig::clock when the caller
    // did not supply one.
    //
    // (#292) The previous comment here claimed "no special member ordering
    // needed", justified by Engine::stop() draining every per-session liveness
    // sleep_until before the dtor returns. That premise holds only when stop()
    // COMPLETES — and the whole point of a bounded stop is that it may not. On
    // the timeout path the premise is false. The ordering is nevertheless left
    // EXACTLY as it was, and the dtor handles that path instead; see below for
    // why moving the Engine would be strictly worse.
    std::shared_ptr<fixpp::core::Clock> clock_;

    // (#292) Held by pointer, but still declared LAST — destroyed FIRST, before
    // ioc_, exactly as the plain value member was.
    //
    // Issue #292 suggests declaring this BEFORE ioc_ so the Engine outlives the
    // context. DO NOT DO THAT. Engine holds
    // `asio::strand<asio::any_io_executor> control_strand_` as a VALUE member
    // (engine.hpp:395) built from this fixture's own executor (engine.cpp:121),
    // and a strand handle destroyed after its io_context dereferences an
    // already-destroyed service: ~strand_impl unlinks through `service_`
    // (asio strand_executor_service.ipp:83-94) which ~execution_context has
    // already destroyed (execution_context.ipp:60-64). Verified with a
    // standalone repro plus a bare-executor control arm (bare executors are
    // fine; strands are not). That reorder would turn a timeout-only hazard
    // into a heap-use-after-free on EVERY run, success path included.
    //
    // The pointer is here for the DESTRUCTOR's benefit only: on the
    // stop()-did-not-complete path it deliberately release()es, which does two
    // jobs at once — ~Engine never runs, so its stopped_ assert cannot abort and
    // replace a named failure; and the Engine (with control_strand_) stays alive,
    // so the suspended stop() frame that ~ioc_ destroys afterwards still has a
    // live referent. On the normal path this is byte-for-byte the old behaviour.
    std::unique_ptr<fixpp::session::Engine> engine_;

    // (#292) The single co_spawned Engine::stop() operation, kept as fixture
    // state rather than a stop_within() local. Two reasons it must outlive the
    // call: a second stop_within() must pump the SAME operation instead of
    // spawning a second stop(), and the destructor must be able to ask whether
    // that specific operation ever finished. `stop_spawned_` is separate from
    // stop_fut_.valid() because get() invalidates the future.
    std::future<void> stop_fut_;
    bool stop_spawned_{false};
    bool stop_completed_{false};

    // Destructor teardown bound. 30 s by default — teardown correctness, not the
    // FR-028 watchdog assertion. Overridable only via set_teardown_bound_for_test.
    std::chrono::milliseconds teardown_bound_{std::chrono::seconds{30}};
};

}  // namespace fixpp::interop
