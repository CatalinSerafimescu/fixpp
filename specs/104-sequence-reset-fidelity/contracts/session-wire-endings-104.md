# Contract: session wire endings changed by 104 (one condition, one observable ending)

Decisions: [research.md](../research.md) D1–D7. Notation:
- N = NextNumIn before the frame, M = the frame's MsgSeqNum(34), N_out = fixpp's next outbound number before the
  frame.
- T = `logout_disconnect_timeout_ms`.
- "rol" = `reset_on_logout`, "rod" = `reset_on_disconnect`.
- "reset" = the 141=Y reset unit `run_reset_unit_(1, 1)` (093 contract C-6): one `MessageStore::reset_to`
  operation, after which both numbers are 1 in the manager and the store. The teardown's reset is
  `reset_seqnums_to_one_durable` (`MessageStore::reset`); a cell tells the two apart by which store call it sees.
- "the marker" = the private member `logout_seen_`, whose name is kept and whose type and contract 104 change
  (data-model "`logout_seen_`"): `none`, `pending` (a receipt arm is between its receipt and its reset point),
  `reached` (this connection's receipt reached its reset point).

## Part 1 — the reset under `reset_on_logout` (supersedes 024 C3.1 by name)

**RT-1 (the trigger; supersedes 024 `contracts/reset-knobs.md` C3.1, "either direction").**
- With rol set, the sequence numbers are reset to 1 when, and only when, a Logout **received** from the peer is
  processed as far as its reset point (RR-1). The receipt arms are:
  - **A** — `on_inbound_active_logout_`: a request in Active, or a Logout in LogonReceived;
  - **L** — `on_inbound_logout_sent_`: the response to fixpp's own Logout, in LogoutSent;
  - **B** — `acknowledge_peer_logout_`: a too-high Logout (B-103-7), at its immediate acknowledgement or the
    bound's (`run_peer_logout_bound_`);
  - **S** — `on_inbound_logon_sent_`: a Logout received in LogonSent, the peer refusing fixpp's Logon (RU-2, new).
- A Logout received in NotConnected (acceptor, before any Logon) is not a receipt arm and never resets (RU-2;
  QuickFIX C++ `validLogonState` is false there).
- A Logout fixpp **sends** never triggers it, whether answered or not: `close(graceful)`'s phase 1, every
  error-path Logout and 104's too-low Logout.

**RR-1 (the reset point; RU-1).** Each receipt arm has one reset point. The reset is made if and only if the arm
reaches it. Processing that ends before it does not reset under rol: a validation failure, a `fromAdmin` throw or
reject, a late parse failure, a failed inbound persist, a refused reply (its `toAdmin` throw, its
`assign_outbound` failure, a cancelled store call), a gate refusal, a too-high Logout's bound that ends
unacknowledged, or a receipt still short of its reset point when `close()` could not wait for it (RR-2 item 4). Only rod then resets, at teardown. There is no teardown backstop and no reconnect-Logon backstop
under rol.

| Arm | Reset point: reached when |
|---|---|
| A | the reply Logout has been attempted (Active only: `inbound_logout_leaf_` returned success, which includes a Logout that did not fit, whose store call failed, or whose write failed), the `sequence_numbers_reset` event, `fromAdmin` returned without a throw, a reject or a late parse failure, and `persist_inbound_advance_` succeeded |
| L | the 35=5 frame passed the arm's garble and fault guards |
| B | its reply has been attempted (as A's reply) and the event emitted |
| S | the 35=5 frame passed the arm's guards ahead of `interpret_logon` (garble, `logon_arm_superseded_`, fault, 099's establishment refusal; the validate gate exempts 35=5), and on an mTLS session the live identity is authorised for the CompID (research D1.2) |

**RR-2 (one owner; RU-1's construction, as ruled 2026-11-17).** Once a reset point is reached the reset is
completed, and nothing that races it can lose it or run it twice. A receipt that has not reached its reset point
is never reset under rol, by anyone.
1. With rol set, the marker is set to `pending` at the receipt (A, L and S at the arm's entry; B at
   `on_inbound_active_too_high_logout_`, held by the bound's frame until its reset point or its exit). With rol
   off it stays `none`: nothing is reset, waited for or refused.
2. It leaves `pending` exactly once: to `reached` at the reset point, or by the teardown (item 4); or to `none` at
   any exit before the reset point. The clear on exit is made by a frame-local owner that clears only a `pending`
   marker, so an exception that unwinds the arm clears it too. An arm clears it before any `close()` it calls
   itself (`close_on_late_parse_failure_`; the throw paths), so `close()` never waits for its own caller.
3. **The reset point is one strand turn.** The arm's `pending` → `reached` and the start of its unit are made with
   no suspension between them: the transition is written immediately before `run_reset_unit_`, whose manager calls
   grant inline (093 C-6 step 3; research R1) and which sets `reset_unit_in_flight_` before its first suspension
   (with no store the unit has no suspension at all). So no other coroutine on the strand observes a `reached`
   receipt whose unit has not started.
4. `close()` (any mode, and the close `Engine::stop()` drives) waits, after its joins and before its teardown
   predicate, while the marker is `pending` or `reset_unit_in_flight_` is set. The wait is 093's: bounded by
   `logon_timeout_ms`, it needs a clock, and on expiry it emits `session_event_close_reset_wait_expired`.
   - **A receipt still `pending` after the wait** (it expired, or there was no clock to wait with) never reached
     its reset point. It is processing that ended before the reset point (RR-1): **no reset under rol**. The
     teardown sets the marker to `reached`, so that when the arm resumes it runs no unit (item 5) and RR-4's
     boundaries refuse its writes. Under rod the teardown resets as it always does.
   - **A receipt that is `reached` but whose unit has not started** is the one case the teardown would reset
     itself, once. By item 3 the teardown never meets it: the case has no input by construction. Cell R-1u's
     mutant opens that window (a suspension between `reached` and the unit) and goes RED, which is the evidence
     that item 3 is load-bearing.
   - The teardown predicate reads `!teardown_reset_done_ && (rod || (rol && <reached, unit not started>))`. Its
     second term is unsatisfiable by item 3, so no code tests it: item 3 is its implementation, and a change that
     splits that turn must add the term (and a way to observe "unit not started"; the three-valued marker has none)
     before R-1u can go GREEN again.
5. At its reset point an arm finds the marker `pending`, sets it to `reached` and runs the unit (item 3); or finds
   it `reached`, because the teardown closed the receipt out (item 4), and runs no unit. From the moment the marker
   is `reached`, RR-4's boundaries refuse the arm's later writes, so nothing it does after `close()` moves a number.
6. `install_reconnected_transport` sets `none`. A received Logout never resets a later connection.

**RR-3 (order at the reset point; RU-3).** In every receipt arm:
1. the reply Logout, if one is owed (A in Active, B);
2. record `Disconnected` (where the arm has not), which stops every emitter from numbering in the reset space
   (RR-4) and drains further inbound frames;
3. the reset point, in one strand turn (RR-2 item 3): the marker `pending` → `reached` and the unit; then
   `reset_cancellation_state(enable_total_cancellation{})` as `on_inbound_logon_sent_` does after its unit; a
   manager or store error is RT-4's;
4. L only: `logout_confirmed_` and phase 1's wake;
5. the transport closes as today (the pump's EOF, `close()`, or `Engine::stop()`).

A `close()` started inside the `onLogout` that step 2 fires finds the marker `pending`. It waits one post first
(B-563-2), so the arm reaches step 3 before it, and the wait then covers the unit. Where that post does not move
the close off the callback's stack (B-563-2's R-7 setup), the close's own wait still covers the receipt, and with
no clock the receipt is not reset (item 4's first case).

`onLogout` fires on the edge into Disconnected at step 2, so it observes the pre-reset numbers (A, B; L's
`onLogout` fired earlier, on phase 1's Active → LogoutSent edge). This is a listed departure (spec "Departures
from QuickFIX"): QuickFIX C++ and QuickFIX/J fire `onLogout` in `disconnect()`, after `m_state.reset`. Nothing on
the wire differs, because QuickFIX resets and then disconnects synchronously and fixpp sends nothing after the
reply. This closes the window in which the numbers were (1, 1) while the session was still Active; it does not
widen 103's FR-021.

**RR-4 (what stops at step 2, read from source).**
- **The liveness loop** (`run_liveness_loop`) tests `fsm_state_ != Active` after each sleep, after the Heartbeat's
  emit and before and after the TestRequest's grace sleep. Between the post-sleep test and `assign_outbound` it
  suspends only on the replay gate's wait, after which it re-tests the state. So it numbers nothing after step 2.
- **`Session::send`** refuses on `fsm_state_ != Active`, at entry and after every replay-gate wait.
- **The resend answer** (`replay_outbound_range_`) returns `dispatch_aborted` on `Disconnected` after its wait for
  the shared holds. The bound does not claim while a walk holds the gate exclusively.
- **The inbound pump** delivers a frame after step 2 to the Disconnected row, which drains it.
- **Not stopped by the state:** an emitter already past its state test holds a pre-reset number. Two cases:
  - its store call was issued in the same strand turn as its `assign_outbound`, before the unit's `reset_to`, and
    D-OUT-10 in `store_then_emit` refuses its transmit once the manager is lowered (research R1 states the
    store-order condition that makes this hold, and its phase-0 check);
  - on route B, a pump-delivered Active-arm frame whose processing was already in flight when the detached bound
    recorded `Disconnected`, and whose later leaves or inbound persist are issued after the unit; and on any
    route, the receipt arm's own later writes after `close()` closed the receipt out (RR-2 item 4).
    **By construction:** `store_then_emit` (before its store call) and `persist_inbound_advance_` (before its
    store call) refuse while the marker is `reached`. `store_then_emit` takes the number back, as D-OUT-1 does
    for a cancelled store call, and returns `dispatch_aborted`; `persist_inbound_advance_` returns success with no
    write. Each boundary is one byte test. The end state equals QuickFIX's, whose reset after the Logout discards
    every earlier increment. The receipt unit and the teardown's reset do not pass through either boundary.

**RT-4 (store failure at the reset).** A manager or store failure of the receipt unit is logged. The arm's ending
is unchanged (Disconnected, and the return value it has without the failure). There is no retry under rol; under
rod the teardown reset still runs. On a refusing persistent store the stored numbers stay pre-reset (L-104-2).

**RT-5 (`close()`).** RR-2 item 3's wait replaces the wait inside the predicate block, whatever the knobs, so a
`close()` that finds a Logon-arm 141=Y unit in flight now waits for it even with no knob set (B-104-2). 024 C5.1
is kept as written: at most one store reset per teardown (`teardown_reset_done_`). With rol and rod both set, a
received Logout makes two resets, one at the reset point and one at teardown; both numbers end at 1 (D2).

**RT-6 (unchanged).** rod (024 C4.1, C4.2); the not-sent and cut Logouts (no reset).

### Outcomes (RU-1): every receipt arm × every way its processing can end

"no reset" means under rol alone; rod still resets at teardown. Engine columns are read from source (research D1).
A cell id is given where the row can be driven; its mutant is in research D10.

| # | Condition | A (Active / LogonReceived) | L (LogoutSent) | B (too-high ack) | S (LogonSent) | QuickFIX | Cell |
|---|---|---|---|---|---|---|---|
| O1 | normal | reply, persist, Disconnected, reset at the arm; wire: the reply only | Disconnected, reset at the arm, then phase 1 wakes; wire: nothing | reply, Disconnected, reset at the arm | Disconnected, reset at the arm; wire: nothing (QuickFIX replies) | reset at receipt, all three | R-1c, R-1d, R-1e, R-1o |
| O2 | `fromAdmin` throws | reply already sent; Disconnected; no reset | n/a (no `fromAdmin`) | n/a | n/a | no reset (QFC, QFJ: `verify` → `fromCallback` throws before the reply) | R-1g |
| O3 | `fromAdmin` rejects | reply, Reject, Disconnected; no reset | n/a | n/a | n/a | no reset (QFC, QFJ: the reject exception leaves `nextLogout`; QFGo: `processReject`) | R-1g |
| O4 | late parse failure | marker cleared, then `close(terminal)`; no reset | n/a | n/a | n/a | no reset (not a processed Logout) | R-1g |
| O5 | reply does not fit 256 bytes | no reply; reset point reached; reset | n/a | as A | n/a | n/a (no fixed buffer) | R-1n |
| O6 | reply `toAdmin` throws | leaf records Disconnected, `app_callback_threw`; no reset | n/a | as A | n/a | QFC: no reset (propagates out of `sendRaw`); QFJ: resets (`sendRaw` catches `Throwable` from `toAdmin`, then sends); QFGo: no throw path | R-1n |
| O7 | reply `assign_outbound` fails | leaf records Disconnected, the error; no reset | n/a | as A | n/a | n/a | R-1n |
| O8 | reply store fails (persistent) | not transmitted; reset point reached; reset | n/a | as A | n/a | reset (QFC, QFJ catch the store's `IOException` in `sendRaw` and continue) | R-1n |
| O9 | reply store call cancelled | `operation_aborted` unwinds the arm; marker cleared; no reset | n/a | as A | n/a | n/a | — (unreachable after the first lock, research R1; the owner covers it) |
| O10 | reply write fails | stored, not on the wire; reset | n/a | as A | n/a | reset (QFC `send` returns false; `nextLogout` continues) | R-1n |
| O11 | inbound persist fails | Disconnected, the error; no reset | n/a | n/a | n/a | no reset (QFC, QFJ: `incrNextTargetMsgSeqNum` throws `IOException`) | R-1n |
| O12 | `close()` during, before the reset point, with a clock | `close()` waits; the arm reaches its reset point and resets once, or exits and does not; `close()` returns after | as A | the bound is claimed: no reset (RR-1) unless already `acking`, then as A | `logon_arm_superseded_`: the arm acts on nothing; no reset | n/a (QuickFIX serialises receipt and disconnect) | R-1k, R-1f |
| O13 | `close()` during, before the reset point, wait expired or no clock | no reset under rol (the receipt did not reach its reset point); the marker is closed out to `reached`; the arm, resuming, runs no unit and writes nothing | as A | as A | as O12 | n/a | R-1l |
| O13a | `reached`, unit not started, at the teardown | the teardown resets once; no input by construction (RR-2 item 3) | as A | as A | as A | n/a | R-1u (its mutant opens the window) |
| O14 | `close()` during the unit | `close()` waits for the unit (clock); without a clock it returns while the unit's store operation completes on the strand (093's no-clock case) | as A | as A | as A | n/a | R-1j |
| O15 | `Engine::stop()` during | as O12–O14 through the close it drives; its total emission can cancel only a store call before the arm's first lock (O9) | as A | as A | as O12 | n/a | R-1k (stop variant) |
| O16 | store reset fails | logged; Disconnected; no retry (RT-4) | as A | as A | as A | QFGo: logged, then `latentState` | R-1i |
| O17 | grace timer of `close(graceful)` expires during L's unit | n/a | phase 1 returns `session_logout_timeout`; the timer branch also force-closes the transport (`live->close()`) before phase 2; the wire is the same (nothing more is sent); `close()` waits for the unit | n/a | n/a | n/a | R-1j |
| O18 | bound ends unacknowledged (claim by `close()`, stop, another ending's Disconnected, reconnect drain, a Logout in flight) | n/a | n/a | marker cleared; no reset | n/a | QFC, QFJ: reset (they process a too-high Logout at receipt, `verify(logout, false, false)`) | R-1f |
| O19 | mTLS identity not authorised | n/a | n/a | n/a | no reset; Disconnected | n/a | R-1o |
| O20 | Logout in NotConnected | — | — | — | — | no reset (QFC `validLogonState` false) | R-1o |

## Part 2 — endings per arm and input

| # | Arm / condition | Input | Wire from fixpp | Numbers after | Store | State |
|---|---|---|---|---|---|---|
| W1 | LogoutSent (fixpp's graceful Logout sent), rol set | no peer Logout; T expires | nothing more | as after the Logout's emit | unchanged by any reset | Disconnected; `close()` ok |
| W2 | same | transport drops (peer EOF) during the wait | nothing more | as W1 | as W1 | Disconnected; `close()` ok |
| W3 | same | peer Logout, before T | nothing more | `(1, 1)` before phase 1 wakes | one `reset_to(1, 1)`, state Disconnected, the transport open, before `close()`'s phase 2 | Disconnected |
| W4 | Active, rol set | peer Logout request, M = N | Logout reply at 34 = N_out | pre-reset at `onLogout`; `(1, 1)` after the arm | inbound persist, then `reset_to(1, 1)` with the state Disconnected and the transport open | Disconnected |
| W5 | Active, rol set, T > 0, a clock | peer Logout with M > N | ResendRequest (as B-103-7); when N passes M or at T, the acknowledgement Logout | as W4, after the acknowledgement | `reset_to(1, 1)` after the acknowledgement, state Disconnected | Disconnected |
| W6 | W5 pending, then `close()`, stop, the peer's EOF or a reconnect drain | — | as B-103-7 for that route | not reset by rol (O18); rod resets at teardown | no `reset_to` | as that route |
| W7 | W3 or W4 with rol **and** rod | — | as W3 / W4 | `(1, 1)` | two resets: the arm's `reset_to`, then the teardown's `reset` | Disconnected |
| W8 | W3 or W4, receipt reset fails | — | as W3 / W4 | manager `(1, 1)`; store pre-reset when the store refused | logged | Disconnected; arm's return unchanged |
| W9 | standalone initiator: W3/W4 reset at receipt, then `drive_reconnect`, numbers advance, then `close()` with rol only | — | the new Logon at the numbers as they stand | not reset by the new connection's teardown | no further reset | closed |
| W10 | LogonSent, rol set | peer Logout | nothing (QuickFIX replies with a Logout) | `(1, 1)` | `reset_to(1, 1)` | Disconnected |
| W11 | W10, rol off; or NotConnected with any knob | peer Logout | nothing | unchanged | no reset | Disconnected |
| U1 | LogonSent, lenient or unilateral, fixpp's Logon without 141=Y, N ≥ 1 | ack 141=Y, M = 1 | no second Logon; the next frame fixpp sends is 34=1 | inbound next 2, outbound next 1 | `reset_to(2, 1)`; event `by_peer_request = true` | Active |
| U2 | same | ack 141=Y, M > 1 | no second Logon; on the peer's next frame (34 > 1), ResendRequest 7=1, 16=0 | inbound next 1, outbound next 1 | `reset_to(1, 1)` | Active |
| U3 | U1 or U2 with 789 enabled and present, 789 = X | X > 1 | Logout "NextExpectedMsgSeqNum too high, expecting 1 but received X" | as after the reset, plus the Logout | reset, then the Logout stored | Disconnected |
| U4 | `bilateral_strict` or a knob-asked reset (fixpp's Logon carried 141=Y) | any ack | unchanged (today's `check_inbound` against N = 1) | unchanged | unchanged | unchanged |
| U4m | U1 or U2 on a direct mTLS initiator whose live identity is not authorised for the CompID | — | nothing | unchanged (no unit) | no `reset_to` | Disconnected |
| U5 | after U1 (outbound next 1, nothing sent) | ResendRequest at 34=2, 7=1, 16=0 | one GapFill 34=1, 43=Y, 122=52, 123=Y, 36=1 | inbound next 3 | inbound persist | Active |
| U6 | Active, any history | ResendRequest whose EndSeqNo ≥ N_out (999999 on FIX.4.0/4.1 included) | as 16=0: the walk to N_out − 1, and an empty-range GapFill's 36 = N_out (never EndSeqNo + 1). With BeginSeqNo B > N_out the GapFill is (34=B, 36=N_out), its NewSeqNo below its MsgSeqNum, QFC's identical frame (`nextResendRequest`'s clamp, then `generateRetransmits`' tail `generateSequenceReset(B, next)`) | unchanged | as today | Active |
| T1 | LogonSent, ack without 141=Y, M < N (43 any) | — | one Logout, 58 = "MsgSeqNum too low, expecting N but received M", 34 = N_out, no 1409, no 789; `toAdmin` called once before its number is taken; on an mTLS session only after the identity is authorised (T8) | N unchanged | the Logout stored as outbound; nothing inbound persisted | Disconnected |
| T2 | NotConnected (acceptor), Logon without 141=Y, M < N, not strict, `reset_on_logon` off (43 any) | — | as T1, 34 = the hydrated next outbound; only after both identity gates pass (T8) | as T1 | as T1 | Disconnected |
| T3 | T1/T2, Logout over 256 bytes (long CompIDs) | — | none (the near-bound CompIDs still emit T1's Logout) | as T1 | nothing | Disconnected; success |
| T4 | T1/T2, `toAdmin` throws | — | none | as T1 | nothing | Disconnected; `app_callback_threw` |
| T5 | T1/T2, `assign_outbound` fails | — | none | as T1 | nothing | Disconnected; that error |
| T6 | T1/T2, the Logout's store or write fails | — | store failure: none; write failure: none on the wire, the Logout stored | as T1 | as the store left it | Disconnected; success |
| T7 | T1/T2 with rol set | — | as T1 | not reset by this Logout | no reset | Disconnected |
| T8 | T1/T2 on an mTLS session whose live identity is not authorised for the CompID; on the acceptor also a Logon whose credentials `authorize_logon` refuses | — | nothing (FIX-TC 1S(c): "Disconnect without sending a message") | as T1 | nothing | Disconnected, with the gate's own event |

**Precedence on one Logon frame** is each arm's code order (research D5), with one change (N-8): a too-low frame
runs the identity gates before its Logout. An earlier guard (validate gate, `interpret_logon`, posture,
SendingTime, `seq == 0`, and in NotConnected the strict refusal and the `reset_on_logon` reset) takes its own
ending. For a too-low frame the arm then runs the mTLS CompID-authorisation gate (both arms) and, on the acceptor,
`authorize_logon`, through the helpers that make today's checks, so the event, the one-shot `live_peer_id_`
consumption and the silent Disconnected are identical. Only then does it send the too-low Logout. The later
guards (1137, strict mismatch, the unit, 789) are not reached by a too-low frame.

**Unchanged and disclosed:**
- The too-low Logout leaves the transport open until EOF, `close()` or stop (B-103-10, L-103-5).
- The Logon arms' sequence check ignores `validate_sequence_numbers`.
- A strict acceptor's 141=Y Logon at 34 ≠ 1 (L-103-4).
- The acceptor twin of U1 is unreachable (research D7).
- The posture, strict and SendingTime refusals and the 1137 Reject still answer before the identity gates: a
  pre-existing departure from FIX-TC 1S(c) that 104 does not widen (L-104-3).

## Superseded by name (header-comment rule, issue #310)

- 024 `contracts/reset-knobs.md` **C3.1** ("either direction"): by RT-1 and RR-1. 024 **C5.1** is kept; its
  "single store reset" reads per teardown (erratum E-4).
- 027 `contracts/next-expected-msgseqnum.md` **C3**, the two-value end model's explicit-end case, and its required
  test "request `[10,20]`, store through 5": by U6 (B-104-5). The cell that pins `NewSeqNo=21`
  (`WalkExtraction.TwoValueEnd_ExplicitEndBeyondStore`) is re-pinned to `NewSeqNo=6`, QuickFIX C++'s identical
  GapFill(34=10, 36=6).
- 093 `contracts/inbound-frame-dispositions.md` **C-6**'s `close()` bullet ("`close()` waits only when it is about
  to issue its teardown reset"): by RR-2 item 3 and RT-5 (B-104-2). B-093-11 gains the same pointer.
