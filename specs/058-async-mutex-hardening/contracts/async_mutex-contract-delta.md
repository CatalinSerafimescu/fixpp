# Contract delta — async_mutex (Cluster-4 hardening)

No public API signature changes. This records the **documentation/contract tightening** that ships
with the fixes (FR-002/FR-003/FR-007) so Gate A reviews the intended envelope, not just the code.

## Drain / teardown contract (tighten — FR-002/FR-003)

The existing NARROWED contract (`async_mutex.hpp:186-195`) stays, with the ordering now MADE SAFE and
the intent stated explicitly:

- `cancel_and_drain()` is **strand-local by design**: it MUST be called on the owning strand,
  co-located with all acquire/cancel/unlock of this mutex.
- **New guarantee (scoped — Gate-A tightened):** the teardown establishes a happens-before (release
  decrement in the resume runner / acquire read in the drain and destructor) such that when the drain
  observes `in_flight_resumers_ == 0`, every resumer's writes into the mutex-owned pool storage are
  already visible — so destroying the mutex after a completed drain is memory-safe **even for a waiter
  that was still PARKED at drain start and got reaped** (reaped → cancelled → counted runner). This
  closes the previously-latent AM-P2-1 cross-executor write-after-free **for parked-then-reaped
  waiters**.
- **Explicit EXCLUSION (Gate-A: Fable):** a waiter parked cross-executor before the drain and then
  **GRANTED** becomes a cross-executor *holder*; if its `unlock()` overlaps the drain, that is the
  `:189` UNDEFINED case and is NOT made safe by this ordering (the holder decrements
  `active_holders_count_` relaxed at `:961` *before* its `state_` CAS, so the drain can observe 0,
  finalize, and the caller can destroy while the holder's pending CAS still targets freed memory — and
  the destructor guard cannot catch it, both counters being 0). **Requirement:** any cross-executor
  waiter that was granted MUST complete its `unlock()` before the drain begins (or unlock
  strand-locally). Callers that keep drain strand-local, co-located with all acquire/cancel/unlock (the
  documented contract), never reach this.
- **Destructor precondition (widened — enforces the parked-reaped case):** `~async_mutex`
  `std::terminate()`s — in debug AND release — if the mutex is held, has residual waiters, **or has any
  in-flight resumer** (`in_flight_resumers_ != 0` — the barrier, decremented last at `:583`). A
  correctly drained-then-destroyed mutex never trips this (the drain forces `in_flight_resumers_==0`
  before returning). This converts the silent cancel-delivered-then-destroy UAF (AM-P2-2) into the loud
  precondition failure the destructor already promises. NOTE: `active_holders_count_` is NOT a valid
  teardown barrier (it decrements early in `unlock`) and is not gated on — the granted-holder case
  above is handled by the contract, not the guard.

## OOM disposition on the resume path (settled — FR-007 / AM-P3-3, T025)

- `asio::post` in the `noexcept` resume path (the resume runner's post call, `async_mutex.hpp`'s
  `store_executor`) may throw `bad_alloc` under genuine allocator exhaustion → `std::terminate`. This is
  the primitive's **final, documented disposition**: an accepted fail-stop, consistent with the
  terminate-on-contract-violation posture used elsewhere (destructor guard, T023/T024 chain-walk and
  null-awaiter traps).
- This is a DIFFERENT case from the trapped pre-grant slot-assign `bad_alloc` (`store_executor` /
  `inherited_slot.assign` failures), which fails closed with `sync_lock_alloc_failed`. The distinguishing
  factor is **grant ordering**, not an inconsistency to be reconciled: slot-assign runs pre-commitment (no
  waiter has been granted yet, so returning an error is a legitimate outcome); the resume runs post-grant
  (the waiter is already granted or definitively cancelled; the resume MUST occur; there is no error
  channel to return through). Both dispositions are correct for the ordering they occur at — this is not
  an open asymmetry to close, it is the intended, final shape of the two sites (AM-P3-3 closed;
  research.md D-8).

## Unchanged (explicitly)

- Public `async_lock` / `unlock` / `cancel_and_drain` / guard signatures and semantics.
- Cross-thread `async_lock`/`unlock` contention stays SUPPORTED (`:190`).
- `sizeof(async_mutex) == 131120`, `alignof == 16` (layout golden).
- The `no-std-mutex` gate (Article XV §9) and `test_async_mutex_layout_golden`.
