# Internal Contract: FileStore file_io_executor offload

**Feature**: `035-filestore-io-offload` | **Date**: 2026-06-13

This is an **internal** contract (no exported/public surface). It specifies the behaviour the
implementation must satisfy and the seams test it. It realizes the approved `.specify/2e-msgstore.md`
§4.3.2 / §6.5 / §6.1.4 and 008 FR-024 / FR-020 / I-13 / I-03 / SC-006.

## C1 — Offload primitive contract

For each FileStore method that performs blocking disk I/O (`store`, `next_seqnum(_, true)`, `reset`):

- **Pre**: invoked on the session strand; the writer mutex (`impl_->mutex_`) is held by the outer
  coroutine for the duration of the offloaded op.
- **Behaviour**: the blocking syscall(s) execute on `impl_->cfg.file_io_executor`; the awaitable resumes
  on the session strand.
- **Post**: the syscall's effect is realized on disk; every `impl_` field mutation happened on the
  strand (before/after the `co_await`); the mutex is released on the strand after resume.
- **Allocation**: ≤ one bounded (~48 B) coroutine-frame allocation per op (Decision 1). `MemoryStore`
  performs **zero** allocations (unchanged).
- **Join**: the offloaded op is `co_await`-ed (`use_awaitable`); it is never `detached`.

**Witness**: a thread-id probe captured inside the syscall window shows the syscall thread is a pool
thread ≠ the session-strand thread (SC-001); a unit of work queued on the strand completes before the
in-flight `fdatasync` returns (SC-002).

## C2 — Durability contract (FR-003)

`store()` under `commit_per_message` completes the awaitable with success **only after** `fdatasync`
(Linux) / `FlushFileBuffers` (Windows) has returned success. The offload changes where the flush runs,
never whether it precedes completion. Crash-survival (008 SC-002): N `commit_per_message` stores then
SIGKILL → all N recovered on restart, 0% loss. The on-disk format and the restart-scan/torn-write
algorithm are byte-unchanged.

## C3 — Cancellation contract per method (FR-004 / SC-006 / §6.1.4)

Single cancellation checkpoint: the `async_mutex` acquire.

| When cancellation wins | Result | State |
|---|---|---|
| **before** the linearisation point (observed at mutex-acquire; no `co_spawn` issued) | `store_cancelled` | no change |
| **after** the offload began (syscall runs to completion uninterruptibly) | normal success | durable |

The offloaded syscall is not an asio suspension point, so it cannot be interrupted mid-call; a durable
success is never lost. If the slot-already-signalled probe shows the nested `co_spawn` can run the child
and then surface `operation_aborted` post-linearisation, the implementation MUST catch it and return the
durable result (`[[feedback_async_mutex_us3_asio_cancel_and_subagent_seams]]`). The nested `co_spawn` MUST
NOT install a cancellation path that swallows `cancellation_type::total` and wedges teardown
(`[[feedback_asio_cospawn_total_cancellation_default]]`).

**Witness**: the FileStore per-method cancellation seam (the existing
`test_store_cancellation_contract.cpp` is MemoryStore-only — `:44–52`): fire before linearisation →
`store_cancelled` + 0 state change; fire after → normal + durable. Mutation-proven discriminating.

## C4 — Concurrency contract (FR-005 / FR-006 / I-03)

- `store` / `next_seqnum` / `reset` are mutually exclusive via `impl_->mutex_`.
- `retrieve` snapshots the index under the mutex, releases it, then walks; its `pread` stays on the
  session strand (Clarifications). All `impl_` mutation is strand-confined ⇒ `impl_` is single-threaded
  ⇒ no data race on `impl_` or the live handle (TSan-clean, SC-005).
- A `reset()` that interleaves a `retrieve()` walk (at an `on_frame` suspension) is detected via the
  `generation_` guard (data-model §4): the walk fails cleanly with `store_io_failure` (distinct from
  `store_seqnum_gap`, so a reset-race can't masquerade as a logical gap — data-model §5); it never reads
  through a swapped/truncated handle.

**Witness**: real 4-thread `asio::thread_pool` `file_io_executor`; concurrent `store`/`retrieve`/`reset`
under TSan + ASan; zero reports; the FR-017 gap-detection still holds; the generation guard fires under a
driven mid-walk `reset()`.

## C5 — Teardown contract (FR-007 / SC-007)

The `file_io_executor` pool is application-owned (the engine does not join it). After `Engine::stop()`
returns, no FileStore pool work is in flight: every store op is awaited by its caller, and `stop()`
(`engine.cpp:1184–1333`) drains all role loops + in-flight sends before returning. The application may
join the pool only after `stop()` completes. **Witness**: shutdown-ordering seam with in-flight offloaded
`store()` + `Engine::stop()` under ASan/TSan — no UAF, no use-of-joined-pool.

## C6 — No-surface-change contract (FR-009 / Art. X)

No public `MessageStore` / `retrieve_visitor` / factory signature, no error-taxonomy C-ABI slot
(the reused mid-walk variant is pre-existing), no `EngineConfig`/`FileStore::Config` field, no codegen,
no wire byte changes. `MemoryStore` behaviour is byte-identical. `tools/check_layers.py` unaffected
(the offload helper is file-local in `file_store.cpp`).
