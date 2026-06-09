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
Load both counters from `store_` at cold open whenever a **persistent** store is configured
(gated by `store_is_persistent_`, D-10 — NOT merely `store_ != nullptr`, since a configured memory
store is non-null but must be skipped). A memory/null store ⇒ skipped ⇒ counters stay 1 ⇒
byte-identical (FR-005). No config flag.
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

### D-6 — one-shot, cold-open placement + Logon-gate-aware inbound seed (`ensure_hydrated_`)
`ensure_hydrated_()` is idempotent + one-shot (a `hydrated_` flag set on first **successful**
run — see D-9). Called at: (a) the **top of `emit_initiator_logon_()`** (`:542`), **before** the
024 `reset_on_logon` block (`:558`) — the shared emit point covering `open()` direct AND
engine-managed first-connect via `drive_reconnect`
([[feedback_initiator_logon_wire_at_shared_emit_point]]); (b) for acceptors, the
`NotConnected` inbound-Logon case **after** the `peer_sent_reset` / `reset_on_logon` header
pre-scan (`:1585-1587`) and **before** `check_inbound` (`:1596`) — the post-pre-scan placement
is mandatory because the RC-1 inbound-seed-withhold decision (C2.4) reads `peer_sent_reset`
(round-1 D-6 said "before `interpret_logon`" `:1530`, which is BEFORE the reset pre-scan and so
could not see the reset flag — corrected by RC-1 to the converged C2.5 placement). **One-shot ⇒
reconnect skips** — re-hydrating a live session regresses the
manager to the store's lower-bound value (the 025 Gate-A **New-1** corruption). Re-hydrate-on-
reconnect is 025 RefreshOnLogon's gated job, deliberately not here.

**The Logon-path seqnum gate is NOT the steady-state gate (Gate-A round-1 RC-1).** The acceptor
`NotConnected` `check_inbound` (`:1596`) and the initiator `LogonSent` `check_inbound` (`:2841`)
both **fatal on too-high** (`record_state_transition_(Disconnected)` at `:1615` / `:2856`) unless
`enable_next_expected_msg_seq_num` (027/789) is on — there is **no ResendRequest arm** on the
Logon path; only the steady-state Active path (`:2253`) enters AwaitingResend. So the naive
"seed `next_inbound` high at the top of the Logon case, the recovery path picks it up" model is
**false**: a hydrated `next_inbound=37` makes a peer reset Logon `34=1,141=Y` (no `reset_on_logon`)
return `session_seqnum_too_low` at `:1596` and the handler disconnects at `:1615` — **before** the
013-only received-141 reset arm at `:1760` can run. This kills FR-010/INV-H5.

*Corrected control flow (RC-1 single-fix, option ii — pre-scan, lower-risk):*
- **Outbound** is hydrated normally (it only feeds the reply/initiator Logon's `34=`, never a
  validation gate — no precedence hazard).
- **Inbound** seed is gated on a **header pre-scan** of the incoming Logon (acceptor) so the
  received-141 / `reset_on_logon` reset arms are honored **before** the hydrated inbound seed can
  gate `check_inbound`. The acceptor already pre-scans `peer_sent_reset = (hdr.reset_seqnum_flag ==
  "Y")` at `:1585` and runs the `reset_on_logon` durable reset at `:1587` (before `check_inbound`);
  the 013-only received-141 reset runs at `:1760` (after `check_inbound`). The fix: when the peer
  announces a reset (`peer_sent_reset` OR `reset_on_logon`), the inbound seed **must not** be
  applied ahead of `check_inbound` — i.e. hydrate the outbound counter but leave `next_inbound` at
  its construction value (1) on the reset branch so `check_inbound(1)` is in-sequence and the
  existing reset path owns the post-state; apply the inbound seed only on the **non-reset** branch.
  Equivalently, pull the received-141 reset arm forward to run before the hydrated seed gates
  `check_inbound` when the header announces a reset. Either realization makes FR-010/INV-H5 true.
