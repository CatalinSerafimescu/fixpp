# Data Model: 104 sequence-reset fidelity

All the state below is private to `Session` and lives on the session strand. No public type, member,
configuration key or default changes. No member is added or renamed (one member's contract changes), so `sizeof(Session)` is unchanged.
Decisions are in [research.md](research.md); the observable contract is in
[contracts/session-wire-endings-104.md](contracts/session-wire-endings-104.md).

## `logout_seen_` (name kept; contract changed)

`bool`, default `false`.

| Event | Effect |
|---|---|
| A peer Logout received: `on_inbound_active_logout_` (Active or LogonReceived), `on_inbound_active_too_high_logout_`, `on_inbound_logout_sent_` | set |
| fixpp's own Logout emitted (`run_logout_phase1`) | **no effect** (the base's set is deleted) |
| Any other Logout fixpp sends (error paths, the too-low Logout) | no effect |
| The receipt reset is about to run (`rol` set, flag set) | cleared, then the unit runs |
| `emit_initiator_logon_` with `rol` set and the flag set (an in-place reconnect after an unfinished receipt) | the Logon's pre-Logon reset runs, then the flag is cleared |
| `Session::close`'s teardown | read: `(flag && rol) \|\| rod` |
| `install_reconnected_transport` | no effect (an owed reset survives into the next Logon, which makes it) |

- **Invariant RO-1.** The flag is set only while a received peer Logout's reset has not been attempted. Where
  `rol` is set it is cleared by exactly one of: the receipt reset, the next in-place Logon's reset, or the
  teardown.
- **Invariant RO-2.** No Logout that fixpp sends sets the flag. FR-015 follows from this and B-538-7's first half
  still holds.
- The header comment states RO-1 and RO-2, names 104 and the 024 T013 / C3.1 clause it supersedes, and gives
  the recipe `git grep -n logout_seen_ -- src include`. It contains no count of sites (FR-006).

## The reset unit: new callers

`run_reset_unit_(next_in, next_out)` (093 data-model E-10, contract C-6) is unchanged. It gains callers:

| Caller | Targets | After the call |
|---|---|---|
| `on_inbound_active_logout_`, `on_inbound_logout_sent_`, `acknowledge_peer_logout_` (receipt, `rol` set) | `(1, 1)` | `reset_cancellation_state(enable_total_cancellation{})`; a manager or store error is logged; the arm continues to its Disconnected write |
| `on_inbound_logon_sent_`, the unasked branch (D3.1) | `(2, 1)` when the acknowledgement's 34 is 1; `(1, 1)` otherwise | today's step 5 to step 7 (030's persistent-fatal disposition) |

`reset_unit_in_flight_` and `reset_unit_wake_` are unchanged. `close()` reads them earlier (below).

## `Session::close`'s teardown: order changed

| Step | Base | 104 |
|---|---|---|
| a | `if (!teardown_reset_done_ && ((logout_seen_ && rol) \|\| rod))` | **wait for an in-flight unit** (`reset_unit_in_flight_ && effective_clock_`, bounded by `logon_timeout_ms`, event on expiry) |
| b | inside: set `teardown_reset_done_`, wait for an in-flight unit, `reset_seqnums_to_one_durable(logged)` | `if (!teardown_reset_done_ && ((logout_seen_ && rol) \|\| rod))`: set `teardown_reset_done_`, then `reset_seqnums_to_one_durable(logged)` |

024 C5.1 (at most one reset per teardown) is unchanged.

## LogonSent acknowledgement: the 141 branch

Inputs, all on the strand:
- `peer_ack_sent_reset_flag`, the acknowledgement's 141=Y, now read **before** Guard (4);
- `own_logon_sent_reset_flag_`, read there and still one-shot cleared where it is today;
- `seq`, the acknowledgement's 34.

| `peer_ack_sent_reset_flag` | `own_logon_sent_reset_flag_` | Sequence check | `logon_inbound_advanced_init` | Unit targets |
|---|---|---|---|---|
| false | any | `check_inbound(seq)` against N (today). A too-low result takes the D5 Logout | as today | no unit |
| true | true (strict, or a knob at `{1,1}`) | `check_inbound(seq)` against N = 1 (today) | as today | as today |
| true | false (**unasked**) | **none** | `seq == 1` | in: 2 if consumed, else 1; out: 1 |

## The too-low Logout text

One file-local builder fills a fixed `std::array<char, K>` with `"MsgSeqNum too low, expecting " + N + " but
received " + M`, the numbers written with `std::to_chars`. It is used by 103's Arm B and by both Logon arms. K is
fixed by Arm B's existing `static_assert` (two phrases plus two `digits10 + 1` numbers). No number is written into a
comment.

## `replay_outbound_range_`: the end model, amended (D3.3a)

Inside the walk, after the gate's shared-hold wait and the store tail read, `end_is_through_current` becomes
`end_is_through_current || requested_end >= next`, where `next` is the tail read's next outbound number (or
`peek_outbound()`, read there, when there is no store). The callers do not change: the ResendRequest answer passes
`rr_end == 0`, and the 789 walk already passes `true`. The rest of 027's two-value end model is unchanged. The
effect is on the empty-range GapFill:

