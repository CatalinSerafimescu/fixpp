# Implementation Plan: Typed app-message tail — evidence pass

**Branch**: `095-tail-evidence-pass` | **Date**: 2026-10-24 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/095-tail-evidence-pass/spec.md`

## Gate A

*(No round yet.)*

## Summary

This feature closes 12 OFFICIAL catalogue rows (23 FIX 5.0SP2 MsgTypes) on the owner's amended bar.
It adds **tests and records only** and changes no production code (FR-020):

1. **US1, offline witnesses.** A new TU joins the existing `test_077_allversions_builder_roundtrip`
   executable. It has one test per MsgType, named `TailRows095.<Row>_<Msg>`. Each test:
   - sets every required field and every required group (≥2 instances, required members in each)
     with literals;
   - builds with `fixpp::v50sp2::build_<Msg>`;
   - parses independently against `FIX50SP2.xml`;
   - reads every value back;
   - runs a required-presence check against the parsed frame. That check is derived at runtime
     from a pugixml walk of the dictionary under the rule in research R-1, so it is not taken from
     the witness's own list.
2. **US2, N-002 and N-003.** Two named registry pins go in the existing v50sp2 completeness
   executable, and one routing cell goes in `session_pure_tests`. The cell shows that an inbound
   BE and BW reach `from_app`.
3. **US3, the live FIXT cell.** A probe-pattern cell runs on 033's FIXT session, with fixpp as
   initiator, against QuickFIX-cpp and QuickFIX/J (two cells). fixpp sends 35=BN, built by
   `build_ExecutionAck`. The peer's generic per-field readback is compared by 089's
   `compare_streams`.
   - Parent harness: register the cells and fix two blockers (research R-9).
   - The parent harness change is pushed first, then the image is republished, then the fixpp PR
     is pushed (R-10).
4. **US4, records.** The 12 rows move to `done`. A-018, A-029 and A-030 narrow to 5.0SP2. The
   coverage-index lines and the B&L delta are added.

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
- gtest, grouped binaries (`[const §VII.8]`): `test_077_allversions_builder_roundtrip`,
  `test_077_v50sp2_builder_completeness`, `session_pure_tests`, and the 033 FIXT interop target;
- pytest for the parent harness and `cell_results_schema_check_test.py`.

**Target Platform**:
- Linux (gcc and clang presets) for the offline witnesses, which run on every tier like 077's
  today;
- the live cell runs in `interop-live` (Linux, advisory) and locally.

**Project Type**: library — evidence and test feature

**Performance Goals**: N/A. No library code changes, so `[const §VIII]` does not apply.

**Constraints**:
- No production source change (FR-020).
- The witness TU must not include a second `all.hpp`; it uses the per-message headers (R-2).
- Builds use `-j2`, one owner per build directory.
- A parallel lane (fixpp#613, `~/Work/Programming/fixpp-parallel`) builds on F:. Check the
  scratchpad marker `lane613.build.active` before a heavy build here.
- The main checkout's `build/linux-clang-debug` is on E:. Read `df -h /mnt/e` first.

**Scale/Scope**:
- 23 witnesses;
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
| §VI.4 / §VI.3 (coverage-index) | yes | One line per tail MsgType in `spec/coverage-index.md`, naming the witness (FR-019). |
| §VI (traceability) | yes | Each row's Tests cell names test IDs that exist; verify greps every cited ID against `ctest -N` / `--gtest_list_tests`. |
| §VII.8 (grouped tests; `ctest -L`) | yes | No new ctest entry. Every new test joins an existing grouped executable (R-2, R-5, R-6). A verify-time single-entry run uses `-R '^<NAME>$'`, by the 094 precedent. |
| §IX.1 (coverage) | n/a | No library line added. The verify record states it per module. |
| §IX.4 (static analysis) | yes, on test code | clang-format on the new TUs. clang-tidy follows the repo config for `tests/`. |
| §VIII (bench) | n/a | No library code change. |
| §X.7 / C-ABI | n/a | No C-ABI change. |
| §XII.7 (fail closed) | n/a | No production path changes. |
| §XVI.6 (orchestrator does not implement) | yes | Every test TU, CMake edit, harness and counterparty change goes to `phase-implementer`. The orchestrator may write the spec bundle, `spec/` catalogue / coverage-index / B&L text and parent docs. |
| §XVII.1 (Gate A trigger) | yes | The session and interop test surfaces are touched, and a live-interop vehicle changes in the parent. Gate A runs (`/gate-a 095-tail-evidence-pass`). |
| §XVII.7 (local pre-PR build gate) | yes | One FULL `ctest --preset linux-clang-debug` on a committed tree runs as the LAST step of verify, after `df -h /mnt/e` and a check of `/mnt/c/temp/fixpp/.sandbox-lock`. Packaging entries compile consumer projects. |
| §XVII.8 (verify before Gate B) | yes | `/speckit-verify` runs in the main checkout, which owns this branch. |
| §VII.6 (interop against an independent FIX implementation) | yes — already met by 089/033; this adds to it | Two live cells against both QuickFIX peers, run locally and in `interop-live` on the PR head (SC-003). |

**Result:** PASS — no deviation.

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
└── tasks.md           # /speckit-tasks
```

