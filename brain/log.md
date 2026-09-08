---
type: Log
title: SecondBrain change log
status: stable
---

# Log

- **2026-09-08 — #289 batch 21, the per-file condition that was false.**
  `failure-classes.md` gains **class 11** (*an inherited obligation can rest on a false premise, and
  discharging it faithfully hides that*) and a class-1 bullet (*a classification reached by FALLBACK
  is not a measurement, even when it is right*). Batch 20 handed over ~27 container `.get()` sites
  with a per-file suspension-point obligation; `asio::co_spawn` holds `outstanding_work.tracked` on
  the SPAWN executor for the frame's lifetime, so the obligation collapses to two lexical clauses and
  the second — a `run()` on an already-stopped context — is a hazard the instruction never named.
  Five arms in `tests/sync/test_co_spawn_work_guard_contract.cpp`; a DRIVE axis in
  `ci/pump-get-sweep.sh`; hazard (a) in `tests/support/pump_until_ready.hpp` corrected by CONDITION
  plus a pointer to the arms, per class 2. Record:
  `decisions/speckit/pr-batch21-the-work-guard-and-the-condition-that-was-false.md`.

- **2026-09-08 — #394, a promptness band that was wrong in both directions.**
  `failure-classes.md` class 1 gains the threshold bullet: a band that flakes is usually also blind,
  and the flake is the half that gets noticed. 088's `EXPECT_LT(elapsed, 100ms)` was **watchdog ÷ 10**
  by its own comment — it went red at 357 ms on a TSan lane with no defect, and stayed **green**
  under a mutation that made cancellation take 50 extra dispatches at 0 ms wall clock. Replaced by a
  bound on `io_context` turns, measured 2/1 across 27 runs and RED at 53/51 against that mutant.
  Record: `decisions/speckit/394-promptness-in-scheduler-turns.md`.

- **2026-09-07 — #289 batch 20, the axis that batch 19's bucket did not have.**
  `components/test.md` and `failure-classes.md` record the answer to the caveat batch 19 filed
  against itself. `ci/pump-get-sweep.sh` gains a third axis — **call-site scope**, `CORO` /
  `CALLER-SIDE` — and tracking for futures held in a container. Together they moved a population of
  previously-invisible sites into the candidate list, among them the coroutine-side wedge shape in a
  file batch 19 never opened; those were migrated. Figures move with the tree AND with the
  instrument, so read them from `bash ci/pump-get-sweep.sh --disposition`, not from here.

  ⚠️ **The discriminator is the RETURN TYPE, not a keyword.** "The enclosing scope contains a
  `co_await`" is satisfied by a TEST body that merely *spawns* a coroutine, so it colours the
  caller-side `.get()` after that lambda's closing brace and reports nearly the whole corpus — a
  survey wearing an axis's clothes. Both readings pass a hand-check; only a control with one `.get()`
  inside the lambda and one immediately after it separates them.

  ⚠️ **A zero is the headline, so the arm is a known-NON-zero corpus.**
  `ci/red-arms/batch20-coroutine-axis.sh` runs the *current* sweep against `tests/` at the
  pre-batch-19 commit and requires **non-zero there, zero here** — a condition, not a count, because
  the historical figure moves whenever the instrument changes. Same instrument, older tree —
  extracting only the corpus is load-bearing, since the old tree would run the old sweep and pass by
  construction. And the sweep's classifier controls now run in tier 1, which they never had.

  ⚠️ **The `/simplify` reuse pass caught the batch about to add a FOURTH brace-walker**, one batch
  after batch 19's headline was "lexer copies 2 → 1". `mock-clock-staging-sweep.sh` already carried
  two near-identical ones. `brace_blocks` / `line_starts` / `line_index_of` now live in
  `ci/cxx_blank.py` with their own controls, and all three callers sit on them — **4 → 1**, proven by
  mutating the shared primitive and watching every caller go red. It was also the efficiency fix: the
  draft walked every character twice (10.4M list appends) and had **doubled** the sweep, 1.02 s →
  2.33 s, in a step just added to tier 1; brace-only `finditer` + `bisect` brings it to 1.63 s with
  output proven identical over 660 files / 768 spans.

