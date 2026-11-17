# Contract: session wire endings changed by 104 (one condition, one observable ending)

Decisions: [research.md](../research.md) D1–D7. Notation:
- N = NextNumIn before the frame, M = the frame's MsgSeqNum(34), N_out = fixpp's next outbound number before the
  frame.
- T = `logout_disconnect_timeout_ms`.
- "rol" = `reset_on_logout`, "rod" = `reset_on_disconnect`.
- "reset" = the 141=Y reset unit `run_reset_unit_(1, 1)`: one `MessageStore::reset_to` operation, after which both
  numbers are 1 in the manager and the store.

## Part 1 — the reset trigger under `reset_on_logout` (supersedes 024 C3.1 by name)

**RT-1 (supersedes 024 `contracts/reset-knobs.md` C3.1, "either direction").**
- With rol set, the sequence numbers are reset to 1 when, and only when, the session **receives** a Logout from
  the peer: a request in Active or LogonReceived, the response to its own Logout in LogoutSent, or a too-high
  Logout (B-103-7).
- A Logout fixpp **sends** never triggers it, whether answered or not. This covers `close(graceful)`'s phase 1,
  every error-path Logout and 104's too-low Logout.
- The trigger is the receipt, kept in the private flag `logout_seen_`, whose contract 104 changes: it is set
  only at the receipt sites named above, and no longer when fixpp's own Logout is emitted.

