---
type: Component Decision Map
title: MessageStore teardown — why there is no drain to call
description: The store's async_mutex must be quiet at destruction; the contract is discharged by quiescence, not by a call. Written because "never explicitly drained" reads as a defect and is not.
status: stable
refs:
  - include/fixpp/session/message_store.hpp
  - include/fixpp/session/file_store.hpp
  - include/fixpp/session/memory_store.hpp
  - src/session/engine.cpp
codegraph_entry: [MessageStore, MemoryStore, FileStore, Engine]
constitution: ["§XV.4"]
---

# `MessageStore` teardown — quiescence, not a drain call

> ## ⚠️ The CODE is authoritative. This page is not.
>
> SecondBrain is a **consultant**, not a source of truth. It points you at the right files and explains
> **why** a decision was taken and what was **rejected** — that half is historical and does not change
> retroactively. It does **not** establish what the code does today.
>
> **Anything here describing current behaviour is a LEAD TO CHECK, not a fact to cite.** Verify against
> source before you rely on it, and cite the source, not this page.
>
> This page exists because signed-off design documents rotted. **It has no immunity from that** — a page
> trusted instead of read becomes the next fossil, and it would be a worse one, because it is the page
> people come to for the fossil list.


## The question this answers

`~MemoryStore` frees its slab and nothing else. `~FileStore` is `= default`. Neither calls
`cancel_and_drain()`. Meanwhile `~async_mutex` calls **`std::terminate()`** if the mutex still has a
holder or waiters (`B-006-2`). That reads as a latent crash, and a blind agent reviewing the blast
radius called it *"the most fragile dependency."*

**It is not a defect.** The facts are right; the framing is wrong.

## Current state — a LEAD, verify against source before citing

`MessageStore` deliberately exposes **no public drain**. There is nothing for a caller to call, so
the literal reading of *"callers must drain the mutex before destroying the store"*
(`message_store.hpp`) is unsatisfiable by design. The obligation is discharged by **quiescence**:

- A store method only returns **after** its `file_io_executor` pool work completes — store calls are
  awaited, never detached.
- `Engine::stop()` **joins** every role loop (tracked by `outstanding_counter_`) **before** the step
  that clears the registry and thereby destroys the sessions and their stores.
- With no store `co_await` in flight, the mutex has no holder and no waiters, so the destructor
  precondition holds **by construction** rather than by a call.

That ordering is load-bearing and was itself hardened by a Gate B round-1 finding: `outstanding_counter_`
must be published **before** any loop is spawned, or a late assignment observes it null, skips the
join, and clears the registry while a spawned loop still holds `SessionEntry&` → use-after-free.

## The case Engine::stop() does NOT cover

A store a consumer constructs and drives **directly**, outside `Engine` ownership. `stop()` neither
sees nor can drain those awaitables. Such a caller must:

1. `co_await` every `store`/`flush` call to completion,
2. **then** `pool.stop()` + `pool.join()`,
3. **only then** destroy the store.

Get it wrong and it is `std::terminate()`, not an error return. This is now disclosed as **`L-035-3`**
— it previously lived only in a header comment on `FileStore::Config`, which is exactly the class of
constraint an integrator must know and will not find.

## Note for reviewers

A symbol grep for `cancel_and_drain` across the decision records produces **false leads**:
`pr321-drain-terminate-and-cancel-drain-gateb.md` and `pr325-322-quiesce-delegation-gateb.md` both
match and are **tests-only** (the `~quiesce_on_exit` harness, "no `src/`/`include/` change"). A symbol
grep cannot tell a production disposition from a test-harness one. The answer was in the
`file_store.hpp` header comment and the `Engine::stop()` ordering — not in the records that matched
the symbol.