- **2026-09-07 — #389: an assessment can be SATISFIED while the code it certifies is broken.**
  `components/wire.md` gains the reserve-estimator section; `failure-classes.md` gains a NEW
  **class 10** (an assessment scoped to one of a change's effects certifies a site the OTHER effect
  breaks) — sibling to class 9, which is the same function's other failure.

  083 changed the repeating-group split loop to use the DICTIONARY delimiter. It left
  `group_slices_reserve_bound()` alone — an estimator that sums the wire's DECLARED counts and
  justified itself by citing the cap in `consume_group_extent`, which walks with the WIRE delimiter.
  Two delimiters, one cap, and an inference across them. The bound could be exceeded, the single
  shared `group_slices_` vector reallocated, and every span already handed out for an earlier
  `no_tag` dangled — including ones the C-ABI stores across calls. Live on the shipped path since
  083, exhibited by 083's own `OutOfScopeWireProbesUnchanged` (bound 2, three pushes).

  ⭐ **The estimator was DELETED rather than repaired.** The hazard was never "the bound is wrong";
  it was that N groups shared ONE growable array, so an estimator and a split loop in two places had
  to agree forever. A better bound restates that obligation. Per-group exact-sized arrays remove it.

  **Three things this turned up that generalise:**

  **(1) C-8.0a was satisfied and the code was still broken.** The clause obliged 083 to assess
  whether its changed MEMBER SETS perturbed the estimator. They did not — the assessment was
  correct, and useless, because the same feature also changed the DELIMITER. *An assessment scoped
  to one of a feature's changes cannot certify a site the feature's OTHER change invalidates.* Its
  recorded verdict — *"the under-reserve failure mode is impossible by construction"* — is retracted;
  ⚠️ but narrowly: the leg's own body is scoped to a member-set change and stays TRUE. The unscoped
  verdict above it generalised past its own argument. **Retract the sentence, not the leg.**

  **(2) The witness that fails is not the obvious one.** Reading through a moved span is UB that
  returns CORRECT BYTES on the shipped path, because the parse arena is a monotonic resource that
  never reuses the abandoned block. A contents assertion is green under both the broken and the
  fixed shape — which is exactly why this survived two features. The discriminator is POINTER
  IDENTITY of the re-fetched cached span. Mutation-proven RED against the pre-fix tree.

  **(3) The delimiter-free bound that looked obvious was rejected ON MEASUREMENT.** Summing extent
  ENTRIES is correct and keeps C-8.0a verbatim — and is ~3x the instance count, the exact ratio
  PR #181 existed to remove (3600 B against the 3744 B that exhausted the MSVC-release arena). The
  cheap-looking option was the one that re-opened a closed defect.

  **(4) I broke rule one inside my own verification.** To prove the index rows could not deep-copy
  the slice buffers I wrote a harness and reported *"0 of 64 relocated"* — a ZERO I never showed the
  harness could report as NON-zero. The review added the control (one member with a `noexcept(false)`
  move): it reports **32/64**, which is what makes the zeros real. ⭐ **A verification harness is an
  instrument and gets the same rule as any other**; writing it yourself is not an exemption, and the
  moment you are proving something cannot happen is exactly when a silent no-op looks like success.

  ⚠️ Also fixed here: the withdrawn B-384-2 carrier that SHIPPED in PR #390 at
  `tests/wire/offset_table_test.cpp` — a FOURTH restatement, missed because the re-derivation recipe
  written to prevent exactly that omitted `tests`, the one directory holding it. **An instrument that
  cannot search where the defect lives fails toward clean.**

  Record: `decisions/speckit/389-two-delimiters-one-cap-and-the-estimator-that-was-deleted.md`.

