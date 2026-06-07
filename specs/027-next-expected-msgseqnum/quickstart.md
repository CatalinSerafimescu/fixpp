# Quickstart — NextExpectedMsgSeqNum(789) fast session resume

## Enable it (both ends)

```cpp
SessionConfig cfg;
cfg.enable_next_expected_msg_seq_num = true;   // default false; enable on BOTH peers
```

789 is a **both-peers-required** feature: enable it on both the initiator and the acceptor. If only one side enables it, the other won't proactively backfill and — because the enabling side suppresses its own at-logon `ResendRequest` — recovery of its gap will not complete (limitation L-027-1). With the knob off (default), behaviour is byte-identical to today.

## What happens on reconnect (both enabled)

1. Each side's Logon carries `NextExpectedMsgSeqNum(789)` = the next inbound seqnum it expects from the peer.
2. On receiving the peer's Logon with `789 = X`, with `N` = our next-outbound:
   - **X < N** → we proactively resend `[X, N-1]` right after our Logon (application messages with `PossDupFlag=Y`+`OrigSendingTime`; admin gaps as `SeqReset`-`GapFill`) — **no `ResendRequest`**.
   - **X == N** → in sync, nothing resent.
   - **X > N** → the peer claims messages we never sent → we send a `Logout` (with explanatory text) and disconnect.
3. The gap is recovered within the Logon exchange — the `ResendRequest`→resend round-trip is eliminated.

## Scenarios → witnesses

| Scenario | Witness (`tests/session/test_next_expected_msgseqnum.cpp`) |
|---|---|
| Initiator advertises correct 789 | `Emit_Initiator_AdvertisesNextExpectedInbound` |
| Acceptor reply advertises correct 789 (off-by-one) | `Emit_AcceptorReply_OffByOneCorrect` |
| Peer behind (X<N) → exact resend, no ResendRequest | `Honor_XltN_ResendsExactRange_NoResendRequest` |
| In sync (X==N) → no resend | `Honor_XeqN_NoResend` |
| Impossible expectation (X>N) → Logout+disconnect | `Honor_XgtN_LogoutTextThenDisconnect` |
| Knob off → byte-identical, inbound 789 ignored | `DefaultOff_ByteIdenticalLogon_InboundIgnored` |
| ResendRequest suppressed on, kept off | `Suppression_KnobOn_NoResendRequest_KnobOff_Yes` |
| 141=Y reset + 789 → advertises 1 | `Reset141Plus789_AdvertisesOne` |
| No heap on emit/resend | `NoHeap_EmitAndResendPath` |
| Live both-role vs QFcpp/QFJ | `tests/interop/happy/hp_fix44_next_expected_test.cpp` (skip-without-counterparty) |

## Not in scope

- **Cross-restart continuity** — 789 reads the in-memory seqnum state; it does NOT persist/rehydrate seqnums across a process restart. That is the parked **025 RefreshOnLogon** dependency (store→manager hydrate-on-open), the G3 capstone.
- **FIXT/5.0SP2 per-version gating** — deferred to G4. 789 is implemented for fixpp's current FIX 4.4 sessions (the field is already in `FIX44.xml`).
