# Data Model: async_mutex cancel_and_drain late-waiter reap

Phase 1 output. **Revised after Gate A round 1.** The fix adds **one new atomic
counter** (`active_unlockers_count_`); no other new type/field/storage. This
document records the counter model, the new convergence invariant **I-33**, the
**amendment to I-32's soundness note**, and the revised reaper state machine.

## Entities

### Existing (unchanged shape)
- **`waiter_record`** — an in-flight `async_lock()` attempt; reaches exactly one
  terminal `waiter_phase` (`granted` / `cancelled`); refcounted (I-32).
- **`drain_latch_state`** — cross-thread latch (`released_`, `aborted_`,
  `in_flight_resumptions_`), published via `drain_latch_ptr_`.
- **`active_holders_count_`** — the **sink** counter: parties currently holding the
  lock.
- **`active_acquirers_count_`** — **feeder**: parties inside `async_lock()` between
  entry increment (L776) and terminal decrement.
- **`in_flight_resumptions_`** — **feeder**: posted resume runners not yet complete.
- **`draining_`** — set once by the reaper.

### NEW
- **`active_unlockers_count_`** : `std::atomic<std::uint32_t>{0}` — **feeder**:
  parties inside `unlock()` between an entry increment and a terminal decrement.
  Brackets the whole `unlock()` body so the reaper can represent an in-flight unlock
  in quiescence (closes B3). **Implementation-critical placement (both Gate A r2
  passes, non-optional):**
  - increment at the very top, **before** `holders--` (L951) AND before the seq_cst
    `draining_` read (L953) — else the party is momentarily in neither counter, or
    the edge-#2′ Dekker breaks;
  - decrement is **seq_cst** (R2-B1 notify Dekker), at **every** `unlock()` return,
    **after** any `push_residual` (L978/L1033) / grant;
  - the recursive `unlock()` tail-calls (L1004, L1055) are NOT returns — the outer
    frame stays counted across them. Use an **RAII entry/exit guard** over the whole
    body; NEVER `unlockers--; unlock(); return` (exposes a transient 0 mid-walk →
    reopens B3). Recursion yields count 1→2→1→0, never a premature 0.
- **`drain_latch_state::terminal_`** : `std::atomic<drain_terminal>{pending}`
  (`enum class drain_terminal { pending, released, aborted }`) — **replaces** the two
  bools `released_`/`aborted_` (R2-B2). `signal_release`/`signal_abort` CAS from
  `pending`; only the winner closes the channel and fixes the outcome. Subscribers +
  the idempotent fast paths read this single state.

## Counter model

- **Feeders** = {`active_acquirers_count_`, `active_unlockers_count_`,
  `in_flight_resumptions_`} — a party that may still *grant a holder* or *push a
  waiter onto a list*.
- **Sink** = `active_holders_count_` — incremented only by a grant from a feeder,
  **sequenced-after** that feeder's increment and **sequenced-before** that feeder's
  terminal decrement.
