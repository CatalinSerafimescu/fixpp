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
    // `teardown_bound` (#292) bounds the DESTRUCTOR's stop() drive. It is a ctor
    // parameter rather than a setter so it cannot be changed after a stop_within()
    // has already drawn a conclusion from the old value, and so the fixture holds
    // no mutable knob. Tests that exercise the miss branch pass 0 ms.
    explicit InteropEngineFixture(fixpp::core::EngineConfig cfg = {},
                                  std::chrono::milliseconds teardown_bound =
                                      std::chrono::seconds{2});

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
    //
    // NOT a hard wall-clock bound, and the difference matters (gate-b/r2 P2-2):
    // io_context::run_for bounds how long the context WAITS for work, not how long
    // an already-dispatched handler may run. A stop continuation that blocks
    // synchronously past the deadline still leaves the future ready, so this can
    // return late with completion recorded. The consequence is a missed DEADLINE
    // diagnostic, not a lifetime hazard — once stop() genuinely completed there
    // are no suspended frames and destroying the Engine is safe.
    // [[nodiscard]] (#292): the destructor used to CALL this and drop the answer,
    // so a stop() that never completed was invisible.
    //
    // Scope of the guarantee, stated precisely because it is easy to overstate:
    // this does NOT make a recurrence of that bug a compile error. The
    // destructor is itself a legitimate discarder — it consumes the outcome via
    // stop_completed_, not via the return value, and writes `(void)stop_within(...)`.
    // What actually prevents the recurrence is stop_completed_. The attribute
    // protects the EXTERNAL call sites — every one of which binds the result
    // today — from silently growing one that does not. Deliberately no count
    // here: a number in a comment goes stale on the next test added, and the
    // invariant ("external callers bind it") is what matters.
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

    // (#292) Held by pointer, but still declared AFTER ioc_ — therefore destroyed
    // BEFORE it, exactly as the plain value member was. (It is no longer the last
    // member; the bookkeeping below follows it. Only the order relative to ioc_
    // matters, and that is unchanged.)
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
    // that specific operation ever finished.
    //
    // shared_future, not future, and that choice is load-bearing. future::get()
    // INVALIDATES, so a plain future needs a separate "already spawned" flag to
    // survive the one path where get() is reached without success: Engine::stop()
    // co_awaits async_wait unguarded in its joins, so a non-zero error_code
    // propagates as system_error, get() throws, and stop_completed_ is never set.
    // With a plain future, valid() would then be false forever — the pump loop
    // could never observe readiness and would spin a whole core for the entire
    // teardown bound before reporting. shared_future::get() neither invalidates
    // nor consumes the exception (it rethrows on every call), so valid() is a
    // permanent "spawned" flag, the throw path reports immediately, and the
    // extra bool disappears.
    std::shared_future<void> stop_fut_;
    bool stop_completed_{false};

    // Destructor teardown bound (#292). Was 30 s, chosen when the destructor
    // DISCARDED the outcome and the wait was therefore free. Now that it reports,
    // the bound is constrained from BOTH sides, and the round-1/round-2 churn
    // here was caused by justifying only one side at a time:
    //
    // UPPER (gate-b/r2 P2-1). interop_business_message_interop_test is ONE ctest
    // entry carrying `TIMEOUT 30` (tests/interop/CMakeLists.txt:380) over FOUR
    // parameterized cells (2 counterparties x 2 roles). Under a stop regression
    // every cell can spend 3 s in expect_graceful_stop (hp_support.hpp:317) and
    // then `bound` in its destructor, so the entry costs 4*(3+bound). At 5 s that
    // is 32 s — ctest kills the binary BEFORE the reports print, destroying the
    // very diagnostic this change exists to produce. At 2 s it is 20 s, inside
    // the timeout with room for the test bodies.
    //
    // LOWER. The bound is only ever REACHED when stop() is already misbehaving:
    // a cell that calls expect_graceful_stop successfully leaves stop_completed_
    // true, and the destructor then returns without pumping at all. For the cells
    // that rely on the destructor for a HEALTHY stop, a loopback stop completes in
    // single-digit ms (support_smoke_test.cpp:114 bounds an idle engine at 2 s and
    // passes instantly). 2 s is ~2 orders of magnitude of headroom on that path.
    std::chrono::milliseconds teardown_bound_;
};

}  // namespace fixpp::interop
