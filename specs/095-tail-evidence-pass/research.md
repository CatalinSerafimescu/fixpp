# Research — 095 tail evidence pass

Read at fixpp `4e8b3779` (= `origin/main`, 2026-10-24) and the parent harness at parent `665b3a8a`.
Every decision below names the source it rests on; where a value can drift, the recipe that
re-derives it is given instead of the value.

## R-1 Required-member rule (FR-002, FR-004, FR-005)

**Decision.** Two rules, applied to `dictionaries/FIX50SP2.xml` (spec Terminology):
- a field is required when every element on its path from the message is `required='Y'`;
- a group is required when every element on its path is required, where a group that is the only
  child of a component inherits that component reference's `required` flag.
A group instance's required members follow the same rules, starting from the group.

**Rationale.**
- The field rule is QuickFIX's AND composition. fixpp matches it, and B&L L-067-1 records that
  this is parity, not a defect.
- The group rule follows from how the vendored dictionary is written. It is byte-identical to
  QuickFIX-cpp v1.16.0's (re-check with `cmp dictionaries/FIX50SP2.xml
  <qfcpp>/spec/FIX50SP2.xml`). It never sets `required='Y'` on a `<group>` that sits inside a
  component; the requirement is carried by the component reference.
  - Example: `TradingSessionList` → `<component name='TrdSessLstGrp' required='Y'/>`, and
    `TrdSessLstGrp` is a single `<group name='NoTradingSessions' required='N'>`.
  - QuickFIX/J 3.0.1's `FIX50SP2.modified.xml` sets `required='Y'` on that same group directly.
- Taking the group's own flag alone yields no required group anywhere in the tail set. It also
  yields none for `NewOrderList`, whose `NoOrders` FIX and QuickFIX/J both require. That reading
  is therefore not credible.

**Audit of the "only child" condition.** A walk listed every group reachable through required
component references, with its component's child count. Over the tail set, every such group is
its component's only child, so a looser "first child" reading would yield the same set today. The
witness check (R-3) asserts the only-child condition: if it meets a required component that holds
a group plus siblings, it fails loudly instead of choosing a reading.

**Re-derivation recipe.** Today the walk lives in a scratch script; it becomes the census of R-3.
- Run it with `NewOrderList` (35=E) as a known positive. It must report `ListOrdGrp/NoOrders`.
- Compare its result with QuickFIX/J's flags. Extract them with
  `unzip -p ~/.m2/repository/org/quickfixj/quickfixj-messages-fix50sp2/3.0.1/*.jar FIX50SP2.modified.xml`
  and walk with `required='Y'` on groups.
- On 2026-10-24 the two agree on every tail MsgType present in both dictionaries, except
  NoSettlOblig (BQ) and NoAsgnReqs (CC). For those two, fixpp/QuickFIX-cpp's component rule
  requires the group and QuickFIX/J's file does not.
- This feature takes the stricter union, because setting a group that a peer does not require is
  harmless.

**Alternatives rejected.**
- *Group flag only:* yields nothing (above).
- *QuickFIX/J's flags as the authority:* CQ is absent from QuickFIX/J, and its file is the
  108-message base edition, not the dictionary fixpp ships.

## R-2 Where the witnesses live (FR-001, FR-006, `[const §VII.8]`)

**Decision.** Add a new translation unit, `tests/session/test_095_tail_rows_roundtrip.cpp`, as a
second source of the existing `test_077_allversions_builder_roundtrip` executable. That adds no new
ctest entry, so §VII.8's grouped-binary rule holds. The TU includes only the 23 per-message headers
`fixpp/v50sp2/messages/<Msg>.hpp` (078's slim layout) and links the `fixpp::builders::v50sp2` target
the executable already links. The existing `test_077_allversions_builder_roundtrip.cpp` is left
byte-unchanged (FR-006).

**Rationale.**
- The 077 file is generated. Editing 23 of its cases by hand makes it neither generated nor
  hand-written.
- A sibling TU keeps the executable and label set, and the per-message headers avoid a second
  ~75 MB `all.hpp` include in one executable.
- The test names carry the row: `TailRows095.<RowId>_<Msg>`, e.g. `TailRows095.A018_ExecutionAck`
  and `TailRows095.N002_UserRequest`. Underscores in a gtest name are allowed but discouraged; the
  repo already uses them (see `ctest -N` output).

**Alternatives rejected.**
- *Edit the 077 file:* above.
- *New executable:* adds a ctest entry, against §VII.8.

## R-3 The dictionary-derived completeness check (FR-004)

**Decision.** The witness TU walks `FIX50SP2.xml` with pugixml at runtime, in a fixture
`SetUpTestSuite`. The walk is independent of codegen; pugixml is already used by
`test_067_completeness.cpp`. It builds a required-member tree per MsgType by R-1.

Every witness, after its value readback, calls one helper, `expect_required_members_present(msg_type,
parsed_view)`. It checks, against the PARSED FRAME:
- every required message-level field is present;
- every required group has a count of at least 2, its instances are present, and each instance
  carries its required members (recursively).

The check reads the built bytes, not the witness's list of what it set. A witness that forgets a
member therefore fails whatever the witness's author believed.

**Non-vacuity arms (FR-004), each recorded RED with its command at verify:**
1. **Known positive:** a test, `TailRows095Census.KnownPositive_NewOrderList`, asserts that the
   walk yields `NoOrders` as required for 35=E and `ClOrdID` as one of its instance members.
2. **Mutant A:** delete the `trading_sessions` assignment in the A-027 TradingSessionList witness →
   RED in the presence check, with the value readback still green for the remaining fields.
3. **Mutant B:** delete one required scalar (`ExecAckStatus(1036)`) from the A-018 witness → RED.
4. **Mutant C:** in the walk, ignore the only-child inheritance → Mutant A's RED disappears, and
   the known positive goes RED. This proves the rule, not a list, decides.

## R-4 Independent parse and group readback (FR-003)

**Decision.**
- Parse with `fixpp::wire::Parser<Index>` over a `table_view` from `XmlLoader().load(FIX50SP2.xml)`,
  as 077 does (`fixpp_test_support::parse_dict`, `tests/session/support/app_message_read_scaffold.hpp`).
- Read top-level scalars with `MessageView::get(tag)` / `get_decimal`.
- Read group instances with `OffsetTable::group_slices(no_tag)` plus a per-slice tag scan, as
  069's `TradeCaptureReport` case does (`tests/session/test_069_all_families_roundtrip.cpp`, its
  `scan_slice_for_tag` helper).
- Chosen values are literals in the TU. Strings are `"<RowId>_<Msg>_<field>"`; for each instance,
  `"..._i<k>"`, so an instance-order swap fails.

**Rationale.** It is the bar A-001..A-012 and N-001 closed on, and the parse path shares no table
with the builder: the builder writes from emitted literals, the parser reads `table_view`.

## R-5 N-002 / N-003 per-row pin and routing (FR-007, FR-008)

**Decision.**
- *Pin:* two tests in `tests/codegen/test_077_v50sp2_builder_completeness.cpp` (an existing
  executable, which already includes `all.hpp` and has `builder_registry`):
  - `BuilderCompleteness077V50sp2.N002_UserRequestUserResponseRegistered` asserts `"BE"` and `"BF"`
    are registry keys;
  - `…N003_ApplicationSequencingRegistered` asserts `"BW"`, `"BX"`, `"BY"`.
  - Each test also calls the registry entry and checks the built body starts `35=<mt>`.
- *Routing:* one cell in `session_pure_tests`, `TailRouting095.InboundBeAndBwReachFromApp`. An
  established session (existing pure-session fixture, FIX.4.4 or FIXT; the classifier is
  version-independent) receives one inbound 35=BE and one 35=BW. The assertions:
  - `Application::from_app` is called once per message, with the MsgType;
  - `from_admin` is not called for them;
  - no Reject is sent.
- RED arm: add `"BE"` to `detail::is_admin_msgtype` (`src/session/msgtype_classifier.hpp`) in a
  scratch mutant only, never committed → the cell goes RED.

**Rationale.**
- DECISION 2 says the five MsgTypes are application messages, and the session's single admin
  allow-list confirms it: `is_admin_msgtype` is `0 1 2 3 4 5`. The routing cell turns that reading
  into a witness.
- The resend path's own `is_admin_type` lambda (`src/session/session.cpp`, in the resend-answer
  coroutine; it adds `A`) also treats them as application messages. They are therefore replayed,
  not gap-filled, which is QuickFIX parity. That half is not witnessed here, and it is named as
  such in the Verified cell.

## R-6 The live FIXT cell (FR-009..FR-016) — vehicle

**Decision.** A standalone parameterized gtest, `tests/interop/happy/hp_fixt50sp2_app_message_test.cpp`,
added as a second source of the existing 033 FIXT target. It follows 089's PROBE pattern
(`tests/interop/conversation/probe_cell_test.cpp`: "a SEPARATE minimal gtest … not a branch inside
Conversation.Cell"), run on 033's FIXT session setup:
- fixpp is the **initiator** only;
- two cells: `HP-QFcpp-init-fixt11-fix50sp2-app-bn` and `HP-QFj-init-fixt11-fix50sp2-app-bn`;
- a one-step script `tests/interop/conversation/fixt_app_script.yaml` (step F-01, 35=BN, originator
  fixpp);
- the peer writes its generic per-field readback (QF-cpp `collect_fields`, QFJ `collectFields`);
- 089's `compare_streams` (`tests/interop/support/witness_comparator.cpp`) compares it against
  fixpp's `sent` record.

**Rationale.** These come from the 2026-10-24 harness survey. Each claim is to be re-verified by the
implementer at the cited line before it is relied on.
- A plain 033 HP cell yields only a golden diff. The peer writes per-field readback only when the
  harness passes `INTEROP_CP_RUN_ID` and its sibling keys, which it does only for a cell with a
  `script_path` (`phase-9-harness/tools/run_interop_cell.py`, the `script_path` env block;
  QF-cpp `interop_counterparty_main.cpp`, the `INTEROP_CP_RUN_ID` gate).
- A FIXT combo inside 089's conversation would move the census, the e7a inventories, the combo
  list, `witness_evidence.yaml` and three hand-coded stage machines.
- One role is enough. The evidence is the PEER's parse, which does not depend on role.

**Alternatives rejected.** The two above. Running the acceptor role too would double the cells for
no new evidence.

## R-7 The live message: 35=BN ExecutionAck (FR-014)

**Decision.** 35=BN.
- The effective required set is the same in fixpp/QuickFIX-cpp's and QuickFIX/J's FIX50SP2
  dictionaries: OrderID(37), ExecAckStatus(1036), ExecID(17), Side(54).
- QuickFIX/J marks `OrderQtyData` `required='Y'` where QuickFIX-cpp does not. Neither
  `OrderQtyData` nor `Instrument` has a required member in any of the three files, so neither
  imposes a field.
- fixpp sends scalar fields only: the four required fields plus Symbol(55), OrderQty(38),
  ClOrdID(11), CumQty(14) and AvgPx(6). All are optional members of `ExecutionAckArgs`.
- The check is R-1's walk run on both dictionaries. The plan's quickstart gives the command.

**Excluded:**
- CQ, absent from QuickFIX/J;
- BJ/BS, where QuickFIX/J's group flag differs;
- BM, where QuickFIX/J requires `Quantity`.

The readiness findings, Q1, record the BJ/BS and BM differences.

## R-8 Session facts the cell relies on

All re-verified at implementation; citations are to symbols, not lines.
- **Dictionaries.** `Session::open()` refuses a `begin_string == "FIXT.1.1"` session whose version
  registry lacks `default_appl_ver_id`, in BOTH roles (042; L-033-5 closed). The 033 test's comment
  saying registration is "harmless for init cells" is stale. Reuse its `make_fixt_engine_config()`.
- **Send path.** `Engine::send(id, span)` → `Session::send`. The span must start `35=`, which
  `build_ExecutionAck` produces (`body_builder bb{"BN"}`). The session stamps the standard header.
- **ApplVerID(1128).** Not stamped, and not needed: QuickFIX-cpp and QuickFIX/J both fall back to
  the DefaultApplVerID agreed at Logon (QF-cpp `Session.cpp` app-dictionary resolution; QFJ
  `Session.java`, the same).
- **Inbound validation.** Off (`SessionConfig::validate_inbound_messages` default) — FR-013,
  L-041-2.
- **`sent` record.** The fixpp cell captures MsgSeqNum(34) of the sent BN in its `to_app` hook for
  the record (pairing key).

## R-9 Parent harness changes and their blockers

The survey found these. Two are defects that would make the cell fail for a reason that is not the
message.
1. `run_interop_cell.py` — register the two cells (fixpp initiator × the existing
   `*-acceptor-tls-fixt50sp2.cfg.in` peer configs; no new config).
2. **Blocker:** `_finalize`'s FR-011 dictionary-digest check hard-codes the FIX44 digest. Add a
   per-cell expected dictionary path, with a unit test in `test_run_interop_cell.py`.
3. `interop_capability_minimums.yaml` — two entries; a missing entry fails closed.
4. **Blocker:** `quickfixj/.../InteropCounterparty.java` — the conversation-mode seqnum bump and
   the FIX44 Heartbeat run outside the `A-TESTREQ` null check. The C++ twin was fixed in 089's
   T062a. On QFJ this induces a sequence gap and sends a FIX44-class Heartbeat on a FIXT session.
   Move both inside the check. C3/C4 declare A-TESTREQ and are unchanged; the live 089 cells re-run
   to prove it.
5. `golden/<2 cells>.fix` via `--update-goldens`.
6. `emit_matrix.py` (not in the image) promotes every `script_path` cell into the 089 evidence
   pipeline, whose census gate would fail this cell. Give the cell a distinct `run_kind` that
   promotion skips, with a test.

QuickFIX-cpp counterparty and peer configs: no change.

## R-10 Ordering and the image (FR-015)

**Decision.** In order:
1. Parent harness change (R-9), committed and pushed to parent `main`. The owner authorised this
   on 2026-10-24.
2. `publish-counterparties.yml` republishes `ghcr.io/catalinserafimescu/fixpp-interop-counterparties:latest`.
   It fires on `phase-9-harness/{quickfix-cpp,quickfixj,configs,golden}/**`,
   `run_interop_cell.py` and `interop_capability_minimums.yaml`.
3. Push the fixpp PR. Its `interop-live` lane runs the new cells against the new image.
4. Merge.
5. Gitlink bump.

**Known red windows, both advisory, disclosed in the PR body:**
- Between (2) and the fixpp merge, other fixpp PRs' `interop-live` reports a stray-filter mismatch
  for the two registered-but-absent cells (`ci/run-interop-live.py`, its stray filter). fixpp#613's
  PR is the one in flight.
- Parent-side, `emit_matrix` raises on a registered-but-undeclared cell until the gitlink bump.

**Peer identity (FR-015).** The cell's hello record carries the counterparty build digest that the
existing 089 records carry. The evidence names the image digest the PR's `interop-live` run pulled,
read from the job log.

## R-11 Evidence files and pins (FR-016)

- `tests/interop/cell_results.yaml`: two `kind: happy` rows.
- `tests/interop/cell_results_schema_check_test.py`: the hand-pinned `EXPECTED_IDS` list gains the
  two ids. RED first: add the rows, run the schema test, and record the failure on the pin.
- `tests/interop/expected-skips-without-counterparty.txt`: two lines.
- No change to `CENSUS_KEY_COUNT`, the e7a inventories, `CONV_COMBO_IDS` or
  `witness_evidence.yaml`. The schema test proves this: it stays green with only the `EXPECTED_IDS`
  edit.

## R-12 Local run of the live cell

- The local QuickFIX-cpp counterparty binary under `phase-9-harness/quickfix-cpp/counterparty/build/`
  is stale (2026-06-11): it predates 089's readback writer.
- Take the counterparty from the published image instead: `docker create` the image, `docker cp`
  `/opt/interop/{interop_counterparty,lib,qfcpp-spec}`, and set `INTEROP_QFC_COUNTERPARTY_BIN`,
  `LD_LIBRARY_PATH` and `INTEROP_QFCPP_SPEC_DIR`.
- QuickFIX/J is rebuilt from the patched source:
  `mvn -f phase-9-harness/quickfixj/pom.xml package -DskipTests` →
  `INTEROP_QFJ_JAR=…/target/phase9-harness-quickfixj-0.1.0-SNAPSHOT-all.jar`.
- The recipe mirrors `fixpp/.github/workflows/interop-live.yml`'s extract-and-run steps; no
  separate procedure doc exists. `quickstart.md` carries the commands.