| Case | Base GapFill NewSeqNo | 104 |
|---|---|---|
| EndSeqNo = 0 | `peek_outbound()` | same |
| EndSeqNo ≥ next outbound (999999 on FIX.4.0/4.1 included) | `requested_end + 1` when `our_last == 0` or `begin > eff_end` | `peek_outbound()` |
| EndSeqNo < next outbound | `requested_end + 1` | same |

The comment on the end model names 104 and keeps the formula. It gives no example values.

## B&L delta (text for phase 9)

New rows:
- **B-104-1** — with `reset_on_logout`, the numbers reset when the peer's Logout is received (a request, the
  response to fixpp's Logout, or a too-high Logout at its acknowledgement). The reset comes after the reply and
  before Disconnected, so an `onLogout` fired on that edge sees 1 and 1 (a local graceful close fired its
  `onLogout` earlier, on leaving Active). A Logout fixpp sends never resets, answered or not. A graceful
  close whose Logout goes unanswered, whether by the timeout or a dropped connection, keeps the numbers.
  Supersedes 024 C3.1 and B-538-7's "answered or not". As QuickFIX C++, QuickFIX/J and QuickFIX/Go.
- **B-104-2** — `close()` waits for any 141=Y reset unit in flight (a receipt reset or a Logon's) before its
  teardown reset decision, whatever the knobs. The wait is bounded by `logon_timeout_ms` and needs a clock.
- **B-104-3** — with `reset_on_logout` and `reset_on_disconnect` both set, a received peer Logout makes two store
  resets, at receipt and at teardown. C5.1 still allows one per teardown. The numbers end at 1. As QuickFIX C++
  and QuickFIX/J.
- **B-104-4** — a lenient or unilateral initiator accepts an unasked 141=Y acknowledgement whatever NextNumIn was.
  At 34=1: inbound next 2, outbound next 1, Active. At 34 = M > 1: inbound next 1, outbound next 1, Active, and
  ResendRequest from 1 on the peer's next frame. No second Logon is sent. With 789 enabled, a 789 above 1 then
  draws "NextExpectedMsgSeqNum too high". Retires L-103-2.
- **B-104-5** — a ResendRequest whose EndSeqNo is at or above fixpp's next outbound number is answered as one
  through-current. An empty-range GapFill's NewSeqNo is the next outbound number, never EndSeqNo + 1.
- **B-104-6** — a frame below NextNumIn in NotConnected or LogonSent draws a Logout with Text "MsgSeqNum too low,
  expecting N but received M", whatever its PossDupFlag. Then Disconnected, with NextNumIn not advanced and no
  1409. The endings when the Logout cannot be sent are those of research D5's table. It is not a
  `reset_on_logout` trigger. It is sent with `validate_sequence_numbers` off too. Retires L-103-7. Amends
  B-103-10's "In Active".
- **B-104-7** — a peer Logout whose reset under `reset_on_logout` was still owed when an in-place reconnect began
  is reset by the new connection's Logon, which goes at 34=1 with 141=Y. A Logout received on the old connection
  never resets the new connection at teardown. Amends B-103-9's disclosure.

New limitations:
- **L-104-1** — a ResendRequest answered when fixpp has sent nothing at or above BeginSeqNo writes a GapFill whose
  NewSeqNo equals its MsgSeqNum, (1, 1) right after an unasked reset. QuickFIX C++ and QuickFIX/J send none at
  next outbound 1, and QuickFIX/Go none at all. The receiver treats it as a no-op.
- **L-104-2** — a failed receipt reset is logged. On a persistent store that refused, the stored numbers keep
  their pre-reset values until the next reset (as the teardown reset, I-07; as QuickFIX/Go).
- **L-104-3** — on an mTLS acceptor, the too-low Logout is sent before the CompID-authorization gate, as the
  posture, strict and SendingTime refusals are. Its Text tells a TLS-validated peer that is not authorised for
  the CompID the acceptor's NextNumIn.
- **L-104-4** — a too-low Logon with 43=Y draws the Logout (FIX-SL §4.3.11, QuickFIX/Go). QuickFIX C++ and
  QuickFIX/J validate PossDup and then wait for their logon timeout.
- **L-104-5** — after an unasked 141=Y acknowledgement at 34 = M > 1, fixpp's ResendRequest waits for the peer's
  next frame. QuickFIX/J and QuickFIX/Go send it at Logon, and QuickFIX C++ continues at inbound 2 (provisional,
  research O-2).
- **L-104-6** — with 789 enabled, an unasked 141=Y acknowledgement's 789 is evaluated after the reset, as
  QuickFIX C++ and QuickFIX/J do. QuickFIX/Go skips 789 when 141 is present.

Amended in place:
- **B-538-7**: "answered or not" struck, pointing to B-104-1.
- **B-103-9**'s disclosure: its last clause is replaced by B-104-7.
- **B-103-10**: "In Active" becomes "In Active, and since 104 in the Logon arms (B-104-6)".

Retired, moved to `spec/behaviors-and-limitations-closed.md`:
- **L-103-2**, resolved by B-104-4.
- **L-103-7**, resolved by B-104-6. 103's spec row "A too-low frame in NotConnected or LogonSent still ends
  silently" is superseded by name there.
