---
type: Log
title: SecondBrain change log
status: stable
---

# Log

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
