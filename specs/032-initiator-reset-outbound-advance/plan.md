# Implementation Plan: Initiator reset_on_logon Outbound Seqnum Restore on Peer 141=Y Echo

**Branch**: `032-initiator-reset-outbound-advance` | **Date**: 2026-06-11 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/032-initiator-reset-outbound-advance/spec.md`

## Summary

Conformance bug fix for merged `024` (catalogue **S-017**, the `ResetOn{Logon,Logout,Disconnect}`
knobs), tracked as **L-024-2**, found on the **first live run** of the `RL-{QFcpp,QFj}-init-fix44-reset-on-logon`
interop cells (the in-process unit pin asserts the divergent value; parallels `030`/`031`). When a
fixpp **initiator** reset its sequence numbers before logon (a reset knob fired in
`emit_initiator_logon_`, `session.cpp:681`) it emits `Logon(141=Y)` at the post-reset sequence `1`,
so its next-outbound advances to `2`. A conformant peer **echoes** `141=Y` in its Logon-ack. On the
initiator `peer_ack_sent_reset_flag` arm (`session.cpp:3185`) fixpp calls
`reset_seqnums_to_one_durable` (`:3195`), which rewinds **both** counters to `1`. The `030` fix
restored the **inbound** counter on this arm (`:3210`, next-inbound → `2` because the peer's consumed
seq-1 reset-ack Logon is a surviving net-advance) but left the **outbound** counter rewound to `1`.
Because fixpp's own reset Logon already consumed sequence `1`, the next outbound frame is then emitted
at `34=1` again — a duplicate sequence number; QuickFIX-cpp v1.16.0 and QuickFIX-J 3.0.1 both reject
("MsgSeqNum too low") and the session cannot carry traffic.

A second, adjacent defect on the same arm: the `sequence_numbers_reset` event's `by_peer_request`
classification is driven by `we_initiated = (policy == bilateral_strict)` (`:3224`), so the
fixpp-initiated `reset_on_logon` + non-strict-policy path is mislabeled `by_peer_request=true`
(per /clarify Q2, corrected here).

**Fix (technical approach, grounded in both reference engines — research.md R1/R2)**: latch the
literal emit-time `141=Y`-emit fact, and gate the ack-arm restore on it plus a seq-1 observable. At the
emit site (`session.cpp:721`, where the Logon's `initr_reset_seqnum` is computed), latch
`own_logon_sent_reset_flag_ := initr_reset_seqnum` — a new private strand-confined `Session` member,
set when the initiator Logon is built, read on the `peer_ack_sent_reset_flag` arm, consumed + cleared
one-shot after the Logon-ack. On the arm, capture the **pre-reset** next-outbound
`n_pre_outbound = peek_outbound()` BEFORE `reset_seqnums_to_one_durable`, define
`reset_before_send := (n_pre_outbound == seqnum_min + 1)`, and restore outbound to `seqnum_min + 1`
(`=2`) after the reset **iff**

```
restore_outbound := own_logon_sent_reset_flag_ && reset_before_send
                 := initr_reset_seqnum (latched at emit) && (n_pre_outbound == seqnum_min+1)
