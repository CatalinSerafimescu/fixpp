# Quickstart: validating 092-garbled-frame-reject

Run from the tree that owns the feature, on branch `092-garbled-frame-reject`. Builds need the
owner's approval first (`[const §XVII.7]` resource gate).

## 0. Before any production change: the baselines

1. **Benches** (research R-9):
   - Land `bench/session/scan_frame_header_bench.cpp` and the clean Active `on_inbound_frame`
     bench in a bench-only commit, and list both in `bench/ci-suite.txt`.
   - Build a clean merge-base worktree with only the bench commit cherry-picked onto it, and the
     candidate tree, on one machine.
   - Record the paired A-B-A-B result (min-per-tree) with both SHAs and the bench commit's
     patch-id. It cannot be reconstructed after the edit.
2. **RED reproduction**: add #507's T076 reproducer (from the issue body) as a real test and run it.
   - Expected today: cells (b), (c), (e) and (f) all show `probe(34=500): resend=0` (reset applied).
3. **Parse-arena ceiling** (research R-4): find the smallest field count at which a well-formed
   NewOrderSingle fails `Parser<Index>::parse` in the inbound arena, on the Linux presets the cells
   run on. Record it with the SHA. The late-site cells (contract C-6) use a frame above it. The
   per-lane measurement is fixpp#515's.
4. **C-ABI preconditions** (plan Phase 0b):
   - `gh release list --exclude-drafts` is empty;
   - no ref defines MINOR 10 yet.

## 1. The behaviour cells (contract C-2, C-3, C-5, C-6)

Select tests by label, never `-R` (`[const §VII.8]`). The new cells live in
`tests/session/unparseable_frame_disposition_test.cpp`, registered beside
`session_validate_gate_inbound` with a `092` label (`ctest -L 092`).

**Every Reject cell asserts the exact 373 and 371 and the RefSeqNum.**

Expected after the change:
- **T076 flipped**: all four framed-but-unparseable cells draw a Reject for the SequenceReset
  (45=2, 372=4, and 373=0 with no 371 or 373=5 with 371 = the Length tag). The probe at 500 draws a
  ResendRequest (SC-001).
