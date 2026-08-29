---
type: Routing Index
title: Component decision maps
description: For each component, EVERY document that claims to describe it — superseded ones flagged.
status: stable
---

# Component decision maps

The value here is **completeness**, not summary. Each page lists every document describing the
component, including the ones that are wrong, because omitting a wrong one is how a careful agent
reports "no disagreements found" over a repository that has some.

| Component | Has a superseded doc still in the tree? |
|---|---|
| [Engine accept path](./engine-accept-path.md) | **Yes — two, plus one stale.** Tracked by issue #334 |
| [`async_mutex`](./async-mutex.md) | **Yes — the whole 006 doc set** is historical for drain/cancellation |
| [`SecurityProfile`](./security-profile.md) | **Yes — and a worse class.** `2g-tls.md` quotes a constitutional article **verbatim as normative**; the article was later amended (v0.3, feature 043) |
| [`MessageStore` teardown](./message-store-quiescence.md) | No — but a real constraint lived only in a header comment until `L-035-3` |

Deprecated concepts do not appear in this table; see [`../history.md`](../history.md).

## Flow pages *(added 2026-08-29 — the interaction axis)*

No design doc owns a runtime flow: `arch §5` is cross-cutting **policy**, and only **2 of ~30** design
and spec documents contain anything flow-shaped. These pages record the **invariants and their
enforcement sites** — never a step narrative, which is the code and rots.

- [`inbound-message-path.md`](./inbound-message-path.md) — socket bytes → `Framer` → `fromApp`
- [`session-liveness-and-reconnect.md`](./session-liveness-and-reconnect.md) — heartbeat, TestRequest
  escalation, reconnect

**Coverage is derived, not listed here:** `python3 tools/brain_inventory.py --census` reports every
long-lived coroutine and whether a page names it.
