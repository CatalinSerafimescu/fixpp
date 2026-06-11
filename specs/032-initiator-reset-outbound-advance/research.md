# Phase 0 — Research: Initiator reset_on_logon Outbound Restore on Peer 141=Y Echo

**Feature**: 032-initiator-reset-outbound-advance | **Date**: 2026-06-11

All NEEDS CLARIFICATION from Technical Context are resolved below. Format: Decision / Rationale /
Alternatives.

---

## R1 — Reference-engine oracle: reset-then-increment on the sender (outbound) side

**Decision**: After a `141=Y` Logon exchange, each side's next **outbound** sequence number is `2`,
because the Logon that carries `141=Y` is itself sent at the post-reset sequence `1` and thereby
consumes it. fixpp must keep its next-outbound at `2` after honoring a peer's echo of fixpp's own
reset.

**Rationale**:
- **Direct empirical oracle (binding).** On the first live run of `RL-{QFcpp,QFj}-init-fix44-reset-on-logon`,
  fixpp (reset_on_logon initiator) sent `Logon(141=Y, 34=1)`, then rebased outbound to `1` and sent its
  next frame at `34=1` again; **both** QuickFIX-cpp v1.16.0 and QuickFIX-J 3.0.1 responded
  `Logout: "MsgSeqNum too low, expecting 2 but received 1"`. The reference engines' own reject text
  states the expected next inbound-from-fixpp value is `2` — i.e. they apply reset-then-increment to
  the counterparty's sender stream exactly as to their own.
