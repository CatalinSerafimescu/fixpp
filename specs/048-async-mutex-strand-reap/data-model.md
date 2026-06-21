# Phase 1 Data Model — async_mutex strand-local drain-reap simplification

The "entities" are the `async_mutex` member set and the `waiter_record`. This file pins the BEFORE→AFTER delta. Line cites are against branch `048-async-mutex-strand-reap` (= main).

## `async_mutex` member set

| Member | Decl (current) | After 048 | Note |
|---|---|---|---|
| `std::atomic<uintptr_t> state_` | `:214` | **KEEP** | LIFO list / `not_locked`(1) / `locked_no_waiters`(0). Unchanged. |
| `std::atomic<waiter_record*> next_drain_head_` | `:218` | **KEEP** | residual FIFO chain. Unchanged. |
| `std::atomic<bool> draining_` | `:233` | **KEEP**, demote to release/acquire (already is) | set by drain; gates new acquirers. Strand-local now. |
| `std::atomic_flag drain_in_progress_` | `:236` | **KEEP** | shipped single-reaper election flag; reentrant-drain *completion* is now signaled by `draining_complete_` (New P3-N2), so this member's role narrows to the first-caller election only. |
| `std::atomic<std::uint32_t> active_holders_count_` | `:240` | **KEEP**, demote to relaxed | inc on grant, dec on unlock — all on one strand now; used by the holder-yield loop and the terminal condition. |
| `std::atomic<std::uint32_t> active_acquirers_count_` | `:244` | **KEEP**, demote to relaxed (strand-local) | cheap insurance against the exact 006/047 lost-wake shape (a new acquirer that has passed the `draining_` check but not yet CAS'd into `state_`). Retained as a plain strand-local counter — NOT removed (New P2-N2). |
| `std::atomic<std::shared_ptr<drain_latch_state>> drain_latch_ptr_` | `:249` | **REMOVE** | the cross-thread latch pointer. **This is the 046 4→3 consumer** (one of: engine reader-snapshot, transport cert-source, pinset snapshot, this — New P2-N1). |
| `detail::drain_latch_state` (type, `:352-388`) | `:352` | **REMOVE** entirely | `released_`/`aborted_`/`in_flight_resumptions_`/`channel_` + `notify`/`signal_release`/`signal_abort`/`async_wait`. All cross-thread. |
| **NEW** `std::atomic<std::uint32_t> in_flight_resumers_` | — | **ADD** (relaxed/strand-local) | strand-local in-flight-resumer count — the SAME role as the removed `drain_latch_state::in_flight_resumptions_`, moved onto the mutex so it survives removing the latch. Incremented when the reap/grant schedules a posted resume; decremented in the resume runner AFTER `release_ref` (`async_mutex.hpp:670-690`). **THE barrier** that keeps the mutex alive until every posted resumer has dereferenced `record->mutex_` (fixes P1-1 UAF). |
| **NEW** `std::atomic<bool> draining_complete_` | — | **ADD** (strand-local) | set true at finalize. A reentrant `cancel_and_drain()` on the strand `while (!draining_complete_) co_await asio::post(executor, use_awaitable)` then returns the terminal result — it does NOT return ok eagerly (fixes P1-2 false-success). |

Net: the mutex loses one `atomic<shared_ptr>` (`drain_latch_ptr_`) + an entire nested type and its `concurrent_channel`; it ADDS one `atomic<uint32>` (`in_flight_resumers_`) + one `atomic<bool>` (`draining_complete_`); it KEEPS `active_holders_count_` and `active_acquirers_count_` (both demoted to relaxed). It keeps two atomic words for the lists + the small drain flags.

## `waiter_record` (`:486-518`)

| Field | After 048 | Note |
|---|---|---|
| `mutex_`, `next_`, `phase_`, `result_`, `attached_awaiter_`, `refcount_`, `resume_fn_`, `destroy_exec_fn_` | **KEEP** | core retained machinery (Explore §C). |
| `std::array<std::byte,64> exec_storage_` | **KEEP** | executor placement-new storage. |

`waiter_record` is **unchanged** by 048. The pre-reserved non-allocating post allocator (`post_storage_`) is **NOT** added — that design is dropped (see "OOM scope" below): the resume `asio::post` (`:614`) remains a **pre-existing** OOM-terminate site, deferred as L-048, not made non-allocating by this feature.

## `error` enum — NO CHANGE

`error::sync_lock_alloc_failed = 44` (`error.hpp:87`), `error::sync_lock_aborted` and `error::sync_lock_drained = 46` (`:97`) all already exist with C-ABI mappings (`:848`, `:852`). 048 reuses `sync_lock_alloc_failed` for the broadened set of fail-closed allocation sites (`inherited_slot.assign`). **Result codes (New P1-N1):** reaped/currently-parked waiters KEEP `sync_lock_aborted` (shipped behavior, `async_mutex.hpp:1129-1130`) — NOT `sync_lock_drained` — so FR-007 "no observable change" holds. `sync_lock_drained` is returned ONLY to NEW post-draining `async_lock` callers (unchanged). No enumerator added/renumbered → **C ABI frozen**; but `sizeof(async_mutex)` CHANGES (C++ layout) — see ABI note below (FR-008).

### ABI / C++ layout (New P1-5)

The C ABI is unchanged (no new/renumbered error code; abidiff-clean). BUT removing `drain_latch_ptr_` (~16 B) + the nested `drain_latch_state` and adding `in_flight_resumers_`/`draining_complete_` CHANGES `sizeof(async_mutex)` and the offsets of every type that embeds it by value (`seqnum_manager`, `memory_store`, `file_store`). This is a header-mostly library → **recompile-required**, NOT a runtime `.so` break. A compile-time `sizeof`/`alignof` layout golden is added as a witness and re-baselined. Claim: "C ABI frozen; C++ layout changes → header-recompile; layout golden re-baselined." Do NOT claim "no ABI change."

## State transitions — `cancel_and_drain()` (AFTER)

```
[not draining]
   │  cancel_and_drain() on the owning strand
   ▼
reentrant? (draining_ already set):  while (!draining_complete_) co_await asio::post(executor)
                                     then return the TERMINAL result   ← NOT eager-ok (P1-2)
   │ (first caller)
   ▼
disable cancellation on self → draining_=true
   │
   ▼
SYNC REAP: exchange state_/next_drain_head_; for each waiter: CAS queued→cancelled,
           result=sync_lock_aborted, ++in_flight_resumers_, posted resume
           (resume runner: after release_ref → --in_flight_resumers_)
   │
   ▼
while active_holders_count_ > 0:           ← yields so a pre-drain holder unlock() runs
     co_await asio::post(executor)         ← strand-local (DRAIN PRECONDITION: holders release promptly)
     re-exchange + reap any spliced residual waiters
   │
   ▼
TERMINAL CONDITION: (active_holders_count_==0) AND (in_flight_resumers_==0)
                    AND (state_ and next_drain_head_ both observed empty in one pass)
   │
   ▼
FINALIZE: CAS state_ locked_no_waiters→not_locked; draining_complete_=true → terminal
```

No `released_`/`aborted_` terminal object; no subscriber list; no `signal_*`; no cross-thread latch. A reentrant `cancel_and_drain()` AWAITS the first drain's `draining_complete_` then returns the terminal result (NOT eager-ok). The drain MUST NOT terminate on `active_holders_count_==0` alone — `in_flight_resumers_==0` is the barrier that keeps the mutex alive until every posted resumer has dereferenced `record->mutex_` (P1-1). Per-waiter `phase_` machine `{queued,granted,cancelled}` is unchanged (D-4).

## Validation rules (from FRs)

- FR-001: every begun waiter resolved exactly once; terminal completion gated on (no holders) ∧ (no posted-but-unrun resumers, `in_flight_resumers_==0`) ∧ (both lists empty). Promptness rests on the drain precondition (holders release promptly), NOT an O(holder-count) bound.
- FR-002: no latch/channel/Dekker reachable from the terminal-state computation (the retained `in_flight_resumers_`/`active_*_count_` are plain strand-local counters, not cross-thread machinery).
- FR-003: `inherited_slot.assign` (`:862`) fails closed → `sync_lock_alloc_failed`; `reaper_slot.assign` (`:1177`) eliminated. The resume post (`:614`) + the holder-yield post remain pre-existing OOM-terminate (deferred L-048).
- FR-004: `phase_` CAS guarantees single-winner resume.
- FR-007: reaped waiters keep `sync_lock_aborted` (no observable code change).
- FR-008: C ABI frozen (abidiff-clean); C++ `sizeof(async_mutex)` changes → header-recompile + layout golden re-baselined.
- FR-009: removing `drain_latch_ptr_` drops 046's `atomic<shared_ptr>` consumer set 4→3.
