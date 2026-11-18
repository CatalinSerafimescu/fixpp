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

### D1 — `reset_on_logout` resets at a received Logout's reset point (B53, #595; FR-001 to FR-003, FR-006)

The rules are the contract's Part 1 (RT-1, RR-1 to RR-4) and its Outcomes table, under orchestrator rulings RU-1,
RU-2 and RU-3 (spec Clarifications, Gate A round 1). This section records why, and what was read.

#### Decision

1. **The receipt marker.** `logout_seen_` keeps its name. Its type becomes a one-byte enum, `none` / `pending` /
   `reached` (data-model "`logout_seen_`"). A rename was weighed and not taken: the name is read in merged design
   records (`.specify/536-537-538-recovery-liveness.md`, `.specify/563-564-callback-send.md`), the B&L and
   `brain/`, and the smaller diff is the comment. Phases 2 and 9 re-read every hit of
   `git grep -n logout_seen -- tests src include bindings tools spec brain .specify` and correct the prose that
   states the old rule (the supersession census, D8).
   - With rol set it changes only where a peer Logout is received (contract RT-1's arms A, L, B and S). Which
     frames set `pending` is the bullet after next. The sets in L (FR-003) and S (RU-2) are **new**.
   - `run_logout_phase1`'s set after its emit (sc ~8236) is **deleted**. With it goes the "site (a)" half of the
     member's comment (FR-006).
   - Only an awaited read-pump frame sets it `pending`: A at its entry, L and S at their 35=5 branch after the
     garble guard (which reaches the reset point in the same strand turn, since neither arm suspends before it),
     and B's immediate acknowledgement.
     That frame is its one owner (contract RR-2 items 1 and 2, construction A, owner 2026-11-18).
     - It leaves `pending` once. It goes to `reached` at the reset point, or when `close()` closes out a receipt it
       could not wait for (no reset). It goes to `none` at any exit before the reset point, by the frame's owner.
     - B's spawn and ignored branches write nothing. A too-high Logout's detached bound keeps its owed receipt in
       103's `peer_logout_.phase`, and it writes the marker once: `none` → `reached` at its reset point, in
       `acking`.
     - `install_reconnected_transport` sets `none`.
     - The construction rests on four invariants read from source: INV-P, INV-M, INV-T and INV-X. They are stated
       once, in the contract's RR-2.
   - The new header comment names 104 and the 024 T013 / C3.1 clause it supersedes. It states the condition and the
     re-derivation recipe (`git grep -n logout_seen_ -- src include`), and no count of sites.
2. **The reset point (RU-1).** Each receipt arm has one (contract RR-1's table). The reset is made if and only if
   the arm reaches it; processing that ends before it makes none under rol, and there is no teardown or
   reconnect-Logon backstop. What QuickFIX does at each ending is read below and tabulated in the contract's
   Outcomes; where fixpp differs it is a row of the spec's "Departures from QuickFIX" (DQ-9 to DQ-14).
3. **The order at the reset point (RU-3).** The reply Logout if owed; `Disconnected`; the marker `reached` and
   `run_reset_unit_(seqnum_min, seqnum_min)` (093 contract C-6); the arm's step-5 restore,
   `reset_cancellation_state(enable_total_cancellation{})`, as `on_inbound_logon_sent_` does after its unit; in L
   only, `logout_confirmed_` and phase 1's wake; then the transport closes as today.
   - **A** (`on_inbound_active_logout_`): after the reply (`inbound_logout_leaf_`), the `sequence_numbers_reset`
     event, `fromAdmin` and `persist_inbound_advance_`. The arm's existing `record_state_transition_(Disconnected)`
     moves ahead of the unit, so `onLogout`, fired on that edge, sees the pre-reset numbers (DQ-10).
   - **L** (`on_inbound_logout_sent_`): the arm already writes `Disconnected` first; the unit goes between it and
     `logout_confirmed_ = true` / `logout_wait_wake_()`. `onLogout` fired earlier, on phase 1's Active →
     LogoutSent edge. What the rule changes here is where the reset runs: in LogoutSent's arm, with the transport
     still open, before `close()`'s phase 2, where the base made it in phase 2's teardown.
   - **B** (`acknowledge_peer_logout_`): after its leaf and event, its existing Disconnected write, then the unit.
     That one site covers the immediate acknowledgement (T = 0, no clock, a Logout in flight, closing or stopped)
     and the bound's (`run_peer_logout_bound_`). The 103 mark tells them apart there: it is `acking` only for the
     bound (contract RR-2 item 5).
   - **S** (`on_inbound_logon_sent_`, RU-2): with rol set only (orchestrator ruling 2026-11-18; contract RR-1
     "Arm S's branch and the knob"), a 35=5 frame is handled ahead of `interpret_logon`, which refuses any
     non-Logon before its BeginString and CompID tests (`admin_messages.cpp`, `interpret_logon`: `msg_type_found
     != "A"` first). It passes the guards that run ahead of `interpret_logon` (garble, `logon_arm_superseded_`,
     fault, 099's `establishment_frame_refused_`; the validate gate exempts 35=5). On an mTLS session the frame
     passes the Logon path's mTLS gate (clarification N-8), with both its arms: (1) with `live_peer_id_` set, it
     calls `compid_authorization_policy.authorize`; on an initiator that identity is set only by
     `install_reconnected_transport`, after `reconnect_fsm.cpp`'s step 7 authorised it; (2) with mTLS and no live
     identity, it fails closed with the same event and no `authorize` call. A direct initiator that reaches
     LogonSent through `open()` has no live identity, so its cells drive arm (2). An unauthorised identity, or none, takes the gate's own ending, its
     `session_event_compid_authorization_failed`, `Disconnected` and success, with no reset (contract O19). An authorised
     identity, or a session without mTLS, then takes `Disconnected`, the marker and the unit. No reply Logout is sent (DQ-9; FIX-TC 1B(e) makes it optional), and no
     `fromAdmin` runs for it, as today (DQ-14); the arm returns success, as the base's ending does. With rol off
     the branch does not run and the frame takes the base's ending: `interpret_logon`'s non-Logon refusal,
     `Disconnected`, success, with no mTLS gate (no `authorize` call) and no event (contract W11).
4. **`close()` (contract RR-2 items 3–5).** `close()` waits, after its joins and before its
   teardown predicate, while the marker is `pending` or `reset_unit_in_flight_` is set. The wait is moved out of
   the predicate block and widened to the marker, and it needs `effective_clock_` (B-104-2).
   - `Engine::stop()` does not take this wait: it reaches `close()` only after its role-loop join, which waits
     for the receipt arm with no bound (`L-563-4`, `L-093-12`). See contract RR-2 item 4, "Under
     `Engine::stop()`", and cell R-1t.
   - It is a poll by post, not 093's event wait. 093's wait had one waker, the unit's completion, and widening it
     to `pending` added exits that call no waker (Opus round 2, N-1).
   - **093 OD-1's reason for rejecting a poll is answered by name.** 093 recorded "a `co_await asio::post` poll
     loop, which spins the strand for the whole bound" (`brain/components/session.md`, the 141=Y unit's `close()`
     wait bullet).
     - That cost is real here. A receipt held in a store call keeps one handler ready on the session's executor for
       up to `logon_timeout_ms` (contract RR-2 item 4, its worst case; disclosed in B-104-2).
     - It is accepted on three grounds:
       - the cost is confined to `close()` and to the receipt's duration, never per message;
       - the loop has the shape of the post-polls `close()` already runs (the joining close's result poll, 103's
         poll-join on `logout_in_flight_`, the liveness join and the bound join);
       - an event wait is correct only if every exit from `pending` calls its waker, and N-1 was a missed one.
     - 093's event-driven shape is superseded (D8); 093's reason stands as a description of the cost.
   - Route B needs nothing new. `close()` claims the 103 mark at its first commit, and its bound join precedes
     this wait. So the wait sees only a pump frame's `pending`, or a unit in flight (contract RR-2 item 4).
   - A marker still `pending` after the wait belongs to a receipt that did not reach its reset point: no reset
     under rol (design ruling 3). `close()` closes it out to `reached`. From then on the arm issues no store call;
     one it had already issued runs to its end (O13-r, O13-p).
   - The reset point is one strand turn with no throw point. So a reset point once passed is never lost.
   - The wait cannot be on the caller's own arm, because an arm clears its marker before any `close()` it calls.
5. **Store failure at the reset: logged, then proceed.** This is 024 I-07's disposition for the teardown reset it
   replaces. A manager or store error from the unit is logged; the arm's ending is the one it has without the
   failure (QFGo `in_session.go` `handleLogout`: `dropAndReset` error → `logError`, then `latentState`). On a
   persistent store that refused, the stored numbers keep their pre-reset values (L-104-2). No retry under rol;
   under rod the teardown reset runs anyway.
6. **The LogoutSent race (O17).** While L's unit is suspended, `close(graceful)`'s grace timer
   (`logout_disconnect_timeout_ms`, armed before phase 1) can expire. Phase 1 then returns
   `session_logout_timeout`, which `close()` logs and proceeds past; because the timer arm won, `close()` also
   force-closes the transport (`live->close()`) before phase 2's `close_async()`. Nothing more was to be sent, so
   the wire is the same: no frame, then the transport closes. Phase 2 joins the liveness loop and the bound and
   waits for the unit (item 4). The arm resumes with the state already Disconnected and sets `logout_confirmed_`
   harmlessly. `close(graceful)` returns ok, as after a timeout today, and the numbers end `(1, 1)`.
7. **Unchanged.**
   - `reset_on_disconnect` (C4.1, C4.2).
   - `teardown_reset_done_`.
   - The Logouts fixpp sends on an error path: SendingTime, posture, strict, the gap deadline, Arm B, and 104's
     too-low Logout. None changes the marker (FR-015).
   - The not-sent and cut Logouts (B-538-7's first half, B-563-5). Nothing sent changes the marker.
   - The `sequence_numbers_reset{by_peer_request=false}` event that A and B emit on every acknowledged peer Logout,
     knob or not. It predates 104 and 104 does not touch it. Noted because an operator may read it as a reset.

#### Read from the engines (RU-1, RU-2)

- **QuickFIX C++** (`v1.16.0`, `src/C++/Session.cpp`): `nextLogout` returns at once when `verify(logout, false,
  false)` fails, then replies unless `sentLogout()`, increments the target, resets under `m_resetOnLogout`, and
  calls `disconnect()`, whose `onLogout` follows the reset. `verify` checks `validLogonState`, `isGoodTime` and
  `isCorrectCompID` and ends with `fromCallback`, which calls `fromAdmin` outside its `try`, so a throw or a
  reject exception leaves `nextLogout` before the reply and the reset. `validLogonState` admits a Logout when
  `m_state.sentLogon()` and returns false before any Logon was sent or received. In `sendRaw` a `toAdmin`
  exception propagates (only `IOException` is caught), so a reply whose `toAdmin` throws makes no reset; a store
  `IOException` is caught and `sendRaw` returns false, so `nextLogout` goes on to the reset; `send` returning
  false does too.
- **QuickFIX/J** (`QFJ_RELEASE_3_0_2`, `quickfixj-core/src/main/java/quickfix/Session.java`): `nextLogout` →
  `verify(logout, false, false)` → `fromCallback` (`fromAdmin`, exceptions propagate); then `generateLogout` unless
  `isLogoutSent()`; `resetState()` under `resetOnLogout || isResetStatePending()`; `disconnect`.
  `validLogonState` has `MsgType.LOGOUT.equals(msgType) && state.isLogonSent()`. `sendRaw` catches a `Throwable`
  from `toAdmin`, logs it, persists and sends, and catches the store's `IOException` returning false; either way
  `nextLogout` resets (DQ-11 for the `toAdmin` case).
- **QuickFIX/Go** (`v0.9.12`): `in_session.go` `handleLogout` → `verifySelect(msg, false, false, true)`, whose
  `verifyMsgAgainstAppImpl` calls `fromCallback` → `FromAdmin`; a reject returns `processReject` before the reset;
  `sendLogoutInReplyTo`'s error is logged; `dropAndReset` under `ResetOnLogout`. `logout_state.go` routes a
  LogoutSent frame through `inSession{}.FixMsgIn`. `logon_state.go` sends a non-Logon to `latentState` with no
  reset.

#### Rationale

- All three engines reset for `ResetOnLogout` at one place: the processing of a received Logout, after it is
  verified and answered. Each logout timeout only disconnects, and only `ResetOnDisconnect` resets there. Read:
  QFC `nextLogout` and `disconnect()`; QFJ `nextLogout` and `disconnect`; QFGo `handleLogout`. PH8A §1 has the same
  reading for the sent-and-not-transmitted case.
- The FIX docs define no reset on Logout (spec clarification 2026-11-17).
- The unit, not `reset_seqnums_to_one_durable`, is used at the reset point because it is one store operation
  (`MessageStore::reset_to`), it runs on an empty cancellation slot so `Engine::stop()`'s total emission cannot cut
  it (093 OD-25, OD-26), and it sets `reset_unit_in_flight_`, which `close()` waits on.
- 093's single-interleaving-point argument (093 contract C-6, its Outcomes) rests on the Logon arms'
  `logon_arm_superseded_` check before the unit, and on there being no concurrent producer in LogonSent. Neither
  holds on routes A and B, so 104 does not borrow it: RR-2 gives the receipt its own owner rule, and RR-3 / RR-4
  give the concurrent producers their own stop (Opus N-2, N-3).
- `Disconnected` before the unit (RU-3) is what removes the window in which the numbers were `(1, 1)` while the
  session was Active. Every emitter that numbers a frame tests the state first (contract RR-4, read from
  `run_liveness_loop`, `Session::send_body_` and `replay_outbound_range_`).

#### Alternatives considered

- *Keep the teardown reset and only stop setting the flag at emit.* Rejected by the owner's clarification Q3
  ("At receipt").
- *A teardown backstop for a receipt whose processing failed* (the round-0 design, and Codex #1's "reset anyway,
  then disconnect"). Rejected by RU-1: QuickFIX makes no reset for a Logout it did not process.
- *Gate the liveness loop and the ResendRequest leaf on `logout_in_flight_` while a receipt reset is in flight*
  (Opus N-2's fix). Rejected by RU-3: it widens 103's FR-021 to admin frames, where recording `Disconnected` first
  stops every emitter that tests the state, with no new gate.
- *`reset_seqnums_to_one_durable(logged)` at receipt.* Rejected: it is two store-facing steps, it is cancellable
  by stop's total emission on a contended lock, and `close()` cannot order its teardown reset after it.
- *Reset at receipt of a too-high Logout, before the ResendRequest.* Rejected: NextNumIn would become 1, so the
  ResendRequest and the bound's "N passes S" test would act on the reset space. 103's bound is fixpp's
  equivalent of QuickFIX processing the Logout, so the reset goes with its acknowledgement (erratum E-3).
- *A retry after a failed receipt reset.* Rejected: two endings for one condition. Under rod the teardown reset
  runs anyway.
- *A separate "receipt in flight" member.* Rejected: the three-valued marker carries `pending` and `reached` in
  the byte the `bool` occupied, with no new clear site beyond `install_reconnected_transport`.
- *Who owns a too-high Logout's receipt* (Gate A round 3, RC-1: one marker, several owners on route B). Round 3
  had B's pump arm hand `pending` to the bound at `co_spawn`, with an unwinding-only owner in the bound. A
  duplicate too-high Logout's frame then cleared the bound's `pending` (Codex #1). The bound's claimed exit cleared
  an in-sequence Logout's `pending`, so `close()` stopped waiting for arm A (Opus N-1).
  - **Chosen, construction A (owner, 2026-11-18):** keep three values, and let only awaited pump frames own
    `pending`. The bound's owed receipt is 103's single-owner mark, and the bound writes the marker once, at its
    reset point. No marker value then has two owners: INV-P serialises the pump frames, and INV-X keeps the bound's
    reset point apart from them.
  - *A fourth value, `pending_bound`, that a receipt arm's entry revokes* (Opus round 3). Not chosen: it keeps the
    hand-over between the bound and a later receipt, which construction A removes, together with a value that
    `close()`'s wait and close-out would have to treat as `pending`.
  - *An owner epoch or token* (Codex round 3). Not taken: it adds a member, and data-model says none is added.

#### Read from source: a too-high Logout in LogonReceived (Fable's unverified observation)

The observation was that a too-high Logout in LogonReceived may reach the spawn branch. The bound would then exit
on its "live" test without clearing the 103 mark, leaving `phase == pending`, which refuses every send. **Read at
`3d04c544`: not reachable.** It is not recorded as OPEN.
- The hazard exists as a shape. The bound's `!live()` exits write nothing to the 103 mark, so when "live" fails on
  `fsm_state_ != Active` alone, the mark stays `pending`. That is harmless only because the spawn branch is
  reachable in Active alone.
- `record_state_transition_(fsm_state::LogonReceived)` has one writer, the acceptor Logon arm
  (`on_inbound_not_connected_`). Re-derive it with
  `git grep -n "record_state_transition_(fsm_state::LogonReceived)" -- src`.
- That frame is awaited to its end before any other frame is delivered (INV-P). The acceptor's first frame is
  delivered and awaited before `run_read_pump` starts.
- Every exit from the arm after that write leaves one of three states:
  - `Active`;
  - `Disconnected`: `record_state_transition_`, `disconnect_unless_superseded_` when not superseded, and
    `on_inbound_active_`'s catch, which records Disconnected unless superseded;
  - superseded: `logon_arm_superseded_`, which is `state_ == lifecycle::closing || engine_stop_requested_ ||`
    a state that is no longer LogonReceived. The two flags imply `closing_or_stopped_()`, and they are not reset
    on that connection.
- So a later frame meets LogonReceived only with `closing_or_stopped_()` true. That routes a too-high Logout to the
  immediate-acknowledgement branch, never the spawn branch.
- An observation for the owner, not an edit to RT-1. By the same reading, arm A's "Logout in LogonReceived" row
  has input only while a close or a stop is in flight.

**Sites:** `Session::on_inbound_active_logout_`, `Session::on_inbound_active_too_high_logout_`,
`Session::on_inbound_logout_sent_`, `Session::on_inbound_logon_sent_` (arm S), `Session::acknowledge_peer_logout_`
(B's reset point, shared by the immediate acknowledgement and the bound, told apart by the 103 mark),
`Session::run_peer_logout_bound_` (no marker write; the Debug assert at its reset point), `Session::run_logout_phase1` (the deleted set),
`Session::close` (the wait's position, its poll and predicate, the close-out of a `pending` marker, the comment on
the predicate), `Session::run_reset_unit_` (its `reset_unit_wake_` call deleted, with the member),
`Session::store_then_emit` and `Session::persist_inbound_advance_` (RR-4's boundaries),
`Session::install_reconnected_transport` (the clear), and `sh` (the member, its type and comment, plus
`drive_reconnect`'s latch paragraph: "a Logout seen on the old connection still triggers reset_on_logout's
teardown reset" is deleted, with nothing received resetting the new connection).

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
    (QFC `if (m_resetOnLogout)`, `if (m_resetOnDisconnect)`; QFJ `if (resetOnLogout ||`, `if (resetOnDisconnect ||`).
  - Keeping C5.1 literal needs no new member, so there is no `sizeof(Session)` change and no new
    connection-scoped latch to clear at reconnect.
- **Alternatives.** *Collapse the pair with a connection-scoped "receipt reset done" latch, cleared at
  `install_reconnected_transport`, and evaluate it after D1 item 4's wait.* Rejected: it adds a member, a clear site
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
- The rest of the arm keeps its order, with one move on the unasked branch only (clarification N-8):
  - 1137 (099);
  - the strict mismatch test (it cannot fire here: strict always asks);
  - **unasked branch: the mTLS CompID-authorisation gate**, moved ahead of the unit through the helper that makes
    today's check (same event, same one-shot `live_peer_id_` consumption, same silent Disconnected). A peer that is
    not authorised leaves the hydrated numbers untouched (contract U4m). The asked branch keeps its gate after
    the unit (FR-010);
  - the unit's targets, from the code that is there. `unit_in` is 2 when consumed, else 1. `unit_out` is 1,
    because `own_logon_sent_reset_flag` is false. FR-008: inbound next 2, outbound next 1;
  - `run_reset_unit_`, with 030's persistent-fatal disposition and step 7's superseded test;
  - the event `by_peer_request = true`;
  - mTLS (asked branch only; the unasked branch has passed it), then the 789 honour, then Active;
  - the persist guard, skipped because `peer_ack_sent_reset_flag` is set.
- The reach of the move: an engine-managed initiator authorises the peer at `reconnect_fsm.cpp`'s step 7, before
  its Logon is sent, so the gate in the arm decides only for a direct mTLS session.
- No second Logon: no code sends one, and QFJ and QFGo send none (QR Part B; spec clarification).
- **FR-009** holds by construction: it is the same unit, the same `close()` wait and the same dispositions.

#### D3.2 The 789 honour after the reset

- `honor_peer_next_expected_` runs after the unit against `peek_outbound()`, which is now 1. A counterparty that
  injected 141=Y and also sends 789 sends the 789 its own pre-reset state computed: X = N_out + 1 in QR Part C's
  QFC trace, where `generateLogon` sets 789 before `toAdmin`. So X > 1, and fixpp sends Logout "NextExpectedMsgSeqNum too high,
  expecting 1 but received X" and records Disconnected.
- QFC `nextLogon` (`nextExpectedMsgSeqNum.getValue() > getExpectedSenderNum()`, after its reset under
  `if (!m_state.sentReset())`) and QFJ `nextLogon` (`targetWantsNextSeqNumToBe > actualNextNum`, after `resetState`
  under `if (!state.isResetSent())`) do the same. QFGo skips 789 when 141 is present (`session.go` `handleLogon`,
  `!msg.Body.Has(tagResetSeqNumFlag)`).
- **Decision: unchanged**, with the QFGo departure listed. It needs `enable_next_expected_msg_seq_num` and a 789
  on the acknowledgement.

#### D3.3 What the peer sends next, and fixpp's answer

- **The peer.** By QR Part C, a QFC or QFGo acceptor that injected 141=Y through `toAdmin` reset its own store
  in `sendRaw`/`prepMessageForSend`. It then finds fixpp's Logon at 34 = N_out too high and sends
  ResendRequest(34=2, 7=1, 16=0). QFJ with `LogonTag=141=Y` instead disconnects itself after the acknowledgement
  (QFJ `nextLogon`, `if (state.isResetSent() && !state.isResetReceived())`).
- **fixpp's numbers.** After D3.1, NextNumIn is 2, so the ResendRequest is in sequence, and next outbound is 1.
- **The engines' answer to RR(7=1, 16=0) at next outbound 1, persistent store:** nothing is sent.
  - QFC clamps EndSeqNo to 0 (`nextResendRequest`, `endSeqNo = getExpectedSenderNum() - 1;`).
    `generateRetransmits(1, 0)` retrieves nothing, and QFC `generateRetransmits`'s tail `if (endSeqNo > msgSeqNum)`
    is `0 > 0`.
  - QFJ clamps the same way (`manageGapFill`, `endSeqNo = expectedSenderNum - 1;`). In `resendMessages(1, 0)`,
    QFJ `generateSequenceResetIfNeeded`'s `if (endSeqNo > msgSeqNum)` is false.
  - QFGo clamps the same way (`in_session.go` `handleResendRequest`, `endSeqNo = expectedSeqNum - 1`).
    `IterateMessages(1, 0)` yields nothing, and QFGo `resendMessages`'s `if seqNum != nextSeqNum {` is false.
- **fixpp's answer today.** `replay_outbound_range_` takes its `!store_ || our_last == 0 || begin > eff_end`
  branch and writes one GapFill at `begin`.
  - With 16=0 it is GapFill(34=1, 43=Y, 123=Y, 36 = `peek_outbound()` = 1). NewSeqNo equals the receiver's
    expected number, which all three treat as a no-op: QFC `nextSequenceReset` (`newSeqNo > getExpectedTargetNum()`,
    `newSeqNo < getExpectedTargetNum()`), QFJ `nextSequenceReset` (`newSequence > getExpectedTargetNum()`,
    `newSequence < getExpectedTargetNum()`), QFGo `in_session.go` `handleSequenceReset`
    (`case newSeqNo > expectedSeqNum:`, `case newSeqNo < expectedSeqNum:`). Neither branch fires, and no Reject is
    sent.
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
    explicit end beyond it. There fixpp wrote NewSeqNo = end + 1 above its own next number. The supersession
    census (D8) finds the cells and the texts that pin the old value.
  - **The shape with BeginSeqNo B above the next outbound number (Opus N-6).** The empty branch takes
    `begin > eff_end` and writes GapFill(34=B, 36=N_out): its NewSeqNo is below its own MsgSeqNum. QFC writes the
    same frame: `nextResendRequest` clamps EndSeqNo to `getExpectedSenderNum() - 1`, and `generateRetransmits`'
    tail sets `endSeqNo = min(endSeqNo + 1, next)` and calls `generateSequenceReset(beginSeqNo, endSeqNo)`. QFJ's
    `generateSequenceResetIfNeeded` has the same body. QFGo sends nothing (DQ-15). B-104-5 and contract row U6 state
    it.
  - **027 C3 is superseded by name** (contract "Superseded by name"). 027's required test "request `[10,20]`,
    store through 5 ⇒ GapFill `NewSeqNo=21`" (`WalkExtraction.TwoValueEnd_ExplicitEndBeyondStore`) is re-pinned
    to `NewSeqNo=6`, with QFC's identical GapFill(34=10, 36=6) as its rationale. Its file-header comment and the
    cell's comment, which state 21, are rewritten as the rule and the recipe, with no value.
- **After that (orchestrator ruling O-1, by source; L-104-7).** The QFC peer keeps its queued copy of fixpp's Logon
  at N_out (`doTargetTooHigh` → `m_state.queue`). When its expected number reaches N_out, `nextQueued` consumes the
  queued Logon by incrementing past it, so fixpp's own frame N_out then reads as too low. The queue is cleared only
  in `disconnect()`, so QFC's second Logon does not avoid it, and every QuickFIX initiator in fixpp's place meets the
  same ending. Not traced on a live peer.

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
- **QFC** accepts and resets. At 34 = M, `nextLogon`'s `isTargetTooHigh(msgSeqNum) && !resetSeqNumFlag` is false,
  so it increments its target to 2 and sends no ResendRequest (it also replies with its own Logon).
- **QFJ** accepts and resets (`verify` skips the sequence checks when a reset was received,
  `if(!state.isResetReceived()){`). Then `nextLogon`'s `isLogonInNormalSequence` is false
  (`!(isTargetTooHigh(sequence) && !resetOnLogon)`), and QFJ `nextLogon`'s `doTargetTooHigh(logon);` queues the
  Logon and sends ResendRequest from 1.
- **QFGo** accepts and resets (`session.go` `handleLogon`, `resetStore = true`, then
  `if err := s.store.Reset(); err != nil {`). Then QFGo `handleLogon`'s `s.checkTargetTooHigh(msg)` returns
  `targetTooHigh`, and `doTargetTooHigh` sends ResendRequest(1, 0) and enters `resendState` (`logon_state.go`
  `(s logonState) FixMsgIn`, `case targetTooHigh:`).
- None ends the session. No QuickFIX engine produces this acknowledgement: each one's injection path resets in
  `sendRaw` and stamps 34=1 (QR Part C).

**fixpp at the base:**
- M < N: silent Disconnected.
- M = N: Active, inbound next 2 (QFC's outcome).
- M > N: silent Disconnected, or with 789 on, tolerated with inbound next 1.

**Decision (orchestrator ruling O-2: QuickFIX/J's and QuickFIX/Go's outcome):**
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
  followed QFJ and QFGo, and two of the three engines request the gap. Ruling O-2 did not take it.

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
  (`"MsgSeqNum too low, expecting "`), QFJ `doTargetTooLow` (`"MsgSeqNum too low, expecting "`) and QFGo
  `targetTooLow.Error()` (`session_rejects.go`, `"MsgSeqNum too low, expecting %d but received %d"`).
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
  | A persistent store refuses its store call (`store_then_emit` returns 059's error before Step 2; the result is discarded) | none | Disconnected | success |
  | A volatile store refuses it (I-07, logged-then-proceed) | one 35=5 with the text | Disconnected | success |
  | Its write fails (`dispatch_aborted`, discarded) | none on the wire; the Logout stored | Disconnected | success |
  | A `close()` or stop has superseded the arm during the Logout's suspensions | as far as it got | `close()`'s | success |
  | The identity gates refuse (an mTLS identity not authorised for the CompID; on the acceptor `authorize_logon`) | none | Disconnected, with the gate's event | the gate's |

  Each row's cell is D10's T-4a to T-4f or T-6 (T-4f is the volatile store's row); the contract's rows T3 to T8 are
  the same endings. The supersession row has no cell, and needs none: 104 adds a caller of
  `refuse_logon_with_logout_`, not a suspension or an ending. The helper's store and write suspensions and its
  `disconnect_unless_superseded_(arm)` are today's, already taken by the posture and strict refusals, and the
  observable ending is `close()`'s or stop's own, which ends the session Disconnected too. So, **inferred**, no mutant of
  the helper's supersession test has an observable that a cell of this arm could tell from `close()`'s.

- **FR-015.** The helper sets no reset flag, and only a receipt sets one (D1). So `reset_on_logout` does not
  fire for this Logout. L-103-5's clause on that point stays true.
- **Precedence in each arm** is the code order, with one change: a too-low frame runs the identity gates before
  its Logout (clarification N-8). An earlier guard takes its own ending:
  - **LogonSent:** the garble and superseded checks, the fault check, 099's establishment refusal, the validate
    gate, `interpret_logon` (a non-Logon first, then BeginString and CompID), posture, SendingTime (Logout via
    `inbound_logon_sent_logout_leaf_`), `seq == 0`. Then D3.1's unasked 141=Y branch, which takes no too-low.
    Then, for a too-low frame, the mTLS CompID-authorisation gate, then the too-low Logout. The later checks (1137,
    strict, the unit, 789) are not reached.
  - **NotConnected:** the same guards up to `interpret_logon`, then hydration, posture, the SendingTime Reject,
    the strict refusal (Logout "ResetSeqNumFlag(141)=Y required", 103 D2.4), and the `reset_on_logon` reset. After
    that reset NextNumIn is 1, so no frame is too low. Then, for a too-low frame, the mTLS CompID-authorisation
    gate and `authorize_logon` (the credentials gate, which today runs after 1137), then the too-low Logout. 1137
    is not reached.
  - The gates run through the helpers that make today's checks, so the event, the one-shot `live_peer_id_`
    consumption and the silent Disconnected are identical to the in-sequence path's.
  - The order agrees with QFC `verify` (`validLogonState`, `isGoodTime`, `isCorrectCompID`, then `isTargetTooHigh`
    or `isTargetTooLow`: logon state, SendingTime, CompID, then too-high or too-low)
    and QFGo `verifySelect` (BeginString, CompID, SendingTime, too-low).
  - QFJ refuses a bad DefaultApplVerID before `verify` (`nextLogon`, `"Invalid DefaultApplVerID="`), which fixpp
    checks after the sequence check. That is pre-existing, outside 104, and not changed.
- **The identity gates (clarification N-8, reversing round 0's disclosure).** FIX-TC 1S(c) (`fixdocs/tc.txt`,
  §4.4.1): a Logon "received with unauthenticated/non-configured identity (e.g. invalid SenderCompID(49), invalid
  TargetCompID(56), invalid source IP address, etc. vs. system configuration)" is answered by "Disconnect without
  sending a message (Note: sending a Reject or Logout(35=5) would consume a MsgSeqNum(34))". A TLS-validated peer
  not authorised for the CompID, and a Logon whose credentials `authorize_logon` refuses, are such identities. So
  the too-low Logout comes after both gates. Round 0's rationale ("sequence numbers are not secret") did not engage
  that text, which is about the consumed number and the message itself, not secrecy. The posture, strict and
  SendingTime refusals and the 1137 Reject still answer before the gates; that is pre-existing, 104 does not
  widen it, and L-104-3 now discloses it (DQ-6). Proposed follow-up: an issue for it, filed and labelled in phase 9.
- **`validate_sequence_numbers` off.** The Logon arms' `check_inbound` ignores the knob (pre-existing), so the
  Logout is sent with the knob off too. QFJ skips its too-low check at Logon when the knob is off (`nextLogon`,
  `if (!verify(logon, false, validateSequenceNumbers))`).
  Disclosed in B-104-6.

#### Rationale

All three engines and the FIX docs send this Logout (FD: FIX-SL §4.3.11, §4.8.1, TC 2c, 1B(d)). B-103-10 already
sends it in Active.

### D6 — A too-low Logon or acknowledgement with PossDupFlag(43)=Y

- **Read.**
  - QFC `doTargetTooLow` routes 43=Y to `doPossDup` (`return doPossDup(msg);`): no 122 → Reject 373=1 371=122
    (`doPossDup`'s `SessionRejectReason_REQUIRED_TAG_MISSING`); 122 > 52 → Reject and Logout
    (`origSendingTime > sendingTime`). `verify` then returns false and `nextLogon` returns with the Logon
    unprocessed, so the session waits for its logon timeout.
  - QFJ: `validatePossDup` in `doTargetTooLow` (`validatePossDup(msg);`), with the same shape.
  - QFGo's `checkTargetTooLow` (`session.go`, `seqNum < s.store.NextTargetMsgSeqNum()`) ignores 43, so the arm
    sends "MsgSeqNum too low …" via `shutdownWithReason` (`logon_state.go` `(s logonState) FixMsgIn`'s
    `case targetTooLow:`, then `shutdownWithReason`'s `logout := session.buildLogout(reason)`).
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
  src` has no hit, so the arm meets at most one Logon that reaches hydration. Its known positive, over the same
  pathspec: `git grep -n "record_state_transition_(fsm_state::LogonReceived" src` hits (D1's one writer), so the
  pattern can report a hit. Every path after hydration records
  Disconnected or leaves for LogonReceived. A validate-gate Reject that keeps the arm open runs before hydration.
- The Engine builds one `Session` per accepted connection: `run_accept_loop` calls `std::make_shared<Session>`
  per registry match (`src/session/engine.cpp`), then `open()`, then `attach_accepted_transport`.
  `drive_reconnect` refuses an acceptor (B-103-9).
- No public C++ or C-ABI call sets a sequence number: `git grep -n "set_next\|set_counters" include` returns only
  test-access or internal declarations, to be read at run time.

So only a test seam (`seqnum_manager_test_access::set_counters`) reaches it. The phase-1 census repeats the
greps. If any hit appears outside `tests/`, the twin comes back into scope.

### D8 — Witnesses and records: the supersession census (FR-005, FR-006)

Round 0's census was driven by RED: its authoritative half applied the change and listed the cells that failed.
A surface that restates a superseded rule and cannot fail was invisible to it: a GREEN cell's comment, a B&L row, a
brain page, another feature's contract. This census finds both kinds. It is a **recipe**; its results go to the
evidence file, never into this bundle or a code comment.

#### The superseded rules and their search keys

| Rule 104 changes | Keys (fixed strings, case as given) |
|---|---|
| 024 C3.1, "a Logout in either direction" resets (D1) | `C3.1`, `EITHER direction`, `either direction` (read each hit: the phrase is common), `sent OR received`, `sent **or** received` (Markdown bold defeats the previous key), `answered or not`, `was sent still resets`, `Logout that was sent`, `brings no reset`, `reset_on_logout`, `logout_seen_`, `logout_seen`, `LocalInitiated`, `T013`, `site (a)`, `fixpp#595` |
| The receipt resets at teardown, and a latched flag resets the next connection (D1) | `teardown reset`, `still triggers`, `latched`, `drive_reconnect` (read each hit) |
| 093 C-6: `close()` waits only before its own teardown reset, woken by the unit alone (D1 item 4) | `waits only when it is about to issue`, `about to issue its teardown reset`, `reset_unit_in_flight_`, `reset_unit_wake_`, `B-093-11`, `event-driven` and `completion signal` (read each hit: prose that describes the wait without naming the member), and `CloseDrainsInsideTheUnit` and `WithoutTeardownReset` (the `LogonCloseDuringSuspension.Q23_*` cells pin "`close()` does not wait for the unit when there is no teardown reset", which RT-5 changes; they are read with the `Q25_*WithoutTeardownReset*` cells; added from the T011 report, 2026-11-18) |
| 027 C3, an explicit end beyond the store answers `requested_end + 1` (D3.3a) | `NewSeqNo=21`, `rr_end+1`, `rr_end + 1`, `requested_end + 1`, `requested_end+1`, `EndSeqNo+1`, `EndSeqNo + 1`, `end+1` and `end + 1` (the last two are common: read each hit; the five were added at T004 by the implementer, 2026-11-18), `TwoValueEnd`, `ExplicitEndBeyondStore`, `two-value end`, `I-NEX-3` |
| A too-low frame in a Logon arm ends silently; an unasked 141=Y at 34=1 is too low (D3, D5) | `L-103-2`, `L-103-7`, `too-low → fatal`, `would be`, `in-sequence`, `ends silently`, `B-103-10` |

#### The recipe

1. For each key, over the tree at the branch head:
   ```bash
   git grep -n -F -e '<key>' -- spec brain specs .specify/*.md \
     include src tests bindings tools ':!specs/104-sequence-reset-fidelity'
   ```
   `specs` reaches every file of every other bundle: its spec, contracts, data model, research, plan, tasks,
   quickstart and checklists. Round 1's pathspec named only `'specs/*/contracts/*'` and `'specs/*/spec.md'`, and
   missed the superseded rule wherever a bundle restated it outside those two (Codex round 2, #5).
   Live B&L only: read `spec/behaviors-and-limitations.md`, not the `-closed` file (a resolved row there is not a
   surface to amend).
   The surfaces outside that pathspec are in scope too, read once by the same keys with
   `command grep -rn -F -e '<key>' -- docs bench ci perf .github README.md CHANGELOG.md CONTRIBUTING.md`, each hit
   dispositioned as in step 3. The C-ABI headers and their documentation are under `include` already. A key run
   over `include` is the known positive that this second grep can report a hit.
2. Keep every hit that states the old rule as current behaviour: a test comment, a code comment, a live B&L row,
   a brain page's current-behaviour row, or another feature's contract clause. A brain page's rejected-alternative
   bullet is the historical half and is not rewritten; it gets a one-line pointer to 104.
3. Give each kept hit one disposition in the evidence file: **re-pin** (a cell whose assertion changes), **reword**
   (a comment or row that becomes procedure or names 104), **supersede by name** (a merged feature's contract or
   bundle, which is not edited; the pointer goes on its component page and in the header comment of the code that
   implements the new rule), or **unaffected**, with the reason. A hit in a merged bundle outside its contract (its
   data model, research, plan, tasks, quickstart or a checklist) is superseded by name too: its component page flags
   that document, not only the contract clause (project rule, issue #334).
4. **The measured half**, kept as a second instrument: in a scratch tree apply D1 alone, then D3 + D3.3a, then D5,
   and run `ctest -L session`, `-L capi`, `-L config`, the standalone interop labels and
   `tests/session/conformance` (quickstart §1). Every failing cell must already be a kept hit of step 2; one that
   is not means a key is missing, and the key is added before the census is accepted.
5. **The instrument proves it can report** (known positives). Each of these must be a hit of step 1, or the census
   is broken:
   - `spec/behaviors-and-limitations.md` B-024-1 ("Logout teardown (sent OR received)"), the seeded positive;
   - the live rows B-538-7 ("answered or not"), B-563-5, B-103-9 (its "still triggers `reset_on_logout`'s
     teardown reset" disclosure), B-103-10, B-093-11, L-103-2, L-103-7;
   - `brain/components/graceful-logout.md`: the invariant row "`logout_seen_` is written only at Logout-specific
     sites", and the R-24 bullet "Logout that was sent and never answered still resets: that case is fixpp#595";
   - `brain/components/session.md`: the bullet "(a graceful phase 1 sets `logout_seen_`)" (a rejected
     alternative: pointer only), and the bullet "`close()`'s wait is bounded by `logon_timeout_ms`, event-driven"
     (093 OD-1; its shape is superseded by RR-2 item 4's poll);
   - `brain/components/message-store-quiescence.md`: its 141=Y unit section's "`close()` also waits, when it is
     about to issue its teardown reset and a unit is in flight" (093 C-6's bullet);
   - `specs/024-reset-refresh-on-logon/contracts/reset-knobs.md` C3.1 and C5.1;
   - `specs/027-next-expected-msgseqnum/contracts/next-expected-msgseqnum.md` C3's required test ("request
     `[10,20]`, store through 5");
   - the non-contract restatements in merged bundles: 027's `data-model.md`, `research.md`, `plan.md` and
     `tasks.md` (`NewSeqNo=21`, `TwoValueEnd`, `two-value end`); 024's `research.md` and `tasks.md` (`EITHER
     direction`), its `data-model.md` row "Logout sent **or** received" (`sent **or** received`), and its
     `quickstart.md` and `tasks.md` (`LocalInitiated`, `C3.1`); 013's `quickstart.md` (`NewSeqNo=21`). The recipe
     with step 1's pathspec was run at `489affaf` on 2026-11-18 and reported each of these; round 1's pathspec
     reported none of them;
   - `specs/093-inbound-frame-dispositions/contracts/inbound-frame-dispositions.md` C-6's `close()` bullet;
   - `tests/session/test_persistent_seqnum_hydrate.cpp`, W8's comments ("in-sequence" against manager=37, and "seq=1
     would be / too-low → fatal → wrong path"; both line-wrapped, so the keys are one line's text);
   - `tests/session/test_logout_not_sent.cpp`, the comment above
     `ByTheGraceTimer_ResetOnLogout_NoResetForALogoutThatWasNotSent` ("close whose Logout was sent still resets:
     ResetOnLifecycleTest.ResetOnLogout_LocalInitiated_Resets");
   - `tests/session/test_next_expected_msgseqnum.cpp`, its header and the comment of
     `WalkExtraction.TwoValueEnd_ExplicitEndBeyondStore` ("NewSeqNo=21");
   - `include/fixpp/session/session_config.hpp`'s `reset_on_logout` comment ("in EITHER direction") and
     `src/session/session.cpp`'s teardown predicate comment ("in either direction") and end-model comment
     (`requested_end + 1`);
   - `ResetOnLifecycleTest.ResetOnLogout_LocalInitiated_Resets` (a cell that must fail in step 4).
6. Leads still to settle in the census (Opus's residual list): `test_recovery_admin_span_gapfill.cpp`'s RR(2,12)
   and RR(2,5) against its store tail, and its header's `NewSeqNo=21`; builders called with variables; the interop
   helpers (`tests/interop/parity/parity_support.hpp`, `tests/interop/conversation/conversation_script.yaml`); the
   no-store comparator path (`test_538_volatile_store_after_reset_logon.cpp`).

#### Base reading of the cells, to be confirmed by the census

| Cell | File | Resolution |
|---|---|---|
| `ResetOnLifecycleTest.ResetOnLogout_LocalInitiated_Resets` | `test_reset_on_lifecycle.cpp` | **Re-pin.** `graceful_close_sync` lets the timeout end the wait, which is exactly FR-002's case. The cell is renamed for the answered close and feeds a peer Logout in LogoutSent, asserting the reset at the reset point. The unanswered half moves to SC-001's cell |
| `ResetOnLifecycleTest.ResetOnLogout_BothRoles_Resets`, initiator arm | same | **Re-pin**, as above (answered close). The acceptor arm is unchanged: a peer Logout gives one reset, now at the reset point |
| `ResetOnLifecycleTest.ResetOnLogoutAndDisconnect_DoubleTrigger_OneStoreReset` | same | **Assertion unchanged, GREEN.** An unanswered close, so only the teardown reset runs. Its comment ("graceful Logout (which sets logout_seen_=true)") states the old rule and is reworded (FR-006). C5.1's two-trigger case moves to a new cell (D10, R-2) |
| `ResetOnLifecycleTest.ResetOnLogout_PeerInitiated_Resets` | same | **Assertion unchanged, GREEN.** The reset moves to the reset point. `SharedStoreWrapper` does not override `reset_to`, so the unit's default body calls the wrapper's `reset()` and `reset_call_count` still moves by one. The comments naming the teardown are reworded |
| `InboundFrameDispositionsOd29.ATerminalCloseAfterPhase1ResolvedJoinsWithNoEarlyCut` | `inbound_frame_dispositions_test.cpp` | **Re-aim** to `reset_on_disconnect`. Its premise, "the graceful close … issues its teardown reset (reset_on_logout), which a HookedStore holds", becomes false: the hold moves into the LogoutSent arm, before phase 1 resolves. With `reset_on_disconnect` the teardown reset is still the one held, so the OD-29 join it witnesses is unchanged. HookedStore's `reset_to_mode::forward` fires `on_reset` either way |
| `GracefulLogoutStoreCancelled.ByTheGraceTimer_ResetOnLogout_NoResetForALogoutThatWasNotSent` | `test_logout_not_sent.cpp` | **Assertion unchanged, GREEN; comment reworded.** Not sent, so no reset. The comment above it names the renamed cell as "a local graceful close whose Logout was sent still resets", the superseded rule; it becomes "a local graceful close resets only when the peer's Logout is received (104, B-104-1)" with the new cell's name |
| `CallbackSend.ToAdminOfGracefulCloseStartsClose_Terminal_ResetOnLogout*` | `test_563_564_callback_send.cpp` | Unchanged (GREEN): cut, so no reset. The cut route records Disconnected in `run_logout_phase1` before the Logout takes its number, so the session never enters LogoutSent and a peer Logout is drained, not taken by arm L. Its comments go through the census |
| `ResetOnLifecycleTest.ResetOnLogout_Off_Preserves`, `…_NextInitiatorLogon_Emits141`, `ResetOnDisconnect_*` | `test_reset_on_lifecycle.cpp` | Unchanged |
| `PersistentSeqnumHydrate.W8_HydratedInitiator_ResetOnLogout_PeerSpontaneous141Y` (`reset_on_logout` at {37,1}) | `test_persistent_seqnum_hydrate.cpp` | **Assertion GREEN, re-pin and reword (Codex #7).** It feeds an unasked 141=Y acknowledgement at 34=37 against a hydrated NextNumIn of 37: D3's M > 1 case. The base consumes it (inbound next 2); 104 does not (inbound next 1, ResendRequest on the peer's next frame). It asserts only Active, `by_peer_request` and outbound 1, so it stays GREEN while its comments become false. It gains an inbound-next-1 assertion (U-4's rule) and its comments state the rule (an unasked 141=Y acknowledgement is compared with 1), not the old outcome |
| `WalkExtraction.TwoValueEnd_ExplicitEndBeyondStore` | `test_next_expected_msgseqnum.cpp` | **Re-pin** `NewSeqNo=21` → `6` (D3.3a; 027 C3 superseded by name), with QFC's GapFill(34=10, 36=6) as rationale. `…_789Caller` passes `true` and is unaffected; the census confirms |
| `ResetPolicyLogonInvariant103.*` | `test_reset_seqnum_policy_matrix.cpp` | Unchanged (Logon only) |
| `Od26.CloseDrainingTheSeqnumMutexBeforeTheHydrateWritesNoState` | `engine_reset_unit_stop_test.cpp` | Unchanged (both knobs off). Re-read it under D1 item 4: no unit is in flight there |
| `LogonCloseDuringSuspension.Q23_*` (the acceptor and initiator cells, and their `FileStore` twins) | `test_session_plaintext_roundtrip.cpp` | **Re-pin (D1 item 4, RT-5; T011 report).** Each pins "`close()` does not wait for the unit when there is no teardown reset", which RT-5 changes: `close()` now waits for a Logon arm's 141=Y unit in flight whatever the knobs. Each is re-pinned to the wait, and read with `LogonCloseDuringSuspension.Q25_*WithoutTeardownReset*`, whose "the targets are left" reading must stay true |
| The pumps `drain_ready_q27` and `Q27Rig::run_both_until` | `test_session_plaintext_roundtrip.cpp` | **Re-pin to a bounded pump** (T012's record, applied at T046). `drain_ready_q27`'s `while (ioc.poll() > 0)` and `run_both_until`'s `ioc.poll()` never return while `close()` polls by post (RR-2 item 4) |
| `RecoveryAdminSpanGapfillTest.AllAdminSpanCollapsesToSingleGapFill` | `test_recovery_admin_span_gapfill.cpp` | **Re-pin (D3.3a; T005's lead, settled at T011's report).** Its GapFill's NewSeqNo is EndSeqNo + 1 on the base, where its explicit end is at or above the next outbound number; it is re-pinned to the next outbound number, read from the cell's own store tail at T074, never from this bundle |
| `FixTcCoverageGaps.ResendRequestEndSeqNoBeyondLastOutbound_GapFills_StaysActive` and `FixTcCoverageGaps.SimultaneousResendRequest_ServicesPeerWhileAwaitingOwn` | `tests/interop/parity/fix_tc_coverage_gaps_test.cpp` | **Re-pin (D3.3a)**, as above: each GapFill's NewSeqNo moves from EndSeqNo + 1 to the next outbound number |
| `FixTcCoverageGaps.BodyFieldsArbitraryOrder_Accepted` | same | **Re-pin the input (D3.3a).** Its NewSeqNo is EndSeqNo + 1 because its explicit end lies beyond the next outbound number; D3.3a clamps that end, so the cell would no longer show that tag 16 was parsed. Its ResendRequest gets an explicit EndSeqNo below the next outbound number, where D3.3a leaves EndSeqNo + 1 in place, so the GapFill's NewSeqNo still depends on 16 |

For US2 and US3 the cells that pin a silent too-low end in a Logon arm, or a too-low acknowledgement with 141=Y,
are found by the same recipe (step 1's last key row) and step 4's run with D3 and D5 applied.
- Known positive for D5: a scratch cell feeding a too-low acknowledgement must flip.
- Leads from the base reading: `UnparseableFrameDisposition.D6Edge_*`, which are LogonReceived and so not
  affected; `ResetSeqnumPolicyMatrixTest.PeerSpontaneous_Initiator_NonReset_Logon_AtNGt1_OutboundUnchanged`
  (W3), whose NextNumIn is 1, so it is unchanged; `DISABLED_TransportInteropConformance.TC009_LogonSeqNumTooLow`,
  which is disabled; and `TcSeqnumTest.Tc2c_MsgSeqNumTooLow`, the Active arm.

#### B&L, brain and contract supersessions (by name)

The text of every new and amended row is data-model.md "B&L delta". This summary, the contract's "Superseded by
name", spec FR-005's list and tasks T100 are views of one list, the census's kept hits (T004, re-run at T101),
which is authoritative: a surface on one view and missing from another is resolved in the census, by adding the key
or the disposition, not by editing the view alone. In summary:
- **024 C3.1** ("EITHER direction"), superseded by 104's contract RT-1 / RR-1 and B-104-1. 024 C5.1 is kept (read
  per teardown, E-4). 024's bundle is a merged, point-in-time record and is not edited.
- **027 C3**'s explicit-end case, superseded by B-104-5 and contract row U6.
- **093 C-6**'s `close()` bullet, superseded by B-104-2 and contract RR-2 item 4.
- **Live B&L rows amended in place:** B-024-1, B-538-7, B-563-5, B-093-11, L-093-12 (a pointer to B-104-2: the
  store calls a held receipt makes `stop()` wait for), B-103-9's disclosure, B-103-10.
- **Retired:** L-103-2 and L-103-7, each moved to `spec/behaviors-and-limitations-closed.md` with 104's
  resolution (FR-011, FR-016). 103's spec row "A too-low frame in NotConnected or LogonSent still ends silently"
  is superseded by name in the closed row.
- **L-103-5**: unchanged. `reset_on_logout` still does not fire after a too-low Logout.
- **Brain:** every component page the issue #334 recipe finds (tasks T100), `message-store-quiescence.md` among
  them. `brain/components/session.md` and `brain/components/graceful-logout.md` each list 024 C3.1, 027 C3,
  093 C-6's bullet and 103's latch disclosure as superseded by 104, flagged in place (the project rule that a
  component page lists every document, wrong ones included). graceful-logout.md's invariant row on
  `logout_seen_` and its R-24 bullet's "still resets: that case is fixpp#595" are reworded to the receipt rule;
  session.md's rejected-alternative bullet gets a one-line pointer.
- **Code comments** naming 104 where they supersede 024, 027 or 093 (header-comment rule, issue #310): the member,
  the teardown block's predicate paragraph, `run_logout_phase1`'s "site (a)" comment, the receipt arms' "024 T013"
  comments, `drive_reconnect`'s latch paragraph, `session_config.hpp`'s two knobs, the end-model comment in
  `replay_outbound_range_`.
- **New rows:** B-104-1 … B-104-7 and L-104-1 … L-104-7.

### D9 — FR-017: the #266 basis, re-recorded last

- **Files:** `ci/expected-eligible-tests.txt` (one `<preset> <count>` line per pinned lane) and
  `ci/ctest-parallelism-probe.md` (a new "RE-RECORD — 104" section above fixpp#675's, in that section's format:
  basis runs, method, the per-lane table). They go in one commit.
- **When:** after the last commit that adds or removes a ctest registration. The re-record commit itself must add
  none.
  - FR-017's "after 104's own tests land" and "after Gate B's last fix" name the same point: Gate B's last fix is
    the last commit that can change a registration. A commit after the dispatch run that adds or removes a
    registration (a late fix, an `origin/main` integration) voids that run, and phase 11 restarts from a new
    dispatch at the new head.
  - Before the dispatch, read in each tier workflow whether a `pull_request` run checks out the PR merge ref or the
    head sha. If it checks out the merge ref and `origin/main` has gained registrations since the merge-base, the
    dispatch counts cannot match the labelled run; that case is a STOP to the orchestrator (tasks T114).
  - Before it: `bash ci/test-parallelism-verdict.sh`, the harness the basis file's header requires after any edit.
    Its disposition outside Gate B's reviewed range is plan phase 11's.
  - Check: `git diff <re-record>^ <re-record> -- '*CMakeLists.txt' '*.cmake'` is empty.
  - Check: `git diff origin/main <head> -- '*CMakeLists.txt' '*.cmake' | grep -E '^[+-].*(add_test|gtest_discover_tests|fixpp_add_[a-z_]*test|add_session_10[34]_bucket|add_dictionary_feature_bucket)'`,
    with each hit read, so the registrations 104 adds are the ones expected.
- **The ordering problem.** The tier matrices run in `pull_request` context only once both gate labels are on,
  and the labels need Gate B's evidence. A re-record pushed after labelling is a `synchronize` event, which
  re-fires every tier, whatever path it touches (the `paths-ignore` filters apply to `push` only, per
  `tier1.yml`'s header). So "read the counts off the labelled run, then commit before the labels" cannot both
  hold. Three sequences were possible; orchestrator ruling O-3 chose **(a)**:
  - **(a) Chosen: a `workflow_dispatch` run of Tier 1, 2 and 3 on the branch at the head, after the last
    registration change and before labelling.** Each tier workflow declares `workflow_dispatch`. Phase 11 first
    re-derives that each lane's `ci/peak-memory-report.sh` step runs on a dispatch event, by reading its `if:` in
    `.github/workflows/tier*.yml`. The re-record commit then lands. The labelled `pull_request` run, which
    includes it, is SC-005's acceptance run. Cost: one extra matrix run. It meets FR-017's "a CI run at 104's head"
    literally.
  - *(b) fixpp#675's method* (main's last CI count plus the registrations 104 adds, counted from the diff):
    rejected, because its values are not read from a run at 104's head, so it would have needed an erratum to
    FR-017. No erratum is made.
  - *(c) Re-record after labelling*: rejected, because the push re-fires the matrix and the Gate B evidence would
    have to be re-read at that head.
- **Source** under (a): Linux, the `ran:` value of each lane's `ci/peak-memory-report.sh` line, cross-checked
  against ctest's `… out of <N>` for the step it sampled; Windows, the `out of <N>` of the suite step. Never a
  value from this bundle or the spec.
  - Which lanes run `ci/peak-memory-report.sh` is read from the workflows at T114 (`git grep -n
    peak-memory-report -- .github/workflows`). SC-005's "every lane" is the lanes the report runs on. A lane that
    has a line in `ci/expected-eligible-tests.txt` and no report step (whether the Windows lanes do is read there) is
    still re-recorded from its `out of <N>`, and the record says it has no EVIDENCE reading.
- **Known positive:** the same run's report must print the `DIAGNOSTIC ONLY` warning (`peak-memory-report.sh`,
  the `EXPECTED` branch near its end) on every lane whose dispatch count differs from the basis file's current
  value, and on at least one lane. That shows the comparison can report a mismatch. A lane whose count happens to
  equal the current basis prints no warning and is not a positive.
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
- The rigs: `fixpp::test_support::plain_rig` (`tests/session/plain_engine_rig.hpp`; round 0 named
  `test_103_support.hpp`, and the Opus review `tests/support/run_thread_engine_rig.hpp`, both wrong),
  `HookedStore` (`tests/support/hooked_store.hpp`), and the `ResetOnLifecycleTest`-style StoreDouble and
  `mock_clock` fixtures.
- **One test-support addition, in phase 1, before any cell that needs it.** HookedStore's `on_reset` fires for
  both `reset()` and `reset_to` under `reset_to_mode::forward`, so a hook cannot tell the unit from the teardown.
  HookedStore gains a per-call record: which call fired (`reset` or `reset_to`, with `reset_to`'s targets) and a
  count of each. R-1c to R-1f, R-1k, R-1l, R-1o, R-1t, R-1u, R-1w, R-1x, R-1y, R-1z, R-2, R-3 and T-5 assert on it; without it R-1l's first mutant and
  the arm-versus-teardown readings of R-1k and R-1u could not go RED. Its own known positive: a cell that calls each of the two directly and reads two distinct records.
  - **Its delta against `StoreLog::writes`.** `StoreLog::writes` already names `reset` and `reset_to <in> <out>`,
    but it is a completion log: an entry is written after the inner call returns, a `reset_to` entry only in
    `reset_to_mode::forward` and `contended` (`default_body` logs the `reset()` its body calls), and no entry
    carries session state. The new record is taken at issue time, at the call's entry
    before any hook, hold or forward, in every mode. Each entry carries the kind, `reset_to`'s targets, and the
    session state and transport-open readings from a snapshot callable the cell sets (HookedStore cannot see the
    session; the shape is `StoreLog::close_began`), plus a count per kind. `on_reset` is one-shot, so a cell that
    needs a reading at a second call reads the record: R-2 reads two entries, `reset_to` with the transport open,
    then `reset` in the teardown.
  - **Which log a cell reads.** A reading at the call (state, transport, which call) or a count by kind: the new
    record. An order against `close()` or `stop()` ("finished before `close()` returns", R-1k, R-1t, R-1u) and the
    final counters: `StoreLog::writes` and `StoreLog::inner`, as today.
- **A second test-support addition, for RR-5's cells (R-5a, R-5c).** HookedStore's `on_inbound_persist` is
  one-shot ("first next_seqnum(inbound, true)"), so a cell cannot hold a later persist, the GapFill's or the
  unparseable frame's. HookedStore gains `Hooks::on_inbound_persist_call`, a `std::function<bool()>` called on every
  `next_seqnum(inbound, true)`. When one call returns true, that operation completes and then holds, as
  `on_outbound_read` and `on_outbound_store` do (`release_when`, else a reset after `close()` began). The existing
  hooks are unchanged. R-5b's store double is cell-local: it fails one outbound `store()` without holding it, and
  holds the reconcile's read while returning success, which HookedStore's `fail_held` (one flag for every held
  operation) cannot express.
- Re-pinned cells stay in their files (D8).

**C ABI and size.**
- No C-ABI change. `reset_on_logout` has no C-ABI setter: re-derive with
  `git grep -n reset_on_logout -- include/fix src/capi bindings`, which has no hit at the base. Its known positive,
  over the same pathspec: `git grep -n logon_timeout -- include/fix src/capi bindings` hits (a knob with a C
  setter, B-093-4). The TOML key and default are unchanged.
- No `Session` member is added or renamed; one private `Engine` member is added, R-1t's step-3 mark (B-104-7).
  `SeqnumManager`, which `Session` holds by value (`seqnum_mgr_`), gains one private flag and the passkey-gated
  `freeze` / `thaw` and query `frozen` (contract RR-5 item 4), so `sizeof(SeqnumManager)` may grow, and with it
  `Session`. No test pins
  `sizeof(SeqnumManager)`: `git grep -nE 'sizeof ?\([^)]*SeqnumManager' -- tests include src` has no hit at the base
  (B-104-7; T092 reads the size).
  `logout_seen_` changes from `bool` to a one-byte enum, and `reset_unit_wake_` is deleted (data-model). No test pins `sizeof(Session)`: `git grep -n "sizeof(Session)\|sizeof(fixpp::session::Session)" tests include src` has no
  hit at the base; the full `-k 0` build (phase 7, phase 8) is the check that nothing else depends on it.
- The public C++ header `session_config.hpp`'s comment on `reset_on_logout` changes. That is documentation, not
  API.

**The cells.** Each is RED on the base, read, and GREEN after, unless noted. Each names a mutant that changes the
outcome of the cell's own input, applied in a scratch copy (quickstart §2). The anti-vacuity element is in the
cell. "Hook" means HookedStore's `on_reset`, which records `sess.state()`, whether the transport is open, and which
store call it saw (`reset_to` is the unit; `reset` is the teardown).

| Id | Cell | Asserts | RED on base because | Mutant (→ RED) |
|---|---|---|---|---|
| R-1a | `ResetOnLogout104.Unanswered_Timeout_NoReset` (both roles; MemoryStore and a persistent StoreDouble) | a 35=5 from fixpp was captured and `LogoutSent` reached **before** the clock advance (the Logout was sent, so "no reset" cannot come from a not-sent route); after the timeout and `close()`: manager and store numbers equal those after the Logout's emit; no `reset`/`reset_to` call | base sets the flag at emit and resets at teardown | restore `run_logout_phase1`'s set |
| R-1b | `…Unanswered_TransportDrop_NoReset` (an engine-managed initiator on `plain_rig`) | as R-1a, ended by the peer's EOF while `LogoutSent`. `logout_disconnect_timeout_ms` is set so the cell's bounded wait for the drop's close ends before it, a ratio stated in the cell and derived from the competing timeout; the cell asserts that `close()` completed before the grace deadline (the drop, not the timer, ended it) | as R-1a | as R-1a |
| R-1c | `…Answered_ResetAtReceipt` (HookedStore, `reset_to_mode::forward`) | the peer's Logout fed in `LogoutSent`; the hook sees `reset_to`, state `Disconnected`, the transport **open**, phase 1 not yet woken; after `close()`: `(1, 1)`, one reset in total | base: the hook sees `reset` in `close()`'s teardown, transport closed | delete L's reset point (no teardown backstop now, so the final state is wrong too; the hook reading keeps the cell honest if a backstop is ever reintroduced) |
| R-1d | `…PeerRequest_ResetAtReceipt` (both roles) | reply 35=5 numbered with the pre-reset next outbound; inside `onLogout` the numbers are the **pre-reset** ones (DQ-10); the hook sees `reset_to`, state `Disconnected`, transport open; one reset | as R-1c | delete A's reset point; second mutant: move the Disconnected write after the unit → `onLogout` sees `(1, 1)` and the hook sees Active |
| R-1e | `…TooHighLogout_ResetAtAcknowledgement` | too-high Logout, gap filled, bound acknowledges; the hook as R-1d; pre-reset numbers inside `onLogout` | as R-1c | delete B's reset point |
| R-1f | `…TooHighLogout_ClaimedByClose_NoReset` (rol only, a mock clock), and `…_AckingWhenClosed_ResetOnce` | (1) `close()` while the mark is pending: no `reset_to`, no `reset`; numbers as before; `close()` returns with the mock clock below `logon_timeout_ms` and no `close_reset_wait_expired`; (2) `close()` while the bound is `acking` (its reply held in HookedStore): one `reset_to`, before `close()` returns. (2) is (1)'s forced-hit arm on the same store double, and contract W6a's `close()` route; W6a's stop, EOF and reconnect-drain routes are read from source, no cell (stop and the EOF reach `close()`'s bound join; the reconnect drain is `drain_for_reconnect_`'s poll of the bound's counter) | (1) base resets at teardown (flag set at receipt); (2) base resets at teardown, `reset` not `reset_to` | (1) B's spawn branch sets the marker `pending` (round 3's hand-off, with no owner left to clear it once `close()` claims the bound) → `close()` polls to `logon_timeout_ms` and emits `close_reset_wait_expired` → RED; (2a) the bound's reset point moves the marker only from `pending` (round 3's rule) → it finds `none` and runs no unit → zero resets → RED; (2b) move 103's bound join to immediately after `seqnum_mgr_.drain()`, before `close_result_` is set → the bound's unit's first manager call returns `session_already_closed` (`reset_to_one` on a drained mutex, the teardown comment's "analyze F1") → zero `reset_to` (and no `reset`, rod being off) → RED. Not past the publication of `close_result_`, and not a deletion of the join: in either, the bound can run after `close()` returned, which under ASan reads as a crash in the fixture, not a clean RED. 103's bound join, ahead of the seqnum drain, covers the `acking` phase and its unit (contract RR-2 item 4) |
| R-1g | `…ReceiptEndsBeforeResetPoint_NoReset_*` (rol only; A in Active): `FromAdminThrow`, `FromAdminReject`, `LateParseFailure` | no `reset_to` and no `reset` after the arm and after `close()`; the arm's existing ending (reply on the wire for throw and reject; Reject for reject) | base resets at teardown (flag set) | per parameter: clear the marker only at the reset point, not on exit → the later `close()` waits for a receipt that will never arrive, to `logon_timeout_ms` (the cell asserts `close()` returns well inside that bound) → RED; for `LateParseFailure`, clear it after the arm's `close()` instead of before → `close()` waits for its own caller, the same assertion → RED |
| R-1h | `…PeerLogoutThenReconnect_NoResetOnNewConnection` (standalone initiator) | peer Logout reset at its reset point; `drive_reconnect`; the reconnect Logon is written (RR-4 does not refuse it: `install_reconnected_transport` runs before `emit_initiator_logon_`); numbers advance; `close(terminal)`: no reset | base's latched flag resets at the new connection's teardown | do not set `none` at `install_reconnected_transport` → RR-4 refuses the reconnect Logon → RED |
| R-1i | `…ReceiptResetStoreFailure_LoggedThenDisconnected` (persistent StoreDouble, `fail_next_reset`) | Disconnected; arm's return as without the failure; manager `(1, 1)`; no teardown retry (rol only) | the base has no receipt reset | make the failure fatal (return the error) |
| R-1j | `…CloseWaitsForReceiptUnit` (HookedStore holds `reset_to`; a mock clock for the session) | the hold is reached in L's arm (state Disconnected, transport open, phase 1 not woken); `close()` does not return during the hold; returns ok after release; `(1, 1)`. O17: `close_grace` is a real `asio::steady_timer`, so the budget is a small real-time `logout_disconnect_timeout_ms` that expires during the hold, and the mock clock, on which the unit wait runs, is never advanced to `logon_timeout_ms` while the hold lasts. The cell asserts the transport closed (the timer arm won) while the `close()` future is still pending | base's hold is at teardown, after phase 2 began | move the wait back inside the predicate block |
| R-1k | `…CloseDuringReplyStore_OneResetByTheArm` (A in Active, HookedStore holds the reply's `store`) | `close()` started during the hold does not return before release; after: exactly one `reset_to`, no `reset`, finished before `close()` returns | base: the teardown's `reset`, no `reset_to` | drop the `pending` term of `close()`'s wait → `close()` closes the receipt out with no reset → zero resets → RED |
| R-1l | `…CloseWhilePending_NoClock_NoReset_{HeldInReplyStore,HeldInPersist}` (no clock, rol only, A in Active; HookedStore holds, after completing it, the reply's `store` or the inbound persist) — ruled case "`close()` without a clock while `pending` → no reset" (O13-r, O13-p) | `close()` returns during the hold; no `reset` and no `reset_to` at any point; after release the arm runs no unit. Each number is named on its own. `HeldInReplyStore`: the store holds the reply 35=5 at N_out; no frame on the cell's live transport after `close()` returned, and on its `transport_send` twin the reply is transmitted (B-563-5); store inbound N (the persist refused, RR-4); manager inbound N+1 (`check_inbound`, before the arm). `HeldInPersist`: store inbound N+1 (the persist landed); manager inbound N+1 | base: the teardown's `reset` (flag set at receipt) | (1) reset a `pending` receipt at the teardown (round 1's rejected take-over) → `reset` seen → RED; (2) drop the arm's "already `reached` → no unit" test → `reset_to` seen → RED; (3) drop RR-4's `persist_inbound_advance_` test → `HeldInReplyStore`'s store inbound becomes N+1 → RED |
| R-1n | `…ReplyEndings_*` (A in Active, rol only): `Overflow` (CompIDs for which the reply does not fit, paired with the longest that fit), `ToAdminThrow`, `AssignFailure` (outbound at `seqnum_max` through the test seam), `StoreFailure` (persistent StoreDouble refuses the reply's store), `WriteFailure` (sink throws), `PersistFailure` | per contract O5–O11: reset for `Overflow`, `StoreFailure`, `WriteFailure`; no reset for `ToAdminThrow`, `AssignFailure`, `PersistFailure` | base: every one resets at teardown, or (`PersistFailure`) per the base's flag | per parameter: for a reset row, treat the leaf's discarded result as an exit → no reset → RED; for a no-reset row, run the reset before the reply's `toAdmin` / before the persist → reset → RED |
| R-1o | `…LogonSentLogout_Resets` (initiator, rol), plus `…_MtlsUnauthorised_NoReset` (direct mTLS initiator started by `open()`, with rol set), `…LogonSentLogout_RolOff_MtlsUnauthorised_Unchanged` (direct mTLS initiator started by `open()`, rol off) and `NotConnectedLogout_NoReset` (acceptor), on one store double. Such an initiator has no live identity (on an initiator only `install_reconnected_transport` sets `live_peer_id_`), so both mTLS arms meet the gate's arm (2): mTLS with no live identity fails closed, with the same event and no `authorize` call. The `Resets` and rol-off arms (one per route of the contract's "Precedence on one Logon frame") feed the Logout at 1 ≤ 34 < N, on a hydrated N > 1 | LogonSent: no 35=5 written by fixpp; `reset_to(1, 1)`; Disconnected (the forced-hit arm of the other three). The `Resets` and rol-off arms' "no 35=5 written by fixpp" at 34 < N is read at the US3 head (FR-012: a 35=5 in LogonSent never draws the too-low Logout). mTLS unauthorised: no reset, Disconnected, the authorisation-failed event. Rol off (contract W11, the orchestrator ruling of 2026-11-18): no reset, Disconnected, the arm returns success, and **no** authorisation-failed event; that absence reads the same event type from `Session::recent_events()` by the same predicate as the mTLS arm, whose event is its forced hit. NotConnected: no reset | LogonSent: base makes no reset. mTLS unauthorised: RED on base, because the base refuses a 35=5 in LogonSent at `interpret_logon` (a non-Logon), before the identity gate, so the authorisation-failed event is never emitted; that assertion also witnesses the N-8 order (the gate before arm S's reset). Rol off: GREEN on base (pin; the base never reaches the gate for a 35=5). NotConnected: GREEN on base (pin) | delete S's reset → RED; skip S's authorisation test → the mTLS arm resets → RED; run arm S's branch whatever the knob → the rol-off arm sees the authorisation-failed event → RED; invoke a reset point (the marker `reached` and the unit) from the NotConnected arm → `reset_to` seen → RED |
| R-1p | `…LivenessDuringReceiptUnit_NumbersNothing` (A in Active, HookedStore holds `reset_to`, small HeartBtInt) | the mock clock advanced past one, then two HeartBtInt during the hold: no frame written after the reply; manager `(1, 1)` after release | the base has no receipt unit (its reset is after the loop is joined) | move the Disconnected write after the unit → the loop's Heartbeat or TestRequest is numbered 1 → RED |
| R-1q | `…BoundRouteFrameDuringUnit_Drained` (B, HookedStore holds `reset_to`) | a frame at 34 > 1 fed during the hold: no ResendRequest, no inbound advance, no `fromApp` | as R-1p | as R-1p |
| R-1r | `…BoundRouteInFlightPumpArm_RefusedAtTheBoundary` (B; a pump arm with a write after the bound's wake, held in HookedStore across the bound's unit) | after release: nothing the in-flight arm writes reaches the wire or the store's numbers; store and manager `(1, 1)` | as R-1p | delete RR-4's boundary test in `store_then_emit` (one arm) and in `persist_inbound_advance_` (a second arm) → the in-flight write lands → RED. The mutant is the same on either route: if phase 0 finds no Active sub-arm that writes after a suspension following `check_inbound`, the cell drives the boundary through the test seam `session_test_access::store_then_emit` after a receipt reset, and records the reading |
| R-1s | `…CloseWaitsForLogonArmUnit_NoKnob` (no knob, initiator 141=Y acknowledgement, HookedStore holds `reset_to`) | `close()` during the hold does not return before release (B-104-2's other half) | base's wait is inside the predicate block, which no knob opens | move the wait back inside the predicate block |
| R-1t | `…EngineStopDuringReplyStore_JoinWaitsPastTheBound_OneResetByTheArm` (O15; rol only, A in Active, on `plain_engine_rig.hpp`'s mock-clock Engine with a HookedStore, as `engine_reset_unit_stop_test.cpp` does; HookedStore holds the reply's `store`; plus the step-3 mark: a private `Engine` test seam set through `engine_test_access`, awaited by `Engine::stop()` on the control strand after step 2's close join and immediately before step 3's role-loop join) | during the hold, in this order: install the step-3 mark (the cell's hook sets a flag and returns); start `Engine::stop()`; pump, bounded on a condition, until the mark has run or `session_test_access::is_closing` holds for the session. Each arm reaches exactly one of the two with no clock advance: the candidate reaches the mark with the session not closing; a `close()` dispatched before step 3's join sets `closing` at entry and then waits, so the mark does not run. Assert that the mark ran and the session is not closing: the order, read directly. Then advance the mock clock once past `logon_timeout_ms` and pump, bounded by a settle window. HeartBtInt is set so that no Heartbeat, TestRequest or liveness-disconnect deadline falls inside the advance, a ratio stated in the cell, so the liveness loop writes nothing and records no Disconnected during the hold. Then, with the hold still in place: the session is still not closing, no `session_event_close_reset_wait_expired`, and stop's completion is not observed after a pump bounded on a condition. Known positive: the same harness observes stop's completion once the hold is released. After the release: exactly one `reset_to`, no `reset`, recorded before stop returns. On every exit path the cell releases the hold, pumps stop to completion and drops its session handle before the Rig is destroyed, so a failed assertion stays an assertion failure. **No no-clock twin**: an Engine session always has a clock (`validate_engine_config` at `Engine::start` refuses a null `EngineConfig::clock`, and `Session::open` falls back from a null `clock_override` to it), so the input has no form under stop. The contrast with R-1l is past the bound instead: there `close()` closes the receipt out, here stop waits. Every pump inside the hold window, from the start of stop to the release (the order pump, the settle window after the advance, and the pump that must not observe stop's completion), has a budget derived from HookedStore's `kHoldBound`, so that their sum stays below it. After the release, assert that the hold was ended by the cell's release and not by its bound (`StoreLog::hold_timed_out` false), as `engine_reset_unit_stop_test.cpp` does. | base: the teardown's `reset`, no `reset_to` | move step 4's `close(terminal)` loop to between step 2's close join and the step-3 mark, ahead of step 3's role-loop join (Codex's rejected reorder, plan.md `## Gate A`) → `close()` is entered during the hold and sets `closing`, then waits in RR-2 item 4's poll, which cannot expire before the cell's advance, so the mark does not run → the order assertion fails → RED, as an assertion failure, not an abort, whatever instant the poll took its bound. Not "delete the join": that is the registry use-after-free step 3 prevents (INV-6), a crash rather than a discriminating RED |
| R-1u | `…CloseInsideOnLogout_ResetOnceByTheArm` (A in Active, a clock; `close(terminal)` started inside the `onLogout` the arm's Disconnected write fires) — ruled case "`reached`, unit not started, teardown → one reset" | exactly one `reset_to`, no `reset`, finished before `close()` returns; the hook sees state Disconnected | base: the teardown's `reset`, no `reset_to` | split RR-2 item 3's turn (a post between the marker's `reached` and `run_reset_unit_`) → `close()`'s posted body runs in that window, finds `reached` and no unit in flight, passes its wait and drains the manager; the unit's manager calls then fail → zero resets → RED. This is the input the teardown's "`reached`, unit not started" term would need; item 3 leaves it none |
| R-1v | `…ReplyStoreCancelled_NoReset` (A in Active, rol only, a mock clock; a cell-local store double whose outbound `store()` returns `store_cancelled` for the reply) — O9 | the number is taken back (manager next outbound N_out again); Disconnected; no frame; no `reset_to`, no `reset`; the owner cleared the marker while `operation_aborted` unwound the arm: a later `close()` returns with the mock clock below `logon_timeout_ms` and no `close_reset_wait_expired`. Forced-hit arm: R-1n's `StoreFailure` input on the same double resets. The thrown timing reaches the same block after the `try` (D-OUT-2), so this one cell witnesses both | base: the teardown's `reset` (flag set at receipt) | clear the marker on return only, not during unwinding → the marker stays `pending` → `close()` polls to `logon_timeout_ms` and emits the event → RED |
| R-1w | `…CloseNotHeldWhenReceiptEndsWithoutUnit_{FromAdminThrow,Storeless,StuckPending}` (A in Active, rol, a mock clock) | `FromAdminThrow`: `close()` started during a held reply `store`; the hold released; `fromAdmin` throws; with the mock clock advanced to just below `logon_timeout_ms`, `close()` has returned, no `close_reset_wait_expired`, no reset. `Storeless`: no store (`store_factory` null), the reply's transport write held (`BlockingWriteTransport`, `tests/support/blocking_write_transport.hpp`, through its `blocking_write_state`'s `arm_block()` / `release()`); `close()` started during the hold; released: the arm reaches its reset point, `run_reset_unit_` returns before setting `reset_unit_in_flight_`, manager `(1, 1)`; `close()` returned as above. Phase 1 reads whether the held write outlasts `close()`'s `close_async()`, which cancels and joins an in-flight operation. If it does not, the storeless exit has no input while `close()` waits, like the manager failure, and the poll covers it by construction: the arm is dropped and that reading is recorded. `StuckPending`, the instrument's forced hit: the hold never released and the mock clock advanced to the bound → one `close_reset_wait_expired`. The manager-failure exit has no input while `close()` waits: the manager refuses only after `close()`'s `seqnum_mgr_.drain()`, which follows the wait | base: `FromAdminThrow` resets at teardown; `Storeless` has no receipt unit | restore 093's event wait (`unit_done.async_wait \|\| await_deadline`, woken only at the unit's completion) widened to `pending` → `close()` runs to the bound and emits `close_reset_wait_expired` in `FromAdminThrow` and `Storeless` → RED |
| R-1x | `…TerminalEmissionAtResetPoint_ResetOnce` (a direct `Session`, A in Active, rol, a clock; the cell `co_spawn`s `on_inbound_frame` bound to its own cancellation slot and emits `cancellation_type::terminal` on it from inside the `onLogout` that RR-3 step 2 fires) | exactly one `reset_to(1, 1)`; the marker `reached`; numbers `(1, 1)`; no `reset` | base: the teardown's `reset`, no `reset_to` | (1) delete the cancellation-state reset at the start of the reset point's turn → `reset_to_one`'s lock throws `operation_aborted` after the marker is `reached` → zero resets → RED, read by "exactly one `reset_to`" (this mutant leaves the marker `reached`); (2) the frame-local owner clears unconditionally on exit (not only from `pending`) → the marker reads `none` after the arm → RED, read by "the marker `reached`" (T041's mechanism; that assertion's only RED source, since it reads a symbol 104 adds) |
| R-1y | `…DuplicateTooHighLogoutWhilePending_OneResetByTheBound_{RouteA,RouteB}` (rol only, a mock clock, HookedStore) — O18a, Codex round 3 #1 | a too-high Logout opens the bound; a second too-high Logout while the 103 mark is `pending` is ignored (no second bound); the gap is filled past the first S; the bound acknowledges: exactly one acknowledgement Logout and exactly one `reset_to(1, 1)`, at the hook with state `Disconnected`; after `close()`: no `reset`. `RouteB`: the duplicate arrives with the resend outstanding (103's `SecondTooHighLogoutWhilePending_*` input). `RouteA`: the gap is first filled to S, so the resend is closed while the mark stays `pending`, and the duplicate arrives above N (103's `H_LogoutResentInSequence_*` input with the resent Logout above N). Phase 2's known positive for the marker's assert (plan phase 1): in a scratch copy, set the marker to `pending` in the bound immediately before its reset-point assert → this cell's bound trips the assert | base: the teardown's `reset`, no `reset_to` | round 3's hand-off, applied together: B's spawn branch sets `pending`, the bound's reset point moves it only from `pending`, and the ignored branch's frame-local owner clears it on its return → the bound finds `none` → zero resets → RED. Under construction A the ignored branch's clear alone is a no-op, because the marker is already `none` there. So the mutant restores the hand-off it pins against. "Exactly one" is its own forced hit: zero and two both fail |
| R-1z | `…ClaimsAPendingBound_OneResetByTheArm_{CloseDuringReplyStore,OneDelivery}` (R-1k on route B: rol only, a mock clock, HookedStore) — O12, Opus round 3 N-1 | a too-high Logout opens the bound; the gap is filled to S; the Logout resent in sequence at S reaches arm A, which claims the 103 mark; A's reply `store` is held; `close(terminal)` is started during the hold; the hold is released. `close()` does not return before the release; exactly one `reset_to(1, 1)`, by A (hook: state `Disconnected`), finished before `close()` returns; no `reset`; no `close_reset_wait_expired`. Arm `OneDelivery`, the input of INV-T's window (plan phase 1): a GapFill past S and the in-sequence Logout at the new N in **one delivery**, from a cell-local coroutine that awaits `on_inbound_frame` for both back to back, as `run_read_pump` does. `Scene::feed` pumps the context after each frame, so two feeds cannot open the window. The bound's wake is then posted and not yet run when the Logout passes the drain test. A claims the mark, the bound exits unacknowledged, and there is one acknowledgement Logout (A's reply) and exactly one `reset_to(1, 1)`, by A. The settling experiment's known positive (plan phase 1): in a scratch copy, a capped post loop between `on_inbound_active_`'s drain test and its dispatch runs until the 103 mark leaves `pending`, so the bound reaches `acking` while the Logout is suspended, and this arm aborts on the entry assert; the cap K is derived in plan phase 1 from the bound's wake path, and hitting it with the mark still `pending`, or leaving the loop with a phase other than `acking` (then hold the bound's reply `store` for the scratch run), fails the known positive. A green run proves only that no suspension in the window outlasts the bound's wake (at least two posted turns); a one-turn suspension is covered by the reading of INV-T alone | base: the teardown's `reset`, no `reset_to` | the bound writes `none` to the marker at its claimed `!live()` exit (round 3's body clear, unconditional) → A's `pending` is lost, `close()` returns during the hold, and A's reset point finds `none` → RED (in Debug, RR-2 item 5's assert fires first) |
| R-2 | `…BothKnobs_PeerAnswers_TwoResets_EndsOneOne` (HookedStore) | two resets: the hook first sees `reset_to` in L's arm with the transport open, then `reset` in the teardown; `(1, 1)` (D2) | base: one reset | skip the teardown reset when the receipt reset ran → one reset → RED |
| R-3 | `…LogonReceivedLogoutWhileClosing_ResetOnceByTheArm` (a direct acceptor, rol only, a clock, HookedStore) — arm A's LogonReceived row, **conditional on T011's reading** (below) | the acceptor's Logon arm exits superseded with the state still LogonReceived; with `close(graceful)` still in flight before it records Disconnected (its flush held through `Hooks::flush_until`), an in-sequence peer Logout reaches arm A. The hook sees `reset_to(1, 1)` with state `Disconnected`; exactly one `reset_to`; no `reset`; `close()` returns only after the arm's unit. The wire is the one T011 reads for A in LogonReceived (`inbound_logout_leaf_` replies in Active only) | base: the teardown's `reset`, no `reset_to` (the base sets the flag at A's entry) | delete A's reset point (R-1d's first mutant) → zero `reset_to` → RED |
| R-5a | `…InFlightGapFillAcrossBoundUnit_ManagerStaysAtOne_{OneDelivery,Deadline}` (B; rol only, a mock clock, HookedStore holding the GapFill's inbound persist through `Hooks::on_inbound_persist_call`) — O21, path (a) of D11 | `OneDelivery`: one cell-local coroutine awaits `on_inbound_frame` for two frames back to back, as R-1z's `OneDelivery` does: frame k fills the gap past S and wakes the bound; frame k+1 is an in-sequence GapFill whose NewSeqNo X is above its own 34 + 1, and its persist is held while the bound's posted turn claims, records `Disconnected` and runs the unit. `Deadline`: one GapFill that fills only part of the gap, its persist held, and the mock clock advanced past `logout_disconnect_timeout_ms`, so the bound acknowledges at its deadline during the hold. Both arms, after the release: manager NextNumIn 1 and next outbound 1; store `(1, 1)`; exactly one `reset_to(1, 1)`, the hook seeing `Disconnected`; the GapFill frame's `on_inbound_frame` result is success; exactly one `Disconnected` entry in `fsm_visit_history()`. Forced-hit arm: the `OneDelivery` input with rol off, where no reset point is reached and the GapFill's `set_next_inbound` lands (manager NextNumIn X), which shows the reading can see a value other than 1. Pumps budgeted from `kHoldBound`, ratio stated | base: no receipt reset under rol (it resets at teardown), so the manager is at X and the store is not `(1, 1)` before `close()`. The RED is D1's absence; the freeze's own evidence is the mutant. The rol-off forced-hit arm is GREEN on the base, a pin: with no receipt reset nothing is refused, there or at the head | delete the freeze test in `set_next_inbound` → manager NextNumIn X → RED |
| R-5b | `…SendReconcileAcrossReceiptUnit_ManagerOutboundStaysAtOne` (A in Active; rol only; a cell-local persistent store double, its factory's `yields_persistent_store()` true) — O21, path (b) | the double refuses the application frame's outbound `store()` (a persistent-store failure) and holds the reconcile's `next_seqnum(outbound, false)` until the cell releases it, then returns the store's pre-reset next outbound number; its reply `store()` and `reset_to` succeed. While the read is held, a peer Logout at N reaches arm A's reset point (reply, `Disconnected`, unit). After the release: manager next outbound 1 and NextNumIn 1; store `(1, 1)`; `send` returned the store's error and its frame is not on the wire; the double records the reconcile read's completion after its `reset_to` (the order without which the cell is vacuous); exactly one `reset_to`. Forced-hit arm: the same input with rol off, where the reconcile's `set_next_outbound` lands (manager next outbound equals the read value) | base: no receipt reset under rol, so the manager holds the read value and the store is not `(1, 1)` before `close()`. The rol-off forced-hit arm is GREEN on the base, a pin, as R-5a's | delete the freeze test in `set_next_outbound` → manager next outbound is the read value → RED |
| R-5c | `…InFlightRejectAcrossBoundUnit_RefusedBeforeToAdmin` (B; rol only, a mock clock, HookedStore holding the frame's inbound persist through `Hooks::on_inbound_persist_call`; the application's `toAdmin` throws on a 35=3) — O21, path (c), the consult's claim 3 | an unparseable in-sequence frame at S, which draws a Reject through `dispose_unparseable_` → `consume_rejected_seqnum_`: its `check_inbound` advances past S and wakes the bound, and its persist is held while the bound's posted turn runs the unit. After the release: manager next outbound 1; store next outbound 1 and `(1, 1)`; zero `toAdmin` calls for a 35=3; no Reject in the store or on the wire; exactly one `reset_to(1, 1)`; exactly one `Disconnected` entry in `fsm_visit_history()`. Forced-hit arm: the same input with rol off, where `toAdmin` is called once for the Reject (and throws), which shows the `toAdmin` predicate can report a call | base: no receipt reset under rol, so the manager's outbound is pre-reset, and `toAdmin` is called for the Reject. The rol-off forced-hit arm is GREEN on the base, a pin, as R-5a's | delete the freeze test in `assign_outbound` → `toAdmin` called and throws, manager next outbound 2 → RED. Second mutant: restore the unconditional `Disconnected` write in the `assign_outbound` failure branch of the `emit_session_reject_` overload that `dispose_unparseable_` reaches (the one taking a reason, a RefTagID and a text) → two `Disconnected` entries → RED |
| R-5d | `SeqnumManagerFreeze104.NonResetMutatorsRefusedWhileFrozen` (a `SeqnumManager` alone on an io_context; counters set through `seqnum_manager_test_access::set_counters`, the flag set and read through `seqnum_manager_test_access`) — RR-5 items 3 to 5 | frozen: `check_inbound`, `assign_outbound` and `hydrate` return `session_already_closed`; `set_next_inbound` and `set_next_outbound` return success; `take_back_outbound` returns; after each, both counters are unchanged. `reset_to_one` returns success and both counters are 1. Thawed (the forced-hit arm): every mutator writes again, each counter read | none on base: the freeze does not exist (a compile failure is not RED evidence; Execution rules). Written with the mechanism (T049); its RED source is its mutants | one per refused mutator, delete its freeze test → that mutator's assertion RED; add a freeze test to `reset_to_one` → its assertion RED (and R-2's teardown `reset` absent). `check_inbound` and `take_back_outbound` have no session input while frozen, and `hydrate` only a direct caller's overlap with `open()`'s emit (contract RR-5 item 5), so R-5d is their only witness |
| R-5e | `…CloseOutThenFromAdminReject_RejectRefusedBeforeToAdmin` (A in Active; rol and rod, no clock; HookedStore holds, after completing it, the reply's `store` with `hold_until_close_reset`, so the arm resumes once the teardown's `reset` is issued and before `close()` drains the manager; `fromAdmin` rejects the Logout) — RR-2 item 5 as amended, O13-r, sub-ruling 1 | `close(terminal)` closes the receipt out to `reached` (no clock, so no wait), then issues the teardown's `reset` (rod; `reset_to_one` is a reset writer, accepted while frozen); the hold returns; the arm resumes, `fromAdmin` rejects, and the Reject is refused at `assign_outbound`: zero `toAdmin` calls for a 35=3; no Reject in the store or on the wire; exactly one `Disconnected` entry; no `reset_to`; one `reset`; both numbers 1 after `close()`. Forced-hit arm: the same fixture with no `close()`, the hold released by `Hooks::release_when`: the arm's Reject calls `toAdmin` once (O3) | base: no close-out; the teardown's `reset` releases the hold, and the arm's Reject takes a number from the undrained manager and calls `toAdmin`. The no-`close()` forced-hit arm is GREEN on the base, a pin: no close-out, so nothing is frozen | drop the close-out's freeze → `toAdmin` called for the Reject → RED. The window is between the close-out and `close()`'s `seqnum_mgr_.drain()`, which refuses every mutator once it has run; a cell whose arm resumes only after the drain cannot tell the two apart, which is why the hold is released by the teardown's `reset` |
| U-1 | `UnaskedResetAck104.Lenient_HydratedN_ResetsAndActive` (persistent store hydrated at in, out > 1) | Active (not merely "no second Logon"); inbound next 2, outbound next 1 in manager and store; `reset_to(2, 1)`; event `by_peer_request = true`; no 35=A written after fixpp's first; the next `send` is 34=1 | base ends Disconnected, too low | restore `check_inbound` before the 141 read |
| U-2 | `…Unilateral_…` | as U-1 | as U-1 | as U-1 |
| U-3 | `…Strict_OwnReset_Unchanged`, two arms: ack 141=Y at 34=1, and at 34 = M > 1 | 34=1: as today (inbound next 2). M > 1: as today (`check_inbound` against 1 → too high → Disconnected, no 789) | GREEN on base (FR-010 pin) | apply the unasked branch to the asked case → the M > 1 arm goes Active, not consumed → RED |
| U-4 | `…AtMAbove1_NotConsumed_ResendOnNextFrame` (N = 7, M = 5) | Active, inbound 1, outbound 1; the peer's 34 = 6 Heartbeat draws ResendRequest 7=1, 16=0 | base: M < N → Disconnected | set consumed for any M |
| U-5 | `…With789_TooHighAfterReset_Logout` (`enable_next_expected_msg_seq_num` set; the acknowledgement carries 789 = X > 1; contract U3) | Logout "NextExpectedMsgSeqNum too high, expecting 1 but received X"; Disconnected | base: too low → Disconnected, no Logout | skip `honor_peer_next_expected_` on the unasked branch → Active, no Logout → RED |
| U-6 | `…FollowUpResendRequest_GapFillOneOne` | after U-1, RR(34=2, 7=1, 16=0) → exactly one frame, GapFill 34=1, 43=Y, 122=52 (OrigSendingTime equal to its own SendingTime, contract U5), 123=Y, 36=1; inbound next 3; the next `send` is 34=1 | unreachable on base: U-1's input ends Disconnected there, so the cell fails at its setup, which is not RED evidence. Its RED source is its mutant (T077) | D3.3's rejected alternative, "answer an empty range with nothing" → no frame → RED |
| U-7 | `…FollowUpResendRequest_ExplicitEnd_Clamped` (FIX.4.1 999999 and FIX.4.4 explicit end) | NewSeqNo = 1, not end + 1 | unreachable on base by this route (a setup failure, as U-6, not RED evidence). Its RED source is its mutant (T077); the clamp's base RED is U-8's | drop the D3.3a clamp |
| U-8 | `ResendAnswer104.ExplicitEndAboveNextOut_NoOvershoot` (Active, no reset, next out 5): RR(7=5, 16=10), and RR(7=10, 16=20) | GapFill 34=5, 36=5; and GapFill 34=10, 36=5 (NewSeqNo below MsgSeqNum, QFC's frame, N-6) | base writes 36=11 and 36=21 | drop the clamp |
| U-9 | `UnaskedResetAck104.Mtls_Unauthorised_NoReset` (direct mTLS initiator started by `open()`, ack 141=Y at 34=1; such an initiator has no live identity, so the Logon path's mTLS gate fails closed in its arm (2), with no `authorize` call) | no `reset_to`; hydrated numbers unchanged; Disconnected; the authorisation-failed event. Forced-hit arm: the authorised twin, an initiator brought to LogonSent by `install_reconnected_transport` with an identity the policy authorises (the gate's arm (1), which calls `authorize`; the setup of `LiveIdentityBindingTest.MockHandshakeIdentityDrivesAuthorization`), records `reset_to(2, 1)` | RED on base, both arms: the base's Guard (4) `check_inbound` ends the acknowledgement as too low before the mTLS gate, so the unauthorised arm sees no authorisation-failed event and the authorised twin no `reset_to(2, 1)`. The event assertion also witnesses the N-8 order (the gate before the unasked unit) | run the unasked unit before the gate → `reset_to(2, 1)` seen → RED |
| U-10 | `UnaskedResetAck104.PersistentStoreRefusesReset_Fatal` (a lenient initiator on a persistent StoreDouble, its factory's `yields_persistent_store()` true, hydrated at in, out > 1; `fail_next_reset()` set; ack 141=Y at 34=1; contract U7) | 030's persistent-fatal ending (data-model "The reset unit: new callers"): the store saw one reset call (`reset_call_count`; StoreDouble does not override `reset_to`, so `MessageStore::reset_to`'s default body calls the failing `reset()`); the arm returns the store's error; Disconnected, never Active; no frame after fixpp's Logon; the store's numbers keep their hydrated values. Forced-hit arm: the same double without `fail_next_reset()` goes Active (U-1's input) | RED on base: the base's Guard (4) ends the acknowledgement as too low before any unit, so the store sees no reset call and the arm returns success | on the unasked branch, log a persistent store's refusal and continue (024 I-07's volatile disposition) → Active → RED |
| T-1 | `LogonTooLow104.Initiator_LogoutWithText` (NextNumIn 5, ack 34=3) | exactly one 35=5; 58 byte-equal to the text; no 1409, no 789; 34 = fixpp's next outbound; NextNumIn 5 in manager and store; `toAdmin(Logout)` once; Disconnected | base writes no 35=5 | remove the helper call |
| T-2 | `…Acceptor_LogoutWithText` (hydrated NextNumIn 5, Logon 34=3) | as T-1, from the acceptor | as T-1 | as T-1 |
| T-3 | `…PossDup_LogoutWithText` (both arms, 43=Y) | as T-1 (D6) | as T-1 | add a 43=Y exemption |
| T-4a | `…Overflow_PairedWithNearBound` (each arm): CompIDs of length L (the longest for which the Logout fits) and L + 1 | L: one 35=5 with the text (the forced hit); L + 1: no frame, Disconnected, success | L: base writes no 35=5. L + 1: GREEN on base (pin: the base writes no frame either); its RED source is the larger-buffer mutant | remove the helper call → the L arm RED; build into a larger buffer → the L + 1 arm emits → RED |
| T-4b | `…ToAdminThrow` (each arm) | `toAdmin` called once with a 35=5 whose 58 is the text (the helper was reached); no frame; Disconnected; `app_callback_threw` | base: no `toAdmin` call, success | remove the helper call; or send despite the throw |
| T-4c | `…AssignFailure` (each arm; outbound at `seqnum_max` through the test seam) | no frame; Disconnected; the assign error returned | base returns success | discard the assign error in the helper → success → RED |
| T-4d | `…PersistentStoreFailure` (each arm; StoreDouble refuses the Logout's store) | `toAdmin` called with the text; no frame; Disconnected; success | base: no `toAdmin` call | transmit despite the store failure → RED |
| T-4e | `…WriteFailure` (each arm; sink throws) | the Logout is stored (store holds a 35=5 at N_out); no frame on the wire; Disconnected; success | base stores nothing | propagate the write failure as the arm's return → RED |
| T-4f | `…VolatileStoreFailure` (each arm; a store double whose factory's `yields_persistent_store()` is false refuses the Logout's store) | one 35=5 with the text on the wire (024 I-07, logged then sent); Disconnected; success | base writes no 35=5 | remove the helper call → no 35=5 → RED |
| T-5 | `…ResetOnLogout_NoResetForTooLowLogout` (initiator, `reset_on_logout` set) | first its own Logout: one 35=5 with T-1's text and one `toAdmin(Logout)`; then, after `close(terminal)`: no `reset_to`, no `reset`. Forced-hit arm on the same store double and fixture: a Logout received in LogonSent (arm S) records one `reset_to` | RED on base: base writes no 35=5 | invoke a reset point (the marker `reached` and the unit) from `refuse_logon_with_logout_` after its emit → `reset_to` seen → RED |
| T-6 | `…Precedence_*`: too low and a bad SendingTime; too low on a strict acceptor without 141; too low on an mTLS acceptor with an off-list identity; too low on the acceptor with refused credentials (`authorize_logon`); too low on a direct mTLS initiator started by `open()`, which has no live identity, so the gate fails closed in its arm (2) with no `authorize` call | SendingTime's ending; the strict text; **no frame** and the gate's own ending for each identity arm (FIX-TC 1S(c), clarification N-8): the authorisation-failed event on the two mTLS arms; on the credentials arm, which emits no event, a counting refusing callable installed with `compid_authorization_policy::set_logon_validator`, called once | SendingTime and strict arms: GREEN on base (pins: both guards run before `check_inbound`). Identity arms: RED on base, because the base's `check_inbound` ends a too-low frame silently before the gates (the mTLS gate follows it in both Logon arms, and `authorize_logon` on the acceptor), so the mTLS arms see no authorisation-failed event and the credentials arm's validator is never called. Each identity arm also witnesses the N-8 order (the gates before the too-low Logout) | move the too-low branch ahead of the SendingTime guard → the SendingTime arm RED; move it ahead of the strict refusal → the strict arm RED; send the too-low Logout before the identity gates → each identity arm RED |

**R-3's condition (a recorded decision, as R-1w's `Storeless`).** D1's "Read from source" gives arm A's
LogonReceived row input only while a close or a stop is in flight. `close()` joins no inbound frame (read from `Session::close`'s awaits: re-derive with `grep -n 'co_await'` over its
body), so a cell reaches the row only by holding `close()` itself after its first commit of `closing` and before it records
Disconnected, which `close(graceful)`'s flush, held through HookedStore's `Hooks::flush_until`, does. Phase 0
(T011) reads whether that input is drivable: the acceptor's Logon arm suspended after it records LogonReceived,
`close(graceful)` started, the Logon arm released to its superseded exit, then the Logout delivered while the flush
is held. If it is, R-3 is written and read as the other cells are. If it is not, the row has no input while
`close()` can be observed in flight, R-3 is dropped, and the reading is recorded in the evidence file.

**Rules carried into the cells.**
- A "no reset" cell runs with `reset_on_disconnect` off.
- **No cell proves "no reset" with a forced miss alone.** Each carries, on the same store double and record, a
  forced-hit arm whose input reaches a reset point and whose record shows one `reset_to`. The arm ships in the cell;
  a mutant is a different thing and runs in a scratch copy.
  - R-1a, R-1b: the answered twin (R-1c's input).
  - R-1f(1): R-1f(2).
  - R-1g: the same fixture with `fromAdmin` returning (R-1d's input).
  - R-1h: its own first half, the old connection's reset.
  - R-1l: R-1k's input, the same hold with a clock.
  - R-1n's no-reset rows: its reset rows.
  - R-1o's mTLS, rol-off and NotConnected arms: its LogonSent arm. The rol-off arm's "no authorisation-failed
    event" also has the mTLS arm as its forced hit, on the same fixture and observation path.
  - R-1v: R-1n's `StoreFailure` input.
  - R-1w `FromAdminThrow`: R-1k's input. Its "no `close_reset_wait_expired`" assertion has `StuckPending` as its
    forced hit, and so do R-1f(1)'s, R-1v's and R-1t's. A forced hit in another cell stands for an assertion only
    where both read the event through the same observation path: the same event type, read from the session's own
    event ring (`Session::recent_events()`) by the same predicate, with no more events emitted after it than the
    ring holds (`kSessionEventRingCapacity`). R-1t reads the Engine session's ring through its session handle by
    `StuckPending`'s predicate, so `StuckPending`'s hit shows the path can report the event; the clock wiring
    differs (the Engine's mock clock) and is not part of the observation.
  - R-1t's "stop has not returned": its own release arm, where the same harness observes stop's completion.
  - T-5: arm S on the same fixture.
  - U-9: its authorised twin, `reset_to(2, 1)`.
  - R-5a, R-5b and R-5c: the same input with rol off, where no reset point is reached and the in-flight write lands
    (a NextNumIn of X, the reconcile's read value, a `toAdmin` call for the Reject). R-5c's history assertion has
    its second mutant as its RED source.
  - R-5d: its thawed arm, where every mutator writes.
  - R-5e: its no-`close()` arm, where the Reject's `toAdmin` is called once.
- A "no second Logon" cell asserts Active and a next frame numbered 1.
- A cell that reads "at the reset point" reads at the hook (state, transport, which store call), never only after
  `close()`. A final-state assertion cannot tell the arm's reset from a teardown reset (R-1k, R-1l, R-1t, R-1u).
- A cell about `onLogout` asserts the pre-reset numbers there (DQ-10).
- Bounded waits are derived from the competing timeout (`logout_disconnect_timeout_ms`, `logon_timeout_ms`,
  HeartBtInt), as a stated ratio.
- Every mutant runs in a scratch copy, with the binary `strings`-checked fresh, and the test count is recorded.
  A selection that runs zero tests fails the step.

### D11 — The manager freeze: a writer in flight at the reset point (owner ruling 2026-11-18; FR-019; contract RR-5)

#### Decision

Implementation stopped at T011 on a window that 104 itself opens: 103 resets only at teardown, after `close()`'s
joins, but 104 resets at the reset point, while other coroutines of the session may be suspended mid-write. The owner
ruled the gap closed by **construction (Z)**:
- `SeqnumManager` accepts only the reset writers while the receipt marker is `reached`, from the reset-point turn
  until `install_reconnected_transport` sets the marker to `none`;
- the refusal is RR-4's own pattern, moved to the one choke point where every counter write is made;
- it adds no wait;
- RU-1, RU-3, construction A and RR-4 are unchanged.

Contract RR-5 states the rule, its scope, the allowed writers and the refused returns. FR-019 is the requirement.
Cells R-5a to R-5e are in D10.

#### The gap, read from source (T011's STOP finding; the Fable consult's claims 1 to 3)

The code is the base's. Each path is cited by symbol and quoted phrase.

1. **(a) A route-B GapFill.**
   - `on_inbound_active_gap_fill_`, with validation on, persists before it applies ("persist the +1 here, then
     jump"): `auto p_r = co_await persist_inbound_advance_();`, then `co_return co_await
     apply_inbound_sequence_reset(parse_seqnum(hdr.new_seqno),`.
   - `persist_inbound_advance_` suspends only on a persistent store: it returns first at
     `if (!store_is_persistent_)`.
   - `apply_inbound_sequence_reset` then reads `const seqnum_t expected = seqnum_mgr_.next_inbound_unsafe();`. When
     `new_seqno > expected`, it calls `auto sr = co_await seqnum_mgr_.set_next_inbound(new_seqno);`. Nothing tests
     the state or the marker in between.
   - If the bound claims and runs its unit during the persist, `expected` is 1. The manager ends at NewSeqNo while
     the store says 1.
   - Two inputs reach it:
     - the bound's deadline: `run_peer_logout_bound_` acknowledges at its deadline whether or not the gap was
       filled;
     - one delivery: frame k fills past S (`note_inbound_advance_` wakes the bound, whose completion is posted),
       and frame k+1 is a GapFill whose persist suspends.
2. **(b) The reconcile, on any route.**
   - `store_then_emit`, on a persistent store's failure, captures the error and then runs "the best-effort
     reconcile read": `auto dk = co_await store_->next_seqnum(direction_t::outbound, false);`, then
     `(void)co_await seqnum_mgr_.set_next_outbound(*dk);`.
   - "the Disconnected transition is caller-owned", so the caller's state holds while the read is suspended.
   - Its callers include emitters that are not pump frames: `Session::send` (through `send_impl`),
     `run_liveness_loop` and `run_logout_phase1`.
   - So route A's pump arm can run its unit while a send's reconcile read is suspended. The read, issued before the
     unit's `reset_to`, returns the pre-reset number, and `set_next_outbound` writes it after the unit.
3. **(c) A late `assign_outbound` that nothing takes back (Fable's claim 3).**
   - `dispose_unparseable_` calls `consume_rejected_seqnum_`. Its `check_inbound` advances an in-sequence frame,
     and its `co_return co_await persist_inbound_advance_();` suspends.
   - Then `emit_session_reject_` runs `auto assign_r = co_await seqnum_mgr_.assign_outbound();`, then
     `if (!fire_to_admin_(*rj_r))`. That branch returns `app_callback_threw` and takes nothing back.
   - `take_back_outbound` has one caller, in `store_then_emit` (re-derive: `git grep -n 'take_back_outbound(' --
     src include`).
   - If the bound's unit ran during the persist, the manager's outbound number ends at 2 and the store's at 1.
     Without the throw, `toAdmin` is still called with a Reject numbered 1, which RR-4 then refuses.
   - The same shape occurs in two more places:
     - `answer_resend_request_`: its `persist_inbound_advance_()` runs before the Reject drawn by a missing
       BeginSeqNo;
     - `on_inbound_active_unsupported_`: "persist, then Reject".

Safe as read, at the base and so without RR-5 (T011's record; Fable's claim 4):
- Reset-mode SequenceReset's `set_next_inbound`: nothing suspends before it.
- `apply_inbound_sequence_reset`'s NewSeqNo-below-expected Reject: its `store_then_emit` meets RR-4.
- `consume_rejected_seqnum_`'s `check_inbound` after a reset: a pre-reset number reads too high, so it returns
  success with no advance.

All of them pass through the freeze too.

#### The writers: the mutator enumeration, as a re-derivation recipe

- **The mutators** are the members of `SeqnumManager` that write a counter. INV-Z's recipe (contract RR-5 item 3)
  proves the set: `check_inbound`, `assign_outbound`, `take_back_outbound`, `hydrate`, `reset_to_one`,
  `set_next_inbound` and `set_next_outbound`. `take_back_outbound` is defined inline in the installed header. The
  other six are in `src/session/seqnum_manager.cpp`.
- **Their callers.** Run `git grep -n -E
  'seqnum_mgr_\.(check_inbound|assign_outbound|take_back_outbound|hydrate|reset_to_one|set_next_inbound|set_next_outbound)\('
  -- src include` and read each hit with its enclosing function. At the base every code hit is in
  `src/session/session.cpp`; the `session.hpp` hits are comments. A hit in another file is a new writer, and is read
  too.
- **Classify each enclosing function** by the coroutine that runs it, and by whether it can be in flight across a
  reset point:

  | Writer | Can be in flight across a reset point? |
  |---|---|
  | Another pump frame, on route B: the leaves after a suspension, `apply_inbound_sequence_reset`, `consume_rejected_seqnum_`, and the reconcile and take-back inside `store_then_emit`. INV-P excludes one on route A | yes: (a), (c), and (b) through its leaves |
  | `Session::send` (`send_impl`), `run_liveness_loop` and `run_logout_phase1`, each through `store_then_emit`'s reconcile and take-back | yes, (b), on any route |
  | The Logon emit, `emit_initiator_logon_` | not under the Engine: the read pump starts after `drive_reconnect` returns. A direct caller could overlap `open()`'s emit with arm S |
  | The Logon arms (`on_inbound_not_connected_`, `on_inbound_logon_sent_`, `honor_peer_next_expected_`) | no: each is the one pump frame, and no bound exists before Active |
  | The reset writers, `run_reset_unit_` and `reset_seqnums_to_one_durable` (from `close()`'s teardown, after its joins) | allowed (RR-5 item 4) |

- T013 re-runs the recipe and reads every caller that falls outside these classes before the freeze is accepted.
  Under RR-5 the classification is used only to read the refused returns. The freeze itself does not depend on it
  (INV-Z).

#### Why (Z), and not (X) or (Y): the consult's claims 3, 6 and 7

The candidates (consult `104-post-reset-inflight-manager-writers`, verdict "probable", recommending (Z)):

- **(X), a join.** The receipt unit would start only when no other session-state writer is in flight, reusing
  `drain_for_reconnect_`'s joins (`inbound_depth_`, the emit-in-flight wait).
  - **Claim 6: one deadlock shape, excluded only by enumeration.** `close()`'s bound join polls the bound's counter
    with no bound. An in-flight pump frame that awaits `close()` after a suspension, while the bound waits in the
    join for that frame, is a cycle. The in-frame `close()` sites are found with `git grep -n -e 'co_await close('
    -e 'close_on_late_parse_failure_()' -- src/session/session.cpp`, and the consult did not trace all of them.
  - The other shapes have no cycle, but they cost:
    - the join must sit between RR-3 step 2 and step 3, so arms L and S, which RR-2 item 1 says never suspend
      before their reset point, gain a suspension;
    - an arm held in the join becomes a new O13-style "no reset" ending under RR-2 item 4's poll;
    - under `Engine::stop()` it widens `L-093-12` to another coroutine's store call or blocked write.
  - **Claim 7: it reopens the wire window RR-4 closes.**
    - Today step 2 and step 3 are adjacent, so a frame that resumes after the bound's `Disconnected` meets
      `reached` and is refused.
    - With the join between them, that frame's later leaves (`inbound_reject_leaf_` and the others, which test no
      state) issue store calls and transmit pre-reset-numbered frames after the reply Logout, as 103 does.
    - RR-3's "fixpp sends nothing after the reply" would no longer hold on route B.
- **(Y), a refusal per write.** `set_next_inbound` and `set_next_outbound` would be refused, from an inbound arm or
  the reconcile, when the marker is `reached` or the state is not Active.
  - **Claim 3: it does not close the class.** It misses `assign_outbound`, path (c).
  - Its "state is not Active" term would also refuse the Logon's own reconcile, which runs in LogonSent and
    LogonReceived.
  - A refusal per call site holds only as long as the enumeration of call sites does.
- **(Z), the freeze at the choke point.** This is (Y) moved into `SeqnumManager` and keyed on the marker, not on the
  state.
  - Every counter write is inside a mutator body (INV-Z). So one test per mutator governs every writer, however it
    was reached and wherever it last suspended.
  - It adds no wait, so it needs no deadlock analysis.
  - It leaves RU-1, RU-3, construction A and RR-4 untouched, and it keeps RR-3's "nothing after the reply" on
    route B.
  - The consult's residual: (Z) closes the manager half only. The store half still rests on RR-4 and on "a call
    already issued runs to its end" (RR-2 item 5).
- The owner chose (Z), "refused", on 2026-11-18, and a narrow Gate A loop (spec Clarifications).

**No new departure.** QuickFIX processes a too-high Logout at its receipt (DQ-12), so a frame after it is never
processed there and nothing of it is sent. On the base, 103's bound lets such a frame in and sends its late Reject
after the acknowledgement, at a pre-reset number. Under (Z) nothing of it is sent after the reset point, so (Z) is
closer to QuickFIX than the base is. The pre-existing difference is 103's bound (B-103-7). On route A the in-flight
writers are fixpp's own concurrent emitters, which QuickFIX's synchronous send path does not have.

#### The allowed writers: the mechanism (sub-ruling 3)

- **Chosen: order plus mutator, with no token** (contract RR-5 item 4).
  - The receipt unit's manager set runs before the freeze, in the reset-point turn. `run_reset_unit_` arms the
    freeze after that set, when the marker is `reached`. This rests on research R1's inline grant (T008's record),
    which RR-2 item 3 already rests on.
  - The teardown's reset uses `reset_to_one`, which has no freeze test and no caller but the two reset writers
    (recipe in RR-5 item 4). `reset_seqnums_to_one_durable`'s other callers, the `reset_on_logon` resets on the
    Logon paths, never run on a frozen manager: the acceptor's arm by INV-F and INV-P, and `emit_initiator_logon_`
    because its only suspension before the reset is `ensure_hydrated_`'s store reads, after which `hydrate` refuses
    on a frozen manager and the emit returns (contract RR-5 item 4).
  - `Session` sets and clears the flag, and reads it for its Debug asserts, through a passkey that only it can
    construct.
  - Debug asserts guard the order: the manager is not frozen at `run_reset_unit_`'s entry, nor after its
    manager set immediately before its freeze; and the manager is frozen if and only if the marker is `reached`
    after each of the three sites.
- **Not relied on: a value coincidence.** The receipt unit's targets are `(1, 1)`, and `reset_to_one` already sets
  `(1, 1)`. A refused `set_next_*` inside the unit would therefore leave the same numbers, which a reorder mutant
  could not show. The design does not rest on this: the unit's setters run before the freeze, and the post-set assert
  catches a reorder in a Debug build (T061). In an asserts-off build the reorder is an equivalent mutant: no
  assertion can see it, and none is claimed.
- **Rejected alternatives:**
  - A reset-scoped token passed to the unit's setters, as a second overload per setter or a keyed setter replacing
    the unit's three calls. It is larger, and it changes 093's unit body (093 contract C-6 step 3).
  - `friend class Session`, with `Session` writing the flag directly. It is one line smaller, but it gives
    `Session` the counters too, so INV-Z would rest on reading `session.cpp`. The passkey limits `Session` to
    freezing, thawing and reading the flag.
  - Freezing on the session state rather than the marker: (Y)'s defect above.
  - A Debug-only accessor (`#ifndef NDEBUG`): rejected. `seqnum_manager.hpp` is installed, and its fixpp#511
    comment states that a macro-gated member makes the class differ between TUs, an ODR violation.
- **`hydrate` while frozen: reachable only by a direct caller's overlap with `open()`'s emit (above).**
  - `ensure_hydrated_`'s callers are `emit_initiator_logon_` and `on_inbound_not_connected_`: re-derive with
    `git grep -n 'ensure_hydrated_(' -- src`.
  - `emit_initiator_logon_` runs at `open()`, where the marker is `none`, and from `drive_reconnect`. There it runs
    after `reconnect_fsm_.drive_reconnect_attempt()`, which calls `install_reconnected_transport`, which thaws the
    manager (data-model "Order against an in-place reconnect"). That includes 025's `force` refresh on each
    reconnect. The thaw coming first does not by itself exclude a freeze during the emit's store reads. No frame of
    the new connection is delivered during them: `drive_reconnect`'s standalone-initiator precondition
    (`include/fixpp/session/session.hpp`: the caller's reader starts on the new `live_transport()` after the call
    returns) and, under the Engine, `run_connect_loop` entering the read pump only after `drive_reconnect` returns.
  - `on_inbound_not_connected_` is the acceptor's NotConnected arm, which a session in `Disconnected` never enters
    (INV-F).
  - The overlap: a direct initiator's `open()` emit, when a concurrent `on_inbound_frame` reaches arm S during
    `ensure_hydrated_`'s store reads. `hydrate` is then refused, `disconnect_unless_superseded_` writes no second
    `Disconnected` entry, and `open()` returns the error (contract RR-5 item 5). It is benign.
  - So, other than that overlap, `hydrate`'s freeze test has no session input. Its witness is the manager-level
    cell R-5d.

#### The refused returns (sub-ruling 2), read in source

The rule: the caller emits nothing, calls no `toAdmin` for a refused number, and adds no second `Disconnected`
entry. `record_state_transition_` appends to the history with no test (`fsm_visit_history_[fsm_visit_write_idx_++ %
16] = new_state;`), so any `Disconnected` write made while the state is already `Disconnected` adds an entry. INV-F
says that is the state whenever the manager is frozen.

- **`check_inbound`.** In `consume_rejected_seqnum_`, any result other than `store_seqnum_overflow` falls to
  `co_return fixpp::core::expected_t<void>{};` (not consumed). So `session_already_closed`, the drained mutex's code,
  costs nothing there. Its other callers run it in their frame's entry turn (`on_inbound_active_`) or in a
  Logon arm, which is the one pump frame (INV-P) with no bound before Active.
- **`assign_outbound`.**
  - Success is not available: a number returned is a number stamped, passed to `toAdmin` and stored.
  - So the refusal is `session_already_closed`. Most failure branches are `record_state_transition_(fsm_state::
    Disconnected); co_return std::unexpected(assign_r.error());`, which would add a second entry.
  - Hence RR-5 item 6, which the manager cannot meet alone: every `Disconnected` write on a path that a refused
    `assign_outbound` returns into becomes conditional, a caller-side edit, by the rule `disconnect_unless_superseded_` and `run_logout_phase1`'s exits already follow ("the
    write is made only when the state is not that already").
  - Already compliant: `inbound_reject_best_effort_leaf_` (`if (assign_r) {`) and `honor_peer_next_expected_`
    (`if (assign_r) {`), which skip.
  - `send_impl`'s `disconnect_required = is_persistent_retain_fatal(assign_r.error());` is not reached while frozen
    (INV-F: `Session::send` refuses first).
- **`set_next_inbound`.** `apply_inbound_sequence_reset`'s failure branch is `if (!sr) {
  record_state_transition_(fsm_state::Disconnected); co_return std::unexpected(sr.error()); }`. On the base that
  write is unconditional, so an error return would add a second entry and end the frame; that is why the refusal is
  success with no write. After 104, T050 makes that write conditional (RR-5 item 6, N-2), so the branch's
  post-drain path (`close()`'s `seqnum_mgr_.drain()`) adds no entry. A success with no write lets the function continue to its awaiting-resend exit
  and `note_inbound_advance_` (neither a counter write) and return success. That is the consult's sub-choice 2,
  taken.
- **`set_next_outbound`.**
  - The reconcile discards the result (`(void)co_await`), so either return is equivalent there.
  - Success with no write is chosen, so that both setters refuse alike.
  - The unit's own `if (r.manager)` chain needs success, but its calls precede the freeze.
- **`take_back_outbound`.** It returns nothing, so the refusal is a no-op. It has no observable effect (RR-5
  item 7):
  - after a unit, `next_outbound_ > k` is false for every k ≥ 1;
  - after a close-out, no frame reaches `store_then_emit`'s take-back with a number assigned in an earlier turn.
  T013 reads that per emitter: `assign_outbound`, `fire_to_admin_` and `store_then_emit`'s boundary with no
  suspension between them.
- **`hydrate`.** It is reachable only by a direct caller's overlap with `open()`'s emit, as read above. There,
  `ensure_hydrated_`'s failure write is
  `disconnect_unless_superseded_(arm)`, which is already conditional.
- **The frame's error, on the pump.** `run_read_pump` delivers with `auto deliver_r = co_await
  session.on_inbound_frame(frame.bytes());`, and on a failure runs `co_await stop_pump(); co_return;`. The session
  is already `Disconnected`, so this changes nothing observable beyond the pump's own exit, which the Disconnected
  row's drain would reach anyway.

#### No `toAdmin` for a refused number: the per-site reading

The rule holds by order where `assign_outbound` precedes `fire_to_admin_`. Where the callback comes first
(`fire_to_admin_`, or `toApp` for the BusinessMessageReject and an application send), the emitter must not be able
to meet a frozen manager:
- `inbound_logout_leaf_`: `if (logout_in_flight_ || fsm_state_ != fsm_state::Active) {` returns before its peek
  and its `fire_to_admin_`.
- `run_liveness_loop`: "Check if we're still Active after the sleep." Behind a resend answer it waits, then
  `continue`s to its top, which sleeps and tests the state again. `fire_to_admin_` follows the test with no
  suspension between (RR-4).
- `run_logout_phase1`: below.
- `inbound_resend_request_leaf_` and `inbound_heartbeat_reply_leaf_`: reached in their frame's entry turn.
  - The first comes from `on_inbound_active_too_high_`, after `reconnect_fsm_.enter_awaiting_resend`, whose body
    does not suspend.
  - The second is `on_inbound_active_test_request_`'s first await.
  - Nothing suspends between `check_inbound` and either leaf, so both run before any reset point that the frame can
    meet.
- `inbound_business_reject_leaf_` ("route 35=j through toApp before assign_outbound"): its callback is `toApp`. It
  is reached from `on_inbound_active_from_app_failed_` after `fromApp` returned. T013 reads that no suspension falls
  between the frame's `check_inbound` and the leaf.
- `send_impl` (`toApp` before its number, so "a vetoed send does NOT consume a seqnum"): `Session::send` refuses
  on the state at its entry and after every replay-gate wait (INV-F).
- `refuse_logon_with_logout_` and `honor_peer_next_expected_`: the Logon arms only (INV-F).

T013 re-reads each of these, and any other callback-first emitter the recipe in RR-5 item 6 finds. A site reachable
while frozen goes to the orchestrator.

#### Phase 1 after a receipt reset (sub-ruling 4)

Read, not run:
- **A `close(graceful)` started after the reset point does not run phase 1.**
  - Its test is `if (!close_escalated_ && (fsm_state_ == fsm_state::Active || fsm_state_ ==
    fsm_state::LogonReceived))`.
  - RR-3 step 2 records `Disconnected` before the reset point.
  - So no Logout is built, no `toAdmin` runs and no `assign_outbound` is called.
- **A `close(graceful)` whose phase 1 passed that test before the receipt** reaches its `assign_outbound` by one of
  two paths:
  - **Through the replay-gate wait.** The Disconnected edge wakes the gate (`record_state_transition_`'s
    `replay_gate_.wake_all()`). The loop then tests `fsm_state_ != fsm_state::Active && fsm_state_ !=
    fsm_state::LogonReceived` and takes its `gate_refused` exit (D-OUT-6's "not sent") before its stamp, its
    `toAdmin` and its number.
  - **With no gate wait.** From close()'s test to `logout_in_flight_ = true` and on to `assign_outbound`, phase 1 has
    no suspension: the only `co_await` between `logout_in_flight_ = true` and its number is `assign_outbound`
    itself, which grants inline. `operator||` starts it inline: asio's `co_spawn_entry_point` first awaits
    `co_spawn_dispatch`, which is `asio::dispatch`, and that runs inline on the session's executor. This is read at
    the pinned asio; re-derive with `grep -n co_spawn_dispatch <asio>/impl/co_spawn.hpp`, as INV-T's inline-start
    reading is. So phase 1 numbers its Logout in close()'s turn, before any reset point.
- If T013 finds a turn between them, the freeze makes that Logout the "not sent" exit at `assign_outbound` (its
  failure branch is already conditional). But its `toAdmin` would have run, so that reading goes to the
  orchestrator.
- **Outcome row:** contract O22. No cell: R-3, when drivable, runs the first half.

#### Records

- **B-104-8, new:** the freeze, as operators see it. It is a row of its own rather than a part of B-104-1, because
  it holds on every route of a received Logout's reset point, and for `close()`'s close-out, which is B-104-2's.
- **B-104-7, widened:** `SeqnumManager`'s layout in the installed header `seqnum_manager.hpp` (data-model "B&L
  delta").
- No C-ABI change: the C ABI exposes `Session` only through an opaque handle, and `SeqnumManager` not at all
  (re-derive: `git grep -n SeqnumManager -- include/fix src/capi bindings`, no hit).

## Spec errata (applied to spec.md, orchestrator ruling of 2026-11-17)

- **E-1 (FR-001, FR-009).** "103's reset unit" reads "093's 141=Y reset unit (`run_reset_unit_`, 093 contract
  C-6), which 103 left unchanged". That is the unit `close()` waits for.
- **E-2 (FR-012, User Story 3).** "with no PossDupFlag(43)=Y" reads "whatever its PossDupFlag(43)" for a frame in
  a Logon arm (D6). FIX-SL §4.3.11 has no PossDup exception for the Logon, and QFGo sends the Logout (DQ-5).
- **E-3 (FR-001, Edge Case "A too-high Logout").** For a too-high Logout (B-103-7), "at the receipt" reads "at
  its acknowledgement, the point where fixpp processes it". One left unacknowledged makes no reset under rol
  (RU-1; contract O18; DQ-12).
- **E-4 (FR-004).** "024's C5.1 (a single store reset)" reads "C5.1 (at most one store reset per teardown)". With
  both knobs, a received Logout makes one reset at its reset point and one at teardown (D2).

No erratum E-5: ruling O-3 chose D9's sequence (a), which meets FR-017 as written.

## Departures from QuickFIX

The one list is spec.md "Departures from QuickFIX" (FR-018), rows DQ-1 to DQ-16 and DQ-12a. This file does not keep a copy;
each decision above names the row it produces.

## OPEN

No open decision. O-1, O-2 and O-3 are ruled (below, "Orchestrator rulings (2026-11-17)"); RU-1 to RU-3 and N-8
are ruled (spec Clarifications, "Gate A round 1").

Phase-0 checks whose outcome can return a question to the orchestrator:
- **R1's store-order condition** (orchestrator ruling: stays a phase-0 check). If a store can apply a later
  `reset_to` before an earlier `store`, implementation stops and the decision returns to the orchestrator; the
  fallback below is a proposal, not a plan.
- **R-1r's route.** If no Active sub-arm writes after a suspension that follows its `check_inbound`, R-1r drives
  RR-4's boundary through the test seam, and the reading is recorded.
- **D8 step 6's census leads**, each settled in the census with a disposition.
- **The L-104-3 follow-up issue** (the refusals that still answer before the identity gates), filed and labelled
  in phase 9.

## Risks

- **R1 — the unit's manager set, and the store order behind RR-4.** Round 0's premise here ("the liveness loop's
  emits wait at the gate") was false: the loop reads neither `logout_in_flight_` nor the gate except for a resend
  answer (Opus N-2). The facts that hold, read from source:
  - **The manager grants inline.** `async_lock_op`'s fast path takes the mutex and runs the handler inside the
    initiation (`include/fixpp/core/sync/async_mutex.hpp`); no `SeqnumManager` method suspends while it holds the
    mutex (`src/session/seqnum_manager.cpp`); all session work is serialised on the session strand, or on an
    `already_serialized_executor` under `direct_executor` (`session_config.hpp` `threading_mode`). So a waiter
    exists only after `seqnum_mgr_.drain()`, which is `close()`'s last step, after the teardown predicate. 093 C-6
    step 3, B-093-11 and `brain/components/session.md` state the same condition.
  - **Cancellation cannot fail the manager set.** This rests on contract RR-2 item 3's reset of the thread's
    cancellation state at the start of the reset point's turn, not on the Engine alone. After the arm's first lock,
    `FIXPP_DETAIL_CO_AWAIT_LOCK` part 4 leaves the filter terminal-only, so a direct caller's `terminal` or `all`
    emission passes it. `Engine::stop()` emits only `total`, and the Engine calls `on_inbound_frame` from the pump
    with no `||` around it. The reset drops whatever was recorded before the turn, and nothing inside the turn can
    record one (cell R-1x). Route L takes no suspension before its unit (read). Route S takes none by its design:
    its guards are synchronous, and its mTLS gate is too in either arm (`compid_authorization_policy.authorize`
    returns a value; the no-identity arm calls nothing).
  - **The store-order condition.** RR-4's first case needs every store call issued before the unit's `reset_to` to
    be applied before it. "Issued" is the order in which the session calls the store; "applied" is the order in
    which the calls' effects land on the store's state, under its writer lock, whatever order their completions
    reach the session in. A user-supplied `MessageStore` is inside the condition as far as the interface's own
    requirements reach (`include/fixpp/session/message_store.hpp`: its methods take the per-instance FIFO
    `async_mutex` on entry, I-01, and the ORDER requirement on `store()`); phase 0 reads the shipped stores and the
    test double against them, and does not certify a user store. HookedStore's holds are consistent with it: a held operation completes its inner
    call and then holds its return to the session ("The operation whose hook fires completes, then does not
    return"), so a call R-1r or R-1l holds is applied in issue order. Phase 0 reads it for `MemoryStore` (a post,
    then its FIFO `async_mutex`), `FileStore` (its offloaded jobs) and HookedStore, with
    `git grep -n "async_lock\|asio::post\|offload" -- include/fixpp/session/*store*.hpp src/session/*store*.cpp`.
    If a store can apply a later `reset_to` before an earlier `store`, implementation stops and the decision
    returns to the orchestrator (ruling of 2026-11-17). The proposal to put to it: the unit waits for the
    session's in-flight outbound store calls before its `reset_to`.
  - Re-derive the takers: `git grep -n "seqnum_mgr_\.\(assign_outbound\|check_inbound\|set_next\|reset_to_one\)" src/session`.
- **R2 — `close()` now waits for a Logon-arm unit with no knob set, and for a `pending` receipt** (D1 item 4). It
  is bounded by `logon_timeout_ms`. The 093 cells around `close()` and the unit (`InboundFrameDispositionsOd29.*`,
  `engine_reset_unit_stop_test.cpp`) are in the measured census.
  - **The poll changes how a test may pump `close()`.** While it polls, `close()` keeps a handler ready on the
    io_context. So an existing cell that holds a store call or a unit while `close()` runs, and pumps with an
    unbounded `ioc.poll()`, `ioc.run()` or a `run_one()` loop with no bound, now hangs where it used to settle.
    The measured census would see that only as a ctest timeout.
  - Recipe, in phase 0's census: in every test file that holds a store call or `reset_to` while `close()` runs
    (`git grep -l -e HookedStore -e hold_until_close_reset -e reset_to_mode -- tests`), list the pumps with
    `git grep -n -e '\.poll()' -e '\.run()' -e 'run_one()' -- <those files>`, and read the helpers those files
    pump through (a rig or settle helper), since the pump may live there. The grep reports hits elsewhere in
    `tests/`, so a miss in those files is a reading, not a broken instrument. Give each pump a disposition in the
    evidence file: bounded already, or re-pinned to a bounded pump (`run_for`, `pump_until_ready`).
- **R3 — D3.3a changes 103's and 027's resend-answer arithmetic** for an explicit end at or above next outbound.
  The supersession census (D8) finds the cells and texts pinning `requested_end + 1`. The two-value end model's
  comment in `replay_outbound_range_` is amended to name 104.
- **R4 — `on_inbound_logout_sent_` gains a suspension.** A frame delivered in the same read after the Logout
  waits for the unit, as the read pump serialises its own frames. On resumption the arm for the current state
  handles it: Disconnected, so it is drained. Route B's bound runs beside the pump, which RR-3 (Disconnected
  first) and RR-4's boundaries cover.

## Orchestrator rulings (2026-11-17)

These were made under the owner's grant of the same evening: "run autonomously through all steps … Stop if you hit a
blocker". Each is listed again in the PR body for the owner's review at Gate B sign-off. Gate A round 1's rulings
(RU-1 to RU-3, and the N-8 decision) are in spec.md Clarifications, "Session 2026-11-17 (Gate A round 1 …)".

- **O-1, resolved by source and not a blocker.** The sequence below holds for a QuickFIX C++ v1.16.0 acceptor whose
  `toAdmin` injects 141=Y:
  1. It queues the initiator's Logon at N_out: `quickfix-cpp@v1.16.0:src/C++/Session.cpp` `nextLogon`, then
     `doTargetTooHigh` → `m_state.queue`.
  2. Its `nextQueued` consumes a queued Logon by incrementing past it. The initiator's own frame N_out then reads
     as too low.
  3. The queue is cleared only in `disconnect()`.

  So a QuickFIX C++ initiator's second Logon (141=Y, 34=1) does not clear it either. Every QuickFIX initiator meets the
  same ending against such a counterparty, and so does fixpp under FR-008. FR-008's ruling therefore has no consequence
  that a second Logon would avoid. The behaviour is disclosed as **L-104-7** (data-model "B&L delta"; spec DQ-16),
  marked source-derived and not traced on a live peer.
- **O-2: QuickFIX/J and QuickFIX/Go's outcome** (not consumed, Active, ResendRequest from 1), as D4 decides. It is
  consistent with the owner's FR-008 ruling, which follows the same two engines.
- **O-3: sequence (a).** A `workflow_dispatch` run at the head before labelling, then the re-record, with the labelled
  run as acceptance. No erratum E-5 is made.
- **E-1 to E-4: applied to spec.md.** E-2 widens the Logon-arm too-low Logout to 43=Y, which is a departure from QuickFIX
  C++ and QuickFIX/J (DQ-5).
- **L-104-3: superseded in Gate A round 1** (clarification N-8). Round 0 accepted the too-low Logout before the mTLS
  CompID-authorisation gate on the ground that "sequence numbers are not secret". FIX-TC 1S(c) asks for no message
  at all to an unauthenticated identity, so the too-low Logout now comes after the identity gates, and L-104-3 now
  discloses only the pre-existing refusals that still answer before them.
- **D2: accepted.** Two store resets when both knobs are set, as in QuickFIX C++ and QuickFIX/J.
- **D3.3a: accepted, owner-visible scope addition (B-104-5).** QuickFIX C++ clamps an EndSeqNo at or above its next
  outbound number to that number minus 1 (`quickfix-cpp@v1.16.0:src/C++/Session.cpp`, `nextResendRequest`:
  `endSeqNo >= getExpectedSenderNum()` → `endSeqNo = getExpectedSenderNum() - 1`). fixpp's `requested_end + 1` is a
  departure from it today, so the clamp restores QuickFIX's behaviour. It supersedes 027 C3's explicit-end case by
  name (D3.3a; contract "Superseded by name").
- **The moved `close()` wait: accepted** (round 0's D1 item 6, now D1 item 4 and contract RR-2 item 4; B-104-2),
  widened in round 1 to a `pending` receipt (RU-1). It is bounded by `logon_timeout_ms`. Its shape became a poll
  by post in Gate A round 2 (N-1); that is a construction under design ruling 2, not a new ruling.
