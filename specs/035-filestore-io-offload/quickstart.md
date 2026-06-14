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

For each of `store`, `next_seqnum(_, true)`, `reset`, exercise the **two** sub-cases (the FileStore twin
of the MemoryStore-only `test_store_cancellation_contract.cpp`):

1. **At mutex-acquire**: fire the slot before the method acquires the writer mutex →
   assert `store_cancelled` + 0 state change (no `co_spawn` issued, no syscall).
2. **Mid-syscall / after-linearisation** (the unconditional-durable witness): let the child pick up and
   enter the syscall, then fire cancellation **while the syscall is mid-flight** (a deterministic
   in-syscall hook) → assert the op completes **durable, NOT `store_cancelled`** (the syscall is not
   interruptible mid-call; a durable success is never lost — this exercises the retained unconditional
   `operation_aborted`→durable catch).

Also assert the `co_spawn` terminal-only default does **not** swallow `total` and wedge while
`Engine::stop()` emits `total` (`[[feedback_asio_cospawn_total_cancellation_default]]`).

## Recipe D — shutdown ordering (SC-007)

> **Gate B r2 correction (2026-06-14):** `Engine::stop()` drives **terminal** close at all sites, which
> SKIPS the graceful-only `flush_for_session_close` — so the original single "Engine::stop() + graceful
> flush co-occurrence" recipe was structurally impossible. The witness is **split** into two independent
> paths (contracts §C5a/§C5b):

**D1 — Engine terminal-close drain (C5a).** The `Engine::stop()` Session-reachable-offload drain is
verified by **code-analysis** (`engine.cpp:1184–1333` drains the role loops, which `co_await` the
Session-reachable `store()` work; the offload is `use_awaitable`/joined), PLUS the pool-level
`test_store_shutdown_ordering` seam (ASan+TSan) which witnesses the **offload-is-joined-before-pool-join**
invariant — the actual UAF guard: queue in-flight offloaded `store()` work, await it, then `pool.stop()/join()`;
assert no UAF / no use-of-joined-pool. (Direct non-Session FileStore use carries the caller obligation that
the executor outlives all outstanding store awaitables — a misuse note, not drained by `stop()`.)

**D2 — Session graceful-close flush (C5b).** `Session::close(graceful)` → `flush_for_session_close` under
`commit_batched` is witnessed by `SessionGracefulCloseFlushesFileStore.FlushRunsAndFramesDurableAfterClose`
(a real `Session`+`FileStore`), made **discriminating** by `g_flush_datasync_count` (incremented only after
the flush's `fdatasync` succeeds) asserted `>= 1`, plus the durability check.

## Regression — MemoryStore + surfaces unchanged (SC-006)

Run the full existing 008 store suite. **Assert**: `MemoryStore` cells byte-identical; no public store
signature / error / config diff; the on-disk crash-survival + torn-write seams still pass (the on-disk
format is untouched).
