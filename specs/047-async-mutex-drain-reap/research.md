# Research: async_mutex cancel_and_drain late-waiter reap

Phase 0 output for feature 047. Resolves the design question — *how to restructure
the drain protocol so no concurrently-acquiring, holding, or unlocking party can
orphan a waiter or be dropped from quiescence* — and proves it correct against the
C++20 memory model.

**Revision after Gate A round 1 (2026-06-21).** The dual-Codex Gate A pass +
Opus/advisor adjudication surfaced **four blockers** beyond the original two-edge
sketch. The fix is now a coordinated restructure of `cancel_and_drain()`,
`async_lock()`, AND `unlock()`. Judge record:
`../../../research/reviews/opus_047_gate_a_r1_judge.md`. User chose **full
restructure** (all four in-feature, each witnessed). This document is the revised
design that round 2 must confirm.

## Decision

Five coordinated changes, all inside `include/fixpp/core/sync/async_mutex.hpp`:

1. **Converging reap+quiesce loop** in the reaper (replaces linear (g)→(h)→finalize).
2. **seq_cst Dekker handshakes** on `draining_` ↔ each *feeder* counter
   (`active_acquirers_count_`, the new `active_unlockers_count_`).
3. **In-flight-unlocker representation** — a new `active_unlockers_count_` so the
   reaper cannot finalize while an `unlock()` that read `draining_==false` is still
   doing structural list work (B3).
4. **Notify-on-every-feeder-decrement** under a published latch (B1).
5. **Feeder-before-sink quiesce read order** + **terminal-ownership CAS at the
   second draining gate** (B2, B4).

### Counter model (the proof's vocabulary)

- **Feeder counters** — represent a party that may still *grant a holder* or *push
  a waiter onto a list*: `active_acquirers_count_` (in `async_lock`),
  `active_unlockers_count_` (NEW, brackets the whole `unlock()` body),
  `in_flight_resumptions_` (posted resume runners).
- **Sink counter** — `active_holders_count_`: a party currently holding the lock.
  Incremented only by a grant from a feeder (acquirer fast-path / unlock grant),
  always **sequenced-after** that feeder's own increment and **sequenced-before**
  that feeder's terminal decrement.
- **Lists** — `state_` (LIFO) and `next_drain_head_` (FIFO): where waiters park.

**Invariant the whole proof rests on:** every grant does `holders++` *sequenced
-before* the granting feeder's `feeder--`; every list push is *sequenced-before*
the pushing feeder's `feeder--`. So observing a feeder at 0 (with the right
acquire ordering) means every holder it transferred and every waiter it pushed is
visible.

### Exact memory-order changes

| Site | Current | New | Why |
|---|---|---|---|
| `async_lock` entry `active_acquirers_count_.fetch_add` (L776) | acq_rel | **seq_cst** | edge #2 acquirer "store" |
| `async_lock` `draining_.load` fast-path gate (L780) | acquire | **seq_cst** | edge #2 — guards the uncontended CAS that never reaches L868 |
| `async_lock` `draining_.load` pre-push gate (L868) | acquire | **seq_cst** | edge #2 — final gate of the contended push path |
| `cancel_and_drain` `draining_.store(true)` (L1110) | release | **seq_cst** | edge #2 reaper "store" |
| `cancel_and_drain` quiesce `active_acquirers_count_.load` | acquire | **seq_cst** | edge #2 reaper "load" |
| `unlock` entry `active_unlockers_count_.fetch_add` (NEW, top) | — | **seq_cst** | edge #2′ unlocker "store" (before reading `draining_`) |
| `unlock` `draining_.load` (L953) | acquire | **seq_cst** | edge #2′ unlocker "load" |
| `cancel_and_drain` quiesce `active_unlockers_count_.load` (NEW) | — | **seq_cst** | edge #2′ reaper "load" |

