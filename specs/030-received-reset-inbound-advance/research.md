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
inbound counter** to `seqnum_min + 1` after the reset, guarded on the reset Logon actually
having been consumed (`logon_inbound_advanced`). This reproduces the engines' net result
without moving the reset (so outbound byte-identity is preserved).

## R-3 — Manager-only restore (INV-H1 lower-bound)

**Decision**: restore via `SeqnumManager::set_next_inbound(seqnum_min + 1)` — **in-memory
manager only, no durable persist**. The durable store stays at `seqnum_min` (written by the
preceding reset).

**Rationale**: 029's INV-H1 requires `durable ≤ manager`. Manager=2, store=1 satisfies it
(under-persist is safe — restart re-hydrates 1, and the 013 ResendRequest on the next Logon
reconciles). Persisting 2 is unnecessary and risks the over-persist class flagged in
[[feedback_unconditional_persist_at_multiexit_gate_breaks_lowerbound]] (029 Gate B P1×2).
The witness MUST assert `store.durable == seqnum_min` **directly** (not via a manager proxy —
the 029 W9b lesson, [[feedback_witness_asserts_named_postcondition_not_proxy]]).

**Alternatives considered**: (a) move the reset before `check_inbound` (mirror QFcpp order
exactly) — **rejected**, larger blast radius and risks the outbound seq-1 byte-identity that
024 pinned; (b) persist 2 durably — **rejected** per the over-persist lesson.

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

## R-5 — Blast radius (5 stale pins)

**Decision**: 5 pre-existing tests pin the defective value and must flip, each individually
re-verified as pinning *this* off-by-one:

1. `test_reset_seqnum_policy_matrix.cpp` — `Bilateral{Strict,Lenient}` /
   `Unilateral_Acceptor_CountersResetToOne` (×3): `next_inbound` 1→2. (The asymmetry
   `next_inbound==1 && next_outbound==2` in `BilateralLenient_Acceptor_CountersResetToOne`
   IS the off-by-one smoking gun — outbound counted the reply, inbound didn't count the
   consumed Logon.)
2. `test_persistent_seqnum_hydrate.cpp` — `Acceptor_ResetLogon_InboundSeedWithheld_NoTooLowFatal`
   (×1): the withheld-seed assertion must reflect next_inbound==2 post-consume.
3. `test_next_expected.cpp` — `Reset.AcceptorReplyReceived141_Advertises1` (×1): advertise
   `789` 1→2; rename to `_Advertises2`.

**Rationale**: these encode the *current* behavior, which we now know is the defect. The
exact file paths/test names are confirmed via grep in `/speckit-tasks` (the names above are
from the prototype-branch blast-radius mapping).

**Open for Gate A**: whether all 5 are pins of *this* case vs a distinct intended behavior;
the initiator-role scope of the correctness claim.

## R-6 — Provenance (prototype validation)

The fix was prototyped on `fix/acceptor-received-141-next-inbound` (HEAD 96cf43c, not merged):
manager-only `set_next_inbound(seqnum_min+1)` guarded by `logon_inbound_advanced`, +2 tests.
`session_reset_on_lifecycle` ran 22/22 green; harm was reproduced deterministically (peer
seq-2 → spurious ResendRequest before the fix). This pipeline re-derives it TDD-first and
lands it with the pin updates, full verify matrix, and Gate B. The prototype branch is a
reference, not the merge path.
