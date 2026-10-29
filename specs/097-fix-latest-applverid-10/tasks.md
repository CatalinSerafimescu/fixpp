---
description: "Task list for 097-fix-latest-applverid-10"
---

# Tasks: FIX Latest is ApplVerID 10 — its own application version, both directions

**Input**: `specs/097-fix-latest-applverid-10/` — spec.md, plan.md, research.md, data-model.md,
quickstart.md, contracts/witnesses.md. Gate A converged at library commit `93a140ca` (plan.md `## Gate A`).
**Tests**: requested. TDD throughout (`[const §VII.3]`, `[const §VII.4]`): every witness W-1..W-9 is shown
RED for its stated reason, on the unfixed tree or against its named mutant (contracts/witnesses.md),
before it is trusted GREEN. W-10 is the full-suite run; W-11 is the records witness (quickstart §4).

## Execution rules (apply to every task)

- **Executor (`[const §XVI.6]`).** Every task names one:
  - `(implementer)` is the `phase-implementer` agent. It makes EVERY change to `src/**`, `include/**`,
    `tools/codegen/**`, `tests/**` (sources, fixtures, CMake), the regenerated golden
    `specs/076-fix-latest-typed-codegen/contracts/golden/vlatest_Messages.golden.hpp` (running the
    regeneration is implementing), and every scratch mutant. The orchestrator never writes those files by
    any tool, python or sed included.
  - `(orchestrator)` writes documentation and governance text only: `.specify/constitution.md` (data-model
    §E verbatim), `.specify/2c-codegen.md`, `spec/behaviors-and-limitations{,-closed}.md`,
    `spec/feature-catalogue.md`, `spec/coverage-index.md`, `brain/components/dictionary.md`, the library
    `CLAUDE.md`, decision records and parent docs. It runs builds, tests, read-only instruments and the
    quickstart blocks, and adjudicates stop rules.
- **The tree.** The main checkout holds branch `097-fix-latest-applverid-10`. Never `git checkout` or
  `git switch` there. Check the branch with
  `git -C research/G19-fix-fpml-iso20022/library rev-parse --abbrev-ref HEAD` before each commit batch.
  Commits are serial. Never `git stash` (every worktree shares it). An implementer makes NEW commits only
  (no `--amend`).
- **Where mutants run (contracts preamble, `[const §XVI.6]`).** Only in the scratch worktree
  `~/Work/Programming/fixpp-097-mutants`, created detached at the implementation head `H` by the
  contracts preamble, with its build trees on `/mnt/wsl/fixppbuild`
  (`097-mutants-linux-clang-debug` = ON tree, `097-mutants-off` = OFF tree). Never in the main checkout,
  never in `~/Work/Programming/fixpp-parallel`, never on the feature branch.
- **Mutant protocol (contracts preamble, quickstart §1.2).** For each row: commit the mutant on the scratch
  worktree's detached head; rebuild the named binaries in that worktree's tree (ON:
  `cmake --build --preset linux-clang-debug --target <t> -j8`; M-9c/M-9d: `cmake --build build/097-mutants-off --target <t> -j8`);
  gate the row on build rc 0 (M-9c alone inverts this gate); for a `tools/codegen/` mutant, also gate on a
  reconfigure rc 0 and on the mutant-took check the row names, because generation runs at configure time
  (`cmake/Codegen.cmake` re-runs codegen when the tool binary is newer than its markers); run each binary
  UNFILTERED (`--gtest_filter` only narrows the report); record witness | mutant | build rc | test rc | the
  failing assertion's message; `git reset --hard "$H"` in the scratch worktree. A row whose rebuild or
  reconfigure failed proves nothing.
- **Evidence file.** Until `/speckit-verify` runs, every "record" goes to
  `.specify/decisions/097-fix-latest-applverid-10-evidence.md` (the parent's tracked
  `decisions/speckit/`, through the symlink). The orchestrator writes it; an implementer task's report
  carries the entry. Each entry: task ID, command, output or the message that fired, the library
  `git rev-parse HEAD` with `git status --porcelain` empty (or the scratch mutant named).
- **Builds (`[const §XVII.7]`).** Only after T002's owner ruling. `export CCACHE_DIR=/mnt/wsl/fixppbuild/ccache`;
  `-j8`; one build owner per build directory; `df -h /mnt/e` before a build in the main checkout's
  `build/linux-clang-debug`, `df -h /mnt/wsl/fixppbuild` before a scratch or OFF tree. Long builds run
  detached (`setsid nohup <build> > <log> 2>&1; touch <marker>.done`) with ONE background waiter on the
  marker (a PID or `.done` file, never `pgrep -f`), never a foreground poll loop. Before any full `ctest`,
  `cat /mnt/c/temp/fixpp/.sandbox-lock`; a lock held by someone else means ask the owner first.
- **Records rules.** No `file:NNN` line-number citation in any added line; no shipped comment records a
  count, an enumerated value range or a pasted result (CLAUDE.md: a comment records a procedure, not a
  result). `python3 tools/check_line_citations.py --range origin/main..HEAD` after every commit batch.
- **No session or validator source change** (plan Constraints; data-model §B "No change"): nothing is
  committed under `src/session/`, `include/fixpp/wire/validator.hpp`, `src/dictionary/fixt_framing_table.hpp`,
  `include/fix/c_api/`, `bindings/` or `dictionaries/*.xml`. The M-7*/M-8* mutants touch those files in
  the scratch worktree only.

## Format: `[ID] [P?] [Story] Description`

`[P]`: parallel-safe (different files, no dependency on an incomplete task). `[US1]`…`[US4]`: spec.md user
stories. Paths are relative to the library root unless marked PARENT
(`/home/catalin/Work/Programming/Antreprenoriat/`).

## Before Phase 1 (pipeline, not tasks)

`/speckit-analyze` runs on this tasks.md with the explicit plan↔spec drift pass over every Clarifications
answer (Q1 both rulings, Q2, the `/speckit-clarify` answer, the Gate A round-1 answer) that
`[const §XVI.4]` and the plan's Constitution Check require, then the checklist audit. Then
`python3 tools/check_line_citations.py --range origin/main..HEAD` charges this bundle (research R-10).

---

## Phase 1: Setup (amendment first, ctest wiring, expected list)

