---
description: "Task list for 096-fix-latest-tail-evidence"
---

# Tasks: Typed app-message tail — evidence pass for the 31 FIX Latest rows

**Input**: `specs/096-fix-latest-tail-evidence/` — spec.md, plan.md, research.md, data-model.md,
contracts/witness-contract.md, quickstart.md.
**Tests**: this feature IS tests and records. Every check is shown RED by its contracts W6 arm before it
is trusted (`[const §VII.3]`).

## Execution rules (apply to every task)

- **Executor (`[const §XVI.6]`).** Every task names one:
  - `(orchestrator)` writes documentation and governance text only: `.specify/constitution.md`
    (data-model §F verbatim), `spec/feature-catalogue.md`, `spec/coverage-index.md`,
    `spec/behaviors-and-limitations.md`, `specs/096-fix-latest-tail-evidence/*.md`, decision records,
    parent docs, `brain/**`, post-merge `CLAUDE-history.md`/`CLAUDE.md`. It runs read-only
    instruments, builds and tests, and the quickstart blocks.
  - `(implementer)` is `phase-implementer`. It makes EVERY change to `tests/**` (C++ sources, the
    header, CMake), `specs/096-fix-latest-tail-evidence/expected-ctest-096.txt`, and every scratch
    mutant. The orchestrator never writes those files by any tool, python or sed included.
- **The tree.** The main checkout, branch `096-fix-latest-tail-evidence`. Never `git checkout` or
  `git switch` there (the post-merge reset, T091, is the one exception). Check the branch with
  `git -C research/G19-fix-fpml-iso20022/library rev-parse --abbrev-ref HEAD` before each commit batch.
  Commits are serial. Never `git stash`. An implementer makes NEW commits only (no `--amend`).
- **No production change (FR-012, SC-005).** Nothing under `src/`, non-generated `include/` or
  `tools/codegen/` is committed. A witness or the differential that exposes a production defect or a
  dictionary disagreement stops the task: the orchestrator files a labelled issue (labels from
  `gh label list`) and brings it to the owner before the affected row flips (FR-012's procedure).
- **Stop rules (spec Edge Cases, plan Risks).** The implementer stops and reports, and does not work
  around, when: a tail MsgType's walk status is not `ok` on either leg; the differential finds a tail
  difference; the parser refuses a deficient frame or splits E-nodelim, CC-nodelim or CC-count2 into
  instances otherwise than 095's contract W6 states (an expected list is never rewritten to fit the
  parser); the parser refuses a witness frame the typed builder produced; a closure-required set holds
  a tag the framing writes; a required group has no string or int member AND no enum member whose code set
  declares at least as many values as the group has instances (data-model §C's implementation
  decision); a closure-required member has no typed `Args` member; the typed builder
  refuses a build a witness or an arm needs; the vlatest TU's compile peak RSS exceeds 8 GiB (research
  R-9; the fallback, splitting the TU by row range, is the orchestrator's call); any quickstart §1 leg
  is non-empty after the move. A parse error, a compile error or a build failure is never recorded as
  an arm's RED.
