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
   - **invalid X** (empty / non-numeric / overflow) → treated as a sequence-integrity error → `Logout` + disconnect (checked before the X<N compare, so a malformed 789 can never trigger a full-history replay).
   - **X < N** → we proactively resend `[X, N-1]` right after our Logon (application messages with `PossDupFlag=Y`+`OrigSendingTime`; admin gaps as `SeqReset`-`GapFill`) — **no `ResendRequest`**. (The acceptor emits the resend after its reply Logon; the initiator after processing the Logon-ack.)
   - **X == N** → in sync, nothing resent.
   - **X > N** → the peer claims messages we never sent → we send a `Logout` (with explanatory text) and disconnect.
3. If we are the behind side (our own Logon `MsgSeqNum` is too-high for the peer), we do NOT fatally disconnect at logon and we do NOT send a `ResendRequest`. We keep our inbound counter where it is and admit the peer's proactive resend (driven by our advertised 789) **in sequence**: each resent frame advances our inbound counter by one until it reaches the peer's next live sequence number, so the next live frame is in sync. (We do NOT jump our counter past the peer's Logon seqnum — that would make the resent frames look too-low and disconnect; the held Logon's seqnum is recovered simply by being inside the resend range.) Both directions work simultaneously if both sides have a gap.
4. The gap is recovered within the Logon exchange — the `ResendRequest`→resend round-trip is eliminated. (If the peer's proactive resend is itself lost, the residual gap is re-requested on the next inbound frame via the steady-state recovery path.)

## Scenarios → witnesses

| Scenario | Witness (`tests/session/test_next_expected_msgseqnum.cpp`) |
|---|---|
| Initiator advertises correct 789 | `Emit_Initiator_AdvertisesNextExpectedInbound` |
| Acceptor reply advertises correct 789 (plain, no `+1`) | `Emit_AcceptorReply_AdvertisesNextInboundNoPlusOne` |
| Acceptor peer behind (X<N) → exact resend AFTER reply, no ResendRequest | `Honor_Acceptor_XltN_ResendsExactRange_AfterReply_NoResendRequest` |
| Initiator peer behind (X<N) → exact resend, no ResendRequest | `Honor_Initiator_XltN_ResendsExactRange_NoResendRequest` |
| In sync (X==N) → no resend | `Honor_XeqN_NoResend` |
| Impossible expectation (X>N) → Logout+disconnect | `Honor_XgtN_LogoutTextThenDisconnect` |
| Invalid 789 (empty / non-numeric / overflow) → Logout+disconnect | `Honor_Invalid789_LogoutThenDisconnect` |
| Behind side admits peer's resend, no fatal disconnect | `BehindSide_KnobOn_AdmitsPeerResend_NoFatalDisconnect_Acceptor` + `_Initiator` (both roles) |
| Bidirectional gap → both recover, no double recovery | `Bidirectional_BothGaps_RecoverNoDoubleRecovery` |
| Lost proactive resend → self-heals via Active arm | `LostResend_SelfHealsViaActiveArm` |
| Knob off → byte-identical, inbound 789 ignored | `DefaultOff_ByteIdenticalLogon_InboundIgnored` |
| At-logon ResendRequest suppressed on, kept off | `Suppression_KnobOn_NoAtLogonResendRequest_KnobOff_FatalOnTooHigh` |
| Reset cause table (1 / 2 / 1) | `Reset_InitiatorResetLogon_Advertises1`, `Reset_AcceptorReplyResetOnLogon_Advertises2`, `Reset_AcceptorReplyReceived141_Advertises1` |
| Walk has a single implementation | `WalkExtraction_SingleImplementation` |
| Walk two-value end (explicit-end-beyond-store / `EndSeqNo=0`-empty-store, both callers) | `WalkExtraction_TwoValueEnd_ExplicitEndBeyondStore`, `WalkExtraction_TwoValueEnd_EndSeqNo0_EmptyStore` |
| No heap on 789 emit append (`build_logon`) | `NoHeap_Emit789Append` (markers bracket the synchronous call; resend no-heap inherited from recovery alloc-guard witness) |
| Live both-role vs QFcpp/QFJ | `tests/interop/happy/hp_fix44_next_expected_test.cpp` (skip-without-counterparty) |

## Not in scope

- **Cross-restart continuity** — 789 reads the in-memory seqnum state; it does NOT persist/rehydrate seqnums across a process restart. That is the parked **025 RefreshOnLogon** dependency (store→manager hydrate-on-open), the G3 capstone.
- **FIXT/5.0SP2 per-version gating** — deferred to G4. 789 is implemented for fixpp's current FIX 4.4 sessions (the field is already in `FIX44.xml`).
