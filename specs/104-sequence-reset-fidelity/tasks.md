---
description: "Task list for 104-sequence-reset-fidelity"
---

# Tasks: Sequence-reset fidelity — QuickFIX's endings for a reset on Logout and for a Logon acknowledgement below NextNumIn

**Input**: `specs/104-sequence-reset-fidelity/`: spec.md (US1 to US4, FR-001 to FR-018, SC-001 to SC-006, the Clarifications sessions, "Departures from QuickFIX"), plan.md ("Phases (for /speckit-tasks)", Project Structure, Constitution Check, `## Gate A`), research.md (D1 to D10, errata E-1 to E-4, risks R1 to R4, rulings O-1 to O-3; D8 the supersession census, D9 FR-017, D10 the cells), data-model.md (the receipt marker, `close()`'s order, the 141 branch, the end model, "B&L delta"), contracts/session-wire-endings-104.md (RT-1, RR-1 to RR-4 with INV-P, INV-M, INV-T, INV-X, RT-4 to RT-6, Outcomes O1 to O20, rows W, U, T, "Superseded by name"), quickstart.md. Gate A converged 2026-11-18 (loop 3 round 1); the design is not reopened here. Where this file and the bundle differ, the bundle wins and the difference goes to the orchestrator.

**Tests**: required (`[const §VII.3]`, `[const §VII.4]`). Every cell of research D10 is its own task before the mechanism it proves, and every mechanism is followed by its mutant task (SC-006).

**Organization**: plan.md's phase order. Plan phase 0 is Phase 2 (Foundational). Plan phases 1 and 2 are US1's two phases (Phases 3 and 4): the settling experiment opens Phase 3. Plan phases 7 and 8 are Phase 9 (verification), plan phase 9 is Phase 10 (Records), plan phase 10 is Phase 11 (Polish), and plan phase 11 is Phase 12, US4 (FR-017), which runs after Gate B.

## Format: `[ID] [P?] [Story] Description`

`[P]`: a different file from every other open task of its phase, and no dependency on an incomplete task. Sections of the one evidence file are exempt: tasks that only append their own section of `.specify/decisions/104-sequence-reset-fidelity-evidence.md` may still be `[P]`. `[US1]` to `[US4]`: spec.md's user stories, on story-phase tasks only. Paths are relative to the library root unless marked PARENT (`/home/catalin/Work/Programming/Antreprenoriat/`). A task marked **(orchestrator)** is run or requested by the orchestrator; every other task is the implementer's (`[const §XVI.6]`: the orchestrator writes no library code by any tool).

## Execution rules (apply to every task)

- **Trees.** `$T` is the main checkout `PARENT/research/G19-fix-fpml-iso20022/library`, branch `104-sequence-reset-fidelity`, the PR's tree. `$M` is `/mnt/wsl/fixppbuild/fixpp-104-mutants`, detached. It is the only place a mutant, a census scratch edit, a scratch draft of a mechanism or a known-positive edit is applied. `$B` is `/mnt/wsl/fixppbuild/fixpp-104-base-<pad>`, detached at `git merge-base HEAD origin/main`, computed when the bench runs. `<pad>` is a run of `x` chosen so that `${#B}` equals `${#T}` (a base path of a different length shifts code layout). The preset's `binaryDir` is `${sourceDir}/build/${presetName}`, so the build paths CMake records (`$B/build/linux-clang-release` against `$T/build/linux-clang-release`) are then equal in length too. The pad is derived when the bench runs; no length is written here. `fixpp-parallel` and the MSVC sandbox are never used as `$M` or `$B`.
- **Git.** Never `git checkout` or `git switch` in any tree, and never `git stash` (it is shared by every worktree). New commits only, never `--amend`.
  - **Commit points.** The implementer commits in `$T`, and checks `git rev-parse --abbrev-ref HEAD` before each batch. Cells are committed before their RED-reading task (T036, T062, T076). Each mechanism is committed before its mutant task, so `H` is always a commit. Every `$M` edit starts from a named commit, T020's and T040's known positives included. `$M` moves with `git -C "$M" reset --hard <H>` and reverts with `git -C "$M" restore . && git -C "$M" clean -fd`. A later `origin/main` is integrated by a merge commit.
- **Builds.** One build or ctest process at a time on this machine. Every build and ctest runs under `flock /tmp/fixpp-build.lock`. A long one is detached (`setsid nohup flock /tmp/fixpp-build.lock <cmd> > <log> 2>&1; touch <marker>.done`), with one background waiter on the marker, never a foreground poll and never `pgrep -f`.
  - Every build directory these tasks create (`$M`, `$B`, the sanitizer, release and bench trees) is on the F: mount `/mnt/wsl/fixppbuild` (`df -h /mnt/wsl/fixppbuild` first). Space is freed by emptying a build directory, never by deleting it. `export CCACHE_DIR=/mnt/wsl/fixppbuild/ccache`.
  - `$T`'s `build/linux-clang-debug` is a real directory on E: (`readlink -f build/linux-clang-debug`; `df -h /mnt/e`). Build it incrementally only: never empty it and never rebuild it from scratch. Moving it to F: is the orchestrator's call.
  - Never run a Windows build and a Linux build at once.
- **The sandbox lock.** Run `cat /mnt/c/temp/fixpp/.sandbox-lock` before any full or unfiltered `ctest`, before each measured census and before any MSVC run. A full `ctest` compiles packaging consumer projects, so it counts as a build during a peer's MSVC window. A lock held by someone else means ask the owner.
- **Base and RED.** `B0` is `bf294f00` (plan.md "Base"; `.specify/feature.json` `pinned_at`).
  - A RED cell is read RED in `$T` at its story's entry head, before its mechanism lands. Re-check the diff at each reading, and record its file list.
    - **T036 (US1):** `git diff --stat bf294f00 HEAD -- src include` lists only inert scaffolding: T016's Debug asserts and T033's step-3 mark. "Inert" is checked hunk by hunk on `git diff bf294f00 HEAD -- src include`: every added line is a plain `assert` (compiled out under `NDEBUG`), a comment, the step-3 mark's declaration, or a line that runs only inside the test on the mark's unset member; any other added line is not inert and stops the reading.
    - **T062 (US2) and T076 (US3):** the earlier stories' mechanisms are in that diff by then. The rule is that `git diff bf294f00 HEAD -- src include` contains no line of the story's own mechanism (US2: T063 and T064; US3: T077 and T078), read hunk by hunk against those tasks. The instrument: list the functions the diff's hunks touch with `git diff -U0 --function-context bf294f00 HEAD -- src/session/session.cpp` and compare them against the functions the story's tasks change (`on_inbound_logon_sent_` and `replay_outbound_range_` for US2; the text builder, `on_inbound_logon_sent_`'s and `on_inbound_not_connected_`'s too-low branches for US3), reading each shared function's hunks line by line. Its known positive: the same command at the story's GREEN head lists those functions.
  - **A cell read RED in research D10 that is GREEN on the base** is not reclassified by the implementer: its RED task stops for that cell, and the reading goes to the orchestrator, which rules it a pin with a recorded RED source or returns it to the design (as `/speckit-analyze` F1's corrections were the orchestrator's, plan `## Gate A`).
  - RED means an assertion failure, or a named Debug-assert message where the task names one. An abort, a crash or a setup failure is not RED.
  - **A compile failure is not RED evidence.** An assertion that reads a symbol 104 adds is added in the task that adds the symbol. One example is the marker accessor of T037. The cell's base RED then comes from its other assertions, and its named mutant is the RED source for that assertion.
  - A cell that research D10 marks GREEN on the base is a pin: it is read GREEN, and the task gives the reason.
  - A cell that research D10 marks unreachable on the base fails there at its setup. That failure is recorded with its cause and is not RED evidence; the cell's RED source is its named mutant.
- **Selection** (`[const §VII.8]`, quickstart §2).
  - One cell inside a bucket: run the binary directly with `--gtest_filter`. Use a leading `*`, because a `TEST_P` suite is instantiated under a prefix. Record the reported test count; 0 fails the step.
  - Constitutional runs: `ctest --preset linux-clang-debug -L 104 --no-tests=error --output-on-failure`.
  - Never `ctest -R <cell>`: it matches nothing inside a bucket and exits 0.
  - Standalone registrations that cells are re-pinned in are selected with `ctest --preset linux-clang-debug -R '^<entry>$' --no-tests=error --output-on-failure`.
  - **Every `ctest` selection, by `-R` or `-L`, carries `--no-tests=error`, and its reported test count is recorded; 0 fails the step**, as for a gtest filter. A task below that says "run its entry" or "run those entries" means this form.
- **Mutant protocol.** For each mutant row, at `H`, the commit holding the mechanism:
  - check that `git -C "$M" rev-parse HEAD` equals `H` and that `git -C "$M" status --porcelain` is empty;
  - apply only the named edit, plus `[[gnu::used]] static const char k104_mut[] = "104-MUT-<task id>-<row>";` in the edited translation unit;
  - reconfigure if CMake changed and rebuild the cell's binary; a non-zero rc on either voids the row;
  - check that `strings <binary> | command grep -c 104-MUT-<task id>-<row>` is non-zero (a stale binary fails toward clean);
  - run the binary whole and unfiltered under `timeout`, set to ten times its unmutated time and at least 60 s;
  - the named cell must go RED by its named assertion, with its forced-hit arm still GREEN in the same run. Exit 124 is a hang, never a RED;
  - record the witness, the mutant, each rc, the message, any collateral `[  FAILED  ]` names and `H`; then revert.

  A mutant that survives stops the story: it is a witness defect. A row whose RED is a Debug assert is read in a Debug build, by that assert's own message. Some mutants trip a Debug assert before their named assertion: R-1z, which research names, and any other whose Debug run aborts on an unnamed assert. Such a row is read twice: in Debug for the assert's message, and in `$M`'s `linux-clang-release` tree (asserts off) for its named assertion failure.
- **Bounded waits** (research D10, "Rules carried into the cells"). Each bounded wait derives its budget from the timeout that competes with it (`logout_disconnect_timeout_ms`, `logon_timeout_ms`, HeartBtInt, HookedStore's `kHoldBound`), and the cell states the ratio. `close()`'s wait is a poll by post, so a cell that holds a store call while `close()` runs pumps with a bounded run (`run_for`, a slice loop, `pump_until_ready`), never an unbounded `poll()`, `run()` or `run_one()` loop. A cell that holds a store call releases the hold and drains on every exit path, so a failed assertion stays an assertion failure.
  - **A cell that asserts `close()` returned under a mock clock** (R-1f(1), R-1g, R-1v, R-1w and the like) pumps `close()` with a bounded run on that condition, and asserts it before anything else. On the path where `close()` has not returned, the cell then advances the mock clock past `logon_timeout_ms` and pumps `close()` to completion before it ends. So a mutant that makes `close()` poll to its bound reads RED by the cell's named assertion, never as exit 124. "Well inside that bound" (R-1g) and "just below `logon_timeout_ms`" (R-1w) are each a ratio of `logon_timeout_ms` that the cell states.
- **Seams.** Every seam a 104 cell uses is a `*_test_access` friend defined in `tests/support/` and befriended by its class (`session_test_access`, `seqnum_manager_test_access`, `engine_test_access`) or a test double (HookedStore and its record, StoreDouble, the cell-local store doubles, `ControlledWriteTransport`). The one production line added for a test is T033's step-3 mark (plan "Structure Decision"). A seam outside that set goes to the orchestrator before it is written.
- **Evidence.** Every record goes to `.specify/decisions/104-sequence-reset-fidelity-evidence.md` until `/speckit-verify` writes its own record. Each record carries the task ID, the command, the result or the message that fired, `git rev-parse HEAD` and `git status --porcelain`. Census results and pass counts go there, **never in code comments or this bundle**.
- **Comments and records.** No line-number citation in any added line: cite a symbol, a heading or a quoted phrase. A comment records a condition and a procedure, never a result: no count, no timing, no enumerated sites, no example values. A file that implements a rule superseding a merged feature's decision names that decision and 104 in a header comment (issue #310).
  - After each commit batch, run `python3 tools/check_line_citations.py --range origin/main..HEAD` and `python3 PARENT/.claude/scripts/check-comment-claims.py --root "$T" --base origin/main`. The second check is lexical, so read added inventories and runtime strings by hand.
- **Shell and index.** Run recipes under `bash` with literal pathspecs, using `command git` and `command grep`. Run `git show "$r:path"` from a variable through a Python subprocess (the RTK hook drops `:path`). CodeGraph's `projectPath` is `$T` and never another tree's index. Run `codegraph sync` in `$T` after every code-changing phase.
- **Peers.** Other sessions are in flight. Before any integration merge and before the first labelled push, list the files both sides changed since the old merge-base. Compute `comm -12` over `git diff --name-only <merge-base> origin/main` and `git diff --name-only <merge-base> HEAD`. Its known positive: with `origin/main` replaced by `HEAD`, it reports 104's whole change. A file on that list that a census or cell task read re-runs that task.

## User stories on the phases

| Story | Priority | Issue | Phases | Independent test (spec.md) |
|---|---|---|---|---|
| US1 an unanswered Logout does not reset under `reset_on_logout` | P1 | #595 (B53) | 3 (settling experiment, RED cells), 4 (the change) | a graceful close with `reset_on_logout` and no peer answer, ended by the timeout and by a drop; both numbers and the store read afterwards |
| US2 a lenient initiator accepts an unasked 141=Y acknowledgement | P1 | #695 (B107) | 5, 6 | an initiator hydrated above 1; an acknowledgement 141=Y at 34=1; state, numbers, store and frames read |
| US3 a too-low frame in a Logon arm draws QuickFIX's Logout | P2 | #701 | 7, 8 | an initiator at NextNumIn N, an acknowledgement at 34 = M < N; an acceptor hydrated at N, a Logon at M < N |
| US4 the peak-memory figure counts as evidence again | P3 | #266 basis | 12 (after Gate B) | the #266 report's own comparison on 104's labelled run |

MVP: Phases 1 to 4 (US1). Nothing ships at the MVP; the feature merges whole.

---

## Phase 1: Setup (no production code)

**Purpose**: prove the tree, the base and the records path before any census or cell.

- [ ] T001 Pre-flight in `$T`. Check each of the following, and record the result:
  - `git rev-parse --abbrev-ref HEAD` is `104-sequence-reset-fidelity`, and `.specify/feature.json` pins `specs/104-sequence-reset-fidelity` at `bf294f00`.
  - `git diff --stat bf294f00 HEAD -- src include tests bindings tools bench ci CMakeLists.txt CMakePresets.json` is empty. A non-empty diff stops Phase 1, because `B0` is the premise of every RED reading.
  - `git status --porcelain` is empty.
  - `readlink -f .specify/decisions` resolves under PARENT `research/G19-fix-fpml-iso20022/decisions/speckit/`.
  - `codegraph status --json` reports `projectPath` = `$T` and a non-zero `fileCount`. If `rev-parse HEAD` moved since the last sync, run `codegraph sync`.
  - The pipeline steps that block implementation are recorded: `/speckit-analyze`, `/speckit-checklist` and `/speckit-checklist-audit` (`.specify/pipeline.md` steps 6, 7 and 9).
  - The build-approval basis is recorded: §XVII.7 resource gate discharged by the owner's standing grant of 2026-11-17 ("run autonomously through all steps … Stop if you hit a blocker"). Builds still run one at a time under `flock /tmp/fixpp-build.lock`, new trees on `/mnt/wsl/fixppbuild`, and stop on any WSL memory pressure.
- [ ] T002 Create `$M` with `git -C "$T" worktree add --detach /mnt/wsl/fixppbuild/fixpp-104-mutants HEAD`. Configure its `build/linux-clang-debug` tree, and check that the preset leaves `NDEBUG` undefined, since the settling experiment needs asserts on. Configure `build/linux-clang-release` in `$M` too, for the asserts-off readings. Both trees go on `/mnt/wsl/fixppbuild`. `$B` is created at T087, not now.
- [ ] T003 Create `.specify/decisions/104-sequence-reset-fidelity-evidence.md` with a header recording `B0`, the three trees, and the section order: censuses, settling experiment, RED readings, GREEN readings, mutants, verification, FR-017.

---

## Phase 2: Foundational (blocking: plan phase 0, and HookedStore's per-call record)

**Purpose**: the censuses whose results feed the story phases' re-pin lists and their STOP conditions, plus the one test-support addition the US1 and US3 cells assert on. No `src/` or `include/` line changes here.

**⚠️ CRITICAL**: no story work begins until T007 and T010 have cleared.

- [ ] T004 [P] D8 supersession census, static half (research D8 "The recipe" steps 1 to 3, and step 5's known positives):
  - Run step 1's `git grep -n -F -e '<key>'` once per key of "The superseded rules and their search keys", over step 1's pathspec as written (`spec brain specs .specify/*.md include src tests bindings tools ':!specs/104-sequence-reset-fidelity'`). For the B&L file, read only the live `spec/behaviors-and-limitations.md`.
  - **Known positives first.** Each entry of step 5 must be a hit, B-024-1 ("Logout teardown (sent OR received)") being the seeded positive. A missing positive means the census is broken; it is fixed before any zero is read.
  - Give each kept hit one disposition in the evidence file (step 3): **re-pin**, **reword**, **supersede by name** (a merged bundle, which is never edited, flagged on its component page instead), or **unaffected**, with the reason.
  - Each story phase's re-pin list is the set of kept hits for its rule row of the key table.
- [ ] T005 [P] D8 step 6, the census leads. Settle each and record its disposition:
  - `tests/session/test_recovery_admin_span_gapfill.cpp`: its RR(2,12) and RR(2,5) against its store tail, and its header's `NewSeqNo=21`;
  - the builders called with variables: the GapFill and SequenceReset builders and matchers whose NewSeqNo is a parameter, so a literal key cannot see the value a call passes. They are the production `build_sequence_reset_gapfill` (`include/fixpp/session/admin_messages.hpp`) and the test-local `make_sequence_reset`, `make_seq_reset_frame`, `make_sequence_reset_frame`, `make_seq_reset_gapfill`, `make_seq_reset_reset`, `build_sequence_reset` and `is_sequence_reset_gapfill`.
    - Re-derive the names first: `git grep -hoE '(build|make|is)_[a-z_]*seq(uence)?_reset[a-z_]*\(' -- src include tests | sort -u`.
    - Then list the calls with `git grep -nE -A3 '\b(<the names, joined by |>)\(' -- src tests`, and read each call's NewSeqNo argument, which often sits on a continuation line. Keep each call whose argument is not a literal and that sets or checks a GapFill's end.
    - **Known positive:** the `build_sequence_reset_gapfill(` call in `replay_outbound_range_` (`src/session/session.cpp`), whose NewSeqNo is a variable, must be in the reading;
  - `tests/interop/parity/parity_support.hpp` and `tests/interop/conversation/conversation_script.yaml`;
  - the no-store comparator path in `test_538_volatile_store_after_reset_logon.cpp`.
- [ ] T006 [P] D7 acceptor-twin reachability (quickstart §1, "Acceptor twin"): run the three `git grep` lines and read each hit. Record whether any hit is a production path that leaves NextNumIn above 1 at an acceptor's first Logon.
- [ ] T007 **(orchestrator)** STOP gate for D7. If T006 found a production path, the acceptor twin of US2 is in scope (spec Edge Cases) with no designed cell. Implementation stops, and the evidence goes to the owner. Otherwise record "unreachable, out of scope", citing T006.
- [ ] T008 [P] Research R1, the manager's inline grant. Re-derive the takers with `git grep -n "seqnum_mgr_\.\(assign_outbound\|check_inbound\|set_next\|reset_to_one\)" src/session`. Read `async_lock_op`'s fast path in `include/fixpp/core/sync/async_mutex.hpp`, and confirm that no `SeqnumManager` method in `src/session/seqnum_manager.cpp` suspends while it holds the mutex. Record the reading. RR-2 item 3 and INV-T rest on it.
- [ ] T009 Research R1, the store-order condition (contract RR-4's first case): run `git grep -n "async_lock\|asio::post\|offload" -- include/fixpp/session/*store*.hpp src/session/*store*.cpp`. Read `MemoryStore` (a post, then its FIFO `async_mutex`), `FileStore` (its offloaded jobs) and `tests/support/hooked_store.hpp`, and record whether any of them can apply a later `reset_to` before an earlier `store`.
- [ ] T010 **(orchestrator)** STOP gate for R1 (orchestrator ruling of 2026-11-17). If T009 found a store that can reorder, implementation stops and the decision goes back to the orchestrator. R1's fallback ("the unit waits for the session's in-flight outbound store calls before its `reset_to`") is a proposal, not a plan. Otherwise record the pass.
- [ ] T011 [P] R-1r's route (contract RR-4, quickstart §1 "The emitters at the reset point"). Read `run_liveness_loop`, `Session::send_body_`, `replay_outbound_range_` and the `inbound_*_leaf_` family. Record the first Active sub-arm that writes (a leaf or `persist_inbound_advance_`) after a suspension that follows its `check_inbound`. If there is none, record that R-1r drives the boundary through `session_test_access::store_then_emit` after a receipt reset. Also record every emitter's state test before its `assign_outbound`.
  - **Arm A in LogonReceived** (research D1, "Read from source: a too-high Logout in LogonReceived", its last bullet). Record, as a disposition, that the row has input only while a close or a stop is in flight. Then read whether research D10's R-3 input is drivable with `close()` in flight (D10, "R-3's condition"): `close()` joins no inbound frame, so the cell must hold `close(graceful)` itself, through HookedStore's `Hooks::flush_until`, after its commit of `closing` and before it records Disconnected, with the acceptor's Logon arm already at its superseded exit in LogonReceived. Record "drivable", and R-3 is written at T025, or "no input while `close()` can be observed in flight", and R-3 is dropped. Also record what A writes on the wire in LogonReceived (`inbound_logout_leaf_` replies in Active only).
- [ ] T012 [P] Research R2, the census of existing `close()` pumps. It runs before the poll lands (T044).
  - Find the files with `git grep -l -e HookedStore -e hold_until_close_reset -e reset_to_mode -- tests`, then list their pumps with `git grep -n -e '\.poll()' -e '\.run()' -e 'run_one()' -- <those files>`. Read the rig and settle helpers those files pump through, since the pump may live there.
  - A miss in those files is a reading, not a broken instrument: the same grep reports hits elsewhere in `tests/`, which is its known positive.
  - Disposition each pump as "bounded already" or "re-pin to a bounded pump" (`run_for`, `pump_until_ready`). T043 applies the re-pins.
- [ ] T013 HookedStore's per-call record in `tests/support/hooked_store.hpp` (research D10, "One test-support addition"). Today `on_reset` fires for both `reset()` and `reset_to` under `reset_to_mode::forward`, so a hook cannot tell the unit from the teardown.
  - Add an issue-time, per-call record beside `StoreLog::writes` (research D10, "One test-support addition", its delta). `StoreLog::writes` is a completion log with no session state. The new record is appended at each `reset` or `reset_to` call's entry, before any hook, hold or forward, in every `reset_to_mode`. Each entry carries which call fired, `reset_to`'s targets, and the state and transport-open readings from a snapshot callable the cell sets (the shape of `StoreLog::close_began`). A count of each kind goes with it.
  - A cell reads the new record for a reading at the call or a count by kind, and `StoreLog::writes` for an order against `close()` or stop and for the final counters. R-2 needs two readings at the call, so it reads two entries of the new record: `on_reset` is one-shot.
  - The existing `on_reset`, hold and `kHoldBound` behaviour is unchanged.
  - The header comment names 104 and states the purpose. It states no count.
- [ ] T014 Register `session_104_tests` with its first file. Create `tests/session/test_104_reset_on_logout.cpp`, holding one cell, `HookedStoreRecord104.ResetAndResetToRecordedApart`, which is T013's known positive. It calls `reset()` and `reset_to` directly and reads two distinct records with their counts.
  - Add `add_session_104_bucket`, a twin of `add_session_103_bucket` with `LABELS "session;104"` and no `TIMEOUT` property, to `tests/session/CMakeLists.txt`. Register `add_session_104_bucket(session_104_tests test_104_reset_on_logout.cpp)` with it. No empty bucket is registered.
  - Build, and run the cell with a non-zero test count. Mutant: in `$M`, the record files `reset_to` as `reset` → the cell is RED.
- [ ] T015 Existing users of HookedStore are unchanged. Map `git grep -l HookedStore -- tests` to its ctest entries, then build and run those entries in `$T`'s debug tree, by the Execution rules' selection form. All are GREEN. Record.

**Checkpoint**: T007 and T010 have cleared; every census has its known positive and its rows; `git diff --stat bf294f00 HEAD -- src include` is empty.

---

## Phase 3: User Story 1, part 1 — the settling experiment, then US1's RED cells (Priority: P1) 🎯 MVP (plan phase 1)

**Goal**: run INV-T and INV-X's Debug asserts against the inputs that reach their windows (owner, 2026-11-18) before the construction is built on them; then write every US1 cell RED.

**Independent test**: the settling selection's asserts stay silent, their known positive fires, and each D10 cell is RED on the base by assertion (or GREEN as D10 states, with its reason).

- [ ] T016 [US1] Debug asserts for INV-T in `src/session/session.cpp`: `assert(peer_logout_.phase != peer_logout_mark::phase_t::acking)` at the entry of `on_inbound_active_logout_` and at the entry of `on_inbound_active_too_high_logout_`. Use the plain `assert` form `session.cpp` already uses, with no test-hook macro. Each assert's comment names INV-T and contract RR-2 and records no result. The asserts compile at the base and change no Release behaviour.
- [ ] T017 [US1] R-1y in `tests/session/test_104_reset_on_logout.cpp`: `ResetOnLogout104.DuplicateTooHighLogoutWhilePending_OneResetByTheBound_{RouteA,RouteB}`, per research D10's row.
  - Setup: rol only, a mock clock, HookedStore. `RouteB` is the input of 103's `SecondTooHighLogoutWhilePending_*`; `RouteA` is the input of `H_LogoutResentInSequence_*`, with the resent Logout above N.
  - Assert exactly one acknowledgement Logout and exactly one `reset_to(1, 1)`, at the hook with state `Disconnected`; after `close()`, no `reset`. "Exactly one" is its own forced hit, since zero and two both fail.
- [ ] T018 [US1] R-1z in `tests/session/test_104_reset_on_logout.cpp`: `ResetOnLogout104.ClaimsAPendingBound_OneResetByTheArm_{CloseDuringReplyStore,OneDelivery}`, per research D10's row.
  - `OneDelivery` feeds a GapFill past S and the in-sequence Logout at the new N in one delivery. They come from a cell-local coroutine that awaits `on_inbound_frame` for both frames back to back, as `run_read_pump` does. `Scene::feed` is not used, because it pumps between frames.
  - Assert: `close()` does not return before the release; exactly one `reset_to(1, 1)`, by A, with the hook seeing `Disconnected`; no `reset`; no `close_reset_wait_expired`.
- [ ] T019 [US1] The settling run (plan phase 1), in `$T`'s Debug build with asserts on. Run both selections together:
  - `session_103_tests --gtest_filter='*TooHighLogoutState103.*'`. First check with `--gtest_list_tests` under the same filter that it includes `SecondTooHighLogoutWhilePending_*` and `H_LogoutResentInSequence_*`.
  - `session_104_tests --gtest_filter='*ResetOnLogout104.DuplicateTooHighLogoutWhilePending_*:*ResetOnLogout104.ClaimsAPendingBound_*'`. `OneDelivery` must be in the list.

  Record each test count, which must not be 0. Keep three readings apart:
  - (a) 103's selection is GREEN;
  - (b) R-1y and R-1z are RED by assertion, as expected on the base (the teardown's `reset`, no `reset_to`);
  - (c) no Debug assert fires.

  An assert abort in any test is (c) failing, and goes to T021. An expected RED in (b) is not a STOP.
- [ ] T020 [US1] The entry assert's known positive, in `$M` only (plan phase 1; quickstart §2):
  - **The edit.** Insert a capped loop between `on_inbound_active_`'s `acking` drain test and its sub-arm dispatch: `for (int i = 0; i < K && peer_logout_.phase == peer_logout_mark::phase_t::pending; ++i) co_await asio::post(…)`. The loop also records the pass at which the mark left `pending`, and the phase it read at exit.
  - **K.** K is eight times the minimum posted-turn count of the bound's wake path, as plan phase 1 derives it. Re-derive that count by its three greps (`operator||` in `<asio>/experimental/awaitable_operators.hpp`, `co_spawn_entry_point` in `<asio>/impl/co_spawn.hpp`, and `^void complete` in `src/core/test/mock_clock.cpp`) at the pinned asio, and record the derivation and K in the evidence file.
  - **The observable.** R-1z's `OneDelivery` arm aborts on the entry assert's message.
  - **The known positive itself fails** if the cap is hit with the mark still `pending`, or if the loop exits with a phase other than `acking`. For the second case the remedy is in the scratch run only: hold the bound's reply `store` in HookedStore so that the bound stays `acking`, then re-run.
  - Record the exit pass and phase. If the arm does not abort, the selection cannot report a failure of INV-T, and the experiment is not accepted.
- [ ] T021 [US1] **(orchestrator)** STOP gate for the settling experiment. If any assert fired in T019, or T020's known positive did not fire (or failed its cap or exit-phase rule), implementation stops. The cell, the assert and the stack go to the owner. Otherwise record what a green `OneDelivery` run proves and what it does not (plan phase 1): a one-turn suspension stays covered by the source reading of INV-T only.
- [ ] T022 [US1] R-1a and R-1b in `tests/session/test_104_reset_on_logout.cpp` (SC-001; contract W1, W2).
  - `ResetOnLogout104.Unanswered_Timeout_NoReset` runs in both roles, with MemoryStore and a persistent StoreDouble. Before the clock advance, assert that a 35=5 was captured and `LogoutSent` reached.
  - `…Unanswered_TransportDrop_NoReset` runs as an engine-managed initiator on `fixpp::test_support::plain_rig` (`tests/session/plain_engine_rig.hpp`). `logout_disconnect_timeout_ms` is set above the cell's bounded wait for the drop's close, by a ratio stated in the cell. Assert that `close()` completed before the grace deadline. The grace timer is a real `asio::steady_timer` (`close_grace`, plan `## Gate A` round 2), so the cell reads the completion instant and the deadline both on `std::chrono::steady_clock`, never on the rig's mock clock.
  - Both cells assert that the numbers equal those after the Logout's emit, and that there is no `reset` or `reset_to` call. The forced-hit arm is the answered twin, R-1c's input, on the same store double.
- [ ] T023 [US1] R-1c, R-1d and R-1e in `tests/session/test_104_reset_on_logout.cpp` (contract O1, W3, W4, W5). Each reads at HookedStore's `on_reset` hook: `reset_to` (not `reset`), state `Disconnected`, and the transport open.
  - `…Answered_ResetAtReceipt`: phase 1 is not yet woken at the hook.
  - `…PeerRequest_ResetAtReceipt`, both roles: the reply 35=5 carries the pre-reset next outbound, and the numbers inside `onLogout` are the pre-reset ones (DQ-10).
  - `…TooHighLogout_ResetAtAcknowledgement`: the gap is filled and the bound acknowledges; the pre-reset numbers are read inside `onLogout`.
  - Each asserts one reset in total.
- [ ] T024 [US1] R-1f in `tests/session/test_104_reset_on_logout.cpp` (contract O12, O18): `…TooHighLogout_ClaimedByClose_NoReset` and `…_AckingWhenClosed_ResetOnce`, on one store double with a mock clock. In (1), `close()` while the mark is `pending` makes no `reset_to` and no `reset`. `close()` returns with the mock clock below `logon_timeout_ms` and with no `close_reset_wait_expired`. In (2), the bound is `acking`, with its reply held in HookedStore, and one `reset_to` lands before `close()` returns. (2) is (1)'s forced-hit arm.
- [ ] T025 [US1] R-1g and R-1n in `tests/session/test_104_reset_on_logout.cpp` (contract O2 to O11). Both run rol only, with A in Active.
  - `…ReceiptEndsBeforeResetPoint_NoReset_{FromAdminThrow,FromAdminReject,LateParseFailure}` asserts the arm's existing ending, then no reset after the arm or after `close()`. `close()` must return well inside `logon_timeout_ms`, a ratio stated in the cell. The forced hit is the same fixture with `fromAdmin` returning (R-1d's input).
  - `…ReplyEndings_{Overflow,ToAdminThrow,AssignFailure,StoreFailure,WriteFailure,PersistFailure}` resets for `Overflow`, `StoreFailure` and `WriteFailure`, and does not reset for the other three.
  - `Overflow` is a pair: CompIDs for which the reply does not fit, and the longest that still fit. `AssignFailure` sets outbound to `seqnum_max` through the test seam. The no-reset rows' forced hits are the reset rows.
  - R-3, `…LogonReceivedLogoutWhileClosing_ResetOnceByTheArm`, only when T011 recorded its input drivable (research D10's row and "R-3's condition"): a direct acceptor with rol, its Logon arm at its superseded exit in LogonReceived, `close(graceful)` held in its flush, then an in-sequence peer Logout. Assert at the hook `reset_to(1, 1)` with state `Disconnected`, exactly one `reset_to`, no `reset`, `close()` returning only after the arm's unit, and the wire T011 read. When T011 recorded no input, the arm is not written, and T036 cites T011.
- [ ] T026 [US1] The `store_cancelled` store double and R-1v, in `tests/session/test_104_reset_on_logout.cpp` (contract O9).
  - The double is cell-local. Its outbound `store()` returns `fixpp::core::error::store_cancelled` for the reply, and the same double can instead refuse the reply's store, which is R-1n's `StoreFailure` input.
  - `…ReplyStoreCancelled_NoReset`, rol only with a mock clock, asserts: the number taken back (next outbound is N_out again); `Disconnected`; no frame; no `reset_to` and no `reset`; a later `close()` returns with the mock clock below `logon_timeout_ms` and no `close_reset_wait_expired`.
  - The forced-hit arm is the `StoreFailure` input on the same double, which resets.
- [ ] T027 [US1] R-1h and R-1i in `tests/session/test_104_reset_on_logout.cpp` (contract W8, W9, O16).
  - `…PeerLogoutThenReconnect_NoResetOnNewConnection` uses a standalone initiator. It asserts the reset at the reset point, then `drive_reconnect`, the reconnect Logon written, the numbers advancing, and no reset at `close(terminal)`. Its first half is its forced hit.
  - `…ReceiptResetStoreFailure_LoggedThenDisconnected` uses a persistent StoreDouble with `fail_next_reset`. It asserts `Disconnected`, the arm's return as without the failure, manager `(1, 1)`, and no teardown retry.
- [ ] T028 [US1] R-1j, R-1k and R-1s in `tests/session/test_104_reset_on_logout.cpp` (contract O12, O14, O17).
  - `…CloseWaitsForReceiptUnit`: HookedStore holds `reset_to`, and the session runs on a mock clock. Assert that the hold is reached in L's arm (`Disconnected`, transport open, phase 1 not woken). `close_grace` is a real `asio::steady_timer`, so a small real-time `logout_disconnect_timeout_ms` expires during the hold, while the mock clock is never advanced to `logon_timeout_ms`. Assert that the transport closed while the `close()` future is pending, and `(1, 1)` after the release. `logout_disconnect_timeout_ms` and HookedStore's `kHoldBound` are both real time: the cell states their ratio, chosen so that the timer expires well inside the hold on the sanitizer lanes too (T085), and asserts `StoreLog::hold_timed_out` false after the release.
  - `…CloseDuringReplyStore_OneResetByTheArm` runs with A in Active and HookedStore holding the reply's `store`, and `close()` is started during the hold. It asserts that `close()` does not return before the release, then exactly one `reset_to` and no `reset`, finished before `close()` returns.
  - `…CloseWaitsForLogonArmUnit_NoKnob` sets no knob and feeds the initiator a 141=Y acknowledgement while `reset_to` is held. `close()` does not return before the release.
- [ ] T029 [US1] R-1l, R-1u and R-1x in `tests/session/test_104_reset_on_logout.cpp` (contract O13-r, O13-p, O13a, O15).
  - `…CloseWhilePending_NoClock_NoReset_{HeldInReplyStore,HeldInPersist}` names each number on its own: store inbound, manager inbound and the reply at N_out. It has a `transport_send` twin, where the reply is transmitted (B-563-5). The forced hit is R-1k's input, the same hold with a clock.
  - `…CloseInsideOnLogout_ResetOnceByTheArm` asserts one `reset_to` and no `reset` before `close()` returns, with the hook seeing `Disconnected`.
  - `…TerminalEmissionAtResetPoint_ResetOnce` uses a direct `Session`. The cell `co_spawn`s `on_inbound_frame` on its own cancellation slot and emits `terminal` from inside the `onLogout` that RR-3 step 2 fires. It asserts one `reset_to(1, 1)`, numbers `(1, 1)` and no `reset`. Its "marker `reached`" assertion is added at T037, with the accessor; its base RED comes from its reset-count assertions.
- [ ] T030 [US1] R-1w in `tests/session/test_104_reset_on_logout.cpp`: `…CloseNotHeldWhenReceiptEndsWithoutUnit_{FromAdminThrow,Storeless,StuckPending}`. `Storeless` holds the reply's transport write in the `arm_block()` / `release()` pattern of `ControlledWriteTransport` (`tests/session/test_live_outbound_serialized.cpp`). `StuckPending` is the forced hit of the expiry event: the hold is never released and the clock is advanced to the bound, giving one `close_reset_wait_expired`. It is also the forced hit of R-1f(1)'s, R-1v's and R-1t's "no `close_reset_wait_expired`" assertions.
  - **The `Storeless` reading (a recorded decision).** Read whether the held write outlasts `close()`'s `close_async()`. If it does not, the storeless exit has no input while `close()` waits. The arm is then dropped, and the reading is recorded in the evidence file.
- [ ] T031 [US1] R-1o in `tests/session/test_104_reset_on_logout.cpp` (contract W10, W11, O19, O20). The arms share one store double:
  - `…LogonSentLogout_Resets`, as an initiator with rol: no 35=5 written by fixpp, `reset_to(1, 1)`, `Disconnected`. It is the forced hit of the others' "no reset".
  - `…_MtlsUnauthorised_NoReset`, as a direct mTLS initiator: no reset, `Disconnected`, and the authorisation-failed event. It is RED on the base by that event assertion: the base refuses a 35=5 in LogonSent at `interpret_logon`, before the identity gate. The assertion also witnesses the N-8 order (the gate before arm S's reset).
  - `…LogonSentLogout_RolOff_MtlsUnauthorised_Unchanged`, as a direct mTLS initiator with rol off and an unauthorised identity (contract W11; orchestrator ruling 2026-11-18): no reset, `Disconnected`, the arm returns success, and no authorisation-failed event. The `_MtlsUnauthorised_NoReset` arm is the forced hit of that absence: it reads the same event type from `Session::recent_events()` by the same predicate. It is GREEN on the base, as a pin; its RED source is T039's mutant "arm S's branch runs whatever the knob", under which the event appears.
  - `NotConnectedLogout_NoReset`, as an acceptor: no reset. It is GREEN on the base, as a pin.
- [ ] T032 [US1] R-1p, R-1q and R-1r in `tests/session/test_104_reset_on_logout.cpp` (contract RR-4).
  - `…LivenessDuringReceiptUnit_NumbersNothing` uses a small HeartBtInt, and moves the mock clock past one HeartBtInt and then two during the hold.
  - `…BoundRouteFrameDuringUnit_Drained` feeds a frame at 34 > 1 during the hold, and asserts no ResendRequest, no inbound advance and no `fromApp`.
  - `…BoundRouteInFlightPumpArm_RefusedAtTheBoundary` takes the route T011 recorded, a pump arm or the `session_test_access::store_then_emit` seam. It asserts that nothing the in-flight arm writes reaches the wire or the store's numbers, and store and manager `(1, 1)`.
- [ ] T033 [US1] R-1t's step-3 mark, a test seam in production code (plan "Scale/Scope"; B-104-7).
  - Add a private member to `Engine` in `include/fixpp/session/engine.hpp`, beside `test_hook_post_send_drain_`, with the same shape: unconditional and never gated on `FIXPP_TEST_HOOKS` (the ODR rule in `brain/components/session.md`, "Test access to private `Session` state").
  - Add its setter to `tests/support/engine_test_access.hpp`, mirroring the existing setter.
  - In `src/session/engine.cpp`, `Engine::stop()` awaits the hook, when set, on the control strand. It sits after the block commented "Join the closes before step 3" and immediately before "Step 3: JOIN + send-drain".
  - The header comment names 104 and R-1t, and says that the seam is inert when unset. The step comment beside the mark in `Engine::stop()` states the dependency a later reorder must re-read: 104's stop ending (contract RR-2 item 4, "Under `Engine::stop()`") rests on step 3's role-loop join preceding step 4's `close()`, and cell R-1t pins it.
  - No public C++ or C-ABI call reaches the mark. Check: `git grep -n <the member's name> -- ':!tests'` hits only its declaration in `engine.hpp` and its read in `engine.cpp`, and its setter is defined only in `tests/support/engine_test_access.hpp`. Known positive: the same grep without `':!tests'` also hits the setter.
  - Build. The existing Engine entries stay GREEN: `engine_reset_unit_stop_test.cpp`'s entry, and the entries of `git grep -l engine_test_access -- tests`. Record.
- [ ] T034 [US1] R-1t in `tests/session/test_104_reset_on_logout.cpp`: `…EngineStopDuringReplyStore_JoinWaitsPastTheBound_OneResetByTheArm`. It runs rol only, with A in Active, on `plain_engine_rig.hpp`'s mock-clock Engine with a HookedStore holding the reply's `store`, as `engine_reset_unit_stop_test.cpp` does. Its sequence, per research D10's row:
  1. Install the step-3 mark through `engine_test_access` (T033), then start `Engine::stop()`.
  2. Pump, bounded on a condition, until the mark has run or `session_test_access::is_closing` holds. Assert that the mark ran and the session is not closing.
  3. Advance the mock clock once past `logon_timeout_ms`, and pump for a settle window. HeartBtInt is set, by a ratio stated in the cell, so that no liveness deadline falls inside the advance.
  4. With the hold still in place, assert: not closing, no `close_reset_wait_expired`, and stop's completion not observed after a bounded pump.
  5. Release the hold. The known positive: the same harness observes stop's completion. Assert exactly one `reset_to` and no `reset`, recorded before stop returns.

  **Budgets.** Every pump from the start of stop to the release (the order pump, the settle window and the pump that must not see stop complete) has a budget derived from HookedStore's `kHoldBound` (`tests/support/hooked_store.hpp`, cited by symbol), so that their sum stays below it; the cell states the ratio. `kHoldBound` is real time (`std::chrono::steady_clock`), so each budget is a real-time bound (`run_for`, a slice loop); the one mock-clock advance past `logon_timeout_ms` takes no real time and is not part of the sum. After the release, assert that `StoreLog::hold_timed_out` is false. On every exit path the cell releases the hold, pumps stop to completion, and drops its session handle before the Rig is destroyed.
- [ ] T035 [US1] R-2 in `tests/session/test_104_reset_on_logout.cpp`: `…BothKnobs_PeerAnswers_TwoResets_EndsOneOne` (D2, contract W7). HookedStore sees `reset_to` in L's arm with the transport open, then `reset` in the teardown; the numbers are `(1, 1)`.
- [ ] T036 [US1] RED reading of every US1 cell (T017, T018, T022 to T032, T034, T035), per the Execution rules' "Base and RED", at `$T`'s head before T037.
  - Record each cell's test count and its failing assertion. Each is RED by assertion, not abort, except R-1o's rol-off and NotConnected arms and T014's cell, which are GREEN with their reasons. R-1o's mTLS arm is RED by its authorisation-failed event assertion (research D10). R-3 is read when T011 recorded it drivable; otherwise the record cites T011.
  - `git diff --stat bf294f00 HEAD -- src include` lists only T016's and T033's files.
  - Re-run T019's selection once more: no assert fires.

**Checkpoint**: the settling experiment has been accepted (T021), and every US1 cell is RED, or a recorded pin.

---

## Phase 4: User Story 1, part 2 — the receipt rule (Priority: P1) (plan phase 2; research D1, D2; contract Part 1)

**Goal**: with `reset_on_logout`, the reset happens if and only if a received Logout reaches its reset point. Sending a Logout never resets. `close()` waits for a `pending` receipt by a poll. RR-4 refuses store calls once the marker is `reached`.

**Independent test**: every Phase 3 cell is GREEN; `ctest -L 104 --no-tests=error` passes; the re-pinned `session_reset_on_lifecycle` and `session_inbound_frame_dispositions` pass; each mutant of T051 to T056 is RED.

- [ ] T037 [US1] The receipt marker's type (data-model "`logout_seen_`"; FR-006), in `include/fixpp/session/session.hpp`.
  - `logout_seen_` keeps its name and becomes a one-byte enum, underlying type `std::uint8_t`, with the values `none`, `pending` and `reached`.
  - Its header comment states RO-1 to RO-3 as conditions and names 104, superseding 024 T013 / C3.1 by name. It names the receipt arms by reference to contract RT-1, not as a list of functions, and gives the recipe `git grep -n logout_seen_ -- src include` and no count of sites.
  - Re-read every hit of `git grep -n logout_seen -- tests src include bindings tools`. At the base, the hits outside `session.cpp` are comments, and each is a T004 kept hit.
  - **The interim tree, until T038, T042 and T044.** T037 changes the type only, and each base use is mapped so that the tree keeps the base's behaviour. Every `logout_seen_ = true` write (the entries of `on_inbound_active_logout_` and `on_inbound_active_too_high_logout_`, and `run_logout_phase1` after its emit) writes `reached`. The teardown predicate reads `logout_seen_ != none` where it read the `bool`. Re-derive the uses with `git grep -n -e 'logout_seen_ = ' -e 'logout_seen_ &&' -- src`. T038 replaces the two arm writes, T042 deletes phase 1's, and T044 rewrites the predicate. Record the mapping in the evidence file, since T040 reads this tree.
  - Add a static read accessor for the marker to `tests/support/session_test_access.hpp` (brain "Test access to private `Session` state": a static function on the existing struct, with no change to the class's access). Add R-1x's "marker `reached`" assertion in T029's cell. Its RED source is T051's R-1x mutant (the frame-local owner clears unconditionally on exit). T052's R-1x mutant is read by "exactly one `reset_to`", since it leaves the marker `reached`.
- [ ] T038 [US1] The pump frames' ownership of the marker, in `src/session/session.cpp` (contract RR-2 items 1 and 2):
  - With rol set, the marker becomes `pending` at receipt in four places only: arm A's entry (`on_inbound_active_logout_`); arm L's 35=5 test after its garble and fault guards (`on_inbound_logout_sent_`); arm S's 35=5 branch ahead of `interpret_logon` (`on_inbound_logon_sent_`); and B's immediate-acknowledgement branch of `on_inbound_active_too_high_logout_`.
  - B's spawn and ignored branches write nothing to the marker.
  - A frame-local owner sets `none` on every exit, by return or exception, if the marker is still `pending`, and never undoes a close-out's `reached`.
  - The frame clears the marker before any `close()` it calls itself (`close_on_late_parse_failure_`, the throw paths).
  - With rol off, the marker stays `none`.
  - Mutants, run at T051: R-1g's two (clear only at the reset point; `LateParseFailure` clearing after the arm's `close()`), R-1v's (clear on return only, not while unwinding), R-1f(1)'s (B's spawn branch sets `pending`), R-1y's (round 3's hand-off, applied together), R-1z's (the bound writes `none` at its claimed `!live()` exit) and R-1x's marker mutant (the frame-local owner clears unconditionally on exit, not only from `pending`).
- [ ] T039 [US1] The reset points, in `src/session/session.cpp` (contract RR-1, RR-2 items 3 and 5, RR-3; data-model "The reset unit: new callers").
  - **Order.** In each of A, L, B and S: the reply, if one is owed; then record `Disconnected`; then the thread's cancellation state is reset (`reset_cancellation_state`). Then, with no suspension and no callback between, the marker moves to `reached` and `run_reset_unit_(1, 1)` starts. Then `reset_cancellation_state(enable_total_cancellation{})` runs; a manager or store error is logged (RT-4). For L only, `logout_confirmed_` and phase 1's wake follow.
  - **Arm S**'s 35=5 branch ahead of `interpret_logon`, with its `authorize` call, runs only with rol set (contract RR-1 "Arm S's branch and the knob"); with rol off the frame takes the base's `interpret_logon` refusal. With rol set it resets only once an mTLS session's live identity is authorised for the CompID; an unauthorised one draws the gate's event and no reset. Either way it records `Disconnected`, returns success and sends no Logout.
  - **The bound's one write.** In `acknowledge_peer_logout_` with the 103 mark `acking`, the bound moves the marker `none` → `reached`. `run_peer_logout_bound_` writes no marker before its reset point.
  - **Debug assert** at each reset point, for the values RR-2 item 5 allows: a pump frame finds `pending` (it runs the unit) or `reached` (it runs no unit); the bound finds `none`.
  - Update the "024 T013" comments in `on_inbound_active_logout_` and `on_inbound_active_too_high_logout_` to name 104.
  - Mutants, run at T052:
    - R-1c, R-1d and R-1e: delete L's, A's and B's reset point in turn.
    - R-1d's second, and R-1p's and R-1q's: move the `Disconnected` write after the unit.
    - R-1f(2a): the bound's reset point moves the marker only from `pending`.
    - R-1o's, one per arm: delete S's reset; skip S's authorisation test; run S's branch whatever the knob (read by the rol-off arm's event assertion); invoke a reset point from the NotConnected arm.
    - R-1u: split the turn with a post between `reached` and `run_reset_unit_`.
    - R-1x(1): delete the cancellation-state reset, read by "exactly one `reset_to`".
    - R-1i: make the unit's failure fatal.
    - R-1n's per-parameter mutants.
    - R-1l(2): drop the arm's "already `reached` → no unit" test.
- [ ] T040 [US1] The marker assert's run and its known positive (plan phase 1, last bullets; quickstart §2):
  - Re-run T019's two selections in the Debug build, with each count non-zero. Readings: no Debug assert fires, at entry or at a reset point. Record which of R-1y and R-1z are still RED by assertion, since they depend on T044: at this head phase 1 still writes `reached` and the teardown predicate still reads `!= none` (T037's interim mapping), so a cell that asserts no teardown `reset` after `close()` stays RED until T044.
  - The known positive, in `$M` only: in the bound, set the marker to `pending` immediately before its reset-point assert. R-1y's bound must trip that assert, by its own message. If it does not, the selection cannot report a violation, and the run is not accepted.
- [ ] T041 [US1] **(orchestrator)** STOP gate for the marker assert. If any assert fired in T040, or its known positive did not fire, implementation stops, and the cell, the assert and the stack go to the owner.
- [ ] T042 [US1] Delete the emit-time trigger in `run_logout_phase1` (`src/session/session.cpp`), and reword its "site (a)" comment to name 104 (FR-002, RO-2). Mutant, run at T053: restore the set (R-1a, R-1b).
- [ ] T043 [US1] Apply T012's re-pin dispositions to the bounded pumps, each in its own file, before the poll lands. Each re-pinned cell stays GREEN at this head. Record.
- [ ] T044 [US1] `close()`'s wait, in `src/session/session.cpp` and `include/fixpp/session/session.hpp` (contract RR-2 item 4, RT-5; data-model "`Session::close`'s teardown"; design rulings 1 to 3).
  - **The wait.** It moves ahead of the teardown predicate and after the joins, so after 103's bound join. It is widened to "marker `pending` or `reset_unit_in_flight_`", and it is a poll by post. The poll needs `effective_clock_`, is bounded by `logon_timeout_ms`, and emits `session_event_close_reset_wait_expired` on expiry. It tests its condition before its first post.
  - **The close-out.** A marker still `pending` after the wait is set to `reached`, with no reset under rol.
  - **The predicate** becomes `!teardown_reset_done_ && rod` only. Rewrite the teardown block's "Predicate" paragraph to name 104. It states the condition the omission rests on: the rol term for a receipt `reached` with its unit not started has no input, because the reset point is one strand turn (contract RR-2 item 3), and a change that splits that turn must add the term and a way to observe "unit not started" (contract RR-2 item 4; cell R-1u).
  - **Deletion.** `reset_unit_wake_` and its call in `run_reset_unit_` are deleted. Re-derive the users with `git grep -n reset_unit_wake_ -- src include tests`. `run_logout_phase1`'s comment that names it as "the pattern close() uses" is reworded.
  - Mutants, run at T054:
    - R-1j and R-1s: move the wait back inside the predicate block.
    - R-1k: drop the `pending` term.
    - R-1l(1): reset a `pending` receipt at the teardown.
    - R-1w: restore 093's event wait, widened to `pending`.
    - R-1f(2b): move 103's bound join to immediately after `seqnum_mgr_.drain()`, before `close_result_` is set.
    - R-2: skip the teardown reset when the receipt reset ran.
- [ ] T045 [US1] RR-4's boundaries and the reconnect clear, in `src/session/session.cpp` (contract RR-4, RR-2 item 6).
  - `store_then_emit`, before its store call: refuse while the marker is `reached`, take the number back as D-OUT-1 does, and return `dispatch_aborted`.
  - `persist_inbound_advance_`, after its `store_is_persistent_` return and before its store call: return success with no write.
  - `install_reconnected_transport` sets the marker to `none`.
  - Each boundary is one byte test.
  - Mutants, run at T055: R-1r's two (delete the test in `store_then_emit`; delete it in `persist_inbound_advance_`), R-1l(3) (drop the `persist_inbound_advance_` test) and R-1h (no `none` at `install_reconnected_transport`).
- [ ] T046 [US1] The remaining comments that state the old rule (FR-006, research D8 "Code comments"). Each names 104 and states a condition and a recipe:
  - `drive_reconnect`'s latch paragraph in `include/fixpp/session/session.hpp` (the latches stay set across a reconnect: `logout_seen_` no longer does);
  - `reset_on_logout` and `reset_on_disconnect` in `include/fixpp/session/session_config.hpp` (the receipt rule; "in EITHER direction" is removed);
  - every other T004 kept hit of the first three key rows in `src/` and `include/`.

  Run `check-comment-claims.py` on the batch.
- [ ] T047 [US1] D8's measured half for D1 (research D8 step 4). In `$M` at `B0`, apply D1 alone as a diff, not as cherry-picked commits: `git diff bf294f00 <the commit after T046> -- src include`, applied to `B0`. That is D1 plus the inert asserts and seam; it includes T037's enum, and it does not conflict with T016. Under the sandbox lock, run quickstart §1's `ctest --preset linux-clang-debug` lines for `-L session`, `-L capi`, `-L config` and `-R '^session_tc_'`, each with `--no-tests=error` added and its test count recorded (0 fails the step), plus the interop entries. Every `fixpp_add_interop_test` registration carries the label `interop` (its `LABEL` list, applied as `IT_LABEL` in `tests/interop/CMakeLists.txt`): list them with `ctest --preset linux-clang-debug -N -L interop`, a non-zero count, and run `-L interop --no-tests=error`. An entry that needs an external peer and is skipped is recorded as skipped. Each failing cell must already be a T004 kept hit. `ResetOnLifecycleTest.ResetOnLogout_LocalInitiated_Resets` must fail: it is the known positive. A measured run whose known positive did not fail is void: it is recorded as void, the cause is found, and the run is repeated; its zero failing cells are never read. A failing cell that is not a kept hit means a key is missing, and the key is added to T004 before the census is accepted.
- [ ] T048 [US1] Re-pins in `tests/session/test_reset_on_lifecycle.cpp` (research D8, "Base reading of the cells"):
  - `ResetOnLogout_LocalInitiated_Resets` is renamed for the answered close. It feeds a peer Logout in LogoutSent and asserts the reset at the reset point. Its unanswered half moves to R-1a.
  - `ResetOnLogout_BothRoles_Resets`' initiator arm gets the same change.
  - The comments of `ResetOnLogoutAndDisconnect_DoubleTrigger_OneStoreReset` ("which sets logout_seen_=true") and `ResetOnLogout_PeerInitiated_Resets` (the teardown) are reworded. Their assertions are unchanged.

  Run `ctest --preset linux-clang-debug -R '^session_reset_on_lifecycle$' --no-tests=error`, GREEN with a non-zero count. Record.
- [ ] T049 [US1] The other D1 kept hits.
  - Re-aim `InboundFrameDispositionsOd29.ATerminalCloseAfterPhase1ResolvedJoinsWithNoEarlyCut` in `tests/session/inbound_frame_dispositions_test.cpp` to `reset_on_disconnect`.
  - Reword the comment above `GracefulLogoutStoreCancelled.ByTheGraceTimer_ResetOnLogout_NoResetForALogoutThatWasNotSent` in `tests/session/test_logout_not_sent.cpp` to "a local graceful close resets only when the peer's Logout is received (104, B-104-1)", with the renamed cell's name.
  - Handle `CallbackSend.ToAdminOfGracefulCloseStartsClose_Terminal_ResetOnLogout*`'s comments as T004 dispositioned them.
  - Handle every other D1 kept hit of T004 and T047.

  Run each entry by `-R '^<entry>$' --no-tests=error`, each count non-zero. Record.
- [ ] T050 [US1] US1 GREEN.
  - Every Phase 3 cell, with the same non-zero count as at T036; `ctest -L 104 --no-tests=error`; and T048's and T049's entries, by `-R '^<entry>$' --no-tests=error`.
  - The SC-004 witnesses, unedited: `ResetOnLifecycleTest.ResetOnLogout_Off_Preserves`, `…_NextInitiatorLogon_Emits141` and `ResetOnDisconnect_*`; `ResetPolicyLogonInvariant103.*`; `Od26.CloseDrainingTheSeqnumMutexBeforeTheHydrateWritesNoState`; `GracefulLogoutStoreCancelled.*`.
  - T019's selection once more, with asserts silent.

  Run `codegraph sync`. Record.
- [ ] T051 [US1] Mutants for T038 (protocol), each RED by its named assertion with its forced hit GREEN:
  - R-1g, the clear only at the reset point;
  - R-1g, `LateParseFailure` clearing after `close()`;
  - R-1v;
  - R-1f(1);
  - R-1y;
  - R-1z, read in Debug and asserts-off;
  - R-1x's marker mutant, read by "the marker `reached`", with R-1x's reset-count assertions GREEN.
- [ ] T052 [US1] Mutants for T039 (protocol): R-1c; R-1d, both; R-1e; R-1f(2a); R-1o, one per arm; R-1p; R-1q; R-1u; R-1x(1), read by "exactly one `reset_to`"; R-3, by R-1d's first mutant, when T011 recorded R-3 drivable; R-1i; R-1n, one per parameter; R-1l(2).
- [ ] T053 [US1] Mutants for T042 (protocol): R-1a and R-1b. The answered twin stays GREEN.
- [ ] T054 [US1] Mutants for T044 (protocol): R-1j; R-1s; R-1k; R-1l(1); R-1w, the `FromAdminThrow` and `Storeless` arms, with `StuckPending` GREEN; R-1f(2b); R-2.
- [ ] T055 [US1] Mutants for T045 (protocol): R-1r, both boundaries; R-1l(3); R-1h.
- [ ] T056 [US1] Mutant for T033 and R-1t (protocol). Move step 4's `close(terminal)` loop in `Engine::stop()` to between step 2's close join and the step-3 mark, ahead of step 3's join. R-1t must go RED at its order assertion, as an assertion failure and not an abort. Deleting the join is not this mutant (INV-6).

**Checkpoint**: US1 GREEN; every US1 mutant RED; the MVP validation point.

---

## Phase 5: User Story 2, RED cells — an unasked 141=Y acknowledgement (Priority: P1) (plan phase 3)

**Goal**: U-1 to U-10 written and read on the base.

**Independent test**: U-1, U-2, U-4, U-5, U-8, U-9 (both arms) and U-10 RED on the base by assertion; U-3 GREEN (FR-010's pin); U-6 and U-7 unreachable on the base (a setup failure there, not RED evidence), their RED source their T071 mutant, with U-8 the clamp's base RED.

- [ ] T057 [US2] Create `tests/session/test_104_unasked_reset_ack.cpp`, and add it to `add_session_104_bucket(session_104_tests …)` in `tests/session/CMakeLists.txt`.
  - U-1, `UnaskedResetAck104.Lenient_HydratedN_ResetsAndActive`, uses a persistent store hydrated at in and out above 1 (contract U1). It asserts: Active; inbound next 2 and outbound next 1 in manager and store; `reset_to(2, 1)`; the event `by_peer_request = true`; no 35=A after fixpp's first; the next `send` is 34=1.
  - U-2, `…Unilateral_…`, makes the same assertions.
- [ ] T058 [US2] U-3 and U-4 in `tests/session/test_104_unasked_reset_ack.cpp`.
  - U-3, `…Strict_OwnReset_Unchanged`, has two arms: an acknowledgement with 141=Y at 34=1, and one at 34 = M > 1. It is GREEN on the base, as FR-010's pin.
  - U-4, `…AtMAbove1_NotConsumed_ResendOnNextFrame`, uses N = 7 and M = 5 (contract U2). It asserts Active, inbound 1 and outbound 1, and that the peer's 34 = 6 Heartbeat draws ResendRequest 7=1, 16=0.
- [ ] T059 [US2] U-5 and U-6 in `tests/session/test_104_unasked_reset_ack.cpp` (contract U3, U5).
  - U-5, `…With789_TooHighAfterReset_Logout`, sets `enable_next_expected_msg_seq_num`, and its acknowledgement carries 789 = X > 1 (contract U3). It asserts "NextExpectedMsgSeqNum too high, expecting 1 but received X", then `Disconnected`.
  - U-6, `…FollowUpResendRequest_GapFillOneOne`, runs after U-1's input. It feeds RR(34=2, 7=1, 16=0) and asserts exactly one GapFill (34=1, 43=Y, 122 equal to its own 52, 123=Y, 36=1; contract U5), then inbound next 3, then a next `send` at 34=1. It is unreachable on the base (U-1's input ends Disconnected there); its RED source is its T071 mutant.
- [ ] T060 [US2] U-7 and U-8 in `tests/session/test_104_unasked_reset_ack.cpp` (D3.3a, contract U6).
  - U-7, `…FollowUpResendRequest_ExplicitEnd_Clamped`, has two arms: FIX.4.1's 999999 and a FIX.4.4 explicit end. It asserts NewSeqNo = 1. It is unreachable on the base by this route, as U-6; its RED source is its T071 mutant, and U-8 is the clamp's base RED.
  - U-8, `ResendAnswer104.ExplicitEndAboveNextOut_NoOvershoot`, runs in Active with no reset and next outbound 5. RR(7=5, 16=10) must give GapFill 34=5, 36=5; RR(7=10, 16=20) must give GapFill 34=10, 36=5.
- [ ] T061 [US2] U-9 and U-10 in `tests/session/test_104_unasked_reset_ack.cpp` (contract U4m, U7; clarification N-8).
  - U-9, `UnaskedResetAck104.Mtls_Unauthorised_NoReset`, uses a direct mTLS initiator with an unauthorised identity, and an acknowledgement with 141=Y at 34=1. It asserts: no `reset_to`; the hydrated numbers unchanged; `Disconnected`; the authorisation-failed event. Its forced-hit arm is the authorised twin, which records `reset_to(2, 1)`.
  - U-9 is RED on the base, both arms: the base's Guard (4) `check_inbound` ends the acknowledgement as too low before the mTLS gate, so there is no authorisation-failed event and no `reset_to(2, 1)`. The event assertion also witnesses the N-8 order (the gate before the unasked unit).
  - U-10, `UnaskedResetAck104.PersistentStoreRefusesReset_Fatal`, per research D10's row: a lenient initiator on a persistent StoreDouble hydrated above 1, `fail_next_reset()` set, and an acknowledgement with 141=Y at 34=1. It asserts 030's persistent-fatal ending: one reset call on the store (`reset_call_count`), the store's error returned by the arm, `Disconnected` and never Active, no frame after fixpp's Logon, and the store's numbers at their hydrated values. Its forced-hit arm is the same double without the failure, which goes Active. It is RED on the base, where the store sees no reset call.
- [ ] T062 [US2] RED reading of T057 to T061 (Execution rules "Base and RED"). Record each count and each failing assertion. The exceptions, arm by arm: U-3's two arms are GREEN (FR-010's pin); U-6 and U-7 fail at their setup on the base, which is recorded with its cause and is not RED evidence (their RED source is their T071 mutant, and U-8 is the clamp's base RED). Every other cell and arm, U-9's two and U-10's included, is RED by assertion. The diff rule is T062's (Execution rules "Base and RED").

**Checkpoint**: US2's cells are RED or recorded pins.

---

## Phase 6: User Story 2, the change (Priority: P1) (plan phase 4; research D3, D4, D3.3a)

**Goal**: in LogonSent, an unasked 141=Y acknowledgement is a reset before the sequence check. At 34=1 the session continues at (2, 1); at 34 = M > 1 it is not consumed (O-2). The unit runs after the mTLS gate. A ResendRequest's EndSeqNo at or above the next outbound number is read as through-current.

**Independent test**: U-1 to U-10 GREEN; W8 and the 027 walk cell re-pinned GREEN; the mutants of T070 and T071 RED.

- [ ] T063 [US2] The 141 branch in `on_inbound_logon_sent_` (`src/session/session.cpp`; data-model "LogonSent acknowledgement: the 141 branch").
  - `peer_ack_sent_reset_flag` is read before Guard (4). `own_logon_sent_reset_flag_` is read there and stays one-shot cleared where it is today.
  - **Unasked** (`true`/`false`): no `check_inbound`; `logon_inbound_advanced_init` = `seq == 1`; unit targets (2, 1) when the acknowledgement's 34 is 1, and (1, 1) otherwise, not consumed (D4, ruling O-2). The unit runs after the mTLS CompID-authorisation gate (N-8).
  - **789.** `honor_peer_next_expected_` is honoured after the reset (D3.2).
  - **Unchanged.** The `true`/`true` row and the `false` row. The `false` row's too-low result is US3's.
  - The comment names 104 and states the rule.
  - Mutants, run at T070:
    - U-1 and U-2: restore `check_inbound` before the 141 read.
    - U-3: apply the unasked branch to the asked case.
    - U-4: set "consumed" for any M.
    - U-5: skip `honor_peer_next_expected_` on the unasked branch.
    - U-9: run the unit before the gate.
    - U-10: on the unasked branch, log a persistent store's refusal and continue (024 I-07's volatile disposition).
- [ ] T064 [US2] D3.3a's end model in `replay_outbound_range_` (`src/session/session.cpp`; data-model "`replay_outbound_range_`: the end model").
  - Inside the walk, after the gate's shared-hold wait and the store tail read, `end_is_through_current` becomes `end_is_through_current || requested_end >= next`. `next` is the tail read's next outbound number, or `peek_outbound()` when there is no store.
  - The callers are unchanged.
  - The end-model comment names 104 and supersedes 027 C3's explicit-end case by name. It keeps the formula and gives no example values.
  - Mutants, run at T071: U-7 and U-8 (drop the clamp); U-6 (answer an empty range with nothing).
- [ ] T065 [US2] D8's measured half for D3 + D3.3a. In `$M` at `B0`, apply T063 and T064's `src`/`include` diff with `git apply -3`. D3's edits to `on_inbound_logon_sent_` sit on code that D1 restructured (arm S), so resolve any conflict in the scratch copy, keeping only D3's and D3.3a's lines, and record each resolution in the evidence file. Show the scratch copy holds only the story's lines: `git -C "$M" diff bf294f00 -- src include` is compared hunk by hunk with `git diff <T063's parent> <T064's commit> -- src include`, and a line in the first that is not in the second voids the run. Then run T047's command set under the sandbox lock; T047's void rule applies.
  - The known positives must fail: `WalkExtraction.TwoValueEnd_ExplicitEndBeyondStore`, and a scratch cell sending RR(7=5, 16=10) at next outbound 5.
  - Each failing cell must be a T004 or T005 kept hit, or a key is added.
- [ ] T066 [US2] Re-pin W8 in `tests/session/test_persistent_seqnum_hydrate.cpp`: `PersistentSeqnumHydrate.W8_HydratedInitiator_ResetOnLogout_PeerSpontaneous141Y` gains an inbound-next-1 assertion (U-4's rule). Its comments ("in-sequence", "seq=1 would be / too-low → fatal") are rewritten to state the rule, that an unasked 141=Y acknowledgement is compared with 1, not the old outcome. Run its entry by `-R '^<entry>$' --no-tests=error`. Record.
- [ ] T067 [US2] Re-pin 027 C3's cell in `tests/session/test_next_expected_msgseqnum.cpp`. `WalkExtraction.TwoValueEnd_ExplicitEndBeyondStore` moves from `NewSeqNo=21` to `NewSeqNo=6`, with QuickFIX C++'s GapFill(34=10, 36=6) as the rationale and 027 C3 superseded by name. The file header's statement of 21 is rewritten as the rule. `…_789Caller` stays unaffected; confirm that from T004. Run its entry by `-R '^<entry>$' --no-tests=error`. Record.
- [ ] T068 [US2] The other D3 and D3.3a kept hits of T004, T005 and T065, including `tests/session/test_recovery_admin_span_gapfill.cpp` as T005 dispositioned it. Each is re-pinned or reworded, and its entry is run by the Execution rules' selection form. Record.
- [ ] T069 [US2] US2 GREEN: U-1 to U-10 with their T062 counts; `ctest -L 104 --no-tests=error`; T066 to T068's entries; the existing `Initiator_Received141Ack_*` cells (US2 AS3); `ResetSeqnumPolicyMatrixTest.PeerSpontaneous_Initiator_NonReset_Logon_AtNGt1_OutboundUnchanged`. Run `codegraph sync`. Record.
- [ ] T070 [US2] Mutants for T063 (protocol): U-1, U-2, U-3, U-4, U-5, U-9 with its authorised twin GREEN, and U-10 with its forced-hit arm GREEN.
- [ ] T071 [US2] Mutants for T064 (protocol): U-6, U-7, U-8.

**Checkpoint**: US2 GREEN; every US2 mutant RED.

---

## Phase 7: User Story 3, RED cells — the too-low Logout in a Logon arm (Priority: P2) (plan phase 5)

**Goal**: T-1 to T-6 written and read on the base.

**Independent test**: T-1 to T-5 RED on the base, since the base writes no 35=5, except T-4a's L + 1 arm (a pin); T-6's identity arms RED on the base, since the base ends a too-low frame before the identity gates; T-6's SendingTime and strict arms GREEN, as pins.

- [ ] T072 [US3] Create `tests/session/test_104_logon_too_low.cpp`, and add it to `session_104_tests` in `tests/session/CMakeLists.txt` (contract T1, T2).
  - T-1, `LogonTooLow104.Initiator_LogoutWithText`, uses NextNumIn 5 and an acknowledgement at 34=3.
  - T-2, `…Acceptor_LogoutWithText`, uses a hydrated NextNumIn of 5 and a Logon at 34=3.
  - T-3, `…PossDup_LogoutWithText`, runs both arms with 43=Y (D6).
  - Each asserts: exactly one 35=5; 58 byte-equal to the text; no 1409 and no 789; 34 = fixpp's next outbound; NextNumIn 5 in manager and store; `toAdmin(Logout)` called once; `Disconnected`.
- [ ] T073 [US3] T-4a to T-4f in `tests/session/test_104_logon_too_low.cpp`, each arm (contract T3 to T6; research D5's table).
  - `…Overflow_PairedWithNearBound` uses CompIDs of length L, the longest for which the Logout still fits, and L + 1. L is the forced hit.
  - `…ToAdminThrow`.
  - `…AssignFailure`, with outbound at `seqnum_max` through the test seam.
  - `…PersistentStoreFailure`.
  - `…WriteFailure`.
  - `…VolatileStoreFailure` (T-4f): a store double whose factory's `yields_persistent_store()` is false refuses the Logout's store. One 35=5 with the text is on the wire (024 I-07), then `Disconnected`, success.
- [ ] T074 [US3] T-5, `…ResetOnLogout_NoResetForTooLowLogout`, in `tests/session/test_104_logon_too_low.cpp` (FR-015, contract T7). It is an initiator with rol. It asserts one 35=5 with T-1's text and one `toAdmin(Logout)`; then, after `close(terminal)`, no `reset_to` and no `reset`, on T013's record.
  - The forced-hit arm uses the same store double and fixture. A Logout received in LogonSent (arm S) records one `reset_to`.
- [ ] T075 [US3] T-6, `…Precedence_*`, in `tests/session/test_104_logon_too_low.cpp` (contract T8, "Precedence on one Logon frame"; FIX-TC 1S(c)). Its arms, each with a too-low frame:
  - with a bad SendingTime;
  - on a strict acceptor without 141;
  - on an mTLS acceptor with an off-list identity;
  - on an acceptor whose credentials `authorize_logon` refuses;
  - on a direct mTLS initiator with an unauthorised identity.

  Each identity arm asserts no frame and the gate's own ending: the authorisation-failed event on the two mTLS arms, and on the credentials arm, which emits no event, a counting refusing callable installed with `compid_authorization_policy::set_logon_validator`, called once.
  - The identity arms are RED on the base: the base's `check_inbound` ends a too-low frame silently before the gates (`authorize` follows it in both Logon arms, and `authorize_logon` on the acceptor), so no event is emitted and the validator is never called. Each also witnesses the N-8 order (the gates before the too-low Logout).
  - The SendingTime and strict arms are GREEN on the base, as pins: both guards run before `check_inbound`.
- [ ] T076 [US3] RED reading of T072 to T075 (Execution rules "Base and RED"). Record each count and each failing assertion. The exceptions, arm by arm, are GREEN on the base as pins, each with its reason and its RED source: T-4a's L + 1 arm (the base writes no frame either; T082's larger-buffer mutant), T-6's SendingTime arm (its guard runs before `check_inbound`; T083's SendingTime-reorder mutant) and T-6's strict arm (likewise; T083's strict-reorder mutant). Every other cell and arm, T-6's identity arms included, is RED by assertion. The diff rule is T076's (Execution rules "Base and RED").

**Checkpoint**: US3's cells are RED or recorded pins.

---

## Phase 8: User Story 3, the change (Priority: P2) (plan phase 6; research D5, D6)

**Goal**: in NotConnected and LogonSent, once the identity gates pass, a too-low frame draws "MsgSeqNum too low, expecting N but received M", whatever its 43; then `Disconnected`.

**Independent test**: T-1 to T-6 GREEN; `*TooLowLogout103.*` byte-identical; the mutants of T082 and T083 RED.

- [ ] T077 [US3] The shared text builder in `src/session/session.cpp` (data-model "The too-low Logout text").
  - It is one file-local builder writing into a fixed `std::array<char, K>`, with the numbers written by `std::to_chars`. K is fixed by Arm B's existing `static_assert`.
  - 103's Arm B is switched to it, byte-identical.
  - The pin: `session_103_tests --gtest_filter='*TooLowLogout103.*'` is GREEN before and after the switch, with the same non-zero count. Record.
- [ ] T078 [US3] The too-low branches of both Logon arms, in `src/session/session.cpp`: `on_inbound_logon_sent_` (the `false` row of T063's table) and `on_inbound_not_connected_` (contract T1, T2, T8; FR-012 to FR-014).
  - **Order.** For a too-low frame the arm first runs the identity gates, through the helpers that make today's checks: the mTLS CompID-authorisation gate in both arms, and `authorize_logon` on the acceptor. The event, the one-shot `live_peer_id_` consumption and the silent `Disconnected` stay identical. Only then does it call `refuse_logon_with_logout_` with the builder's text, whatever the frame's 43, with no 1409 and no 789, and with NextNumIn not advanced.
  - **Unchanged.** The earlier guards keep their endings: the validate gate, `interpret_logon`, posture, SendingTime, `seq == 0`, and in NotConnected the strict refusal and the `reset_on_logon` reset.
  - Neither arm's too-low branch sets the receipt marker.
  - The comment names 104 and supersedes 103's spec row "A too-low frame in NotConnected or LogonSent still ends silently" by name.
  - Mutants, run at T082 and T083:
    - T-1 and T-2: remove the helper call.
    - T-3: add a 43=Y exemption.
    - T-4a: remove the helper call; or build into a larger buffer.
    - T-4b: remove the helper call; or send despite the throw.
    - T-4c: discard the assign error.
    - T-4d: transmit despite the store failure.
    - T-4e: propagate the write failure as the arm's return.
    - T-4f: remove the helper call.
    - T-5: invoke a reset point from `refuse_logon_with_logout_` after its emit.
    - T-6: move the too-low branch ahead of the SendingTime guard; or ahead of the strict refusal; or send the Logout before the identity gates.
- [ ] T079 [US3] D8's measured half for D5. In `$M` at `B0`, apply T077 and T078's `src`/`include` diff with `git apply -3`. D5's edits sit on Logon-arm code that D1 and D3 restructured, so resolve any conflict in the scratch copy, keeping only D5's lines, and record each resolution in the evidence file. Show the scratch copy holds only the story's lines, as T065 does, against `git diff <T077's parent> <T078's commit> -- src include`. Then run T047's command set under the sandbox lock; T047's void rule applies.
  - The known positive must flip: a scratch cell feeding a too-low acknowledgement.
  - Each failing cell must be a T004 kept hit of the last key row.
  - Read the base-reading leads: `UnparseableFrameDisposition.D6Edge_*`, `DISABLED_TransportInteropConformance.TC009_LogonSeqNumTooLow` and `TcSeqnumTest.Tc2c_MsgSeqNumTooLow`, which is unchanged.
- [ ] T080 [US3] The D5 kept hits of T004 and T079. Each is re-pinned or reworded, and its entry is run by the Execution rules' selection form. Record.
- [ ] T081 [US3] US3 GREEN: T-1 to T-6 with their T076 counts; `ctest -L 104 --no-tests=error`; `*TooLowLogout103.*`; the conformance entries (`-R '^session_tc_' --no-tests=error`); T080's entries. Run `codegraph sync`. Record.
- [ ] T082 [US3] Mutants for T078's T-1 to T-4f rows (protocol). Each overflow, throw and assign row is RED by its named assertion, with its forced hit GREEN.
- [ ] T083 [US3] Mutants for T078's T-5 and T-6 rows (protocol), with T-5's arm-S forced hit GREEN.

**Checkpoint**: all three behavioural stories GREEN; every behavioural FR has its cell and its mutant recorded.

---

## Phase 9: Whole-feature verification (plan phases 7 and 8)

**Purpose**: the local runs Article XVII §7, Article IX §1 and §2, and Article VIII §3 require, one at a time, before the first labelled push.

- [ ] T084 linux-clang-debug build in `$T`, incremental, under the flock, followed by the targeted ctest. The entries are: `-L 104 --no-tests=error`; `session_103_tests`; `session_reset_on_lifecycle`; `session_inbound_frame_dispositions`; the entries of `test_logout_not_sent.cpp`, `test_next_expected_msgseqnum.cpp`, `test_persistent_seqnum_hydrate.cpp` and `engine_reset_unit_stop_test.cpp`; the entries T043, T068 and T080 touched; `-R '^session_tc_'`; `-L capi`; `-L config`; and the `tests/alloc_guard/` entries, each run under `tools/check_alloc.py` as its registration says, with a reading of whether any reaches `store_then_emit` or the LogonSent arm (plan Constitution Check, XV §1 row). Each selection carries `--no-tests=error`, and each count is recorded and non-zero. Re-derive the entry list from `git diff --name-only origin/main..HEAD -- tests`, mapped to the CMakeLists. Record.
- [ ] T085 Sanitizers (`[const §IX.2]`): ASan, UBSan and TSan builds on `/mnt/wsl/fixppbuild` of `session_104_tests`, `session_103_tests` and the re-pinned targets of T084, each run through `ctest --preset <lane> -R '^<entry>$' --no-tests=error` (the Execution rules' selection form, with the lane's preset), one lane at a time. A finding is a defect until disproven (project rule). Record.
- [ ] T086 Size, layout and C-ABI checks (research D10 "C ABI and size"; B-104-7):
  - `git grep -nE 'sizeof ?\([^)]*\b(Engine|Session)\b' -- tests include src` has no hit. Its known positive is the same pattern with `FieldRef`, which must hit.
  - `git grep -n reset_on_logout -- include/fix src/capi bindings` has no hit.
  - Re-derive each non-static data member that `git diff origin/main...HEAD -- include/fixpp/session/engine.hpp include/fixpp/session/session.hpp` adds or removes, for B-104-7's text.
  - A one-off size reading for B-104-7: `print sizeof(fixpp::session::Session)` and `print sizeof(fixpp::session::Engine)` in gdb, on one debug test binary built at `B0` in `$M` and on the same binary at the head. The values go to the evidence file only; T091 writes B-104-7's direction for `Session` from them. The headers carry `NDEBUG`-dependent members (re-derive: `grep -n NDEBUG include/fixpp/session/engine.hpp include/fixpp/session/session.hpp`), so the absolute sizes are Debug's and are never written into the row. Only the direction is, and it holds for Release because the members 104 adds and deletes are unconditional (re-read each in the `git diff` above for an `NDEBUG` guard; one that has one is read in a Release binary too).
  - Record.
- [ ] T087 The paired `session_send_bench` run (plan "Performance Goals", the Article VIII §3 row).
  - Create `$B` at `git merge-base HEAD origin/main`, computed now, with its path padded.
  - Release builds of `bench/session/session_send_bench.cpp`, one at a time. The candidate builds in `$T`'s `build/linux-clang-release`, a symlink into `/mnt/wsl/fixppbuild/build/` (`readlink -f` it first); the base builds in `$B/build/linux-clang-release`, a real directory on F:. Both are configured with `-DFIXPP_BUILD_BENCH=ON` (the option is in `cmake/ProjectOptions.cmake`; read `FIXPP_BUILD_BENCH` in each `CMakeCache.txt`).
  - Check that the source and build paths the two caches record (`CMAKE_HOME_DIRECTORY`, `CMAKE_CACHEFILE_DIR`) are equal in length, per the Execution rules' pad rule. A mismatch re-pads `$B` before any run.
  - The bench sends over a `MemoryStore` (`cfg.store_factory` in `bench/session/session_send_bench.cpp`), so every measured send passes RR-4's byte test before its store call; re-read that at this head.
  - Run them base against candidate on one machine, alternating A-B-A-B at least (Article VIII §2's method), over every benchmark row the binary reports, with the repetitions per run, the number of alternations and the command recorded in the evidence file. Reduce each tree by its minimum (§2's min-per-tree). A crashed, empty or uninformative run is a failure, never a pass (§2a).
  - **The decision rule (Article VIII §2).** A slowdown of at most +5 % is within budget. A slowdown beyond +5 % is not author-accepted: the PR then carries the paired measurement itself, a rationale, and approval by someone other than the author, or the owner's explicit ratification in the PR thread.
  - **Known positive, in `$M`.** A scratch edit that makes the byte test measurably expensive (a fixed busy loop beside it) must read as a slowdown in the same paired harness; otherwise the run cannot report a regression and is not accepted.
  - The results go to the evidence file, and the PR body points to that record (T098).
  - Record that `engine_send_bench` never reaches `store_then_emit`, and that `persist_inbound_advance_`'s test is not benched.
- [ ] T088 Every Tier-1 source-audit job locally, the list derived from `.github/workflows/tier1.yml` at this head. That means every `ci-script-pins` step and `ci/test-parallelism-verdict.sh`, plus clang-format, clang-tidy and cppcheck on the changed files (never on `specs/` or `include/fix/c_api/*.h`), and `tools/check_layers.py`. Also run the owner's standing pre-push items: a `linux-gcc-release` build of the touched targets, and the whole `pytest bindings/python/tests` suite. include-what-you-use is not repeated here: T102's `/speckit-verify` runs it on every touched file that has a compile command. Record.
- [ ] T089 The one full `ctest` (`[const §XVII.7]`), at the code head. First `cat /mnt/c/temp/fixpp/.sandbox-lock`, outside any peer's MSVC window, and `df -h /mnt/e`. Then build all targets incrementally in `$T`'s existing tree (never emptied), and run **one** full `ctest --test-dir build/linux-clang-debug`, detached under the flock with one marker waiter. Record the sha for the PR body's `local build: green on linux-clang-debug @ <sha>` line.
- [ ] T090 MSVC, before the first labelled push (the owner's pre-push MSVC rule: even under a targeted verify, the feature's own targets are built on MSVC with `-k 0` before the first labelled push; plan phase 8). `[const §IX.3]` is not cited, since no msvc-asan run is planned; it applies only if one is added. Take `.sandbox-lock` per Step 0 of PARENT `research/G19-fix-fpml-iso20022/msvc-local-build-procedure.md`; a lock held by someone else means ask the owner. Never run this alongside a Linux build.
  - rsync, then build with `-k 0` `session_104_tests`, `session_103_tests` and every target T084 lists. Every target that includes `engine.hpp` or `session.hpp`'s changed members is covered by those targets' closure.
  - Run `session_104_tests`, and record its reported test count, which must be non-zero (0 fails the step, as for a gtest filter). Release the lock at close-out.

**Checkpoint**: every local gate green at the code head; the full-ctest sha recorded.

---

## Phase 10: Records (plan phase 9)

**Purpose**: everything the bundle says must be written down. **A limitation is open only if it is in the live B&L file**; a grep across the live and closed pair is never the check. Merged bundles (`specs/024-*`, `specs/027-*`, `specs/093-*`, `specs/103-*`) are never edited; they are superseded by name.

- [ ] T091 [P] New B&L rows in `spec/behaviors-and-limitations.md`: B-104-1 to B-104-7, and L-104-1 to L-104-7, with the text of data-model "B&L delta". B-104-7's member list comes from T086. L-104-7 is marked source-derived and not live-traced (ruling O-1). L-104-3 cites the issue T097 files. Each new row ends with its evidence pointer in the live file's form, `*(FR-0nn; witness <Suite>.<Cell> in ctest <entry>)*`, taken from `## Requirement map`, whose row is its re-derivation recipe; a row with no cell says `no cell`, as L-103-5 does.
- [ ] T092 Amend in place, by name, in the live `spec/behaviors-and-limitations.md` (data-model "Amended in place"). The list below is the planned set; the amend set is T004's live-row kept hits. A live row T004 dispositions **reword** is amended here too, with the wording T004 recorded; a cited row T004 dispositions **unaffected** keeps its text (the cited rows to read for it include B-013-1's Active-only clause, L-103-5, B-093-10, `B-563-2`, `L-563-4` and `B-598-1`), with the reason in the evidence file:
  - B-024-1 points to B-104-1.
  - In B-538-7, "answered or not" is struck and points to B-104-1, and each other sentence data-model quotes is replaced as written there.
  - B-563-5: the rationale points to B-104-1, and its clause keeps its ending.
  - B-093-11 points to B-104-2.
  - L-093-12 points to B-104-2, and names the reply store and inbound persist.
  - B-103-9's disclosure is replaced as written there.
  - B-103-10: "In Active" becomes "In Active, and since 104 in the Logon arms (B-104-6)"; its title and its closing sentence citing `L-103-7` change as data-model writes.
- [ ] T093 Retire L-103-2 (resolved by B-104-4) and L-103-7 (resolved by B-104-6). Move both rows to `spec/behaviors-and-limitations-closed.md` with 104's resolution, in the closed file's form: a `<!-- L-103-n — closed: resolved by 104-sequence-reset-fidelity (FR-0nn, research Dn), <date>: <the resolution>; witnesses <Suite>.<Cell> in ctest <entry> -->` line above the moved row, as 103's L-029-3, and no stub left in the live file. In the closed row, L-103-7 supersedes 103's spec row "A too-low frame in NotConnected or LogonSent still ends silently" by name. L-103-2's resolution records the corrected engine reading, so the closed file does not keep the row's parity claim uncorrected: all three QuickFIX initiators accept the acknowledgement and reset both numbers (spec "Context & Problem" item 2; research D3 "Rationale"). Confirm both are gone from the live file by reading it. Then run the B&L checks 103's T151 ran, each with its pass condition: `python3 PARENT/.claude/scripts/check_bl_delta.py --self-test` first; `python3 PARENT/.claude/scripts/check_bl_delta.py gate --feature 104-sequence-reset-fidelity --root "$T" --base origin/main` at exit 0 only; `python3 PARENT/.claude/scripts/check_bl_citations.py gate` at exit 0, so that a retired ID still cited by a live row (B-103-10's `L-103-7`, until T092 changes it) is found.
- [ ] T094 [P] Brain pages (research D8, "B&L, brain and contract supersessions"; the project rule that a component page lists every document, wrong ones included), in `brain/components/graceful-logout.md`, `brain/components/session.md` and every page the recipe below finds:
  - **Find the pages by recipe (issue #334).** Every `brain/components/*.md` whose `refs` list a document 104 supersedes or restates (`command grep -l -e 'specs/024-' -e 'specs/027-' -e 'specs/093-' -e 'specs/103-' -e '536-537-538-recovery-liveness' -e '563-564-callback-send' -- brain/components/*.md`), or whose `codegraph_entry` names a function 104 changes (plan "Scale/Scope"; read each page's front matter). Read each page found and flag in place each statement of a superseded rule; a page with none is recorded as read. Known positives: `brain/components/message-store-quiescence.md` (its 141=Y unit section's "when it is about to issue its teardown reset") and `brain/components/session-liveness-and-reconnect.md` (its refs list 093's contract) must be in the set.
  - **Superseded by 104.** Each page flags, in place: 024 `contracts/reset-knobs.md` C3.1 (C5.1 is kept, read per teardown, E-4, so with both knobs set a received Logout makes two resets, B-104-3); 027 `contracts/next-expected-msgseqnum.md` C3's explicit-end case and its required test; 093 `contracts/inbound-frame-dispositions.md` C-6's `close()` bullet and its event-driven shape; B-538-7's clause; 103's latch disclosure; and every merged-bundle document outside a contract that T004 flagged, such as 024's and 027's data model, research, plan, tasks and quickstart, and 013's quickstart.
  - **`graceful-logout.md`.** Reword the invariant row "`logout_seen_` is written only at Logout-specific sites" and the R-24 bullet "…still resets: that case is fixpp#595" to the receipt rule.
  - **`session.md`.** Add a one-line pointer to 104 on the rejected-alternative bullet "(a graceful phase 1 sets `logout_seen_`)", which is the historical half and is not rewritten. Flag the bullet "`close()`'s wait is bounded by `logon_timeout_ms`, event-driven" as superseded by contract RR-2 item 4's poll: the flag names the shape only, and 093 OD-1's reason for rejecting a poll stays as the historical half, with a pointer to research D1 item 4, which answers it. Add a 104 section that routes to the bundle, in the shape of the "(103)" section.
- [ ] T095 D8 reconciliation at the head.
  - Re-run T004's static recipe. Every hit that still states an old rule as current is now re-pinned, reworded or superseded by name, as T004 dispositioned it.
  - Re-run the recipe at `B0` as well: every known positive must still hit there.
  - Every test whose expected outcome `git diff origin/main..HEAD -- tests` changes is a T004, T005, T047, T065 or T079 kept hit, or a 104 cell (SC-004).
  - A changed expectation outside them stops for a decision. Record.
- [ ] T096 Header-comment supersession pointers (issue #310; contract "Superseded by name").
  - The `logout_seen_` comment in `include/fixpp/session/session.hpp` is final. It states RO-1 to RO-3 as a condition plus the recipe, names 104, and supersedes 024 T013 / C3.1 by name.
  - Each file that implements a superseding rule names the superseded decision and 104 in its header comment: `src/session/session.cpp` (024 C3.1; 093 C-6's `close()` bullet; 027 C3's explicit-end case), `include/fixpp/session/session_config.hpp`, and `include/fixpp/session/engine.hpp` (the step-3 mark). So does each test file that re-pins a superseded rule's cell: `tests/session/test_next_expected_msgseqnum.cpp` (027 C3) and `tests/session/test_reset_on_lifecycle.cpp` (024 C3.1), plus `tests/support/hooked_store.hpp` for T013's record (104 only).
  - If this task touches `src`, `include` or `tests`, rebuild the debug tree and re-run T084's targeted entries.
- [ ] T097 **(orchestrator)** File a fixpp issue for L-104-3: the posture, strict and SendingTime refusals and the 1137 Reject still answer before the identity gates (FIX-TC 1S(c); DQ-6).
  - The body, as 103's T155 specified L-103-2's: the refusals that answer before the identity gates, per role (on an mTLS acceptor the posture, strict and SendingTime refusals and the 1137 Reject; on a direct mTLS initiator the posture and SendingTime Logouts); FIX-TC 1S(c)'s text, quoted from research D5; the configurations that reach it (an mTLS acceptor; a direct mTLS initiator; not an engine-managed initiator, which authorises at connect); QuickFIX's behaviour (no engine binds certificates to CompIDs, spec DQ-6); and L-104-3's ID. Grep the body for a closing keyword inside a negation before filing.
  - Confirm that `bug` and `follow-up` exist in `gh label list`, and file the issue with both labels at creation.
  - Record the number for T091's L-104-3 row and for the PR body.
- [ ] T098 Draft the PR description in the evidence file:
  - the closing set, fixpp#595, #695 and #701, by keyword;
  - the orchestrator rulings O-1 to O-3, E-1 to E-4, RU-1 to RU-3, N-8, D2 and D3.3a, plus the moved `close()` wait, for the owner's review;
  - B-104-7, the layout change with no C-ABI change;
  - the `local build` line from T089;
  - the issue from T097;
  - the departures, copied from spec.md's "Departures from QuickFIX" table, each DQ row with the B&L row that carries it (the row its text names; DQ-1, B-104-4; DQ-9 to DQ-14 and DQ-12a, B-104-1), as 103's T181 copied its table;
  - the INV-T residual, with the owner's ruling of 2026-11-18 as its authority: a suspension in INV-T's window shorter than the bound's wake is covered by the source reading only (plan phase 1, contract RR-2 INV-T), with T021's record;
  - a pointer to T087's paired `session_send_bench` record (Article VIII §2);
  - #266, referenced for FR-017's re-record and never with a closing keyword: the closing set is fixpp#595, #695 and #701 only.

  Grep the body and every commit message on the branch for a closing keyword inside a negation, the sentence that names #266 included. GitHub links it anyway, and the oracle is `closingIssuesReferences`.
- [ ] T099 Run `python3 PARENT/.claude/scripts/check-comment-claims.py --root "$T" --base origin/main` over the records batch (plan phase 9), and read by hand the added inventories and the runtime strings, the too-low text among them. Record.

**Checkpoint**: records match the code; L-103-2 and L-103-7 are only in the closed file.

---

## Phase 11: Polish, final head and mandatory close-out (plan phase 10)

**Purpose**: the gates that read the whole tree. Gate B, the PR and the labels are pipeline steps; Gate B runs after T107 and before Phase 12.

- [ ] T100 Peers, per the Execution rules' recipe, against `origin/main` now. If it moved, integrate it by a merge commit. Then re-run every census, cell or mutant task that read an overlapping file, and T084 to T090 as they apply.
- [ ] T101 **(orchestrator)** `/simplify` (pipeline step 11) over the implementation diff; the implementer applies the in-scope simplifications. If any code changes:
  - re-run the mutant tasks of the changed mechanism (T051 to T056, T070, T071, T082, T083);
  - re-run T084's targeted entries;
  - re-run T090's MSVC build of the touched targets.

  No second full `ctest` is added by default (owner: after a small late fix, targeted re-runs only). The PR's §XVII.7 line cites T089's sha, with the targeted runs at the later head in the evidence file. If the change goes beyond a local simplification, the orchestrator decides whether T089 re-runs.
- [ ] T102 **(orchestrator)** `/speckit-verify` (pipeline step 12) from `$T`, the main checkout, so its hardcoded root is right. It writes `.specify/decisions/104-sequence-reset-fidelity-verify.md`, a Tier-1 mirror that includes MSVC of the new targets and the coverage reading `[const §IX.1]` asks for (≥95 % line, ≥85 % branch on the touched modules; no silent uncovered error path).
- [ ] T103 Run `python3 PARENT/.claude/scripts/check-comment-claims.py --root "$T" --base origin/main` at the head, and read the added inventories and runtime strings by hand. Record.
- [ ] T104 Run `python3 tools/check_line_citations.py --self-test`, then `--range origin/main..HEAD`, then `--shift-audit origin/main..HEAD`. These charge this file and the bundle too. Record.
- [ ] T105 Run `codegraph sync` in `$T`. `codegraph status --json` must show `projectPath` = `$T` and a non-zero `fileCount`. Record.
- [ ] T106 [P] **Catalogue close-out** (mandatory, `[const §XVII.8]`). spec.md and plan.md name no catalogue row that 104 owns, so the rows are derived by recipe, never from a frozen list.
  - **Find the rows.** Search `spec/feature-catalogue.md` and read each hit. The OFFICIAL rows to update are those whose Spec ref is one of spec.md's Normative References keys (re-derive with `command grep -n -F '<§ or scenario>' spec/coverage-index.md` for each of FIX-SL §4.3.11, §4.4.2, §4.4.3, §4.6, §4.7.1, §4.8.1 and §4.10.2, and FIX-TC 1B, 1S, 2 and 13), plus those whose note names #595, #695, #701, B53, B107, `reset_on_logout`, L-103-2 or L-103-7.
  - **Update them.** Add 104's witnesses (cell names) and the PR reference to each row found. Flip a feature-owned row from `in-progress`/`backlog` to `done`. A shared row (one another feature also witnesses, FIX-TC 2c's and 13's among them) gains 104's witnesses and PR reference in its notes and keeps its status. Add 104's row only where the catalogue keeps one for a change of this kind (re-derive how 103 is recorded: `command grep -n '103-resend-logon-reset-fidelity' spec/feature-catalogue.md`), with its status `done` at merge, the witnesses of the Requirement map and the PR reference. Add the matching `spec/coverage-index.md` notes for those keys.
  - **The witness per key** (spec Normative References; research D10), each named in the notes column of its key: §4.3.11, T-1 and T-2; §4.4.2, U-1 and U-2; §4.4.3, U-1 (the unasked reset at Logon); §4.6, R-1a and R-1b; §4.7.1, T-1 (no 789, no 1409); §4.8.1, T-3 (43=Y) with T-1; §4.10.2, R-1c; FIX-TC 1B, T-1 for 1B(d) and R-1o for 1B(e); FIX-TC 1S, T-6's identity arms and R-1o's mTLS arm for 1S(c); FIX-TC 2, T-1 and T-2 for 2c; FIX-TC 13, R-1c and R-1d.
  - **If no row is feature-owned,** record "no feature-owned OFFICIAL row: the recipe above, run at `<sha>`, found only shared rows, each annotated" under `## Completeness` in `.specify/decisions/104-sequence-reset-fidelity-verify.md`, with the hits listed there. That section is the one `/gate-b` pre-flight 4d reads, with T107's verdict.
- [ ] T107 **Feature-completeness audit (MUST be the FINAL task of this phase and the last before Gate B)**. At the head being accepted, assert that:
  - (i) every row of this file is `[X]` or carries an explicit waiver rationale;
  - (ii) every FR-001 to FR-018 and SC-001 to SC-006 maps, through the Requirement map below, to a landed test and a landed implementation;
  - (iii) every feature-owned OFFICIAL catalogue row is `done` with a matching `coverage-index.md` entry (T106).

  Record the verdict, "100 %, with every waiver listed" (103's precedent, its T184 with T163 waived-pending), under `## Completeness` in `.specify/decisions/104-sequence-reset-fidelity-verify.md`, the section `/gate-b` pre-flight 4d reads. **Waived-pending:** T108 to T113 (US4, FR-017, SC-005) run after Gate B by design (plan phase 11; research D9; ruling O-3). Their waiver names T113 as its closing task, and the orchestrator closes it there. This is the hard `/gate-b` precondition (Article XVII §8, pre-flight 4d).

---

## Phase 12: User Story 4 — the #266 basis, re-recorded last (Priority: P3) (plan phase 11; research D9, sequence (a), ruling O-3)

**Goal**: the #266 report reads EVIDENCE again, from a basis read off a CI run at 104's head (FR-017, SC-005).

**Independent test**: the labelled `pull_request` run that includes the basis commit prints no DIAGNOSTIC ONLY for a count mismatch on any lane whose Test step succeeded.

- [ ] T108 [US4] **(orchestrator)** After Gate B's last fix, which is the last commit that can change a ctest registration:
  - **Base check first.** Re-run the Execution rules' peers recipe, and confirm that `origin/main` is an ancestor of `HEAD`. Then read, in each tier workflow, whether a `pull_request` run checks out the PR merge ref or the head sha. If it checks out the merge ref and `origin/main` has gained registrations since the merge-base, the dispatch counts cannot match the labelled run. An integration merge at that point is outside Gate B's reviewed range, so that case is a STOP to the orchestrator.
  - Check `git diff origin/main HEAD -- '*CMakeLists.txt' '*.cmake'`, filtered to D9's registration pattern, and read each hit: the registrations are the ones 104 adds.
  - Read each tier workflow's `if:` on its `ci/peak-memory-report.sh` step in `.github/workflows/tier1.yml`, `tier2.yml` and `tier3-libcxx.yml` (re-derive: `ls .github/workflows/tier*.yml`), and confirm that the step runs on `workflow_dispatch`. Record which lanes run the report at all (`git grep -n peak-memory-report -- .github/workflows`), the Windows lanes included: that set is SC-005's "every lane" (research D9 "Source").
  - Push the branch with no label change, and dispatch Tier 1, 2 and 3 (`workflow_dispatch`) at that head.
- [ ] T109 [US4] Read every lane's count off that dispatch run. On Linux it is the `ran:` value of the lane's `ci/peak-memory-report.sh` line, cross-checked against ctest's `… out of <N>` for the step it sampled; on Windows it is the suite step's `out of <N>`. Confirm that the same run printed the `DIAGNOSTIC ONLY` warning on every lane whose count differs from the basis file's current value, and on at least one lane: that is the known positive, showing the comparison can report a mismatch (research D9). No value is taken from this bundle or the spec. Record in the evidence file.
- [ ] T110 [US4] Write the basis files.
  - Write `ci/expected-eligible-tests.txt`, one `<preset> <count>` line per pinned lane.
  - Add a "RE-RECORD — 104" section to `ci/ctest-parallelism-probe.md`, above fixpp#675's section and in its format: basis runs, method, and the per-lane table.
  - Run `bash ci/test-parallelism-verdict.sh`, the harness the basis file's header requires (not `ci/test-peak-rss.sh`), and record its result.
  - Commit both files in one commit that touches no CMake file. `git diff <re-record>^ <re-record> -- '*CMakeLists.txt' '*.cmake'` must be empty. Do not push yet.
- [ ] T111 [US4] **(orchestrator)** The commit's disposition (`.specify/close-out.md` §1, "One line to settle in the Gate B record"). The Gate B record `.specify/decisions/104-sequence-reset-fidelity-gateb.md` and the verify record each get one sentence that names the basis commit. It says the commit is CI data only, deliberately outside the range Gate B reviewed and `/speckit-verify` recorded, and that neither is re-run for it, because it changes no code, test or registration (T110's checks and the verdict harness).
- [ ] T112 [US4] **(orchestrator)** The final sign-off order: **BODY → LABELS → wait ~2 min → PUSH LAST**.
  - Update the PR body, including the closing set and the `local build` line.
  - Apply `gate-a-done` and `gate-b-done` in one call, per `[const §XVII.8]`'s label evidence rule. T108 to T113 being waived-pending under `## Completeness` does not hold `gate-b-done` back: 103's precedent (T163 waived-pending there, verdict "100 %, with every waiver listed").
  - Wait about 2 min, then push the basis commit as the last push.
  - Never cancel a gate-label wave that looks like a duplicate.
- [ ] T113 [US4] **(orchestrator)** SC-005's acceptance on the labelled `pull_request` run that includes the basis commit: no `DIAGNOSTIC ONLY` for a count mismatch on any lane whose Test step succeeded. A lane whose Test step failed on that run does not discharge SC-005 by its exclusion: it is re-run, its tier being a required check, and SC-005 is read on its passing run. Record it in the verify record, and close T107's waiver under `## Completeness`, which brings the audit to 100 %.

---

## Requirement map

| Requirement | Cells (task) | Implementation (task) | Mutants (task) |
|---|---|---|---|
| FR-001 (receipt rule, reset points, `close()` and stop waits, one strand turn) | R-1c–R-1l, R-1n–R-1z, and R-3 when T011 reads it drivable (T017, T018, T023–T032, T034) | T038, T039, T044, T045, T033 | T051, T052, T054, T055, T056 |
| FR-002 (unanswered close keeps numbers) | R-1a, R-1b (T022) | T042 | T053 |
| FR-003 (LogoutSent response resets) | R-1c, R-1j (T023, T028) | T039 | T052 (R-1c) |
| FR-004 (rod unchanged; two resets with both knobs) | R-2 (T035); existing `ResetOnDisconnect_*` (T050) | T044 | T054 (R-2) |
| FR-005 (supersession by name; witnesses re-pinned) | census T004, T005, T047, T065, T079, T095; re-pins T048, T049, T066, T067, T068, T080 | T092, T093, T094, T096 | census known positives (T004, T047, T065, T079) |
| FR-006 (comments naming 104) | `check-comment-claims` (T046, T099, T103) | T037, T039, T042, T044, T046, T064, T078, T096 | none (documentation) |
| FR-007 (141=Y recognised before the comparison) | U-1, U-2 (T057) | T063 | T070 |
| FR-008 (unasked reset, no second Logon, follow-up answer) | U-1, U-2, U-4, U-5, U-6, U-7, U-8 (T057–T060) | T063, T064 | T070, T071 |
| FR-009 (durable, its order against `close()` and its failure handling; unit after the mTLS gate) | U-1 persistent store, U-9, U-10 (T057, T061); R-1s, `close()` waiting for a Logon arm's unit (T028) | T063, T044 | T070 (U-9, U-10), T054 (R-1s) |
| FR-010 (strict unchanged) | U-3 (T058); `Initiator_Received141Ack_*` (T069) | T063 (unchanged rows) | T070 (U-3) |
| FR-011 (L-103-2 retired) | — | T093 | none (records) |
| FR-012 (too-low Logout after identity gates, any 43) | T-1, T-2, T-3, T-6 (T072, T075) | T077, T078 | T082, T083 |
| FR-013 (numbering, no 1409) | T-1, T-2 (T072) | T078 | T082 |
| FR-014 (Logout that cannot be sent) | T-4a–T-4f (T073) | T078 | T082 |
| FR-015 (too-low Logout not a rol trigger) | T-5 (T074) | T078 | T083 |
| FR-016 (L-103-7 retired) | — | T093 | none (records) |
| FR-017 (#266 basis) | dispatch run T108, T109 | T110 | known positive: DIAGNOSTIC ONLY on the dispatch run (T109) |
| FR-018 (B&L rows; one departures list) | — (records; each B&L row's own witness is its FR's cell, and DQ-4's behaviour is FR-008's U-5) | T091, T092, T093, T098 (the departures copied from the spec's table) | none (records) |
| SC-001 | R-1a, R-1b (T022) | T042 | T053 |
| SC-002 | U-1, U-2 (T057) | T063 | T070 |
| SC-003 | T-1, T-2 (T072) | T078 | T082 |
| SC-004 | the unedited witnesses (T050, T069, T081); T095's changed-expectation check | — | — |
| SC-005 | T113 | T110 | T109 (known positive) |
| SC-006 | every mutant task; forced-hit arms in every "no reset" cell | — | T014, T020, T040, T051–T056, T070, T071, T082, T083 |
| Article VIII §3 | paired `session_send_bench` (T087) | T045 | — |

## Dependencies and execution order

- **Pipeline ahead of Phase 1**: `/speckit-analyze`, `/speckit-checklist` and `/speckit-checklist-audit` (`.specify/pipeline.md` steps 6, 7 and 9). The audit blocks implementation.
- **Phase order**: 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10 → 11 → Gate B → 12. Every production task edits `src/session/session.cpp` or `include/fixpp/session/session.hpp`, so the stories run in this order, never in parallel.
- **STOP gates**: T007 (D7), T010 (R1 store order), T021 (the settling experiment) and T041 (the marker assert). Each one that fires halts every later task until the owner rules.
- **Within US1**: T013 and T014 come before any cell that reads the record. T016 comes before T019, then T019 → T020 → T021, then the remaining cells. T033 comes before T034. T011 comes before T025, which writes R-3 only on T011's "drivable" reading. T036 comes before T037. T039 → T040 → T041. T043 comes before T044. T047 comes before T048 and T049. The mechanisms land before T050, and the mutants T051 to T056 follow T050.
- **Records**: T097 → T091, because L-104-3 cites the issue T097 files.
- **Cross-story**: US3's T074 uses T013's record and US1's arm S (T039). US2's U-6 to U-8 need only T064. T063's `false` row is completed by US3's T078.
- **FR-017 vs the final audit**: `/gate-b` hard-blocks without T107's record, and FR-017 runs only after Gate B (ruling O-3). So T107 is the last task before Gate B, with T108 to T113 waived-pending. T113 closes that waiver after the labelled acceptance run, and T112's push is the last push.

### Parallel opportunities

- Phase 2's read-only censuses T004, T005, T006, T008, T011 and T012 touch no file but the evidence file, so they run in parallel; their evidence sections are merged by the implementer.
- Cell tasks within one story share one test file and run sequentially.
- In Phase 10, T091 and T094 run in parallel (different files). T092 and T093 follow T091 in the same B&L file.
- No two builds run at once (Execution rules), so a parallel task never builds alongside another.

### Parallel examples

```bash
# Phase 2, after T003 (read-only censuses):
Task: "T004 D8 static census with known positives"
Task: "T006 D7 acceptor-twin greps"
Task: "T008 R1 inline-grant reading"
Task: "T011 R-1r route reading"
Task: "T012 R2 close() pump census"

# Phase 10, after T090:
Task: "T091 new B&L rows in spec/behaviors-and-limitations.md"
Task: "T094 brain/components/{graceful-logout,session}.md flags"
```

## Implementation strategy

- **Who executes.** A `phase-implementer` agent executes the implementer's tasks. The orchestrator runs the **(orchestrator)** tasks and commits only its own records. A report that leaves a task unfinished says which and why.
- **MVP first**: Phases 1 to 4 deliver US1 (#595), the P1 defect on the default close path. **STOP and VALIDATE** at Phase 4's checkpoint. The feature still merges whole.
- **Incremental**: US2 (Phases 5 and 6), then US3 (Phases 7 and 8). Each story is GREEN with its mutants before the next one starts, then verification, records, polish, Gate B, and US4 last.
- **Placement decisions** taken here, for the orchestrator to review:
  1. D8's measured half runs once per mechanism (T047, T065, T079), as a scratch draft on `B0` in `$M`, after the mechanism exists and before that story's re-pins. Plan phase 0 lists it, but it cannot run before a draft of the change exists.
  2. HookedStore's record (T013) and its known-positive cell (T014, which registers the bucket) are Foundational. D10 asks for them "in phase 1, before any cell that needs it".
  3. The marker accessor (T037) is a `session_test_access` static function. The bundle needs it (R-1x reads "the marker `reached`") but does not name it.
  4. The completeness audit is final before Gate B, with US4 waived-pending (T107, T113).
  5. `$T`'s debug tree is on E: (Execution rules, "Builds").
  6. Research D10 has no cell R-1m; none is invented.

## Notes (hard rules)

- Write recipes, not results: no pasted counts, no line numbers as pins, and no example values in code comments. Cite symbols.
- Never edit a merged feature's bundle; supersede it by name on its component page and in the implementing file's header comment.
- A gate result is scoped to the commits that existed when it ran: re-run T103 and T104 as the last actions before any labelled push.
