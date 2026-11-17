# Research: 104 sequence-reset fidelity

Base `bf294f00`; the session code is 103's squash `b3df5d45`. `sc` = `src/session/session.cpp`, `sh` =
`include/fixpp/session/session.hpp`. Every line number is a lead taken at the base; the symbol names are the
citation. The engines are read at QuickFIX C++ `v1.16.0` (QFC, `src/C++/Session.cpp`), QuickFIX/J
`QFJ_RELEASE_3_0_2` (QFJ, `quickfixj-core/src/main/java/quickfix/Session.java`) and QuickFIX/Go `v0.9.12` (QFGo),
from `~/Work/Programming/Antreprenoriat/reference-engines/`. The parent repo holds the earlier readings under
`research/probes/b107_695_104/` (`quickfix_reading.md` = QR, `fix_docs_reading.md` = FD, `q1_quickfix_history.md`)
and `research/probes/b41_563_564/ph8a_readings.md` §1 (PH8A). Nothing in this file was run. A statement is
**read** from the lines cited, or marked **inferred**.

## Decisions

### D1 — `reset_on_logout` resets at receipt of the peer's Logout (B53, #595; FR-001 to FR-003, FR-006)

#### Decision

1. **The trigger.** `logout_seen_` keeps its name; its contract becomes: *a Logout from the peer was received,
   and the reset it calls for under `reset_on_logout` has not yet been attempted.* A rename was weighed and not
   taken: the name is read in merged design records (`.specify/536-537-538-recovery-liveness.md`,
   `.specify/563-564-callback-send.md`), the B&L and `brain/`, and the smaller diff is the comment. Phases 2 and 9
   re-read every hit of `git grep -n logout_seen -- tests src include bindings tools spec brain .specify` and
   correct the prose that states the old rule.
   - It is set where a peer Logout is received, and only there:
     - `on_inbound_active_logout_` (a request in Active, or in LogonReceived);
     - `on_inbound_active_too_high_logout_` (103's too-high Logout);
     - `on_inbound_logout_sent_` (the response to fixpp's own Logout). This set is **new** (FR-003).
   - `run_logout_phase1`'s set after its emit (sc ~8236) is **deleted**. With it goes the "site (a)" half of the
     member's comment (FR-006).
   - It is cleared where the reset is attempted (item 3), and where an in-place reconnect's Logon consumes it
     (item 5).
   - The new header comment names 104 and the 024 T013 clause it supersedes. It states the condition and the
     re-derivation recipe (`git grep -n logout_seen_ -- src include`), and no count of sites.
2. **The reset at receipt.** When `cfg_.reset_on_logout` is set and the flag is set, the arm runs the reset
   unit `run_reset_unit_(seqnum_min, seqnum_min)` (093 contract C-6). It then runs the arm's step-5 restore,
   `reset_cancellation_state(enable_total_cancellation{})`, as `on_inbound_logon_sent_` does after its unit.
   The position in each arm follows QFC `nextLogout` (`Session.cpp:312-330`): the reply, then the target
   increment, then `m_state.reset`, then `disconnect()`, whose `onLogout` comes after the reset. It also follows
   QFJ `nextLogout` (`Session.java:1459-1488`) and QFGo `handleLogout` (`in_session.go:89-110`):
   - **`on_inbound_active_logout_`**: after the reply Logout (`inbound_logout_leaf_`), the
     `sequence_numbers_reset` event, `fromAdmin` and `persist_inbound_advance_`, and before
     `record_state_transition_(Disconnected)`. So `onLogout`, fired on that edge, sees `(1, 1)` as in QFC and QFJ.
   - **`on_inbound_logout_sent_`**: before its `record_state_transition_(Disconnected)`, before
     `logout_confirmed_ = true` and before `logout_wait_wake_()`. Today that arm writes Disconnected first.
     On this route `onLogout` has already fired: `record_state_transition_` fires it on any edge out of Active,
     here phase 1's `Active → LogoutSent`, before any answer, so it never observes the reset. What the receipt
     rule fixes here is where the reset runs: in LogoutSent, with the transport still open, before `close()`'s
     phase 2, where the base made it in phase 2's teardown. QFC and QFJ call `onLogout` from `disconnect()`, after
     their reset. fixpp's earlier `onLogout` on a local close predates 104 and is not changed.
   - **`acknowledge_peer_logout_`**: after its leaf and event, before its Disconnected write. That one site
     covers the immediate acknowledgement (T = 0, no clock, a Logout in flight, closing or stopped) and the bound's
     (`run_peer_logout_bound_`).
3. **Clearing.** The flag is cleared immediately before the unit. One receipt gets one attempt; there is no
   retry, whatever the unit returns.
4. **Store failure at receipt: logged, then proceed.** This is 024 I-07's disposition for the teardown reset it
   replaces.
   - A manager error or a store error from the unit is logged. The arm then records Disconnected, as it would
     anyway, and returns what it returns today.
   - QFGo does the same (`in_session.go:105-109`: `dropAndReset` error → `logError`, then `latentState`).
   - One condition, one ending: the session ends `Disconnected` in every case. On a persistent store that refused,
     the stored numbers keep their pre-reset values (L-104-2).
5. **Backstops for an owed reset whose arm ended another way.** A receipt can end before the reset step:
   - a `fromAdmin` throw, a late parse failure or a failed inbound persist in `on_inbound_active_logout_`;
   - a too-high Logout's bound exiting unacknowledged because `close()` claimed the mark, a stop, another
     ending's Disconnected, or a reconnect drain.

   Each of these leaves the flag set, and the first of two points makes the reset:
   - **`Session::close`'s teardown.** Its predicate keeps today's shape,
     `(logout_seen_ && cfg_.reset_on_logout) || cfg_.reset_on_disconnect`. The spec's edge case
     ("[the too-high Logout] already sets the flag on receipt, and that is unchanged") holds.
   - **An in-place reconnect's Logon.** `emit_initiator_logon_` widens `reset_before_logon` with
     `(cfg_.reset_on_logout && logout_seen_)` and clears the flag after its fatal-disposition reset.
     The new Logon is then built at `(1, 1)` and carries 141=Y by 024 C2.1's OR-of-three. That is what a
     QuickFIX engine's next Logon carries after a `ResetOnLogout` reset (QFC `shouldSendReset`,
     `Session.cpp:1036-1040`).
6. **`close()` waits for an in-flight unit before its teardown predicate, not inside it.**
   - Today the bounded wait (`reset_unit_in_flight_ && effective_clock_`, bounded by `logon_timeout_ms`, event
     `session_event_close_reset_wait_expired`) sits inside the `if (!teardown_reset_done_ && …)` block (sc
     ~2491-2515). With only `reset_on_logout` set, and the flag cleared at receipt, that block is not entered, so
     nothing would wait for a receipt reset in flight (FR-001: "`close()` MUST wait for it").
   - The wait moves immediately ahead of the block, unchanged otherwise. It still needs `effective_clock_`:
     without a clock there is no bound, and close() proceeds as today (B-104-2 states it).
   - Consequence: a `close()` that finds a Logon-arm 141=Y unit in flight now waits for it even when no teardown
     reset follows. That is bounded, and the only observable effect is when `close()` returns.
7. **The LogoutSent race.** While the receipt unit's store call is suspended, `close(graceful)`'s grace timer
   (`logout_disconnect_timeout_ms`, armed before phase 1) and `run_logout_phase1`'s deadline can expire.
   - Then phase 1 returns `session_logout_timeout`, which close() logs and proceeds past. Phase 2 force-closes the
     transport, joins the liveness loop and the bound, and waits for the unit (item 6).
   - The arm resumes, sees the state already Disconnected (its write is guarded by `!= Disconnected`), and sets
     `logout_confirmed_` harmlessly.
   - `close(graceful)` returns ok, as it does after a timeout today. The numbers end `(1, 1)`: the unit completes
     before the teardown. The Logout reply was received before the deadline, so whether the timer or the arm
     wins changes only which branch of phase 1 is logged.
8. **Unchanged.**
   - `reset_on_disconnect` (C4.1, C4.2).
   - `teardown_reset_done_`.
   - The Logouts fixpp sends on an error path: SendingTime, posture, strict, the gap deadline, Arm B, and 104's
     too-low Logout. None sets the flag (FR-015).
   - The not-sent and cut Logouts (B-538-7's first half, B-563-5). No flag is set for them now because nothing
     sent sets it.
   - The `sequence_numbers_reset{by_peer_request=false}` event that `on_inbound_active_logout_` and
     `acknowledge_peer_logout_` emit on every acknowledged peer Logout, knob or not. It predates 104 and 104
     does not touch it. Noted because an operator may read it as a reset.

#### Rationale

- All three engines reset for `ResetOnLogout` at one place: the processing of a received Logout. Each logout
  timeout only disconnects, and only `ResetOnDisconnect` resets there. Read: QFC `nextLogout` `:325-327` and
  `disconnect()` `:638-640`; QFJ `nextLogout` `:1484-1485` and `disconnect` `:2169`; QFGo `handleLogout`
  `:105-109`. PH8A §1 has the same reading for the sent-and-not-transmitted case.
- The FIX docs define no reset on Logout (spec clarification 2026-11-17).
- The unit, not `reset_seqnums_to_one_durable`, is used because:
  - it is one store operation (`MessageStore::reset_to`);
  - it runs on an empty cancellation slot, so `Engine::stop()`'s total emission cannot cut it (093 OD-25, OD-26);
  - it sets `reset_unit_in_flight_`, which is what `close()` waits on. That gives FR-001 and FR-009 "the
    guarantees [the] reset unit gives today" without a second mechanism.
- Clearing the flag at the attempt and consuming an owed reset at the next Logon is what removes 103's latched
  `logout_seen_` consequence (spec Edge Cases, "An in-place reconnect"). Nothing received on an old connection
  resets the new one at its teardown, and nothing received goes unreset.

#### Alternatives considered

- *Keep the teardown reset and only stop setting the flag at emit.* Rejected by the owner's clarification Q3
  ("At receipt"). It is also observably different: `onLogout` would see the pre-reset numbers, and an in-place
  reconnect would log on at the old numbers after a received Logout.
- *`reset_seqnums_to_one_durable(logged)` at receipt.* Rejected: it is two store-facing steps, it is cancellable
  by stop's total emission on a contended lock, and `close()` cannot order its teardown reset after it.
- *Reset at receipt of a too-high Logout, before the ResendRequest.* Rejected: NextNumIn would become 1, so the
  ResendRequest and the bound's "N passes S" test would act on the reset space. 103's bound is fixpp's
  equivalent of QuickFIX processing the Logout, so the reset goes with its acknowledgement (erratum E-3).
- *Clear the flag at `install_reconnected_transport` (103 D1's pattern).* Rejected: an owed reset, from an
  unacknowledged too-high Logout drained by the reconnect, would then never run. QuickFIX would have reset at
  receipt.
- *A teardown retry after a failed receipt reset.* Rejected: two endings for one condition. Under
  `reset_on_disconnect` the teardown reset runs anyway.

**Sites:** `Session::on_inbound_active_logout_`, `Session::on_inbound_active_too_high_logout_`,
`Session::on_inbound_logout_sent_`, `Session::acknowledge_peer_logout_`, `Session::run_logout_phase1` (the
deleted set), `Session::emit_initiator_logon_` (`reset_before_logon`), `Session::close` (the wait's position, and
the comment on the predicate), and `sh` (the member and its comment, plus `drive_reconnect`'s latch paragraph:
"a Logout seen on the old connection still triggers reset_on_logout's teardown reset" becomes item 5's rule).

### D2 — 024 C5.1 is kept as written; with both knobs a peer Logout makes two store resets (FR-004)

- **Decision.** C5.1 says the durable reset "fires **at most once** observably per teardown" and collapses
  overlapping triggers *on one teardown*. The receipt reset is not a teardown reset, so C5.1's text holds
  unchanged: the teardown makes at most one reset, through `teardown_reset_done_`.
- **Consequence.** With `reset_on_logout` and `reset_on_disconnect` both set, a received peer Logout makes one
  store reset at receipt and one at teardown, and both numbers end at 1 (FR-004). An unanswered close makes one
  reset, the teardown's.
- **Rationale.**
  - Every observable is equal under the alternatives: the wire, the final numbers and the store state. Only the
    count of store I/O differs, and it is not on the wire.
  - QFC and QFJ also reset twice on this path: `nextLogout`'s reset, then `disconnect()`'s `ResetOnDisconnect`
    (QFC `:325-327`, `:638-640`; QFJ `:1484-1485`, `:2169`).
  - Keeping C5.1 literal needs no new member, so there is no `sizeof(Session)` change and no new
    connection-scoped latch to clear at reconnect.
- **Alternatives.** *Collapse the pair with a connection-scoped "receipt reset done" latch, cleared at
  `install_reconnected_transport`, and evaluate it after item 6's wait.* Rejected: it adds a member, a clear site
  and a race rule, to save one FileStore rename on a path QuickFIX also resets twice. The spec's paraphrase of
  C5.1, "(a single store reset)", is per teardown (erratum E-4).

### D3 — An unasked 141=Y acknowledgement is a reset before the sequence check (B107, #695; FR-007 to FR-011)

#### D3.1 The arm

**Site:** `Session::on_inbound_logon_sent_`, Guard (4) and the 141 block after it.

- `peer_ack_sent_reset_flag = (hdr.reset_seqnum_flag == "Y")` moves ahead of `seqnum_mgr_.check_inbound(seq)`.
- **The unasked case.** It is `peer_ack_sent_reset_flag && !own_logon_sent_reset_flag_`: fixpp's own Logon
  carried no 141=Y. Here `check_inbound` is not called, and `logon_inbound_advanced_init = (seq == seqnum_min)`.
  So the acknowledgement's 34 is compared with 1, the NextNumIn of the reset space, and never with the hydrated N.
  `seq == 0` has already ended the arm. M > 1 is D4.
- **The asked case and the no-141 case** keep today's `check_inbound`. This covers a strict initiator, and a
  lenient one whose reset knob put it at `(1, 1)` before its Logon. FR-010: the strict path is byte-identical.
  For the asked case NextNumIn is already 1, so "recognised as a reset before it is compared" holds
  (`emit_initiator_logon_` resets first when `reset_before_logon`; 024 C2.1's 141=Y needs `{1,1}`).
- The rest of the arm is unchanged, in this order:
  - 1137 (099);
  - the strict mismatch test (it cannot fire here: strict always asks);
  - the unit's targets, from the code that is there. `unit_in` is 2 when consumed, else 1. `unit_out` is 1,
    because `own_logon_sent_reset_flag` is false. FR-008: inbound next 2, outbound next 1;
  - `run_reset_unit_`, with 030's persistent-fatal disposition and step 7's superseded test;
  - the event `by_peer_request = true`;
  - mTLS, then the 789 honour, then Active;
  - the persist guard, skipped because `peer_ack_sent_reset_flag` is set.
- No second Logon: no code sends one, and QFJ and QFGo send none (QR Part B; spec clarification).
- **FR-009** holds by construction: it is the same unit, the same `close()` wait and the same dispositions.

#### D3.2 The 789 honour after the reset

- `honor_peer_next_expected_` runs after the unit against `peek_outbound()`, which is now 1. A counterparty that
  injected 141=Y and also sends 789 sends the 789 its own pre-reset state computed: X = N_out + 1 in QR Part C's
  QFC trace, where `generateLogon` sets 789 before `toAdmin`. So X > 1, and fixpp sends Logout "NextExpectedMsgSeqNum too high,
  expecting 1 but received X" and records Disconnected.
- QFC (`:228-236`, after its reset at `:206`) and QFJ (`:2263`, after `resetState` at `:2222-2224`) do the same.
  QFGo skips 789 when 141 is present (`session.go:571`).
- **Decision: unchanged**, with the QFGo departure listed. It needs `enable_next_expected_msg_seq_num` and a 789
  on the acknowledgement.

#### D3.3 What the peer sends next, and fixpp's answer

- **The peer.** By QR Part C, a QFC or QFGo acceptor that injected 141=Y through `toAdmin` reset its own store
  in `sendRaw`/`prepMessageForSend`. It then finds fixpp's Logon at 34 = N_out too high and sends
  ResendRequest(34=2, 7=1, 16=0). QFJ with `LogonTag=141=Y` instead disconnects itself after the acknowledgement
  (QFJ `:2290-2292`).
- **fixpp's numbers.** After D3.1, NextNumIn is 2, so the ResendRequest is in sequence, and next outbound is 1.
- **The engines' answer to RR(7=1, 16=0) at next outbound 1, persistent store:** nothing is sent.
  - QFC clamps EndSeqNo to `getExpectedSenderNum() - 1 = 0` (`:378-382`). `generateRetransmits(1, 0)` retrieves
    nothing, and its tail `if (endSeqNo > msgSeqNum)` (`:495`) is `0 > 0`.
  - QFJ clamps the same way (`manageGapFill`, `:1360-1370`). In `resendMessages(1, 0)`,
    `generateSequenceResetIfNeeded`'s `endSeqNo > msgSeqNum` (`:2458`) is false.
  - QFGo clamps the same way (`in_session.go:204-208`). `IterateMessages(1, 0)` yields nothing, and
    `seqNum != nextSeqNum` (`:279`) is false.
- **fixpp's answer today.** `replay_outbound_range_` takes its `!store_ || our_last == 0 || begin > eff_end`
  branch and writes one GapFill at `begin`.
  - With 16=0 it is GapFill(34=1, 43=Y, 123=Y, 36 = `peek_outbound()` = 1). NewSeqNo equals the receiver's
    expected number, which all three treat as a no-op: QFC `nextSequenceReset` `:356-360`, QFJ `:1556-1579`, QFGo
    `handleSequenceReset` `:166-176`. Neither branch fires, and no Reject is sent.
  - **Decision: kept**, disclosed as L-104-1. It is benign, and fixpp sends the same GapFill(N, N) shape that QFC
    and QFJ send when begin = next > 1.
  - **With an explicit EndSeqNo** (16 = 999999 on FIX.4.0/4.1, which all three engines send for those versions,
    or a QFJ chunked request) fixpp writes NewSeqNo = `requested_end + 1`. The peer then expects 1 000 000 (or
    end + 1) while fixpp's next frame is 34=1, and the peer logs fixpp out as too low. *Inferred from the three
    `nextSequenceReset` bodies cited above; not run.*
  - 104's FR-008 is the route that makes `our_last == 0` reachable in Active: an acknowledgement that restarts
    outbound at 1 before fixpp has sent anything at 1. So the defect is in scope.
- **Decision (D3.3a).** In `replay_outbound_range_`, a requested end at or above the next outbound number reads
  as through-current.
  - **Where.** The clamp sits inside `replay_outbound_range_`, not at its callers. It goes after the gate's wait
    for the open shared holds and after the store tail read, so a send numbered while the walk waited cannot move
    the comparator.
  - **The comparator.** With a store, it is the tail read's next outbound number (`our_last + 1`). Without a
    store, it is `seqnum_mgr_.peek_outbound()`, read at the same point. The parameter `end_is_through_current`
    becomes `end_is_through_current || requested_end >= <comparator>`.
  - **The callers.** `answer_resend_request_` passes `rr_end == 0`. `honor_peer_next_expected_`'s 789 walk
    already passes `true`, so the clamp cannot change it. Re-derive: `git grep -n "replay_outbound_range_(" src`.
  - That is the clamp all three engines apply (above). The empty-range GapFill's NewSeqNo is then
    `peek_outbound()`.
  - It also corrects the same overshoot in Active with no reset: begin above the last sent number, with an
    explicit end beyond it. There fixpp wrote NewSeqNo = end + 1 above its own next number. The phase-1 census
    finds any cell that pins the old value.
- **After that.** Inferred, not traced (OPEN O-1): the QFC peer keeps its queued copy of fixpp's Logon at N_out
  (`doTargetTooHigh` queues it, `:1121-1143`). When its expected number reaches N_out, `nextQueued` consumes the
  queued Logon by incrementing past it (`:1156-1157`), so fixpp's own frame N_out would then read as too low. A QFJ or QFGo initiator in
  fixpp's place, which also sends no second Logon, would meet the same thing.

#### Rationale

All three initiators accept an unasked 141=Y acknowledgement and reset both numbers (QR Part B). The FIX-SL
field definition and §4.4.2 treat 141=Y as a reset of both sides (FD). The asked path is left alone (FR-010).

#### Alternatives considered

- *Skip the sequence check for every 141=Y acknowledgement.* Rejected: it would change the strict and knob
  paths, against FR-010.
- *Answer an empty-range request with no frame.* That would match all three engines at next outbound 1, but it
  differs from QFC and QFJ at begin = next > 1. It also changes 103's E5 cell shape. The benign GapFill is kept
  (L-104-1).

### D4 — A 141=Y acknowledgement at 34 = M ≠ 1 on a lenient or unilateral initiator (US2 AS4)

**The engines, read:**
- **QFC** accepts and resets. `isTargetTooHigh(M) && !resetSeqNumFlag` is false (`:254`), so it increments its
  target to 2 and sends no ResendRequest (it also replies with its own Logon).
- **QFJ** accepts and resets (`verify` skips the sequence checks when a reset was received, `:1831`). Then
  `isLogonInNormalSequence` is false (`:2250`), and `doTargetTooHigh(logon)` (`:2308`) queues the Logon and sends
  ResendRequest from 1.
- **QFGo** accepts and resets (`session.go:530-544`). Then `checkTargetTooHigh` (`:587`) returns `targetTooHigh`,
  and `doTargetTooHigh` sends ResendRequest(1, 0) and enters `resendState` (`logon_state.go:47-53`).
- None ends the session. No QuickFIX engine produces this acknowledgement: each one's injection path resets in
  `sendRaw` and stamps 34=1 (QR Part C).

**fixpp at the base:**
- M < N: silent Disconnected.
- M = N: Active, inbound next 2 (QFC's outcome).
- M > N: silent Disconnected, or with 789 on, tolerated with inbound next 1.

**Decision (provisional, owner confirmation O-2):**
- M > 1 in the unasked case is not consumed. D3.1's `logon_inbound_advanced_init` is false, so `unit_in` is 1.
- The session goes Active with inbound next 1 and outbound next 1, and sends no second Logon.
- The peer's next frame (34 > 1) is too high in Active, and fixpp's existing Arm A sends ResendRequest(7=1, 16=0).
- So fixpp reaches QFJ's and QFGo's request one frame later than they send it (L-104-5). It departs from QFC's
  "continue at 2".
- No new mechanism: this is the "proceed, not advanced" disposition that 027's behind-side tolerance already
  uses on the same arm.

**Rejected:**
- *Compare with 1, then take the existing too-high ending.* It turns today's M = N acceptance into a silent
  end, which departs from all three engines.
- *Send the ResendRequest from the LogonSent arm before Active.* It needs AwaitingResend entered before Active,
  and a ResendRequest emitted from an establishment arm. There is no such leaf there today: `enter_awaiting_resend`
  and `inbound_resend_request_leaf_` serve Active. That is a new mechanism for a case no QuickFIX engine produces.
- *QFC's outcome (inbound next 2 for every M).* It is equally cheap. It is not chosen because the clarification
  followed QFJ and QFGo, and two of the three engines request the gap. It is the owner's alternative in O-2.

### D5 — A too-low frame in a Logon arm draws QuickFIX's Logout (#701; FR-012 to FR-016)

#### Decision

- **Sites:** the `!chk` branch after `seqnum_mgr_.check_inbound(seq)`, in `Session::on_inbound_logon_sent_` and in
  `Session::on_inbound_not_connected_`. When `chk.error() == session_seqnum_too_low` the arm builds the text and
  returns `co_await refuse_logon_with_logout_(text, <arm>)`. Every other refusal keeps its present ending: too
  high, 789 tolerance, a drained mutex.
- **The helper.** `refuse_logon_with_logout_` is reused, not `inbound_logout_leaf_`, and not
  `inbound_logon_sent_logout_leaf_`:
  - it is already called from both Logon arms (the posture refusal, and the strict refusal in NotConnected);
  - it calls `toAdmin` before `assign_outbound`. That is QFC's order (`fill`, `toAdmin`, then the number
    consumed at `persist`, PH8A §1), and that of 103's Arm B through `inbound_logout_leaf_`;
  - it records Disconnected through `disconnect_unless_superseded_(arm)`;
  - it builds into its own 256-byte buffer. A Logout that does not fit is not sent, and the session still ends.
  - `inbound_logout_leaf_` returns early unless `fsm_state_ == Active`. `inbound_logon_sent_logout_leaf_` assigns
    before `toAdmin` and does not disconnect.
- **The text.** It is "MsgSeqNum too low, expecting N but received M": byte for byte QFC `doTargetTooLow`
  (`:1105-1118`), QFJ `:1878-1885` and QFGo `targetTooLow.Error()` (`session_rejects.go:45-46`).
  - N is `seqnum_mgr_.next_inbound_unsafe()`: refused, so not advanced.
  - M is `parse_seqnum(hdr.msg_seq_num)`, written with `std::to_chars`. The peer's bytes are not echoed.
  - The builder is one file-local function, shared with 103's Arm B (`on_inbound_active_out_of_sequence_`), so
    Arm B and the two Logon arms cannot diverge. Its buffer is fixed by Arm B's `static_assert`. Arm B's bytes are unchanged,
    and its `TooLowLogout103.*` cells pin them.
- **The fields.** Text(58) only, with no 1409 and no 789 (FR-013). `build_logout` takes no other body field.
  - The number is `peek_outbound()` at the helper's entry: in LogonSent the number after fixpp's own Logon, in
    NotConnected the hydrated durable next outbound number.
  - NextNumIn is not advanced, and nothing inbound is persisted.
- **The endings (FR-014, from the helper as it is):**

  | Condition | Wire | State | Arm returns |
  |---|---|---|---|
  | The Logout is built, stored and written | one 35=5 with the text | Disconnected | success |
  | The Logout does not fit 256 bytes (long CompIDs) | none | Disconnected | success |
  | `toAdmin` throws | none | Disconnected | `app_callback_threw` |
  | `assign_outbound` fails | none | Disconnected | that error |
  | Its store or write fails (`store_then_emit`'s result is discarded) | none, or what the store and write let through | Disconnected | success |
  | A `close()` or stop has superseded the arm during the Logout's suspensions | as far as it got | `close()`'s | success |

- **FR-015.** The helper sets no reset flag, and only a receipt sets one (D1). So `reset_on_logout` does not
  fire for this Logout. L-103-5's clause on that point stays true.
- **Precedence in each arm** is the code order, unchanged. An earlier guard takes its own ending:
  - **LogonSent:** the garble and superseded checks, the fault check, 099's establishment refusal, the validate
    gate, `interpret_logon` (CompID, BeginString, a non-Logon), posture, SendingTime (Logout via
    `inbound_logon_sent_logout_leaf_`), `seq == 0`. Then D3.1's unasked 141=Y branch, which takes no too-low.
    Then the too-low Logout. The later checks (1137, strict, the unit, mTLS, 789) are not reached.
  - **NotConnected:** the same guards up to `interpret_logon`, then hydration, posture, the SendingTime Reject,
    the strict refusal (Logout "ResetSeqNumFlag(141)=Y required", 103 D2.4), and the `reset_on_logon` reset. After
    that reset NextNumIn is 1, so no frame is too low. Then the too-low Logout. 1137 and the CompID-authorization
    gate are not reached.
  - The order agrees with QFC `verify` (`:976-1018`: logon state, SendingTime, CompID, then too-high or too-low)
    and QFGo `verifySelect` (BeginString, CompID, SendingTime, too-low).
  - QFJ refuses a bad DefaultApplVerID before `verify` (`:2188-2199`), which fixpp checks after the sequence
    check. That is pre-existing, outside 104, and not changed.
- **The mTLS acceptor (disclosed, L-104-3).** The too-low Logout goes before the CompID-authorization gate, as
  the posture refusal, the strict refusal and the SendingTime Reject already do. A peer whose certificate passed
  TLS validation but is not authorised for the CompID learns NextNumIn from the Text. Moving the sequence check
  after the gate would reorder an arm 104 does not otherwise touch. The disclosure is the chosen cost, and it is a
  point for Gate A.
- **`validate_sequence_numbers` off.** The Logon arms' `check_inbound` ignores the knob (pre-existing), so the
  Logout is sent with the knob off too. QFJ skips its too-low check at Logon when the knob is off (`:2216`).
  Disclosed in B-104-6.

#### Rationale

All three engines and the FIX docs send this Logout (FD: FIX-SL §4.3.11, §4.8.1, TC 2c, 1B(d)). B-103-10 already
sends it in Active.

### D6 — A too-low Logon or acknowledgement with PossDupFlag(43)=Y

- **Read.**
  - QFC `doTargetTooLow` routes 43=Y to `doPossDup` (`:1083-1103`): no 122 → Reject 373=1 371=122; 122 > 52 →
    Reject and Logout. `verify` then returns false and `nextLogon` returns with the Logon unprocessed, so the
    session waits for its logon timeout.
  - QFJ: `validatePossDup` in `doTargetTooLow` (`:1878-1885`), with the same shape.
  - QFGo's `checkTargetTooLow` (`session.go:679-690`) ignores 43, so the arm sends "MsgSeqNum too low …" via
    `shutdownWithReason` (`logon_state.go:44-46`, `:75-90`).
  - FIX-SL §4.3.11 has no PossDup exception for the Logon (FD). Only §4.8.1, the general rule, has one.
  - fixpp at the base: a silent Disconnected, which matches none of them.
- **Decision.** The Logon arms send D5's Logout for every too-low frame, whatever its 43. That is QFGo and
  FIX-SL §4.3.11. It departs from QFC and QFJ, which validate PossDup and then wait for their logon timeout.
- **Why.** One condition, one ending, and no new branch: `check_inbound` does not read 43. QFC and QFJ's wait
  would need the establishment deadline to end the session, and a Reject path for 122 on an establishment arm.
- This widens FR-012's "with no PossDupFlag(43)=Y" (erratum E-2).

### D7 — The acceptor twin of US2 is unreachable (out of scope)

For a 141=Y Logon to meet an acceptor NextNumIn above 1, hydration would have to be skipped or seed above 1 at the
acceptor's first Logon. Re-derive each step:
- `ensure_hydrated_` withholds the inbound seed when `peer_sent_reset || reset_on_logon`, seeding
  `seqnum_min`. A non-persistent store skips hydration, so the manager keeps its construction value, 1.
- It is skipped only when `hydrated_ && !force`. Its callers are `git grep -n "ensure_hydrated_(" src`: the
  acceptor's is in `on_inbound_not_connected_`.
- NotConnected is entered only at construction. `git grep -n "record_state_transition_(fsm_state::NotConnected"
  src` has no hit, so the arm meets at most one Logon that reaches hydration. Every path after hydration records
  Disconnected or leaves for LogonReceived. A validate-gate Reject that keeps the arm open runs before hydration.
- The Engine builds one `Session` per accepted connection: `run_accept_loop` calls `std::make_shared<Session>`
  per registry match (`src/session/engine.cpp`), then `open()`, then `attach_accepted_transport`.
  `drive_reconnect` refuses an acceptor (B-103-9).
- No public C++ or C-ABI call sets a sequence number: `git grep -n "set_next\|set_counters" include` returns only
  test-access or internal declarations, to be read at run time.

So only a test seam (`seqnum_manager_test_access::set_counters`) reaches it. The phase-1 census repeats the
greps. If any hit appears outside `tests/`, the twin comes back into scope.

### D8 — Witnesses and records

#### Cells that pin the old rule (FR-005), and how each is resolved

Recipe:
- **Static half (leads only):**
  `git grep -n -e reset_on_logout -e logout_seen -e LocalInitiated -e ResetOnLogout_ -e reset_call_count -- tests bindings`.
  Keep each cell that sets `reset_on_logout` and either drives `close(graceful)` without a peer Logout, or asserts
  a reset count, numbers, or store state after a teardown.
- **Measured half (authoritative):** in a scratch tree, apply D1 alone and run `ctest -L session`, `-L capi`,
  `-L config`, the standalone interop labels and `tests/session/conformance`. Every failure is a row.
  - Known positive: `ResetOnLifecycleTest.ResetOnLogout_LocalInitiated_Resets` must fail, or the instrument is
    broken.

Base reading:

| Cell | File | Resolution |
|---|---|---|
| `ResetOnLifecycleTest.ResetOnLogout_LocalInitiated_Resets` | `test_reset_on_lifecycle.cpp` | **Re-pin.** `graceful_close_sync` lets the timeout end the wait, which is exactly FR-002's case. The cell is renamed for the answered close and feeds a peer Logout in LogoutSent, asserting the reset at receipt. The unanswered half moves to SC-001's cell |
| `ResetOnLifecycleTest.ResetOnLogout_BothRoles_Resets`, initiator arm | same | **Re-pin**, as above (answered close). The acceptor arm is unchanged: a peer Logout gives one reset, now at receipt |
| `ResetOnLifecycleTest.ResetOnLogoutAndDisconnect_DoubleTrigger_OneStoreReset` | same | **Assertion unchanged, GREEN.** An unanswered close, so only the teardown reset runs. Its comment ("graceful Logout (which sets logout_seen_=true)") states the old rule and is corrected (FR-006). C5.1's two-trigger case moves to a new cell (D10, R-2) |
| `ResetOnLifecycleTest.ResetOnLogout_PeerInitiated_Resets` | same | **Assertion unchanged, GREEN.** The reset moves to receipt. `SharedStoreWrapper` does not override `reset_to`, so the unit's default body calls the wrapper's `reset()` and `reset_call_count` still moves by one. The comments naming the teardown are corrected |
| `InboundFrameDispositionsOd29.ATerminalCloseAfterPhase1ResolvedJoinsWithNoEarlyCut` | `inbound_frame_dispositions_test.cpp` | **Re-aim** to `reset_on_disconnect`. Its premise, "the graceful close … issues its teardown reset (reset_on_logout), which a HookedStore holds", becomes false: the hold moves into the LogoutSent arm, before phase 1 resolves. With `reset_on_disconnect` the teardown reset is still the one held, so the OD-29 join it witnesses is unchanged. HookedStore's `reset_to_mode::forward` fires `on_reset` either way |
| `GracefulLogoutStoreCancelled.ByTheGraceTimer_ResetOnLogout_NoResetForALogoutThatWasNotSent` | `test_logout_not_sent.cpp` | Unchanged (GREEN): not sent, so no reset |
| `CallbackSend.ToAdminOfGracefulCloseStartsClose_Terminal_ResetOnLogout*` | `test_563_564_callback_send.cpp` | Unchanged (GREEN): cut, so no reset |
| `ResetOnLifecycleTest.ResetOnLogout_Off_Preserves`, `…_NextInitiatorLogon_Emits141`, `ResetOnDisconnect_*` | `test_reset_on_lifecycle.cpp` | Unchanged |
| `PersistentSeqnumHydrate` W8 (`reset_on_logout` at {37,1}) | `test_persistent_seqnum_hydrate.cpp` | Unchanged (Logon predicate only) |
| `ResetPolicyLogonInvariant103.*` | `test_reset_seqnum_policy_matrix.cpp` | Unchanged (Logon only) |
| `Od26.CloseDrainingTheSeqnumMutexBeforeTheHydrateWritesNoState` | `engine_reset_unit_stop_test.cpp` | Unchanged (both knobs off). Re-read it under D1 item 6: no unit is in flight there |

For US2 and US3, the cells that pin a silent too-low end in a Logon arm, or a too-low acknowledgement with 141=Y,
or the empty-range GapFill's NewSeqNo with an explicit end, are found by the same measured census with D3, D3.3a
and D5 applied.
- Known positive for D5: a scratch cell feeding a too-low acknowledgement must flip.
- Leads from the base reading: `UnparseableFrameDisposition.D6Edge_*`, which are LogonReceived and so not
  affected; `ResetSeqnumPolicyMatrixTest.PeerSpontaneous_Initiator_NonReset_Logon_AtNGt1_OutboundUnchanged`
  (W3), whose NextNumIn is 1, so it is unchanged; `DISABLED_TransportInteropConformance.TC009_LogonSeqNumTooLow`,
  which is disabled; and `TcSeqnumTest.Tc2c_MsgSeqNumTooLow`, the Active arm.

#### B&L and contract supersessions (by name)

- **024 C3.1** ("EITHER direction"), superseded by 104 D1 and B-104-1. 024's bundle is a merged, point-in-time
  record and is not edited. `brain/components/session.md` flags C3.1 superseded and names 104, and so do the
  header comments at `logout_seen_` and at the teardown predicate.
- **B-538-7**: amended in place. "a Logout that was sent still resets, answered or not" is struck and points to
  B-104-1. Its first half stands.
- **B-103-9's disclosure**: "and a Logout seen on the old connection still triggers `reset_on_logout`'s teardown
  reset" becomes: "a peer Logout received on the old connection reset the numbers at receipt; one whose reset was
  still owed is made by the new connection's Logon (B-104-7)".
- **B-103-10**: amended. "In Active" becomes "In Active (and, since 104, in the Logon arms, B-104-6)".
- **L-103-2** and **L-103-7**: retired. Each moves to `spec/behaviors-and-limitations-closed.md` with 104's
  resolution (FR-011, FR-016). 103's spec row "A too-low frame in NotConnected or LogonSent still ends silently"
  is superseded by name in the closed row.
- **L-103-5**: unchanged. `reset_on_logout` still does not fire after a too-low Logout.
- **New rows:** B-104-1 … B-104-7 and L-104-1 … L-104-6, listed in [data-model.md](data-model.md) "B&L delta"
  with their text.

### D9 — FR-017: the #266 basis, re-recorded last

- **Files:** `ci/expected-eligible-tests.txt` (one `<preset> <count>` line per pinned lane) and
  `ci/ctest-parallelism-probe.md` (a new "RE-RECORD — 104" section above fixpp#675's, in that section's format:
  basis runs, method, the per-lane table). They go in one commit.
- **When:** after the last commit that adds or removes a ctest registration. The re-record commit itself must add
  none.
  - Check: `git diff <re-record>^ <re-record> -- '*CMakeLists.txt' '*.cmake'` is empty.
  - Check: `git diff origin/main <head> -- '*CMakeLists.txt' '*.cmake' | grep -E '^[+-].*(add_test|gtest_discover_tests|fixpp_add_[a-z_]*test|add_session_10[34]_bucket|add_dictionary_feature_bucket)'`,
    with each hit read, so the registrations 104 adds are the ones expected.
- **The ordering problem.** The tier matrices run in `pull_request` context only once both gate labels are on,
  and the labels need Gate B's evidence. A re-record pushed after labelling is a `synchronize` event, which
  re-fires every tier, whatever path it touches (the `paths-ignore` filters apply to `push` only, per
  `tier1.yml`'s header). So "read the counts off the labelled run, then commit before the labels" cannot both
  hold. Three sequences are possible, and the owner picks one (OPEN O-3):
  - **(a) Proposed: a `workflow_dispatch` run of Tier 1, 2 and 3 on the branch at the head, after the last
    registration change and before labelling.** Each tier workflow declares `workflow_dispatch`. Phase 11 first
    re-derives that each lane's `ci/peak-memory-report.sh` step runs on a dispatch event, by reading its `if:` in
    `.github/workflows/tier*.yml`. The re-record commit then lands. The labelled `pull_request` run, which
    includes it, is SC-005's acceptance run. Cost: one extra matrix run. It meets FR-017's "a CI run at 104's head"
    literally.
  - **(b) fixpp#675's method:** main's last CI count, plus the registrations 104 adds, counted from the diff
    (above), confirmed on the labelled run. This needs no extra run. It needs an erratum to FR-017 (E-5), since
    the values are not read from a run at 104's head.
  - **(c) Re-record after labelling.** The push re-fires the matrix, so the Gate B evidence must be re-read at
    that head. The sign-off order (body, labels, wait, push last) is kept by making the re-record the last push.
- **Source** under (a): Linux, the `ran:` value of each lane's `ci/peak-memory-report.sh` line, cross-checked
  against ctest's `… out of <N>` for the step it sampled; Windows, the `out of <N>` of the suite step. Never a
  value from this bundle or the spec.
- **Known positive:** the same run's report must print the `DIAGNOSTIC ONLY` warning (`peak-memory-report.sh`,
  the `EXPECTED` branch near its end) for the lanes the spec names. That shows the comparison can report a
  mismatch.
- **Acceptance (SC-005):** the first run that includes the re-record prints no `DIAGNOSTIC ONLY` for a count
  mismatch on any lane whose Test step succeeded. Under (a) that is the labelled `pull_request` run.
  - The probe file's own rule, "Do NOT batch a re-record with a change that adds tests", is met by the commit
    boundary above.

### D10 — Test design

**Files and target.**
- New files:
  - `tests/session/test_104_reset_on_logout.cpp` (US1);
  - `tests/session/test_104_unasked_reset_ack.cpp` (US2 and D3.3a);
  - `tests/session/test_104_logon_too_low.cpp` (US3).
- One ctest entry, `session_104_tests`, through a `add_session_104_bucket` twin of `add_session_103_bucket` in
  `tests/session/CMakeLists.txt`. It has the label `104`, so that `ctest -L 104 --no-tests=error` selects it.
- The rigs are `tests/session/test_103_support.hpp`'s `plain_rig` and the `ResetOnLifecycleTest`-style
  StoreDouble and `mock_clock` fixtures, reused as they are.
- Re-pinned cells stay in their files (D8).

**C ABI and size.**
- No C-ABI change. `reset_on_logout` has no C-ABI setter: re-derive with
  `git grep -n reset_on_logout include/fix`, which has no hit at the base. The TOML key and default are unchanged.
- No member is added or renamed (`logout_seen_` changes its contract only). So `sizeof(Session)` is unchanged.
  `git grep -n "sizeof(Session)\|sizeof(fixpp::session::Session)" tests include src` has no hit at the base.
- The public C++ header `session_config.hpp`'s comment on `reset_on_logout` changes. That is documentation, not
  API.

**The cells.** Each is RED on the base, read, and GREEN after, unless noted. Each names its mutant. The anti-vacuity
element is in the cell.

| Id | Cell | Asserts | RED on base because | Mutant (→ RED) |
|---|---|---|---|---|
| R-1a | `ResetOnLogout104.Unanswered_Timeout_NoReset` (both roles; MemoryStore and a persistent StoreDouble) | a 35=5 from fixpp was captured and `LogoutSent` reached **before** the clock advance (the Logout was sent, so "no reset" cannot come from a not-sent route); after the timeout and `close()`: manager and store numbers equal those after the Logout's emit; no `reset`/`reset_to` call | base sets the flag at emit and resets at teardown | restore `run_logout_phase1`'s set |
| R-1b | `…Unanswered_TransportDrop_NoReset` (an engine-managed initiator on `plain_rig`) | as R-1a, ended by the peer's EOF while `LogoutSent`. `logout_disconnect_timeout_ms` is set so the cell's bounded wait for the drop's close ends before it, a ratio stated in the cell and derived from the competing timeout; the cell asserts that `close()` completed before the grace deadline (the drop, not the timer, ended it) | as R-1a | as R-1a |
| R-1c | `…Answered_ResetAtReceipt` (HookedStore, `reset_to_mode::forward`) | the peer's Logout fed in `LogoutSent`; the store's `on_reset` hook records `sess.state()` and whether the transport is still open: **`LogoutSent`, open**; after `close()`: `(1, 1)`, one reset in total | base: the hook fires in `close()`'s teardown, state `Disconnected`, transport closed | delete the LogoutSent arm's reset (the teardown backstop then still reaches `(1, 1)`: this is the forced-hit check that a final-state-only assertion would miss it, so the cell reads at the hook) |
| R-1d | `…PeerRequest_ResetAtReceipt` (both roles) | reply 35=5 numbered with the pre-reset next outbound; `(1, 1)` inside `onLogout`; one reset | as R-1c | delete the Active arm's reset |
| R-1e | `…TooHighLogout_ResetAtAcknowledgement` | too-high Logout, gap filled, bound acknowledges; `(1, 1)` inside `onLogout` | as R-1c | delete the reset in `acknowledge_peer_logout_` |
| R-1f | `…TooHighLogout_ClaimedByClose_ResetAtTeardown` | `close()` while pending; teardown reset once; `(1, 1)` | GREEN on base (unchanged behaviour; its role is the backstop's witness) | drop the flag from the teardown predicate |
| R-1g | `…OwedReset_ConsumedByReconnectLogon` (standalone initiator) | too-high Logout pending, `drive_reconnect` from Active; the new Logon is 34=1 with 141=Y; the new connection's later `close(terminal)` makes no reset | base logs on at the old numbers and resets at the new connection's teardown | remove the `reset_before_logon` widening |
| R-1h | `…PeerLogoutThenReconnect_NoResetOnNewConnection` | peer Logout reset at receipt; reconnect; numbers advance; `close(terminal)`: no reset | base's latched flag resets at teardown | do not clear the flag at the receipt attempt |
| R-1i | `…ReceiptResetStoreFailure_LoggedThenDisconnected` (persistent StoreDouble, `fail_next_reset`) | Disconnected; arm's return as without the failure; manager `(1, 1)`; no teardown retry (`reset_on_logout` only) | the base has no receipt reset | make the failure fatal (return the error) |
| R-1j | `…CloseWaitsForReceiptUnit` (HookedStore holds `reset_to`) | the hold is reached while `LogoutSent` (at receipt, before phase 2); `close()` does not return during the hold; returns ok after release; `(1, 1)` | base's hold is at teardown, after phase 2 began | move the wait back inside the predicate block |
| R-2 | `…BothKnobs_PeerAnswers_TwoResets_EndsOneOne` (HookedStore) | two resets, the first recorded by the hook in `LogoutSent` with the transport open, the second in the teardown; `(1, 1)` (D2) | base: one reset | — (pins D2; the mutant "skip the teardown when the receipt ran" turns it RED on the count) |
| U-1 | `UnaskedResetAck104.Lenient_HydratedN_ResetsAndActive` (persistent store hydrated at in, out > 1) | Active (not merely "no second Logon"); inbound next 2, outbound next 1 in manager and store; `reset_to(2, 1)`; event `by_peer_request = true`; no 35=A written after fixpp's first; the next `send` is 34=1 | base ends Disconnected, too low | restore `check_inbound` before the 141 read |
| U-2 | `…Unilateral_…` | as U-1 | as U-1 | as U-1 |
| U-3 | `…Strict_OwnReset_Unchanged` | strict, ack 141=Y 34=1 → as today | GREEN on base (FR-010 pin) | apply the unasked branch to the asked case: a strict ack at 34 = M > 1 then proceeds, which it must not |
| U-4 | `…AtMAbove1_NotConsumed_ResendOnNextFrame` (N = 7, M = 5) | Active, inbound 1, outbound 1; the peer's 34 = 6 Heartbeat draws ResendRequest 7=1, 16=0 | base: M < N → Disconnected | set consumed for any M |
| U-5 | `…With789_TooHighAfterReset_Logout` (789 on) | Logout "NextExpectedMsgSeqNum too high, expecting 1 but received X"; Disconnected | base: too low → Disconnected, no Logout | — (pins D3.2) |
| U-6 | `…FollowUpResendRequest_GapFillOneOne` | after U-1, RR(34=2, 7=1, 16=0) → exactly one frame, GapFill 34=1, 43=Y, 123=Y, 36=1; inbound next 3; the next `send` is 34=1 | unreachable on base (the session ended) | — (pins L-104-1) |
| U-7 | `…FollowUpResendRequest_ExplicitEnd_Clamped` (FIX.4.1 999999 and FIX.4.4 explicit end) | NewSeqNo = 1, not end + 1 | unreachable on base by this route; RED on base by U-8's route | drop the D3.3a clamp |
| U-8 | `ResendAnswer104.ExplicitEndAboveNextOut_NoOvershoot` (Active, no reset, next out 5, RR(7=5, 16=10)) | GapFill 34=5, 36=5 | base writes 36=11 | drop the clamp |
| T-1 | `LogonTooLow104.Initiator_LogoutWithText` (NextNumIn 5, ack 34=3) | exactly one 35=5; 58 byte-equal to the text; no 1409, no 789; 34 = fixpp's next outbound; NextNumIn 5 in manager and store; `toAdmin(Logout)` once; Disconnected | base writes no 35=5 | remove the helper call |
| T-2 | `…Acceptor_LogoutWithText` (hydrated NextNumIn 5, Logon 34=3) | as T-1, from the acceptor | as T-1 | as T-1 |
| T-3 | `…PossDup_LogoutWithText` (both arms, 43=Y) | as T-1 (D6) | as T-1 | add a 43=Y exemption |
| T-4 | `…Endings_*`: CompID overflow, `toAdmin` throw, persistent store failure (each arm) | D5's endings table, row by row | base: silent for all (the overflow row is GREEN on base; it is the fail-closed pin) | — |
| T-5 | `…ResetOnLogout_NoResetForTooLowLogout` | `reset_on_logout` set; after the too-low Logout and `close(terminal)`: no reset | GREEN on base (vacuous there: no Logout) | set the flag in `refuse_logon_with_logout_` |
| T-6 | `…Precedence_*`: too low and a bad SendingTime; too low on a strict acceptor without 141; too low on an mTLS acceptor with an off-list identity | SendingTime's ending; the strict text; the too-low Logout (L-104-3) | base: per-guard endings, the last silent | — |

**Rules carried into the cells.**
- A "no reset" cell runs with `reset_on_disconnect` off.
- A "no second Logon" cell asserts Active and a next frame numbered 1.
- A cell that reads "at receipt" reads inside `onLogout` (only where the receiving arm leaves Active: R-1d, R-1e)
  or at the store's hook (the LogoutSent route: R-1c, R-1j, R-2), never only after `close()`:
  the teardown backstop would make a final-state assertion pass under the receipt mutant (R-1c's forced-hit
  column).
- Bounded waits are derived from the competing timeout (`logout_disconnect_timeout_ms`, `logon_timeout_ms`,
  HeartBtInt), as a stated ratio.
- Every mutant runs in a scratch copy, with the binary `strings`-checked fresh, and the test count is recorded.
  A selection that runs zero tests fails the step.

## Spec errata proposals

- **E-1 (FR-001, FR-009).** "103's reset unit" should read "093's 141=Y reset unit (`run_reset_unit_`, 093
  contract C-6), which 103 left unchanged". That is the unit `close()` waits for.
- **E-2 (FR-012, User Story 3).** "with no PossDupFlag(43)=Y" should read "whatever its PossDupFlag(43)" for a
  frame in a Logon arm (D6). FIX-SL §4.3.11 has no PossDup exception for the Logon, and QFGo sends the Logout.
  Spec Edge Case "A too-low Logon acknowledgement with 43=Y" is answered by D6.
- **E-3 (FR-001, Edge Case "A too-high Logout").** For a too-high Logout (B-103-7), "the reset MUST happen at
  the receipt" should read "at its acknowledgement, the point where fixpp processes it". One left unacknowledged
  resets at teardown, as today, or at the next in-place Logon (D1 item 5).
- **E-4 (FR-004).** "024's C5.1 (a single store reset)" should read "C5.1 (at most one store reset per
  teardown)". With both knobs, a received Logout makes one reset at receipt and one at teardown (D2).

- **E-5 (FR-017), only if the owner picks D9's sequence (b).** "derived from a CI run at 104's head" should
  read "derived from the last CI run on `main` plus the ctest registrations 104 adds, counted from the diff, and
  confirmed on 104's labelled run".

## Departures from QuickFIX (completing the spec's list)

| Departure | Engines it departs from | Authority |
|---|---|---|
| No second Logon after an unasked 141=Y acknowledgement (FR-008) | QFC | spec, owner clarification 2026-11-17 |
| An unasked 141=Y acknowledgement at 34 = M > 1 continues at inbound 1, and the ResendRequest from 1 waits for the peer's next frame (D4, L-104-5) | QFC (continues at 2); QFJ and QFGo (request at Logon) | D4, provisional (O-2) |
| A ResendRequest answered with nothing sent at or above its BeginSeqNo writes GapFill(34=B, 36=next), (1, 1) included (L-104-1) | QFC and QFJ at next outbound 1; QFGo always | D3.3 |
| 789 evaluated after an unasked reset (D3.2) | QFGo (skips 789 when 141 is present) | QFC, QFJ |
| A too-low Logon with 43=Y draws the Logout (D6, L-104-4) | QFC, QFJ (PossDup handling, then their logon timeout) | FIX-SL §4.3.11; QFGo |
| The too-low Logout goes before the mTLS CompID-authorization gate (L-104-3) | none (no engine binds certificates to CompIDs) | precedent: posture, strict and SendingTime refusals |
| The Logon arms' sequence check is made with `validate_sequence_numbers` off (B-104-6) | QFJ (skips it at Logon) | pre-existing, unchanged |
| The too-low Logout leaves the transport open until EOF, `close()` or stop (as B-103-10, L-103-5) | QFC, QFJ (close at once) | 103 precedent |

## OPEN

- **O-1 — the counterparty's queued Logon after an injected 141=Y.** By reading, a QFC acceptor whose `toAdmin`
  injects 141=Y queues fixpp's Logon at N_out (`doTargetTooHigh`, `:1121-1143`). When its expected number reaches
  N_out, `nextQueued` consumes the queued Logon by incrementing past it (`:1156-1157`), so fixpp's own frame N_out
  would read as too low. A QFJ or QFGo initiator in fixpp's place meets the same thing. QFC as an initiator
  replies with a second Logon (141=Y, 34=1), which might reset the acceptor again and clear this.
  - Not traced end to end: what QFC's queue holds after its `sendRaw` reset, and whether QFGo queues the Logon
    in `resendState`.
  - **Decisive question:** does a QFC v1.16.0 acceptor, injecting 141=Y in `toAdmin` and facing an initiator
    that sends no second Logon, log that initiator out when the initiator's outbound reaches N_out? If yes, the
    owner's FR-008 ruling has a consequence it did not weigh, and the owner decides whether it stands.
  - Settle by a live run against the interop QFC image with a `toAdmin` that sets 141=Y, or by a trace of
    `SessionState::queue`/`retrieve` and `nextQueued`.
  - Not blocking 104's code: FR-008 is binding as written.
- **O-2 — D4's M > 1 case.** The provisional answer is "not consumed, Active, ResendRequest on the peer's next
  frame". **Question for the owner:** QFJ/QFGo's request (provisional), or QFC's "continue at inbound 2"? Both
  cost the same. No QuickFIX engine produces this acknowledgement.

- **O-3 — FR-017's sequence** (research D9). The proposal is (a): a `workflow_dispatch` run at the head before
  labelling, then the re-record, with the labelled run as acceptance. The alternatives are (b) fixpp#675's
  counted delta, which needs erratum E-5, and (c) re-recording after labelling, which re-fires the matrix.
  **Question for the owner:** which sequence?

## Risks

- **R1 — the unit's manager set in Active.** `run_reset_unit_`'s manager steps take the seqnum mutex and grant
  inline only when it is uncontended (L-518-1's condition). In Active at receipt, the reply Logout holds
  `logout_in_flight_`, so `Session::send` is refused (103 FR-021), and the liveness loop's emits wait at the gate.
  If something did contend, the lock wait would enable total cancellation, and a stop during it would fail the
  manager set, which is then logged (D1 item 4).
  - Re-derive in phase 1: every `seqnum_mgr_` lock taker reachable while `logout_in_flight_` is set
    (`git grep -n "seqnum_mgr_\.\(assign_outbound\|check_inbound\|set_next\|reset_to_one\)" src/session`).
- **R2 — `close()` now waits for a Logon-arm unit with no knob set** (D1 item 6). It is bounded by
  `logon_timeout_ms`. The 093 cells around `close()` and the unit (`InboundFrameDispositionsOd29.*`,
  `engine_reset_unit_stop_test.cpp`) are in the measured census.
- **R3 — D3.3a changes 103's and 027's resend-answer arithmetic** for an explicit end at or above next outbound.
  Cells pinning `requested_end + 1` there flip (census). The two-value end model's comment in
  `replay_outbound_range_` is amended to name 104.
- **R4 — `on_inbound_logout_sent_` gains a suspension.** A frame delivered in the same read after the Logout
  waits for the unit, as inbound frames are serialised. On resumption the arm for the current state handles it:
  Disconnected, so it is drained.

## Orchestrator rulings (2026-11-17)

These were made under the owner's grant of the same evening: "run autonomously through all steps … Stop if you hit a
blocker". Each is listed again in the PR body for the owner's review at Gate B sign-off.

- **O-1, resolved by source and not a blocker.** The sequence below holds for a QuickFIX C++ v1.16.0 acceptor whose
  `toAdmin` injects 141=Y:
  1. It queues the initiator's Logon at N_out: `quickfix-cpp@v1.16.0:src/C++/Session.cpp:250-264`, then
     `doTargetTooHigh` → `m_state.queue`.
  2. Its `nextQueued` consumes a queued Logon by incrementing past it (`:1150-1157`). The initiator's own frame N_out
     then reads as too low.
  3. The queue is cleared only in `disconnect()` (`:636`).

  So a QuickFIX C++ initiator's second Logon (141=Y, 34=1) does not clear it either. Every QuickFIX initiator meets the
  same ending against such a counterparty, and so does fixpp under FR-008. FR-008's ruling therefore has no consequence
  that a second Logon would avoid. The behaviour is disclosed as an L-104 row ("a counterparty acceptor that injects
  141=Y in `toAdmin` ends the session when fixpp's outbound reaches its Logon's number"). It is not traced on a live
  peer.
- **O-2: QuickFIX/J and QuickFIX/Go's outcome** (not consumed, Active, ResendRequest from 1), as D4 proposes. It is
  consistent with the owner's FR-008 ruling, which follows the same two engines.
- **O-3: sequence (a).** A `workflow_dispatch` run at the head before labelling, then the re-record, with the labelled
  run as acceptance. E-5 is not needed.
- **E-1 to E-4: applied to spec.md.** E-2 widens the Logon-arm too-low Logout to 43=Y, which is a departure from QuickFIX
  C++ and QuickFIX/J (spec Edge Cases).
- **L-104-3: accepted as disclosed.** The too-low Logout goes before the mTLS CompID-authorization gate, as the
  posture, strict and SendingTime refusals already do. Sequence numbers are not secret on a TLS-authenticated
  connection.
- **D2: accepted.** Two store resets when both knobs are set, as in QuickFIX C++ and QuickFIX/J.
- **D3.3a: accepted, owner-visible scope addition (B-104-5).** QuickFIX C++ clamps an EndSeqNo at or above its next
  outbound number to that number minus 1 (`quickfix-cpp@v1.16.0:src/C++/Session.cpp`, `nextResendRequest`:
  `endSeqNo >= getExpectedSenderNum()` → `endSeqNo = getExpectedSenderNum() - 1`). fixpp's `requested_end + 1` is a
  departure from it today, so the clamp restores QuickFIX's behaviour.
- **D1 item 6: accepted** (the moved `close()` wait, B-104-2), for Gate A review. It is bounded by `logon_timeout_ms`.
