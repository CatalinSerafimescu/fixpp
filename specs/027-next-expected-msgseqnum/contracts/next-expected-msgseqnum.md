# Contracts — NextExpectedMsgSeqNum(789) fast session resume

Interface-level contracts for the 027 slice. No C-ABI surface, no new error slot, no codegen. All behaviour is gated on the `SessionConfig` knob (default off ⇒ contracts C2–C6 are no-ops).

## C1 — Config surface
- `SessionConfig::enable_next_expected_msg_seq_num : bool = false`.
- Single knob controlling both emit (C2) and honor (C3–C5). Default false ⇒ byte-identical no-op (C7).

## C2 — Emit (outbound Logon)
- `build_logon(..., std::optional<seqnum_t> next_expected_seq = std::nullopt)` (internal `admin_messages.hpp` builder).
  - `next_expected_seq` present ⇒ append `789=<value>` after the `141` block (valid SEQNUM, ≥1).
  - absent (default) ⇒ no 789 field; output byte-identical to today.
- Call sites pass the value only when the knob is on:
  - Initiator (`session.cpp:601`): `next_expected_seq = seqnum_mgr_.next_inbound_unsafe()`.
  - Acceptor reply (`session.cpp:1745`): `next_expected_seq` per data-model **E-OBO** (off-by-one against fixpp's increment timing; RED-witnessed).

## C3 — Honor (inbound Logon carrying 789), knob on
With `X = parse(789)` and `N = seqnum_mgr_.peek_outbound()`:
- `X < N` → proactively resend `[X, N-1]` via the existing replay walk (`session.cpp:2485+`): app messages replayed `PossDupFlag(43)=Y`+`OrigSendingTime(122)` at original `MsgSeqNum`; admin/absent runs → one `SeqReset`-`GapFill`. No `ResendRequest` emitted for this range.
- `X == N` → no resend.
- `X > N` → C4.
- 789 absent on the inbound Logon → no proactive resend (standard handling).

## C4 — Integrity error (X > N)
- `build_logout` with text "NextExpectedMsgSeqNum too high, expecting N but received X" (or equivalent), then disconnect. MUST NOT advance to established as in-sync.

## C5 — ResendRequest suppression (knob on)
- The at-logon too-high arm (`session.cpp:1964-1991`) MUST NOT emit `ResendRequest(2)` when the knob is on (rely on the peer's 789-driven proactive resend).
- No automatic fallback: if the peer doesn't act on our 789, our own gap is not recovered (limitation L-027-1).

## C6 — Both roles (symmetry)
- C2/C3/C4/C5 apply identically whether fixpp is initiator or acceptor.

## C7 — Default-off invariant
- Knob off ⇒ outbound Logon byte-identical to baseline (no 789), inbound 789 ignored, existing `ResendRequest` recovery (013) unchanged.

## C8 — Live interop (VII.6)
- `tests/interop/happy/hp_fix44_next_expected_test.cpp` (skip-without-counterparty), both roles: fixpp + a `EnableNextExpectedMsgSeqNum` peer (QFcpp/QFJ) recover an at-logon gap with **zero `ResendRequest` on the wire**, all missed messages delivered in order.
