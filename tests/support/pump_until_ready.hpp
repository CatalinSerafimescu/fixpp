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
#include <exception>
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

// Shared stem for a teardown drain's residual report. The two drains below
// diverge IMMEDIATELY after it, and the divergence is load-bearing for their
// witnesses, not decorative: `EXPECT_NONFATAL_FAILURE` matches a SUBSTRING, so a
// stem that both messages carried in full would make every matcher bound to it
// pass whichever helper reported. `drain_or_report` continues ", so a coroutine";
// `cancel_and_drain_or_report` continues " even with clock sleeps released on
// every slice, so a coroutine". A witness must match past the stem to
// discriminate. ⚠️ They then RE-CONVERGE: everything after that clause up to the
// divergent tail is `kDrainResidualCause` below, shared by both. "Diverge
// immediately" describes the next few words, not the rest of the message.
// A witness must therefore match the DIVERGENT clause specifically -- matching
// anywhere in the shared cause is no more discriminating than matching the stem.
//
// (#322) And past the DIVERGENCE too, where the caller is
// `~quiesce_on_exit`: it delegates to `cancel_and_drain_or_report`, so guard and
// primitive now compose byte-identical text apart from the trailing `site` -- a
// witness for one of them must bind that site. (The header already publishes kWindowMiss /
// kPumpBudgetMiss / kDrainThrew for exactly this reason; this stem was inlined prose in two places
// and had already drifted on punctuation at birth.)
inline constexpr const char* kDrainResidual =
    "#289: the io_context did not run out of work within the teardown drain";

// (#322) The two drains RE-CONVERGE after their divergent clause, and the stem
// comment above used to imply they never do. This is the shared middle: 251
// bytes that were byte-identical in both `ADD_FAILURE`s. Hoisted for exactly the
// reason `kDrainResidual` was -- a fragment duplicated in two places drifts, and
// this one is four times longer than the stem that had already drifted on
// punctuation at birth.
//
// Safe to hoist because it is PRODUCER-side only: no matcher anywhere under
// tests/ binds any part of it. Derived AND demonstrated, because a grep proves
// only that a search found nothing:
//   - `git grep "suspended and will be destroyed\|observes the residual\|disjunctive"
//     -- tests` returns nothing outside this header;
//   - and rewording inside this constant ("cause" -> "reason") leaves all 29
//     cells in test_quiesce_on_exit_residual.cpp GREEN, which is the same claim
//     stated as an experiment rather than as an absence.
// So this text is deliberately UNWITNESSED and freely rewordable. Contrast the
// divergent TAIL below, which witnesses DO bind -- rewording that reds cells --
// and which must therefore stay spelled out at its producer. The asymmetry is
// the point: discriminating fragments are pinned, explanatory prose is not.
//
// Composition, in order: kDrainResidual + <the drain's own divergent clause> +
// kDrainResidualCause + <optional divergent tail> + "Site: " + site.
inline constexpr const char* kDrainResidualCause =
    "so a coroutine frame is probably still suspended and will be destroyed while "
    "referencing objects that are about to die. This observes the residual, not its "
    "cause (stopped() is disjunctive -- see quiesce_on_exit's comment on the "
    "disjunction, below). ";

// Budget for a teardown drain. Deliberately the same value as
// `quiesce_on_exit`'s default below, and since #322 that guard DELEGATES to
// `cancel_and_drain_or_report` -- so where no transport is set, a two-argument
// `{ioc, clock}` guard and a defaulted-budget call to the primitive are the same
// loop over the same 5 s. Where one IS set the guard forwards it and the two
// diverge.
inline constexpr auto kQuiesceBudget = std::chrono::seconds{5};

// Failure text for a teardown drain that a handler threw out of (#308). Stream
// the site name after it.
inline constexpr const char* kDrainThrew =
    "#308: a handler threw during the teardown drain, so the drain did not complete. Site: ";

