# Feature Specification: Sequence-reset fidelity — QuickFIX's endings for a reset on Logout and for a Logon acknowledgement below NextNumIn

**Feature Branch**: `104-sequence-reset-fidelity`

**Created**: 2026-11-17

**Status**: Draft

**Base**: `bf294f00` (fixpp `main`, the squash of PR #700, which changes no file under `src/`, `include/` or `tests/`;
the session code is that of 103's squash `b3df5d45`).

**Input**: batches B53 (fixpp#595) and B107 (fixpp#695 L-103-2, fixpp#701 L-103-7), run as one feature by owner
ruling of 2026-11-17, toward the owner's v1.0 goal 1 ("no known bugs"). The owner's rulings of the same day are:
one feature; B107 takes option (i) of 103's research D2.3; #701 joins B107. The evidence was read against the FIX
docs and the session source of QuickFIX C++ v1.16.0, QuickFIX/J 3.0.2 and QuickFIX/Go v0.9.12. It is kept in the
parent repo under `research/G19-fix-fpml-iso20022/research/probes/b107_695_104/` (`quickfix_reading.md`,
`fix_docs_reading.md`), plus `research/probes/b41_563_564/ph8a_readings.md` §1 for B53. The fixpp readings below
were taken at `bf294f00`. Every line citation is a lead to re-derive, not a pin.

## Context & Problem

A counterparty sees three behaviours on the sequence-reset surface that differ from QuickFIX's. The first two also
differ from the FIX Session Layer.

1. **`reset_on_logout` resets after a local Logout the peer never answered (#595).**
   - With `reset_on_logout` set, `Session::close`'s teardown reset ("024 T014") fires when `logout_seen_` is set.
   - `run_logout_phase1` sets `logout_seen_` once its own Logout has been emitted, so a local graceful close whose
     Logout is sent and never answered resets both numbers to 1. The wait for the answer ends either at
     `logout_disconnect_timeout_ms` or, for an engine-managed session, at a transport drop.
   - QuickFIX C++, QuickFIX/J and QuickFIX/Go reset for `ResetOnLogout` in one place only: on receipt of the peer's
     Logout, whether it is a request or a response. A logout timeout only disconnects, and then only
     `ResetOnDisconnect` resets.
   - What a peer sees: the peer never processed the Logout, so it did not reset. fixpp's next Logon carries 34=1
     where a QuickFIX engine's would carry the next number, and the peer reads it as too low.
   - The rule is 024's contract clause C3.1 ("a Logout occurred in EITHER direction"). B-538-7 restates it ("a Logout
     that was sent still resets, answered or not").
2. **An unasked ResetSeqNumFlag(141)=Y Logon acknowledgement at 34=1 ends a lenient initiator silently (#695, L-103-2).**
   - In `Session::on_inbound_logon_sent_`, the sequence check (`check_inbound`) runs before the 141 read.
   - Take a `bilateral_lenient` (default) or `unilateral` initiator whose NextNumIn is above 1 at its Logon, because
     the store was hydrated and nothing reset it before the Logon. An acknowledgement with 141=Y at 34=1 is too low
     for it. The session records `Disconnected`, sends no Logout and never runs the reset.
   - All three QuickFIX initiators accept such an acknowledgement and reset both numbers. QuickFIX C++ then sends its
     own Logon with 141=Y at 34=1. QuickFIX/J and QuickFIX/Go send nothing more at the Logon.
   - No QuickFIX acceptor sends this 141=Y by its own logic, even with `ResetOnLogon=Y`. It reaches the wire through
     the counterparty application's `toAdmin`, in all three engines, or through QuickFIX/J's `LogonTag=141=Y`.
   - The FIX Session Layer (Nov 2020) defines 141 as "both sides … should reset their NextNumIn and NextNumOut values
     to 1 and start a new FIX session" (the field table, §11.10).
3. **A frame below NextNumIn in a Logon arm ends the session silently (#701, L-103-7).**
   - In NotConnected (`on_inbound_not_connected_`, acceptor) and LogonSent (initiator), a frame whose 34 is below
     NextNumIn reaches `check_inbound`'s too-low branch. That branch records `Disconnected` and returns success, and no
     Logout is sent.
   - All three QuickFIX engines send a Logout with Text "MsgSeqNum too low, expecting N but received M", then
     disconnect. This is QuickFIX C++'s `doTargetTooLow`, QuickFIX/J's `doTargetTooLow` and QuickFIX/Go's
     `shutdownWithReason` with `targetTooLow`.
   - FIX-SL §4.3.11 says: "If MsgSeqNum(34) is present in the incoming Logon(35=A) message and is less than NextNumIn,
     then a Logout(35=5) message should be sent … followed by a termination of the transport layer connection." §4.8.1
     and Test Cases 2c and 1B(d) say the same.
   - The common reach: fixpp reconnects at stored numbers to a `ResetOnLogon=Y` acceptor. Every QuickFIX acceptor
     configured that way resets, then acknowledges at 34=1 without 141.
   - 103's B-103-10 already sends this Logout in Active. 103 left the Logon arms out of scope.

Also in this feature, by owner instruction of 2026-11-17: re-record the #266 peak-memory test-count basis.
- The libc++ lanes now execute more tests than the recorded basis. This is a dated observation of one named run,
  the merge run of `b3df5d45`, read from each lane's `ran:` value in its `ci/peak-memory-report.sh` line. Its numbers
  are not copied here; FR-017 re-derives the counts at 104's head.
- Because the counts differ, `ci/peak-memory-report.sh` marks every lane's figure DIAGNOSTIC ONLY, so the memory
  criterion is not discharged.

## Clarifications

### Session 2026-11-17 (owner rulings before the spec)

- Q: B53 and B107, one feature or two? → A: One feature, 104.
- Q: B107: option (i) (read 141 ahead of the sequence check in LogonSent, accept and reset as QuickFIX does), or
  keep L-103-2? → A: Option (i). Also check the official FIX docs. They were checked; see `fix_docs_reading.md`.
- Q: #701 (L-103-7): join 104, a separate batch, or leave? → A: Join 104 (with the FIX docs check, done).
- Q: The #266 test-count basis? → A: Re-record it in 104's PR.

### Session 2026-11-17 (`/speckit-clarify`; the owner asked for each answer to be checked against the FIX docs first)

- Q: With `reset_on_logout`, does the reset happen when the peer's Logout is received (as in QuickFIX), or at
  teardown, fired by that receipt? → A: At receipt. The reset follows the reply Logout, if one is owed, and precedes
  the disconnect, as in QuickFIX C++ (`nextLogout`), QuickFIX/J (`nextLogout`) and QuickFIX/Go (`handleLogout`).
  `reset_on_disconnect` stays at teardown. The FIX docs define no reset on Logout: FIX 4.4 Vol 2 says a reset to 1
  "constitutes the beginning of a new FIX session" by bilateral agreement, and that "session termination without
  receiving a Logout should treat the counterparty as logged out". If the peer's Logout never arrives, there is no
  reset under `reset_on_logout`. That agrees with the docs and with all three engines, whose logout timeout only
  disconnects.
- Q: Does the too-low Logout in the Logon arms carry SessionStatus(1409)=9 as well as Text? → A: Text only, exactly
  "MsgSeqNum too low, expecting N but received M". This matches QuickFIX C++, QuickFIX/J and QuickFIX/Go, and 103's
  Active arm (B-103-10). FIX-SL allows 1409 on Logout only "by counterparty agreement" (§4.3, EP56), and §4.7.1 says
  "may". The FIX 4.4 Vol 2 Logout has only 58/354/355, and 1409 is not in the FIX 4.4 dictionary.
- Q: After accepting an unasked 141=Y acknowledgement and resetting, does fixpp send its own Logon with 141=Y at
  34=1 (QuickFIX C++) or nothing further (QuickFIX/J, QuickFIX/Go)? → A: Nothing further. Both numbers are reset:
  inbound continues at 2 after the acknowledgement's 34=1, and outbound restarts at 1. Then the session goes Active.
  - History (parent `research/probes/b107_695_104/q1_quickfix_history.md`): QuickFIX C++'s reply to an unasked
    141=Y is a side effect of `f26ca5fc` (2010), which swapped the operands of the 2005 condition `05d4d39c`. That
    reply was never tested or discussed, and quickfix/quickfix#417 (2023, open) reports it as a bug.
  - QuickFIX/J ported the 2005 condition and removed it deliberately in 2008 (QFJ-380, after a forum thread: "There
    is no reason A should send a second Logon").
  - QuickFIX/Go never had an initiator reply.
  - FIX 4.4 Vol 2's and FIX-SL §4.4.2's reset procedure (the receiver of a reset Logon answers with 141=Y at 34=1)
    describes a reset request, not an acknowledgement. §4.4.3 makes the reset at Logon the initiator's option.

### Session 2026-11-17 (Gate A round 1; orchestrator rulings under the owner's autonomy grant)

The owner's grant of 2026-11-17 ("run autonomously through all steps … Stop if you hit a blocker"). Each ruling is
listed again in the PR body for the owner's review at Gate B sign-off. The mechanism is the contract's Part 1
(RT-1, RR-1 to RR-4, and its Outcomes table).

- Q (RU-1; Codex #1, Opus N-3): a received Logout whose processing fails before its reset (a `fromAdmin` throw, a
  late parse failure, a failed persist, a refused reply), or that races `close()` or `Engine::stop()`: reset
  anyway, or not? → A: **QuickFIX's rule.** Under `reset_on_logout` a received Logout resets if and only if its
  processing reaches its reset point: after the reply Logout has been attempted, if one is owed, or after a
  response has been accepted. Processing that ends before it does not reset. The teardown backstop under
  `reset_on_logout` is deleted; `reset_on_disconnect` stays at teardown. By construction, a reset point once
  reached is completed and has one owner: `close()` and `Engine::stop()` wait for the receipt (rule RR-2); a
  receipt they could not wait for (no clock, or the wait expired) has not reached its reset point and is not reset
  (refined by the design rulings below).
  Evidence: QuickFIX C++ resets only inside `nextLogout`, after `verify()`, which runs `fromCallback` →
  `fromAdmin` (`quickfix-cpp@v1.16.0:src/C++/Session.cpp` `nextLogout`, `verify`, `fromCallback`); QuickFIX/J the
  same (`quickfixj@QFJ_RELEASE_3_0_2:quickfixj-core/src/main/java/quickfix/Session.java` `nextLogout` → `verify` →
  `fromCallback`, then `resetState()` under `resetOnLogout`); QuickFIX/Go `in_session.go` `handleLogout`:
  `verifySelect(msg, false, false, true)` (whose `verifyMsgAgainstAppImpl` calls `FromAdmin`) returns into
  `processReject` before `dropAndReset`. Read with it: QuickFIX/J's `sendRaw` catches a `Throwable` from
  `toAdmin`, so a reply whose `toAdmin` throws still resets there (contract Outcomes O6, a listed departure), and
  QuickFIX C++ and QuickFIX/J catch the store's `IOException` in `sendRaw`, so a reply whose store write fails
  still resets (O8, which fixpp matches).
- Q (RU-2; Opus N-1): a Logout received in LogonSent (the peer refusing fixpp's Logon), and one received in
  NotConnected? → A: **QuickFIX C++ and QuickFIX/J.** In LogonSent it resets under `reset_on_logout`. fixpp still
  sends no reply Logout there and still ends `Disconnected`; QuickFIX replies, a listed departure that FIX-TC 1B(e)
  allows ("(Optional) Send Logout"). In NotConnected (acceptor, before any Logon) it does not reset. Evidence: QFC
  `validLogonState` admits a Logout when `m_state.sentLogon()`, and false before any Logon was sent or received;
  `nextLogout` then resets under `m_resetOnLogout`. QFJ `validLogonState`:
  `MsgType.LOGOUT.equals(msgType) && state.isLogonSent()`; `nextLogout` resets under `resetOnLogout`. QFGo's
  `logonState.FixMsgIn` sends a non-Logon to `latentState` with no reset (the third engine, not followed).
- Q (RU-3; Opus N-2): the numbers are (1, 1) during the unit's store suspension while the session is still Active
  — gate the emitters, or reorder? → A: **construction, not a gate.** In every receipt arm: the reply Logout, if
  owed; record `Disconnected` (which stops the liveness loop from numbering, refuses sends and drains further
  inbound frames); then the durable reset unit, which `close()` waits for; then the transport closes as today.
  This does not widen 103's FR-021. Nothing on the wire differs from QuickFIX, which resets and then disconnects
  synchronously. Listed departure: `onLogout` fires before the reset, where QFC's `disconnect()` fires it after
  `m_state.reset`. Read from source (contract RR-4): the liveness loop, `Session::send` and the resend answer stop
  on `Disconnected`; an Active-arm frame already in flight when the detached too-high bound records `Disconnected`
  does not, and is stopped by construction at `store_then_emit` and `persist_inbound_advance_`. Research R1's false
  premise ("the liveness loop's emits wait at the gate") is removed.
- Q (N-8, decided from the text): must the too-low Logout, and the initiator's unasked reset, come after the mTLS
  CompID-authorisation gate? → A: **Yes for every outbound answer; yes for the reset by the same principle.**
  FIX-TC 1S(c): a Logon "received with unauthenticated/non-configured identity … Disconnect without sending a
  message (Note: sending a Reject or Logout(35=5) would consume a MsgSeqNum(34))"
  (`fixdocs/tc.txt`, §4.4.1). So on the acceptor the too-low Logout comes after the mTLS CompID-authorisation gate
  and after `authorize_logon`, which judge the identity; on the initiator after its mTLS gate. A reset sends
  nothing, so 1S(c) does not bind it by its text, and FIX-TC 1B (the initiator) has no identity sub-case. But a
  durable reset made at an unauthenticated peer's word changes the session's state more than one consumed number
  does, so the unasked unit also runs after the initiator's mTLS gate, and the LogonSent Logout's reset needs the
  same authorisation (RR-1's row S). The asked (strict or knob) path keeps today's order, unit before the gate
  (FR-010).
- Q (design ruling 1): the receipt marker? → A: **Accepted:** `logout_seen_` becomes a three-valued marker,
  `none` / `pending` / `reached` (data-model).
- Q (design ruling 2): may `close()` wait while a receipt is `pending`, not only while its unit is in flight? →
  A: **Accepted** (contract RR-2 item 4).
- Q (design ruling 3): may the teardown take over a `pending` receipt that `close()` could not wait for? → A:
  **Rejected:** that is the reset-without-processing RU-1 rules out. A receipt still `pending` when `close()`
  cannot wait (no clock, or the wait expired) never reached its reset point, so it is not reset under
  `reset_on_logout`; the teardown closes it out to `reached`, so the arm, resuming, runs no unit and RR-4 refuses
  its writes. The teardown resets only a receipt that is `reached` with its unit not started, once; the
  reset point being one strand turn (RR-2 item 3) leaves that case with no input. Round 1's DQ-17 and L-104-8 are
  deleted; the no-reset case is disclosed in B-104-1 and B-104-2, with no new departure. Cells: R-1l ("`close()`
  without a clock while `pending` → no reset") and R-1u ("`reached`, unit not started, teardown → one reset",
  driven through its mutant).
- Q (design ruling 4): the write refusal once the marker is `reached` (RR-4)? → A: **Accepted** as a
  by-construction guard; its cell (R-1r) carries a real mutant.
- Q (design ruling 5): arm S's reset requiring mTLS authorisation, and the unasked unit after the initiator's mTLS
  gate? → A: **Both accepted.** Both follow FIX-TC 1S(c)'s principle, that nothing is acted on for an
  unauthenticated identity, rather than its text, which speaks of sending a message.
- Q (records): the L-104-3 follow-up? → A: plan phase 9 files a labelled issue (bug, follow-up) for the refusals
  that still answer before the identity gates.
- Q (R1): the store-order check? → A: it stays a phase-0 check. If it fails, implementation stops and the
  decision returns to the orchestrator.

### Session 2026-11-18 (owner, after Gate A exhaustion)

- Q: Gate A ended at round 3 with two P1s of one root cause. On route B one receipt marker had several owners: a
  duplicate too-high Logout's frame cleared the bound's `pending`, and the bound's claimed exit cleared arm A's,
  so `close()` stopped waiting for A. Rewrite the bundle, or amend it, and with which ownership construction? → A:
  **A targeted amendment with construction A (Fable's three-state), followed by a fresh Gate A loop.**
  - The marker keeps three values, and only awaited read-pump frames own `pending`.
  - B's spawn and ignored branches write nothing. The bound's owed receipt is 103's `peer_logout_.phase`, and the
    bound writes the marker once, `none` → `reached` at its reset point.
  - `close()` needs nothing new for the bound.
  - The construction rests on INV-P, INV-M, INV-T and INV-X (contract RR-2).
  - **Settling experiment.** Plan phase 1 opens with Debug asserts of INV-T and INV-X, run with
    `TooHighLogoutState103.*` and the two new cells, R-1y and R-1z. If an assert fires, implementation stops and
    the decision returns to the owner.

### Session 2026-11-18 (checklist audit; orchestrator ruling)

- Q (CHK006, CHK036): do arm S's 35=5 branch in `on_inbound_logon_sent_`, which sits ahead of `interpret_logon`, and
  its mTLS `authorize` call run with `reset_on_logout` off? → A: **No: only when `reset_on_logout` is set.** With it
  off nothing changes from the base: `interpret_logon` refuses the frame as a non-Logon, the session records
  `Disconnected` and returns success, and no `session_event_compid_authorization_failed` is emitted, whatever the
  identity (contract W11). With it set the branch runs, the mTLS gate first (design ruling 5, FIX-TC 1S(c)): an
  authorised identity resets once (RU-2, W10), and an unauthorised one draws the gate's event with no reset (O19).
  In both cases the arm ends `Disconnected` and returns success, exactly as the base's ending does, and sends no
  reply Logout (RU-2, DQ-9). Reasons: it keeps W11, contract RR-2 item 1 and data-model invariant RO-1 (with rol off 104
  adds nothing); it is the minimal change; and QuickFIX's observable difference with the knob off is the reply Logout,
  which RU-2 already lists as a departure (DQ-9). Cell: R-1o's rol-off arm, RED by the mutant "the branch runs
  whatever the knob" (contract RR-1 "Arm S's branch and the knob", research D10).

## User Scenarios & Testing *(mandatory)*

The users are the operators and integrators of a fixpp session that talks to a QuickFIX-family counterparty.

### User Story 1 — An unanswered Logout does not reset the numbers under `reset_on_logout` (Priority: P1)

An operator sets `reset_on_logout` and closes a session gracefully. The peer does not answer the Logout, or the
connection drops while fixpp waits for the answer. The numbers are kept, as a QuickFIX engine keeps them, so the next
Logon carries the stored next number and the peer, which never processed the Logout, accepts it.

**Why this priority**: it is a silent sequence desynchronisation on the default close path of any session with the
knob set, and the peer can reach it by not answering.

**Independent Test**: drive a graceful close with `reset_on_logout` set and no peer answer, ending once by the
timeout and once by a dropped connection. Read both numbers and the store afterwards.

**Acceptance Scenarios**:

1. **Given** `reset_on_logout` set, `reset_on_disconnect` not set, numbers (in, out) = (N, M) with N, M > 1, **When**
   a graceful close sends its Logout and `logout_disconnect_timeout_ms` passes with no Logout from the peer, **Then**
   after the close the session and its store still hold the numbers they held after the Logout was sent; no reset ran.
2. **Given** the same, on an engine-managed session, **When** the transport drops during the wait, **Then** no reset
   ran.
3. **Given** the same, **When** the peer answers with a Logout before the timeout, **Then** the numbers are reset to 1
   (unchanged behaviour, now caused by the receipt).
4. **Given** `reset_on_logout` set, **When** the peer sends a Logout request and fixpp answers it, **Then** the
   numbers are reset to 1 (unchanged).
5. **Given** `reset_on_disconnect` set, with or without `reset_on_logout`, **When** any of the above happens,
   **Then** the numbers end at 1, and the teardown makes at most one store reset (024 C5.1; with both knobs and a
   received Logout, one more at receipt, D2).
6. **Given** `reset_on_logout` set on an initiator in LogonSent, **When** the peer answers its Logon with a Logout,
   **Then** the numbers are reset to 1, no Logout is sent, and the session ends `Disconnected` (RU-2).
7. **Given** `reset_on_logout` set, **When** the processing of a received Logout ends before its reset point (a
   `fromAdmin` throw or reject, a late parse failure, a failed persist, a refused reply, a too-high Logout left
   unacknowledged), **Then** no reset is made under `reset_on_logout` (RU-1).

---

### User Story 2 — A lenient initiator accepts an unasked 141=Y acknowledgement and resets (Priority: P1)

A `bilateral_lenient` or `unilateral` initiator logs on over a store whose inbound number is above 1. The
counterparty acknowledges with 141=Y at 34=1 although fixpp did not ask for a reset. fixpp accepts the acknowledgement
and resets both numbers, as every QuickFIX initiator does, and the session reaches Active in sequence with the peer.

**Why this priority**: today the session ends silently at every such Logon, with no Logout and no indication of the
cause. A counterparty that sets 141=Y in its `toAdmin` makes this permanent.

**Independent Test**: hydrate an initiator at numbers above 1, feed an acknowledgement with 141=Y at 34=1, and read
the state, both numbers, the store, and the frames fixpp sends.

**Acceptance Scenarios**:

1. **Given** a lenient initiator with NextNumIn = N > 1 that sent its Logon without 141, **When** the
   acknowledgement carries 141=Y at 34=1, **Then** the session does not end on it. Both numbers are reset
   (inbound next 2, outbound next 1). The session reaches Active without sending a second Logon, and the next frame
   fixpp sends is numbered 1.
2. **Given** the same with `unilateral`, **Then** the same.
3. **Given** a `bilateral_strict` initiator (which sent 141=Y and reset before its Logon), **When** its
   acknowledgement carries 141=Y at 34=1, **Then** behaviour is unchanged (in sequence, reset as today).
4. **Given** a lenient initiator, **When** the acknowledgement carries 141=Y at a 34 = M > 1, **Then** the
   acknowledgement is not consumed: both numbers are reset (inbound next 1, outbound next 1), the session reaches
   Active without sending a second Logon, and the peer's next frame draws a ResendRequest from 1 (research D4,
   ruling O-2; DQ-2; B-104-4, L-104-5).

---

### User Story 3 — A too-low frame in a Logon arm draws QuickFIX's Logout (Priority: P2)

A frame whose MsgSeqNum is below NextNumIn, without 141=Y and whatever its PossDupFlag(43) (E-2), arrives while the session is
establishing: the acknowledgement on an initiator, or the Logon on an acceptor. fixpp sends a Logout with Text
"MsgSeqNum too low, expecting N but received M", then ends the session. That is what the FIX Session Layer asks for
(§4.3.11) and what all three QuickFIX engines do without 43=Y.

**Why this priority**: the session ends either way, but today a silent end leaves the peer's operator nothing to
diagnose. The commonest reach is a reconnect against a `ResetOnLogon=Y` acceptor.

**Independent Test**: on an initiator with NextNumIn = N > 1, feed an acknowledgement at 34 = M < N without 141. On
an acceptor hydrated at NextNumIn = N > 1, feed a Logon at 34 = M < N without 141. Read the frames sent and the
final state.

**Acceptance Scenarios**:

1. **Given** an initiator in LogonSent with NextNumIn = N, **When** the acknowledgement arrives at 34 = M < N without
   141=Y, whatever its 43, **Then** fixpp sends one Logout with Text exactly "MsgSeqNum too low, expecting N but received M".
   That Logout is numbered with fixpp's next outbound number. NextNumIn is not advanced. Then the session records
   `Disconnected`.
2. **Given** an acceptor in NotConnected with NextNumIn = N, **When** a Logon arrives at 34 = M < N without
   141=Y, whatever its 43, **Then** the same, from the acceptor.
3. **Given** either arm, **When** the Logout cannot be sent (its build does not fit, its store call or its transmit
   fails), **Then** the session still ends, with the ending fixpp's existing Logout helpers give that failure.

---

### User Story 4 — The peak-memory figure counts as evidence again (Priority: P3)

The #266 peak-memory step reads its lane's executed test count against a recorded basis. After 104 the basis
matches what the lanes run, so a lane's figure is EVIDENCE again and not DIAGNOSTIC ONLY.

**Independent Test**: the step's own comparison on 104's CI run.

**Acceptance Scenarios**:

1. **Given** 104's head, **When** a lane's Test step succeeds, **Then** its executed count equals the recorded basis
   and the report does not print the DIAGNOSTIC ONLY warning for a count mismatch.

### Edge Cases

- **A Logout that was never sent** (its build failed, its store call was cancelled or failed, it was cut by a
  terminal close): no reset under `reset_on_logout`. This is unchanged (B-538-7's first half, B-563-5); it now
  follows from the receipt rule.
- **A Logout sent by an error path** (a too-low Logout, a posture or strict refusal, the SendingTime refusal): no
  reset under `reset_on_logout`, as today. QuickFIX resets only on receipt.
- **The peer's Logout response received in LogoutSent**: it must now be a reset trigger. Today only `logout_seen_`'s
  earlier set at emit makes it reset, because the LogoutSent arm sets only `logout_confirmed_`.
- **A too-high Logout** (103's bounded recovery, `on_inbound_active_too_high_logout_`): it resets at its
  acknowledgement (erratum E-3). One that ends unacknowledged (claimed by `close()`, a stop, another ending, a
  reconnect drain) makes no reset under `reset_on_logout` (RU-1). QuickFIX C++ and QuickFIX/J would have reset at
  receipt, since they process a too-high Logout at once; that follows from 103's bound and is a listed departure.
- **A received Logout whose processing ends before its reset point** (a `fromAdmin` throw or reject, a late parse
  failure, a failed inbound persist, a reply whose `toAdmin` throws or whose number cannot be assigned, or a
  receipt `close()` could not wait for because there was no clock or the wait expired): no reset under
  `reset_on_logout`, as in QuickFIX (RU-1). There is no teardown backstop or take-over.
- **A Logout received in LogonSent** (the peer refuses fixpp's Logon): it resets under `reset_on_logout`, sends no
  Logout and ends `Disconnected` (RU-2). **A Logout received in NotConnected** fails `interpret_logon`, ends the
  session and does not reset, as today.
- **An in-place reconnect** (a direct `Session::drive_reconnect`; the Engine does not reconnect in place): a peer
  Logout on the old connection reset the numbers at its reset point, before the reconnect, and the next Logon goes
  at 1, as in QuickFIX. Nothing received on the old connection resets the new one: the reconnect's
  `install_reconnected_transport` clears the receipt marker. 103's disclosure that `logout_seen_` stays latched
  across the reconnect is retired (B-103-9 amended).
- **The peer's Logout never arrives** (timeout, or a dropped connection): there is no reset under `reset_on_logout`.
  Only `reset_on_disconnect` resets (FR-002, FR-004).
- **141=Y acknowledgement at 34 ≠ 1** on a lenient initiator: research D4 reads the three engines, and ruling O-2
  adopts QuickFIX/J's and QuickFIX/Go's outcome (US2 AS4; DQ-2, L-104-5).
- **A too-low Logon or acknowledgement with 43=Y**: it draws the same Logout (E-2, research D6). This follows
  FIX-SL §4.3.11 and QuickFIX/Go. QuickFIX C++ and QuickFIX/J run their PossDup handling and then wait for their logon
  timeout, which is a departure recorded in the plan.
- **The acceptor twin of User Story 2** (a Logon with 141=Y at 34=1 on an acceptor whose NextNumIn is above 1): the
  inbound seed is withheld for a 141=Y Logon when hydration runs. An acceptor Session serves one accepted connection,
  so hydration runs at its first Logon. The plan confirms whether any route leaves NextNumIn above 1 at that point. If
  one does, it is in scope.
- **A strict initiator's refusal of an acknowledgement without 141** is silent in LogonSent, while NotConnected's
  refusal sends a Logout. It is not a too-low case. It stays out of scope unless the plan finds it shares the arm
  change.

## Requirements *(mandatory)*

### Functional Requirements

**Reset on Logout (B53, #595)**

- **FR-001**: With `reset_on_logout` set, the sequence numbers MUST be reset to 1 when, and only when, a Logout
  received from the peer is processed as far as its reset point (RU-1): a request in Active or LogonReceived, a
  response to its own Logout in LogoutSent, a too-high Logout at its acknowledgement (erratum E-3), or a Logout in
  LogonSent (RU-2). A Logout received in NotConnected MUST NOT reset. The reset point follows the reply Logout's
  attempt, if one is owed, or the acceptance of a response, as in QuickFIX C++, QuickFIX/J and QuickFIX/Go
  (clarification 2026-11-17). Processing that ends before it MUST NOT reset under `reset_on_logout`. At the reset
  point the arm records `Disconnected`, then runs 093's 141=Y reset unit (`run_reset_unit_`, 093 contract C-6,
  unchanged by 103) (erratum E-1; RU-3). Sending a Logout MUST NOT by itself cause the reset. The reset MUST be
  durable on a persistent store. `close()` MUST wait for a receipt that is between its receipt and its reset point,
  and for its unit, with the bound and the clock condition of 093's wait (B-104-2). `Engine::stop()` MUST wait for
  it, and for its unit, with no bound, by joining its role loops before it closes the session (`L-563-4`,
  `L-093-12`); under stop a receipt is never closed out (contract RR-2 item 4, O15). A too-high Logout whose
  acknowledgement is already under way is waited for by 103's join of its bound instead. `close()` MUST stop
  waiting as soon as the receipt leaves that interval, by whatever exit. A receipt still before its reset point
  when `close()` cannot wait (no clock, or the wait expired) MUST NOT be reset under `reset_on_logout`. The arm, resuming,
  MUST run no unit and MUST issue no store call after the close-out. A store call it had already issued runs to its
  end (contract O13-r, O13-p). The reset point's marker transition and the unit's start MUST be one strand turn, with
  no throw point, so no reset point once passed can be lost (contract RR-2).
- **FR-002**: A graceful close whose Logout was sent and not answered MUST NOT reset under `reset_on_logout`,
  whether its wait ended by `logout_disconnect_timeout_ms` or by a dropped connection.
- **FR-003**: The peer's Logout response received in LogoutSent MUST be a reset trigger under `reset_on_logout`.
- **FR-004**: `reset_on_disconnect` MUST be unchanged: it resets at teardown on every route it resets on today. A
  session with both knobs set MUST end its teardown with both numbers at 1. 024's C5.1 is read as "at most one store reset
  per teardown" (erratum E-4): with both knobs set, a received Logout makes one reset at receipt and one at teardown,
  as QuickFIX C++ and QuickFIX/J do (research D2).
- **FR-005**: 024's contract clause C3.1, and B-538-7's "answered or not", MUST be superseded by name, and so MUST
  every other surface that restates a rule 104 changes: 027 C3's explicit-end case (D3.3a), 093 C-6's `close()`
  bullet, the live B&L rows, the brain pages and the test comments. Every witness that pins a reset after an
  unanswered local Logout MUST be re-pinned to the new rule or retired with a note. The surfaces are found by the
  supersession census (research D8), which finds surfaces that stay GREEN as well as cells that fail; its known
  positives include `ResetOnLifecycleTest.ResetOnLogout_LocalInitiated_Resets` and B-024-1.
- **FR-006**: The code comments that state the old rule (for example the "EXACTLY two … sites" comment on
  `logout_seen_` in `session.hpp`) MUST be corrected or removed with the change. Each one MUST name 104's rule where
  it supersedes 024's (header-comment rule, issue #310).

**Unasked 141=Y acknowledgement (B107, #695)**

- **FR-007**: In LogonSent, a Logon acknowledgement carrying 141=Y MUST be recognised as a reset before it is
  compared with NextNumIn. An acknowledgement with 141=Y at 34=1 MUST NOT end the session as too low, whatever
  NextNumIn was. On the asked path (fixpp's own Logon carried 141=Y) NextNumIn is already 1 when the
  acknowledgement is compared, so that path keeps its `check_inbound` (FR-010; research D3.1).
- **FR-008**: On such an acknowledgement that fixpp did not ask for (lenient or unilateral policy, no 141=Y on fixpp's
  own Logon), both sequence numbers MUST be reset: inbound continues at 2 after the acknowledgement's 34=1, and
  outbound restarts at 1. At a 34 = M > 1 the acknowledgement is not consumed: inbound next 1, outbound next 1, and
  the peer's next frame draws a ResendRequest from 1 (research D4, ruling O-2). fixpp MUST NOT send a further Logon.
  This follows QuickFIX/J and QuickFIX/Go (clarification 2026-11-17). The plan derives, from the three engines, how
  fixpp answers a peer's ResendRequest that follows the reset.
- **FR-009**: The reset MUST be durable on a persistent store, with the guarantees 093's reset unit gives today (E-1):
  ordering against `close()` and failure handling. On a direct mTLS initiator the unasked unit MUST run only after
  the live identity is authorised for the CompID (clarification N-8).
- **FR-010**: A strict initiator's handling of its own acknowledgement MUST be unchanged, and so MUST that of any
  initiator whose own Logon carried 141=Y (a lenient one whose reset knob put it at (1, 1) before its Logon): both
  are the asked case (research D3.1).
- **FR-011**: L-103-2 MUST be retired, and its B&L row moved to the closed file with 104's resolution.

**Too-low frame in the Logon arms (#701)**

- **FR-012**: In LogonSent and in NotConnected, a Logon (35=A) whose MsgSeqNum is below NextNumIn, whatever its
  PossDupFlag(43) (erratum E-2: FIX-SL §4.3.11 has no PossDup exception for the Logon; QuickFIX/Go), and not exempted
  by FR-007, MUST draw a Logout whose Text(58) is exactly "MsgSeqNum too low, expecting N but
  received M" (N = NextNumIn before the frame, M = the frame's 34 as parsed; both written in decimal by
  `std::to_chars`, with no padding and no sign, and the peer's bytes never echoed; research D5 "The text"). The
  session then ends `Disconnected`. Only a Logon reaches the sequence check in these arms: `interpret_logon` refuses
  any other first frame before it, with its own ending, so a Logout (35=5) in LogonSent never reaches the sequence
  check, whatever its 34: with `reset_on_logout` set arm S's branch ahead of `interpret_logon` takes it (contract
  RR-1 row S, W10, O19), and with it off `interpret_logon`'s non-Logon refusal does (W11, clarification
  2026-11-18), so it never draws the too-low Logout. The Logout MUST NOT be
  sent before the arm's identity gates pass: the mTLS CompID-authorisation gate in both arms, and `authorize_logon`
  on the acceptor. A frame they refuse takes the gate's own silent ending (FIX-TC 1S(c); clarification N-8). On an
  engine-managed initiator the peer's identity is authorised at connect, before its Logon is sent
  (`reconnect_fsm.cpp`'s step 7), so the in-arm gate decides only for a direct mTLS session (research D3.1).
- **FR-013**: That Logout MUST be numbered with the session's next outbound number, and NextNumIn MUST NOT be
  advanced, as in B-103-10. It MUST carry Text(58) only, with no SessionStatus(1409) and no
  NextExpectedMsgSeqNum(789) (clarification 2026-11-17; contract T1).
- **FR-014**: A Logout that cannot be sent (build, store or transmit failure) MUST NOT stop the session from ending.
  The ending MUST be the one fixpp's existing Logon-arm Logout helpers give that failure, stated in the B&L row.
- **FR-015**: The too-low Logout MUST NOT be a `reset_on_logout` trigger (FR-001).
- **FR-016**: L-103-7 MUST be retired, its B&L row moved with 104's resolution. 103's spec row "A too-low frame in
  NotConnected or LogonSent still ends silently" is superseded by name.

**Peak-memory basis (owner, 2026-11-17)**

- **FR-017**: After 104's own tests land, the #266 basis MUST be re-recorded in `ci/ctest-parallelism-probe.md` and
  `ci/expected-eligible-tests.txt` in one commit. The values MUST be derived from a CI run at 104's head, never copied
  from this spec.

**Records**

- **FR-018**: Each behaviour change MUST carry a B&L row (`B-104-*`), and each disclosed residual an `L-104-*` row.
  Every departure from a QuickFIX engine MUST be listed once, in this spec's "Departures from QuickFIX".

### Key Entities

- **Reset trigger under `reset_on_logout`**: a Logout received from the peer and processed as far as its reset
  point (contract RR-1). It replaces "a Logout in either direction".
- **Logon acknowledgement**: the peer's Logon received in LogonSent. Its 141 and 34 decide whether it resets, is in
  sequence, is too high, or is too low.
- **Too-low Logout**: a Logout whose Text is "MsgSeqNum too low, expecting N but received M". After 104 every arm
  sends it (Active since 103).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A graceful close with `reset_on_logout`, whose Logout goes unanswered, leaves both numbers and the store
  unchanged, whether it ends by the timeout or by a drop. Each case has a cell that is RED on the base and GREEN on
  104.
- **SC-002**: A lenient or unilateral initiator at NextNumIn > 1 reaches Active on an acknowledgement with 141=Y at
  34=1, with inbound next 2 and outbound next 1, and sends no second Logon (FR-008). The cell is RED on the base.
- **SC-003**: A too-low acknowledgement (initiator) and a too-low Logon (acceptor) each produce exactly one Logout
  with the FR-012 text on the wire, then `Disconnected`. Each cell is RED on the base, which sends no Logout.
- **SC-004**: Every existing `reset_on_disconnect`, peer-initiated `reset_on_logout` and not-sent-Logout witness stays
  GREEN, unedited unless FR-005 names it.
- **SC-005**: On 104's CI run, the #266 report reads EVIDENCE for every lane whose Test step succeeded.
- **SC-006**: Every new behaviour has a mutant: delete the mechanism and the cell goes RED. A forced-hit arm is
  included where a bounded wait or a timer could satisfy a cell by another route (the repo's anti-vacuity rules).

## Assumptions

- Gate A is required: the feature changes session wire behaviour and a reset contract.
- There is no C-ABI change. The configuration keys and their defaults are unchanged; only `reset_on_logout`'s trigger
  changes.
- Where the three engines differ from each other, the choice and its authority are a row of "Departures from
  QuickFIX" below (FR-008's follow-up, D4's M > 1, RU-2's LogonSent Logout, a reply whose `toAdmin` throws).
- The text "MsgSeqNum too low, expecting N but received M" is QuickFIX's and Test Case 2c's, as 103 adopted for Active.
- The Engine does not reconnect a session in place. Each connection of an engine-managed initiator ends in a terminal
  close (`run_connect_loop`), so the teardown reset runs per connection on the Engine route.

### Departures from QuickFIX

The one list (FR-018) of 104's observable departures from QuickFIX C++ (QFC), QuickFIX/J (QFJ) or QuickFIX/Go
(QFGo), each with its authority. research.md, data-model.md and the contract refer to these rows and do not keep
their own list. A row marked *pre-existing* is a departure 104 touches the surface of but does not create.
Engine behaviour restated elsewhere (the contract's Outcomes "QuickFIX" column, data-model's B-104-* and L-104-*
rows) is subordinate to this table: on a difference this table wins, and a restatement that shows a departure not
listed here is a defect of that restatement. The B&L row that carries each *pre-existing* row to operators: DQ-6,
L-104-3; DQ-7, B-104-6; DQ-12a, DQ-13 and DQ-14, B-104-1's departure clause.

| # | Departure | Engines it departs from | Authority |
|---|---|---|---|
| DQ-1 | No second Logon after an unasked 141=Y acknowledgement (FR-008) | QFC (replies with Logon 141=Y at 34=1) | owner clarification 2026-11-17; QFC's reply is an untested side effect of `f26ca5fc`, reported as quickfix/quickfix#417; QFJ removed it in QFJ-380 |
| DQ-2 | An unasked 141=Y acknowledgement at 34 = M > 1 continues at inbound 1, and the ResendRequest from 1 waits for the peer's next frame (D4, L-104-5) | QFC (continues at 2); QFJ and QFGo (request at Logon) | orchestrator ruling O-2 (QFJ and QFGo's outcome, one frame later) |
| DQ-3 | A ResendRequest answered with nothing sent at or above its BeginSeqNo writes GapFill(34=B, 36=next), (1, 1) included (L-104-1) | QFC and QFJ at next outbound 1; QFGo always | research D3.3 (benign; QFC and QFJ send the same shape at begin = next > 1) |
| DQ-4 | 789 evaluated after an unasked reset (D3.2, L-104-6) | QFGo (skips 789 when 141 is present) | QFC, QFJ |
| DQ-5 | A too-low Logon or acknowledgement with 43=Y draws the Logout (E-2, D6, L-104-4) | QFC, QFJ (PossDup handling, then their logon timeout) | FIX-SL §4.3.11; QFGo |
| DQ-6 | The posture, strict and SendingTime refusals and the 1137 Reject answer before the identity gates on an mTLS acceptor, and the posture and SendingTime Logouts before the gate on a direct mTLS initiator (L-104-3) | none (no engine binds certificates to CompIDs); FIX-TC 1S(c) | *pre-existing*; 104's too-low Logout and unasked reset come after the gates (clarification N-8) |
| DQ-7 | The Logon arms' sequence check is made with `validate_sequence_numbers` off (B-104-6) | QFJ (skips it at Logon) | *pre-existing*, unchanged |
| DQ-8 | The too-low Logout leaves the transport open until EOF, `close()` or stop (as B-103-10, L-103-5) | QFC, QFJ (close at once) | 103 precedent |
| DQ-9 | No reply Logout to a Logout received in LogonSent; the reset is made (RU-2) | QFC, QFJ (reply with a Logout, then reset) | orchestrator ruling RU-2; FIX-TC 1B(e) makes that Logout optional |
| DQ-10 | `onLogout` fires before the receipt reset, on the edge into Disconnected (RU-3) | QFC, QFJ (`disconnect()` fires it after `m_state.reset` / `resetState()`) | orchestrator ruling RU-3; nothing on the wire differs |
| DQ-11 | A reply Logout whose `toAdmin` throws makes no reset (contract O6) | QFJ (`sendRaw` catches the `Throwable`, sends, resets) | QFC (the throw leaves `nextLogout`); RU-1 |
| DQ-12 | A too-high Logout whose bound ends unacknowledged makes no reset under `reset_on_logout` (contract O18) | QFC, QFJ (process a too-high Logout at receipt, `verify(logout, false, false)`) | RU-1 on 103's bound (B-103-7) |
| DQ-12a | A too-high Logout gets no `fromAdmin`, at its receipt or at its acknowledgement, so at B's reset point no `fromAdmin` throw or reject can prevent the reset, as one does on arm A (contract O2, O3) | QFC (calls `fromAdmin` at receipt, inside `verify`) | *pre-existing* (103 Risk R8, 103 spec's departure "No `fromAdmin` for a too-high ResendRequest or Logout"); 104 adds only the reset at the acknowledgement |
| DQ-13 | The Logout reply is sent before `fromAdmin` runs, so a `fromAdmin` throw or reject follows a reply already on the wire (contract O2, O3) | QFC, QFJ, QFGo (`fromAdmin` runs inside `verify`, before the reply) | *pre-existing* (019 T016); 104 adds only "no reset" |
| DQ-14 | A Logout received in LogoutSent or LogonSent resets with no CompID, SendingTime or `fromAdmin` check of that Logout | QFC, QFJ (`verify` checks CompID and SendingTime and runs `fromAdmin` first) | *pre-existing* acceptance of that Logout (the LogoutSent arm's "confirm" row); 104 adds the reset |
| DQ-15 | An explicit EndSeqNo at or above the next outbound number is read as through-current (D3.3a, B-104-5). With BeginSeqNo B above the next outbound number the answer is GapFill(34=B, 36=N_out), NewSeqNo below MsgSeqNum | QFGo (sends nothing: its walk's `seqNum != nextSeqNum` is false) | QFC (`nextResendRequest`'s clamp, then `generateRetransmits`' `generateSequenceReset(B, next)`) and QFJ (`generateSequenceResetIfNeeded`), identical frames; it restores QFC's and QFJ's answer where fixpp wrote `EndSeqNo + 1` |
| DQ-16 | Against a counterparty acceptor whose `toAdmin` injects 141=Y, the session ends when fixpp's outbound number reaches the number of its own Logon (L-104-7) | none: every QuickFIX initiator meets the same ending, and QFC's second Logon does not avoid it (orchestrator ruling O-1, by source) | listed because it is the consequence of DQ-1 an operator meets; not traced on a live peer |

## Normative References

Entries are the exact keys of `spec/coverage-index.md` (Article VI §5), with the matching recipe of 103's spec.

- `[FIX-SL §4.3.11] Initial synchronization of messages (Logon seqnum check, ResendRequest on gap)`: a Logon below
  NextNumIn draws a Logout and a disconnect (FR-012).
- `[FIX-SL §4.4.2] Using ResetSeqNumFlag(141) for 24-hour connectivity`: the reset procedure, with both numbers at 1
  (FR-007, FR-008).
- `[FIX-SL §4.4.3] Using ResetSeqNumFlag(141) during connection establishment`: the reset at Logon is by agreement
  (FR-008's context).
- `[FIX-SL §4.6] FIX connection termination`: the Logout initiator waits for the peer's acknowledgement (FR-001,
  FR-002).
- `[FIX-SL §4.7.1] Using NextExpectedMsgSeqNum(789) on invalid MsgSeqNum(34)`: the optional 789/1409 on a too-low
  Logon's Logout. FR-013 does not send them (clarification 2026-11-17).
- `[FIX-SL §4.8.1] Ordered message processing`: a too-low message without PossDupFlag ends the session with a
  Logout (FR-012).
- `[FIX-SL §4.10.2] FIX logout process state transition diagram`: "Receive Logout Response → Disconnect" (FR-003).
- `[FIX-TC §4 scenario 1B] Connect and Send Logon (Initiator)`: sub-case 1B(d), an invalid Logon acknowledgement
  draws a Logout with Text, then a disconnect (FR-012); sub-case 1B(e), a non-Logon in reply, makes the Logout
  optional (RU-2, DQ-9).
- `[FIX-TC §4 scenario 1S] Receive Logon (Acceptor)`: sub-case 1S(c), an unauthenticated or non-configured
  identity is disconnected without a message (FR-012's identity gates, clarification N-8).
- `[FIX-TC §4 scenario 2] Receive Message Standard Header`: sub-case 2c, MsgSeqNum too low (FR-012).
- `[FIX-TC §4 scenario 13] Receive Logout message`: the acknowledgement of a Logout (FR-001, FR-003).

Informative, not coverage-index entries:
- the ResetSeqNumFlag(141) field definition and §11.10 of FIX-SL;
- the LFIXT profile's §5.4.7, which exempts a 141=Y Logon from the too-low error;
- the three engines' session source at the tags above, for observable behaviour.

## Out of Scope

- A Logout that was never sent: B28's and #589's ruling stands (B-538-7's first half).
- The Engine's multi-cycle reconnect, which is deferred (E-1a).
- A general rule for 141=Y on an Active session (24-hour reset, 030's received-reset path): unchanged.
- The strict initiator's silent refusal in LogonSent, unless the plan finds it shares the arm change (see Edge Cases).
