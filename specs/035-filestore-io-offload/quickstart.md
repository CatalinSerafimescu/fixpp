# Quickstart: witnessing the FileStore offload

**Feature**: `035-filestore-io-offload` | **Date**: 2026-06-13

The recipes the seams implement. Recipes A/B use a **real multi-thread** `asio::thread_pool` as the
`file_io_executor` for the concurrency surface; Recipe A also carries a **single-thread-pool witness
variant** (Recipe A2 below) — a one-thread pool **distinct from the session strand** is a valuable witness
(it proves genuine offload without leaning on pool parallelism, and is the deadlock-risk configuration the
spec Edge Cases require to be correct). A *never-run* executor is the only configuration that masks the
property (the syscall can never reach a pool thread) — that is the backpressure/suspend edge case, not a
witness.

## Recipe A — the syscall runs on a pool thread, not the strand (SC-001 / SC-002)

1. Build a `FileStore` (`commit_per_message`) with `file_io_executor` = a 4-thread `asio::thread_pool`,
   driven from a single-thread "session strand".
2. Capture `g_strand_tid` on the strand. Instrument the offloaded syscall (or a test seam wrapping it) to
   record `std::this_thread::get_id()` **inside the syscall window**.
3. `co_await store(seq, frame, dir)` from the strand.
4. **Assert**: the recorded syscall thread ≠ `g_strand_tid` (it is a pool thread) → SC-001; and a second
   unit of work posted to the strand runs before the in-flight `fdatasync` returns → SC-002.

The standalone probe shape (`research/probes/cospawn_probe.cpp`) is the reference: it shows 5000/5000
syscalls on a non-strand thread and 5000/5000 resumes back on the strand.

## Recipe A2 — single-thread-pool offload witness (spec Edge Case)

1. Same as Recipe A but the `file_io_executor` is a **single-thread** `asio::thread_pool` distinct from the
   session-strand thread.
2. `co_await store(...)` from the strand.
3. **Assert**: the syscall thread ≠ `g_strand_tid` (genuine offload without relying on pool parallelism)
   **and** the call completes with **no deadlock** (the strand and the one pool thread are distinct, so the
   strand→pool→strand handoff makes progress). This is the configuration the spec Edge Cases require to be
   correct.

## Recipe B — concurrent store/retrieve/reset is race-free (SC-005)

1. Same real 4-thread pool `file_io_executor`.
2. Drive, overlapping: a stream of `store()` calls; a `retrieve()` walk over the persisted range; and a
   `reset()`.
3. Build under TSan + ASan.
4. **Assert**: zero TSan reports on `impl_` / the live log handle; `retrieve()` returns coherent frames
   (FR-017 gap detection intact); a `reset()` driven **during** a `retrieve()` walk makes the walk fail
   cleanly via the `generation_` guard (data-model §4), never a torn read.

## Recipe C — cancellation contract (SC-004)

For each of `store`, `next_seqnum(_, true)`, `reset`, exercise the **three** sub-cases (the FileStore twin
of the MemoryStore-only `test_store_cancellation_contract.cpp`):

1. **Checkpoint (i) — at mutex-acquire**: fire the slot before the method acquires the writer mutex →
   assert `store_cancelled` + 0 state change (no `co_spawn` issued, no syscall).
2. **Checkpoint (ii) — queued-not-picked-up** (the new hard case): use a **stalled / saturated**
   `file_io_executor` so the child is enqueued but not yet picked up; fire `cancellation_type::total`
   before pickup → assert **no syscall ran**, **0 state change**, result `store_cancelled`. Requires a
   deterministic hook for the child-not-picked-up state.
3. **Child-in-syscall / after-linearisation**: let the child pickup and enter the syscall (a deterministic
   child-entered-syscall hook), fire after → assert normal completion + durable state (the syscall is not
   interruptible mid-call; a durable success is never lost).

Also assert the `co_spawn` terminal-only default does **not** swallow `total` and wedge while
`Engine::stop()` emits `total` (`[[feedback_asio_cospawn_total_cancellation_default]]`).

## Recipe D — shutdown ordering (SC-007)

Queue in-flight offloaded `store()` work **and** drive a graceful close (`Session::close(graceful)` →
`flush_for_session_close` under `commit_batched`, where the close-flush has buffered frames to drain), then
`Engine::stop()`; build under ASan + TSan. **Assert**: no UAF, no use-of-joined-pool — `stop()` returns
only after every **Session-reachable** store-awaiting / close-flush coroutine completes, so the app-owned
pool is safe to join afterwards. (Direct non-Session FileStore use carries the caller obligation that the
executor outlives all outstanding store awaitables — a misuse note, not drained by `stop()`.)

## Regression — MemoryStore + surfaces unchanged (SC-006)

Run the full existing 008 store suite. **Assert**: `MemoryStore` cells byte-identical; no public store
signature / error / config diff; the on-disk crash-survival + torn-write seams still pass (the on-disk
format is untouched).
