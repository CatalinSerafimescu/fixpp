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
  - include/fixpp/core/sync/async_mutex.hpp
  - .specify/544-hot-path-zero-alloc.md
  - .specify/536-537-538-recovery-liveness.md
  - .specify/624-offload-completion-lifetime.md
  - .specify/2e-msgstore.md
  - specs/035-filestore-io-offload/contracts/file-offload.md
  - specs/093-inbound-frame-dispositions/spec.md
  - specs/093-inbound-frame-dispositions/plan.md
  - specs/093-inbound-frame-dispositions/contracts/inbound-frame-dispositions.md
  - spec/behaviors-and-limitations.md
  - tests/session/test_file_store_crash_survival.cpp
  - tests/session/test_598_store_offload.cpp
  - tests/session/test_624_offload_completion_lifetime.cpp
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/2e-msgstore.md
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
  awaited, never detached. ⚠️ **That holds on a file executor that runs the jobs handed to it**: on one
  that was stopped first, or has no thread, the call never returns, and neither does a stop or a close
  that waits for it. The host must keep the executor running until `Engine::stop()` has returned
  (fixpp#598, D-598-8; B&L `B-598-2`). And before fixpp#598, on a pool of more than one thread, the
  pool work could complete without running its body (the section below).
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
`FIXPP_DETAIL_CO_AWAIT_LOCK` (2f Erratum E-6), and its leading post is awaited as `deferred`. Each removes
a cycled coroutine frame from the store chain: slot headroom under the exported count, witnessed by arms that
fill the slots to exactly that count, not the reason W-D reads zero. `next_seqnum`,
`reset`, `reset_to` and `retrieve` stay on the public `async_lock`, and FileStore is not converted
(`.specify/544-hot-path-zero-alloc.md` §2.3). **This page's argument is unchanged:** the macro is the
same lock with the same waiter and reap model, so the store's mutex still has no holder and no waiter
once no store `co_await` is in flight. Re-derive the converted sites with
`git grep -n "FIXPP_DETAIL_CO_AWAIT_LOCK" -- src include`.

⚠️ **Documents that describe `store()`'s lock as the public `async_lock`**, written before fixpp#544 and
not updated: `.specify/2e-msgstore.md`'s `MemoryStore::store` latency-budget row, and
`specs/008-message-store/contracts/message_store.hpp`'s fast-path comment. Their lock semantics still
hold; for `MemoryStore::store` the route is the macro (2f Erratum E-6). Frozen bundles are annotated
here, not edited.

The 2e convergence record (parent repo, `decisions/2e-msgstore.md`) is the **why** of the store's
mutex, of durable-before-transmit and of the atomic `reset()`: current as history. ⚠️ Its Windows
`reset()` primitive, `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`, is
**superseded**: the reset now renames with POSIX semantics over the open log and flushes, with three
outcomes (the FileStore reset section below). Re-derive with
`grep -n "posix_rename_over_open\|MoveFileExW" src/session/file_store.cpp`.

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
- **A failure the job THROWS is a failure like a returned one (fixpp#599, D-599-1).** ⚠️ Until
  B57, an exception out of the reset's job (an allocation after the rename) left the offload's await
  and `reset_store_to` without reaching Region 3: no poison, and later writes went to the unlinked
  inode. Now (a) the POSIX branch builds every post-rename path before the rename, and (b)
  `offload_reset_job`, through which `reset_store_to` awaits the job, catches any exception and
  returns false, so Region 3 reads `rename_done` and poisons after the rename. Design:
  `.specify/624-offload-completion-lifetime.md` §7; witness
  `ResetThrowAfterRename.PoisonsTheStoreAndReturnsTheResetsError` and the `ResetStepArms.*` cells in
  `tests/session/test_611_store_offload_error_arms.cpp`.

⚠️ **Re-derive before relying on it:** read `reset_store_to` and check that `*rename_done = true`
directly follows the successful rename on both branches, before the directory open on POSIX and
before the flush verdict on Windows; and that `store_wide_path` is the only narrow-to-wide
conversion in `open_log` and the reset worker; and that `reset_store_to` awaits its job through
`offload_reset_job`, whose `catch (...)` returns false.

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

## The offload's cancellation line, and three signed sentences it supersedes (fixpp#598)

The design is `.specify/536-537-538-recovery-liveness.md` §11 (D-598-1 to D-598-8, rulings R-25 to R-27);
the user-facing rows are B&L `B-598-1`, `B-598-2`, `L-598-1` and `B-035-1`'s amendment.

- **The line is the grant of the store's lock** (R-26). Before it, a cancellation ends the call with
  nothing stored. After it, `offload_to` completes through a token bound to an empty cancellation
  slot, so no cancellation reaches the write job: the job runs its body, on an executor that runs its
  jobs, and the call returns the write's own result. Before fixpp#598 the token was plain
  `use_awaitable`: a terminal cancellation on the awaiting thread handed the pool a second job, and on
  a pool of more than one thread that job could run first, so asio's pre-check skipped the write's body
  and the handler around the offload reported the skipped write as a durable success (measured on the
  tree before the fix, `tests/session/test_598_store_offload.cpp`). The two jobs also raced on the
  spawned coroutine's cancellation state, which ThreadSanitizer reported.
- **The handlers were deleted, not corrected** (D-598-2), with the counter and the seam that existed
  only for them.

| Signed document | The sentence | Status |
|---|---|---|
| `specs/035-filestore-io-offload/contracts/file-offload.md`, C3 | "The implementation MUST **unconditionally** catch it and return the durable success", with its premise that an `operation_aborted` at the outer await "necessarily post-dates linearisation" | ⚠️ **SUPERSEDED by the note's §11, D-598-2.** asio enters that handler for a cancellation in one case only: the job's body never ran. The handler is deleted. C3's table and its one observation point, the lock, **stand**: R-26 confirms them. The bundle is frozen and not edited. |
| `.specify/2e-msgstore.md` §6.1.4, the list "The linearisation point for each method is precisely defined", in its entries for the `FileStore` (`store`, `next_seqnum(_, true)`, `reset`), with the footnote "Linearisation-as-durability" | the durable transition, the flush's or the rename's return, as the point that splits the cancellation table's "before" from its "after" | ⚠️ **SUPERSEDED as the boundary of the cancellation table** by 035's C3 and ruling R-26, which put the line at the grant of the store's lock: a cancellation between the grant and the flush's return does not win. **Still the definition of when an effect is durable.** The table's cells stand. Not edited for it. |
| `.specify/2e-msgstore.md` §6.2.1, step 1 | "in-flight ops complete with `store_cancelled`" | ⚠️ **SUPERSEDED (D-598-1, "Whose line it is").** A store call that is past its lock completes with its write's result, not `store_cancelled`, and the close waits for it (D-598-3, D-598-4). The close cancels its losing arm with `cancellation_type::all`, not `total`. Not edited for it. |

- **Rejected** (§11.4): withdrawing the job so that the call returns at the cancellation (asio cannot
  remove a posted job, and the job holds a descriptor and a span into the store's buffer, which the
  next call would rewrite); the line at the moment a pool thread starts the job (R-26's other option:
  it keeps the split decided by which pool thread runs first, and the data race); the same line drawn
  with a flag of the store's own (an atomic and a slot handler on every call); a handler with a true
  meaning in place of the deletion (no input reaches it, so no cell can hold it true); the handlers left
  as they were; the whole store call made uncancellable, the wait at the lock included (it reverses
  R-15 for a call queued at the lock); undoing a completed write by truncating the log; a store call
  that completes on an executor that runs nothing (its completion is itself a job of that executor);
  repairing a damaged log at open (R-27: the open scan is not changed, `L-598-1`).
- **`retrieve` after a reset (fixpp#538).** Both stores look a number up relative to the first frame
  the direction retains; `.specify/2e-msgstore.md` carries an erratum for it at its end, and
  `message_store.hpp`'s `retrieve` comment binds third-party stores (B&L `B-538-1`, `B-538-3`).

## The offload's completion hand-off (fixpp#624)

The design is `.specify/624-offload-completion-lifetime.md`, decision **D-624-1** (offload completion:
the caller waits for the job's last touch). `offload_to` no longer awaits a nested `co_spawn`: it posts a
job of its own to the file executor, the job posts the completion back to the caller's executor and
then stores a flag in the awaiting frame as its last action, and the caller waits for that flag before
the store call returns. Before it, the file I/O thread released copies of the caller's executor (and
could drop a strand's last reference) after the caller had resumed, so a host that destroyed its own
execution context while the file I/O pool lived on met a use-after-free. D-598-1 and D-598-2 (no
cancellation reaches the job; the body's own result is returned) are kept. Re-derive the current shape
from `offload_to` in `src/session/file_store.cpp`; this paragraph is a pointer, not the code.

Documents that describe `offload_to` as a nested `co_spawn`. The live B&L row is amended; the others are not edited, and each is flagged here.

| Document | The claim | Status |
|---|---|---|
| `specs/035-filestore-io-offload/research.md`, Decision 1 | the offload is a nested `co_spawn(pool, fn, use_awaitable)`; a hand-rolled `async_initiate` two-post is rejected as "strictly worse (2 allocs)" | ⚠️ **SUPERSEDED as the mechanism by D-624-1**, which is an `async_initiate` with two posts. The allocation comparison was 035's measurement of its own probes; re-measure, do not cite it (the note's §3, "Allocation", gives the procedure). |
| `specs/035-filestore-io-offload/data-model.md` §2 | `offload_to(...) // = co_await co_spawn(pool_ex, run(fn), use_awaitable)`, "the final, intended shape" | ⚠️ **SUPERSEDED by D-624-1.** The bundle is frozen and not edited. |
| `specs/035-filestore-io-offload/plan.md`; `checklists/api-abi-nfr.md` CHK010 | the sites become "nested co_spawn"; the nested-co_spawn allocation per op | ⚠️ **SUPERSEDED as the mechanism by D-624-1.** History of feature 035. |
| `spec/behaviors-and-limitations.md`, `B-035-1` | the offload ran "via genuine `co_await asio::co_spawn(…)`" with one `co_spawn` frame per disk op | ✅ **AMENDED for fixpp#624**: the mechanism sentence now describes the posted job and the last touch, and the allocation is stated as a condition. `B-624-1` is the host-facing row. |
| `.specify/563-564-callback-send.md` §2.6, "How a cancellation reaches the write" | `offload_to` "awaits a `co_spawn` onto the file executor", and asio's cancellation handler hands the executor a second job | ⚠️ **HISTORY.** True of the tree before fixpp#598 for the cancellation, and before D-624-1 for the `co_spawn`. |
| `include/fixpp/session/message_store.hpp`, `store`'s comment | "before the nested co_spawn that offloads pwrite / fdatasync to file_io_executor" | ⚠️ **STALE on the mechanism since D-624-1**; the obligation it states (deep-copy before any suspension) holds. |

**D-632-1 (fixpp#632): the `FileStore` index keeps the last record of a sequence number.** A
`store()` that failed after its frame record was written leaves that record in the log; a later
`store()` of the same number replaces it in the index, at open and live, and the open refuses a frame
record below a direction's last number. No document read for this page described the index as
append-only by number. (The log stays append-only; that sentence in `.specify/2e-msgstore.md` §6.3 is
about the file and still holds.) Design: `.specify/624-offload-completion-lifetime.md` §8; B&L
`B-632-1`.

**D-635-1 (fixpp#635): a failed `FileStore` write restores the log's committed end.** A failed
`store()` or increment truncates the log back to where the call started and flushes that; if it cannot,
the store is poisoned until it is opened again. So in a live process a failed store leaves no record,
and D-632-1 governs only what a crash leaves. Design: `.specify/624-offload-completion-lifetime.md` §9;
B&L `B-635-1`, and `B-632-1`'s amendment.

## Note for reviewers

A symbol grep for `cancel_and_drain` across the decision records produces **false leads**:
`pr321-drain-terminate-and-cancel-drain-gateb.md` and `pr325-322-quiesce-delegation-gateb.md` both
match and are **tests-only** (the `~quiesce_on_exit` harness, "no `src/`/`include/` change"). A symbol
grep cannot tell a production disposition from a test-harness one. The answer was in the
`file_store.hpp` header comment and the `Engine::stop()` ordering — not in the records that matched
the symbol.
