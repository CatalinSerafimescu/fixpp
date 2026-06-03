# Contract: Parent-Harness Admin Obligations

**Feature**: `018-interop-live-admin` | **Date**: 2026-06-03
Defines what the gitignored parent `phase-9-harness/` MUST provide for the G1 cells. The submodule depends on these obligations but does **not** vendor the orchestration (mirrors the 016 `parent-harness-gate-contract.md` boundary; `[const §XV.18]`).

## Obligations

1. **Counterparty drive (QuickFIX-J 3.0.1)**: launch QFJ over `one_way_ca` TLS as initiator AND acceptor (both fixpp roles, FR-005a); configure `HeartBtInt(108)=1`s for the cadence cell (US2). Bring the counterparty up FIRST (R5 rendezvous, 016 R5).
2. **TestRequest origination (US1)**: for the **fixpp→peer** leg, hold the link inbound-silent so fixpp's liveness loop emits a `TestRequest` with an **engine-chosen** `112` (the fixture does NOT choose the id; there is no admin-originate API); capture QFJ's answering `Heartbeat` and correlate by the **observed** `112`. For the **peer→fixpp** leg, drive QFJ to emit a `TestRequest(112=)` so fixpp's Heartbeat echo can be captured. The parent captures both legs.
3. **Inbound gap induction (US3.i / `recovery_inbound`)**: **pinned** mechanism = drop/withhold one QFJ→fixpp frame at the passthrough layer so fixpp sees `MsgSeqNum > expected` and emits a `ResendRequest`. Do NOT also use a QFJ send-seqnum skip in this cell (it would answer with GapFill instead of replay → a different golden; New-3).
4. **Outbound-answer trigger (US3.ii / FR-004a / `recovery_outbound`)**: drive QFJ to issue a `ResendRequest` against fixpp via QFJ restart/reconnect expecting a lower inbound sequence (standard QFJ resend-on-logon). The concrete choreography is pinned in the `recovery_outbound` table below.
5. **Malformed-admin injection (US4, both directions)**: the parent **corrupts an admin frame in flight at the proxy layer** (a controlled-invalid tag value) — fixpp never originates malformed bytes (New-2). For the **fixpp-rejects** direction, corrupt a QFJ→fixpp frame so fixpp emits `Reject(35=3)` per `[FIX-SL §4.5.4]`; for the **peer-rejects** direction, corrupt a fixpp→QFJ frame so QFJ rejects. Select an input that elicits a Reject (not a disconnect) for the positive cell (R8).
6. **Enriched-golden capture**: capture the `toAdmin`/`fromAdmin` engine-log transcript per cell at first paired run; write `happy/golden/<cell_id>.fix` (no MITM, no hand-fabrication).
7. **Cell-result emission**: emit `cell_results.yaml` rows for the G1 cells through the **same** in-repo `interop_cell_results_schema_check` rules; integrate at **release-prep tier** (not per-PR). A live `skip`/`fail` ⇒ badge-ineligible.
8. **Sanitizer pass**: run the in-repo interop ctest under ASan/UBSan/TSan against live QFJ admin traffic once (fixpp-only instrumentation) — this discharges the orthogonal 016 `/speckit-verify` YELLOW waiver.

## `recovery_outbound` choreography (FR-004a, both roles)

Concrete preconditions so a QFJ-issued `ResendRequest` against fixpp is deterministic. G1 exchanges no application messages before the gap, so the withheld range is admin-only → fixpp answers **predominantly (often exclusively) with `SequenceReset-GapFill(35=4,123=Y)`**; the `(43=Y,122=)` replay arm is the conditional/optional shape, not a required assertion (R8). The spec accepts either (FR-004a).

| Aspect | fixpp-acceptor (QFJ initiator) | fixpp-initiator (QFJ acceptor) |
|---|---|---|
| Initial QFJ store | **preserved** across the reconnect (NOT wiped) — QFJ remembers a higher expected inbound seqnum | **preserved** likewise |
| `ResetOnLogon` | **`N` (disabled)** — a Logon with `ResetOnLogon=Y` resets seqnums and issues **no** `ResendRequest`, defeating the scenario | **`N` (disabled)**, same reason |
| Trigger | QFJ (as initiator) reconnects/relogs at a lower outbound seqnum than fixpp's expected, so QFJ detects the gap on fixpp's higher inbound and issues `ResendRequest(7=BeginSeqNo,16=EndSeqNo)` | QFJ (as acceptor) on reconnect detects fixpp's higher seqnum and issues the `ResendRequest`; fixpp (initiator) reconnects to it |
| Expected `7/16` | `BeginSeqNo(7)` = QFJ's last-acked+1; `EndSeqNo(16)` = fixpp's next-1 (or `0` = "through infinity" per `[FIX-SL §4.8.2]`) | same shape; values per the captured golden |
| Accepted fixpp reply | `SequenceReset-GapFill(35=4,123=Y)` over the admin range (baseline) **and/or** replay `(43=Y,122=)` if any app message is in range | identical |
| Resync proof | QFJ returns to `Active`, fixpp returns to `Active`, heartbeats resume; golden shows fixpp's reply frames | identical |

The exact `7/16` values are captured at first paired run (never hand-fabricated) and matched verbatim under the `{52,10}` admin profile.

## Non-obligations (stay in submodule)

- The in-process FSM/seqnum witnesses, the golden-diff assertion utility, the scenario descriptors, MATRIX.md, and the checked-in goldens live under `tests/interop/` (the committed deliverable).

## Degradation

- When QFJ is unavailable, every G1 cell resolves `skip:counterparty-unavailable` (FR-009); the parent gate records the skip with reason and the cell is excluded from the green tally (never silent-pass).
