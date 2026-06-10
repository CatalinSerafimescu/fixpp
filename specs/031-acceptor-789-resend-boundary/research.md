# Research: Acceptor NextExpectedMsgSeqNum(789) Resend-Range Boundary Fix (031)

**Date**: 2026-06-10 · **Branch**: `031-acceptor-789-resend-boundary`

This resolves the one detail `spec.md` deferred (the genuine-gap resend endpoint) and pins
the conformance oracle line-by-line against both reference engines, plus the exact fixpp
defect site and the role asymmetry that constrains the fix shape.

---

## R1 — The defect (fixpp source, grounded)

**Site**: `src/session/session.cpp`, `Session::honor_peer_next_expected_` (`:4509`), reached
from two call sites:
- **Acceptor** (`:2027`) — invoked **after** the acceptor's reply Logon `store_then_emit`
  (`:2015`), per the deliberate 027 **RC#4 ordering** (`:2023-2030`: reply first, then honor).
- **Initiator** (`:3322`) — invoked on **receiving** the peer's reply Logon-ACK (no reply is
  emitted in response on this arm).

**The bug**: `honor_peer_next_expected_` reads `n789 = seqnum_mgr_.peek_outbound()` (`:4512`)
and uses that single value for **all three** comparisons:
```
x789 == 0      → Logout "NextExpectedMsgSeqNum invalid" + disconnect     (:4513)
x789 >  n789   → Logout "NextExpectedMsgSeqNum too high" + disconnect    (:4544)
x789 <  n789   → proactive resend [x789, n789 - 1]                       (:4587, :4591)
x789 == n789   → in-sync, no resend                                       (:4597)
```
On the **acceptor** path, by the time honor runs the reply Logon has already consumed a
sequence number, so `peek_outbound()` returns `N_post = N_pre + 1`, where `N_pre` is the
next-outbound **before** the reply. The peer initiator's **initial** Logon advertised
`789 = X = N_pre` in the ordinary in-sync case (it expects fixpp's very next message; see
R2). Post-reply the branch `x789 < n789` evaluates `N_pre < N_pre+1` = **true**, so fixpp
emits a spurious `SequenceReset-GapFill` for `[N_pre, N_pre]` — a single frame at the
sequence number the reply Logon **already used**. That is a duplicate-`MsgSeqNum` protocol
violation; the peer rejects it and Logs out.

**Live evidence** (QuickFIX-cpp v1.16.0 FileLog, fresh in-sync session, `N_pre = 1`):
```
CPTY → 35=A 34=1 789=1                       (peer Logon: expects fixpp #1 = N_pre)
fixpp→ 35=A 34=1 789=2                       (reply Logon — consumes seq 1, outbound→2 = N_post)
fixpp→ 35=4 34=1 36=2 123=Y                  (SPURIOUS GapFill at seq 1 — already used!)
CPTY → 35=5 58="MsgSeqNum too low, expecting 2 but received 1" (Logout, rejects duplicate)
```

## R2 — Conformance oracle (QuickFIX-cpp v1.16.0)

`reference-engines/quickfix-cpp/src/C++/Session.cpp`:
- `nextLogon` (`:179`): the retransmit **decision** is taken at `:227-239` using
  `getExpectedSenderNum()` **before** the reply Logon is generated at `:244`
  (`generateLogon(logon)`): `if (nextExpectedMsgSeqNum > getExpectedSenderNum()) → Logout`
  (`:230-238`, too-high); `if (< getExpectedSenderNum()) → sendRetransmitsAfterLogon=true`
  (`:228-229`); equal ⇒ no retransmit. So the comparison threshold is the **pre-reply**
  next-sender = `N_pre`.
- The **initial** Logon advertises `NextExpectedMsgSeqNum(getExpectedTargetNum())` (`:687`,
  **no** `+1`); the **reply** Logon advertises `getExpectedTargetNum() + 1` (`:709-710`,
  "+1 because incoming Logon did not increment the target SeqNum yet").
