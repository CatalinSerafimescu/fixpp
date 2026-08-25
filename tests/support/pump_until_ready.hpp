// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/pump_until_ready.hpp
//
// Bounded pumps for manually-driven io_contexts, and the teardown guard their
// failure path requires.
//
// Hoisted (issue #284) from tests/session/logout_exchange_test.cpp, which
// introduced `pump_until_ready` for exactly this defect. Parameterised on the
// way out, because it is the seventh member of a family that had each solved
// the same problem separately and incompatibly — see issue #289's sibling-
// helper census, alongside #289(a)'s census of raw
// `run_for(W); restart(); get()` sites that have no helper at all.

#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <chrono>
#include <fixpp/core/clock.hpp>
#include <fixpp/transport/transport.hpp>
#include <future>

namespace fixpp::test_support {

// Default budget for a bounded pump. Generous by design: it converts a wedge
// into a diagnosable failure, it is not a performance assertion.
inline constexpr auto kPumpBudget = std::chrono::seconds{10};

// Default slice. Note this is a COST FLOOR, not just a resolution: `run_for`
// on a context with outstanding work never drains early, so a pump built on it
// burns a whole slice per call however fast the work is. Measured on 106 calls
// of a 5-post operation: 20 ms slice = 2151 ms, 1 ms slice = 124 ms. Callers
// that pump often should pass something small. The floor holds for every full
// slice; only the final slice of a run is clipped to the remaining budget (see
// `pump_until` below), so the measured figures are unaffected.
inline constexpr auto kPumpSlice = std::chrono::milliseconds{1};

// Drive `ioc` until `ready()` returns true, or `budget` elapses. Returns
// whether it became ready.
//
// Replaces the `ioc.run_for(FIXED); ioc.restart(); fut.get()` idiom, which
// deadlocks when the awaited op posts its completion back to `ioc` AFTER the
// fixed window closes and nothing pumps `ioc` again (observed as a 120 s ctest
// timeout on slow MSVC-debug runs of the FileStore-offloaded test, and again on
// the linux-clang-coverage lane as issue #284).
//
// The window expiring is a LIVE outcome, not a pathology: `io_context::run_for`
// is `run_until`, and `run_one_until` tests `now < abs_time` BEFORE dispatching
// (asio impl/io_context.hpp), so an expired deadline returns leaving ready
// handlers QUEUED. Worse, a co_spawned coroutine holds an outstanding-work
// guard, so `run_for` never drains early — whether the op finished inside the
// window is purely a scheduling question.
//
// A work_guard keeps `ioc` alive across slices; the trailing restart() leaves
// it runnable for the next phase (a stopped context makes the next run_for a
// silent no-op).
template <class Ready>
[[nodiscard]] bool pump_until(asio::io_context& ioc, Ready ready,
                              std::chrono::steady_clock::duration budget = kPumpBudget,
                              std::chrono::steady_clock::duration slice = kPumpSlice) {
    auto wg = asio::make_work_guard(ioc);
    const auto deadline = std::chrono::steady_clock::now() + budget;
    auto now = std::chrono::steady_clock::now();
    while (!ready() && now < deadline) {
        ioc.run_for(std::min(slice, deadline - now));
        now = std::chrono::steady_clock::now();
    }
    wg.reset();
    const bool done = ready();
    ioc.restart();
    return done;
}

// Future-shaped specialisation of `pump_until`.
//
// Callers MUST check the bool BEFORE fut.get(), so a genuine lost-wake FAILs
// loudly at the budget instead of hanging out the ctest timeout. On the false
// path the awaited coroutine is still SUSPENDED, holding references to whatever
// the caller passed it; destroying that frame after those objects die is
// undefined, so the caller must also quiesce before teardown — `quiesce_on_exit`
// below is the scope-guard form that covers ASSERT_*'s early return.
//
// This obligation binds callers whose coroutine input is NOT already outlived
// by the pump's own scope (e.g. a block-scoped buffer passed by span into the
// coroutine frame). It does NOT relieve a caller from also giving that input a
// lifetime enclosing the guard — `quiesce_on_exit` only fixes the ORDER of
// destruction, not a dangling reference within the surviving objects.
//
// This is a test-harness utility, but the hazard it exists for is NOT
// test-only — an earlier revision of this comment claimed it was, and that
// claim was wrong. It is prevented by construction only on the C ABI, whose
// boundary owns an internal io_context and worker thread(s) running ioc_.run()
// continuously (src/capi/engine.cpp:8-9, 248-255). The C++ API is the opposite:
// EngineConfig::executor is consumer-supplied and Engine::start() "does NOT
// block or run the executor" (include/fixpp/session/engine.hpp:223), so a
// consumer that drives its own io_context with a bounded run, and then blocks
// on a fixpp awaitable, deadlocks exactly as this test did IF the bounded run
// returns before the awaitable completes AND no other thread continues driving
// that io_context. Recorded, with those two conditions attached to the
// deadlock claim, as L-284-1 in spec/behaviors-and-limitations.md — read it
// before treating this shape as a harness quirk.
// [[feedback_mock_clock_advance_before_timer_armed_race]]
template <class Fut>
[[nodiscard]] bool pump_until_ready(asio::io_context& ioc, Fut& fut,
                                    std::chrono::steady_clock::duration budget = kPumpBudget,
                                    std::chrono::steady_clock::duration slice = kPumpSlice) {
    return pump_until(
        ioc, [&fut] { return fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready; },
        budget, slice);
}

// Failure text for a `pump_until*` that ran out of budget. Stream the site name
// after it.
inline constexpr const char* kPumpBudgetMiss =
    "#284: the operation did not complete within the bounded-pump budget. Site: ";

// Failure text for a `run_window_then_ready` that missed. DELIBERATELY NOT
// `kPumpBudgetMiss`: no bounded-pump budget was ever granted to this shape, so
// that wording would misdescribe what happened (and legitimate budget-miss
// callers still need it unchanged). Stream the site name after it.
inline constexpr const char* kWindowMiss =
    "#289: the operation was not ready when its preserved run window returned, and did "
    "not become ready within one boundary grace slice. Site: ";

// Budget for a teardown drain. Deliberately the same value as
// `quiesce_on_exit`'s default below, so the two report on the same terms.
inline constexpr auto kQuiesceBudget = std::chrono::seconds{5};

// Run a caller's ORIGINAL fixed window, then report whether `fut` is ready.
//
// This is the #289 migration shape, and it is NOT `pump_until_ready`. The two
// differ on purpose:
//
//   - No work guard. `pump_until` takes one (:64) so `run_for` cannot drain
//     early, which is what gives that helper its documented COST FLOOR (:32-39:
//     "1 ms slice = 124 ms" over 106 calls, ~1.17 ms/call). The #289 sites were
//     measured at ~27 us, so adopting that floor would be a ~40x per-call
//     regression at 32 sites. Here `run_for` keeps its normal early-drain
//     behaviour and the window costs what it costs today.
//   - The window is PRESERVED, not sliced. A site's first transition to Active
//     co_spawns a DETACHED `run_liveness_loop()` (src/session/session.cpp:2733
//     acceptor, :4187 initiator), and `co_spawn` POSTS its first resumption, so
//     it lands on a LATER `run_one_until`. An early-exit pump that stopped at
//     future-readiness would leave that detached task unserviced; running the
//     original window to drain-or-deadline services it exactly as today.
//
// What it DOES take from `pump_until_ready` is the load-bearing property at
// :79-81 — the caller must consult the bool BEFORE `fut.get()`. `[[nodiscard]]`
// puts a compiler DIAGNOSTIC on ignoring it, rather than leaving it to a
// reviewer or a lexical checker. Note that is a WARNING, not an error: this
// repo does not build tests under a blanket `-Werror` (the one targeted use is
// `-Werror=deprecated-declarations` at tests/session/CMakeLists.txt:2312), so
// do not describe it as compiler-ENFORCED.
//
// The grace slice is not a "CI tolerance" and must not be grown into one.
// `run_one_until` tests `now < abs_time` BEFORE dispatching (:50-52), so a
// handler that became ready at the instant the window closed is left QUEUED.
// One `kPumpSlice` gives exactly that handler a dispatch opportunity. It moves
// the deadline by one slice; it does not make a slow runner safe, and no
// statically-chosen value could.
//
// A false return means the awaited coroutine is still SUSPENDED holding
// whatever the caller passed it. The caller MUST then drain while that state is
// still alive — see `drain_or_report` and its warning about storage that is not
// a fixture member.
template <class Fut>
[[nodiscard]] bool run_window_then_ready(asio::io_context& ioc, Fut& fut,
                                         std::chrono::steady_clock::duration window,
                                         std::chrono::steady_clock::duration grace = kPumpSlice) {
    const auto ready = [&fut] {
        return fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
    };
    ioc.run_for(window);
    ioc.restart();
    if (ready()) return true;
    ioc.run_for(grace);
    ioc.restart();
    return ready();
}

// Drain `ioc` and REPORT whether it reached quiescence. The teardown half of
// the shape above.
//
// A free function rather than an RAII guard like `quiesce_on_exit`, for THREE
// reasons — the first is the obvious one and the other two are the load-bearing
// ones:
//   1. `quiesce_on_exit` requires a non-null `fixpp::core::Clock&` (its
//      `clock` member below) and these fixtures assign none.
//   2. It must run ONLY on the miss branch. `quiesce_on_exit` drains
//      unconditionally at scope exit; arming one per call at a site invoked 32
//      times, each measured at ~27 us, would reintroduce exactly the per-call
//      cost this shape exists to avoid.
//   3. At a callee like `Fixture::feed`, the state that must stay alive is the
//      CALLER'S expression-temporary. No guard the callee declares can extend
//      that; only code running before the callee returns can drain it. A guard
//      is the wrong shape there, not merely an unavailable one.
//
// ⚠️ DIVERGENCE FROM `quiesce_on_exit`, AND IT IS A KNOWN TRAP FOR THE REST OF
// #289's MIGRATION: this does NOT close a transport. `quiesce_on_exit` grew its
// `transport` field (below) from a real defect (gate-b/r1 P1-1) — cancelling
// clock sleeps does not unstick a coroutine parked in `async_write` /
// `async_read_some`, which completes only when the transport itself is closed.
// No caller here attaches a live Transport (this file has zero references to
// one), so the gap is latent. It stops being latent for the FIRST migrated
// fixture that has a live Transport and no Clock — precisely the combination
// this helper is for. Such a caller must close the transport itself before
// calling this, or use `quiesce_on_exit`. Do not assume this drain is
// sufficient because it was sufficient here.
//
// Call this while EVERY object the suspended coroutine references is still
// alive. That is not automatic: a fixture destructor body protects fixture
// MEMBERS, but a coroutine holding a span into a caller's temporary, or into a
// block-local declared after the fixture, outlives its referent unless the
// drain happens in THAT scope. Both shapes exist at the #289 sites.
//
// `ioc.stopped()` is a DISJUNCTION and is NOT authoritative — see
// `quiesce_on_exit`'s comment on the disjunction, below.
// A false return is evidence of residual work, not proof of its absence.
inline void drain_or_report(asio::io_context& ioc, const char* site,
                            std::chrono::steady_clock::duration budget = kQuiesceBudget) {
    ioc.restart();
    ioc.run_for(budget);
    // (#305) THE PROBE. See `quiesce_on_exit`'s destructor for the full argument;
    // the short version is that `run_for` can return WITHOUT ever consulting the
    // work count, so `stopped()` alone reports a residual that does not exist.
    // This helper is NEW (#301) and inherited the defect by having the identical
    // shape — fixed here in the same change rather than only in the older copy,
    // because a fix that lands on one of two identical shapes is how this repo's
    // "fixed some sites of a claim, missed another" class keeps recurring.
    (void)ioc.poll_one();
    if (!ioc.stopped()) {
        ADD_FAILURE() << "#289: the io_context did not run out of work within the teardown "
                         "drain, so a coroutine frame is probably still suspended and will be "
                         "destroyed while referencing objects that are about to die. This "
                         "observes the residual, not its cause (stopped() is disjunctive — see "
                         "quiesce_on_exit's comment on the disjunction, below). Site: "
                      << site;
    }
}

// Best-effort attempt to destroy any still-suspended coroutine frames while the
// objects they reference are alive.
//
// Declare AFTER the fixtures whose lifetimes are at stake, so it runs BEFORE
// them, on every exit path including the early `return` an ASSERT_* performs.
//
// Reordering the declarations instead cannot work, for TWO independent reasons.
// An earlier revision of this comment gave only the second one and presented it
// as the reason; that was incomplete, and the first is the load-bearing one
// because it is unconditional rather than schedule-dependent:
//
//  1. NOTHING HOLDING A STRAND TAKEN FROM THE CONTEXT MAY OUTLIVE THE CONTEXT.
//     `asio::strand`'s handle is a shared_ptr<strand_impl>, and
//     `strand_impl::~strand_impl()` locks `service_->mutex_` and unlinks itself
//     from `service_->impl_list_` (asio/detail/impl/strand_executor_service.ipp
//     :83-94), where `service_` is a RAW pointer to a service owned by the
//     io_context's registry. `~execution_context()` runs `shutdown()` then
//     `destroy()`, and `destroy()` destroys every service
//     (asio/impl/execution_context.ipp:60-64); `shutdown()` sets
//     `impl->shutdown_` but never detaches `service_`
//     (strand_executor_service.ipp:34-50). So destroying the context first and
//     the strand after is a heap-use-after-free EVERY TIME, not on a race.
//     This reaches further than it looks: under the default
//     `threading_mode::per_session_strand` a Session's bound executor IS such a
//     strand (session_config.hpp:101,168; core/session_executor.hpp:126,142),
//     and so is `Engine::control_strand_` (session/engine.hpp:395). A BARE
//     `io_context::executor_type` is NOT affected — it is untracked and its
//     destructor touches nothing — so an `executor_override` may outlive the
//     context safely. Measured both arms under ASan; only the strand faults.
//  2. The clock's parked waiters hold work guards on the io_context, so on that
//     path either destruction order has a dangling side.
//
// The only safe state is no pending state — but this guard only OBSERVES
// whether that state was reached; it cannot force it (see below).
//
// `run_for` rather than a `run_one_for` loop deliberately: `run_one_until` also
// tests its deadline before dispatching, so a slice-at-a-time drain can return
// on "nothing was ready within the slice" and leave work queued — the very
// mechanism documented above. `run_for` drains until the context genuinely runs
// out of work OR its deadline elapses, whichever comes first — the difference
// from a sliced drain is deadline LENGTH, not immunity to the mechanism. A
// co_spawn frame holds a tracked-work guard, so a coroutine parked on anything
// other than a clock sleep (the common case, released by cancel_sleeps() above)
// keeps the context non-empty for the full 5s, and `run_for` returns with that
// frame still pending — sufficient for the case this guard exists for, not a
// general fix.
//
// `io_context::stopped()` is a DISJUNCTION, not an exact detector: it is true
// either because the context ran out of work or because something called
// `stop()` explicitly (asio `io_context.hpp:453-455`). The `stop()` sweep
// supports the false-negative direction only: for any `io_context` reachable
// from this caller, the preceding `restart()` clears any prior stopped flag and
// nothing on this call path calls `stop()` on that same context. The one
// `tests/support/` exception is `disjoint_session_executor.hpp:71`'s
// `ioc_.stop()`, but that helper owns a private `ioc_` and is included only by
// `tests/core/test_trace_context_resume.cpp`, so it is out of reach here. The
// false-positive sweep is separate: for any `io_context` reachable from this
// caller, no production code outside `src/capi/` creates a work guard, so
// `stopped()==false` here really does mean session/coroutine work is still
// outstanding.
//
// ⚠️ (#305) THAT LAST SENTENCE WAS AN OVER-CLAIM, AND THE PROBE BELOW IS WHAT
// MAKES IT TRUE. The work-guard census above is sound as far as it goes, but it
// reasons only about who can hold work OPEN — it never asks whether the work
// count was CONSULTED. `run_for` can return without consulting it at all (see the
// probe in the destructor), and then `stopped()==false` means "the deadline had
// already passed", not "work is outstanding". The census could not have caught
// that, because the defect is not in the population it enumerates. Kept, rather
// than deleted and quietly rewritten, so the shape of the mistake stays visible:
// an exhaustive sweep of the wrong axis reads exactly like proof.
//
// It is also not authoritative: `mock_clock::sleep_until`
// registers a new waiter whenever the deadline is still in the future
// (`src/core/test/mock_clock.cpp:119-126`), and `cancel_sleeps()` above is a
// ONE-SHOT drain that installs nothing to reject a later registration
// (`src/core/test/mock_clock.cpp:166-181`) — so a coroutine whose first run
// happens during this destructor's own `run_for` (the session liveness loop's
// `sleep_until`, `src/session/session.cpp:4816`, is the concrete case) can arm
// a sleep nothing will ever fire, and this guard cannot force that residual,
// only observe it. Report what was observed rather than exit silently:
// ADD_FAILURE (non-fatal — a fatal assertion in a destructor is an inert
// `return`) so a guard that never quiesces shows up in CI instead of leaving a
// dangling frame to fire nondeterministically later.
struct quiesce_on_exit {
    asio::io_context& ioc;
    fixpp::core::Clock& clock;
    // Defaulted to preserve every existing two-argument {ioc, clock}
    // aggregate initialisation's current 5s behaviour. A caller with a
    // deterministic reason to bound this tighter (e.g. a test whose only
    // purpose is to trigger the branch below) may supply a shorter budget.
    std::chrono::steady_clock::duration budget = kQuiesceBudget;
    // A live transport under the caller's control, if any (gate-b/r1 P1-1).
    // Cancelling clock sleeps is not sufficient to unstick a coroutine parked
    // in async_write/async_read_some on a still-open transport — that op
    // completes only when the transport itself is closed. nullptr (default)
    // when no such transport is in play; set it (this struct is a plain
    // aggregate, so the field may be assigned after construction, e.g. once
    // the caller attaches the transport) whenever one is. Assumes
    // Transport::close() is noexcept and idempotent (true of every transport
    // in this suite) — safe to call even if the caller already closed it.
    fixpp::transport::Transport* transport = nullptr;

