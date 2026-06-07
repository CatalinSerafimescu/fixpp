# Contracts — NextExpectedMsgSeqNum(789) fast session resume

Interface-level contracts for the 027 slice. No C-ABI surface, no new error slot, no codegen. All behaviour is gated on the `SessionConfig` knob (default off ⇒ contracts C2–C6 are no-ops). **The honor/suppression contracts (C3–C6) are defined against the real two inbound-Logon handlers (research D-0): acceptor `NotConnected` (`session.cpp:1508`, fatal `check_inbound` @ `:1571`, reply emit @ `:1766`) and initiator `LogonSent` (`:2755`, fatal `check_inbound` @ `:2842`). The Active arm `:1968-2009` is OFF the reconnect-Logon path (C5 secondary).**

## C1 — Config surface
- `SessionConfig::enable_next_expected_msg_seq_num : bool = false`.
- Single knob controlling both emit (C2) and honor (C3–C6). Default false ⇒ byte-identical no-op (C8).

## C2 — Emit (outbound Logon)
- `build_logon(..., std::optional<seqnum_t> next_expected_seq = std::nullopt)` (internal `admin_messages.hpp` builder).
  - `next_expected_seq` present ⇒ append `789=<value>` after the `141` block (`admin_messages.cpp:150-155`); valid SEQNUM, ≥1.
  - absent (default) ⇒ no 789 field; output byte-identical to today.
- Header capture: the `scan_frame_header` switch (`session.cpp:1213`) gains a `case 789:` writing `FrameHeader::next_expected_msg_seq_num` (E3; consistent with data-model E3 + tasks T011). `interpret_logon` is unchanged (tolerates the optional field).
- Call sites pass the value only when the knob is on, **plain `next_inbound_unsafe()` (NO `+1`)**:
  - Initiator (`session.cpp:601`): `next_expected_seq = seqnum_mgr_.next_inbound_unsafe()`.
  - Acceptor reply (`session.cpp:1745`): `next_expected_seq = seqnum_mgr_.next_inbound_unsafe()` — already post-`check_inbound` increment (E-OBO; the value is cause-dependent under 141, data-model Reset table).

## C3 — `replay_outbound_range_` helper (the walk extraction — TWO-value end model)
- `asio::awaitable<expected_t<void>> replay_outbound_range_(seqnum_t begin, seqnum_t requested_end, bool end_is_through_current)`.
- Factored from the inline `ResendRequest`-reply walk (`session.cpp:2485-2635`): replays stored app messages `PossDupFlag(43)=Y`+`OrigSendingTime(122)` at original `MsgSeqNum`; collapses admin/absent runs into one `SeqReset`-`GapFill`. Transmit-only (no live-counter advance, not re-stored).
- **The signature preserves the inline walk's TWO-value end model** — a single `(begin, end_inclusive)` arg is WRONG (it loses the original requested end and regresses the shipped 013 ResendRequest GapFill `NewSeqNo`). The helper OWNS the resolution: `eff_end = (end_is_through_current || requested_end > our_last) ? our_last : requested_end` (the per-slot loop + trailing-flush bound); the empty/short-store GapFill `NewSeqNo = end_is_through_current ? peek_outbound() : (requested_end + 1)` (mirrors `:2558-2559` `rr_end` basis). The `EndSeqNo=0 ⇒ through-current` resolution moves INTO the helper (removed from the ResendRequest handler).
- Returns `expected_t<void>`: `app_callback_threw` when an emitted GapFill's toAdmin throws (the old `gapfill_callback_threw` semantics); other failures are non-`Disconnected`-driving — **the CALLER owns the FSM `Disconnected` transition** (the inline block's embedded `record_state_transition_(Disconnected)` early-returns move to the caller).
- Callers: (1) the ResendRequest handler (Active): `begin=rr_begin, requested_end=rr_end` (raw parsed `EndSeqNo`), `end_is_through_current=(rr_end==0)`; (2) the 789 honor path: `begin=X, requested_end=N-1, end_is_through_current=true` (N-1 is our current last outbound). Exactly ONE walk implementation (invariant-count regression).
- **Required tests (both callers):** explicit-end-beyond-store (request `[10,20]`, store through 5 ⇒ GapFill `NewSeqNo=21`) AND `EndSeqNo=0`/through-current empty-store (`NewSeqNo=peek_outbound()`).

