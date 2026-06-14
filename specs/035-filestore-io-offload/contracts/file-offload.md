# Internal Contract: FileStore file_io_executor offload

**Feature**: `035-filestore-io-offload` | **Date**: 2026-06-13

This is an **internal** contract (no exported/public surface). It specifies the behaviour the
implementation must satisfy and the seams test it. It realizes the approved `.specify/2e-msgstore.md`
§4.3.2 / §6.5 / §6.1.4 and 008 FR-024 / FR-020 / I-13 / I-03 / SC-006.

## C1 — Offload primitive contract

For each FileStore method that performs blocking disk I/O (`store`, `next_seqnum(_, true)`, `reset`, and
`flush_for_session_close` — the graceful-close `datasync`, `file_store.cpp:1252`):

- **Pre** (`store` / `next_seqnum(_, true)` / `reset`): invoked on the session strand; the writer mutex
  (`impl_->mutex_`) is held by the outer coroutine for the duration of the offloaded op.
- **Pre** (`flush_for_session_close`): invoked on the session **close** strand at graceful close,
  **after** ordinary store awaitables drain, **outside** the phase-1 cancellable in-flight set;
  **no writer mutex**; **never** surfaces `store_cancelled` (only `store_io_failure` on a flush error) —
  the graceful-close durability seam per 2e §6.2.1:1025 (`file_store.cpp:1239–1257` has no `async_lock()`).
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

Per the authoritative §6.1.4 (`:990–1006`) the per-method contract is **binary** — there is **one**
cancellation observation point, the `async_mutex` acquire (on the strand, before any `co_spawn` is
issued). It governs `store`, `next_seqnum(_, true)`, and `reset` (each mutex-guarded). It does **not**
govern `flush_for_session_close` (see the carve-out below).

| When cancellation wins | Result | State |
|---|---|---|
| at/before mutex-acquire (no `co_spawn` issued, no syscall) | `store_cancelled` | no change |
| after the mutex is held and the child is issued (syscall runs to durable completion, uninterruptibly) | normal success | durable |

Once the mutex is held and the child is `co_spawn`'d, the offloaded syscall is not an asio suspension
point, so it cannot be interrupted mid-call: it runs to **durable completion**, and cancellation is
observed (if at all) only when the outer `co_await` resumes on the strand, linearised at the durable
transition — never a torn/partial state, and a durable success is never lost (the §XV.15-safe direction:
queued-then-run-to-completion leaves the counter and FSM consistent and a later peer `ResendRequest`
honourable).

**Retained — `operation_aborted`→durable try/catch, simplified to UNCONDITIONAL**
(`[[feedback_async_mutex_us3_asio_cancel_and_subagent_seams]]`): since the only observation point is the
mutex-acquire, any `operation_aborted` surfaced at the *outer* await necessarily post-dates linearisation
(the syscall is non-interruptible and has already run to durable completion). The implementation MUST
**unconditionally** catch it and return the durable success — it MUST NOT return `store_cancelled` for a
frame that is on disk (a false-cancel on durable state is the `[const §XV.15]`-adjacent silent-loss
class). The nested `co_spawn` MUST NOT install a cancellation path that swallows `cancellation_type::total`
and wedges teardown (`[[feedback_asio_cospawn_total_cancellation_default]]`).

**Carve-out — `flush_for_session_close` is NOT in this cancellation table.** Per 2e §6.2.1:1025 it is the
graceful-close durability seam: it runs to completion outside the cancellable in-flight set and **never**
surfaces `store_cancelled` (a flush error maps only to `store_io_failure`). It is offloaded (the offload
helper still applies) but it is **not cancellable**.

**Witness**: the FileStore per-method cancellation seam (the existing
`test_store_cancellation_contract.cpp` is MemoryStore-only — `:44–52`) covering both sub-cases: fire
at mutex-acquire → `store_cancelled` + 0 state change; fire while the syscall is mid-flight (a
deterministic in-syscall hook) → normal + durable, **not** `store_cancelled` (proves a durable success is
never lost, exercising the retained unconditional catch). Mutation-proven discriminating.

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

The `file_io_executor` pool is application-owned (the engine does not own or join it). The contract has
two complementary sub-properties:

**C5a — Engine/terminal-close drain.** After `Engine::stop()` returns, no Session-reachable in-flight
`store()` offload is in flight. `Engine::stop()` drives `close(terminal)` at every call site
(`engine.cpp:517,947,1042,1085,1366`); `terminal` skips phase-1 (`session.cpp:1241`) and therefore does
NOT invoke `flush_for_session_close`. The drain is established by **code-analysis**:
`engine.cpp:1184–1333` `co_await`s every in-flight Session-reachable `store()` offload; the offload is
`use_awaitable`, never detached — join is structurally guaranteed, no UAF possible. The application may
then safely join the pool. **Witness**: `test_store_shutdown_ordering.cpp` Test 6 — **pool-level
offload-joined-before-pool-join invariant** (standalone `FileStore` + app-owned pool + in-flight
`store()` + ASan/TSan). [gate-b/r2 R#1b: removed `flush_for_session_close() drain` from this clause —
terminal close does NOT invoke the flush (line above); Test 6 witnesses the pool-join ordering
invariant, not `Engine::stop()` directly; the Engine drain proof is code-analysis.]

**C5b — Graceful-close flush.** `Session::close(close_mode::graceful)` invokes the A1 typed-thunk
`flush_for_session_close` (`session.cpp:1258–1264`) and `co_await`s it to durable completion (C1 — never
detached). The flush join is structurally guaranteed; no UAF is possible on the graceful path.

`stop()` **cannot** drain a **direct** (non-Session) `FileStore` call made outside Session ownership;
for those the **caller obligation** is that the `file_io_executor` MUST outlive all outstanding store
awaitables (a misuse note in `file_store.hpp:Config::file_io_executor` documents this).

## C6 — No-surface-change contract (FR-009 / Art. X)

No public `MessageStore` / `retrieve_visitor` / factory signature, no error-taxonomy C-ABI slot
(the reused mid-walk variant is pre-existing), no `EngineConfig`/`FileStore::Config` field, no codegen,
no wire byte changes. `MemoryStore` behaviour is byte-identical. `tools/check_layers.py` unaffected
(the offload helper is file-local in `file_store.cpp`).
