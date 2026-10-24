# Contract — live FIXT cell (FR-009..FR-016)

**L1 — session.** FIXT.1.1, DefaultApplVerID FIX.5.0SP2 (`1137=9`), TLS as in 033's FIXT cells;
fixpp is the initiator; the engine config registers the application dictionaries
(`make_fixt_engine_config()` reuse) so `Session::open()` accepts the FIXT session.
`SessionConfig::validate_inbound_messages` stays off (FR-013).

**L2 — peers.** Two cells: `HP-QFcpp-init-fixt11-fix50sp2-app-bn` (QuickFIX-cpp v1.16.0) and
`HP-QFj-init-fixt11-fix50sp2-app-bn` (QuickFIX/J 3.0.1), each against the existing
`*-acceptor-tls-fixt50sp2.cfg.in` config: `UseDataDictionary=Y`, `TransportDataDictionary` FIXT11,
`AppDataDictionary` FIX50SP2.

**L3 — message.** After Logon, fixpp sends one 35=BN built by `fixpp::v50sp2::build_ExecutionAck`
from the step F-01 intent values (research R-7 field set), via `Engine::send`. The MsgSeqNum is
captured in `to_app` for the `sent` record.

**L4 — evidence.** The peer's generic per-field readback of the received BN is compared with
fixpp's `sent` record by `compare_streams` (decimal resolver over `FIX50SP2.xml`). The cell passes
only if exactly one comparison row exists and it passes, no peer Reject was seen, and the session
logged out cleanly. Its outcome row lands in `tests/interop/cell_results.yaml` (`kind: happy`) and
the schema test's `EXPECTED_IDS`.

**L5 — non-vacuity (recorded at verify):**
- a chosen value altered on the send side after the `sent` record is fixed → RED (comparison
  mismatch);
- a required field (ExecID 17) omitted → the peer rejects → RED (missing readback and/or
  unexpected peer Reject).
Both arms are scratch edits, reverted.

**L6 — ordering.** The parent harness change is on parent `main` and the counterparty image is
republished before the fixpp branch carrying L1..L4 is pushed; the PR's `interop-live` run is the
CI evidence, and its pulled image digest is recorded in the verify record.

**L7 — no regression on 089.** 089's CONV-C1..C4 and PROBE cells pass against the patched QuickFIX/J
counterparty, locally, before the parent push.

**L8 — disclosure.** The B&L delta states that the live cell witnesses one message (BN) from fixpp
to each peer with fixpp's inbound validation off (L-041-2), and that the other 22 tail MsgTypes
close offline.
