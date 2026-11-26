---
description: "Task list for 106-logon-guards-write-bounds"
---

# Tasks: Logon guards before any answer, a time limit on every write, a configurable initiator handshake bound, and the msvc-asan job cap

**Input**: the converged bundle `specs/106-logon-guards-write-bounds/` (Gate A loop 2 round 3, CONVERGED P1 0 / P2 0 by
delegated owner ruling): spec.md (US1 to US6, FR-001 to FR-017, SC-001 to SC-007, the Clarifications and rulings R1 to
R14), plan.md (Summary, "Phases (for /speckit-tasks)", Project Structure, "Seams touched", "Deferred to tasks.md",
Complexity Tracking, Gate A), research.md (F-1 to F-9, D1 to D11, the D9 cell and carrier tables, Supersession, Risks),
data-model.md (E-1 to E-10, INV-106-1, INV-106-2), contracts/logon-guards-write-bounds-106.md (C-1 to C-9) and
quickstart.md (sections 1 to 7). Cell and mutant names are the bundle's, verbatim. Where the bundle gives a name pattern
(`IdentityFirst106.<Refusal>_<Gate>`, `WriteTimeout106.<Class>`, `OneEnding106.<First>Then<Second>`), the task quotes the
pattern and lists its instances; an instance name the bundle does not spell out is marked *(proposed)* and may be
renamed at write time, the rename recorded in the evidence file. Where this file and the bundle differ, the bundle
wins and the difference goes to the orchestrator.

**Phase moves against plan.md "Phases (for /speckit-tasks)".** Each item below runs in a different phase than the plan
gives it. Re-derive: walk plan.md's Phases items 0 to 7 against each task's phase heading; a new move is added here.
- To Phase 2 (foundations), from plan P4: the `write_timeout_ms` field and TOML key (T029, T030), the
  `session_write_timeout` enumerator and its C mapping (T024, T028), the `TomlWriteTimeout106.*` cells (T025, T026),
  M11 (T034), M11′ (T035) and M15 (T033).
- To Phase 2, from plan P5: the `EngineConfig` and `EngineEstablishment` fields and the root key (T029, T030), the range
  and type checks of `[session.transport]`/`[default.transport]` `tls_handshake_timeout_ms` (T030; feeding the factory
  stays in US5, T135), the `TomlHandshake106` refusal cells and `AcceptorKeyAbsent_Default1500` (T025, T026), M7⁗
  (T036) and the session-scope refusal's mutant (T037).
- To Phase 2, from plan P2 and P3: the test seams' hooks (T031); their call sites stay with their mechanisms.
- To US3 (Phase 5), from plan P4: M9″ (T096), M9 with M9′ over the pairs without the limit (T097), and M13 for the sites a
  US3 cell returns through (T098; the write-limit sites stay in US4, T130).
- To US4 (Phase 6), from plan P3: the `ShippingClock106` cell (T107), with its mutant M8 (T126), which plan P4 already
  lists; the `ConnectionBound106` cells that need a write armed (T106) and M12's arms over them (T129).

**Tests**: required (`[const §VII.3]`, FR-016). In every story phase the cells come first as their own tasks and are
shown RED (on the base, or under the named mutant for a [seam] or [guard] cell), then the mechanism, then the mutant
and spurious-hit arms.

## Format: `[ID] [P?] [Story] Description`