- **Read-order rule (Edge #3 / B2):** the reaper reads **all feeders first**; only
  if every feeder == 0 does it read the sink (`holders`). Feeder loads that
  participate in a Dekker handshake (`acquirers`, `unlockers`) are **seq_cst**;
  `resumptions` and `holders` are acquire.

## Reaper convergence state machine

```
PUBLISH      -> publish latch; draining_.store(true, seq_cst)
INITIAL_REAP -> exchange both lists; reap LIFO+FIFO
CONVERGE (loop):
   drain-lists-until-empty                       # inner (g)
   load acquirers[sc], unlockers[sc], resumptions[acq]
   ├── any feeder != 0 ──> co_await latch->async_wait()
   │                        ├── reaper_cancelled ──> ABORT (I-5)
   │                        └── else ──> CONVERGE
   └── all feeders == 0 ──> load holders[acq]
        ├── holders != 0 ──> co_await latch->async_wait() ──> (cancel?ABORT : CONVERGE)
        └── holders == 0 ──> CONFIRM_SCAN: exchange both lists
             ├── empty   ──> CONVERGED
             └── nonempty ─> reap; CONVERGE        # edge #1 caught a late push
FINALIZE     -> CAS state_ locked_no_waiters->not_locked
                (terminal-flag check: publish release via released_-vs-aborted_
                 resolution so a late cancel cannot double-publish — see research B4-tail);
                signal_release(); clear drain_latch_ptr_
```

Every feeder terminal decrement, when a latch is published, calls `latch->notify()`
(B1) — routed through one audited helper so no path forgets.

## Invariants

### NEW — I-33 (drain convergence)

> **I-33 (drain convergence).** `cancel_and_drain()` transitions to FINALIZE only
> from a CONVERGE iteration in which, with `draining_ == true` already globally
> visible (seq_cst store at PUBLISH), the reaper observed — within one pass and in
> feeder-before-sink order — **all feeders zero** (`active_acquirers_count_`,
> `active_unlockers_count_`, `in_flight_resumptions_`, the first two via seq_cst
> loads), **then** `active_holders_count_ == 0`, **then** both lists (`state_`,
> `next_drain_head_`) empty on a confirming exchange. By the seq_cst total order
> over the `draining_` ↔ feeder Dekker ops, any party not yet terminated at that
> observation either has already observed `draining_ == true` (so it fast-fails /
> takes the no-grant draining branch and pushes nothing) or is still counted in a
> feeder (contradicting the zero observation). Every grant's `holders++` is
> sequenced-before its feeder's decrement (so feeders==0 ⇒ holders snapshot is
> coherent), and every push is sequenced-before its feeder's decrement (so
> feeders==0 ⇒ the confirming scan sees it). Hence no un-reaped `waiter_record`
> exists in either list at FINALIZE and the finalize CAS cannot fail on a live
> waiter.

### AMENDED — I-32 soundness note (waiter_record reclamation)

006 data-model.md:124 grounds I-32 reclamation on the **single structural walker**
precondition, stated as *"unlock() and the reaper are mutually exclusive — unlock()
short-circuits when `draining_ == true`."* **That statement is insufficient:** an
`unlock()` that read `draining_ == false` *before* the reaper's publication does
**not** short-circuit and is a competing walker (this is B3). The amended soundness
argument under feature 047:

> Single-walker reclamation remains sound because (a) a non-draining `unlock()`
> **detaches** its chain (`next_drain_head_.exchange`) before walking — it and the
> reaper traverse **disjoint** nodes; and (b) the reaper **waits** for
> `active_unlockers_count_ == 0` before its confirming scan and FINALIZE, so it
> never reclaims/finalizes while an in-flight unlock still holds or re-publishes a
> node. The unlock's residual re-push (`push_residual`) is sequenced-before its
> `active_unlockers_count_--`; the reaper observes `unlockers == 0` (seq_cst) before
> the confirming scan, so the residual is reaped, not orphaned. The Dekker handshake
> `active_unlockers_count_` ↔ `draining_` (seq_cst) guarantees the
> mutual-exclusion-at-finalization the old short-circuit claim assumed but did not
> establish.

I-1..I-31 unchanged. I-04..I-18 (unlock block) preserved: the unlocker counter only
delays finalization; it changes neither which waiter `unlock()` grants nor the grant
order.

## State transitions of a racing party (outcomes; all reach exactly one terminal state)

| Party / timing vs drain | Outcome | Reaped / settled by |
|---|---|---|
| Acquirer pushed before INITIAL_REAP | `cancelled` | INITIAL_REAP / inner (g) |
| Acquirer pushed during CONVERGE, still counted | `cancelled` | edge #1 confirming scan (after acquirers==0) |
| Acquirer saw `draining_==true` (L780/L868) | `sync_lock_drained`, never pushed | fast-fail (self), notifies reaper |
| Acquirer won fast-path CAS before drain | `granted` then released | sink quiesce (holders→0), B2 read order |
| **Unlock that read `draining_==false`, has residual chain** | residual re-pushed, then reaped | reaper waits unlockers→0, then confirming scan (B3) |
| Unlock that read `draining_==true` | no-grant draining branch; notifies | self |
| Cancellation at second draining gate | handler invoked **exactly once** | `queued→cancelled` CAS ownership (B4) |
| Reaper own-cancel mid-loop | abort return; subscribers woken | F-3 / I-5 (unchanged) |