- [ ] T001 (orchestrator) Pre-flight: branch is `097-fix-latest-applverid-10`, `git merge-base --is-ancestor 93a140ca HEAD` holds, `git status --porcelain` is empty. Create `.specify/decisions/097-fix-latest-applverid-10-evidence.md` with a header naming the feature, the branch and this file's role (Execution rules, Evidence file).
- [ ] T002 (orchestrator) `AskUserQuestion` (`[const §XVII.7]` resource gate): approve (a) incremental `linux-clang-debug` builds of the named binaries in `build/linux-clang-debug`; (b) the scratch worktree's ON and OFF trees on `/mnt/wsl/fixppbuild` for the mutant rows; (c) the final full configure + build (`-j8`) + `ctest` at the preset's default parallelism (T070); (d) the one `FIXPP_CODEGEN_FIX_LATEST=OFF` configure+build with `FIXPP_WERROR=ON` (T071); (e) the `/speckit-verify` preset scope and how a skipped preset is waived. Record the ruling in the evidence file.
- [ ] T003 (orchestrator) **Amendment, the FIRST implementation commit (`[const §XX.1]`).** Edit `.specify/constitution.md` verbatim from data-model §E: §E.1 (Article I §1 FIX Latest bullet, both replacements), §E.2 (Article I §1 post-1.0 milestone line only, not the historical Sync Impact Report entry with the same label), §E.3 (Article XVIII §2 v1.2 annotation), §E.4 (Sync Impact Report v4.2 → v4.3 at the top in the v4.2 report's shape: date, `PENDING`, MINOR with no `CHANGELOG.md` entry, Modified, Read and unchanged (§XVIII.1, §XVIII.5, §X.7 with their arguments), Rationale, Affected catalogue rows, Affected feature specs, Process, and the Gate A / Gate B / owner sign-off slots left `PENDING`). The live `> **Status:**` blockquote is NOT changed here (it changes at the RATIFIED flip, after Gate B and owner sign-off). Every residue locus carries the canonical sentence of data-model §E.0 byte for byte on one physical line: For FIX Latest, only the EP-level field back-port into the legacy dictionaries and Extension Pack negotiation (DefaultApplExtID(1407) / ApplExtID(1156), which fixpp does not read) remain post-1.0, both in the Article XVIII §2 v1.2 milestone.
  Commit alone: `Constitution: amend §I.1, §XVIII.2 — FIX Latest is ApplVerID 10 (v4.3 PENDING) (097)`. Then run quickstart §4.2's constitution rows (Article I §1 `feature 097`, Article XVIII §2 annotation, the two canonical-residue rows, the Sync Impact Report header) with `R=.` and `R="$BASE"`; each must read its quickstart head value and base value; record. A text that cannot be applied verbatim returns to the owner (it is Gate-A-reviewed constitution text).
- [ ] T004 (implementer) ctest label wiring and the session data directory (`[const §VII.8]`; no new executable):
  - append label `097` with `set_property(TEST <entry> APPEND PROPERTY LABELS 097)`, placed after that entry's last LABELS setter, to every entry of the expected list below: in `tests/session/CMakeLists.txt`, `tests/dictionary/CMakeLists.txt` (`dictionary_reify_tests` inside its `if(TARGET fixpp_codegen_generate)` block), `tests/config/CMakeLists.txt`, `tests/codegen/CMakeLists.txt` (`codegen_vlatest_tests` inside its `if(FIXPP_CODEGEN_FIX_LATEST)` block);
  - add `"FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""` to `session_fixt_logon_establishment`'s `target_compile_definitions` in `tests/session/CMakeLists.txt` (it has none today; W-7 loads `dictionaries/FIXT11.xml` and `dictionaries/orchestra/OrchestraFIXLatest.xml`; the codegen targets' line is the precedent).
  Reconfigure `build/linux-clang-debug`; `ctest --preset linux-clang-debug -N -L 097` lists exactly the expected list; record the `ctest --preset linux-clang-debug -N` total at this head as the W-10 baseline total. Commit: `097 P1: ctest label 097 and the session dictionary data dir`.
- [ ] T005 (implementer) Baseline at T004's head (code identical to `origin/main`): build every expected-list binary in `build/linux-clang-debug` and run each whole; report pass counts for the evidence file.

### Expected ctest list (W-10; derived, not counted)

The ctest entries that carry label `097`, one per existing grouped binary hosting a 097 witness
(contracts W-1..W-9): `codegen_determinism_test`, `codegen_vlatest_tests`, `config_044_tests`,
`dictionary_orchestra_tests`, `dictionary_pure_tests` (it builds `version_profile_test.cpp`),
`dictionary_reify_tests`, `session_fixt_logon_establishment`.
- **Derive** at any head with `ctest --preset linux-clang-debug -N -L 097` (ON tree). In the OFF tree
  (T071) the same command lists the same entries minus `codegen_vlatest_tests` (built only under
  `if(FIXPP_CODEGEN_FIX_LATEST)`).
