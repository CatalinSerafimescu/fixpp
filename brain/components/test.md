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

⚠️ **The teardown shape is a property of the FIXTURE, not a style choice.** A drain is what RESUMES a
suspended frame, so draining in the wrong scope is worse than not draining:

| the `Session` is… | teardown |
|---|---|
| owned by the fixture | drain in the fixture destructor **body** |
| a block-local declared AFTER the fixture | drain on the **miss branch**, in the scope that still owns the storage — a destructor drain there is a measured `stack-use-after-scope` |

⚠️ **`ioc` must stay the FIRST fixture member.** Nothing holding a strand taken from the context may
outlive the context.

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
evidence about the HIT path only** — `ci/pump-red-arm.sh` forces each site's window to miss and
requires it to report. Two properties are load-bearing and neither is obvious:

- **One arm per rebuild.** PR #316 forced fifteen at once and covered **seven**: the first miss on a
  code path returns, and every later site on that path is never reached. Two sites in one helper, or
  one helper a driver calls twice, mask each other exactly this way. The rebuild cost per arm is the
  method, not overhead to optimise away.
- **Zero BOTH durations.** `run_window_then_ready(ioc, fut, window, grace)` defaults `grace` to
  `kPumpSlice`, so zeroing only the window leaves a live grace slice that usually still satisfies the
  future — a vacuous arm that passes without entering the branch it claims to test.

⚠️ **A TIMEOUT IS A DIFFERENT FINDING FROM A SILENT ARM, and collapsing them loses the interesting
one.** A forced miss HANGS rather than reports when the site's pump is INDIRECTED through a helper
(census blind spot (c)) — the arm zeroed a window the test never waited on. The driver reports that
as `INCONCLUSIVE` and names it, which is the same question the dead-code note above asks: a
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

⚠️ **It is a STRICTLY WEAKER witness, and the difference is not a detail.** The seam exercises *the
primitive's forced path* under a site's label. The block it runs is the same block at every site, so
it CANNOT see a site whose own miss branch has the wrong drain flavour, a missing `return`, or a
sentinel a caller can confuse with a real value. Only textual mutation witnesses the recipe as
written at that site. Use the seam for breadth and `ci/pump-red-arm.sh` to spot-check correctness;
**keep both**.

⚠️ **THE SEAM'S SILENCE HAS TWO CAUSES AND ONE FAILS TOWARD CLEAN.** No report can mean the miss
branch did not report, or that the label matched nothing at all — a typo, a site that passes no
label, or a site the run never reached. Identical empty output, opposite meanings. So the primitive
ANNOUNCES on stderr *before* zeroing the window, and the driver requires that line: no announcement
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
