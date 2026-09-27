# Quickstart: validating 092-garbled-frame-reject

Run from the tree that owns the feature, on branch `092-garbled-frame-reject`. Builds need the
owner's approval first (`[const §XVII.7]` resource gate).

## 0. Before any production change: the baselines

1. **Benches** (research R-9):
   - Land `bench/session/scan_frame_header_bench.cpp` and the clean Active `on_inbound_frame`
     bench in a bench-only commit, and list both in `bench/ci-suite.txt`.
   - Build that commit in a clean merge-base worktree and in the candidate tree, on one machine.
   - Record the paired A-B-A-B result (min-per-tree) with both SHAs. It cannot be reconstructed
     after the edit.
2. **RED reproduction**: add #507's T076 reproducer (from the issue body) as a real test and run it.
   - Expected today: cells (b), (c), (e) and (f) all show `probe(34=500): resend=0` (reset applied).
3. **Parse-arena ceiling** (research R-4): find the smallest field count at which a well-formed
   NewOrderSingle fails `Parser<Index>::parse` in the inbound arena, on the release and debug
   presets. Record it with the SHA. The residual cells use a frame above it.
4. **C-ABI preconditions** (plan Phase 0b):
   - `gh release list --exclude-drafts` is empty;
   - no ref defines MINOR 10 yet.

## 1. The behaviour cells (contract C-2, C-2b, C-3, C-5)

Select tests by label, never `-R` (`[const §VII.8]`). The new cells live in
`tests/session/unparseable_frame_disposition_test.cpp`, registered beside
`session_validate_gate_inbound` with a `092` label (`ctest -L 092`).

**Every Reject cell asserts the exact 373 and 371 and the RefSeqNum.** A residual Reject (373=3, no
371) must not satisfy a C-2 cell (SC-006).

Expected after the change:
- **T076 flipped**: all four framed-but-unparseable cells draw a Reject for the SequenceReset
  (45=2, 372=4, and 373=0 with no 371 or 373=5 with 371 = the Length tag). The probe at 500 draws a
  ResendRequest (SC-001).
- **D-5 advance witness**: a faulty application message at N, then a conformant one at N+1. The
  second is delivered with no ResendRequest (SC-003). Repeated for Reject(3) and Logout(5) as the
  faulty frame (the no-reject-loop supersession), and with `validate_sequence_numbers` off (FR-011).
- **D-4 no-advance witness**: the same pair with a faulty SequenceReset at N. The next message draws
  a ResendRequest. With `validate_sequence_numbers` off, the cell pins the inherited outcome: the
  counter stays at N and later frames are delivered without advancing (research R-4).
- **D-6**: faulty frames at too-low and too-high 34, each with and without PossDupFlag=Y. Each gets a
  Reject, no advance, no ResendRequest and no too-low Logout.
- **D-7**: a fault before 34, and a `34=abc` before the fault. Expected: no outbound message,
  NextNumIn unchanged, the session stays Active, and the next valid message draws a ResendRequest
  (SC-005).
- **D-8**: the mixed defect `8|9|49=…|35=D|34=N|9x9=1|…|10`, and a frame whose third field is the
  malformed one. Each is disregarded in Active and in LogonReceived. The same frames in
  NotConnected and in LogonSent are refused, and the cell asserts that the session ends (no
  pre-Active timer would end a disregarded one, contract C-2).
- **D-1 / D-2**: a Logon with a malformed **tag** (a malformed count is already refused by 091 and
  cannot go RED) is refused as acceptor and as initiator, on FIX.4.2, FIX.4.4 and FIXT.1.1 (SC-004).
  A faulty non-Logon in NotConnected and in LogonSent is refused too.
- **D-3**: a faulty Logon (third field 35, 34 read) while Active, and while LogonReceived, ends in a
  silent Disconnected, with no Reject and no Logout.
- **D-9**: in LogoutSent, a faulty **Logout** is not taken as the reply, and the logout timeout ends
  the session. A faulty non-Logout is already drained today and is not a RED witness.
- **LogonReceived** gets its own cells for D-4 to D-7. It skips the Active-only block, so it cannot
  share Active's cells.
