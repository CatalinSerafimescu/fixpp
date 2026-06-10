# Phase 0 Research: Received-Reset Inbound Advance Correction

## R-1 — Oracle behavior (reference-engine source sweep)

**Decision**: After consuming a peer `Logon(141=Y)`, next-expected-inbound MUST be 2, and
(027-on) the reply Logon MUST advertise `789=2`. The conformant order is **reset-then-increment**.

**Rationale / evidence** (also recorded in spec `## Clarifications`):

- **QuickFIX-cpp** `src/C++/Session.cpp::nextLogon`:
  - `~206`: on received reset (and not already sent), `m_state.reset()` rebases both counters to 1 — **before** the consume.
  - `~253-267`: reads the incoming Logon's `MsgSeqNum`; because `resetSeqNumFlag` is set, the `isTargetTooHigh && !resetSeqNumFlag` branch is skipped and the **else** runs `m_state.incrNextTargetMsgSeqNum()` → target 1→2.
  - `~710` (generateLogon 789 path): `getExpectedTargetNum() + 1` with comment *"+1 because incoming Logon did not increment the target SeqNum yet"* → advertises 2.
- **QuickFIX-J** `quickfixj-core/.../Session.java::nextLogon`:
  - `2200-2206`: explicitly **infers** ResetSeqNumFlag when `MsgSeqNum == 1` — *"Inferring ResetSeqNumFlag as sequence number is 1 in response to reset request"* (QFJ-383). This directly grounds the assumption that a reset Logon carries `MsgSeqNum=1`.
  - `2212-2217`: on reset received, `resetState()` rebases to 1.
  - `2266-2278`: 789 advertisement computes `nextTarget(1)` then `++` when in normal sequence → 2; comment *"we always send 2 ... we haven't inc'd for current message yet +1"*.
  - `2302-2304`: in-sequence else-branch runs `state.incrNextTargetMsgSeqNum()` → target 1→2.

**Alternatives considered**: treat fixpp's reset-to-1 (net next-expected=1) as an acceptable
divergence. **Rejected** — both reference engines (in production use) and the FIX session
semantics net 2; fixpp emits a spurious ResendRequest, an observable conformance failure
found in a live cell.

## R-2 — Root cause: increment-then-reset (wrong order) with no re-increment

**Decision**: fixpp's defect is ordering. `check_inbound` (the in-memory gate) advances
next-expected-inbound 1→2 for the consumed reset Logon **first**; then
`reset_seqnums_to_one_durable()` rebases both counters to 1 with no re-increment. Net = 1.

**Rationale**: the 024 design deliberately placed `reset_seqnums_to_one_durable()` **after**
`check_inbound` to keep the **outbound reply** stamped seq 1 (byte-identity, FR-001). That is
correct for the outbound counter. But the same reset also clobbers the **inbound** advance,
which is a separate counter that should remain at 2. The two counters were conflated.

**Fix shape**: leave the order (outbound stays re-based → reply seq 1), but **restore the
inbound counter** to `seqnum_min + 1` in BOTH the in-memory manager AND the durable store
after the reset, guarded on the reset Logon actually having been consumed
(`logon_inbound_advanced`). This reproduces the engines' net result without moving the reset
(so outbound byte-identity is preserved).

**Restore placement (load-bearing — implementation constraint)**: on the acceptor arm the
restore+persist MUST land **between the reset call (`session.cpp:1942`, block ends `:1947`) and
the 789 read (`:1974`)**, both inside the same `{}` block, so the 789 advertisement reflects the
corrected counter. A naive copy of the post-Active normal persist site (`:2040`) would pass the counter
+ harm-repro witnesses but the reply Logon's `789` was already built reading the un-restored
value → it silently stays 1, failing FR-004 / US3 / the discriminating triple's `reply.789==2`
clause. Use a **dedicated restore + persist on the received-141 arm**; do NOT loosen the
029-fixed net-advance persist guard at `:2039` (loosening it would re-open the 029 over-persist
class on other sub-paths).

## R-3 — Persist-to-2 restore (INV-H1 equality, guaranteed by the scoped-fatal reset — see R-8)

**Decision**: restore via `SeqnumManager::set_next_inbound(seqnum_min + 1)` (in-memory) PLUS,
**on a persistent store**, one `next_seqnum(inbound, increment=true)` (durable write-through, the
`persist_inbound_advance_` equivalent, a no-op when `!store_is_persistent_` per INV-H4) on the
received-141 arm, giving **`store == manager == 2`** on a persistent store. The durable store is
brought up to 2 to match the manager — this is correct write-through for the consumed seq-1 reset
Logon, identical to how any other in-sequence Logon persists at the normal site (`:2039`), and
matches QuickFIX/QuickFIX-J FileStore write-through (`incrNextTargetMsgSeqNum` persists).