**RT-2 (where the reset runs).** In the arm that received the Logout, in this order:
1. the reply Logout, if one is owed (Active request; the too-high Logout's acknowledgement);
2. what the arm does today before its Disconnected write: the event, `fromAdmin` and the inbound persist in
   Active, nothing in LogoutSent;
3. **the reset**, with the flag cleared immediately before it;
4. `Disconnected`. Where the arm leaves Active here (a request, a too-high Logout), `onLogout`, fired on that
   edge, observes `(1, 1)`. On the LogoutSent route `onLogout` fired earlier, on phase 1's Active → LogoutSent
   edge, and the reset is observable only by its position: in LogoutSent, before `close()`'s phase 2;
5. LogoutSent only: then `logout_confirmed_` and phase 1's wake.

For the too-high Logout, steps 1 to 4 run in `acknowledge_peer_logout_`, at its immediate acknowledgement or the
bound's.

**RT-3 (backstops).** When the arm ends before step 3, the flag stays set. The causes are a `fromAdmin` throw, a
late parse failure, a failed inbound persist, or a too-high Logout's bound ending unacknowledged (claimed by
`close()`, stop, another ending's Disconnected, or a reconnect drain). The first of these then makes the reset:
- **`close()`'s teardown**, whose predicate is `(owed && rol) || rod`;
- **an in-place reconnect's Logon**, through `emit_initiator_logon_`'s pre-Logon reset (fatal disposition). That
  Logon is built at `(1, 1)` and carries 141=Y (024 C2.1).

**RT-4 (failure).** A manager or store failure of the receipt reset is logged.
- The arm's ending is unchanged: Disconnected, and the return value it has without the failure.
- There is no retry under rol. Under rod, the teardown reset still runs.
- On a refusing persistent store, the stored numbers stay pre-reset (L-104-2).

**RT-5 (`close()`).**
- `close()` waits for an in-flight reset unit, a receipt reset or a Logon arm's 141=Y unit, **before** it
  evaluates its teardown predicate, whatever the knobs. The wait is bounded by `logon_timeout_ms` and needs a
  clock. On expiry it emits `session_event_close_reset_wait_expired`.
- 024 C5.1 is kept as written: at most one store reset per teardown (`teardown_reset_done_`).
- With rol and rod both set, a received Logout therefore makes two resets, one at receipt and one at teardown.
  Both numbers end at 1.

**RT-6 (unchanged).** `reset_on_disconnect` (024 C4.1, C4.2), and the not-sent and cut Logouts (no reset).

## Part 2 — endings per arm and input

| # | Arm / condition | Input | Wire from fixpp | Numbers after | Store | State |
|---|---|---|---|---|---|---|
| W1 | LogoutSent (fixpp's graceful Logout sent), rol set | no peer Logout; T expires | nothing more | as after the Logout's emit | unchanged by any reset | Disconnected; `close()` ok |
| W2 | same | transport drops (peer EOF) during the wait | nothing more | as W1 | as W1 | Disconnected; `close()` ok |
| W3 | same | peer Logout, before T | nothing more | `(1, 1)` before the arm's Disconnected write (`onLogout` already fired at Active → LogoutSent) | one `reset_to(1, 1)` while LogoutSent, the transport open, before `close()`'s phase 2 | Disconnected |
| W4 | Active, rol set | peer Logout request, M = N | Logout reply at 34 = N_out | `(1, 1)` at `onLogout` | inbound persist, then `reset_to(1, 1)` | Disconnected |
| W5 | Active, rol set, T > 0, a clock | peer Logout with M > N | ResendRequest (as B-103-7); when N passes M or at T, the acknowledgement Logout | `(1, 1)` at `onLogout` after the acknowledgement | `reset_to(1, 1)` after the acknowledgement | Disconnected |
| W6 | W5 pending, then `close()`, stop, the peer's EOF or a reconnect drain | — | as B-103-7 for that route | `(1, 1)` at teardown, or at the reconnect's Logon (34=1, 141=Y) | one reset at teardown or before that Logon | as that route |
| W7 | W3 or W4 with rol **and** rod | — | as W3 / W4 | `(1, 1)` | two resets: receipt and teardown | Disconnected |
| W8 | W3 or W4, receipt reset fails | — | as W3 / W4 | manager `(1, 1)`; store pre-reset when the store refused | logged | Disconnected; arm's return unchanged |
| W9 | standalone initiator: W3/W4 reset at receipt, then `drive_reconnect`, numbers advance, then `close()` with rol only | — | the new Logon at the numbers as they stand | not reset by the new connection's teardown | no further reset | closed |
| U1 | LogonSent, lenient or unilateral, fixpp's Logon without 141=Y, N ≥ 1 | ack 141=Y, M = 1 | no second Logon; the next frame fixpp sends is 34=1 | inbound next 2, outbound next 1 | `reset_to(2, 1)`; event `by_peer_request = true` | Active |
| U2 | same | ack 141=Y, M > 1 | no second Logon; on the peer's next frame (34 > 1), ResendRequest 7=1, 16=0 | inbound next 1, outbound next 1 | `reset_to(1, 1)` | Active |
| U3 | U1 or U2 with 789 enabled and present, 789 = X | X > 1 | Logout "NextExpectedMsgSeqNum too high, expecting 1 but received X" | as after the reset, plus the Logout | reset, then the Logout stored | Disconnected |
| U4 | `bilateral_strict` or a knob-asked reset (fixpp's Logon carried 141=Y) | any ack | unchanged (today's `check_inbound` against N = 1) | unchanged | unchanged | unchanged |
| U5 | after U1 (outbound next 1, nothing sent) | ResendRequest at 34=2, 7=1, 16=0 | one GapFill 34=1, 43=Y, 122=52, 123=Y, 36=1 | inbound next 3 | inbound persist | Active |
| U6 | Active, any history | ResendRequest whose EndSeqNo ≥ N_out (999999 on FIX.4.0/4.1 included) | as 16=0: the walk to N_out − 1, and an empty-range GapFill's 36 = N_out (never EndSeqNo + 1) | unchanged | as today | Active |
| T1 | LogonSent, ack without 141=Y, M < N (43 any) | — | one Logout, 58 = "MsgSeqNum too low, expecting N but received M", 34 = N_out, no 1409, no 789; `toAdmin` called once before its number is taken | N unchanged | the Logout stored as outbound; nothing inbound persisted | Disconnected |
| T2 | NotConnected (acceptor), Logon without 141=Y, M < N, not strict, `reset_on_logon` off (43 any) | — | as T1, 34 = the hydrated next outbound | as T1 | as T1 | Disconnected |
| T3 | T1/T2, Logout over 256 bytes (long CompIDs) | — | none | as T1 | nothing | Disconnected; success |
| T4 | T1/T2, `toAdmin` throws | — | none | as T1 | nothing | Disconnected; `app_callback_threw` |
| T5 | T1/T2, `assign_outbound` fails | — | none | as T1 | nothing | Disconnected; that error |
| T6 | T1/T2, the Logout's store or write fails | — | none, or what the store and write let through | as T1 | as the store left it | Disconnected; success |
| T7 | T1/T2 with rol set | — | as T1 | not reset by this Logout | no reset | Disconnected |

**Precedence on one Logon frame** is each arm's code order (research D5): an earlier guard (validate gate,
`interpret_logon`, posture, SendingTime, `seq == 0`, and in NotConnected the strict refusal and the
`reset_on_logon` reset) takes its own ending, and the later guards (1137, strict mismatch, the unit, the
CompID-authorization gate, 789) are not reached by a too-low frame. On an mTLS acceptor, T2 precedes the
authorization gate (L-104-3).

**Unchanged and disclosed:**
- The too-low Logout leaves the transport open until EOF, `close()` or stop (B-103-10, L-103-5).
- The Logon arms' sequence check ignores `validate_sequence_numbers`.
- A strict acceptor's 141=Y Logon at 34 ≠ 1 (L-103-4).
- The acceptor twin of U1 is unreachable (research D7).