// Run a teardown drain and REPORT a throwing handler rather than terminating.
// Returns whether the drain ran to completion. (#308)
//
// Every drain in this header funnels through here, so the exception policy is
// decided ONCE. That is the point, not a convenience: `drain_or_report` was
// added later than `quiesce_on_exit` and inherited that guard's #305 deadline
// defect purely by being a structurally identical copy. A second copy of a
// try/catch would re-arm the same "fixed one of two identical shapes" class this
// header keeps paying for.
//
// (#322) `~quiesce_on_exit` delegates to `cancel_and_drain_or_report` instead of
// carrying its own `restart`/`run_for`/`poll_one`/`stopped`/`ADD_FAILURE`. #321
// shipped that third copy with the #305 deadline artefact reintroduced, caught
// only because the two siblings carried a zero-budget witness it lacked.
//
// ⚠️ WHAT REMAINS IS NOT A COUNT TO KEEP DRIVING DOWN, and stating it as one
// ("two copies, not three") invites the next reader to try for one. The
// CONDITION: there is one drain per DRAIN ALGORITHM. `drain_or_report` is a
// single full-budget `run_for`; `cancel_and_drain_or_report` is a 1 ms-sliced
// loop that re-cancels sleeps every pass. Neither is a copy of the other -- they
// share only a `try` / `pump_or_report_throw` / `if (!ioc.stopped())` skeleton
// whose exception half is ALREADY factored out into this function. Folding them
// would impose the sliced loop's per-call floor on `drain_or_report`'s callers,
// which is the objection `run_window_then_ready` above already accepted against
// adopting `pump_until_ready`'s floor. A third drain is warranted only by a third
// algorithm, and a duplicated one never is.
//
// WHY A DRAIN NEEDS THIS AT ALL. Pumping dispatches arbitrary ready handlers, and
// a handler may throw. `~quiesce_on_exit` has no exception specification and no
// exception handling, so it is implicitly `noexcept` and an escaping exception is
// `std::terminate` -- no gtest failure, no test name, and no indication of which
// guard died, which is strictly LESS diagnosable than the `ADD_FAILURE` the guard
// exists to produce. `drain_or_report` is not itself `noexcept`, but it is called
// from destructor BODIES (`~Fixture` at test_next_expected_msgseqnum.cpp:374), so
// the exception meets an implicitly-noexcept frame one level up and terminates
// just the same.
//
// The exception TEXT is preserved deliberately: swallowing it would trade a
// terminate for a silent pass, which is worse than either.
//
// ⚠️ This is NOT `quiesce_or_release_on_exit`'s shape (added by #304, in
// test_test_request_id_cross_session_race.cpp), and it must not be conflated with
// it. That guard's catch is nested so that an exception mid-pump still takes the
// RELEASE branch -- it exists to fail SAFE. "Do not lose the release" and "do not
// terminate" are different requirements; `~InteropEngineFixture`
// (tests/interop/support/interop_fixture.cpp:95-165) records that collapsing them
// into one outer catch was itself a defect. This helper has no release branch to
// lose, so a single catch is correct HERE and would not be correct THERE.
template <class Pump>
[[nodiscard]] bool pump_or_report_throw(Pump pump, const char* site) {
    try {
        pump();
        return true;
    } catch (const std::exception& e) {
        ADD_FAILURE() << kDrainThrew << site << " -- what(): " << e.what();
    } catch (...) {
        ADD_FAILURE() << kDrainThrew << site << " -- a non-std exception, so no text is available.";
    }
    return false;
}

