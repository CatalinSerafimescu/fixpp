# Phase 1 Data Model — async_mutex strand-local drain-reap simplification

The "entities" are the `async_mutex` member set and the `waiter_record`. This file pins the BEFORE→AFTER delta. Line cites are against branch `048-async-mutex-strand-reap` (= main).

## `async_mutex` member set

| Member | Decl (current) | After 048 | Note |
|---|---|---|---|
| `std::atomic<uintptr_t> state_` | `:214` | **KEEP** | LIFO list / `not_locked`(1) / `locked_no_waiters`(0). Unchanged. |
| `std::atomic<waiter_record*> next_drain_head_` | `:218` | **KEEP** | residual FIFO chain. Unchanged. |
| `std::atomic<bool> draining_` | `:233` | **KEEP**, demote to release/acquire (already is) | set by drain; gates new acquirers. Strand-local now. |
| `std::atomic_flag drain_in_progress_` | `:236` | **KEEP** | idempotency guard for re-entrant drain on the strand. |
| `std::atomic<std::uint32_t> active_holders_count_` | `:240` | **KEEP**, demote to relaxed | inc on grant, dec on unlock — all on one strand now; used by the bounded holder-yield loop. |
| `std::atomic<std::uint32_t> active_acquirers_count_` | `:244` | **REMOVE** | in-flight cross-thread acquirer epoch — meaningless on a single strand (a new acquirer either parks before the drain → reaped, or starts after → sees `draining_` → fast-fail). |
| `std::atomic<std::shared_ptr<drain_latch_state>> drain_latch_ptr_` | `:249` | **REMOVE** | the cross-thread latch pointer. **This is the 046 4→3 consumer.** |
| `detail::drain_latch_state` (type, `:352-388`) | `:352` | **REMOVE** entirely | `released_`/`aborted_`/`in_flight_resumptions_`/`channel_` + `notify`/`signal_release`/`signal_abort`/`async_wait`. All cross-thread. |

Net: the mutex loses one `atomic<shared_ptr>` + one `atomic<uint32>` + an entire nested type and its `concurrent_channel`. It keeps two atomic words for the lists + two small drain flags + one holder count.

## `waiter_record` (`:486-518`)

| Field | After 048 | Note |
|---|---|---|
| `mutex_`, `next_`, `phase_`, `result_`, `attached_awaiter_`, `refcount_`, `resume_fn_`, `destroy_exec_fn_` | **KEEP** | core retained machinery (Explore §C). |
| `std::array<std::byte,64> exec_storage_` | **KEEP** | executor placement-new storage. |
| **NEW** `std::array<std::byte,N> post_storage_` + a `post_allocator` | **ADD** | pre-reserved storage for the resume `asio::post` runner closure, so the post is non-allocating (D-3). `N` is pinned by a `static_assert` measured on BOTH libstdc++ and libc++ posted-op storage; the runner is bound via `asio::bind_allocator(post_alloc, runner)`. If a platform needs more than `N`, the *grant decision* falls back to `sync_lock_alloc_failed` synchronously on-strand (catchable), never terminating. |

The embedded **awaiter** (`async_mutex_awaiter`, ≤96 B per §4.2) is unchanged — `post_storage_` lives on the `waiter_record` (the pool/heap record), not on the embedded awaiter, so the §1.1 awaiter byte budget is unaffected.

## `error` enum — NO CHANGE

`error::sync_lock_alloc_failed = 44` (`error.hpp:87`) and `error::sync_lock_drained = 46` (`:97`) both already exist with C-ABI mappings (`:848`, `:852`). 048 reuses `sync_lock_alloc_failed` for the broadened set of fail-closed allocation sites and `sync_lock_drained` for reaped acquirers (unchanged). No enumerator added/renumbered → ABI frozen (FR-008).

## State transitions — `cancel_and_drain()` (AFTER)

```
[not draining]
   │  cancel_and_drain() on the owning strand
   ▼
disable cancellation on self → draining_=true
   │
   ▼
SYNC REAP: exchange state_/next_drain_head_; for each waiter: CAS queued→cancelled,
           result=sync_lock_drained, posted resume (non-allocating)
   │
   ▼
while active_holders_count_ > 0:           ← bounded, O(holders); strand-local
     co_await asio::post(executor)         ← yield so a pre-drain holder unlock() runs
     re-exchange + reap any spliced residual waiters
   │
   ▼
FINALIZE: CAS state_ locked_no_waiters→not_locked   → drained terminal
```

No `released_`/`aborted_` terminal object; no subscriber list; no `signal_*`. A re-entrant `cancel_and_drain()` observes `draining_` and returns ok (idempotent). Per-waiter `phase_` machine `{queued,granted,cancelled}` is unchanged (D-4).

## Validation rules (from FRs)

- FR-001: every begun waiter resolved exactly once; bounded completion (the only loop is `active_holders_count_`-bounded).
- FR-002: no latch/channel/Dekker/acquirer-counter reachable from the terminal-state computation.
- FR-003: the three escaping allocation sites fail closed / are eliminated / are non-allocating (D-3 table).
- FR-004: `phase_` CAS guarantees single-winner resume.
- FR-008: zero enum/ABI delta (abidiff-clean).
- FR-009: removing `drain_latch_ptr_` drops 046's `atomic<shared_ptr>` consumer set 4→3.
