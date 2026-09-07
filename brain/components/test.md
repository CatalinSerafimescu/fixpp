---
type: Component Decision Map
title: Test infrastructure — the seams are designed surfaces, not test-only afterthoughts
description: A pluggable Clock, a public mock transport, and an LD_PRELOAD allocation interceptor. The seams exist because the constitution required them, not because tests needed them later.
status: stable
refs:
  - include/fixpp/transport/test/mock_transport.hpp
  - .specify/constitution.md
  - tools/check_alloc.py
  - tests/support/pump_until_ready.hpp
  - ci/pump-census.sh
  - ci/pump-get-sweep.sh
  - ci/pump-red-arm.sh
  - ci/pump-seam-arm.sh
  - ci/pump-label-uniqueness.sh
  - ci/cxx_blank.py
codegraph_entry: [mock_transport, Clock, system_clock_source]
constitution: ["§VII", "§VII.4", "§VIII.5"]
---

# Test infrastructure

> ## ⚠️ The CODE is authoritative. This page is not.
>
> It exists because `test` is a catalogue family with **no owning design doc**, and because the
> testing seams here are *architectural decisions* that a reader will otherwise meet only as
> unexplained interfaces.

## The seams are the design, not scaffolding

`[const §VII]` requires that everything touching the outside world be pluggable so the FSM and parser
can be tested without real I/O. That requirement is why these exist at all — they are not conveniences
added afterwards:

| Seam | Replaces | Where |
|---|---|---|
| `fixpp::core::Clock` | wall-clock time | heartbeat, `SendingTime`, reconnect schedules — a mock steps time deterministically instead of sleeping |
| `mock_transport` | a socket | drives the session FSM through a pre-recorded byte stream |
| `MessageStoreFactory` / `TransportFactory` | disk, network | see [`plugin-factory-ownership.md`](./plugin-factory-ownership.md) |

⭐ **Consequence worth holding on to:** if a new subsystem cannot be tested without real I/O, the
missing piece is a **seam in the design**, not a cleverer test. That is the same reasoning that put a
`Clock` interface in a FIX engine.

## ⭐ `mock_transport` is a PUBLIC header that refuses to compile in production

It lives under `include/`, not `tests/` — a deliberate choice, so external consumers can drive the FSM
in *their* tests. The safety comes from a build-system token: production targets do not define it, and
the header `#error`s without it.

> **The pattern is worth copying: gate by a token the build controls, not by directory placement.**
> "It's in `tests/` so it can't ship" is a convention; an `#error` is checked by the compiler. Same
> family as the `static_assert` idiom on [`quickfix-compat.md`](./quickfix-compat.md).

## Allocation discipline is enforced by an interceptor, not by review

`[const §VIII.5]` demands zero allocation between parse and `fromApp`. That is checked by
**`LD_PRELOAD`-ing a malloc interceptor** around dedicated guard binaries and failing if any
`malloc`/`free` is seen between markers.

⚠️ **Two things to know before trusting a green run.** The instrument is Linux-only by construction —
a passing Windows build proves nothing about allocation. And a guard test only covers the window its
markers enclose: *"zero allocations"* means *zero in that window*, never *anywhere*.

**Re-derive what is actually guarded** — the set changes, and a list here would rot:

```bash
ls tests/alloc_guard/ && sed -n '1,12p' tools/check_alloc.py
```

## ⭐ Bounded pumps (#289): the hazard is the unconditional `get()`, not the fixed window

Tests drive a manually-pumped `io_context`. The idiom
`ioc.run_for(W); ioc.restart(); fut.get()` **deadlocks** whenever the awaited op posts its completion
after `W` closes and nothing pumps again — reported by ctest as a timeout, and on a lane with no ctest
timeout configured, as a wedged job. `tests/support/pump_until_ready.hpp` holds the replacements.

**Why the window is PRESERVED rather than replaced by a self-driving pump.** Two reasons, both
measured and both still binding:

- `pump_until_ready` takes a **work guard**, so `run_for` cannot drain early and every call burns a
  slice. That is a documented per-call cost floor, and the migrated sites are microsecond-scale.
