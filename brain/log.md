---
type: Log
title: SecondBrain change log
status: stable
---

# Log

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
