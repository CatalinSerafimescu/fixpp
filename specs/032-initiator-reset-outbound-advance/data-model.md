# Phase 1 — Data Model: Initiator reset_on_logon Outbound Restore

**Feature**: 032-initiator-reset-outbound-advance | **Date**: 2026-06-11

No new entities, types, wire fields, error slots, or config. This feature corrects the value of an
existing in-memory/durable counter and one existing event field on one code arm. The "model" is the
counter-state transition on the initiator `peer_ack_sent_reset_flag` arm and the predicate that gates
the correction.

## Existing entities touched (no schema change)

| Entity | Field | Type | Change |
|---|---|---|---|
| `SeqnumManager` (per session) | next-outbound | `seqnum_t` | value corrected on this arm (restore to `seqnum_min+1` when applicable) via a **new public** `set_next_outbound(seqnum_t)` method on the internal `SeqnumManager`, mirroring the merged-030 public `set_next_inbound` twin (no existing outbound setter — RC2 storage half); header change, no new C-ABI surface |
| `Session` (per session) | `own_logon_sent_reset_flag_` | `bool` | **NEW private strand-confined member** — latches the literal emit-time `initr_reset_seqnum` value (`session.cpp:721`) when the initiator Logon is built; read on the `peer_ack_sent_reset_flag` arm; consumed + cleared one-shot after the Logon-ack. Private member only (no public/wire/C-ABI surface) |
| `SeqnumManager` (per session) | next-inbound | `seqnum_t` | UNCHANGED — `030` already restores it to `2` on this arm |
| `MessageStore` (if persistent) | durable outbound | `seqnum_t` | written through a **new private** `Session::persist_outbound_advance_()` (mirroring `029`'s `persist_inbound_advance_()`; no existing outbound-persist path) to match the corrected manager value; `030` fatal-when-persistent disposition; INV-H1 (`store ≤ manager`) preserved (equality at `2`) |
| `session_event_sequence_numbers_reset` | `by_peer_request` | `bool` | classification corrected: `false` when fixpp initiated (emitted `141=Y`), `true` when peer initiated |

## The gates (one latched fact + one on-arm observable)

The "fixpp emitted `141=Y`" half is the **latched emit-time fact** `own_logon_sent_reset_flag_`, NOT a
reconstruction. At emit (`session.cpp:721`, where `initr_reset_seqnum` is computed for the Logon being
built) the literal value is latched; on the ack arm it is read and one-shot cleared. The seq-1 half is
the on-arm observable `reset_before_send`:

```
// At emit (session.cpp:721):
own_logon_sent_reset_flag_ := initr_reset_seqnum
                           := (policy == bilateral_strict) || (any_reset_knob && seqnums_at_one)
// seqnums_at_one := (peek_outbound()==1 AND next_inbound_unsafe()==1) — BOTH counters (session.cpp:719-720)

// On the peer_ack_sent_reset_flag arm:
n_pre_outbound    := seqnum_mgr_.peek_outbound()              // sampled before the reset rewinds it
reset_before_send := (n_pre_outbound == seqnum_min + 1)      // fixpp's own Logon went at the post-reset seq 1
restore_outbound  := own_logon_sent_reset_flag_ && reset_before_send   // outbound-restore gate (BOTH required)
by_peer_request   := !own_logon_sent_reset_flag_             // event label — latch ONLY
```

**The two gates are DISTINCT.** `restore_outbound` requires the latch AND `reset_before_send`;
`by_peer_request` reads the latch ALONE. They diverge on `bilateral_strict`-at-N (latch true →
`by_peer_request=false`, but `reset_before_send` false → no restore). Do NOT collapse them onto one
predicate (the earlier draft did).

**Why latch, not reconstruct (RC1 / Codex #1):** reconstructing
`we_initiated := bilateral_strict || (any_reset_knob && reset_before_send)` on the ack arm is strictly
weaker than `initr_reset_seqnum` — it drops the inbound-at-1 conjunct of `seqnums_at_one`. A hydrated
initiator `{inbound=37, outbound=1}` + `reset_on_logout=true` emits NO `141=Y` (inbound≠1 at emit), but
the reconstruction would see `any_reset_knob && n_pre_outbound==2` and misclassify it → wrongly restore
to 2 + `by_peer_request=false`. Latching the literal emit-time value carries inbound-at-1 for free.

**`reset_before_send` is load-bearing in `restore_outbound`** (necessary but not sufficient): the latch
alone is true for `bilateral_strict`-at-N (via the policy branch), whose Logon went at seq 10 — restoring
to 2 there regresses `BilateralStrict_Initiator_CountersResetToOne` (W4). The latch is also true for a
fresh `bilateral_strict` at `{1,1}` where the restore IS wanted; `reset_before_send` is what separates
the two. (The fresh-no-knob peer-spontaneous case is excluded by the latch being false — W7.)

## Counter-state transition on the initiator `peer_ack_sent_reset_flag` arm

Entry pre-state (after `check_inbound` on the peer's seq-1 `141=Y` Logon-ack):

| Path | inbound (post check_inbound) | `n_pre_outbound` | `reset_before_send` | latch (`own_logon_sent_reset_flag_`) | `restore_outbound` | `by_peer_request` |
|---|---|---|---|---|---|---|
| reset_on_logon (bug) | 2 | 2 | true | true | **true** | false |
| reset_on_logout/disconnect reconnect-at-1 ({1,1}) | 2 | 2 | true | true | **true** | false |
| fresh bilateral_strict {1,1} | 2 | 2 | true | true | **true** | false |
| bilateral_strict pre-seeded N=10 | 2 | 11 | false | true | false | false |
| peer-spontaneous, fixpp Logon at N>1 | 2 | N+1 | false | false | false | true |
| **hydrated {in=37,out=1}, reset_on_logout, non-strict; peer-spontaneous 141=Y** | 2 | 2 | true | **false** (inbound≠1 at emit → latch F) | **false** | true |
| **fresh no-knob lenient; peer-spontaneous 141=Y** | 2 | 2 | true | **false** | **false** | true |

(Switching reconstruction→latch flips ONLY the hydrated row: the reconstruction wrongly gave latch=true
there. All other rows are identical under both.)

`reset_seqnums_to_one_durable` rewinds → `{inbound=1, outbound=1}` — then:

| Step | inbound | outbound | Notes |
|---|---|---|---|
| after `reset_seqnums_to_one_durable` | 1 | 1 | both rewound |
| `030` inbound restore (existing) | **2** | 1 | guarded on `logon_inbound_advanced_init` |
| **032** outbound restore (new) | 2 | **2** | guarded on `restore_outbound = own_logon_sent_reset_flag_ && reset_before_send`; `set_next_outbound(2)` then `persist_outbound_advance_()`, manager-first, store-second |
| event emit | — | — | `by_peer_request = !own_logon_sent_reset_flag_` (latch only) |

Post-state on the `restore_outbound=false` arms (either conjunct false): outbound stays `1` (no
restore) — UNCHANGED baseline.

(Mechanism A — restore-after-reset — is the resolved design. Mechanism B (skip-reset) is dropped: it is
unsound for the fresh `bilateral_strict`-at-`{1,1}` row, whose only durable reset on the path is this
ack-arm reset — see research.md R4.)

## The event classification (latched emit-time fact)

Today (`session.cpp:3224`):
```
we_initiated := (policy == bilateral_strict)            // bilateral_strict-only → mislabels reset_on_logon non-strict
```
Corrected — keyed off the **latched** literal emit-time fact (NOT reconstructed on the arm):
```
// latched at emit (session.cpp:721): own_logon_sent_reset_flag_ := initr_reset_seqnum
by_peer_request := !own_logon_sent_reset_flag_          // read on the arm; false iff fixpp actually emitted 141=Y
```

| Path | latch (`own_logon_sent_reset_flag_`) | by_peer_request | vs today |
|---|---|---|---|
| reset_on_logon (+ non-strict, Logon at {1,1}) | true | **false** | corrected (was `true`) |
| bilateral_strict (any Logon seq) | true | false | unchanged |
| hydrated reset_on_logout, inbound≠1 at emit (no 141=Y) | false | true | correct (reconstruction would have said false) |
| peer-spontaneous (no fixpp 141=Y) | false | true | unchanged |

## Invariants

- **INV-032-1**: on the `restore_outbound=true` arm (`own_logon_sent_reset_flag_ && reset_before_send`),
  post-fix next-outbound `== seqnum_min+1` (`=2`).
- **INV-032-2**: on every `restore_outbound=false` arm (bilateral_strict-at-N, peer-spontaneous-at-N,
  fresh peer-spontaneous-at-seq-1, AND the hydrated reset_on_logout-with-inbound≠1-at-emit case),
  next-outbound is byte-identical to the pre-fix baseline.
- **INV-H1 (preserved, 029)**: `durable_outbound ≤ manager_outbound` at all times; equality at `2` on
  the restore is a surviving net-advance, not over-persist.
- **INV-032-3**: `by_peer_request == false` iff fixpp actually emitted `141=Y` (the latched
  `own_logon_sent_reset_flag_`); `== true` iff the peer initiated a reset fixpp did not request.
- Acceptor role, knob-off default: untouched (no entry to this initiator arm).
