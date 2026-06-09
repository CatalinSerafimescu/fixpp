# Research: RefreshOnLogon — per-logon re-hydrate (025)

Phase 0 decision record. All `session.cpp` anchors verified against the merged tree
(submodule main `0b9c8b8`, post-029).

## Reference-engine sweep (the parity authority)

| Engine | RefreshOnLogon? | Reload semantic | Reset-flag (`141=Y`) decision |
|--------|-----------------|-----------------|-------------------------------|
| **QuickFIX-cpp** v1.16.0 | **Yes** (`RefreshOnLogon`) | `Session::refresh()` called unconditionally at **3** sites: `setResponder` (connect), `nextLogon` (receive Logon), `generateLogon` (send Logon). NEWS: *"refresh the store whenever the session logs on. Useful for creating backup systems."* | `shouldSendReset()` = `(resetOnLogon\|\|resetOnLogout\|\|resetOnDisconnect) && getExpectedSenderNum()==1 && getExpectedTargetNum()==1` — **`{1,1}`-guarded** |
| **QuickFIX/J** 3.0.1 | **Yes** (`refreshOnLogon`) | `refreshState()` = `getStore().refresh()` at **2** sites: `generateLogon` (send) + Logon-receipt. Composes `refreshState()` → `resetState()` → `isResetNeeded()` | `isResetNeeded()` = `(resetOnLogon\|\|resetOnLogout\|\|resetOnDisconnect) && sender==1 && target==1` — **`{1,1}`-guarded** |
| **Fix8** 1.4.3 | recover is **always-on** (`recover_seqnums()`, not a per-logon knob) | `recover_seqnums()` loads from the persister on connect/logon unless resetting (`session.cpp:194`/`:588`) | `_reset_sequence_numbers` couples a reset to `{1,1}` (`:190`/`:584`) **with** the `141=Y` emission (`:946`); else recover. |

**Cross-engine invariant: `ResetSeqNumFlag(141=Y)` ⟹ Logon body `MsgSeqNum == 1`, always.** No
engine emits a `141=Y` Logon with a non-1 body — each couples "announce reset" with "be at `{1,1}`".
QFJ even accepts `refreshOnLogon` + `resetOnLogon` together (`generateLogon`: refresh loads non-1,
reset overwrites to 1, `isResetNeeded()` then fires) — it does **not** reject the combo.

**Official FIX/FIXT:** does **not** define RefreshOnLogon (it is an engine config, not a protocol
feature). The only normative constraint is the `141=Y` ⟹ `MsgSeqNum=1` rule (hard reset-to-1).

## Decisions

### D-RoL-1 — Store-wins / unconditional (NOT advance-only) [user-locked]

The re-hydrate sets the manager to the store's values whether up or down. **Rationale:** the
documented use case is a hot-standby/backup that must adopt the primary's persisted counters,
including following a primary's reset-to-1 (DOWN); an advance-only `max(store, live)` clamp would
break that. Matches QFcpp/QFJ unconditional `refresh()`/`refreshState()`. **Alternatives rejected:**
advance-only clamp (breaks standby-follows-reset); a separate "merge" policy (no reference analogue,
gratuitous surface).

### D-RoL-2 — Branch identity: keep `025` [user-locked]

