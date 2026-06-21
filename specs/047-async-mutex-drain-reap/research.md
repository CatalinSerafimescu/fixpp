# Research: async_mutex cancel_and_drain late-waiter reap

Phase 0 output for feature 047. Resolves the single design question — *how to
restructure the reaper so no concurrently-acquiring waiter is orphaned* — and
proves the fix correct against the C++ memory model. No `NEEDS CLARIFICATION`
remained from the spec; this document records the design decision and its proof.

## Decision

Replace the reaper's linear **(g) re-walk → (h) quiesce → (i)/(j) finalize** with a
**single converging reap+quiesce loop**, and **strengthen the
`draining_`↔`active_acquirers_count_` handshake to seq_cst**. Both are confined to
`include/fixpp/core/sync/async_mutex.hpp`.

### Part 1 — converging reap+quiesce loop (reaper body)

Pseudocode (replaces current L1151-1232):

```
loop:
    # drain both lists fully (current (g), unchanged)
    repeat:
        lifo = state_.exchange(locked_no_waiters, acq_rel)
        fifo = next_drain_head_.exchange(nullptr, acq_rel)
        if lifo == empty and fifo == empty: break
        reap_chain(reverse_lifo(lifo)); reap_chain(fifo)

    # quiescence check — counts loaded seq_cst (see Part 2)
    if holders == 0 and acquirers == 0 and in_flight_resumptions == 0:
        # CONFIRMING scan: a late-but-counted waiter that pushed before its
        # decrement is now visible (edge #1). One more exchange:
        lifo2 = state_.exchange(locked_no_waiters, acq_rel)
        fifo2 = next_drain_head_.exchange(nullptr, acq_rel)
        if lifo2 == empty and fifo2 == empty:
            break            # converged: lists empty AND quiesced in one pass
        reap_chain(...); continue   # caught a late waiter; re-iterate
    else:
        co_await latch->async_wait()   # wait for a counter edge, then re-loop
        if reaper_cancelled: -> abort path (unchanged I-5 handling)

finalize: CAS state_ locked_no_waiters -> not_locked; signal_release(); clear ptr
```

The reaper's own-cancellation handling ((h) cancel slot, abort path, F-2 latch
retention) is preserved verbatim — only the loop *shape* around the quiesce
changes.

### Part 2 — seq_cst Dekker handshake (acquirer + reaper)

Strengthen exactly these ops from their current orders to `seq_cst`:

| Site | Current | New | Role |
|---|---|---|---|
| `async_lock` entry `active_acquirers_count_.fetch_add` (L776) | acq_rel | **seq_cst** | acquirer "store" of the Dekker pair |
| `async_lock` first `draining_.load` (L780) | acquire | **seq_cst** | acquirer "load" |
| `async_lock` second `draining_.load` (L868) | acquire | **seq_cst** | acquirer "load" (post-record, pre-push) |
| `cancel_and_drain` `draining_.store(true)` (L1110) | release | **seq_cst** | reaper "store" |
| `cancel_and_drain` quiesce count loads (L1189-1191) | acquire | **seq_cst** | reaper "load" of the Dekker pair |

All count **decrements** (L781/793/835/850/869/887/907) stay `acq_rel` — they are
already ≥release, which is all edge #1 needs.

## Rationale — correctness proof

The fix is correct iff **no waiter that begins an `async_lock()` acquisition is
left parked in `state_`/`next_drain_head_` when the reaper finalizes.** Two
independent happens-before edges are required; the current code has only the
first available but never uses it, and lacks the second entirely.

### Edge #1 — push-visibility (already satisfiable; the converging loop *uses* it)

