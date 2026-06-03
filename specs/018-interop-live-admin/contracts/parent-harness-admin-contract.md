# Contract: Parent-Harness Admin Obligations

**Feature**: `018-interop-live-admin` | **Date**: 2026-06-03
Defines what the gitignored parent `phase-9-harness/` MUST provide for the G1 cells. The submodule depends on these obligations but does **not** vendor the orchestration (mirrors the 016 `parent-harness-gate-contract.md` boundary; `[const §XV.18]`).

## Obligations

1. **Counterparty drive (QuickFIX-J 3.0.1)**: launch QFJ over `one_way_ca` TLS as initiator AND acceptor (both fixpp roles, FR-005a); configure `HeartBtInt(108)=1`s for the cadence cell (US2). Bring the counterparty up FIRST (R5 rendezvous, 016 R5).
2. **TestRequest origination (US1, peer→fixpp leg)**: drive QFJ to emit a `TestRequest(112=)` so fixpp's Heartbeat echo can be captured. (fixpp→peer leg is driven indirectly via inbound-silence; the parent captures both.)
3. **Inbound gap induction (US3.i)**: drop/withhold a QFJ→fixpp frame at the passthrough layer (or drive QFJ to skip a sequence) so fixpp sees `MsgSeqNum > expected` and emits a `ResendRequest`.
4. **Outbound-answer trigger (US3.ii / FR-004a)**: drive QFJ to issue a `ResendRequest` against fixpp — preferred: QFJ restart/reconnect expecting a lower inbound sequence (standard QFJ resend-on-logon), needing no bespoke code.
5. **Malformed-admin injection (US4)**: inject a controlled-invalid admin frame toward fixpp at the proxy so fixpp emits `Reject(35=3)` per `[FIX-SL §4.5.4]`; select an input that elicits a Reject (not a disconnect) for the positive cell (R8).
6. **Enriched-golden capture**: capture the `toAdmin`/`fromAdmin` engine-log transcript per cell at first paired run; write `happy/golden/<cell_id>.fix` (no MITM, no hand-fabrication).
7. **Cell-result emission**: emit `cell_results.yaml` rows for the G1 cells through the **same** in-repo `interop_cell_results_schema_check` rules; integrate at **release-prep tier** (not per-PR). A live `skip`/`fail` ⇒ badge-ineligible.
8. **Sanitizer pass**: run the in-repo interop ctest under ASan/UBSan/TSan against live QFJ admin traffic once (fixpp-only instrumentation) — this discharges the orthogonal 016 `/speckit-verify` YELLOW waiver.

## Non-obligations (stay in submodule)

- The in-process FSM/seqnum witnesses, the golden-diff assertion utility, the scenario descriptors, MATRIX.md, and the checked-in goldens live under `tests/interop/` (the committed deliverable).

## Degradation

- When QFJ is unavailable, every G1 cell resolves `skip:counterparty-unavailable` (FR-009); the parent gate records the skip with reason and the cell is excluded from the green tally (never silent-pass).
