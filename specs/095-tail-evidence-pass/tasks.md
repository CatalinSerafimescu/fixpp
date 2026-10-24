---

description: "Task list for 095-tail-evidence-pass"
---

# Tasks: Typed app-message tail — evidence pass for the 12 FIX 5.0SP2 rows

**Feature**: `095-tail-evidence-pass` | **Branch**: `095-tail-evidence-pass` (main checkout,
`research/G19-fix-fpml-iso20022/library`)

**Input**: `specs/095-tail-evidence-pass/`:
- spec.md (FR-001…FR-021, with FR-004a; SC-001…SC-005; User Stories 1–4; Clarifications through the
  post-exhaustion review);
- plan.md (`## Gate A` and its disagreement sections, Constitution Check, Files changed, Phases P0…P9,
  Risks);
- research.md (R-1…R-12);
- data-model.md (§A rows → witnesses, §B closure-required tree, §C literals, §D live-cell record,
  §E catalogue edits and the Spec-ref recipe, §F the v4.0 → v4.1 amendment text);
- contracts/witness-contract.md (W1…W8, the W6 arm table);
- contracts/live-cell-contract.md (L0…L8);
- quickstart.md (§1…§7).

**Gate A**: the loop exhausted at round 3; the owner's post-exhaustion pass ("Hand-edit + one Opus
round, skip Codex") and its Opus review were applied; scoped round 5 on the amendment ran (Codex P1=1
P2=1; Opus P1=0 P2=2 P3=1) and its fixes are applied; narrow scoped round 6 on the `[const §VI.2]` clause body converged with no
finding. **Gate A converged** (plan `## Gate A`; FR-021's Codex review of the amendment is discharged).

**Preconditions (pipeline steps 6–9, not tasks of this file)**: `/speckit-analyze` has run over spec,
plan and this file and its CRITICAL/HIGH findings are remediated; `/speckit-checklist-audit` has
dispositioned every checklist under `specs/095-tail-evidence-pass/checklists/`. The audit blocks
`/speckit-implement`, so it cannot be a task inside the file it gates.

**Tests**: requested — this is an evidence feature, written test-first. Every witness, census,
known positive, mutant and RED arm of contracts W6, live-cell contract L5, the routing cell's two
legs, FR-014's selection check, FR-016's pin, `RegistryAgreesWithTheRealTemplateTest`, quickstart §7
and data-model §E's recipe has its own task below, naming the command and the expected RED or GREEN,
so `/speckit-verify` can check each one (SC-002).

**Recorded deviation (094's precedent):** T090–T103 (the amendment review check, Gate B, the owner's
sign-off, the RATIFIED flip, merge, the post-merge parent commit and `interop-live` dispatch) run
chronologically AFTER the completeness audit, because `/gate-b` pre-flight 4d needs the audit record
first. They are numbered BEFORE the two mandatory close-out tasks so that T105 stays the FINAL task.
Each carries its own "Open at the audit — waiver" line, which the audit's check (i) accepts. T087–T089
(PR draft, push and PR, verify-delta check) and T104 (catalogue close-out, which needs the PR number)
run before the audit, in the order T087 → T088 → T104 → T089 → T105.

## Execution rules (apply to every task)

- **Executor (`[const §XVI.6]`).** Every task names one, in its description:
  - `(orchestrator)` writes documentation and governance text only: `spec/feature-catalogue.md`,
    `spec/coverage-index.md`, `spec/behaviors-and-limitations.md`, `.specify/constitution.md` (data-model
    §F verbatim), `specs/095-tail-evidence-pass/*.md`, decision records, parent docs, `brain/**`,
    post-merge `CLAUDE-history.md` / `CLAUDE.md`. It runs read-only instruments, builds and tests,
    reviews goldens, and makes and pushes the one post-merge parent commit (the harness files in it
    are the implementer's).
  - `(implementer)` is `phase-implementer`. It makes EVERY change to `tests/**` (C++ sources, CMake,
    the interop YAML, `cell_results_schema_check_test.py`, `expected-skips-without-counterparty.txt`),
    `specs/095-tail-evidence-pass/expected-ctest-095.txt` (derived from `ctest -N -L`, so it is owned
    by whoever owns the labels), the parent `phase-9-harness/**` files, and every scratch mutant,
    including the scratch `src/` mutants of US2.
  - The orchestrator never writes the implementer's files by any tool, python or sed through Bash
    included.
- **The tree.** Work happens in the main checkout on `095-tail-evidence-pass`. **Never `git checkout`
  or `git switch` there** (T097's post-merge reset is the one exception, under its own conditions).
  Before each commit batch run `git -C research/G19-fix-fpml-iso20022/library rev-parse --abbrev-ref HEAD`
  from the parent root. Commits are serial; never commit concurrently in this tree. Never `git stash`
  (the stash is shared by every worktree).
- **No production change (FR-020, SC-005).** Nothing under `src/`, `include/` or `tools/codegen/` is
  committed. Every scratch mutant there is reverted and checked with `git diff --exit-code -- src include tools/codegen`.
  A witness that exposes a production defect stops the task: the orchestrator files an issue
  (labelled from `gh label list`) and brings it to the owner before the affected row flips.
- **Stop rules (plan Risks).** The implementer stops and reports, and does not work around, when: the
  walk returns a non-`ok` status for a tail MsgType; the parser refuses a deficient frame or reads
  CC-count2 otherwise than contracts W6 says; a closure-required member has no typed `Args` member;
  FR-014's check disagrees on BN; the typed builder refuses a build an arm needs. A parse error or a
  build failure is never recorded as an arm's RED.
- **Mutant protocol (contracts W6).** Apply the scratch edit; rebuild only the affected target;
  rerun the task's filter; record the command and the message that fired (it must be the one the
  task names); revert; `git diff --exit-code` over the touched paths. Mutants are never committed.
- **Builds (`[const §XVII.7]`).** T005's owner ruling covers them, or an `AskUserQuestion` precedes the
  build. For every heavy build: `export CCACHE_DIR=/mnt/wsl/fixppbuild/ccache`; `-j2`; one build
  owner per build directory; first check
  `/tmp/claude-1000/-home-catalin-Work-Programming-Antreprenoriat/94f182a9-92bf-450a-92fa-f65ed094e81d/scratchpad/lane613.build.active`
  (a parallel lane builds in `fixpp-parallel`: if the marker exists, wait or ask); run `df -h /mnt/e`
  before a build in the main checkout's `build/linux-clang-debug`. Long builds run detached
  (`setsid nohup <build> > <log> 2>&1; touch <marker>.done`) with ONE background waiter on the
  marker, never a foreground poll loop.
- **The MSVC sandbox** is not used. Before any full `ctest` (packaging entries compile consumer
  projects), `cat /mnt/c/temp/fixpp/.sandbox-lock`; a lock held by someone else means ask the owner
  first. The verify record states plainly that MSVC was not run locally.
- **The parent working copy (owner ruling "Keep #462: register after merge.", research R-10).** Every
  `phase-9-harness/` change stays an uncommitted edit in the parent working copy until T099. Nobody
  stages a `phase-9-harness/` path before T099. A copy of every changed and new harness file is kept
  under `/tmp/claude-1000/-home-catalin-Work-Programming-Antreprenoriat/94f182a9-92bf-450a-92fa-f65ed094e81d/scratchpad/095-harness-copy/`,
  refreshed after every harness edit. Any pre-merge parent commit (decision records live in the parent
  through the `.specify/decisions/` symlink) checks `git status` and `git diff --cached --name-only`
  first, stages explicit paths only, and runs `git ls-tree HEAD research/G19-fix-fpml-iso20022/library` after.
- **No closing keyword.** No commit message or PR body puts "close", "fix" or "resolve" (any form,
  a negation included) next to #462, #613, #625 or #626. This PR closes no issue.
- **No placeholder number is committed.** Before every commit, `git grep -nE 'fixpp#<|#<pr>|<sign-off date>'`
  over `spec/` and `.specify/constitution.md` prints nothing new against `origin/main`.
- **Text is copied, never paraphrased.** Constitution text comes from data-model §F, catalogue text
  from data-model §E, byte for byte. Before replacing, `grep -F` the before-text; a miss stops the
  edit. Locate clauses by heading and leading number, rows by ID, never by line.
- **Commit plan, in execution order:** C-P0 (T019) · C-US1 (T036) · C-US2 (T042) · C-US3 (T058) ·
  C-AMEND (T061, the constitution only, before any catalogue edit) · C-RECORDS (T078) · the
  `/simplify` fixes, if any (T079) · C-PRCELL (T104) · the pre-audit `tasks.md` ticks (T089, before
  T105) · the later `tasks.md` ticks (T094) · C-RATIFY (T095),
  the LAST commit before merge. Then, post-merge, the one parent commit (T099).
- **Workspace-root scripts** resolve through `W=$(git rev-parse --show-superproject-working-tree)`, run
  from the library root; `$B`, `$P`, `$S` are quickstart's binary variables.
- **Gate results are scoped to the commits that existed when they ran.** Re-run the docs gates as the
  last action before every push.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an incomplete task). Tasks in
  `tests/session/test_095_tail_rows_roundtrip.cpp`, `spec/feature-catalogue.md` or
  `.specify/constitution.md` are serial with every other task on the same file; builds stay
  single-owner per build directory even for [P] tasks.
- **[Story]**: US1…US4 (spec User Stories 1–4).

---

## Phase 1: Setup (baselines and the resource gate)

**Purpose**: confirm the base, and give each later instrument a "before" from a run shown able to
report non-zero.

- [ ] T001 (orchestrator) **Base and tree check.** In `research/G19-fix-fpml-iso20022/library`:
  `git rev-parse --abbrev-ref HEAD` reads `095-tail-evidence-pass`; `git status --porcelain` is clean
  apart from this bundle; `git fetch origin`; record `git merge-base HEAD origin/main` under the verify
  record's `## Base`. In the parent: `git status --short -- research/G19-fix-fpml-iso20022/phase-9-harness`
  prints nothing (the harness baseline R-10 needs). `codegraph status --json` in this tree:
  `projectPath` is this tree, `fileCount` nonzero; `codegraph sync` if HEAD moved.
- [ ] T002 [P] (orchestrator) **Issue placement, read-only.** fixpp#625 and fixpp#626 are already
  filed and placed (no filing task): `grep -n '#625\|#626' research/G19-fix-fpml-iso20022/phases/phase-4/issue-batches.md`
  (parent) shows both under their batch headings, and `gh issue view 625` / `gh issue view 626`
  (`--repo CatalinSerafimescu/fixpp --json state,labels`) read OPEN. Record both in the verify record.
- [ ] T003 [P] (orchestrator) **The bundle's own line-citation charge.** `python3 tools/check_line_citations.py --range origin/main..HEAD`
  and `--shift-audit origin/main..HEAD` both pass on the bundle commits before implementation starts.
  A hit is fixed in the bundle (orchestrator) before T007.
- [ ] T004 [P] (orchestrator) **Scratch tools and the SC-004 baseline.** Copy the text of 094's
  `catalogue_census.py` out of `.specify/decisions/094-v1-tier0-bookkeeping-verify.md` into
  `/tmp/claude-1000/-home-catalin-Work-Programming-Antreprenoriat/94f182a9-92bf-450a-92fa-f65ed094e81d/scratchpad/tools/catalogue_census.py`
  (the 094 session's scratchpad path is gone). Show it can report non-zero: run it on
  `git show origin/main:spec/feature-catalogue.md` and record the OFFICIAL open / done figures and
  `--list open`, which must contain all 12 tail rows.
- [ ] T005 (orchestrator) **Resource gate (`[const §XVII.7]`, plan phase 0).** `AskUserQuestion` for
  the builds of this feature (the `linux-clang-debug` tree in the main checkout, the QuickFIX/J `mvn`
  build, the docker pull, the full `ctest` of T085). Record the ruling in the verify record. No build
  runs before it.
- [ ] T006 [P] (orchestrator) **SC-005 instrument, known positive first.** Show that
  `git diff --name-only <range> -- src include tools/codegen` reports non-zero on a range that touches
  those paths (094's merge, `45a620f4~1..45a620f4`, touched `src/` and `include/`), then record that
  it prints nothing for `origin/main..HEAD`. T084 re-runs it at the verified head.

---

## Phase 2: Foundational — the walk, the presence check and the known positives (plan P0)

**Purpose**: the dictionary-derived check every witness asserts (FR-004), proven able to fire before
any witness relies on it. FR-014's selection check (T043) reuses the same walk.

**⚠️ CRITICAL**: no witness (US1) is written before T019's commit.

- [ ] T007 (implementer) **The TU and its walk.** Create `tests/session/test_095_tail_rows_roundtrip.cpp`:
  - a header comment that records the rule (spec Terminology; research R-1) and the re-derivation
    recipe, and no derived count or member list (W7, FR-005);
  - the walk over a loaded `pugi::xml_document` (data-model §B), returning one `WalkResult` per
    MsgType with status `ok` / `nested_required_group` / `group_among_siblings` (naming the element),
    the closure-required fields and groups, each group's `count_tag`, `delim_tag` (first emitted tag
    in dictionary order, components expanded, the loader's rule in `src/dictionary/xml_loader.cpp`
    `expand_field_list`) and `delim_group`;
  - the body enumeration of research R-4 / contracts W4 (`OffsetTable::entries()`, framing tags and
    tag 35 removed, each entry attributed by extent to message level or to the outermost required
    group's `group_slices(count_tag)` instance), shared by the readback and the presence check;
  - `required_members_missing(...)`, data-model §B's legs, each missing path reported once, MsgType
    taken from the parsed frame's tag 35;
  - a fixture `SetUpTestSuite` that loads `FIXPP_DICT_DATA_DIR "/FIX50SP2.xml"` once.
  The parse path is `fixpp::wire::Parser<Index>` over `XmlLoader().load(...)`'s `table_view`
  (`fixpp_test_support::parse_dict`, `tests/session/support/app_message_read_scaffold.hpp`). The TU
  includes per-message headers `fixpp/v50sp2/messages/<Msg>.hpp`, never `all.hpp` (R-2).
- [ ] T008 (implementer) **CMake for the TU** in `tests/session/CMakeLists.txt`: add the TU to
  `test_077_allversions_builder_roundtrip`; link pugixml if the target does not already; add a compile
  definition naming `tools/quickfix_required_golden/golden.csv`. `tests/session/test_077_allversions_builder_roundtrip.cpp`
  stays byte-unchanged (FR-006, W8).
- [ ] T009 (implementer) **The `095` label and its expected set.** `set_property(TEST <name> APPEND PROPERTY LABELS 095)`,
  with a comment in the 092/093 pattern, on `test_077_allversions_builder_roundtrip` and
  `session_pure_tests` (`tests/session/CMakeLists.txt`), `codegen_077_v50sp2_builder_completeness_test`
  (`tests/codegen/CMakeLists.txt`) and the ctest entry of `interop_hp_fixt50sp2_logon_hb_logout_test`
  (`tests/interop/CMakeLists.txt`). Reconfigure, then write `specs/095-tail-evidence-pass/expected-ctest-095.txt`
  from `ctest --preset linux-clang-debug -N -L '^095$'` (one ctest name per line, sorted; 093's
  format). Quickstart §4's `diff` prints nothing. No new ctest entry (`[const §VII.8]`).
- [ ] T010 (implementer) **Field-leg known positive** `TailRows095Census.FieldLegMatchesQuickFixCppGolden`
  (W6 row "walk `ok`, field leg"): first assert `golden.csv` holds exactly one `FIX50SP2,<MT>,` row
  for each of the 23 tail MsgTypes and 35=E, then that the walk's message-level field set equals that
  row, read from the file at test time and never copied into the TU. GREEN expected.
- [ ] T011 (implementer) **Deficient frames** `TailRows095Census.KnownPositive_DeficientFrames`
  (contracts W6): frames E-absent, E-one, E-short, E-member, CC-nodelim, CC-nested, CC-count2 (literal
  bytes or typed-builder output, `NewOrderList.hpp` / `StreamAssignmentRequest.hpp`), each asserting
  the exact list `required_members_missing` returns. The 35=E and 35=CC walk statuses are asserted
  `ok`. **First** confirm and report that the parser accepts every frame and that CC-count2's two
  nested instances parse as one outer instance (`consume_group_extent` / `group_slices_status`,
  `src/wire/offset_table.cpp`); otherwise stop (Execution rules). GREEN expected.
- [ ] T012 (implementer) **Fail-loud known positives**: `TailRows095Census.NestedRequiredGroupFailsLoud_MassQuote`
  (status `nested_required_group` for 35=i, without breaking `SetUpTestSuite` for the tail set) and
  `TailRows095Census.WalkStatusesOnSyntheticDictionaries` (inline `load_string` documents S1, a
  required component holding one field and one group → `group_among_siblings` naming the component;
  S2, a delimiter chain → `nested_required_group` naming the inner group). GREEN expected.
- [ ] T013 (implementer) **RED arm, field leg.** Scratch: drop one tag from the walk's field set for
  one tail MsgType (e.g. a component-flag check turned off). `"$B" --gtest_filter='TailRows095Census.FieldLegMatchesQuickFixCppGolden'`
  fails, the message naming the MsgType and both sets. Second scratch: add a MsgType absent from
  `golden.csv` to the asserted set; the row-count precondition fails naming it. Revert, `git diff --exit-code`.
- [ ] T014 (implementer) **RED arm, Mutant C, frame half** (W6 row "group leg"). Scratch: the walk
  ignores the only-child inheritance. `--gtest_filter='TailRows095Census.KnownPositive_DeficientFrames'`
  fails on the `NoOrders`(73) assertions. Revert, `git diff --exit-code`. (Its second half, with Mutant
  A, is T030: it needs the A-027 witness.)
- [ ] T015 (implementer) **RED arm, nested detector.** Scratch: the nested-required-group detector
  returns `ok`. `--gtest_filter='TailRows095Census.NestedRequiredGroupFailsLoud_MassQuote'` fails
  naming 35=i. Revert, `git diff --exit-code`.
- [ ] T016 (implementer) **RED arms, synthetic statuses.** Scratch (a): the delimiter-chain check
  disabled → `WalkStatusesOnSyntheticDictionaries` fails on S2. Scratch (b), separately: the
  group-among-siblings detector disabled → it fails on S1. Each reverted, `git diff --exit-code`.
- [ ] T017 (implementer) **RED arms, Mutants L, M, N** (one run each, `--gtest_filter='TailRows095Census.KnownPositive_DeficientFrames'`):
  L — only the delimiter leg disabled → CC-nodelim's list loses `1499[0].453`; M — only the
  nested `!= 1` comparison disabled → CC-count2's list comes back empty; N — only the `delim_group`
  leg's nested-delimiter-absent disjunct disabled → CC-nested's list comes back empty. Each reverted,
  `git diff --exit-code`.
- [ ] T018 (implementer) **P0 GREEN run**: `--gtest_filter='TailRows095Census.*'` passes with no
  witness yet (the census test lands in T023); the existing 077 tests in the same binary pass.
- [ ] T019 (implementer) **Commit C-P0** (the TU, the three CMake label edits, `expected-ctest-095.txt`),
  after the reverted-mutant `git diff --exit-code`. Report the commit hash; orchestrator runs
  `codegraph sync` and ticks T007–T019.

**Checkpoint**: the presence check and every walk status are proven able to fire.

---

## Phase 3: User Story 1 — every tail MsgType has a named, complete offline witness (Priority: P1) 🎯 MVP

**Goal**: 23 witnesses, one per MsgType, each bidirectionally read back and checked by the
dictionary-derived presence check, plus the run-order-free census (plan P1).

**Independent test**: quickstart §1.

### Witnesses (one TU, serial)

Each witness (W1–W5): an intent list of `(path, literal)` entries chosen before the build by data-model
§C's literal rules (enum fields take their first declared value); every closure-required member set,
every required group with ≥ 2 instances, each with its delimiter and required members (CC's
`NoAsgnReqs` instances each carrying one `NoPartyIDs` instance); built with `fixpp::v50sp2::build_<Name>`
(build must succeed); framed with BeginString `FIX.5.0SP2`; parsed independently; W4 compared in both
directions against the enumeration; the intent list asserted ≥ 2 entries; `required_members_missing`
asserted empty; parsed tag 35 asserted equal to the MsgType the test's own `current_test_info()` name
maps to.

- [ ] T020 [US1] (implementer) Witnesses for A-018…A-029 in `tests/session/test_095_tail_rows_roundtrip.cpp`:
  `TailRows095.A018_ExecutionAck`, `TailRows095.A023_OrderMassActionRequest`,
  `TailRows095.A023_OrderMassActionReport`, `TailRows095.A027_TradingSessionListRequest`,
  `TailRows095.A027_TradingSessionList`, `TailRows095.A027_TradingSessionListUpdateReport`,
  `TailRows095.A028_MarketDefinitionRequest`, `TailRows095.A028_MarketDefinition`,
  `TailRows095.A028_MarketDefinitionUpdateReport`, `TailRows095.A029_SecurityListUpdateReport`,
  `TailRows095.A029_SecurityDefinitionUpdateReport`.
- [ ] T021 [US1] (implementer) Witnesses for A-030…C-003 in the same TU:
  `TailRows095.A030_SettlementObligationReport`, `TailRows095.A031_AllocationInstructionAlert`,
  `TailRows095.A032_UserNotification`, `TailRows095.A033_StreamAssignmentRequest`,
  `TailRows095.A033_StreamAssignmentReport`, `TailRows095.A033_StreamAssignmentReportACK`,
  `TailRows095.C003_AccountSummaryReport`.
- [ ] T022 [US1] (implementer) Witnesses for N-002/N-003 in the same TU:
  `TailRows095.N002_UserRequest`, `TailRows095.N002_UserResponse`,
  `TailRows095.N003_ApplicationMessageRequest`, `TailRows095.N003_ApplicationMessageRequestAck`,
  `TailRows095.N003_ApplicationMessageReport`.
- [ ] T023 [US1] (implementer) **The census** `TailRows095Census.EveryTailMsgTypeHasExactlyOneWitness`
  (FR-004a, W1): the row → MsgTypes table of data-model §A as a literal; iterate the registered tests
  of suite `TailRows095` via `::testing::UnitTest::GetInstance()`; map each `<Name>` to a MsgType
  through the dictionary's `<message name=… msgtype=…>`; collect every finding (multiplicity 0,
  multiplicity > 1, unmapped name, extra MsgType, row mismatch) before asserting.
- [ ] T024 [US1] (implementer) **US1 GREEN run** — quickstart §1, as a `bash -e` script: the count
  assertion (`TailRows095.*` lists 23); `--gtest_filter='TailRows095.*:TailRows095Census.*'` passes;
  `TailRows095Census.*` alone passes; `--gtest_shuffle --gtest_repeat=3` passes. The 077 tests of the
  binary pass (FR-006), and `git diff --exit-code origin/main -- tests/session/test_077_allversions_builder_roundtrip.cpp`
  is clean (W8).

### RED arms (contracts W6; Mutant protocol; record each in the verify record)

- [ ] T025 [US1] (implementer) **Mutant D** — delete one intent entry, setter kept → RED in W4
  ("parsed, not intended") naming the path.
- [ ] T026 [US1] (implementer) **Mutant H** — in `TailRows095.A018_ExecutionAck`, delete the CumQty(14)
  setter and keep its intent entry → RED in W4 ("intended, not parsed") naming the path; W5 stays
  green.
- [ ] T027 [US1] (implementer) **Changed literal** — one witness's buffer altered after the build (or
  its intent literal changed) → RED in W4 naming the path and both values.
- [ ] T028 [US1] (implementer) **Mutant E** — cut `TailRows095.A029_SecurityListUpdateReport` to one
  field (setters **and** intent entries deleted) → RED in the intent-size assertion, with its message.
- [ ] T029 [US1] (implementer) **Mutant B** — in `TailRows095.A018_ExecutionAck`, delete the
  ExecAckStatus(1036) setter **and** its intent entry → RED in W5 (not W4) naming `1036`. This is
  FR-004's "setter and intent entry both" known positive.
- [ ] T030 [US1] (implementer) **Mutant A, and Mutant C on top** — in `TailRows095.A027_TradingSessionList`,
  delete the `trading_sessions` assignment and its intent entries → RED in W5 naming the group. Then,
  with Mutant A still applied, re-apply T014's Mutant C → that W5 RED disappears (the rule, not a
  list, decides). Record both runs; revert both; `git diff --exit-code`.