**The equality is GUARANTEED by a scoped-fatal reset, not asserted on faith (Gate-A round-2 P1
fix — see R-8)**: the shared helper `reset_seqnums_to_one_durable` swallows a `store_->reset()`
failure on `reset_disposition::logged` (`session.cpp:527-528`), so under the original `logged`
disposition a failed durable reset would leave the store at stale-N while the manager reached the
reset base; the subsequent persist-to-2 write-through would then advance the **stale** store
N→N+1, giving `store(N+1) > manager(2)` for any N>1 — the 029 over-persist / silent-inbound-skip
harm. To make `store == manager == 2` actually hold, the received-141 durable reset is made
**fatal when the store is persistent** (`store_is_persistent_ ? fatal : logged`): persist-to-2
runs only after a known-good reset; a persistent-store reset failure DISCONNECTS (no inconsistent
durable state). See R-8 for the full ruling and rejected alternatives.

**Gate-A round-1 reconcile (029 over-persist lesson — resolves the OPPOSITE way here)**:
029's [[feedback_unconditional_persist_at_multiexit_gate_breaks_lowerbound]] forbade persisting
at a multi-exit Logon gate **when there is no surviving net-advance** (`durable > manager`,
silent inbound skip on restart). The discriminating question is *"is there a surviving
net-advance?"* — here the answer is **YES**: the consumed seq-1 reset Logon is a real advance
that survives the reset. So `store == manager` is **equality, not `durable > manager`
over-persist** — *provided the durable reset actually succeeded*. The 029 lesson does not apply to
the success path; but it **does** re-appear on a swallowed-reset-failure path, which is exactly why
the reset is made fatal-when-persistent (R-8). With the fatal flip, INV-H1 (`store ≤ manager`)
holds with equality on every reachable persistent-store path.

A **manager-only** restore (store left at `seqnum_min`) was the bundle's original (v1) design
and is **wrong**: the acceptor knob-off Logon gate has **no ResendRequest arm**
(`session.cpp:1789-1793`: the only non-fatal too-high branch is the 027 behind-side tolerance;
knob-off too-high falls to `record_state_transition_(Disconnected)` — pinned by
`test_persistent_seqnum_hydrate.cpp:1509-1512`). So with store=1/manager=2, on restart hydrate
seeds 1, the peer's next non-reset message at seq 2 reads too-high → **fatal disconnect**. The
`persist_inbound_advance_` guard at `:2039` (`logon_inbound_advanced && !peer_sent_reset &&
!cfg_.reset_on_logon`) excludes `peer_sent_reset`, so the normal site does NOT persist this
Logon — hence the dedicated restore+persist on the arm. Manager-only is a half-fix that
re-opens the exact T034 inbound-persistence gap 029 closed, localized to this path, trading a
spurious-ResendRequest for a fatal-disconnect-on-restart.

The witness MUST assert `store.durable_inbound == seqnum_min + 1` (== 2) and `store == manager`
**directly** (not via a manager proxy — the 029 W9b lesson,
[[feedback_witness_asserts_named_postcondition_not_proxy]]).

**Alternatives considered**: (a) move the reset before `check_inbound` (mirror QFcpp order
exactly) — **rejected**, larger blast radius and risks the outbound seq-1 byte-identity that
024 pinned; (b) manager-only (store stays 1) — **rejected** (fatal-disconnect-on-restart, above).
For the *swallowed-reset-failure* soundness question (keep `logged` vs scoped-fatal vs gate-and-
document), see R-8.

## R-4 — Outbound / 789 coupling (not a flat "byte-identical")

**Decision**: state the outbound invariant as a **pair**: reply Logon `MsgSeqNum` is
byte-identical (seq 1, unchanged); reply `NextExpectedMsgSeqNum(789)` *content* corrects
1→2, but **only when 027 advertisement is enabled**. The 789 read derives from
`next_inbound_unsafe()`, which now returns 2, so the advertisement corrects automatically
once the manager is restored — no separate 789 code change.

**Rationale**: the discriminating witness is the **triple** (next_inbound==2 AND
reply.MsgSeqNum==1 AND reply.789==2, 027-on). A flat "outbound byte-identical" claim would
be false and would (correctly) trip Gate B; a "next_inbound==2"-only witness would miss the
789 coupling. (Advisor-flagged; [[feedback_witness_asserts_named_postcondition_not_proxy]].)