    ~quiesce_on_exit() {
        if (transport) {
            (void)transport->close();
        }
        clock.cancel_sleeps();
        ioc.restart();
        ioc.run_for(budget);
        // ── (#305) THE PROBE, and it repairs a predicate this header over-claimed ──
        //
        // `io_context::run_for` is `run_until`, which loops on `run_one_until`, and
        // `run_one_until` is `while (now < abs_time) { ... }  return 0;`
        // (asio impl/io_context.hpp:112-131). If the deadline has ALREADY arrived at
        // entry the loop body never runs, `impl_.wait_one` is never called, and
        // nothing consults `outstanding_work_` or sets `stopped_`. The `restart()`
        // immediately above has just cleared that flag. So `stopped()` reads false
        // whether or not any work exists — unconditionally at a zero budget, and
        // reachable at any budget's deadline boundary.
        //
        // `poll_one()` closes it: `scheduler::poll_one` is
        // `if (outstanding_work_ == 0) { stop(); return 0; }`
        // (asio detail/impl/scheduler.ipp:289-295), so after this line `stopped()`
        // reflects the WORK COUNT rather than the deadline.
        //
        // ⚠️ THIS IS A NEW CAPABILITY AT A ZERO BUDGET, NOT AN INHERITED OBLIGATION,
        // and the distinction is the one an earlier draft of this comment got wrong.
        // `run_for(budget)` already resumes coroutines, so "a drain resumes frames" is
        // nothing new at a normal budget. But `run_for(0)` resumes NOTHING — it returns
        // before entering the scheduler — whereas `poll_one()` WILL dispatch. So the
        // expired-deadline path gains the ability to resume a frame, in exactly the case
        // where a caller chose a zero budget because it wanted nothing run. The
        // obligation that follows is the usual one and it now binds a path it did not
        // bind before: storage a suspended frame borrowed must outlive this guard.
        // Witnessed directly rather than described — see the zero-budget cases in
        // tests/session/test_quiesce_on_exit_residual.cpp.
        (void)ioc.poll_one();
        if (!ioc.stopped()) {
            ADD_FAILURE() << "quiesce_on_exit: the io_context did not run out of work within the "
                             "configured quiesce window. At this caller that is expected to mean a "
                             "coroutine frame is still suspended and will be destroyed while "
                             "referencing objects that are about to be destructed, but this guard "
                             "only observes the residual, not its cause.";
        }
    }
};

}  // namespace fixpp::test_support
