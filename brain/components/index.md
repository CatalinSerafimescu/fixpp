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