## R-5 — Blast radius (7 pins: 6 value-pins + 1 contract-witness split)

**Decision**: 6 pre-existing tests pin the defective *value* and must flip, each individually
re-verified as pinning *this* off-by-one; plus a **7th pin in a different category** — a merged
024 *contract* witness whose behavior the R-8 fatal flip amends (see R-8 below). The 6 value-pins:

1. `test_reset_seqnum_policy_matrix.cpp` — `Bilateral{Strict,Lenient}` /
   `Unilateral_Acceptor_CountersResetToOne` (×3): `next_inbound` 1→2. (The asymmetry
   `next_inbound==1 && next_outbound==2` in `BilateralLenient_Acceptor_CountersResetToOne`
   IS the off-by-one smoking gun — outbound counted the reply, inbound didn't count the
   consumed Logon.)
2. `test_persistent_seqnum_hydrate.cpp` — `Acceptor_ResetLogon_InboundSeedWithheld_NoTooLowFatal`
   / W9b (×1 test, **two** sub-assertions, both flip under persist-to-2):
   `next_inbound == 1` (`:1587`) → 2, and `store->durable_inbound == 1` (`:1610`) → 2. The
   `EXPECT_LE(store->durable_inbound, manager_ni)` (`:1602`) stays satisfied at equality. The
   "reset won over hydrate" comment inverts — the consumed-Logon advance now *survives* the reset.
3. `test_next_expected_msgseqnum.cpp` — `Reset.AcceptorReplyReceived141_Advertises1` (`:1441`,
   ×1): advertise `789` 1→2; rename to `_Advertises2`.