- **No new executable**: `ctest -N`'s total at the final head equals T004's recorded total.
- **Tests this contract names as rewritten** (W-10's only allowed differences from `origin/main`): the
  `"10"` entries of the "bad" lists in `tests/dictionary/version_profile_test.cpp` and
  `tests/dictionary/reify_dispatch_test.cpp`; `OrchestraFR010Guard.BothDictsFailLoud` (becomes
  `OrchestraRegistry097.BothDictsCoexist`); `OrchestraVersionIdentity.DistinctVLatestMapsToV50SP2`;
  `tests/codegen/vlatest_dispatch_exclusion_test.cpp` (header and halves); the `which()` assertion of
  `tests/codegen/vlatest_compile_smoke_test.cpp`; `DeterminismTest.VlatestGeneratedMatchesGolden` (golden
  regenerated).

**Checkpoint**: amendment PENDING is the first commit; `ctest -N -L 097` matches the list.

---

## Phase 2: Foundational — unmodified-code witnesses (plan phase 2, quickstart §1.1)

**⚠️ Blocks every later phase.** Before any production edit, write the witnesses that compile against
unmodified code, exactly plan phase 2's list. Build and run them in `build/linux-clang-debug`; that run is
the RED record and the characterization record.

- [ ] T006 [P] (implementer) W-1's string half in `tests/dictionary/version_profile_test.cpp`: `resolve("10")` has a value; drop `"10"` from that file's existing "bad" list. Names no new enumerator.
- [ ] T007 [P] (implementer) W-3's text arm in `tests/config/` (beside `test_load_happy_path.cpp`'s ApplVerID cases, per witnesses.md W-3): an unknown token `"11"` yields `reason_class::unknown_enum` and the message contains `"2".."10"`.
- [ ] T008 [P] (implementer) W-6 (a)/(b)/(c) in `tests/session/test_fixt_logon_establishment.cpp`, cloned from the existing W5 v50sp2 acceptor case, with an Orchestra-dictionary helper beside `make_dict` (per witnesses.md W-6); `validate_inbound_messages` at its default (off). (a) registry {FIX Latest}, inbound `1137=10`: the assertion "reply is a Logon, not a Reject" (the negotiated-version assertion waits for T017); (b) registry {FIX50SP2}, `1137=10`: Reject 373=5, 371=1137, then disconnect; (c) registry {FIX Latest}, `1137=9`: established, negotiated `v50sp2`.
- [ ] T009 (implementer) W-7 (a)/(b)/(c)/(d)/(e)/(f) and every control arm (a0)..(f0) in `tests/session/test_fixt_logon_establishment.cpp`, `validate_inbound_messages` ON, each arm naming its session dictionary (`SessionConfig::dictionary`) and its registry separately exactly as witnesses.md W-7 states; never `kMinimalFix44Xml`. (e)/(f) use the shape of `fix44_dictionary_with_1137()` from `tests/session/engine_reset_unit_stop_test.cpp`, copied or moved into a shared test-support header per witnesses.md W-7(e). (b)'s negotiated-version assertion waits for T017. (e)'s and (f)'s 371/373 values are read from this phase's run and asserted as recorded.
- [ ] T010 (implementer) W-8's phase-2 arms in `tests/session/test_fixt_logon_establishment.cpp`: the counter helper (`"\x01" "1407="`, `"\x01" "1156="`, `"\x01" "1128="` in a captured outbound frame) and the MsgType helper; the instrument's three seeded positives (frames carrying `1407=303`, `1156=303`, `1128=9`, each asserted to count 1); frame source (ii) (initiator `"9"`, registry {FIX50SP2}: Logon at `open()`, peer Logon reply fed, Heartbeat on an inbound TestRequest, TestRequest from the liveness loop per `tests/session/heartbeat_testrequest_test.cpp`'s pattern, Logout at `close()`), and frame source (iii)'s W-6(b) Reject and W-6(c) Logon reply. Non-vacuity first: every listed frame captured before any count is read; then all counts 0; (ii)'s Logon carries `1137=9`.
- [ ] T011 (implementer) Build `dictionary_pure_tests`, `config_044_tests`, `session_fixt_logon_establishment` and `dictionary_orchestra_tests` in `build/linux-clang-debug`; run each UNFILTERED; report every arm's outcome against quickstart §1.1: RED for the stated reason (W-1's `"10"` arm, W-3's text arm, W-6(a), W-7(b)); today's W-4 death test `OrchestraFR010Guard.BothDictsFailLoud` passes = the construction aborts with `FR-010`; characterization pins that pass (W-5's FIX-Latest-alone arm in `OrchestraFR010Guard.SingleDictConfigsSucceed`, W-6(b)/(c), W-7(a)/(c)/(d)/(e)/(f), W-8's phase-2 arms with every family present and all counts 0); every control arm's `origin/main` outcome ((a0), (d0), (e0) established; (f0) delivered to `fromApp`; (b0) and (c0) the 1137 Reject 373=5, 371=1137, the same as (b)/(c)). Do not adjust any arm to make it pass; report.
- [ ] T012 (orchestrator) Adjudicate T011 under plan phase 2's **stop rule, in two halves**, quoted verbatim:
  - "If a **control arm** (the frame without 1407/1156) departs from its expected outcome on this tree (contracts W-7; on `origin/main`, (b0) and (c0) expect the 1137 Reject, (f0) delivery, and the others establishment), the fixture is wrong: the dictionary, the frame or the session setup. Fix the fixture, which is test setup and not FR-008, and re-run. Proceed only once every control matches."
  - "If a 1407/1156 arm's outcome differs from its matching control's (for example rejected where the control is established), under a session dictionary that declares the tag (`FIXT11.xml` or the Orchestra file), RC-1's premise is false: FR-008 would need code, and "no `src/session/` change, no validator change" is wrong. Stop and decide, and do not adjust the test."
  - "W-7(e) and W-7(f) (the dictionary-decided rejects of 1407 and of header 1156) are characterization pins. If an observed Reject shape departs from contracts' text while its control passes, record the observed shape and correct the text. No production code changes."
  A fixture fix goes back to the implementer (T008–T010) and T011 re-runs. The second half stops the feature: escalate to the owner with T011's record; nobody adjusts the test. A shape correction under the third half is an orchestrator edit to `contracts/witnesses.md` W-7(e)/(f), recorded. Record the adjudication.
- [ ] T013 (implementer) Commit T006–T010 (after T012 clears): `097 P2: unmodified-code witnesses (RED and characterization record)`. Run the line-citation check; report the hash.

**Checkpoint**: every control arm matches its expected outcome; the RED and characterization record is in the evidence file. No production file has changed.

---

## Phase 3: User Story 1 — A FIX Latest peer can log on (Priority: P1) 🎯 MVP

**Goal**: `"10"` parses to and renders from `application_version::vlatest`, the config token `"10"` loads, and
the registry answers FIX Latest from its own slot with the one-way fallback, so a FIXT acceptor holding
FIX Latest establishes a `1137=10` peer and no session-originated frame carries 1407, 1156 or header 1128.
**Independent test**: `ctest --preset linux-clang-debug -L 097 --output-on-failure` GREEN at US1's head;
W-6(a) shows a Logon reply, Active, negotiated `vlatest` (SC-001).
**Why the registry change lands here (not US2)**: US1's acceptor and `open()` arms (W-6(a)/(f), W-7(b),
W-8(i)) need `get(vlatest)` to answer; the slot cannot land without the one-way fallback (or the shipped
FIX-Latest-alone `get(v50sp2)` answer regresses: W-5's pin, W-6(c)); and the slot change breaks the two
074 tests W-4 rewrites. So W-4 and W-5 are written here, before the registry change, and US2 holds
W-6(d) and the registry mutant rows.

### Tests for User Story 1 (write first; they name `vlatest` and compile only after T018)

- [ ] T014 [P] [US1] (implementer) W-1 full table and W-2 in `tests/dictionary/version_profile_test.cpp`: every ApplVerID value `"0"`..`"10"` (`"2"`..`"9"` as before, `"10"` → `vlatest`, `"0"`/`"1"` stay `dict_unknown_appl_ver_id`); the malformed values `"10 "`, `"010"`, `"1O"`, `"11"`, `"100"`, `"99"` stay `dict_unknown_appl_ver_id`; `render(vlatest) == "10"`; over the nine renderable members `v40`..`vlatest` render-then-parse is the identity and the nine strings are pairwise distinct; separately `render(Unknown)` is `dict_unknown_appl_ver_id`. In `tests/dictionary/reify_dispatch_test.cpp`, drop `"10"` from the existing "bad" list.
- [ ] T015 [P] [US1] (implementer) W-3 accepted-token arm in `tests/config/` with its new fixture session `default_appl_ver_id = "10"` (fixture file per witnesses.md W-3): `"10"` loads as `vlatest`.
- [ ] T016 [P] [US1] (implementer) W-4 and W-5 in `tests/dictionary/orchestra_loader_test.cpp`: `OrchestraFR010Guard.BothDictsFailLoud` becomes `OrchestraRegistry097.BothDictsCoexist` (death assertion deleted together with the abort): building from {FIX50SP2, FIX Latest} in both orders returns, `get(v50sp2)` is the FIX50SP2 dict and `get(vlatest)` the FIX Latest dict; `OrchestraVersionIdentity.DistinctVLatestMapsToV50SP2` rewritten to assert the mapping to `vlatest`; `OrchestraFR010Guard.SingleDictConfigsSucceed` kept unchanged; new `OrchestraRegistry097.OneWayFallback` asserting every row of data-model §A's lookup table (T020 quotes it). (FR-005/FR-006 are US2's requirements; landed here per the phase note.)
- [ ] T017 [P] [US1] (implementer) In `tests/session/test_fixt_logon_establishment.cpp`: W-6(a)'s negotiated-profile assertion (`vlatest`); W-6(e) initiator, Logon-ack `1137=10` → negotiated `vlatest` (no registry lookup); W-6(f) `open()` with default `"10"` and {FIX Latest} succeeds, default `"9"` and {FIX Latest} still succeeds (fallback), default `"10"` and {FIX50SP2} returns `invalid_session_config`; W-7(b)'s negotiated-`vlatest` assertion (with (b0) now expected established and negotiated `vlatest` at the implementation head); W-8 frame source (i) (initiator `"10"`, registry {FIX Latest}, the same frame families as (ii)) and (iii)'s W-6(a) Logon reply, non-vacuity first, all counts 0, (i)'s Logon carries `1137=10`.

### Implementation for User Story 1