// Run a caller's ORIGINAL fixed window, then report whether `fut` is ready.
//
// This is the #289 migration shape, and it is NOT `pump_until_ready`. The two
// differ on purpose:
//
//   - No work guard. `pump_until` takes one (its `wg = asio::make_work_guard(ioc)`)
//     so `run_for` cannot drain early, which is what gives that helper its
//     documented COST FLOOR (`kPumpSlice`'s doc comment above:
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
// What it DOES take from `pump_until_ready` is the load-bearing property in
// its own doc comment — the caller must consult the bool BEFORE `fut.get()`. `[[nodiscard]]`
// puts a compiler DIAGNOSTIC on ignoring it, rather than leaving it to a
// reviewer or a lexical checker. Note that is a WARNING, not an error: this
// repo does not build tests under a blanket `-Werror` (the one targeted use is
// `-Werror=deprecated-declarations` at tests/session/CMakeLists.txt:2312), so
// do not describe it as compiler-ENFORCED.
//
// The grace slice is not a "CI tolerance" and must not be grown into one.
// `run_one_until` tests `now < abs_time` BEFORE dispatching (see `pump_until`'s
// doc comment above), so a handler that became ready at the instant the window
// closed is left QUEUED.
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
// Stated as a CONDITION rather than a count, because the count form of this
// sentence was already going stale. It used to read "no caller here attaches a
// live Transport … so the gap is latent", and #307 lands exactly such a caller
// (`CompID_KnobOff_AuthzAllowListStillEnforced`, which attaches a NullSinkTransport
// and has no Clock). A sentence that names its own falsification condition — "it
// stops being latent for the FIRST fixture that…" — should have been written as
// that condition in the first place. Conditions are merge-order independent;
// counts create an obligation on whoever merges next.
//
// The condition:
//
//     A caller whose transport can PARK a coroutine — i.e. whose async_write /
//     async_read_some can stay pending — MUST use `quiesce_on_exit` with
//     `.transport` set, or `cancel_and_drain_or_report` with its `transport`
//     argument (#322), or close the transport itself before calling this.
//     `drain_or_report` cannot close a transport and never will, whoever calls it.
//
// A transport that cannot park is unaffected: that is a property of the transport,
// not of how many callers exist. #307's is of that kind — its `async_read_some`
// reports EOF immediately and its `async_write` swallows its bytes — so the gap is
// unreachable there. Do not read "unreachable at that caller" as "unreachable".
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
    try {
        // (#305) THE PROBE. See `cancel_and_drain_or_report`'s probe below for the full
        // argument — it moved there with #322, when `~quiesce_on_exit`, which used to
        // hold it, became one delegating call. The short version is that `run_for` can
        // return WITHOUT ever consulting the work count, so `stopped()` alone reports a
        // residual that does not exist.
        // This helper is NEW (#301) and inherited the defect by having the identical
        // shape — fixed here in the same change rather than only in the older copy,
        // because a fix that lands on one of two identical shapes is how this repo's
        // "fixed some sites of a claim, missed another" class keeps recurring.
        //
        // WHAT THIS COSTS A CALLER, bounded rather than left as "it may dispatch",
        // because #289's migration is adding callers faster than anyone re-reads this:
        //   - At the default `kQuiesceBudget` (5 s), and at any budget whose deadline
        //     has NOT already passed at entry, `run_for` enters the scheduler and has
        //     already run every ready handler. The probe can then dispatch at most ONE
        //     handler that became ready exactly at the deadline boundary. On the
        //     quiesced path it dispatches nothing and only sets `stopped_`.
        //   - Only at a zero or already-expired budget does the probe resume work that
        //     `run_for` would not have.
        //
        // ⚠️ THE SECOND BULLET IS A PRECONDITION ON CALLERS, NOT A SURVEY RESULT. It is
        // true today that every non-witness call takes the default budget, but that is a
        // property of the current tree, not an invariant — and #289 is adding callers.
        // A caller passing a zero or already-expired budget makes the probe resume work
        // nothing else would. DO NOT pass one here without reading the paragraph below.
        //
        // ⚠️ AND THE SAFETY OF THAT DISPATCH IS A PROPERTY OF THE CALL SITE, NOT OF THIS
        // FUNCTION. An earlier version of this comment claimed the dispatch "happens in
        // the CALLER'S scope, so anything the resumed frame borrowed is still alive by
        // construction". That is true of a MISS-BRANCH drain — the shape this helper was
        // designed for, where the drain runs inside the scope that still owns the
        // borrowed storage — and FALSE of a DESTRUCTOR-BODY drain, which is the other
        // shape callers actually use. A destructor body protects fixture MEMBERS; a
        // caller's temporary, or a block-local declared after the fixture, is already
        // dead by then.
        //
        // Both shapes exist right now, in one file:
        //   `test_next_expected_msgseqnum.cpp:393`  `Fixture::feed`'s miss branch — SAFE
        //   `test_next_expected_msgseqnum.cpp:374`  `~Fixture()`               — NOT
        // and that is not a hypothetical pairing: deleting the in-`feed` drain while
        // keeping `~Fixture`'s reproduces a `heap-use-after-free` under ASan, because
        // `~Fixture`'s drain is precisely what RESUMES the frame over the dead temporary.
        //
        // ⚠️ (gate-b/r2) SAFE ABOVE IS CONDITIONAL, not unqualified: it holds only
        // because `feed`'s own drain SUCCEEDS at this call site. If `drain_or_report`
        // ever reported a residual here, `feed` returns anyway (this function is
        // `void`), and `~Fixture`'s LATER drain would then resume a frame whose
        // borrowed temporary is already gone -- `feed` cannot retain it; the storage
        // is not its to hold. It does not bite HERE because this fixture has no
        // clock and no transport (`quiesce_on_exit`'s ⚠️ DIVERGENCE paragraph above
        // applies: no forcing lever, nothing closes a transport, nothing cancels a
        // sleep), so nothing between `feed`'s drain and `~Fixture`'s can change a
        // frame's readiness -- a frame that survives `feed`'s 5 s drain is in the
        // same state at `~Fixture`. The one caller that reaches the miss branch with
        // non-default durations leaves a single posted handler, which the default
        // budget's drain dispatches.
        //
        // The remedy for the day this DOES bite is not a `bool` return from
        // `drain_or_report` -- that pushes a policy decision onto every #289
        // migration call site, and it still would not help `feed`, which cannot
        // retain a caller's temporary regardless of what it is told. The remedy is
        // the arena-copy pattern `logout_exchange_test.cpp`'s `feed_inbound` already
        // uses: copy into a fixture-owned arena declared before `ioc` (so it
        // outlives `ioc`'s own destruction) and span the copy, never the caller's
        // own buffer.
        //
        // So this probe confers no safety. It inherits whatever the call site already
        // guarantees, and it makes a resumption slightly more likely at sites that were
        // quiet only because nothing resumed them. (Correction owed to the #289/#307
        // session, which had the RED oracle for it.)
        //
        // (#308) The pump is guarded: a handler that throws here would otherwise meet
        // the implicitly-noexcept destructor BODY this is called from and terminate the
        // process. See `pump_or_report_throw` above for why the report is the whole
        // point. On the throwing path the drain did not complete, so the residual check
        // below is skipped -- reporting "work is still outstanding" on top of "a handler
        // threw" would describe a consequence as if it were an independent finding.
        if (!pump_or_report_throw(
                [&] {
                    ioc.restart();
                    ioc.run_for(budget);
                    (void)ioc.poll_one();
                },
                site)) {
            return;
        }
        if (!ioc.stopped()) {
            ADD_FAILURE()
                << kDrainResidual << ", " << kDrainResidualCause << "Site: " << site;
        }
    } catch (...) {
        // (gate-b/r1) Nothing may escape a teardown frame -- see pump_or_report_throw's
        // own comment above ("this helper has no release branch to lose, so a single
        // catch is correct HERE") and the exception-policy-decided-ONCE argument two
        // comments above that. ADD_FAILURE() is not nothrow (gtest's
        // AddTestPartResult throws GoogleTestFailureException under
        // --gtest_throw_on_failure, and the streaming chain can throw on
        // allocation), and this function is called from destructor BODIES -- an
        // uncaught throw here meets an implicitly-noexcept frame one level up and
        // terminates the process. The no-throw contract is unconditional BY DESIGN,
        // including when this free function is called directly from a test body
        // (where propagation would otherwise be legal): the exception policy for
        // every drain in this header is decided ONCE, here, not per call site.
    }
}