**Acquirer + unlocker terminal decrements → seq_cst** (Gate A round 2, R2-B1): the
notify helper that wakes the reaper reads these decrements as the Dekker "store" of a
SECOND handshake `feeder-decrement` ↔ `draining_`-load (see Liveness below). seq_cst
⊇ release, so edge #1 is unaffected. (`in_flight_resumptions_--` stays acq_rel — its
notify uses a *captured* latch, not a `drain_latch_ptr_` load, so it has no
publication race; see Liveness.)

**NOT strengthened** (release/acquire suffices): the `state_` push CAS (L904), the
`active_holders_count_` quiesce load (sink, not a Dekker participant), and the
`in_flight_resumptions_` quiesce load (self-bracketed via synchronous
`invoke_handler`) — M7.

### Converging loop (revised)

```
PUBLISH:  publish latch; draining_.store(true, seq_cst)
INITIAL_REAP: exchange both lists, reap LIFO+FIFO
CONVERGE (loop):
   drain-lists-until-empty                                  # inner (g), unchanged
   if (acquirers!=0 [sc] || unlockers!=0 [sc] || resumptions!=0 [acq]):
       co_await latch->async_wait(); if reaper_cancelled -> ABORT; continue
   if (holders!=0 [acq]):                                   # sink read AFTER feeders
       co_await latch->async_wait(); if reaper_cancelled -> ABORT; continue
   # all feeders 0 AND holders 0, with draining_ globally visible:
   confirming exchange of state_ + next_drain_head_
   if both empty: break (CONVERGED)
   else: reap; continue                                     # edge #1 caught a late push
FINALIZE: CAS state_ locked_no_waiters->not_locked; signal_release(); clear ptr
```

## Correctness proof

The fix is correct iff at FINALIZE no waiter is parked in either list and no party
can still grant/push. Required happens-before edges:

### Edge #1 — push-visibility (corrected wording; H4)

A feeder's push (e.g. acquirer L904 release-CAS on `state_`, or unlock's
`push_residual` on `next_drain_head_`) is **sequenced-before** that feeder's own
terminal decrement (`acq_rel` RMW). The push and the decrement are on **different
atomic objects** — the push is *not* the head of the counter's release sequence
(the prior draft mis-stated this). The valid chain is:

```
push (state_/next_drain_head_, release)
  —sequenced-before→  feeder-- (active_*_count_, acq_rel/release)
  —release-sequence through later counter RMWs—  (every count access is an RMW;
     verified: no plain store to active_acquirers_count_ exists in the header)
  —synchronizes-with→  reaper's acquire/seq_cst load of count==0
  —sequenced-before→  reaper's confirming list scan
```

Hence the confirming scan happens-after every push that contributed to the count
reaching 0, and sees it. The current code never performs that post-quiesce scan →
orphan; the converging loop's confirming exchange performs exactly it. ∎

### Edge #2 — termination-stability, `draining_` ↔ `active_acquirers_count_` (Dekker)

The loop terminates only when feeders are observed 0. Safe against a *new* acquirer
iff: once the reaper has observed quiescence, no acquirer can still push. The
store-buffer (SB) litmus:

```
Acquirer:  fetch_add(acquirers)   ; r1 = load(draining_)     # L776 ; L780/L868
Reaper:    store(draining_, true) ; r2 = load(acquirers)     # L1110 ; quiesce
```