- This is a **deliberate refinement of the 024 cause-dependent split**, not a byte-identity
  regression of the non-hydrated baseline: for a non-hydrated session the pre-check reset on the
  received-141 path is a *new* post-state only on the **hydrated** branch (where `next_inbound`
  would otherwise be 37→reset→1, i.e. the received-141 post-state changes 1→2 vs. the pre-024
  baseline's 1). The non-hydrated path (no `store_` / memory store) is unchanged byte-for-byte.

*Ordering with existing knobs:* outbound hydrate runs **before** `reset_on_logon` (024 reset wins
on the initiator); the inbound seed is applied **after** / conditionally on the Logon-gate reset
decision (so a 013 received-141 peer reset still wins on the acceptor). *Alternative rejected:*
hydrate in `open()` only — silently misses engine-managed sessions (the cited memory's exact trap)
and acceptors.

**Lower-bound recovery precondition (RC-1, narrows SC-004/L-029-1).** Because the Logon gate
fatals on too-high with the knob off, a knob-off restart-after-GapFill whose peer Logon carries a
seq **higher** than the hydrated stale lower bound **can fatal on the peer Logon** (`:1615` /
`:2856`) — it does **not** silently recover via ResendRequest (the Logon path has no such arm).
Lower-bound recovery without a fatal therefore requires **either** `enable_next_expected_msg_seq_num`
(789) enabled (behind-side tolerance admits the peer Logon and the proactive resend resyncs) **or**
a peer **reset** Logon (`141=Y`, which goes in-sequence to 1). The honest contract: knob-off
restart-after-GapFill is recovery-correct only via a 789/reset handshake; otherwise it is a
documented fatal-then-reconnect case (L-029-1).

### D-7 — `SeqnumManager::hydrate(next_inbound, next_outbound)` (FR-008)
New awaitable production setter mirroring `set_next_inbound`: acquire `mutex_.async_lock()`,
set both fields, return. The first production outbound setter. Awaitable for drain-consistency
with the sibling setters; at cold open it is the first strand op (no contention). The "first
strand op / no contention" claim is a happens-before assertion that must be **witnessed**, not
asserted: W8 asserts `hydrate` completes-before the first `check_inbound` on both roles (New-4).

### D-9 — `hydrated_` latches only after success; transient read failure stays retryable (RC-3)
`hydrated_` is set **only after** both store reads AND `seqnum_mgr_.hydrate()` succeed — **not**
before the awaits. A transient store-read failure disconnects (FR-006) but leaves `hydrated_ ==
false`, so the **next** reconnect (engine-managed initiator: `open()` succeeds without emitting;
first `drive_reconnect()` hits the read, fails, disconnects; reconnect #2 retries) re-attempts
hydrate from the last durable value — consistent with FR-007. Re-entrancy (a second call before
the first returns on the same strand) is handled by a strand-scoped `hydrating_` guard cleared on
failure, **not** by pre-latching `hydrated_` (which would make a transient failure sticky and
silently regress both counters to the in-memory `1` — the very path 029 exists to protect).
*Rejected:* `hydrated_ = true` before the reads (Codex P2#5: sticky failure).

### D-10 — non-persistent discriminator captured at `open()` (RC-3, memory-store byte-identity)
A configured `MemoryStore` is **non-null** and its `next_seqnum(false)` posts + locks + allocates
(`memory_store.hpp:359-378`), so `if (store_ != nullptr) read both` would add two posted reads on
a memory-store open path — breaking the FR-005/SC-003 "memory store ⇒ zero added store reads,
zero added allocation, byte-identical" promise. Fix: capture a **single `bool
store_is_persistent_`** at `Session::open()` from the new `MessageStoreFactory::yields_persistent_store()`
accessor (default `true`; `MemoryStoreFactory` → `false`) — a one-bit additive factory surface,
NOT a 5th `MessageStore` pure-virtual — and gate hydrate's reads on it: `if
(!store_is_persistent_) co_return ok;`. **The accessor lives on the FACTORY interface, not on the
`MessageStore` 4-pure-virtual interface, so Article XIV.2's ≤5 cap on `MessageStore` is untouched
(cap stays 4).** It is a non-pure `virtual bool yields_persistent_store() const noexcept`
defaulting to `true` (persistent-by-default is the safe default: a custom store hydrates unless it
opts out — a missed override resumes, never silently restarts-at-1); `MemoryStoreFactory` overrides
it to `false`, `FileStoreFactory` inherits the `true` default. The flush-hook tag (`flush_hook()`)
is **not** reused as the discriminator: its semantics are "does this store flush on *graceful
close*", orthogonal to durability — a custom persistent store with no flush hook would be wrongly
discriminated non-persistent → hydrate skipped → restart-starts-at-1 (the exact bug 029 fixes).
This restores the memory-store byte-identity promise the spec actually wants **without** a 5th
`MessageStore` pure-virtual — one additive factory accessor, automatic (no operator burden). The
`store_ == nullptr` case is subsumed (a null store has no factory → flag stays `false`).
*Rejected:* narrowing FR-005/SC-003 to `store_ == nullptr` only (would leave the common
in-memory-store sessions paying two reads, contradicting the spec's "memory store" wording); a 5th
`MessageStore` pure-virtual `is_persistent()` (burns the `MessageStore` cap headroom for a one-bit
fact the factory already settles); the flush-hook tag (a correctness trap, see above).

**Capture point (New-A):** `store_is_persistent_` is written at `open()` **inside the
`if (cfg_.store_factory)` branch** (`session.cpp:779`, right after `make()` succeeds) —
`store_is_persistent_ = cfg_.store_factory->yields_persistent_store();` — and otherwise stays
`false` (the null-store path has no factory). It is set **exactly once, before the first counter
touch**, consistently on BOTH the direct-`open()` and engine-managed-reconnect paths (`open()` runs
once per session before either emit/accept site — the same shared-point hazard the initiator-Logon
wiring tripped, [[feedback_initiator_logon_wire_at_shared_emit_point]]).

### New-3 — hydrate feeds the 027/789 advertisement (RC-4)
With `ensure_hydrated_()` at the top of `emit_initiator_logon_()` (before the reset block at
`:558`), the hydrated counters feed two `:596-606` predicates: (a) `seqnums_at_one` (`:596`)
samples `peek_outbound()`/`next_inbound_unsafe()` — a hydrate to `{42,37}` makes it **false**, so
the `any_reset_knob && seqnums_at_one` arm does **NOT** spuriously emit `141=Y` on a resumed
session (correct — `reset_on_logon` at `:558` runs after hydrate and still resets to `{1,1}` →
141 emitted on the reset path); (b) `initr_next_expected` (789, `:603-606`) — when
`enable_next_expected_msg_seq_num` is on the initiator now advertises `789 = next_inbound_unsafe()
== <hydrated>` (the true resumed position) instead of `1`. This is behaviorally correct and is the
one place hydrate's resumed value changes a wire field's content; it is also load-bearing for the
only non-fatal knob-off-less recovery path (the peer tolerates our advertised resume). The
acceptor reply 789 (`acpt_next_expected`) is fed the same way. A witness asserts a hydrated
initiator advertises `789 = <hydrated next_inbound>` when the knob is on (W11).

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

## Gate A round-1 resolutions (2026-06-09)

Both open items above stand as designed (lower-bound accepted; asymmetry accepted). The round-1
review surfaced four root-cause corrections, applied here without a new `/clarify` (the 3
clarifications D-1/D-2/D-3 stand — these are correct *application* of them against the real
`session.cpp` control flow):

- **RC-1** — Logon-path gate is not the steady-state gate: corrected acceptor-141 precedence
  (pre-scan-gated inbound seed, D-6) + narrowed the lower-bound recovery contract (knob-off
  restart-after-GapFill can fatal on the Logon; recovery needs 789-or-reset).
- **RC-2** — replaced "one persist call" with the site-keyed persist disposition matrix
  (`persist_inbound_advance_()`, data-model.md §Persist disposition matrix; resend-fill app
  messages PERSIST, GapFill jump does NOT).
- **RC-3** — `hydrated_` latches only after success (D-9); non-persistent discriminator
  `store_is_persistent_` captured at `open()` (D-10) restores memory-store byte-identity.
- **RC-4** — doc-accuracy sweep (L-024-1 not L-025-1; coverage-index S-042; Normative References;
  spec:89 softened to last-successful-outbound-write; checklist unchecked).