- **2026-09-07 — #289 batch 19, the `.get()` inside the coroutine.** `components/test.md` gains a
  FOURTH `#289` shape, and it is the one where the standing remedy does not apply: the `.get()` runs
  **on the pumping thread**, inside a handler the driver dispatched, so a bounded outer driver — a
  5 s deadline loop, or `run_to_exhaustion_or_report`, which is already the #289 guard — is itself
  wedged. Measured, not read: `ci/red-arms/batch19-coroutine-side-get.sh` injects a defect no
  yielding can fix and shows the pre-batch shape WEDGE where the guarded shape REPORTS. New
  primitive `yield_window_then_ready`, which PRESERVES the yield window and reports via
  `kWindowMiss` — **reworded to be unit-neutral** rather than duplicated into a fourth literal or
  shipped wrong behind a disclosure. The handover's "a fourth literal costs every driver" estimate
  was never measured — and the reword then **broke a live `EXPECT_NONFATAL_FAILURE` matcher** that
  three successive enumerations could not see, because each was keyed on something other than the
  message text itself. **Enumerate a shared literal's dependants by BOTH fragments, with `grep -rn`
  and not `git grep` (a batch's own new arm scripts are untracked), over the whole tree.** `failure-classes.md` class 1 gains "a bound outside the thing it bounds may be
  inside it at runtime" and "a residual BUCKET is not the CLASS"; class 8 gains the harness that
  broke when the script it relocates gained an import.

  ⚠️ **`ci/mock-clock-staging-sweep.sh`'s escalation bucket was READ, and the reasons are not
  interchangeable.** All 14 rows now name their own KIND at the site (time-stamp · no waiter exists ·
  must fire nothing · stored-anchor rescue · armed and observed · fail-loud · vestigial). The tally
  does not move — a disposition is not a migration — and that is stated up front rather than
  compensated for with a metric that does.

- **2026-09-07 — #384: a rationale can outlive the branch it was written about.** `failure-classes.md`
  class 9 gains a **third** disposition and two warnings; `components/wire.md` gains the delimiter-
  oracle section.

  083's C-8.4 justified `group_slices_status()`'s wire-derived instance delimiter for a guard with
  two disjuncts — `opaque_dict_ == nullptr || group_delim_fn_ == nullptr` — and argued only the
  first (*"pre-083 behaviour is the correct answer for a table with no dictionary"*). **#220 deleted
  that disjunct**, and the sentence stayed, sitting over the disjunct it was never about. Unlike a
  stale line number, it still read as current: nothing about it had changed. **The lesson is that a
  justification goes stale by losing its SUBJECT, not by drifting — so when you delete a disjunct,
  re-read what justified the guard, not just what the guard did.**

  Two findings the fix itself produced, both recorded because they cut against the obvious move:

  **(1) I rejected the obvious fix on a measurement, and the measurement was not the reason.**
  Folding `group_delim_fn_ == nullptr` into #220's decline does turn
  `TypedReadSplitAgreement.OutOfScopeWireProbesUnchanged` RED — it builds the half-threaded table on
  purpose as its pre-083 baseline. But that witness passes `nullptr` only as a **spelling**: a
  zero-returning oracle is behaviourally identical, so a one-line fixture change removes the cost
  and the decline stands. **A cost a one-line fixture change removes is not a design constraint.**
  The reason that survives is structural — declining on a null callback does not remove the branch,
  because a callback ANSWERING 0 still reaches it. Caught by the altitude review, not by me, and it
  is this class's own error committed inside this class's own fix.

  **(2) The row nobody had re-read was ALSO wrong.** C-8.4's *second* row said "there is no wire
  fallback" for the dictionary-present case. The shipped guard is `if (d != 0) { delim = d; }` — a
  zero answer keeps the wire value. Its stated reason ("a zero means not-a-group, which callers
  handle as absent") cannot hold at that point either, because the splitter runs only after
  membership has confirmed the group HAS members. Correcting only the disjunct the issue named would
  have left this one for the next round.

  ⚠️ **The corollary that bites hardest: a zero-returning stub is not a threaded oracle.** It is
  behaviourally identical to `nullptr`, so "every site threads the callback" is falsifiable by a
  stub. `tests/support/context_group_delim_fn.hpp` carries that warning at the point of use.

  **What shipped** is class 9's new third option — make the un-informed construction *unspellable by
  omission*. Removing the `= nullptr` default from all four dict-aware constructors changes no
  runtime behaviour in either direction and turns every half-threaded site into a compile error,
  which is what let the compiler — not a source sweep — enumerate the population: 5 test/fuzz files,
  **zero** sites in `src/` or `include/`. ⚠️ It is deliberately recorded as HALF a cure: stripping a
  default removes the un-informed spelling by omission, not the branch, whenever the callback's
  answer space still carries a value equivalent to absence. Class 9's new bullet states that test;
  the structural cure — one both-or-neither aggregate instead of a two-disjunct guard — is recorded
  as deferred with its blast radius, not silently dropped.

- **2026-09-07 — #289 batch 18, the lost mock-clock advance.** `components/test.md` gains a THIRD
  `#289` shape: a fixed `run_for` STAGING a mock-clock advance. No `.get()` near it, so the census
  never saw it; the terminal half was usually already migrated and correct. If the coroutine has not
  parked when the window returns, the advance lands on a timer that is not yet armed and is LOST —
  unrecoverable, not slow. The migration is an observable condition (`pump_until` on `LogoutSent`),
  never a longer window, and `ci/mock-clock-staging-sweep.sh` is the detector.

  **The entry worth reading is the instrument that was NOT shipped.** The obvious mechanism-level
  barrier — expose `mock_clock`'s parked-waiter count, since `advance()` already computes the woken
  set — is a better idea than a lexical sweep on every axis that can be argued: no lookahead, no
  same-function constraint, a positive observation rather than a proxy. It was built and then
  **removed**, because running it at both sites no FSM state covers showed it could not
  discriminate. One cell passes with its staging window starved to `run_for(0ms)`; the other goes RED
  when starved but RED with the barrier in place too, because starving removed a TERMINAL collection
  window instead. ⚠️ **The mechanism first written for that was wrong and the correction is the
  durable half**: not monotonicity but the ARM SHAPE — `sleep_until` fires immediately for a deadline
  already passed, so an arm from a STORED ANCHOR is rescued while a NOW-RELATIVE arm is lost, which
  is also why no clock-side fix reaches the second case. It would have shipped as an assertion that cannot go RED for
  its own class — `failure-classes.md` class 1, arrived at from the opposite direction: not an
  instrument that reports clean because it is broken, but a *correct* instrument watching a
  condition nothing in the tree can violate. **The argument for it was sound and the measurement
  killed it**; that asymmetry is the reason the removal is recorded rather than the addition.

  Two more, both class 1. `ci/pump-get-sweep.sh` counted `using R = decltype(fut.get());` as a
  `.get()` CALL — an unevaluated operand — and that idiom is part of #316's settled value-helper
  recipe, so **migrating a value helper RAISED the residual**, the one direction an instrument must
  never move. And the new sweep's own controls all passed while it misread the single real
  hand-rolled `pump_until` in the tree, whose `while` condition WRAPS across two lines: fixtures
  written by the same hand as the rule share the rule's blind spot, so the control that catches it
  is copied from the shape that broke it (`ci/mock-clock-staging-sweep.sh`).

  Record: `decisions/speckit/pr-batch18-lost-advance-and-the-instrument-that-could-not-fail.md`.

- **2026-09-07 — #289 batch 17, the unbounded-`run()` class.** `failure-classes.md` class 1 gains two
  bullets, both about instruments that read SOURCE TEXT.

  **(1) A fix's own controls inherit the fix's blind spot.** `ci/cxx_blank.py` read every `'` as
  opening a character literal; `10'000` is a C++14 digit separator, and reading it as an opener
  started a literal that ran to the next apostrophe *anywhere in the file* and blanked every
  intervening line, code included. Measured: one such token hid two labelled seam calls from
  `ci/pump-label-uniqueness.sh`, which then printed *"every site label is unique"* over a tree it
  could not fully read. The fix shipped with controls for `10'000`, `0x1F'FF` and `u8'0'` — the cases
  the author could think of — and the review immediately produced `.1'0`, which the C++ grammar admits
  and the fix still ate. **The examples were chosen by the model that was wrong.** The transferable
  step is to enumerate the grammar production, not to brainstorm harder. ⚠️ Scoped by measurement
  rather than by alarm: running the scanner with both lexers over each tree gives `main` **505 → 505,
  hidden 0** and the branch **666 → 668, hidden 2**. No pre-existing site was ever hidden — the defect
  was latent and this batch was the first change to put a labelled call after a separator in the same
  file. The narrow claim is the true one.

  **(2) A formatter moves what an instrument reads without moving what the program does.**
  `clang-format` splits a string literal that crosses the column limit; C++ concatenates the halves
  back. `ci/pump-label-uniqueness.sh` harvested "the last literal in the call" and so read only the
  tail — GREEN before formatting, a nonexistent duplicate after it. The dangerous sign is reachable
  from the same defect: two sites with the SAME label, one split and one not, harvest as `tail` and
  `full` and read as DISTINCT. Source-reading gates now run after formatting, and the harvest joins
  the trailing RUN of whitespace-separated literals.

  Also recorded, because it is the batch's own justification and it took two attempts to state
  honestly: **a forced-miss arm proves the miss branch RUNS, not that it is REACHABLE.** The seam
  driver forced 158 of 162 sites RED, which says nothing about whether any real edit could reach
  those branches. `ci/red-arms/batch17-genuine-miss.sh` injects the defect a real edit would make
  (delete one `ioc.restart()`) and runs three shapes: base-with-restart PASSES (the attribution
  control), base-without WEDGES at exit 124, guarded-without REPORTS at exit 1. An earlier draft ran
  only the third arm while its header claimed the pair — *"the guard reports"* is not *"the old code
  was worse"*.
- **2026-09-06 — #220, and why a "documented degradation" was neither.** `failure-classes.md` gains
  **class 9: a correctness fix leaves the OLD rule standing on the path that lacked the
  information.** `OffsetTable::group()` bounded a repeating group by rest-of-message whenever no
  dictionary was threaded. Read cold, that looks like a design choice — the public header called it
  a "deliberate, scoped degradation" and `004-wire-codec-completeness.md` carried a `[2b §4.7]`
  waiver for it. `git log` says otherwise: it is the leftover half of a first-instance member-set
  heuristic that PR #68 removed from the dict-aware path **as a P1** at gate round 3, whose commit
  message is explicit — *"no end-of-message fallback, no 32-tag ceiling."* It survived exactly where
  membership was unavailable, and acquired the appearance of design when someone waived it.

  Two things worth carrying beyond the fix. **A waiver's premise is a fact about the call graph.**
  This one read "no production wire caller uses the dict-free construction path — test/utility
  surfaces only", and it had already been false in shipped code: L-063-2's 066 amendment records
  `Session::parse_and_dispatch_` building its `Parser` with the dictionary-free default constructor,
  so every inbound-dispatched message took the waived path for months. `Parser<Mode>::Parser() =
  default` is still public API. **And the reported symptom was the smaller half** — #220 was filed
  as a DoS-cap false positive, but the same over-extent fed `group_slices_status()` and so the typed
  `group_view<GroupT>`; the splitter even carried a comment asserting the opposite
  (*"trailing top-level fields are never included in the last instance slice"*), true only on the
  dictionary path and unconditionally stated, which is plausibly why the second half went unnoticed.

  Resolved by **declining**, not by a better guess: `[2b §4.7]` defines the boundary as the
  dictionary's first-field-of-group rule per `[FIX50SP2 §3]`, so dict-free it is undefined rather
  than hard. Confirmed against both reference engines before choosing — QuickFIX C++ returns from
  `Message::setGroup` when `DataDictionary::getGroup` fails and forms no `Group`; QuickFIX/J guards
  all three `parseGroup` call sites on a non-null dictionary and, run without one, flattens the
  members as ordinary fields. That last one is a MEASUREMENT, not a reading — a harness built against
  `quickfixj-base` 3.0.1 showed one instance parsing clean with `getGroups(453).size == 0` and a
  two-instance frame tripping the duplicate-tag guard, identically for `validate=true` and `false`.
  ⚠️ It is not checkable from this repo: the engines are vendored in the PARENT repo under
  `reference-engines/{quickfix-cpp,quickfixj,fix8}`, so re-derive there, not here. `brain/components/wire.md` gains the boundary and its rejected alternatives, so the next
  "just split on the delimiter" proposal meets the P1 that already killed it.

  **Second lesson, from the review rather than the fix — class 1 gains "a green suite is evidence
  about the EXECUTABLE you ran".** The change broke nine test cells that parsed dict-free and then
  asserted a NON-EMPTY typed group; they had been reading the removed defect as their baseline. Six
  were found by building beyond the wire suite. The last three were found by a reviewer running
  `wire_codegen_tests` — a binary I had never built, while repeatedly reporting "wire_pure_tests
  257/257" as though it covered `tests/wire`. It does not: that directory fans out into **ten**
  executables. One of the three was the conformance witness named by catalogue row **W-007**
  (OFFICIAL, nested repeating groups), so an OFFICIAL row's evidence was red while every suite I had
  run was green. Worth separating the two halves: the *code* fix was found by provenance and settled
  against two reference engines; the *blast radius* was found only by running, and the grep-shaped
  sweep that predicted five of the nine missed a sixth outright — its helper carried none of the
  words the sweep looked for. The durable correction is procedural: derive the target list from
  `ninja -t targets`, never from the directory name, and state which binaries a green covers.

- **2026-09-04 — #289 batch 10, the shared surfaces.** `failure-classes.md` gains **class 8:
  consolidating N copies dissolves the population an audit asserts over.** Hoisting
  `kWindowMissSentinel` out of three test files emptied the byte-identity population
  `audit-copy-span.sh` was built to compare — and the script did not say so; it printed three
  `EXTRACTOR FAILED` lines, which read as a broken instrument rather than as a check with nothing
  left to check. The mechanism worth carrying is narrower than "dedup breaks audits": **a selector
  that was EXACT silently becomes a PROXY.** "Mentions `X`" and "defines `X`" are the same predicate
  only while every mention sits beside its definition, and consolidation ends that in one step, with
  the selector's own text unchanged. Fixed by matching the definition; the population is now empty
  *by construction*, which is only readable because a control builds a synthetic definer each run.

  Also recorded: **the batch's own RED-arm driver would have destroyed the work it verified.** Its
  `restore()` was `git checkout -- <file>`, but the sources a forced-MISS arm rewrites are modified
  in the WORKING TREE — an uncommitted migration is precisely what is under test. Arm 1 would have
  reverted it and arms 2..N would have reported `SILENT` against a tree with no miss branch left,
  which reads as "the migration is broken". Caught by reading, not by a run; no green result would
  have exposed it, because the failure mode *is* the green-looking one.

- **2026-09-04 — #289 batch 9, review rounds.** `components/test.md` gains `ci/pump-get-sweep.sh`
  and, more usefully, the script's own three-round history: every fix for a false-clean introduced
  the next one, which `failure-classes.md` class 1 now carries as a condition. Also recorded there
  because it is a distinct trap: a limitation can be *invisible rather than absent* — that script's
  scope disclosure shipped as literal `\n` escapes on one 613-character line, so the honest caveat
  nobody could read was worth nothing.

- **2026-09-04 — #289 batch 9: the census gains a THIRD blind spot, and it is the one widening
  cannot reach.** `components/test.md` records it: when the pump is indirected through a helper
  (`f.drain();` between the `co_spawn` and the `get()`) there is no `ioc.run_for` for the census to
  anchor on, so no lookahead width finds it. Found by a forced-miss arm that HUNG rather than going
  RED. The durable lesson is about the detector, not the defect: the first one written matched a bare
  `run()` but excluded a preceding `.`, so `f.drain()` was invisible and it reported zero for the very
  file that hung — `failure-classes.md` class 1 already says an instrument fails toward clean, and
  this adds the specific remedy, which is to start the sweep from the `get()` rather than from the
  pump so no unanticipated helper shape can hide. Also recorded: a pin row can sit in DEAD CODE (a
  migrated site in an uncalled fixture helper drops a row and can never fire), so a non-firing arm is
  a question about reachability before it is evidence of a broken arm.

- **2026-09-04 — #289 batch 8.** `components/test.md` gains the bounded-pump section: why the window is
  PRESERVED, the fixture-dependent teardown shape, the census's two blind spots, and — new — that **a
  state assertion after a helper call is not a masking barrier**, because the miss branch's own drain
  completes the coroutine and satisfies the assertion. That falsified a forced-miss arm's predicted
  count in the safe direction; it could as easily go the other way. `failure-classes.md` class 1 gains
  three conditions, all from defects found in this batch's own instruments: a control set thorough
  about one CONFIGURATION proves nothing about the others; synthetic fixtures assert a property of the
  instrument where real-file anchors assert a contingent fact about the tree; and an instrument's
  no-result path must FAIL rather than return a value.

- **2026-08-29 — Created.** Routing index + the first three component decision maps
  (engine accept path, `async_mutex`, `MessageStore` teardown). Scope deliberately narrow: the
  measured deficit was ROUTING and INCOMPLETE DECISION SETS, not retrieval — see
  `decisions/speckit/brain-baseline.md` for the five atomic and two composite blind-agent runs that
  set that scope.

- **2026-09-04 — `dictionary` gains a group-context section; failure classes gain a 7th.**
  Issue #264 (the FR-023 probe and the registration path clamping the ancestor chain at opposite ends)
  produced three decisions worth keeping and, more usefully, **three rejected alternatives** — clamping
  inside the walk, truncating on a cycle, and refusing on depth alone — each rejected on measurement
  rather than taste. Class 7 (*removing a spurious gate unmasks whatever it was holding back*) is added
  with #264 as its reference instance: the false rejection was the only thing keeping unrepresentable
  contexts out of the store, so fixing it correctly is what exposed a silent merge behind it.

- **2026-09-04 (later) — `dictionary` query-side clamp: lead → guarded.** PR #367 added a debug-only
  assertion on the precondition the page had recorded as an unguarded lead, so the page said the tree
  was less protected than it is. Also records what was NOT done and why (a `group_ctx_path` type that
  makes the wrong key unrepresentable — a public-header change, out of scope for the fix), and that
  the collision check's FALSE-POSITIVE arm is the load-bearing one because that check rejects a
  dictionary.


- **2026-09-04 (later still) — the #289 pump gains a RUNTIME forcing seam; failure class 1 gains the
  FALLBACK sub-lesson.** Batch 11 (82 sites / 39 files) could not have been verified under the
  textual driver: that driver rebuilds once per arm, and one arm per site is the method rather than
  overhead, so an 82-site batch is 82 rebuilds. `run_window_then_ready` now takes an optional site
  label and honours `FIXPP_FORCE_WINDOW_MISS`, making forcing a runtime decision — one build, N runs.
  Recorded in `components/test.md` with the two things that make it safe: it is a **weaker** witness
  (so the textual driver is NOT retired), and its silence is ambiguous, so forcing **announces
  itself** and an unannounced run is `NO-SUCH-SITE` rather than a pass.
  ⚠️ **SUPERSEDED IN BATCH 12 (#289):** this entry originally read *"it exercises the primitive's
  forced path, not the site's own miss block"*. That was false when written — forcing always ran the
  caller's miss branch — and the seam has since changed besides. What forcing does and does not
  exercise is stated ONCE, at `run_window_then_ready` in `tests/support/pump_until_ready.hpp`.

  Class 1 gains: **a classifier's fallback is a claim, and a fallback set to the common case fails
  toward the easy answer.** `classify-289.py`'s enclosing-function walk returned `("TEST", "<none>")`
  when it exhausted — TEST-body being the shape with the simplest migration recipe — so three rows in
  a libFuzzer harness that links no gtest were being offered for a migration whose miss branch is
  `ADD_FAILURE()`. A correct row and a fallback row printed identically. This is the no-result path
  wearing a value, and it is why the unresolved case now has its own bucket outside every recipe.

- **2026-09-05 — batch 11's ARM PHASE found three defects in the INSTRUMENTS; the SITE defects were
  caught earlier, by the compiler.** ⚠️ An earlier wording of this entry said the batch found "none
  in the 82 migrated sites", which is false as written and contradicted by its own sibling commit
  (*"SIX REAL DEFECTS, ONE CLASS"*): six sites had a clock expression derived per FILE where C++
  scope is per SITE, and `*clock` bound to `::clock` from `<ctime>`. Those were found at BUILD time
  and fixed before any arm ran. The true statement is narrower and still worth keeping: **once the
  tree compiled, forcing all 82 miss branches found defects only in the drivers.** (1) `ci/pump-red-arm.sh` could report a false `SILENT` — `pipefail` plus
  `grep -q` in a pipeline exits 141 *when the pattern matches*, size-dependently, so it had shipped
  in batch 10 and passed on small arms. Recorded as a new bullet under failure class 1, because the
  tell is a CONTRADICTION (the matcher says "not found" while the diagnostic prints the text) rather
  than an error. (2) A timeout discarded the output, collapsing "reported then wedged" into
  "inconclusive"; the driver now reads the partial output and surfaces the wedge count on the summary
  line. (3) The arm timeout was a round 180 s where the competing quantity is `kQuiesceBudget` x the
  forced count — measured 48 for one label — so it manufactured three false timeouts.

  All three wedges turned out to be the SAME thing and it is worth keeping: forcing a miss wedged the
  run at an **unmigrated** `run_for(); … get()` later in the same test. That is #289's hazard shown
  live, and it is evidence for the remaining migration rather than against the migrated sites.

- ⭐ **A FORCING MECHANISM CANNOT MANUFACTURE THE STATE IT IS MEANT TO PRESERVE. Every claim about
  what forcing exercises is CONDITIONAL on the site's state at entry** (#289 batch 12). Three
  separate claims about `FIXPP_FORCE_WINDOW_MISS` — *it gives the drain something to quiesce*, *the
  drain is what resumes the frame*, *it reproduces a real miss's state* — were each written
  unconditionally, and each was false at the same site for the same reason: a future that was
  ALREADY READY before its window opened has no suspended frame for any forcing mode to preserve.
  The site was known and documented as the batch's exception in a sibling file at the time, so the
  two artifacts disagreed. ⚠️ **The unconditional form was written FIRST all three times**, twice
  by the author and once by the reviewer, which is why this is a class and not an oversight: the
  conditional reads like a hedge on a claim that feels structural, and it is not — it is the claim.

- ⭐ **"I DID NOT FIND X" AND "THERE IS NO X" ARE DIFFERENT CLAIMS, AND THE SECOND NEEDS A COMMAND
  ATTACHED** (#289 batch 12; ⚠️ **a REPEAT of batch 11's own lesson**, where a reviewer's "I did not
  find X" was twice restated as "there is no X" and the X was then found). Three times in batch 12 I
  told a reviewer that a phrase or a claim existed nowhere, without grepping in the same breath, and
  each time it was in the tree — once in the file whose whole subject was that claim. It is adjacent
  to *instruments fail toward clean* but distinct and needs its own name: **there, a tool ran and was
  broken; here no tool ran at all.** An absence is the one claim that cannot be checked by reading,
  because reading is what produced it. Attach the grep or do not make the claim.