- The first transition to Active `co_spawn`s a **detached** `run_liveness_loop()`, and `co_spawn`
  POSTS its first resumption. An early-exit pump that stopped at future-readiness would leave that
  task unserviced; running the original window services it exactly as before.

So `run_window_then_ready` runs the caller's own window, then grants **one** boundary grace slice —
because `run_one_until` tests `now < abs_time` *before* dispatching, leaving a handler that became
ready at the instant the window closed merely QUEUED. The grace is not a CI tolerance and must not be
grown into one.

### The SECOND shape: `ioc.run()` with no window at all (#289 batch 17)

`run_window_then_ready` is not the whole story, and a reader who finds only it will reach for the
wrong primitive. A large class of sites had **no window**:

```cpp
auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
ioc.run();
auto val = fut.get();        // unconditional
```

`run()` returns when the context has **no work left**, which is not "the coroutine finished" — and
the live failure is not the exotic one. These tests reuse ONE `io_context` across several
`co_spawn`/`run` pairs and hand-write the `restart()` between them; **on a context still stopped from
the previous `run()`, `run()` returns immediately having dispatched nothing**, and the `get()` below
never returns. One deleted line.

`run_to_exhaustion_or_report(ioc, fut, "Site")` is the replacement. Three things about it differ from
`run_window_then_ready` and each is deliberate:

- **`ioc.run()` is preserved verbatim.** Bounding it is a *different* hazard (a context that always
  has work never returns), covered by ctest's per-test timeout (#337); a budget here would trade a
  wedge for a false RED on the slowest sanitiser leg.
- **It touches no context state on the success path** — no `restart()`. The migrated sites state that
  decision themselves, and a helper that restarted would take it over at every call site.
- **The miss branch is INSIDE the helper**, unlike every other #289 recipe, because at these sites it
  does not vary: no clock to cancel against, no window to have missed. The cost is that gtest reports
  the header's line; the report streams the site label instead.

⚠️ **A forced-miss arm proves the branch RUNS, not that it is REACHABLE.** The pairing that justifies
the class is `ci/red-arms/batch17-genuine-miss.sh`: base-shape-with-restart PASSES, base-shape-without
WEDGES, guarded-without REPORTS. Read all three; the third alone says nothing about the second.

⚠️ **The teardown shape is a property of the FIXTURE, not a style choice.** A drain is what RESUMES a
suspended frame, so draining in the wrong scope is worse than not draining:

| the `Session` is… | teardown |
|---|---|
| owned by the fixture | drain in the fixture destructor **body** |
| a block-local declared AFTER the fixture | drain on the **miss branch**, in the scope that still owns the storage — a destructor drain there is a measured `stack-use-after-scope` |

⚠️ **`ioc` must stay the FIRST fixture member.** Nothing holding a strand taken from the context may
outlive the context.

### The THIRD shape: a STAGING window before a mock-clock advance (#289 batch 18)

Neither of the two above. There is no `.get()` near it at all, so the census cannot see it; the
terminal half is usually already migrated and correct.

```cpp
auto close_fut = co_spawn(ioc, sess.close(graceful), use_future);
ioc.run_for(50ms);                    // <- STAGING window, blind
ioc.restart();
clock->advance(seconds{3});           // <- the discriminator
if (!run_window_then_ready(ioc, close_fut, 200ms)) { ... }   // terminal half, already migrated
```

The window's job is to get the coroutine to its **mock-clock sleep** before the test advances that
clock. If it has not parked when the window returns, the advance lands on a timer that is not yet
armed and is **LOST** — unrecoverable, not slow, because nothing advances the clock again and no
later pump rescues it. It surfaced as `session_tc_liveness` failing 1-of-369 on a starved
`linux-clang-asan` lane; fixed exemplar fixpp `4179da94`.

⚠️ **A LONGER `run_for` IS THE FIX EVERYONE REACHES FOR FIRST AND IT IS NOT A FIX** — it lowers the
probability and keeps the failure mode. The migration is an **observable staging condition**:
`pump_until(sess.state() == fsm_state::LogoutSent)`. `LogoutSent` is exactly "Logout emitted,
parked, not complete", so the window stays a *staging* window rather than becoming a *completion*
window — which is the objection that kept these sites blind.

