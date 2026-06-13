# Quickstart: witnessing the FileStore offload

**Feature**: `035-filestore-io-offload` | **Date**: 2026-06-13

Two recipes the seams implement. Both use a **real** multi-thread `asio::thread_pool` as the
`file_io_executor` (a single-thread or never-run executor would mask the property under test — see
spec Edge Cases).

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

## Recipe B — concurrent store/retrieve/reset is race-free (SC-005)

1. Same real 4-thread pool `file_io_executor`.
2. Drive, overlapping: a stream of `store()` calls; a `retrieve()` walk over the persisted range; and a
   `reset()`.
3. Build under TSan + ASan.
4. **Assert**: zero TSan reports on `impl_` / the live log handle; `retrieve()` returns coherent frames
   (FR-017 gap detection intact); a `reset()` driven **during** a `retrieve()` walk makes the walk fail
   cleanly via the `generation_` guard (data-model §4), never a torn read.

## Recipe C — cancellation contract (SC-004)

For each of `store`, `next_seqnum(_, true)`, `reset`: fire the cancellation slot **before** the
linearisation point → assert `store_cancelled` + 0 state change; fire **after** → assert normal
completion + durable state. This is the FileStore twin of the MemoryStore-only
`test_store_cancellation_contract.cpp`.

## Recipe D — shutdown ordering (SC-007)

Queue in-flight offloaded `store()` work, then `Engine::stop()`; build under ASan + TSan. **Assert**: no
UAF, no use-of-joined-pool — `stop()` drains every store-awaiting coroutine before returning, so the
app-owned pool is safe to join afterwards.

## Regression — MemoryStore + surfaces unchanged (SC-006)

Run the full existing 008 store suite. **Assert**: `MemoryStore` cells byte-identical; no public store
signature / error / config diff; the on-disk crash-survival + torn-write seams still pass (the on-disk
format is untouched).
