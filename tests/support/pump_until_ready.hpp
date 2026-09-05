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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/transport/transport.hpp>
#include <future>

// The self-driving-executor twin (#315). Included here, not defined here, so
// that every existing pump caller sees `wait_until_observed` through the include it
// already has -- without this header's asio/gtest/Clock dependencies leaking
// into the tests/capi callers that also need it. See wait_until.hpp for why.
#include "support/wait_until.hpp"

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

// The value a VALUE-RETURNING helper returns from its `run_window_then_ready`
// miss branch, after it has reported the miss via `kWindowMiss`. A TEST body and
// a void helper just `return;`; a helper that owes its caller a value needs
// something to hand back.
//
// DELIBERATELY NOT LOAD-BEARING under ordinary GoogleTest execution: the
// `ADD_FAILURE()` on the same branch records a nonfatal failure the enclosing
// test retains, so a window miss cannot read as a pass whatever this value is.
// `--gtest_throw_on_failure` does not change that -- it throws AFTER reporting.
//
// THE CONDITION THAT HOLDS UNDER, stated rather than counted: NO CALLER
// INTERCEPTS THE FAILURE. `EXPECT_NONFATAL_FAILURE` and
// `ScopedFakeTestPartResultReporter` (gtest-spi.h) install a fake reporter that
// ABSORBS the failure and lets the enclosing test pass. The first caller that
// does makes this value the only remaining signal -- and `dispatch_aborted` is
// then ambiguous with a real `open()` / `on_inbound_frame()` outcome ([2d §6.5];
// the `dispatch_aborted` returns in `Session::live_write_serialized_`), so an
// assertion on the error could be satisfied by this synthetic one. The remedy at
// that point is a distinct harness result (`std::optional<expected_t<void>>`),
// not a different production error.
//
// The narrower reason that holds either way: a caller checking `has_value()`
// must not proceed on a fabricated success.
//
// ⚠️ HOISTING WIDENED THIS PRECONDITION'S POPULATION, so re-measure it rather than
// inheriting the answer. It used to range over the three files that defined the
// constant; it now ranges over every user of a shared one, and it grows silently
// with each new adopter. The recipe (a PROCEDURE — the answer is a measurement
// and belongs nowhere in this comment):
//
//     comm -12 \
//       <(git grep -l kWindowMissSentinel -- tests/ | grep -v pump_until_ready.hpp | sort) \
//       <(git grep -lE 'EXPECT_NONFATAL_FAILURE|ScopedFakeTestPartResultReporter|gtest-spi' \
//              -- tests/ | sort)
//
// The precondition holds while that intersection is EMPTY, transitively through
// includes.
//
// ⚠️ THIS FILE MUST BE EXCLUDED FROM THE ADOPTER SIDE, and the first version of
// this recipe was not — it reported a hit on the very first run. This header
// appears in BOTH lists for reasons that are not adoption: it DEFINES the
// constant, and the paragraph above NAMES the interceptor macros in prose. A
// recipe that flags its own documentation cries wolf on every future run, which
// is how a real hit gets waved through.
//
// ⚠️ Do not `head` either list — this is a claim about a whole SET, and truncating
// one side is how a set-difference reports clean. The interceptor grep is proven
// able to fire (it returns several files today), so an empty intersection is a
// measurement rather than a broken pattern.
//
// Hoisted here from three call-site files (#324's `send_path_test.cpp`,
// `tc_establishment_test.cpp`, `tc_seqnum_test.cpp`), which each carried a
// verbatim copy. Keeping it local was the right call while three files needed it
// and the shared header was being avoided mid-batch; adopting it across the
// remaining value-returning helpers made a fourth copy the wrong trade.
inline constexpr auto kWindowMissSentinel = fixpp::core::error::dispatch_aborted;

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
// What follows from that is only this: matching the shared stem ALONE cannot
// discriminate, and neither can matching only WITHIN the shared cause. Anything
// further -- which spans the existing witnesses actually use, and how many there are
// of each shape -- is a measurement, not a fact to cache here; see
// kDrainResidualCause's comment below for the procedure. An earlier version of this
// sentence enumerated the shapes and got the enumeration wrong.
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
// comment above used to imply they never do. This is the shared middle: 249
// bytes that were byte-identical in both `ADD_FAILURE`s. Hoisted for exactly the
// reason `kDrainResidual` was -- a fragment duplicated in two places drifts, and
// this one is four times longer than the stem that had already drifted on
// punctuation at birth.
//
// Byte-identity of the hoist is INTACT, measured against origin/main: `drain_or_report`'s
// composed message is 327 bytes at base and at HEAD, byte-for-byte identical, and
// `cancel_and_drain_or_report`'s composed message is identical through byte 421 -- the
// only difference is inside the tail this PR deliberately rewrote. So the hoist changed
// no byte any matcher can see. ⚠️ Splitting this fragment back into each producer
// (undoing the hoist) would make it genuinely unbound, but at the cost of re-duplicating
// "so a coroutine frame" across two producers -- exactly the drift this hoist exists to
// remove, in a stem this file already records above as having drifted on punctuation at
// birth once already. Not adopted for that reason; see below for what is actually bound.
//
// ⚠️ BYTES IN THIS CONSTANT ARE WITNESS-BOUND. That is the whole claim, and it is
// deliberately the whole claim: this comment records NO cached answer about WHICH bytes,
// which matchers, or how many. Four rounds of Gate B review each found a different such
// answer to be false, every one of them written by the round that had just deleted the
// previous wrong one. A procedure may be written down; a result may not.
//
// ⚠️ THE STRADDLE IS NOT FORCED -- a design observation, not a coverage result, which is
// why it is kept: `"teardown drain,"` discriminates the two drains perfectly and binds
// zero bytes of this constant. Nothing about the message shapes REQUIRES a matcher to
// carry into this text; whether any does is a property of how the matchers happen to be
// written, and is therefore a thing to measure, not to remember.
//
// To measure it: mutate the bytes you care about, rebuild `session_pure_tests`, run the
// WHOLE binary with no `--gtest_filter`, and -- before believing a green -- confirm the
// mutated bytes are actually in the built binary, e.g.
// `strings build/linux-clang-asan/bin/session_pure_tests | grep`. That last step is not
// optional: a stale binary is this procedure's only failure mode, and it fails toward
// clean. The same procedure answers the same question for the divergent TAIL below.
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
// repo does not build tests under a blanket `-Werror`, so do not describe it as
// compiler-ENFORCED. ⚠️ Re-derive that rather than trusting it -- this sentence used
// to name a LINE NUMBER for its one supporting example, which had moved, and to say
// "the one targeted use" when there are two (`-Werror=deprecated-declarations`, on
// WILL_FAIL probe targets in tests/session/ and tests/tls/). `grep -rn Werror` over
// the build files is the derivation; note also that `FIXPP_WERROR` and
// `fixpp_maybe_werror` in cmake/Helpers.cmake have ZERO call sites, so the blanket
// form is absent because the plumbing is dead, not because it was decided against.
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
//
// ── THE SITE-KEYED FORCING SEAM (optional `site`) ────────────────────────────
//
// Passing a site label opts the call into RUNTIME forcing: exporting
// `FIXPP_FORCE_WINDOW_MISS=<label>` makes exactly that site take its miss branch,
// with no source edit and no rebuild.
//
// WHY IT EXISTS. Verifying a migrated site means proving its miss branch actually
// reports, which means forcing a miss. The textual driver (`ci/pump-red-arm.sh`)
// does that by rewriting the source and rebuilding ONCE PER SITE, because forcing
// every site at once does not work: the first miss on a code path returns and masks
// every later site on it (PR #316 forced fifteen and covered seven). At batch-11
// scale that is one full rebuild per site. Here it is one build and N runs.
//
// ⚠️ IT IS A WEAKER WITNESS THAN TEXTUAL MUTATION, AND DOES NOT REPLACE IT. State the
// difference precisely -- this paragraph has been wrong in BOTH directions, once
// overstating the seam and once understating it, so re-derive it from the code below
// rather than trusting the sentence:
//   IT DOES exercise the SITE'S OWN miss block -- forcing returns false, so the caller's
//     `if (!run_window_then_ready(...))` body always runs, drain and report included.
//   ⚠️ EVERYTHING BELOW IS CONDITIONAL ON THE SITE'S STATE AT ENTRY, because FORCING
//     PRESERVES WHATEVER STATE EXISTS -- IT CANNOT MANUFACTURE ONE. Three separate claims
//     here have been written unconditionally and been false at the same site for the same
//     reason; state the condition or do not state the claim.
//   IT DOES reproduce a real miss's STATE *WHERE THE FUTURE WAS NOT YET READY WHEN THE
//     WINDOW OPENED*: forcing does not dispatch, so the frame is still suspended when the
//     miss branch runs and the DRAIN is what resumes it -- which is the lifetime obligation
//     (see (b) at the definition below). Where the future was ALREADY READY before the
//     window opened there is no suspended frame, the drain resumes nothing, and no forcing
//     mode can change that. `LO_InboundLogout_Confirm/close` is such a site, and
//     `ci/red-arms/batch12-spotcheck.tsv` records it as the batch's measured exception.
//   IT DOES NOT exercise the primitive's own pumping path: a forced call never touches the
//     io_context. Nothing in this tree depends on that, because both drains call
//     `ioc.restart()` themselves before pumping -- but a NEW miss branch that relied on
//     this function having restarted would pass here and fail for real. Do not write one.
//   IT DOES NOT judge the drain FLAVOUR, and NEITHER DRIVER'S FORCING MODE CHANGES THAT.
//     Which drain a site needs is a quiescence question the ANNOUNCEMENT cannot see. The
//     textual driver can catch a wrong flavour, because `ci/pump-red-arm.sh` fails an arm
//     whose drain reports a residual -- but that power comes from the FLAVOUR alone.
//     ⚠️ REASON, not a case list, because a case list invites a hunt for a case it missed:
//     BOTH drains are strictly LONGER pumps than the window they follow (`drain_or_report`
//     is `restart(); run_for(5s); poll_one()`; the clocked one is a 5 s sliced loop). So
//     zeroing DEFERS work into the drain rather than withholding it, and the residual
//     verdict reads the FIXED POINT -- what is still outstanding once pumping stops --
//     which both a zeroed and an unzeroed arm reach. An earlier revision of this line said
//     the residual check "has power only because forcing does not dispatch; the two are
//     coupled". False: the only thing surviving either pump is a mock-clock waiter, and a
//     real window cannot release one either. The zeroing and this check are independent.
//
//     ⚠️ WHEN THE FLAVOUR IS LIVE, since half this tree's sites could use either drain and
//     nothing said so. `cancel_and_drain_or_report` earns its `cancel_sleeps()` iff a
//     MOCK-CLOCK WAITER can be registered by the time the drain runs. In this tree that is
//     AMBIENT work, not the awaited frame: `run_liveness_loop` is co-spawned on the first
//     transition to Active and parks on a `heartbeat_interval` deadline. So the flavour
//     matters where the fixture sets a NON-ZERO `heartbeat_interval` AND the site is
//     reached at or after Active; where the fixture sets 0 there is no liveness loop and
//     the two drains are behaviourally identical. Derive it from the fixture's config, not
//     from what the awaited coroutine happens to be blocked on -- that question is the
//     wrong one and answering it suggests, falsely, that the clocked drain is decorative.
//
//     ⚠️ AND THE FLAVOUR IS NOT ALWAYS THE ANSWER, because a mock-clock waiter is only ONE
//     of the two things a pump cannot release. Pumping alone reaches everything EXCEPT
//     (i) a mock-clock waiter, released only by `cancel_sleeps()` -- the flavour; and
//     (ii) an op that never completes on its own, e.g. a coroutine parked in
//     `async_write`/`async_read_some` on a LIVE TRANSPORT, which no amount of pumping and
//     no `cancel_sleeps()` will end. For (ii) the answer is the transport-aware drain
//     (`cancel_and_drain_or_report` with a transport, or `quiesce_on_exit` with `.transport`
//     set), NOT a different clock argument. `ci/pump-red-arm.sh` says "Check the drain
//     FLAVOUR at this site" on a residual, which points at (i) only; if a residual survives
//     BOTH clock flavours, it is (ii). No #289 batch-12 site reaches (ii) -- the three
//     clock-free files use a `MinimalTransportFactory` with no parking transport -- so this
//     is written to be found rather than rediscovered.
// Use the seam for breadth and textual mutation to spot-check recipe correctness.
// Do not retire `ci/pump-red-arm.sh`.
//
// ⚠️ SILENCE IS AMBIGUOUS, WHICH IS WHY FORCING ANNOUNCES ITSELF. A run that produces
// no `kWindowMiss` report can mean the seam fired and the miss branch failed to
// report, or that the label matched NOTHING (a typo, a site that passes no label, or
// a site the run never reached) -- the same empty output from opposite causes, with
// the second failing toward clean. So a firing seam writes `kWindowMissForced` +
// the label to stderr BEFORE returning. A driver MUST require that line and
// treat its absence as "no such site", never as a silent pass.
// [[feedback_every_broken_instrument_in_this_repo_fails_toward_clean]]
//
// ⚠️ ADOPTION IS INCREMENTAL AND THE PARAMETER IS DEFAULTED, SO "the seam can force
// any site" IS FALSE. It forces only sites that pass a label. Sites migrated before
// the seam existed pass none and are reachable only through textual mutation. Derive
// which sites are forceable -- do not assume a batch's whole population is.
//
// The label is not new text: it already exists at every migrated site, as the operand
// of `ADD_FAILURE() << kWindowMiss << "<Site>"`. Adopting the seam relocates that
// literal into the call; it does not invent an identity.
//
// COST, because this primitive rejected an alternative on exactly this ground. The doc
// above records the ~1 ms-slice shape being refused as a ~40x per-call regression against
// sites measured at ~27 us. What the seam adds on a labelled call is one pointer compare,
// one acquire load on the magic static, and a `strcmp` only when the env var is set at all.
// Measured 2026-09-04 (clang -O2, this code shape, 50M iterations): **~0.6 ns/call**, i.e.
// ~0.002 % of that budget. An UNLABELLED call -- every site from batches 1-10 -- pays the
// pointer compare and nothing else.
// ⚠️ Re-derive rather than trusting the figure: it is a measurement of a code shape on one
// box, and the load-bearing claim is the RATIO to the ~27 us site cost, not the nanoseconds.
inline const char* forced_window_miss_site() {
    // Read ONCE per process. `getenv` per call would put a lookup on a path whose
    // whole reason to exist is that the #289 sites measured ~27 us -- and the value
    // cannot change usefully mid-run anyway. A test that `setenv`s after the first
    // pump will NOT be seen by this; that is deliberate, not an oversight.
    static const char* const forced = std::getenv("FIXPP_FORCE_WINDOW_MISS");
    return forced;
}