- **D-5 advance witness**: a faulty application message at N, then a conformant one at N+1. The
  second is delivered with no ResendRequest (SC-003). Repeated for Reject(3) and Logout(5) as the
  faulty frame (the no-reject-loop supersession), and with `validate_sequence_numbers` off (FR-011).
  With the knob off, delivery alone cannot witness the advance: a too-high frame is delivered
  without advance and without a ResendRequest (Guard 4's knob-off S4 path). So the knob-off arm also
  asserts the inbound counter directly through `SeqnumManager::next_inbound_unsafe()` (as
  `tests/session/test_validation_compat_toggles.cpp` reads it): N+1 after the faulty frame and N+2
  after the conformant one. It goes RED in a scratch copy when the disposer skips
  `consume_rejected_seqnum_`.
- **D-5 persistence** (FR-013, #423 precedent): a `092 disposer (D-5)` case, a faulty application
  frame at seq 2 (field 3 is 35, a malformed tag after 34), is added to both
  `PersistentSeqnumHydrate.RejectedInSequence_AdvanceIsPersisted` and
  `PersistentSeqnumHydrate.RejectedInSequence_PersistFailure_Fatal` in
  `tests/session/test_persistent_seqnum_hydrate.cpp`, labelled by `Case::site` like its siblings.
  - `RejectedInSequence_AdvanceIsPersisted`: the session stays Active, a Reject is sent, and
    `FaultStore::durable_inbound` is 3.
  - `RejectedInSequence_PersistFailure_Fatal`: the state is `fsm_state::Disconnected`,
    `FaultStore::durable_inbound` stays 2, and `fix->capture.frames` does not grow (no Reject),
    which also proves the persist precedes the Reject.
  - RED proofs, each in a scratch copy: a disposer that skips `consume_rejected_seqnum_` turns
    `RejectedInSequence_AdvanceIsPersisted` RED; one that emits the Reject before it, or discards
    its error, turns `RejectedInSequence_PersistFailure_Fatal` RED. Outbound store-before-emit
    needs no new case: it is `emit_session_reject_`'s unchanged body.
- **D-4 no-advance witness**: the same pair with a faulty SequenceReset at N. The next message draws
  a ResendRequest. With `validate_sequence_numbers` off, the cell pins the inherited outcome: the
  counter stays at N, asserted directly as `next_inbound_unsafe() == N`, and later frames are
  delivered without advancing (research R-4).
- **D-6**: faulty frames at too-low and too-high 34, each with and without PossDupFlag=Y. Each gets a
  Reject, no advance, no ResendRequest and no too-low Logout.
- **D-7**: a fault before 34, and a `34=abc` before the fault. Expected: no outbound message,
  NextNumIn unchanged, the session stays Active, and the next valid message draws a ResendRequest
  (SC-005).
- **D-8**: the mixed defect `8|9|49=…|35=D|34=N|9x9=1|…|10`, and a frame whose third field is the
  malformed one. Each is disregarded in Active and in LogonReceived. The same frames in
  NotConnected and in LogonSent are refused, as today, and the cell asserts that the session ends
  (contract C-2; the pre-Active disregard is fixpp#514).
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
- **Duplicates** (I-3, E-1): `34=99|35=D|34=2|9x9=1` at expected 2 is D-6 (the Reject is addressed
  from `fault_ref_seq_num`, 99). A fault-free NewOrderSingle `34=1|…|34=5` at expected 5 is
  delivered, as today (last-wins kept). A faulty `…|35=D|34=N|35=4|9x9=1|…` at expected N is D-5,
  decided on `fault_ref_msg_type` (D), never the last-wins 4: NextNumIn advances, the Reject carries
  372=D, and N+1 is then delivered with no ResendRequest.
- **MaxMessageSize** (C-1 step 1b): an oversized faulty frame in Active ends in Disconnected.
- **Liveness** (FR-018): a D-5 frame in Active refreshes inbound liveness, and a D-7 frame does not.
- **372 bound** (R-5): a faulty frame with an over-long MsgType draws a Reject without 372, and the
  number is not silently consumed.
- **Reject-loop bound** (contract C-2): a scripted peer answers each fixpp Reject with a malformed
  Reject. The number of fixpp Rejects equals the number of malformed frames the peer sent, and
  fixpp originates none in reply to a well-formed Reject.
- **Late sites** (C-6, SC-008): one cell per late inbound parse site that contract C-6's command
  derives, with a well-formed frame above the measured ceiling. Each asserts that the session
  closes, no Reject is sent and the parse target's receive callback (`fromAdmin`/`fromApp`) is not
  invoked; `onLogout` from the close and callbacks fired earlier at the site (e.g. `toAdmin` for the
  confirming Logout) are not counted. It goes RED when that site's close is deleted. No cell
  asserts that an effect taken before the site's parse (for example the Logout reply) is absent.
  Each cell then reconnects, over a persistent store that survives the close, and asserts whether
  NextNumIn includes the closed-on frame: the peer's resend is requested, or the number was
  consumed. That pins today's per-site outcome (C-5 L-6).
- **Replay** (R-12): an **admin** frame with a fault in field 2, stored through a custom
  MessageStore, is gap-filled on resend and not resent. RED against stop-first without the guard.
- **C mapping for a tag above 0xFFFF** (data-model E-6, R2-008): `translate` gives
  `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` for `OffsetTable::build`'s `wire_tag_out_of_range`, and
  `FIXPP_ERR_WIRE_INVALID_FRAME` for the validator's `wire_invalid_tag_number` on the same bytes.
- **Disclosed outcomes** (C-5):
  - **L-1**: a replaying peer and a deterministically malformed frame faulty before 34. The observed
    resend loop is pinned.
  - **L-2**: a faulty GapFill during AwaitingResend. The disconnect sequence is pinned.
  - **L-4**: a malformed 93/89 pair draws 373=5.
  - **L-6**: covered by the late-site cells above.
- **C-ABI** (FR-017, SC-004, SC-009):
  - the `tests/capi` malformed-tag Logon refusal on every observer, both roles;
  - `version_test.cpp` at 1.10.

## 2. The instruments (each must be shown able to fail)

- **Differential** (C-3 I-4): run the scan-vs-`OffsetTable` mutation corpus and its accepted
  controls. Every seed is asserted clean before mutation. Then, in a scratch copy, seed one
  disagreement **per mutation family** (non-digit, overflow, empty tag, no `=`, non-SOH,
  end-equals-size, the `fault_ref_seq_num` first-34 selection, the `fault_ref_msg_type` third-field
  selection) and confirm that family goes RED.
  Header values are compared against `entries()`, never `find(34)`.
- **Mechanism deletion** (SC-006): in a scratch copy, delete the inline fault branch in one state arm
  and confirm that arm's cells go RED. Repeat with the late-site close (C-6) also deleted, so a
  refusal cell kept green by a late-site close is exposed.
- **Fuzz**: `fuzz_session_recovery_admin_parse` with the equivalence arm (it also compares
  `msg_type_is_third` and `fault_ref_msg_type` with `entries()`), for the Article VII §7 time
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