A waiter that pushes onto `state_` does so at L904 (release-CAS), **sequenced
-before** its `active_acquirers_count_` decrement at L907 (`acq_rel`). Every count
decrement is ≥release, so they form a release sequence headed by each waiter's
push. When the reaper's quiesce-condition load observes `active_acquirers_count_
== 0` (seq_cst ⊇ acquire), it *synchronizes-with* the final decrement and, through
the release sequence, **happens-after every push that contributed to the count
reaching 0**. Therefore a list-scan performed *after* observing count==0 is
guaranteed to see any such push. The current code never performs that
post-quiesce scan → orphan. Part 1's confirming scan performs exactly it. ∎

### Edge #2 — termination-stability (needs seq_cst; the spec's missing piece)

The converging loop terminates when, in one pass, lists are empty AND counts are
0. This is unsafe against a **new** acquirer that increments the count *after* the
reaper's count-load but pushes *after* the reaper's confirming scan — unless we
guarantee: **once the reaper has observed quiescence, no acquirer can still push.**

Consider the Dekker/store-buffer (SB) litmus on the two threads:

```
Acquirer:  fetch_add(acquirers)   ; r1 = load(draining_)      # L776 ; L780/L868
Reaper:    store(draining_, true) ; r2 = load(acquirers)      # L1110 ; L1189
```

With acquire/release only, the SB outcome `r1 == false (not draining) && r2 == 0
(increment unseen)` is **permitted** — the acquirer's increment sits in its store
buffer w.r.t. the reaper's load, and the reaper's `draining_` store sits in its
buffer w.r.t. the acquirer's load. This holds **even on x86 TSO**: x86 forbids
load-load / store-store / load-store reordering but *permits store→load* reorder,
and the reaper's `release` store of `draining_` is a plain `mov` that can sink past
its `acquirers` load. So an acquirer could read `draining_ == false`, pass both
fast-fail checks, and push — after the reaper terminated. Orphan.

Making **all four** ops seq_cst imposes a single total order S over them, which
forbids the SB outcome: in S, either `store(draining_)` precedes `load(draining_)`
(acquirer sees `true` → fast-fails at L780 or L868, never pushes) **or**
`fetch_add(acquirers)` precedes `load(acquirers)` (reaper sees count ≥ 1 → does not
terminate; it waits, the acquirer pushes-then-decrements, and the reaper re-scans
via edge #1). Both branches are safe; the unsafe interleaving is eliminated. ∎

The acquirer's *second* `draining_` load (L868) must also be seq_cst: it is the
last gate before the push loop, so it is the one that must observe `draining_ ==
true` in the branch where the reaper "wins" the total order. (L780 short-circuits
the common case; L868 is the one load-bearing for the proof.)

### Why both edges, not one

- Edge #1 alone (converging loop, acquire/release handshake) closes the *witnessed*
  orphan (a waiter already counted at drain start) but leaves edge #2's new-acquirer
  race — a rarer but real lost wake the fix must not reintroduce under a different
  timing. `/analyze` and Gate A should confirm edge #2 is addressed, not just #1.
- Edge #2 alone (seq_cst handshake, no converging loop) still orphans the *original*
  witnessed waiter, because the reaper never re-scans after quiescing.

## Invariant preservation (FR-004)

- **FIFO-fair grant order**: untouched — the converging loop reaps in the same
  reverse-LIFO-then-FIFO order as today; grant order is set by `unlock()`, not the
  drain.
- **Uncontended fast path** (L789 CAS): untouched. No new suspension, no new alloc.
- **Idempotent / concurrent-caller drain** (steps (a)/(b)): untouched.
- **Reentrant-drain UAF closure (F-2)**: latch retention on the abort path is
  preserved verbatim; the loop change does not touch `drain_latch_ptr_` lifecycle.
- **Reaper own-cancellation (I-5, F-3)**: the cancel slot + abort return are kept;
  the `co_await latch->async_wait()` inside the loop is the same wait point, now
  re-entered per iteration instead of once.
- **`noexcept`**: no new throwing op; the existing try/catch around the wait covers
  the only co_await. Memory-order strengthening cannot throw.
- **FR-005 serialized no-op**: seq_cst vs acquire/release is unobservable on a
  single strand (one thread → no reordering); the converging loop on one strand
  executes the same statements as today (after the acquirer's synchronous
  `count++ → push` it has already returned, so lists are empty and counts 0 on the
  reaper's first pass — identical to the current single-pass behavior).

## Alternatives considered

1. **Make the witness deterministic (barrier all waiters before drain) to get
   green.** REJECTED — enshrines the bug ([[feedback_coverage_push_enshrines_bugs]],
   and the finding doc's explicit DO-NOT). The witness must keep catching this.
2. **Add a test-only scheduling seam to the production primitive to force the
   late-park window.** REJECTED by clarification (FR-006): no production seam;
   prove RED via stress + self-deadline + mutation-revert instead.
3. **Edge #2 via `atomic_thread_fence(seq_cst)` instead of seq_cst ops.** Viable
   and equivalent, but a single seq_cst fence on each side is harder to read and
   to keep paired across future edits than naming the four ops seq_cst directly.
   PREFER explicit seq_cst on the four ops; revisit only if a benchmark shows the
   acquirer fast path regressed (Article VIII §2 ±5%).
4. **Spin the reaper on counts without re-scanning (status-quo + busy-poll).**
   REJECTED — does not address either edge; the orphan is a *list* membership, not
   a count.

## Verification strategy (informs tasks.md)

- **RED proof**: documented pre-fix repro (libstdc++ release, `taskset -c 0,1`,
  ~1/3 runs — finding doc) + mutation-revert (revert Part 1 → witness RED via its
  internal self-deadline, not a lane hang).
- **TSan basis**: validate under **libstdc++ TSan (Tier 1)** — libc++ TSan throws
  false `std::promise` teardown races on this exact `use_future` join (finding 1);
  it cannot adjudicate this fix. Optionally also run the witness without
  `use_future` (poll a done-flag) to remove the teardown noise entirely.
- **Coverage**: the converging loop's new branches (confirming-scan-empty vs
  caught-late-waiter; quiesced vs wait-again) must be hit by the witness or carry a
  recorded waiver (Article IX §1, lcov DA/BRDA basis).
- **No-regression**: full existing 006 `test_cancel_and_drain.cpp` + the whole
  sync suite green across debug/release/ASan/UBSan/TSan/gcc, one preset at a time
  (WSL2 -j2 cap).