// Announced on stderr when the seam fires. Distinct from `kWindowMiss` on purpose:
// this says "the miss was FORCED here", the other says "the wait missed".
inline constexpr const char* kWindowMissForced = "#289 FORCED window miss at site: ";

template <class Fut>
[[nodiscard]] bool run_window_then_ready(asio::io_context& ioc, Fut& fut,
                                         std::chrono::steady_clock::duration window,
                                         std::chrono::steady_clock::duration grace = kPumpSlice,
                                         const char* site = nullptr) {
    // ⚠️ FORCING RETURNS WITHOUT DISPATCHING. It does not shrink the window and it does not
    // run it. Both halves of that are load-bearing and each fixes a different defect, so do
    // not "simplify" either away:
    //
    //   (a) RETURN FALSE rather than shrink the durations. Shrinking is NOT sufficient: the
    //       checks below are `run_for(window); if (ready()) return true;`, so a site whose
    //       future is ALREADY READY when its window opens returns true however small the
    //       window is. The seam would then ANNOUNCE while the site's miss branch never ran
    //       -- which `ci/pump-seam-arm.sh` reports as SILENT, i.e. "the miss branch did not
    //       report", reading as a defect in correctly-migrated code. MEASURED, not reasoned:
    //       a confirming Logout can complete a `close()` before its pump ever opens.
    //       Zeroing only the window, leaving `grace` live, was an earlier and weaker fix for
    //       this same defect; zeroing BOTH was the next one, and it is still not enough.
    //
    //   (b) DO NOT DISPATCH, so the drain faces a LIVE SUSPENDED FRAME. `run_one_until`
    //       tests `now < abs_time` BEFORE dispatching (see `pump_until`'s comment above), so
    //       a genuine miss leaves the awaited coroutine suspended -- and RESUMING a suspended
    //       frame is what a miss-branch drain does and where its LIFETIME obligation bites
    //       (#301's caller-temporary `feed`, #313/#316's miss-branch drains). A forced call
    //       that ran the real window would COMPLETE the frame first AT ANY SITE WHOSE FRAME
    //       THE WINDOW CAN COMPLETE, so the drain would resume nothing and that obligation
    //       would go unexercised. ⚠️ That qualifier is not decoration: at a site whose frame
    //       is held by a MOCK-CLOCK sleep the window completes nothing either, so running it
    //       would lose nothing there. What makes the qualifier bite in THIS batch is a
    //       measurement, not a property -- every site's awaited frame is held by queued work
    //       rather than a live clock sleep. Re-measure it for a new batch.
    //       ⚠️ WITNESSED, and not by a #289 arm: `PumpWindowMiss.FeedMissDrainsWhileCaller-
    //       TemporaryAlive` (tests/session/test_next_expected_msgseqnum.cpp) passes ZERO for
    //       BOTH durations and its comment records why -- a zero window alone does not miss,
    //       because the boundary grace dispatches the queued work and the future becomes
    //       ready. That is this property, checked in and proven RED.
    //       ⚠️ AND NOTE WHAT (b) DOES *NOT* BUY, because an earlier revision of this comment
    //       claimed it: it does not improve the odds of catching a wrong drain FLAVOUR. That
    //       needs a CLOCK-BOUND frame, and a real window does not advance a mock clock, so
    //       such a frame stays suspended whether the window ran or not.
    //       An intermediate revision DID run the window, to close a blind spot ("a miss
    //       branch that depends on the restarts having happened"), and that blind spot is
    //       HYPOTHETICAL: both drains call `ioc.restart()` themselves before pumping.
    //       Traded a real witness for a hypothetical one; reverted.
    //
    // The io_context is therefore left untouched, deliberately, per (b) and its last sentence.
    if (site != nullptr) {
        if (const char* forced = forced_window_miss_site();
            forced != nullptr && std::strcmp(forced, site) == 0) {
            // Announce BEFORE returning, so the line is emitted even if what follows hangs
            // -- a hang is exactly the blind-spot-(c) outcome a driver must be able to tell
            // apart from "the label matched nothing".
            std::fprintf(stderr, "%s%s\n", kWindowMissForced, site);
            std::fflush(stderr);
            return false;
        }
    }
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

// Site-labelled call taking the DEFAULT grace. A `const char*` argument selects this and a
// `duration` selects the form above, so the two do not compete for any call in this tree; a
// site needing a non-default grace spells both and uses the five-argument form.
//
// ⚠️ NOT "cannot bind" -- that phrasing was checked and is false. A bare `0` is a NULL
// POINTER CONSTANT, and the standard conversion to `const char*` beats the user-defined
// conversion to `duration`, so `run_window_then_ready(ioc, fut, w, 0)` would silently mean
// `site = nullptr` rather than `grace = 0`. Nothing in the tree passes a bare `0` for grace
// (every caller spells a duration), so this is a hazard of the SIGNATURE, not a live defect
// -- recorded as the condition rather than as "there is no such caller", which is a count.
template <class Fut>
[[nodiscard]] bool run_window_then_ready(asio::io_context& ioc, Fut& fut,
                                         std::chrono::steady_clock::duration window,
                                         const char* site) {
    return run_window_then_ready(ioc, fut, window, kPumpSlice, site);
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
// ⚠️ (#322) THE PER-SLICE CANCEL IS SAFE FOR A REAL CLOCK TOO, AND THAT IS NOT
// OBVIOUS FROM THE PARAGRAPH ABOVE, which reasons entirely about `mock_clock`.
// The delegation brought `system_clock_source` sites under this loop for the
// first time (`test_live_outbound_serialized.cpp` binds one at all seven of
// its guard sites -- five built purely for the guard, two shared with the
// session's own clock), and that implementation does something `mock_clock`
// does not: `cancel_sleeps()` does not complete waiters inline, it walks an
// in-flight map and `asio::post`s a `sp->cancel()` onto each live timer's own
// executor (src/core/system_clock_source.cpp, `cancel_sleeps`). Posting new work
// on EVERY slice is exactly the shape that could keep a context from ever
// draining.
//
// It terminates, for reasons read from the source rather than inferred from a
// green suite, and the strongest one is structural, though it covers only five
// of the seven sites. At those five (`:461`, `:759`, `:829`, `:1087`, `:1174`)
// `teardown_clock` is a freshly-constructed source that nothing but the guard
// holds, and the guard only ever calls `cancel_sleeps()` on it — so nothing can
// register a sleep there and `inflight` is empty BY CONSTRUCTION, and the
// per-slice cancel is INERT at those five sites, with nothing for it to do. The
// other two (`:706`, `:933`) share their clock with the session (`eng.clock =
// clock`), so the lever is live there, and it rests on two mechanisms that hold
// in the general case: the map holds `weak_ptr`, and only entries that still
// lock get a post; and `sleep_until` installs an RAII `dereg` guard that erases
// its entry on scope exit, covering the deadline-reached, cancelled and
// exception paths alike. So even a shared clock's cancelled sleep de-registers
// as its frame unwinds and later slices post nothing.
//
// Cost, measured at -O0 with ASan (the preset that actually runs this): on an
// empty container both clock types are indistinguishable from a loop with no
// cancel at all — under ~2 us against an 18-24 us slice, below the noise floor.
// The pathological never-erased case is bounded and linear, ~+18 us per slice per
// live entry, not runaway.
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
                    // FORMATTING CHOICE. `pump_or_report_throw` is load-bearing here --
                    // for `ioc.restart()`, `run_for()`, `poll_one()` and every handler
                    // they dispatch, none of which is `noexcept` -- and that holds no
                    // matter where `close()` sits. `close()` and `cancel_sleeps()` are
                    // *declared* `virtual ... noexcept = 0`: an override declared with a
                    // LOOSER exception specification is ill-formed, so every override is
                    // itself `noexcept`. That is a signature-time guarantee, not a runtime
                    // one -- `noexcept` is a property of the CALLEE, so an override whose
                    // BODY throws is still well-formed, and calls `std::terminate` at THAT
                    // function's own boundary regardless of which frame calls it -- moving
                    // `close()` outside this lambda changes nothing today. It stays inside
                    // anyway, per #308's refusal and #322's requirement: containment is already
                    // correct if either interface is ever relaxed to
                    // potentially-throwing, and it costs nothing to have it now.
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
                // matcher silently; spelling it out means re-derive by mutating the
                // fragment below, rebuilding `session_pure_tests`, running the WHOLE
                // binary with no `--gtest_filter`, and confirming the mutated bytes
                // are actually present in the built binary before believing a green.
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
// WHAT THE SLICING COSTS, measured rather than argued, because a per-slice loop
// at every plain `quiesce_on_exit` teardown site (population and re-derivation
// recipe: the "THE POPULATION" paragraph in
// `test_test_request_id_cross_session_race.cpp`, and its
// `grep -rn 'quiesce_on_exit [a-zA-Z_]*{' tests/` recipe) is exactly the
// shape that quietly gets expensive.
// Instrumented by breaking on the loop's own `restart()`, so the slice count is
// exact rather than a proxy:
//
//   session_logout_exchange (12 sites, mock_clock)        12 drains / 12 slices
//   LiveOutboundSerializedTest (7 sites: 5 inert / 2 live)  7 drains /  7 slices
//
// One slice per drain, at every site measured so far: the loop body cannot run
// zero times, so 19 drains totalling 19 slices means every drain currently in
// the suites quiesces inside a single `kPumpSlice`. That is a CONDITION, not a
// property of the loop -- a site whose work takes longer than `kPumpSlice` to
// quiesce takes roughly proportionally more slices, not an exact ⌈work / kPumpSlice⌉:
// per-slice overhead (measured above at ~18-24 us per pass)
// and the trailing `poll_one()` both affect which slice actually completes the
// work, so the count is an approximation, not arithmetic.
//
// And even at ONE slice this is NOT the same op sequence the old single-`run_for`
// destructor did: `cancel_sleeps()`, `ioc.restart()` and `poll_one()` were each
// ALREADY performed once by the old destructor -- they are not new at one slice,
// only REPEATED by every additional one. What one slice genuinely adds over the
// old destructor is two `steady_clock::now()`, a `std::min`, and a `stopped()` +
// deadline comparison -- plus `run_for(5s)` becoming `run_for(<=1ms)`.
//
// This is a DATED, SCOPED MEASUREMENT, not a property of the loop: the 19/19
// count above and the 83-slice figure below came from a manual debugger session
// (a breakpoint on the loop's own `restart()`), not a checked-in instrument, and
// both describe the CURRENT suites only. A future site whose quiescing spans
// several slices would still be correct, merely slower, and would not
// contradict this paragraph. The witness suites, which DO burn budget, counted
// 83 slices across 9 drains at that same session.
//
// The cost that does exist is confined to the already-failing path, and it is
// CPU, not wall: at a 5 s budget both loop shapes are budget-bound at 5000 ms
// wall, and the sliced one spends ~89 ms CPU idle / ~136 ms against a re-arming
// sleeper, versus <1 ms for the single `run_for`. A test on that path is already
// reporting `kDrainResidual`.
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
    // would put `transport->close()` back in THIS frame -- and that changes
    // nothing today either way: `close()` and `cancel_sleeps()` are *declared*
    // `virtual ... noexcept = 0`: an override declared with a LOOSER exception
    // specification is ill-formed, so every override is itself `noexcept`. That is
    // a signature-time guarantee, not a runtime one -- `noexcept` is a property of
    // the CALLEE, so an override whose BODY throws is still well-formed, and calls
    // `std::terminate` at THAT function's own boundary whether it is called
    // from here or from inside the primitive's lambda (see the primitive's own
    // ⚠️ THE CLOSE IS INSIDE THE GUARD paragraph). It stays inside the
    // primitive anyway, per #308's refusal and #322's requirement: containment
    // is already correct if either interface is ever relaxed to
    // potentially-throwing, and the primitive taking the transport is what
    // lets the refusal survive the delegation.
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
