# Data Model: Real file_io_executor offload for FileStore

**Feature**: `035-filestore-io-offload` | **Date**: 2026-06-13

This feature changes **execution**, not the on-disk or public data model. The only structural additions
are internal to `FileStoreImpl`. No public type, no wire field, no `MessageStore` signature, no
`EngineConfig`/`FileStore::Config` field changes (FR-009).

## 1. `FileStoreImpl` delta (`src/session/file_store.cpp:405–443`)

Add one field to the pimpl:

| Field | Type | Purpose | Mutated by | Read by |
|---|---|---|---|---|
| `generation_` | `std::uint64_t` (init `0`) | Monotonic epoch bumped on every successful `reset()` live-handle swap; lets a concurrent `retrieve()` walk detect that the log it snapshotted was replaced/truncated mid-walk. | `reset()` **on the session strand**, at/after `impl_->file = std::move(new_file)` (`:1149`) | `retrieve()` — snapshot under the mutex at index-snapshot time (`:908–932`), re-check before each frame read in the walk (`:940–974`) |

No other `FileStoreImpl` field changes. All existing fields (`OsFile file`, `async_mutex mutex_`,
`store_scratch_`, `retrieve_scratch_`, the indices, the counters, `write_pos`, `log_path_`) keep their
types and roles. `generation_` is plain (mutated strand-only, like every other `impl_` field per
Decision 3), so it needs no atomic.

## 2. The offload helper (file-local, `file_store.cpp` — no exported surface)

A file-local coroutine that runs a single blocking syscall on the `file_io_executor` and returns its
result, with the resume landing back on the caller's (session-strand) executor:

```
// conceptual — exact spelling resolved at implement
template <class Syscall>
asio::awaitable<std::invoke_result_t<Syscall>>
offload_to(asio::any_io_executor pool_ex, Syscall fn);   // = co_await co_spawn(pool_ex, run(fn), use_awaitable)
```

**Invariants**:
- `Syscall fn` is a small callable capturing **only by value** the raw syscall arguments (the `OsFile`
  handle/fd, an offset, a `std::span` into an already-populated buffer). It touches **no** `impl_` field.
- The body runs on `pool_ex`; the outer `co_await` resumes on the session strand (probe-confirmed).
- One ~48 B PMR-opaque frame allocation per call (Decision 1) — **permitted by the `[const §XV.1]` v0.2 §XV.4-offload exemption** (≤1 bounded O(1) frame/op on the FileStore offload path); `MemoryStore::store` stays zero.
- `use_awaitable` (joined), never `detached` (Decision 5).

## 3. Per-method execution map (after the change)

| Method | On the strand (outer coroutine) | On `file_io_executor` (nested lambda) | Linearisation point (unchanged) |
|---|---|---|---|
| `store` | acquire mutex (`:794`); seqnum check; deep-copy → `store_scratch_` (`:820`); counter incr (`:835–839`); `write_pos` update (`:846`); release mutex (`:871`) | `write_frame` `pwrite` (+pad); counter-record `pwrite`; `datasync` (`commit_per_message`) | `fdatasync` return (`:850`) |
| `next_seqnum(_, true)` | acquire mutex (`:998`); overflow check; counter incr (`:1012`); `write_pos` update (`:1023`); release (`:1032`) | counter-record `pwrite` (`:1018`); `datasync` (`:1024`) | counter `pwrite` (`:1018`) |
| `reset` | acquire mutex (`:1050`); **`impl_->file = std::move(new_file)` (`:1149`)**; **`generation_++`**; index clear + counter reset (`:1205–1210`); release (`:1217`) | tmp open; `initialise_fresh` (sentinel+counter `pwrite` + `fdatasync`); close tmp; `rename` (`:1108`); parent-dir `fsync` (`:1120–1136`) [Win: `MoveFileExW`] — all against a **local** `OsFile`, not `impl_->file` | parent-dir `fsync` (`:1129`) / `MoveFileExW` (`:1180`) |
| `retrieve` | acquire mutex (`:908`); index snapshot + **`generation_` snapshot**; release (`:933`); **`pread` per frame (stays on strand)**; **re-check `generation_`**; `visitor.on_frame` `co_await` (`:957`) | — (read path not offloaded, per Clarifications; the `:942` inert post is **removed**) | per-frame visitor resume (read-only; no durable transition) |
| `flush_for_session_close` | `open_ok` check (`:1241`); error-map/return | `datasync` (`:1252`) | `datasync` return |

## 4. The retrieve generation-guard invariant (FR-006 / I-03)

```
g0 := generation_            // sampled inside the mutex, with the index snapshot
for each entry in snapshot:
    if generation_ != g0:    // a reset() ran on the strand during a prior on_frame() suspension
        return store_io_failure   // (§5) never read against the swapped/truncated log; distinct from store_seqnum_gap
    pread(entry) on the strand
    co_await visitor.on_frame(...)      // suspension point — a reset() may interleave here
```

Because `reset()` mutates `impl_->file` and `generation_` only on the strand, and `retrieve`'s `pread`
also runs on the strand, the read and the swap never physically overlap; the guard closes the **logical**
window opened by the `on_frame` suspension. The `generation_` re-check and the `pread` that follows it are
**atomic on the session strand** — there is **no `co_await` between them**, so a `reset()` cannot land in
that gap (the only suspension point is the `visitor.on_frame()` `co_await` *after* the read). An
implementer MUST NOT insert a suspension point between the re-check and the read. (The existing FR-017 gap
detection for concurrent `store()` index growth is retained and unaffected.)

## 5. Error-variant decision (FR-021 freeze) — PINNED

008 FR-021 freezes the store error set at **exactly 10** `store_*` variants (`error.hpp:165–230`); a new
variant is out (breaching FR-021 is a Gate-A blocker). The mid-walk-`reset()` clean-failure value is
**`store_io_failure` (56)** — a read whose live handle was swapped/truncated by a concurrent `reset()` is
an I/O-state failure of the read. It is deliberately **not** `store_seqnum_gap` (57), so a reset-race
cannot masquerade as a logical never-persisted gap (which would mask a real gap bug). The discriminating
test asserts: reset-race → `store_io_failure`; FR-017 logical gap → `store_seqnum_gap`. No new
C-ABI-exported variant; no group-mapping change. (Gate-A-confirmable.)

## 6. Unchanged data

On-disk log format, CRC32 per record, sentinel, atomic-rename reset, the `MemoryStore` entry array, all
public signatures, the wire, and every config field — **unchanged**.
