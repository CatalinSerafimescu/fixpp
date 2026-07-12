# Contract: Public C++ Session API — `reset_sequence_numbers()`

The single public surface added by feature 071. No C-ABI and no Python change (FR-008).

## Signature

```cpp
// include/fixpp/session/session.hpp — adjacent to send() (:282) and close() (:188)
//
// Originate a mid-session sequence-number reset on an INITIATOR session:
// send Logon(141=Y) on the live transport, durably reset both counters to 1,
// and complete on the peer's confirming Logon(141=Y). Does NOT drop the transport.
[[nodiscard]] asio::awaitable<expected_t<void>> reset_sequence_numbers();
```

- Runs on the session's single-writer strand (same discipline as `send()`/`close()`).
- Returns `expected_t<void>` (`std::expected<void, fixpp::core::error>`).

## Preconditions & return contract

Eligibility requires **all three** conjuncts (FR-007):

| Condition | Result |
|---|---|
| `cfg_.role == initiator` AND `fsm_state_ == Active` AND `!reconnect_fsm_.is_awaiting_resend()` | Proceeds: set `midsession_reset_in_progress_` and transition `Active→LogonSent` **before the first `co_await`** (FR-014), then durable reset → emit `Logon(141=Y)` at seq 1 under the non-Active state. Resolves `expected_t<void>{}` once the reset Logon is durably stored+emitted (the peer handshake completes asynchronously via the ack arm, like connect-time logon). |
| `cfg_.role == acceptor` (even if Active) | `std::unexpected(error::session_invalid_state_for_reset)`. No state change, no frame emitted. |
| `fsm_state_ != Active` (NotConnected / LogonSent / LogonReceived / LogoutSent / Disconnected) | `std::unexpected(error::session_invalid_state_for_reset)`. No-op. |
| `Active` but `is_awaiting_resend()` (mid inbound-gap resend) | `std::unexpected(error::session_invalid_state_for_reset)`. No-op. |
| Durable store reset fails on a persistent store | Existing fatal disposition (→terminal), mirroring the shipped reset arms (FR-002); `onLogout` fires (FR-015). |

## Post-handshake observable contract

- On the peer's confirming `Logon(141=Y)`: `next_outbound == 2`, `next_inbound == 2`; the next `send()` carries MsgSeqNum 2 (no duplicate 1). (FR-005 / INV-071-1). If a non-reset frame intervened in the window, the handshake fails closed rather than mis-restoring (FR-004 / INV-071-6).
- **Callbacks**: across a **successful** reset the application observes **neither** `onLogon` nor `onLogout` (INV-071-2). Across a **failed** reset (see below) it observes `onLogout` exactly once (INV-071-2b).
- The transport is never closed by this operation, and it is **not** closed by a peer `Heartbeat`/`TestRequest` arriving in the LogonSent reset window (drained/tolerated — FR-017/INV-071-7). (SC-001)
- **Peer non-confirmation**: there is **no dedicated logon-response timeout** (verified; same as connect-time logon). The session remains in LogonSent until transport EOF or an application close; on that teardown `onLogout` fires. Limitation **L-071-3**. (R5/FR-009)

## Acceptor / peer expectation

Scoped **initiator-only, against a conforming acceptor** (interop-validated vs QuickFIX). Whether fixpp's own acceptor honors an *inbound* mid-session `Logon(141=Y)` while Active is out of scope (spec Scope Boundaries).

## Error value contract

`error::session_invalid_state_for_reset` — new `fixpp::core::error` slot (non-renumbering append), `to_string` → `"session: invalid state for reset"`. Non-fatal (a rejected trigger, not a session-fatal condition). Not surfaced at the C-ABI.
