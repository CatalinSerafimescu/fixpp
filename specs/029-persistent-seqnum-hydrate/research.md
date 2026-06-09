# Phase 0 Research: Persistent seqnum hydrate

All anchors source-verified against the working tree at branch `029-persistent-seqnum-hydrate`
(post-028). Reference engines: `reference-engines/quickfix-cpp` (v1.16.0),
`reference-engines/quickfixj` (3.0.1).

## Problem statement (verified)

- The running session persists **only** the outbound counter: the single `store_->store(...)`
  call is `session.cpp:3782`, `direction_t::outbound`. `grep` finds **zero** inbound `store()`
  and **zero** `next_seqnum(inbound, true)` calls in `src/`. The store's `next_inbound` is never
  advanced by a live session (`session.cpp:1518` admits "T034 wires the store call; here the
  seqnum check is the gate" — never wired).
- `SeqnumManager` (`seqnum_manager.hpp`) starts both counters at `seqnum_min` (1) and exposes
  `set_next_inbound` + `reset_to_one` but **no production outbound setter** (only the
  `FIXPP_TEST_HOOKS set_counters_for_test`). So hydrate has nothing to call today.
- `MessageStore` already carries the direction: `store(seq, frame, dir)` and
  `next_seqnum(dir, increment)` both take `direction_t`. `FileStore` persists **both** counters
  in one record (`file_store.cpp:480 write_counter(ni, no)`); `store(...,inbound)` would advance
  `next_inbound` (`:836`) and `next_seqnum(inbound, true)` advances + persists the counter
  (`:1004-1018`) **without** writing a frame body. `FileStore` recovery already reads both
  counters back at construction (`:700-722`).

## Reference sweep (QFcpp / QFJ)

- **Construction-time hydrate (OD-1):** QFcpp `Session` ctor creates the store
  (`Session.cpp:80`); `FileStore::populateCache()` reads the persisted `sender:target` seqnum
  pair from disk into the cache (`FileStore.cpp:198-209`). The session reads *through* to the
  store (`SessionState::getNextTargetMsgSeqNum` → `m_pStore->getNextTargetMsgSeqNum()`), so the
  store IS the source of truth — hydrate is implicitly **always-on**, no enable flag. `refresh()`
  is re-invoked only for the separate `RefreshOnLogon` feature (`Session.cpp:183,677`).
- **Persist-vs-deliver ordering (OD-NEW):** QFcpp delivers **then** persists. `verify()`
  (`:976`) calls `fromCallback` → `fromApp`/`fromAdmin` (`:1032,1067-69`); the target counter is
  advanced **after**, in each handler / the `next()` dispatcher (`incrNextTargetMsgSeqNum` at
  `:265,299,1249,1300`). At-least-once: crash mid-delivery re-delivers on restart.
- **Inbound persist failure (OD-2):** `setNextTargetMsgSeqNum` / `incrNextTargetMsgSeqNum`
  throw `IOException` out of the inbound path → the session disconnects. ⇒ fatal.
- **SequenceReset jump:** QFcpp persists a jump with `setNextTargetMsgSeqNum(MsgSeqNum(newSeqNo))`
  (`:357`) — an **absolute set**. fixpp's `MessageStore` has no absolute set (see D-5).

## Decisions

### D-1 — hydrate trigger: ALWAYS-ON when a persistent store is configured (clarify)
Load both counters from `store_` at cold open whenever `store_ != nullptr`. A memory/null
store yields 1 ⇒ `hydrate(1,1)` no-op ⇒ byte-identical (FR-005). No config flag.
*Rationale:* QF-faithful (store is source of truth). *Alternatives rejected:* a gating flag
(diverges from QF, leaves the latent "restart starts at 1" bug on by default).

### D-2 — crash ordering: DELIVER-THEN-PERSIST / at-least-once (clarify)
Advance the **durable** inbound counter **after** the in-seq `fromApp`/`fromAdmin` callback
returns. The **in-memory** `check_inbound` advance stays **before** delivery (the gate —
unchanged). *Rationale:* matches QFcpp/QFJ; FIX favors duplicate-over-loss (PossDup exists).
*Alternative rejected:* persist-then-deliver (the unwired I-3 comment) risks silent message loss.

### D-3 — inbound persist failure: FATAL-DISCONNECT (clarify)
`next_seqnum(inbound,true)` failure → `record_state_transition_(Disconnected)` + return error,
reusing the existing store-failure disposition (no new error slot). *Rationale:* QFcpp throws
`IOException`; reconnect re-hydrates the last durable value + 013 resyncs; avoids New-2 desync.
*Asymmetry:* the existing **outbound** store write stays I-07 logged-then-proceed (008/024,
out of scope) → residual L-029-2. Flagged for Gate A.

### D-4 — persist mechanism: `next_seqnum(inbound, increment=true)` (counter-only)
Persist the inbound advance via `next_seqnum(inbound, true)` — advances + durably writes the
counter, **no frame body**. *Rationale:* fixpp never resends inbound (only outbound frames are
retained for ResendRequest), so storing inbound frame bodies (the literal I-3 "store(inbound)")
is wasteful; counter-only matches QFcpp `incrNextTargetMsgSeqNum`. *Alternative rejected:*
`store(seq, frame, inbound)` — heavier (frame body) and equally unable to jump.

### D-5 — lower-bound invariant; GapFill jumps NOT persisted (INV-H1)
`MessageStore` has 4 pure-virtuals and **no absolute counter set**; a `+1` mechanism cannot
mirror a `SequenceReset`-GapFill **jump**. Verified: `apply_inbound_sequence_reset` calls
`seqnum_mgr_.set_next_inbound(new_seqno)` but **never** touches `store_`. We accept this: do
**not** persist GapFill jumps. **INV-H1**: `persisted_next_inbound ≤ manager.next_inbound`
always — a monotonic lower bound, never ahead ⇒ restart never skips an inbound; any residual
gap is reconciled by the existing 013 ResendRequest on the post-restart Logon (at-least-once).
*Limitation L-029-1:* a restart that follows a GapFill triggers a bounded redundant ResendRequest
(recovery-correct). *Alternatives rejected (flagged for Gate A):* (a) a 5th `MessageStore`
pure-virtual `set_seqnum(dir,n)` — burns the ≤5 cap headroom for exact lockstep; (b) a bounded
catch-up loop of `next_seqnum(inbound,true)` after each jump — O(gap), unbounded for large
`NewSeqNo`. KISS + cap-preservation favors the lower bound.

### D-6 — one-shot, cold-open placement (`ensure_hydrated_`)
`ensure_hydrated_()` is idempotent + one-shot (a `hydrated_` flag set on first run). Called at:
(a) the **top of `emit_initiator_logon_()`** (`:542`), **before** the 024 `reset_on_logon`
block (`:558`) — the shared emit point covering `open()` direct AND engine-managed first-connect
via `drive_reconnect` ([[feedback_initiator_logon_wire_at_shared_emit_point]]); (b) the **top of
the `NotConnected` inbound-Logon case** (`:1524`) for acceptors, before `interpret_logon` /
`check_inbound` / reply `peek_outbound`. **One-shot ⇒ reconnect skips** — re-hydrating a live
session regresses the manager to the store's lower-bound value (the 025 Gate-A **New-1**
corruption). Re-hydrate-on-reconnect is 025 RefreshOnLogon's gated job, deliberately not here.
*Ordering with existing knobs:* hydrate runs **before** `reset_on_logon` (024 reset wins on the
initiator) and **before** `check_inbound` (so a 013 received-141 peer reset still wins on the
acceptor, applied after). *Alternative rejected:* hydrate in `open()` only — silently misses
engine-managed sessions (the cited memory's exact trap) and acceptors.

### D-7 — `SeqnumManager::hydrate(next_inbound, next_outbound)` (FR-008)
New awaitable production setter mirroring `set_next_inbound`: acquire `mutex_.async_lock()`,
set both fields, return. The first production outbound setter. Awaitable for drain-consistency
with the sibling setters; at cold open it is the first strand op (no contention).

### D-8 — I-3 comment reconciliation
The unwired `session.cpp:1517` comment ("Inbound ordering (I-3 / [2e §7.6]): store(inbound)
BEFORE fromAdmin/fromApp. T034 wires the store call …") and any `[2e §7.6]`-derived prose are
corrected to **deliver-then-persist** so shipped code and comments agree (a
[[feedback_verify_caught_design_pivot_stale_doc_bundle_drift]] sweep — grep `2e §7.6` / `I-3` /
`store(inbound)` / `store-before-deliver` across `src/`, `spec/`, `specs/`).

## Open items for Gate A (explicitly surfaced)

1. **INV-H1 lower-bound vs 5th pure-virtual `set_seqnum`** (D-5) — is the bounded-redundant-resend
   limitation (L-029-1) acceptable, or is exact lockstep worth a 5th `MessageStore` pure-virtual?
2. **Inbound-fatal / outbound-logged asymmetry** (D-3 / L-029-2) — accept the residual, or pull
   outbound→fatal into scope (re-opens 008/024 I-07)?
