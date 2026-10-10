---
type: Component Decision Map
title: MessageStore teardown — why there is no drain to call
description: The store's async_mutex must be quiet at destruction; the contract is discharged by quiescence, not by a call. Written because "never explicitly drained" reads as a defect and is not.
status: stable
refs:
  - include/fixpp/session/message_store.hpp
  - include/fixpp/session/file_store.hpp
  - include/fixpp/session/memory_store.hpp
  - src/session/file_store.cpp
  - src/session/engine.cpp
  - src/session/session.cpp
  - specs/093-inbound-frame-dispositions/spec.md
  - specs/093-inbound-frame-dispositions/plan.md
  - specs/093-inbound-frame-dispositions/contracts/inbound-frame-dispositions.md
  - spec/behaviors-and-limitations.md
  - tests/session/test_file_store_crash_survival.cpp
  - .specify/544-hot-path-zero-alloc.md
  - include/fixpp/core/sync/async_mutex.hpp
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/speckit/093-inbound-frame-dispositions-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/093-inbound-frame-dispositions-evidence.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/544-hot-path-zero-alloc-tasks.md
codegraph_entry: [MessageStore, MemoryStore, FileStore, Engine, run_reset_unit_]
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
  ⚠️ **This ordering is a RESULT, and it is the load-bearing one on this page** — the whole "no drain
  needed" argument collapses if the join ever moves after the clear, and nothing in this bundle would
  notice. **Re-derive before relying on it:** read `Engine::stop()` in `src/session/engine.cpp` and
  check that the `outstanding_counter_` wait precedes the registry clear.
- With no store `co_await` in flight, the mutex has no holder and no waiters, so the destructor
  precondition holds **by construction** rather than by a call.

That ordering is load-bearing and was itself hardened by a Gate B round-1 finding: `outstanding_counter_`
must be published **before** any loop is spawned, or a late assignment observes it null, skips the
join, and clears the registry while a spawned loop still holds `SessionEntry&` → use-after-free.

## `MemoryStore::store`'s lock is the frameless lock op; its siblings are not (fixpp#544)

`MemoryStore::store` (`include/fixpp/session/memory_store.hpp`) locks its `async_mutex` through
`FIXPP_DETAIL_CO_AWAIT_LOCK` (2f Erratum E-6), and its leading post is awaited as `deferred`; together
they take two coroutine frames off the store chain, which fixpp#544's W-D window gates at zero. `next_seqnum`,
`reset`, `reset_to` and `retrieve` stay on the public `async_lock`, and FileStore is not converted
(`.specify/544-hot-path-zero-alloc.md` §2.3). **This page's argument is unchanged:** the macro is the
same lock with the same waiter and reap model, so the store's mutex still has no holder and no waiter
once no store `co_await` is in flight. Re-derive the converted sites with
`git grep -n "FIXPP_DETAIL_CO_AWAIT_LOCK" -- src include`.

## The 141=Y reset unit's store operation (093, fixpp#524) — still awaited, now not cancellable

093 added `MessageStore::reset_to`, a non-pure virtual, and the session's 141=Y reset unit runs it as
its one store operation. The unit `co_spawn`s **only** that call on the session strand, completing
through a token bound to an empty cancellation slot, and **awaits** it (`run_reset_unit_` in
`src/session/session.cpp`; contract C-6's erratum, plan OD-25, OD-26). So the quiescence argument
above still holds for it: the store call is awaited, not detached, and the role loop that awaits it is
what `Engine::stop()` joins. What changed is that no cancellation reaches it, so `stop()`'s join
**waits for it**. A store operation that never completes hangs `stop()`, as `close()`'s teardown
reset already did (B&L `L-093-12`). ⚠️ **Re-derive before relying on it:** check that the `co_spawn`
in `run_reset_unit_` is `co_await`ed with `use_awaitable`, not `detached`.

`close()` also waits, when it is about to issue its teardown reset and a unit is in flight, bounded by
`logon_timeout_ms` (plan OD-1). That wait is about ordering the two resets, not about destruction.

Rejected for this unit (plan OD-14, OD-25): an in-place `reset_cancellation_state(disable)` shield,
which `async_mutex::async_lock()` silently replaces after its first acquisition; and a store-side
`async_mutex` mode that respects the caller's cancellation policy, which would change a core primitive
and not reach user or default-body stores. A default-body store keeps today's reset-then-advance
sequence and has no crash atomicity (`L-093-3`).

## The FileStore reset's commit boundary (093 Gate B)

`FileStore::reset()` and `reset_to()` share one body, `reset_store_to` in `src/session/file_store.cpp`.
Its pool worker writes the fresh log under a temporary name, renames it over the live log, makes
that durable, and reopens. A shared `rename_done` flag tells Region 3 which side of the rename a
failure fell on. Before the rename, the store keeps its open file. After it, that file names the
replaced log, so every write to it would vanish on restart, and the store is poisoned
(`open_ok = false`, B&L `L-035-2`).

- **The flag is published at the namespace mutation, not after the directory fsync.** It marks the
  moment the live name changes, not the moment the change is durable, so any later failure
  (directory open or fsync, reopen, lock) poisons. Set after the directory block, a failed
  directory open or fsync took the pre-rename branch and kept writing to the unlinked inode
  (fixpp#548 describes that hole).
- **On Windows the rename has three outcomes** (`rename_outcome`, returned by
  `posix_rename_over_open`): not renamed, renamed but `FlushFileBuffers` failed, renamed and
  flushed. Rejected: the one-liner `return ok && flushed`. It reports a flush failure as "not
  renamed", which sends it down the pre-rename branch: the store keeps a handle to the file the
  POSIX-semantics rename already replaced, the same orphan-write hole.
- **Open and reset take their wide paths from one conversion, `store_wide_path`.** Two conversions
  can name two different files: a byte-wise widening and `std::filesystem::path` decode a byte at or
  above 0x80 differently, and a CompID may contain one. The reset then renames onto a file the
  store never opened and still reports success.

⚠️ **Re-derive before relying on it:** read `reset_store_to` and check that `*rename_done = true`
directly follows the successful rename on both branches, before the directory open on POSIX and
before the flush verdict on Windows; and that `store_wide_path` is the only narrow-to-wide
conversion in `open_log` and the reset worker.

Witnesses, in `tests/session/test_file_store_crash_survival.cpp`: POSIX
`FileStoreResetTo.Q29_ADirectoryFsyncFaultAfterTheRenamePoisonsTheStore`,
`Q29_ADirectoryOpenFaultAfterTheRenamePoisonsTheStore` and
`Q29_ResetWithADirectoryFsyncFaultPoisonsTheStore`; Windows
`Q29_AFlushFaultAfterTheRenamePoisonsTheStore`, `Q29_ANonAsciiCompIdResetsTheLogTheFactoryOpened` and
`Q29_ANonAsciiDirectoryResetsTheLogTheFactoryOpened`; and the control
`Q29_Control_AResetToWithNoFaultLeavesTheStoreWritable` on both.

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