- [ ] T031 [US1] (implementer) **Census Mutant G** — delete `TailRows095.C003_AccountSummaryReport` →
  census RED naming CQ with multiplicity 0.
- [ ] T032 [US1] (implementer) **Census Mutants F, I, J** (may share one rebuild with T031; the record
  shows each message; the quickstart §1 count assertion is skipped for these runs, and the record says
  so): F — duplicate the A-018 witness as `TailRows095.A024_ExecutionAck` → RED naming BN with
  multiplicity 2 **and** the row mismatch (A024 against A-018); I — add `TailRows095.A018_ExecutionAcknowledgement`
  → RED naming the unmapped name; J — add `TailRows095.A001_NewOrderSingle` → RED naming 35=D as
  extra.
- [ ] T033 [US1] (implementer) **Mutant K** — the name → MsgType lookup returns another tail MsgType
  for one generated name → RED in that witness's tag-35 assertion naming both MsgTypes (the census
  goes RED too).
- [ ] T034 [US1] (implementer) **Census order-freedom arm** — `"$B" --gtest_filter='TailRows095Census.EveryTailMsgTypeHasExactlyOneWitness'`
  alone, with Mutant G applied, is RED (the census reads registration, not runs). Revert.
- [ ] T035 [US1] (implementer) Re-run T024 on the reverted tree; all GREEN; `git diff --exit-code`.
- [ ] T036 [US1] (implementer) **Commit C-US1** (the TU only). Report the hash; orchestrator runs
  `codegraph sync` and ticks T020–T036.