4. `test_reset_seqnum_policy_matrix.cpp` — `BilateralStrict_Initiator_CountersResetToOne`
   (`:593-594`, ×1): `next_inbound` 1→2. This is the **initiator** off-by-one value-pin — its
   `EXPECT_EQ(next_inbound_unsafe(), seqnum_t{1})` ("bilateral_strict initiator: next_inbound_
   must be 1 after 141=Y mutual reset") drives the same `peer_ack_sent_reset_flag` initiator arm
   FR-009 corrects, so under 030 it flips 1→2 (matching the FR-009 initiator witness for the
   identical path). (Gate-A round-3 found this pin — see R-5's round-3 note below.)

**Rationale**: these encode the *current* behavior, which we now know is the defect. The
**initiator** received-141 off-by-one IS pinned — by `BilateralStrict_Initiator_CountersResetToOne`
(pin 4 above, flips 1→2), alongside the net-new FR-009 initiator witnesses. The exact file
paths/test names are confirmed via grep in `/speckit-tasks`.

**Gate-A round-1 resolved**: the 5 acceptor-side value-pins are pins of *this* off-by-one
(justified corrections), and the initiator received-141 arm is **in scope** (both arms fixed +
witnessed symmetrically — see R-7), per [[feedback_half_restructure_symmetric_api]]. (Round 1
recorded "no initiator pin"; Gate-A round 3 corrected that — the initiator IS pinned by
`BilateralStrict_Initiator_CountersResetToOne`, added as value-pin 4 above.)

**Gate-A round-2 — contract-witness split (the 7th pin)**: the merged 024 witness
`ResetOnLogon_Off_Inbound141_StoreFailure_StillActive` (`test_reset_on_lifecycle.cpp:531-558`)
pins a *behavior* (stay-Active under a swallowed store-reset failure), not a counter value, so it
is a separate category from the 6 value-pins. Its `StoreDoubleFactory` inherits the default
`yields_persistent_store()==true`, so the R-8 fatal-when-persistent flip changes its outcome
Active→Disconnected. It is **split**: the persistent variant flips to assert **Disconnect** (+
error propagated); a NEW non-persistent sibling (`yields_persistent_store()==false`) retains the
stay-Active characterization. Total = **7 pins**. This is the 024 FR-001/C2.6 I-07 contract
amendment for the persistent received-141 sub-case (see R-8).

**Gate-A round-3 — initiator value-pin**: `BilateralStrict_Initiator_CountersResetToOne`
(`test_reset_seqnum_policy_matrix.cpp:593-594`) is an EXISTING value-pin of the **initiator**
off-by-one — its `next_inbound_unsafe()==1` assertion drives the same `peer_ack_sent_reset_flag`
initiator arm FR-009 corrects, so under 030 it flips 1→2. Round 1's "no initiator pin" claim was
false; this is value-pin 4 above. Total taxonomy = **6 value-pins + 1 contract-witness split = 7 pins**.

## R-7 — Both arms in scope (acceptor + initiator), symmetric fix + symmetric witnesses

**Decision (Gate-A round-1)**: the initiator received-141 Logon-ack arm is a **separate,
reachable** code path with the identical clobber, and is **in scope** — fixed symmetrically and
witnessed on the initiator role. It is NOT deferred behind an L-030-1.

**Evidence**: the acceptor arm is the `NotConnected` Logon handler (`session.cpp:1697+`); the
initiator Logon-ack arm is distinct — `check_inbound` advances 1→2 at `:3119`, then
`peer_ack_sent_reset_flag` (`:3150`) gates `seqnum_mgr_.reset_to_one()` (`:3162`) +
`(*store_).reset()` with a **swallowed** `(void)store_rst_r` (`:3167-3169`) and no inbound restore
after. The arm is reachable: bilateral_strict initiator sends `141=Y` and a peer that acks `141=Y`
(`peer_ack_sent_reset_flag` true); bilateral-lenient / unilateral where the peer initiates the
reset. Same harm (spurious ResendRequest on peer seq-2). The in-code comment at `:3155` already
cites [[feedback_half_restructure_symmetric_api]] ("symmetric to acceptor arm") — confirming the
two are paired arms requiring symmetric fixes.

**Gate-A round-2 — consolidate the initiator onto the shared helper**: the initiator arm
hand-rolls the reset (`:3162` + `:3167-3169`) instead of calling `reset_seqnums_to_one_durable`.
Because R-8 makes the disposition scoped-fatal, the two arms MUST share one disposition primitive
(else one arm's policy could drift from the other — [[feedback_half_restructure_symmetric_api]]).
The fix replaces the hand-rolled `:3162`/`:3167-3169` block with a
`reset_seqnums_to_one_durable(store_is_persistent_ ? fatal : logged)` call + the symmetric guarded
restore+persist.

**Why not acceptor-only (the v1 plan's flagged-#2 mis-application)**: flagged-#2 cited 025's
L-025-2 unreachable-arm lesson ([[feedback_symmetric_api_claim_unreachable_arm]]) to license
acceptor-only scope. That precedent is **inapplicable** — L-025-2 licenses not *witnessing* a
structurally **unreachable** effect (fresh Session per accept, `hydrated_` never reset). This
arm is reachable with identical harm; the governing lesson is the half-restructure one (fix both
in one pass). Acceptor-only would knowingly ship a reachable conformance bug.

**Witness scope per role**: acceptor = the discriminating triple (next_inbound==2 AND
reply.MsgSeqNum==1 AND reply.789==2, 027-on) + INV-H1 (`store == manager == 2`, asserted on the
store directly). Initiator = next_inbound==2 + harm-repro (peer seq-2 accepted, no ResendRequest)
+ INV-H1. The `reply.789` clause is acceptor-reply-specific: the initiator already sent its Logon
before this handler runs and builds no reply Logon here (789-advertisement is acceptor-role on
this path; a later initiator re-advertise derives from the same corrected counter, per 027).

## R-6 — Provenance (prototype validation)

The fix was prototyped on `fix/acceptor-received-141-next-inbound` (HEAD 96cf43c, not merged):
manager-only `set_next_inbound(seqnum_min+1)` guarded by `logon_inbound_advanced`, +2 tests.
`session_reset_on_lifecycle` ran 22/22 green; harm was reproduced deterministically (peer
seq-2 → spurious ResendRequest before the fix). **Note**: the prototype's *manager-only* shape
is superseded by the persist-to-2 decision (R-3) — Gate A round 1 rejected manager-only as a
fatal-disconnect-on-restart half-fix; the merge path persists the store to 2 as well, and covers
the initiator arm (R-7). This pipeline re-derives it TDD-first and lands it with the pin updates,
full verify matrix, and Gate B. The prototype branch is a reference, not the merge path.

## R-8 — Scoped-fatal received-141 durable reset (persistent stores) — Gate-A round-2 P1

**Decision (user, Option (a))**: on a **persistent** store, the received-141 durable reset is
made **fatal** (`store_is_persistent_ ? reset_disposition::fatal : reset_disposition::logged`) on
BOTH arms, so the FR-005 persist-to-2 write-through only runs after a known-good reset.

**Problem (Gate-A round-2 P1, Codex + Opus judge CONFIRM)**: the original bundle claimed INV-H1
holds with equality `store == manager == 2` **unconditionally**. It does not. The shared helper
`reset_seqnums_to_one_durable` (`session.cpp:505-533`) propagates the in-memory
`seqnum_mgr_.reset_to_one()` on failure (`:511-513` → manager genuinely reaches {1,1}) but
**swallows** the durable `store_->reset()` failure on `reset_disposition::logged` (`:527-528`
`(void)store_rst_r`). The acceptor arm (`:1942`) uses `logged`; the initiator hand-rolls the same
swallow (`:3167-3169`). On a swallowed failure: **manager = {1,1}, store = stale-N**. Then
persist-to-2 (`set_next_inbound(2)` + `next_seqnum(inbound, true)`) sets manager = 2 and advances
the **stale** store N→N+1 → **`store(N+1) > manager(2)`** for any N>1: an INV-H1 (`store ≤
manager`) violation → durable > in-memory → **silent inbound skip on restart** (the 029
over-persist harm, [[feedback_unconditional_persist_at_multiexit_gate_breaks_lowerbound]]). The
planned INV-H1 witness asserted `store == manager == 2` only after a *successful* reset, so it
could not catch the fault — an un-witnessable false soundness claim.

**Why fatal-when-persistent (Option (a))**: a failed durable reset leaves the store stale; you
cannot reason about its value, so neither persisting on top of it nor skipping the persist is
sound. Making the reset fatal guarantees the reset succeeded before persist-to-2 runs, so
`store == manager == 2` truly holds; a reset failure DISCONNECTS — the session re-opens,
re-hydrates store=stale-N, and the peer re-drives the reset (no silent skip, no inconsistent
durable state). Aligns with 029 **D-3** ("inbound-correctness failures are fatal") and the
existing fatal reset sites at `session.cpp:682` (initiator knob) and `:1764` (acceptor knob) — the
received-141 arm was the only durable-reset site still on `logged`, and since persist-to-2 now
*depends* on the reset having succeeded, `logged` is the wrong choice for it.

**Rejected alternatives**:
- **(b) split the helper to skip the persist on store-reset failure** — does NOT restore INV-H1:
  the store is still stale-N while the manager is 2 → `store(N) > manager(2)` for N>2 (and >
  manager even without persist-to-2 for N>1). Skipping trades "store N+1" for "store N" — still an
  over-persist.
- **(c) gate on `store_is_persistent_` and document under an L-030-x** — fixes nothing. The
  durable write-through **already** no-ops when `!store_is_persistent_` (`:622-624`), so the
  over-persist fault exists **only** on persistent stores — exactly the case (c) would document
  rather than fix, writing a limitation that describes the precise silent-inbound-skip harm 029
  exists to prevent, on a path 030 actively persists to.

**Non-persistent stores are unaffected**: no durable counter (write-through no-ops, INV-H4), no
hydrate-on-restart (so no over-persist hazard), and the durable reset cannot meaningfully fail —
so the disposition stays `logged` and a received-141 reset never disconnects on a non-persistent
store.

**024 contract amendment + blast radius**: the fatal flip **amends the 024 FR-001/C2.6 I-07
logged-then-proceed contract** for the persistent received-141 sub-case (now disconnect, was
stay-Active) and **breaks merged witness (5)** `ResetOnLogon_Off_Inbound141_StoreFailure_StillActive`
(`test_reset_on_lifecycle.cpp:531-558`), whose default-persistent factory makes the flip hit it.
Blast radius is therefore **7 pins** (the 6 value-pins of R-5 + the witness-(5) split: persistent
variant → Disconnect, NEW non-persistent sibling → stay-Active). See R-5's round-2 note.

**Initiator consolidation**: the initiator arm is moved onto the shared
`reset_seqnums_to_one_durable(disposition)` helper (R-7) so both arms share the scoped-fatal
disposition — no hand-rolled copy that could drift ([[feedback_half_restructure_symmetric_api]]).

**Fault-injection witness (soundness proof, both arms)**: persistent store + `fail_next_reset()`
on the received-141 path ⇒ session **Disconnected** + store error propagated, persist-to-2 NOT
reached, no `store > manager` ever observable. This makes the "INV-H1 holds because the reset is
fatal" claim falsifiable — without it the claim is again unwitnessed (the 029 W9b proxy-gap lesson,
[[feedback_witness_asserts_named_postcondition_not_proxy]]).
