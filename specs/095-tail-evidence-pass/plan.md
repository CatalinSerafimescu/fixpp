# Implementation Plan: Typed app-message tail — evidence pass

**Branch**: `095-tail-evidence-pass` | **Date**: 2026-10-24 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/095-tail-evidence-pass/spec.md`

## Gate A

- Round 1 applied 2026-10-24: Codex P1=4 P2=7 P3=1; Opus post-judging P1=1 P2=9 P3=17; rewrite addresses root causes A (witness completeness checked from the frame: intent list, bidirectional readback, returned missing-member list, fail-loud walk, run-order-free census, non-circular known positives), B (FR-014's selection as a committed check over the QFJ file the peer loads), C (the live-cell harness contract, L0), D (traceability and citations), plus the owner ruling on N-3 (N-002 narrowed to 5.0SP2, follow-up issue for the v44 exclusion). Reviews: research/reviews/codex_095-tail-evidence-pass_gate_a_review.md, research/reviews/opus_095-tail-evidence-pass_gate_a_adversarial_review.md.
- Round 2 applied 2026-10-24: Codex P1=1 P2=4 P3=1; Opus post-judging P1=1 P2=5 P3=7; rewrite addresses root causes R1 (an arm for every walk status, comparator direction, presence leg and census finding: contracts W6's table, the synthetic dictionaries, Mutants G–K, and the CC delimiter rule), R2 (the selection check hard-asserts only BN, with BM as its known positive), R3 (via the owner ruling) and R4 (records: the extended §VI.3 disposition, the coverage-index entries listed verbatim, quickstart §7 and the `cmp`, fixpp#625 named, counts deleted, data-model §C's enum rule, the YAML rows specified); owner ruling "keep #462" applied. Reviews: research/reviews/codex_095-tail-evidence-pass_gate_a_2_review.md, research/reviews/opus_095-tail-evidence-pass_gate_a_2_adversarial_review.md. *(Superseded at round 3: R4's "extended §VI.3 disposition" is withdrawn; see Round 3 — disagreements.)*
- Round 3 (final, loop exhausted) 2026-10-24: Codex P1=1 P2=1 P3=2; Opus post-judging P1=0 P2=3 P3=4 (root causes RC-α a constitutional reading recorded as authority, RC-β arms enumerated per output not per predicate, RC-γ claims made of a larger population than checked). Reviews: research/reviews/codex_095-tail-evidence-pass_gate_a_3_review.md, research/reviews/opus_095-tail-evidence-pass_gate_a_3_adversarial_review.md.
- Post-exhaustion pass applied 2026-10-24 (owner: hand-edit + one Opus round, skip Codex): residuals R-1..R-7 + §VI amendment drafted; amendment clauses still owe a scoped Codex Gate A review per [const §XX.2].

### Round 1 — disagreements

Each is a Codex finding (or fix) the Opus judge marked Disagree, or a review fix this rewrite
replaced; the reason is the one the bundle acts on.

- **Codex #1b (canonical `[DocAbbrev §X.Y.Z]` Spec refs) — not applied.** The `[FIX50SP2] <Title>`
  form already sits on catalogue rows that are `done` (A-001); no lint in `tools/`, `ci/` or
  `.github/` checks it; `[const §VI.6]`'s closure list (a)–(d) does not include re-canonicalising
  Spec refs. Pre-existing debt, not 095's obligation. spec Normative References discloses it.
  *(Reworded at round 2: the form is a recorded `[const §VI.3]` disposition that 095 extends, not
  debt; Round 2 — disagreements, Codex #1.)* *(Superseded at Gate A round 3: §VI.3 covers
  design-decision rows in `[impl]`/`[constitution]` form; it does not authorize message-level refs on
  normative rows, and a coverage-index comment is not an amendment. The owner then ruled to amend
  §VI.2/§VI.5 in 095; Round 3 — disagreements.)*
- **Codex #1c (Verified cells must restate green tests + Codex pass + interop) — not applied.** The
  catalogue header's "Verified requires …" is a precondition for writing the cell, not its template
  (A-001's cell does not restate Codex pass); `[const §VI.6](c)` is met by Gate B on the PR.
- **Codex #2 (§VII.8: census must move to its own target; `-R` forbidden) — not applied.**
  `test_077_allversions_builder_roundtrip` is already a standalone target (one `add_executable`, one
  `add_test`), which §VII.8 keeps for exact-set gates; the census reads the registry of the binary
  its witnesses live in, so it cannot move to another one. The real defect, mislabelled targets and
  no feature label (Opus N-4), is applied: §VII.8 row below, `ctest -L 095`, `expected-ctest-095.txt`.
- **Codex #7, W6/W7 half (a known-positive literal contradicts W7) — not applied.** A known-positive
  assertion re-runs on every build, so it cannot rot silently, which is the defect the claims-rot
  rule names. W7 now carries that one narrow exception explicitly. (The self-violation half of #7,
  member lists in R-1/R-7, is applied: root cause B.)
- **Codex #4's fix (recursion over `OffsetTable::nested_group_slices` plus a MassQuote mutant) —
  replaced.** No tail MsgType needs a recursive readback, so it would ship an untested branch. The
  walk fails loudly on a nested required group instead, proven on 35=i (Opus root cause A).
- **Codex #5's fix (static descriptor manifest) — replaced** by iterating `::testing::UnitTest`'s
  registered tests, which already is a static registry; a manifest would be a second list.
- **Codex #10's fix (promotion withheld "until the gitlink lands", reconciliation "accepts" the
  cells) — replaced.** The cells are never promoted: they are not 089 conformance or
  validator-positive-control runs and have no ledger kind; their evidence is the `cell_results.yaml`
  row. *(Superseded at round 2: the registration lands with the gitlink bump, so no window exists in
  which `select_cells` sees a registered-but-undeclared cell; the live-cell contract's clause L0.)*
- **Codex #11's "workflow-path trigger assertion" — not applied.** The path is corrected; the
  `publish-counterparties.yml` `paths:` list was read and names both `tools/run_interop_cell.py` and
  `tools/interop_capability_minimums.yaml`, and a missing capability entry fails closed. Opus
  downgraded the finding to P3 for that reason.
- **Opus N-2's first fix option (`--gtest_fail_if_no_test_selected`) — not applied.** The vendored
  GoogleTest does not know the flag: run against the built `test_077_allversions_builder_roundtrip`
  with a zero-match filter, it prints the usage text and exits 0 (as
  `specs/085-fold-flat-cap-loop/quickstart.md` already records). The quickstart uses Opus's other
  option, an explicit count assertion that stops the block, plus `ctest --no-tests=error`.

### Round 2 — disagreements

- **Codex #1, the canonicalisation half (section-level `[DocAbbrev §X.Y.Z]` entries in the coverage
  index, catalogue Spec refs rewritten to them) — not applied.** The coverage index indexes the
  application layer "at message granularity (one row per MsgType)" and records message-level refs as
  a deliberate `[const §VI.3]` disposition (067's "ART VI §3 DISPOSITION" comment, which 069's spec
  extended). So it has no section-level entries to copy, and adding them would re-index the whole
  application layer against a recorded choice. `[const §VI.6]`'s (a)–(d) do not include
  re-canonicalisation. The residual Opus judged a 095 defect is applied (spec Normative References):
  095 extends the disposition to its 12 rows instead of calling the form "debt", lists the
  coverage-index entries, and quotes the FIXT entries. *(Superseded at Gate A round 3: §VI.3 covers
  design-decision rows in `[impl]`/`[constitution]` form; it does not authorize message-level refs on
  normative rows, and a coverage-index comment is not an amendment. Round 3 reverses this fix; Round
  3 — disagreements.)*
- **Codex #3's fixture for the tag-at-two-levels detector — replaced** by deleting the detector, the
  alternative Opus accepted. W4 and W5 share one enumeration that attributes by extent, so no check
  uses the first-occurrence lookup the detector guarded. The group-among-siblings detector keeps its
  synthetic fixture as Codex asked.
- **Codex #5's committed records checker — replaced** by the scripted quickstart §7 (Opus
  downgraded the finding to P3). The edit is a one-shot table change, Gate B reads the catalogue
  diff, and 067/069 closed more rows without a checker. §7 still has its own RED run, before P7.

### Round 3 — disagreements and owner rulings

- **Round 2's fix of Codex #1 (extend the `[const §VI.3]` disposition to the 12 rows) — reversed.**
  Round 3 judged it on the text. §VI.3 reaches rows "backed by design decisions instead of spec
  sections", in `[impl]`/`[constitution]` form, and the 12 rows are normative message rows. Only
  Article XX amends. Every forward-looking surface of the claim is deleted:
  - spec Normative References;
  - Summary item 4;
  - the Constitution Check row;
  - P7's coverage-index disposition line;
  - quickstart §7's check of that line and its perturbation.

  The log surfaces above carry supersession notes.
- **The residual's interim remedy (a Constitution Check note disclosing a pre-existing §VI.2
  non-conformance) — replaced by owner ruling.** "Amend the constitution in 095": FR-021 amends
  §VI.2 and §VI.5 (data-model §F), so the 12 rows' refs conform to the amended text. Nothing is
  disclosed as non-conforming.
- **Codex #1's remedy (section-bearing refs for the 12 rows) — not applied.** There is no
  `§X.Y.Z` to copy:
  - the FIX Repository numbers no message category;
  - the FIX 4.4 application volumes 3–5 head them "CATEGORY: …" and number no heading; the
    FIX 5.0–5.0SP2 volumes are not local and were not checked (spec Context).

  Applying the amendment's form did find two refs naming the wrong category: A-033 and C-003 are
  corrected (FR-018).
- **Owner: "Hand-edit + one Opus round, skip Codex"** for residuals R-1..R-7; this pass applies
  them. The amendment's clause text is new to every Codex round, so it carries its own scoped Codex
  Gate A round (FR-021).

## Summary

This feature closes 12 OFFICIAL catalogue rows (23 FIX 5.0SP2 MsgTypes) on the owner's amended bar.
It adds **tests and records**, plus one constitution amendment (`[const §VI.2]`, `[const §VI.5]`;
FR-021), and changes no production code (FR-020):

1. **US1, offline witnesses.** A new TU joins the existing `test_077_allversions_builder_roundtrip`
   executable. It has one test per MsgType, named `TailRows095.<RowId>_<Msg>` (`<RowId>` without its
   hyphen). Each test:
   - declares one intent list of `(path, literal)` entries covering every closure-required member
     (≥2 instances per required group, each with its delimiter and required members) and at least
     two fields;
   - builds with `fixpp::v50sp2::build_<Msg>`;
   - parses independently against `FIX50SP2.xml`;
   - compares the intent list with an enumeration of the parsed body, exactly, in both directions;
   - asserts that a required-presence check on the parsed frame finds nothing missing. That check
     is derived at runtime from a pugixml walk of the dictionary under the rule in research R-1, so
     it is not taken from the witness's own list.

   A census over the registered tests proves exactly one witness per MsgType. Every output of the
   walk, the comparator, the presence check and the census has an arm (contracts W6's table): measured
   QuickFIX-cpp output (`golden.csv`), deficient 35=E and 35=CC frames, the fail-loud on 35=i, inline
   synthetic dictionaries, and scratch mutants (research R-3).
2. **US2, N-002 and N-003.** Two named registry pins go in the existing
   `codegen_077_v50sp2_builder_completeness_test` executable, and one routing cell goes in
   `session_pure_tests`. The cell shows that an inbound BE and BW reach `from_app`, each leg with
   its own RED arm.
3. **US3, the live FIXT cell.** A probe-pattern cell runs on 033's FIXT session, with fixpp as
   initiator, against QuickFIX-cpp and QuickFIX/J (two cells). fixpp sends 35=BN, built by
   `build_ExecutionAck`, on a shared fixture that loads the production `FIX50SP2.xml`. The peer's
   generic per-field readback is compared by 089's `compare_streams`. BN was selected by a committed
   check over the vendored file and the QuickFIX/J file the peer loads (research R-7).
   - Parent harness: register the cells with the fields the live-cell contract's clause L0 names,
     and fix two blockers (research R-9).
   - Ordering (R-10; owner ruling "Keep #462: register after merge."): the harness change stays
     uncommitted in the parent working copy until the fixpp PR merges. Then it lands in one parent
     commit with the gitlink bump, the image republishes, and `interop-live` is dispatched on fixpp
     `main`. Before the merge, the live evidence is local.
4. **US4, records.** The 12 rows move to `done`. A-018, A-029, A-030 and N-002 narrow to 5.0SP2
   (owner ruling 2026-10-24 for N-002). A-033's and C-003's Spec refs are corrected to their
   MsgTypes' category (FR-018). The existing coverage-index rows name their witnesses, and the B&L
   delta is added. The v44 BE/BF exclusion is fixpp#625.
5. **The amendment (FR-021, owner ruling 2026-10-24).** `[const §VI.2]` admits the message-level
   form `[DocAbbrev] Category` for application-message rows and records that its CI check is owed;
   `[const §VI.5]` lets Normative References list such refs. Constitution v4.0 → v4.1 (MINOR). It
   lands before the catalogue edits, and reads PENDING until a scoped Codex Gate A round, Gate B and
   the owner's sign-off (data-model §F).

## Technical Context

**Language/Version**: C++23 (tests), Python 3 (harness, schema tests), Java 17 (QuickFIX/J counterparty)

**Primary Dependencies**:
- fixpp `fixpp::builders::v50sp2` (078) and `fixpp_wire` / `fixpp_dictionary`;
- pugixml (already a test dependency);
- GoogleTest;
- QuickFIX-cpp v1.16.0 and QuickFIX/J 3.0.1 counterparties in the GHCR image
  `fixpp-interop-counterparties`.

**Storage**: N/A

**Testing**:
- gtest, in existing targets (`[const §VII.8]`, categories in the Constitution Check): the
  standalone `test_077_allversions_builder_roundtrip` and `codegen_077_v50sp2_builder_completeness_test`,
  the grouped bucket `session_pure_tests`, and the 033 FIXT interop target
  `interop_hp_fixt50sp2_logon_hb_logout_test`; each gains the label `095`;
- pytest for the parent harness and `cell_results_schema_check_test.py`.

**Target Platform**:
- Linux (gcc and clang presets) for the offline witnesses, which run on every tier like 077's
  today;
- the live cell runs in `interop-live` (Linux, advisory) and locally.

**Project Type**: library — evidence and test feature

**Performance Goals**: N/A. No library code changes, so `[const §VIII.3]` does not apply.

**Constraints**:
- No production source change (FR-020).
- The witness TU must not include a second `all.hpp`; it uses the per-message headers (R-2).
- Builds use `-j2`, one owner per build directory.
- A parallel lane (fixpp#613, `~/Work/Programming/fixpp-parallel`) builds on F:. Check the
  scratchpad marker `lane613.build.active` before a heavy build here.
- The main checkout's `build/linux-clang-debug` is on E:. Read `df -h /mnt/e` first.

**Scale/Scope**:
- 23 witnesses, the census, the known-positive tests of contracts W6, one verify-time selection
  check;
- 2 pins and 1 routing cell;
- 2 live cells;
- parent harness: the files of the parent table under Files changed;
- 12 catalogue rows.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Article | Applies | Disposition |
|---|---|---|
| §I.3 (tag gate) | yes | 12 rows go `backlog` → `done`. Re-derive the gate's backlog count before and after from `spec/feature-catalogue.md` (SC-004). |
| §VI.6 (closing a `done` row) | yes | (a) 095 is the `/specify` artifact. (b) Every row's witnesses run green at verify, and their RED arms are recorded. (c) Codex Gate B runs. (d) Gate A runs on this bundle. |
| §VI.4 (coverage-index) | yes | Each tail MsgType's existing row in `spec/coverage-index.md` names its witness in its gap-note column; no new row (FR-019). No new OFFICIAL row, so no new entry is owed. |
| §VI.5 (Normative References) | yes, as amended by FR-021 | spec.md `## Normative References` lists the section entries that inform 095 (`[FIX50SP2 §3.1]`, `§3.2`, `§3.3`, and the FIX-SL/FIXT entries, titles quoted), then the 12 rows' message-level refs. Under the amended clause those may be listed but do not stand in for a section entry. It also lists the coverage-index MsgType entries, grouped by their "Application Messages — …" heading. |
| §VI.2 (Spec ref form) | yes, as amended by FR-021 | The 12 rows' refs take the amended clause's message-level form `[DocAbbrev] Category` (data-model §F). A-033 and C-003 are corrected to their MsgTypes' category (FR-018); data-model §E's recipe re-checks all 12. The clause's CI check is owed, and nothing lints Spec refs today: `grep -n -i catalog .github/workflows/*.yml` is empty, and `tools/`/`ci/` hold no Spec-ref check. That gap is recorded as a follow-up issue the orchestrator files (below), with the `[const §IX.6]` "catalogue consistency check" that also has no Tier-1 job. Which rows outside the 12 fit the amended form is not checked here. The follow-up's lint does that mechanically; data-model §F has the recipe that lists the candidates. |
| §VI.3 (design-decision rows) | read, not applicable | §VI.3 is for rows backed by design decisions, in `[impl]`/`[constitution]` form. The 12 are normative message rows, so §VI.3 does not reach them (Gate A round 3). 067's coverage-index "ART VI §3 DISPOSITION" comment is left as written: it is history, and an amendment is the only route (`[const §XX.1]`). |
| §VI.6(b) (verifying tests) | yes | Each row's Tests cell names test IDs that exist. Quickstart §7 resolves every cited gtest name against `--gtest_list_tests` of its own binary and the live-cell IDs against the harness `CELLS` registry, and checks Status, version cells and coverage-index gap notes; its RED run is recorded before P7. |
| §VII.8 (grouped tests; `ctest -L`) | yes | No new ctest entry. The witnesses, census and known positives join `test_077_allversions_builder_roundtrip`, a standalone exact-set-gate target (its census reads its own binary's registry). The pins join `codegen_077_v50sp2_builder_completeness_test`, a standalone exact-set completeness gate. The routing cell joins the grouped bucket `session_pure_tests` (isolation-safe). The live cell joins `interop_hp_fixt50sp2_logon_hb_logout_test`, a live harness-selected target. Each gains `set_property(TEST … APPEND PROPERTY LABELS 095)`; verify selects with `ctest -L 095 --no-tests=error` against `expected-ctest-095.txt`. |
| §IX.1 (coverage) | n/a | No library line added. The verify record states it per module. |
| §IX.4 (static analysis) | yes, on test code | `/speckit-verify` Step 1 runs all four tools on every hand-written C/C++ file the diff touches. Its scope is an exclude-list (`specs/`, `third_party/`), so `tests/**` is in scope. It is a changed-lines instrument: debt on lines this diff does not modify is recorded, not fixed. Per tool, for this diff (the new and edited TUs under `tests/session/`, `tests/codegen/`, `tests/interop/happy/`): **clang-tidy** runs with `-p build/linux-clang-debug`. It needs a compile command, so the verify record shows each touched TU is in that build's `compile_commands.json`; any TU that is not is recorded `SKIPPED (no compile command)`, never PASS. **clang-format** runs `--dry-run --Werror` on every touched TU; it needs no compile command. **cppcheck** runs with verify Appendix A's flag set and `cppcheck-suppressions.txt` on every touched TU; it needs no compile command. **include-what-you-use** has the same compile-command rule as clang-tidy. A tool that is not installed is recorded `SKIPPED (tool not installed)` and named in the record's environment section. No workflow runs any §IX.4 tool, so verify is the only enforcement point. The CMake and YAML edits are not C/C++ and are out of §IX.4's scope. |
| §VIII.3 (bench with a perf change) | n/a | No library code change. |
| §X.7 / C-ABI | n/a | No C-ABI change. |
| §XII.7 (fail closed) | n/a | No production path changes. |
| §XVI.6 (orchestrator does not implement) | yes | Every test TU, CMake edit, harness and counterparty change goes to `phase-implementer`. The orchestrator may write the spec bundle, the `.specify/constitution.md` amendment (data-model §F's text, verbatim), `spec/` catalogue / coverage-index / B&L text and parent docs, places fixpp#625 in the parent's `phases/phase-4/issue-batches.md` if it is not there, and makes and pushes the one post-merge parent commit (the harness files in it are the implementer's). |
| §XVI.7 (`/simplify` before verify) | yes | `/simplify` runs on the implementation diff before `/speckit-verify` (phase P8). |
| §XVII.1 (Gate A trigger) | yes | The session and interop test surfaces are touched, and a live-interop vehicle changes in the parent. Gate A runs (`/gate-a 095-tail-evidence-pass`). |
| §XVII.7 (local pre-PR build gate) | yes | One FULL `ctest --preset linux-clang-debug` on a committed tree runs as the LAST step of verify, after `df -h /mnt/e` and a check of `/mnt/c/temp/fixpp/.sandbox-lock`. Packaging entries compile consumer projects. **Resource gate:** an `AskUserQuestion` precedes the first agent-run local build, and the owner approves it before it runs. |
| §XVII.8 (verify before Gate B) | yes | `/speckit-verify` runs in the main checkout, which owns this branch. |
| §VII.6 (interop against an independent FIX implementation) | yes — already met by 089/033; this adds to it | Two live cells against both QuickFIX peers, run locally before merge and in `interop-live` dispatched on `main` after the post-merge parent commit (SC-003). |
| §XVIII.7 (v1.0 codegen scope) | read, not amended | Its text keeps "The N-002/N-003 session-FSM pair … remains deferred to the separate v1.0-tagging gate". DECISION 2 supersedes that reason for the v50sp2 rows this feature closes; the v44 BE/BF exclusion it describes is unchanged here and is the subject of fixpp#625. 095's only constitution edit is FR-021's §VI.2/§VI.5 amendment; §XVIII.7 is not touched. |
| §XX.1 (amend first) | yes | 095 writes two Spec ref cells in the message-level form (FR-018). Under v4.0's §VI.2 that form does not conform, so the article is amended first, in the same PR. The rationale is committed in the Sync Impact Report (data-model §F.3). The constitution commit precedes P7's catalogue commit. |
| §XX.2 (amendment process) | yes | The PR is titled `Constitution: amend §VI.2, §VI.5 — message-level Spec refs for application-message rows (v4.1) (+ 095 tail evidence pass)`. Its description states the change, the rationale and the affected rows and specs. **Codex Gate A on the amendment:** a scoped Codex Gate A round on the two clause bodies and the Sync Impact Report (data-model §F.1–§F.3), with an Opus judge, under the owner's `/gate-a` invocation. It is owed because the post-exhaustion pass skipped Codex. Then Gate B, then owner sign-off. Like 094's, the amendment rides the feature PR rather than a standalone PR, and the title form meets §2's first step. |
| §XX.3 (Sync Impact Report) | yes | data-model §F.3 has a new block above the v3.0 → v4.0 report. It records the date, the bump and its §4 classification, the clauses changed, the rationale, the affected rows and specs, the review and sign-off, and the feature and PR. Its header and the Status line (§F.4) read PENDING until the scoped Gate A round, Gate B and the owner's sign-off. The RATIFIED flip is the last commit before merge. |
| §XX.4 (classification) | yes | **MINOR, v4.0 → v4.1, no `CHANGELOG.md` entry.** The test is the one the v4.0 report applies (four earlier drafts got it wrong at round 1): does any PR mergeable under v4.0 fail under v4.1? None does. §VI.2 adds a form and leaves the section form valid for every row it was valid for. Its last sentence keeps "vague refs" non-conforming and only records that no CI job checks it, which is today's state. §VI.5 adds a permission ("may also be listed") and restates the existing obligation word for word. It does not newly require any entry: a `/specify` artifact that lists only section entries conforms under both versions. Requiring message-level entries would have made a bundle conforming under v4.0 fail, which is a MAJOR change, and it was rejected for that reason. |

**Result:** PASS once FR-021's amendment lands. Under v4.1 the 12 rows' refs conform (A-033 and C-003 after FR-018's corrections). No Complexity Tracking row.

**Follow-up issue (the orchestrator files it; label from `gh label list`):** "Constitution §VI.2: lint
catalogue Spec refs, and give §IX.6's 'catalogue consistency check' a Tier-1 job". Body: v4.1's
§VI.2 says no CI job checks the clause yet and that the check is owed. `[const §IX.6]` lists a Tier-1
"catalogue consistency check", and `grep -n -i catalog .github/workflows/*.yml` returns nothing, so
neither exists. The lint should:
- check every OFFICIAL row's Spec ref against the amended §VI.2: the section form, the message-level
  form `[DocAbbrev] Category` for application-message rows (the token's vendored `dictionaries/<ver>.xml`
  defines every MsgType the row names; the category is the vendored
  `dictionaries/orchestra/OrchestraFIXLatest.xml` `category` of each MsgType), the whole-layer form, or §VI.3's `[impl]`/`[constitution]`;
- carry a table from category identifier to title;
- run in `tier1.yml`.

Its first run will list the rows outside 095 that do not fit, FIX Latest's A-035..A-065 among the
candidates. Those rows need re-canonicalising or an explicit exception before the lint can be
required. FUNCTIONAL TRACK row 3 should check its rows against §VI.2 as amended.

## Project Structure

### Documentation (this feature)

```text
specs/095-tail-evidence-pass/
├── plan.md            # this file
├── research.md        # R-1..R-12
├── data-model.md      # the 23 MsgTypes → rows → witnesses; required-member tree; live-cell record
├── quickstart.md      # commands that run every witness and RED arm, and the live cell
├── contracts/
│   ├── witness-contract.md     # what one offline witness must do (FR-001..FR-005)
│   └── live-cell-contract.md   # what the live cell must do and record (FR-009..FR-016)
├── expected-ctest-095.txt      # the ctest entries labelled 095 (093's format)
└── tasks.md           # /speckit-tasks
```

### Files changed

**fixpp (this branch):**

| File | Change |
|---|---|
| `tests/session/test_095_tail_rows_roundtrip.cpp` | NEW: 23 witnesses, the walk (taking a `pugi::xml_document`) and `required_members_missing`, the census, the known-positive tests of contracts W6 (synthetic dictionaries inline), the `DISABLED_` selection check (R-3, R-7) |
| `tests/session/CMakeLists.txt` | add the TU to `test_077_allversions_builder_roundtrip`; link pugixml if the target does not already; a compile definition naming `tools/quickfix_required_golden/golden.csv`; `APPEND PROPERTY LABELS 095` on it and on `session_pure_tests` |
| `tests/codegen/test_077_v50sp2_builder_completeness.cpp` | +2 named pins (N-002, N-003), in target `codegen_077_v50sp2_builder_completeness_test` |
| `tests/codegen/CMakeLists.txt` | `APPEND PROPERTY LABELS 095` on `codegen_077_v50sp2_builder_completeness_test` |
| `tests/session/<routing cell source in session_pure_tests>` | +1 cell, `TailRouting095.InboundBeAndBwReachFromApp` (implementer picks the existing fixture TU and names it in the task report) |
| `tests/interop/happy/hp_support.hpp` | the shared production FIXT11/FIX50SP2 engine-config fixture and its digest (contracts L1); `production_dictionary_and_digest` unchanged |
| `tests/interop/happy/hp_fixt50sp2_app_message_test.cpp` | NEW: the live cell (2 parameters) |
| `tests/interop/CMakeLists.txt` | the new source joins `interop_hp_fixt50sp2_logon_hb_logout_test`; link `fixpp::builders::v50sp2` + codegen include; `target_compile_definitions` for `FIXPP_DICT_DATA_DIR` after the `fixpp_add_interop_test` call; `APPEND PROPERTY LABELS 095` |
| `tests/interop/conversation/fixt_app_script.yaml` | NEW: one-step script, F-01 / 35=BN |
| `tests/interop/cell_results.yaml`, `cell_results_schema_check_test.py`, `expected-skips-without-counterparty.txt` | +2 rows / ids / lines |
| `specs/095-tail-evidence-pass/expected-ctest-095.txt` | NEW: one ctest name per line |
| `.specify/constitution.md` | FR-021: §VI.2 and §VI.5 clause bodies, the Status line, a new Sync Impact Report block (data-model §F). No `CHANGELOG.md` change (MINOR) |
| `spec/feature-catalogue.md`, `spec/coverage-index.md`, `spec/behaviors-and-limitations.md` | rows → `done`; version narrowing (four rows); A-033 and C-003 Spec refs (FR-018); existing coverage rows' gap notes; B&L delta |

**Parent (`phase-9-harness/`): uncommitted until the fixpp PR merges, then one parent commit with
the gitlink bump (research R-10):**

| File | Change |
|---|---|
| `tools/run_interop_cell.py` | register the 2 cells with the L0 fields (`combo_id` C1/C3, `run_kind="app-message-evidence"`, arm); `FIXPP_FIX50SP2_DICT_XML` at `_run_initiator_cell`; per-cell expected dictionary in `_finalize`'s FR-011 check |
| `tools/test_run_interop_cell.py` | tests for the per-cell dictionary and the registration fields |
| `tools/interop_capability_minimums.yaml` | +2 entries |
| `quickfixj/.../InteropCounterparty.java` | move the conversation-mode seqnum bump + Heartbeat inside the A-TESTREQ check |
| `golden/<2 cells>.fix` | captured with `--update-goldens`, then reviewed |
| `tools/emit_matrix.py`, `tools/test_emit_matrix.py` | the L0 `run_kind` branches: probe rule narrowed to `"validator-positive-control"` (stubbed-template raise tested), promotion skipped, N2 unchanged, and a both-directions pin for the two cells beside `RegistryAgreesWithTheRealTemplateTest` — each tested |

**Structure decision:** tests only, in existing ctest targets (no new entry). Parent harness changes stay
limited to the files above.

## Phases

0. **Before the first local build:** `AskUserQuestion` for the build (`[const §XVII.7]` resource
   gate). Orchestrator, at any point before P7: place fixpp#625 (the v44 BE/BF exclusion) in the
   parent's `phases/phase-4/issue-batches.md` if it is not already placed.
1. **P0 — walk, helper, census and known positives (US1 foundation).**
   - Build the runtime walk (`WalkResult` per MsgType, over a `pugi::xml_document`) and
     `required_members_missing` over W4's enumeration.
   - Add the golden field-leg test, the deficient 35=E and 35=CC frames, the 35=i fail-loud and the
     synthetic dictionaries S1/S2 (contracts W6). Confirm the parser accepts every deficient frame
     (W6: if it refuses one, stop and report). Prove the deficient-frame test RED under Mutant C, under
     Mutants L and M (one W5 leg each), and each walk detector RED under its disabled-detector mutant.
   - Add the labels and `expected-ctest-095.txt`.
2. **P1 — 23 witnesses and the census (US1).** One test per MsgType (data-model §A), each with its
   intent list and bidirectional readback. Run every remaining mutant row of contracts W6 (A, B, D,
   E, F, G, H, I, J, K and the changed literal).
3. **P2 — N-002/N-003 (US2).** Add the two pins and the routing cell. Run the routing cell's two
   RED arms (scratch classifier mutants, `"BE"` then `"BW"`).
4. **P3 — parent harness (US3, parent side), uncommitted.**
   - Apply R-9's items 1–4 and 6 (contracts L0), with their tests, as uncommitted edits in the parent
     working copy (research R-10 step 1: nothing staged under `phase-9-harness/`, a copy kept outside
     the tree, no `git stash`).
   - Build QFJ.
   - Extract the QF-cpp counterparty from the image.
5. **P4 — fixpp live cell (US3).**
   - Run the FR-014 selection check (quickstart §3); it must pass before the cell is built on BN.
   - Add the shared fixture, the cell source, CMake and script.
   - Run locally against both peers. Capture the goldens (R-9 item 5) and review them.
   - Run the RED arms: change one chosen value after the expected record is written, and force a
     peer reject by sending an unknown required-field violation, e.g. omit ExecID(17) with the
     peer's dictionary on.
6. **P5 — evidence files and pins (US3).**
   - Run the parent's `RegistryAgreesWithTheRealTemplateTest` locally with the registration in place
     and before the rows exist, and record its RED naming the two cells.
   - Edit `cell_results.yaml` (research R-11) and the skip list.
   - Edit the `EXPECTED_IDS` pin, RED first.
7. **P6 — local live evidence (stands in for the PR's `interop-live`).** Run quickstart §5 in full:
   the whole-population `--list` reconcile, every live cell through `ci/run-interop-live.py` against
   the local harness (the 089 CONV and PROBE cells and the 033 FIXT cells included, L7), the harness
   suites, and the real-template test, now green.
8. **P7 — amendment, then records (US4).**
   - Run quickstart §7 first and record its RED (no row is `done` yet).
   - **Amend first** (`[const §XX.1]`, FR-021): apply data-model §F to `.specify/constitution.md`
     (§VI.2, §VI.5, the Status line and the Sync Impact Report, all PENDING), in a commit of its
     own, before any catalogue edit. File the follow-up issue (Constitution Check, below the table)
     before this commit, so its number replaces `fixpp#<N>` and no placeholder lands.
   - Catalogue flips with witness names.
   - Version narrowing: A-018, A-029, A-030, N-002 (data-model §E).
   - Spec ref corrections: A-033 → `[FIX50SP2] Market Data`, C-003 → `[FIX50SP2] Account
     Reporting` (FR-018). Run data-model §E's recipe and record its output for all 12 rows.
   - coverage-index: each existing tail row's gap note names its witness.
   - B&L delta: the live cell's validation-off disclosure under L-041-2, a B-row naming what the
     live cell does and does not witness, and a line that FIX 4.4 BE/BF have no typed builder (the
     v44 exclusion, fixpp#625).
   - Run quickstart §7 again; it must pass.
9. **P8 — simplify, verify, Gate B, PR.**
   - `/simplify` on the implementation diff (`[const §XVI.7]`).
   - `/speckit-verify`, which carries the local live evidence of P4–P6 (commands, outputs, peer
     builds) and quickstart §7's output.
   - Push the fixpp branch and open the PR, titled in `[const §XX.2]` form (FR-021). The PR body
     discloses what its `interop-live` will show (below).
   - The scoped Codex Gate A round on the amendment (data-model §F.1–§F.3), under the owner's own
     `/gate-a` invocation, if it has not already run on the bundle before implementation.
   - Run `/gate-b`. Then owner sign-off on the amendment. Then the last commit before merge flips
     the Sync Impact Report and the Status line to RATIFIED (data-model §F.3, §F.4).
10. **P9 — after the merge (close-out, research R-10 steps 4–6).**
   - The fixpp close-out resets the main checkout to `main` (close-out rows 16–17).
   - One parent commit: the harness changes, the goldens, and the gitlink bump to the merged fixpp
     `main` (alone, without the bump, if another parent commit already moved the gitlink past the
     merge). Check `git ls-tree HEAD research/G19-fix-fpml-iso20022/library` and the staged paths
     first.
   - Push parent `main` (owner authorization 2026-10-24). This carries any unpushed parent commits.
   - Record `harness-tests` green on that commit.
   - Wait for `publish-counterparties`, and record the digest. Re-dispatch the parent
     `interop-matrix` if its push run started before the publish.
   - Dispatch `interop-live` on fixpp `main`. Both new cells must pass; record the run id and digest.
     If they do not, the close-out stops and goes to the owner (SC-003).

### What the fixpp PR's `interop-live` shows (advisory; disclosed in the PR body)

The PR head adds two ids to `expected-skips-without-counterparty.txt`, and the pulled pre-095 image
does not register them. `ci/run-interop-live.py` builds the image's `--list-binaries` targets (the
033 FIXT target, with the new cell source, among them), then fails its coverage reconcile and stops
with "population reconciliation FAILED -- not running any cell". So the PR's lane compiles the new
cell but runs **no** live cell, the existing ones included; P6's local full run carries that coverage
(research R-10). The lane is advisory (`interop-live.yml`'s header) and not in branch protection.
Gate B and the merge do not wait on it.

## Risks

- **The live cell fails for a harness reason, not a message reason.** Two such reasons are already
  known and fixed in P3. Others may surface on the first local run (e.g. the hello record's
  dictionary digest under FIXT). Mitigation: P4–P6 run everything locally before the PR.
- **The uncommitted harness edits are lost or swept into someone else's parent commit.** The parent
  checkout and its index are shared with other sessions, which commit and push the parent. Mitigation:
  nothing under `phase-9-harness/` is staged before P9; every committer in the window checks `git
  status` and stages explicit paths; a copy of every changed and new harness file is kept outside
  the tree (research R-10).
- **Other fixpp PRs' `interop-live` around the merge.** Between the merge and the republish, a PR
  based on the post-095 `main` fails reconcile the same way; after it, a PR based on a pre-095 `main`
  fails reconcile's skew check until it rebases. Advisory and disclosed; fixpp#613's PR may see it
  (research R-10).
- **The walk fails loudly on a tail MsgType** (a nested required group, a group among siblings, a
  delimiter chain). The implementer stops and reports; extending the walk is a design change that
  returns to the orchestrator.
- **A deficient frame cannot be parsed**, so its W5 leg cannot be reached from a parsed frame. The
  implementer stops and reports (contracts W6).
- **The FR-014 selection check disagrees on BN** (for instance, a QuickFIX/J file requires more).
  The live cell is not built on BN; the orchestrator re-selects with the check's output.
- **The rule-derived check finds a member no witness can set.** For example, a required field whose
  typed `Args` member is absent. If that happens, the implementer stops and reports. It would be a
  codegen defect (FR-020) and goes to the owner.
- **The post-merge parent push carries unrelated parent commits.** The owner accepted this on
  2026-10-24. The push also triggers the parent's own workflows. Check `ls-tree HEAD <submodule>`
  before pushing, so the pushed gitlink is the intended one.
- **A peer (another Claude session) pushes the parent meanwhile.** Before pushing, check
  `git status` for staged files that are not ours (shared staging), and run `git fetch`.

## Complexity Tracking

None.
