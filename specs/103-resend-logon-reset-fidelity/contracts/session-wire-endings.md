# Contract: session wire endings changed by 103 (one condition, one observable ending)

N = NextNumIn, S = a too-high Logout's MsgSeqNum, T = `logout_disconnect_timeout_ms`, HB = HeartBtInt.
Decisions: research D1–D9.

| # | Condition (session state) | Observable ending (wire + state) | Issue / FR |
|---|---|---|---|
| E1 | Default policy, initiator, stored next-out N_out > 1, no reset setting | Logon 34=N_out, no 141; an in-sequence ack → Active | #682 FR-001, FR-005 |
| E2 | `bilateral_strict` initiator, any stored numbers | durable reset to {1,1}; Logon 141=Y, 34=1; ack 141=Y, 34=1 → Active; next message 34=2 | #682 FR-002, FR-003 |
| E3 | `bilateral_strict` acceptor, Logon without 141=Y (in or out of sequence) | Logout 58=`ResetSeqNumFlag(141)=Y required` (none if the CompIDs overflow the buffer); Disconnected; N not advanced; arm returns `session_seqnum_reset_mismatch` | #682 FR-004 |
| E4 | Any reconnect in place | new connection starts with no resend outstanding and no pending-Logout mark; first too-high frame → ResendRequest from N | #578 FR-006, FR-011 |
| E5 | ResendRequest with 7=0 | answer byte-identical to 7=1's (modulo 52); no frame with 34=0 | #580 FR-012 |
| E6 | ResendRequest without tag 7, in sequence | Reject 373=1, 371=7, 58=`Required tag missing`; no replay; N+1 persisted | #580 FR-013 |
| E7 | ResendRequest with 34 > N, no resend outstanding | the requested range is answered; then one ResendRequest from N; N unchanged | #577 FR-008, FR-009 |
| E8 | ResendRequest with 34 > N, resend outstanding | the requested range is answered; no second ResendRequest | #577 FR-008, FR-009 |
| E9 | Logout with 34 = S > N, T > 0, a clock | ResendRequest (if none outstanding); exactly one Logout acknowledgement when N passes S or at T; then Disconnected, transport open until the peer's EOF / `close()` / stop; `send` refused meanwhile | #577 FR-010, FR-011 |
| E10 | Logout with 34 > N, T = 0 or no clock | Logout acknowledgement at once; no ResendRequest; Disconnected | #577 FR-010 |
| E11 | Any outbound emitter outside the inbound chain while a resend answer runs | its frame is written after the answer's last frame and numbered then; a frame numbered before the answer started is written before its first frame | #588 FR-014–FR-016 |
| E12 | Resend outstanding, N not advanced for 2 × HB, a frame that does not advance N arrives | Logout 58=`ResendRequest unanswered, expecting MsgSeqNum <N>`; Disconnected | #581 FR-017 |
| E13 | Same, but the frame advances N | processed as today; deadline restarts | #581 FR-017 |
| E14 | Frame with 34 < N, no 43=Y, sequence validation on, LogonReceived or Active (Heartbeat included) | Logout 58=`MsgSeqNum too low, expecting <N> but received <M>`; Disconnected; N not advanced | #581 FR-018 |

**Exactly one Logout** across E9, E12, E14, an in-sequence peer Logout and `close(graceful)`: research D4.6's claim
table (amended by pass 2: the bound claims after its gate wait; Arm B claims `pending → none`).

**Unchanged and disclosed**: HB ≤ 0 (no gap deadline); `validate_sequence_numbers` off (too-high ResendRequest/Logout
delivered to `fromAdmin` only; too-low frames dropped as today); a 141=Y Logon received with 34 ≠ 1; a too-low frame
in NotConnected / LogonSent (L-103-7); a peer-spontaneous 141=Y ack at 34=1 with a hydrated N > 1 (L-103-2).
