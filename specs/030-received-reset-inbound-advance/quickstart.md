# Quickstart: Received-Reset Inbound Advance Correction

## Reproduce the harm (RED)

Unit (deterministic, no counterparty):

```
ctest --test-dir build/<preset> -R session_reset_on_lifecycle
# RED before fix: Received141_PeerNextMsgSeq2_HarmCheck fails — fixpp emits a
# ResendRequest when the peer sends its first post-reset message at seq 2.
```

The mechanism: a peer `Logon(34=1, 141=Y)` into an acceptor with `reset_on_logon=false`
leaves next-expected-inbound at 1 (the reset clobbered the consumed-Logon advance), so the
peer's seq-2 message reads too-high.

## Verify the fix (GREEN)

1. **Discriminating triple** (027 advertisement on), the single witness that proves it:
   - `next_inbound == 2` (counter restored)
   - `reply.MsgSeqNum == 1` (outbound rebase intact — byte-identical)
   - `reply.789 == 2` (advertisement corrected)

2. **Harm closed**: `Received141_PeerNextMsgSeq2_HarmCheck` — peer seq-2 accepted
   in-sequence, **no** ResendRequest, next_inbound advances to 3.

3. **INV-H1 held**: durable store `== seqnum_min` (1) **AND** manager `== 2` (assert the
   store value *directly*, not via a manager proxy).

4. **Guard**: a path with no consumed in-sequence reset Logon does NOT fire the restore.

5. **Unchanged**: `reset_on_logon=true` knob path byte-identical; policy matrix
   (bilateral-strict/lenient, unilateral) and non-persistent store hold.

## Cross-feature pin updates (must flip with the fix)

```
grep -rn "AcceptorReplyReceived141_Advertises1\|InboundSeedWithheld\|Acceptor_CountersResetToOne" tests/session/
# Confirm 5 pins, flip each next_inbound 1→2 / 789 1→2, re-read each to certify it pins THIS case.
```

## Close-out (the real proof — SC-001)

Re-run the live acceptor interop cell against **both** QuickFIX-cpp and QuickFIX-J: a peer
`141=Y` reset must reach Active with **zero** fixpp ResendRequest and the peer's seq-2
message accepted. This is the defect's birthplace (a failed live cell) and its true closure.