// Drain `ioc` while REPEATEDLY releasing clock sleeps, and report whether it
// reached quiescence. The #289 miss-branch teardown for a fixture that HAS a
// Clock.
//
// It replaces the two-line `clock->cancel_sleeps(); drain_or_report(ioc, site);`
// pair that #313 and #316 shipped at ~40 sites. That pair is ONE-SHOT, and the
// gap is one of ORDER, not of power.
//
// (#322) It is ALSO the whole body of `~quiesce_on_exit` below, which is why this
// function takes a `transport` and why the #305 probe's full argument lives here
// rather than in that destructor. Read the two together: everything the guard
// documents about WHEN a drain may be called binds every caller of this function,
// and everything here about what the loop can and cannot force binds the guard.
//
// THE MECHANISM, stated as a chain because no single link is obvious. On the
// working path `cancel_sleeps()` completes every registered waiter with
// `asio::error::operation_aborted` (src/core/test/mock_clock.cpp:166-181);
// `mock_clock::sleep_until` initiates with a `void(std::error_code)` signature
// under `use_awaitable`, which THROWS `std::system_error` on a non-zero code
// (:99-105); and `run_liveness_loop`'s `catch (const std::system_error&)` sits
// OUTSIDE its `while (fsm_state_ == fsm_state::Active)` loop
// (src/session/session.cpp), so the throw crosses the loop boundary and converts
// to a clean `co_return`. A single cancel therefore DOES terminate a sleeping
// liveness loop -- it does not merely wake it to sleep again. Worth writing down:
// the loop re-arms on every normal wake, so "cancellation kills it" is a property
// of the THROW escaping the loop, not of the cancellation itself.
//
// WHAT THE ONE-SHOT PAIR MISSES is therefore only this: `sleep_until` re-registers
// freely whenever the deadline is still in the future
// (src/core/test/mock_clock.cpp:119-126), and `cancel_sleeps()` installs nothing to
// reject a LATER registration. So a miss branch whose drain itself COMPLETES A
// STATE TRANSITION that co_spawns a sleeping coroutine -- `run_liveness_loop`'s
// `sleep_until` and `run_logout_phase1`'s are the two known instances -- arms its
// sleep AFTER the single cancel has already run. Nothing then releases it, and the
// pair burns the whole budget before reporting a residual the caller has no lever
// to clear. Alternating the cancel with the drain closes exactly that window.
//
// THREE BUCKETS, NOT TWO, and the discriminator is WHERE the miss falls -- whether
// the drain performs the transition, not merely whether the fixture has a clock.
// Measured on test_validate_gate_logon_arm.cpp with `window = grace = 0ms`:
// `Row_F_InboundLogout_NoRejectLoop/logon-ack` (the drain performs the Active
// transition) took 5000 ms and reported two failures; the pre-Active control
// `Row_F_InboundReject_NoRejectLoop/open` took 0 ms and reported one.
//
// ⚠️ DIAGNOSTIC QUALITY ON AN ALREADY-FAILING PATH, not a lifetime fix. Both arms
// above are ASan-clean. This does not make a missed window safe and must not be
// described as doing so; it stops the report from misdescribing why the context
// would not quiesce.
//
// ⚠️ IT CLOSES A TRANSPORT ONLY IF YOU PASS ONE, and that is a CONDITION on the
// call, not a property of the helper. `transport` defaults to nullptr, and at a
// null transport this drain is exactly what it was before #322: two clock levers
// and nothing that can complete a parked async_write/async_read_some.
// `drain_or_report`'s ⚠️ DIVERGENCE paragraph above states the binding condition and
// it applies here in that form: a caller whose transport can PARK a coroutine --
// i.e. whose async_write / async_read_some can stay pending -- MUST pass it as
// `transport` here (or set `quiesce_on_exit::transport`, which is the same lever
// reached through the guard), or close the transport itself before calling this.
//
// The pointee must OUTLIVE the call: `close()` is invoked unconditionally when the
// pointer is non-null. `Transport::close()` is assumed noexcept and idempotent
// (true of every transport in this suite), so passing an already-closed transport
// is safe. It is closed ONCE, before the first slice, and INSIDE
// `pump_or_report_throw` -- see the body for why that placement is load-bearing
// rather than incidental.
//
// Every caveat `drain_or_report` carries about WHEN to call a drain binds here too:
// call this while every object the suspended coroutine references is still alive,
// and note that `ioc.stopped()` is a DISJUNCTION rather than an exact detector (see
// `quiesce_on_exit`'s comment on the disjunction, below).
//
// Reports AT MOST ONCE, at the end. A per-slice report would emit thousands of
// failures on precisely the path that is already failing.
inline void cancel_and_drain_or_report(asio::io_context& ioc, fixpp::core::Clock& clock,
                                       const char* site,
                                       std::chrono::steady_clock::duration budget = kQuiesceBudget,
                                       fixpp::transport::Transport* transport = nullptr) {
    try {
        // (#308) Guarded for the same reason the drain above is: this is called from
        // miss branches and destructor bodies, and a throwing handler would terminate
        // rather than fail the test.
        if (!pump_or_report_throw(
                [&] {
                    // ⚠️ THE CLOSE IS INSIDE THE GUARD, AND THAT IS #308'S FIX, NOT A
                    // FORMATTING CHOICE. Hoisting it out -- "close the transport, then
                    // call the primitive" -- would put `close()` back in the caller's
                    // frame, which for `~quiesce_on_exit` is an implicitly-noexcept
                    // destructor. `close()` is documented noexcept, so that adds no
                    // path today; leaving it outside is what makes that documentation
                    // load-bearing for process survival, and #308 deliberately refused
                    // that dependency. `cancel_sleeps()` is inside for the same reason.
                    //
                    // Closed ONCE, before the first slice, rather than per pass: it is
                    // idempotent, nothing here reopens it, and the witness that pins it
                    // (ClosesTransportBeforeDraining) asserts ordering, not repetition.
                    if (transport) {
                        (void)transport->close();
                    }
                    const auto deadline = std::chrono::steady_clock::now() + budget;
                    for (;;) {
                        // Cancel FIRST, and on every pass: the sleep this exists to
                        // release is the one armed by the PREVIOUS slice's drain.
                        clock.cancel_sleeps();
                        ioc.restart();
                        const auto now = std::chrono::steady_clock::now();
                        // ⚠️ THE DEADLINE TEST IS AT THE BOTTOM, AND THAT IS THE #305 FIX,
                        // NOT A STYLE CHOICE. Testing it here and `break`ing would exit
                        // before `poll_one()` had ever run, so at a zero or already-expired
                        // budget the `restart()` above would leave `stopped()` false on a
                        // context holding NO work -- reporting a residual that does not
                        // exist. That is exactly the artefact #305 removed from the two
                        // drains above, and writing this loop the obvious way reintroduced
                        // it in the THIRD copy. `std::min` yields a negative duration once
                        // the deadline has passed and `run_for` then returns without
                        // dispatching, so the pass costs nothing but still reaches the probe.
                        // Pinned by ZeroBudgetOnEmptyContextIsNotResidual, the twin of the
                        // witnesses the other two helpers already carry.
                        ioc.run_for(std::min(std::chrono::steady_clock::duration{kPumpSlice},
                                             deadline - now));
                        // ── (#305) THE PROBE, and it repairs a predicate this header
                        // over-claimed. This is the canonical statement of the argument;
                        // `drain_or_report` above points here, and so does
                        // `~quiesce_on_exit` below, which held it until #322 reduced that
                        // destructor to one delegating call. ─────────────────────────────
                        //
                        // `io_context::run_for` is `run_until`, which loops on
                        // `run_one_until`, and `run_one_until` is
                        // `while (now < abs_time) { ... }  return 0;`
                        // (asio impl/io_context.hpp:112-131). If the deadline has ALREADY
                        // arrived at entry the loop body never runs, `impl_.wait_one` is
                        // never called, and nothing consults `outstanding_work_` or sets
                        // `stopped_`. The `restart()` immediately above has just cleared
                        // that flag. So `stopped()` reads false whether or not any work
                        // exists -- unconditionally at a zero budget, and reachable at any
                        // budget's deadline boundary. At a SLICED drain that is not an edge
                        // case at all: every slice that expires with work pending reaches
                        // it.
                        //
                        // `poll_one()` closes it: `scheduler::poll_one` is
                        // `if (outstanding_work_ == 0) { stop(); return 0; }`
                        // (asio detail/impl/scheduler.ipp:289-295), so after this line
                        // `stopped()` reflects the WORK COUNT rather than the deadline.
                        //
                        // ⚠️ THIS IS A NEW CAPABILITY AT A ZERO BUDGET, NOT AN INHERITED
                        // OBLIGATION, and the distinction is the one an earlier draft of
                        // this comment got wrong. `run_for(budget)` already resumes
                        // coroutines, so "a drain resumes frames" is nothing new at a
                        // normal budget. But `run_for(0)` resumes NOTHING -- it returns
                        // before entering the scheduler -- whereas `poll_one()` WILL
                        // dispatch. So the expired-deadline path gains the ability to
                        // resume a frame, in exactly the case where a caller chose a zero
                        // budget because it wanted nothing run. The obligation that follows
                        // is the usual one and it binds a path it did not bind before:
                        // storage a suspended frame borrowed must outlive this drain.
                        // Witnessed rather than described -- every drain in this header
                        // has a zero-budget resumption case AND an at-most-one-dispatch
                        // case in tests/session/test_quiesce_on_exit_residual.cpp.
                        (void)ioc.poll_one();
                        if (ioc.stopped() || now >= deadline) break;
                    }
                },
                site)) {
            return;
        }
        // `quiesced` used to be tracked in a bool threaded out of the lambda. It is
        // derivable: every exit path runs `restart()` then the probe, so `stopped()`
        // after the loop IS the verdict -- and saying it this way makes the shape
        // identical to the two sibling drains, which both end in `if (!ioc.stopped())`.
        if (!ioc.stopped()) {
            ADD_FAILURE()
                << kDrainResidual << " even with clock sleeps released on every slice, "
                << kDrainResidualCause
                // ⚠️ THIS TAIL IS BOUND BY WITNESSES IN ANOTHER FILE, which is why it
                // stays spelled out here while the shared cause above is a constant.
                // EVERY `quiesce_on_exit` residual matcher in
                // tests/session/test_quiesce_on_exit_residual.cpp binds
                // "warning above. Site: quiesce_on_exit". Binding a producer-side
                // CONSTANT instead would make a reworded message agree with its own
                // matcher silently; spelling it out means a reword REDS those cells.
                // So: reword the fragment below and expect them to fail. That is the
                // coupling working as intended, not a defect -- re-derive which cells
                // with `git grep -n "warning above. Site: quiesce_on_exit" -- tests`.
                << "A transport parked in async_write/async_read_some is a residual this "
                   "drain clears only when a transport was passed to it; see the transport "
                   "warning above. Site: "
                << site;
        }
    } catch (...) {
        // (gate-b/r1) Nothing may escape a teardown frame -- see the identical
        // comment in `drain_or_report` above; the exception policy is decided ONCE
        // for every drain in this header. (#322) This catch also covers
        // `~quiesce_on_exit`, which delegates here and deliberately adds no handler
        // of its own -- so it is the LAST line of defence for a destructor, not just
        // for a free function called from one.
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
// The only safe state is no pending state, and this guard's job is to reach it
// where it can and to REPORT where it cannot. It carries two forcing levers —
// `transport` below, and the repeated `cancel_sleeps()` it inherits by delegating
// — neither of which makes a residual impossible.
//
// ⚠️ (#322) THIS USED TO SAY `run_for` WAS CHOSEN OVER A `run_one_for` LOOP
// DELIBERATELY, AND THE ARGUMENT NO LONGER HOLDS. It read: a slice-at-a-time drain
// can return on "nothing was ready within the slice" and leave work queued,
// because `run_one_until` tests its deadline before dispatching. That objection is
// sound against the OBVIOUS sliced loop and does not bind
// `cancel_and_drain_or_report`'s, which never breaks on an empty slice: it breaks
// only on `ioc.stopped()` AFTER `poll_one()` has consulted the work count, or on
// the deadline. The paragraph predated that loop; it was never a considered
// rejection of this shape. Kept as a correction rather than deleted, because a
// reader who remembers the old rationale needs to see it retired.
//
// What survives from it is the LIMIT, which is unchanged by the delegation: a
// co_spawn frame holds a tracked-work guard, so a coroutine parked on anything
// neither lever reaches keeps the context non-empty for the full budget, and the
// drain returns with that frame still pending — sufficient for the case this guard
// exists for, not a general fix.
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
// ⚠️ (#305) THAT LAST SENTENCE WAS AN OVER-CLAIM, AND THE PROBE IS WHAT MAKES IT
// TRUE. (#322) The probe is ABOVE now, in `cancel_and_drain_or_report`; this
// headline still said "below" after the body was repointed, which is the
// fixed-one-of-two-identical-shapes class this very paragraph memorialises.
//
// The work-guard census above is sound as far as it goes, but it
// reasons only about who can hold work OPEN — it never asks whether the work
// count was CONSULTED. `run_for` can return without consulting it at all (see the
// probe in `cancel_and_drain_or_report`), and then `stopped()==false` means "the
// deadline had already passed", not "work is outstanding". The census could not
// have caught that, because the defect is not in the population it enumerates.
// Kept, rather than deleted and quietly rewritten, so the shape of the mistake
// stays visible: an exhaustive sweep of the wrong axis reads exactly like proof.
//
// ⚠️ (#322) THE SLEEP ARMED DURING THE GUARD'S OWN DRAIN IS NOW FORCED, NOT MERELY
// OBSERVED. This paragraph used to end "this guard cannot force that residual,
// only observe it", and that stopped being true when #321 landed
// `cancel_and_drain_or_report`. The mechanism it described is real and unchanged:
// `mock_clock::sleep_until` registers a new waiter whenever the deadline is still
// in the future (`src/core/test/mock_clock.cpp:119-126`), and a SINGLE
// `cancel_sleeps()` installs nothing to reject a later registration
// (`src/core/test/mock_clock.cpp:166-181`), so a coroutine whose first run happens
// during the drain — the session liveness loop's `sleep_until`,
// `src/session/session.cpp:4816`, is the concrete case — arms a sleep the one-shot
// cancel has already missed. What changed is the lever: this destructor delegates
// to `cancel_and_drain_or_report`, whose loop alternates the cancel with the
// drain, so the sleep armed by slice N is released by slice N+1. Pinned by
// CancelAndDrainOrReportWitness.{OneShotCancelThenDrainCannotReleaseTheSleep,
// ReleasesASleepArmedDuringItsOwnDrain}.
//
// It remains an OBSERVER for every residual neither lever reaches — a coroutine
// parked on something that is not a clock sleep and not this transport. Report
// what was observed rather than exit silently: ADD_FAILURE (non-fatal — a fatal
// assertion in a destructor is an inert `return`) so a guard that never quiesces
// shows up in CI instead of leaving a dangling frame to fire nondeterministically
// later. The report is emitted by the primitive and carries the site
// `"quiesce_on_exit"`, which is what discriminates it from a direct call.
struct quiesce_on_exit {
    asio::io_context& ioc;
    fixpp::core::Clock& clock;
    // Defaulted to preserve every existing two-argument {ioc, clock}
    // aggregate initialisation's current 5s behaviour. A caller with a
    // deterministic reason to bound this tighter (e.g. a test whose only purpose
    // is to trigger the residual report) may supply a shorter budget. (#322) That
    // report is no longer "the branch below" -- it is `cancel_and_drain_or_report`'s
    // `ADD_FAILURE`, which this guard reaches by delegating.
    std::chrono::steady_clock::duration budget = kQuiesceBudget;
    // A live transport under the caller's control, if any (gate-b/r1 P1-1).
    // Cancelling clock sleeps is not sufficient to unstick a coroutine parked
    // in async_write/async_read_some on a still-open transport — that op
    // completes only when the transport itself is closed. nullptr (default)
    // when no such transport is in play; set it (this struct is a plain
    // aggregate, so the field may be assigned after construction, e.g. once
    // the caller attaches the transport) whenever one is — which is how every
    // real caller does it, so a two-argument `{ioc, clock}` aggregate init is
    // NOT evidence the lever is unset.
    //
    // (#322) THE CONTRACT IS THE PRIMITIVE'S, NOT THIS FIELD'S. Since the
    // destructor delegates, this is a forwarding slot: it is passed straight to
    // `cancel_and_drain_or_report`'s `transport` argument, which is what
    // dereferences it and where the `close()` happens. Requirements on the
    // pointee — it must outlive the call, `close()` is assumed noexcept and
    // idempotent, it is closed once before the first slice — are stated there
    // (see its ⚠️ IT CLOSES A TRANSPORT ONLY IF YOU PASS ONE paragraph) and are
    // deliberately NOT restated here; an earlier copy of them at this field had
    // already drifted into naming the wrong dereferencing frame.
    //
    // What is local to the guard, and only this: the pointee must be declared
    // BEFORE the guard, since the guard's own destruction is what triggers the
    // forwarding. Whether it is a `Session`-owned transport or a plain
    // block-local does not matter, and both shapes exist. Declaring the guard
    // first leaves this dangling.
    fixpp::transport::Transport* transport = nullptr;

    // (#322) ONE DELEGATING CALL, and every part of the teardown this destructor
    // used to spell out now lives in `cancel_and_drain_or_report`: the #308
    // try/catch and `pump_or_report_throw` wrapping, the #305 probe, the residual
    // `ADD_FAILURE`, and the transport close INSIDE the pump.
    //
    // ⚠️ IT MUST STAY ONE CALL. "Close the transport, then call the primitive"
    // would put `transport->close()` back in THIS frame, which has no exception
    // specification and no handler of its own and is therefore implicitly
    // `noexcept` -- making "close() is noexcept" load-bearing for process survival
    // in a destructor. That is precisely the dependency #308 refused; the primitive
    // takes the transport so the refusal survives the delegation.
    //
    // Nothing may escape here either, and nothing does: every path through
    // `cancel_and_drain_or_report` is inside its own unconditional `catch (...)`
    // (its own comment: the exception policy for every drain in this header is
    // decided ONCE), including the throwing `ADD_FAILURE()` that
    // `--gtest_throw_on_failure` produces. So this destructor adds no handler of
    // its own -- a second one would be the duplicated try/catch
    // `pump_or_report_throw`'s comment argues against.
    ~quiesce_on_exit() {
        cancel_and_drain_or_report(ioc, clock, "quiesce_on_exit", budget, transport);
    }
};

}  // namespace fixpp::test_support