acquire/release permits `r1==false && r2==0` — real even on x86 TSO (the reaper's
release-store of `draining_` can sink past its count-load). Adversarial round-1
attack #3 formalised it as the cycle `I < D < S < C < I` that **only** seq_cst on
all four ops forbids: under one total order S, either the acquirer's `draining_`
load sees `true` (fast-fails, never pushes) or the reaper's count-load sees the
increment (waits; later catches the push via edge #1). **Both `draining_` loads are
load-bearing** (M7): L780 guards the uncontended fast-path CAS that grants without
reaching L868; L868 is the final gate of the contended push path. Two litmus
instances, one per path. ∎

### Edge #2′ — same Dekker on the new `active_unlockers_count_`

`unlock()` is bracketed by `active_unlockers_count_` incremented at entry **before**
it reads `draining_` (L953). Identical SB litmus → identical seq_cst requirement on
{unlocker entry `fetch_add`, unlock `draining_.load`, reaper `draining_.store`,
reaper unlocker-load}. Then either the unlocker sees `draining_==true` and takes the
no-grant draining branch, or the reaper sees `unlockers!=0` and waits. This is what
closes **B3**: a stale-non-draining unlock can no longer privatize a chain and push
a residual after finalize, because the reaper cannot finalize while it is counted,
and its residual `push_residual` is sequenced-before its `unlocker--` (edge #1).
The reaper reads `unlockers==0` (seq_cst ⊇ acquire) **before** the confirming list
scan, so any residual tail is visible to the scan. ∎

### Edge #3 — coherent quiesce snapshot (feeder-before-sink read order; B2)

The reaper must not observe `holders==0` spuriously while a feeder→holder grant is
in flight. On the acquirer fast-path, `holders++` (L792) is sequenced-before
`acquirers--` (L793); on the unlock grant, `holders++` (L974/L1029) is sequenced
-before `unlocker--`. **Therefore the reaper reads all feeders first; only if every
feeder is 0 does it read `holders`.** Observing `acquirers==0 && unlockers==0`
(seq_cst, so synchronizing with the last decrement of each) guarantees every
transferred `holders++` is visible to the subsequent acquire-load of `holders`. So
a true `holders==0` cannot coexist with an in-flight grant. Separate atomics are
not an atomic snapshot — but the sequenced-before grant ordering plus feeder-first
read order makes the snapshot *coherent for the property we need*. ∎

The **resumption→re-entrant-acquirer** handoff needs no extra read-order rule:
`invoke_handler` (L561→L556) resumes the coroutine **synchronously** within the
resume runner, so a re-entrant `async_lock()` does its `acquirers++ … acquirers--`
(fast-failing on `draining_`) entirely **sequenced-before** the runner's L598
`in_flight_resumptions_--`. While a resumption is in flight (`resumptions!=0`) the
reaper does not terminate; by the time `resumptions==0`, that re-entrant acquirer
has already returned to 0. (Verified — not assumed.)

### Liveness — notify discipline (B1) + publication protocol (R2-B1)

The converging loop blocks on `latch->async_wait()` and is woken only by
`latch->notify()` / `signal_*`. Today only `unlock()`'s draining branch (L958), the
reap-path resumption (L598-599), and reaper own-cancel notify. The acquirer
**fast-fail decrement (L781)** and **alloc-fail decrements (L835/L850)** decrement
`active_acquirers_count_` with NO notify → a reaper parked on `acquirers!=0` misses
the `→0` edge → hang (same lost-wake class). **Fix (B1):** every acquirer/unlocker
terminal decrement, when a drain latch is published, MUST `notify()` it; route all
through one audited helper so no path can forget. The capacity-1 channel coalesces
safely because every wake re-checks all predicates (M8).

**Publication protocol (R2-B1) — the helper must reliably FIND the latch.** The
helper notifies by loading `drain_latch_ptr_`; a naïve acquire-load can read
stale-null (the reaper's publication not yet synchronized) → no notify → the same
deadlock. This is itself a store-buffer hazard: helper `W(feeder, dec); R(draining_)`
vs reaper `W(draining_, true); R(feeder)`. The litmus outcome "helper reads
`draining_==false` ∧ reaper reads `feeder` stale-nonzero" is forbidden **only if all
four ops are seq_cst** — hence the acquirer/unlocker decrement (helper "store") and
the helper's `draining_`-load are seq_cst (the reaper's `draining_`-store and
feeder-load already are). The helper protocol:

```
feeder.fetch_sub(1, seq_cst);
if (draining_.load(seq_cst)) {                 // synchronizes-with reaper's seq_cst store
    if (auto l = drain_latch_ptr_.load(acquire)) l->notify();   // guaranteed non-null
}
```

`drain_latch_ptr_` is guaranteed visible-and-non-null here because the reaper
publishes it (`store(latch, release)`, L1109) **before** `draining_.store(true,
seq_cst)` (L1110); observing `draining_==true` (seq_cst) therefore happens-after the
release-store of the pointer. The **resumption** decrement (L598) is EXEMPT from this
protocol: its runner holds the latch by *capture* (L591) and notifies unconditionally
(L599) — no `drain_latch_ptr_` load, no publication race, so its decrement stays
acq_rel and the reaper reads `resumptions` with acquire.

### Liveness contract (M8)

`cancel_and_drain()` completes under **fair scheduling + finite in-flight
activity**: every post-publication acquirer/unlocker fast-fails in bounded work and
notifies, and the reaper makes progress on each notify. It is **not** starvation
-free under an unbounded stream of fresh arrivals (that would need admission
gating); this matches the primitive's contract (drain is a shutdown operation, not
a steady-state path).

### B4 — terminal-ownership CAS at the second draining gate

The second draining branch (L868) currently does an **unconditional**
`record->phase_.store(cancelled)` + `schedule_record_resume` after the cancel
handler is installed (L860-865). A concurrent `on_cancel` that already CAS'd
`queued→cancelled` and scheduled a resume → two `invoke_handler` calls → UAF.
**Fix:** the branch must win terminal ownership via
`phase_.compare_exchange(queued→cancelled)` and only **schedule + set result** on
success; on CAS loss the `on_cancel` path already owns resumption (its CAS at
L736-742). **Common cleanup runs for BOTH outcomes** (both Gate A r2 passes): the
`active_acquirers_count_` terminal decrement (+ notify helper) and the creator-ref
release belong to the `async_lock()` invocation, not to CAS ownership — CAS loss
must NOT bypass them. So: winner → set `sync_lock_drained`, schedule once, dec+notify,
release creator ref; loser → dec+notify, release creator ref, return. (Separable —
its own commit — but fixed in-feature: it is a UAF in the path being rewritten.)

### Terminal-flag arbitration (R2-B2; was the attack-6 tail)

Cancellation firing between the L1200 `reaper_cancelled` check and the L1224
`reaper_slot.clear()` can set `aborted_` while the reaper also `signal_release()`s
and returns success → both terminal flags published; a concurrent subscriber returns
aborted while the reaper returned success. A re-check alone does NOT close it
(cancellation can fire right after the re-check), and two independent bools
(`released_`, `aborted_`) cannot be mutually arbitrated.

**Fix (concrete, single-winner):** replace `drain_latch_state`'s two bools with
**one atomic terminal state** `enum class drain_terminal { pending, released,
aborted }`. `signal_release()` and `signal_abort()` each `compare_exchange` from
`pending` to their value; **only the CAS winner** closes the channel and fixes the
outcome. The reaper's finalize calls `signal_release()`; if the CAS loses (a
concurrent cancel already set `aborted`), the reaper returns the aborted result
instead of success. Subscribers (`subscribe()` lambda) and the idempotent fast paths
(steps (a)/(b)) read the single terminal state instead of the two booleans. This
makes exactly one terminal edge authoritative for reaper and subscribers alike.
F-2 latch retention on the abort path is unaffected (the state stays `aborted`,
`drain_latch_ptr_` stays published).

