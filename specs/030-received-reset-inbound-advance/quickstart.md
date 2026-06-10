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

1. **Acceptor discriminating triple** (027 advertisement on), the witness that proves the
   acceptor arm:
   - `next_inbound == 2` (counter restored)
   - `reply.MsgSeqNum == 1` (outbound rebase intact — byte-identical)
   - `reply.789 == 2` (advertisement corrected)

1b. **Initiator witness** (the separate `peer_ack_sent_reset_flag` Logon-ack arm — reset at
   `session.cpp:3162`, swallow at `:3167-3169`, consolidated onto
   `reset_seqnums_to_one_durable(scoped-fatal)`): `next_inbound == 2` after consuming the `141=Y`
   ack Logon + harm-repro. No `reply.789` clause — the initiator builds no reply Logon on this arm.

2. **Harm closed**: acceptor `Received141_PeerNextMsgSeq2_HarmCheck` — peer seq-2 accepted
   in-sequence, **no** ResendRequest, next_inbound advances to 3. The initiator arm has its own
   harm-repro witness (per 1b) with the same assertions.

3. **INV-H1 held (persistent store)**: durable store `== 2` **AND** manager `== 2`
   (`store == manager == 2`, equality — the consumed reset Logon is a surviving net-advance, NOT
   029 over-persist). Assert the store value *directly*, not via a manager proxy. The equality is
   **guaranteed** because the persistent-store durable reset is **fatal** (persist-to-2 runs only
   after a known-good reset; it never advances a stale store). *(A manager-only restore leaving
   the store at 1 is WRONG — fatal-disconnect-on-restart, no ResendRequest arm on the Logon gate.)*

4. **Fault-injection (FR-010 soundness proof, both arms)**: persistent store + `fail_next_reset()`
   on the received-141 path ⇒ session **Disconnected** + store error propagated, persist-to-2 NOT
   reached, **no `store > manager`** ever observable. (Without the fatal flip, persist-to-2 would
   advance a stale store → `store > manager` → silent inbound skip on restart, the 029 harm.)

5. **Guard**: a path with no consumed in-sequence reset Logon does NOT fire the restore+persist.

6. **Unchanged**: `reset_on_logon=true` knob path byte-identical; policy matrix
   (bilateral-strict/lenient, unilateral) hold; non-persistent store holds — its durable
   write-through no-ops AND a received-141 reset failure does NOT disconnect (the split sibling of
   merged witness (5) — stay-Active retained for non-persistent only).

## Cross-feature pin updates (must flip with the fix)

```
grep -rn "AcceptorReplyReceived141_Advertises1\|InboundSeedWithheld\|_CountersResetToOne" tests/session/
# Confirm the 6 VALUE-pins (the broadened _CountersResetToOne needle now also catches the INITIATOR cell
# BilateralStrict_Initiator_CountersResetToOne :593-594), flip each next_inbound 1→2 / 789 1→2, re-read
# each to certify it pins THIS case.
grep -rn "ResetOnLogon_Off_Inbound141_StoreFailure_StillActive" tests/session/
# The 7th pin (DIFFERENT category — a contract behavior, not a value): split this merged 024 witness —
# persistent variant (default factory) flips to assert Disconnect; add a NEW non-persistent
# (yields_persistent_store()==false) sibling that retains stay-Active. (6 value-pins + 1 contract split = 7.)
```

## Close-out (the real proof — SC-001)

Re-run the live acceptor interop cell against **both** QuickFIX-cpp and QuickFIX-J: a peer
`141=Y` reset must reach Active with **zero** fixpp ResendRequest and the peer's seq-2
message accepted. This is the defect's birthplace (a failed live cell) and its true closure.
