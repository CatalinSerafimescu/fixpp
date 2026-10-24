# Contract — live FIXT cell (FR-009..FR-016)

**L0 — what the harness enforces.** Every row below names the code that enforces it today (parent
`phase-9-harness/tools/`, fixpp `ci/`); the implementer re-reads each symbol before relying on it,
and each branch marked *test* gets a unit test in the parent harness's test module.

| Concern | Required value / behaviour | Enforced by |
|---|---|---|
| Gtest ids | fixed here, before the cell source exists (T044 registers them first): INSTANTIATE prefix `Fixt`, suite `HappyFixtAppMessage`, test `TypedExecutionAck`, param names `QFcpp_init_fix50sp2` and `QFj_init_fix50sp2` (033's `fixt_cell_name` shape). So the ids are `Fixt/HappyFixtAppMessage.TypedExecutionAck/QFcpp_init_fix50sp2` and `Fixt/HappyFixtAppMessage.TypedExecutionAck/QFj_init_fix50sp2`, used verbatim as the cells' `gtest_filter` and as the two skip-list lines. The built binary's `--gtest_list_tests` must list both exactly once (T051, and again after any edit of the cell source: T079, T091) | `run_interop_cell.py`'s `parse_gtest_status` maps gtest's `did not match any test` warning to `fail`, so a filter that does not match the binary turns the cell RED, never a pass (the vendored GoogleTest prints that warning on a zero-match filter and exits 0); `ci/assert-interop-skips.py`, on every tier's no-counterparty interop step (an unlisted skip or a listed id that does not skip fails). The filter, the skip-list line and the gtest id are one string, so either check catches a cell whose id no longer matches, the post-merge `interop-live` included |
| `Cell` fields | `binary="interop_hp_fixt50sp2_logon_hb_logout_test"` (the 033 FIXT target, research R-6); `gtest_filter` the cell's gtest id above; `config_template` the engine's existing `*-acceptor-tls-fixt50sp2.cfg.in` (`quickfix-cpp-acceptor-tls-fixt50sp2.cfg.in`, `quickfixj-acceptor-tls-fixt50sp2.cfg.in`; no new config); `counterparty`, `role="fixpp-initiator"` and `spec_ref` as the 033 FIXT cells set them; plus `combo_id`, `arm`, `run_kind` and `script_path` (rows below) | `run_interop_cell.py::Cell`, `_build_cells` |
| Combo | `combo_id="C1"` on the QF-cpp cell, `"C3"` on the QFJ cell: 089's fixpp-initiator combos (`_build_cells`, `CONV-C1-*` / `CONV-C3-*`), reused as `CONV-PROBE-*` reuses C1. At T044 the implementer re-derives every reader of `combo_id` (`grep -n combo_id phase-9-harness/tools/*.py`, plus the counterparties' intent-file readers), records each with its effect on these two cells in the evidence file, and stops if one is not neutral for a FIXT cell; the readers named in this row are a survey, not the list | `build_conversation_meta` raises "sets script_path but not combo_id" |
| Script steps | `fixt_app_script.yaml` step F-01 lists `applicable_combos: [C1, C3]` | `render_intent_file`, `expected_peer_rejects` filter steps by it |
| Arm | `arm="validation-off"` (the `Cell` default), FR-013 | `build_conversation_meta` → hello |
| `run_kind` | `"app-message-evidence"`, distinct from `"conformance"` **and** from `"validator-positive-control"`, so `run_live_matrix`'s sibling lookup never pairs the new cells with the probes. At T044/T047 the implementer re-derives every reader of `run_kind` (`grep -n run_kind phase-9-harness/tools/*.py`) and records it in the evidence file; each reader beyond the rows Select, Promote and N2 gets its own disposition there, and one that would treat the cells as conformance runs stops the task | `Cell.run_kind` |
| Select | Selected through the template row its id declares in `tests/interop/cell_results.yaml` (research R-11), **not** through `select_cells`' probe rule (scripted, non-conformance, combo declared). The probe rule is narrowed to `"validator-positive-control"`. Without the narrowing, the probe rule would admit the two cells through combos C1/C3 even with no template row, so `RegistryAgreesWithTheRealTemplateTest` could never report them missing. *test*: with `declared_cell_ids` stubbed to omit the two rows (the suite's existing stub idiom, not the real template), `select_cells` raises naming both | `emit_matrix.py::select_cells` |
| Select, `--conversation-only` | `select_cells(conversation_only=True)` returns every scripted cell, so the new cells run there too. Intended; the promotion skip below covers it — *test* | `emit_matrix.py::select_cells` |
| Run | `run_cell` as for any scripted cell; `status` comes from the cell's own pass/fail | `run_interop_cell.py::run_cell` |
| Promote | **Skipped** for `"app-message-evidence"`: `run_live_matrix`'s promotion loop calls `promote_and_record` for every `script_path` cell and flips a withheld promotion to `fail` — *test* that the new cells are neither promoted nor flipped | `emit_matrix.py::run_live_matrix` |
| N2 rows | Unchanged: the producer already keeps only `run_kind == "conformance"` — *test* that the new cells add no row | `emit_matrix.py` N2 producer |
| Dictionary digest | `_finalize`'s FR-011 check compares fixpp's hello `dictionary_digest` with the per-cell expected file: `dictionaries/FIX50SP2.xml` for these cells, FIX44 for every other — *test* both | `run_interop_cell.py::_finalize` |
| Dictionary path | one env var, `FIXPP_FIX50SP2_DICT_XML`, set at the `_run_initiator_cell` launch site next to `FIXPP_FIX44_DICT_XML`, and read by the gtest's fixture (L1); same file on both sides | `_run_initiator_cell` |
| Capability minimums | two entries in `phase-9-harness/tools/interop_capability_minimums.yaml`, each keyed by its cell id (`check_capability_minimums` looks up `cell.cell_id`), with the same capability keys and minimum values as the file's `CONV-PROBE-*` entries, copied at T045 (the same counterparty builds announce them); a missing entry fails closed | `CAPABILITY_MINIMUMS_PATH`, `check_capability_minimums` |
| Reconcile | two lines in `tests/interop/expected-skips-without-counterparty.txt`; `ci/run-interop-live.py::reconcile` fails on a stray image filter and on an unaccounted skip id. Against the pre-095 image (the fixpp PR's own `interop-live`), the two ids are unaccounted, so it runs no cell (research R-10) | `reconcile` |
| Image digest | the hello carries `INTEROP_COUNTERPARTY_IMAGE_DIGEST` or the sentinel `local-build:unpinned`; the lane does not set it, so the pulled digest is recorded at close-out (data-model §D) | `build_conversation_meta` |
| Registration lands with its template rows | no parent commit registers the two cells while its gitlink lacks their rows (fixpp#462; owner ruling, Gate A round 2): one parent commit after the fixpp merge carries every harness change and the gitlink bump (research R-10). `RegistryAgreesWithTheRealTemplateTest` holds at every parent commit. *test*: a both-directions pin for the two cells, modelled on `test_the_recovery_inbound_cells_are_registered_AND_declared` (registered in `CELLS` and declared by the real template), because the real-template test checks only registered ⇒ declared | `test_emit_matrix.py::RegistryAgreesWithTheRealTemplateTest` |

**L1 — session.** FIXT.1.1, DefaultApplVerID FIX.5.0SP2 (`1137=9`), TLS as in 033's FIXT cells;
fixpp is the initiator; ApplVerID(1128) is not stamped (research R-8); inbound validation off (below). 033's `make_fixt_engine_config()` is **not** reused: it sits in an anonymous
namespace and registers stub dictionaries. A shared fixture in `tests/interop/happy/hp_support.hpp`
(beside `production_dictionary_and_digest`, which stays FIX44-only) loads the **production**
`FIXT11.xml` and `FIX50SP2.xml`, registers `FIX50SP2.xml` under ApplVerID 9 so `Session::open()`
accepts the FIXT session, and returns the SHA-256 of the `FIX50SP2.xml` bytes it loaded for the hello.
It resolves the file from `FIXPP_FIX50SP2_DICT_XML`, falling back to `FIXPP_DICT_DATA_DIR`, which the
033 target gains through a `target_compile_definitions` after its `fixpp_add_interop_test` call (the
helper has no definitions keyword). 033's own cells are unchanged.
`SessionConfig::validate_inbound_messages` stays off (FR-013); the cell source's header comment
states it and why (L-041-2: the session validator takes one dictionary), which with the verify
record is FR-013's "cell's record"; the hello carries `arm="validation-off"`.
The cell's **first** statement that can skip or return is `INTEROP_REQUIRE_COUNTERPARTY(...)`
(`tests/interop/support/counterparty_probe.hpp`), as in 033's cell: with no counterparty leased it
skips with the one reason `ci/assert-interop-skips.py` accepts, and any later guard (the TLS fixture
directory, the dictionary file) comes after it. A guard placed before it would make the no-counterparty
lanes skip for a reason that checker refuses (the skip list's header).

**L2 — peers.** Two cells: `HP-QFcpp-init-fixt11-fix50sp2-app-bn` (QuickFIX-cpp v1.16.0) and
`HP-QFj-init-fixt11-fix50sp2-app-bn` (QuickFIX/J 3.0.1), each against the existing
`*-acceptor-tls-fixt50sp2.cfg.in` config: `UseDataDictionary=Y`, `TransportDataDictionary` FIXT11,
`AppDataDictionary` FIX50SP2 (QuickFIX/J: the jar's `FIX50SP2.xml` classpath resource).

**L3 — message.** After Logon, fixpp sends one 35=BN built by `fixpp::v50sp2::build_ExecutionAck`
from the step F-01 intent values (research R-7 field set), via `Engine::send`. The span passed to
`Engine::send` is `build_ExecutionAck`'s output buffer, unmodified: no re-serialisation and no generic
body builder on the path; the session adds the standard header and trailer only. The `sent`
record's values are the step F-01 literals, fixed before the build (data-model §D); only the
MsgSeqNum is captured, in `toApp`, as the pairing key.

**L4 — evidence.** The peer's generic per-field readback of the received BN is compared with
fixpp's `sent` record by `compare_streams` (decimal resolver over `FIX50SP2.xml`). The cell passes
only if exactly one comparison row exists and it passes, no peer Reject was seen, and the session
logged out cleanly. Two checks decide "logged out cleanly", both required: on the fixpp side
`hp::expect_graceful_stop` (`tests/interop/happy/hp_support.hpp`: `Engine::stop()` within its bound
and `stopped()` true), and in the harness `run_interop_cell.py::_logout_sync_step`, which requires
the counterparty's trailing `LOGOUT` transcript line whenever `_logout_marker_required` says so (on
every capture, and on a run against a committed golden that ends with that line). Its template row lands in `tests/interop/cell_results.yaml` (`kind: happy`, status
`skip:counterparty-unavailable`, kept after the live runs: research R-11) and
the schema test's `EXPECTED_IDS`. In conversation mode both counterparties' `fromApp` stage-0 branch
looks up `B-02` in the intent file; this script has no such entry, so they log and return
(`send_conversation_message` / `sendConversationMessage`) — expected, not a defect. That rests on
both peers writing the generic readback before that lookup (QF-cpp `emit_readback`, QFJ
`emitReadback`, called first in `fromApp`); the implementer re-verifies it at T051, and if it ever
fails, the one-row condition above fails the cell rather than passing it.

A peer that sends no readback (a crash, or a counterparty build without the readback writer,
research R-12) leaves the `sent` record unmatched, which `compare_streams` reports as a failing
row (`missing`), so the cell fails. A cell that skips reports `skip:…` through `parse_gtest_status`,
never `pass`.

Which pass condition has which arm: the comparison row and the peer Reject have L5's arms (a), (b)
and (c). The two Logout checks are shared machinery and are not re-armed per cell: the harness's
`_logout_sync_step` / `_logout_marker_required` have their own tests in
`phase-9-harness/tools/test_run_interop_cell.py`, and `hp::expect_graceful_stop` is 033's helper,
which every 033 HP cell runs. That is a stated limit of this cell's evidence, not an arm.

**L5 — non-vacuity (recorded at verify):**
- (a) a chosen value altered on the send side after the `sent` record is fixed: the value put in
  `ExecutionAckArgs` changes, the F-01 literal does not → RED in the comparison (a mismatch naming
  that field);
- (b) a required field omitted: the arm leaves `args.exec_id` unset and calls
  `build_ExecutionAck` directly, never `validate_ExecutionAck` (the generated builder writes
  ExecID(17) only when that member is set) → the peer rejects → RED. The record must show the
  peer-Reject check's own message, naming the peer's 35=3; a RED from the missing comparison row
  alone does not count. So the cell's Reject check is non-fatal, and both checks are evaluated;
- (c) nothing sent: the `Engine::send` call and the `sent` record both skipped, so no comparison
  row exists → RED in the exactly-one-row condition.
All three are scratch edits, reverted. If the typed builder refuses (b)'s build, the implementer
stops (tasks.md Execution rules).

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