## Invariant preservation (FR-004)

- **I-1..I-32 (006), incl. I-32 waiter_record reclamation** — the convergence
  invariant is named **I-33** (NOT I-32; H5). Repeated list exchanges keep
  list-membership refs; no new ABA (adversarial #5 DEFEATED: a recycled `state_`
  head denotes a currently-live record protected by its fresh membership ref).
- **I-04..I-18 (unlock invariant block)** — re-proven under the new
  `active_unlockers_count_`: the counter only *delays* finalization; it does not
  change which waiter unlock grants or the grant order. The "single structural
  walker" property is preserved because the reaper still never walks a chain an
  unlock has privatized — it instead *waits* for the unlock to finish (unlockers→0)
  and reaps the residual the unlock re-published.
- **FIFO-fair** unchanged (reaping cancels, never grants). **F-2** latch retention
  on abort unchanged. **F-3 / I-5** reaper own-cancel preserved (the per-iteration
  wait is the same wait point). **noexcept** preserved (no new throwing op).

## FR-005 (corrected wording; L9)

Serialized **functional** semantics are unchanged (same outcomes, API, suspension,
allocation on a single strand). It is **not** an unqualified "single-thread no-op":
seq_cst changes emitted instructions/latency on weakly-ordered archs, and the entry
increment + both `draining_` loads + the new unlocker increment are on every
acquire/unlock. Mitigation: confirm the fast-path stays within the Article VIII §2
±5% budget (bench or asm diff on a supported arch) at verify.