by_peer_request  := !own_logon_sent_reset_flag_      // event label — latch ONLY, distinct gate
```

(manager-first, store-second, mirroring the `030` inbound restore and its INV-H1 lower-bound +
fatal-when-persistent discipline.)

**Why latch, not reconstruct (RC1 / Codex #1 + New-P3).** The earlier draft reconstructed
`we_initiated := bilateral_strict || (any_reset_knob && reset_before_send)` on the arm. That is
**strictly weaker** than the real emit predicate `initr_reset_seqnum = bilateral_strict ||
(any_reset_knob && seqnums_at_one)`, because `seqnums_at_one` (`:719-720`) requires BOTH outbound==1
AND **inbound**==1 at emit, while `reset_before_send` proves only outbound==1 — the reconstruction
drops the inbound-at-1 conjunct. A hydrated initiator `{next_inbound=37, next_outbound=1}` +
`reset_on_logout=true` (non-strict, `reset_on_logon=false`) emits its Logon with **no `141=Y`** (inbound≠1
at emit); if the peer then sends a spontaneous in-sequence `141=Y` ack, the reconstruction sees
`any_reset_knob && n_pre_outbound==2`, **misclassifies it as fixpp-initiated**, restores outbound to 2
and emits `by_peer_request=false` — the exact FR-005/FR-006 regression. Latching the literal
`initr_reset_seqnum` value at emit carries the inbound-at-1 conjunct for free and dissolves the hole.

**The two gates are DISTINCT.** `restore_outbound` is `latch && reset_before_send`; `by_peer_request`
reads the latch ALONE. The `reset_before_send` conjunct is **load-bearing** in `restore_outbound`
(necessary but not sufficient): the latch alone is true for `bilateral_strict`-at-N (via the policy
branch), whose Logon went at seq 10 — restoring there regresses `BilateralStrict_Initiator_CountersResetToOne`
(W4). It is also true for a **fresh** `bilateral_strict` at `{1,1}` where the restore IS wanted;
`reset_before_send` separates the two. The latch being **false** excludes both the fresh
peer-spontaneous-at-seq-1 case (W7) and the hydrated inbound≠1 case (W8). The label diverges from the
restore exactly on `bilateral_strict`-at-N (`by_peer_request=false`, no restore) — do NOT collapse them.

The **mechanism is resolved at Gate A to restore-after-reset (Mechanism A, the proven `030` pattern)**.
The skip-the-redundant-reset alternative (Mechanism B) is **dropped**: it is unsound for the fresh
`bilateral_strict`-at-`{1,1}` row that this gate admits as `restore_outbound=true` — the open-time reset
gate `session.cpp:681` fires on `reset_on_logon` ONLY, so `bilateral_strict` never ran the open-time
durable reset; its only durable reset on the whole path is the ack-arm reset Mechanism B would skip
([[feedback_unconditional_persist_at_multiexit_gate_breaks_lowerbound]], research.md R4).

## Technical Context

**Language/Version**: C++23 (Clang; asio awaitables, `std::expected`) — [const §II]
**Primary Dependencies**: `Session::on_inbound_frame` LogonSent initiator arm
(`src/session/session.cpp:3185` `peer_ack_sent_reset_flag` block) + the emit site (`:721`) where the
latch is set. Reused existing helpers: `seqnum_mgr_.peek_outbound()` (capture `n_pre_outbound`),
`reset_seqnums_to_one_durable` (`:505`, `030` fatal-when-persistent disposition unchanged), `seqnum_min`
constant. **NEW private/internal methods** (no existing outbound twin — verified against
`seqnum_manager.hpp`/`.cpp` + `session.hpp`): `SeqnumManager::set_next_outbound(seqnum_t)` (mirrors
`set_next_inbound`; there is NO existing `set_next_outbound`) + `Session::persist_outbound_advance_()`
(mirrors `029`'s `persist_inbound_advance_()`; there is NO existing outbound-persist path —
`assign_outbound` advances in-memory but does NOT persist). The `by_peer_request` classification
(`:3224`) reads the latched `own_logon_sent_reset_flag_` (a new private `Session` member) instead of the
`bilateral_strict`-only `we_initiated`. No new deps, no codegen, no wire field, no new error slot, no
config knob, no FSM state, no PUBLIC/C-ABI surface.
**Storage**: `MessageStore` interface UNCHANGED (4 pure-virtual cap preserved). The restore writes the
corrected outbound counter via the NEW private `persist_outbound_advance_()` (`030` fatal-when-persistent
disposition); INV-H1 (store ≤ manager) and the `029` hydrate spine are preserved — `store_outbound ==
manager_outbound == 2` is equality, not over-persist (the consumed seq-1 Logon is a surviving net-advance,
the outbound twin of `030`'s inbound argument). (Mechanism B / skip-reset is dropped — it is unsound for
fresh `bilateral_strict`-at-`{1,1}`; see research.md R4.)
**Testing**: GoogleTest; ASan/UBSan/TSan; coverage llvm-cov. Witnesses (research.md R5):
**W1** reset_on_logon initiator + peer `141=Y` echo ⇒ Active AND `peek_outbound()==2` (flip the
committed `DISABLED_ResetOnLogon_Initiator_PeerAck141_OutboundStaysTwo` to enabled; RED-on-main
already verified) **+ EXTENDED (P2#4 / SC-002): originate a post-Active outbound frame, capture the
emitted bytes, assert wire `34=2` with no second frame at `34=1`** (the committed test asserts only
counters — the counter is a supporting check, NOT the wire proxy,
[[feedback_witness_asserts_named_postcondition_not_proxy]]); **W2** the `by_peer_request=false`
classification on the reset_on_logon path (RED on main = today `true`), gating on the latch;
**W3** peer-spontaneous, Logon at `N>1` (fixpp sent no `141=Y`) ⇒ outbound UNCHANGED from baseline +
`by_peer_request=true` (non-regression); **W4** `bilateral_strict`-at-seq-`N`
(`BilateralStrict_Initiator_CountersResetToOne`) ⇒ outbound stays `1` UNCHANGED (must NOT flip);
**W5** persistent-store reset-failure fatal-when-persistent unchanged (`030` discipline); **W6**
fault/INV-H1 witness: persistent store, after the restore `store_outbound == manager_outbound`, no
`store > manager`; **W7** (discriminating witness for the latch conjunct) **fresh initiator, NO reset
knob, `bilateral_lenient`, default `{1,1}`, fixpp `Logon(34=1)` with no `141=Y`, peer spontaneously
sends `Logon(34=1, 141=Y)`** ⇒ outbound stays **`1`** + `by_peer_request=true` (RED under a
`reset_before_send`-alone gate, GREEN under `latch && reset_before_send` — latch false here);
**W8** (the latch's inbound-at-1 conjunct — RC1 / Codex #1 counterexample) **hydrated initiator
`{next_inbound=37, next_outbound=1}`, `reset_on_logout=true`, non-strict; Logon at `34=1` with no
`141=Y` (inbound≠1 at emit); peer sends spontaneous in-sequence `141=Y` ack** ⇒ outbound stays **`1`** +
`by_peer_request=true` (RED under the dropped counter-reconstruction, GREEN under the latch).
Live `RL-*-init` cells re-run vs QFcpp/QFJ (SC-003 close-out). — [const §VII, §IX]
**Target Platform**: Linux/Clang Tier-1 (sanitizer matrix); the live cells run vs QFcpp/QFJ in the
parent `phase-9-harness`.
**Project Type**: single C++ library (`fixpp`) + tests + interop-harness extension.
**Performance Goals**: no hot-path change; one extra in-memory counter capture + a conditional
restore on the cold/rare initiator reset-Logon-ack path; default knob-off + acceptor paths untouched.
**Constraints**: `noexcept`/`expected_t` preserved; no new include into the `session.hpp` awaitable
closure ([const §XV.9] N/A, confirm at verify); acceptor role, knob-off default, peer-spontaneous,
and `bilateral_strict`-at-`N` paths **byte-identical**.
**Scale/Scope**: one emit-time latch (`own_logon_sent_reset_flag_` set at `:721`, cleared one-shot on
the arm) + one `n_pre_outbound` capture + one guarded outbound restore (new `set_next_outbound` +
`persist_outbound_advance_`) + the `by_peer_request` read switched to the latch on the initiator arm
(~14–20 effective LoC + comments + the two private method bodies, symmetric to `030`'s inbound restore);
net-new/amended witnesses in `tests/session/test_persistent_seqnum_hydrate.cpp` +
`tests/session/test_reset_seqnum_policy_matrix.cpp` + re-verified `030` pins in
`tests/session/test_reset_on_lifecycle.cpp` + `tests/session/test_refresh_on_logon.cpp` (W6); a
blast-radius pin set (R4) confirmed at /analyze under a clean-build FULL ctest. Private session/manager
header change (no FSM state, store interface, config, codegen, C-ABI, or wire/public change).

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked after Phase 1.*

| Article | Gate | Status |
|---------|------|--------|
| **II** Language | C++23/Clang, no new deps | ✅ PASS |
| **VI** Spec coverage | **No net-new catalogue row** — corrects behavior owned by `024` under existing row **S-017** (`ResetOn{Logon,Logout,Disconnect}`). Amend S-017 Notes AND **S-032 + coverage-index §4.4.2** (the row that owns the cited `[FIX-SL §4.4.2]` `ResetSeqNumFlag(141)`; `030` amended S-017/S-031/S-032 — updating only S-017 leaves the §4.4.2 trace stale) for the ResetSeqNumFlag ack-arm OUTBOUND restore + `by_peer_request` semantics. `spec.md` carries **Normative References** (`[FIX-SL §4.4.2]`) per §VI.5. Exact §VI delta below (Polish). | ⚠ RESOLVED (delta specified) |
| **VII** Testing/TDD | RED-first: W1 (Active + outbound==2 + **extended: originate a post-Active frame, assert wire `34=2`, no dup `34=1`** — P2#4); W2 (`by_peer_request=false`, RED on main); W3/W4 (peer-spontaneous-at-N + bilateral_strict-at-N non-regression); **W7** (fresh-no-knob peer-spontaneous-at-seq-1 ⇒ outbound stays `1`, the latch conjunct, RED under `reset_before_send`-alone); **W8** (hydrated reset_on_logout inbound≠1-at-emit ⇒ outbound stays `1` — the latch's inbound-at-1 conjunct, RED under the dropped reconstruction); W5/W6 (persistent fatal + INV-H1). **W7/W8 are GREEN-on-main AND green-post-fix → discriminating power confirmed by a MUTATION step at implement (drop the latch conjunct → W7 RED; use the reconstruction → W8 RED), [[feedback_symmetric_api_claim_unreachable_arm]].** | ✅ planned |
| **VII.6** Interop | live `RL-{QFcpp,QFj}-init-fix44-reset-on-logon` cells: reset_on_logon initiator ⇒ session establishes + carries one post-logon frame at `34=2`, peer does NOT Logout-reject (SC-003 close-out); flip from `deferred:initiator-141echo-outbound-rebase` | ✅ planned |
| **VIII.5** Allocator | sequence-counter correction; no new container/frame/allocation; existing no-heap witnesses on the reset path stay green | ✅ PASS (no new alloc) |
| **IX.1** Coverage | ≥95/85 on the new guarded restore branch — the `own_logon_sent_reset_flag_ && reset_before_send` TRUE arm AND both FALSE sub-arms (`reset_before_send` false = bilateral_strict-at-N; latch false = fresh peer-spontaneous-at-seq-1 / hydrated inbound≠1) — + the latch set at `:721` and the `by_peer_request` read at the arm | ✅ planned |
| **IX.2** Sanitizers | ASan/UBSan/TSan on the initiator reset-echo path + amended/new tests + interop ctest | ✅ planned |
| **X** ABI | no C-ABI/error-slot/wire/public change. Adds two **private/internal** methods (`SeqnumManager::set_next_outbound`, `Session::persist_outbound_advance_`) + one private `Session` member (`own_logon_sent_reset_flag_`) — a private header change → source rebuild; no public ABI surface (FR-009 holds: no NEW wire/error-slot/codegen/C-ABI/config) | ✅ private header change (no public ABI surface) |
| **XI.4** Threading | the capture + restore run on the existing session strand inside the inbound Logon handler; no new concurrency surface | ✅ PASS |
| **FR-009** No new config surface | **no new config flag** — gated by the pre-existing reset knobs + the latched emit-time fact + the observable `n_pre_outbound == seqnum_min+1`; no behavior changes without an explicit existing knob. (Basis is FR-009's no-new-public-surface / ABI-surface constraint, NOT Article XII.5 — XII.5 is the `SecurityProfile` construction rule and SecurityProfile is not touched here.) | ✅ PASS (no new knob) |
| **XIV.2** Pluggable ≤5 pure-virtual | `MessageStore` untouched | ✅ PASS |
| **XV.9** Banned (`std::mutex` in awaitable hdr) | no new include; the arm is already in the awaitable corpus | ✅ N/A (confirm at verify) |
| **XVI.3/4** /clarify before /plan | Session 2026-06-11 — two user decisions recorded (coverage → all reset-before-send paths; event label → also fixed); trigger grounded by the emit-side `initr_reset_seqnum` predicate + the QFcpp/QFJ reset-then-increment sweep (research.md R1/R2) | ✅ PASS |
| **XVII.1** Gate A before /tasks | Gate A round 1 converged; findings applied to this bundle (see `## Gate A` below). Next = `/speckit-tasks` | ✅ Gate A round 1 applied |

