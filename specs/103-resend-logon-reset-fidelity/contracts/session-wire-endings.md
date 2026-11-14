# Contract: session wire endings changed by 103 (one condition, one observable ending)

N = NextNumIn, S = a too-high Logout's MsgSeqNum, T = `logout_disconnect_timeout_ms`, HB = HeartBtInt.
Decisions: research D1–D9.

| # | Condition (session state) | Observable ending (wire + state) | Issue / FR |
|---|---|---|---|
| E1 | Default policy, initiator, stored next-out N_out > 1, no reset setting | Logon 34=N_out, no 141; an in-sequence ack → Active | #682 FR-001, FR-005 |
| E2 | `bilateral_strict` initiator, any stored numbers | durable reset to {1,1}; Logon 141=Y, 34=1; ack 141=Y, 34=1 → Active; next message 34=2 | #682 FR-002, FR-003 |
| E3 | `bilateral_strict` acceptor, Logon without 141=Y (in or out of sequence) | Logout 58=`ResetSeqNumFlag(141)=Y required` (none if the CompIDs overflow the buffer); Disconnected; N not advanced; arm returns `session_seqnum_reset_mismatch` | #682 FR-004 |
| E4 | Any reconnect in place (a new transport installed, from `Disconnected` or from Active) | new connection starts with no resend outstanding and no pending-Logout mark; the old connection's bound sends nothing on it; first too-high frame → ResendRequest from N | #578 FR-006, FR-011 |
| E5 | ResendRequest with 7=0 | answer byte-identical to 7=1's (modulo 52); no frame with 34=0 | #580 FR-012 |
| E6 | ResendRequest without tag 7, in sequence | Reject 373=1, 371=7, 58=`Required tag missing`; no replay; N+1 persisted | #580 FR-013 |
| E7 | ResendRequest with 34 > N, no resend outstanding | the requested range is answered; then one ResendRequest from N; N unchanged | #577 FR-008, FR-009 |
| E8 | ResendRequest with 34 > N, resend outstanding | the requested range is answered; no second ResendRequest | #577 FR-008, FR-009 |
| E9 | Active, Logout with 34 = S > N, T > 0, a clock | ResendRequest (if none outstanding); one Logout acknowledgement when N passes S or at T (none in FR-010's cases 1–5: CompID overflow, `toAdmin` throw, assignment or store failure, the transport ending first, a `close(graceful)` grace that runs out); then Disconnected, transport open until the peer's EOF / `close()` / stop; `send` refused while pending; TestRequests and ResendRequests answered while pending | #577 FR-010, FR-011 |
| E10 | Active, Logout with 34 > N, T = 0 or no clock | Logout acknowledgement at once (none in FR-010's cases 1–3); no ResendRequest; Disconnected | #577 FR-010 |
| E11 | Any outbound emitter outside the inbound chain while a resend answer runs | its frame is written after the answer's last frame and numbered then; a frame numbered before the answer started is written before its first frame | #588 FR-014–FR-016 |
| E12 | Resend outstanding, N not advanced for 2 × HB, a frame that does not advance N arrives (not a garbled frame the Framer disregards, nor a faulty frame disposed of before any handler reads it: FIX-SL §4.5.2; they do not refresh liveness, so such a peer ends at the TestRequest grace check) | Logout 58=`ResendRequest unanswered, expecting MsgSeqNum <N>` (none if the CompIDs overflow the Logout buffer, B-537-2); Disconnected | #581 FR-017 |
| E13 | Same, but the frame advances N | processed as today; deadline restarts | #581 FR-017 |
| E14 | Active, sequence validation on, a frame with 34 < N and no 43=Y that reaches the sequence check (Heartbeat included) | Logout 58=`MsgSeqNum too low, expecting <N> but received <M>` (none if the CompIDs overflow the Logout buffer or `toAdmin` throws); Disconnected; N not advanced. Not this row: a frame that fails dictionary validation is Rejected at the validate gate and the session stays Active (QuickFIX C++ validates before `verify`; owner ruling); a frame failing the CompID or SendingTime check takes that check's ending; an unparseable or garbled frame keeps its 093 disposition | #581 FR-018 |

**At most one Logout** across E9, E12, E14, an in-sequence peer Logout and `close(graceful)`, and **exactly one
except FR-010's enumerated cases** (CompID overflow; `toAdmin` throw; assignment or store failure; the transport
ending first: the peer's EOF, `close(terminal)`, `Engine::stop()`; a `close(graceful)` grace that runs out first,
L-103-6), each with its B&L row: research D4.6's claim table. The bound claims after its gate wait; Arm B claims
`pending → none`; `close(graceful)` that finds the acknowledgement claimed joins it inside its grace before it
records Disconnected and closes the transport.

**State scope**: E9, E10 and E14 are Active only. LogonReceived is entered and left inside the Logon's own inbound
chain and inbound frames are serialised, so no other frame is processed there (research D4.2).

**Unchanged and disclosed**: HB ≤ 0 (no gap deadline); `validate_sequence_numbers` off (too-high ResendRequest/Logout
delivered to `fromAdmin` only; too-low frames dropped as today); a 141=Y Logon received with 34 ≠ 1; a too-low frame
in NotConnected / LogonSent (L-103-7); a peer-spontaneous 141=Y ack at 34=1 with a hydrated N > 1 (L-103-2).