Deleted the stale parked branch `025-refresh-on-logon`@`9ff5604` (recoverable), recreated fresh off
main, kept the `025` number for catalogue/S-018/doc continuity. The old parked bundle's premises
(outbound-only descope; "store is not the inbound source of truth" RC#3/New-1/New-2) are
**discharged by 029** and do not apply.

### D-RoL-3 — Suppress under `bilateral_strict` [clarify Session 2026-06-09, Option A]

`bilateral_strict` (the **default** `reset_seqnum_policy`) emits an unconditional `141=Y` on the
initiator Logon (`session.cpp:713-715`, arm A is `bilateral_strict`-only — `bilateral_lenient`/
`unilateral` are NOT in it). A store-wins hydrate to a non-1 outbound under it would build a
malformed Logon (`141=Y` + non-1 body). **Decision:** the per-logon re-hydrate fires **only under
`bilateral_lenient`/`unilateral`**; it is suppressed (no-op) under `bilateral_strict`. The gate
lives at the **call sites** (`force = refresh_on_logon && policy != bilateral_strict`), so under
`bilateral_strict` there are zero extra store reads. `reset_on_logon` needs no special-casing (029
ordering: outbound hydrate → durable reset → reset-flag → body=1, tested at
`test_persistent_seqnum_hydrate.cpp:585-609`). **Rationale:** the only choice that keeps fixpp's
wire behaviour matching all three reference engines + the FIX `141=Y`⟹`MsgSeqNum=1` rule; `bilateral_strict`
is an "always reset to 1" policy whose semantic is the opposite of "adopt the store's preserved
value", so there is nothing coherent to refresh-to-non-1. **Alternatives rejected:** (i) hard config
rejection — more restrictive than any reference engine (QFJ accepts the combo); plain-value
`SessionConfig` has no validation hook. (ii) Make `bilateral_strict` itself `{1,1}`-guarded like
QFJ/QFcpp — a 024 behaviour change touching the mutual-agreement handshake (`session.cpp:1795` peer-
must-also-send-141 disconnect); OUT OF SCOPE. (iii) Force body=1 under strict while still refreshing
inbound — defeats refresh's purpose, edges into 024 semantics. **Consequence (L-025-1):** since
`bilateral_strict` is the default, `refresh_on_logon` is a no-op out of the box until a non-strict
policy is selected.

### D-RoL-4 — Reuse the 029 pipeline; the only delta is a `force` latch-bypass

`ensure_hydrated_(apply_inbound_seed, force=false)`: when `force`, skip **only** the `hydrated_`
one-shot early-return (`session.cpp:564-566`). Everything else is reused unchanged — the
`hydrating_` re-entrancy guard (`:568`), the `store_is_persistent_` skip (`:576`, INV-H4 / FR-005
non-persistent no-op), the both-reads-before-mutate no-partial-seed rule (`:583-594`), the
`apply_inbound_seed` withhold (`:601`, RC-1), and the fatal read/hydrate-failure disposition
(`record_state_transition_(Disconnected)`, FR-006). The `hydrated_` latch (`:610`) is left set by a
forced call — harmless, `force` ignores it. **Rationale:** the per-logon re-hydrate has identical
read/seed/failure semantics to the cold hydrate; the only difference is *whether it re-runs*. A
separate `refresh_seqnums_()` method would duplicate the read/seed/fatal logic — rejected for KISS.

### D-RoL-5 — Two existing 029 call sites, re-entered per logon (no new site)

- **Initiator:** `emit_initiator_logon_()` (`:639`), the `ensure_hydrated_` call at `:658`. This
  runs on `open()` (direct) AND every `drive_reconnect()` (engine lazy-connect) — so it is the
  per-logon initiator event. The hydrate is before `peek_outbound()` (`:699`) samples the body.
- **Acceptor:** the `NotConnected` inbound-Logon case, the `ensure_hydrated_` call at `:1738`. This
  runs on every received Logon — the per-logon acceptor event. The hydrate is before `check_inbound`
  and before the reply Logon samples `peek_outbound()` (`:1951`).

Two sites cover both roles' "at logon, before the outbound body is sampled" point. QFJ uses 2
(send + receipt); QFcpp 3 (adds `setResponder`). fixpp's emit + acceptor-Logon-handler cover the
send + receive events; **no third site** is added (the initiator Logon-ack path does not re-sample
the outbound body, so a refresh there would be redundant for the same logon). **Flagged for Gate A**
if a reviewer argues a Logon-ack-receipt refresh is needed for QFJ parity.

### D-RoL-6 — Latent 029 cold-open `bilateral_strict` gap: flag, default-defer

029's cold-open hydrate seeds the outbound counter unconditionally (`:602`, no policy gate), so a
`bilateral_strict` session restarting against a persistent non-1 store already emits a malformed
cold-open Logon — **untested in 029** (all its hydrate witnesses use `bilateral_lenient`). 025's
knob, gated to non-strict (D-RoL-3), never makes this worse. **Decision:** do NOT fix the cold path
in 025 (out of scope per the advisor's "don't re-fix 029"); record as **L-029-3** (deferred 029
follow-up). The same one-line `policy != bilateral_strict` guard, if applied to the outbound seed
inside `ensure_hydrated_`, would close it — **routed to Gate A** as a fold-in-or-defer decision.

## Open items for Gate A

1. D-RoL-5: are two refresh sites sufficient, or is a Logon-ack-receipt refresh needed for QFJ
   parity? (Recommendation: two suffice — the ack path does not re-sample the outbound body.)
2. D-RoL-6: fold the cold-open `bilateral_strict` gap fix into 025, or defer as L-029-3?
   (Recommendation: defer.)