### Files changed

**fixpp (this branch):**

| File | Change |
|---|---|
| `tests/session/test_095_tail_rows_roundtrip.cpp` | NEW: 23 witnesses, the runtime dictionary census, the known-positive test |
| `tests/session/CMakeLists.txt` | add the TU to `test_077_allversions_builder_roundtrip`; link pugixml if the target does not already |
| `tests/codegen/test_077_v50sp2_builder_completeness.cpp` | +2 named pins (N-002, N-003) |
| `tests/session/<routing cell source in session_pure_tests>` | +1 cell, `TailRouting095.InboundBeAndBwReachFromApp` (implementer picks the existing fixture TU and names it in the task report) |
| `tests/interop/happy/hp_fixt50sp2_app_message_test.cpp` | NEW: the live cell (2 parameters) |
| `tests/interop/CMakeLists.txt` | the new source joins the 033 FIXT target; link `fixpp::builders::v50sp2` + codegen include |
| `tests/interop/conversation/fixt_app_script.yaml` | NEW: one-step script, F-01 / 35=BN |
| `tests/interop/cell_results.yaml`, `cell_results_schema_check_test.py`, `expected-skips-without-counterparty.txt` | +2 rows / ids / lines |
| `spec/feature-catalogue.md`, `spec/coverage-index.md`, `spec/behaviors-and-limitations.md` | rows → `done`; version narrowing; coverage lines; B&L delta |

**Parent (`phase-9-harness/`, pushed to parent `main` first):**

| File | Change |
|---|---|
| `tools/run_interop_cell.py` | register the 2 cells; per-cell expected dictionary in `_finalize`'s FR-011 check |
| `tools/test_run_interop_cell.py` (or the harness's existing test module) | test for the per-cell dictionary |
| `interop_capability_minimums.yaml` | +2 entries |
| `quickfixj/.../InteropCounterparty.java` | move the conversation-mode seqnum bump + Heartbeat inside the A-TESTREQ check |
| `golden/<2 cells>.fix` | captured with `--update-goldens`, then reviewed |
| `tools/emit_matrix.py` (+ its test) | distinct `run_kind` for the probe cells; promotion skips it |

**Structure decision:** tests only, in existing grouped executables. Parent harness changes stay
limited to the files above.

## Phases

1. **P0 — census and known positive (US1 foundation).**
   - Build the runtime walk and `expect_required_members_present`.
   - Add the known-positive test (35=E). Prove it RED under Mutant C.
2. **P1 — 23 witnesses (US1).** One test per MsgType (data-model §A). Run the RED arms A and B.
3. **P2 — N-002/N-003 (US2).** Add the two pins and the routing cell. Run the routing cell's
   RED arm (scratch classifier mutant).
4. **P3 — parent harness (US3, parent side).**
   - Apply R-9's items 1–4 and 6, with their tests.
   - Build QFJ.
   - Extract the QF-cpp counterparty from the image.
5. **P4 — fixpp live cell (US3).**
   - Add the cell source, CMake and script.
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
   - Version narrowing.
   - coverage-index.
   - B&L delta: the live cell's validation-off disclosure under L-041-2, and a B-row naming what
     the live cell does and does not witness.
9. **P8 — verify, Gate B, PR.**
   - `/speckit-verify`.
   - Push the fixpp branch, open the PR, run `/gate-b`.
   - `interop-live` must be green on the head (SC-003).

## Risks

- **The live cell fails for a harness reason, not a message reason.** Two such reasons are already
  known and fixed in P3. Others may surface on the first local run (e.g. the hello record's
  dictionary digest under FIXT). Mitigation: run P4 locally before any parent push.
- **The image republish breaks other PRs' `interop-live`.** This is advisory, disclosed, and a
  bounded window (R-10). fixpp#613's PR may see it.
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