- The resend **range** (`:273-292`) uses `endSeqNo = getExpectedSenderNum() - 1` evaluated
  **after** the reply (`getExpectedSenderNum()` is now `N_pre+1`), i.e. `endSeqNo = N_pre`.
  → resends `[X, N_pre]` (inclusive of the reply Logon's own seq, gap-filled).

## R3 — Conformance oracle (QuickFIX-J 3.0.1)

`reference-engines/quickfixj/quickfixj-core/src/main/java/quickfix/Session.java`, `nextLogon`:
- Too-high check (`:2244-2263`): `actualNextNum = getNextSenderMsgSeqNum()` read at `:2250`
  **before** `generateLogon(reply)` at `:2278`; `if (targetWantsNextSeqNumToBe > actualNextNum)
  → generateLogout + disconnect`. Pre-reply threshold = `N_pre`.
- Resend decision + range (`:2308-2336`): compares `targetWantsNextSeqNumToBe !=
  nextSenderMsgNumAtLogonReceived` — a **snapshot of the sender seqnum taken when the inbound
  Logon was received** (pre-reply for the acceptor). `endSeqNo = nextSenderMsgNumAtLogonReceived`
  (`:2313`); `resendMessages(logon, X, endSeqNo)` (`:2334`) → resends `[X, N_pre]`.
  In-sync (`X == N_pre`) ⇒ the `!=` is false ⇒ **no resend**.
- The initial Logon advertises `NextExpectedMsgSeqNum = expectedTargetNum` via
  `generateLogon(otherLogon, expectedTargetNum)` (`:2612-2630`); the acceptor reply uses
  `nextExpectedTargetNum` with `+1` when the Logon was in-sequence (`:2272-2278`).

**Both engines agree**: the peer's advertised `789` is compared against the **pre-reply**
next-outbound (`N_pre`); `X == N_pre` ⇒ in-sync, no resend; `X > N_pre` ⇒ Logout; genuine-gap
range = `[X, N_pre]` inclusive.

## R4 — Deferred endpoint resolved: genuine-gap range = `[X, N_pre]` (NO change to fixpp)

`spec.md` deferred whether the genuine-gap resend is `[X, N_pre-1]` or `[X, N_pre]`. **Resolved:
`[X, N_pre]`** — both engines resend through `N_pre` inclusive (QFcpp `endSeqNo = N_pre`; QFJ
`endSeqNo = nextSenderMsgNumAtLogonReceived = N_pre`). fixpp's **current** range,
`replay_outbound_range_(x789, n789 - 1, end_is_through_current=true)` with `n789 = N_post`, is
`[x789, N_post - 1] = [x789, N_pre]` — **identical to both engines**. Therefore:

> **The genuine-gap RANGE is already correct. The defect is exclusively the COMPARISON
> THRESHOLD (`n789 = N_post` instead of `N_pre`).** The resend range computation
> (`replay_outbound_range_(x789, peek_outbound()-1, …)`) is left **unchanged**, satisfying
> FR-003's non-regression requirement.

## R5 — Role asymmetry (constrains the fix shape; initiator MUST stay unchanged)

The acceptor and initiator reach `honor_peer_next_expected_` in different states:
- **Acceptor** (`:2027`): honor runs **after** the reply Logon consumed a seq, so
  `peek_outbound() = N_post`. The peer's **initial** Logon `789` was measured pre-reply
  (`= N_pre`, no `+1`, R2/R3). Correct threshold = `N_pre = peek_outbound() - 1`.
- **Initiator** (`:3322`): honor runs on receiving the peer's reply ACK; fixpp's own Logon was
  sent earlier and **no reply is emitted on this arm**, so `peek_outbound() = N_post` is the
  correct reference, and the peer's **reply** `789 = target+1` (R2 `:709-710`, R3 `:2272-2278`)
  already accounts for fixpp's Logon → `X == peek_outbound()` ⇒ in-sync. **Empirically the
  `NE-*-init` live cells pass against both engines** — the initiator path is already correct.

⇒ The fix is **role-aware**: the comparison threshold differs by call site. It is **not** a
blanket `peek_outbound() - 1` (that would break the initiator: its in-sync `X = N_post` would
become `X > N_pre = N_post-1` → spurious Logout).

## R6 — Design decision (comprehensive, per the /clarify answer)

Parameterize the comparison reference:
1. `honor_peer_next_expected_` gains a `seqnum_t next_outbound_ref` parameter (the next-outbound
   the peer's `789` is compared against). All three comparisons (`==0`, `> ref`, `< ref`,
   `== ref`) use `next_outbound_ref` instead of the internally-read `peek_outbound()`.
2. The resend **range** stays `replay_outbound_range_(x789, peek_outbound() - 1,
   end_is_through_current=true)` — reads the live counter, unchanged (R4).
3. **Acceptor call site** (`:2027`): capture `const seqnum_t n_pre = seqnum_mgr_.peek_outbound();`
   **before** the reply Logon emit (before `:2015`) and pass `n_pre`. (Snapshot-before-reply
   mirrors QFJ's `nextSenderMsgNumAtLogonReceived`; robust against the exact emit count.)
4. **Initiator call site** (`:3322`): pass `seqnum_mgr_.peek_outbound()` — current behavior,
   byte-identical (FR-008).
5. The X>N "too-high" Logout text currently reports `n789` (post-reply); it now reports
   `next_outbound_ref` (= `N_pre` for the acceptor). Observable text change only; no error slot.

Per the /clarify answer (**comprehensive**), the same `next_outbound_ref` drives the too-high
arm too — so a peer advertising exactly `N_pre + 1` (= `N_post`) in its initial Logon is now
correctly **too-high → Logout** (it claims a message fixpp has not sent), where today fixpp
mis-classifies it as in-sync.

- **Decision**: role-aware comparison reference (param), range unchanged.
- **Rationale**: matches both engines exactly; minimal surface (one new param + a pre-reply
  capture on the acceptor arm); keeps the initiator and the genuine-gap range byte-identical.
- **Alternatives rejected**: (a) blanket `peek_outbound()-1` in honor — breaks the initiator
  (R5); (b) fix only the in-sync guard, leave X>N comparing against `N_post` — leaves a latent
  off-by-one in the too-high arm (the /clarify "narrow" option, rejected); (c) re-order honor
  before the acceptor reply emit — would violate the 027 RC#4 ordering invariant and ripple
  into the behind-side-tolerance / 789-advertisement sequencing.

## R7 — Blast-radius hypotheses (to confirm at /analyze + TDD)

The fix changes acceptor 789-honor behavior at the boundary. Candidate existing pins
(`tests/session/test_next_expected_msgseqnum.cpp` + the interop cell), to be enumerated exactly
during `/speckit-tasks`/`/analyze`:
1. **[CORRECTED at Gate A]** `TEST(Honor, XeqN_NoResend)` acceptor arm
   (`test_next_expected_msgseqnum.cpp:776-817`) advertises `X = N_post` (seeds `N_pre=4`, reply
   Logon at `34=4`, feeds `789=5 == N_post`) and asserts Active + no-resend. It is **NOT
   unaffected**: under the comprehensive fix (`R = N_pre = 4`) `X=5 > R` → **Logout**, so the pin
   FLIPS (the 030-W9b analog — it encoded the buggy post-reply reference). Split it: W1 in-sync
   (`789=N_pre=4`) + W3 too-high (`789=N_pre+1=5` → Logout). No pin advertised `X == N_pre` (the bug
   shipped because nothing hit the in-sync-at-`N_pre` boundary live), so W1's in-sync arm is net-new.
2. **[CONFIRMED at Gate A]** There is **no dedicated acceptor 789-`X>N` (too-high) pin** today; the
   `BehindSide` "too-high" tests (`:976/:1051/:1123`) concern the peer Logon's *MsgSeqNum* (34=)
   under formulation-A tolerance, feeding `789 = 3` (well below the boundary), NOT the 789-honor
   threshold — unaffected. The only verdict-flipping pin is item 1; W3 (the repurposed `789=N_pre+1`
   arm) becomes the first dedicated acceptor 789-too-high pin.
3. The acceptor genuine-gap (X<N) resend-range test: **unchanged** (range = `[X, N_pre]`,
   R4) — must remain green (FR-003).
4. The initiator cells (`NE-*-init` + unit): **unchanged** (R5) — non-regression.

No new wire field, error slot, codegen, or C-ABI (FR-009). The honor signature change is an
internal source rebuild only.

## R8 — Discriminating witnesses (for TDD; avoid the `drive_to_active` trap)

Per `[[feedback_witness_asserts_named_postcondition_not_proxy]]` and CHECKPOINT 8: `drive_to_active`
alone is too weak (it passes even when the peer rejects fixpp right after Active). The acceptor
in-sync witness MUST assert the **discriminating** postcondition directly:
- **W1 (in-sync, the bug)**: acceptor honors peer `789 = N_pre` ⇒ the reply Logon WAS emitted at
  `34==N_pre`, and inspecting only frames AFTER it, fixpp emits **zero** `SequenceReset`, **zero**
  `ResendRequest`, **zero** `43=Y`, and **no newly originated** frame re-using `34==N_pre` (assert
  on emitted frames / `recent_events`). NOT a universal strict-monotonicity claim — the genuine-gap
  resend (W2) legitimately carries historical seqnums (FR-004/SC-003). Currently RED (emits the
  spurious GapFill at `N_pre`).
- **W2 (genuine gap, non-regression)**: acceptor with outbound at `N>2`, peer `789 = X < N_pre`
  ⇒ proactive resend of the stored gap (`[X, our_last]` PossDup/GapFill) + GapFill-through-current
  (`NewSeqNo = peek_outbound()`), no `ResendRequest`. Must stay green. **Note (audit CHK017):** the
  range *endpoint* arg is inert at this call site (`end_is_through_current=true` forces
  `eff_end=our_last`, `session.cpp:4419-4420`), so W2 cannot discriminate `next_outbound_ref-1` from
  `peek_outbound()-1` — directive #3 is enforced by code review + the live cell, not a unit RED.
- **W3 (too-high boundary)**: peer `789 = N_pre + 1` (initial Logon) ⇒ Logout + disconnect
  (was mis-classified in-sync). Discriminates the comprehensive scope.
- **W4 (initiator non-regression)**: `NE-*-init` unit + live ⇒ unchanged (in-sync, no resend).
- **W5 (invalid-789 / X==0)**: unchanged Logout arm.
- **Live**: the `027` SC-005 acceptor cell (`NE-*-acc`) vs QFcpp/QFJ establishes and the peer
  does **not** Logout-reject; witness hardened beyond `drive_to_active` (stay-Active /
  `session_event` discriminator, mirroring 030 RC#2).
