# Research — 095 tail evidence pass

Read at fixpp `4e8b3779` (= `origin/main`, 2026-10-24) and the parent harness at parent `665b3a8a`.
Every decision below names the source it rests on; where a value can drift, the recipe that
re-derives it is given instead of the value. The Gate A round 1 revisions (2026-10-24) re-read the
symbols they cite at fixpp `45a3f821` and the same parent harness. The Gate A round 2 revisions
(2026-10-24) re-read theirs at fixpp `5ed00d76`: `ci/run-interop-live.py`, `interop-live.yml`, the
parent's `emit_matrix.py` / `test_emit_matrix.py` and workflows, and the generated
`StreamAssignmentRequest.builder.inl`.

## R-1 Required-member rule (FR-002, FR-004, FR-005)

**Decision.** Two rules, applied to `dictionaries/FIX50SP2.xml` (spec Terminology); together they
give the **closure-required set**, the owner's bar:
- a field is required when every element on its path from the message is `required='Y'`;
- a group is required when every element on its path is required, where a group that is the only
  child of a component inherits that component reference's `required` flag.
A group instance's required members follow the same rules, starting from the group.

**Rationale.**
- The field rule is QuickFIX's AND composition. fixpp matches it, and B&L L-067-1 records that
  this is parity, not a defect. The witness TU proves it against the measured QuickFIX-cpp golden
  (`tools/quickfix_required_golden/golden.csv`, R-3 arm 1).
- The group rule is **not** QuickFIX-cpp behaviour: QuickFIX-cpp does not require a
  component-wrapped group (L-067-1; its instance-level golden is
  `tools/quickfix_required_golden/golden_groups.csv`). It is the owner's closure bar, stricter than
  either engine.
