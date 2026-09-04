---
type: Log
title: SecondBrain change log
status: stable
---

# Log

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

