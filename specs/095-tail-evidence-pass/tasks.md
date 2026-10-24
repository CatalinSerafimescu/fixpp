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
finding. **Gate A converged** (plan `## Gate A`; FR-021's Codex review of the §VI.2/§VI.5 amendment is discharged).
*(Superseded in part 2026-10-24: the owner folded a `[const §XVIII.7]` correction into v4.1
(data-model §F.5). A narrow scoped Codex round on it is owed before implement; plan `## Gate A`.)*
*(Discharged 2026-10-24: narrow scoped round 7 on §F.5 converged with no finding; plan `## Gate A`.)*

**Preconditions (pipeline steps 6–9, not tasks of this file)**: `/speckit-analyze` has run over spec,
plan and this file and its CRITICAL/HIGH findings are remediated; `/speckit-checklist-audit` has
dispositioned every checklist under `specs/095-tail-evidence-pass/checklists/`; the narrow scoped
Codex round on data-model §F.5 (`[const §XVIII.7]`) has converged (round 7, no finding), logged in
plan `## Gate A`. The
audit blocks `/speckit-implement`, so it cannot be a task inside the file it gates.

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
  walk returns a non-`ok` status for a tail MsgType; the parser refuses a deficient frame, or splits
  E-nodelim, CC-nodelim or CC-count2 into instances otherwise than contracts W6 states (an expected
  list is never rewritten to fit the parser); the parser refuses a witness frame the typed builder
  produced (a builder/parser disagreement, FR-020); a closure-required set contains a tag the
  framing writes (contracts W3); a required group has no string or int member to carry an
  instance-distinguishing literal (data-model §C); a closure-required member has no typed `Args`
  member; FR-014's check disagrees on BN; the typed builder refuses a build a witness or an arm
  needs; an inbound BW does not reach `fromApp` on the unmutated routing fixture (spec FR-008). A
  parse error or a build failure is never recorded as an arm's RED.
- **Mutant protocol (contracts W6).** Apply the scratch edit; rebuild only the affected target and
  show the rebuild took the edit (the build log relinks the target, and the binary's mtime is newer
  than the edited file's; record both), so a stale binary cannot report the pre-mutant result;
  rerun the task's filter; record the command and the message that fired (it must be the one the
  task names); revert, rebuild the same way; `git diff --exit-code` over the touched paths. Mutants
  are never committed.
- **Evidence file (pre-verify).** `/speckit-verify` (T085) writes
  `.specify/decisions/095-tail-evidence-pass-verify.md` whole, replacing what is there. So every
  "record" in a task before T085 goes to `.specify/decisions/095-tail-evidence-pass-evidence.md`
  instead (the parent's tracked `decisions/speckit/`, through the symlink), never to the verify
  record. The orchestrator writes it; an implementer task's report carries the entry. Each entry
  names its task ID, the command, its output or the message that fired, and where it ran: the
  library `git rev-parse HEAD` (with `git status --porcelain` empty, or the applied scratch mutant
  named), and for a run that reads an uncommitted file (the parent harness, or a fixpp file before
  its commit), `sha256sum` of each such file. T085 copies its entries into the verify record by hand, under its carry rule. After T085, every task that writes the
  verify record (T089, T091, T092, T093, T103, T105) appends the same text to the evidence file, so a
  `/speckit-verify` re-run copies them in again rather than erasing them.
- **Builds (`[const §XVII.7]`).** T005's owner ruling covers them, or an `AskUserQuestion` precedes the
  build. The only preset tree built locally is `linux-clang-debug` (owner ruling 2026-10-24; T085
  waives every other leg). For every heavy build: `export CCACHE_DIR=/mnt/wsl/fixppbuild/ccache`; `-j2`; one build
  owner per build directory; first check
  `/tmp/claude-1000/-home-catalin-Work-Programming-Antreprenoriat/94f182a9-92bf-450a-92fa-f65ed094e81d/scratchpad/lane613.build.active`
  (a parallel lane builds in `fixpp-parallel`: if the marker exists, wait or ask); run `df -h /mnt/e`
  before a build in the main checkout's `build/linux-clang-debug`. Long builds run detached
  (`setsid nohup <build> > <log> 2>&1; touch <marker>.done`) with ONE background waiter on the
  marker, never a foreground poll loop.
- **The MSVC sandbox** is not used. Before any full `ctest` (packaging entries compile consumer
  projects), `cat /mnt/c/temp/fixpp/.sandbox-lock`; a lock held by someone else means ask the owner
  first. The verify record states plainly that MSVC was not run locally (`windows-msvc-debug` and
  `windows-msvc-asan` are among T085's pre-recorded waivers).
- **The parent working copy (owner ruling "Keep #462: register after merge.", research R-10).** Every
  `phase-9-harness/` change stays an uncommitted edit in the parent working copy until T099. Nobody
  stages a `phase-9-harness/` path before T099. A copy of every changed and new harness file is kept
  under `/tmp/claude-1000/-home-catalin-Work-Programming-Antreprenoriat/94f182a9-92bf-450a-92fa-f65ed094e81d/scratchpad/095-harness-copy/`,
  refreshed after every harness edit. Any pre-merge parent commit (decision records live in the parent
  through the `.specify/decisions/` symlink) checks `git status` and `git diff --cached --name-only`
  first, stages explicit paths only, and runs `git ls-tree HEAD research/G19-fix-fpml-iso20022/library` after.
- **No closing keyword.** No commit message or PR body puts "close", "fix" or "resolve" (any form,
  a negation included) next to #462, #613, #625 or #626. This PR closes no issue.
- **No placeholder number is committed.** The slots are those data-model §E and §F.3 define:
  `<pr>`, `<gate-b rounds>`, `<sign-off date>` and `<routing TU>`, plus `fixpp#<`. Before every
  commit, `git grep -nE 'fixpp#<|<pr>|<gate-b rounds>|<sign-off date>|<routing TU>'` over `spec/`
  and `.specify/constitution.md` prints nothing new against `origin/main`. Its positive control: the
  same grep over `specs/095-tail-evidence-pass/data-model.md` prints the slot lines. A generic `<…>`
  pattern cannot replace it: the amended §VI.2 clause itself contains `35=<MT>` and `<DocAbbrev>`.
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

- [X] T001 (orchestrator) **Base and tree check.** In `research/G19-fix-fpml-iso20022/library`:
  `git rev-parse --abbrev-ref HEAD` reads `095-tail-evidence-pass`; `git status --porcelain` is clean
  apart from this bundle; `git fetch origin`; record `git merge-base HEAD origin/main` under the
  evidence file's `## Base` (Execution rules). In the parent: `git status --short -- research/G19-fix-fpml-iso20022/phase-9-harness`
  prints nothing (the harness baseline R-10 needs). `codegraph status --json` in this tree:
  `projectPath` is this tree, `fileCount` nonzero; `codegraph sync` if HEAD moved.
- [X] T002 [P] (orchestrator) **Issue placement, read-only.** fixpp#625 and fixpp#626 are already
  filed and placed (no filing task): `grep -n '#625\|#626' research/G19-fix-fpml-iso20022/phases/phase-4/issue-batches.md`
  (parent) shows both under their batch headings, and `gh issue view 625` / `gh issue view 626`
  (`--repo CatalinSerafimescu/fixpp --json state,labels`) read OPEN. Record both in the evidence file.
- [X] T003 [P] (orchestrator) **The bundle's own line-citation charge.** `python3 tools/check_line_citations.py --range origin/main..HEAD`
  and `--shift-audit origin/main..HEAD` both pass on the bundle commits before implementation starts.
  A hit is fixed in the bundle (orchestrator) before T007.
- [X] T004 [P] (orchestrator) **Scratch tools and the SC-004 baseline.** Copy the text of 094's
  `catalogue_census.py` out of `.specify/decisions/094-v1-tier0-bookkeeping-verify.md` into
  `/tmp/claude-1000/-home-catalin-Work-Programming-Antreprenoriat/94f182a9-92bf-450a-92fa-f65ed094e81d/scratchpad/tools/catalogue_census.py`
  (the 094 session's scratchpad path is gone). Show it can report non-zero: run it on
  `git show origin/main:spec/feature-catalogue.md` and record the OFFICIAL open / done figures and
  `--list open`, which must contain all 12 tail rows.
- [X] T005 (orchestrator) **Resource gate (`[const §XVII.7]`, plan phase 0).** `AskUserQuestion` for
  the builds of this feature: the `linux-clang-debug` tree in the main checkout, and no other preset
  tree (owner ruling 2026-10-24: local verify scope is `linux-clang-debug` only; T085 waives every
  other leg), including the interop targets T055a builds; the QuickFIX/J `mvn` builds (unpatched at
  T048, patched at T046); the docker pull; the full `ctest` of T085; and T085's re-run at the
  verified head of every arm its carry rule does not carry (every scratch-mutant arm recorded before C-US3: each
  mutant's rebuild of its target, `test_077_allversions_builder_roundtrip`, `session_pure_tests` and
  `interop_hp_fixt50sp2_logon_hb_logout_test`, and both peers' live runs for T053 (a)–(c), which need
  quickstart §5's docker extraction and the patched jar still present). Record the ruling in the
  evidence file. No build runs before it.
- [X] T006 [P] (orchestrator) **SC-005 instrument, known positive first.** Show that
  `git diff --name-only <range> -- src include tools/codegen` reports non-zero on a range that touches
  those paths (094's merge, `45a620f4~1..45a620f4`, touched `src/` and `include/`), then record that
  it prints nothing for `origin/main..HEAD`. T084 re-runs it at the verified head.

---

## Phase 2: Foundational — the walk, the presence check and the known positives (plan P0)

**Purpose**: the dictionary-derived check every witness asserts (FR-004), proven able to fire before
any witness relies on it. FR-014's selection check (T043) reuses the same walk.

**⚠️ CRITICAL**: no witness (US1) is written before T019's commit.

- [X] T007 (implementer) **The TU and its walk.** Create `tests/session/test_095_tail_rows_roundtrip.cpp`:
  - a header comment that records the rule (spec Terminology; research R-1) and the re-derivation
    recipe, and no derived count or member list (W7, FR-005);
  - the walk over a loaded `pugi::xml_document` (data-model §B), returning one `WalkResult` per
    MsgType with status `ok` / `nested_required_group` / `delimiter_chain` / `group_among_siblings` (naming the element),
    the closure-required fields and groups, each group's `count_tag`, `delim_tag` (first emitted tag
    in dictionary order, components expanded, the loader's rule in `src/dictionary/xml_loader.cpp`
    `expand_field_list`) and `delim_group`;
  - the body enumeration of research R-4 / contracts W4 (`OffsetTable::entries()`, framing tags and
    tag 35 removed, each entry attributed by extent to message level or to the outermost required
    group's `group_slices(count_tag)` instance), shared by the readback and the presence check;
  - `required_members_missing(...)`, data-model §B's legs, each missing path reported once, MsgType
    taken from the parsed frame's tag 35, returning an ordered sequence (a `std::vector`, duplicates
    kept) in §B's leg order;
  - a fixture `SetUpTestSuite` that loads `FIXPP_DICT_DATA_DIR "/FIX50SP2.xml"` once.
  The parse path is `fixpp::wire::Parser<Index>` over `XmlLoader().load(...)`'s `table_view`
  (`fixpp_test_support::parse_dict`, `tests/support/app_message_read_scaffold.hpp`). The TU
  includes per-message headers `fixpp/v50sp2/messages/<Msg>.hpp`, never `all.hpp` (R-2).
- [X] T008 (implementer) **CMake for the TU** in `tests/session/CMakeLists.txt`: add the TU to
  `test_077_allversions_builder_roundtrip`; link pugixml if the target does not already; add a compile
  definition naming `tools/quickfix_required_golden/golden.csv`. `tests/session/test_077_allversions_builder_roundtrip.cpp`
  stays byte-unchanged (FR-006, W8). Before the edit, take the before-list from a build without
  it (T005's ruling, Builds rule): `cmake --build --preset linux-clang-debug --target test_077_allversions_builder_roundtrip -j2`,
  then record `build/linux-clang-debug/bin/test_077_allversions_builder_roundtrip --gtest_list_tests`
  in the evidence file; T024 compares against it.
- [X] T009 (implementer) **The `095` label and its expected set.** `set_property(TEST <name> APPEND PROPERTY LABELS 095)`,
  with a comment in the 092/093 pattern, on `test_077_allversions_builder_roundtrip` and
  `session_pure_tests` (`tests/session/CMakeLists.txt`), `codegen_077_v50sp2_builder_completeness_test`
  (`tests/codegen/CMakeLists.txt`) and the ctest entry of `interop_hp_fixt50sp2_logon_hb_logout_test`
  (`tests/interop/CMakeLists.txt`). Each line goes in the block that defines its target: the
  `test_077_allversions_builder_roundtrip` line inside `tests/session/CMakeLists.txt`'s
  `if(TARGET fixpp_codegen_generate)` block, after that target's `set_tests_properties`, because
  outside it the line would name a test that does not exist when that target is absent. Reconfigure, then write `specs/095-tail-evidence-pass/expected-ctest-095.txt`
  from `ctest --preset linux-clang-debug -N -L '^095$'` (one ctest name per line, sorted; 093's
  format). Quickstart §4's `diff` prints nothing. No new ctest entry (`[const §VII.8]`).
- [X] T010 (implementer) **Field-leg known positive** `TailRows095Census.FieldLegMatchesQuickFixCppGolden`
  (W6 row "walk `ok`, field leg"): first assert `golden.csv` holds exactly one `FIX50SP2,<MT>,` row
  for each of the 23 tail MsgTypes and 35=E, then that the walk's message-level field set equals that
  row, read from the file at test time and never copied into the TU. GREEN expected. Record the
  command and its `[       OK ]` line in the evidence file.
- [X] T011 (implementer) **Deficient frames** `TailRows095Census.KnownPositive_DeficientFrames`
  (contracts W6): frames E-absent, E-one, E-short, E-member, E-nodelim, CC-nodelim, CC-nested, CC-count2 (literal
  bytes or typed-builder output, `NewOrderList.hpp` / `StreamAssignmentRequest.hpp`), each asserting
  the exact sequence `required_members_missing` returns (data-model §B's order, duplicates kept). The 35=E and 35=CC walk statuses are asserted
  `ok`. **First** confirm and report that the parser accepts every frame, that E-nodelim and
  CC-nodelim open instance 0 without its delimiter as W6 states, and that CC-count2's two
  nested instances parse as one outer instance (`consume_group_extent` / `group_slices_status`,
  `src/wire/offset_table.cpp`); otherwise stop (Execution rules). GREEN expected. Record in the
  evidence file the command, each frame's asserted list as the test states it, and the parser
  confirmation.
- [X] T012 (implementer) **Fail-loud known positives**: `TailRows095Census.NestedRequiredGroupFailsLoud_MassQuote`
  (status `nested_required_group` for 35=i and the nested group it names, both literals, without
  breaking `SetUpTestSuite` for the tail set) and
  `TailRows095Census.WalkStatusesOnSyntheticDictionaries` (inline `load_string` documents S1, a
  required component holding one field and one group → `group_among_siblings` naming the component;
  S2, a delimiter chain → `delimiter_chain` naming the inner group). GREEN expected. Record
  the command and each asserted status with the element it names in the evidence file.
- [X] T013 (implementer) **RED arm, field leg.** Scratch: drop one tag from the walk's field set for
  one tail MsgType (e.g. a component-flag check turned off). `"$B" --gtest_filter='TailRows095Census.FieldLegMatchesQuickFixCppGolden'`
  fails, the message naming the MsgType and both sets. Second scratch: add a MsgType absent from
  `golden.csv` to the asserted set; the row-count precondition fails naming it. Revert, `git diff --exit-code`.
- [X] T014 (implementer) **RED arm, Mutant C, frame half** (W6 row "group leg"). Scratch: the walk
  ignores the only-child inheritance. `--gtest_filter='TailRows095Census.KnownPositive_DeficientFrames'`
  fails on the `NoOrders`(73) assertions. Revert, `git diff --exit-code`. (Its second half, with Mutant
  A, is T030: it needs the A-027 witness.)
- [X] T015 (implementer) **RED arm, nested detector.** Scratch: the nested-required-group detector
  returns `ok`. `--gtest_filter='TailRows095Census.NestedRequiredGroupFailsLoud_MassQuote'` fails
  naming 35=i. Revert, `git diff --exit-code`.
- [X] T016 (implementer) **RED arms, synthetic statuses.** Scratch (a): the delimiter-chain check
  disabled → `WalkStatusesOnSyntheticDictionaries` fails in S2's status assertion. Scratch (b),
  separately: the group-among-siblings detector disabled → it fails in S1's status assertion. Each reverted, `git diff --exit-code`.
- [X] T017 (implementer) **RED arms, Mutants L, M, N and P** (one run each, `--gtest_filter='TailRows095Census.KnownPositive_DeficientFrames'`;
  each RED is the exact-sequence comparison of data-model §B):
  L — only the delimiter leg disabled → CC-nodelim's list loses `1499[0].453`; M — only the
  nested `!= 1` comparison disabled → CC-count2's list comes back empty; N — only the `delim_group`
  leg's nested-delimiter-absent disjunct disabled → CC-nested's list comes back empty; P — the field
  leg no longer skips `delim_tag` → E-nodelim's sequence carries `73[0].11` twice. Each reverted,
  `git diff --exit-code`.
- [X] T018 (implementer) **P0 GREEN run**: `--gtest_filter='TailRows095Census.*'` passes with no
  witness yet (the census test lands in T023); the existing 077 tests in the same binary pass.
- [X] T019 (implementer) **Commit C-P0** (the TU, the three CMake label edits, `expected-ctest-095.txt`),
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

- [X] T020 [US1] (implementer) Witnesses for A-018…A-029 in `tests/session/test_095_tail_rows_roundtrip.cpp`:
  `TailRows095.A018_ExecutionAck`, `TailRows095.A023_OrderMassActionRequest`,
  `TailRows095.A023_OrderMassActionReport`, `TailRows095.A027_TradingSessionListRequest`,
  `TailRows095.A027_TradingSessionList`, `TailRows095.A027_TradingSessionListUpdateReport`,
  `TailRows095.A028_MarketDefinitionRequest`, `TailRows095.A028_MarketDefinition`,
  `TailRows095.A028_MarketDefinitionUpdateReport`, `TailRows095.A029_SecurityListUpdateReport`,
  `TailRows095.A029_SecurityDefinitionUpdateReport`.
- [X] T021 [US1] (implementer) Witnesses for A-030…C-003 in the same TU:
  `TailRows095.A030_SettlementObligationReport`, `TailRows095.A031_AllocationInstructionAlert`,
  `TailRows095.A032_UserNotification`, `TailRows095.A033_StreamAssignmentRequest`,
  `TailRows095.A033_StreamAssignmentReport`, `TailRows095.A033_StreamAssignmentReportACK`,
  `TailRows095.C003_AccountSummaryReport`.
- [X] T022 [US1] (implementer) Witnesses for N-002/N-003 in the same TU:
  `TailRows095.N002_UserRequest`, `TailRows095.N002_UserResponse`,
  `TailRows095.N003_ApplicationMessageRequest`, `TailRows095.N003_ApplicationMessageRequestAck`,
  `TailRows095.N003_ApplicationMessageReport`.
- [X] T023 [US1] (implementer) **The census** `TailRows095Census.EveryTailMsgTypeHasExactlyOneWitness`
  (FR-004a, W1): the row → MsgTypes table of data-model §A as a literal; iterate the registered tests
  of suite `TailRows095` via `::testing::UnitTest::GetInstance()`; map each `<Name>` to a MsgType
  through the dictionary's `<message name=… msgtype=…>`; collect every finding (multiplicity 0,
  multiplicity > 1, unmapped name, extra MsgType, row mismatch) before asserting.
- [X] T024 [US1] (implementer) **US1 GREEN run** — quickstart §1, as a `bash -e` script: the count
  assertion (`TailRows095.*` lists 23); `--gtest_filter='TailRows095.*:TailRows095Census.*'` passes;
  `TailRows095Census.*` alone passes; `--gtest_shuffle --gtest_repeat=3` passes. The 077 tests of the
  binary pass (FR-006): `"$B" --gtest_filter='-TailRows095*'` passes, and
  `"$B" --gtest_filter='-TailRows095*' --gtest_list_tests` (`B` as in quickstart §1,
  `build/linux-clang-debug/bin/test_077_allversions_builder_roundtrip`) equals T008's before-list (the new TU adds only `TailRows095*` suites to the binary). Also `git diff --exit-code origin/main -- tests/session/test_077_allversions_builder_roundtrip.cpp`
  is clean (W8).

### RED arms (contracts W6; Mutant protocol; record each in the evidence file)

- [X] T025 [US1] (implementer) **Mutant D** — delete one intent entry, setter kept → RED in W4
  ("parsed, not intended") naming the path.
- [X] T026 [US1] (implementer) **Mutant H** — in `TailRows095.A018_ExecutionAck`, delete the CumQty(14)
  setter and keep its intent entry → RED in W4 ("intended, not parsed") naming the path; W5 stays
  green.
- [X] T027 [US1] (implementer) **Changed value** — in one witness, after the build and before
  `make_frame`, one character of one string field's literal in the built buffer changed to another
  letter, never a count or other structural value (contracts W6) → RED in W4 naming the path and both values. A literal changed before the build is
  not this arm: both sides would carry it.
- [X] T028 [US1] (implementer) **Mutant E** — cut `TailRows095.A029_SecurityListUpdateReport` to one
  field (setters **and** intent entries deleted) → RED in the intent-size assertion, with its message.
- [X] T029 [US1] (implementer) **Mutant B** — in `TailRows095.A018_ExecutionAck`, delete the
  ExecAckStatus(1036) setter **and** its intent entry → RED in W5 (not W4) naming `1036`. This is
  FR-004's "setter and intent entry both" known positive.
- [X] T030 [US1] (implementer) **Mutant A, and Mutant C on top** — in `TailRows095.A027_TradingSessionList`,
  delete the `trading_sessions` assignment and its intent entries → RED in W5 naming the group. Then,
  with Mutant A still applied, re-apply T014's Mutant C → that W5 RED disappears (the rule, not a
  list, decides). Record both runs in full: in the second, the A-027 witness passes and T014's
  `NoOrders`(73) deficient-frame assertions fail, which shows Mutant C is active and that the
  disappearance is its effect, not a second fault. Revert both; `git diff --exit-code`.
- [X] T031 [US1] (implementer) **Census Mutant G** — delete `TailRows095.C003_AccountSummaryReport` →
  census RED naming CQ with multiplicity 0.
- [X] T032 [US1] (implementer) **Census Mutants F, I, J and O** (F, I and J may share one rebuild
  with T031; O has its own rebuild and is applied alone; the record shows each message; the
  quickstart §1 count assertion is skipped for the runs of F, I and J, and the record says so): F — duplicate the A-018 witness as `TailRows095.A024_ExecutionAck` → RED naming BN with
  multiplicity 2 **and** the row mismatch (A024 against A-018); I — add `TailRows095.A018_ExecutionAcknowledgement`
  → RED naming the unmapped name; J — add `TailRows095.A001_NewOrderSingle` → RED naming 35=D as
  extra; O — rename `TailRows095.A027_TradingSessionList` to `TailRows095.A028_TradingSessionList` →
  RED naming BJ's row mismatch alone (multiplicity stays 1, and the listed count stays 23, so the
  count assertion runs for O). O's commands, with only O applied:
  `cmake --build --preset linux-clang-debug --target test_077_allversions_builder_roundtrip -j2`
  (Mutant protocol: show the relink and the binary's mtime); `B=build/linux-clang-debug/bin/test_077_allversions_builder_roundtrip`;
  quickstart §1's count line (`n=$("$B" --gtest_filter='TailRows095.*' --gtest_list_tests | command grep -c '^  ' || true)`;
  `test "$n" -eq 23`), which must pass; then `"$B" --gtest_filter='TailRows095Census.EveryTailMsgTypeHasExactlyOneWitness'`
  RED with BJ's row mismatch and no multiplicity finding. Revert, rebuild, `git diff --exit-code`.
- [X] T033 [US1] (implementer) **Mutant K** — the name → MsgType lookup returns another tail MsgType
  for one generated name → RED in that witness's tag-35 assertion naming both MsgTypes (the census
  goes RED too).
- [X] T034 [US1] (implementer) **Census order-freedom arm** — `"$B" --gtest_filter='TailRows095Census.EveryTailMsgTypeHasExactlyOneWitness'`
  alone, with Mutant G applied, is RED (the census reads registration, not runs). Revert.
- [X] T035 [US1] (implementer) Re-run T024 on the reverted tree; all GREEN; `git diff --exit-code`.
- [X] T036 [US1] (implementer) **Commit C-US1** (the TU only). Report the hash; orchestrator runs
  `codegraph sync` and ticks T020–T036.

**Checkpoint**: SC-001's offline half holds; every W6 row has a recorded RED.

---

## Phase 4: User Story 2 — N-002 / N-003 close the N-001 way (Priority: P1)

**Goal**: per-row registry pins and a routing witness, no production change (plan P2).

**Independent test**: quickstart §2.

- [X] T037 [P] [US2] (implementer) **The two pins** in `tests/codegen/test_077_v50sp2_builder_completeness.cpp`
  (FR-007, R-5): `BuilderCompleteness077V50sp2.N002_UserRequestUserResponseRegistered` asserts `"BE"`
  and `"BF"` are `builder_registry` keys and `BuilderCompleteness077V50sp2.N003_ApplicationMessageRequestAckReportRegistered`
  asserts `"BW"`, `"BX"`, `"BY"`; each also calls the entry and checks the built body starts `35=<mt>`.
- [X] T038 [P] [US2] (implementer) **The routing cell** `TailRouting095.InboundBeAndBwReachFromApp`
  (FR-008, R-5) in an existing `session_pure_tests` fixture TU under `tests/session/` (the implementer
  picks it and names it in the report): on an established session, one inbound 35=BE and one 35=BW;
  `Application::fromApp` called once per message with its MsgType, `fromAdmin` not called for
  them, no Reject sent. The BE and BW assertions are separate, so each leg fails on its own. The
  fixture runs with inbound validation off and a registered `Application` (spec FR-008); the report
  names the session's BeginString and dictionary. If BW does not reach `fromApp` unmutated, stop.
- [X] T039 [US2] (implementer) **US2 GREEN run** — quickstart §2's script (count assertions: 2 pins,
  1 routing cell). Instrument check (CLAUDE.md "prove the instrument can report non-zero"): scratch,
  one pin's expected key changed to a MsgType absent from the registry → that pin fails; revert.
- [X] T040 [US2] (implementer) **RED arm, BE leg** — scratch: add `"BE"` to `detail::is_admin_msgtype`
  (`src/session/msgtype_classifier.hpp`); rebuild `session_pure_tests`; `"$S" --gtest_filter='TailRouting095.*'`
  fails on the BE assertions only. Revert; `git diff --exit-code -- src`.
- [X] T041 [US2] (implementer) **RED arm, BW leg** — the same with `"BW"`; the BW assertions fail
  only. Revert; `git diff --exit-code -- src`.
- [X] T042 [US2] (implementer) **Commit C-US2** (the two test files). Report the hash and the routing
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
- [ ] T044 [P] [US3] (implementer) **Registration and dictionary digest, test-first** (R-9 items 1–2;
  `[const §VII.3]` applies to the parent harness, orchestrator ruling 2026-10-24) in
  `phase-9-harness/tools/run_interop_cell.py`. **First** write the tests in
  `phase-9-harness/tools/test_run_interop_cell.py`: the registration fields of both cells (contracts
  L0's `Cell` fields `binary`, `gtest_filter`, `config_template`, `combo_id`, `run_kind`, `arm`,
  `script_path`) and both dictionary cases of `_finalize`'s FR-011 check. Run them against the
  unmodified harness and record the RED (the cells absent from `CELLS`; the FIX50SP2 digest refused).
  **Then** register both cells with L0's values (`binary` `interop_hp_fixt50sp2_logon_hb_logout_test`;
  `gtest_filter` the cell's gtest id, contracts L0 "Gtest ids"; `config_template` the existing
  `*-acceptor-tls-fixt50sp2.cfg.in` of its engine; `combo_id` C1 on the QF-cpp cell, C3 on the QFJ
  cell; `run_kind="app-message-evidence"`; `arm="validation-off"`; `script_path` the new script); set
  `FIXPP_FIX50SP2_DICT_XML` at the `_run_initiator_cell` launch site; give `_finalize`'s FR-011 check
  a per-cell expected dictionary (`dictionaries/FIX50SP2.xml` for these cells, FIX44 for every
  other). The tests pass; record the GREEN. Re-derive the readers of `combo_id` and `run_kind`
  (contracts L0, Combo and `run_kind` rows) and record each with its effect on the two cells; a
  reader that is not neutral for them stops the task. Refresh the outside-tree copy.
- [ ] T045 [P] [US3] (implementer) **Capability minimums** (R-9 item 3): two entries in
  `phase-9-harness/tools/interop_capability_minimums.yaml`, keyed by the two cell ids, each copying
  the capability keys and minimum values of the entries for its own counterparty build (contracts
  L0): the QF-cpp cell from `CONV-PROBE-*` / `CONV-C1-*` (the QuickFIX-cpp counterparty), the QFJ
  cell from `CONV-C3-*` (the QuickFIX/J counterparty). Re-read the file's comments for which build
  each entry was derived from before copying. Refresh the copy.
- [ ] T046 [US3] (implementer) **QuickFIX/J A-TESTREQ blocker, RED first** (R-9 item 4; the same
  ruling as T044). Runs **after** T051 and T052's QF-cpp half: it needs the cell, T044/T045's
  registration and T048's **unpatched** jar.
  - **RED:** `python3 ../phase-9-harness/tools/run_interop_cell.py HP-QFj-init-fixt11-fix50sp2-app-bn --build-root build`
    against the unpatched jar, without `--update-goldens`. Record the symptom from the run's
    transcript, not only its exit status (a missing golden also exits red): after Logon the peer's
    outbound MsgSeqNum(34) skips one value, its next frame is the induction Heartbeat (35=0), and
    fixpp answers the gap with a ResendRequest (35=2). If that sequence does not show, stop and
    report: the blocker's premise is wrong.
  - **Fix:** in `phase-9-harness/quickfixj/.../InteropCounterparty.java`, move the conversation-mode
    seqnum bump and the Heartbeat inside the `A-TESTREQ` null check (the C++ twin's 089 T062a shape).
    Rebuild the jar (`mvn … package -DskipTests`, detached, marker). Refresh the copy.
  - **GREEN:** the same command; the recorded symptom is absent. The QFJ cell's capture and its full
    pass are T052's second half.
  - *(2026-10-24, premise correction, orchestrator ruling: the RED's predicted sequence did not show,
    because the cell logs out within the induction's 800 ms sleep; the observed symptom, a stray
    `OUT 0` after the peer's `LOGOUT` line, is the RED. See the evidence file, Phase 5, "T046 premise
    correction".)*
- [ ] T047 [US3] (implementer) **`emit_matrix` `run_kind` handling, test-first** (R-9 item 6; L0
  rows Select, Select `--conversation-only`, Promote, N2, Registration; the same ruling as T044).
  After T044: its tests need the two cells registered, so that each RED is the rule's and not an
  absent cell's. **First** add each test to `phase-9-harness/tools/test_emit_matrix.py` and run it against the
  unmodified `emit_matrix.py`, recording each RED: `declared_cell_ids` stubbed to omit the two rows →
  `select_cells` raises naming both (RED today: the probe rule admits them through combos C1/C3);
  `conversation_only=True` returns the new cells; promotion skipped for `"app-message-evidence"`,
  neither promoted nor flipped; N2 adds no row; and, inside class
  `RegistryAgreesWithTheRealTemplateTest`, a both-directions pin for the two cells modelled on its
  `test_the_recovery_inbound_cells_are_registered_AND_declared`. A test that passes before the change
  is recorded as such, with the reason (N2's producer already keeps only `"conformance"`); the
  pin's RED is T054's (the template rows do not exist until T055). **Then** change `emit_matrix.py`
  (the probe rule narrowed to `"validator-positive-control"`, the promotion skip); every test but the
  pin passes. Refresh the copy.
- [ ] T048 [US3] (implementer) **Peers and harness unit run.** `mvn -f ../phase-9-harness/quickfixj/pom.xml package -DskipTests`
  **before** T046's fix, so this jar is the unpatched one T046's RED needs (detached, marker);
  quickstart §5's docker pull / inspect / create / cp of the QF-cpp counterparty, recording the image
  digest; `cmp dictionaries/FIX50SP2.xml "$P/qfcpp-spec/FIX50SP2.xml"` prints nothing.
  `python3 -m pytest -q -p no:cacheprovider test_run_interop_cell.py test_emit_matrix.py -k 'not RegistryAgreesWithTheRealTemplate'`
  passes (the deselected class holds the real-template test and T047's pin, both RED until T055; T054
  records them). Parent `git diff --cached --name-only` prints nothing.

### fixpp live cell (plan P4)

- [ ] T049 [US3] (orchestrator) **Run the selection check** — quickstart §3 as a `bash -e` script:
  the unzip from the `~/.m2` jar T048's build fetched; the run passes (`[  PASSED  ] 1 test`); the
  input-absent arms FAIL: the variable unset (`env -u FIXPP_095_QFJ_DICT_DIR`), and the variable
  naming an empty directory. The printed axes go in the evidence file,
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
  - `tests/interop/happy/hp_fixt50sp2_app_message_test.cpp` (NEW): the two-parameter cell, with the
    gtest ids of contracts L0 "Gtest ids" verbatim (INSTANTIATE prefix, suite, test and param names);
    fixpp initiator; the cell's **first** statement that can skip or return is
    `INTEROP_REQUIRE_COUNTERPARTY(...)` (033's cell shape; any TLS-fixture or dictionary guard comes
    after it, contracts L1); after Logon one 35=BN built by `fixpp::v50sp2::build_ExecutionAck` from
    the R-7 field values fixed before the build, sent via `Engine::send`; MsgSeqNum captured in
    `toApp`; `validate_inbound_messages` off; the `sent` record and the comparison by
    `compare_streams` (decimal resolver over `FIX50SP2.xml`). The peer-Reject check is non-fatal
    (`EXPECT_*`, never `ASSERT_*`) and is evaluated whatever the comparison returns, so arm (b) of
    T053 can show its own message (contracts L5(b)). Its header comment is FR-013's "cell's
    record": inbound validation is off, and why (L-041-2: the session validator takes one
    dictionary). The verify record carries the rest, and the hello carries `arm="validation-off"`;
  - `tests/interop/CMakeLists.txt`: the new source joins `interop_hp_fixt50sp2_logon_hb_logout_test`
    through `target_sources` after the `fixpp_add_interop_test` call, inside an
    `if(TARGET fixpp_codegen_generate)` block with the `fixpp::builders::v50sp2` link, the codegen
    include and `add_dependencies(… fixpp_codegen_generate)` (the pattern of `interop_conversation_test`
    in the same file), so a configure without codegen leaves 033's target as it is;
    `target_compile_definitions` for `FIXPP_DICT_DATA_DIR` after the `fixpp_add_interop_test` call;
  - `tests/interop/conversation/fixt_app_script.yaml` (NEW): step F-01, 35=BN, originator fixpp,
    `applicable_combos: [C1, C3]`.
  - **Re-verify the readback order (contracts L4)** and record it in the evidence file: in each
    counterparty source, `phase-9-harness/quickfix-cpp/counterparty/interop_counterparty_main.cpp`
    and `phase-9-harness/quickfixj/src/main/java/io/fixpp/phase9harness/quickfixj/InteropCounterparty.java`,
    `grep -n` the `fromApp` definition, its generic readback call (QF-cpp `emit_readback`, QFJ
    `emitReadback`) and the stage-0 conversation send (`send_conversation_message("B-02"` /
    `sendConversationMessage("B-02"`), then read the `fromApp` body and confirm the readback call
    comes first, before the conversation-mode lookup. Record the commands and what they show. If
    either peer does not write the readback first, stop and report.
  033's own cells are unchanged. After the build, `--gtest_list_tests` on the target lists the two
  ids of contracts L0 exactly once each, and 033's `Fixt/HappyFixtLogonHbLogout.*` cases unchanged
  (compare with the list taken before the edit); record both lists in the evidence file.
- [ ] T052 [US3] (implementer) **Local run and goldens** — first run quickstart §5's environment and
  certificate lines (the `export` of `INTEROP_QFC_COUNTERPARTY_BIN`, `LD_LIBRARY_PATH`,
  `INTEROP_QFCPP_SPEC_DIR` and `INTEROP_QFJ_JAR` over T048's extraction and jar, `test -f "$INTEROP_QFJ_JAR"`,
  and `bash ../phase-9-harness/tls/gen-interop-certs.sh`); build the 033 FIXT target (Builds rule).
  Run each cell with `python3 ../phase-9-harness/tools/run_interop_cell.py <cell> --build-root build`,
  first capture with `--update-goldens` (R-9 item 5) into `phase-9-harness/golden/`; refresh the
  copy. In two halves: the QF-cpp cell first; then T046 (its RED against the unpatched jar, the fix,
  the rebuilt jar); then the QFJ cell against the patched jar. Each cell: exactly one passing
  comparison row; no peer Reject; a clean Logout, which two checks decide, both required:
  `hp::expect_graceful_stop` on the fixpp side (`Engine::stop()` within its bound, `stopped()`
  true) and the harness's `_logout_sync_step`, which on a capture requires the peer's trailing
  `LOGOUT` transcript line (`_logout_marker_required`). The orchestrator reviews both goldens by hand
  against these criteria and records each: the Logon carries BeginString FIXT.1.1 and `1137=9`; one
  35=BN from fixpp is present; neither side sends a 35=3; the golden ends with that `LOGOUT` line. A
  golden that fails one is not approved.
- [ ] T053 [US3] (implementer) **L5 RED arms**, each against both peers, scratch, reverted:
  (a) one value in `ExecutionAckArgs` altered after the `sent` record is fixed, the F-01 literal
  unchanged → RED (a comparison mismatch naming that field); (b) `args.exec_id` left unset,
  `build_ExecutionAck` called directly (never `validate_ExecutionAck`) → the peer rejects → RED, the
  record showing the peer-Reject check's own message naming the peer's 35=3 (a RED from the missing
  comparison row alone does not count); (c) the `Engine::send` call and the `sent` record skipped →
  RED in the exactly-one-row condition (contracts L5). (c)'s commands, with only (c) applied and
  quickstart §5's environment exported: `cmake --build --preset linux-clang-debug --target interop_hp_fixt50sp2_logon_hb_logout_test -j2`
  (Mutant protocol: show the relink and the binary's mtime); then, for each cell,
  `python3 ../phase-9-harness/tools/run_interop_cell.py HP-QFcpp-init-fixt11-fix50sp2-app-bn --build-root build`
  and `python3 ../phase-9-harness/tools/run_interop_cell.py HP-QFj-init-fixt11-fix50sp2-app-bn --build-root build`,
  never with `--update-goldens` (it would overwrite the golden); the record shows the
  exactly-one-row condition's own message (a golden-diff RED alone does not count). After the revert
  and rebuild, `diff` each of the two cells' goldens under `phase-9-harness/golden/` against its
  outside-tree copy: nothing differs. If the typed builder refuses (b)'s build, stop
  (Execution rules). `git diff --exit-code`
  in both trees' touched paths.

### Evidence files and pins (plan P5)

- [ ] T054 [US3] (implementer) **Real-template RED first.** With T044's registration in place and
  before any template row exists: `python3 -m pytest -q -p no:cacheprovider test_emit_matrix.py -k RegistryAgreesWithTheRealTemplate`
  (from `phase-9-harness/tools`) FAILS naming both cells, in both of the class's tests that read them
  (`test_no_registered_cell_is_undeclared_by_the_submodule_template` and T047's both-directions pin).
  Record it.
- [ ] T055a [US3] (implementer) **Skip-list gate, RED first** (`ci/assert-interop-skips.py`,
  which the ctest-interop steps of `tier1.yml`, `tier2.yml` and `tier3-libcxx.yml` run). Before T055
  adds the two skip lines, run that step's recipe on `linux-clang-debug`, reading it from
  `tier1.yml`'s "Interop gate" step (its count pin against `ci/expected-interop-tests.txt` is left
  out: that file has no `linux-clang-debug` line):
  - `unset INTEROP_QUICKFIX_CPP_PORT INTEROP_QUICKFIX_J_PORT INTEROP_QUICKFIX_CPP_HOST INTEROP_QUICKFIX_J_HOST TESTBRIDGE_TEST_ONLY`
    and every `GTEST_*` variable;
  - build every target `ctest --preset linux-clang-debug -N -L interop` names (an interop ctest name
    is its target name, `fixpp_add_interop_test`), except the pytest entry
    `interop_cell_results_schema_check`;
  - `--expected-count` is that list's length without the pytest entry;
  - `GTEST_OUTPUT="json:$D/" ctest --preset linux-clang-debug -L interop --no-tests=error --output-on-failure`
    into a fresh `D=$(mktemp -d)`;
  - `python3 ci/assert-interop-skips.py --json-dir "$D" --bin-dir build/linux-clang-debug/bin --expected-skips tests/interop/expected-skips-without-counterparty.txt --expected-count <n>`.

  Expected RED: exit 1, "unexpected skips" naming exactly the two new gtest ids and no other
  violation. Exit 2 is an instrument failure, never this RED; any other violation stops the task.
  Record the command, the count and the output; keep `$D`.
- [ ] T055 [US3] (implementer) **Template rows and skip list** (R-11): two rows next to 033's FIXT rows
  in `tests/interop/cell_results.yaml` (`id`, `config: normal`, `kind: happy`,
  `status: "skip:counterparty-unavailable"`, `matrix_disposition: live`, `spec_ref: "FIX-SL §5.3.1/§5.3.3"`).
  The status stays that pre-live value on the PR and after it: the file header's convention (live
  cells carry it; the parent overrides it with real results) and the precedent of the two
  `HP-QFj-*-fix44-recovery-inbound` rows fixpp#462 added (PR #474). The pass lives in the evidence file and
  the verify record (T057, T085) and in the Gate B close-out record (T101), never in this file
  (data-model §D). Two
  lines in `tests/interop/expected-skips-without-counterparty.txt`, the two gtest ids of contracts L0
  verbatim, each in sorted position (the file's FORMAT header). Then
  `python3 -m pytest tests/interop/cell_results_schema_check_test.py -q` FAILS on the `EXPECTED_IDS`
  pin (FR-016's RED first). Record it.
- [ ] T055b [US3] (implementer) **Skip-list gate, GREEN.** With T055's two lines in place, re-run only
  T055a's `assert-interop-skips.py` command over the same `$D` (the binaries are unchanged, so the
  pair is attributable to the two lines): exit 0. Record it.
- [ ] T056 [US3] (implementer) **The pin** — add the two ids to `EXPECTED_IDS` in
  `tests/interop/cell_results_schema_check_test.py`; the schema test passes with no other edit (no
  `CENSUS_KEY_COUNT`, e7a inventory, `CONV_COMBO_IDS` or `witness_evidence.yaml` change). The
  real-template test of T054 and T047's pin now pass.

### Local live evidence (plan P6; stands in for the PR's `interop-live`, contracts L7)

- [ ] T057 [US3] (implementer) **Quickstart §5 in full**, as a script: `python3 ci/run-interop-live.py --harness ../phase-9-harness --list`
  reconciles the whole population; `--list-binaries` builds; the full `python3 ci/run-interop-live.py --harness ../phase-9-harness --build-root build`
  ends "all … live interop cell(s) passed" (the two new cells, 033's FIXT cells, 089's CONV-C1…C4
  and PROBE cells against the patched QuickFIX/J); the schema test; both harness suites; the
  real-template test. Record in the evidence file: every output, the image digest, the jar's source
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

- [X] T059 [US4] (orchestrator) **Quickstart §7, RED run, before any record edit**: the script fails at
  its first Status check (no tail row is `done`). Record the message.
- [X] T060 [US4] (orchestrator) **Data-model §E recipe, positive control** on the unedited catalogue:
  it exits 1 and flags exactly A-030, A-033, C-003 and N-003, no other row. Record the output.

### Amendment (its own commit, before any catalogue edit)

- [X] T061 [US4] (orchestrator) **Commit C-AMEND** — `.specify/constitution.md` only, data-model §F
  verbatim: `## Article VI` §2 and §5 (§F.1, §F.2), `## Article XVIII` §7's two sentences (§F.5)
  (every before-text `grep -F`-matched first, exactly once), the Status line's PENDING form (§F.4),
  and the PENDING Sync Impact Report block above the v3.0 → v4.0 block (§F.3). No `CHANGELOG.md`
  change (MINOR). Copy only data-model text a converged scoped round has seen: §F.1 and §F.2 as
  round 6 converged on them, and §F.5 as narrow round 7 converged on it. Then
  `command grep -qF 'may instead use the message-level form `[DocAbbrev] Category`' .specify/constitution.md`
  and `command grep -qF 'are application messages, not a session-FSM pair' .specify/constitution.md`
  both succeed, and `git diff` shows no other changed line in `## Article XVIII`. **Byte check per
  clause**, since each clause is one line and a line diff cannot see a change inside it: write each
  before-text and after-text of data-model §F.1, §F.2 and §F.5 to its own scratch file, copied from
  data-model.md, never retyped. (a) §VI.2 and §VI.5: the amended line, extracted by heading and
  leading number, `cmp`s equal to its after-text file. (b) §XVIII.7: with `old7` the §7 line of
  `git show origin/main:.specify/constitution.md` and `new7` the working tree's,
  `python3 -c 'import sys; o,n,b1,a1,b2,a2=(open(f,encoding="utf-8").read().rstrip("\n") for f in sys.argv[1:]); sys.exit(0 if n.count(a1)==1 and n.count(a2)==1 and n.replace(a1,b1).replace(a2,b2)==o else 1)' old7 new7 before1 after1 before2 after2`
  exits 0. Record both checks; T090 repeats them at its head.

### Catalogue, coverage index, B&L (after T061)

- [X] T062 [US4] (orchestrator) `spec/feature-catalogue.md`, the 12 rows (data-model §E): Status `done`;
  `/specify` `095-tail-evidence-pass`; Tests the witness IDs of data-model §A (plus, for N-002 and
  N-003, the pin and `TailRouting095.InboundBeAndBwReachFromApp`; for A-018 the two live-cell IDs);
  Verified the text of data-model §E "Verified cells", byte for byte (the offline-bar sentence; A-018's
  generated `ExecutionAck` and live-cell sentence; N-002's and N-003's pin, routing and resend-path
  sentences, R-5). T063 adds the version sentences. The PR cell stays as on `origin/main` until T104.
- [X] T063 [US4] (orchestrator) **Version narrowing** (FR-018): FIX version(s) → `5.0SP2` for A-018,
  A-029, A-030 and N-002; each Verified cell ends with its version sentence of data-model §E, byte for
  byte (N-002's names `kN002N003Excluded` and fixpp#625).
- [X] T064 [US4] (orchestrator) **Spec-ref corrections** (FR-018): A-030 → `[FIX50SP1] Settlement Instruction`,
  A-033 → `[FIX50SP2] Market Data`, C-003 → `[FIX50SP2] Account Reporting`, N-003 → `[FIX50SP2] Application`.
  Every other Spec ref cell byte-unchanged.
- [X] T065 [US4] (orchestrator) **Data-model §E recipe after the edits**: exits 0. Then, on scratch
  copies (`CAT` under `mktemp -d`), each must exit 1 with its leg's message, output recorded:
  (a) A-030's token changed to `[FIX44]`; (b) one tail row's Title cell given `35=A`; (c) A-030's
  token changed to `[FIXT11]` (registry leg); (d) A-030's token changed to `[FIX51]` (file leg)
  (data-model §E, RED arms). Record the 2010 Repository `<CategoryID>`
  cross-check where the owner-local edition has the message.
- [X] T066 [P] [US4] (orchestrator) `spec/coverage-index.md`, data-model §E "Coverage-index edits"
  byte for byte: each tail MsgType's existing row names its witness in its gap-note column by the
  rule there (no new row, no row moved; CC, CD, CE, CQ stay under "Infrastructure"; FR-019), and the
  one superseder comment follows 067's "ART VI §3 DISPOSITION" comment; `command grep -cF 'SUPERSEDED (095-tail-evidence-pass' spec/coverage-index.md`
  prints 1.
- [X] T067 [P] [US4] (orchestrator) `spec/behaviors-and-limitations.md` delta (FR-013, FR-019,
  contracts L8): data-model §E's "B&L delta" section, byte for byte, at the end of the live file
  (B-095-1: what the live cell witnesses and the validation-off disclosure under L-041-2; L-095-1:
  FIX 4.4 BE/BF have no typed builder, fixpp#625), plus a row per exposure under §E's rule, if any. Then `python3 "$W/.claude/scripts/check_bl_delta.py" gate --feature 095-tail-evidence-pass`
  passes and `check_bl_citations.py gate` reports 0 dangling (`W=$(git rev-parse --show-superproject-working-tree)`).

### Records check (quickstart §7) and its perturbations

- [X] T068 [US4] (orchestrator) **Quickstart §7, GREEN** on the edited tree (with the uncommitted
  parent registration present, so the live-cell IDs resolve in `CELLS`).
- [X] T069 [US4] (orchestrator) Perturbation, scratch `CAT`: one narrowed version cell reverted → RED
  ("version is …, not 5.0SP2").
- [X] T070 [US4] (orchestrator) Perturbation, scratch `CAT`: `fixpp#625` dropped from N-002's Verified
  cell → RED.
- [X] T071 [US4] (orchestrator) Perturbation, scratch `CAT`: one witness name removed from its row's
  Tests cell → RED naming it.
- [X] T072 [US4] (orchestrator) Perturbation, scratch `CI`: one witness name removed from its
  coverage-index gap note → RED naming the MsgType and witness.
- [X] T073 [US4] (orchestrator) Perturbation, scratch `CAT`: one pin or live-cell ID removed from its
  row's Tests cell → RED naming it.
- [X] T074 [US4] (orchestrator) Perturbation, scratch `CAT`: A-033 or C-003's Spec ref reverted to its
  old category → RED.
- [X] T075 [US4] (orchestrator) Perturbation, scratch `CAT`: A-030 or N-003's Spec ref reverted to its
  old heading spelling → RED.
- [X] T076 [US4] (orchestrator) Perturbation, scratch `CONST`: the amended §VI.2 sentence removed →
  RED ("§VI.2 is not amended").
- [X] T076a [US4] (orchestrator) Perturbation, scratch `CAT`: one tail row's Title cell given an extra
  `35=<MT>` that no witness maps to → RED ("Title names …, its witnesses map to …"), the check that
  ties the TU's literal row table to the catalogue.
- [X] T077 [US4] (orchestrator) **SC-004 census**: T004's `catalogue_census.py` on the merge-base's
  catalogue and on the head's; OFFICIAL open falls by 12, done rises by 12, and `--list` differs only
  in the 12 tail rows. Every output in the evidence file.
- [X] T078 [US4] (orchestrator) **Commit C-RECORDS** (`spec/feature-catalogue.md`,
  `spec/coverage-index.md`, `spec/behaviors-and-limitations.md`), after the placeholder grep. C-AMEND
  precedes it in `git log`.

**Checkpoint**: all 12 rows `done` on the branch; the amendment is in, PENDING.

---

## Phase 7: Polish, verify, gates and close-out (plan P8, P9)

### `/simplify` (`[const §XVI.7]`, before verify)

- [X] T079 (orchestrator) **Run `/simplify`** on the implementation diff (`git diff origin/main..HEAD -- tests`
  and the uncommitted parent harness diff). The orchestrator triages; `phase-implementer` applies
  in-scope fixes (parent edits stay uncommitted, copy refreshed). A change to the witness TU re-runs
  T024 and the W6 arms its edit touches; to the routing cell, T040–T041; to the live cell, T051's
  `--gtest_list_tests` check, then T052–T053.
  Record the triage in the evidence file.

### Instruments (each with its known positive)

- [ ] T080 [P] (orchestrator) **Line citations and comment claims.** `python3 tools/check_line_citations.py --range origin/main..HEAD`
  and `--shift-audit origin/main..HEAD` pass; `python3 "$W/.claude/scripts/check-comment-claims.py" --root "$PWD" --base origin/main`
  passes. Read what the lexical check cannot see: the witness TU header (W7: rule and recipe, no
  result), the cell's comments, the harness comments.
- [ ] T081 [P] (orchestrator) **Every tier1 source-audit job, run locally.** Read `.github/workflows/tier1.yml`
  with a YAML parser; run each ungated audit job's `run:` blocks (`ci-script-pins`, `check-layers`,
  `co-spawn-closure-audit`, and any other) as Actions would (`bash -e`). Record a job → rc table in
  the evidence file.
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

- [ ] T085 (orchestrator) **Run `/speckit-verify 095-tail-evidence-pass`** in the main checkout, with
  the owner's local scope (ruling 2026-10-24: `linux-clang-debug` only):
  `--skip=linux-clang-release,linux-clang-asan,linux-clang-ubsan,linux-clang-tsan,linux-clang-coverage,linux-clang-libc++,windows-msvc-debug,windows-msvc-asan`
  and, for each preset in that `--skip`, `--waive=T085-<preset>:test-only change, no library code; CI runs it once both labels land; owner ruling 2026-10-24`.
  Coverage is N/A by the skill's Step 0c (no `src/` or `include/` file touched), so its coverage
  re-add does not apply. The verdict is therefore YELLOW (every skip paired with its waiver), or RED.
  It writes `.specify/decisions/095-tail-evidence-pass-verify.md`, replacing the file. The skill has
  no import step: the orchestrator copies the evidence file's entries into the skill's Step 3a
  (load-bearing witnesses) buffer by hand, each entry marked re-run or carried. **Carry rule:** an arm recorded at library SHA `s` is carried
  only if `git diff --name-only s..<verified head>` lists none of: the files its task names; the
  source file its test lives in (for every W6 arm, `tests/session/test_095_tail_rows_roundtrip.cpp`);
  and its binary's transitive inputs (`dictionaries/`, `tests/support/`, every `CMakeLists.txt`,
  `cmake/`, `src/`, `include/`, `tools/codegen/`). For an arm that ran on files uncommitted at run
  time (the live cell before C-US3, the parent harness), the `sha256sum` match REPLACES the diff test
  for those files (orchestrator ruling): a fixpp file's recorded `sha256sum` must equal its head
  blob's (`git show <verified head>:<path> | sha256sum`); a parent-harness file, uncommitted until
  T099, is compared with the working-copy file when T085 runs. Every other file the clauses above
  name stays under the diff test. Otherwise the arm is re-run at the verified head. So T043's edit
  of the witness TU, landing in C-US3 after C-US1, re-runs every arm of T010–T034 at the verified
  head, and so does any T079 or Gate B edit of that TU. **Accepted consequence (orchestrator
  ruling):** C-US3 edits `tests/interop/CMakeLists.txt`, which is in every arm's transitive-input
  clause, so every scratch-mutant arm recorded at a SHA before C-US3 re-runs at the verified head:
  the W6 arms (T013–T017, T025–T034), T039–T041 and both peers' T053 (a)–(c); T005's resource gate
  covers that re-run. The record lists, per arm, "re-run at `<head>`" or
  "carried from `<s>`". **RED-first arms** (T044, T046, T047, T054, T055a, T055's schema RED), whose RED was taken against the pre-change state, are carried as historical by construction: their entry keeps the SHA, command and output of the RED run, and the record adds their GREEN re-run at the verified head (orchestrator ruling 2026-10-24); they are never re-staged against a reconstructed pre-state. Its evidence:
  - quickstart §1 (T024), §2 (T039), §3 (T049), §4 (`ctest -L '^095$' --no-tests=error` against
    `expected-ctest-095.txt`), §5 (T057's local live run and its peer identities), §7 (T068) and
    SC-004 (T077);
  - the GREEN known positives with their commands and asserted lists or statuses: T010–T012;
  - every RED arm, with its GREEN pair where its task has one, its command and the message that
    fired: T013–T017, T025–T034, T039's
    instrument check, T040–T041, the harness RED-firsts of T044, T046 and T047, T049–T050 (both
    input-absent arms), T053 (arms a–c), T054, T055a, T055, T055b, T056, T059–T060, T065 (a–d),
    T069–T076a (SC-002);
  - T080–T084's results; coverage `[const §IX.1]` n/a per touched module (no library line);
  - MSVC stated plainly as not run locally, and each skipped preset with its waiver;
  - **one FULL `ctest --preset linux-clang-debug --output-on-failure --no-tests=error`, run LAST**, on a
    committed tree, after every selected run: before it `df -h /mnt/e`, `cat /mnt/c/temp/fixpp/.sandbox-lock`
    (someone else's lock → ask the owner), and the `lane613.build.active` marker check; detached with a
    `.done` marker. A full `ctest` regenerates `_codegen` in the same tree, so any build or run after
    it rebuilds first. T087's `local build: green` line cites its SHA.
  A row whose evidence did not hold is taken out of the flip set (its row reverted by a docs task)
  and the owner told. Its witnesses STAY: FR-004a's census keeps all 23, and quickstart §1's
  `n -eq 23` is unchanged. Every tail row, N-002 and N-003 included, is named as closed by 095 in
  data-model §F.5's `[const §XVIII.7]` after-text, so dropping one is a clause-body change: edit §F.5
  first, then the constitution's §XVIII.7 sentence to match, and T090's extra narrow scoped Codex
  round runs on that change before the RATIFIED flip. The same docs task makes every other edit
  that dropping the row requires:
  - quickstart §7: the dropped ID is removed from the script's one `FLIP` list, and from nothing
    else in it. Every row check reads `FLIP` (`in_flip`): the Status and Title loops iterate it, the
    version, `fixpp#625`, pins/routing, live-cell and Spec-ref checks skip an ID outside it, and the
    witness loop counts that row's witnesses as skipped instead of checking its Tests cell and gap
    notes (they stay as on `origin/main`, FR-020). Its assertion, checked plus skipped equal 23 with at
    least one checked, is unchanged;
  - data-model §E: the recipe's ID list; the Tests, Verified, version and Spec ref edits of that
    row; the coverage-index gap-note edits for its MsgTypes (FR-020: its coverage-index rows stay as
    on `origin/main`); the B&L section heading's "12" and B-095-1's row list;
  - data-model §F.3's SIR sentence naming the rows, and §F.5's clause sentence (above);
  - T077's "by 12" and "the 12 tail rows"; T104's "12 rows" and its coverage-index check's "each of
    the 23 tail MsgTypes"; T062–T064 and T066 for that row;
  - spec FR-017's and SC-004's counts; SC-001 is read over the rows that flip (FR-020), so its
    reading is stated in the verify record rather than left implied.
  The list is a starting point, not the population: re-derive it with
  `command grep -rnE '\b12\b|\b23\b|A-018 A-023|tail rows|<dropped ID>' specs/095-tail-evidence-pass/`
  and dispose of every hit in the docs task's record.
  After the skill writes the verify record, commit it with the evidence file in one parent commit,
  explicit paths only, under the Execution rules' parent-commit checks.
- [ ] T086 (orchestrator) `codegraph sync` in this tree after the last code-changing commit; read
  back `codegraph status --json`.

### PR, gates, ratification, merge and post-merge

> T087–T089 run before T105's audit (T088 → T104 → T089 → T105). T090–T103 run AFTER the audit,
> chronologically, and are numbered before the close-out tasks so that T105 stays FINAL (recorded
> deviation, header); each of them is open at the audit, under the waiver written in it.

- [ ] T087 (orchestrator) **Draft the PR description** in the parent's `research/reviews/` scratch
  area: title exactly `Constitution: amend §VI.2, §VI.5, §XVIII.7 — message-level Spec refs for application-message rows; FIX50-only tail rows delivered (v4.1) (+ 095 tail evidence pass)`
  (FR-021); body states the change, the rationale, the affected rows and specs; discloses what the
  PR's own `interop-live` will show (plan, "What the fixpp PR's `interop-live` shows": compiles the
  new source, then "population reconciliation FAILED -- not running any cell"; advisory, not SC-003
  evidence) and the post-merge windows (R-10); `## Gate A outcome` (rounds 1–3, the post-exhaustion
  passes, the scoped amendment rounds, the §XVIII.7 round) and the literal heading
  `## Gate B — waived` (U+2014 em dash; owner ruling 2026-10-24, the 094 precedent) with the
  placeholder `Gate B: pending.` under it, under which the verify record is linked and its
  pre-recorded waivers are stated with their rationale; `local build: green on linux-clang-debug @ <sha>`
  from T085; T082's grep over the body, so `closingIssuesReferences` is empty. T096 applies the final
  body.
- [ ] T088 (orchestrator) **Re-run T080–T082 as the last action, then push and open the PR without
  gate labels**: `git push -u origin 095-tail-evidence-pass`; `gh pr create` with T087's title and
  body. Read `closingIssuesReferences` (empty) and record the PR number. Read the ungated checks; a red
  one goes back through the fixer. The PR's `interop-live` result is recorded as advisory only after
  its log is read (quickstart §6): the build step succeeded, and the reconcile failed naming exactly
  the two new skip-list ids; any other failure is triaged like a red check. T104
  follows (it needs the PR number).
- [ ] T089 (orchestrator) **Verify-delta check and the pre-audit tick commit**, after T104's C-PRCELL:
  `git diff --name-only <T085 full-ctest SHA>..HEAD` lists only `spec/` and `specs/` paths; the
  verify record states the delta is docs-only (otherwise T085's full `ctest` re-runs last). Then
  commit the `tasks.md` ticks of T001–T089 and T104 (`specs/` only, placeholder grep and T082's
  grep first), so T105's check (i) reads committed state, re-run T080–T082 and push.
- [ ] T090 (orchestrator) **Amendment review check** (FR-021, `[const §XX.2]`). The scoped Codex Gate A
  review converged at round 6 on the §VI.2/§VI.5 clause bodies and, in its own narrow round, on the
  §XVIII.7 sentences (plan `## Gate A`). Diff the three amended clause texts in
  `.specify/constitution.md` against data-model §F.1, §F.2 and §F.5 at this point: identical → tick
  this task, record "no clause-body change since the converged scoped rounds". Any clause-body change
  since → one more narrow scoped Codex round with an Opus judge on that change, recorded in plan
  `## Gate A` and the Gate A record, before the RATIFIED flip. A records-only change (Status line, SIR outside the clause
  bodies) needs only Gate B's re-check.
  - *Open at the audit — waiver: it is a post-audit check by construction (it compares the head that
    Gate B reviews).*
- [ ] T091 (orchestrator) **Run `/gate-b 095-tail-evidence-pass`.** First `git fetch origin`; if
  `origin/main` moved past T001's base, merge it (`phase-implementer` resolves code/build conflicts),
  then re-run T080–T084 and record the new base. Preconditions: T085 non-RED, T105's `## Completeness`
  present, `check_bl_delta.py gate` green. Fixer: `phase-implementer` for code/test findings, the
  orchestrator for docs; a parent-harness fix stays uncommitted (copy refreshed). After a fix that
  touches a non-docs path, re-run the affected arms (for the live cell's source, T051's
  `--gtest_list_tests` check first) and T085's full `ctest` command last, under the
  same pre-checks (the command alone, appended to the verify record and the evidence file; a full
  `/speckit-verify` re-run instead copies the evidence file's entries in again, Execution rules). Gate B also
  reviews the PENDING amendment text. The Gate B record carries the B&L delta
  disposition (T067's rows).
  - *Open at the audit — waiver: Gate B needs the audit record first (pre-flight 4d).*
- [ ] T092 (orchestrator) **SecondBrain pass on the branch** (pipeline step 15a; close-out row 3), after
  Gate B converges: brain pages that describe the touched surfaces (`brain/components/` for session,
  interop, codegen tests) are checked and updated if needed; then both freshness halves run
  locally, neither standing in for the other: the in-repo `refs` half, every `run:` step of
  `.github/workflows/brain-freshness.yml`, read from the workflow with a YAML parser at this head
  (not a pasted command list), run as Actions would (`bash -e`); and the `refs_external` half, the
  parent's `tools/brain-external-sweep.sh` wrapper, never a bare `check_brain.py sweep`. Record each
  command and rc in the verify record and the evidence file.
  - *Open at the audit — waiver: after Gate B.*
- [ ] T093 (orchestrator) **Sign-off packet, then ASK THE OWNER** for the Art. XX §2 sign-off: the
  amended §VI.2 and §VI.5 and the corrected §XVIII.7 sentences, the PENDING SIR and Status line as on the PR head, the Gate A tallies
  (including the scoped amendment rounds), Gate B's rounds, and the RATIFIED-form text T095 will write
  (data-model §F.3, §F.4). Gate A's "convergence counts as sign-off" rule (plan `## Gate A`, round
  6) signs off the Gate A design review only; it does not discharge this Article XX §2 sign-off.
  Nothing in T094–T097 runs until the owner answers explicitly. A request to
  change clause text goes back through T090. Record the answer and its date in the verify and Gate B
  records.
  - *Open at the audit — waiver: Art. XX §2 orders it after Codex Gate A and Gate B.*
- [ ] T094 (orchestrator) (after sign-off) **Commit the `tasks.md` ticks of T090–T093 and T105**
  (T001–T089 and T104 were committed by T089); the ticks of T095–T103 go in the Gate B record's
  close-out table, so T095 stays the last commit.
  - *Open at the audit — waiver: after T093.*
- [ ] T095 (orchestrator) (after sign-off) **Commit C-RATIFY, the LAST commit before merge**
  (FR-021): `.specify/constitution.md` only — the SIR to data-model §F.3's RATIFIED form (header
  `— RATIFIED`; the first paragraph and "Review and sign-off" replaced by §F.3's literal RATIFIED
  text with its slots `<pr>`, `<gate-b rounds>` and `<sign-off date>` filled) and the Status line to
  §F.4's RATIFIED form. Only these record lines change. The multi-line
  paragraph is anchored by position (SIR header line to the line before `  Bump:`), printed with
  `sed -n` and confirmed before replacing. `git fetch origin` first; if `origin/main` moved, repeat
  T091's integration so C-RATIFY stays last. Checks on the flip head: the placeholder grep (baseline
  `git show origin/main:.specify/constitution.md`; positive control on data-model.md) prints nothing
  new; T080 re-run.
  - *Open at the audit — waiver: only after the owner's sign-off.*
- [ ] T096 (orchestrator) **Body → labels → push last.** Labels (owner ruling 2026-10-24, the 094
  precedent): `gate-a-done`, with the Gate A convergence record, and `gate-b-waived`, because the
  verify verdict is YELLOW by construction (T085's pre-recorded waivers). The label records that
  verify consequence only: Gate B itself still runs and converges (T091), and the body's Gate B
  section reports its outcome, so the label does not mean Gate B was skipped. The waiver
  rationales in the verify record and under the body's literal `## Gate B — waived` heading (T087). A
  RED verdict at this head → no label, and the owner is told. First edit the body to its final form
  (sign-off date, verified SHA, Gate B outcome replacing `Gate B: pending.`, which `grep -cF` must then
  count 0); then run `gate-b.yml`'s heading awk locally on the final body, byte for byte (the heading's
  dash is U+2014), with its known positive: the same awk on a copy of the body without the heading
  exits 1. Then apply the labels in ONE `"$W/.claude/scripts/gh-pr-meta.sh"` call and read
  them back. Then push C-RATIFY. Never cancel a "duplicate" gate-label wave.
  - *Open at the audit — waiver: after T095.*
- [ ] T097 (orchestrator) **Merge (owner-driven) and the fixpp close-out.** Before merging,
  `closingIssuesReferences` empty and all three tiers plus both gate checks green on the C-RATIFY
  head. The sanitizer jobs (`linux-clang-{asan,ubsan,tsan}`) of the first labelled run are read:
  each ran the `095`-labelled entries (they appear in its log) and passed; a sanitizer report is a
  defect until disproven and is triaged before merge, whatever the leg's waiver says. If `--admin` is needed, give the owner a one-line `!` command with `--body-file`. After the
  merge, `.specify/close-out.md` §3's table in the Gate B record, every row DONE or N/A:<reason>: row 5
  labels read back; row 9 none closed, #462, #613, #625, #626 read OPEN; row 16 the main checkout
  reset (`git switch main && git merge --ff-only origin/main`, only if this tree's branch is this PR's
  and `git status --porcelain` is clean; the uncommitted parent harness edits are outside the
  submodule and survive it); row 17 purge the branch locally and on the remote; row 18 N/A (the lock
  was never taken). Then `codegraph sync`.
  - *Open at the audit — waiver: post-merge.*
- [ ] T098 (orchestrator) **POST-MERGE: confirm the harness edits are intact** before the parent
  commit: `diff -r` the outside-tree copy against the parent working copy; `git -C <parent> status --short -- research/G19-fix-fpml-iso20022/phase-9-harness`
  lists exactly the files of plan "Files changed" (parent table, which already lists the two goldens).
  - *Open at the audit — waiver: post-merge.*
- [ ] T099 (orchestrator) **POST-MERGE: the ONE parent commit** (FR-015, research R-10 step 4): every
  R-9 harness change (the two goldens are its item 5), and the gitlink bump to the merged fixpp `main` (or the harness
  changes alone, if another parent commit already bumped the gitlink to a library commit containing
  the merge). `git fetch`; check `git diff --cached --name-only` holds nothing that is not ours;
  stage with `git add` (explicit paths), commit **without** a pathspec; then `git ls-tree HEAD research/G19-fix-fpml-iso20022/library`
  equals `git -C research/G19-fix-fpml-iso20022/library rev-parse HEAD` and `git cat-file -t` says `commit`.
  In the "alone" case, first prove the gitlink already contains the merge:
  `git -C research/G19-fix-fpml-iso20022/library merge-base --is-ancestor <095 squash SHA> <gitlink SHA>`
  exits 0, with the gitlink SHA read from `git ls-tree HEAD research/G19-fix-fpml-iso20022/library`.
  - *Open at the audit — waiver: post-merge (owner ruling "Keep #462").*
- [ ] T100 (orchestrator) **POST-MERGE: push parent `main`** (owner authorization 2026-10-24; it
  carries any unpushed parent commits): `ls-tree` re-checked first; then `harness-tests` green on that
  commit; wait for `publish-counterparties` and record the image digest; re-dispatch the parent
  `interop-matrix` if its push run started before the publish finished, decided by comparing the
  push run's `createdAt` with the publish run's completion time (`gh run view <id> --json
  createdAt,updatedAt,conclusion` on each), and record the post-publish run.
  - *Open at the audit — waiver: post-merge.*
- [ ] T101 (orchestrator) **POST-MERGE: dispatch `interop-live` on fixpp `main`** (`gh workflow run interop-live.yml --ref main`;
  it has no `push` trigger). It must list `HP-QFcpp-init-fixt11-fix50sp2-app-bn` and
  `HP-QFj-init-fixt11-fix50sp2-app-bn` as passed. Record the run id and the pulled image digest in
  the Gate B record's close-out table (the hello carries `local-build:unpinned`, data-model §D). The
  pulled digest must equal T100's published one, which shows the run used the republished image;
  otherwise re-dispatch. Pull that digest locally and `cmp` its `qfcpp-spec/FIX50SP2.xml` against
  `dictionaries/FIX50SP2.xml` (quickstart §6). If either cell does not pass, the close-out stops and
  goes to the owner (SC-003).
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
    only, placeholder grep first); `git diff --word-diff HEAD~1 HEAD -- spec/feature-catalogue.md`
    on C-PRCELL shows only the PR cell of the 12 rows changed;
  - extract each of the 12 rows and diff it against data-model §E: Status `done`, `/specify`, PR,
    Tests (the data-model §A witness IDs, the N-002/N-003 pins and routing cell, A-018's live cells),
    Verified (the offline-bar sentence; A-018's live cells and `ExecutionAck`; the four narrowed rows'
    "no typed tier" sentences; N-002's `kN002N003Excluded` / fixpp#625 sentence); the four FR-018
    Spec refs; every other column byte-identical to `origin/main`. The diff has a known positive
    first: on a scratch copy with one character of one Verified cell changed, it reports that row;
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
  needs T014's Mutant C; T031 and T032's F, I and J may share a rebuild, T032's O has its own) → T035 → T036.
- **US2 (T037–T042)**: after T005; independent of US1 by file, but shares the build tree, so its
  builds wait for the US1 build owner. T037 ‖ T038 → T039 → T040 → T041 → T042.
- **US3 (T043–T058)**: T043 after T019; T044 ‖ T045 (parent files) → T047 (test-first, needs T044's
  registration) → T048
  (the unpatched jar) → T049 (needs T043 and T048's jar) → T050; T051 after T049 passes on BN → T052's
  QF-cpp half → T046 (RED against the unpatched jar, then the fix and the rebuilt jar) → T052's QFJ
  half → T053; T054 (needs T044, T047) → T055a → T055 → T055b → T056 → T057 → T058.
- **US4 (T059–T078)**: after T058 (T062 cites test IDs that must exist; T068 resolves the live-cell IDs
  in the uncommitted `CELLS`). T059 ‖ T060 → **T061 (C-AMEND) before every catalogue edit** → T062 →
  T063 → T064 → T065; T066 ‖ T067 after T061; T068 after T062–T067 → T069…T076a → T077 → T078.
- **Polish**: T079 → T080–T084 (parallel) → T085 → T086 → T087 → T088 → **T104** → T089 → **T105** →
  T090 → T091 → T092 → **T093 (owner)** → T094 → **T095 (last commit)** → T096 → T097 → T098 → T099 →
  T100 → T101 → T102, T103.

### Critical path

T001 → T005 → T007…T019 → T020…T036 → T043 → T048 → T049 → T051 → T052 (QF-cpp) → T046 → T052 (QFJ)
→ T053 → T054 → T055a → T055 → T055b → T056…T058 → T059…T078 → T079 → T085 →
T088 → T104 → T105 → T091 → **T093 owner sign-off** → T095 → T096 → T097 → T099 → T100 → T101.

### Parallel opportunities

- T002 ‖ T003 ‖ T004 ‖ T006 (read-only).
- T037 ‖ T038 (two test files), single build owner.
- T044 ‖ T045 (two parent harness files; copy refreshed after each). T047 follows T044 (its tests
  need the registration); T046 follows T051 (its RED needs the cell and T048's unpatched jar).
- T066 ‖ T067 (coverage index vs B&L), after T061.
- T080 ‖ T081 ‖ T082 ‖ T083 ‖ T084.

## Parallel example: User Story 3, parent harness

```text
Task: "T044 [US3] (implementer) phase-9-harness/tools/run_interop_cell.py + test_run_interop_cell.py — L0 registration, per-cell dictionary"
Task: "T045 [US3] (implementer) phase-9-harness/tools/interop_capability_minimums.yaml — two entries"
```

T047 (`emit_matrix.py` + `test_emit_matrix.py`, the `run_kind` rows) starts once T044 lands.

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
