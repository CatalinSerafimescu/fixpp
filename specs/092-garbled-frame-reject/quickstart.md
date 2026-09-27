# Quickstart: validating 092-garbled-frame-reject

Run from the tree that owns the feature, on branch `092-garbled-frame-reject`. Builds need the
owner's approval first (`[const §XVII.7]` resource gate).

## 0. Before any code change: the baselines

1. **Bench baseline** (research R-9): add `bench/session/scan_frame_header_bench.cpp` and run it
   against the **unchanged** scan. Record the numbers with the commit SHA. It cannot be
   reconstructed afterwards.
2. **RED reproduction**: add #507's T076 reproducer (from the issue body) as a scratch test and run
   it. Expected today: cells (b), (c), (e) and (f) all show `probe(34=500): resend=0` (reset applied).

## 1. The behaviour cells (contract C-2, one per row D-1…D-9)

Select tests by label, never `-R` (`[const §VII.8]`). The new cells live in
`tests/session/unparseable_frame_disposition_test.cpp`, registered beside `session_validate_gate_inbound`
with a `092` label (`ctest -L 092`).

Expected after the change:
- **T076 flipped**: all four garbled cells draw a Reject for the SequenceReset (45=2, 372=4, 373=0
  or 5), and the probe at 500 draws a ResendRequest (SC-001).
- **D-5 advance witness**: a faulty app message at N, then a conformant one at N+1. The second is
  delivered with no ResendRequest (SC-003).
- **D-4 no-advance witness**: the same pair with a faulty SequenceReset at N. The next message draws
  a ResendRequest.
- **D-7 / D-8**: fault before 34, or before 35 → no outbound message, NextNumIn unchanged, the
  session stays Active, and the next valid message draws a ResendRequest (SC-005).
- **D-1 / D-2 / D-3**: a Logon with a malformed tag is refused as acceptor and as initiator, on
  FIX.4.2, FIX.4.4 and FIXT.1.1 (SC-004).
- **D-9**: in LogoutSent, a faulty Logout is not taken as the reply; the logout timeout ends the
  session.

## 2. The instruments (each must be shown able to fail)

- **Differential** (C-3 I-4): run the scan-vs-OffsetTable mutation corpus. Then seed one
  disagreement in a scratch copy (e.g. drop the scan's non-SOH check) and confirm it goes RED.
- **Mechanism deletion** (SC-006): in a scratch copy, delete the `dispose_unparseable_` call in one
  state arm and confirm that arm's cells go RED, including the cell that passes if a fault is
  treated as "no reject".
- **Fuzz**: `fuzz_session_recovery_admin_parse` with the equivalence assertion, for the Article VII
  §7 time (≥ 10 min). Plant a disagreement once to prove the trap fires.
- **Scripted peer** (SC-007): the in-process peer sends one raw malformed application frame, then
  keeps sending. It handles our Reject as QuickFIX's `nextReject` does. Every later message must be
  delivered.

## 3. Regression and cost

- The full session suite, the C-ABI suite and `pytest bindings/python/tests/` pass unchanged,
  except the tests research R-10 lists as intentionally updated.
- Re-run `scan_frame_header_bench` and compare it with step 0.
- `/speckit-verify` runs the sanitizer and coverage matrix, as usual.