**Checkpoint**: SC-001's offline half holds; every W6 row has a recorded RED.

---

## Phase 4: User Story 2 — N-002 / N-003 close the N-001 way (Priority: P1)

**Goal**: per-row registry pins and a routing witness, no production change (plan P2).

**Independent test**: quickstart §2.

- [ ] T037 [P] [US2] (implementer) **The two pins** in `tests/codegen/test_077_v50sp2_builder_completeness.cpp`
  (FR-007, R-5): `BuilderCompleteness077V50sp2.N002_UserRequestUserResponseRegistered` asserts `"BE"`
  and `"BF"` are `builder_registry` keys and `BuilderCompleteness077V50sp2.N003_ApplicationSequencingRegistered`
  asserts `"BW"`, `"BX"`, `"BY"`; each also calls the entry and checks the built body starts `35=<mt>`.
- [ ] T038 [P] [US2] (implementer) **The routing cell** `TailRouting095.InboundBeAndBwReachFromApp`
  (FR-008, R-5) in an existing `session_pure_tests` fixture TU under `tests/session/` (the implementer
  picks it and names it in the report): on an established session, one inbound 35=BE and one 35=BW;
  `Application::from_app` called once per message with its MsgType, `from_admin` not called for
  them, no Reject sent. The BE and BW assertions are separate, so each leg fails on its own.