- **Established parity (030/031).** `030` grounded and shipped the inbound (target) twin of this rule
  on this same arm: after a received-141 Logon the next target is `2` (QFcpp reply-build comment "+1
  because incoming Logon did not increment the target SeqNum yet"); the live `RL-*-acc` cells pass.
  The sender/outbound side is the symmetric statement. `031` further confirmed the engines advertise
  `NextExpectedMsgSeqNum = target` (no +1) on the initial Logon and `target+1` on the reply — both
  consistent with "the Logon at seq 1 consumed seq 1, next is 2".
- **Reference-engine source** (`reference-engines/quickfix-cpp`, `quickfix-j`) is the project's
  standing oracle ([[project_reference_engines_setup]]) but is **not checked out in this working tree**
  (gitignored). The live wire transcript above is the binding, reproducible evidence; a source
  line-cite can be added at Gate A if the engines are re-cloned, but is not required — the live reject
  is dispositive.

**Alternatives considered**: "reset means next is 1" (the current buggy behavior) — refuted directly
by the live reject. Rejected.

---

## R2 — The fix is the outbound twin of the 030 inbound restore (not a new mechanism)

**Decision**: This is structurally the same fix `030` applied to the inbound counter on the **same**
arm (`session.cpp:3210`), applied to the **outbound** counter. `030` is the design precedent.

**Rationale**: On the initiator `peer_ack_sent_reset_flag` arm, `reset_seqnums_to_one_durable` rewinds
both counters to `1`. `030` recognized that `check_inbound` had already advanced the inbound counter
to `2` (the peer's consumed seq-1 reset-ack Logon is a surviving net-advance) and restored it. The
outbound counter has the symmetric surviving net-advance: **fixpp's own** reset Logon consumed the
post-reset seq `1`, so its next outbound is `2`. The restore is the same shape (set the counter +
durable write, manager-first/store-second, INV-H1 equality at `2`).

**Alternatives considered**: treating it as unrelated to `030` and inventing a separate path —
rejected; it would duplicate logic and miss the proven INV-H1/persist discipline.

---

## R3 — The trigger is the CONJUNCTION: "Logon went at post-reset seq 1" AND the LATCHED emit-time fact "fixpp emitted 141=Y"

**Decision**: Latch the literal emit-time `141=Y`-emit fact and gate the outbound restore on **both**
conjuncts:

```
// At emit (session.cpp:721, where initr_reset_seqnum is computed for the Logon being built):
own_logon_sent_reset_flag_ := initr_reset_seqnum
                           := (policy == bilateral_strict) || (any_reset_knob && seqnums_at_one)
// seqnums_at_one := (peek_outbound()==1 AND next_inbound_unsafe()==1)  — BOTH counters (session.cpp:719-720)

// On the peer_ack_sent_reset_flag arm (consumed + one-shot cleared after the Logon-ack):
reset_before_send := (n_pre_outbound == seqnum_min + 1)              // fixpp's Logon went at seq 1
we_initiated      := own_logon_sent_reset_flag_                       // the LATCHED emit-time fact
restore_outbound  := own_logon_sent_reset_flag_ && reset_before_send  // BOTH required
by_peer_request   := !own_logon_sent_reset_flag_                      // event label — latch ONLY
```

**Why latch, not reconstruct.** The earlier draft reconstructed
`we_initiated := bilateral_strict || (any_reset_knob && reset_before_send)` on the ack arm. That is
**strictly weaker** than the real emit predicate `initr_reset_seqnum`, because `seqnums_at_one`
(`session.cpp:719-720`) requires BOTH outbound==1 AND **inbound**==1 at emit, while `reset_before_send`
proves only that **outbound** went at 1. The reconstruction drops the inbound-at-1 conjunct. A hydrated
initiator at `{next_inbound=37, next_outbound=1}` with `reset_on_logout=true` (or `reset_on_disconnect`),
`reset_on_logon=false`, non-strict policy, emits its Logon with **NO `141=Y`** (because inbound≠1 at
emit, so `seqnums_at_one` is false). If the peer then sends a spontaneous in-sequence `141=Y` ack, the
reconstruction sees `n_pre_outbound==2` and `any_reset_knob==true` and **misclassifies it as
fixpp-initiated** → restores outbound to 2 and emits `by_peer_request=false` (the exact FR-005/FR-006
regression). Latching the literal `initr_reset_seqnum` value at emit carries the inbound-at-1 conjunct
for free and dissolves the hole. The latch is **necessary but not sufficient** for the counter restore:
`reset_before_send` (= `n_pre_outbound==seqnum_min+1`) remains a load-bearing second conjunct — see the
`bilateral_strict`-at-N row, where the latch is true (via the policy branch) but the Logon went at seq
10, so restore must NOT fire.

**The latch and the event label are DISTINCT gates.** `by_peer_request := !own_logon_sent_reset_flag_`
uses the latch ALONE; `restore_outbound` adds the `reset_before_send` conjunct. They diverge exactly on
the `bilateral_strict`-at-N row (`by_peer_request=false` because fixpp emitted `141=Y`, but restore=false
because the Logon went at seq N>1). Do NOT collapse them onto one predicate.

**Rationale** — partition (each verified against `emit_initiator_logon_` `:681`/`:719-723` and the arm).
`own_logon_sent_reset_flag_` is the value `initr_reset_seqnum` took **at emit**:

| Path | fixpp 141=Y? (latch) | Logon seq | `n_pre_outbound` | `reset_before_send` | latch (`we_initiated`) | Restore? | by_peer_request | Post-fix outbound | Correct? |
|---|---|---|---|---|---|---|---|---|---|
| `reset_on_logon` (live bug) | yes | 1 | 2 | T | T | **yes** | false | **2** | ✅ (the fix) |
| `reset_on_logout`/`reset_on_disconnect`, reconnect at seq 1 (counters {1,1}) | yes | 1 | 2 | T | T | **yes** | false | **2** | ✅ (clarify Q1) |
| fresh `bilateral_strict`, default `{1,1}` | yes | 1 | 2 | T | T | **yes** | false | **2** | ✅ (mutual reset → 2) |
| `bilateral_strict`, pre-seeded N>1 (`..._CountersResetToOne`) | yes | N (=10) | N+1 | F | T | no | false | **1** | ✅ unchanged |
| peer-spontaneous, fixpp Logon at N>1 | no | N | N+1 | F | F | no | true | **1** | ✅ unchanged |
| **hydrated `{inbound=37, outbound=1}`, `reset_on_logout=true`, non-strict; peer-spontaneous `141=Y`** | **no** (inbound≠1 at emit → `seqnums_at_one` F → latch F) | **1** | **2** | **T** | **F** | **no** | **true** | **1** | ✅ (latch fixes the reconstruction hole — Codex #1 counterexample) |
| **fresh, NO knob, lenient/unilateral; peer spontaneously sends `141=Y`** | **no** | **1** | **2** | **T** | **F** | **no** | **true** | **1** | ✅ (peer declared epoch → fixpp next = 1; restore-to-2 = too-high regression) |

The last two rows are the holes a reconstruction would mishandle. The **hydrated** row is Codex's
counterexample: only the latched emit-time fact (which carries the inbound-at-1 conjunct) classifies it
correctly; the reconstruction `bilateral_strict || (any_reset_knob && reset_before_send)` would give
latch=T → restore-to-2 + `by_peer_request=false` (wrong). The final row is the fresh peer-spontaneous-at-1
case (a ResetOnLogon **acceptor** peer commonly sends `Logon(34=1, 141=Y)`); fixpp emitted no `141=Y` so
the latch is false. Switching reconstruction→latch flips ONLY the new hydrated row; all six other rows
keep identical results.

**Alternatives considered**:
- Key on `cfg_.reset_on_logon` only — **rejected** (clarify Q1): misses `reset_on_logout`/`disconnect`.
- Reconstruct `we_initiated := bilateral_strict || (any_reset_knob && reset_before_send)` on the ack arm
  — **rejected** (Codex #1 / RC1): drops the inbound-at-1 conjunct, misclassifies the hydrated row above.
  Replaced by the emit-time latch `own_logon_sent_reset_flag_`.
- Key on the latch only (no seq-1 conjunct) for the restore — **rejected**: `bilateral_strict`-at-N has
  the latch true but must NOT restore; the latch alone does not capture "Logon went at seq 1".
- Key on `reset_before_send` only (no latch conjunct) — **rejected**: regresses the fresh
  peer-spontaneous-at-seq-1 case into a spurious too-high `ResendRequest`.
- Blind restore-to-`seqnum_min+1` whenever the arm fires — **rejected**: regresses both
  `..._CountersResetToOne` and the fresh/hydrated peer-spontaneous cases.

---

## R4 — Mechanism (RESOLVED at Gate A: Mechanism A) + storage primitive + blast radius

**Decision**: **Mechanism A — restore-after-reset** is THE design. Mechanism B (skip-the-redundant-reset)
is **DROPPED** — it is unsound for an admitted row (below). Blast radius enumerated empirically at
/tasks — do not pre-judge — but seeded here with the historically-missed `030` pins.

**Mechanism A — restore-after-reset (030 pattern)**: capture
`const seqnum_t n_pre_outbound = seqnum_mgr_.peek_outbound();` at the top of the
`peer_ack_sent_reset_flag` block (before `reset_seqnums_to_one_durable`); after the reset succeeds,
`if (own_logon_sent_reset_flag_ && reset_before_send) { <set outbound to seqnum_min+1> ; <persist outbound> ; }`
(manager-first, store-second; the `030` fatal-when-persistent disposition on the reset itself is
unchanged). INV-H1: `store_outbound == manager_outbound == 2` — equality, a surviving net-advance, not
over-persist.

**Storage primitive (RC2 / Codex #3 — resolved now, not deferred).** There is **no existing single
primitive** that sets outbound to `2` and persists. Verified against
`include/fixpp/session/seqnum_manager.hpp` + `src/session/seqnum_manager.cpp`: `SeqnumManager`
exposes exactly `assign_outbound()` (read-then-advance in-memory, does NOT persist), `hydrate(in, out)`,
`reset_to_one()`, `set_next_inbound(n)` — **there is no `set_next_outbound`**. And
`include/fixpp/session/session.hpp` has `persist_inbound_advance_()` (added by `029`) but **no
`persist_outbound_advance_`**. The fix therefore ADDS a narrowly-scoped, private/internal outbound
restore primitive, mirroring the `030` inbound twin:
- `SeqnumManager::set_next_outbound(seqnum_t)` — a private/internal outbound setter mirroring
  `set_next_inbound`'s shape (force the in-memory counter; store write is the caller's responsibility,
  same split as `reset_to_one`/`set_next_inbound`), **and**
- `Session::persist_outbound_advance_()` — a durable outbound write mirroring `029`'s
  `persist_inbound_advance_()`.
- Failure disposition: the durable write follows the **`030` fatal-when-persistent** contract NOW (not
  deferred) — a store failure on a persistent store records `Disconnected` and propagates the error;
  no-op (INV-H4) on a null/non-persistent store.

These are **private session/manager methods** (header change → source rebuild) — NOT a new public/wire/
error-slot/C-ABI/config surface. FR-009's no-new-public-surface claim still holds; the no-`surface`-change
phrasing in the plan's Constitution Check is reconciled to acknowledge the private header addition. The
exact method names are confirmed at /tasks via codegraph; the SHAPE + failure disposition are fixed here.

**Why Mechanism B is dropped (Codex #2 / RC2).** Mechanism B's premise — "counters are already `{2,2}`
because open() reset them, so skip the ack-arm reset" — is **false** for the fresh `bilateral_strict`
at `{1,1}` row that this design's own gate admits as `restore_outbound=true` (R3 row 3). The open-time
reset gate is `if (cfg_.reset_on_logon)` ONLY (`session.cpp:681`); `bilateral_strict` does NOT run the
open-time durable reset — it reaches `{1,1}` by being a fresh session and emits `141=Y` purely via the
`:721-723` policy branch. For that row the only durable reset on the whole path is the ack-arm reset
Mechanism B proposes to skip; skipping it drops the durable store reset that establishes the reset
epoch. Mechanism B is therefore unsound as written, and narrowing it to "skip only when a durable reset
provably ran before emit" reduces to `reset_on_logon` (not the full `restore_outbound` set), no longer
covering `bilateral_strict` — not worth the asymmetry. Mechanism A is the proven `030` pattern and is
adopted.

**Blast-radius hypothesis (confirm at /tasks /analyze — R4 directive; clean-build FULL ctest, not
incremental, per the `030` close-out which undercounted by 2 pins)**:
- `tests/session/test_persistent_seqnum_hydrate.cpp::DISABLED_ResetOnLogon_Initiator_PeerAck141_OutboundStaysTwo`
  — the committed RED harm test; **flips to enabled + green** + EXTENDED to originate a post-Active
  frame and assert wire `34=2` (W1, SC-002).
- `tests/session/test_persistent_seqnum_hydrate.cpp::INV_H1_Initiator_PeerAck141_NoOverPersist`
  (`:2096`) — the `029/030` initiator INV-H1 twin on THIS arm. Adding a **second** durable write
  (outbound) on the same arm is precisely the change that can break an INV-H1 lower-bound pin
  ([[feedback_unconditional_persist_at_multiexit_gate_breaks_lowerbound]]) — re-verify
  `durable_outbound ≤ manager_outbound` (New-P2).
- `tests/session/test_reset_on_lifecycle.cpp` — the `030` initiator-arm witnesses live HERE on the SAME
  `peer_ack_sent_reset_flag` arm and MUST be re-run / re-verified (Codex #6):
  `Initiator_Received141Ack_NextInboundTwo_NoResend` (`:1906`),
  `Initiator_Received141Ack_PersistentStore_StoreEqualsManagerTwo` (`:1952`),
  `Initiator_Received141Ack_PersistentStore_ResetFailure_Disconnects` (`:1985`),
  `Initiator_Received141Ack_GuardSkipsWhenNoConsumedReset` (`:2036`). Add outbound + event-label
  assertions where the arm is exercised, or an explicit inbound-only justification per pin.
- `tests/session/test_refresh_on_logon.cpp::W6_Acceptor_KnobOn_PeerResetLogon_InboundSeedWithheld`
  (`:1196`) — `030` flipped its store-interaction **call_count** (2→3), invisible to a value-grep; an
  added outbound store write on the shared reset path can shift such a count (New-P2). Re-run + confirm
  the count.
- A `by_peer_request` event-label pin on the reset_on_logon path, if one exists, reconciled to
  `false` (W2). `BilateralStrict_Initiator_PeerConfirms141Y` already asserts `false` and is unchanged
  (bilateral_strict → latch true).
- `test_reset_seqnum_policy_matrix.cpp::BilateralStrict_Initiator_CountersResetToOne` (outbound `1`,
  Logon at seq 10) and any peer-spontaneous pin — MUST stay green **unchanged**.
- The acceptor `*_Acceptor_CountersResetToOne` pins (030-owned) — untouched (acceptor role out of scope).

---

## R5 — Witness design (RED-first, discriminating)

**Decision**: Seven witnesses; the harm test is already committed RED-on-main.

- **W1** (`test_persistent_seqnum_hydrate.cpp`): enable `ResetOnLogon_Initiator_PeerAck141_OutboundStaysTwo`
  — reset_on_logon initiator, peer `141=Y` echo ⇒ Active AND `peek_outbound()==2`. RED-on-main verified.
  **EXTENDED (P2#4 / SC-002):** the committed test asserts only the counters (`peek_outbound()==2`,
  inbound==2) — it does NOT originate a frame. W1 is extended (or a paired sub-witness added) to, after
  the Active transition, **originate a post-Active outbound frame through the unit harness, capture the
  emitted bytes, and assert wire `34=2` with no second frame at `34=1`**. The counter assertion is a
  supporting check, NOT the wire proxy — a buggy impl could set the counter for the assertion and still
  regress send/store ordering at the next emit ([[feedback_witness_asserts_named_postcondition_not_proxy]]).
- **W2**: same fixture ⇒ `recent_events()` carries `sequence_numbers_reset{by_peer_request=false}`
  (RED on main = today emits `true` because `we_initiated` is bilateral_strict-only). The corrected
  label gates on the latched `own_logon_sent_reset_flag_` (latch ONLY, no seq-1 conjunct). Idiom
  already in `test_reset_seqnum_policy_matrix.cpp`.
- **W3** (`test_reset_seqnum_policy_matrix.cpp`): peer-spontaneous, Logon at `N>1` — `reset_on_logon=false`,
  fixpp Logon at `N>1` (pre-seed), fixpp sends no `141=Y`, peer sends `141=Y` under a tolerant policy ⇒
  outbound UNCHANGED from baseline + `by_peer_request=true`. Non-regression (the `reset_before_send`-false arm).
- **W4**: `BilateralStrict_Initiator_CountersResetToOne` stays green unchanged (outbound `1`, Logon at
  seq 10) — discriminating proof that the fix keys on the seq-1 conjunct, not the policy alone.
- **W7** (THE discriminating witness for the latch conjunct): **fresh initiator, NO reset
  knob, `bilateral_lenient`, default `{1,1}`; fixpp `Logon(34=1)` with no `141=Y`; peer spontaneously
  sends `Logon(34=1, 141=Y)`** ⇒ outbound stays **`1`** + `by_peer_request=true`. RED under a
  `reset_before_send`-alone gate (would restore to `2` → spurious too-high), GREEN under
  `own_logon_sent_reset_flag_ && reset_before_send` (the latch is false here — fixpp emitted no `141=Y`).
  W3's Logon-at-`N>1` cannot catch this seq-1 hole (advisor finding).
- **W8** (the latch's inbound-at-1 conjunct — RC1 / Codex #1 counterexample): **hydrated initiator
  `{next_inbound=37, next_outbound=1}`, `reset_on_logout=true`, `reset_on_logon=false`, non-strict
  policy** — its Logon goes at `34=1` with NO `141=Y` (because inbound≠1 at emit → `seqnums_at_one`
  false → `initr_reset_seqnum` false → latch false); peer then sends a spontaneous in-sequence `141=Y`
  ack ⇒ outbound stays **`1`** + `by_peer_request=true`. RED under the dropped reconstruction
  `bilateral_strict || (any_reset_knob && reset_before_send)` (it would see `any_reset_knob && n_pre==2`
  → restore to 2 + `by_peer_request=false`), GREEN under the latch. This is the witness that a
  counter-only reconstruction cannot pass; only latching the emit-time fact (which carries inbound-at-1)
  classifies it correctly.
- **W5**: persistent-store reset-failure on this arm ⇒ fatal-when-persistent (the `030` disposition)
  unchanged; seed the store to a distinct sentinel `N≫asserted` so the failure assertion can falsify
  (avoid the trivial-seed-fault tautology — [[feedback_witness_asserts_named_postcondition_not_proxy]]).
- **W6**: persistent-store success ⇒ after the restore `store_outbound == manager_outbound` and never
  `store_outbound > manager_outbound` (INV-H1; seed to sentinel, assert the concrete corrected value,
  not a `≤` proxy).

**Rationale**: W4 (seq-1 conjunct), W7 (latch conjunct, no-knob seq-1), and W8 (latch's inbound-at-1
conjunct, hydrated) are the three load-bearing discriminators — W4 fails a blind symmetric restore, W7
fails a `reset_before_send`-alone gate, W8 fails the dropped counter-reconstruction; only the latched
`own_logon_sent_reset_flag_ && reset_before_send` passes all three.

**W7/W8 are GREEN-on-main AND GREEN-post-fix** (main has no outbound restore → outbound stays 1 in both;
the correct latch fix also leaves it at 1). Their discriminating power therefore exists ONLY relative to
the *wrong* gate, so each MUST be confirmed by a **mutation step** at /tasks/implement
([[feedback_symmetric_api_claim_unreachable_arm]] — "drop the gate → expect a fail; none = unwitnessed"):
temporarily implement `reset_before_send`-alone (no latch conjunct) → **W7 must go RED**; temporarily
implement the dropped reconstruction `bilateral_strict || (any_reset_knob && reset_before_send)` → **W8
must go RED**. Without these mutation checks the RC1 latch (the load-bearing change) ships unguarded —
W8 is the headline RC1 witness and passes trivially under a correct impl. W5/W6 carry the `029`/`030`
persistence discipline. Live `RL-*-init`
cells (hardened past
`drive_to_active` — assert a post-logon frame exchange / stay-Active, not merely reaching Active)
close SC-003.

**Latch-lifecycle mutation confirmation (cross-reconnect stale-latch).** The W8 single-connection
witness does NOT catch a latch-lifecycle deviation: if the implementation sets the member CONDITIONALLY
at emit (only when `initr_reset_seqnum` is true, rather than unconditional assign) AND clears it at the
end of the ack arm, a stale `true` latched on a prior reset-logon connection would survive into a later
non-reset connection and reintroduce the W8 regression — and no single-connection witness sees it.
Therefore require:
- (a) the latch is **unconditionally assigned** every emit (`own_logon_sent_reset_flag_ := initr_reset_seqnum`,
  per Edit 2 / the contract Inputs line) — never a conditional set-only-when-true.
- (b) a mutation check / **multi-connection witness**: a `Session` reused across reconnect where the first
  connection is a reset-logon (latch true) and the second connection's Logon is NOT a reset (latch must
  read false) — assert the second connection's peer-spontaneous `141=Y` does NOT restore outbound. Under
  a conditional-set-plus-clear mutation this goes RED (stale `true` survives → spurious restore); under
  the unconditional assign it is GREEN.

**Alternatives considered**: assert `peek_outbound() >= 2` (≤-proxy) — rejected; assert the exact
`==2`. Assert only Active without the frame-at-34=2 check — rejected (a plain establishment satisfies
it; must witness the non-duplicate next send).