- [ ] T018 [US1] (implementer) `include/fixpp/dict/version_profile.hpp`: enumerator `vlatest = 9` appended after `v50sp2` (data-model §A: "**`vlatest` (new, value 9)**"; research R-1: existing values unchanged, `version_profile`'s size unchanged); `render_appl_ver_id` case `vlatest` → `"10"`. `src/dictionary/version_profile.cpp`: `resolve_application_version` gains a two-character arm accepting exactly `"10"` (research R-2); every other value keeps its result. Comment rewrites wait for T063.
- [ ] T019 [P] [US1] (implementer) `src/config/scalar_mappers.cpp`: the `default_appl_ver_id` chain accepts token `"10"` → `vlatest`; the unknown-token diagnostic names `"2".."10"` (research R-7).
- [ ] T020 [US1] (implementer) `include/fixpp/dict/version_registry.hpp` and `src/dictionary/version_registry.cpp`, one change: one slot per application version, `kTableSize` derived from the enum with a `static_assert` (plan phase 6); `session_to_application(vlatest)` → `vlatest`; the FR-010 collision check and its `std::abort` deleted, not left as dead code (research R-3); `get` gains the one-way fallback, "exact slot first; an empty v50sp2 slot answers from the vlatest slot; no other fallback" (data-model §A), whose table is:
  - {FIX50SP2}: `get(v50sp2)` = FIX50SP2, `get(vlatest)` = none;
  - {FIX Latest}: `get(v50sp2)` = FIX Latest (fallback, as today), `get(vlatest)` = FIX Latest;
  - {FIX50SP2, FIX Latest}: `get(v50sp2)` = FIX50SP2, `get(vlatest)` = FIX Latest;
  - {}: none, none.
  Two dictionaries with the same session version keep last-writer-wins (unchanged).
- [ ] T021 [US1] (implementer) Build the expected-list binaries (dictionary, config, session) in `build/linux-clang-debug`; run each UNFILTERED; `ctest --preset linux-clang-debug -L 097 --output-on-failure` all pass (the codegen entries unchanged so far). Quickstart §2: the dictionary binary's output holds no `FR-010` text (`grep -c FR-010` = 0) and `grep -rn "std::abort\|FR-010" src/dictionary/version_registry.cpp` is empty. Commit T014–T020: `097 US1: FIX Latest = ApplVerID 10 (parse, render, config, own registry slot, one-way fallback)`. Report the hash (`H` for T022).

### RED arms for User Story 1 (Mutant protocol; scratch worktree only; record each)

- [ ] T022 [US1] (implementer) Create the scratch worktree exactly per the contracts preamble with `H` = T021's hash (`df -h /mnt/wsl/fixppbuild`; `git worktree add --detach ~/Work/Programming/fixpp-097-mutants "$H"`; both `/mnt/wsl/fixppbuild/build/097-mutants-*` trees and their symlinks; `conan install`; ON configure; OFF configure with `-DFIXPP_CODEGEN_FIX_LATEST=OFF -DFIXPP_WERROR=ON`). Build the US1 binaries in the ON tree unmutated and run them: GREEN, the baseline each row is read against.
- [ ] T023 [US1] (implementer) M-1: the `"10"` arm widened to any two-character string starting `'1'` → W-1's `"11"` row RED (`dictionary_pure_tests`).
- [ ] T024 [US1] (implementer) M-1n: `"10"` mapped to `v50sp2` → W-1's `"10"` → `vlatest` row RED; W-6(a) and W-6(e) RED on their negotiated-version assertions (`session_fixt_logon_establishment`); M-1n is W-6(e)'s only RED. (W-6(d)'s rows re-run in T034.)
- [ ] T025 [US1] (implementer) M-2: `render(vlatest)` returns `"9"` → W-2's round-trip and distinctness checks RED; W-8's `1137=10` assertion on (i)'s Logon RED.
- [ ] T026 [US1] (implementer) M-3: the `"10"` branch deleted in `src/config/scalar_mappers.cpp` → W-3's accepted-token arm RED (the load yields a diagnostic).
- [ ] T027 [US1] (implementer) M-7a, in `Session::on_inbound_not_connected_`'s DefaultApplVerID block (`src/session/session.cpp`), reading the raw inbound frame for `"\x01" "1407="`: refuse (373=5) → W-7(a) and (b) RED while (a0) and (b0) stay green.
- [ ] T028 [US1] (implementer) M-7b: a header carrying 1156 is session-rejected → W-7(d) RED while (d0) stays green.
- [ ] T029 [US1] (implementer) M-7c, same site as M-7a: when the raw inbound Logon contains `"\x01" "1407="`, `serviceable` is `resolved.has_value()` alone (registry lookup skipped) → W-7(c) established (RED) while (c0) still gets Reject 373=5, 371=1137.
- [ ] T030 [US1] (implementer) M-7d: the `wire_unexpected_tag` test of the field walk in `include/fixpp/wire/validator.hpp` accepts 1407 for every dictionary → W-7(e) RED (established) while (e0) stays green.
- [ ] T031 [US1] (implementer) M-7e: the same check accepts 1156 for every dictionary (the one tag exempted at the check, not the framing table installed for every version) → W-7(f) RED (delivered) while (f0) stays green.
- [ ] T032 [US1] (implementer) M-8a, M-8b, M-8c in `src/session/admin_messages.cpp`, one row each (each its own rebuild): M-8a `build_logout` appends `1156=303` after TargetCompID → the Logout's 1156 count RED; M-8b `build_logon` appends `1407=303` → (i)'s and (ii)'s Logon 1407 counts RED; M-8c `build_heartbeat` appends `1128=9` → the Heartbeat's 1128 count RED. The worktree is kept for T034.

**Checkpoint**: US1 GREEN at `H`; every US1 row recorded with build rc 0 and the expected failing message.

---

## Phase 4: User Story 2 — One engine serves FIX50SP2 and FIX Latest (Priority: P1)

**Goal**: one engine (direct C++, `EngineConfig::dictionaries`; L-097-1) holds both dictionaries, each version
answered by its own; two peers `1137=9` and `1137=10` both establish.
**Independent test**: `dictionary_orchestra_tests` (W-4, W-5) and W-6(d) GREEN; M-4 and M-5a..c RED.
**Production change**: none in this phase; the registry change is T020 (see the Phase 3 note).

### Tests for User Story 2

- [ ] T033 [US2] (implementer) W-6(d) in `tests/session/test_fixt_logon_establishment.cpp`: acceptor, registry {FIX50SP2, FIX Latest}, one session per peer, `1137=9` and `1137=10`, both established, each negotiating its own version; one arm uses a session whose own dictionary is FIX50SP2 receiving `1137=10`: admitted (the engine-wide rule, spec Clarifications `/speckit-clarify`). Build and run `session_fixt_logon_establishment` UNFILTERED: GREEN. Commit: `097 US2: W-6(d) two peers on one engine`. Report the hash (`H` for T034).

### RED arms for User Story 2 (Mutant protocol; record each)

- [ ] T034 [US2] (implementer) `git -C ~/Work/Programming/fixpp-097-mutants reset --hard "$H"` (T033's hash); rebuild the ON tree baseline, GREEN. Re-run M-1n for W-6(d): its `1137=10` sessions RED on their negotiated-version assertion.
- [ ] T035 [US2] (implementer) M-4: restore `case vlatest: return v50sp2;` without the abort → W-4 RED (`get(vlatest)` empty and `get(v50sp2)` the last writer, so one order fails); W-6(d)'s both-registered arm RED.
- [ ] T036 [US2] (implementer) M-5a: delete the fallback → W-5's FIX-Latest-alone `get(v50sp2)` arm RED; W-6(c) and W-6(f)'s second case RED.
- [ ] T037 [US2] (implementer) M-5b: add the reverse fallback (an empty vlatest slot answers from v50sp2) → W-5's FIX50SP2-alone arm RED; W-6(b) and W-6(f)'s third case RED.
- [ ] T038 [US2] (implementer) M-5c: the fallback wins over an exact match → W-5's both-registered arm RED.

**Checkpoint**: US1 and US2 GREEN; every W-4/W-5/W-6 arm has its recorded RED (contracts W-4..W-6).

---

## Phase 5: User Story 3 — A FIX Latest message is typed as FIX Latest (Priority: P2)

**Goal**: the generated application dispatch gains a `vlatest` arm when the FIX Latest tier is ON (none
under OFF), and `fixpp::vlatest::owning_<Msg>::which()` reports `vlatest`.
**Independent test**: `codegen_vlatest_tests`, `dictionary_reify_tests` and `codegen_determinism_test`
GREEN (ON); W-9(c) GREEN in the OFF tree (T071).

### Tests for User Story 3 (write first; RED at US2's head for the stated reason)

- [ ] T039 [US3] (implementer) CMake: in `tests/codegen/CMakeLists.txt` add `"FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""` to `codegen_vlatest_tests`'s `target_compile_definitions` (the W-9(a3) census; `codegen_vlatest_census_test` in the same file is the precedent). In `tests/dictionary/CMakeLists.txt`, inside the `if(TARGET fixpp_codegen_generate)` block, `if(NOT FIXPP_CODEGEN_FIX_LATEST)` define `FIXPP_097_FIX_LATEST_TIER_OFF` on `dictionary_reify_tests` (the compile definition witnesses.md W-9(c) asks `/speckit-tasks` to name); and, unconditionally in the same block, `"FIXPP_097_REIFY_DISPATCH_APPLICATION_HPP=\"${_REIFY_CODEGEN_INC}/fixpp/_dispatch/reify_dispatch_application.hpp\""` on `dictionary_reify_tests` (the generated header W-9(c) reads at run time). Confirmed for this choice: `dictionary_reify_tests` is gated on `if(TARGET fixpp_codegen_generate)`, which `cmake/Codegen.cmake` defines unconditionally, and `tests/dictionary/reify_dispatch_test.cpp` includes the `_dispatch/` headers and `fixpp/v44/Reify.hpp`, no `fixpp/vlatest/` header, so the binary builds under OFF.
- [ ] T040 [P] [US3] (implementer) Rewrite `tests/codegen/vlatest_dispatch_exclusion_test.cpp` per witnesses.md W-9: header comment states the rule and the derivation (FR-012(c), names 097, no count); (a1) and (a2) kept; (a3) census through direct `dispatch::dispatch_application(mv, msg_type, application_version::vlatest, profile, &arena)` with an empty `MV` over every `MessageEntry` of `OrchestraLoader`'s `Dictionary::messages()` from `FIXPP_DICT_DATA_DIR "/orchestra/OrchestraFIXLatest.xml"` except the seven `is_fixt_admin` MsgTypes, asserting `has_value()`, `version().k == kind::application` and `version().application == vlatest`, each failure naming the MsgType, with the non-vacuity assertions (all seven admin MsgTypes found and skipped; dispatched count = `messages().size()` minus the seven skipped; any comment states that derivation, never a literal count); (a3) samples (`EC` and `D` → `vlatest`; a MsgType absent from FIX Latest → `dict_reify_unknown_msg_type`); (a4) XMLnonFIX (35=n) → `kind::application`, `application == vlatest`; (b) the generated outer switch has exactly one `case …::v50sp2:` and exactly one `case …::vlatest:`.
- [ ] T041 [P] [US3] (implementer) `tests/dictionary/reify_dispatch_test.cpp`: the (a3) admin-interception case through `dict::reify` on a framed message (`ReifyFixture`): a FIXT admin MsgType under the `vlatest` profile keeps its session-admin label, compiled only under `#ifndef FIXPP_097_FIX_LATEST_TIER_OFF`; the W-9(c) case compiled only under `#ifdef FIXPP_097_FIX_LATEST_TIER_OFF`: under a `vlatest` resolution `dict::reify` returns `dict_reify_unknown_msg_type` for `D` and for `EC`, and the generated header at `FIXPP_097_REIFY_DISPATCH_APPLICATION_HPP` (T039) holds no `case …::vlatest:`.
- [ ] T042 [P] [US3] (implementer) `tests/codegen/vlatest_compile_smoke_test.cpp`: `vlatest::owning_Heartbeat::which() == application_version::vlatest` (FR-009(b)).
- [ ] T043 [US3] (implementer) At US2's head plus T039–T042 (no codegen change yet), reconfigure and build `codegen_vlatest_tests` and `dictionary_reify_tests`; run UNFILTERED; report RED for the stated reason: the (a3) census, the (a3) `EC`/`D` samples and (a4) RED (no `vlatest` arm: the value falls to the outer default), (b)'s `vlatest` count 0, the smoke test's `which()` reads `v50sp2`. Report the (a3) admin-interception case's result as observed (witnesses.md names no mutant for it; recorded as a characterization result). `DeterminismTest.VlatestGeneratedMatchesGolden` is still GREEN here.

### Implementation for User Story 3

- [ ] T044 [US3] (implementer) `tools/codegen/fixpp-codegen/emit_dispatch.cpp`: `kAppVersions` row `{vlatest, vlatest}`, "appended last (this keeps the bytewise order)" (data-model §B); an outer `case` is emitted only for a row whose IR is present (OFF tier: no `vlatest` case, falls to the outer `default`, research R-5). Comment rewrites in this file wait for T063.
- [ ] T045 [P] [US3] (implementer) `tools/codegen/fixpp-codegen/ir.cpp`: `kCodegenVersions` vlatest `.a = vlatest`. `tools/codegen/fixpp-codegen/gen_util.hpp`: `app_version_enum("vlatest")` → `"vlatest"`, its guard deleted (research R-4).
- [ ] T046 [US3] (implementer) Rebuild `fixpp-codegen`, reconfigure `build/linux-clang-debug` (generation runs at configure time), build. Regenerate `specs/076-fix-latest-typed-codegen/contracts/golden/vlatest_Messages.golden.hpp` with the recipe `DeterminismTest.VlatestGeneratedMatchesGolden` prints in its failure message. Then run quickstart §3 exactly (base extracted with Python and size-checked; `git diff -U0` shows exactly two changed-line shapes, `-…application_version::v50sp2;` and `+…application_version::vlatest;`, each counted `n0` times; `git diff --stat` one file; the seeded positive gains a third shape). `git status` lists only files named in plan "Files changed"; any other changed generated file stops the phase for a decision (plan Risks). `ctest --preset linux-clang-debug -L 097 --output-on-failure` GREEN; quickstart §1.4's ON-tree grep `grep -c "application_version::vlatest:" build/linux-clang-debug/_codegen/include/fixpp/_dispatch/reify_dispatch_application.hpp` prints 1. Commit T039–T046: `097 US3: FIX Latest reify arm, vlatest typed classes report vlatest, golden regenerated`. Report the hash (`H` for T047).

### RED arms for User Story 3 (Mutant protocol; each codegen row also gated on reconfigure rc 0 and its mutant-took check)

- [ ] T047 [US3] (implementer) `git -C ~/Work/Programming/fixpp-097-mutants reset --hard "$H"` (T046's hash); reconfigure and build both scratch trees unmutated: ON tree US3 binaries GREEN; OFF tree builds rc 0 under `FIXPP_WERROR=ON` and W-9(c) in `dictionary_reify_tests` is GREEN there (the baseline for M-9c/M-9d).
- [ ] T048 [US3] (implementer) M-9a: delete the `kAppVersions` vlatest row (ON) → the (a3) census, the (a3) `EC`/`D` samples, (a4) and (b) RED. Mutant-took: the regenerated ON header has no `case …::vlatest:`.
- [ ] T049 [US3] (implementer) M-9b: revert `app_version_enum("vlatest")` to `"v50sp2"` (ON) → the smoke test and the golden RED. Mutant-took: the regenerated `vlatest/Messages.hpp` holds `application_version::v50sp2;`.
- [ ] T050 [US3] (implementer) M-9c: restore emitting an outer `case` for a row whose IR is absent (today's shape, an unread `rmv_app`), OFF tree. **Inverted gate**: RED = OFF build rc ≠ 0 with `-Werror=unused-variable` in the generated header; record the diagnostic.
- [ ] T051 [US3] (implementer) M-9d: the OFF emitter writes a `case …::vlatest:` that dispatches through the v50sp2 MsgType switch, OFF tree → W-9(c) RED (a handle returned where the error was expected). Mutant-took: the regenerated OFF header holds a `case …::vlatest:`.
- [ ] T052 [US3] (implementer) M-9e: when `emit_dispatch_application` writes the `vlatest` arm, and only that arm, skip every message whose `is_application` is false (ON). Mutant-took: the regenerated `_dispatch/reify_dispatch_application.hpp`'s `vlatest` arm holds no `XMLnonFIX` case while its v44 arm still does. → (a4) RED (`dict_reify_unknown_msg_type` where an `application`/`vlatest` handle was expected) and the (a3) census RED naming `n`; the (a3) samples and the (a3) admin-interception case stay green.
- [ ] T053 [US3] (implementer) M-9f: in `emit_dispatch_application`, skip `m.msg_type == "8"` only when `av.ns == "vlatest"` (ON). Mutant-took: the regenerated header's `vlatest` arm holds no `case '8'` while its v50sp2 arm keeps one. → the (a3) census RED naming `8`; the (a3) samples and (a4) stay green.
- [ ] T054 [US3] (implementer) After the last row: `git worktree remove ~/Work/Programming/fixpp-097-mutants` and delete both `/mnt/wsl/fixppbuild/build/097-mutants-*` trees (ON and OFF). Report the mutation table's `H` per story (T021, T033, T046).

**Checkpoint**: all user-story witnesses GREEN at T046's head; the mutation table complete for M-1..M-9f.

---

## Phase 6: User Story 4 — The record says what is true (Priority: P2)

**Goal**: constitution (T003), 2c, B&L, catalogue, coverage index, component page and library `CLAUDE.md`
say FIX Latest is ApplVerID 10; W-11's checks pass with their seeded negatives.
**Independent test**: quickstart §4.1 prints 0 at the head with a non-zero seeded negative on
`origin/main`; every quickstart §4.2 row (the Status-line row excepted) reads its head value at the head
and its base value at base.

### RED before records

- [ ] T055 [US4] (orchestrator) Run quickstart §4.1 at the current head with its seeded positives; it prints a non-zero count. That hit list is the population data-model §C says to re-derive at the implement head: classify each hit as a record (this phase) or a code/test comment (T063), and record both lists. Extract quickstart §4.2's `$BASE` copies (Python, size-asserted) and run every §4.2 row at the head: the record rows read their base values; record.

### Records (orchestrator; all after T003)

- [ ] T056 [P] [US4] (orchestrator) `.specify/2c-codegen.md`: the six in-place `Updated (097-fix-latest-applverid-10): …` notes of data-model §C verbatim (§4.3 `enum class application_version` sketch; §4.3 wire `ApplVerID(1128)` → `application_version` table; §4.8 "Runtime-dispatch switch" paragraph; the paragraph immediately after the §4.8 `reify_dispatch_application.hpp` sketch's closing code fence, not inside the code block; appended to the end of §6.3 step 3; appended to the end of §6.6 item 8), each on a line of its own or at the end of its own paragraph or list item, no two sharing a line, the text before each kept. Nothing data-model §C lists as left alone is touched.
- [ ] T057 [P] [US4] (orchestrator) `spec/behaviors-and-limitations.md`: new section `## 097 — FIX Latest is ApplVerID 10 (2026-10-29)` with B-097-1, B-097-2, B-097-3 (carrying the canonical sentence on one line), B-097-4 and L-097-1 verbatim from data-model §D; the in-place `<!-- SUPERSEDED (097-fix-latest-applverid-10, constitution v4.3): … -->` comment of data-model §F appended to the live 074 section intro line. `spec/behaviors-and-limitations-closed.md`: L-074-1 and L-080-1 moved verbatim, each preceded by `<!-- L-0NN-1 — closed: resolved by 097 (FIX Latest is ApplVerID 10; own registry slot) -->` with `0NN` replaced by the row's own number.
- [ ] T058 [P] [US4] (orchestrator) `spec/feature-catalogue.md` per data-model §F: S-025 version column `5.0SP2, FIXT.1.1` → `5.0SP2, FIX Latest, FIXT.1.1` plus a Verified-cell sentence naming W-4, W-5, W-6; S-043 a sentence naming 097 for the one-way fallback (W-5, W-6); D-011's four note edits (the L-074-1 sentence, the `wire app-version = \`v50sp2\`` phrase, the residue sentence replaced by the canonical sentence verbatim, the `std::abort` backstop sentence), its Tests cell (`FR010Guard×2` → `OrchestraRegistry097.BothDictsCoexist`, `OrchestraFR010Guard.SingleDictConfigsSucceed`, `OrchestraRegistry097.OneWayFallback`), its case count marked "(count at 074)" and not recomputed; in rows A-035..A-065, every occurrence of `FIX Latest goes on the wire as ApplVerID 9 with no ApplExtID(1156) (L-074-1, REMAINING-WORK row 4b)` replaced by `FIX Latest goes on the wire as ApplVerID 10 when the session's DefaultApplVerID is 10; a session configured 9 negotiates FIX50SP2 (097, B-097-1, B-097-4)` (count the old sentence before, the new after; the two counts are equal; no status changes); the SUPERSEDED comment appended to the 076 partial-progress note.
- [ ] T059 [P] [US4] (orchestrator) `spec/coverage-index.md` per data-model §F: the §4.3.7, §5.3.3, §5.3.5 rows and the DefaultApplVerID(1137) / ApplVerID(1128) rows gain "097: ApplVerID 10 = FIX Latest"; the Post-1.0 Gap Registry D-011 row's item, reason (the "(retired by 097: own registry slot, B-097-1)" addition and the canonical sentence verbatim) and target (`v1.2 (EP-level field back-port; Extension Pack negotiation)`) columns; the SUPERSEDED comment appended to the 074 link note, after 096's comment.
- [ ] T060 [P] [US4] (orchestrator) `brain/components/dictionary.md` (FR-012(e)): flag 074's reconcile (`specs/074-orchestra-native-reader/`: the reconcile, FR-010, L-074-1), the `specs/076-fix-latest-typed-codegen/` prose on `.a = v50sp2` / `app_version_enum`, and `specs/080-orchestra-runtime-load/` (L-080-1) as superseded in place, naming `097-fix-latest-applverid-10`; point at the 2c notes of T056.
- [ ] T061 [P] [US4] (orchestrator) Library `CLAUDE.md`: delete the "Also open, no issue filed: **ApplExtID(1156)=303** …" line (plan phase 6; Gate A round 2, G097-018), nothing else.
- [ ] T062 [US4] (orchestrator) Commit T056–T061: `097 US4: records — 2c notes, B&L (B-097-1..4, L-097-1; L-074-1/L-080-1 closed), catalogue, coverage index, component page`. Re-run quickstart §4.2's record rows at the head and base (the Status-line row is deferred to the RATIFIED flip; the `emit_dispatch.cpp` row waits for T063); each reads its head value at the head and its base value at base; a row whose base run gives the head value is rewritten before it is trusted. Re-run §4.1: every remaining unannotated hit is on T055's code/test-comment list. Record.

**Checkpoint**: W-11's record rows pass; the code-comment hits are handed to T063.

---

## Phase 7: Polish, final head, verify and close-out (plan phases 6–7)

- [ ] T063 (implementer) Comment rewrites (plan phase 6; FR-012(c); data-model §B/§C). Each rewritten comment states the current rule and the derivation and names 097; none records a count, an enumerated value range or a closed version list: `include/fixpp/dict/version_profile.hpp` (the wire-table comment and the `"2".."9"` table pointing at the code; the `session_version::vlatest` comment); `include/fixpp/dict/version_registry.hpp` (slot comment); `src/dictionary/version_registry.cpp` (header comment names 097; the mapping and FR-010 comments); `src/config/scalar_mappers.cpp` (its `"2".."9"` table points at the code); `tools/codegen/fixpp-codegen/emit_dispatch.cpp` (the file-header (version, MsgType) case-count comment, the comment above `kAppVersions` and its sort example, `emit_dispatch_application`'s block comment, and the generated header's banner and outer `default:` comment lines, each derived from `kAppVersions` — data-model §B, verbatim: "A rewritten generated comment never contains the literal `application_version::vlatest:` (quickstart §1.4's grep and W-9(b) count that string)"); `tools/codegen/fixpp-codegen/ir.cpp` (the `vlatest` row's own comment only; the pre-097 banner, diagnostic and `ir.hpp` namespace list are left alone, plan Gate A Round 2, G097-016); `tools/codegen/fixpp-codegen/gen_util.hpp` (`app_version_enum`'s comment); `tests/config/fixtures/pos_fixt_appl_ver_ids.toml`'s header and `tests/config/test_load_happy_path.cpp`'s section banner for that fixture (find them with `grep -rn '"2".."9"' tests/config`); and every other code/test-comment hit on T055's list. Commit: `097: comments state the 097 rule (no counts)`.
- [ ] T064 (implementer) Rebuild `fixpp-codegen`, reconfigure and build `build/linux-clang-debug` (the generated dispatch header's comment lines changed); `ctest --preset linux-clang-debug -L 097 --output-on-failure` GREEN; quickstart §1.4's ON grep prints 1 and W-9(b) holds. Report `git diff <T046's hash>..HEAD -- src include tools tests`: if it is not comment-only, the affected mutant rows re-run in a re-created scratch worktree (contracts preamble) before T073.
- [ ] T065 [P] (orchestrator) PARENT `.claude/scripts/check-comment-claims.py --root /home/catalin/Work/Programming/Antreprenoriat/research/G19-fix-fpml-iso20022/library --base origin/main`: clean, or each hit adjudicated; known positive first (a seeded result comment in a throwaway `git worktree add --detach <scratchpad>/<name> HEAD`, removed afterwards, is reported). It is lexical: also read any claim inside a runtime string or an inventory by eye. Record.
- [ ] T066 [P] (orchestrator) `python3 tools/check_line_citations.py --range origin/main..HEAD` → OK and `python3 tools/check_line_citations.py --shift-audit origin/main..HEAD` → OK; known positive first (a commit adding a resolvable `file:NNN` in a throwaway worktree is reported). Record.
- [ ] T067 (orchestrator) W-11 at the head: quickstart §4.1 (last line prints 0; both seeded positives; the seeded negative over `origin/main` prints non-zero; each annotated hit read once and recorded; any current-rule entry recorded with its reason), every quickstart §4.2 row at head and base (the Status-line row scheduled at the RATIFIED flip; the reversed-polarity rows as stated), and quickstart §4.3. Record.
- [ ] T068 (orchestrator) `/simplify` on the branch diff (`[const §XVI.7]`); the implementer applies the in-scope findings; T064–T067 re-run on any source or comment change.
- [ ] T069 (orchestrator) Run every tier-1 source-audit job locally, the list derived from `.github/workflows/tier1.yml` at this head (not a judged subset), plus clang-format/clang-tidy on the changed files with `tools/codegen/` included (`[const §IX.4]`); keep a job → rc table in the evidence file. Fixes go to the implementer, then this task re-runs.
- [ ] T070 (orchestrator) `[const §XVII.7]` at the final code head, after the sandbox-lock check and `df -h /mnt/e`: `conan install` (if the profile's packages are absent) + `cmake --preset linux-clang-debug` + full build (`cmake --build --preset linux-clang-debug -j8`) + full `ctest --preset linux-clang-debug` at the preset's default parallelism (ctest parallelism is not widened, fixpp#229), run detached with one marker waiter. W-10: zero failures outside the rewritten-tests list (Phase 1); `ctest -N` total equals T004's; the characterization pins pass (W-5's FIX-Latest-alone arm, W-6(c), W-6(f)'s second case, W-7(a)/(d)/(e)/(f), W-8's phase-2 arms). Record the SHA and pass counts (the PR body's `local build:` line cites them).
- [ ] T071 (orchestrator) W-9(c), quickstart §1.4: `df -h /mnt/wsl/fixppbuild`; one configure+build with `-DFIXPP_CODEGEN_FIX_LATEST=OFF -DFIXPP_WERROR=ON` in a build tree on `/mnt/wsl/fixppbuild` (same preset and toolchain, its own binary dir); build rc 0; run `dictionary_reify_tests` UNFILTERED (W-9(c) GREEN); `grep -c "application_version::vlatest:" <OFF tree>/_codegen/include/fixpp/_dispatch/reify_dispatch_application.hpp` prints 0, and the same grep on the ON tree prints 1 (seeded positive); `ctest -N -L 097` in the OFF tree lists the expected list minus `codegen_vlatest_tests`. Record (no CI lane builds OFF).
- [ ] T072 (orchestrator) `codegraph sync` in the main checkout (`/home/catalin/Work/Programming/Antreprenoriat/research/G19-fix-fpml-iso20022/library`); `codegraph status --json` shows that `projectPath` and a nonzero `fileCount`. Record.
- [ ] T073 (orchestrator) `/speckit-verify 097-fix-latest-applverid-10` with the preset scope and waivers of T002's ruling: writes `.specify/decisions/097-fix-latest-applverid-10-verify.md`; copy in the evidence-file entries, the mutation table (witness | mutant | build rc | test rc | message, with each story's `H`), T070's and T071's results, and T067's W-11 record; coverage of the touched modules ≥ 95/85 or a per-line assessment (`[const §IX.1]`); the B&L functional delta (B-097-1..4, L-097-1, L-074-1/L-080-1 closed) recorded and PARENT `.claude/scripts/check_bl_delta.py gate --feature 097-fix-latest-applverid-10` run with its rc. Verdict must be non-RED.

### Mandatory close-out tasks (Gate-B preconditions, Article XVII §8)

- [ ] T074 (orchestrator) **Catalogue close-out** (data-model §F): confirm against the head that the S-025, S-043 and D-011 edits, the A-035..A-065 replacement sentence (new count = base's old count, old count 0) and the coverage-index rows (§4.3.7, §5.3.3, §5.3.5, DefaultApplVerID(1137), ApplVerID(1128), Gap Registry D-011) are landed, by re-running quickstart §4.2's catalogue and coverage-index rows with their base runs. **This feature flips no backlog row to `done`** (`[const §I.3]`: no `backlog` row closes; L-074-1 and L-080-1 retire, which is a B&L move, not a catalogue status change); S-025, S-043, D-011 and A-035..A-065 keep their status. Record in the verify record.
- [ ] T075 (orchestrator) **FINAL — Feature-completeness audit.** Assert that (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale; (ii) every spec FR- and SC- maps to a landed test AND a landed implementation: FR-001 W-1/T018; FR-002 W-2/T018; FR-003 W-3/T019; FR-004 W-4/T020; FR-005 W-4/T020; FR-006 W-5, W-6(c)/(f)/T020; FR-007 W-6/T018+T020 (no session change, research R-6); FR-008 W-7, W-8 (no new rule: the landed "implementation" is the absence of a session/validator change, confirmed by `git diff origin/main..HEAD -- src/session include/fixpp/wire` being empty); FR-009 W-9/T044+T045+T046; FR-010 the mutation table M-1..M-9f; FR-011 B-097-2, L-097-1/T057; FR-012 W-11/T003+T056–T062+T063; FR-013 W-10/T070; SC-001 W-6(a); SC-002 W-4; SC-003 the mutation table; SC-004 W-1/W-2; SC-005 W-8; SC-006 W-10; SC-007 W-11 plus the owner sign-off; (iii) every feature-owned catalogue row touched (S-025, S-043, D-011, A-035..A-065) matches T074's record and its `coverage-index.md` entry. Known open item for (ii): the W-9(a3) admin-interception case has no named RED arm in contracts/witnesses.md (T043 records it as observed only), so FR-009/SC-003 are not 100 % on it until `/speckit-analyze` adds a mutant to the contract or the owner waives it; the verdict names which. Record the verdict (100 % or fully-waived) in `.specify/decisions/097-fix-latest-applverid-10-verify.md` under `## Completeness`. It is first written before `/gate-b` (its pre-flight 4d precondition), with the steps that follow this list dispositioned `Open at the audit — <reason>`: Gate B, the owner's Article XX §2 sign-off, the RATIFIED flip of the Sync Impact Report and the `> **Status:**` line (and quickstart §4.2's Status-line row, run at the flip), the merge, and the parent REMAINING-WORK row 4b close-out. It is re-confirmed after merge.

---

## Dependencies and execution order

- **Phase 1**: T001 → T002 → T003 (the amendment is the first implementation commit) → T004 → T005.
- **Phase 2** (blocks everything after): T006 ∥ T007 ∥ T008, then T009 → T010 (same file as T008) → T011
  → T012 (stop rule) → T013.
- **US1** (after T013): T014 ∥ T015 ∥ T016 ∥ T017 (tests first) → T018 → T019 ∥ T020 → T021 (commit,
  `H`) → T022 → T023–T032 serial (one scratch build tree).
- **US2** (after T032; needs T020's registry and the scratch worktree): T033 → T034 → T035–T038 serial.
- **US3** (after T038): T039 → T040 ∥ T041 ∥ T042 → T043 (RED) → T044 ∥ T045 → T046 (commit, `H`) →
  T047 → T048–T053 serial → T054.
- **US4**: T055 needs T003 and the code of US1–US3 present (its sweep reads code comments too);
  T056–T061 in parallel after T055 → T062. US4 touches no file a code task touches, so it may run
  alongside US3's mutant rows.
- **Polish**: T063 needs T055's code-comment list and T046 → T064 → T065 ∥ T066 → T067 → T068 → T069 →
  T070 → T071 → T072 → T073 → T074 → T075 (last).
- Outside tasks.md, after T075's first write: PR (titled in Article XX §2 form, data-model §E.4 Process),
  `/gate-b`, the owner's sign-off, the RATIFIED flip as the last content commit, labels, merge,
  `.specify/close-out.md`'s rows; T075 is re-confirmed after merge.

### Parallel opportunities

- **Phase 2**: T006 (`tests/dictionary/version_profile_test.cpp`) ∥ T007 (`tests/config/`) ∥ T008
  (`tests/session/test_fixt_logon_establishment.cpp`); T009 and T010 share T008's file.
- **US1**: the four test tasks T014 ∥ T015 ∥ T016 ∥ T017 (four files); T019 ∥ T020 (config vs
  registry). Mutant rows are serial (one scratch build tree).
- **US2**: none (one test file, then serial mutant rows).
- **US3**: T040 ∥ T041 ∥ T042 (three test files); T044 ∥ T045 (different emitter files, one build after).
- **US4**: T056 ∥ T057 ∥ T058 ∥ T059 ∥ T060 ∥ T061 (six record files).
- **Polish**: T065 ∥ T066.

## Implementation strategy

- **MVP = Phase 1 + Phase 2 + US1**: a FIX Latest peer logs on to a fixpp acceptor holding the FIX Latest
  dictionary (SC-001), with parse, render, config and the registry slot under their witnesses and RED
  arms. STOP and validate: `ctest -L 097` GREEN and the US1 mutation rows recorded.
- **Increment 2 = US2**: one engine serves both versions, proven by W-6(d) and the registry mutants.
- **Increment 3 = US3**: the reify arm and the typed tier's version; the golden moves by exactly the
  intended lines.
- **Increment 4 = US4 + Polish**: records, comments, the final head, the OFF build, verify, close-out.
  Nothing is ratified until Gate B and the owner's sign-off.

## Notes (hard rules)

- The orchestrator writes no library code, test, CMake line, fixture, golden or mutant by any tool
  (python and sed included); `phase-implementer` does (`[const §XVI.6]`).
- Mutants run only in `~/Work/Programming/fixpp-097-mutants` (detached), never in the shared main
  checkout, never in `fixpp-parallel`, never committed to the feature branch.
- No `git stash`; no `git checkout`/`git switch` in the main checkout; new commits only, no `--amend`.
- Commit after each task group (T003, T004, T013, T021, T033, T046, T062, T063, plus any fix commit),
  and run the line-citation check after each.
- No line-number citations and no counts, enumerated ranges or pasted results in shipped comments; a
  rewritten comment states the rule and its derivation, and names 097.
- The constitution amendment stays PENDING until Gate B converges and the owner signs off (Article XX
  §2); the RATIFIED flip is then the last content commit, and any later fix commit redoes it.
