# Quickstart — Reproduce + Validate 032 (Initiator reset_on_logon Outbound Restore)

**Feature**: 032-initiator-reset-outbound-advance | **Date**: 2026-06-11

## Reproduce the bug (in-process, RED on main)

The harm witness is already committed (RED-on-main verified):

```bash
cd research/G19-fix-fpml-iso20022/library
# Build the session unit suite (debug) and run the committed (currently DISABLED) harm test:
ctest --test-dir build/linux-clang-debug -R persistent_seqnum_hydrate -V \
  2>&1 | grep -i "OutboundStaysTwo"   # the test exists at tests/session/test_persistent_seqnum_hydrate.cpp:2158
# To see it RED, temporarily un-prefix DISABLED_ and run:
#   --gtest_filter='*ResetOnLogon_Initiator_PeerAck141_OutboundStaysTwo*'
# Expected RED on main: peek_outbound()==1 (got 1), assertion wants 2.
```

Bug mechanics: a `reset_on_logon=true` initiator emits `Logon(141=Y, 34=1)` (outbound→2); the peer's
`141=Y` Logon-ack echo enters `session.cpp:3185`, `reset_seqnums_to_one_durable` rewinds outbound to
`1`, `030` restores inbound to `2` but leaves outbound at `1` → next send duplicates `34=1`.

## Validate the fix (in-process)

After implementing per `contracts/initiator-reset-echo.md`:

```bash
# W1: enable + EXTEND the harm test → GREEN (Active + peek_outbound()==2 AND originate a post-Active
#     frame, capture bytes, assert wire 34=2 with no second frame at 34=1 — the counter is a supporting
#     check, NOT the wire proxy [[feedback_witness_asserts_named_postcondition_not_proxy]])
# W2: by_peer_request==false on the reset_on_logon path (gates on the latch own_logon_sent_reset_flag_)
# W7: fresh-no-knob peer-spontaneous-at-1 ⇒ outbound stays 1 (latch false)
# W8: hydrated reset_on_logout {in=37,out=1}, inbound≠1 at emit ⇒ outbound stays 1 (latch false)
# W3/W4 non-regression: peer-spontaneous baseline + BilateralStrict_Initiator_CountersResetToOne unchanged
# 030 pins re-verified on the SAME arm: test_reset_on_lifecycle + test_refresh_on_logon W6 (clean-build FULL ctest)
ctest --test-dir build/linux-clang-debug \
  -R 'persistent_seqnum_hydrate|reset_seqnum_policy_matrix|reset_on_lifecycle|refresh_on_logon' -V
# Expect: all green, including BilateralStrict_Initiator_CountersResetToOne (outbound==1, UNCHANGED),
#   INV_H1_Initiator_PeerAck141_NoOverPersist (no store>manager), and the 030 Initiator_Received141Ack_* pins.
```

Full verify matrix (6 presets, one at a time — [const build cap -j2]): debug + asan + ubsan + tsan +
coverage + gcc-release; coverage ≥95/85 on the new guarded restore branch (both arms) + the widened
`we_initiated` predicate.

## Validate live (SC-003 close-out — the true conformance proof)

In the parent `phase-9-harness`, with QFcpp/QFJ counterparties available:

```bash
cd research/G19-fix-fpml-iso20022/phase-9-harness
# Re-run the previously-deferred initiator reset-on-logon cells:
python3 tools/run_interop_cell.py RL-QFcpp-init-fix44-reset-on-logon --config ...
python3 tools/run_interop_cell.py RL-QFj-init-fix44-reset-on-logon  --config ...
# Expect: session establishes, fixpp sends one post-logon frame at 34=2, peer does NOT
# Logout-reject with "MsgSeqNum too low". Flip from deferred:initiator-141echo-outbound-rebase → pass.
```

(The exact runner invocation + cell registration is finalized at /tasks; the witness must be hardened
past `drive_to_active` — assert a post-logon frame exchange / stay-Active, not merely reaching Active —
per the CP8 lesson, [[feedback_witness_asserts_named_postcondition_not_proxy]].)

## Done when
- W1 (incl. wire `34=2`)/W2 GREEN, W3/W4/W5/W6/W7/W8 GREEN (non-regression + persistence + the two latch
  discriminators), the `030` pins in `test_reset_on_lifecycle.cpp` + `test_refresh_on_logon.cpp` W6 +
  `INV_H1_Initiator_PeerAck141_NoOverPersist` GREEN under a clean-build FULL ctest, full 6-preset verify GREEN.
- `RL-*-init` live cells pass vs QFcpp + QFJ.
- L-024-2 flipped RESOLVED; **S-017 AND S-032** catalogue Notes + coverage-index **§4.4.2** + B-032-1
  updated; obsolete-prose grep-sweep clean.