- **Where orchestrator controls run (`[const §XVI.6]`).** An orchestrator task's controls (mutated copies of the header, of 095's TU, of a record file; a seeded commit) run only on copies outside the PR's worktree: files under the session scratchpad (`H=`, `T095=`, `CAT=`, `COV=`, `BL=`, `CON=` pointed at them), or a throwaway `git worktree add --detach <scratchpad>/<name> HEAD`, removed afterwards. Never in the main checkout.
- **Mutant protocol (contracts W6).** Apply the scratch edit; rebuild only the affected target and
  show the rebuild took the edit (the build log relinks the target and the binary's mtime is newer than
  the edited file's; record both); run the task's filter; record the command and the message that
  fired, which must be the one the task names; revert; rebuild the same way; `git diff --exit-code`
  over the touched paths. Mutants are never committed. Mutants G, F, I, J of one suite may share one
  rebuild (each message recorded); O always gets its own.
- **Evidence file.** Until `/speckit-verify` runs (T081), every "record" goes to
  `.specify/decisions/096-fix-latest-tail-evidence-evidence.md` (the parent's tracked
  `decisions/speckit/`, through the symlink). The orchestrator writes it; an implementer task's report
  carries the entry. Each entry: task ID, command, output or the message that fired, the library
  `git rev-parse HEAD` with `git status --porcelain` empty (or the applied scratch mutant named). T081
  copies the entries into the verify record; every later task that writes the verify record appends
  the same text to the evidence file.
- **Builds (`[const §XVII.7]`).** Only after T002's owner ruling. Only the `linux-clang-debug` tree is
  built locally. `export CCACHE_DIR=/mnt/wsl/fixppbuild/ccache`; `-j8` (owner ruling 2026-10-28, T002); one build owner per build
  directory; `df -h /mnt/e` before a build in the main checkout's `build/linux-clang-debug`. Long
  builds run detached (`setsid nohup <build> > <log> 2>&1; touch <marker>.done`) with ONE background
  waiter on the marker (a PID or `.done` file, never `pgrep -f`), never a foreground poll loop.
- **MSVC sandbox** is not used. Before any full `ctest`, `cat /mnt/c/temp/fixpp/.sandbox-lock`; a lock
  held by someone else means ask the owner first.
- **Records rules.** No `file:NNN` line-number citation in any added line
  (`python3 tools/check_line_citations.py --range origin/main..HEAD` after every commit batch); no
  comment records a RESULT (`.claude/scripts/check-comment-claims.py --root <lib> --base origin/main`
  on the added C++ comments; the W7 exceptions are assertions, not comments).
- **Autonomy (owner, 2026-10-28, as for row 2).** Through Gate B, push, PR, labels, and merge if green
  (serialized with any other open PR). Gate exhaustion, owner matters and the Article XX §2 sign-off
  go to the owner. `gh pr merge --admin` is the owner's command (give a one-line `!` command).

## Format: `[ID] [P?] [Story] Description`

`[P]`: parallel-safe (different files, no dependency on an incomplete task). `[US1]`…`[US3]`: spec.md
user stories. Paths are relative to the library root unless marked PARENT.

---

## Phase 1: Setup (evidence, resource gate, baselines)

- [X] T001 (orchestrator) Create `.specify/decisions/096-fix-latest-tail-evidence-evidence.md` with a header naming the feature, the branch and this file's role (Execution rules, Evidence file).
- [x] T002 (orchestrator) Asked the owner (`AskUserQuestion`), 2026-10-28. **Build gate:** incremental `linux-clang-debug` builds of the two binaries during P1–P4 and one full configure + build + full `ctest` at the final code head (T081), at `-j8`; every other preset waived to CI. **Verdict and labels (corrected ruling, same day):** score `/speckit-verify` by the text — a preset skipped on the owner's ruling is `SKIPPED (--skip flag)` with its paired waiver, SKIPPED-with-reason, so it is no failure and the verdict can be GREEN (`[const §XVII.8]`: GREEN = all PASS or SKIPPED-with-reason; YELLOW needs a waived FAIL). Labels follow T082's actual verdict: GREEN gives `gate-a-done` (with `096-fix-latest-tail-evidence-gatea.md`) and `gate-b-done` if Gate B converges with no waiver; a `-waived` label only for a real waived FAIL or an explicit Codex-side waiver, its rationale in the verify record and the PR body under a `## Gate A …`/`## Gate B …` heading. 095's YELLOW with SKIPPED-only rows is not followed. Recorded in the evidence file and the project memory.
- [X] T003 (orchestrator) Run quickstart §0 as written and its controls (`TAIL="DS BN"`, `Q=/nonexistent`, `TAIL="DS ZZ"`, a BNX-only `QF`, a non-checkout `QC`: each exits 1) and its negative control (a `QF` holding `msgtype="BN"` and `msgtype="DSX"`: exits 0, `DSX` is not `DS`); record each exit status in the evidence file. A non-zero exit on the live inputs stops the feature.
- [X] T004 (implementer) Write `specs/096-fix-latest-tail-evidence/expected-ctest-096.txt`: the two lines `test_077_allversions_builder_roundtrip` and `test_077_allversions_builder_roundtrip_vlatest`, sorted, trailing newline.
- [X] T005 (implementer) Baseline at the branch's base: build `test_077_allversions_builder_roundtrip` and `test_077_allversions_builder_roundtrip_vlatest` (`build/linux-clang-debug`), run both whole; report pass counts. State the compile-RSS method for T027: the TU's command from `build/linux-clang-debug/compile_commands.json` run under `/usr/bin/time -v`, peak RSS read from "Maximum resident set size".

---

## Phase 2: Foundational — the move (plan P1, contracts W0)

**Blocks every later phase.**

- [X] T006 (implementer) Create `tests/support/tail_rows_witness.hpp` and edit `tests/session/test_095_tail_rows_roundtrip.cpp` exactly as contracts W0 states: the moved region between one `// clang-format off` / `// clang-format on` pair as `T1(region)`, one empty line, the `check_witness` free-function signature on one line, `T2(method)`; outside the markers only `#pragma once`, includes, the namespace `fixpp_test_support::tail_rows` wrapper, comments, blank lines; 095's TU = its merge-base text minus the region, minus `using fixpp_test_support::parse_dict;`, with the method replaced by quickstart §1's forwarder byte for byte (qualified call `fixpp_test_support::tail_rows::check_witness`). Build `test_077_allversions_builder_roundtrip`; run `--gtest_filter='TailRows095*:AllVersionsRoundtrip077V50SP2.*'`; all pass. Do not commit yet.
- [X] T007 (orchestrator) Run quickstart §1 (all four legs) on the tree: exit 0. Run its column-0 re-check (every column-0 function head of the header region carries `inline`; quickstart §1's `region <base> in | grep -n '^[A-Za-z]'` against the header). Negative control: a correct move run through `clang-format --style=file` prints nothing and exits 0. Then its controls on scratch copies (whitespace inside a literal in the region and in `check_witness`; an inserted statement; a deleted and a commented-out 095 `EXPECT`; a left-behind `join` copy; `parse_dict` using kept; a forwarder token changed; a function after the markers; a second / no off marker; `join` and `join_tags` swapped): each exits 1 with its leg's line. Record all in the evidence file. A non-empty leg on the tree returns T006 to the implementer.
- [X] T008 (implementer) Commit T006 alone: `096 P1: move 095's witness machinery into tests/support/tail_rows_witness.hpp (pure move, contracts W0)`. Run the line-citation and comment-claims checks; report the hash.

**Checkpoint**: 095's suites pass in the same binary; quickstart §1 reads clean; the carry-over of 095's L/M/N/P and of D/H/E/value-mismatch (contracts W6) holds from here on.

---

## Phase 3: User Story 1 — every tail MsgType has a named, complete witness on each tier (Priority: P1) 🎯 MVP

**Goal**: 62 witnesses (31 per tier), each setting every closure-required field and group of its
tier's dictionary, read back both ways, held to data-model §A by a census per suite.
**Independent test**: quickstart §2 — both binaries pass the `TailRows096*` filters; 31 + 31 witnesses
listed under their exact names.

### v50sp2 leg (plan P2; one TU, serial)

- [X] T009 [US1] (implementer) `tests/session/CMakeLists.txt`: add `test_096_tail_rows_roundtrip.cpp` as a source of `test_077_allversions_builder_roundtrip`; append label `096` to that test entry after its last LABELS setter (`set_property(TEST … APPEND PROPERTY LABELS 096)`).
- [X] T010 [US1] (implementer) `tests/session/test_096_tail_rows_roundtrip.cpp` header comment (the rule and the re-derive recipe, no result) + data-model §A's literal 31-row table + fixture (loads `FIXPP_DICT_DATA_DIR "/FIX50SP2.xml"` once, walk via the shared `Walker`) + `TailRows096Census.EveryTailMsgTypeHasExactlyOneWitness` (suite selected by `test_suite_name() == "TailRows096"`, every finding collected before asserting) + `TailRows096Census.FieldLegMatchesQuickFixCppGolden` (row-count precondition, then set equality per tail MsgType, read from `FIXPP_095_QUICKFIX_GOLDEN_CSV` at test time). No witness yet. Build and run: the census is RED with 31 multiplicity-0 findings; the golden check is GREEN. Record the census message.
- [X] T011 [US1] (implementer) The 31 witnesses `TailRows096.<RowId>_<Name>` (data-model §A order), each per data-model §C and contracts W2–W5 through the shared `check_witness`: every closure-required member set, required groups with ≥ 2 instances each carrying the delimiter and its required members, ≥ 2 intent entries, values written twice (intent entry and setter, neither reading the other). Run `--gtest_filter='TailRows096*'`: all GREEN. Then `--gtest_filter='TailRows096*' --gtest_shuffle --gtest_repeat=3`: all GREEN (FR-005's shuffle leg); record.
- [X] T012 [US1] (implementer) Report the clean link of `test_077_allversions_builder_roundtrip` with two TUs including `tail_rows_witness.hpp` (095's and 096's) as W0's `inline` evidence (a missing `inline` is a multiple-definition error). Commit T009–T011: `096 P2: v50sp2 witnesses, census and golden field-leg check (A-035..A-065)`. Report the hash.

### v50sp2 RED arms (contracts W6, v50sp2 table; Mutant protocol; record each)

- [X] T013 [US1] (implementer) Field-leg mutant 1: one tag dropped from the walk's field set for one tail MsgType → RED in `FieldLegMatchesQuickFixCppGolden` naming that MsgType and both sets.
- [X] T014 [US1] (implementer) Field-leg mutant 2: a MsgType absent from `golden.csv` added to the asserted set → RED in the row-count precondition.
- [X] T015 [US1] (implementer) Mutant G: delete `TailRows096.A065_QuoteAck` → census RED naming CW, multiplicity 0. With G applied, run the census alone (`--gtest_filter='TailRows096Census.EveryTailMsgTypeHasExactlyOneWitness'`) → the same RED with no witness run (order-freedom).
- [X] T016 [US1] (implementer) Mutant F: duplicate the A-035 witness as `TailRows096.A036_CrossRequest` → RED naming DS multiplicity 2 and the row mismatch (A036 against A-035).
- [X] T017 [US1] (implementer) Mutant I: add `TailRows096.A035_CrossOrderRequest` → RED naming the unmapped name.
- [X] T018 [US1] (implementer) Mutant J: add `TailRows096.A001_NewOrderSingle` → RED naming 35=D as extra.
- [X] T019 [US1] (implementer) Mutant O (own rebuild): rename `TailRows096.A043_PartyDetailsListRequest` to `TailRows096.A044_PartyDetailsListRequest` → RED naming CF's row mismatch and no multiplicity finding.
- [X] T020 [US1] (implementer) Mutant K: the name → MsgType lookup returns DT for `CrossRequest` → RED in the A-035 witness's tag-35 assertion naming DS and DT.
- [X] T021 [US1] (implementer) Mutant D: delete one intent entry, setter kept → RED in W4 "parsed, not intended" naming the path.
- [X] T022 [US1] (implementer) Mutant H: in the A-065 witness delete an optional field's setter, keep its intent entry → RED in W4 "intended, not parsed" naming the path; W5 stays green.
- [X] T023 [US1] (implementer) Value mismatch: after the build and before `make_frame`, change one letter of one string literal in the built buffer → RED in W4 naming the path and both values.
- [X] T024 [US1] (implementer) Mutant E: cut the A-064 witness to one entry (setters and intent entries) → RED in the intent-size assertion.
- [X] T025 [US1] (implementer) Mutant B: in the A-065 witness delete the `QuoteAckStatus(1865)` setter and its intent entry → RED in W5 (not W4) naming `1865`.
- [X] T026 [US1] (implementer) Mutant A: in the A-037 witness delete the required group's assignment and its intent entries → RED in W5 naming the group.

### vlatest leg (plan P3; one TU, serial)

- [X] T027 [US1] (implementer) `tests/session/CMakeLists.txt` (inside `if(FIXPP_CODEGEN_FIX_LATEST)`): add `test_096_tail_rows_roundtrip_vlatest.cpp` to `test_077_allversions_builder_roundtrip_vlatest`, link `pugixml::pugixml`, define `FIXPP_095_QUICKFIX_GOLDEN_CSV` (same value as the v50sp2 target), append label `096`. Create the TU with its header comment, the literal 31-row table and one placeholder `TEST(TailRows096LatestSetup, Placeholder)` (outside every census-held suite; T030 deletes it), compile it alone under T005's method and report peak RSS (stop above 8 GiB). Then measure again after T031 with all 31 message headers included.
- [X] T028 [US1] (implementer) In the vlatest TU: `OrchestraWalk{WalkMap walk; std::string error}` and `OrchestraWalker` per data-model §B.2 (raw pugixml over the Orchestra file, sharing no code with `src/dictionary/orchestra_loader.cpp` or `tools/codegen/`; duplicate `msgType` → `error` exactly `duplicate fixr:message msgType <MT>`; start at `fixr:structure`; skip StandardHeader/StandardTrailer; presence AND over componentRef/groupRef/fieldRef; delimiter = first emission, components expanded, a nested groupRef emitting its count tag; statuses `nested_required_group`, `delimiter_chain`; element named by the `numInGroup` field's name). Write FIRST the tests `TailRows096LatestCensus.OrchestraWalkOnSyntheticFragments` (fragments S2, S4, S5, S6 per contracts W6) and `TailRows096LatestCensus.FieldLegMatchesQuickFixCppGolden`, run them RED against a walker stub returning an empty map, then implement until GREEN. Record the RED messages.
- [X] T029 [US1] (implementer) `TailRows096LatestCensus.KnownPositive_DeficientFrames` (095's frames E-absent, E-one, E-short, E-member, E-nodelim, CC-nodelim, CC-nested, CC-count2 copied byte for byte except the `.expected` lists, under clang-format off; each list re-derived from the Orchestra file by data-model §B.2's recipe and written from that reading) and `TailRows096LatestCensus.NestedRequiredGroupFailsLoud_MassQuote` (status and element `NoQuoteEntries` as literals). Parse under an `OrchestraLoader`-built `table_view`. Before writing the lists, re-derive each frame MsgType's required shape (its closure-required set: required fields, required groups with their delimiters and per-instance required members) on BOTH files with research R-4's recipes and report the comparison for the evidence file; a difference, or an empty Orchestra list where 095's list is non-empty, stops the task. A frame the parser refuses or splits otherwise stops the task. Run GREEN.
- [X] T029b [US1] (orchestrator) Run quickstart §1b on the tree (exit 0) and its controls on scratch copies (a copy with a different expected list: 0; one byte changed in a frame body: 1; block absent: 1); record.
- [X] T030 [US1] (implementer) Fixture for `TailRows096Latest` (Orchestra doc + walk loaded once; `OrchestraWalk::error` copied into `load_error`, no `ADD_FAILURE` in `SetUpTestSuite`; the parse dictionary from `OrchestraLoader`) and `TailRows096LatestCensus.EveryTailMsgTypeHasExactlyOneWitness` (`test_suite_name() == "TailRows096Latest"`, name → MsgType through `fixr:message@name`). Delete T027's placeholder test. Run: RED with 31 multiplicity-0 findings; record.
- [X] T031 [US1] (implementer) The 31 witnesses `TailRows096Latest.<RowId>_<Name>` via `fixpp::vlatest::build_<Name>`, per data-model §C (enum literals from the field's Orchestra `fixr:codeSet` first code) and contracts W2–W5. Run `--gtest_filter='TailRows096Latest*'`: all GREEN, then with `--gtest_shuffle --gtest_repeat=3`: all GREEN (FR-005); record. Report T027's second RSS reading.
- [X] T032 [US1] (implementer) Commit T027–T031: `096 P3: Orchestra required-walk, vlatest witnesses, census and known positives`. Report the hash.

### vlatest RED arms (contracts W6, vlatest table; Mutant protocol; record each)

- [X] T033 [US1] (implementer) S5 key rule: checked insertion replaced by plain assignment → RED in S5's assertion (`error` empty).
- [X] T034 [US1] (implementer) S6 start: the walk's root is the `fixr:message` element → RED in S6's assertion (fields ≠ `{9301}`).
- [X] T035 [US1] (implementer) Field-leg mutant 1 (Orchestra walk): one tag dropped for one tail MsgType → RED naming it and both sets.
- [X] T036 [US1] (implementer) Field-leg mutant 2: a MsgType absent from `golden.csv` added to the asserted set → RED in the precondition.
- [X] T037 [US1] (implementer) Header/trailer skip removed → RED in the field-leg assertion naming the header tags.
- [X] T038 [US1] (implementer) componentRef presence ignored → RED in S4's assertion.
- [X] T039 [US1] (implementer) Every groupRef treated as required → RED in `KnownPositive_DeficientFrames`.
- [X] T040 [US1] (implementer) No groupRef treated as required → RED in E-absent's list.
- [X] T041 [US1] (implementer) Instance members not collected → RED in E-member's list.
- [X] T042 [US1] (implementer) Nested-group emissions ignored when taking the first emission → RED in CC-nodelim's list.
- [X] T043 [US1] (implementer) `delim_group` never set → RED in CC-nested's and CC-count2's lists.
- [X] T044 [US1] (implementer) `nested_required_group` detector returns `ok` → RED naming 35=i.
- [X] T045 [US1] (implementer) Element named by `fixr:group@name` instead of the `numInGroup` field's name → RED naming the element.
- [X] T046 [US1] (implementer) `delimiter_chain` check disabled → RED in S2's assertion.
- [X] T047 [US1] (implementer) Census arms on `TailRows096Latest` (Orchestra name lookup): Mutants G (+ order-freedom run alone), F, I, J in one rebuild, each message recorded, names as T015–T018.
- [X] T048 [US1] (implementer) Census Mutant O on `TailRows096Latest` (own rebuild), as T019.
- [X] T049 [US1] (implementer) Mutant K on `TailRows096Latest` (Orchestra lookup returns DT for `CrossRequest`), as T020.
- [X] T050 [US1] (implementer) Mutant B on the vlatest A-065 witness (`QuoteAckStatus(1865)` setter + intent entry deleted) → RED in W5 (not W4) naming `1865`.
- [X] T051 [US1] (implementer) Mutant A′ on the vlatest A-037 witness (`order_entries` assignment + its intent entries deleted; the builder writes `2428=0`) → RED in W5's count-below-two leg naming `2428`, recorded with W4's co-firing `parsed, not intended: 2428=0`.

**Checkpoint**: quickstart §2's v50sp2 and vlatest witness parts pass; every US1 arm recorded.

---

## Phase 4: User Story 2 — the two dictionaries agree on what each tail MsgType requires (Priority: P1)

**Goal**: the cross-dictionary differential (data-model §D) with its measured and synthetic positives.
**Independent test**: `--gtest_filter='TailRows096Differential.*'` on the vlatest binary.

- [X] T052 [US2] (implementer) In the vlatest TU: suite `TailRows096Differential` with its own fixture, loading `FIXPP_DICT_DATA_DIR "/FIX50SP2.xml"` through the shared `Walker` (the QuickFIX walk) and the Orchestra file through `OrchestraWalker`, once, copying any load or walk error into `load_error` (asserted empty in `SetUp`); `SyntheticPairs` feeds each pair's QuickFIX fragment to `Walker{doc}.walk_all()` and its Orchestra fragment to `OrchestraWalker`, both after `load_string`. `Finding{msg_type, path, fix50sp2, orchestra}` and `diff(qf_walk, orch_walk, msg_types)` per data-model §D (every finding collected, then sorted by (msg_type, path) as `std::string`; member findings only for a group required on both sides; a non-`ok` status is one `status` finding). Write FIRST `TailRows096Differential.KnownDifference_MarketDataSnapshotFullRefresh` (exactly `[{W, 779, required, absent}]`), `TailRows096Differential.KnownNonOk_MassQuote` (one `status` finding, each side `nested_required_group NoQuoteEntries`) and `TailRows096Differential.SyntheticPairs` (one pair per path — group, delimiter, instance-member, `delim_group` — each exactly one whole `Finding`; pair M per contracts W6: inputs `{SM, SA}`, the exact sorted four-finding vector). Run them RED against a `diff` stub returning nothing; record; implement until GREEN.
- [X] T053 [US2] (implementer) `TailRows096Differential.TailMsgTypesAgree`: findings over data-model §A's 31 MsgTypes, asserted empty, printed otherwise. A non-empty result stops the task (FR-012). Run GREEN.
- [X] T054 [US2] (implementer) Commit T052–T053: `096 P4: cross-dictionary differential (FIX50SP2.xml vs Orchestra)`. Report the hash.

### Differential RED arms (contracts W6; Mutant protocol; record each)

- [X] T055 [US2] (implementer) 35=W appended to `TailMsgTypesAgree`'s compared list → RED printing `{W, 779, required, absent}`.
- [X] T056 [US2] (implementer) Field comparison skipped → RED in `KnownDifference_MarketDataSnapshotFullRefresh` (empty findings).
- [X] T057 [US2] (implementer) Non-`ok` check skipped → RED in `KnownNonOk_MassQuote`.
- [X] T058 [US2] (implementer) Group comparison skipped → RED in its `SyntheticPairs` pair.
- [X] T059 [US2] (implementer) Delimiter comparison skipped → RED in its pair.
- [X] T060 [US2] (implementer) Instance-member comparison skipped → RED in its pair.
- [X] T061 [US2] (implementer) `delim_group` comparison skipped → RED in its pair.
- [X] T062 [US2] (implementer) Pair M, comparator returns after its first finding → RED (one finding).
- [X] T063 [US2] (implementer) Pair M, sort removed → RED (collection order). If it reads green, stop: the pair is redesigned, not the assertion.
- [X] T064 [US2] (implementer) Pair M, member findings emitted for a one-sided group → RED (extra `{SA, 9200[*].9201, required, absent}`).
- [X] T065 [US2] (implementer) Delimiter, component emission (vlatest table row): component emissions ignored when taking the first emission → RED in `TailMsgTypesAgree` naming DP and `<count_tag>/delim` with both values (an A-042 witness RED alongside is not this arm's RED).

- [X] T065b [US2] (orchestrator) Run quickstart §2 whole (exit 0) and §4 (exit 0), then their controls on scratch stubs (§2: a witness unlisted → `UNLISTED`, a lister that prints the list and exits 1 → `LIST:`, a failing ctest; §4: one binary unlabelled → `LABEL:`, a `ctest -N` that prints the list and exits 1): each exits 1; record.

**Checkpoint**: quickstart §2 and §4 pass with their controls.

---

## Phase 5: User Story 3 — the catalogue and the constitution say what shipped (Priority: P2)

**Goal**: v4.2 amendment, then the 31 rows, coverage index and B&L, checked by quickstart §5a–§5c.
**Independent test**: quickstart §5a exits 0 in the PENDING state with its perturbation arms each exiting 1.

### RED before records

- [ ] T066 [US3] (orchestrator) Run quickstart §5a on the unedited tree with `PR=<the PR number, or a placeholder number until T084>`: it exits 1, family A flagging every tail row except A-037 and A-038, families B–E firing, F silent. Record.

### Amendment (its own commit, before any catalogue edit; `[const §XX.1]`)

- [ ] T067 [US3] (orchestrator) Edit `.specify/constitution.md` verbatim from data-model §F: the §F.3 PENDING Sync Impact Report block above the v4.0 → v4.1 block, §F.4's PENDING Status line, §F.1's §XVIII.2 bullet, §F.2's §XVIII.7 sentence (all 31 row IDs). Commit alone: `Constitution: amend §XVIII.2, §XVIII.7 — v4.2 PENDING (096)`.
- [ ] T068 [US3] (orchestrator) Scoped Codex Gate A round on the amended clause bodies (§XVIII.2 bullet, §XVIII.7 sentence, the SIR's Bump/Modified/Rationale) via `.claude/scripts/codex-tool.sh --mode review`, then an Opus judge; fix and repeat until P1=0 P2=0 (each change to a clause body gets another scoped round; exhaustion at round 3 goes to the owner). Archive the reviews in PARENT `research/G19-fix-fpml-iso20022/research/reviews/` as `{codex,opus}_096-fix-latest-tail-evidence_amendment_<n>_*.md`; fill the SIR's review line. A fix to a clause body changes data-model §F.1–§F.4 and quickstart §5a's `ANN`/`SENT7` constants in the same commit, and T073 re-runs after it.

### Records (after T067)

- [ ] T069 [US3] (orchestrator) `spec/feature-catalogue.md`: the 31 rows per data-model §E (version `5.0SP2, FIX Latest`; Spec ref per its table, A-037/A-038 unchanged; Status `done`; `/specify` `096-fix-latest-tail-evidence`; PR `#<pr>`; Tests cell; Verified = S0 + space + S1 with `<MT>` substituted). No other row changes.
- [ ] T070 [P] [US3] (orchestrator) `spec/coverage-index.md`: the 31 gap notes and the SUPERSEDED comment at its four sites (data-model §E), line count unchanged.
- [ ] T071 [P] [US3] (orchestrator) `spec/behaviors-and-limitations.md`: append data-model §E's widening text to the L-069-1 bullet; add a `096-fix-latest-tail-evidence` section only for what a witness or the differential exposed (none pre-numbered), and state in the evidence file which it was.
- [ ] T072 [US3] (orchestrator) Commit T069–T071: `096: catalogue A-035..A-065 done, coverage index, L-069-1 widened (fixpp#637)`.
- [ ] T073 [US3] (orchestrator) After T068 converges: run quickstart §5a: exit 0 (PENDING). Then every perturbation arm it lists on scratch copies (families A–F, the C mutants for both states, `PR=banana`, `PR=0`): each exits 1 with its family's line; the filled RATIFIED scratch copy and the v4.1-text copy exit 0. Record all.
- [ ] T074 [US3] (orchestrator) Run quickstart §5b (census: open count down by 31, only A-035 … A-065 changed, skipped counts equal) and §5c (SC-005) with their controls; record.

**Checkpoint**: records and amendment PENDING in place; quickstart §5 passes.

---

## Phase 6: Polish, verify, gates and close-out (plan P7)

- [ ] T075 (orchestrator) `/simplify` on the branch diff (`[const §XVI.7]`); its findings are applied by the implementer, then T007's §1 legs re-run if the header or 095's TU changed.
- [ ] T076 [P] (orchestrator) Instruments with their known positives: `python3 tools/check_line_citations.py --range origin/main..HEAD` and `--shift-audit origin/main..HEAD` (positive control: a commit adding a resolvable `file:NNN`, made in a throwaway `git worktree`, reports it), `.claude/scripts/check-comment-claims.py --root <lib> --base origin/main` (positive: a seeded result comment in the same throwaway worktree), clang-format/clang-tidy on the changed test files (`[const §IX.4]`); record each.
- [ ] T077 [P] (orchestrator) W8: `git diff --exit-code origin/main -- tests/session/test_077_allversions_builder_roundtrip.cpp tests/session/test_077_allversions_builder_roundtrip_vlatest.cpp`; record.
- [ ] T078 (orchestrator) Run every tier-1 source-audit job locally, the list derived from `.github/workflows/tier1.yml` at this head (not a judged subset), keeping a job → rc table in the evidence file.
- [ ] T079 (orchestrator) Catalogue close-out (Article XVII §8): confirm every feature-owned OFFICIAL row A-035 … A-065 reads `done` with its Tests and Verified cells, and each tail MsgType's coverage-index row names its two witnesses (quickstart §5a families B and D exit 0); record.
- [ ] T080 (orchestrator) Re-run quickstart §0–§5 on the final pre-verify head; record.
- [ ] T081 (orchestrator) `[const §XVII.7]` local pre-PR cycle at the final code head (after the sandbox-lock check): `conan install` (if the profile's packages are absent) + `cmake --preset linux-clang-debug` (configure) + full build + full `ctest`; record the SHA and pass counts (the PR body's `local build:` line cites them).
- [ ] T082 (orchestrator) `/speckit-verify 096-fix-latest-tail-evidence --skip=<every preset of the skill's matrix except linux-clang-debug> --waive=T002-<preset>:<owner ruling 2026-10-28, CI runs it>` (one `--waive` per skipped preset: the skill marks an unpaired `--skip` as FAIL; confirm the preset list from the skill file at run time, and whether its coverage step treats this tests-only diff as N/A or needs the coverage preset's ctest): writes `.specify/decisions/096-fix-latest-tail-evidence-verify.md`; copy the evidence-file entries and T081's result in; each preset T002 waived recorded `SKIPPED (--skip flag)` with its paired waiver (every preset but `linux-clang-debug`, MSVC not run), scored by the text (T002); any real waived FAIL or Codex-side waiver gets its label rationale written here; re-derive contracts W6's completeness (its output list against the arm rows) and record it. Verdict must be non-RED.
- [ ] T083 (orchestrator) B&L functional delta for Gate B: the L-069-1 widening plus any T071 row, or an explicit no-further-delta disposition, in the verify record; run `.claude/scripts/check_bl_delta.py gate --feature 096-fix-latest-tail-evidence` (PARENT) and record its rc.
- [ ] T083b (orchestrator) SecondBrain pass on the branch, before the push (pipeline step 15a): update `brain/components/test.md` (the new `tests/support/tail_rows_witness.hpp` and the Orchestra required-walk) and any other page for a touched surface; run `brain-freshness.yml`'s in-repo checks locally and PARENT `research/G19-fix-fpml-iso20022/tools/brain-external-sweep.sh`; record each result.
- [ ] T084 (orchestrator) Push the branch and open the PR titled `Constitution: amend §XVIII.2, §XVIII.7 — FIX Latest tail rows A-035..A-065 delivered in v1.0 on v50sp2 and vlatest (v4.2) (+ 096 FIX Latest tail evidence pass)`; body: change, rationale, affected rows and specs, `## Gate A` (record path, owner rulings; and, only if a `-waived` label applies, its rationale), `## Gate B` (filled at T088; its rationale only for a `-waived` outcome), verify verdict, the line `local build: green on linux-clang-debug @ <T081's SHA>` (`[const §XVII.7]`), no closing keyword for #637 (grep the body and commit messages). Then set the real PR number in the catalogue PR cells, re-run quickstart §5a with it, commit.
- [ ] T085 (orchestrator) `/gate-b` on the PR; rounds per the skill; a gate at its round cap goes to the owner. A Gate B fix that changes a clause body in T068's scope (the §XVIII.2 bullet, the §XVIII.7 sentence, the SIR's Bump/Modified/Rationale) re-opens T068's scoped round, then T073, before T086; a fix confined to §F.3's opening/review spans or §F.4's Status line needs only T073's re-run and Gate B's re-check.
- [ ] T086 (orchestrator) Owner Article XX §2 sign-off on v4.2 (`AskUserQuestion`, with the amended clause text and the scoped Gate A and Gate B tallies).
- [ ] T087 (orchestrator) `git fetch origin`; if `origin/main` moved, integrate it (merge into the branch), re-run quickstart §5a/§5b and the affected suites. Then the RATIFIED flip, the last content commit: the §F.3 RATIFIED block (opening and review spans naming Gate A, Gate B and `owner sign-off <date>` in order), the §F.4 RATIFIED Status line; quickstart §5a exits 0 in the RATIFIED state; record.
- [ ] T088 (orchestrator) Final sign-off order: PR body (re-confirm the `local build:` line against the final head) → labels per T002's ruling and T082's actual verdict, in ONE call through PARENT `.claude/scripts/gh-pr-meta.sh`, then read the labels back (`gh pr view --json labels`); a `-waived` label needs its `## Gate A …`/`## Gate B …` heading in the body (run the awk check locally first) → push LAST. Never cancel a duplicate gate-label wave.
- [ ] T089 (orchestrator) CI: wait on the PR's checks with one waiter (`gh pr view --json statusCheckRollup`); a red that is not a known flake is fixed (implementer for tests); a fix commit after T087 means the RATIFIED flip is redone as the last commit, then T088's order again.
- [ ] T090 (orchestrator) Merge if green (squash); verify the PR closes no issue; record the squash hash.
- [ ] T091 (orchestrator) Post-merge close-out: `git switch main && git merge --ff-only origin/main` in the main checkout (clean status only); purge the merged branch locally and on the remote; parent gitlink bump; `REMAINING-WORK.md` row 3 → DONE with its evidence in the detail file (`remaining-work/typed-messages.md`); `CLAUDE-history.md` + `CLAUDE.md` pointer; `phases/phase-4.md` and `phases/phase-4/<module>/README.md`; the verify record's User sign-off line; project memory; `codegraph sync` in the main checkout; `.specify/close-out.md`'s rows, each ticked.
- [ ] T092 (orchestrator) FINAL — **Feature-completeness audit**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale; (ii) every spec FR- and SC- maps to a landed test AND a landed implementation (for this tests-and-records feature: the landed witness/check AND the landed record or test code it requires); (iii) every feature-owned OFFICIAL catalogue row (A-035 … A-065) is `done` with a matching `coverage-index.md` entry. Record the verdict (100 % or fully-waived) in `.specify/decisions/096-fix-latest-tail-evidence-verify.md` `## Completeness`. It is first written before T085 (the `/gate-b` pre-flight 4d precondition), with each of T085–T091 dispositioned `Open at the audit — <reason>`, and re-confirmed here after merge.

---

## Dependencies and execution order

- Phase 1 → Phase 2 (T006–T008 block everything after).
- US1 v50sp2 (T009–T026) and US1 vlatest (T027–T051) touch different TUs but the same CMake file and
  the same build tree: run them serially (one build owner).
- US2 (T052–T065) needs T031 (vlatest TU and walk). T065 needs T053.
- US3: T066 after Phase 2 and before T067 (it needs the unedited tree); T067 before T069; T068 may run
  in parallel with T069–T072; T073 needs T067–T072 and T068 converged (and re-runs after any T068 fix).
- Phase 6: T075–T083 after US1–US3; T092's audit record must exist before T085's pre-flight; T086
  after T085 converges; T087 after T086; T088–T091 in order.

### Parallel opportunities

- T070 ∥ T071 (different record files).
- T076 ∥ T077.
- T068's Codex round runs in the background while T069–T072 are written.
- Inside each RED-arm block, arms are serial (one build tree), but G/F/I/J share one rebuild per suite.

## Implementation strategy

MVP = Phase 2 + the v50sp2 half of US1 (31 v50sp2 witnesses GREEN, census and golden check, their
arms): it already shows the closure bar on the tier 095 proved. The vlatest half, the differential
and the records follow; no row flips until all three legs (v50sp2 witness, vlatest witness,
differential) hold for it and the amendment is in.