⚠️ **#316 TOUCHED ONE OF THESE FUNCTIONS AND WROTE A COMMENT JUSTIFYING THE BLINDNESS.** Its premise
was right (*"the future must still be PENDING when this returns, so it is a staging window, not a
completion window"*) and its conclusion was wrong: the premise rules out a COMPLETION check, it does
not license a blind window. Batch 18 found the identical comment shape a second time, in
`cancellation_two_phase_test.cpp`. Both were **replaced**, not annotated.

⚠️ **A POST-HOC `EXPECT_EQ(state, LogoutSent)` IS NOT THE FIX EITHER**, and three sites had one. It
observes the right thing at the wrong time — it converts a lost advance into a confusing failure
instead of preventing it, and being non-fatal it lets the advance run anyway. Waiting on the same
predicate makes the same claim and removes the race.

**The instrument is `ci/mock-clock-staging-sweep.sh`**, which classifies each `advance()`/`step_to()`
by the nearest preceding pump inside its enclosing brace block. `poll()`/`run()` are NOT candidates —
they return on "no ready work", so starvation lengthens them, never shortens them; only a WALL-CLOCK
bound can return with the staging work still queued. Its verdict `NO-PUMP-IN-SCOPE` is an
**escalation**, not a clean bill: a fixture helper can stage while its caller advances, and no
same-function analysis finds that pair. **Quote the candidate count as "N that the sweep can see".**

⚠️ **THE FORCING SEAM IS THE WRONG INSTRUMENT FOR THIS SHAPE.** A forced MISS cannot catch a
spurious HIT — the hazard is a REAL `asio::steady_timer` completing the awaited future while the
mock-clock path never runs (PR #337: green in 2202 ms with all four forced-miss arms passing). The
arms that work inject the defect: delete the `clock->advance()` and assert RED, and force the staging
predicate to `true` and assert RED. `ci/red-arms/batch18-lost-advance.sh`.

⚠️ **AND THE OBVIOUS MECHANISM-LEVEL BARRIER WAS BUILT, MEASURED, AND REMOVED.** `mock_clock` already
computes the woken set in `advance()`, so exposing the parked-waiter count looks strictly better than
a lexical sweep — no lookahead, no same-function constraint, a positive observation. At both sites no
FSM state covers, it FAILED TO DISCRIMINATE, proven by running rather than by reading. `HbTrTest`'s cell passes with
its staging window starved to `run_for(0ms)`; `AdminDistinctNow`'s goes RED when starved but RED with
the barrier in place too, because what starving removed there was the TERMINAL collection window.

⚠️ **The mechanism first written for that disposition was WRONG, and the correction is the durable
half.** Not monotonicity — **how the sleep is ARMED**. `sleep_until` fires immediately when
`deadline <= steady`, so a late arm is rescued exactly when the deadline it names is already past: an
arm from a **stored anchor** (`last_inbound_steady_ + heartbt_int`) names a passed instant and is
RESCUED, while a **now-relative** arm (`steady_now() + logout_disconnect_timeout_ms`) names a NEW
future instant and is LOST. The condition on the first row: the anchor must PREDATE the advance —
inbound traffic refreshes it to `steady_now()`, after which that arm is now-relative in effect. That
rule decides candidate-vs-defect for the whole class, and it is why no clock-side fix reaches the
second row. It would have shipped as an assertion that cannot go RED for its own
class. Rebuild it only against a site where a mutation shows it load-bearing.

### The FOURTH shape: a `.get()` INSIDE the coroutine (#289 batch 19)

The three shapes above are all caller-side: the `.get()` runs on a thread that is *outside* the
io_context, and the whole #289 remedy is to give that thread a bounded way to wait. This one is
different in the only way that matters.

```cpp
auto fd = co_spawn(ex, drain(), use_future);   // spawned from inside a coroutine
co_await yield_n(8);                           // the window, counted in YIELDS
fd.get();                                      // <- runs ON the pumping thread
```

**The bounded outer driver does not bound this.** `fd.get()` executes inside the handler the driver
dispatched, so the block happens *within* `ioc.run_for(100ms)` / `ioc.run()`. The driver never gets
another turn: a 5 s deadline loop never re-tests its deadline, and `run_to_exhaustion_or_report` —
already the #289 guard for the outer future — is itself inside `run()`. The binary hangs and ctest
reports a timeout naming no site.

⚠️ **That is a measurement, not a reading of asio.** `ci/red-arms/batch19-coroutine-side-get.sh`
injects a defect no yielding can fix (the holder is never released, so the drain can never finalize)
and runs both shapes under it: the pre-batch shape **WEDGES** at the arm's budget, the guarded shape
**REPORTS** naming the site. Two arms, and they must not be collapsed.

⚠️ **THE WINDOW IS NOT THE MUTATION HERE, and reaching for it is the obvious mistake.** Starving the
yields is *recovered* by the guard's grace — correctly, that is what the grace is for — so it
produces a PASS, not a report. The injected defect has to be one no amount of yielding fixes.

The primitive is `yield_window_then_ready(fut, window, site, grace = kYieldGrace)` in
`tests/support/pump_until_ready.hpp` — the coroutine-side twin of `run_window_then_ready`, and it
**preserves the window** the same way: the `window` posts are issued unconditionally, so a site that
used `co_await yield_n(N)` for its own interleaving semantics keeps every one of those N yields.

⚠️ **IT REPORTS VIA `kWindowMiss`, AND THAT LITERAL WAS REWORDED TO BE UNIT-NEUTRAL RATHER THAN
DUPLICATED OR DISCLOSED.** The handover priced this item at "a fourth report literal, which is a
change to every driver that greps a `REPORT_TAIL`" — the thing batch 17 got wrong, shipping seven
correctly-driven sites as FLAG. The first draft accepted that price, reused `kWindowMiss` unchanged,
and shipped a **20-line comment explaining why the sentence was wrong** for the new caller ("run
window" and "one boundary grace slice" are durations; here they are yields). Both halves were
mistakes and the `/simplify` altitude pass caught them:

- **The cost estimate was never measured**, and no corrected size is written here either — a
  corrected estimate rots on the same schedule as the one it replaced. What is durable is **how to
  enumerate the dependants**, and every shortcut was tried and failed: not by the constant's NAME
  (a gtest-spi matcher binds the message, naming nothing); not by a driver's variable name
  (`git grep REPORT_TAIL` misses `pump-red-arm.sh`'s `TAIL=`); not by ONE fragment (drivers bind
  the tail, matchers bind the head); and **not with `git grep` at all**, because every #289 batch
  adds an *untracked* red-arm script carrying the tail, so a tracked-files-only search fails toward
  clean over exactly the files the batch is adding. The canonical recipe lives at `kWindowMiss`.
- **A knowingly-wrong sentence plus a disclosure is not a fix.** The literal now reads "its preserved
  **window**" and "the **bounded grace that follows**" — true of both primitives, at all ~540 sites.
  Both drivers re-proven against the new tail: seam arm 11/11 RED, `pump-red-arm.sh` RED as required.
- ⚠️ **AND THE REWORD BROKE A TEST MATCHER THE ENUMERATION COULD NOT SEE.** `EXPECT_NONFATAL_FAILURE`
  in `test_next_expected_msgseqnum.cpp` binds the literal's **head**; both probes used to size the
  change (`git grep REPORT_TAIL ci/`, and the literal's **tail**) were partial, and each was partial
  in a different way. **Enumerate a shared literal's dependants over the WHOLE tree, by a fragment
  the change does not touch** — and check `EXPECT_NONFATAL_FAILURE` / `ScopedFakeTestPartResultReporter`
  specifically, because a matcher bound to a message is invisible to every search keyed on the
  constant's NAME. The matcher was then proven load-bearing (old wording ⇒ RED), which is what makes
  the fix a fix rather than a second guess.

⚠️ **`yield_window_then_ready` IS DELIBERATELY ABSENT FROM `DRIVING_FREE_FUNCTIONS`** in
`tools/audit_co_spawn_named_closure.py`, and that is the one instrument where "teach it the new
spelling" was the wrong answer. It takes no `io_context&`, so that tool cannot resolve which context
it drove — which is the whole basis on which an entry there credits a caller. The exclusion errs
LOUD (a named closure driven only by it reads FLAG, never SAFE) and is pinned by a self-test arm.

⚠️ **`ci/mock-clock-staging-sweep.sh` NOW GATES THE ESCALATION BUCKET.** Every `NO-PUMP-IN-SCOPE`
row must carry a `KIND <letter>` in the comment block above it, drawn from the taxonomy table in the
sweep itself; a row without one exits 2. The first version of that control was per TREE — a set of
every letter used anywhere — and a hostile round falsified it in one line: delete ONE row's
disposition and every letter is still present somewhere, so it printed `ok`. **A control whose
population is the whole tree cannot see a single site lose its answer.**

⚠️ **`CALLER-ONLY × HELPER 9 → 1` IS A BUCKET, NOT THE CLASS.** `ci/pump-get-sweep.sh` classifies by
executor class × pump shape, and **neither axis captures "inside a coroutine"**. The eight sites
batch 19 took are the ones that happened to land in that bucket; the same shape can sit under POOL,
THREADED or THREAD-IN-FILE and the sweep would say nothing. It also under-counts *within* a file it
does classify: three live sites of this exact shape were `for (auto& f : futs) f.get();`, and a
range-for variable is not a receiver the sweep can trace back to a `co_spawn`. Do not read the
bucket going to 1 as the shape being done.

#### Batch 20 answered that with an AXIS, and the axis found two more sites

`ci/pump-get-sweep.sh` now carries a third axis — **call-site scope**, `CORO` or `CALLER-SIDE` —
alongside executor class and pump shape, plus tracking for the container shape. Together they moved
**39 previously-invisible sites** into the candidate list, of which **2** were the coroutine-side
wedge shape in `tests/sync/test_drain_immediate_destroy_after_reap.cpp` — a file batch 19 never
opened, whose own hang message already names `futs.get()` as a suspect. Both migrated; `CORO`
unguarded is now **0**.

- ⚠️ **THE SCOPE DISCRIMINATOR IS THE RETURN TYPE, NOT A KEYWORD.** "The enclosing scope contains a
  `co_await`" is satisfied by a TEST body that merely *spawns* a coroutine lambda, so it marks the
  caller-side `.get()` **after that lambda's closing brace** as coroutine-side and reports nearly the
  whole corpus. A block introduced by a function or lambda returning `asio::awaitable<…>` cannot be
  entered from outside. The two controls that separate the readings put one `.get()` inside such a
  lambda and one immediately after it, and both die under a mutation of their own rule.
- ⚠️ **A ZERO FROM A NEW AXIS NEEDS A KNOWN-NON-ZERO CORPUS.** The synthetic controls prove the axis
  *can* say `CORO`; a wrong root or a broken traversal survives them.
  `ci/red-arms/batch20-coroutine-axis.sh` runs the **current** sweep against `tests/` at the
  pre-batch-19 commit — same instrument, older corpus — and requires non-zero. It reports **13**
  there and **0** here. Extract only the corpus: checking out the whole old tree would run the *old*
  sweep, which has no axis, and pass by construction.
- ⚠️ **Container coverage is ONE SPELLING, not the class.** `push_back|emplace_back(co_spawn(…))`
  consumed by `for (auto& e : c) e.get()` is what is tracked. An earlier draft of that disclosure
  listed the evasions it expected — an index loop, `futs[i].get()`, a moved-from container — and a
  check found **none of them** in the tree. The condition and the re-derivation recipe ship; the
  hypothetical enumeration does not.
- The sweep's controls now run in **tier 1** (`bash ci/pump-get-sweep.sh --disposition`). It still
  does not gate the candidate list — that list is not pinnable — but until this batch nothing ran
  its classifier controls at all, so a regression in them would have surfaced only as a number
  nobody could tell was wrong.

### ⚠️⚠️ A state assertion after a helper call is NOT a masking barrier

When designing forced-miss (RED) arms, the natural model is that a helper's miss-branch `return` will
be caught by the caller's next `ASSERT_EQ(sess.state(), …)`, aborting the test and masking every later
site on that path. **That model is wrong, and it is wrong in the direction that makes an arm look
masked when it is live.**

The miss branch calls `cancel_and_drain_or_report`, whose drain is generously budgeted. That drain
**completes the suspended coroutine** — which is its entire purpose — so the session reaches the state
the assertion is checking, the assertion PASSES, and execution continues into the sites the model
predicted were unreachable.

- **Trigger:** you are partitioning forced-miss arms and reasoning about which sites mask which.
- **Procedure:** treat the predicted firing count as **falsifiable**, run the arm, and count. What
  actually masks is an early `return` reaching a caller that cannot continue *for a reason the drain
  cannot repair* — not a state check the drain satisfies on its way past.
- ⚠️ Count on the **miss message's own distinctive tail**, not on the site label: the drain's residual
  report carries the same label, so a label-only count conflates the two.

This is the same family as [`message-store-quiescence.md`](./message-store-quiescence.md)'s warning
that a cleanup which completes a pending operation writes the state your verdict then reads.

**What remains to migrate is derived, never remembered** — the pin is an exact set, checked both
directions:

```bash
bash ci/pump-census.sh        # exit 0 iff the tree matches ci/expected-pump-sites.txt
bash ci/test-pump-census.sh   # the census's own assertions
```

⚠️ **The census has THREE blind spots and none is visible in the pin.** A site you deliberately
preserve de-censuses itself when a neighbour's migration shifts its `.get()` past the lookahead; a
site whose `.get()` was always beyond it was never *in* the pin and cannot leave it; and — the one
that widening cannot reach — **the window may not be lexically present at all**, because the pump is
indirected through a helper (`f.drain();` between the `co_spawn` and the `get()`). There is no
`ioc.run_for` to anchor on, so no lookahead width finds it.

**An empty pin would be a statement about the census, not about the tree.** The registry lives in
`ci/pump-census.sh`'s header — add to it, do not renumber it.

⭐ **The sweep that answers "does this file still have an unguarded `get()`?" must start from the
`get()`, not from the pump** — `ci/pump-get-sweep.sh`. A detector that recognises helper SHAPES can
only find the shapes its author thought of, and the cost of a miss is a wedged lane rather than a
failed assertion. Anchor on the thing every hazard must reach, and require the guard to NAME the
future it guards.

⚠️⚠️ **THAT SCRIPT'S OWN HISTORY IS THE WARNING: each fix for a false-clean introduced the NEXT
one.** Round 1 anchored on a single physical line, so a split declaration was invisible. The fix
spliced statements — and counted parens over raw text, so an unbalanced `(` inside a *string
literal* swallowed a whole test body, while a `continue` after the declaration skipped that region's
own `get()`. The fix for the foreign-guard mode narrowed it in RADIUS (a lookback window → the whole
file) rather than eliminating it, so guard state leaked across tests. **Three rounds, three
false-cleans, each created by the previous remedy** — the exact shape `failure-classes.md` class 2
names, arriving in an instrument rather than in prose. Every mode now ships as a control; add one the
day a new evasion is found, and do not trust a clean file as proof.

⚠️ **An undisclosed limitation is how this recurs — and a disclosure can be invisible rather than
absent.** That script's scope-limitation paragraph was once written with literal `\n` escapes
instead of newlines: a single 613-character line nothing would ever read, including through
`--help`. State limitations, and then *look at the rendered file*.

⚠️ **A pin row can sit in DEAD CODE.** The census is lexical and has no notion of reachability, so a
migrated site in an uncalled fixture helper drops a pin row while being unable to fire in any arm.
Read a non-firing RED arm as a question about reachability before assuming the arm is broken.

⭐ **A migrated site's miss branch is DEAD CODE under normal execution, so "the tests still pass" is
evidence about the HIT path only** — `ci/pump-red-arm.sh` forces each site's miss and requires
it to report. Two properties are load-bearing and neither is obvious:

- **One arm per rebuild.** PR #316 forced fifteen at once and covered **seven**: the first miss on a
  code path returns, and every later site on that path is never reached. Two sites in one helper, or
  one helper a driver calls twice, mask each other exactly this way. The rebuild cost per arm is the
  method, not overhead to optimise away.
- **An arm zeroes BOTH durations *and* forces the verdict, and neither alone is sufficient.**
  `((void)run_window_then_ready(ioc, fut, 0ms, 0ms), false)`. The wrapper is what works at a site
  whose future is ALREADY READY when its window opens; the zeroing is what stops the call
  dispatching, which is what leaves the awaited coroutine SUSPENDED so the miss branch's drain has
  a live SUSPENDED frame to resume **wherever one exists at entry** — which is where a
  miss-branch drain's LIFETIME obligation
  bites (#301/#313/#316), and is witnessed by `PumpWindowMiss.FeedMissDrainsWhileCaller-
  TemporaryAlive`, which zeroes both durations for exactly this reason. ⚠️ It does *not* help
  catch a wrong drain FLAVOUR — that needs a clock-bound frame, which a real window cannot
  complete either, so both forms see it equally. An earlier revision of this bullet said otherwise.
  **Do not restate the rule here beyond that sentence**; it lives at `run_window_then_ready`'s
  definition, and two superseded versions of it (*"zero the window"*, then *"zero both"*, then
  *"force the verdict, leave the durations"*) each survived in this bullet until someone read the
  header instead.

⚠️ **A TIMEOUT IS A DIFFERENT FINDING FROM A SILENT ARM, and collapsing them loses the interesting
one.** A forced miss HANGS rather than reports when the site's pump is INDIRECTED through a helper
(census blind spot (c)) — the call the arm forced is not the one the test waits on. The driver
reports that as `INCONCLUSIVE` and names it, which is the same question the dead-code note above asks: a
non-firing arm is a claim about REACHABILITY before it is a claim about the branch.

⚠️ **The driver is an instrument, so its REDs mean nothing until it is shown able to report non-RED.**
Seed a site that cannot report — delete one `ADD_FAILURE()` — and require the driver to call that arm
`SILENT`. N REDs from an unseeded driver prove only that it runs.

### The RUNTIME seam — `ci/pump-seam-arm.sh`, and why it does not retire the textual driver

`run_window_then_ready` takes an optional trailing site label. When it is passed,
`FIXPP_FORCE_WINDOW_MISS=<label>` makes exactly that site take its miss branch at RUNTIME, so a batch
costs one build and N runs instead of one rebuild per arm. That is what makes an ~80-site batch
verifiable at all; "one arm per rebuild" above remains the rule for the TEXTUAL driver, and the
masking reason behind it is unchanged — the seam does not fix masking, it makes forcing one site at a
time cheap enough to do everywhere.

⚠️ **It is a WEAKER witness, and the difference is not a detail.** Use the seam for breadth and
`ci/pump-red-arm.sh` to spot-check correctness; **keep both**. ⚠️ **The precise difference is stated
at `run_window_then_ready` in `tests/support/pump_until_ready.hpp` and is deliberately NOT copied
here.** It has been wrong in both directions already — once overstating the seam, once understating
it — and a third copy is a third thing to keep true and the one nobody updates.

⚠️ **THE SEAM'S SILENCE HAS TWO CAUSES AND ONE FAILS TOWARD CLEAN.** No report can mean the miss
branch did not report, or that the label matched nothing at all — a typo, a site that passes no
label, or a site the run never reached. Identical empty output, opposite meanings. So the primitive
ANNOUNCES on stderr *before it pumps*, and the driver requires that line: no announcement
is `NO-SUCH-SITE`, never a pass. The driver carries a negative-control arm forcing a label no site
carries, to prove that verdict is reachable.

⚠️ **The seam forces only sites that PASS a label, which is a strict subset of the migrated sites.**
Everything migrated before the seam existed passes none and is reachable only through textual
mutation. Derive which sites are forceable — `ci/red-arms/batch11-labels.txt` is one batch's list,
not the population.

⚠️ **Locate a label with `strings` on the BINARY, never a source grep.** A stale binary is this
procedure's only silent failure mode and it fails toward clean: the source says the label exists
while the binary that actually runs contains no such string.

## ⚠️ The catalogue's `test` rows are not a coverage measure

Every `test` row reads `backlog`, and — as with `nfr` — **that is not evidence the work is absent**;
see [`nfr-and-tooling.md`](./nfr-and-tooling.md) for the condition and the derivation recipe. The test
tree is large and the CI tiers are real. **Do not read this family's status column as coverage.**

## Related

- [`nfr-and-tooling.md`](./nfr-and-tooling.md) — the status-column caveat, and where the CI gates live.
- [`transport.md`](./transport.md) — the interface `mock_transport` implements.