- The group rule follows from how the vendored dictionary is written. It is byte-identical to
  QuickFIX-cpp v1.16.0's (re-check with `cmp dictionaries/FIX50SP2.xml
  <qfcpp>/spec/FIX50SP2.xml`). It never sets `required='Y'` on a `<group>` that sits inside a
  component; the requirement is carried by the component reference.
  - Example: `TradingSessionList` → `<component name='TrdSessLstGrp' required='Y'/>`, and
    `TrdSessLstGrp` is a single `<group name='NoTradingSessions' required='N'>`.
  - QuickFIX/J 3.0.1's `FIX50SP2.modified.xml` sets `required='Y'` on that same group directly.
- Taking the group's own flag alone does not find `NewOrderList`'s `NoOrders`, which FIX and
  QuickFIX/J both require; Mutant C (R-3) is exactly that reading and turns the 35=E known positive
  RED. That reading is therefore not credible.

**The "only child" condition.** The walk (R-3) asserts it: a required component that holds a group
plus siblings fails loudly instead of choosing a reading. The same fail-loud covers a nested required
group and a delimiter chain longer than one nested group (data-model §B), so none of them is decided
by a reading recorded here. No vendored dictionary has a group among siblings, so that status's arm
is an inline synthetic dictionary (contracts W6).

**The delimiter.** A group's delimiter is its first emitted tag in dictionary order, components
expanded: the loader's rule (`src/dictionary/xml_loader.cpp`, `expand_field_list`, which pushes a
nested group's count tag at the outer level and names this the #208 shape). It may be a nested
group's count tag; CC's `NoAsgnReqs` is that case (re-derive:
`grep -o 'group_begin([0-9]*, [0-9]*)' build/<preset>/_codegen/include/fixpp/v50sp2/messages/StreamAssignmentRequest.builder.inl`).

**Re-derivation recipe.** The walk is the witness TU's (R-3); it is re-run on every build. The
comparison with QuickFIX/J's files is the committed selection check of R-7, run at verify (quickstart
§3); its output, not this file, says where the files differ. The witnesses check the vendored file's
walk. The selection check hard-asserts only what selects the live message (R-7); for every other
tail MsgType it prints both axes and asserts nothing, because those MsgTypes are not sent to a peer
(Clarifications, Gate A round 2).

**Alternatives rejected.**
- *Group flag only:* misses `NoOrders` (above).
- *QuickFIX/J's flags as the authority:* CQ is absent from QuickFIX/J, and its file is a different
  edition from the dictionary fixpp ships.

## R-2 Where the witnesses live (FR-001, FR-006, `[const §VII.8]`)

**Decision.** Add a new translation unit, `tests/session/test_095_tail_rows_roundtrip.cpp`, as a
second source of the existing `test_077_allversions_builder_roundtrip` executable. That adds no new
ctest entry. The executable is a **standalone** target (one `add_executable`, one `add_test`), not a
grouped bucket; under `[const §VII.8]` it is an exact-set gate's home, which is what the FR-004a
census needs (it reads the registered tests of the binary its witnesses live in). The TU includes the
23 per-message headers `fixpp/v50sp2/messages/<Msg>.hpp` (078's slim layout) plus `NewOrderList.hpp`
if the deficient 35=E frames are built with the typed builder, and links the `fixpp::builders::v50sp2`
target the executable already links. The existing `test_077_allversions_builder_roundtrip.cpp` is
left byte-unchanged (FR-006).

**Rationale.**
- The 077 file is generated. Editing 23 of its cases by hand makes it neither generated nor
  hand-written.
- A sibling TU keeps the executable and label set, and the per-message headers avoid a second
  ~75 MB `all.hpp` include in one executable.
- The test names carry the row: `TailRows095.<RowId>_<Msg>`, `<RowId>` without its hyphen (a gtest
  name cannot contain `-`), e.g. `TailRows095.A018_ExecutionAck` and `TailRows095.N002_UserRequest`.
  Underscores in a gtest name are allowed but discouraged; the repo already uses them (see
  `ctest -N` output).
- Selection: each of the four targets 095 touches gains `set_property(TEST <name> APPEND PROPERTY
  LABELS 095)` (the 091–093 pattern, e.g. `session_pure_tests`' `092`/`093` lines), and the bundle
  ships `expected-ctest-095.txt` (093's format: one ctest name per line), so verify selects with
  `ctest -L 095`.

**Alternatives rejected.**
- *Edit the 077 file:* above.
- *New executable:* adds a ctest entry, against §VII.8.

## R-3 The dictionary-derived completeness check (FR-004, FR-004a)

**Decision.** The witness TU walks the dictionary with pugixml at runtime, in a fixture
`SetUpTestSuite`. The walk is independent of codegen; pugixml is already used by
`test_067_completeness.cpp`. Its entry point takes a loaded `pugi::xml_document`, so the same code
walks `FIX50SP2.xml` and the inline synthetic dictionaries of the W6 arms (`pugi::xml_document::load_string`
literals, not files). It builds one `WalkResult` per MsgType by R-1: a status, which is non-`ok` on a
nested required group, on a group among siblings or on a delimiter chain longer than one nested
group, and the closure-required tree (data-model §B).

Every witness, after its readback (R-4), asserts that `required_members_missing(...)` is empty. The
helper takes the MsgType from the parsed frame's tag 35 and reads the same body enumeration as W4
(R-4: each entry attributed to message level or to the instance whose extent contains it, never by
first occurrence). Against that PARSED FRAME it returns:
- every required message-level field absent at message level;
- every required group that is absent, whose count is below 2, or whose instance count differs from
  its count value;
- in each instance, its delimiter and required fields when absent (and, for a nested-group
  delimiter, the nested group's count and delimiter).

The check reads the built bytes, not the witness's intent list. A witness that forgets a member
therefore fails whatever the witness's author believed.

**Non-vacuity arms (FR-004).** Each output of the walk, of W4's comparator, of W5's helper and of
the census has its own arm, recorded RED at verify with its command and the message that fired. The
arms are listed once, in the table of contracts W6. The non-circular known positives among them:
1. **Field-leg golden:** `TailRows095Census.FieldLegMatchesQuickFixCppGolden` reads
   `tools/quickfix_required_golden/golden.csv` at test time (path from a compile definition on the
   target) and asserts the walk's message-level field set equals its row, for every tail MsgType and
   35=E. Measured QuickFIX-cpp output is non-circular: it is not derived from this walk.
2. **Deficient frames:** `TailRows095Census.KnownPositive_DeficientFrames` asserts the exact list
   `required_members_missing` returns on each deficient 35=E and 35=CC frame of W6, one frame per
   presence leg. This tests the helper on a frame.
3. **Nested fail-loud:** `TailRows095Census.NestedRequiredGroupFailsLoud_MassQuote` asserts the walk's
   status for 35=i.
4. **Synthetic dictionaries:** `TailRows095Census.WalkStatusesOnSyntheticDictionaries` asserts the
   group-among-siblings and delimiter-chain statuses, with the element each names.

**Census (FR-004a).** `TailRows095Census.EveryTailMsgTypeHasExactlyOneWitness` iterates
`::testing::UnitTest::GetInstance()`'s registered test infos (registration is static), so it holds
under `--gtest_filter='TailRows095Census.*'` and `--gtest_shuffle`. It collects every finding
(multiplicity 0, multiplicity above 1, unmapped name, extra MsgType, row mismatch) before asserting.
A second name for one MsgType must carry another row ID, since gtest names are unique within a suite,
so multiplicity above 1 always comes with a row mismatch; W6's Mutant F records both findings.

**Alternatives rejected.**
- *Recursive readback over `OffsetTable::nested_group_slices`:* it would ship a branch no tail
  MsgType exercises beyond CC's one-instance delimiter group, which attribution by extent covers;
  the fail-loud is simpler and proven on 35=i.
- *A tag-at-two-levels detector:* it can fire on no vendored dictionary, and it guarded a
  first-occurrence lookup that no check uses once W4 and W5 share the enumeration by extent (spec
  Clarifications, Gate A round 2).
- *Runtime self-registration at each witness's start:* order-dependent, and a set collapses
  duplicates.
- *A static manifest of descriptors:* a second list beside the tests; `::testing::UnitTest` already
  is the registry.

## R-4 Independent parse and group readback (FR-003)

**Decision.**
- Parse with `fixpp::wire::Parser<Index>` over a `table_view` from `XmlLoader().load(FIX50SP2.xml)`,
  as 077 does (`fixpp_test_support::parse_dict`, `tests/session/support/app_message_read_scaffold.hpp`).
- Enumerate the parsed body with `OffsetTable::entries()` (document order), removing from both
  sides the tags the test's framing writes and tag 35 (the vendored FIX50SP2 file's `<header/>` is
  empty, so the dictionary cannot name them), and assign each entry to message level or to the
  `OffsetTable::group_slices(no_tag)` instance whose extent contains it; the per-slice scan follows
  069's `TradeCaptureReport` case (`tests/session/test_069_all_families_roundtrip.cpp`, its
  `scan_slice_for_tag` helper). Attribution is to the outermost required group whose instance extent
  contains the entry, so a delimiter's nested group (CC's 453 and 448 inside a 1499 instance) takes
  paths in the outer instance (`1499[<k>].453`, `1499[<k>].448`). The same enumeration feeds W5
  (R-3), so the readback and the presence check read one attribution.
- Compare the resulting `(path, value)` multiset with the witness's intent list in both directions
  (contracts W4), the model of `tests/interop/support/witness_comparator.cpp::compare_streams`. No
  first-occurrence `MessageView::get(tag)` lookup decides a readback.
- Chosen values are literals in the TU. Strings are `"<RowId>_<Msg>_<field>"`; for each instance,
  `"..._i<k>"`, so an instance-order swap fails.

**Rationale.** It is the bar A-001..A-012 and N-001 closed on, and the parse path shares no table
with the builder: the builder writes from emitted literals, the parser reads `table_view`.

## R-5 N-002 / N-003 per-row pin and routing (FR-007, FR-008)

**Decision.**
- *Pin:* two tests in `tests/codegen/test_077_v50sp2_builder_completeness.cpp`, a source of the
  existing standalone target `codegen_077_v50sp2_builder_completeness_test` (it already includes
  `all.hpp` and has `builder_registry`):
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
- RED arms, one per leg: add `"BE"` to `detail::is_admin_msgtype` (`src/session/msgtype_classifier.hpp`)
  → the BE assertions go RED; separately, add `"BW"` → the BW assertions go RED. Scratch mutants only,
  never committed.

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
- fixpp sends scalar fields only, all members of `ExecutionAckArgs`: OrderID(37), ExecAckStatus(1036),
  ExecID(17), Side(54), Symbol(55), OrderQty(38), ClOrdID(11), CumQty(14), AvgPx(6). This is the
  design's field choice, not a derived result.
- **The check** is `TailRows095Selection.DISABLED_LiveMessageAgreesOnBothAxes`, in the witness TU so
  it reuses R-3's walk (one implementation of the rule). It reads three files: the vendored
  `dictionaries/FIX50SP2.xml`, and the QuickFIX/J 3.0.1 jar's `FIX50SP2.xml` (the file
  `quickfixj-acceptor-tls-fixt50sp2.cfg.in`'s `AppDataDictionary` loads) and `FIX50SP2.modified.xml`,
  from a directory named by `FIXPP_095_QFJ_DICT_DIR`. It:
  - prints, for each tail MsgType, axis (a), each engine's own rule over its own file, and axis (b),
    the closure walk over each file (spec Clarifications, Gate A round 1);
  - hard-asserts only what selects BN: BN present in every file and equal on every axis, BN's
    closure-required set contained in the sent fields above, and every sent field a BN member in
    every file;
  - asserts nothing about the other tail MsgTypes, which are only printed (R-1);
  - asserts that the same equality predicate is **false** for BM, which differs on axis (b) (the
    round-2 Gate A reviews measured it; the check's printed output shows where). This known positive
    shows the equality can fail on real inputs, which the input-absent arm below cannot show
    (contracts W7 lists it as an exception). If a dictionary change makes BM agree, the check goes
    RED and a new known positive is chosen from its output;
  - **fails** when `FIXPP_095_QFJ_DICT_DIR` is unset or a file is missing — it never skips.
- It is `DISABLED_` so plain `ctest` (which has no jar) does not run it; verify runs it with
  `--gtest_also_run_disabled_tests` and asserts it ran (quickstart §3).

**Excluded:** every other tail MsgType. Which fail which axis is what the check prints; it is not
recorded here (FR-005).

## R-8 Session facts the cell relies on

All re-verified at implementation; citations are to symbols, not lines.
- **Dictionaries.** `Session::open()` refuses a `begin_string == "FIXT.1.1"` session whose version
  registry lacks `default_appl_ver_id`, in BOTH roles (042; L-033-5 closed). The 033 test's comment
  saying registration is "harmless for init cells" is stale. 033's `make_fixt_engine_config()`
  cannot be reused (anonymous namespace; stub dictionaries); the cell uses the shared production
  fixture of the live-cell contract's clause L1.
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
   `*-acceptor-tls-fixt50sp2.cfg.in` peer configs; no new config) with the L0 fields of
   contracts/live-cell-contract.md (`combo_id`, `run_kind`, arm), and set `FIXPP_FIX50SP2_DICT_XML` at
   the `_run_initiator_cell` launch site.
2. **Blocker:** `_finalize`'s FR-011 dictionary-digest check hard-codes the FIX44 digest. Add a
   per-cell expected dictionary path, with a unit test in `test_run_interop_cell.py`.
3. `tools/interop_capability_minimums.yaml` — two entries; a missing entry fails closed.
4. **Blocker:** `quickfixj/.../InteropCounterparty.java` — the conversation-mode seqnum bump and
   the FIX44 Heartbeat run outside the `A-TESTREQ` null check. The C++ twin was fixed in 089's
   T062a. On QFJ this induces a sequence gap and sends a FIX44-class Heartbeat on a FIXT session.
   Move both inside the check. C3/C4 declare A-TESTREQ and are unchanged; the live 089 cells re-run
   to prove it.
5. `golden/<2 cells>.fix` via `--update-goldens`.
6. `emit_matrix.py` (not in the image) promotes every `script_path` cell into the 089 evidence
   pipeline, and `select_cells`' probe rule admits any scripted non-conformance cell of a declared
   combo. Apply the `run_kind` rows of the live-cell contract's clause L0 (select, promote, N2,
   registration-with-template), each with a test in `test_emit_matrix.py`.

QuickFIX-cpp counterparty and peer configs: no change.

## R-10 Ordering and the image (FR-015)

**The invariant (fixpp#462; owner ruling, Gate A round 2: "Keep #462: register after merge.").**
The parent's `RegistryAgreesWithTheRealTemplateTest`
(`phase-9-harness/tools/test_emit_matrix.py`) calls the real `select_cells(False, "normal")` against
the gitlinked library's `tests/interop/cell_results.yaml` and fails on its raise. Its docstring calls
it "the reason a harness registration and its gitlink bump must land in ONE parent commit". It runs
in parent `harness-tests.yml` on every push touching `phase-9-harness/**` or the library gitlink. So
no parent commit may register the two cells while its gitlink lacks their rows.

**Decision.** In order:
1. **Before the merge, nothing reaches the parent history.** The phase-implementer makes the R-9
   changes as uncommitted edits in the parent working copy. No parent commit carries them, so no
   other session's parent push can publish them. Nobody stages a `phase-9-harness/` path before
   step 4, and a copy of every changed and new harness file is kept outside the tree, refreshed after
   each harness edit. (`git stash` is not used: it is shared by every worktree.)
2. **Local evidence.** With those edits in place and the main checkout on the 095 branch (which
   carries the template rows), the cells run locally against both peers (R-12, quickstart §5). The
   parent's own suite runs locally too: `RegistryAgreesWithTheRealTemplateTest` passes, and it fails
   before P5 adds the template rows, which shows it can see these two cells.
3. **The fixpp PR** is pushed, reviewed and merged. Its own `interop-live` runs against the pre-095
   image (below).
4. **One parent commit** carries every R-9 change, the goldens, and the gitlink bump to the merged
   fixpp `main`. If another parent commit (for example fixpp#613's close-out) has already bumped the
   gitlink to a library commit containing the merge, this commit carries the harness changes alone;
   the invariant still holds. Before committing, check `git ls-tree HEAD
   research/G19-fix-fpml-iso20022/library` and the staged paths (shared index). The orchestrator
   pushes parent `main` (owner, 2026-10-24); the push carries any unpushed parent commits with it.
5. **Image.** That push fires `publish-counterparties.yml` (its `paths:` include
   `phase-9-harness/{quickfix-cpp,quickfixj,configs,golden}/**`, `tools/run_interop_cell.py` and
   `tools/interop_capability_minimums.yaml`), which republishes
   `ghcr.io/catalinserafimescu/fixpp-interop-counterparties:latest`. Wait for it, and record the
   digest.
6. **CI evidence.** `interop-live.yml` has no `push` trigger (`pull_request` and `workflow_dispatch`
   only), so dispatch it on fixpp `main`. It must list both new cells as passed; record the run id
   and the pulled digest at close-out. If it does not pass both, the close-out does not complete and
   the failure goes to the owner (spec SC-003).

**What the fixpp PR's own `interop-live` shows (advisory; disclosed in the PR body).** The PR head
adds the two ids to `tests/interop/expected-skips-without-counterparty.txt`, while the pulled image's
registry predates them. `ci/run-interop-live.py`'s `reconcile` therefore fails its coverage check
("interop case(s) neither run here nor are excluded with a reason"), and `main()` stops with
"population reconciliation FAILED -- not running any cell". Before that, the build step still
compiles the image's `--list-binaries` targets under `linux-clang-release`, the 033 FIXT target with
the new cell source among them, so the PR's compile of that target is real evidence. **No live cell
runs on the PR**, the existing 033 FIXT cells in the binary 095 modifies included. The local
pre-merge run therefore covers those cells, the 089 L7 set and a `--list` reconcile of the whole
population against the local harness (quickstart §5). Exclusion lines in
`tests/interop/live-cells-excluded.txt` would not help: after the republish, the stale-exclusion
check would fail instead.

**Other windows (advisory, disclosed):**
- Between the fixpp merge and the republish, a fixpp PR based on the post-095 `main` shows the same
  reconcile failure. After the republish, a PR based on a pre-095 `main` fails reconcile's skew check
  ("registered cell filter(s) name a gtest case this tree's skip set does not contain … republish
  the image or rebase") until it rebases. fixpp#613's PR is the one in flight.
- The same parent push also fires parent `interop-matrix.yml` (its `push` paths name the gitlink and
  `phase-9-harness/**`). It uses the checkout's harness and pulls the image's counterparties, so it
  can run before `publish-counterparties` finishes. Then the QFJ cell runs against the unpatched jar
  (R-9 item 4) and can fail. Re-dispatch `interop-matrix` once the image is published; the
  post-publish run is the one recorded.
- Parent `harness-tests.yml` is never red because of 095: the invariant holds at every parent commit.

**Peer identity (FR-015).** The cell's hello record carries `build_conversation_meta`'s
`image_digest`: `INTEROP_COUNTERPARTY_IMAGE_DIGEST` if set, else the sentinel `local-build:unpinned`.
`interop-live.yml` does not set it, so the committed evidence carries the sentinel. The image digest
the post-merge `interop-live` run pulled is read from its job log and recorded at close-out. The
local run's peer builds are the extracted image's QF-cpp counterparty (its digest from `docker
inspect`) and the locally built QFJ jar (its source is the parent working copy), recorded in the
verify record.

## R-11 Evidence files and pins (FR-016)

- `tests/interop/cell_results.yaml`: two rows next to 033's FIXT rows, `{ id: <cell id>, config:
  normal, kind: happy, status: "skip:counterparty-unavailable", matrix_disposition: live, spec_ref:
  "FIX-SL §5.3.1/§5.3.3" }`. The fields are the schema test's `REQUIRED_FIELDS`. The status is the
  US1 block's pre-live value, not the `pass` of 033's banked rows: at the PR commit these cells have
  no CI run (R-10). `declared_cell_ids` reads a non-conversation row's `id` as its registry cell id,
  so these rows are what declares the two cells.
- `tests/interop/cell_results_schema_check_test.py`: the hand-pinned `EXPECTED_IDS` list gains the
  two ids. RED first: add the rows, run the schema test, and record the failure on the pin.
- `tests/interop/expected-skips-without-counterparty.txt`: two lines.
- No change to `CENSUS_KEY_COUNT`, the e7a inventories, `CONV_COMBO_IDS` or
  `witness_evidence.yaml`: the new cells reuse combos C1 and C3 and are never promoted (contracts
  L0), so they add no ledger row. The schema test proves this: it stays green with only the `EXPECTED_IDS`
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
- The QF-cpp peer loads `${QFCPP_SPEC}/FIX50SP2.xml`
  (`phase-9-harness/configs/quickfix-cpp-acceptor-tls-fixt50sp2.cfg.in`), from the image's
  `/opt/interop/qfcpp-spec`, while FR-014 reads the vendored file in its place. Quickstart §5 runs
  `cmp` on the two after the `docker cp` and puts the output in the verify record. QuickFIX/J needs
  no such check: `quickfixj/pom.xml` pins `quickfixj.version` to the 3.0.1 jar FR-014 reads.
- Because the PR's `interop-live` runs no cell (R-10), the local run covers what it would have:
  the two new cells, the existing 033 FIXT cells of `interop_hp_fixt50sp2_logon_hb_logout_test`
  (whose binary 095 changes), 089's CONV and PROBE cells (L7), and `ci/run-interop-live.py
  --harness ../phase-9-harness --list`. That last command reconciles the whole population of this
  tree against the local harness and runs nothing, so it shows the post-merge reconcile will pass.
- The recipe mirrors `fixpp/.github/workflows/interop-live.yml`'s extract-and-run steps; no
  separate procedure doc exists. `quickstart.md` carries the commands.