- **AwaitingResend**: a faulty in-sequence application message that fills the gap closes it (D-5
  through `consume_rejected_seqnum_`). The faulty GapFill case is C-5 L-2 below.
- **Duplicates** (I-3, I-4, C-5 L-5): `34=99|35=D|34=2|9x9=1` at expected 2 is D-6 (first 34 wins).
  A fault-free NewOrderSingle `34=1|…|34=5` at expected 5 is now too-low and ends in Disconnected,
  where it was delivered. The same shape as a Heartbeat is ignored.
- **MaxMessageSize** (C-1 step 1b): an oversized faulty frame in Active ends in Disconnected.
- **Liveness** (FR-018): a D-5 frame in Active refreshes inbound liveness, and a D-7 frame does not.
- **372 bound** (R-5): a faulty frame with an over-long MsgType draws a Reject without 372, and the
  number is not silently consumed.
- **Reject-loop bound** (contract C-2): a scripted peer answers each fixpp Reject with a malformed
  Reject. The number of fixpp Rejects equals the number of malformed frames the peer sent, and
  fixpp originates none in reply to a well-formed Reject.
- **Residual sites** (C-2b, SC-008): one cell per inbound parse site, with a well-formed frame above
  the measured ceiling. Each asserts:
  - 373=3 and no 371;
  - persist before Reject at the post-Guard-4 sites;
  - no handler effect (no Heartbeat, no Logout reply, no NewSeqNo applied);
  - refusal at the pre-Active validate gates.

  The knob-off GapFill cell asserts the recorded deviation (+1 kept, NewSeqNo not applied).
- **Masking and replay** (R-12): a stored frame with a fault after its 35 still classifies as it
  did.
- **Disclosed outcomes** (C-5):
  - **L-1**: a replaying peer and a deterministically malformed frame faulty before 34. The observed
    resend loop is pinned.
  - **L-2**: a faulty GapFill during AwaitingResend. The disconnect sequence is pinned.
  - **L-3**: a well-formed Heartbeat above the ceiling, in Active. It is processed with no
    Application registered, and Rejected with 373=3 with one.
  - **L-4**: a malformed 93/89 pair draws 373=5.
- **C-ABI** (FR-017, SC-004, SC-009):
  - the `tests/capi` malformed-tag Logon refusal on every observer, both roles;
  - `version_test.cpp` at 1.10.

## 2. The instruments (each must be shown able to fail)

- **Differential** (C-3 I-4): run the scan-vs-`OffsetTable` mutation corpus and its accepted
  controls. Every seed is asserted clean before mutation. Then, in a scratch copy, seed one
  disagreement **per mutation family** (non-digit, overflow, empty tag, no `=`, non-SOH,
  end-equals-size, first-wins) and confirm that family goes RED.
- **Mechanism deletion** (SC-006): in a scratch copy, delete the inline fault branch in one state arm
  and confirm that arm's cells go RED. Repeat with the residual path also deleted, so a cell kept
  green by a residual Reject is exposed.
- **Fuzz**: `fuzz_session_recovery_admin_parse` with the equivalence arm, for the Article VII §7 time
  (≥ 10 min). Report the skipped-resource-status count. Plant a disagreement once to prove the trap
  fires.
- **Scripted peer** (SC-007): the in-process peer sends one raw malformed application frame at a
  too-high MsgSeqNum, handles our Reject as QuickFIX's `nextReject` does, and answers each
  ResendRequest by replaying stored bytes with 43=Y and 122 added. The resend must converge, and
  every later message must be delivered.
- **Version mutant** (Phase 0b): MINOR back to 9 turns `version_test` RED.

## 3. Regression and cost

- The full session suite and `pytest bindings/python/tests/` pass, except the tests research R-10
  and R-7 list as intentionally updated.
- **The C-ABI suite does not pass unchanged.** The error-enumeration pins (R-7), the version pin and
  the freeze manifest move in this PR, each RED first where a RED is meaningful.
- Re-run the paired bench (step 0.1) against the candidate head. The budget is +5%
  (`[const §VIII.2]`).
- `/speckit-verify` runs the sanitizer and coverage matrix, as usual.