- [ ] T039 [US2] (implementer) **US2 GREEN run** — quickstart §2's script (count assertions: 2 pins,
  1 routing cell). Instrument check (CLAUDE.md "prove the instrument can report non-zero"): scratch,
  one pin's expected key changed to a MsgType absent from the registry → that pin fails; revert.
- [ ] T040 [US2] (implementer) **RED arm, BE leg** — scratch: add `"BE"` to `detail::is_admin_msgtype`
  (`src/session/msgtype_classifier.hpp`); rebuild `session_pure_tests`; `"$S" --gtest_filter='TailRouting095.*'`
  fails on the BE assertions only. Revert; `git diff --exit-code -- src`.
- [ ] T041 [US2] (implementer) **RED arm, BW leg** — the same with `"BW"`; the BW assertions fail
  only. Revert; `git diff --exit-code -- src`.
- [ ] T042 [US2] (implementer) **Commit C-US2** (the two test files). Report the hash and the routing
  cell's TU; orchestrator runs `codegraph sync` and ticks T037–T042.

**Checkpoint**: N-002/N-003's per-row evidence exists; FR-008's two legs each recorded RED.

---

## Phase 5: User Story 3 — one live FIXT.1.1 / FIX.5.0SP2 cell against both QuickFIX peers (Priority: P2)

**Goal**: two live cells (`HP-QFcpp-init-fixt11-fix50sp2-app-bn`, `HP-QFj-init-fixt11-fix50sp2-app-bn`)
carrying a typed-built 35=BN, run locally against both peers before the merge (plan P3–P6).

**Independent test**: quickstart §3 and §5.

### Parent harness, uncommitted (plan P3; research R-9, R-10; contracts L0)

- [ ] T043 [US3] (implementer) **FR-014 selection check** `TailRows095Selection.DISABLED_LiveMessageAgreesOnBothAxes`
  in `tests/session/test_095_tail_rows_roundtrip.cpp`, reusing the walk (research R-7): reads the
  vendored `dictionaries/FIX50SP2.xml` and, from `FIXPP_095_QFJ_DICT_DIR`, the QuickFIX/J 3.0.1 jar's
  `FIX50SP2.xml` and `FIX50SP2.modified.xml`; prints axes (a) and (b) for the tail set; hard-asserts
  only what selects BN (present in every file, equal on every axis, closure-required set within the
  R-7 sent fields, every sent field a member in every file); asserts the same equality predicate is
  false for BM; **fails, never skips**, when the variable is unset or a file is missing.
- [ ] T044 [P] [US3] (implementer) **Registration and dictionary digest** (R-9 items 1–2) in
  `phase-9-harness/tools/run_interop_cell.py`: register both cells with L0's fields (`combo_id` C1 on
  the QF-cpp cell, C3 on the QFJ cell; `run_kind="app-message-evidence"`; `arm="validation-off"`;
  `script_path` the new script), against the existing `*-acceptor-tls-fixt50sp2.cfg.in` configs; set
  `FIXPP_FIX50SP2_DICT_XML` at the `_run_initiator_cell` launch site; give `_finalize`'s FR-011 check
  a per-cell expected dictionary (`dictionaries/FIX50SP2.xml` for these cells, FIX44 for every other).
  Tests in `phase-9-harness/tools/test_run_interop_cell.py` for both dictionary cases and the
  registration fields. Refresh the outside-tree copy.
- [ ] T045 [P] [US3] (implementer) **Capability minimums** (R-9 item 3): two entries in
  `phase-9-harness/tools/interop_capability_minimums.yaml`. Refresh the copy.