## Alternatives considered

1. Deterministic witness (barrier waiters) — REJECTED, enshrines the bug
   ([[feedback_coverage_push_enshrines_bugs]]; finding-doc DO-NOT).
2. Test-only production seam to force the window — REJECTED by clarification (FR-006).
3. B3 via "make the draining branch walk `next_drain_head_`" instead of the
   unlocker counter — **REJECTED (advisor): insufficient.** It still lets
   `cancel_and_drain` return success while the residual W2 is transiently parked; a
   later unlock reaps W2 after the caller may have destroyed the mutex → UAF
   (violates US1-AS3). The reaper-waits-on-unlock representation is mandatory.
4. seq_cst fences instead of seq_cst ops — equivalent only with a fence between
   each path's publish and its final gate (the slow path has the later L868 gate),
   harder to keep paired; PREFER named seq_cst ops, revisit only on a bench regression.

## Verification strategy (drives tasks.md) — one witness per blocker

Per [[feedback_coverage_push_enshrines_bugs]], witnessing only the original orphan
would enshrine the other three fixes. Each blocker gets its own discriminating
RED→GREEN, mutation-tested (revert that fix → its witness goes RED):

- **W-orig (B-orphan)**: the moved 046 `drain_latch_publish_acquire` witness, hardened
  — ≥4 workers, ≥32 acquirers/round, ≥100 rounds; internal self-deadline (fast
  attributable FAIL, never lane hang); **orphan-at-teardown safety** (mutex dtor
  rejects non-empty state → isolated child-process or leak-safe timeout); fresh
  mutex per round; mutation baseline ≥99/100 pinned-release RED.
- **W-B1 (notify)**: reaper parks while exactly one acquirer is between increment
  and fast-fail decrement; assert drain completes (no hang) — RED if the
  fast-fail decrement does not notify.
- **W-B2 (snapshot)**: an acquirer wins the fast-path CAS (acquirer→holder) inside
  the reaper's quiesce window; assert `cancel_and_drain` does not report success
  while the lock is held.
- **W-B3 (unlock privatization)**: a holder with a residual `W1→W2` chain in
  `next_drain_head_` unlocks concurrently with a drain; assert W2 is reaped, not
  orphaned, and drain does not finalize early.
- **W-B4 (double-schedule)**: a cancellation hits the second draining gate
  concurrently; assert the awaiter's handler is invoked exactly once.

**TSan basis = libstdc++ Tier-1** (libc++ TSan throws false `use_future`/promise
teardown races — finding 1; it cannot adjudicate this fix). Optionally drop
`use_future` from cross-thread joins (poll a done-flag) to remove teardown noise.
**Coverage**: the converging loop's new branches (confirming-empty vs late-waiter;
each feeder-wait arm) hit by a witness or waived (Article IX §1 lcov DA/BRDA).
