---
type: Log
title: SecondBrain change log
status: stable
---

# Log

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
