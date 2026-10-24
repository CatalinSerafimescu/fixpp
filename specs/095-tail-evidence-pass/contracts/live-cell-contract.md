# Contract — live FIXT cell (FR-009..FR-016)

**L0 — what the harness enforces.** Every row below names the code that enforces it today (parent
`phase-9-harness/tools/`, fixpp `ci/`); the implementer re-reads each symbol before relying on it,
and each branch marked *test* gets a unit test in the parent harness's test module.

| Concern | Required value / behaviour | Enforced by |
|---|---|---|
| Combo | `combo_id="C1"` on the QF-cpp cell, `"C3"` on the QFJ cell: 089's fixpp-initiator combos (`_build_cells`, `CONV-C1-*` / `CONV-C3-*`), reused as `CONV-PROBE-*` reuses C1 | `build_conversation_meta` raises "sets script_path but not combo_id" |
| Script steps | `fixt_app_script.yaml` step F-01 lists `applicable_combos: [C1, C3]` | `render_intent_file`, `expected_peer_rejects` filter steps by it |
| Arm | `arm="validation-off"` (the `Cell` default), FR-013 | `build_conversation_meta` → hello |
| `run_kind` | `"app-message-evidence"`, distinct from `"conformance"` **and** from `"validator-positive-control"`, so `run_live_matrix`'s sibling lookup never pairs the new cells with the probes | `Cell.run_kind` |
| Select | Selected through the template row its id declares in `tests/interop/cell_results.yaml` (research R-11), **not** through `select_cells`' probe rule (scripted, non-conformance, combo declared). The probe rule is narrowed to `"validator-positive-control"`. Without the narrowing, the probe rule would admit the two cells through combos C1/C3 even with no template row, so `RegistryAgreesWithTheRealTemplateTest` could never report them missing. *test*: with `declared_cell_ids` stubbed to omit the two rows (the suite's existing stub idiom, not the real template), `select_cells` raises naming both | `emit_matrix.py::select_cells` |
| Select, `--conversation-only` | `select_cells(conversation_only=True)` returns every scripted cell, so the new cells run there too. Intended; the promotion skip below covers it — *test* | `emit_matrix.py::select_cells` |
| Run | `run_cell` as for any scripted cell; `status` comes from the cell's own pass/fail | `run_interop_cell.py::run_cell` |
| Promote | **Skipped** for `"app-message-evidence"`: `run_live_matrix`'s promotion loop calls `promote_and_record` for every `script_path` cell and flips a withheld promotion to `fail` — *test* that the new cells are neither promoted nor flipped | `emit_matrix.py::run_live_matrix` |
| N2 rows | Unchanged: the producer already keeps only `run_kind == "conformance"` — *test* that the new cells add no row | `emit_matrix.py` N2 producer |
| Dictionary digest | `_finalize`'s FR-011 check compares fixpp's hello `dictionary_digest` with the per-cell expected file: `dictionaries/FIX50SP2.xml` for these cells, FIX44 for every other — *test* both | `run_interop_cell.py::_finalize` |
| Dictionary path | one env var, `FIXPP_FIX50SP2_DICT_XML`, set at the `_run_initiator_cell` launch site next to `FIXPP_FIX44_DICT_XML`, and read by the gtest's fixture (L1); same file on both sides | `_run_initiator_cell` |
| Capability minimums | two entries in `phase-9-harness/tools/interop_capability_minimums.yaml`; a missing entry fails closed | `CAPABILITY_MINIMUMS_PATH` |
| Reconcile | two lines in `tests/interop/expected-skips-without-counterparty.txt`; `ci/run-interop-live.py::reconcile` fails on a stray image filter and on an unaccounted skip id. Against the pre-095 image (the fixpp PR's own `interop-live`), the two ids are unaccounted, so it runs no cell (research R-10) | `reconcile` |
| Image digest | the hello carries `INTEROP_COUNTERPARTY_IMAGE_DIGEST` or the sentinel `local-build:unpinned`; the lane does not set it, so the pulled digest is recorded at close-out (data-model §D) | `build_conversation_meta` |
| Registration lands with its template rows | no parent commit registers the two cells while its gitlink lacks their rows (fixpp#462; owner ruling, Gate A round 2): one parent commit after the fixpp merge carries every harness change and the gitlink bump (research R-10). `RegistryAgreesWithTheRealTemplateTest` holds at every parent commit. *test*: a both-directions pin for the two cells, modelled on `test_the_recovery_inbound_cells_are_registered_AND_declared` (registered in `CELLS` and declared by the real template), because the real-template test checks only registered ⇒ declared | `test_emit_matrix.py::RegistryAgreesWithTheRealTemplateTest` |

**L1 — session.** FIXT.1.1, DefaultApplVerID FIX.5.0SP2 (`1137=9`), TLS as in 033's FIXT cells;
fixpp is the initiator. 033's `make_fixt_engine_config()` is **not** reused: it sits in an anonymous
namespace and registers stub dictionaries. A shared fixture in `tests/interop/happy/hp_support.hpp`
(beside `production_dictionary_and_digest`, which stays FIX44-only) loads the **production**
`FIXT11.xml` and `FIX50SP2.xml`, registers `FIX50SP2.xml` under ApplVerID 9 so `Session::open()`
accepts the FIXT session, and returns the SHA-256 of the `FIX50SP2.xml` bytes it loaded for the hello.
It resolves the file from `FIXPP_FIX50SP2_DICT_XML`, falling back to `FIXPP_DICT_DATA_DIR`, which the
033 target gains through a `target_compile_definitions` after its `fixpp_add_interop_test` call (the
helper has no definitions keyword). 033's own cells are unchanged.
`SessionConfig::validate_inbound_messages` stays off (FR-013).

**L2 — peers.** Two cells: `HP-QFcpp-init-fixt11-fix50sp2-app-bn` (QuickFIX-cpp v1.16.0) and
`HP-QFj-init-fixt11-fix50sp2-app-bn` (QuickFIX/J 3.0.1), each against the existing
`*-acceptor-tls-fixt50sp2.cfg.in` config: `UseDataDictionary=Y`, `TransportDataDictionary` FIXT11,
`AppDataDictionary` FIX50SP2 (QuickFIX/J: the jar's `FIX50SP2.xml` classpath resource).

**L3 — message.** After Logon, fixpp sends one 35=BN built by `fixpp::v50sp2::build_ExecutionAck`
from the step F-01 intent values (research R-7 field set), via `Engine::send`. The MsgSeqNum is
captured in `to_app` for the `sent` record.

**L4 — evidence.** The peer's generic per-field readback of the received BN is compared with
fixpp's `sent` record by `compare_streams` (decimal resolver over `FIX50SP2.xml`). The cell passes
only if exactly one comparison row exists and it passes, no peer Reject was seen, and the session
logged out cleanly. Its outcome row lands in `tests/interop/cell_results.yaml` (`kind: happy`) and
the schema test's `EXPECTED_IDS`. In conversation mode both counterparties' `fromApp` stage-0 branch
looks up `B-02` in the intent file; this script has no such entry, so they log and return
(`send_conversation_message` / `sendConversationMessage`) — expected, not a defect.

**L5 — non-vacuity (recorded at verify):**
- a chosen value altered on the send side after the `sent` record is fixed → RED (comparison
  mismatch);
- a required field (ExecID 17) omitted → the peer rejects → RED (missing readback and/or
  unexpected peer Reject).
Both arms are scratch edits, reverted.

**L6 — ordering (research R-10).** Before the merge, the harness change exists only as uncommitted
edits in the parent working copy, and the cells run locally against both peers (verify record:
commands, outputs, peer builds). After the merge, one parent commit carries the harness change and
the gitlink bump, the orchestrator pushes it, and `publish-counterparties` republishes the image.
Then `interop-live` is dispatched on fixpp `main` and must list both cells as passed; its run id and
pulled digest are recorded at close-out. The fixpp PR's own `interop-live` runs against the pre-095
image, fails its reconcile and runs no cell; it is advisory and is not this cell's evidence.

**L7 — no regression, locally, before the fixpp PR is pushed.** Because the PR's `interop-live`
runs no cell, the local run carries that coverage: 089's CONV-C1..C4 and PROBE cells against the
patched QuickFIX/J counterparty, the existing 033 FIXT cells of the binary 095 changes, and
`ci/run-interop-live.py --harness ../phase-9-harness --list` (the whole-population reconcile against
the local harness, which runs nothing).

**L8 — disclosure.** The B&L delta states that the live cell witnesses one message (BN) from fixpp
to each peer with fixpp's inbound validation off (L-041-2), and that the other 22 tail MsgTypes
close offline.
