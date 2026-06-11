# Contract — Initiator `peer_ack_sent_reset_flag` arm: outbound restore + event label

**Feature**: 032-initiator-reset-outbound-advance | **Date**: 2026-06-11
**Site**: `src/session/session.cpp` initiator LogonSent handler, `peer_ack_sent_reset_flag` block
(`:3185`), `we_initiated` computation (`:3224`). No public/ABI/wire surface — internal session
behavior contract.

## Inputs (arm entry)

- Role: **initiator**; FSM state `LogonSent`; inbound frame is a Logon-ack with `141=Y`
  (`peer_ack_sent_reset_flag == true`).
- `own_logon_sent_reset_flag_` (**NEW**, latched at emit) — the literal `initr_reset_seqnum` value
  (`session.cpp:721`, `= bilateral_strict || (any_reset_knob && seqnums_at_one)`, where `seqnums_at_one`
  requires BOTH counters at 1) captured when the initiator Logon was built; the fact "fixpp actually
  emitted `141=Y`". **Unconditionally assigned** on every initiator-Logon emit
  (`own_logon_sent_reset_flag_ := initr_reset_seqnum`, matching the data-model.md `:=` phrasing) — which
  OVERWRITES it to `false` when fixpp does not emit `141=Y` — NOT conditionally set-only-when-true. This
  unconditional assign on every emit is what makes a stale latch structurally impossible across reconnects
  (the round-2 inertness argument). Consumed + one-shot cleared after the Logon-ack. Read on this arm —
  NOT reconstructed from config flags + a counter sample (which drops the inbound-at-1 conjunct — RC1 / Codex #1).
- `n_pre_outbound := seqnum_mgr_.peek_outbound()` captured BEFORE `reset_seqnums_to_one_durable`.
- `reset_before_send := (n_pre_outbound == seqnum_min + 1)` — fixpp's own Logon went at post-reset seq 1.
- `restore_outbound := own_logon_sent_reset_flag_ && reset_before_send` — the outbound-restore gate
  (both conjuncts required; the latch alone is insufficient — `bilateral_strict`-at-N has the latch true
  but Logon at seq N>1).
- `logon_inbound_advanced_init` (existing, `030`) — the peer's seq-1 Logon-ack was consumed by `check_inbound`.
- `store_is_persistent_` (existing, `029`).

## Postconditions

### C1 — Outbound counter (FR-001/FR-003)
- **If `restore_outbound` (= `own_logon_sent_reset_flag_ && reset_before_send`)**: after honoring the
  echoed reset, `peek_outbound() == seqnum_min + 1` (`=2`) via `set_next_outbound(seqnum_min+1)` +
  `persist_outbound_advance_()`. The next frame fixpp originates carries `34 == 2`; **no** emitted frame
  re-uses `34 == 1` (the reset Logon's consumed seq).
- **Else** (either conjunct false — `bilateral_strict`-at-N, peer-spontaneous-at-N, fresh
  peer-spontaneous-at-seq-1, OR hydrated-reset_on_logout-with-inbound≠1-at-emit): `peek_outbound()` is
  byte-identical to the pre-fix baseline (the reset rewinds to `1`; no restore). The
  fresh-peer-spontaneous-at-seq-1 case (`reset_before_send` true but the latch false) and the hydrated
  case (latch false because inbound≠1 at emit) MUST stay `1` — restoring to `2` is a too-high regression.

### C2 — Inbound counter (FR-004, 030 preserved)
- `next_inbound == seqnum_min + 1` (`=2`) on this arm whenever `logon_inbound_advanced_init` — the
  `030` restore is unchanged.

### C3 — Persistence (FR-007, INV-H1)
- Persistent store: `durable_outbound == manager_outbound` after the restore (equality at `2`); never
  `durable_outbound > manager_outbound`. The reset-failure disposition is `030`'s
  `store_is_persistent_ ? fatal : logged` — unchanged.

### C4 — Reset event classification (FR-006)
- `sequence_numbers_reset.by_peer_request == false` iff `own_logon_sent_reset_flag_` (the latched
  emit-time fact that fixpp actually emitted `141=Y`) — the latch ALONE, no `reset_before_send` conjunct.
- `== true` iff the peer initiated a reset fixpp did not request (fixpp emitted no `141=Y`).
- C4 (latch only) and C1 (`latch && reset_before_send`) are DISTINCT gates: they diverge on
  `bilateral_strict`-at-N (`by_peer_request=false` but no outbound restore).

### C5 — FSM (FR-002)
- The session reaches `Active` on this arm exactly as before; the state transition is unchanged.

### C6 — Out-of-scope invariance (FR-008)
- Acceptor role, knob-off default, and any path not entering this arm: **byte-identical** to baseline.

## Mechanism (RESOLVED at Gate A: Mechanism A)
- **A (the design)**: restore-after-reset — `reset_seqnums_to_one_durable` then, if `restore_outbound`
  (`own_logon_sent_reset_flag_ && reset_before_send`), `set_next_outbound(seqnum_min+1)` +
  `persist_outbound_advance_()` (manager-first, store-second; `030` fatal-when-persistent disposition).
  `Session::persist_outbound_advance_` is a NEW private Session method; `SeqnumManager::set_next_outbound`
  is a NEW **public** method on the internal `SeqnumManager` class, mirroring the merged-030 public
  `set_next_inbound` twin. No new wire/error-slot/codegen/**C-ABI**/config surface (FR-009 holds —
  `SeqnumManager` is awaitable-returning C++, not C-ABI-exportable; no external caller).
- **B (DROPPED)**: skip `reset_seqnums_to_one_durable` — unsound for the fresh `bilateral_strict`-at-`{1,1}`
  row (whose only durable reset on the path is this ack-arm reset; the open-time reset gate
  `session.cpp:681` fires on `reset_on_logon` only). See research.md R4.

## Negative / non-regression (must NOT change)
- `bilateral_strict` initiator, Logon pre-seeded at seq N>1: outbound stays `1` (`reset_before_send`
  false); this is `BilateralStrict_Initiator_CountersResetToOne` — green unchanged (W4).
- Peer-spontaneous reset, fixpp Logon at N>1 (fixpp sent no `141=Y`): outbound baseline,
  `by_peer_request=true` (W3).
- **Fresh, no-knob, lenient/unilateral initiator; Logon at seq 1 with no `141=Y`; peer spontaneously
  sends `Logon(34=1, 141=Y)`**: outbound MUST stay `1` (latch false), `by_peer_request=true` (W7). This
  is the latch conjunct's load-bearing case — `reset_before_send`-alone would wrongly restore to `2`.
- **Hydrated initiator `{next_inbound=37, next_outbound=1}`, `reset_on_logout=true`, `reset_on_logon=false`,
  non-strict; Logon emitted at seq 1 with no `141=Y` (inbound≠1 at emit → `seqnums_at_one` false); peer
  sends a spontaneous in-sequence `141=Y` ack**: outbound MUST stay `1` (latch false), `by_peer_request=true`
  (W8). This is the inbound-at-1 case that a counter-only reconstruction would misclassify (RC1 / Codex #1);
  only the latched emit-time fact passes it.
