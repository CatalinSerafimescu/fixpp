# Data Model: async_mutex cancel_and_drain late-waiter reap

Phase 1 output. This feature adds **no new types, fields, or storage** — it is a
control-flow + memory-order fix. This document records the *state* the reaper
reasons about and the new convergence invariant, so the proof in
[research.md](./research.md) is anchored to concrete data.

## Entities (existing — unchanged shape)

- **`waiter_record`** (`detail::waiter_record`): an in-flight `async_lock()`
  attempt. Reaches exactly one terminal `waiter_phase`: `granted` or `cancelled`.
  Reference-counted (creator + list-membership + attached-awaiter + resumer refs).
  No field change.
- **`drain_latch_state`** (`detail::drain_latch_state`): the cross-thread
  publish/acquire latch; carries `released_`, `aborted_`, and
  `in_flight_resumptions_`. Published via `drain_latch_ptr_` (atomic shared ptr).
  No field change.
- **Mutex counters** (members of `async_mutex`):
  - `active_holders_count_` — threads currently holding the lock.
  - `active_acquirers_count_` — threads inside `async_lock()` between entry
    increment (L776) and their terminal decrement. **This is the Dekker "store"
    side** that the reaper must observe.
  - `latch->in_flight_resumptions_` — posted resume handlers not yet run.
  - `draining_` (atomic bool) — set once by the reaper; **Dekker "store" of the
    handshake**.

## Reaper convergence state machine (the only logic change)

States the reaper passes through per `cancel_and_drain()` invocation:

```
PUBLISH      -> set draining_ (seq_cst), publish latch                 [steps (a)-(c)]
INITIAL_REAP -> exchange both lists, reap LIFO+FIFO                    [steps (e)-(g)]
                |
                v
CONVERGE  (loop):
   drain-lists-until-empty           # inner (g) re-walk
   load counts (seq_cst)
   ├── all-zero ──> CONFIRM_SCAN: exchange both lists once more
   │                 ├── empty  ──> CONVERGED  (exit loop)
   │                 └── nonempty ─> reap, re-enter CONVERGE   (edge #1 caught a late waiter)
   └── nonzero  ──> co_await latch->async_wait()
                     ├── reaper_cancelled ──> ABORT (I-5, unchanged)
                     └── else ──> re-enter CONVERGE
                
FINALIZE     -> CAS state_ locked_no_waiters->not_locked; signal_release(); clear ptr
```

### Termination / convergence invariant (NEW — to append to 006 design doc as I-32)

> **I-32 (drain convergence).** `cancel_and_drain()` transitions to FINALIZE only
> from a CONVERGE iteration in which, with `draining_ == true` already globally
> visible (seq_cst store at PUBLISH), the reaper observed — within the same pass —
> **both** waiter lists (`state_`, `next_drain_head_`) empty **and**
> `active_holders_count_ == active_acquirers_count_ == in_flight_resumptions_ == 0`,
> the count loads being seq_cst. By the seq_cst total order over
> {entry `fetch_add`, both `draining_` fast-fail loads, `draining_.store(true)`,
> quiesce count loads}, any acquirer not yet terminated at that observation either
> (a) has already observed `draining_ == true` and will fast-fail without pushing,
> or (b) is counted in `active_acquirers_count_`, contradicting the count==0
> observation. Hence no un-reaped `waiter_record` can exist in either list at
> FINALIZE, and the finalize CAS `locked_no_waiters -> not_locked` cannot fail on
> a live waiter.

This invariant supersedes the implicit assumption behind the old linear
"(g) then (h)" ordering, which permitted a push between the last (g) pass and the
(h) quiesce. I-1..I-31 and the F-2/F-3 fixes are unchanged.

## State transitions of a racing acquirer (unchanged outcomes, now provably reaped)

| Acquirer timing relative to drain | Outcome | Reaped by |
|---|---|---|
| Pushed before INITIAL_REAP | `cancelled` (aborted) | INITIAL_REAP / inner (g) |
| Pushed during CONVERGE, still counted | `cancelled` | edge #1 confirming scan |
| Saw `draining_==true` at L780/L868 | `sync_lock_drained`, never pushed | fast-fail (self) |
| Won fast-path CAS before drain | `granted` then released | holder quiesce (count→0) |

All four reach exactly one terminal state (FR-001/FR-003); none is left suspended.
