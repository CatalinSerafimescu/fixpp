# Implementation Plan: Typed app-message tail — evidence pass

**Branch**: `095-tail-evidence-pass` | **Date**: 2026-10-24 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/095-tail-evidence-pass/spec.md`

## Gate A

- Round 1 applied 2026-10-24: Codex P1=4 P2=7 P3=1; Opus post-judging P1=1 P2=9 P3=17; rewrite addresses root causes A (witness completeness checked from the frame: intent list, bidirectional readback, returned missing-member list, fail-loud walk, run-order-free census, non-circular known positives), B (FR-014's selection as a committed check over the QFJ file the peer loads), C (the live-cell harness contract, L0), D (traceability and citations), plus the owner ruling on N-3 (N-002 narrowed to 5.0SP2, follow-up issue for the v44 exclusion). Reviews: research/reviews/codex_095-tail-evidence-pass_gate_a_review.md, research/reviews/opus_095-tail-evidence-pass_gate_a_adversarial_review.md.

### Round 1 — disagreements

Each is a Codex finding (or fix) the Opus judge marked Disagree, or a review fix this rewrite
replaced; the reason is the one the bundle acts on.

- **Codex #1b (canonical `[DocAbbrev §X.Y.Z]` Spec refs) — not applied.** The `[FIX50SP2] <Title>`
  form already sits on catalogue rows that are `done` (A-001); no lint in `tools/`, `ci/` or
  `.github/` checks it; `[const §VI.6]`'s closure list (a)–(d) does not include re-canonicalising
  Spec refs. Pre-existing debt, not 095's obligation. spec Normative References discloses it.
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
  row. Before the gitlink bump, `select_cells` raises on them (fixpp#431 rule) rather than accepting
  them (the live-cell contract's clause L0).
- **Codex #11's "workflow-path trigger assertion" — not applied.** The path is corrected; the
  `publish-counterparties.yml` `paths:` list was read and names both `tools/run_interop_cell.py` and
  `tools/interop_capability_minimums.yaml`, and a missing capability entry fails closed. Opus
  downgraded the finding to P3 for that reason.
- **Opus N-2's first fix option (`--gtest_fail_if_no_test_selected`) — not applied.** The vendored
  GoogleTest does not know the flag: run against the built `test_077_allversions_builder_roundtrip`
  with a zero-match filter, it prints the usage text and exits 0 (as
  `specs/085-fold-flat-cap-loop/quickstart.md` already records). The quickstart uses Opus's other
  option, an explicit count assertion that stops the block, plus `ctest --no-tests=error`.

## Summary

This feature closes 12 OFFICIAL catalogue rows (23 FIX 5.0SP2 MsgTypes) on the owner's amended bar.
It adds **tests and records only** and changes no production code (FR-020):

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

   A census over the registered tests proves exactly one witness per MsgType. The known positives
   are measured QuickFIX-cpp output (`golden.csv`), deficient 35=E frames, and the walk's fail-loud
   on 35=i (research R-3).
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
   - The parent harness change is pushed first, then the image is republished, then the fixpp PR
     is pushed (R-10).
4. **US4, records.** The 12 rows move to `done`. A-018, A-029, A-030 and N-002 narrow to 5.0SP2
   (owner ruling 2026-10-24 for N-002). The existing coverage-index rows name their witnesses, and
   the B&L delta is added. The orchestrator files the follow-up issue for the v44 BE/BF exclusion.

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
- 23 witnesses, the census and three known-positive tests, one verify-time selection check;
- 2 pins and 1 routing cell;
- 2 live cells;
- parent harness: about 6 files;
- 12 catalogue rows.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Article | Applies | Disposition |
|---|---|---|
| §I.3 (tag gate) | yes | 12 rows go `backlog` → `done`. Re-derive the gate's backlog count before and after from `spec/feature-catalogue.md` (SC-004). |
| §VI.6 (closing a `done` row) | yes | (a) 095 is the `/specify` artifact. (b) Every row's witnesses run green at verify, and their RED arms are recorded. (c) Codex Gate B runs. (d) Gate A runs on this bundle. |
| §VI.4 (coverage-index) | yes | Each tail MsgType's existing row in `spec/coverage-index.md` names its witness in its gap-note column; no new row (FR-019). No new OFFICIAL row, so no new entry is owed. |
| §VI.5 (Normative References) | yes | spec.md `## Normative References`, added at Gate A round 1. |
| §VI.2 (canonical Spec ref) | no change | The 12 rows' Spec refs are left as they stand; re-canonicalising them is pre-existing debt (Gate A round 1 disagreement on Codex #1b). |
| §VI.6(b) (verifying tests) | yes | Each row's Tests cell names test IDs that exist; verify greps every cited ID against `ctest -N` / `--gtest_list_tests`. |
| §VII.8 (grouped tests; `ctest -L`) | yes | No new ctest entry. The witnesses, census and known positives join `test_077_allversions_builder_roundtrip`, a standalone exact-set-gate target (its census reads its own binary's registry). The pins join `codegen_077_v50sp2_builder_completeness_test`, a standalone exact-set completeness gate. The routing cell joins the grouped bucket `session_pure_tests` (isolation-safe). The live cell joins `interop_hp_fixt50sp2_logon_hb_logout_test`, a live harness-selected target. Each gains `set_property(TEST … APPEND PROPERTY LABELS 095)`; verify selects with `ctest -L 095 --no-tests=error` against `expected-ctest-095.txt`. |
| §IX.1 (coverage) | n/a | No library line added. The verify record states it per module. |
| §IX.4 (static analysis) | yes, on test code | clang-format on the new TUs. clang-tidy follows the repo config for `tests/`. |
| §VIII.3 (bench with a perf change) | n/a | No library code change. |
| §X.7 / C-ABI | n/a | No C-ABI change. |
| §XII.7 (fail closed) | n/a | No production path changes. |
| §XVI.6 (orchestrator does not implement) | yes | Every test TU, CMake edit, harness and counterparty change goes to `phase-implementer`. The orchestrator may write the spec bundle, `spec/` catalogue / coverage-index / B&L text and parent docs, and files the follow-up issue. |
| §XVI.7 (`/simplify` before verify) | yes | `/simplify` runs on the implementation diff before `/speckit-verify` (phase P8). |
| §XVII.1 (Gate A trigger) | yes | The session and interop test surfaces are touched, and a live-interop vehicle changes in the parent. Gate A runs (`/gate-a 095-tail-evidence-pass`). |
| §XVII.7 (local pre-PR build gate) | yes | One FULL `ctest --preset linux-clang-debug` on a committed tree runs as the LAST step of verify, after `df -h /mnt/e` and a check of `/mnt/c/temp/fixpp/.sandbox-lock`. Packaging entries compile consumer projects. **Resource gate:** an `AskUserQuestion` precedes the first agent-run local build, and the owner approves it before it runs. |
| §XVII.8 (verify before Gate B) | yes | `/speckit-verify` runs in the main checkout, which owns this branch. |
| §VII.6 (interop against an independent FIX implementation) | yes — already met by 089/033; this adds to it | Two live cells against both QuickFIX peers, run locally and in `interop-live` on the PR head (SC-003). |
| §XVIII.7 (v1.0 codegen scope) | read, not amended | Its text keeps "The N-002/N-003 session-FSM pair … remains deferred to the separate v1.0-tagging gate". DECISION 2 supersedes that reason for the v50sp2 rows this feature closes; the v44 BE/BF exclusion it describes is unchanged here and is the subject of the follow-up issue, which names this clause. No constitution edit in 095. |

**Result:** PASS — no deviation; one pre-existing condition recorded (§VI.2, Gate A round 1
disagreement on Codex #1b).

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
| `tests/session/test_095_tail_rows_roundtrip.cpp` | NEW: 23 witnesses, the walk and `required_members_missing`, the census, the three known-positive tests, the `DISABLED_` selection check (R-3, R-7) |
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
| `spec/feature-catalogue.md`, `spec/coverage-index.md`, `spec/behaviors-and-limitations.md` | rows → `done`; version narrowing (four rows); existing coverage rows' gap notes; B&L delta |

**Parent (`phase-9-harness/`, pushed to parent `main` first):**

| File | Change |
|---|---|
| `tools/run_interop_cell.py` | register the 2 cells with the L0 fields (`combo_id` C1/C3, `run_kind="app-message-evidence"`, arm); `FIXPP_FIX50SP2_DICT_XML` at `_run_initiator_cell`; per-cell expected dictionary in `_finalize`'s FR-011 check |
| `tools/test_run_interop_cell.py` | tests for the per-cell dictionary and the registration fields |
| `tools/interop_capability_minimums.yaml` | +2 entries |
| `quickfixj/.../InteropCounterparty.java` | move the conversation-mode seqnum bump + Heartbeat inside the A-TESTREQ check |
| `golden/<2 cells>.fix` | captured with `--update-goldens`, then reviewed |
| `tools/emit_matrix.py`, `tools/test_emit_matrix.py` | the L0 `run_kind` branches: probe rule narrowed to `"validator-positive-control"`, promotion skipped, N2 unchanged, the before-gitlink raise — each tested |

**Structure decision:** tests only, in existing ctest targets (no new entry). Parent harness changes stay
limited to the files above.

## Phases

0. **Before the first local build:** `AskUserQuestion` for the build (`[const §XVII.7]` resource
   gate). Orchestrator, at any point before P7: file the follow-up issue for lifting the v44 BE/BF
   exclusion — cite `kN002N003Excluded` / `is_n002_n003_excluded` in
   `tools/codegen/fixpp-codegen/emit_builders.cpp`, `kN002N003PresentInFix44` in
   `tests/session/test_067_completeness.cpp`, `[const §XVIII.7]`'s deferral sentence, and DECISION 2;
   labels chosen from `gh label list`; no closing keyword in the body; place it in the parent's
   `phases/phase-4/issue-batches.md`. Its number goes in N-002's Verified cell.
1. **P0 — walk, helper, census and known positives (US1 foundation).**
   - Build the runtime walk (`WalkResult` per MsgType) and `required_members_missing`.
   - Add the golden field-leg test, the deficient 35=E frames and the 35=i fail-loud. Prove the
     deficient-frame test RED under Mutant C.
   - Add the labels and `expected-ctest-095.txt`.
2. **P1 — 23 witnesses and the census (US1).** One test per MsgType (data-model §A), each with its
   intent list and bidirectional readback. Run the RED arms A, B, D, E, F and the changed literal
   (contracts W6).
3. **P2 — N-002/N-003 (US2).** Add the two pins and the routing cell. Run the routing cell's two
   RED arms (scratch classifier mutants, `"BE"` then `"BW"`).
4. **P3 — parent harness (US3, parent side).**
   - Apply R-9's items 1–4 and 6 (contracts L0), with their tests.
   - Build QFJ.
   - Extract the QF-cpp counterparty from the image.
5. **P4 — fixpp live cell (US3).**
   - Run the FR-014 selection check (quickstart §3); it must pass before the cell is built on BN.
   - Add the shared fixture, the cell source, CMake and script.
   - Run locally against both peers. Capture the goldens (R-9 item 5) and review them.
   - Run the RED arms: change one chosen value after the expected record is written, and force a
     peer reject by sending an unknown required-field violation, e.g. omit ExecID(17) with the
     peer's dictionary on.
   - Re-run 089's CONV and PROBE cells against the patched QFJ to show item 4 changes nothing for
     them.
6. **P5 — evidence files and pins (US3).**
   - Edit `cell_results.yaml` and the skip list.
   - Edit the `EXPECTED_IDS` pin, RED first.
7. **P6 — parent push and image.**
   - Commit the parent harness change.
   - Push parent `main` (owner authorization 2026-10-24). This carries the unpushed 094 close-out
     commits.
   - Wait for `publish-counterparties` to finish. Record the image digest.
8. **P7 — records (US4).**
   - Catalogue flips with witness names.
   - Version narrowing: A-018, A-029, A-030, N-002 (data-model §E).
   - coverage-index: each existing tail row's gap note names its witness.
   - B&L delta: the live cell's validation-off disclosure under L-041-2, a B-row naming what the
     live cell does and does not witness, and a line that FIX 4.4 BE/BF have no typed builder (the
     v44 exclusion, with the follow-up issue).
9. **P8 — simplify, verify, Gate B, PR.**
   - `/simplify` on the implementation diff (`[const §XVI.7]`).
   - `/speckit-verify`.
   - Push the fixpp branch, open the PR, run `/gate-b`.
   - `interop-live` must be green on the head (SC-003).

## Risks

- **The live cell fails for a harness reason, not a message reason.** Two such reasons are already
  known and fixed in P3. Others may surface on the first local run (e.g. the hello record's
  dictionary digest under FIXT). Mitigation: run P4 locally before any parent push.
- **The image republish breaks other PRs' `interop-live`.** This is advisory, disclosed, and a
  bounded window (R-10). fixpp#613's PR may see it.
- **The walk fails loudly on a tail MsgType** (a nested required group, a group among siblings, a
  tag at two levels). The implementer stops and reports; extending the walk is a design change that
  returns to the orchestrator.
- **The FR-014 selection check disagrees on BN** (for instance, a QuickFIX/J file requires more).
  The live cell is not built on BN; the orchestrator re-selects with the check's output.
- **The rule-derived check finds a member no witness can set.** For example, a required field whose
  typed `Args` member is absent. If that happens, the implementer stops and reports. It would be a
  codegen defect (FR-020) and goes to the owner.
- **Parent push carries unrelated parent commits.** The owner accepted this on 2026-10-24. The push
  also triggers the parent's own workflows. Check `ls-tree HEAD <submodule>` before pushing, so the
  pushed gitlink is the intended one.
- **A peer (another Claude session) pushes the parent meanwhile.** Before pushing, check
  `git status` for staged files that are not ours (shared staging), and run `git fetch`.

## Complexity Tracking

None.