`[P]`: a different file from every other open task of its phase and no dependency on an incomplete task (appending
one's own section of the evidence file does not count as a shared file). `[US1]` to `[US6]`: story phases only. Paths
are relative to the library root (`$T`) unless marked PARENT (`/home/catalin/Work/Programming/Antreprenoriat/`). A task
marked **(orchestrator)** is run or requested by the orchestrator; every other task is the implementer's
(`[const §XVI.6]`).

## Execution rules (apply to every task)

- **Trees.** `$T` = the main checkout `PARENT/research/G19-fix-fpml-iso20022/library`, branch
  `106-logon-guards-write-bounds`. `$B` = `/mnt/wsl/fixppbuild/fixpp-106-base`, a detached worktree at
  `git -C $T merge-base HEAD origin/main` (RED readings and bench bases only). `$M` =
  `/mnt/wsl/fixppbuild/fixpp-106-mutants`, detached (mutants, census scratch edits, seeded known positives only). `$V` =
  `/mnt/wsl/fixppbuild/fixpp-106-verify`, a detached worktree at the head, created by T169 for the one full ctest only
  (never a mutant, never a scratch edit; its readings are recorded in `$T`'s evidence file). Never
  `git checkout`/`git switch` in `$T`, never `git stash`, never `--amend`, never `git commit -a`. `fixpp-parallel` and
  the MSVC sandbox are never `$B`, `$M` or `$V`.
- **The base.** "RED on the base" means read on `$B`. Its `src/`, `include/` and `tests/` must equal `9d81082a`, where
  the bundle was read: `git diff --stat 9d81082a "$(git -C $T merge-base HEAD origin/main)" -- src include tests` prints
  nothing. The branch's own commits before the first code change touch only `specs/` and `.github/workflows/tier2.yml`
  (US6). A rebase moves the merge-base: `$B` is then re-created at the new one and the recipe re-run; a non-empty output
  stops Phase 3 and goes to the orchestrator.
- **Builds.** One build or ctest process at a time on the machine (WSL load cap: `-j5` for a build, `-j3` for ctest),
  under `flock /tmp/fixpp-build.lock`. A long one runs detached (`setsid nohup … ; touch <marker>.done`) with one
  background waiter on the marker, never a foreground poll, never `pgrep -f`. `$B`, `$M` and every sanitizer, release,
  coverage and bench tree live on `/mnt/wsl/fixppbuild` (`export CCACHE_DIR=/mnt/wsl/fixppbuild/ccache`); `$T`'s
  `build/linux-clang-debug` is built incrementally only (no full rebuild on E:). Read
  `/mnt/c/temp/fixpp/.sandbox-lock` before the full ctest and before any MSVC run. Owner ruling 2026-11-25: autonomous
  end-to-end work; local builds are pre-approved by the owner for this feature (`[const §XVII.7]` satisfied by that
  ruling). Scope rule (owner 2026-11-25): targeted tests only, plus ONE full ctest on linux -debug; one build at a time
  (the Test scope bullet below gives the detail).
- **Test scope (owner, 2026-11-25, binding).** Targeted runs only: the feature's labels (`106`, `105`, `104`, `099`) and
  the binaries whose test sources the branch touches (the config, transport, C-ABI and 538 binaries, `-L 544` on the
  release lane, and `-L alloc_guard` on the release lane (T166) because the branch touches `tests/alloc_guard/`), selected
  by label or by binary filter. Sanitizer lanes (asan, ubsan, tsan) run the same targeted set,
  never `-L session` or a full suite. Exactly **one** full `ctest`, on `linux-clang-debug` in `$V` (T169; never `$M`, the mutant
  tree), as the last test run before the labelled push (Article XVII §7's evidence). MSVC builds the feature's own targets with `-k 0` and runs their
  targeted tests; msvc-asan only if a change needs it (quickstart §5 gives the condition). After a small late fix only the touched cells re-run. Quickstart §5 is the one list of the scope,
  including T170's `linux-gcc-release` build, T171's whole pytest suite and T172's buildless Tier 1 steps. "The
  binaries whose test sources the branch touches" is derived, not listed: `git diff --name-only "$(git merge-base HEAD
  origin/main)" HEAD -- tests` mapped to the ctest entries of each file's CMake target. A suite whose test sources are
  untouched but whose production code changed is covered only by the one full ctest and by CI's full matrix (quickstart
  §5 states that residual). A late code fix after T169 leaves T169's evidence at its SHA: the PR body's line names the
  SHA the full ctest ran at, and the late fix's targeted re-runs and T177's list are recorded against the new head; no
  second full ctest (owner rule).
- **RED on the base.** Cells are committed in `$T` (commit `C<n>`), overlaid on `$B` with
  `git -C "$B" restore --source=C<n> -- $(git -C $T ls-tree --name-only C<n> tests/session/ | grep -E '^tests/session/test_106_') tests/session/CMakeLists.txt`
  plus every other test file the task names (a re-pinned 104 or 099 cell, a config cell), reconfigured (a reading counts
  only after the reconfigure returned 0), built (the named targets only; never `session_106_seam_tests`) and run with
  `ctest --preset <p> -L 106 -LE 106_seam --no-tests=error` (or the named binary's `--gtest_filter`). A cell must fail
  by an assertion for the reason its task states. A compile failure, abort, crash, hang or ctest timeout is not RED. A
  cell expected RED that reads GREEN stops its task; the reading goes to the orchestrator. Every reading records its
  test count and skip count; a TLS cell that skips (`FIXPP_TLS_FIXTURE_DIR` unset) is not a reading, and a non-zero
  skip count fails the step. Every `BlockingWriteTransport` cell releases its blocked writes at teardown, so a base run
  fails by assertion, never by a hang.
- **[seam: Mn] cells** name a symbol the base lacks (`write_timeout_ms`, `session_write_timeout`,
  `EngineConfig::acceptor_tls_handshake_timeout`, `EngineEstablishment::acceptor_tls_handshake_timeout`, a test-access
  read of a new member, a new seam). They live in `tests/session/test_106_seams.cpp` (ctest `session_106_seam_tests`,
  labels `session`, `106` and `106_seam`). Their RED is read under the named mutant at the implementation head, in
  `$M`, and recorded in place of the base reading (research D9, 105's rule). Any other cell that fails to compile on
  `$B` stops its task.
- **[guard: Mn] cells** keep a rule the base already keeps: GREEN on the base by design, RED under the named mutant.
- **Instants (mock-clock cells; research D9 "Instants").** G := `detail::held_test_request_cap`'s value, L + 3T + 1 ns,
  for an emit not returned or held; 105's max(L + 2T, `tr_written_at` + T) + 1 ns for a written TestRequest. L is
  `last_inbound_steady_`, W is `write_timeout_ms`, B is `logout_disconnect_timeout_ms`, S a write's start. Every
  timer cell reaches its instant with `step_to(<instant>)` and drains, never a coarser `advance` (the one exception is
  `OneEnding106.BothDueAtOneWake`). It asserts the record absent at G − 1 ns and present at G, open at G + B − 1 ns and
  closed at G + B; a write-limit cell asserts open at S + W − 1 ns and closed at S + W. Each timing cell states its
  parameters and the inequality of its instant against the competing instant, with the margin, as research D9's row
  gives it, and each has its spurious-hit arm.
- **FR-017 assertions** (contract C-4.6, INV-106-2). Wherever a task says "FR-017": exactly one new `Disconnected`
  entry in `fsm_visit_history()`, `onLogout` at most once (once from Active, none before Active), one close, and the
  reason records FR-017 names (one for the give-up, the write limit or the establishment deadline; none for an ending
  Logout, `close()`, `Engine::stop()`, a reconnect drain or a bound failure exit).
- **Selection.** At the head `ctest --preset <p> -L 106 --no-tests=error --output-on-failure` (it takes in the seam
  entry), then `-L 105`, `-L 104`, `-L 099` as the task names; one cell by the binary's
  `--gtest_filter='*<Suite>.<Cell>*'`. Every run records its reported test count; 0 fails the step. Never
  `ctest -R <cell>`.
- **Mutant protocol** (quickstart section 4). Each mutant edits only `$M`'s copy of the file holding the named mechanism
  (`src/session/session.cpp` unless the task names another) and records its row in the evidence file. At commit `H<n>`:
  `$M` at `H<n>` with a clean status; apply only the named edit (a combined arm, such as "M4 with M9 and M9′", applies
  all its edits); `strings`-check a mutant-specific literal so the binary is fresh; build; run the named cells, then
  the `106` label unfiltered; record RED per cell; revert with `git -C "$M" restore . && git -C "$M" clean -fd`. A
  CMake-touching mutant counts only after its reconfigure returned 0. A pair or cell GREEN under its mutant is reported
  as vacuous, not counted, and goes to the orchestrator.
- **Comments.** A comment may record a procedure, never a result (no counts, offsets, measured values or pasted
  output). Cite symbols, never line numbers. **Every comment that supersedes a recorded decision names the new one**,
  in the form "106 D<n> supersedes <feature> <decision>: …" (for example "106 D3 supersedes 105 E-4's
  `tr_written_at` count for an emit not returned"); research.md's Supersession table is the list, and T158 checks
  it.
- **Evidence.** Census outputs, RED readings, mutant tables and bench runs go to
  `.specify/decisions/106-logon-guards-write-bounds-evidence.md`, never into a code comment.
- **CodeGraph.** `projectPath` = `$T` only (never `$B` or `$M`); `codegraph sync` in `$T` after every code-changing
  phase. No CLI lookup subcommands.

---

## Phase 1: Setup and censuses (no production code; plan phase 0; quickstart section 1)

**Purpose**: the trees, the evidence file, the test registrations and every census the stories assert over, each
census shown first to report its known positive (a census that reports nothing is not believed until its positive
shows). Re-derived at HEAD. **Blocks every later phase.**

- [X] T001 Check `git -C $T rev-parse --abbrev-ref HEAD` is `106-logon-guards-write-bounds`; run the base-equivalence
  recipe of the Execution rules and record its output; `df -h /mnt/wsl/fixppbuild`, then create `$B` (at
  `git -C $T merge-base HEAD origin/main`) and `$M` with `git worktree add --detach` on `/mnt/wsl/fixppbuild`; run
  `codegraph sync` in `$T` and confirm `codegraph status --json` names `$T` with a nonzero `fileCount`.
- [X] T002 Create `.specify/decisions/106-logon-guards-write-bounds-evidence.md` with one section per census, per RED
  reading (C1 to C6), per mutant task, per bench run and per lane run of the Polish phase.
- [X] T003 Register ctest `session_106_tests` (labels `session` and `106`) in `tests/session/CMakeLists.txt` after
  `add_session_105_bucket`'s shape, over `tests/session/test_106_identity_first.cpp`, `test_106_give_up.cpp`,
  `test_106_one_ending.cpp` and `test_106_handshake.cpp`, and a second entry `session_106_seam_tests` (labels `session`,
  `106` and `106_seam`) over `tests/session/test_106_seams.cpp` alone; create each file as a compiling stub so that
  configure succeeds in `$T` and on `$B`. Both entries define `FIXPP_TLS_FIXTURE_DIR` and link `fixpp_tls` and the
  OpenSSL and zlib libraries as `add_session_105_bucket` does, so no TLS cell skips. Find every list a new entry or
  label must join: `git grep -nE 'session_105_tests|session_105_seam_tests|add_session_105_bucket|105_seam' -- ci tools .github tests`
  (known positive: the CMakeLists entries); read `ci/pump-census.sh` against `ci/expected-pump-sites.txt` (the new
  files use readiness-checked pumps only); read `ci/expected-preset-conditional-tests.txt` and
  `ci/expected-eligible-tests.txt` (a per-lane count the two entries move is left to the PR's first CI reading, as 105
  did, and recorded). Config cells: read `tests/config/CMakeLists.txt` and record where the `TomlWriteTimeout106` and
  `TomlHandshake106` cells go: the refusal cells in `tests/config/test_load_negative_battery.cpp` (105 T092's
  precedent), the cells that mint a TLS transport (`TomlHandshake106.{SessionKey, SecondSessionDiverges,
  DefaultTransport_MixedRoles_Accepted}`) in a config-suite file only if that bucket links the TLS transport and the
  fixture directory, else in `tests/session/test_106_handshake.cpp`; each such file carries label `106`.
- [X] T004 Register `alloc_guard_106_connection_bound_mallocnesia` in `tests/alloc_guard/CMakeLists.txt` through
  `fixpp_add_mallocnesia_test` (labels `alloc_guard` and `106`, the Release gating the other mallocnesia entries use),
  over a stub `tests/alloc_guard/test_106_connection_bound.cpp`, with a CMake option `FIXPP_106_ALLOC_BASE` (default
  OFF) passed as a compile definition: with it ON the TU compiles only the emit half with the limit off and names no
  symbol 106 adds, which is how `$B` builds the base half of the emit differential (T108): the base reading overlays
  `tests/alloc_guard/CMakeLists.txt` and the TU on `$B` and counts only after its reconfigure with
  `-DFIXPP_106_ALLOC_BASE=ON` returned 0 (without the overlaid CMakeLists the option does not exist there).
- [X] T005 Write `tests/session/test_106_support.hpp` (compiles on `$B`; names no production symbol 106 adds): the
  instants helper (G for an emit not returned or held, G for a written TestRequest, S + W) over `mock_clock`; reuse of
  `t105::DirectRig` (`tests/session/test_105_direct_rig.hpp`), `rgs::Scene`, `tests/support/blocking_write_transport.hpp`
  and `t105::GateStoreFactory`, an Engine rig of `PreActive105`'s shape, and the 104 Logon rigs
  (`tests/session/test_104_support.hpp`); a counter of `Disconnected` entries over `fsm_visit_history()`; a capturing
  log sink (`tests/support/log_capture.hpp`) with a matcher for the slot-74 record and for the write-timeout record's
  format string; a silent TCP listener (accepts, never reads, never speaks TLS) for the handshake cells; a peer-side
  read observer (bytes after the handshake, EOF).

### Censuses (read-only, re-derived at HEAD; each with its known positive)

- [X] T006 [P] Size pins (research F-8, data-model E-9): run the seed `git grep -n 'static_assert(sizeof(' -- tests`
  first (it must report `tests/dictionary/ref_shape_test.cpp`), then
  `git grep -nE 'sizeof\((fixpp::)?(core::|session::|config::)?(Session|SessionConfig|EngineConfig|EngineEstablishment)\b' -- tests src include bench bindings`;
  list every hit for T029 and T083.
- [X] T007 [P] Gate call sites (D1): `git grep -n 'logon_identity_admitted_()\|logon_credentials_admitted_(' -- src/session`.
  On the base it must show the two arms' sites, both too-low branches and arm S; record each site's enclosing function
  for T049. Also rule research F-3's stale-identity path: whether `drive_reconnect()` can install a connection before the first
  `open()` and leave `live_peer_id_` unconsumed when it ends (`open()` itself refuses unless `never_opened`; read
  `drive_reconnect`'s preconditions and `git grep -n 'live_peer_id_' -- src/session`); unreachable, or a task that makes `begin_connection_()` reset `live_peer_id_`, with a cell.
- [X] T008 [P] `src/session/session.cpp`: The D2 walk (D2, "Returns after the base's advance"): walk `on_inbound_not_connected_` and
  `on_inbound_logon_sent_` from their `check_inbound` call to `record_state_transition_(fsm_state::Active)`, listing every
  `co_return`. The S3 Reject of both arms must appear. Compare the list with research D2's table row by row; a return
  with no row is added to the table (orchestrator) and to T056 / T057.
- [X] T009 [P] Pinned consumed numbers (D9; FR-007 census): `git grep -nE 'next_in\(\)|next_inbound' -- tests/session/test_099_*.cpp tests/session/test_104_*.cpp`,
  read for assertions after a refusal ending (first candidate `W5Scan.NextNumInIsTheHeadersAndNoReset` in
  `tests/session/test_099_establishment.cpp`); then the FR-007 recipe
  `grep -n 'r.open()' tests/session/test_104_logon_too_low.cpp` plus each acceptor cell there ending at a gate refusal,
  read for a NextNumIn assertion after the refusal (known positive: `LogonTooLow104.Precedence_MtlsAcceptorOffList`'s
  `next_in() == kN`). Each hit classed "S3, strict or 789 refusal: re-pinned by T059", "identity refusal: re-pinned
  by T043", or "admitted path: unchanged". *(Amended by T051: this recipe cannot see 099's trace-line `c`/`k` field or
  its frame pins; research D9's "FR-007 census amendment" gives the run-based recipe for the identity refusals.)* Also list, with `--gtest_list_tests` on the 099 binary, the instances of the
  cells that cover spec US2 scenario 3 (the unasked 141=Y acknowledgement refused at S3; NextNumIn kept) for T057:
  `W5PerShape.Initiator141YWith1137_4_NoReset` (shapes T, A, L) and the `kLogonSent` row LS-e `Logon 141=Y 1137=4`
  run by `W5LogonSent.Ending`, both in `tests/session/test_099_establishment.cpp`; class them "US2 sc.3: unchanged".
- [X] T010 [P] Logout sites (data-model E-6): `git grep -n 'build_logout(' -- src/session`, each followed to its
  `store_then_emit`. The R-1 leaf (`inbound_logout_leaf_`, kind `r1`) and `run_logout_phase1` must appear. Re-derive the
  **Logout-site table** below at HEAD; a site not in it is added, with a cell task (T104) or an unreachable
  ruling.
- [X] T011 [P] Live write sites (D5): `git grep -n 'store_then_emit(\|live_write_serialized_(' -- src/session`, each
  followed to its write class of contract C-4.2 (known positive `run_logout_phase1`, class `logout`); every class of
  T100 must have at least one site, and every site a class.
- [X] T012 [P] Bundle-to-`EngineConfig` copy sites (D6, C-2):
  `git grep -nE '= *[a-z_]+(\.|->)engine\.clock' -- tests examples bindings bench`; known positives
  `tests/config/test_098_dialect_overlay.cpp` and `tests/config/test_099_toml_live.cpp`. List each for T137.
- [X] T013 [P] `include/fixpp/core/error.hpp`: Last error enumerator (D5): `git grep -n wire_undefined_tag -- include src tests tools bindings`; every
  site that names the last enumerator (an exact-set or count gate among them) is listed for T028.
- [X] T014 [P] `dispatch_aborted` branches (D5): `git grep -n 'dispatch_aborted' -- src/session/session.cpp`, keeping
  the `==` and `!=` lines (known positive: `Session::send`'s F9 block in `send_body_`), then the same over `src/session`
  so a comparison in another file shows. Re-derive the **`dispatch_aborted` ruling table** below; a new line gets a
  ruling there.
- [X] T015 [P] Unconditional `Disconnected` writes (D11):
  `git grep -n 'record_state_transition_(fsm_state::Disconnected)' -- src/session/session.cpp`. Known positives: the
  liveness loop's emit-failure branches in `run_liveness_loop_body_` (after `fire_to_admin_` and after
  `store_then_emit`, for the TestRequest and the Heartbeat) and `Session::send`'s F9 block (`send_body_`). Read every
  hit for "can a claimed ending's cut write, or its close, return here" and class it in the evidence file's
  **Disconnected-site table** (one row per hit, by enclosing function): *reachable* (it becomes
  `record_disconnected_unless_already_()` in T079, and gets one M13 arm and the cell whose cut returns through it, in
  T098 or T130) or *unreachable* (with the reason: no live write precedes it on that path, it runs only before
  any claim can be taken, or it is already conditional). Seed rows the reading must classify: the Reject leaf
  (`inbound_reject_leaf_`), each Logon-class write's failure tail (`emit_initiator_logon_`,
  `inbound_acceptor_reply_logon_leaf_`, `honor_peer_next_expected_`'s resend frames), the leaves of the other write
  classes (`inbound_heartbeat_reply_leaf_`, `inbound_resend_request_leaf_`, `answer_resend_request_`,
  `emit_session_reject_`). The table is the list plan.md's "M13 sites that no cell reaches" asks tasks.md for: every
  row classed unreachable is named there with its reason. Disposition (orchestrator ruling on the Phase 1–2 report):
  the census is authoritative. Quickstart §4's M13 row and research D9's FR-009 row are rewritten from its table; the
  Reject leaf's write-failure tail records nothing (`inbound_reject_leaf_` discards `emit_r`), so M13 has no arm there;
  the reachable `inbound_heartbeat_reply_leaf_` and `inbound_business_reject_leaf_` tails gain T071a and T071b; the
  two-argument `emit_session_reject_` is read by T100's `Reject` rig. The table is the evidence file's T015 section.
- [X] T016 [P] Ending claimants (D11 row 3): `git grep -n 'logout_in_flight_ = true\|enter_ending_write_(' -- src/session/session.cpp`;
  known positives `run_liveness_loop_body_` (R-1) and `run_logout_phase1`. Re-derive the **Ending-claimant table** below
  at HEAD, and for each claimant site the base's record sites
  (`git grep -n 'FIXPP_SLOG\|session_event_' -- src/session/session.cpp`, read inside each site): none is expected, since
  an ending Logout's reason is its Text(58).
- [X] T017 [P] Sleepers (D3, #715): `git grep -n 'sleep_until\|await_deadline' -- src/session/session.cpp`, each hit
  read for which coroutine sleeps there and what else of this Session sleeps at the same time; kept for T090.
- [X] T018 [P] C-ABI surfaces (D7): `git grep -nE 'MINOR 18|1\.18' -- include tests bindings tools ci examples`, each hit classed
  "pins the minor" (moved in T150) or "history" (unchanged), recorded per consumer category of Article X §7 (the
  Python binding, the tests with `tests/interop` as its own row, examples, the CI and tool scripts), with a seeded known
  positive in `$M` for `bindings/` so a zero there could report non-zero; the generator of `tools/capi_freeze.sha256`
  (`git grep -n capi_freeze -- tools ci .github CMakeLists.txt`) and whether it hashes header comments; then
  `git grep -n 'amended in C-ABI 1.18\|BREAKING (C-ABI 1.18' include/fix/c_api/session.h` (known positives: the docs of
  `fixpp_session_close`, `_is_established`, `_send`, `_register_callback` and `_register_send_callback`, none in
  `_open`'s).
- [X] T019 [P] `co_spawn` census: `grep -n 'co_spawn' src/session/session.cpp src/session/engine.cpp` on `$B`, kept
  for the `co-spawn-closure-audit` of T172 and the spawn-shape review of T083.
- [X] T020 [P] B&L census (D8), run on the LIVE file before any edit:
  `grep -n 'L-093-12\|L-104-3\|L-104-8\|L-105-f\|B-544-1\|L-497-1\|tr_written\|runs ahead of mTLS' spec/behaviors-and-limitations.md`
  (known positive: `B-099-2`'s line). Rule each hit in the evidence file as "amended by T153", "retired by
  T152", or "unaffected" with the reason; `B-105-3` and `B-544-1` must be classed amended. Confirm none of the
  rows to be amended sits in `spec/behaviors-and-limitations-closed.md` (read the live file; never grep across the pair).
- [X] T021 [P] "Has a cell" census (D9): `grep -n -i 'has a cell\|a cell for' specs/106-logon-guards-write-bounds/*.md`
  and plan.md's IX §1 row, each claim looked up in the traceability tables at the end of this file; a claim with no
  task goes to the orchestrator.
- [X] T022 [P] Supersession sources (research "Supersession"): for each row, locate the code or comment the change
  touches at HEAD (by symbol: the two Logon arms, the too-low branches, `run_liveness_loop_body_`, the held cap, the
  listener's `accepted_transport_config` in `src/session/engine.cpp`) and list the task that writes its "106 D<n>
  supersedes …" comment; checked again by T158.
- [X] T023 [P] Bench census (Article VIII §2; contract C-9): read `bench/session/engine_send_bench.cpp` (its writes
  must reach `live_write_serialized_`), `session_send_bench` and `on_inbound_frame_bench`, and their rows in
  `bench/ci-suite.txt` (`engine_send_bench` is `paired`; the other two are run locally); record how each is run for
  T164. The plan requires all three paired runs (quickstart section 6).

**Checkpoint**: every population has shown its known positive; the three tables below are re-derived at HEAD.

### Logout-site table (seeded from `git grep -n 'build_logout(' -- src/session` at `9d81082a`; T010 re-derives it)

| Site (function) | Ending kind | Claim (D11) | Write class (E-6) | M4 cell |
|---|---|---|---|---|
| `inbound_logout_leaf_`, kind `r1` | the give-up's Logout | written under `decide_give_up_()`'s claim; takes none | `logout` | `WriteTimeout106.EndingLogout_Keeps105Limit` (T103), M4 with M9 and M9′ |
| `run_logout_phase1` | `close()`'s Logout | none: `closing` owns it | `logout` | `WriteTimeout106.CloseLogout_KeepsGrace` (T103), M4 with M9 and the closing term |
| `inbound_logout_leaf_`, kinds other than `r1` (R-2, the reply) | 105's R-2 and reply Logouts | claims at its `logout_in_flight_` / `enter_ending_write_` site | `logout` | T104 |
| `refuse_logon_with_logout_` | the Logon arms' refusal Logouts | claims at its `enter_ending_write_` site | `logout` | T104 |
| `inbound_logon_sent_logout_leaf_` | the initiator arm's refusal Logouts | claims at its `enter_ending_write_` site | `logout` | T104 |
| `honor_peer_next_expected_` | the two NextExpectedMsgSeqNum(789) Logouts (105 ruling R-789) | claims at its `enter_ending_write_` site | `logout` | T104 |

### Ending-claimant table (D11 row 3; seeded from T016's grep at `9d81082a`)

| Site (function) | Today | 106 |
|---|---|---|
| `run_liveness_loop_body_` (R-1: `logout_in_flight_ = true`, `enter_ending_write_(ending_kind::r1, …)`) | the give-up block | moves into `decide_give_up_()`, which claims `give_up` first (T082) |
| `run_logout_phase1` | `close()`'s Logout | excluded: written under `closing`; takes and tests no claim |
| `inbound_logout_leaf_` | sets the owner, enters writing (kind passed in) | kind `r1` excluded (the give-up's claim); every other kind claims `ending_logout` where it takes the owner (T080) |
| `refuse_logon_with_logout_` | `enter_ending_write_(ending_kind::r2, …)` | claims `ending_logout` first (T080) |
| `inbound_logon_sent_logout_leaf_` | `enter_ending_write_(ending_kind::r2, …)` | claims `ending_logout` first (T080) |
| `honor_peer_next_expected_` | `enter_ending_write_(ending_kind::r2, …)` | claims `ending_logout` first (T080) |
| `enter_ending_write_` (its definition) | — | not a site: the callee |

### `dispatch_aborted` ruling table (D5; seeded from T014's grep at `9d81082a`)

| Branch (function) | Ruling |
|---|---|
| `send_body_`, the F9 block (`disconnect_required \|\| (!impl_r && impl_r.error() == error::dispatch_aborted)`) | Takes `session_write_timeout` as it takes `dispatch_aborted` (research D5), through `record_disconnected_unless_already_()` (D11). The arm is **redundant**: on that value the block's only action is the `Disconnected` record, then `co_return impl_r` un-coerced, so the caller receives `session_write_timeout` either way, and the expiry's step 4 (data-model E-5) has already recorded `Disconnected`. No cell of its own; the tail is covered by `WriteTimeout106.Send` under M13 at this site (T130). The block's catch tail (`fsm_state_ != fsm_state::Disconnected` test, then `dispatch_aborted`) is already conditional. |

---

## Phase 2: Foundational — the error enumerator, the configuration fields and TOML plumbing, the test seams (plan phases 4–5 surfaces that every story needs)

**Purpose**: the symbols the stories' cells name, each landed RED-first. **⚠️ No story phase starts before this
checkpoint.** The behaviour that uses these symbols lands in its story (the write limit in US4, the handshake bounds in
US5).

### RED cells for the foundations (write first)

- [X] T024 [P] `tests/capi/expected_error_map.csv`: add the row `session_write_timeout,FIXPP_ERR_TRANSPORT_IO`
  (data-model E-8), and read `tests/capi/error_surface_test.cpp`'s error-map cell at the head before T028: it must fail
  by assertion (the row names an enumerator the enum lacks), not by a compile error. [seam: M15] for the mapping itself
  (T033).
- [X] T025 [P] `tests/config/test_load_negative_battery.cpp`: Config refusal cells (compile on the base; T003's placement): `TomlWriteTimeout106.OutOfRange` and
  `TomlWriteTimeout106.WrongType` (contract C-4.7; `OutOfRange` covers −1 and `UINT32_MAX` + 1, each `out_of_range`; `WrongType` a string and a
  float, each `malformed_value`, Q4); `TomlHandshake106.OutOfRange` and `TomlHandshake106.WrongType` for `[session.transport].tls_handshake_timeout_ms`
  (contract C-5.1; `OutOfRange` covers 0, −1 and `UINT32_MAX` + 1); `TomlHandshake106.AcceptorKey_OutOfRange`,
  `TomlHandshake106.AcceptorKey_WrongType` and `TomlHandshake106.AcceptorKey_AtSessionScope_Refused` for the root key `acceptor_tls_handshake_timeout_ms` (contract C-5.2; `OutOfRange` covers 0, −1
  and `UINT32_MAX` + 1; at session scope the `unknown_key` class, as `dialect_overlay` at root). Each asserts the
  error kind and the key named in it. RED on the base: `TomlWriteTimeout106.*` and `AcceptorKey_{OutOfRange, WrongType}`
  by an `unknown_key` refusal where the cell asserts `out_of_range` or `malformed_value` (the keys are table keys
  `recognize_keys` does not know); `TomlHandshake106.{OutOfRange, WrongType}` by no diagnostic at all, because the base
  silently ignores `tls_handshake_timeout_ms`: `recognize_keys` checks only a table's own keys, not those inside
  `[session.transport]` or `[default.transport]`, and `build_tls_factory` always uses a default `Transport::Config
  t_cfg{}`; `AtSessionScope_Refused` is [guard] on the base (unknown there too) and
  RED under its mutant (T037).
- [X] T026 [P] Seam cells in `tests/session/test_106_seams.cpp`: `TomlWriteTimeout106.Set` [seam: M11] (the
  resolved `SessionConfig::write_timeout_ms` equals the key, set in a `[[session]]` table and, separately, in
  `[default]`); `TomlWriteTimeout106.KeyAbsent_Off` [seam: M11′] (a bundle with no key resolves `write_timeout_ms == 0`,
  and `SessionConfig{}.write_timeout_ms == 0`; exact equality); `TomlHandshake106.AcceptorKeyAbsent_Default1500` [seam:
  M7⁗] (no `acceptor_tls_handshake_timeout_ms` key: `bundle.engine.acceptor_tls_handshake_timeout == 1500ms`, the
  `EngineConfig` the cell copies from it reads 1500 ms, and `EngineConfig{}.acceptor_tls_handshake_timeout` and
  `fixpp::config::EngineEstablishment{}.acceptor_tls_handshake_timeout` are each 1500 ms; exact equality, since
  `EngineFirstFramePhaseATls.Q17_*` accepts a window). They compile only after T029.
- [X] T027 `.specify/decisions/106-logon-guards-write-bounds-evidence.md`: Commit the cells (`C1`); read T025 on `$B` (the config binary, overlaid) and T024 at the
  head; record per cell the RED reason or guard status and the test count.

### Implementation for the foundations

- [X] T028 `include/fixpp/core/error.hpp`: `session_write_timeout = 136` appended after `wire_undefined_tag` (`[const §X.4]`:
  append-only, no numeric reuse) with its `error_message` text of data-model E-8; `src/capi/error.cpp`: `translate` maps
  it to the existing `FIXPP_ERR_TRANSPORT_IO` (no new C constant; the occupancy file untouched); every site T013
  listed is updated in the same commit, so no exact-set gate goes red. T024 GREEN; `-L capi`'s error entries
  GREEN.
- [X] T029 Layout (C++ BREAKING, data-model E-9): `include/fixpp/session/session_config.hpp`
  `SessionConfig::write_timeout_ms` (`std::uint32_t`, default 0 = off, Doxygen naming contract C-4.1: no C setter);
  `include/fixpp/core/engine_config.hpp` `EngineConfig::acceptor_tls_handshake_timeout` and
  `include/fixpp/config/config_bundle.hpp` `EngineEstablishment::acceptor_tls_handshake_timeout` (each
  `std::chrono::milliseconds{1500}`, Doxygen naming contract C-5.2). Every size pin T006 found is updated or
  shown unaffected.
- [X] T030 TOML plumbing (data-model E-7): `src/config/toml_config_loader.cpp` `kRecognized` gains `write_timeout_ms` and
  `acceptor_tls_handshake_timeout_ms`, and the session-scope refusal of the root key; `src/config/scalar_mappers.cpp` the
  `write_timeout_ms` mapper (`logon_timeout_ms`'s shape: `out_of_range` outside `uint32`, `malformed_value` for another
  type); `src/config/selector_resolver.cpp` the root key into `EngineEstablishment::acceptor_tls_handshake_timeout` (> 0,
  same checks) and the range and type checks of `[session.transport].tls_handshake_timeout_ms` and
  `[default.transport].tls_handshake_timeout_ms` (0 < v ≤ `UINT32_MAX`). Feeding the latter into the factory is US5's
  (T135). T025 and T026 GREEN; `-L config` GREEN.
- [X] T031 Test seams (research D9, data-model E-3): in `include/fixpp/session/session.hpp` the
  `std::function<bool()>` members `connection_bound_spawn_fails_` and `connection_bound_wait_throws_` (105's
  `ending_bound_spawn_fails_` / `ending_bound_race_throws_` shape) and the commit-freeze hook of
  `NextNumIn106.CommitRefused_Disconnected` (a `std::function<void()>` run between the classification and the commit);
  in `tests/support/session_test_access.hpp` their setters (`set_connection_bound_spawn_fails`,
  `set_connection_bound_wait_throws`, the freeze setter), the hooks only: the reads of `ending_claim_` and of the
  connection bound's counter land with those members (T078, T083), so `H0` compiles. The seams' call sites land with their mechanisms (T064, T083). A seam reached
  by no cell is a defect (each is named in research D9).
- [X] T032 `.specify/decisions/106-logon-guards-write-bounds-evidence.md`: At the head: `-L config`, the C-ABI error entries and `session_106_seam_tests` (T026) GREEN; record
  counts. Commit `H0`.

### Mutant arms for the foundations (in `$M` at `H0`)

- [X] T033 In `$M`'s `src/capi/error.cpp`: M15: `translate` maps `session_write_timeout` to `FIXPP_ERR_CANCELLED`; RED: the error-map cell of
  `tests/capi/error_surface_test.cpp` over the CSV row.
- [X] T034 In `$M`'s `src/config/scalar_mappers.cpp`: M11: the `write_timeout_ms` mapper recognises the key and drops its value; RED: `TomlWriteTimeout106.Set`.
- [X] T035 In `$M`'s `include/fixpp/session/session_config.hpp`: M11′: `SessionConfig::write_timeout_ms`'s default changed to 300 ms; RED: `TomlWriteTimeout106.KeyAbsent_Off`.
- [X] T036 In `$M`'s `include/fixpp/config/config_bundle.hpp` and `include/fixpp/core/engine_config.hpp`: M7⁗, two arms run separately: `EngineEstablishment::acceptor_tls_handshake_timeout`'s default changed to 0 ms,
  then to 2500 ms; and, separately, `EngineConfig::acceptor_tls_handshake_timeout`'s default changed; RED in each:
  `TomlHandshake106.AcceptorKeyAbsent_Default1500`.
- [X] T037 In `$M`'s `src/config/toml_config_loader.cpp`: The root key accepted at session scope (the refusal in `toml_config_loader.cpp` deleted); RED:
  `TomlHandshake106.AcceptorKey_AtSessionScope_Refused`.
- [x] T037a **(orchestrator)** Article X §6 `/plan` sign-off: owner pre-approved the plan 2026-11-25 (autonomous
  end-to-end delegation); record in plan.md (Constitution Check, row XII §9, XI §7, X §6) with the date, the words and
  the bundle head. Before T038. Done at step 9 (checklist audit, 2026-11-26): plan.md's row records "I'll pre-approve
  the plan" (owner, 2026-11-25, as relayed by the orchestrator) at bundle head `edf0a758`.

**Checkpoint**: the enumerator, the three fields and the keys load; every story may start.

---

## Phase 3: User Story 1 — an unauthorised identity is disconnected without a message and changes nothing (Priority: P1) 🎯 MVP (#702; research D1; contract C-1; data-model E-1)

**Goal**: in the acceptor's NotConnected arm and the initiator's LogonSent arm, every identity gate that applies
admits the identity before the arm writes a frame or changes sequence state (FR-003 to FR-005, FR-007).

**Independent test**: `ctest --preset linux-clang-debug -L 106 --no-tests=error` with `IdentityFirst106.*`, plus
`-L 104` and `-L 099` unfiltered.

### RED cells for US1 (write first; `tests/session/test_106_identity_first.cpp`)

- [X] T038 [US1] Acceptor matrix, the pattern `IdentityFirst106.<Refusal>_<Gate>` (research D9 row FR-003/004; spec US1
  scenarios 1 to 3), instantiated over the refusals `ValidateFirst`, `Posture`, `SendingTime`, `Strict`, `ResetOnLogon`,
  `S3_1137` and the gates `OffListMtls` (an mTLS acceptor whose peer certificate is not authorised for the Logon's
  SenderCompID), `RefusedCredentials_NonMtls` (a plaintext acceptor whose logon validator refuses) and
  `RefusedCredentials_AdmittedMtls` (an mTLS acceptor whose identity is admitted and whose validator refuses), with
  `ValidateFirst_RefusedCredentials_NonMtls` spelled as the bundle spells it. Each drives one Logon that both triggers
  the refusal and fails the gate, and asserts zero frames written after the handshake, NextNumOut and NextNumIn
  unchanged in memory and in the store, no reset (the store's `reset_to` not called), and `Disconnected`. Each records
  the assertion that fails on the base: the pre-gate action writes a frame, changes sequence or reset state, or both;
  for `ResetOnLogon` and `S3_1137_OffListMtls` (no frame on the base, its acceptor gate precedes 1137) the RED is the
  reset or the in-memory NextNumIn. `ValidateFirst_RefusedCredentials_NonMtls` is also RED under M1a (T053).
- [X] T039 [US1] `tests/session/test_106_identity_first.cpp` (same file), unreadable credentials with a validator installed (research D9 row FR-003/004; contract
  C-1.4; spec Edge Cases): `IdentityFirst106.ValidateFirst_UnreadableCredentials_NonMtls` and
  `IdentityFirst106.ValidateFirst_UnreadableCredentials_AdmittedMtls` *(proposed)*: a validating acceptor, a Logon that
  `interpret_logon` refuses and that draws the validate-first Reject on the base; zero frames, `Disconnected`. Only
  validate-first: every other refusal follows `interpret_logon`. RED on the base: the Reject is written.
- [X] T040 [US1] `tests/session/test_106_identity_first.cpp` (same file), a direct mTLS initiator started by `open()` (F-3; contract C-1.7; spec US1 scenario 4), the
  pattern instantiated as `IdentityFirst106.<Refusal>_DirectMtlsOpen` *(proposed gate name)* over `ValidateFirst`,
  `Posture`, `SendingTime`, `S3` and `Asked141` (the credentials gate is acceptor-only): zero frames, both numbers
  unchanged in memory and store, no reset, `Disconnected`. RED on the base: the posture and SendingTime Logouts, the S3
  Reject, the validate-first Reject are written, or the asked unit resets (its RED is the reset).
- [X] T041 [US1] `tests/session/test_106_identity_first.cpp` (same file), the plain and strict refusals (research D9 row "FR-003 plain", N-5; spec US1 scenario
  7): `IdentityFirst106.Plain_<Role>_<Gate>` as `Plain_Acceptor_OffListMtls`, `Plain_Acceptor_RefusedCredentials` (on a
  plaintext acceptor) and `Plain_Initiator_DirectMtlsOpen` — a well-formed, in-sequence, unauthorised Logon or
  acknowledgement with no other fault; and `IdentityFirst106.Strict_Initiator_<Gate>` as
  `Strict_Initiator_DirectMtlsOpen` (the `bilateral_strict` silent refusal). Each asserts zero frames and NextNumIn
  unchanged in memory and store. RED on the base: the in-memory NextNumIn.
- [X] T042 [US1] `tests/session/test_106_identity_first.cpp` (same file), admitted identities (research D9 row FR-005; contract C-1.5): `IdentityFirst106.Admitted_<Refusal>`
  over the refusals of T038 and T040 that an admitted identity can reach, each asserting the answer
  and the ending as the base's (the frame bytes captured on `$B` and pinned); NextNumIn follows FR-006 (spec.md
  Clarifications, R16) —
  [guard: M1b]; `IdentityFirst106.Admitted_StayInArmReject_NextLogonActive` (a validate-first Reject whose RefTagID keeps
  the session in its arm, F-2, then a corrected Logon on the same connection reaches Active) — [guard: M1];
  `IdentityFirst106.Admitted_UnreadableNoValidator_ValidateFirstReject` (a non-mTLS acceptor, inbound validation on, no
  `set_logon_validator` call, a Logon `interpret_logon` refuses: the validate-first Reject, byte-identical) — [guard:
  M1c].
- [X] T043 [US1] `tests/session/test_104_logon_too_low.cpp`, the one FR-007 update (plan P1, research D9 "FR-007
  census"): `LogonTooLow104.Precedence_MtlsAcceptorOffList`'s setup assertion becomes
  `EXPECT_EQ(r.next_in(), <the value read before the feed>)`, because the refusal now precedes hydration (D1 item 4).
  RED on the base (the base reads `kN` after the refusal). Every other `LogonTooLow104.Precedence_*` cell and
  `UnaskedResetAck104.Mtls_Unauthorised_NoReset` stay as they are (FR-007; an initiator hydrated at open).
- [X] T044 [US1] `.specify/decisions/106-logon-guards-write-bounds-evidence.md`: Commit the cells (`C2`); read T038 to T043 on `$B` (`session_106_tests` and the 104 binary
  overlaid; `-L 106 -LE 106_seam` and the re-pinned cell by filter); record per cell its RED reason or guard status, the
  test count and the skip count (TLS cells: 0 skips).

### Implementation for US1

- [X] T045 [US1] `include/fixpp/session/compid_authorization_policy.hpp` and `src/session/compid_authorization_policy.cpp`:
  `CompIdAuthorizationPolicy::has_logon_validator() const noexcept`, true when `set_logon_validator` installed a callable:
  false when never set, true after a callable, false again after `set_logon_validator` with an empty one (the test
  `authorize_logon` makes; pimpl: no layout change; data-model E-1; Q1). Its header comment, with
  `set_logon_validator`'s, states E-1's obligations for a policy author: a frame `interpret_logon` refuses never
  reaches the validator (it fails closed, `B-106-1`); an absent Username(553) or Password(554) reaches it as empty, and
  a validator that requires them refuses that itself; the validator may be called twice per connection, after the bound
  event (`L-106-c`).
- [X] T046 [US1] `include/fixpp/session/session.hpp`, `src/session/session.cpp`: the private callee
  `logon_identity_refuses_(frame, consume)` *(superseded: the parameter is removed, both callers consumed; data-model E-1)* (data-model E-1's per-half table: the mTLS half applies on `mtls_ca` or
  `mtls_pinned` in either role, and with `consume` false emits no event and consumes nothing; the credentials half applies
  on the acceptor whatever its security profile, reads 553/554 through `interpret_logon`, and treats any
  `interpret_logon` refusal as unreadable, refusing it only when `has_logon_validator()`). Its locals stay in its own
  frame (104 T093).
- [X] T047 [US1] `src/session/session.cpp`: `on_inbound_not_connected_` (research D1 items 2 to 4): the validate-first Reject decided first,
  then held until `logon_identity_refuses_` admits; where `logon_reject_disconnects_` holds, the consuming
  `logon_identity_admitted_()` runs before the Reject is written and a refusal there ends the arm silently (N-13.2);
  where the Reject stays in the arm, nothing is consumed. After `interpret_logon` succeeds and before
  `ensure_hydrated_`: `logon_identity_admitted_()` then `logon_credentials_admitted_(*result)`; a refusal records
  `Disconnected` and returns success. Posture, SendingTime, strict, `reset_on_logon`, the sequence check, the too-low
  Logout, S3/1137 and the reply now follow the gates.
- [X] T048 [US1] `src/session/session.cpp`: `on_inbound_logon_sent_` (research D1 items 2 to 4, "Moved checks"): the validate-first rule as
  in T047 (mTLS half only); `logon_identity_admitted_()` right after `interpret_logon`, before posture; the asked
  141=Y unit keeps its place after 099's S3 test, now after the gate. Arm S (the `reset_on_logout` Logout branch) is
  unchanged.
- [X] T049 [US1] `src/session/session.cpp`: Delete every later gate call T007 listed (research D1 item 5): the acceptor's after
  `check_inbound` and after the 1137 block, the initiator's after the asked unit, and both 104 too-low branches' own calls.
  Comments at each moved or deleted site name the decision: "106 D1 supersedes 104 N-8 (the asked path's unit before the
  gate)", "106 D1 supersedes 104 DQ-6 / L-104-3 (answers before the gates)", "106 D1 supersedes 104 T8 (the too-low
  branches' own gate calls): one gate site per arm".
- [X] T050 [US1] `src/session/session.cpp`: No new local held across an await in either arm (104 T093; plan Constraints): read the arms' diff
  for every local declared before a `co_await` that it outlives; any found moves into a callee. Record the reading.
- [X] T051 [US1] `.specify/decisions/106-logon-guards-write-bounds-evidence.md`: At the head: `-L 106`, `-L 104` and `-L 099` unfiltered GREEN; re-run T007 at the head (each arm
  keeps one consuming site, arm S its own, the precheck callee its own) and record it. Commit `H1`; `codegraph sync`.
  At H1 `-L 099` and 038's binary read RED in cells that pin an answer or a consumed number ahead of an identity gate;
  T009's census had not listed them. Orchestrator ruling on the Phase 3 report: re-pinned to the 106 behaviour (a silent
  `Disconnected`, nothing written, nothing consumed), each comment naming "106 D1", recorded as research D9's "FR-007
  census amendment" with its re-derivation recipe; 038's Cell 11 renamed `Cell11_BadStalePlusBadCreds_CredsWin`. Each
  re-pin RED on `$B` reading the base's answer, GREEN at the head; the evidence file's T051 section has the list and
  the readings.

### Mutant arms for US1 (in `$M` at `H1`)

- [X] T052 [US1] In `$M`'s `src/session/session.cpp`: M1: the consuming mTLS gate placed before the stay-in-arm validate Reject; RED:
  `IdentityFirst106.Admitted_StayInArmReject_NextLogonActive`.
- [X] T053 [US1] In `$M`'s `src/session/session.cpp` (`logon_identity_refuses_`): M1a: the precheck's credentials half skipped when the session is not mTLS; RED:
  `IdentityFirst106.ValidateFirst_RefusedCredentials_NonMtls`.
- [X] T054 [US1] In `$M`'s `src/session/session.cpp`: M1b: the consuming gate at D1 item 4 refuses an admitted identity, with
  the second arm (orchestrator ruling on the Phase 3 report) at the consuming gate in front of the held validate-first
  Reject that disconnects; RED: every `IdentityFirst106.Admitted_<Refusal>` cell (no answer). The item-4 arm cannot
  reach `Admitted_ValidateFirst_Acceptor` and `Admitted_ValidateFirst_Initiator` (their frames never pass
  `interpret_logon`); the held-Reject arm is theirs. Both arms are quickstart §4's M1b row; readings in the evidence
  file, T052 to T055.

- [X] T055 [US1] In `$M`'s `src/session/compid_authorization_policy.cpp`: M1c: `has_logon_validator()` returns true whatever is installed; RED:
  `IdentityFirst106.Admitted_UnreadableNoValidator_ValidateFirstReject` (no Reject).

**Checkpoint**: US1 is GREEN and every US1 mutant RED; the identity gates run first in both arms.

---

## Phase 4: User Story 2 — a refused Logon does not consume its MsgSeqNum (Priority: P1) (#703 and the class ruling; research D2; contract C-2; data-model E-2)

**Goal**: `SeqnumManager::classify_inbound` classifies without advancing; each arm commits the advance at one point;
every refusal between keeps NextNumIn in FR-006's sense (FR-006, SC-002).

**Independent test**: `-L 106` with `NextNumIn106.*`, plus `-L 104` and `-L 099` unfiltered.

### RED cells for US2 (write first)

- [X] T056 [US2] `tests/session/test_106_identity_first.cpp`, the pattern `NextNumIn106.<Role>_<Refusal>` for every
  acceptor row of research D2's table whose 106 column is "unchanged" or "unchanged; no unit runs" (as re-derived by
  T008): `NextNumIn106.Acceptor_S3_1137` *(proposed)* (FIXT routing, inbound validation on, spec US2 scenario 1: the
  Reject is sent), `NextNumIn106.Acceptor_Peer141_S3_NoAdvanceNoReset` (also asserts the store's `reset_to` not called),
  `NextNumIn106.Acceptor_S3_RejectAssignFails` *(proposed)* (`inbound_reject_leaf_`'s error returned). Each asserts the
  store's NextNumIn at its value before the frame and the in-memory NextNumIn equal to the value the arm classified the
  frame against (an acceptor hydrates inside the arm, so not a value read before the feed). RED on the base: consumed in
  memory. Plus [guard: the knob's reset moved after S3] `NextNumIn106.Acceptor_ResetOnLogon_S3_KeepsTheReset`, which
  pins `L-106-a` (NextNumIn 1 and the store reset). Orchestrator ruling on the Phase 4 report: the guard stays as
  written, and `NextNumIn106.Acceptor_ResetOnLogon_S3_InMemoryOne` [seam: M6a] (`tests/session/test_106_seams.cpp`)
  pins the in-memory NextNumIn at exactly 1 after the reset.
- [X] T057 [US2] `tests/session/test_106_identity_first.cpp` (same file), every initiator row "unchanged": `NextNumIn106.Initiator_S3` (an ordinary acknowledgement,
  spec US2 scenario 2), `NextNumIn106.Initiator_Asked141_S3_NoAdvanceNoReset` (also `reset_to` not called),
  `NextNumIn106.Initiator_S3_RejectAssignFails`, `NextNumIn106.Initiator_Strict` (the `bilateral_strict` silent refusal),
  `NextNumIn106.Initiator_789TooHigh`, `NextNumIn106.Initiator_789Invalid`, `NextNumIn106.Initiator_789Below_WalkFails`
  (the `!h789` error return included) and `NextNumIn106.Initiator_789Below_Superseded` (all *(proposed)* but
  `Initiator_S3`). Same oracle. RED on the base: consumed in memory. Spec US2 scenario 3 (the unasked 141=Y
  acknowledgement refused at S3) is as on the base: covered by the existing 099 cells
  `W5PerShape.Initiator141YWith1137_4_NoReset` (shapes T, A, L) and the `kLogonSent` row LS-e `Logon 141=Y 1137=4` run
  by `W5LogonSent.Ending` (`tests/session/test_099_establishment.cpp`; instance names as T009 lists them), re-run
  unfiltered. The
  residual after a unit (`L-106-a`; Q3) keeps its base pin: `UnaskedResetAck104.With789_TooHighAfterReset_Logout`, run in
  `-L 104` unfiltered.
- [X] T058 [US2] `tests/session/test_106_seams.cpp`: `NextNumIn106.CommitRefused_Disconnected` [seam: the
  manager frozen between classification and commit through T031's hook] (research D9 row "FR-006 commit", N-4):
  `Disconnected` once, nothing sent after it, NextNumIn unconsumed in memory and store. Its mutant: T069.
- [X] T059 [US2] Every existing cell T009 classed "S3, strict or 789 refusal" changes its pinned number to the
  unconsumed value, starting with `W5Scan.NextNumInIsTheHeadersAndNoReset` (`tests/session/test_099_establishment.cpp`);
  each re-pin is named in the evidence file with its old and new value and is RED on the base.
  T009 classed no cell; at H2 `-L 099` failed 134 cells, each differing from its pin only in the trace line's
  `c`→`k`. Orchestrator ruling on the Phase 4 report (the owner's class ruling: every Logon refusal keeps NextNumIn,
  FR-006; spec.md Clarifications, R17): all 134 re-pinned to `k`, by the run-based recipe of research D9's second
  FR-007 census amendment, each group's comment naming "106 D2". RED on `$B` reading the base's `c`, GREEN at the
  head; the evidence file's T059 section has the list and the readings.
- [X] T060 [US2] `.specify/decisions/106-logon-guards-write-bounds-evidence.md`: Commit the cells (`C3`); read T056, T057 and T059 on `$B` (the 099 and 104 files
  overlaid as named); record per cell its RED reason or guard status and the counts.

### Implementation for US2

- [X] T061 [US2] `include/fixpp/session/seqnum_manager.hpp`, `src/session/seqnum_manager.cpp`:
  `SeqnumManager::classify_inbound(seq)` (data-model E-2: the body of `check_inbound` without the advance; the lock, a
  drained lock giving `session_already_closed`, `frozen_`, too low, too high, `seqnum_max`). A member function only.
  Its header comment, beside `check_inbound`'s, states E-2's contract: `noexcept`, the error per case above, no
  advance.
- [X] T062 [US2] `src/session/session.cpp`: `on_inbound_not_connected_`: the classification where `check_inbound` was; the commit
  (`co_await seqnum_mgr_.check_inbound(seq)`) after the 1137 block, before `LogonReceived`, when
  `logon_inbound_advanced`, so the reply's 789 reads the advanced value (F-4); a refused commit records `Disconnected`
  unless superseded and returns success. INV-106-1 holds: from the arm's entry to the commit only a reset unit, the
  knob's reset and hydration change `next_inbound_`.
- [X] T063 [US2] `src/session/session.cpp`: `on_inbound_logon_sent_`: the classification; the commit after the 789 honour and its superseded
  test, before `Active`, only when `logon_inbound_advanced_init && !peer_ack_sent_reset_flag` (the units keep writing
  absolute targets from the classification); a refused commit as in T062.
- [X] T064 [US2] `src/session/session.cpp`: The commit-freeze hook's call site between classification and commit in both arms (T031), a
  no-op when unset.
- [X] T065 [US2] `src/session/session.cpp`: Comments at both commit points naming the decision: "106 D2: the advance is committed here, so
  every refusal before it keeps NextNumIn (closes L-104-8)", and at the two locals: `logon_inbound_advanced(_init)` now
  means "classified in sequence; consumed at the commit". No result in a comment.
- [X] T066 [US2] `.specify/decisions/106-logon-guards-write-bounds-evidence.md`: At the head: `-L 106` (seam entry included), `-L 104`, `-L 099` unfiltered GREEN; record counts.
  Commit `H2`; `codegraph sync`. At H2 `-L 099` read 13/15 (T059); after T059's re-pins and the rulings' cells
  (`01a586a4`) `-L 106`, `-L 104` and `-L 099` read GREEN.

### Mutant arms for US2 (in `$M` at `H2`)

- [X] T067 [US2] In `$M`'s `src/session/session.cpp`: M6a: the acceptor arm commits the advance at the classification (the base's `check_inbound`); RED: every
  `NextNumIn106.Acceptor_*` cell but the residual guard.
- [X] T068 [US2] In `$M`'s `src/session/session.cpp`: M6b: the initiator arm commits at the classification; RED: every `NextNumIn106.Initiator_*` cell.
- [X] T069 [US2] In `$M`'s `src/session/session.cpp`: The commit's result ignored (a refused commit proceeds to `LogonReceived` / `Active`); RED:
  `NextNumIn106.CommitRefused_Disconnected`.
- [X] T070 [US2] In `$M`'s `src/session/session.cpp`: The acceptor knob's `reset_on_logon` reset moved after S3; RED:
  `NextNumIn106.Acceptor_ResetOnLogon_S3_KeepsTheReset`.

**Checkpoint**: US1 and US2 GREEN; the Logon arms are done.

---

## Phase 5: User Story 3 — a peer that stops reading cannot hold the session (Priority: P1) (#714a; research D3, D4, D11; contract C-3, C-4.6; data-model E-3, E-4, E-10)

**Goal**: the give-up fires at its instant whether or not a write is blocked (the connection bound decides while the
loop is suspended in its own emit), the close fails every blocked write, and every ending has one claim (FR-008,
FR-009, FR-017, SC-003).

**Independent test**: `-L 106` with `GiveUp106.*`, `OneEnding106.*` (limit off) and the US3 `ConnectionBound106.*`
cells; `-L 105` and the 538 binary as guards.

**Open Q1 (the Phase 1–2 report; pre-existing; orchestrator ruling, decided in this phase).** The drained-mutex branches
of both Logon arms' sequence check and of `on_inbound_active_out_of_sequence_` write `Disconnected` unconditionally;
`close()` records `Disconnected` before its `seqnum_mgr_.drain()`, so `close()` plus an arm suspended at the manager's
lock gives two entries (T015's rows marked "open question"). If D11's ending claim or `record_disconnected_unless_already_()`
removes it with a one-line change at each site, that change lands in this phase with a cell and is named in the bundle;
otherwise it is recorded as an L-106 limitation with a follow-up issue.
**Disposition (Phase 5):** recorded as `L-106-g` (research D8) with a follow-up issue to file (the evidence
file's "Follow-ups to file"): the one-line change at each site is `record_disconnected_unless_already_()`, but a
cell needs an arm held at the manager's lock while `close()` reaches `seqnum_mgr_.drain()`, a seam the bundle does
not name.

### RED cells for US3 (write first)

- [X] T071 \[P\] [US3] `tests/session/test_106_give_up.cpp`: `GiveUp106.{TestRequestBlocked, HeartbeatBlocked,
  BehindBlockedSend, ResendHeldNoPending, RejectBlocked}`, limit off, H = 10 s, T = 10 s, B = 500 ms, both roles where
  the rig allows (research D9 row FR-008; spec US3 scenarios 1 to 3; contract C-3.1 to C-3.4). Each: no slot-74 record
  at G − 1 ns and one at G; open at G + B − 1 ns, closed at G + B; no complete Logout at the peer; and, in a twin arm,
  a peer read released inside B gets the Logout after the frames ahead of it. `HeartbeatBlocked`: one peer frame at
  t = 5 s, so the Heartbeat falls due at 10 s and the TestRequest at 15 s; G = 35 s + 1 ns. Inequalities as D9 states
  them (in `TestRequestBlocked` the TestRequest and Heartbeat instants tie at 10 s and the loop's order emits the
  TestRequest; nothing else closes by G + B). `ResendHeldNoPending` asserts Text(58) `TestRequest unanswered` without
  the TestReqID clause and an empty `test_req_id` in the slot-74 record (Q2). FR-009 in each: each blocked and queued
  write returns an error (`dispatch_aborted`; `FIXPP_ERR_CANCELLED` is the C mapping, unchanged, B-105-8) and none
  hangs; FR-017 for the tail the close resumes (the blocked send's F9 block in `BehindBlockedSend`, the Reject leaf in
  `RejectBlocked`, the loop's give-up path in the emit cells). RED on the base (no close at G + B). Plus the guard
  `GiveUp106.ResendHeldPending` *(proposed name for "`ResendHeldNoPending`'s pending twin")*: the same hold with a
  TestRequest pending, given up at L + 3T + 1 ns on the base too.
- [X] T071a [US3] `tests/session/test_106_give_up.cpp` (same file): `GiveUp106.HeartbeatReplyBlocked` *(proposed)* (research D9
  row FR-009; T015's reachable row `inbound_heartbeat_reply_leaf_`): `RejectBlocked`'s parameters (limit off, H = T = 10 s,
  B = 500 ms). The peer's TestRequest at L draws the Heartbeat reply and that write blocks; the loop's first emit after L
  falls due by L + 10 s and queues behind it, so G = L + 3T + 1 ns = L + 30 s + 1 ns follows it by at least 20 s; the peer
  is silent after L, so nothing else closes by G + B. T071's readings at G and G + B; the blocked reply and the queued
  emit return an error (`dispatch_aborted`); FR-017 for the leaf's write tail (one new `Disconnected` entry). RED on the
  base (no close at G + B); M2 (T092); M13 at that tail (T098).
- [X] T071b [US3] `tests/session/test_106_give_up.cpp` (same file): `GiveUp106.BusinessRejectBlocked` *(proposed)* (research
  D9 row FR-009; T015's reachable row `inbound_business_reject_leaf_`): T071a's parameters and instants, with an
  application whose `fromApp` rejects the peer's NewOrderSingle at L, which draws the BusinessMessageReject (35=j) whose
  write blocks. The same readings and FR-017 for that leaf's write tail. RED on the base (no close at G + B); M2 (T092);
  M13 at that tail (T098).
- [X] T072 [US3] `tests/session/test_106_give_up.cpp` (same file): `GiveUp106.RejectBlocked_EndingBoundSpawnFails_ClosedAtG` (research D9 row "FR-008
  spawn failure"): `RejectBlocked`'s rig with `session_test_access::set_ending_bound_spawn_fails` returning true. At G:
  one slot-74 record, the transport closed (the peer's read ends), one `Disconnected` entry, the blocked Reject write and
  the queued emit return an error, no Logout; the spurious check at G + B + 1 s has no competitor. RED on the base (no
  give-up at G); M14 (T094).
- [X] T073 [US3] `tests/session/test_106_give_up.cpp` (same file): `GiveUp106.EmitHeldInStore_GivesUpAtG_ClosesAtGPlusB` (research D9 row "FR-008 store
  phase"; contract C-3.3's exception): the loop's TestRequest held inside its `MessageStore::store` call
  (`t105::GateStoreFactory`, as `LivenessTiming105.FreshWindow_FromTheWrite_StoreDelayAtLeastT` uses it); no slot-74 record
  at G − 1 ns, one at G; open at G + B − 1 ns, closed at G + B; no `Disconnected` and no join until the store returns
  (`L-093-12`, amended); the store released at teardown. RED on the base (no slot-74 record at G); M2 (T092).
- [X] T074 [US3] `tests/session/test_106_give_up.cpp` (same file), the two disclosure guards: `GiveUp106.PreActive_LimitOff_NotCutByEstablishment` (Engine
  rig, limit off, the reply Logon's write blocked, `logon_timeout_ms` 1 s; still open at `logon_timeout_ms` + 1 s;
  `L-106-f`; a fix turns it RED) and `GiveUp106.ZeroLogoutTimeout_StaysOpen` (B = 0, emit blocked; still open at
  G + 10 s, which follows the default B a regression would use by 8 s; `L-105-f`) — [guard: an unconditional close at
  G] (T095).
  **Done (orchestrator ruling A on the Phase 5 remainder report):** `ZeroLogoutTimeout_StaysOpen` written (C4, GREEN
  on `$B` and at the head). `PreActive_LimitOff_NotCutByEstablishment` is dropped: no seam can block the Engine's own
  accept-path write (the Engine's accept path mints its own TCP transport). `L-106-f` stays disclosed; its basis is the
  code reading recorded in the evidence file's T074 section (the establishment deadline is tested only between frames
  and while reading), and the cell is on T167's and research D9's no-cell list with that reason.
- [X] T075 \[P\] [US3] `tests/session/test_106_one_ending.cpp`: the `OneEnding106.<First>Then<Second>` pairs whose rig
  names no new symbol (write_timeout_ms unset), at least `OneEnding106.GiveUpThenClose` (`close()` at C = G + 100 ms < G + B,
  `logout_disconnect_timeout_ms` 5000 ms, the mock step issued at once after `close()`, a guard: `close()` takes no
  claim). Each asserts FR-017. A pair that needs the limit is written in T105. Record which pairs land here.
- [X] T076 [US3] `tests/session/test_106_seams.cpp`, the connection-bound cells whose rig needs no write limit
  (research D9 row C-4), each [seam]: `ConnectionBound106.SpawnFails_AtLoop` (fail closed: the loop takes the claim,
  `Disconnected`, closed once, zero reason records), `ConnectionBound106.WaitThrows` (the claim free: claim,
  `Disconnected`, closed once; no record, no Logout), `ConnectionBound106.SpuriousWake_NoEnding` (a wake with nothing due
  ends nothing), `ConnectionBound106.Teardown_ConnectionEnd_Parked` (the counter reads 0 after a bounded pump, read after
  the `Disconnected` edge and before any `close()`, drain or stop). `ConnectionBound106.Teardown_Close_*` and `ConnectionBound106.Teardown_EngineStop_*` are
  reported as covered by the `Disconnected` edge (data-model E-3's joiner table) and not counted; the evidence file
  records that disposition. They compile only after T078 and T083 (the `session_test_access` reads of `ending_claim_`
  and the bound's counter).
- [X] T077 [US3] `.specify/decisions/106-logon-guards-write-bounds-evidence.md`: Commit the cells (`C4`); read T071 to T075 on `$B` (`-L 106 -LE 106_seam`); record per
  cell its RED reason or guard status and the counts. The seam cells of T076 are read under their mutants
  (T099).

### Implementation for US3

- [X] T078 [US3] `include/fixpp/session/session.hpp`, `src/session/session.cpp`, `tests/support/session_test_access.hpp`: The ending claim (data-model E-10, research D11): `enum class ending_claim : std::uint8_t { none,
  give_up, write_timeout, ending_logout, establishment_timeout, internal }`, the strand-confined member `ending_claim_`,
  `claim_ending_(ending_claim who) noexcept` (refuses when taken, when `closing_or_stopped_()` holds, or when
  `reconnect_drain_in_flight_` holds; otherwise sets and returns true, no suspension between test and set); reset to
  `none` in `begin_connection_()`; the `Disconnected` edge in `record_state_transition_` takes it as `internal` when it is
  `none`. `tests/support/session_test_access.hpp` gains the read of `ending_claim_` the seam cells need.
- [X] T079 [US3] `src/session/session.cpp`: The `Disconnected` census applied (D11; T015's table): every row classed reachable
  becomes `record_disconnected_unless_already_()`: the liveness loop's TestRequest and Heartbeat `store_then_emit` failure
  tails in `run_liveness_loop_body_`, `send_body_`'s F9 block, `emit_initiator_logon_`'s write tail, the two-argument
  `emit_session_reject_`'s write tail, and the write tails of `inbound_resend_request_leaf_`,
  `inbound_heartbeat_reply_leaf_` and `inbound_business_reject_leaf_`. The Reject leaf (`inbound_reject_leaf_`) holds no
  such site (its write tail discards `emit_r` and records nothing), and the reply Logon's and the 789 resend frames'
  failure tails are already conditional, so nothing changes there. Every row classed unreachable or already conditional
  is left as it is and keeps its reason in the table.
- [X] T080 [US3] `src/session/session.cpp`: 105's other ending Logouts claim (D11 row 3; the Ending-claimant table): `claim_ending_(ending_logout)`
  where each takes the Logout owner or enters writing, whichever comes first, in `inbound_logout_leaf_` (kinds other
  than `r1`), `refuse_logon_with_logout_`, `inbound_logon_sent_logout_leaf_` and `honor_peer_next_expected_`; a refused
  claim writes nothing and records nothing. `run_logout_phase1` and `inbound_logout_leaf_` for kind `r1` neither take nor
  test it. No reason record is added at these sites.
- [X] T081 [US3] The establishment deadline as a claimant (D11 row 4): `session_engine_access::claim_ending` in
  `src/session/session_engine_access.hpp`; the Engine pump's expiry in `src/session/engine.cpp` claims
  `establishment_timeout` before `note_establishment_timeout`, replacing 105's "the session recorded no ending of its own"
  test, with a comment naming "106 D11 supersedes 105's no-ending-recorded test: the claim decides".
- [X] T082 [US3] `include/fixpp/session/session.hpp`, `src/session/session.cpp`: `decide_give_up_()` extracted from the loop's give-up block (research D3 "The decision";
  data-model E-3): the claim first (refused: nothing, return false); the slot-74 record; the Logout owner;
  `enter_ending_write_(ending_kind::r1, …)` with its budget from the decision; on `refused` (105's ending bound cannot be
  spawned) it records `Disconnected` (unless already) and, when `state_ == lifecycle::open`,
  `spawn_connection_close_(live_transport_shared_())` then `connection_end_owed_.reset()`, and returns; on `unlimited` no
  close comes. The loop's give-up block calls it (N-12: with the claim held elsewhere the block now records nothing and
  exits). The Text is built from `pending_test_req_id_` as today; with none pending, `TestRequest unanswered` without the
  TestReqID clause (Q2; data-model E-4 rows 3 and 5).
- [X] T083 [US3] The connection bound (research D3 "The primitive"; data-model E-3): `run_connection_bound_`, one
  coroutine per connection, spawned by the liveness loop at its start on the loop's executor (never `executor()`), with
  `connection_bound_spawn_fails_`'s seam (refused spawn: the loop takes the claim, records `Disconnected` and ends; the
  connection closes at its exit); the members `connection_bound_` (`running`, `gen`, `shipping`, `wake`) and
  `connection_bound_counter_` (a `std::shared_ptr<std::atomic<int>>` joined by `counter_join`), with its read in
  `tests/support/session_test_access.hpp` for the seam cells; the one waiting callee
  `connection_bound_wait_(timer, deadline)` with the timer a local of the bound's frame: on `system_clock_source` (tested
  once at the spawn by `dynamic_cast`) `expires_at(deadline)` / `expires_at(max)` and one
  `async_wait(asio::as_tuple(asio::use_awaitable))`, never `Clock::sleep_until`; on any other `Clock`
  `timer.async_wait(…) || detail::await_deadline(*effective_clock_, deadline)`; `connection_bound_wait_throws_`'s seam and
  the failure exit (an exception, or a deadline arm completed before its instant: with the claim free, claim,
  `Disconnected`, close once; with it taken, nothing). No cancellation slot, no lock. The shape is compared with
  T019's list (one new spawn). Re-derive (research D3, "What it allocates"): the bound's only `co_spawn`
  (`git grep -n 'run_connection_bound_' -- src`), and a read of the shipping branch of `connection_bound_wait_` showing it
  names no `Clock` member; recorded in the evidence file.
  **Implemented (`aa146ac7`); re-derivation recorded at H3 (evidence file, "T083 and T084").**
- [X] T084 [US3] The bound's joiners, wakes and exit terms (data-model E-3's two tables, in their order): exit on
  `gen` changed, `Disconnected`, `closing_or_stopped_()`, `reconnect_drain_in_flight_` or `ending_claim_` taken; the
  give-up row before the write row at one re-test; wakes at the loop's guard (both edges), `close()` and
  `Engine::stop()`'s step 1 before their joins, `drain_for_reconnect_`'s entry beside `wake_ending_logout_bound_()`, and
  the `Disconnected` edge; `close()` and `drain_for_reconnect_` join `connection_bound_counter_` by `FIXPP_JOIN_COUNTERS`
  beside their existing counters; `install_reconnected_transport` and `~Session` assert it zero, as `~Session` asserts
  `ending_logout_bound_counter_`. Re-derive first and after (data-model E-3):
  `git grep -n 'FIXPP_JOIN_COUNTERS\|connection_bound_counter_' -- src/session/session.cpp`, each join read for the state
  the session can be in when it is reached, and `Engine::stop()`'s steps in `src/session/engine.cpp`; the result goes in
  the evidence file against data-model E-3's joiner table.
  **Implemented (`aa146ac7`); orchestrator rulings 1 and 2 applied (`29626eb4`):** the liveness loop's exit sets
  `connection_bound_.loop_left` and wakes the bound (data-model E-3's joiner row "teardown without `close()`");
  `~Session` asserts the counter, as it asserts 105's bound; the rigs that destroy an Active session with neither
  `close()` nor a drain declare `close_before_destroy`. Re-derivation recorded at H3 (evidence file).
- [X] T085 [US3] `src/session/session.cpp`: The loop's arming (research D3 "The loop arms it", D4): `emit_in_flight_` set when a TestRequest or
  Heartbeat emit starts (after its replay-gate shared hold, and for a TestRequest after `pending_test_req_id_` is set),
  with an RAII guard in the loop's frame that clears it and wakes the bound at the return or an exception; while it is
  set the bound's deadline is G = L + 3T + 1 ns (`detail::held_test_request_cap`), and at G with
  `liveness_may_originate_()` the bound calls `decide_give_up_()`. After its emit the loop tests the claim first: the
  give-up's with an ending phase entered → its existing write path with the owner already claimed; the give-up's with
  none (refused bound) → no Logout, return; otherwise its failure branches through
  `record_disconnected_unless_already_()`. A written TestRequest keeps 105's instant (row 1 of data-model E-4).
- [X] T086 [US3] `src/session/session.cpp`: The held cap with none pending (research D3 "The resend-answer case"; data-model E-3): the loop's
  replay-gate wait becomes `wait_for_wake() || await_deadline(held_test_request_cap(...))` with none pending too, and
  `held_cap` drops its `!pending_test_req_id_.empty()` term.
- [X] T087 [US3] `src/session/session.cpp`: Supersession comments (research "Supersession"; Execution rules "Comments"): at the bound's
  instant "106 D4 supersedes 105 E-4's `tr_written_at` count for an emit not returned (ruling R1)"; at the TestRequest
  fit check "106 D4 supersedes 105 C-1 row 8's stated reason; the rule stays"; at the held cap "106 D3 supersedes 105
  T119b's implementation reading: capped with none pending too"; at `decide_give_up_()` "106 D3 supersedes 103 D5′.4 /
  105 D3 (the loop alone decides the give-up): the connection bound decides while the loop is suspended in its own
  emit, under one claim (D11)"; at the loop's inline emits "106 D3 bounds the inline emits of
  .specify/536-537-538-recovery-liveness.md §3".
- [X] T088 [US3] Deferred item (plan "Deferred to tasks.md", "The RAII clears"), for `emit_in_flight_`: re-derive that
  every exception and `catch` return of the loop's emit reaches the `Disconnected` edge (whose exit ends the bound first,
  so a mutant of the clear is masked); a return that does not reach it gets a cell in `tests/session/test_106_seams.cpp`
  with the clear's mutant RED. Record the walk and its result in the evidence file.
- [X] T089 [US3] `src/session/session.cpp`: Deferred item (plan "Deferred to tasks.md", "The loop's give-up block with the claim refused";
  research D11 N-12): name the state in which the loop's give-up block meets a claim held elsewhere —
  `liveness_may_originate_()` stops the write limit, the ending Logouts, the establishment deadline and a drain from
  Active before the block, which leaves `close()` between setting `closing` and its `Disconnected` record — and either
  write the cell `OneEnding106.CloseDuringGiveUpBlock_NoRecord` *(proposed)* (no slot-74 record, one `Disconnected`) with
  the mutant "the block's claim test deleted" RED, or record the block unreachable with the reading that shows it.
- [X] T090 [US3] Re-run T017 at the head: the one new `sleep_until` / `await_deadline` caller is the held cap's
  wait (it replaces the loop's own sleep); no hit is the connection bound's shipping branch (#715 out of scope).
- [X] T091 [US3] `.specify/decisions/106-logon-guards-write-bounds-evidence.md`: At the head: `-L 106` (seam entry included) GREEN; guards unfiltered: `-L 105`
  (`LivenessTiming105.*`, `HeldTestRequestCap105.*`, `WriteLimit105.*`,
  `PreActive105.SilentPeer_StillClosedAtEstablishmentDeadlineWithEvent`, `LivenessTiming105.GiveUp_BoundSpawnFails_StillEof`)
  and the 538 binary (`ReplayGate588.R588g*`, `R588h*`); spec US3 scenario 4 (a reading peer: no instant moves). Record
  counts. Commit `H3`; `codegraph sync`.

### Mutant and spurious-hit arms for US3 (in `$M` at `H3`)

  **Resolved by the orchestrator's rulings 1 to 3 (`29626eb4`); H3 = `05c21065`, its readings in the evidence file.** The
  earlier stop read: at `aa146ac7` `-L 106`, `-L 105`, `-L 104`, `-L 099` pass; `-L 538` fails:
  `ReplayGate588.R588c_*` (two) and `R588g_AnswerInsideTheFreshWindow_KeepsTheSession` fail at their teardown drain
  (the bound's parked timer is outstanding work after a teardown with no `close()`), and
  `CancelledOutboundStoreMeasurement.Returned_StoreCancelledAtItsLock` pins two `Disconnected` entries that T079's
  conversion makes one. H3 not committed as such.
- [X] T092 [US3] In `$M`'s `src/session/session.cpp`: M2, arm 1: the loop's arming of the connection bound for its emit deleted; RED: every `GiveUp106` cell
  except the guards (nothing else closes by G + B: the spurious-hit arm of FR-008), `EmitHeldInStore_*` included.
  **Done (orchestrator ruling B on the Phase 5 remainder report):** the diagnostic (a stderr line at
  `emit_in_flight_guard`'s construction, `$M` at H3) reads no construction in any of the three
  `ResendHeldNoPending*` cells, both roles: they do not reach the mutated site (the loop gives up through the held
  cap before any emit starts). They are M2 arm 2's cells (T093, RED there), and T092's named set is every `GiveUp106`
  cell except the guards and those three; every cell that reaches the site reads RED under arm 1 (evidence file,
  "T092 to T099" and "Ruling B").
- [X] T093 [US3] In `$M`'s `src/session/session.cpp`: M2, arm 2: the held cap's pending term restored; RED: `GiveUp106.ResendHeldNoPending`; the pending twin
  stays GREEN.
- [X] T094 [US3] In `$M`'s `src/session/session.cpp`: M14: `decide_give_up_()`'s `refused` branch records `Disconnected` and returns without the close; RED:
  `GiveUp106.RejectBlocked_EndingBoundSpawnFails_ClosedAtG` (still open at G + B + 1 s). Record that
  `LivenessTiming105.GiveUp_BoundSpawnFails_StillEof` stays GREEN under M14 (the loop's exit still closes): a guard of the
  base path only.
- [X] T095 [US3] In `$M`'s `src/session/session.cpp`: An unconditional close at G (the `unlimited` branch closes); RED: `GiveUp106.ZeroLogoutTimeout_StaysOpen`.
- [X] T096 [US3] In `$M`'s `src/session/engine.cpp` and `src/session/session_engine_access.hpp`: M9″: the establishment expiry's `claim_ending` always refused; RED:
  `PreActive105.SilentPeer_StillClosedAtEstablishmentDeadlineWithEvent` (no event).
- [X] T097 [US3] In `$M`'s `src/session/session.cpp`: M9 (`claim_ending_`'s test deleted) with M9′ (E-3's exit on a taken claim deleted), over the pairs of
  T075: RED (two reasons or two entries), or the pair reported as a guard of its state test, not counted toward M9
  (research D9 row FR-017).
- [X] T098 [US3] In `$M`'s `src/session/session.cpp`: M13 per reachable site a US3 cell returns through (T015's table), one arm each: the site's
  `record_disconnected_unless_already_()` restored to the raw `record_state_transition_(fsm_state::Disconnected)`; RED
  (two `Disconnected` entries): the F9 block by `GiveUp106.BehindBlockedSend`, the loop's TestRequest and Heartbeat tails
  by `GiveUp106.TestRequestBlocked` and `GiveUp106.HeartbeatBlocked`, `inbound_heartbeat_reply_leaf_`'s tail by
  `GiveUp106.HeartbeatReplyBlocked` and `inbound_business_reject_leaf_`'s by `GiveUp106.BusinessRejectBlocked`. The
  Reject leaf has no arm: its write tail records nothing, so `GiveUp106.RejectBlocked` keeps its FR-017 assertion (one
  `Disconnected`, the close's) without an M13 arm.
  **Done (orchestrator ruling B on the Phase 5 remainder report):** the diagnostic (a stderr line at each of the five
  sites with whether the state was already `Disconnected`, `$M` at H3) reads, both roles: the loop's TestRequest
  and Heartbeat tails are not reached in `TestRequestBlocked` and `HeartbeatBlocked` (the loop's
  `ending_claim_ == give_up` test precedes its `!emit_r` tail); the F9 block, the Heartbeat reply's and the
  BusinessMessageReject's tails are reached once each, with the state not yet `Disconnected` (each is the first
  recorder; the loop's give-up write half is the second, and it dedupes), so those three arms are EQUIVALENT in these
  cells. The mutated code is not redundant: at a write-limit expiry the bound records `Disconnected` (data-model E-5,
  step 4) before the close fails the cut write, so each of these tails is then the second recorder. Every arm is
  therefore read at T130 on the `WriteTimeout106` cell whose cut returns through its site: `Send` (F9),
  `TestRequest`, `Heartbeat`, and the two classes the ruling adds, `HeartbeatReply` and `BusinessReject`
  (T100). The evidence file's "Ruling B" section has the diagnostic's readings.
- [X] T099 [US3] In `$M`'s `src/session/session.cpp`: The US3 connection-bound arms: the spawn seam's failure path and the wait seam's failure path each
  deleted (the cells of T076 read RED: `SpawnFails_AtLoop` no close, `WaitThrows` no close); M12's
  `ConnectionEnd` arm (the `Disconnected` edge's wake deleted; its two exit terms are redundant): RED
  `ConnectionBound106.Teardown_ConnectionEnd_Parked` by its counter assertion, never a hang; `SpuriousWake_NoEnding`
  under "a wake ends the bound" RED.

**Checkpoint**: US3 GREEN, its mutants RED; the bound decides the give-up; one claim per connection.

---

## Phase 6: User Story 4 — an operator can opt in to a time limit on every write (Priority: P2) (#714b; research D5; contract C-4; data-model E-3, E-5, E-6, E-8)

**Goal**: with `write_timeout_ms` set, a non-Logout live write that does not complete within it, counted from its
`async_write` call, ends the session with no Logout, one warn record and the close; the cut call returns
`session_write_timeout` (FR-010 to FR-012, FR-017, SC-004). Depends on US3's connection bound and claim.

**Independent test**: `-L 106` (every cell below is in the seam entry), the C-ABI error entries, and
`alloc_guard_106_connection_bound_mallocnesia` on `linux-clang-release`.

### Cells for US4 (write first; every one names `write_timeout_ms`, so [seam], `tests/session/test_106_seams.cpp` unless named)

- [X] T100 [US4] `tests/session/test_106_seams.cpp`: The pattern `WriteTimeout106.<Class>` (research D9 row FR-010; contract C-4.2, C-4.3; spec US4
  scenario 1), one cell per class: Active `WriteTimeout106.Send`, `Heartbeat`, `TestRequest`, `ResendFrame`, `Reject`,
  `SequenceReset`, `ResendRequest`; pre-Active `WriteTimeout106.InitiatorLogon`, `ReplyLogon` and `Honor789Frame` (the
  `honor_peer_next_expected_` frame) — class names *(proposed)* but `Send`. The `Reject` class's rig draws the two-argument
  `emit_session_reject_` (research D9 row FR-010: `on_inbound_active_from_admin_failed_`, or `on_inbound_active_unsupported_`),
  shown by its frame's SessionRejectReason(373) = 3, so T130's arm at that form's write tail reads it. W = 300 ms, HeartBtInt 1 s; in Active the
  peer sends a Heartbeat every 1 s, so the give-up cannot fire; the `TestRequest` class sets `test_request_threshold`
  500 ms and blocks the first 35=1 write (S = L + 500 ms; the cut at L + 800 ms precedes the next peer frame at L + 1 s
  and the bound's give-up row at L + 1.5 s + 1 ns); every other class keeps T = 1.2 s (margin 200 ms). Pre-Active: an
  Engine rig, S within 100 ms of the accept, `logon_timeout_ms` 10 s (the cut precedes it by at least 9.6 s). Each
  asserts open at S + W − 1 ns and closed at S + W, one write-timeout record
  (`session ended: write timed out: limit_ms=300 error=<n>`, `<n>` the value of `session_write_timeout`), the cut call's
  `session_write_timeout`, `dispatch_aborted` for the writes queued behind it, no Logout, and FR-017 (`onLogout` once
  from Active, none before Active). Mutants: M3′ and M3″ (T119, T120), M3‴ for `Send` (T121), and per class the M13
  arm of its tail (T130).
  **Done** (`2100cc67`, `1b03bc6d`; GREEN at H4 = `748db96f`): `WriteTimeout106.CutAtTheLimit/<Class>_<Role>`, both roles for every
  Active class; the classes include `HeartbeatReply` and `BusinessReject`, added by orchestrator ruling B so that T098's two
  leaf arms have a cell whose cut returns through them. The pre-Active classes `InitiatorLogon`, `ReplyLogon` and
  `Honor789Frame` run on the direct rig (a proposed substitute: no seam holds an Engine acceptor's reply Logon write, the
  T074 finding; on a direct Session no establishment deadline runs, so none competes). The cut call's
  `session_write_timeout` is asserted where a caller observes the cut write's error (`Send`, `InitiatorLogon`, `Reject`,
  `ResendRequest`, `HeartbeatReply`, `BusinessReject`, `ReplyLogon`); the loop's emits have no caller, and the walk's
  frames (`ResendFrame`, `SequenceReset`, `Honor789Frame`) return through `replay_outbound_range_`'s own result, which keeps
  `dispatch_aborted` (its contract), so those classes assert the record, the close and FR-017 only. Readings: evidence file,
  "Phase 6". The pre-Active substitution is accepted (orchestrator ruling 5 on the Phase 6 report; research D9 row FR-010).
- [X] T101 [US4] `tests/session/test_106_seams.cpp`: The spurious-hit arm of every `WriteTimeout106.<Class>` cell (research D9 row "FR-010 spurious"):
  the same rig with `write_timeout_ms` 0, still open at S + 1.3 s (margins 200 ms and 500 ms against the TestRequest and
  give-up instants; pre-Active, before `logon_timeout_ms` 10 s); spec US4 scenario 3.
  **Done**: `WriteTimeout106.LimitOff_StaysOpen/<Class>_<Role>` (the same staging, W = 0): open at S + 1.3 s, no
  write-timeout record; GREEN at H4.
- [X] T102 [US4] `tests/session/test_106_seams.cpp`: `WriteTimeout106.QueuedWrite_CountsFromItsOwnStart` (W = 300 ms; B queued at A's start S_A; A
  released at S_A + W − 1 ms; B then blocked and cut at its own start + W = S_A + 2W − 1 ms; M3's cut at S_A + W precedes
  it by 299 ms) and `WriteTimeout106.ResendWalk_ProgressNotCut` (three frames, each released at its own start + W − 1 ms;
  the walk completes in 897 ms, below T = 1.2 s; spec US4 scenario 2, contract C-4.4). Mutant: M3 (T118).
  **Done**: `WriteLimit106.QueuedWrite_CountsFromItsOwnStart` and `WriteLimit106.ResendWalk_ProgressNotCut` (both roles,
  the suite `WriteLimit106`, both in `test_106_seams.cpp`), GREEN at H4. M3 reads at T118.
- [X] T103 [US4] `tests/session/test_106_seams.cpp`: Ending Logouts keep their limits (research D9 rows FR-011; contract C-4.5; spec US4 scenario 4):
  `WriteTimeout106.CloseLogout_KeepsGrace` (W = 300 ms, B = 5000 ms; Active; `close()`'s `run_logout_phase1` Logout write
  blocked from S; the mock step to S + W + 1 ms issued at once after `close()`: no write-timeout record, still open) and
  `WriteTimeout106.EndingLogout_Keeps105Limit` (W = 300 ms, B = 500 ms; the give-up's Logout blocked from S = G; open at
  S + W and at S + B − 1 ns, closed at S + B). Mutants: M4 at each site with its unmasking companions (T123,
  T122).
  **Done**: `WriteLimit106.CloseLogout_KeepsGrace`, `WriteLimit106.EndingLogout_Keeps105Limit` (both roles), GREEN at H4.
- [X] T104 [US4] `tests/session/test_106_seams.cpp`: Deferred item (plan "Deferred to tasks.md", "M4 at the Logout sites other than the R-1 leaf and
  `run_logout_phase1`"): for each remaining row of the Logout-site table (T010), a cell
  `WriteTimeout106.EndingLogout_<Site>` with B > W — `EndingLogout_R2Leaf` (`inbound_logout_leaf_`, an R-2 kind, for
  example the gap deadline's Logout), `EndingLogout_RefuseLogon` (`refuse_logon_with_logout_`),
  `EndingLogout_LogonSentLeaf` (`inbound_logon_sent_logout_leaf_`) and `EndingLogout_Honor789`
  (`honor_peer_next_expected_`), names *(proposed)* — each asserting no write-timeout record at S + W and the cut at
  S + B; or the site named in the evidence file as one whose Logout write cannot be held past W, with the reading that
  shows it. Mutant: T124.
  **Done**: `WriteLimitLogoutSite106.EndingLogout_{R2Leaf, RefuseLogon, LogonSentLeaf, Honor789}` (the R-2 Logout of a
  MsgSeqNum below NextNumIn; the posture-mismatch refusal; the LogonSent stale-SendingTime Logout; the too-high 789
  Logout), each with B = 500 ms > W = 300 ms: no write-timeout record at S + W and the cut at S + B. GREEN at H4; no site is
  named unheld.
- [X] T105 [US4] `tests/session/test_106_seams.cpp`: The `OneEnding106.<First>Then<Second>` pairs with the limit set (research D9 row FR-017; contract
  C-4.6), GiveUp106's H = T = 10 s, W = 300 ms, B = 500 ms unless stated, each with the suspension point it exercises and
  FR-017: `OneEnding106.GiveUpThenWatchdog` (S = G − 100 ms; G < S + W < G + B); `OneEnding106.WatchdogThenGiveUp` (S = G − 500 ms;
  guard: the loop fails `liveness_may_originate_()`); `OneEnding106.WatchdogThenEndingLimit` (R-2 decision at D = S + 100 ms; counted,
  spec.md R18); `OneEnding106.EndingLimitThenWatchdog` (W = 1 s; D + B = S + 600 ms < S + 1 s; guard, spec.md R18);
  `OneEnding106.WatchdogThenClose` (C = S + W + 100 ms; guard: `close()` takes no claim); `OneEnding106.CloseThenWatchdog` (C = S + 100 ms;
  `close()`'s grace a wall-clock timer, B = 5000 ms, the mock step issued at once); `OneEnding106.BothDueAtOneWake` (the
  loop in its TestRequest emit, that write armed, S = G − W, one `step_to` from G − 1 ms to G: the give-up claims, R9);
  `OneEnding106.Watchdog_PreActive_NoEstablishmentRecord` (Engine rig, the reply Logon blocked at S = 0, W = 2 s,
  `logon_timeout_ms` 1 s: at S + W one write-timeout record, no `session_event_establishment_timeout`, no `onLogout`; the
  same at S + W + 1 s). Plus any pair T075 left for this file. Mutants: T127, T128.
  **Partly done** (`2100cc67`): `OneEnding106.{GiveUpThenWatchdog, WatchdogThenGiveUp, WatchdogThenEndingLimit,
  EndingLimitThenWatchdog, WatchdogThenClose, CloseThenWatchdog, BothDueAtOneWake}` in `test_106_seams.cpp`, both roles,
  GREEN at H4; the R-2 stimulus is a frame at MsgSeqNum 1 (below NextNumIn). FR-017's "one close" is read as the
  transport's close (`closed()`), as the US3 cells read it: a give-up's cut and a later close of the same transport both
  count in `blocking_write_state::closes`, with the limit off too. **Stopped:** `OneEnding106.Watchdog_PreActive_NoEstablishmentRecord`
  needs an Engine acceptor's reply Logon write held, the T074 wall (ruling A dropped T074's twin); not written. Candidate
  for the orchestrator: an Engine initiator whose write is held inside `on_inbound_logon_sent_` (a 789 resend frame or a
  Logon-arm Reject), which is a different stimulus from the one research D9 names.
  **Disposition (orchestrator ruling 1 on the Phase 6 report):** `Watchdog_PreActive_NoEstablishmentRecord` is dropped, for
  T074's reason (no seam holds the Engine acceptor's own write); it is on T167's and research D9's no-cell list. The R-2
  pairs' labels follow spec.md R18.
- [X] T106 [US4] `tests/session/test_106_seams.cpp`: The connection-bound cells that need a write armed (research D9 row C-4; data-model E-3's joiner
  table), each [seam]: `ConnectionBound106.SpawnFails_AtWrite` (the write is not started and returns
  `dispatch_aborted`; claim, `Disconnected`, closed once, zero records); `ConnectionBound106.Teardown_Drain_Parked` (a
  direct initiator in LogonSent with `write_timeout_ms` set, its reconnected Logon written, the acknowledgement withheld,
  a second `drive_reconnect()` returns; the counter reads 0 after a bounded pump); `Teardown_Drain_Timing` (the armed
  write a Logon-arm Reject blocked in LogonSent, W = 300 ms, the teardown at S + 100 ms, the clock not advanced after;
  the drain parks at step 3, so only the entry wake ends the bound); `Teardown_ConnectionEnd_Timing` (armed at S, edge at
  S + 100 ms); `ConnectionBound106.WriteExpiryDuringDrain_NoRecord` (FIXT, inbound validation on, the S3 Reject's write
  blocked at S, `inbound_depth_` 1, the drain parked at step 3; `step_to(S + W)`: no write-timeout record, no
  `Disconnected` from the bound, the counter 0, the write ended by the drain's own wall-clock bound within 5 s + 2 s);
  `ConnectionBound106.DrainCancelled_RespawnsOnBlockedWrite` (the same rig, the second `drive_reconnect()` cancelled
  through its cancellation slot while parked, so it returns `dispatch_aborted`; then `step_to(S + W)`: one write-timeout
  record, `Disconnected`, closed once, `session_write_timeout` from the Reject's write);
  `ConnectionBound106.DrainCancelled_RespawnFails_ClosedAtOnce` (the same rig, `connection_bound_spawn_fails_` set
  after the bound's first spawn (the Reject's write armed) and before the cancellation, so the respawn is refused:
  at that return, with the clock not advanced, the claim taken, `Disconnected`, closed once, zero reason records, and
  the Reject's write returns `dispatch_aborted`, its epoch not cut; RED with the refused-respawn path deleted: open, no
  `Disconnected`). Every Drain cell sets `logout_disconnect_timeout_ms` 5000 ms. Mutants: T129.
  **Done** (`2100cc67`, `08431a85`, `1b03bc6d`): `WriteLimit106.ConnectionBound_SpawnFails_AtWrite` (HeartBtInt 0, the first
  limited write is the initiator's Logon or the acceptor's reply Logon), `WriteLimit106.ConnectionBound_Teardown_ConnectionEnd_Timing`
  (the edge with no write before it: the reply Logout's toAdmin throws), and `ConnectionBound106.{Teardown_Drain_Parked,
  Teardown_Drain_Timing, WriteExpiryDuringDrain_NoRecord, DrainCancelled_RespawnsOnBlockedWrite,
  DrainCancelled_RespawnFails_ClosedAtOnce}` on a direct initiator whose validate-first Reject (an acknowledgement without
  EncryptMethod(98)) is held in LogonSent. `Teardown_Drain_Parked` reads the old bound's count at the new connection's first
  spawn of the bound (the spawn seam as an observer): with the limit on, the reconnected Logon spawns the new connection's
  bound, so the counter itself does not read 0 after the second `drive_reconnect()` returns. GREEN at H4. That substitution
  is accepted (orchestrator ruling 5 on the Phase 6 report; research D9 row C-4).
- [X] T107 [US4] `tests/session/test_106_seams.cpp`: `ShippingClock106.DirectDriver_BoundBesideLiveness_TestRequestAtT` [seam: M8] (research D9 row R2(b);
  placed in plan P3, written here because its rig needs D5's write arming): `system_clock_source`, the coroutines on
  `session->executor()`, HeartBtInt 2 s (T = 2.4 s), W = 200 ms, B = 2000 ms; app sends every 300 ms, the peer silent;
  every send completes until the TestRequest is observed at the peer in [T, 2T); the next send blocks at
  S ≤ `tr_written_at` + 300 ms plus scheduling and is cut within W + 1 s of S (1.5 s < T, 0.9 s margin); one write-timeout
  record, no slot-74 record, `session_write_timeout` from the cut send. Mutant: T126.
  **Done** (`08431a85`, `1b03bc6d`): `WriteLimit106.ShippingClock106_DirectDriver_BoundBesideLiveness_TestRequestAtT` (both
  roles), every call after `open()` on `session->executor()`; GREEN at H4; M8 at T126.
- [X] T108 [US4] `tests/alloc_guard/test_106_connection_bound.cpp`: `alloc_guard_106_connection_bound_mallocnesia`
  (research D9 row R2(a); Release, label `106`): a real Engine session over `system_clock_source` under `B-544-1`'s two
  conditions. Send half: HeartBtInt 30 s; after warm-up 20 spaced sends 100 ms apart, W = 50 ms, limit off and on: zero
  global allocations and no write-timeout record. Emit half: allocations per Heartbeat cycle at the head minus the base's
  under identical traffic (research D9 row R2(a) defines it), must be 0, limit off and again limit on against the base's
  limit-off reading; the base half built on `$B` with `-DFIXPP_106_ALLOC_BASE=ON` (T004),
  the TU and `tests/alloc_guard/CMakeLists.txt` overlaid, the reconfigure rc 0. Known positives: M5 and M8 (T125, T126). The zero covers the connection bound only.
  **Done** (`99f9ed56`; readings: evidence file, "Phase 6 remainder", ruling 6): one source, two binaries (544's
  run-thread shape). `test_106_connection_bound` is the interceptor's (`alloc_guard_106_connection_bound_mallocnesia`, the
  send half, limit off and on); `test_106_connection_bound_count` counts glibc's allocation entry points in-process for both
  halves and is registered Release-only as `alloc_guard_106_connection_bound_count` (a proposed addition: the interceptor
  exits on the first allocation in a window, so it cannot read the emit half's per-cycle count). The emit half is the
  counter binary's, its base half that binary built on `$B` with `-DFIXPP_106_ALLOC_BASE=ON`. GREEN on `linux-clang-release`
  at the head; the differential 0 against the base, limit off and on. Known positives: M8 RED (T126); M5 GREEN, stopped
  (T125). "No write-timeout record" is asserted as its consequence (Active, every send delivered, the socket open): the rig
  attaches no logger.
- [X] T109 [US4] `.specify/decisions/106-logon-guards-write-bounds-evidence.md`: Commit the cells (`C5`); record that none builds on `$B` but the emit half's base build, and read
  each cell's RED under its mutant at `H4` (the mutant tasks below). Record the base emit-half count for T108.

  **Done:** C5 committed (`2100cc67`, `08431a85`, `1b03bc6d`); every C5 cell is a seam cell, read under its mutant at
  H4 (T118 to T130). The base emit-half count is recorded for T108 (evidence file, "Phase 6 remainder", ruling 6).
### Implementation for US4

- [X] T110 [US4] `include/fixpp/session/session.hpp`, `src/session/session.cpp`: The write class (data-model E-6): `enum class write_class : std::uint8_t { ordinary, logout };` a
  defaulted (`ordinary`) argument of `store_then_emit`, forwarded to `live_write_serialized_`; every row of the Logout-site
  table passes `logout`; a `logout` write never arms the write deadline.
  **Done** (`28cb7469`).
- [X] T111 [US4] `include/fixpp/session/session.hpp`, `src/session/session.cpp`: The write arming (research D5; data-model E-5): with `cfg_.write_timeout_ms != 0`,
  `live_write_serialized_` sets `write_started_at_` (one `steady_now()`), bumps `write_epoch_` and sets
  `write_in_flight_` just before `live->async_write(frame)`, after the `write_gate_` grant and ruling W's hold-back test;
  an RAII guard clears `write_in_flight_` on every exit, both `catch` returns included; `begin_connection_()` clears it
  and `write_cut_epoch_` with `ending_claim_`. It wakes the bound unless the bound is already armed on an earlier write's
  deadline. The first live write with the limit on spawns the bound when none runs (same executor rule); a refused spawn
  fails closed (the write not started, `dispatch_aborted`, claim, `Disconnected`, close). With the limit off: one test of
  the knob per write, nothing else.
  **Done** (`28cb7469`): the flag's clear is explicit at both returns and by the guard's destructor only while unwinding
  (`emit_in_flight_guard`'s shape), so a frame destroyed unresumed touches nothing; the clear does not wake the bound. The
  wake on a write's start compares the bound's armed deadline (`connection_bound_.armed_until`) with the write's own
  deadline. Spawned at most once per connection by the latch `connection_bound_.spawned`.
- [X] T112 [US4] The bound's write row and the expiry (data-model E-3 row 4, E-5 steps 1 to 5): at a wake or its
  deadline, the same epoch in flight and the clock at or past `write_started_at_` + limit: `claim_ending_(write_timeout)`
  (refused: nothing), the warn record through the session logger (format registered in `src/log/format_registry.cpp`,
  data-model E-8), `write_cut_epoch_ = write_epoch_`, `record_disconnected_unless_already_()`, and the one close
  (`spawn_connection_close_(live_transport_shared_())` then `connection_end_owed_.reset()` when
  `state_ == lifecycle::open`). No Logout.
  **Done** (`28cb7469`): `expire_write_()`; the record is registered in `src/log/format_registry.cpp`.
- [X] T113 [US4] `src/session/session.cpp`: The error through every seam it crosses (research D5, N-3): `live_write_serialized_` returns
  `session_write_timeout` for a write whose epoch is `write_cut_epoch_` once the close fails it (every other failure stays
  `dispatch_aborted`); `store_then_emit`'s live path passes it through unflattened; `send_body_`'s F9 block takes it as it
  takes `dispatch_aborted`, through `record_disconnected_unless_already_()`, per the `dispatch_aborted` ruling table. Every
  other `dispatch_aborted` line T014 found at HEAD is ruled in that table before this task closes.
  **Done** (`28cb7469`). T014's recipe at H4 finds the same one comparison (`send_body_`'s F9 block, now testing both values)
  and its comment line; the `dispatch_aborted` ruling table is unchanged. `replay_outbound_range_`'s `transmit_async` keeps its
  bool (its contract returns `dispatch_aborted` for a write error): the walk's cut frames are recorded by the expiry, not by
  their return value.
- [X] T114 [US4] `src/session/session.cpp`: The respawn after a failed drain (data-model E-3, N-6): at `drive_reconnect`'s failed-drain return,
  after clearing `reconnect_drain_in_flight_`, spawn the bound again when `conn_gen_` has not moved,
  `closing_or_stopped_()` does not hold, `write_in_flight_` or `emit_in_flight_` holds, and `connection_bound_.running` is
  false (that term defensive, no cell: data-model E-3); the new bound re-arms on the in-flight write's
  `write_started_at_`. A refused respawn fails closed in T111's live-write form without its first step, as data-model
  E-3's respawn paragraph words it: the in-flight write is already started, so the session takes the claim, records
  `Disconnected` and closes (research D3, "One close"), and the close fails that write. Cell:
  `ConnectionBound106.DrainCancelled_RespawnFails_ClosedAtOnce` (T106); mutant: T129 arm (e).
  **Done** (`28cb7469`): at `drive_reconnect`'s failed-drain return, with `fail_closed_without_bound_()` for a refused respawn.
- [X] T115 [US4] `src/session/session.cpp`: Deferred item ("The RAII clears"), for `write_in_flight_`: as T088, every exception and `catch`
  return of `live_write_serialized_` re-derived to reach the `Disconnected` edge, or given a cell with the clear's mutant
  RED. Record it.
  **Done** (walk in the evidence file): both returns of `live_write_serialized_` after the arming clear explicitly; an
  exception other than `asio::system_error` from `async_write` unwinds through the guard's destructor, which clears. The
  normal-path clear's mutant reads RED (arm T115, H4): `OneEnding106.{BothDueAtOneWake, GiveUpThenWatchdog,
  WatchdogThenGiveUp}` and the shipping-clock cell, both roles; the two cells first named for it (`QueuedWrite_*`,
  `ResendWalk_*`) stay GREEN, since no clock step passes a completed write's deadline there.
- [X] T116 [US4] `src/session/session.cpp`: Comments naming the decision at the write arming and the class argument: "106 D5 supersedes 105
  D-e's scope (only ending Logouts are limited): every non-Logout write is limited, opt-in; the clock starts at
  `async_write`, not at the gate (105's ending limit counts from the leaf's entry)".
  **Done** (`28cb7469`, `748db96f`): at the arming in `live_write_serialized_` and at `write_class` in `session.hpp`.
- [X] T117 [US4] At the head: `-L 106` (seam entry included), `-L 105`, the C-ABI error entries, `-L config` GREEN;
  the mallocnesia cell on a `linux-clang-release` tree on `/mnt/wsl/fixppbuild` GREEN with its base half read. Record
  counts. Commit `H4`; `codegraph sync`.

  **Done** (H4 = `748db96f`; readings in the evidence file): `-L 106`, `-L 105`, `-L 104`, `-L 099`, `-L 538`,
  `-L config`, `capi_pure_tests` (the C-ABI error entries) GREEN; `codegraph sync` run. The mallocnesia cell (`99f9ed56`)
  GREEN on `linux-clang-release` with its base half read (evidence file, "Phase 6 remainder", ruling 6).
### Mutant and spurious-hit arms for US4 (in `$M` at `H4`)

- [X] T118 [US4] In `$M`'s `src/session/session.cpp`: M3: `write_started_at_` taken before the `write_gate_` acquire; RED:
  `WriteTimeout106.QueuedWrite_CountsFromItsOwnStart`, `WriteTimeout106.ResendWalk_ProgressNotCut`.
  **Stopped (a cell expected RED read GREEN, to the orchestrator):** under M3 `QueuedWrite_CountsFromItsOwnStart` reads RED
  (both roles); `ResendWalk_ProgressNotCut` reads GREEN (both roles): the walk's frames take the write gate uncontended, so
  a start read before the gate's acquire equals the start read at `async_write`; that cell cannot see M3, and it stays GREEN
  under T115's arm too. What it keeps is the walk's progress under the limit (spec US4 scenario 2).
  **Done (orchestrator ruling 2 on the Phase 6 report):** `ResendWalk_ProgressNotCut` is reclassified as a guard; M3 is
  equivalent there by construction. Proof (symbols): `replay_outbound_range_` constructs its
  `detail::replay_gate::exclusive_guard` before its first frame and waits while `replay_gate_.shared` is non-zero; every
  emitter that writes outside the walk (`send_body_`, the liveness loop's emits, `store_then_emit`'s numbered path) waits
  while `replay_gate_.exclusive` holds and takes a shared hold before its write; and the walk runs inside the inbound arm
  that drew it, so no inbound reply is written beside it. So no other write holds or requests `write_gate_` during the
  walk, each frame's `write_gate_.async_lock()` is granted without suspending, and the start M3 reads before the acquire
  is the start the head reads after the grant. Re-derive with `git grep -n 'replay_gate_' -- src/session/session.cpp`.
  `QueuedWrite_CountsFromItsOwnStart` stays M3's counted cell (RED, both roles).
- [X] T119 [US4] In `$M`'s `src/session/session.cpp`: M3′: the bound's write arming deleted; RED: every `WriteTimeout106.<Class>` cell.
  **Done**: M3′ (the write's flag not set) RED on every `WriteTimeout106.CutAtTheLimit` cell, both roles (evidence file).
- [X] T120 [US4] In `$M`'s `src/session/session.cpp`: M3″: the expiry does not set `write_cut_epoch_`; RED: every `WriteTimeout106.<Class>` cell (the cut call
  returns `dispatch_aborted`).
  **Stopped (cells expected RED read GREEN, to the orchestrator):** under M3″ the classes whose cut call is observed
  read RED (`Send`, `InitiatorLogon`, `Reject`, `ResendRequest`, `HeartbeatReply`, `BusinessReject`, `ReplyLogon`, both roles
  where they run); `Heartbeat`, `TestRequest`, `ResendFrame`, `SequenceReset` and `Honor789Frame` read GREEN, since no
  caller observes their cut write's error (T100's note). T120's "every `WriteTimeout106.<Class>` cell" holds only for the
  first group.
  **Done (orchestrator ruling 3 on the Phase 6 report):** the five classes' cells already assert the ending (the one
  `Disconnected`, the write-timeout record, the close, no Logout, FR-017); M3″ re-read at `42fb40e0` reads them GREEN, so
  M3″ is recorded as EQUIVALENT on those classes, with the proof in the evidence file ("Phase 6 remainder", ruling 3): no
  reader of `session_write_timeout` sits between their cut write and the ending (`git grep -n 'session_write_timeout' --
  src`). The other seven classes stay RED under M3″.
- [X] T121 [US4] In `$M`'s `src/session/session.cpp`: M3‴: `store_then_emit`'s pass-through deleted (`session_write_timeout` flattened to `dispatch_aborted`);
  RED: `WriteTimeout106.Send` (its cut `send` returns through `send_impl`'s `store_then_emit`).
  **Done**: M3‴ RED on `WriteTimeout106.CutAtTheLimit/Send_*`.
- [X] T122 [US4] In `$M`'s `src/session/session.cpp`: M4 at the R-1 leaf (`inbound_logout_leaf_`, kind `r1`, passes `write_class::ordinary`) with M9 and M9′
  (alone it is masked by the claim); RED: `WriteTimeout106.EndingLogout_Keeps105Limit` (closed at S + W).
  **Done**: RED on `EndingLogout_Keeps105Limit` (both roles); M4 alone there GREEN (masked by the claim), as stated.
- [X] T123 [US4] In `$M`'s `src/session/session.cpp`: M4 at `run_logout_phase1` with M9 and E-3's closing term deleted from the exit row (alone it is
  masked by `closing`); RED: `WriteTimeout106.CloseLogout_KeepsGrace` (a write-timeout record at S + W).
  **Done**: RED on `CloseLogout_KeepsGrace` (both roles).
- [X] T124 [US4] In `$M`'s `src/session/session.cpp`: M4 at each other Logout site of T104, with M9 and M9′, one arm per site; RED: its
  `WriteTimeout106.EndingLogout_<Site>` cell. A site ruled unheld in T104 has no arm.
  **Done**: one arm per site, each RED on its `WriteLimitLogoutSite106.EndingLogout_<Site>` cell.
- [ ] T125 [US4] In `$M`'s `src/session/session.cpp`: M5: a coroutine spawned per write and per emit; RED: `alloc_guard_106_connection_bound_mallocnesia` (the
  send half non-zero, the emit half's delta positive).
  **Stopped (a cell expected RED read GREEN, to the orchestrator):** M5 (a coroutine with a timer, spawned per limited
  write, so per emit too with the limit on) reads GREEN on the send half (interceptor and counter both 0) and 39 against
  40 on the emit half, below the limit-off count rather than above it. Under `B-544-1`'s two conditions asio's per-thread
  recycler serves the spawned coroutine's frames, so a spawn per write allocates nothing in steady state. Readings:
  evidence file, "Phase 6 remainder", ruling 6.
- [X] T126 [US4] In `$M`'s `src/session/session.cpp`: M8: `connection_bound_wait_`'s shipping branch replaced by `await_deadline(*effective_clock_, …)`; RED:
  `ShippingClock106.DirectDriver_BoundBesideLiveness_TestRequestAtT` (no TestRequest by 2T) and the mallocnesia cell.
  **Partly done:** M8 (the shipping branch's wait replaced by the mock branch's `timer || await_deadline(*effective_clock_, …)`,
  so the bound's wait takes the clock's per-session slot) RED on the shipping-clock cell, both roles, by "no TestRequest by
  2T", no abort. The literal form (`await_deadline` alone, no timer) also reads "no TestRequest by 2T" but then aborts at
  `~Session`'s connection-bound assertion (the bound can no longer be woken), so it is not counted. **Done:** the mallocnesia
  half, the same counted arm at `99f9ed56` on `linux-clang-release`, RED: the interceptor fails the limit-on send half
  (every interception through `malloc`), the counter reads it non-zero and the emit half's delta positive (evidence file).
- [X] T127 [US4] In `$M`'s `src/session/session.cpp`: M9 with M9′ over the T105 pairs whose second decider reaches `claim_ending_` or the bound's
  taken-claim exit; `CloseThenWatchdog` under M9 with the closing term deleted from E-3's exit row; RED: two reasons or
  two entries. The guards (`EndingLimitThenWatchdog`, `WatchdogThenGiveUp`, `WatchdogThenClose`, `GiveUpThenClose`; spec.md R18) are
  not counted; a pair GREEN under every arm is reported as vacuous.
  **Stopped (the bundle's guard labels contradict the code, to the orchestrator):** under M9 with M9′,
  `GiveUpThenWatchdog` and `WatchdogThenEndingLimit` read RED and `EndingLimitThenWatchdog` GREEN (both roles): T080 has the
  R-2 leaf claim at its entry (D), so in both orders the R-2 Logout holds the claim first; with W = 300 ms the bound's instant
  S + W precedes D + B and meets the taken claim (RED once the test and the exit term are deleted), and with W = 1 s the cut at
  D + B ends the connection first, so the bound leaves on `Disconnected`, a term M9′ keeps (a guard). The bundle labels the
  first a guard and counts the second. `CloseThenWatchdog` under M9 with the closing term deleted: RED. `BothDueAtOneWake` is
  GREEN under M9 with M9′ (the bound leaves after the give-up row's decision, so the write row is never evaluated at that
  re-test); the arm that reaches its property, the two rows' order swapped (arm `RowSwap`), reads RED, both roles. The guards
  `WatchdogThenGiveUp`, `WatchdogThenClose` and `GiveUpThenClose` read GREEN, as stated.
  **Done (orchestrator ruling 4 on the Phase 6 report; spec.md R18):** the code is followed. The bundle's labels are
  swapped to match the observed winners: `WatchdogThenEndingLimit` is counted (RED under M9 with M9′, both roles) and
  `EndingLimitThenWatchdog` is a guard (GREEN, as a guard reads); `BothDueAtOneWake`'s arm is RowSwap (RED, both roles) in
  place of M9 with M9′. `CloseThenWatchdog`: RED under M9 with the closing term. Research D9 row FR-017, data-model E-3,
  quickstart section 4 and the cell's comment read so.
- [X] T128 [US4] In `$M`'s `src/session/engine.cpp`: M10, alone (without M9): the pump races `on_inbound_frame` against the establishment deadline; RED:
  `OneEnding106.Watchdog_PreActive_NoEstablishmentRecord` (an establishment record and no write-timeout record).
  **Stopped:** its cell (`OneEnding106.Watchdog_PreActive_NoEstablishmentRecord`, T105) is not written.
  **Disposition (orchestrator ruling 1 on the Phase 6 report):** M10 has no cell by construction: its only cell needs the
  Engine acceptor's reply Logon write held, and no seam holds the Engine acceptor's own write (T074's reason). Recorded so
  in the evidence file's mutant table and in research D9's carrier table.
- [X] T129 [US4] In `$M`'s `src/session/session.cpp`: M12, one arm per joiner of data-model E-3's table: (a) the drain's entry wake and its
  `reconnect_drain_in_flight_` term deleted — RED `Teardown_Drain_Parked` and `Teardown_Drain_Timing` by the counter (never
  a hang); (b) the `Disconnected` edge's wake deleted — RED `Teardown_ConnectionEnd_Timing`; (c) the drain term and
  `claim_ending_`'s drain refusal deleted — RED `WriteExpiryDuringDrain_NoRecord` (a write-timeout record); (d)
  `drive_reconnect`'s respawn after a failed drain deleted — RED `DrainCancelled_RespawnsOnBlockedWrite` (no write-timeout
  record at S + W); (e) the respawn's refused-spawn path deleted (the refusal returns without the claim, the record or
  the close) — RED `DrainCancelled_RespawnFails_ClosedAtOnce` (open, no `Disconnected`). Plus the write-spawn seam's
  failure path deleted — RED `ConnectionBound106.SpawnFails_AtWrite`.
  **Done**: arms (a) to (e) and the write-spawn seam's failure path each RED on their named cells by assertion (no abort;
  arm (d) read after its first build failed on an unused variable, the stale binary's reading discarded). The drain rig's
  teardown closes the session before the rig's unbounded drain (`1b03bc6d`), after arm (a)'s first run kept a drain's join
  spinning past the cell.
- [X] T130 [US4] In `$M`'s `src/session/session.cpp`: M13 per reachable site a `WriteTimeout106.<Class>` cut returns through (T015's table), one arm
  each, the site's raw `record_state_transition_(fsm_state::Disconnected)` restored; RED: that class's cell (two
  `Disconnected` entries): `send_body_`'s F9 block by `WriteTimeout106.Send`, `emit_initiator_logon_`'s tail by
  `WriteTimeout106.InitiatorLogon`, the two-argument `emit_session_reject_`'s by `WriteTimeout106.Reject`,
  `inbound_resend_request_leaf_`'s by `WriteTimeout106.ResendRequest`, the loop's TestRequest and Heartbeat tails by
  `WriteTimeout106.TestRequest` and `WriteTimeout106.Heartbeat`. Every reachable row of the table now has an
  arm here or in T098; a row with none is named unreachable in the table with its reason.

  **Done**: one arm per site, each RED (two `Disconnected` entries) on its class's cell, both roles: the F9 block on `Send`,
  `emit_initiator_logon_`'s tail on `InitiatorLogon`, the two-argument `emit_session_reject_` on `Reject`,
  `inbound_resend_request_leaf_` on `ResendRequest`, the loop's tails on `TestRequest` and `Heartbeat`, and (ruling B) the
  Heartbeat reply's and the BusinessMessageReject's tails on `HeartbeatReply` and `BusinessReject`. Every reachable row of
  T015's table now has a RED arm here.
**Checkpoint**: US4 GREEN, its mutants RED; the write limit and the one-ending invariant hold together.

---

## Phase 7: User Story 5 — the TLS handshake bounds are proven and configurable (Priority: P2) (#717; research D6; contract C-5)

**Goal**: `[session.transport].tls_handshake_timeout_ms` reaches the initiator factory's `Transport::Config`, and
`EngineConfig::acceptor_tls_handshake_timeout` (TOML root `acceptor_tls_handshake_timeout_ms`) replaces the listener's
literal; a never-handshaking peer ends each attempt at the bound (FR-013, FR-013a, FR-014, SC-005).

**Independent test**: the config cells, `HandshakeBound106.*` and `EngineHandshake106.*` by label `106`, plus the
transport binary's `AsioTlsTransportErrorPaths.*` and `-L 105`'s `TlsClose105.*`.

### RED cells for US5 (write first)

- [X] T131 [US5] `tests/session/test_106_handshake.cpp` or the config-suite file T003 names (`tests/config/`): In T003's file for the minting cells: `TomlHandshake106.SessionKey` (key 300 ms; the bundle's
  factory mints a transport whose `async_handshake` against the silent listener returns `transport_handshake_timeout`
  within key + 2 s), `TomlHandshake106.SecondSessionDiverges` (two TLS initiator sessions, the second's key differs: it
  gets its own factory and its own bound), `TomlHandshake106.DefaultTransport_MixedRoles_Accepted` (the key in
  `[default.transport]`, one TLS initiator and one acceptor session: the bundle loads, and the initiator's minted
  transport times out at the key; `L-106-d`). Spurious arm (key absent): still pending at 2.3 s, which precedes the 30 s
  default by 27.7 s. RED on the base: the base silently ignores the key (`recognize_keys` checks no key inside
  `[session.transport]` or `[default.transport]`, and `build_tls_factory` always uses a default `Transport::Config
  t_cfg{}`), so each minted transport keeps the 30 s default and is still pending at key + 2 s. Mutants: T140, T144.
  **Done** (C6 = `efb24877`): the four cells in `tests/session/test_106_handshake.cpp` (`session_106_tests` now links
  `fixpp::config_toml` and defines `FIXPP_CONFIG_FIXTURE_DIR`), the spurious arm as its own cell
  `TomlHandshake106.SessionKeyAbsent_Pending` *(proposed)*. Each asserts `transport_handshake_timeout`, returned no earlier
  than the key and within key + 2 s; `SecondSessionDiverges` also asserts session[1]'s own factory. RED on `$B` by
  assertion (T134), GREEN at H5.
- [X] T132 [US5] `tests/session/test_106_seams.cpp`: `EngineHandshake106.Acceptor_ClosesAtTheKey` [seam: M7‴] (the
  `EngineConfig` built from the loaded bundle, `EngineEstablishment::acceptor_tls_handshake_timeout` copied as `clock` is;
  key 300 ms, `logon_timeout_ms` 10 s; a TCP client that never handshakes sees EOF in [300 ms, 1.2 s); spurious arm (key
  60 s): still connected at 2.3 s) and its C++ field twin `EngineHandshake106.Acceptor_CxxField_ClosesAtTheField`
  *(proposed)* [seam: M7′] (`EngineConfig::acceptor_tls_handshake_timeout` set directly, same windows). Inequalities as
  research D9 row FR-013a (the 1500 ms default lands 300 ms after the window; 2.3 s precedes `logon_timeout_ms` by 7.7 s).
  **Done** (`efb24877`): the two cells and the spurious arm `EngineHandshake106.Acceptor_LongKey_StillConnected`
  *(proposed)* in `test_106_seams.cpp`, on `EngineLoopbackHarness`'s TLS acceptor with `t106::SilentClient`; the TOML helper
  gains root keys and a loader bound to the cell's io_context (the bundle's clock is the Engine's). GREEN at H5; RED under
  M7′ and M7‴ (T141, T142).
- [X] T133 [P] [US5] `tests/session/test_106_handshake.cpp`: `HandshakeBound106.DriveReconnect_OneAttempt` (research D9
  row FR-014; spec US5 scenario 3): the real TLS factory, `max_attempts` 1, bound 300 ms through
  `Transport::Config::tls_handshake_timeout`, the silent listener: `drive_reconnect()` returns
  `transport_reconnect_limit_exceeded` within 300 ms + 2 s (the FSM's final error unchanged; R4). [guard: spurious arm,
  bound 60 s, not returned at 2.3 s; M7″ (T143)].
  **Done** (`efb24877`): `HandshakeBound106.DriveReconnect_OneAttempt` and its spurious arm
  `HandshakeBound106.DriveReconnect_LongBound_NotReturned` *(proposed)* on `resend_gap::Scene`'s direct initiator; the
  verdict is read before the listener's close ends a pending attempt. GREEN on `$B` and at H5 (a guard); RED under M7″.
- [X] T134 [US5] `.specify/decisions/106-logon-guards-write-bounds-evidence.md`: Commit the cells (`C6`); read T131 and T133 on `$B`; record per cell its RED reason or
  guard status, and the skip count (0).
  **Done**: C6 = `efb24877`; readings in the evidence file ("Phase 7").

### Implementation for US5

- [X] T135 [US5] `src/config/selector_resolver.cpp` (research D6 "Initiator"): `build_tls_factory` gains a
  `Transport::Config` parameter; `resolve_transport` reads `tls_handshake_timeout_ms` (T030's checks) into
  `Transport::Config::tls_handshake_timeout`; the minting loop gains the third divergence term (the value differing from
  session[0]'s), so a later session's value is never dropped; on a plaintext or acceptor session the key loads and has no
  effect (`L-106-d`). No `SessionConfig` field; `TransportFactory` untouched (`[const §XIV.2]`).
  **Done** (H5 = `b7da8891`): `transport_config_of` reads the merged transport table's key (an in-range integer; any
  other value is `check_transport_handshake_timeouts`' diagnostic); `build_tls_factory` takes the `Transport::Config`; the
  minting loop's third term compares the bound with session[0]'s.
- [X] T136 [US5] `src/session/engine.cpp` (research D6 "Acceptor"): the listener's
  `accepted_transport_config` reads `EngineConfig::acceptor_tls_handshake_timeout` in place of the 1500 ms literal, with
  the comment "106 D6 supersedes 015 FR-014's hard-coded 1500 ms: `EngineConfig::acceptor_tls_handshake_timeout` (default
  1500 ms)". The comment in `src/transport/asio_tls_transport.cpp` beside the handshake timer that states the accepted
  transport's 1500 ms literal now names the field and its default as a pointer (106 D6), not the value.
  **Done** (`b7da8891`): the listener reads `engine_cfg.acceptor_tls_handshake_timeout`; both comments name the field and
  106 D6.
- [X] T137 [US5] Every bundle-to-`EngineConfig` copy site T012 found (`tests/config/test_098_dialect_overlay.cpp`,
  `tests/config/test_099_toml_live.cpp`, and any other) copies `acceptor_tls_handshake_timeout` beside `clock` (contract
  C-5.2: the host copies it).
  **Done** (`b7da8891`): T012's two sites copy the field beside `clock` (re-derive: `git grep -nE
  '= *[a-z_]+(\.|->)engine\.clock' -- tests examples bindings bench`; the 106 cells copy it as well).
- [X] T138 [US5] `.specify/decisions/106-logon-guards-write-bounds-verify.md`: Deferred item (plan "Deferred to tasks.md", "The exact 30 s default of `tls_handshake_timeout_ms` when
  the key is absent"): no cell, by named ruling: pinning it through the resolver needs the factory accessor research D6
  rejects; the spurious arm of T131 catches a resolver that writes 0 or a short sentinel, and
  `Transport::Config`'s default is a default 106 does not edit (Gate A loop 2 round 2, C-3). Check whether an existing cell
  pins `Transport::Config{}.tls_handshake_timeout` (`git grep -n 'tls_handshake_timeout' -- tests`) and cite it in the
  verify record; record the ruling there.
  **Done:** ruled in `.specify/decisions/106-logon-guards-write-bounds-verify.md` (T138 section): no cell; no existing cell
  pins `Transport::Config{}.tls_handshake_timeout`; `TomlHandshake106.SessionKeyAbsent_Pending` is the spurious arm.
- [X] T139 [US5] `.specify/decisions/106-logon-guards-write-bounds-evidence.md`: At the head: the US5 cells GREEN; guards unfiltered:
  `AsioTlsTransportErrorPaths.HandshakeTimeoutMapsToTransportHandshakeTimeout` (the attempt's
  `transport_handshake_timeout`), `TlsClose105.*`, `EngineFirstFramePhaseATls.Q17_*` (builds `EngineConfig` directly; a close
  in its window); `-L config`. Record counts and skip counts. Commit `H5`; `codegraph sync`.
  **Done** (`b7da8891`; readings in the evidence file, "Phase 7"): the US5 cells GREEN; the guards GREEN; `-L 106`,
  `-L 105`, `-L 104`, `-L 099`, `-L config` GREEN; 0 skipped; `codegraph sync` run.

### Mutant arms for US5 (in `$M` at `H5`)

- [X] T140 [US5] In `$M`'s `src/config/selector_resolver.cpp`: M7: `tls_handshake_timeout_ms` read and dropped (the base's default `Transport::Config`); RED:
  `TomlHandshake106.SessionKey`, `TomlHandshake106.SecondSessionDiverges`, `TomlHandshake106.DefaultTransport_MixedRoles_Accepted`.
  **Done**: RED, the three cells (evidence file).
- [X] T141 [US5] In `$M`'s `src/session/engine.cpp`: M7′: `EngineConfig::acceptor_tls_handshake_timeout` ignored (the literal back); RED: the C++ twin
  `EngineHandshake106.Acceptor_CxxField_ClosesAtTheField` and `EngineHandshake106.Acceptor_ClosesAtTheKey`.
  **Done**: RED, both cells (closed at the 1500 ms literal).
- [X] T142 [US5] In `$M`'s `tests/session/test_106_seams.cpp` (the cell's host copy): M7‴: the host's copy of `EngineEstablishment::acceptor_tls_handshake_timeout` deleted (in the cell's
  copy, as the hosts copy); RED: `EngineHandshake106.Acceptor_ClosesAtTheKey`.
  **Done**: RED (closed at the 1500 ms default).
- [X] T143 [US5] In `$M`'s `src/transport/asio_tls_transport.cpp`: M7″: the factory's bound replaced by 60 s; RED: `HandshakeBound106.DriveReconnect_OneAttempt`.
  **Done**: RED (not returned at 300 ms + 2 s).
- [X] T144 [US5] In `$M`'s `src/config/selector_resolver.cpp`: The key refused on an acceptor or plaintext session; RED:
  `TomlHandshake106.DefaultTransport_MixedRoles_Accepted`.
  **Done**: RED (the mixed-role bundle does not load).

**Checkpoint**: US5 GREEN; both handshake bounds configurable and proven.

---

## Phase 8: User Story 6 — the Windows asan leg finishes inside its cap (Priority: P1, CI) (B129, #724; research D10)

**Goal**: FR-001, FR-002, SC-006. Done on this branch before the plan; this PR's own Tier 2 runs are its acceptance.

- [X] T145 [US6] `.github/workflows/tier2.yml`: the `windows-msvc-asan` leg's own `timeout-minutes`, raised above the
  debug and release legs, which keep theirs (FR-001). Done: `febad771`.
- [X] T146 [US6] `.github/workflows/tier2.yml`'s comment: the whole-job re-derive recipe (every step of a cold-cache
  push:main asan run, the restores, Configure, Build, the cache Save and Test, plus a margin, below the hosted runner's
  per-job limit) with no measured value, and the owner's permanence ruling covering both leg values (FR-002). Done:
  `febad771` and `aea0d5a5`.
- [ ] T147 [US6] **(orchestrator)** Record this PR's `windows-msvc-asan` leg outcome (completed, green or red on its own
  steps, not on the cap; SC-006) in the verify record and the Gate B record, from
  `gh pr view <N> --json statusCheckRollup`, and read that leg's `sccache statistics` step from its log
  (`gh run view --job <id> --log`): a warm run is recorded as warm and does not discharge the cold case, which then
  rests on the workflow comment's push:main recipe (SC-006, FR-001). B&L: `B&L delta: none — a CI job cap is not shipped behaviour` (research D8),
  recorded for `check_bl_delta.py`.

---

## Phase 9: Surfaces and records (plan phase 6; research D7, D8; contract C-6, C-8)

- [ ] T148 [P] `include/fix/c_api/version.h`: `FIXPP_C_ABI_VERSION_MINOR` 19 and a 1.19 history entry marked
  **BREAKING** (`[const §X.7]`), behaviour only, no new symbol: an unauthorised identity's Logon draws nothing and changes
  nothing (C-1; C++ and TOML behaviour with no reach from C, contract C-6's no-reach clause); a refused Logon or acknowledgement keeps NextNumIn (C-2); the give-up fires with a write blocked, its
  Logout limited as 105's (C-3); the C++/TOML-only write limit and handshake keys, unreachable from C (C-4.1, C-5.4).
- [ ] T149 [P] `include/fix/c_api/session.h` (comments only; never clang-formatted): `fixpp_session_close` gains a
  `BREAKING (C-ABI 1.19, [const §X.7]; 106)` block, and "amended in C-ABI 1.19, 106" clauses on
  `fixpp_session_is_established` and `fixpp_session_send`, each clause's text as contract C-6 words it:
  - the close block: a refused Logon or acknowledgement keeps NextNumIn; and C-6's no-reach clause, stated in the
    form of the STRICT clause's "no reach as shipped": an unauthorised identity's Logon draws nothing and changes
    nothing in C++ and TOML; no C session reaches an identity gate that can refuse a Logon the base let establish
    (no C setter installs a logon validator or a CompID binding, and `FIXPP_SECURITY_TLS`'s `mtls_ca` policy is
    empty, so default-deny on the base too); so close's return code is the base's on every C session; that holds while
    C-6's `git grep` over `src/capi` and `include/fix/c_api` finds nothing;
  - `fixpp_session_is_established`: the give-up with a write blocked. It reads false from the `onLogout` that the
    `Disconnected` record fires, because the C slot's `established` flag follows the trampoline's `onLogon` and
    `onLogout`. That record follows 105's cut at G + B at the latest, where on the base the flag stayed true while
    the write hung (C-3.3's store-phase exception aside);
  - `fixpp_session_send`: a send blocked at the give-up's close returns `FIXPP_ERR_CANCELLED`. The value is
    `B-105-8`'s, unchanged. The clause is BREAKING because on the base that send hung: a call that "could already" be
    made now returns "a result other than the one documented" (`[const §X.7]`).
  `fixpp_session_open` and the two callback registrations are unchanged (D7's census, T018).
- [ ] T150 Move every "pins the minor" row of T018 to 1.19 (the capi version tests, the Python binding's version
  pins, the CI and tool scripts; the Python binding gains nothing else, D7); re-record `tools/capi_freeze.sha256` through
  its generator if it hashes the touched headers (105 re-recorded it at verify), and run the freeze check.
- [ ] T151 `.specify/decisions/106-logon-guards-write-bounds-evidence.md`: Article X §7 predicate: `gh release list --exclude-drafts` on the fixpp repo (with a known positive, another
  repo's listing, so an empty answer is believed); empty keeps the MINOR bump; a non-empty listing stops and goes to the
  orchestrator. Record the result; re-run at the PR head by T176.
- [ ] T152 `spec/behaviors-and-limitations.md` (the LIVE file): move `L-104-3` (closed by D1) and `L-104-8` (closed by
  D2) to `spec/behaviors-and-limitations-closed.md` under its cut rule, each marked "closed by 106".
- [ ] T153 `spec/behaviors-and-limitations.md`: Same live file, amended in place, "(amended <the edit's date>, 106)", each changed clause named (research D8):
  `B-099-2` (title: the identity gates run first in both roles; its "Consume limit" clause: the gate Reject and the block
  both keep NextNumIn on every role; `L-104-8` cited as closed); `B-105-3` (a written TestRequest keeps the count from its
  write's return; a TestRequest or Heartbeat whose write has not returned is given up at L + 3T + 1 ns; the held case
  capped at L + 3T + 1 ns with one pending or none); `L-093-12` (the liveness loop's TestRequest or Heartbeat store call among those that hold
  `close()`'s and `stop()`'s joins; the give-up at G and the close at G + B still happen); `B-544-1`'s title and body and
  `L-497-1`'s title and condition (6) *Clock*, with D8's clause-by-clause text copied verbatim and ruling R14 flagged for
  the owner's review at merge; `L-105-f` narrowed (non-ending writes bounded when `write_timeout_ms` is set; the give-up
  no longer waits behind a blocked write; the held no-pending case capped; what stays open: the limit off with a peer
  that keeps sending and never reads, and B = 0 or no clock).
- [ ] T154 `spec/behaviors-and-limitations.md`: Same live file, a section `## 106`: `B-106-1` (the identity gates first, silent, no state change, with the
  credentials rule's width, N-11; as research D8 words it: the observables of each refusal class, contract C-1.3, and that a
  direct mTLS initiator started by `open()` holds no peer identity and never reaches Active, contract C-1.7), `B-106-2` (NextNumIn kept on every Logon refusal, with D2's table), `B-106-3` (the
  give-up with a write blocked, D4's instant, the one ending), `B-106-4` (`write_timeout_ms`), `B-106-5` (the two
  handshake keys), `B-106-6` (the surfaces: C-ABI 1.19 BREAKING behaviour only; the C++ layout of `Session`,
  `SessionConfig`, `EngineConfig`, `EngineEstablishment`; the additions `SeqnumManager::classify_inbound` and
  `CompIdAuthorizationPolicy::has_logon_validator`; the error enumerator `session_write_timeout` (136) and its C
  mapping); `L-106-a` to `L-106-f` as research D8 and
  contract C-8 word them (`L-106-f` pinned by `GiveUp106.PreActive_LimitOff_NotCutByEstablishment`); the C-7 departures
  table copied, not re-derived. Each row cites its cells by name.
- [ ] T155 `spec/behaviors-and-limitations.md`: Close T020: re-run its grep on the edited live file; every hit is now amended, retired or ruled
  unaffected, and the evidence table is complete.
- [ ] T156 [P] Brain pages, PRE-PUSH in the PR before CI (pipeline step 15a; close-out row 3; the late-docs rule):
  `brain/components/session.md` gains a 106 section (why the gates run first, the tentative advance, the connection bound
  and the one claim, and what was rejected: each "Alternatives rejected" of research D1 to D6 and D11), and its list of
  documents flags the superseded 103, 104 and 105 decisions of research's Supersession table by name;
  `brain/components/c-api.md` records C-ABI 1.19 (behaviour only, `session_write_timeout` → `FIXPP_ERR_TRANSPORT_IO`). Read
  `brain/components/config.md`, `transport.md` and `session-liveness-and-reconnect.md` for a statement 106 falsifies
  (the 1500 ms literal, the 3T cap's pending term, the loop alone deciding the give-up) and flag or amend each hit,
  citing source, not the page.
- [ ] T157 Brain checks: `python3 tools/check_brain.py --self-test`, `gate` and `census`, the steps of
  `.github/workflows/brain-freshness.yml`, then the wrapper PARENT
  `research/G19-fix-fpml-iso20022/tools/brain-external-sweep.sh` (never a bare `check_brain.py sweep`); record its
  non-zero execution count.
- [ ] T158 `src/` and `include/` (read), `.specify/decisions/106-logon-guards-write-bounds-evidence.md`: Supersession close (research "Supersession"; T022): at the head, both quickstart section 1's recipe
  `git grep -n '106 (' -- src include` and `git grep -n '106 D' -- src include` (the "106 D<n> supersedes …" form the
  Execution rules prescribe, after the brief's example; the quickstart recipe alone would miss it, and the evidence file
  records why both run), each hit read against the table; every row of the table is named by a comment at the code it changes, and no comment
  carries a result (T173).
- [ ] T159 Article XIX §5: re-derive `git grep -n logout_disconnect_timeout_ms docs examples` (no page names it, so no
  page is owed for the new keys) and that the Doxygen of `session.h`, `session_config.hpp` and `engine_config.hpp`
  regenerates without warning.
- [ ] T160 No C setter and no C TOML entry (FR-012, FR-013a; contract C-4.1, C-5.4; research F-7), a statement
  checked by grep with a seeded positive first (a scratch line naming `write_timeout` in `$M`'s `include/fix/c_api/session.h`
  must be reported): `git grep -n toml include/fix/c_api` is empty, and
  `git grep -nE 'write_timeout|handshake_timeout' -- include/fix/c_api src/capi bindings` reports nothing that sets either
  value: every hit is read, and a hit that maps or names an error (such as the `session_write_timeout` case in
  `translate`) is not a setter. Record the output.

---

## Phase 10: Polish, verification and mandatory close-out (plan phase 7; quickstart sections 5 to 7)

**Order**: /simplify → benches → targeted sanitizer and release lanes → coverage → MSVC → the ONE full ctest →
gcc-release, the whole pytest suite and every buildless Tier 1 step, re-run last → `/speckit-verify` → Gate B → the
labelled push in the final sign-off order. One build or ctest at a time throughout.

- [ ] T161 **(orchestrator)** `/simplify` over the implementation diff (`[const §XVI.7]`): the orchestrator triages
  the reviewers' findings; the implementer applies each accepted one, behaviour-neutral, one commit per item or group;
  after any change re-run `-L 106` and the touched labels.
- [ ] T162 clang-format on touched files only (never `specs/`, never `include/fix/c_api/*.h`):
  `git diff --name-only --diff-filter=d origin/main...HEAD -- src include tests bench | grep -E '\.(cpp|hpp|h)$' | grep -vE '^include/fix/c_api/' | xargs clang-format --dry-run --Werror`;
  a file whose `origin/main` copy fails with the same count is checked with `--lines` over this branch's hunks.
- [ ] T163 clang-tidy, cppcheck (the local 2.22 first on PATH) and IWYU on the touched `src/` and `include/` files
  (Article IX §4), new findings against `$B` fixed or dispositioned; a seeded control reported in every TU; a TU cppcheck
  abandons is not clean.
- [ ] T164 Paired benchmarks (Article VIII §2; contract C-9; quickstart section 6; T023): base-vs-candidate A-B-A-B
  runs of `engine_send_bench` (also CI-paired), `session_send_bench` and `on_inbound_frame_bench`, 105's T138 method: a
  release tree of `$B` on `/mnt/wsl/fixppbuild` whose path is padded to the candidate tree's length (`cmp` the `.text` of
  a same-source build to prove the layout matches), min per tree, +5 % one-sided. Results in the evidence file; a breach
  needs the measurement, a rationale and an approval by someone other than the author, or, where no independent approver
  exists, explicit user ratification recorded in the PR thread (`[const §VIII.2]`).
- [ ] T165 `.specify/decisions/106-logon-guards-write-bounds-evidence.md`: Targeted sanitizer lanes `linux-clang-asan`, `linux-clang-ubsan` and `linux-clang-tsan` (owner rule): only
  `-L 106`, `-L 105`, `-L 104`, `-L 099` and the binaries whose test sources the branch touches (config, transport,
  C-ABI, 538); on TSan the `GiveUp106`, `WriteTimeout106`, `OneEnding106` and `ConnectionBound106` cells also with
  `--repeat until-fail:50` (D3 and D5 add a strand-confined coroutine per connection). A report is a defect until
  disproven by a reproduction or a client-path analysis. Record each lane's entries, test count and skip count (0).
- [ ] T166 `linux-clang-release` (on `/mnt/wsl/fixppbuild`): `alloc_guard_106_connection_bound_mallocnesia` and the `-L 544`
  registrations, plus every `-L alloc_guard` entry by name (`ctest -N -L alloc_guard`); record each result.
- [ ] T167 Coverage of added lines (Article IX §1): fresh per-binary profraw from every binary the `106` label runs
  (`session_106_seam_tests` included: the spawn and wait failure exits and the commit refusal are reached only there), for
  the `session`, `config`, `log`, `capi` and `core` modules the diff adds a reachable line to; every uncovered added line or
  branch tested, waived with a one-line rationale (the respawn's `connection_bound_.running` term, the
  `install_reconnected_transport` and `~Session` asserts, the `Engine::stop()` term's decision side, which data-model E-3
  names defensive or seamless; and `L-106-f`'s guard `GiveUp106.PreActive_LimitOff_NotCutByEstablishment`, dropped by
  orchestrator ruling A because no seam can block the Engine's own accept-path write, its basis the code reading in the
  evidence file's T074 section; and `OneEnding106.Watchdog_PreActive_NoEstablishmentRecord`, dropped by ruling 1 on the
  Phase 6 report for the same reason, so M10 has no cell), or escalated to a filed, labelled issue, in
  `.specify/decisions/106-logon-guards-write-bounds-verify.md`.
- [ ] T168 **(orchestrator)** windows-msvc-debug in `/mnt/c/temp/fixpp`, after taking `.sandbox-lock` (PARENT
  `research/G19-fix-fpml-iso20022/msvc-local-build-procedure.md` Step 0), never at the same time as a Linux build: build
  the feature's own targets (`session_106_tests`, `session_106_seam_tests`, the touched config, transport and C-ABI test
  targets, the library) with `-k 0` before the first labelled push, and run them targeted (`-L 106`, the touched binaries'
  filters). Check new identifiers against the `<windows.h>` macro class (`near`, `far`, `min`, `max`, `small`, `ERROR`,
  `IN`, `OUT`). A TLS cell that skips is recorded with its reason and not counted as a pass. msvc-asan only if a change
  needs it. Release the lock after.
- [ ] T169 One full `ctest --preset linux-clang-debug --no-tests=error -j3` (Article XVII §7), the last test run before
  the labelled push, in `$V` (Execution rules; created with `git worktree add --detach` at the head on
  `/mnt/wsl/fixppbuild`, clean status, reconfigured rc 0, every target built `-j5 -k 0`; never `$M`), after reading the sandbox lock; record the count, any failure and its
  attribution, and the SHA for the PR body's `local build: green on linux-clang-debug @ <sha>` line.
- [ ] T170 `.specify/decisions/106-logon-guards-write-bounds-evidence.md`: `linux-gcc-release` `-Werror` build of the touched targets (the library, `session_106_tests`,
  `session_106_seam_tests`, the touched config, transport, C-ABI and alloc-guard targets, `engine_send_bench`),
  `ninja -k 0`, no error, no warning.
- [ ] T171 The WHOLE pytest suite: `python3 -m pytest bindings/python/tests` with the debug tree's module rebuilt at
  C-ABI 1.19; record passed and skipped.
- [ ] T172 Every buildless Tier 1 step, derived from `.github/workflows/tier1.yml` and run locally: `ci-script-pins`
  (all its steps, including `check_co_spawn_lambda.py --self-test` and its gate), `check-layers`, `co-spawn-closure-audit`
  (it applies: the connection bound's spawn, against T019's base list) and `ci/pump-census.sh` against
  `ci/expected-pump-sites.txt`. Each rc 0, or the failure attributed.
- [ ] T173 Comment-claim lint: PARENT `.claude/scripts/check-comment-claims.py --root PARENT/research/G19-fix-fpml-iso20022/library --base origin/main`;
  read added runtime strings and inventories by hand (the write-timeout format string, the Doxygen added in
  T029).
- [ ] T174 Line-citation gate: `python3 tools/check_line_citations.py --range origin/main..HEAD` and
  `--shift-audit origin/main..HEAD` (the feature's own `specs/` bundle is charged by the range gate).
- [ ] T175 B&L checks in the parent: PARENT `.claude/scripts/check_bl_citations.py` (every cell a B&L row cites exists in
  the head binaries' `--gtest_list_tests`) and `.claude/scripts/check_bl_delta.py gate --feature 106-logon-guards-write-bounds`
  (close-out row 2), each run with its self-test or a seeded miss first.
- [ ] T176 `.specify/decisions/106-logon-guards-write-bounds-evidence.md`: Re-run T151 at the PR head and record it.
- [ ] T177 **(orchestrator)** `/speckit-verify 106-logon-guards-write-bounds` (Article XVII §8), its plan targeted only
  plus the ONE full ctest of T169; writes `.specify/decisions/106-logon-guards-write-bounds-verify.md`. Re-run
  T162, T173, T174, T172 and T175 as the last actions before any labelled push.
- [ ] T178 **(orchestrator)** The PR body, written before the gate labels and before CI runs (an edited body re-queues the
  matrix): C-ABI 1.19 **BREAKING** (behaviour only) with the `version.h` history entry and `B-106-6`; the C++ layout changes,
  the C++ additions and the error enumerator (`B-106-6`'s list);
  the `local build: green on linux-clang-debug @ <sha>` line (T169); the Gate A outcome
  (`.specify/decisions/106-logon-guards-write-bounds-gatea.md`) and, after it, the Gate B outcome; ruling R14 with its scope
  named (spec.md Clarifications, R14 and R15): the `Session::send` half, with `write_timeout_ms` set on a user clock,
  is a new conditional zero, flagged for the owner's review at merge; the emit half is a disclosure of base behaviour,
  since the base's liveness loop already waits through `Clock::sleep_until` once per cycle on a user clock; closing keywords only for #702, #703, #714, #717 and #724, with `closingIssuesReferences` read
  back (a keyword fires inside a negation, in the body and in commit messages). Then the final sign-off order: body →
  labels in one API call → wait about 2 minutes → push last.
- [ ] T178a **(orchestrator)** Gate B prep, before T179: re-run T050's reading at the head against the base (research
  Risks, "Coroutine frame growth in the Logon arms"; contract C-9's per-Logon row). For each Logon arm the diff touches
  (`on_inbound_not_connected_`, `on_inbound_logon_sent_`), list every local declared before a `co_await` that it
  outlives, at the head and at `$B`; a new one fails the rule and moves into a callee. Record the two lists and the
  ruling in `.specify/decisions/106-logon-guards-write-bounds-evidence.md`, cited in the Gate B record.
- [ ] T179 **(orchestrator)** `/gate-b` on the PR (preconditions: the verify record non-RED, T183 non-failing, the
  B&L delta recorded); every fixer round re-runs the touched cells only, then T177's last actions.
- [ ] T180 `/mnt/wsl/fixppbuild/fixpp-106-base`, `/mnt/wsl/fixppbuild/fixpp-106-mutants` and `/mnt/wsl/fixppbuild/fixpp-106-verify`: Remove `$B`, `$M` and `$V` (`git worktree remove`, then `git worktree prune`) once every RED reading, mutant,
  bench run and the full ctest of T169 is recorded; never `fixpp-parallel`, never the MSVC sandbox.
- [ ] T181 **(orchestrator, post-merge)** Close-out per `.specify/close-out.md` §1 (rows 3b re-check, 4, 5, 9, 16, 17,
  copied into the Gate B record): a `CLAUDE-history.md` entry for `106-logon-guards-write-bounds` (newest first: the
  batches, the PR and squash SHA, the closed issues, the B-106 rows, C-ABI 1.19), and `CLAUDE.md`'s "Last merged FEATURE"
  line replaced to name 106 with the same pointers, 105's line moving to `CLAUDE-history.md` only.

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [ ] T182 **Catalogue close-out**: read `spec/feature-catalogue.md` for every OFFICIAL row whose Feature column names 106
  or whose subject is a 106 issue (`grep -nE '106-logon|#702|#703|#714|#717|#724'`), then by subject the rows this feature's
  normative references name (`T-041` FIXS authorization linked to authentication; `TC-001` Logon 1B/1S; the
  `[FIX-SL §4.3.11]`, `§4.5.1`, `§4.5.5` and `§4.6.4` session rows); flip each feature-owned row to `done` with the PR and
  evidence and its `spec/coverage-index.md` entry (the exact keys of spec.md's Normative References, `[FIXS §4.4]` as its
  FIXS section's §4.4 row, also named in the "Added T-041" gap note), or record "none owned" after the check. Rows this feature amends but does not own get a dated
  "106 amends this row" note naming the B-106 rows and witnesses (cell names checked against the head binaries'
  `--gtest_list_tests`); their state does not change.
- [ ] T183 **Feature-completeness audit (MUST be the FINAL task)**: against the final head, (i) every row of this file is
  `[X]` or carries a waiver rationale (T147, T180 and T181 recorded as pending where the merge has not
  happened); (ii) every FR and SC maps to a landed cell and a landed implementation (the FR/SC table below, re-read cell by
  cell against `--gtest_list_tests`), every mutant of the mutant table maps to a recorded RED, every item of plan.md
  "Deferred to tasks.md" maps to its cell or named ruling (the deferred table below), and every contract clause to a
  landed cell or its named instrument; (iii) every feature-owned OFFICIAL catalogue row is `done` with its
  `coverage-index.md` entry (T182). Record the verdict, 100 % or fully waived, in
  `.specify/decisions/106-logon-guards-write-bounds-verify.md` under `## Completeness`.

---

## Traceability: FR/SC → tasks

| Requirement | Cells | Implementation | Mutants / instrument |
|---|---|---|---|
| FR-001, FR-002 (asan cap, recipe) | this PR's Tier 2 runs (T147) | T145, T146 | — |
| FR-003, FR-004 (gates first, silent) | T038, T039, T040, T041 | T045, T046, T047, T048, T049 | T053; census T007 |
| FR-005 (admitted: as the base) | T042 | T047, T048 | T052, T054, T055 |
| FR-006 (NextNumIn kept) | T056, T057, T058, T059 | T061, T062, T063, T064 | T067, T068, T069, T070; census T008, T009 |
| FR-007 (104 precedence cells) | T043, T051 (`-L 104` unfiltered) | — | census T009 |
| FR-008 (give-up at its instant) | T071, T072, T073, T074 | T082, T083, T084, T085, T086 | T092, T093, T094, T095 |
| FR-009 (blocked writes return) | T071, T071a, T071b, T072 | T082, T079 | T098 |
| FR-010 (the write limit) | T100, T101, T102 | T111, T112, T113 | T118, T119, T120, T121, T130 |
| FR-011 (ending Logouts keep 105's limit) | T103, T104 | T110 | T122, T123, T124 |
| FR-012 (C++ and TOML; no C setter) | T025, T026 | T029, T030 | T034, T035; statement T160 |
| FR-013 (initiator handshake key) | T025, T131 | T030, T135 | T140, T144; ruling T138 |
| FR-013a (acceptor handshake key) | T025, T026, T132 | T029, T030, T136, T137 | T141, T142, T036, T037; census T012; statement T160 |
| FR-014 (one bounded attempt) | T133, T139 (`AsioTlsTransportErrorPaths`) | T135 | T143 |
| FR-015 (B&L) | — | T152, T153, T154, T155 | T020, T175 |
| FR-016 (RED first) | every RED task: T027, T044, T060, T077, T109, T134 | — | every mutant task |
| FR-017 (one ending) | T075, T105, T076, T106, and the FR-017 assertions of T071 and T100 | T078, T079, T080, T081, T112 | T097, T127, T096, T128, T129, T098, T130; census T015, T016 |
| SC-001 | T038 to T041 | US1 implementation | US1 mutants |
| SC-002 | T056, T057 | US2 implementation | T067, T068 |
| SC-003 | T071 (and on every sanitizer lane, T165) | US3 implementation | T092 |
| SC-004 | T100 | US4 implementation | T119 |
| SC-005 | T133, T132 | US5 implementation | T143, T141 |
| SC-006 | T147 | T145 | — |
| SC-007 | T051, T066, T091, T117, T139, T169 | — | — |
| C-ABI 1.19 (D7, C-6) | T024 | T028, T148, T149, T150 | T033; T018, T151, T176 |
| Article VIII §2, §3; XV §1 (C-9) | T108 | T083, T111 | T125, T126; T164, T166 |
| Supersession | — | T049, T065, T081, T087, T116, T136 | T022, T158 |

## Traceability: plan.md "Deferred to tasks.md" → tasks

| Deferred item | Task | Ends in |
|---|---|---|
| M4 at the Logout sites other than the R-1 leaf and `run_logout_phase1` | T010, T104, T124 | a `WriteTimeout106.EndingLogout_<Site>` cell with M4 + M9 + M9′, or a named unheld site |
| D5's `dispatch_aborted` census | T014, the `dispatch_aborted` ruling table, T113 | the F9 block ruled redundant (no observable), covered by M13 at that site (T130) |
| M13 sites that no cell reaches | T015 (the Disconnected-site table), T079, T098, T130 | each reachable site with an arm and a cell; each other named unreachable with its reason |
| The exact 30 s default of `tls_handshake_timeout_ms` | T138 | a named ruling (the accessor D6 rejects), plus any existing pin cited |
| The RAII clears on the exception and `catch` returns | T088 (`emit_in_flight_`), T115 (`write_in_flight_`) | every return shown to reach the edge, or a cell with the clear's mutant |
| The loop's give-up block with the claim refused | T089 | `OneEnding106.CloseDuringGiveUpBlock_NoRecord` with its mutant, or a named unreachable ruling |

Also named with no cell by the bundle, and recorded so by the tasks shown: `Engine::stop()`'s exit term (no seam
between its steps 1 and 2; T167), the respawn's `connection_bound_.running` term (defensive; T114, T167),
the `install_reconnected_transport` and `~Session` asserts (T084, T167), `Teardown_Close_*` and
`Teardown_EngineStop_*` (covered by the `Disconnected` edge; T076).

## Traceability: mutants → tasks

| Mutant | Task | Mutant | Task |
|---|---|---|---|
| M1 | T052 | M7 | T140 |
| M1a | T053 | M7′ | T141 |
| M1b (item-4 and held-Reject arms) | T054 | M7″ | T143 |
| M1c | T055 | M7‴ | T142 |
| M2 (both arms) | T092, T093 | M7⁗ (both arms) | T036 |
| M3 | T118 | M8 | T126 |
| M3′ | T119 | M9 with M9′ | T097, T127 |
| M3″ | T120 | M9 with the closing term | T127, T123 |
| M3‴ | T121 | M9″ | T096 |
| M4 per Logout site | T122, T123, T124 | M10 (no cell by construction) | T128 |
| M5 | T125 | M11, M11′ | T034, T035 |
| M6a, M6b | T067, T068 | M12 (four joiner arms) | T099 (`ConnectionEnd`), T129 |
| M13 per site | T130 (T098's arms read there, orchestrator ruling B) | M14 | T094 |
| M15 | T033 | the commit's result ignored | T069 |
| an unconditional close at G | T095 | the knob's reset moved after S3 | T070 |
| the key accepted at session scope | T037 | the key refused on an acceptor or plaintext session | T144 |
| the spawn and wait seams | T099, T129 | the respawn deleted; its refused spawn's path deleted | T129 (arms d, e) |
| a wake ends the bound (`SpuriousWake_NoEnding`) | T099 | the two rows' order swapped (RowSwap; spec.md R18) | T127 |
| the refused respawn marks the cut epoch (proposed) | T129 | the normal-path clear deleted | T115 |

## Traceability: contract clauses → tasks

| Clause | Cells | Implementation / instrument |
|---|---|---|
| C-1.1 to C-1.3 | T038, T040, T041 | T047, T048 |
| C-1.4, C-1.4a | T039, T042 | T046, T045 |
| C-1.5 | T042 | T047 |
| C-1.6 (event order; `L-106-c`) | statement | T154 |
| C-1.7 | T040 | T048 |
| C-2.1 to C-2.3 | T056, T057, T058 | T062, T063 |
| C-3.1 to C-3.5 | T071, T072, T073, T074, T091 (US3 scenario 4) | T082 to T086 |
| C-4.1 to C-4.5 | T100, T101, T102, T103 | T110 to T113; C-4.1's no C setter: T160 |
| C-4.6 | T075, T105 | T078, T080, T081 |
| C-4.7 | T025, T026 | T030 |
| C-5.1 to C-5.3 | T025, T131, T132, T133 | T135, T136, T137 |
| C-5.4 (no C setter) | statement | T160 |
| C-6 | T024 | T148, T149, T150, T178 |
| C-7 | statement | T154 (copied) |
| C-8 | the cells each row names | T153, T154 |
| C-9 | T108 | T164, T166, T178a |

## Dependencies and execution order

- **Before Phase 1 (orchestrator)**: `/speckit-analyze`, the `checklists/requirements.md` refresh and
  `/speckit-checklist-audit` (`.specify/pipeline.md` step 9; it blocks implementation; a SPEC-FIXED item re-runs
  `/speckit-analyze`). The user's `/plan` sign-off (plan Constitution Check, the fourth control) is recorded by the
  orchestrator with its date, words and bundle head in T037a, before T038.
- **Phases**: 1 → 2 → 3 (US1) → 4 (US2) → 5 (US3) → 6 (US4) → 7 (US5) → 9 → 10. Phase 8 (US6) is done but
  T147, which follows the PR's first Tier 2 run. US1 to US4 all edit `src/session/session.cpp` and run in order,
  never in parallel. US2 follows US1 (both Logon arms). US4 depends on US3's bound and claim (T078 to T084).
  US5 touches `src/config` and `src/session/engine.cpp` only and could follow Phase 2 directly, but runs after US4 to keep
  one build at a time and one reviewer queue.
- **Within each story**: cells → RED reading → implementation → green run → commit `H<n>` → mutants at `H<n>`.
- **Phase 9** after US1 to US5 (its rows cite their cells). **Phase 10**: T161 first; T169 the last test
  run; T177 after T161 to T176; T178 after T177 and before the labels; T179 after the
  labels; T180 before T182; T182 → T183 last.

### Parallel opportunities

- Phase 1: the censuses T006 to T023 are read-only and run in parallel.
- Phase 2: T024, T025 and T026 (three files).
- US3: T071 (`test_106_give_up.cpp`) and T075 (`test_106_one_ending.cpp`); the other US3 cells share
  those files or the seam file.
- US5: T133 (`test_106_handshake.cpp`) beside T131 when the minting cells are in the config suite.
- Phase 9: T148, T149 and T156 (different files); the B&L tasks share one file and run in order.
- Phase 10: the read-only checks T173, T174 and T175; no two builds ever run at once.

## Implementation strategy

- **MVP = US1** (Phases 1 to 3): the identity gates first, the security-affecting defect (#702, Article XII §9). It is
  a checkpoint, not a shippable state: the batch is one PR and no story ships alone.
- **Then** US2 (P1, the other half of the Logon arms), US3 (P1, peer-reachable denial), US4 and US5 (P2), the
  surfaces and records, and the Polish phase, in that order.
- **Who executes.** A `phase-implementer` agent runs the implementer's tasks, one phase per invocation, each ending at its
  commit (`H0` to `H5`); the orchestrator runs the tasks marked (orchestrator), reviews every RED reading and mutant table
  before the next phase, and holds the MSVC sandbox lock.
- **Before any labelled push**: T177's last actions re-run, then the final sign-off order (body, labels, wait, push
  last).