- [ ] T046 [P] [US3] (implementer) **QuickFIX/J A-TESTREQ blocker** (R-9 item 4): in
  `phase-9-harness/quickfixj/.../InteropCounterparty.java`, move the conversation-mode seqnum bump and
  the Heartbeat inside the `A-TESTREQ` null check (the C++ twin's 089 T062a shape). Refresh the copy.
- [ ] T047 [P] [US3] (implementer) **`emit_matrix` `run_kind` handling** (R-9 item 6; L0 rows Select,
  Select `--conversation-only`, Promote, N2, Registration) in `phase-9-harness/tools/emit_matrix.py`,
  each with a test in `phase-9-harness/tools/test_emit_matrix.py`: the probe rule narrowed to
  `"validator-positive-control"` (*test*: `declared_cell_ids` stubbed to omit the two rows →
  `select_cells` raises naming both); `conversation_only=True` returns the new cells (*test*);
  promotion skipped for `"app-message-evidence"`, neither promoted nor flipped (*test*); N2 adds no
  row (*test*); a both-directions pin for the two cells modelled on
  `test_the_recovery_inbound_cells_are_registered_AND_declared` (*test*). Refresh the copy.
- [ ] T048 [US3] (implementer) **Peers and harness unit run.** `mvn -f ../phase-9-harness/quickfixj/pom.xml package -DskipTests`
  (detached, marker); quickstart §5's docker pull / inspect / create / cp of the QF-cpp counterparty,
  recording the image digest; `cmp dictionaries/FIX50SP2.xml "$P/qfcpp-spec/FIX50SP2.xml"` prints
  nothing. `python3 -m pytest -q -p no:cacheprovider test_run_interop_cell.py test_emit_matrix.py -k 'not RegistryAgreesWithTheRealTemplate'`
  passes (the real-template test is expected RED until T055; T054 records it). Parent
  `git diff --cached --name-only` prints nothing.

### fixpp live cell (plan P4)

- [ ] T049 [US3] (orchestrator) **Run the selection check** — quickstart §3 as a `bash -e` script:
  the unzip from the `~/.m2` jar T048's build fetched; the run passes (`[  PASSED  ] 1 test`); the
  input-absent arm (`env -u FIXPP_095_QFJ_DICT_DIR`) FAILS. The printed axes go in the verify record,
  never in an artifact (FR-005). If BN does not pass, stop: the live cell is not built on BN and the
  orchestrator re-selects from the output (plan Risks).
- [ ] T050 [US3] (implementer) **Selection check, predicate mutant** — scratch: the equality predicate
  returns `true` unconditionally → the run fails on the BM assertion. Revert, `git diff --exit-code`.
- [ ] T051 [US3] (implementer) **The cell, its fixture, CMake and script** (contracts L1–L4, research
  R-6, R-8):
  - `tests/interop/happy/hp_support.hpp`: the shared production FIXT11/FIX50SP2 engine-config fixture
    (loads production `FIXT11.xml` and `FIX50SP2.xml`, registers FIX50SP2 under ApplVerID 9, returns
    the SHA-256 of the FIX50SP2 bytes; file from `FIXPP_FIX50SP2_DICT_XML`, falling back to
    `FIXPP_DICT_DATA_DIR`); `production_dictionary_and_digest` unchanged;
  - `tests/interop/happy/hp_fixt50sp2_app_message_test.cpp` (NEW): the two-parameter cell; fixpp
    initiator; after Logon one 35=BN built by `fixpp::v50sp2::build_ExecutionAck` from the R-7 field
    values fixed before the build, sent via `Engine::send`; MsgSeqNum captured in `to_app`;
    `validate_inbound_messages` off (FR-013); the `sent` record and the comparison by `compare_streams`
    (decimal resolver over `FIX50SP2.xml`);
  - `tests/interop/CMakeLists.txt`: the source joins `interop_hp_fixt50sp2_logon_hb_logout_test`; link
    `fixpp::builders::v50sp2` and the codegen include; `target_compile_definitions` for
    `FIXPP_DICT_DATA_DIR` after the `fixpp_add_interop_test` call;
  - `tests/interop/conversation/fixt_app_script.yaml` (NEW): step F-01, 35=BN, originator fixpp,
    `applicable_combos: [C1, C3]`.
  033's own cells are unchanged.
- [ ] T052 [US3] (implementer) **Local run and goldens** — build the 033 FIXT target (Builds rule);
  run each cell with `python3 ../phase-9-harness/tools/run_interop_cell.py <cell> --build-root build`
  against the T048 peers; first capture with `--update-goldens` (R-9 item 5) into
  `phase-9-harness/golden/`; refresh the copy. Each cell: exactly one passing comparison row, no peer
  Reject, clean Logout. The orchestrator reviews both goldens by hand and records the review.
- [ ] T053 [US3] (implementer) **L5 RED arms**, each against both peers, scratch, reverted:
  (a) one chosen value altered on the send side after the `sent` record is fixed → RED (comparison
  mismatch); (b) ExecID(17) omitted → the peer rejects → RED (missing readback and/or unexpected peer
  Reject). If the typed builder refuses (b)'s build, stop (Execution rules). `git diff --exit-code`
  in both trees' touched paths.

### Evidence files and pins (plan P5)

- [ ] T054 [US3] (implementer) **Real-template RED first.** With T044's registration in place and
  before any template row exists: `python3 -m pytest -q -p no:cacheprovider test_emit_matrix.py -k RegistryAgreesWithTheRealTemplate`
  (from `phase-9-harness/tools`) FAILS naming both cells. Record it.
- [ ] T055 [US3] (implementer) **Template rows and skip list** (R-11): two rows next to 033's FIXT rows
  in `tests/interop/cell_results.yaml` (`id`, `config: normal`, `kind: happy`,
  `status: "skip:counterparty-unavailable"`, `matrix_disposition: live`, `spec_ref: "FIX-SL §5.3.1/§5.3.3"`);
  two lines in `tests/interop/expected-skips-without-counterparty.txt`. Then
  `python3 -m pytest tests/interop/cell_results_schema_check_test.py -q` FAILS on the `EXPECTED_IDS`
  pin (FR-016's RED first). Record it.
- [ ] T056 [US3] (implementer) **The pin** — add the two ids to `EXPECTED_IDS` in
  `tests/interop/cell_results_schema_check_test.py`; the schema test passes with no other edit (no
  `CENSUS_KEY_COUNT`, e7a inventory, `CONV_COMBO_IDS` or `witness_evidence.yaml` change). The
  real-template test of T054 now passes.

### Local live evidence (plan P6; stands in for the PR's `interop-live`, contracts L7)

- [ ] T057 [US3] (implementer) **Quickstart §5 in full**, as a script: `python3 ci/run-interop-live.py --harness ../phase-9-harness --list`
  reconciles the whole population; `--list-binaries` builds; the full `python3 ci/run-interop-live.py --harness ../phase-9-harness --build-root build`
  ends "all … live interop cell(s) passed" (the two new cells, 033's FIXT cells, 089's CONV-C1…C4
  and PROBE cells against the patched QuickFIX/J); the schema test; both harness suites; the
  real-template test. Record in the verify record: every output, the image digest, the jar's source
  state (`git -C .. status --short -- phase-9-harness`), the peer builds (FR-015).
- [ ] T058 [US3] (implementer) **Commit C-US3** (fixpp only: `tests/session/test_095_tail_rows_roundtrip.cpp`'s
  selection check, `tests/interop/**` changes, `hp_support.hpp`). The parent harness stays
  uncommitted: parent `git diff --cached --name-only` prints nothing, and the outside-tree copy
  matches the working copy (`diff -r`). Report the hash; orchestrator runs `codegraph sync` and ticks
  T043–T058.

**Checkpoint**: SC-003's local half holds; FR-012, FR-014, FR-016 arms recorded RED.

---

## Phase 6: User Story 4 — the amendment, then the records (Priority: P2)

**Goal**: v4.1 amendment first (`[const §XX.1]`, FR-021), then the 12 rows `done` with their
witnesses, four version narrowings, four Spec-ref corrections, coverage-index gap notes and the B&L
delta (plan P7).

**Independent test**: quickstart §7 and data-model §E's recipe.

### RED before records

- [ ] T059 [US4] (orchestrator) **Quickstart §7, RED run, before any record edit**: the script fails at
  its first Status check (no tail row is `done`). Record the message.
- [ ] T060 [US4] (orchestrator) **Data-model §E recipe, positive control** on the unedited catalogue:
  it exits 1 and flags exactly A-030, A-033, C-003 and N-003, no other row. Record the output.

### Amendment (its own commit, before any catalogue edit)

- [ ] T061 [US4] (orchestrator) **Commit C-AMEND** — `.specify/constitution.md` only, data-model §F
  verbatim: `## Article VI` §2 and §5 (before-texts `grep -F`-matched first), the Status line's
  PENDING form (§F.4), and the PENDING Sync Impact Report block above the v3.0 → v4.0 block (§F.3).
  No `CHANGELOG.md` change (MINOR). Copy from the data-model text a converged scoped round has seen;
  if the owed narrow §VI.2 round (header) changes the clause, data-model §F is edited first, then
  copied. Then `command grep -qF 'may instead use the message-level form `[DocAbbrev] Category`' .specify/constitution.md`.

### Catalogue, coverage index, B&L (after T061)

- [ ] T062 [US4] (orchestrator) `spec/feature-catalogue.md`, the 12 rows (data-model §E): Status `done`;
  `/specify` `095-tail-evidence-pass`; Tests the witness IDs of data-model §A (plus, for N-002 and
  N-003, the pin and `TailRouting095.InboundBeAndBwReachFromApp`; for A-018 the two live-cell IDs);
  Verified the offline-bar sentence of §E (A-018's naming the generated `ExecutionAck` and the live
  cells; N-002/N-003's naming the pin and the routing cell, and that the resend-path replay half is
  not witnessed, R-5). The PR cell stays as on `origin/main` until T104.
- [ ] T063 [US4] (orchestrator) **Version narrowing** (FR-018): FIX version(s) → `5.0SP2` for A-018,
  A-029, A-030 (Verified adds "FIX 5.0SP1: no typed tier; runtime-XML path only") and N-002 (Verified
  adds §E's FIX 5.0/5.0SP1 and FIX 4.4 BE/BF sentence, naming `kN002N003Excluded` and fixpp#625).
- [ ] T064 [US4] (orchestrator) **Spec-ref corrections** (FR-018): A-030 → `[FIX50SP1] Settlement Instruction`,
  A-033 → `[FIX50SP2] Market Data`, C-003 → `[FIX50SP2] Account Reporting`, N-003 → `[FIX50SP2] Application`.
  Every other Spec ref cell byte-unchanged.
- [ ] T065 [US4] (orchestrator) **Data-model §E recipe after the edits**: exits 0. Then, on scratch
  copies (`CAT` under `mktemp -d`), each must exit 1, output recorded: (a) A-030's token changed to
  `[FIX44]`; (b) one tail row's Title cell given `35=A`. Record the 2010 Repository `<CategoryID>`
  cross-check where the owner-local edition has the message.
- [ ] T066 [P] [US4] (orchestrator) `spec/coverage-index.md`: each tail MsgType's existing row names its
  witness in its gap-note column (no new row, no row moved; CC, CD, CE, CQ stay under
  "Infrastructure"; FR-019).
- [ ] T067 [P] [US4] (orchestrator) `spec/behaviors-and-limitations.md` delta (FR-019, contracts L8):
  the live cell's validation-off disclosure under L-041-2 (FR-013); a B-row naming what the live cell
  witnesses (one BN from fixpp to each peer, inbound validation off) and that the other 22 tail
  MsgTypes close offline; FIX 4.4 BE/BF have no typed builder (the v44 exclusion, fixpp#625); anything
  a witness exposed. Then `python3 "$W/.claude/scripts/check_bl_delta.py" gate --feature 095-tail-evidence-pass`
  passes and `check_bl_citations.py gate` reports 0 dangling (`W=$(git rev-parse --show-superproject-working-tree)`).

### Records check (quickstart §7) and its perturbations

- [ ] T068 [US4] (orchestrator) **Quickstart §7, GREEN** on the edited tree (with the uncommitted
  parent registration present, so the live-cell IDs resolve in `CELLS`).
- [ ] T069 [US4] (orchestrator) Perturbation, scratch `CAT`: one narrowed version cell reverted → RED
  ("version is …, not 5.0SP2").
- [ ] T070 [US4] (orchestrator) Perturbation, scratch `CAT`: `fixpp#625` dropped from N-002's Verified
  cell → RED.
- [ ] T071 [US4] (orchestrator) Perturbation, scratch `CAT`: one witness name removed from its row's
  Tests cell → RED naming it.
- [ ] T072 [US4] (orchestrator) Perturbation, scratch `CI`: one witness name removed from its
  coverage-index gap note → RED naming the MsgType and witness.
- [ ] T073 [US4] (orchestrator) Perturbation, scratch `CAT`: one pin or live-cell ID removed from its
  row's Tests cell → RED naming it.
- [ ] T074 [US4] (orchestrator) Perturbation, scratch `CAT`: A-033 or C-003's Spec ref reverted to its
  old category → RED.
- [ ] T075 [US4] (orchestrator) Perturbation, scratch `CAT`: A-030 or N-003's Spec ref reverted to its
  old heading spelling → RED.
- [ ] T076 [US4] (orchestrator) Perturbation, scratch `CONST`: the amended §VI.2 sentence removed →
  RED ("§VI.2 is not amended").
- [ ] T077 [US4] (orchestrator) **SC-004 census**: T004's `catalogue_census.py` on the merge-base's
  catalogue and on the head's; OFFICIAL open falls by 12, done rises by 12, and `--list` differs only
  in the 12 tail rows. Every output in the verify record.
- [ ] T078 [US4] (orchestrator) **Commit C-RECORDS** (`spec/feature-catalogue.md`,
  `spec/coverage-index.md`, `spec/behaviors-and-limitations.md`), after the placeholder grep. C-AMEND
  precedes it in `git log`.

**Checkpoint**: all 12 rows `done` on the branch; the amendment is in, PENDING.

---

## Phase 7: Polish, verify, gates and close-out (plan P8, P9)

### `/simplify` (`[const §XVI.7]`, before verify)

- [ ] T079 (orchestrator) **Run `/simplify`** on the implementation diff (`git diff origin/main..HEAD -- tests`
  and the uncommitted parent harness diff). The orchestrator triages; `phase-implementer` applies
  in-scope fixes (parent edits stay uncommitted, copy refreshed). A change to the witness TU re-runs
  T024 and the W6 arms its edit touches; to the routing cell, T040–T041; to the live cell, T052–T053.
  Record the triage in the verify record.

### Instruments (each with its known positive)

- [ ] T080 [P] (orchestrator) **Line citations and comment claims.** `python3 tools/check_line_citations.py --range origin/main..HEAD`
  and `--shift-audit origin/main..HEAD` pass; `python3 "$W/.claude/scripts/check-comment-claims.py" --root "$PWD" --base origin/main`
  passes. Read what the lexical check cannot see: the witness TU header (W7: rule and recipe, no
  result), the cell's comments, the harness comments.
- [ ] T081 [P] (orchestrator) **Every tier1 source-audit job, run locally.** Read `.github/workflows/tier1.yml`
  with a YAML parser; run each ungated audit job's `run:` blocks (`ci-script-pins`, `check-layers`,
  `co-spawn-closure-audit`, and any other) as Actions would (`bash -e`). Record a job → rc table in
  the verify record.
- [ ] T082 [P] (orchestrator) **Closing-keyword check.** Positive controls first: seed `Closes #625` and
  `Resolves https://github.com/CatalinSerafimescu/fixpp/issues/626` into a scratch copy of one message;
  the grep hits both. Then `git log origin/main..HEAD --format=%B | grep -inE '\b(close[sd]?|fix(e[sd])?|resolve[sd]?)\b([: ]+([a-z0-9_.-]+/[a-z0-9_.-]+)?#[0-9]+|\s*:?\s*https://github.com/[^ ]+/issues/[0-9]+)'`
  prints nothing.
- [ ] T083 [P] (orchestrator) **Static analysis on test code** (`[const §IX.4]` row of plan Constitution
  Check): clang-tidy (`-p build/linux-clang-debug`; each touched TU shown in `compile_commands.json`,
  else `SKIPPED (no compile command)`), clang-format `--dry-run --Werror`, cppcheck with verify
  Appendix A's flags and `cppcheck-suppressions.txt`, include-what-you-use — on every touched C/C++
  file under `tests/session/`, `tests/codegen/`, `tests/interop/happy/`. A missing tool is recorded
  `SKIPPED (tool not installed)`. `/speckit-verify` Step 1 may carry this; it is listed so it cannot
  be skipped.
- [ ] T084 [P] (orchestrator) **SC-005 at the head**: T006's command prints nothing for
  `origin/main..HEAD`.

### `/speckit-verify` (`[const §XVII.8]`)

- [ ] T085 (orchestrator) **Run `/speckit-verify 095-tail-evidence-pass`** in the main checkout. It
  writes `.specify/decisions/095-tail-evidence-pass-verify.md` with a GREEN or YELLOW verdict. Its
  evidence, at the post-simplify head:
  - quickstart §1 (T024), §2 (T039), §3 (T049), §4 (`ctest -L '^095$' --no-tests=error` against
    `expected-ctest-095.txt`), §5 (T057's local live run and its peer identities), §7 (T068) and
    SC-004 (T077);
  - every RED arm with its command and the message that fired: T013–T017, T025–T034, T040–T041,
    T049–T050, T053–T056, T059–T060, T065, T069–T076 (SC-002);
  - T080–T084's results; coverage `[const §IX.1]` n/a per touched module (no library line);
  - MSVC stated plainly as not run locally;
  - **one FULL `ctest --preset linux-clang-debug --output-on-failure --no-tests=error`, run LAST**, on a
    committed tree, after every selected run: before it `df -h /mnt/e`, `cat /mnt/c/temp/fixpp/.sandbox-lock`
    (someone else's lock → ask the owner), and the `lane613.build.active` marker check; detached with a
    `.done` marker. A full `ctest` regenerates `_codegen` in the same tree, so any build or run after
    it rebuilds first. T087's `local build: green` line cites its SHA.
  A row whose evidence did not hold is taken out of the flip set (its row reverted by a docs task)
  and the owner told.
- [ ] T086 (orchestrator) `codegraph sync` in this tree after the last code-changing commit; read
  back `codegraph status --json`.

### PR, gates, ratification, merge and post-merge

> T087–T089 run before T105's audit (T088 → T104 → T089 → T105). T090–T103 run AFTER the audit,
> chronologically, and are numbered before the close-out tasks so that T105 stays FINAL (recorded
> deviation, header); each of them is open at the audit, under the waiver written in it.

- [ ] T087 (orchestrator) **Draft the PR description** in the parent's `research/reviews/` scratch
  area: title exactly `Constitution: amend §VI.2, §VI.5 — message-level Spec refs for application-message rows (v4.1) (+ 095 tail evidence pass)`
  (FR-021); body states the change, the rationale, the affected rows and specs; discloses what the
  PR's own `interop-live` will show (plan, "What the fixpp PR's `interop-live` shows": compiles the
  new source, then "population reconciliation FAILED -- not running any cell"; advisory, not SC-003
  evidence) and the post-merge windows (R-10); `## Gate A outcome` (rounds 1–3, the post-exhaustion
  passes, the scoped amendment rounds) and `## Gate B outcome` with the placeholder `Gate B: pending.`
  under which the verify record is linked; `local build: green on linux-clang-debug @ <sha>` from
  T085; T082's grep over the body, so `closingIssuesReferences` is empty. T096 applies the final
  body.
- [ ] T088 (orchestrator) **Re-run T080–T082 as the last action, then push and open the PR without
  gate labels**: `git push -u origin 095-tail-evidence-pass`; `gh pr create` with T087's title and
  body. Read `closingIssuesReferences` (empty) and record the PR number. Read the ungated checks; a red
  one goes back through the fixer. The PR's `interop-live` result is recorded as advisory. T104
  follows (it needs the PR number).
- [ ] T089 (orchestrator) **Verify-delta check and the pre-audit tick commit**, after T104's C-PRCELL:
  `git diff --name-only <T085 full-ctest SHA>..HEAD` lists only `spec/` and `specs/` paths; the
  verify record states the delta is docs-only (otherwise T085's full `ctest` re-runs last). Then
  commit the `tasks.md` ticks of T001–T089 and T104 (`specs/` only, placeholder grep and T082's
  grep first), so T105's check (i) reads committed state, re-run T080–T082 and push.
- [ ] T090 (orchestrator) **Amendment review check** (FR-021, `[const §XX.2]`). The scoped Codex Gate A
  review of the amendment converged at round 6 (plan `## Gate A`). Diff the two clause bodies in
  `.specify/constitution.md` against data-model §F.1 at this point: identical → tick this task, record
  "no clause-body change since round 6". Any clause-body change since round 6 → one more narrow
  scoped Codex round with an Opus judge on that change, recorded in plan `## Gate A` and the Gate A
  record, before the RATIFIED flip. A records-only change (Status line, SIR outside the clause
  bodies) needs only Gate B's re-check.
  - *Open at the audit — waiver: it is a post-audit check by construction (it compares the head that
    Gate B reviews).*
- [ ] T091 (orchestrator) **Run `/gate-b 095-tail-evidence-pass`.** First `git fetch origin`; if
  `origin/main` moved past T001's base, merge it (`phase-implementer` resolves code/build conflicts),
  then re-run T080–T084 and record the new base. Preconditions: T085 non-RED, T105's `## Completeness`
  present, `check_bl_delta.py gate` green. Fixer: `phase-implementer` for code/test findings, the
  orchestrator for docs; a parent-harness fix stays uncommitted (copy refreshed). After a fix that
  touches a non-docs path, re-run the affected arms and T085's full `ctest` last, under the same
  pre-checks. Gate B also reviews the PENDING amendment text. The Gate B record carries the B&L delta
  disposition (T067's rows).
  - *Open at the audit — waiver: Gate B needs the audit record first (pre-flight 4d).*
- [ ] T092 (orchestrator) **SecondBrain pass on the branch** (pipeline step 15a; close-out row 3), after
  Gate B converges: brain pages that describe the touched surfaces (`brain/components/` for session,
  interop, codegen tests) are checked and updated if needed; both freshness halves run locally.
  - *Open at the audit — waiver: after Gate B.*
- [ ] T093 (orchestrator) **Sign-off packet, then ASK THE OWNER** for the Art. XX §2 sign-off: the
  amended §VI.2 and §VI.5, the PENDING SIR and Status line as on the PR head, the Gate A tallies
  (including the scoped amendment rounds), Gate B's rounds, and the RATIFIED-form text T095 will write
  (data-model §F.3, §F.4). Nothing in T094–T097 runs until the owner answers explicitly. A request to
  change clause text goes back through T090. Record the answer and its date in the verify and Gate B
  records.
  - *Open at the audit — waiver: Art. XX §2 orders it after Codex Gate A and Gate B.*
- [ ] T094 (orchestrator) (after sign-off) **Commit the `tasks.md` ticks of T090–T093 and T105**
  (T001–T089 and T104 were committed by T089); the ticks of T095–T103 go in the Gate B record's
  close-out table, so T095 stays the last commit.
  - *Open at the audit — waiver: after T093.*
- [ ] T095 (orchestrator) (after sign-off) **Commit C-RATIFY, the LAST commit before merge**
  (FR-021): `.specify/constitution.md` only — the SIR to data-model §F.3's RATIFIED form (header
  `— RATIFIED`; first paragraph "Article XX §2 sequence observed in order: …" with each round's tally and
  the sign-off date; "Review and sign-off" filled with the rounds, Gate B rounds on PR #<n>, sign-off
  date) and the Status line to §F.4's RATIFIED form. Only these record lines change. The multi-line
  paragraph is anchored by position (SIR header line to the line before `  Bump:`), printed with
  `sed -n` and confirmed before replacing. `git fetch origin` first; if `origin/main` moved, repeat
  T091's integration so C-RATIFY stays last. Checks on the flip head: the placeholder grep (baseline
  `git show origin/main:.specify/constitution.md`; positive control on data-model.md) prints nothing
  new; T080 re-run.
  - *Open at the audit — waiver: only after the owner's sign-off.*
- [ ] T096 (orchestrator) **Body → labels → push last.** Choose labels from the verify verdict at this
  head (GREEN → `gate-a-done` + `gate-b-done`, each with its convergence record; YELLOW, or GREEN with
  gate-level waivers → the `-waived` label, with the waiver rationales in the verify record and under a
  literal `## Gate A …` / `## Gate B …` heading in the body; RED → no label). First edit the body to
  its final form (sign-off date, verified SHA, Gate B outcome replacing `Gate B: pending.`, which
  `grep -cF` must then count 0); for a `-waived` label run the workflows' heading awk locally, with
  its known positive. Then apply the labels in ONE `"$W/.claude/scripts/gh-pr-meta.sh"` call and read
  them back. Then push C-RATIFY. Never cancel a "duplicate" gate-label wave.
  - *Open at the audit — waiver: after T095.*
- [ ] T097 (orchestrator) **Merge (owner-driven) and the fixpp close-out.** Before merging,
  `closingIssuesReferences` empty and all three tiers plus both gate checks green on the C-RATIFY
  head; if `--admin` is needed, give the owner a one-line `!` command with `--body-file`. After the
  merge, `.specify/close-out.md` §3's table in the Gate B record, every row DONE or N/A:<reason>: row 5
  labels read back; row 9 none closed, #462, #613, #625, #626 read OPEN; row 16 the main checkout
  reset (`git switch main && git merge --ff-only origin/main`, only if this tree's branch is this PR's
  and `git status --porcelain` is clean; the uncommitted parent harness edits are outside the
  submodule and survive it); row 17 purge the branch locally and on the remote; row 18 N/A (the lock
  was never taken). Then `codegraph sync`.
  - *Open at the audit — waiver: post-merge.*
- [ ] T098 (orchestrator) **POST-MERGE: confirm the harness edits are intact** before the parent
  commit: `diff -r` the outside-tree copy against the parent working copy; `git -C <parent> status --short -- research/G19-fix-fpml-iso20022/phase-9-harness`
  lists exactly the files of plan "Files changed" (parent table) plus the two goldens.
  - *Open at the audit — waiver: post-merge.*
- [ ] T099 (orchestrator) **POST-MERGE: the ONE parent commit** (FR-015, research R-10 step 4): every
  R-9 harness change, the two goldens, and the gitlink bump to the merged fixpp `main` (or the harness
  changes alone, if another parent commit already bumped the gitlink to a library commit containing
  the merge). `git fetch`; check `git diff --cached --name-only` holds nothing that is not ours;
  stage with `git add` (explicit paths), commit **without** a pathspec; then `git ls-tree HEAD research/G19-fix-fpml-iso20022/library`
  equals `git -C research/G19-fix-fpml-iso20022/library rev-parse HEAD` and `git cat-file -t` says `commit`.
  - *Open at the audit — waiver: post-merge (owner ruling "Keep #462").*
- [ ] T100 (orchestrator) **POST-MERGE: push parent `main`** (owner authorization 2026-10-24; it
  carries any unpushed parent commits): `ls-tree` re-checked first; then `harness-tests` green on that
  commit; wait for `publish-counterparties` and record the image digest; re-dispatch the parent
  `interop-matrix` if its push run started before the publish finished, and record the post-publish
  run.
  - *Open at the audit — waiver: post-merge.*
- [ ] T101 (orchestrator) **POST-MERGE: dispatch `interop-live` on fixpp `main`** (`gh workflow run interop-live.yml --ref main`;
  it has no `push` trigger). It must list `HP-QFcpp-init-fixt11-fix50sp2-app-bn` and
  `HP-QFj-init-fixt11-fix50sp2-app-bn` as passed. Record the run id and the pulled image digest in
  the Gate B record's close-out table (the hello carries `local-build:unpinned`, data-model §D). If
  either cell does not pass, the close-out stops and goes to the owner (SC-003).
  - *Open at the audit — waiver: post-merge.*
- [ ] T102 (orchestrator) **POST-MERGE: parent bookkeeping** — parent `REMAINING-WORK.md` FUNCTIONAL
  TRACK row 2 marked done; `phases/phase-4.md` dashboard; a project memory note with its `MEMORY.md`
  line; parent commits only, `ls-tree` after each.
  - *Open at the audit — waiver: post-merge.*
- [ ] T103 (orchestrator) **POST-MERGE, on fixpp `main`: the `CLAUDE-history.md` entry** (newest
  first) and `CLAUDE.md`'s "Last merged FEATURE" pointer: the feature, the PR, the v4.1 amendment, the
  12 flips, the four narrowings and four Spec-ref corrections, fixpp#625 and fixpp#626, the
  `interop-live` run id. A `.md`-only push to `main`; push it right away. Then re-assert T105's
  checks in the verify record's "Closure at the final head" subsection, including SC-003's CI half
  from T101.
  - *Open at the audit — waiver: post-merge.*

### Mandatory close-out tasks (Gate-B preconditions, Article XVII §8)

- [ ] T104 (orchestrator) **Catalogue close-out** (after T088, before T105 and T091). The 12 flips
  themselves land in US4 (T062–T064, C-RECORDS), because quickstart §7 and `/speckit-verify` must see
  them; this task fills what could not exist then and re-checks the rest:
  - fill the PR cell of the 12 rows with T088's PR number, commit C-PRCELL (`spec/feature-catalogue.md`
    only, placeholder grep first);
  - extract each of the 12 rows and diff it against data-model §E: Status `done`, `/specify`, PR,
    Tests (the data-model §A witness IDs, the N-002/N-003 pins and routing cell, A-018's live cells),
    Verified (the offline-bar sentence; A-018's live cells and `ExecutionAck`; the four narrowed rows'
    "no typed tier" sentences; N-002's `kN002N003Excluded` / fixpp#625 sentence); the four FR-018
    Spec refs; every other column byte-identical to `origin/main`;
  - `spec/coverage-index.md`: each of the 23 tail MsgTypes' existing row names its witness in its
    gap-note column (quickstart §7's coverage-index loop), and no row was added;
  - re-run quickstart §7 and data-model §E's recipe on the C-PRCELL head: both pass.
- [ ] T105 (orchestrator) **Feature-completeness audit (MUST be the FINAL task).** Assert against the
  branch head:
  - **(i)** every `tasks.md` row through T089, and T104, is `[X]` in the committed head (T089's tick
    commit) or carries an explicit waiver; `git diff --quiet HEAD -- specs/095-tail-evidence-pass/tasks.md`
    first, so an uncommitted tick cannot pass. T090–T103 carry their own waivers; T105's own
    completion is evidenced in the verify record and its tick lands with T094;
  - **(ii)** every FR-001…FR-021 (with FR-004a) and SC-001…SC-005 maps to a landed test and its
    implementation (test name → TU → recorded GREEN, and its RED arm where SC-002 demands one), landed
    text, or a landed commit. Items that cannot hold yet carry an explicit waiver, each reading "waived
    at audit: satisfied after Gate B by T0xx, re-asserted in the Closure subsection": SC-003's and
    FR-015's post-merge halves (T099–T101); SC-004's "on `main` after merge" (T097); FR-021's scoped
    Gate A convergence, owner sign-off and RATIFIED flip (T090, T093, T095); FR-021's PR title and
    body (T088, T096);
  - **(iii)** every feature-owned catalogue row and coverage-index row is as T104 recorded it;
  - **(iv)** the tasks ↔ FR/SC ↔ catalogue map: each of the 12 rows → its Tests-cell IDs → each ID
    resolved in its binary (`--gtest_list_tests`) or the harness `CELLS` → its FR → its task.
  Record the verdict (100 %, or every shortfall paired with its waiver rationale) in
  `.specify/decisions/095-tail-evidence-pass-verify.md` `## Completeness`. `/gate-b` pre-flight 4d
  hard-blocks without it. After T095, re-assert (i)–(iv) at the C-RATIFY head, and after T101 at the
  post-merge state, each in a "Closure" subsection.

---

## Dependencies and execution order

### Phase dependencies

- **Setup (T001–T006)**: T001 first; T002–T004 and T006 read-only and parallel; T005 before any build.
- **Foundational (T007–T019)**: T007 → T008 → T009 → T010, T011, T012 (same TU, serial) → T013–T017
  (each needs its known positive) → T018 → T019. **Blocks every US1 task and T043.**
- **US1 (T020–T036)**: T020 → T021 → T022 → T023 → T024 → T025…T034 (one mutant at a time; T030
  needs T014's Mutant C; T031 and T032 may share a rebuild) → T035 → T036.
- **US2 (T037–T042)**: after T005; independent of US1 by file, but shares the build tree, so its
  builds wait for the US1 build owner. T037 ‖ T038 → T039 → T040 → T041 → T042.
- **US3 (T043–T058)**: T043 after T019; T044 ‖ T045 ‖ T046 ‖ T047 (parent files) → T048 → T049 (needs
  T043 and T048's jar) → T050; T051 after T049 passes on BN → T052 → T053; T054 (needs T044, T047) →
  T055 → T056 → T057 → T058.
- **US4 (T059–T078)**: after T058 (T062 cites test IDs that must exist; T068 resolves the live-cell IDs
  in the uncommitted `CELLS`). T059 ‖ T060 → **T061 (C-AMEND) before every catalogue edit** → T062 →
  T063 → T064 → T065; T066 ‖ T067 after T061; T068 after T062–T067 → T069…T076 → T077 → T078.
- **Polish**: T079 → T080–T084 (parallel) → T085 → T086 → T087 → T088 → **T104** → T089 → **T105** →
  T090 → T091 → T092 → **T093 (owner)** → T094 → **T095 (last commit)** → T096 → T097 → T098 → T099 →
  T100 → T101 → T102, T103.

### Critical path

T001 → T005 → T007…T019 → T020…T036 → T043 → T048 → T049 → T051…T058 → T059…T078 → T079 → T085 →
T088 → T104 → T105 → T091 → **T093 owner sign-off** → T095 → T096 → T097 → T099 → T100 → T101.

### Parallel opportunities

- T002 ‖ T003 ‖ T004 ‖ T006 (read-only).
- T037 ‖ T038 (two test files), single build owner.
- T044 ‖ T045 ‖ T046 ‖ T047 (four parent harness files; copy refreshed after each).
- T066 ‖ T067 (coverage index vs B&L), after T061.
- T080 ‖ T081 ‖ T082 ‖ T083 ‖ T084.

## Parallel example: User Story 3, parent harness

```text
Task: "T044 [US3] (implementer) phase-9-harness/tools/run_interop_cell.py + test_run_interop_cell.py — L0 registration, per-cell dictionary"
Task: "T045 [US3] (implementer) phase-9-harness/tools/interop_capability_minimums.yaml — two entries"
Task: "T046 [US3] (implementer) phase-9-harness/quickfixj/.../InteropCounterparty.java — A-TESTREQ check"
Task: "T047 [US3] (implementer) phase-9-harness/tools/emit_matrix.py + test_emit_matrix.py — run_kind rows"
```

## Implementation strategy

- **MVP = Foundational + US1**: the presence check proven able to fire, then 23 witnesses and the
  census. That alone is the offline bar for all 12 rows.
- **Then US2**: the pins and the routing cell (N-002/N-003's per-row evidence).
- **Then US3**: the parent harness edits held uncommitted, the selection check, the live cell, its
  local evidence.
- **Then US4**: the amendment commit, then the records, checked by quickstart §7 and data-model §E's
  recipe.
- **Then** `/simplify`, verify, the PR, the catalogue close-out and audit, the scoped amendment round,
  Gate B, sign-off, the RATIFIED flip last, merge, and the post-merge parent commit and `interop-live`
  dispatch that complete SC-003.