**Result**: PASS to proceed. No new config/ABI/wire/interface/store surface; the acceptor role, the
knob-off default, the peer-spontaneous path, and `bilateral_strict`-at-`N` are byte-identical
(research.md R3); only the initiator reset-before-send outbound counter + its event label are
corrected. The mechanism is resolved (A) at Gate A; see below.

### Resolved at Gate A (round 1)

1. **Mechanism — RESOLVED to A (restore-after-reset); B DROPPED.** The design is restore-after-reset
   (capture `n_pre_outbound`, restore outbound to `seqnum_min+1` after `reset_seqnums_to_one_durable`
   when `restore_outbound`; manager-first/store-second, `030` pattern). Mechanism B
   (skip-the-redundant-reset) is **dropped** — it is unsound for the fresh `bilateral_strict`-at-`{1,1}`
   row this gate admits as `restore_outbound=true`: the open-time reset gate `session.cpp:681` fires on
   `reset_on_logon` ONLY, so `bilateral_strict` never ran the open-time durable reset, and its only
   durable reset on the path is the ack-arm reset B would skip (Codex #2 / RC2). **The restore gate is
   `own_logon_sent_reset_flag_ && reset_before_send`** (the latched emit-time fact AND the seq-1
   observable) — NOT a config+counter reconstruction (which drops the inbound-at-1 conjunct and
   misclassifies the hydrated row, Codex #1 / RC1) and NOT `reset_before_send` alone (which would
   restore the fresh peer-spontaneous-at-1 case, W7). `by_peer_request := !own_logon_sent_reset_flag_`
   is a DISTINCT gate (latch only). The `030` half-fix lesson in its sharper form.
2. **The outbound restore primitives — RESOLVED (RC2 storage half / Codex #3).** Verified there is NO
   existing outbound twin: `SeqnumManager` has `assign_outbound`/`hydrate`/`reset_to_one`/`set_next_inbound`
   only (NO `set_next_outbound`); `Session` has `persist_inbound_advance_` only (NO
   `persist_outbound_advance_`; `assign_outbound` advances in-memory but does NOT persist). The fix ADDS
   two narrowly-scoped **private/internal** methods, named as /tasks deliverables: (a)
   `SeqnumManager::set_next_outbound(seqnum_t)` mirroring `set_next_inbound`'s shape (force in-memory;
   store write is the caller's responsibility); (b) `Session::persist_outbound_advance_()` mirroring
   `029`'s `persist_inbound_advance_()`. Failure disposition = the `030` **fatal-when-persistent** now
   (store failure on a persistent store → `Disconnected` + propagate; INV-H4 no-op on null/non-persistent).
   These are private header changes (source rebuild), NOT a public/wire/C-ABI surface — FR-009 holds;
   the Constitution-Check X row is reconciled accordingly. INV-H1 (`store_outbound ≤ manager_outbound`;
   equality at `2` a surviving net-advance, not over-persist) confirmed.
3. **Blast-radius pin set (R4) — WIDENED (Codex #6 + New-P2).** Enumerate exactly at
   `/speckit-tasks`/`/analyze` under a **clean-build FULL ctest** (not incremental — the `030` close-out
   undercounted by 2 pins on this same arm). The set now spans FOUR test files, not two:
   - `test_persistent_seqnum_hydrate.cpp`: the committed `DISABLED_` harm test (flips enabled + green +
     EXTENDED to originate a post-Active frame, assert wire `34=2` — P2#4); **`INV_H1_Initiator_PeerAck141_NoOverPersist`**
     (`:2096`, the `029/030` initiator INV-H1 twin — adding a second durable write on this arm is exactly
     what can break an INV-H1 pin, New-P2).
   - `test_reset_on_lifecycle.cpp` (Codex #6 — the `030` initiator-arm witnesses live HERE on the SAME
     arm): `Initiator_Received141Ack_NextInboundTwo_NoResend` (`:1906`),
     `Initiator_Received141Ack_PersistentStore_StoreEqualsManagerTwo` (`:1952`),
     `Initiator_Received141Ack_PersistentStore_ResetFailure_Disconnects` (`:1985`),
     `Initiator_Received141Ack_GuardSkipsWhenNoConsumedReset` (`:2036`). Add outbound + event-label
     assertions where the arm is exercised, or an explicit inbound-only justification per pin.
   - `test_refresh_on_logon.cpp`: **`W6_Acceptor_KnobOn_PeerResetLogon_InboundSeedWithheld`** (`:1196`) —
     `030` flipped its store-interaction **call_count** (2→3), invisible to a value-grep; an added
     outbound store write on the shared reset path can shift such a count (New-P2). Re-run + confirm.
   - `test_reset_seqnum_policy_matrix.cpp`: `BilateralStrict_Initiator_CountersResetToOne` (outbound `1`,
     Logon at seq 10) + any peer-spontaneous pin — MUST stay green **unchanged**.
   Confirm — do not assume; pre-judging risks the `030` regression.

**Exact §VI delta (applied at Polish):**
- `spec/feature-catalogue.md`: amend the **S-017** row (`ResetOn{Logon,Logout,Disconnect}`) Notes to
  cite `032-initiator-reset-outbound-advance` as the initiator reset-echo conformance correction
  (after a peer echoes fixpp's own `141=Y`, the outbound counter is restored to `2` because the reset
  Logon consumed seq `1`; the `by_peer_request` classification reflects fixpp-initiated; the acceptor
  role, knob-off, peer-spontaneous, and `bilateral_strict`-at-`N` paths unchanged). **AND amend the
  S-032 row** (the row that owns `[FIX-SL §4.4.2]` `ResetSeqNumFlag(141)`; `030` amended S-017/S-031/S-032
  — Codex #5) to cite `032` for the initiator ack-arm OUTBOUND restore + `by_peer_request` semantics
  (the outbound twin of `030`'s inbound correction). No new S-row.
- `spec/coverage-index.md`: amend **§4.4.2** (mapped to S-032 — Codex #5) for the `032` initiator
  ack-arm outbound-restore + latched-`by_peer_request` semantics, and map the guarded outbound-restore
  branch + the latch ↔ `tests/session/test_persistent_seqnum_hydrate.cpp` (W1/W2/W5/W6/W8) +
  `test_reset_seqnum_policy_matrix.cpp` (W3/W4/W7) + the re-verified `030` pins in
  `test_reset_on_lifecycle.cpp` + `test_refresh_on_logon.cpp` under the existing 024/S-032 entry.
- `spec/behaviors-and-limitations.md`: flip **L-024-2** to RESOLVED and add **B-032-1** (initiator
  reset-echo: a peer's `141=Y` echo of fixpp's own deliberate reset restores outbound to `2`, no
  duplicate-seq frame; reference-engine-conformant; covers all reset knobs).
- **Obsolete-prose grep-sweep** (one exhaustive pass, [[feedback_verify_caught_design_pivot_stale_doc_bundle_drift]]):
  amend any `024`/`030` comment/doc framing the initiator reset arm as resetting outbound to `1`, or
  the `we_initiated = bilateral_strict` comment (`session.cpp:3220-3225`) — the `by_peer_request` read
  now uses the latched `own_logon_sent_reset_flag_`, not the policy flag — or the `030` "no reply Logon
  on this arm" comment region (`:3202-3209`) that omits the outbound twin. Needles:
  `next_outbound.*1` near the initiator arm, `peer_ack_sent_reset_flag`, `we_initiated`,
  `BilateralStrict_Initiator_CountersResetToOne` rationale comment, L-024-2 prose.

## Project Structure

### Documentation (this feature)

```text
specs/032-initiator-reset-outbound-advance/
├── plan.md              # this file
├── spec.md              # /speckit-specify + /speckit-clarify output
├── research.md          # Phase 0 — reference-engine oracle + observable-trigger derivation + mechanism options
├── data-model.md        # Phase 1 — the reset-arm counter states + the fixpp-initiated predicate
├── quickstart.md        # Phase 1 — how to reproduce + validate (unit RED + live cells)
├── contracts/
│   └── initiator-reset-echo.md   # peer_ack_sent_reset_flag arm: outbound restore + event-label contract
├── checklists/
│   └── requirements.md  # spec-quality checklist (GREEN)
└── tasks.md             # Phase 2 — /speckit-tasks output (NOT created here)
```

### Source Code (repository root = library submodule)

```text
src/session/session.cpp        # emit-site latch own_logon_sent_reset_flag_ (:721); peer_ack_sent_reset_flag arm (:3185): n_pre capture + guarded outbound restore + by_peer_request reads the latch (:3224)
include/fixpp/session/session.hpp        # NEW: own_logon_sent_reset_flag_ member + persist_outbound_advance_() decl (private header change)
include/fixpp/session/seqnum_manager.hpp # NEW: set_next_outbound(seqnum_t) decl (mirrors set_next_inbound; private)
src/session/seqnum_manager.cpp           # set_next_outbound body
tests/session/test_persistent_seqnum_hydrate.cpp     # W1 (enable + EXTEND DISABLED harm test, wire 34=2) + W2/W5/W6/W8 + INV_H1_Initiator_PeerAck141_NoOverPersist re-verify
tests/session/test_reset_seqnum_policy_matrix.cpp    # W3 peer-spontaneous + W4 bilateral_strict-at-N + W7 fresh-no-knob non-regression
tests/session/test_reset_on_lifecycle.cpp            # 030 initiator-arm pins re-verified on this SAME arm (Codex #6): Initiator_Received141Ack_* (:1906/:1952/:1985/:2036)
tests/session/test_refresh_on_logon.cpp              # W6_Acceptor_KnobOn_PeerResetLogon_InboundSeedWithheld (:1196) call_count re-verify (New-P2)
tests/interop/happy/hp_fix44_reset_on_logon_test.cpp # RL-*-init witness hardening (live close-out; confirm exact file at /tasks)
```

**Structure Decision**: in-place fix on the existing `024`/`030` initiator reset arm; no new modules,
files-of-record only as listed. Tests extend the existing `024`/`029`/`030` unit + the `RL-*` interop
suites.

## Complexity Tracking

> No constitution violations. The mechanism is resolved to A (restore-after-reset; research.md R4); it
> introduces one private latch member + two private setter/persist methods mirroring the `030` inbound
> twin — no new public abstraction. It captures the emit-time fact, then conditionally threads the
> corrected counter back through the new private setters, exactly as `030` did for the inbound twin.

## Gate A

*(Runs after this plan, before `/speckit-tasks` — [const §XVII.1]. Record the convergence + sign-off here.)*

- Round 1 applied 2026-06-11: Codex P1=2 P2=4 P3=1; Opus post-judging P1=2 P2=5 P3=2; rewrite addresses root causes RC1 (latch emit fact, drop reconstruction) + RC2 (Mechanism A only, specify outbound primitive) + RC3 (traceability S-032/coverage + test_reset_on_lifecycle + XII.5 cite). Reviews: research/reviews/codex_032-initiator-reset-outbound-advance_gate_a_review.md, research/reviews/opus_032-initiator-reset-outbound-advance_gate_a_adversarial_review.md.

### Round 1 — disagreements

- None. All 7 Codex findings were confirmed by Opus; both Opus new findings (New-P2 blast radius, New-P3 FR-006 wording) applied. No Disagree dispositions.