## C4 — Honor (inbound Logon carrying 789), knob on — BOTH handlers
With `X = parse_seqnum(789)` and `N = seqnum_mgr_.peek_outbound()`:
- 789 absent → no proactive resend (standard handling).
- **`X == 0` (present-but-invalid: empty / non-digit / overflow)** → C6 (Logout+disconnect). Evaluated FIRST, before the `X<N` compare.
- `X < N` → proactively resend `[X, N-1]` via `replay_outbound_range_`. No `ResendRequest` emitted for this range.
  - **Acceptor**: the resend runs AFTER the reply Logon's `store_then_emit` succeeds (`:1766`), not at the parse point (RC#4 ordering).
  - **Initiator**: the resend runs after processing the Logon-ack (its own Logon was already sent @ `:601`).
- `X == N` → no resend.
- `X > N` → C6.

## C5 — Behind-side recovery + ResendRequest suppression (knob on)
- When the peer's Logon `MsgSeqNum = X_logon` is itself too-high, the handler MUST NOT take the fatal `check_inbound` path (acceptor `:1571`, initiator `:2842`). **Held-Logon consume — formulation A (do-NOT-advance):** leave `next_inbound_` at its current value X (no `set_next_inbound`, no advance for the held Logon's own seqnum), emit NO at-logon `ResendRequest`, and proceed toward Active. The peer's proactive resend of `[X, peer_N-1]` (driven by our advertised 789) is admitted **in-sequence via the existing Active in-sequence path** and carries `next_inbound_` forward to `peer_N`; the held Logon's seqnum is consumed by being inside that range (the resend always spans `X_logon` since `X_logon ≤ peer_N-1`). After recovery `next_inbound_ == peer_N` (NOT `X_logon+1` — `set_next_inbound(X_logon+1)` would make the resent frames too-low and fatally disconnect; data-model I-NEX-5/I-NEX-12). No explicit `enter_awaiting_resend` on the Logon path — the resent frames are in-sequence relative to the un-advanced counter.
- The Active too-high arm (`session.cpp:1968-2009`) — a SECONDARY, steady-state site — keeps emitting `ResendRequest` when the knob is OFF (013 intact). It remains active even when the knob is on, as recovery-of-last-resort for a lost proactive resend (C7).
- No automatic fallback: if the peer doesn't act on our 789, our own at-logon gap does not fast-recover (limitation L-027-1).
- The 789 comparison basis is `peek_outbound()` (OUTBOUND); the Active arm's own too-high test is `next_inbound_unsafe()` (INBOUND) — distinct counters (data-model I-NEX-11).

## C6 — Integrity error (X > N, or present-but-invalid X)
- `build_logout` with text "NextExpectedMsgSeqNum too high, expecting N but received X" (X>N) or "NextExpectedMsgSeqNum invalid" (parse→0), then disconnect. MUST NOT advance to established as in-sync. Both handlers.

## C7 — Lost-resend self-heal
- 789 suppresses only the at-logon ResendRequest; a dropped proactive resend is re-requested by the next inbound frame via the Active arm (data-model I-NEX-10 / research D-11).

## C8 — Both roles (symmetry)
- C2–C7 apply to both inbound-Logon handlers (acceptor + initiator), at each handler's correct sites (the two handlers are structurally distinct — D-0).

## C9 — Default-off invariant
- Knob off ⇒ outbound Logon byte-identical to baseline (no 789), inbound 789 ignored, existing `ResendRequest` recovery (013) unchanged.

## C10 — Live interop (VII.6)
- `tests/interop/happy/hp_fix44_next_expected_test.cpp` (skip-without-counterparty), both roles: fixpp + a `EnableNextExpectedMsgSeqNum` peer (QFcpp/QFJ) recover an at-logon gap with **zero `ResendRequest` on the wire**, all missed messages delivered in order; plus a bidirectional-gap cell.
