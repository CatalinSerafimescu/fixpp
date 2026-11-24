---
description: "Task list for 105-transport-liveness"
---

# Tasks: Transport and liveness — a session that ends closes its connection; TestRequest timing and pre-session rejects

**Input**: the converged bundle `specs/105-transport-liveness/` (Gate A loop 2 round 2, P1 0 / P2 0, its P3s applied):
spec.md (US1 to US5, FR-001 to FR-019, SC-001 to SC-006, rulings R-1 to R-4, Q1, Q2, D-a to D-g), plan.md (Summary,
"Phases (for /speckit-tasks)", Project Structure, T-DOC-1, "Follow-ups to file before Gate B"), research.md (F-1 to
F-16, D1 to D12, R1 to R4), data-model.md (E-1 to E-9), contracts/transport-liveness-105.md (C-1 to C-12),
quickstart.md (sections 1 to 6), and the post-Gate-A `/speckit-plan` re-run (spec clarification "Session 2026-11-21 (post-Gate-A plan re-run)":
[seam] and [guard] cells, the base). This file replaces the pre-Gate-A tasks.md entirely; no earlier task number is carried
forward. Where this file and the bundle differ, the bundle wins and the difference goes to the orchestrator.

**Tests**: required (`[const §VII.3]`, FR-017). In every story phase the RED cells come first as their own tasks, then the
mechanism, then the mutant and spurious-hit arms. Every close cell asserts the peer-observed EOF (or close_notify), a
log record, or a timing on the mock clock, never an internal flag. Where a cell reads test access, that read is only
the proof that the cell reached its path; its oracle stays peer- or application-visible.

## Format: `[ID] [P?] [Story] Description`

`[P]`: a different file from every other open task of its phase and no dependency on an incomplete task (appending
only one's own section of the evidence file does not count as a shared file). `[US1]` to `[US5]`: story phases only.
Paths are relative to the library root unless marked PARENT (`/home/catalin/Work/Programming/Antreprenoriat/`). A task
marked **(orchestrator)** is run or requested by the orchestrator; every other task is the implementer's
(`[const §XVI.6]`).

## Execution rules (apply to every task)

- **Trees.** `$T` = the main checkout `PARENT/research/G19-fix-fpml-iso20022/library`, branch `105-transport-liveness`.
  `$B` = `/mnt/wsl/fixppbuild/fixpp-105-base`, a detached worktree at the base, `git -C $T merge-base HEAD origin/main`
  (RED readings and bench bases only).
  `$M` = `/mnt/wsl/fixppbuild/fixpp-105-mutants`, detached (mutants, census scratch edits, seeded known positives only).
  Never `git checkout`/`git switch` in `$T`, never `git stash`, never `--amend`, never `git commit -a`.
  `fixpp-parallel` and the MSVC sandbox are never `$B` or `$M`.
- **The base.** "RED on the base" means read on `$B`, the branch's merge-base with `origin/main` (spec FR-017 and
  clarification "Session 2026-11-21 (post-Gate-A plan re-run)"). Its `src/` and `include/` must be identical to `52aeed4e`, where the design was
  read: `git diff --stat 52aeed4e "$(git -C $T merge-base HEAD origin/main)" -- src include` prints nothing. T001 runs
  that recipe and records its output; a non-empty output stops Phase 3 and goes to the orchestrator. No commit hash is
  the base: a rebase moves the merge-base, and `$B` is then re-created at the new one and the recipe re-run.
- **Builds.** One build or ctest process at a time on the machine, under `flock /tmp/fixpp-build.lock`; a long one
  detached (`setsid nohup … ; touch <marker>.done`) with one background waiter on the marker, never a foreground poll,
  never `pgrep -f`. `$B`, `$M` and every sanitizer, release, coverage and bench tree live on `/mnt/wsl/fixppbuild`
  (`export CCACHE_DIR=/mnt/wsl/fixppbuild/ccache`). `$T`'s `build/linux-clang-debug` is built incrementally only. Read
  `/mnt/c/temp/fixpp/.sandbox-lock` before any full ctest and before any MSVC run; a lock held by another owner means
  ask the owner.
- **RED on the base.** Cells are committed in `$T` (commit `C`), overlaid on `$B` with
  `git -C "$B" restore --source=C -- $(git -C $T ls-tree --name-only C tests/session/ | grep -E '^tests/session/test_105_') tests/session/CMakeLists.txt`
  (every `test_105_*` file, `test_105_seams.cpp` and `test_105_support.hpp` included, since the CMakeLists registers them),
  reconfigured (a reading counts only after the reconfigure returned 0: a failed one leaves the old registration), built
  (target `session_105_tests` only) and run there with `ctest --preset <p> -L 105 -LE 105_seam --no-tests=error`, and must
  fail by an assertion for the reason the task states. A TLS cell that skips (`FIXPP_TLS_FIXTURE_DIR` unset) is not a
  reading: every reading records its skip count, and a non-zero one fails the step. A compile failure, abort, crash, hang or
  ctest timeout is not RED. A cell expected RED that reads GREEN on the base stops its task; the reading goes to the
  orchestrator.
- **Cells that cannot compile on the base.** Every cell, or reach-proof twin, that names a symbol 105 adds (an
  allocation-failure seam, the bound's in-body race seam, the listener-construction seam, a new `constexpr` helper, a
  test-access read of a new member) lives in `tests/session/test_105_seams.cpp`, registered as its own ctest
  `session_105_seam_tests` (labels `105` and `105_seam`, T003). A base reading overlays every `test_105_*` file and the
  CMakeLists, but builds only the target `session_105_tests` and excludes the seam entry by label (`-LE 105_seam`), so a
  seam cell never breaks a later base reading and its unbuilt executable is never run there. Its task
  marks it **[seam]** or **[guard]** and names the mutant under which it must go RED at the implementation head; that RED
  stands in for the base reading and is recorded as such. A reach-proof twin repeats its RED cell's scenario and adds only
  the test-access read; the RED cell itself keeps its peer-visible oracle and stays in its story file. Any other cell that
  fails to compile on `$B` stops its task.
- **Guards.** A **[guard: <mutant>]** cell keeps a rule the base already keeps: GREEN on the base by design, and RED
  under its named mutant.
- **Guard static_asserts.** Every `static_assert` of a [guard] `constexpr` cell in `tests/session/test_105_seams.cpp` sits
  under `#if FIXPP_105_STATIC_GUARDS`, a compile definition of `session_105_seam_tests` set from a CMake option that is ON
  by default (T003), so the head build and every GREEN run compile them (check: the definition is on that target's
  `compile_commands.json` entries). A mutant of such a cell is built in `$M` reconfigured with
  `-DFIXPP_105_STATIC_GUARDS=OFF`, so the mutant compiles and the cell's runtime twin (the same values asserted with
  `EXPECT_*`) gives the RED. That reading counts only when the reconfigure returned 0, the `strings` check shows the binary
  is fresh, and the runtime twin fails by an assertion; a compile error is not RED. `$M` is reconfigured with the option
  ON before its next mutant.
- **"At once" cells (SC-001, contract preamble).** The mock clock is held still, and the executor is pumped with the
  readiness-checked helpers of `tests/support/pump_until_ready.hpp` (or T004's bounded drive) until the peer's read ends
  or a wall cap passes; the cap fails as an assertion. Each such cell names the wall-clock timers that could also close
  the connection (the TLS handshake and first-frame deadlines, `close_grace`, the drain's bound,
  `asio_tls_transport::close_async`'s `close_deadline`) and sets each one it can reach well above its wall cap.
- **Bounds from the competing timeout (FR-017).** Every bounded close is asserted on both sides of its bound on the mock
  clock (the bound − 1 ms open, the bound EOF), and every cell has a spurious-hit arm: the mechanism under test deleted in
  `$M` turns it RED. Every competing timeout runs on the mock clock and is set past the instant the cell tests.
- **Liveness instants in US1 and US2 cells.** A US1 or US2 cell that reaches a liveness instant (the give-up, the
  Heartbeat instant) sets `test_request_threshold = HeartBtInt` and is driven by T004's wire reader, so its instants are
  the same on the base (which ignores the field), at the US1 head and at the final head, up to the 1 ns guard.
- **Selection.** `ctest --preset <p> -L 105 --no-tests=error --output-on-failure` at the head (on `$B`, `-L 105 -LE 105_seam`);
  one cell by the binary's
  `--gtest_filter='*<Suite>.<Cell>*'`. Every run records its reported test count; 0 fails the step. Never
  `ctest -R <cell>`.
- **Mutant protocol.** Each mutant edits only `$M`'s copy of the file that holds the named mechanism
  (`src/session/session.cpp` unless the task names `src/session/engine.cpp`, `src/config/scalar_mappers.cpp` or a test-side
  instrument) and records its row in the evidence file. At commit `H` holding the mechanism: `$M` at `H` with a clean status; apply only the named edit;
  `strings`-check a marker so the binary is fresh; build; run the named cells, then the `105` label unfiltered; record
  RED per cell; revert with `git -C "$M" restore . && git -C "$M" clean -fd`. A CMake-touching mutant counts only after
  its reconfigure returned 0.
- **Evidence.** Census outputs, RED readings and mutant tables go to
  `.specify/decisions/105-transport-liveness-evidence.md`, never into a code comment. A comment may record a procedure,
  never a result. Cite symbols, never line numbers.
- **CodeGraph.** `projectPath` = `$T` only; `codegraph sync` in `$T` after every code-changing phase.

---

## Phase 1: Setup (no production code)

- [X] T001 Check `git -C $T rev-parse --abbrev-ref HEAD` is `105-transport-liveness`; run the base-equivalence recipe of
  the Execution rules and record its output; `df -h /mnt/wsl/fixppbuild`, then create `$B` (at
  `git -C $T merge-base HEAD origin/main`) and `$M` with `git worktree add --detach`; run `codegraph sync` in `$T` and confirm `codegraph status --json` names `$T` with a nonzero
  `fileCount`.
  Done: evidence file, section T001. The base-equivalence recipe printed nothing.
- [X] T002 Create `.specify/decisions/105-transport-liveness-evidence.md` with one section per census (T006 to T017), per
  RED reading, per mutant task and per bench run.
  Done: evidence file, header and section skeleton.
- [X] T003 Register ctest `session_105_tests` (labels `session` and `105`) in `tests/session/CMakeLists.txt` after
  `add_session_104_bucket`'s shape, over `tests/session/test_105_connection_close.cpp`, `test_105_logout_wait.cpp`,
  `test_105_write_limit.cpp`, `test_105_liveness_timing.cpp`, `test_105_heartbeat_knob_off.cpp`,
  `test_105_presession_log.cpp` and `test_105_tls_close.cpp` (research D10), each created here as a compiling stub (an empty translation unit) so that configure succeeds in `$T` and
  on `$B` before its cells exist, and a second entry `session_105_seam_tests` (labels `session`, `105` and `105_seam`)
  over `tests/session/test_105_seams.cpp` alone, also created as a stub (Execution rules, "Cells that cannot compile on the base"), with a CMake option
  `FIXPP_105_STATIC_GUARDS` (default ON) passed to it as a compile definition (Execution rules, "Guard static_asserts"). Find every list a new entry must join with
  `git grep -nE 'session_104_tests|add_session_104_bucket' -- ci tools .github tests` (known positive: the CMakeLists
  entry), and `git grep -nE '105_seam|expected-.*label|LABELS' -- ci tools .github` for any list that enumerates labels, which
  the new `105_seam` label must join; read `ci/pump-census.sh` against `ci/expected-pump-sites.txt` (the new files join its population, so they use
  readiness-checked pumps, never `run_for(W); restart(); get()`); before placing `test_105_tls_close.cpp`, copy the
  registration form of an existing user of `loopback_tls_session_harness.hpp` (`git grep -l loopback_tls_session_harness
  tests` and its CMake entry), including whether it is listed in `ci/expected-preset-conditional-tests.txt`. The Engine-over-TLS rig
  is `tests/session/engine_loopback_harness.hpp` (`EngineLoopbackHarness::build`; research D10): a real
  `make_asio_tls_transport_factory` under `mtls_ca`, the peer a `LoopbackTlsFixture::make_client` mTLS client as
  `engine_firstframe_test.cpp`'s `probe_post_handshake` drives it, acceptor role (the registered initiator's endpoint is a
  placeholder). `loopback_tls_session_harness.hpp` drives a direct initiator Session through `on_inbound_frame`, with no
  pump, and serves only the direct-Session TLS cells. Both entries define `FIXPP_TLS_FIXTURE_DIR` and link `fixpp_tls` and
  the OpenSSL and zlib libraries, as `add_session_104_bucket` does, so no TLS cell skips.
  Done: evidence file, section T003. `ci/expected-eligible-tests.txt` pins a per-lane ctest count the two entries move; its rules require a CI reading, so it is left to the PR's first CI run.
- [X] T004 Write `tests/session/test_105_support.hpp`, part 1: a peer-side observer of EOF and of close_notify on
  `engine_loopback_harness.hpp` (a `mock_clock` passed in `EngineConfig::clock`, since `build()` installs
  `system_clock_source` only for a null one; the acceptor's `SessionConfig` edited through `configure_acceptor`),
  `plain_engine_rig.hpp` and `loopback_tls_session_harness.hpp`; on TLS the observer reads close_notify as
  `transport_read_eof` (the ending `tests/transport/test_close_truncated_mapping.cpp` pins apart from
  `transport_read_truncated`) and any other read ending as abortive, pinning `transport_read_truncated` only where a
  reading has shown it; a frozen-clock "pump
  until the peer's read ends or the wall cap" helper whose cap fails as an assertion; a bounded drive (`run_one` × N per
  mock-clock step, never run-until-idle); a capturing log sink; a wire reader that advances the mock clock until a named
  MsgType is read, with a mock-clock cap above 2.4 × HeartBtInt plus the guard that fails as an assertion; an
  `INSTANTIATE_TEST_SUITE_P` over `session_role` as 537's cells use. No production symbol 105 adds is read, so it
  compiles on `$B`.
  Done: evidence file, section T004 / T005. `test_105_connection_close.cpp` includes the header (it is not an empty TU), so every build of `session_105_tests`, on `$B` too, compiles it.
- [X] T005 `tests/session/test_105_support.hpp`, part 2 (same file as T004): the counting `Clock` decorator over
  `mock_clock` (counts `sleep_until` calls and forwards each; on request from the test, throws `std::bad_alloc` from
  `sleep_until` for one named instant only), for `SessionConfig::clock_override`; the held-write transport (the scripted
  transport's `write_hold` of `include/fixpp/transport/test/mock_transport.hpp`, as `test_103_resend_state_reconnect.cpp`
  sets it); the **deferred-completion transport** (its `async_write` commits the bytes to the peer, then holds its success
  completion behind a second flag and returns success once released, even after a close; data-model E-3,
  `ending_wait_.cut`); a store whose call can be held and released, and one that fails on demand; a TLS peer that stops
  reading (a zero window). All compile on `$B`.
  Done: evidence file, section T004 / T005.

---

## Phase 2: Foundational — censuses (read-only; plan phase 1; quickstart section 1)

**Purpose**: fix the populations every story asserts over, each census shown first to report its known positive. No
production code. **Blocks every story phase.**

- [X] T006 [P] Ending-site census (research D1, "The census, by execution context"): run its write-site grep and root
  grep over `src/session/session.cpp` and `src/session/engine.cpp`, walk each site with `codegraph_callers` (`projectPath`
  = `$T`) to every root, and regenerate the **Ending-site table** below (one row per site and root, class P/D/A/W/X of
  data-model E-1, cell task). The walk must reproduce the three known positives (the unanswered-TestRequest write under
  `run_liveness_loop`; Arm B of `on_inbound_active_out_of_sequence_` under the pump; `send_body_`'s failure write under
  the send root). A site with no cell and no named exemption fails SC-001. Each write of `Disconnected` over
  `Disconnected` is recorded as a census row with the path that reaches it (data-model E-2 I-5; no peer-visible observable, so no
  cell). Acceptance: no row of the regenerated table is left unclassed; each seed row reading "class: assigned by T006"
  gets P, D, A, W or X, or a named exemption.
  Done: evidence file, section T006. Escalated there: `honor_peer_next_expected_`'s two ending Logouts lie outside D2 item 1; data-model E-2 I-5's "synchronous chain" premise does not hold for an unguarded write after a suspension (its conclusion does).
  Ruled (orchestrator, 2026-11-21): the two Logouts are R-2 ending Logouts (ruling R-789; research D2 items 1 and 11, contract C-2 "Which Logouts", the table row below, T049, T051, T059, T063); E-2 I-5's premise corrected in data-model.md, its conclusion kept.
- [X] T007 [P] R-2 caller and write-point census (research D2 items 1 and 11): D2 item 1's census grep (the four leaf and refusal
  callers), each hit classed
  "initiates" or "replies" (known positive: `resend_gap_deadline_` initiates, `on_inbound_active_logout_` replies); and
  `grep -n 'inbound_logout_leaf_(tic' src/session/session.cpp`, each hit's enclosing function an ending, with
  `run_logout_phase1` reaching no such call.
  Done: evidence file, section T007.
- [X] T008 [P] Silent-ending and absorption census (research D2 item 4): D1's write-site grep restricted to the frame
  arms, each hit classed "peer-frame condition" (goes through `end_session_silently_`) or "internal failure" (stays on the
  edge); known positives: the BeginString guard of `on_inbound_active_` (peer-frame) and `inbound_resend_request_leaf_`'s
  assignment failure (internal).
  Done: evidence file, section T008.
- [X] T009 [P] Supersession census (FR-019, research D11): run D11's `git grep` over `specs/ .specify/ brain/ spec/ tests/
  include/ src/ ci/`; it must report its three known positives (`tests/session/test_537_gap_deadline.cpp` "fixpp#534",
  `include/fixpp/core/error.hpp` "1×HeartBtInt", `src/session/session.cpp` "Phase 6 US4") and, by the pattern's
  `off only at` alternative, `src/session/read_first_frame_bounded.hpp` ("off only at exactly 0"), and by its
  `UnansweredTestRequestDisconnects` alternative (orchestrator ruling, 2026-11-21) `tests/session/test_heartbeat_cadence_8cell.cpp`'s
  `HeartbeatCadence8Cell.CellC_*_UnansweredTestRequestDisconnects`. Class each hit: merged
  bundle (`specs/005-*`, `013-*`, `092-*`, `093-*`, `103-*`, `104-*`: superseded by name, never edited), living surface
  (rewritten by a named task below), this bundle (`specs/105-transport-liveness/`, where "off only at 0" describes the
  base), or not this rule. Every FR-019 known-positive pin must appear; a living-surface hit with no task
  is added to T128.
  Done: evidence file, section T009, and its addendum "Re-run after the ruling": with the `UnansweredTestRequestDisconnects` alternative added to D11's pattern (orchestrator ruling, 2026-11-21) the census reports every known positive and every FR-019 pin, `HeartbeatCadence8Cell.CellC_*_UnansweredTestRequestDisconnects` included; the classed table and the T128 additions are in the evidence file.
  Widened again by the T035 ruling (orchestrator, 2026-11-21): the pins the H1 reading found outside FR-019's list, keyed on
  their old claims, add the alternatives `(with|the|its) transport open`, `ends (the session|it),? silently`,
  `ends at the grace check`, `session ends at the grace`, `written after the reply`, `one Disconnected entry` and
  `Disconnected entries in the` (research D11). Run on `0ea8cbf7` (the tree before those re-pins), the widened pattern
  reports the eight known positives the old one does not; every added hit is classed in the evidence file, section
  "T035 — the ruling's adjudication".
- [X] T010 [P] Size-pin census (research D7): the narrow
  `sizeof\((fixpp::session::)?(Session|ReconnectFsm)\b` grep over `src include tests ci tools bench bindings`, then the
  wide grep that must report `tests/dictionary/ref_shape_test.cpp`'s `FieldRef` pin (the instrument's proof); list every
  hit for T112.
  Done: evidence file, section T010.
- [X] T011 [P] Moved-timing census (research D10, "The cell recipe for moved timings"): the TestRequest grep over
  `tests/session tests/capi tests/conformance tests/interop`, read for mock-clock advances across HeartBtInt that expect a
  TestRequest or a give-up, plus `git grep -nE 'stale|different' tests/session` read near `test_req_id`; list each cell
  for T099.
  Done: evidence file, section T011.
- [X] T012 [P] C-ABI minor-pin census (research D9): `git grep -nE 'MINOR 17|1\.17' -- include tests bindings tools ci
  docs`, each hit classed "pins the minor" (moved in T117) or "history" (unchanged); then the 103 bump itself:
  `git log --format=%h -S'FIXPP_C_ABI_VERSION_MINOR 17' -- include/fix/c_api/version.h` and `git show --name-only` on that
  commit, every C-ABI file it touched being a row (version macros, `session.h` clauses, capi tests, `tools/capi_freeze.sha256`
  and the generator found by `git grep -n capi_freeze -- tools ci .github CMakeLists.txt`). Known positives:
  `include/fix/c_api/version.h` (`FIXPP_C_ABI_VERSION_MINOR`) and `tests/capi/version_test.cpp`. Run the same grep over
  `bindings/` with a seeded known positive in `$M` so a zero there is an instrument that could report non-zero. Record
  the result per consumer category of Article X §7 (research D9): the Python binding (`bindings/`), the tests (`tests/`,
  the `tests/interop` harnesses named as their own row), examples (`ls -d examples`; none at the base, recorded as
  such), and the CI and tool scripts (`ci/`, `tools/`), so a missed category shows as an empty row.
  Done: evidence file, section T012.
- [X] T013 [P] `ReconnectFsm` census (research D7, SI-1): the stub caller grep (known positive: `drive_logout`'s test call)
  and the constructor call-site grep (known positive `tests/session/test_logout_timeout.cpp`).
  Done: evidence file, section T013. The constructor recipe misses `Session::Session`'s member initializer and one `std::make_unique` site; both are listed for T109.
- [X] T014 [P] Bench census (research D10, ruling D-f): read `bench/session/engine_send_bench.cpp`'s harness (its published
  Active session whose read pump waits on a scripted read), `on_inbound_frame_bench` and `session_send_bench`, and
  `bench/ci-suite.txt`'s entries for the three; record where the new pump case goes and how each is run. Fix, before the first run, the new case's frame mix and
  count (Heartbeat and application frames through `run_read_pump`, the unarmed path), identical on base and candidate;
  and record whether `on_inbound_frame_bench`'s Heartbeat case runs with a TestRequest pending (plan Constitution Check
  VIII): if it does not, T138 adds that case.
  Done: evidence file, section T014. Frame mix fixed there: one Heartbeat and one NewOrderSingle per iteration, `->Iterations(20000)`; `on_inbound_frame_bench` has no Heartbeat-with-TestRequest-pending case, so T138 adds it. Escalated there: CI's paired `engine_send_bench` entry skips a benchmark only the candidate has, so the local A-B-A-B run is the pump case's only paired reading.
- [X] T015 [P] Pre-session reject census (data-model E-6): every `(void)transport->close(); continue;` in
  `run_accept_loop` before `open()` maps to one row of E-6, plus the listener `catch (...)`; a branch with no row is a
  table defect for the orchestrator.
  Done: evidence file, section T015.
- [X] T016 [P] `co_spawn` census: `grep -n 'co_spawn' src/session/session.cpp src/session/engine.cpp` on `$B`, kept for
  T135's `co-spawn-closure-audit` and for the spawn-shape review of T027 and T059.
  Done: evidence file, section T016.
- [X] T017 [P] Establishment-cap condition (data-model E-3, "The establishment cap"): re-derive in source the
  `target<session_executor>()` test in `system_clock_source::sleep_until` and the executor `Engine::start` spawns
  `run_accept_loop` and `run_connect_loop` on; record that an Engine session's sleeps are independent, or stop and report
  to the orchestrator (the design's stated condition fails).
  Done: evidence file, section T017. The condition holds at the base.

**Checkpoint**: the Ending-site table is filled; every population has shown its known positive.

### Ending-site table (regenerated by T006 on the base by research D1's recipe; T006's recipe is re-run at the implementation head)

Function names only, one row per write site's function and root. Roots: "pump" is `on_inbound_frame` as `run_read_pump` and
`run_accept_loop`'s first-frame delivery call it (class P); the liveness loop and the too-high Logout bound are detached roots
(D); the send root (`Session::send`, `send_deferred_` for `Engine::send`), `open` and `drive_reconnect` are application calls
(A); `close()`, `run_logout_phase1` under it and the drain are excluded (X). "(T006)" marks a class this task assigned. The
walk, its supplements and the `Disconnected`-over-`Disconnected` rows are in the evidence file (T006).

| Write site (function) | Root | Class (E-1) | Cell task |
|---|---|---|---|
| `drain_for_reconnect_` | `drive_reconnect` (the drain) | X | T022 |
| `ensure_hydrated_` | `drive_reconnect` | A (T006) | T020 |
| `ensure_hydrated_` | `open` | A (T006) | T020 |
| `ensure_hydrated_` | pump | P (T006) | T018 |
| `persist_inbound_advance_` | pump | P (T006) | T018 |
| `consume_rejected_seqnum_` | pump | P (T006) | T018 |
| `disconnect_unless_superseded_` | liveness loop | D (T006) | T020 |
| `disconnect_unless_superseded_` | `close()` (`run_logout_phase1`) | X (T006) | T022 |
| `disconnect_unless_superseded_` | `drive_reconnect` | A (T006) | T020 |
| `disconnect_unless_superseded_` | `open` | A (T006) | T020 |
| `disconnect_unless_superseded_` | pump | P (T006) | T018 |
| `disconnect_unless_superseded_` | send root | A (T006) | T020 |
| `disconnect_unless_superseded_` | too-high Logout bound | D (T006) | T020 |
| `emit_initiator_logon_` | `drive_reconnect` | A | T020 |
| `emit_initiator_logon_` | `open` | A | T020 |
| `close` | `close()` | X | T022 |
| `refuse_logon_with_logout_` | pump | P; W (R-2 before Active) | T021, T049, T051 |
| `emit_session_reject_` | pump | P (T006) | T018 |
| `dispose_unparseable_` | pump | P | T018, T021 |
| `inbound_reject_leaf_` | pump | P (T006) | T018 |
| `inbound_reject_best_effort_leaf_` | pump | P (T006) | T018 |
| `inbound_logout_leaf_` | too-high Logout bound | W (reply); D | T019, T051 |
| `inbound_logout_leaf_` | pump | W (reply); P | T019, T051 |
| `inbound_logon_sent_logout_leaf_` | pump | P; W (R-2 before Active) | T021, T049, T051 |
| `inbound_resend_request_leaf_` | pump | P (T006) | T018, T046 |
| `inbound_heartbeat_reply_leaf_` | pump | P (T006) | T018 |
| `inbound_business_reject_leaf_` | pump | P (T006) | T018 |
| `inbound_acceptor_reply_logon_leaf_` | pump | P (T006) | T018 |
| `on_inbound_not_connected_` | pump | P; W (R-2 before Active) | T021, T049, T051 |
| `on_inbound_active_` | pump | P | T018, T045, T081, T082 |
| `resend_gap_deadline_` | pump | W (R-2) | T042, T048 |
| `answer_pending_test_request_` | pump | P | T018, T045, T081, T082 |
| `on_inbound_active_sending_time_reject_` | pump | W (R-2) | T042, T044 |
| `acknowledge_peer_logout_` | too-high Logout bound | W (reply); D | T019, T051 |
| `acknowledge_peer_logout_` | pump | W (reply) | T019, T051 |
| `run_peer_logout_bound_` | too-high Logout bound | D | T019, T051 |
| `on_inbound_active_poss_dup_` | pump | W (R-2) | T042, T044 |
| `on_inbound_active_out_of_sequence_` | pump | W (R-2, Arm B); P (#582 mismatch) | T042, T045; T081 |
| `on_inbound_active_logout_` | pump | W (reply); P | T019, T051 |
| `answer_resend_request_` | pump | P (T006) | T018 |
| `on_inbound_logout_sent_` | pump, while `close()` holds `closing` (LogoutSent is `close()`'s phase 1) | X: the latch's `lifecycle` term excludes it | T022 |
| `on_inbound_logon_sent_` | pump | P; W (R-2 before Active) | T021, T049, T051 |
| `send_body_` | send root | A | T020, T046 |
| `run_liveness_loop` | liveness loop | W (R-1); D | T023, T051, T070; T020 |
| `store_then_emit` | liveness loop | D (T006) | T020 |
| `store_then_emit` | `close()` (`run_logout_phase1`) | X (T006) | T022 |
| `store_then_emit` | `drive_reconnect` | A (T006) | T020 |
| `store_then_emit` | `open` | A (T006) | T020 |
| `store_then_emit` | send root | A (T006) | T020 |
| `store_then_emit` | too-high Logout bound | D (T006) | T020 |
| `store_then_emit` | pump | P (T006) | T018, T046 |
| `apply_inbound_sequence_reset` | pump | P (T006) | T018 |
| `run_logout_phase1` | `close()` (`run_logout_phase1`) | X | T022 |
| `honor_peer_next_expected_` | pump | P (T006) for its resend-failure ending; W (R-2 before Active) for its two NextExpectedMsgSeqNum(789) Logouts (ruling R-789) | T021, T033; T049, T051 |
| `record_disconnected_unless_already_` (its body in `session.hpp`) | each caller's root, rows above | each caller's class | each caller's cell task |
| `refuse_too_low_logon_` | pump | no write site of its own at the base: it ends through `refuse_logon_with_logout_` | T021, T049, T051 |
| `send_deferred_` | send root | no write site of its own at the base: it ends through `send_body_` | T020, T046 |
| `run_ending_logout_bound_` (new) | ending bound | D (E-3 rows 4, 10, 11, 16); not on the base | T043, T049, T050, T070 |

---

## Phase 3: User Story 1, part A — every ending closes its connection (Priority: P1) 🎯 MVP (research D1, D3, D4)

**Goal**: FR-001 to FR-003 and FR-005 to FR-008 for the classes P, D and A: every Engine-managed ending closes the
transport through `close_async()`, and the unanswered TestRequest sends its Logout with Text and closes. (The ending
Logout's write limit and the R-2 wait are part B, Phase 4.)

**Independent Test**: per ending class, the peer observes EOF within the class's bound with no further inbound frame
and no local API call (SC-001).

### RED cells for US1 part A (write first)

- [X] T018 [P] [US1] `tests/session/test_105_connection_close.cpp`, `ConnectionClose105.PumpSilentEnding_<site>_PeerSeesEofAtOnce`,
  both roles, one cell per class-P silent ending after Active of the Ending-site table (TestReqID mismatch, BeginString
  guard, CompID guard, MsgSeqNum 0, D-3, a too-low frame whose Logout does not fit, a reply leaf whose number assignment
  fails; contract C-1 row 1), and `PumpSilentEnding_NoApplication_PeerSeesEofAtOnce` (`EngineConfig::application` null,
  N-4): mock clock frozen; asserts the peer reads nothing more, then EOF; no event. RED on the base before the
  fix: the transport stays open past the wall cap.
  Done: `ConnectionClose105.PumpSilentEnding_<BeginStringGuard|CompIdGuard|MsgSeqNumZero|D3FaultyLogon|TestReqIdMismatch|ReplyLeafAssignFails|TooLowLogoutDoesNotFit|NoApplication>_PeerSeesEofAtOnce`, both roles; base reading in the evidence file, section T025.
- [X] T019 [US1] Same file, `PeerLogoutInActive_EofAfterReplyAndReset` (C-1 row 3, FR-005), both roles, `reset_on_logout`
  on: the peer reads the reply Logout, then EOF; the store reads (1, 1) before the EOF is observed; `onLogout` once. And
  `TooHighLogoutAcknowledged_EofAfterReset` (C-1 row 5; the too-high bound's route and the pump's at-once route): reply
  Logout, reset unit complete, then EOF. RED on the base before the fix: no EOF. Plus [guard: the reply admitted in
  LogonReceived, the Active test around `inbound_logout_leaf_` in `on_inbound_active_logout_` and the leaf's own state
  test both widened to LogonReceived] `PeerLogoutInLogonReceivedDuringClose_NoReplyLogout_ResetFollows104` (C-1 row 3's
  LogonReceived case, FR-005; spec clarification "Session 2026-11-21 (analyze re-run)"), acceptor role only (LogonReceived
  is the acceptor's state), `reset_on_logout` on, the session's store a `HookedStore` (`tests/support/hooked_store.hpp`):
  the acceptor's Logon arm held at a store operation by a `HookedStore` hold, a graceful `close()` started there and held
  at its flush step by `Hooks::flush_until` (`flushes_begun` asserted 1), the arm released and its supersede return
  leaving the session LogonReceived (asserted before the peer's Logout is delivered), and the peer's Logout delivered
  before the flush is released. The flush is the only suspension between `close()`'s commit of `closing` and its
  phase-1 state test, and `close()` writes its own Logout from LogonReceived once it passes that test, so the cell
  releases the flush only after the peer's Logout has been processed. Oracle: no 35=5 written by fixpp; the numbers reset as 104's reset point resets them (the store reads (1, 1)
  once the unit has run); the connection closed by the running `close()`, one close, and `onLogout` not called:
  `record_state_transition_` calls it only on a transition out of Active (its `was_active` test, on the base as at the
  head), and this session never was Active (text corrected to the source by the T035 ruling; re-derive: the `was_active`
  test in `record_state_transition_`, `git -C $B show HEAD:src/session/session.cpp`). GREEN on the base by design.
  Done: `PeerLogoutInActive_EofAfterReplyAndReset`, `TooHighLogoutAcknowledged_<Bound|AtOnce>_EofAfterReset` (both roles) and the guard `ConnectionClose105Acceptor.PeerLogoutInLogonReceivedDuringClose_NoReplyLogout_ResetFollows104`. The guard asserts `onLogout` 0, as the corrected text above states.
- [X] T020 [US1] Same file, the detached roots and application calls (C-1 rows 8, 9, 10; C-1.c), both roles where the
  root exists: `Liveness<ToAdminThrows|AssignFails|StoreFails>_EofAtOnce` (on a Heartbeat or TestRequest emission, with
  `test_request_threshold = HeartBtInt`, driven by the wire reader), `SendStoreFailure_EofAfterSendReturns`,
  `DirectInitiatorLogonEmitFails_<Open|DriveReconnect>_EofAfterTheCallReturns` and
  `DirectInitiator_EndingThenDriveReconnect_Reconnects` (the transport closed, the Session `Disconnected` and not closed,
  `drive_reconnect` installs a new transport that logs on), and `EngineInitiator_EndingOnConnectLoop_NoHeldTransport_CycleEnds`
  (an Engine-managed initiator on the engine-managed connect loop, ended in Active by a liveness-loop root: the peer sees
  EOF with the clock frozen, the connect loop holds no transport, and its single cycle ends as C-1.c states, the session
  terminal and no second connect attempt; spec Assumptions; the ending is `toAdmin` throwing on the Heartbeat emission, as
  in `LivenessToAdminThrows_EofAtOnce`). Each asserts peer EOF with the clock frozen once the root has
  exited. RED on the base before the fix: no EOF (the reconnect cell in its EOF half; the connect-loop cell's transport held).
  Done: `Liveness<ToAdminThrows|AssignFails|StoreFails>_EofAtOnce`, `SendStoreFailure_EofAfterSendReturns` (both roles), `ConnectionClose105Direct.DirectInitiatorLogonEmitFails_DriveReconnect_EofAfterTheCallReturns`, `ConnectionClose105Direct.DirectInitiator_EndingThenDriveReconnect_Reconnects`, `ConnectionClose105Engine.EngineInitiator_EndingOnConnectLoop_NoHeldTransport_CycleEnds`, each RED on the base (T025). `DirectInitiatorLogonEmitFails_Open_*` dropped by the T035 ruling, with this census note: a direct initiator's `open()` writes its Logon over `transport_send_` with no live transport, so no connection exists to close (class G, no transport at open).
- [X] T021 [US1] Same file, before Active and the acceptor's first frame (C-4, FR-006, FR-007), both roles where the
  refusal exists for the role: `PreActiveRefusal_<D-1|D-2|D-3|NotConnected|LogonSent|IdGuard|SeqNum0>_EofBeforeDeadlineNoTimeoutEvent`
  (a short `logon_timeout_ms`, clock frozen: EOF, no `session_event_establishment_timeout`, no
  `session not established within logon_timeout_ms={}` record), `AcceptorFirstFrameError_EofAtOnce`,
  `AcceptorFirstFrameRefused_EofAtOnceNoTimeoutEvent`, `AcceptorFirstFrameAcceptedLaterFrameRefused_EofAtOnce` (C-4.5), the
  TLS handshake and first-frame deadlines set above the cap and named in each cell. RED on the base before the fix:
  closed only at the deadline, with the event (the first frame's result discarded). Plus
  [guard: `expire()` closes without its event] `SilentPeer_StillClosedAtEstablishmentDeadlineWithEvent` (C-4.3, B-093-4).
  Done (the cells below are written and read): `PreActive105.PreActiveRefusal_<D1D2_FaultyLogon|NotConnectedOrLogonSent_NotALogon|SeqNum0>_EofBeforeDeadlineNoTimeoutEvent` (both roles; D-1 is the acceptor's, D-2 the initiator's), `PreActive105Initiator.PreActiveRefusal_IdGuard_*`, `AcceptorFirstFrame105.<AcceptorFirstFrameError_EofAtOnce|AcceptorFirstFrameRefused_EofAtOnceNoTimeoutEvent|AcceptorFirstFrameAcceptedLaterFrameRefused_EofAtOnce>`, and the guard `PreActive105.SilentPeer_StillClosedAtEstablishmentDeadlineWithEvent` (both roles). `PreActiveRefusal_D-3` recorded unreachable as an FR-007 refusal (T035 ruling): D-3's one pre-Active state, LogonReceived, is written only by the acceptor's Logon arm, which leaves it before it returns, for Active or Disconnected or through a supersede return (`logon_arm_superseded_`: `close()` committed, or the Engine's stop); so a frame meets LogonReceived only after a supersede return, where the running `close()` or `Engine::stop()` owns the connection's close, on an Engine or a direct Session alike (a frame fed while the arm is held overlaps `on_inbound_frame` calls, outside the Session contract). Re-check: `grep -n 'record_state_transition_(fsm_state::LogonReceived)' src/session/session.cpp`, then every return of that arm after the write.
- [X] T022 [US1] Same file, one close per ending (C-1.a, class X): `OnLogoutCallsClose_OneClose` and
  `EngineStopRacesEndingClose_OneClose` (both on the TSan lane in T137; each asserts one EOF, no use after close, and the
  close at once), RED on the base before the fix in their EOF-timing half; [guard: the latch's `lifecycle` term
  deleted] `InsideClose_NoSecondClose`; [guard: the drain flag deleted] `InsideReconnectDrain_DrainKeepsItsBound`
  (B-103-9's bound).
  Done: `OnLogoutCallsClose_OneClose` and `EngineStopRacesEndingClose_OneClose` (both roles), GREEN on the base (the `close()` and `Engine::stop()` they start close the transport themselves): regression cells of C-1.a. They are not [guard] cells: every dedupe mutant tried at the head survives them (the owe's lifecycle term, the release's lifecycle term, the release's generation check, `close()`'s clear of the latch at its commit of closing; evidence, section "T035 — the ruling's adjudication"), because each closes through a terminal close, which no second close changes, and the pump takes the latch before any closer commits (the [guard] reading the ruling asked for is escalated). The [guard: the latch's `lifecycle` term deleted] is `InsideClose_EndingDuringItsLogoutStore_NoSecondClose` (both roles): a class P ending while a graceful `close()` is in its Logout's store call; GREEN on the base, RED under the mutant (the stray `close(terminal)` escalates the graceful close, OD-29, and cuts its phase 1 in the held store call: no Logout written). `InsideClose_NoSecondClose` stays as its control (no ending inside the close). [guard: the drain flag deleted] `ConnectionClose105Direct.InsideReconnectDrain_DrainKeepsItsBound`.
- [X] T023 [P] [US1] `tests/session/test_105_liveness_timing.cpp`, R-1 (C-3), both roles, `test_request_threshold =
  HeartBtInt`, driven past the give-up by the wire reader: `GiveUp_LogoutTextPinned_ThenEof` (exactly one 35=5 whose 58 is
  `TestRequest unanswered, TestReqID <id>` spelled out in the cell, `<id>` the 112 of the TestRequest read, then EOF with
  the clock frozen and no Logout from the peer); `GiveUp_OneLogRecordWithSlot74` (capturing sink: one warn record of C-3.3's
  format with `error` the value of `error::session_test_request_unanswered`, written before the Logout's bytes reach the
  peer; no call returns slot 74, L-105-c); `GiveUp_<LogoutDoesNotFit|ToAdminThrows|AssignFails|StoreFailsPersistent|StoreFailsVolatile|WriteFails>_StillEof`
  (C-3.2: each asserts the record, the wire — no complete Logout, except `StoreFailsVolatile`, whose Logout is on the wire
  — and EOF). RED on the base before the fix: no Logout, no record, no EOF.
  Done: `LivenessTiming105.GiveUp_<LogoutTextPinned_ThenEof|OneLogRecordWithSlot74|ToAdminThrows|AssignFails|StoreFailsPersistent|StoreFailsVolatile|LogoutDoesNotFit>_*` (both roles) and `LivenessTiming105Direct.GiveUp_WriteFails_StillEof` (a direct initiator on a mock transport that cuts the Logout's write).
- [X] T024 [P] [US1] `tests/session/test_105_tls_close.cpp`, the real-TLS orderly close (C-8.1, FR-002). On
  `engine_loopback_harness.hpp`, acceptor role (an Engine session, so the pump consumes the latch; T003):
  `TlsClose105.<PumpSilentEnding|GiveUpWhilePumpParkedInRead|PreActiveSilentRefusal|PeerLogoutReply>_PeerReceivesCloseNotify`
  (the give-up cell is the B-348-1 case: a detached-root ending while the pump is parked in a TLS read). On
  `loopback_tls_session_harness.hpp` (a direct Session, no pump), only classes that close at a root's exit:
  `TlsClose105.DirectSession<GiveUp|SendStoreFailure>_PeerReceivesCloseNotify`. Each: the peer reads the frames the class
  writes, then a close_notify, then EOF; the TLS handshake timeout and `tls_close_timeout` above the cap. RED on the base before the fix: no close_notify and no EOF.
  Done: the four Engine cells, and `TlsClose105.DirectSession<GiveUp|SendStoreFailure>_PeerReceivesCloseNotify` on a resend_gap::Scene Session over the TLS factory and fixture certificates `loopback_tls_session_harness.hpp` builds, the server end `TlsPeer::accept`, with a caller's reader on the installed transport (without one the TLS records the server end sends after the handshake stay unread and the close resets the socket: the peer read `transport_read_error`). RED on the base by assertion (no Logout at the give-up, no close). The earlier "`drive_reconnect()` does not return" re-run at 10 × (50 s) on `$B` and at the head: with the server end's handshake running it returns at once on both; with no server-end handshake it does not return within 50 s on both, so pre-existing, not 105's: a client TLS handshake against a peer that accepts and never handshakes is not bounded (plan, "Follow-ups to file before Gate B").
- [X] T025 [US1] Commit the cells (`C1`); read T018 to T024 on `$B` (Execution rules "RED on the base"); record per cell its
  RED reason or guard status, and the test count.
  Done: `C1` = `62b290a4`; evidence file, section T025: 72 tests, 62 RED by an assertion for the stated reason, 10 GREEN (the guards, and the two T022 regression cells), 0 skipped. Re-read with the T035 ruling's cells (`1494eef8`): 76 tests, 64 RED (the two `TlsClose105.DirectSession*` cells added), 12 GREEN (`InsideClose_EndingDuringItsLogoutStore_NoSecondClose` added, both roles), 0 skipped (evidence, section "T025 re-read with the T035 ruling's cells").

### Implementation for US1 part A

- [X] T026 [US1] The latch (research D1 item 1, data-model E-2) in `include/fixpp/session/session.hpp` and
  `src/session/session.cpp`: `connection_end_owed_` (the `conn_gen_` owed), `reconnect_drain_in_flight_` (held only around
  `drain_for_reconnect_`'s call in `drive_reconnect`), and the one setter `owe_connection_end_() noexcept`
  (`state_ == lifecycle::open && !reconnect_drain_in_flight_`; T065 adds the "no wait armed before Active" term). Its
  first caller: `record_state_transition_`, on a transition **into** `Disconnected` from another state only (the prior
  state captured at entry), in the `new_state == fsm_state::Disconnected` block beside `wake_peer_logout_bound_()`,
  **above** the `engine_.application == nullptr` return; two stores, no suspension, no spawn (F-1).
  Done: `owe_connection_end_`, `connection_end_owed_`, `reconnect_drain_in_flight_`; the edge's call in `record_state_transition_` on a transition into Disconnected from another state, above the no-application return.
- [X] T027 [US1] The close frame (research D1 items 4, 5; data-model E-2) in `src/session/session.cpp` and `session.hpp`:
  `connection_close_counter_`, `connection_close_join` (`peer_logout_bound_join`'s shape: +1 at construction, −1 at
  destruction), the static coroutine `close_transport_(join, std::shared_ptr<Transport>)` (awaits `close_async()` on the
  transport it holds, touches no `Session` member), and `release_connection_end_() noexcept` (acts only when the latch is
  set, its generation is `conn_gen_`, `state_ == lifecycle::open` and `inbound_depth_ == 0`; takes
  `live_transport_shared_()`; spawns on `exec_.underlying()` inside a `try`; clears the latch after `co_spawn` returns;
  its `catch (...)` calls the synchronous `Transport::close()` of the transport it took, clears the latch and does not
  rethrow — ruling D-g). Add the test-only allocation-failure seam at this spawn (the seam family research D10 names).
  Call `release_connection_end_()` at the exit of every class D and A root of research D1 item 7, on every exit, a
  refused call included (contract C-1.c): `run_liveness_loop`,
  `run_peer_logout_bound_`, `send_body_` / `send_deferred_` (`Session::send`, `Engine::send`), `drive_reconnect`, and
  `Session::open` where it emits a direct initiator's Logon. Do not copy the liveness loop's `fetch_add`-then-bare-`co_spawn`
  shape.
  Done: `release_connection_end_`, `connection_close_join`, `close_transport_`, the seam `close_frame_spawn_fails_` (set through `session_test_access::set_close_frame_spawn_fails`). The release runs at the exits of `run_liveness_loop` and `run_peer_logout_bound_` (each now awaits a `*_body_` coroutine and releases after it, so a frame destroyed unresumed touches no Session member), of `send_body_`, `send_deferred_` and `drive_reconnect` (the RAII `connection_end_releaser`), and in `open()` after a direct initiator's Logon emission.
- [X] T028 [US1] Joins (research D1 item 6): `close()` clears the latch at its commit of `closing` and joins
  `connection_close_counter_` by post before its own `close_async()`; `drain_for_reconnect_` clears the latch and joins the
  counter before its step 3 quiescence test; `~Session` asserts the counter is zero. (The second join after the ending
  bound's is T069.)
  Done: `close()` clears the latch at its commit of `closing` and joins the close frame by post before its `close_async()`; `drain_for_reconnect_` clears and joins at its entry, before step 2; `~Session` asserts the counter is 0.
- [X] T029 [US1] Engine side (research D1 items 2, 3) in `src/session/engine.cpp` and `src/session/session_engine_access.hpp`:
  `session_engine_access::take_connection_end(Session&) noexcept -> bool` and `ended(Session const&) noexcept -> bool`;
  `run_read_pump` runs `if (!r || session_engine_access::take_connection_end(session)) co_await stop_pump();` after every
  `on_inbound_frame` return; `run_read_pump` gains `std::span<const std::byte> first_frame`, delivered before
  `initial_bytes` without the `expired()` pre-check; `run_accept_loop`'s step 8 (the discarded `deliver_r`) is deleted;
  `run_connect_loop` passes an empty span.
  Done: `take_connection_end`, `ended`; `run_read_pump`'s `first_frame` and its latch test after every `on_inbound_frame`; step 8 deleted; `run_connect_loop` passes an empty span.
- [X] T030 [US1] D4 in `src/session/engine.cpp`: the pump's `expire()` calls `note_establishment_timeout` only when
  `!session_engine_access::ended(session)`, then `stop_pump()` either way (data-model E-5). Its discriminating cell is
  `PreActive_WaitArmedAtEstablishmentDeadline_ClosedNoEvent` (T049), its mutant T076.
  Done: `expire()` writes the timeout's event and record only when `!ended(session)`.
- [X] T031 [US1] D3 (R-1) in `src/session/session.cpp`, `src/log/format_registry.cpp` and `session.hpp`: at the give-up,
  write the slot-74 warn record `session ended: TestRequest unanswered: test_req_id={} threshold_ms={} error={}` (format
  registered) **before** the Logout's write; take the replay-gate shared hold; `claim_peer_logout_none_`; write the Logout
  through `inbound_logout_leaf_` with Text `TestRequest unanswered, TestReqID <id>` built in a bounded stack buffer with a
  `static_assert` on its size; then `record_disconnected_unless_already_()`; the loop's exit runs `release_connection_end_()`.
  Delete `unanswered_tr_`, its writes (`answer_pending_test_request_`, the in-sequence Heartbeat arm of `on_inbound_active_`,
  the loop) and its declaration comment.
  Done (part A): the record, the replay-gate wait and shared hold, `claim_peer_logout_none_`, the Logout with Text through `inbound_logout_leaf_`, then `record_disconnected_unless_already_`; `unanswered_tr_` deleted. The Logout owner is tested and set before any suspension by both the leaf and `run_logout_phase1`, so a `close()` started during the gate wait cannot add a second 35=5. Its bounded half is moved to Phase 4 as T059a (T035 ruling 2; spec clarification "Session 2026-11-21 (implementation, Phase 3)"): until it lands the give-up waits for an exclusive hold with no bound, which `ReplayGate588.R588f4_*` reads RED (T072's list).
- [X] T032 [US1] Comments naming the new rule (issue #310, FR-018), part A: `record_state_transition_` ("105 D1: the edge
  sets the connection-end latch; its consumers …"), `run_read_pump`'s disposition list, `dispose_unparseable_` (092's "D-1
  and D-2 are a state transition, not a close" and its D-3 twin rewritten to name 105 FR-007), the "(Phase 6 US4 wires the
  Logout emission before Disconnected.)" comment deleted, `pending_test_req_id_`'s comment, and the
  `session_test_request_unanswered` comment in `include/fixpp/core/error.hpp` rewritten without arithmetic ("inbound
  silence of 2 × the TestRequest threshold with no answering Heartbeat; the session sends a Logout with Text and closes").
  Done: the comments named; `run_liveness_loop`'s cadence comment names R-1.
- [X] T033 [US1] Re-pin the class-P/D/A flips (FR-019), renaming where the name states the old rule:
  `InboundFrameDispositionsQ10.WithinCapMismatchInActive_DisconnectsWithoutALogout` (its open-transport half) and the
  #534-commented cells of `tests/session/inbound_frame_dispositions_test.cpp`; `SecondFrameInFlight.*` and
  `ReconcileAfterTheTakeBack.*` (#534-commented) in `tests/session/test_cancelled_outbound_store_engine.cpp`;
  `EstablishmentTimeoutQ16.AcceptorAfterRefusedLogon_*` in `tests/session/engine_establishment_timeout_test.cpp`;
  `ResendAnswerStoreFailureEngine.Acceptor789Path_SlotReadFails_ConnectionStaysOpenUntilTheEstablishmentDeadline` in
  `tests/session/test_538_resend_answer_store_failure_engine.cpp`; `Capi537Witness.Deadline_CompIdsLeaveNoRoomForTheLogout_EndsSilently`
  ("ends silently and closes at once") in `tests/capi/recovery_liveness_capi_witness_test.cpp`. Run each entry.
  Done: `WithinCapMismatchInActive_DisconnectsWithoutALogout` flipped; `EstablishmentTimeoutQ16.AcceptorAfterRefusedLogon_<GarbageOnlyPeer|SilentPeer>_ClosedAtOnceNoTimeout` (renamed); `ResendAnswerStoreFailureEngine.Acceptor789Path_SlotReadFails_ConnectionClosesAtOnce` (renamed); `Capi537Witness.Deadline_CompIdsLeaveNoRoomForTheLogout_EndsSilentlyAndClosesAtOnce` (renamed); the #534 comments of `SecondFrameInFlight.*` and `ReconcileAfterTheTakeBack.*` rewritten (those cells keep their readings at H1). And, by T009's classing of `engine_establishment_timeout_test.cpp` under T033, the same FR-007 rule's other cells there: `EstablishmentTimeoutQ16.AcceptorAfterNonLogonFrame_<GarbageOnlyPeer|SilentPeer>_ClosedAtOnceNoTimeout`, `ReadableAcrossT_ValidNonLogonFrames_ClosedAtTheFirstRefusal` and `EstablishmentTimeoutNearClockMax.AcceptorAfterNonLogonFrame_RefusalClosesAtOnce` (renamed). Each entry run: GREEN.
- [X] T034 [US1] [seam] `ConnectionClose105.CloseFrameSpawnFails_ReleaseAtLoopExit_PeerSeesEofWithNoLaterCall` and its TLS
  arm on the Engine-over-TLS rig, both in `tests/session/test_105_seams.cpp` (an Engine session ended by the liveness loop
  while its pump is parked in a read; the seam of T027 fails the spawn): the peer sees EOF with no inbound frame and no API
  call, the counter is 0 after `close()`; on TLS no close_notify (L-105-h). Not readable on `$B` (the seam is new); RED under
  T041's mutant.
  Done: `ConnectionClose105.CloseFrameSpawnFails_ReleaseAtLoopExit_PeerSeesEofWithNoLaterCall` and `TlsClose105.CloseFrameSpawnFails_ReleaseAtLoopExit_AbortiveNoCloseNotify` in `test_105_seams.cpp`; RED reading under T041's mutant in the evidence file.
- [X] T035 [US1] Run T018 to T024 and T034 GREEN, then `-L 105`, `-L session` and `-L capi` on linux-clang-debug. **R-2-class
  pins are expected RED here** (`GapDeadlineOutOfSequenceSite.FrameAtTheBoundDrawsTheLogoutAndTheTransportStaysOpen` and the
  537 note's control cell "D-537-6, N"): they flip at T072; do not re-pin them to part A's behaviour. Commit `H1`;
  `codegraph sync`.
  Done: `-L 105` GREEN, `H1` committed. The pins of a superseded rule outside T033's list, adjudicated by the T035 ruling
  (evidence, section "T035 — the ruling's adjudication"): each read GREEN ×3 on `$B` (none pre-existing) and RED ×3 at the
  head; re-pinned under FR-019, each naming the 105 rule that predicts it: `InboundFrameDispositionsQ6.Disconnected_*` and
  `InboundFrameDispositionsQ8Pump.Disconnected_*` (FR-007, renamed), `CapiC7Witness.Row3_*Disconnected*` and
  `Row5_*Refused*` (FR-007, renamed; T128's entry), `ReconnectFence103.G10_*/InitiatorDisconnected` (FR-005; the arm's
  connection closes at the peer's Logout, so B-103-9's order refuses with `session_already_closed`),
  `ResetOnLogout104.LivenessHeartbeatAcrossBoundUnit_OneEnding_GateWait_RolOff` (FR-005: the close after the reply; the
  Heartbeat's store call runs to its end, FR-008), `TooHighHeartbeatDuringTheGap.Control_HeartbeatBelowNextNumIn_*`
  (R-1's Logout; its timing re-pins at T099, T011's list) and `CancelledOutboundStoreMeasurement.Returned_StoreCancelledAtItsLock`
  (FR-001 and data-model E-2 I-5: the store outcomes equal the base's, measured; the held send's write finds the connection
  closed and records Disconnected over Disconnected). None is a regression (each store outcome and the one close per
  ending hold). `ReplayGate588.R588f4_*` stays RED for T059a (ruling 2). `-L "session|capi"` then reads RED on T072's
  list and `R588f4` only. The census misses are found by the widened D11 pattern (T009's addendum).

### Mutant and spurious-hit arms for US1 part A (in `$M` at `H1`)

- [X] T036 [US1] The pump's latch test deleted: T018, T019 (pump route) and T021 RED (the clock is frozen, so no competing
  close can supply the EOF).
- [X] T037 [US1] `release_connection_end_` made a no-op: T019 (bound route), T020 and T023 RED.
- [X] T038 [US1] The edge's owe moved below the no-application return: `PumpSilentEnding_NoApplication_*` RED. Then the
  setter census `grep -n 'owe_connection_end_(' src/session/session.cpp` (known positive: the edge) and, per call site at
  `H1`, that call deleted: its cells RED.
- [X] T039 [US1] `close_transport_` calling the synchronous `Transport::close()` instead of `close_async()`: T024's
  close_notify assertions RED (C-8.2). The R-1 Logout write deleted (the close kept): `GiveUp_LogoutTextPinned_ThenEof` RED.
  Done (the first half): `GiveUpWhilePumpParkedInRead_PeerReceivesCloseNotify` RED, and at the T035 head the two
  `TlsClose105.DirectSession*` cells RED too (abortive endings: the caller's reader has a read in flight); the pump-route
  TLS cells close through `close(terminal)` and survive, as T036's reading shows. The R-1 half is moved to Phase 4 as T077a
  (T035 ruling 2).
- [X] T040 [US1] Guards: the latch's `lifecycle` term deleted (`InsideClose_NoSecondClose` RED); the drain flag deleted
  (`InsideReconnectDrain_DrainKeepsItsBound` RED); `expire()` closing without its event (`SilentPeer_StillClosedAtEstablishmentDeadlineWithEvent` RED); the reply admitted in LogonReceived (the Active test
  around `inbound_logout_leaf_` in `on_inbound_active_logout_` and the leaf's own state test both widened to LogonReceived;
  `PeerLogoutInLogonReceivedDuringClose_NoReplyLogout_ResetFollows104` RED).
  Done: the drain flag, `expire()` and LogonReceived arms RED. The `lifecycle` term (T035 ruling 3): its mutant survives
  `InsideClose_NoSecondClose` (the pump's second `close()` there joins a graceful close whose phase 1 has already ended)
  and turns `InsideClose_EndingDuringItsLogoutStore_NoSecondClose` RED, both roles, by an assertion (no Logout written:
  the stray `close(terminal)` escalates the graceful close, OD-29, and its phase 1 is cancelled in the held store call),
  rc 1, no abort. The guard is that cell (T022). Recorded gaps (T035 ruling 4, measured): three more dedupe terms each
  survive the whole `-L 105` suite at the head, both binaries unfiltered: the release's `lifecycle` term, the release's
  generation check (data-model E-2 I-7) and `close()`'s clear of the latch at its commit of closing. No-observable
  dispositions in part A, the terms kept as written: the owe's `lifecycle` term and `close()`'s clear mask the release's
  term and each other; the drain's clear at its entry, the drain flag and the drain's wait for the old connection's
  emitters mask the generation check, so I-7 has no witness in part A. Phase 4 adds the generation's other readers (the
  bound's spawns and live predicate, T061 and T062), where a witness for I-7 is owed or the gap is restated.
- [X] T041 [US1] The close frame's fail-closed `catch` replaced by round 2's (the latch left set for a later consumer):
  `CloseFrameSpawnFails_ReleaseAtLoopExit_*` RED within its wall cap.

**Checkpoint**: every class P, D and A ending closes; R-2-class endings still close at once, pending part B.

---
## Phase 4: User Story 1, part B — the ending Logout: write limit, R-2 wait, bound and latch (Priority: P1) (research D2; contract C-2; data-model E-3)

**Goal**: FR-004, FR-004a, FR-004b, FR-005's limit and FR-008: every ending Logout's write is limited by
`logout_disconnect_timeout_ms` from its start; after an R-2 Logout the session closes at the peer's Logout or at the same
budget's end; the dispatch overlay W-1 to W-15 holds; the bound fails closed.

**Independent Test**: EOF follows the peer's Logout, or comes at exactly the bound and not before; a Logout whose write
does not complete closes the connection at the bound.

### RED cells for US1 part B (write first)

- [X] T042 [P] [US1] `tests/session/test_105_logout_wait.cpp`, W-1 after Active (C-2.2, clarification D-a), both roles:
  `LogoutWait105.TooLow_PeerLogoutBelowNextNumIn_EndsWaitAndResets`, `GapDeadline_PeerLogoutAboveNextNumIn_EndsWaitAndResets`,
  `SendingTimeReject_PeerLogoutInSequence_EndsWait`, `OrigSendingTimeReject_PeerLogoutEndsWait` — the peer's Logout at the
  number a real peer uses; positive control: the connection open and `fixpp_session_is_established` true before it; then
  EOF after it, the receipt reset under `reset_on_logout` (store read before EOF), NextNumIn advanced only for the
  in-sequence one, the count of `fromAdmin` calls for the peer's Logout asserted in each cell (one at NextNumIn, none
  off it; W-1), `onLogout` once. And `LogoutWait105.PeerLogoutPossDupY_<AtNextNumIn|BelowNextNumIn>_EndsWait` (C-2.9,
  research D2 item 3: the response test precedes the PossDup stage-1 arm), on the SendingTime-Reject route (at NextNumIn)
  and the too-low route (below it), the peer's Logout carrying PossDupFlag(43)=Y: no Reject written, the wait ended, EOF
  after it, NextNumIn advanced and `fromAdmin` called once at NextNumIn, neither below it. RED on the base before the fix:
  the session is `Disconnected` at the Logout's write and the transport stays open.
- [X] T043 [US1] Same file, the bound (W-12, C-2.5, C-1.d): `Bound_EofAtExactlyTheBound` (open at the bound − 1 ms, EOF at the
  bound, `onLogout` then), `Bound_DoesNotSlideWithAnAnswerSent` (a Heartbeat answering the peer's TestRequest mid-wait),
  `OnLogoutFiresAtWaitEnd`; the liveness instants and the gap deadline set past the bound and named in each cell. RED on the
  base before the fix: no EOF at the bound.
- [X] T044 [US1] Same file, W-2 to W-7 on the SendingTime-Reject route (C-2.2), both roles:
  `AppMessageDuringWait_ReachesFromAppAndAdvances`, `TestRequestDuringWait_Answered`, `ResendRequestDuringWait_Answered`,
  `HeartbeatDuringWait_AnswersPendingTestRequest`, `TooHighDuringWait_NotDeliveredResendRequestSent`,
  `GapDeadlinePassedDuringWait_NoSecondLogout` — each asserts the wire reply (or its absence) and `fromApp` delivery, with the
  session shown established at the frame. RED on the base before the fix: the frames reach a `Disconnected` session
  and are drained.
- [X] T045 [US1] Same file, W-8 absorption: `Absorbed_<TooLow|SecondSendingTimeReject|OrigSendingTimeReject|TestReqIdMismatchInSequence|TestReqIdMismatchKnobOffOutOfSequence|TestReqIdMismatchTooHighAnswerPending|CompIdMismatch|MsgSeqNumZero>_ThenAppMessageAndPeerLogout`
  (one cell per route of the TestReqID mismatch, contract C-2 W-8; NextNumIn asserted after each absorbed frame, by W-8's
  rule for its cause and route; no second Logout on the wire, the application message reaches `fromApp`, the peer's Logout resets under
  `reset_on_logout`, `onLogout` counted once at the wait's end, then EOF) and `TooLowRoute_PeerFramesBelowNextNumIn_Absorbed`.
  RED on the base before the fix.
- [X] T046 [US1] Same file, W-9, one cell per disposition, each asserting EOF with the clock frozen, `onLogout` once and no
  second Logout: `StoreFailureDuringWait_EofAtOnce` (error, `Disconnected` written), `PersistFailureDuringWait_EofAtOnce`
  (error, none written), `TooHighDuringWait_ResendRequestAssignFails_EofAtOnce` (success, `Disconnected` written),
  `SendHeldInStoreAcrossTheEnding_StoreFails_EofAtOnce` (a non-pump root), and `DirectSession_StoreFailureDuringWait_ClosesAtNextSend`
  (L-105-b's form). RED on the base before the fix.
- [X] T047 [US1] Same file, W-10 and W-11 (C-2.3, C-2.4): `SendDuringWait_Refused` (`session_invalid_state_for_send`, with
  `fixpp_session_is_established` true at the same instant as positive control), `SendHeldInStoreAcrossTheEnding_NumberedBeforeTheLogout`
  (the send's number below the Logout's; the wire order not asserted, L-105-e),
  `LivenessDuringWait_ArmedOneTickBeforeHeartbeat_SendsNothing` and `LivenessDuringWait_ArmedOneTickBeforeGiveUp_SendsNothing`
  (`test_request_threshold = HeartBtInt`; positive control `fsm_state() == Active` at the instant tested). RED on the base before the fix: the session is `Disconnected`.
- [X] T048 [US1] Same file, no wait and the closers (C-2 "No wait is armed", W-13, W-14, C-2.8):
  `TimeoutZero_NoWaitEofAfterWrite`, `NoClock_NoWait_ClosesAtNextCall` (a direct Session, the only form with no clock, since `Engine::open` refuses a null
  `EngineConfig::clock`: no wait armed, so the next `send()` or `close()` closes, L-105-b's oracle as in
  `DirectSession_StoreFailureDuringWait_ClosesAtNextSend`), `<CloseTerminal|EngineStop|DriveReconnectDrain>DuringWait_OneClose`,
  `LogoutNotTransmitted_ClosesAtOnce` (the long-CompIDs Logout), `PendingTooHighMark_GapDeadlineLogout_EofAfterWrite` (a
  too-high Logout while a resend is outstanding, then a non-advancing frame after the gap deadline: the deadline's Logout,
  then EOF with the clock frozen). RED on the base before the fix where the base keeps the transport open; a cell
  the base already meets is marked [guard] with its mutant from T076 and recorded.
- [X] T049 [US1] Same file, before Active (W-1 to W-15's before-Active column, C-2.6, E-3 rows 6, 9, 11), both roles where
  the refusal exists: `PreActive_BoundSurvivesTheRefusalsOwnEdge`, `PreActive_FramesDrained`,
  `PreActiveTooLowLogon_PeerLogoutEndsWaitNoReset` (OQ-1), `PreActive_BoundExpiryCloses`,
  `PreActive_WaitArmedAtEstablishmentDeadline_ClosedNoEvent` (Engine; no arm in flight, so this is E-5's cell, not the
  cap's), `PreActive_DirectSession_BoundAloneEndsWait`, `PreActive_CloseDuringWait_OneClose`, one cell per Logon refusal with a
  Logout (the too-low Logon of B-104-6, the TestMessageIndicator posture mismatch, the strict-reset refusal, LogonSent's
  Logout-with-Text), and `PreActive789_<Invalid|TooHigh>_WaitThenEof` for the two NextExpectedMsgSeqNum(789) Logouts of
  `honor_peer_next_expected_` (both roles: LogonReceived for the acceptor, LogonSent for the initiator; ruling R-789); and the establishment cap (E-3, "The establishment cap"; research D10):
  `PreActive_<LogoutBlockedInWrite|LogoutHeldInStore>_ClosedAtEstablishmentDeadlineNoEvent` (establishment time left below
  `logout_disconnect_timeout_ms`; open at the deadline − 1 ms, EOF at it, no event and no record) and
  `PreActive_ClockOverride_CapOnEngineClock` (a second mock clock as `clock_override`; only the engine clock reaches the
  deadline). RED on the base before the fix: closed at the deadline with the event, or never.
- [X] T050 [US1] Same file, the bound's expiry under a blocked arm (rows 10, 11; N-1):
  `Bound_ExpiresWhileResendAnswerBlocked_EofAtTheBound` — SendingTime-Reject route, the peer's ResendRequest during the wait,
  its answer blocked in `async_write` behind a peer that stopped reading; open at the bound − 1 ms, EOF at the bound. RED on the
  base before the fix.
- [X] T051 [P] [US1] `tests/session/test_105_write_limit.cpp` (C-2.7, FR-004b), both roles, on the held-write transport of
  T005, `logon_timeout_ms` well above the bound. Each `LogoutBlocked` and `LogoutQueuedBehindBlockedSend` cell asserts: open at the
  bound − 1 ms, no complete Logout at the peer, EOF at the bound, `onLogout` once, no wait after it (the peer's Logout sent
  after the bound changes nothing); the other cells state their own oracle:
  `WriteLimit105.<R1|R2AfterActive|R2BeforeActive|R2BeforeActive789|Reply>_LogoutBlocked_ClosedAtTheBound` (for R1 the slot-74 record
  present; `R2BeforeActive789` is a NextExpectedMsgSeqNum(789) Logout of `honor_peer_next_expected_`, ruling R-789); `WriteLimit105.<R1|R2AfterActive|Reply>_LogoutQueuedBehindBlockedSend_ClosedAtTheBound` (for `Reply` an
  application send blocked in `async_write` holding `write_gate_`, then the peer's Logout: the receipt arm resumes,
  `onLogout` once, the reset unit run as 104 orders it; no pre-Active queued cell, unreachable on an Engine session);
  `WriteReleasedBeforeTheBound_LogoutOnWire_WaitKeepsTheSameBudget` (control: released at the bound − 1 ms, the Logout complete
  on the wire, EOF at the same bound); `R1WriteHeld_PeerLogoutArrives_LogoutCompleteThenEof` (the latch deferred to the
  writer); `R1WriteHeld_LimitFiresInsideStoreCall_EofAtBoundOnLogoutAfterStore` (EOF at the bound, `onLogout` when the store
  returns); and [guard: a 0 ms limit read as "close at once"] `TimeoutZero_WriteUnbounded`. R1 cells set
  `test_request_threshold = HeartBtInt`. RED on the base before the fix: the held write never returns and nothing
  closes.
- [X] T052 [US1] `tests/session/test_105_seams.cpp` (it reads `cut` through test access), [guard: "Leaving `writing`" ignores `cut`]
  `WriteLimit105.DirectSession_WriteCompletesAfterTheBoundFires_NoWaitArmed` on T005's deferred-completion transport, a
  direct Session, an R-2 Logout. Schedule, pinned in the cell: advance the mock clock to the bound; `run_one` until the bound
  has taken E-3 row 4 (its write-limit close); release the completion; drain by the bounded drive. Positive control: the
  peer holds the whole Logout. Oracle: EOF with no further advance, `Disconnected` and `onLogout` once, the session not left
  Active. The reach proof (`cut` set when the leaf returned, or the close frame's counter at 1, through `session_test_access`)
  is asserted before the oracle. GREEN on the base by design (no wait exists there), so it is written with T060 and read only
  under T077's mutant.
- [X] T053 [US1] `test_105_logout_wait.cpp`, the bound's absorbed exception (E-3 row 16):
  `LogoutWait105.EndingBound_DeadlineArmAbsorbsException_<Writing|Armed>_EofAtOnce`, T005's counting decorator as
  `clock_override`, throwing `std::bad_alloc` from `sleep_until` for the bound's instant only. `Writing`: the R-2 Logout's
  write held, the throw armed before the write. `Armed` (after Active, and before Active): the throw armed by the test only
  after the peer has read the whole Logout (the leaf has returned), so the first race, built in `writing`, never fires it.
  Each: EOF at once with the clock frozen, `onLogout` once (after Active), driven by the bounded drive so its RED arm
  terminates. RED on the base before the fix: no EOF. Its reach-proof twins (the phase the exit found) go in
  `test_105_seams.cpp` with T061.
- [X] T054 [P] [US1] `tests/session/test_105_tls_close.cpp`, TLS arms of part B (C-8.4, C-8.5):
  `TlsClose105.CloseGracefulDuringWait_CloseNotifyAtTheBound` (`logout_disconnect_timeout_ms` set well above the wall cap,
  the mock clock advanced to the bound: close_notify, then EOF, at the bound's mock instant) and
  `TlsWriteLimit105.R2AfterActive_PeerStopsReading_AbortiveAtBoundPlusTlsCloseTimeout` (no close_notify; the close within the
  bound plus `tls_close_timeout`; L-105-g). RED on the base before the fix.
- [X] T055 [US1] `test_105_logout_wait.cpp`, lifetime (ASan lane; B-103-13): `Lifetime105.CloseFrame_SpawnedByWriteLimitDuringGracefulClose`
  (the mock clock advanced to the bound well inside `close_grace`'s wall time: EOF at the bound, the Session destroyed after
  `close()` returns), RED on the base before the fix in its EOF-at-the-bound half; [guard: the bound's join deleted, and
  the close frame's first join deleted] `Lifetime105.<EndingLogoutBound|CloseFrame>_SessionDestroyedAfterClose`.
- [X] T056 [US1] Commit the cells (`C2`); read T042 to T055 on `$B` (T052 excepted: a seam-file guard, written with T060);
  record per cell.

### Implementation for US1 part B

- [X] T057 [US1] `store_then_emit`'s private transmitted out-parameter (research D2 item 1): set only when
  `live_write_serialized_` returned `written` with a live transport present, or the direct sink returned success; the Logout
  leaves pass it up.
- [X] T058 [US1] The ending state (data-model E-3 fields) in `session.hpp`: `ending_wait_` (`phase` `{none, writing, armed,
  resolved}`, `kind` `{r1, r2, reply}`, `entered_from`, `from_active`, `cut`, `deadline`), `establishment_cap_` (on
  `engine_.clock`), `ending_logout_wake_` (`peer_logout_wake_`'s shape), `ending_logout_bound_counter_` and
  `ending_logout_bound_join`. `session_engine_access::set_establishment_deadline(Session&, steady_time_point) noexcept` (one
  store), called by `run_read_pump` before its first delivery when it holds `establish_deadline` (E-5).
- [X] T059 [US1] **Entering `writing`** at each write point (research D2 item 11, T007's census): `inbound_logout_leaf_` at its
  owner claim (after Active; kinds `r1` from the liveness loop, `r2` from `send_ending_logout_`, `reply` from
  `on_inbound_active_logout_` and `acknowledge_peer_logout_`), and before Active the build-and-emit of
  `refuse_logon_with_logout_` and `inbound_logon_sent_logout_leaf_`, and `honor_peer_next_expected_`'s two (ruling R-789;
  research D2 item 11's `build_logout(` census). Only with a clock and `logout_disconnect_timeout_ms > 0`:
  `phase = writing`, `cut = false`, `deadline = deadline_after(now, logout_disconnect_timeout_ms)` on `effective_clock_`, read
  once at the owner claim (before the number assignment, the store call and the gate acquire); spawn
  `run_ending_logout_bound_(join, gen)` **before the write** on `co_await asio::this_coro::executor` (never
  `Session::executor()`), join as the parameter; the `catch (...)` around `co_spawn` sets `phase = none`, does not rethrow,
  and the Logout is not written (row 2a: the leaf's "frame that cannot be built" path; the site records `Disconnected`).
  Add the bound-spawn allocation-failure seam. Timeout 0 or no clock enters nothing (row 2).
- [X] T059a [US1] (moved from T031, its bounded half; T035 ruling 2; spec clarification "Session 2026-11-21
  (implementation, Phase 3)") R-1's budget counted from the give-up's decision, before the replay gate is taken: the
  liveness loop's give-up enters the machine as kind `r1` with the deadline set at the decision, so a give-up that
  still waits for an exclusive replay-gate hold at the deadline closes through the write-limit path as "Logout not
  sent", writing nothing past the gate. Witnesses: `ReplayGate588.R588f4_ContendedSend_LivenessDisconnects_RefusedNothingWritten`
  (`tests/session/test_538_resend_answer_store_failure.cpp`) GREEN, with no 35=5 written while the answer holds the gate;
  it reaches Disconnected only once its mock clock passes the budget from the give-up, so it is re-read, and re-pinned
  under FR-019 if its clock moves do not pass the budget; and a T051 R1 cell behind an exclusive hold.
- [X] T060 [US1] **Leaving `writing`** (E-3), on the strand with no suspension before the next step: `r2`, transmitted, `!cut`,
  no pending too-high mark claimed, `fsm_state_ == entered_from` → `phase = armed` and the bound woken (it rebuilds its race in
  `armed`); every other case → `phase = none`, the bound woken, and the site records `Disconnected` as today. Before Active the
  order is: enter `writing` and spawn, the write, `armed`, then the refusal's own `Disconnected` write (rows 1, 3, 6). Add the
  T052's cell to `test_105_seams.cpp`.
- [X] T061 [US1] `run_ending_logout_bound_(join, gen)` (research D2 item 8, data-model E-3 "The bound's loop" and "The bound's
  exits"): publish the wake; loop: test the live predicate by phase (`writing`: `phase == writing && conn_gen_ == gen`;
  `armed` after Active: `phase == armed`, generation, `!engine_stop_requested_`, `!reconnect_drain_in_flight_`,
  `fsm_state_ == Active`, never `closing_or_stopped_()`; `armed` before Active: `phase == armed`, generation,
  `state_ == lifecycle::open`, `!reconnect_drain_in_flight_`); race the wake against
  `detail::await_deadline(*effective_clock_, deadline)` and, under the cap, `detail::await_deadline(*engine_.clock,
  *establishment_cap_)`; read the completion by arm (a wake is a re-test; a deadline arm whose clock has reached its instant
  is a re-test; one that completed before its instant is the failure exit). The three exits, each total over the phase it
  finds: **expiry** (row 4 from `writing`: set `cut`, spawn the close frame tested on the generation and a live transport only;
  rows 10, 11, 14 from a live `armed`: `resolved`, then after Active `Disconnected`, before Active `owe_connection_end_()`
  called directly after `resolved`, then the close frame tested on the generation, a live transport and
  `state_ == lifecycle::open`, then the latch cleared), **failure** (the `catch (...)` and the early completion: the expiry
  exit of the phase it finds, never `release_connection_end_()` from `writing` or a live `armed`; row 16), **ordinary**
  (`armed` → `resolved`, then `release_connection_end_()`). Every exit clears its wake. Add the in-body race seam that throws
  `std::bad_alloc` before the `||` expression is awaited, never inside an arm; add T053's reach-proof twins in `test_105_seams.cpp`.
- [X] T062 [US1] The close frame spawned by the bound at rows 4, 10, 11 and 16, with T027's join and D-g fallback, testing
  neither `inbound_depth_` nor the `writing` deferral (data-model E-2, I-2's exceptions); the spawn-failure seam covers these
  spawns too.
- [X] T063 [US1] `send_ending_logout_(text)` (research D2 item 1): writes through the leaf for the state, then arms or records;
  the `Disconnected` writes of Arm B, `resend_gap_deadline_` (`record_disconnected_unless_already_()`),
  `refuse_logon_with_logout_` (`disconnect_unless_superseded_`) and the SendingTime and OrigSendingTime arms move into it;
  `claim_peer_logout_none_()` moves into it and reports a pending mark, which arms no wait (C-2.8); every "initiates" caller of
  T007 routed through it, `honor_peer_next_expected_`'s two Logouts included (ruling R-789; research D2 item 1's widened census); while `ending_in_progress_()` it writes and records nothing (absorbed, W-8).
- [X] T064 [US1] `end_session_silently_()` (research D2 item 4) for the peer-frame silent endings of T008 (the BeginString and
  CompID guards, MsgSeqNum 0, the TestReqID mismatch at both sites, D-3 of `dispose_unparseable_`), recording `Disconnected`
  unless `ending_in_progress_()` (`phase == writing` or `armed`, with `from_active`); every internal failure stays on the edge.
  While armed, `resend_gap_deadline_` returns `nullopt` (W-7).
- [X] T065 [US1] `owe_connection_end_()` gains the term "no wait armed before Active"; during a wait armed after Active the
  edge's call sets `resolved` and owes (row 12); the second caller (the bound's expiry and failure exit before Active, after
  `resolved`; rows 11, 16) and the third (the pre-Active response, row 9).
- [X] T066 [US1] The response test (research D2 item 3, W-1) in `on_inbound_active_`, after the garble test, the header-fault
  disposition, the liveness refresh, the validation gate and the BeginString and CompID guards, and before the Reset-mode
  SequenceReset bypass, MsgSeqNum 0, the too-high branch, the PossDup stage-1 arm and `check_inbound`: at NextNumIn today's
  in-sequence route with `resolved` set at the arm's entry; off NextNumIn a response arm in `acknowledge_peer_logout_`'s shape
  (`resolved`, a `pending_receipt_owner`, the leaf writing nothing, the event, `Disconnected`, the reset point under
  `reset_on_logout`; no number consumed, no `persist_inbound_advance_`, no `fromAdmin`). Before Active, the new
  `Disconnected`-row arm of `on_inbound_cold_`, reached only while a wait is armed: the header scan; `35=5` with no fault,
  MsgType third and the right BeginString and CompIDs sets `resolved` and calls `owe_connection_end_()`; every other frame is
  drained.
- [X] T067 [US1] The latch defers to the writer (E-3, "Other roots during `writing`"): `take_connection_end` returns false and
  leaves the latch set while `phase == writing`; `release_connection_end_` returns without spawning then; the pump's error close
  (`!r`) does not defer.
- [X] T068 [US1] `liveness_may_originate_()` = `fsm_state_ == fsm_state::Active && ending_wait_.phase == none` (data-model E-4):
  the loop condition and the test after every suspension (after the sleep, after each emit); R-1's Logout goes through the
  machine as kind `r1`.
- [X] T069 [US1] `close()` and the drain (E-3 rows 5, 5c, 13 to 15, 17; research D2 item 9): wake the bound at `close()`'s first
  commit of `closing` (beside its `claim_peer_logout_none_()`) and at `drain_for_reconnect_`'s entry; after their transport
  close, join `ending_logout_bound_counter_`, then join `connection_close_counter_` a second time (I-4); `~Session` asserts both
  counters zero; at `install_reconnected_transport` and `attach_accepted_transport` assert `phase` is `resolved` or `none`,
  set `none` and reset `establishment_cap_` (row 17).
- [X] T070 [US1] [seam] cells on the new seams, not readable on `$B`, each RED under its named mutant at `H2`:
  `LogoutWait105.BoundSpawnFails_<R2AfterActive|R2BeforeActive|Reply>_NoLogoutWritten_EofAtOnce` (no Logout bytes at the peer,
  no wait, `Disconnected` and `onLogout` once after Active, EOF at once) and `LivenessTiming105.GiveUp_BoundSpawnFails_StillEof`
  (the slot-74 record, no Logout, EOF); `ConnectionClose105.CloseFrameSpawnFails_<WriteLimit|BoundExpiry>_PeerSeesEofWithNoLaterCall`;
  `LogoutWait105.EndingBound_RaceThrows_<Writing|Writing_StoreHeld>_EofAtOnce` (the Logout's write, or its store call, held;
  EOF at once with the clock frozen, no wait after it, `onLogout` once), `EndingBound_RaceThrows_Armed_EofAtOnce` after Active
  and before Active (the seam armed by the test after the leaf has returned and the phase reads `armed`; the phase the exit
  found asserted as the reach proof; oracle EOF at once), and `EndingBound_RaceThrows_Armed_ResendAnswerBlocked_EofAtOnce`
  (after Active on the SendingTime-Reject route, the peer's ResendRequest answer blocked in `async_write`, so
  `inbound_depth_ != 0`, the seam armed in `armed`: EOF at once). All in `tests/session/test_105_seams.cpp`.
  **Disposition (orchestrator ruling closing Phase 4, 2026-11-21):** `EndingBound_RaceThrows_Armed_ResendAnswerBlocked_EofAtOnce`
  is DROPPED. The race seam runs only when the bound builds its race, and while the wait is armed with a resend answer
  blocked in `async_write` no wake reaches the bound (its wake sources are the leaf's leave, `close()`'s commit, the
  drain's entry, the owe and the response arm), so a seam armed then is never evaluated; the armed failure exit it
  would witness is the exit `EndingBound_RaceThrows_Armed_EofAtOnce` (after and before Active) and
  `EndingBound_DeadlineArmAbsorbsException_Armed*` already reach (evidence, "Phase 4 run 2" and "Phase 4 close-out").
- [X] T071 [US1] Comments naming the new rule, part B (issue #310): `resend_gap_deadline_` and Arm B (R-2, through
  `send_ending_logout_`), `run_ending_logout_bound_` (its exits), and the caller preconditions on `drive_reconnect`'s
  declaration in `session.hpp` (B-103-13: `close()` joins the too-high bound, the R-2 bound and the close frame; `~Session`
  asserts each counter zero; no exemption).
- [X] T072 [US1] Re-pin the R-2-class flips (FR-019): `GapDeadlineOutOfSequenceSite.FrameAtTheBoundDrawsTheLogoutAndTheTransportStaysOpen`
  and its helper `expect_logout_and_open_transport` in `tests/session/test_537_gap_deadline.cpp` (its
  `state_after_the_bound_frame == Disconnected` flips too: Active during the wait), and the 537 note's control cell
  "D-537-6, N"; rename where the name states the old rule. Read RED at H1 and left RED for this task (T035): the cells
  through `expect_logout_and_open_transport` (`GapDeadlineIntervalNeverSet.OutOfSequenceSite_FrameAtTwiceTheDefaultIntervalDrawsTheLogout`,
  `GapDeadlineValidateGateSite.V_InvalidFrameAtTheBoundDrawsTheLogoutAndNoReject`), `BothRoles/GapDeadlineOutOfSequenceSite.FrameAtTheBoundDrawsTheLogoutAndTheTransportStaysOpen/{Initiator,Acceptor}`,
  `TooHighHeartbeatDuringTheGap.N_Control_NothingDelivered_EndsSilentlyAtTheGraceCheck` (the control cell), and the C-ABI
  twins `Capi537Witness.Deadline_<TooHighApplicationFrames|TooHighHeartbeats|TooHighPossDupFrames>_Logout`,
  `Capi537Witness.Deadline_<PossDupWithoutOrigSendingTime|PossDupWithUnparseableOrigSendingTime|ResetBelowExpected>_RejectsThenLogout`
  (`tests/capi/recovery_liveness_capi_witness_test.cpp`); and, for T059a, `ReplayGate588.R588f4_*`.
- [X] T073 [US1] Run T042 to T055 and T070 GREEN, then `-L 105`, `-L session`, `-L capi` on linux-clang-debug and the
  `Lifetime105.*` cells on linux-clang-asan; commit `H2`; `codegraph sync`.

### Mutant and spurious-hit arms for US1 part B (in `$M` at `H2`; quickstart section 5)

- [X] T074 [US1] The response test deleted, and moved below the too-high branch: the off-NextNumIn `*_EndsWaitAndResets` cells
  of T042 RED. The response test moved below the PossDup stage-1 arm: T042's `PeerLogoutPossDupY_<AtNextNumIn|BelowNextNumIn>_EndsWait`
  RED. **Disposition (orchestrator ruling closing Phase 4, 2026-11-21):** two cells are *equivalent-ending survivors*,
  another branch reaching the same observable ending: `GapDeadline_PeerLogoutAboveNextNumIn_EndsWaitAndResets` under
  the deleted response test (the Logout above NextNumIn takes 103's too-high route, which during an ending acknowledges
  at once with the owner set, writes nothing, reaches no `fromAdmin` and resets under `reset_on_logout`), and
  `PeerLogoutPossDupY_AtNextNumIn_EndsWait` under the test moved below the PossDup stage-1 arm (at NextNumIn a PossDup
  Logout with a valid OrigSendingTime passes stage 1 into the in-sequence Logout arm). Every other cell named here is
  RED under its mutant (evidence, "Phase 4 run 2").
- [X] T075 [US1] Absorption: `send_ending_logout_`'s absorb branch deleted, and `end_session_silently_` recording while armed
  (`Absorbed_*` RED); `inbound_resend_request_leaf_`'s assignment failure routed through `end_session_silently_`
  (`TooHighDuringWait_ResendRequestAssignFails_*` RED); the owe refused whenever a wait is `armed` (round 1's predicate: the
  W-9 cells RED).
- [X] T076 [US1] The wait: the bound's deadline read from the last send, and from the write's completion
  (`Bound_DoesNotSlideWithAnAnswerSent`, `WriteReleasedBeforeTheBound_*` RED); the bound exiting on any wake, and the arming
  after the refusal's own `Disconnected` write (`PreActive_BoundSurvives*` RED); the live predicate testing
  `closing_or_stopped_()` (`CloseGracefulDuringWait_*` RED); the claim's result ignored (`PendingTooHighMark_*` RED);
  `expire()` without its `ended` test (`PreActive_WaitArmedAtEstablishmentDeadline_ClosedNoEvent` RED); one post-suspension
  test of the liveness loop reverted to the state test alone (`LivenessDuringWait_*` RED).
- [X] T077 [US1] The write limit: deleted (every `WriteLimit105.*` RED; the spurious-hit arm: the connection still open past
  the bound, which also shows nothing else closes it); its close made through the latch consumer's `inbound_depth_` test
  (`WriteLimit105.R2*`, `Reply_*` RED); the latch consumers' `writing` deferral deleted (`R1WriteHeld_PeerLogoutArrives_*` RED);
  the slot-74 record moved after the write (`WriteLimit105.R1_*`'s record assertion RED); "Leaving `writing`" ignoring `cut`
  (`DirectSession_WriteCompletesAfterTheBoundFires_NoWaitArmed` RED, the phase read `armed` after the drain); a 0 ms limit
  read as "close at once" (`TimeoutZero_WriteUnbounded` RED).
- [X] T077a [US1] (moved from T039, its R-1 half; T035 ruling 2) The R-1 Logout write deleted (the close kept):
  `LivenessTiming105.GiveUp_LogoutTextPinned_ThenEof` RED. T059a's budget counted from the gate's acquisition instead of
  the give-up's decision: `ReplayGate588.R588f4_*` RED.
- [X] T078 [US1] The deadlines' closes: the bound's expiry close made through `release_connection_end_`
  (`Bound_ExpiresWhileResendAnswerBlocked_*` RED); the cap's arm deleted (`PreActive_<LogoutBlockedInWrite|LogoutHeldInStore>_ClosedAtEstablishmentDeadlineNoEvent`,
  `PreActive_ClockOverride_CapOnEngineClock` RED: the close comes at the write limit); `close()`'s second join deleted
  (`Lifetime105.CloseFrame_SpawnedByWriteLimitDuringGracefulClose` RED: the Debug assert, or the counter read after `close()`);
  the bound's join deleted, and the close frame's first join deleted (`Lifetime105.<EndingLogoutBound|CloseFrame>_SessionDestroyedAfterClose`
  RED, T055's guards). **Disposition (orchestrator ruling closing Phase 4, 2026-11-21):** `close()`'s second and first
  close-frame joins deleted (T078c, T078e), and with them T077c2 (`release_connection_end_`'s `writing` deferral
  deleted), survive on linux-clang-debug and on linux-clang-asan (`-L 105` unfiltered, `Lifetime105.*`,
  `Capi537Witness.Deadline_DestroyDuringTheWait_NoBoundOutlivesTheSession`): no observable. The code is kept, with
  the lifetime invariant it defends recorded in data-model E-2 I-4a (I-4); the bound's join (T078d) is RED by
  `~Session`'s Debug assert, on the C++ and the C-ABI destroy paths (evidence, "Phase 4 close-out").
- [X] T079 [US1] The failure exits and spawns: the bound's failure exit routed through `release_connection_end_`
  (`EndingBound_RaceThrows_Writing*`, `EndingBound_DeadlineArmAbsorbsException_Writing*` RED); the armed failure exit routed
  through `release_connection_end_` (`EndingBound_RaceThrows_Armed_EofAtOnce`, after and before Active, RED; its
  resend-answer-blocked variant is dropped, T070); the early completion
  read as a re-test (`EndingBound_DeadlineArmAbsorbsException_*` RED as a failed count within the bounded drive); each
  spawn's fail-closed `catch` replaced by round 2's (`CloseFrameSpawnFails_*` RED; `BoundSpawnFails_*` RED: a Logout on the
  wire under "the failure read as timeout 0").
- [X] T080 [US1] Record the part-B mutant table; every quickstart section 5 mutant named in T036 to T041 and T074 to T079 has a
  RED row, or the task stops. **Disposition (orchestrator ruling closing Phase 4, 2026-11-21):** every row is RED but
  the survivors the ruling dispositions: T074's two equivalent-ending survivors (T074), the dropped T070 cell (its
  mutant read against `EndingBound_RaceThrows_Armed_EofAtOnce`, T079), and the no-observable guards T077c2, T078c,
  T078e and D1 to D3, kept and recorded in data-model E-2 I-4a.

Phase 4 status: closed by the orchestrator's rulings of 2026-11-21 (evidence file, "Phase 4 close-out").

**Checkpoint**: US1 complete; every ending class of the Ending-site table closes within its bound (SC-001).

---
## Phase 5: User Story 2 — a peer that answers is not disconnected with sequence validation off (Priority: P2) (research D5)

**Goal**: FR-009: with `validate_sequence_numbers` off an out-of-sequence Heartbeat reaches the TestReqID rule; on every
Heartbeat route an earlier TestReqID of this connection neither answers nor ends (ruling D-d).

**Independent Test**: knob off, the peer's numbers ahead, fixpp sends a TestRequest, the peer answers with a matching 112;
the session stays Active past the give-up (SC-003).

### RED cells for US2 (write first)

- [X] T081 [P] [US2] `tests/session/test_105_heartbeat_knob_off.cpp`, both roles, driven by the wire reader with
  `test_request_threshold = HeartBtInt`: `HeartbeatKnobOff105.AheadPeerAnswersTenCycles_StaysActive` (at least 10 TestRequest
  cycles, each answered with the matching 112 at a number ahead of NextNumIn; the peer never reads a Logout or EOF and
  `fixpp_session_is_established` stays true), `MismatchedTestReqId_EndsAndCloses` (EOF at once, no Logout),
  `AbsentTestReqId_Answers`, `BehindPeerAnswers`. RED on the base before the fix: the session gives up at the grace
  check. Plus [guard: the #582 predicate widened to the knob-on route] `KnobOn_Unchanged` (B-537-3's cells stay GREEN
  unedited). *Done (`a3012e44`):* "`fixpp_session_is_established` stays true" is read as the Engine session's
  `state() == Active` (C++ cells); `KnobOn_Unchanged` is a Heartbeat below NextNumIn carrying the pending 112 with the
  knob on (the too-low Logout, the TestRequest left pending), and its mutant widens the knob-off branch and its answer
  to every out-of-sequence Heartbeat (T088). B-537-3's control cell that pinned #582 itself,
  `TooHighHeartbeatDuringTheGap.Control_KnobOff_HeartbeatAboveNextNumIn_LeavesItPending`, flips by design and is
  re-pinned as `..._AnswersIt` (FR-019; `b483f501`).
- [X] T082 [US2] Same file, the 112 rule (C-5.4): `TestReqId105.<InSequenceArm|AnswerPendingRoute>_EarlierId_NeitherAnswersNorEnds`
  (a Heartbeat carrying TR1's 112 after TR2: no Logout, no EOF, TR2 still pending — the next give-up comes 2T after that
  frame), `TestReqId105.PreviousConnectionId_Mismatch` (across `drive_reconnect`: EOF at once). RED on the base before
  the fix: the earlier 112 ends the session. Plus [guard: the canonical-form test deleted] `TestReqId105.LeadingZeroId_Mismatch`
  and [guard: the pending test deleted] `TestReqId105.NoTestRequestPending_NotCompared`. *Done (`a3012e44`):*
  `PreviousConnectionId_Mismatch` is a direct initiator (`TestReqId105Direct`) and keeps the rule the US1 head keeps (its
  mutant: the floor not reset at a connection, T088); it and `LeadingZeroId_Mismatch` read RED on the base for US1's
  reason (the base closes no connection at a silent ending) and GREEN at the US1 head; the evidence holds both readings.
- [X] T083 [US2] Commit the cells; read T081 and T082 on `$B`; record.

### Implementation for US2

- [X] T084 [US2] #582 in `on_inbound_active_out_of_sequence_`'s Heartbeat branch: `answer_pending_test_request_(hdr.test_req_id)`
  runs when a TestRequest is pending and either (too high with a resend outstanding) or `!cfg_.validate_sequence_numbers`; the
  rest of the branch unchanged (no advance, no persist, no `fromAdmin`).
- [X] T085 [US2] The TestReqID window (data-model E-4): a free `constexpr` function of `(n, floor, next)` in
  `src/session/read_first_frame_bounded.hpp`, beside `deadline_after` and `resend_gap_deadline` (and the default-threshold
  helper), `(n − floor − 1) mod 2³² < (next − floor) mod 2³²`; the parse-and-classify step a second free `constexpr`
  function there, `detail::is_earlier_test_req_id(std::string_view id, std::uint32_t floor, std::uint32_t next)` (`TR` plus
  the canonical decimal of an n that fits `std::uint32_t`, then the window); `classify_test_req_id_(std::string_view)`
  (answers / earlier / mismatch: absent or the pending id answers, else the free parse function) calls it, replacing both comparisons
  (the in-sequence Heartbeat arm of `on_inbound_active_` and `answer_pending_test_request_`); `test_request_id_floor_` set to
  `next_test_request_id_` at `open()`, `install_reconnected_transport` and `attach_accepted_transport`; a mismatch through
  `end_session_silently_`; `answer_pending_test_request_` returns false only for a mismatch. Fuzzing (`[const §VII.7]`;
  plan Constitution Check VII §7, 027's seed-extension precedent): extend `tests/fuzz/fuzz_session_recovery_admin_parse.cpp`'s
  seeds with Heartbeats carrying 112 forms (canonical, a leading zero, a sign, letters, an overflowing number, `TR` alone,
  an empty value `112=`, and the canonical forms of the window's two bounds),
  and have the harness call `detail::is_earlier_test_req_id` directly on each fed payload's 112 bytes (including
  `session/read_first_frame_bounded.hpp`; check the target's include directories carry `src/`, and add it if not), so the
  parse is fuzzed whether or not a TestRequest is pending. The run is T148.
- [X] T086 [US2] [guard: a non-modular comparison; the full-cycle case dropped, `next == floor` read as a full window]
  `TestReqIdWindow105.<AtUint32Max|AcrossTheWrap|OneFullCycle_NoValueEarlier>` in `tests/session/test_105_seams.cpp`:
  `constexpr` cells (`static_assert` under `FIXPP_105_STATIC_GUARDS` plus a runtime twin, so a mutant reports as a test
  failure, not a compile error; Execution rules, "Guard static_asserts") over T085's window function; and [guard: the
  parse's canonical-form, sign, overflow or `TR`-length test deleted]
  `TestReqIdParse105.<Canonical_Earlier|LeadingZero_Mismatch|Sign_Mismatch|Overflow_Mismatch|PrefixAlone_Mismatch>` in the
  same file, `constexpr` cells of the same form over `detail::is_earlier_test_req_id` (a canonical earlier id, a leading
  zero, a sign, a number past `std::uint32_t`, `TR` alone). Not readable on `$B` (new functions).
- [X] T087 [US2] Run T081, T082 and T086 GREEN, `-L 105` and the B-537-3 cells; commit `H3`.

### Mutant arms for US2 (in `$M` at `H3`)

- [X] T088 [US2] The #582 predicate set back (`AheadPeerAnswersTenCycles_*` RED); the #582 predicate widened to the knob-on
  route (`KnobOn_Unchanged` RED); each 112 site's plain comparison restored (`TestReqId105.*_EarlierId_*` RED per site); the
  canonical-form test and the pending test deleted (their guards RED); the window function made non-modular, and its
  full-cycle case dropped (`TestReqIdWindow105.*` RED); in `detail::is_earlier_test_req_id`, one mutant each: the
  canonical-form test deleted (`TestReqIdParse105.LeadingZero_Mismatch` and `TestReqId105.LeadingZeroId_Mismatch` RED),
  the sign refusal deleted (`Sign_Mismatch`), the overflow test deleted (`Overflow_Mismatch`), the `TR`-length test deleted
  (`PrefixAlone_Mismatch`). Record the mutant table; every quickstart section 5 row for this story is RED, or stop.
- [X] T148 [US2] Fuzz run ([const §VII.7]): build `fuzz_session_recovery_admin_parse` at `H3` with T085's seeds and direct
  call (the `linux-clang-asan` preset, which sets `FIXPP_BUILD_FUZZ=ON` and builds the fuzz targets with
  `-fsanitize=fuzzer,address,undefined`, a tree on `/mnt/wsl/fixppbuild`, under the build lock), show first that the run reaches
  `detail::is_earlier_test_req_id` (a coverage read or a counter, non-zero), then run it for at least 10 minutes
  (`-max_total_time=600` over its corpus directory). Record the command, the duration, the execution count and the
  outcome in the evidence file; a crash or a sanitizer report is a defect until disproven.

**Checkpoint**: US2 complete.

---

## Phase 6: User Story 3 — TestRequest timing is QuickFIX C++'s and the threshold works (Priority: P2) (research D6)

**Goal**: FR-010 to FR-012: TestRequest at T of inbound silence (default 1.2 × HeartBtInt), give-up strictly after 2T of
inbound silence, one sleep per iteration, T validated.

**Independent Test**: per role, the default cell and a set-threshold cell, both RED on the base (SC-002).

### RED cells for US3 (write first)

- [X] T089 [P] [US3] `tests/session/test_105_liveness_timing.cpp`, both roles, H = 10 s on the mock clock, threshold unset
  unless named: `LivenessTiming105.Default_NoTestRequestAt11s_OneAt12s` (the base sends at 10 s),
  `Default_ActiveAt23900ms_EndedAt24sPlusGuard` (the base ends near 20 s), `SetThreshold7s_TestRequestAt7s_GiveUpAt14sPlusGuard`,
  `GuardAtExactly2T_EchoWins`, `At2TPlus1ns_GivesUp`. Each asserts the wire (the TestRequest, the Logout) and EOF at the named
  mock instants. RED on the base before the fix: TestRequest at 1 × HeartBtInt, give-up near 2 × HeartBtInt.
  Done: evidence file, Phase 6 (US3); cells in `c179f57b`, the timeline's read-at-the-instant wait in `f636aebb`.
- [X] T090 [US3] Same file, the silence measure and the sleep set (clarification D-c, data-model E-4):
  `TrafficAfterTestRequestNoHeartbeat_NotGivenUpAt2T_GivenUpAt2TAfterLastFrame`;
  `SilentPeer_NoHeartbeatWhileTestRequestPending` (positive control: the TestRequest at 12 s and the session Active at 22 s;
  no Heartbeat at 22 s; the give-up just after 24 s); `FrameAfterTestRequest_HeartbeatAt22s_TR2At25s_NoneAt35s_GiveUpAfter37s`;
  `FrameAfterHeartbeatInstant_HeartbeatAtTheStaleGiveUpWake` (a frame at 23 s; the Heartbeat just after 24 s; positive control:
  none at 23 s); `SilentAfterTestRequest_NoSpin` (T005's counting decorator as `clock_override`: one `sleep_until` per wake, a
  bounded count from TR1 to the give-up, under the bounded drive so its RED arm terminates as a failed count);
  `SecondTestRequest_LateAnswerToTheFirst_StaysActive` (D-d: TR2 stays pending; the give-up 2T after that frame). RED on the base before the fix.
  Done: as T089.
- [X] T091 [US3] Same file, precedence and edges (FR-010, C-6.4, C-6.5): `GapOpen_PeerSendsNonAdvancingFrame_DeadlineLogout`,
  `GapOpen_PeerSilent_R1LogoutAt2T`, `GapOpen_ThresholdAboveTwoH_PeerSilent_TestRequestAtT_R1LogoutAt2T` (T = 25 s > 2H with
  H = 10 s: no TestRequest before T, no gap-deadline Logout with no frame, R-1's Logout and EOF just after 2T; the base
  sends the TestRequest at H and ends near 2H), `HeartBtIntNegative_NoTestRequest`, `ThresholdOverflow_Saturates` (2T past
  the clock's range: no give-up), RED on the base before the fix; [guard: the loop's HeartBtInt test deleted]
  `HeartBtIntZero_NoTestRequest`.
  Done: as T089; the gap cells open the gap at 1 s, so their instants are 1 s later than a gap at Active.
- [X] T092 [P] [US3] `tests/config/test_load_negative_battery.cpp`: `LoadNegativeBattery.TestRequestThreshold_Zero_OutOfRange`
  (an `out_of_range` diagnostic at `session[i].test_request_threshold`), RED on the base before the fix; [guard: the
  duration parser's leading-digit refusal deleted] `LoadNegativeBattery.TestRequestThreshold_Negative_MalformedValue`.
  Base reading (T094; the Execution rules' overlay covers `tests/session/` only): overlay this one file on `$B` from the
  commit holding the cells (`git -C "$B" restore --source=<commit> -- tests/config/test_load_negative_battery.cpp`), build
  only its own target `config_044_tests`, and run it by a label that lists that entry alone on `$B` (`-L config` also selects
  entries not built there, which do not run: not a reading). Record `ctest --preset <p> -N -L '^044$'` first; when it lists
  only `config_044_tests`, run `GTEST_FILTER='LoadNegativeBattery.TestRequestThreshold_*' ctest --preset <p> -L '^044$'
  --no-tests=error -V`; otherwise run the binary with `--gtest_filter='LoadNegativeBattery.TestRequestThreshold_*'`. Record
  per cell its result and the reported count (0 fails the step). Never `-R`.
  Done: as T089, with fixtures `neg_test_request_threshold_{zero,negative}.toml`.
- [X] T093 [US3] `test_105_liveness_timing.cpp`: `SessionOpen105.Threshold<Zero|Negative>_InvalidConfig` and
  `EngineRegister105.Threshold<Zero|Negative>_InvalidConfig` (`invalid_session_config`). RED on the base before the fix.
  [guard: an upper bound on T added at `Session::open()`] `SessionOpen105.ThresholdAboveGapDeadline_Accepted` (T = 25 s > 2H
  with H = 10 s: `open()` succeeds; FR-011, C-6.6: no bound but `T > 0`).
  Done: as T089.
- [X] T094 [US3] Commit the cells; read T089 to T093 on `$B` (T092 by its own recipe); record.
  Done: evidence file, Phase 6 (US3), T094 (read on `$B` from `c179f57b` and again from `f636aebb`: 39 ran, 36 RED, the 3 guards GREEN, no skip).

### Implementation for US3

- [X] T095 [US3] `run_liveness_loop` (data-model E-4, research D6): T computed once before the loop,
  `cfg_.test_request_threshold.value_or(default_test_request_threshold(H))`, the helper (H × 1200 ms, saturating at
  `milliseconds::max()`) in a `detail` header beside `deadline_after` (`read_first_frame_bounded.hpp` or its sibling);
  the TestRequest instant `deadline_after(last_inbound_steady_, T)`; the give-up instant three saturating steps (T, T, then
  `std::chrono::nanoseconds{1}`); `tr_sent_at`; the eligibility rules (Heartbeat eligible with no TestRequest pending or an
  inbound frame since it; give-up eligible with one pending and none since [superseded by spec clarification "Session 2026-11-21
  (implementation, Phase 6)": eligible with one pending]); **one sleep per iteration** to the earliest
  instant of the sleep set (the Heartbeat instant when eligible or not yet reached, the TestRequest and give-up instants when
  eligible), the Heartbeat instant reached while ineligible dropped (the spin trap); on waking act on every eligible
  instant reached (give-up, else TestRequest, else Heartbeat); D-536-2's state test and filter reset before every sleep;
  the loop and the gap deadline off at HeartBtInt ≤ 0 (`<= 0`).
  Done: `1f1bfff0` (`detail::default_test_request_threshold`, `detail::test_request_give_up_instant`).
- [X] T096 [US3] Validation (FR-011, C-6.6): `Session::open()` and `Engine::register_session` refuse a set T ≤ 0 with
  `invalid_session_config`; `src/config/scalar_mappers.cpp` maps a zero duration to `out_of_range` (a negative token keeps the
  parser's `malformed_value`).
  Done: `1f1bfff0`.
- [X] T097 [US3] [guard: the product formed unsaturated; the guard added by hand to a saturated instant]
  `LivenessTiming105.<DefaultThresholdHelper_Saturates|GuardAtTheSaturatedInstant>` in `tests/session/test_105_seams.cpp`: `constexpr` cells (`static_assert` under
  `FIXPP_105_STATIC_GUARDS`, with runtime twins; Execution rules, "Guard static_asserts") of the default-threshold helper and of the 1 ns guard at `steady_time_point::max()`. Not readable on `$B` (new helper).
  Done: `1f1bfff0` (`tests/session/test_105_seams.cpp`, namespace `ltg`).
- [X] T098 [US3] FR-012 surfaces and comments: `SessionConfig::test_request_threshold`'s comment in
  `include/fixpp/session/session_config.hpp`; `run_liveness_loop`'s algorithm block (its "1 × HeartBtInt" steps) and its
  declaration comment in `session.hpp` ("after heartbt_int of no inbound data", "grace window (1×heartbt_int)"); the comment
  above `detail::resend_gap_deadline` in `src/session/read_first_frame_bounded.hpp` ("off only at exactly 0"); the QuickFIX
  parity-table row in `tests/config/test_quickfix_parity_table.cpp` and its TOML documentation; each states the new rule.
  Done: `1f1bfff0` (the session_config.hpp, session.hpp, run_liveness_loop and resend_gap_deadline comments) and
  `a60083ad` (orchestrator ruling, Phase 6: the parity table's `test_request_threshold` row, keyed on the QuickFIX
  key HeartBtInt, since QuickFIX C++'s SessionSettings.h has no key of its own for the TestRequest timing). No TOML
  documentation page names any session key; none exists, so none is updated (recipe: evidence file, Phase 6, T098).
- [X] T099 [US3] Re-pin the moved timings (FR-019): `tests/session/test_heartbeat_cadence_8cell.cpp`'s T018 cells (that
  file's own task, not this bundle's T018) `HeartbeatCadence8Cell.CellB_*` and `CellC_*_UnansweredTestRequestDisconnects`
  (a Cell B that discriminates 1 × from 1.2 ×), `HbTrTest.UnansweredTestRequestDisconnects` in `tests/session/heartbeat_testrequest_test.cpp` where it crosses HeartBtInt, `ci/red-arms/batch18-lost-advance.sh` (the cell it names), every cell of T011's list, and any
  cell pinning an earlier TestReqID as an ending; rename where the name states the old rule.
  Done: `735aa11d`; the re-pin list and the T011-list classification: evidence file, Phase 6 (US3), T099.
- [X] T100 [US3] Run T089 to T093 and T097 GREEN, then `-L 105`, `-L session` and `-L config`; commit `H4`; `codegraph sync`.
  Done: `735aa11d` (H4): the gate population 249/249, `-L config` 4/4, `-L 105` 2/2; evidence file, Phase 6 (US3), T100.

### Mutant arms for US3 (in `$M` at `H4`)

- [X] T101 [US3] The default threshold set back to H (the timing cells RED); the give-up's eligibility ignoring inbound since
  the TestRequest (`TrafficAfterTestRequestNoHeartbeat_*` RED) [superseded by spec clarification "Session 2026-11-21
  (implementation, Phase 6)": that edit is now the design; its reverse, the term restored, is the mutant, RED by
  `ReplayGate588.R588g_*`]; the Heartbeat's eligibility ignoring inbound since the
  TestRequest (`FrameAfterTestRequest_HeartbeatAt22s_*` RED); the passed, ineligible Heartbeat instant kept in the sleep set
  (`SilentAfterTestRequest_NoSpin` RED, terminating under the bounded drive); the HeartBtInt test reverted to `== 0`
  (`HeartBtIntNegative_NoTestRequest` RED); the loop's HeartBtInt test deleted (`HeartBtIntZero_NoTestRequest` RED); the
  duration parser's leading-digit refusal deleted (`TestRequestThreshold_Negative_MalformedValue` RED); an upper bound on T
  added at `Session::open()` (`ThresholdAboveGapDeadline_Accepted` RED); each 112 site's plain comparison restored at `H4`
  (`SecondTestRequest_LateAnswerToTheFirst_StaysActive` RED); T097's two mutants (built per Execution rules, "Guard
  static_asserts"). Record the mutant table; every quickstart section 5 row for this story is RED, or stop.
  Done: evidence file, Phase 6, "T101 mutant table" and "Phase 6 close (orchestrator rulings)". RED: Mdef, Mcfg,
  MhbElig, Mspin, MhbDel, MupperT, M112, MguardHand, MgiveEligBack (the dropped term restored, `R588g_*`), MhbGate
  (R588c's Heartbeat sibling). Dispositions by ruling: MeqZero aborts at `deadline_after`'s assert, a loud RED for a
  debug-asserted precondition; MleadDel an equivalent survivor (`from_chars` refuses the same token); Munsat refused
  at compile time (`-Werror,-Winteger-overflow`), no runtime reading owed.

**Checkpoint**: US3 complete.

---

## Phase 7: User Story 4 — every acceptor connection refused before a session is logged (Priority: P3) (research D8)

**Goal**: FR-014, FR-015.

**Independent Test**: an acceptor with a capturing sink; one record per refused connection naming its reason (SC-004).

### RED cells for US4 (write first)

- [X] T102 [P] [US4] `tests/session/test_105_presession_log.cpp`, capturing sink:
  `PreSessionLog105.<NotTls|TlsHandshake|NoTimeLeft|FirstFrameTimeout|FirstFrameOverBudget|FirstFrameOverLimit|FirstFrameReadError|NoIds|NoSession>_OneRecord`
  (exactly one warn record of C-7.1's format with that `reason`; each `FirstFrame*` cell asserts `reason=first_frame` and the
  read's error that tells it apart from the other three: timeout, over budget, over-limit frame, read error, the last by
  the peer resetting the connection inside its first frame), `NoSession_RecordNamesCompIds`,
  `Flood_BoundedWithSuppressedCount` (at most one record per window on the engine clock; the next carries the suppressed
  count), `LongCompIds_CutAt15Bytes` asserting L-105-d's cut, `ControlBytesInCompIds_WrittenAsQuestionMarks` (a first frame whose
  SenderCompID carries a line feed and another control byte: the record holds `?` for each and no line break; C-7.1), `ListenerBindFails_OneRecord` (a port in use: one error record of C-7.3's format with the system error's value). RED on
  the base before the fix: no record.
  Partial: every named cell but `NotTls_OneRecord` in `d3f661db` (13 cells; evidence file, Phase 7). Not written,
  escalated: `NotTls_OneRecord`. The branch is unreachable without a seam: on a TLS profile the listener mints its
  connections through `asio_tls_transport_factory::make_accepted` (`asio_listener::async_accept`), which always yields
  a `TlsTransport`; the record is written at the branch (T105) with no cell. Disclosed: `FirstFrameOverBudget` and
  `FirstFrameOverLimit` carry the same read error, `wire_frame_too_large` (the budget check and the Framer's limit
  both return it, `read_first_frame_bounded`), so the record tells those two apart from the timeout and the read error
  but not from each other; each cell asserts that value.
  Done: `NotTls_OneRecord` not written (orchestrator ruling 2026-11-24): unreachable without a seam — a TLS listener
  mints only `TlsTransport` via `asio_tls_transport_factory::make_accepted`; the branch is an uncovered defensive line,
  dispositioned per Article IX §1 at T143/verify. The over-budget / over-limit same-reason premise is accepted; the
  contract C-7.1 / data-model E-6 wording is amended in Phase 10.
- [X] T103 [US4] Commit the cells; read T102 on `$B`; record.
  Done: evidence file, Phase 7, T103 (13 ran, 13 RED by the record assertion, no skip, no abort).

### Implementation for US4

- [X] T104 [US4] Hoist the rate gate (data-model E-7): `include/fixpp/session/detail/log_rate_gate.hpp` (new installed
  implementation header, namespace `fixpp::session::detail`, the same fields), inline `admit_log_record(gate, now, window,
  occurrences)`; `Session` keeps the name by a member alias and `admit_log_record_` forwards with `effective_clock_` and
  `max(HeartBtInt, 1 s)`.
  Done: `17a16832`. One departure from data-model E-7's parameter list: `admit_log_record(gate, now, window_end,
  occurrences)` takes the window's end, computed by the caller with `detail::deadline_after` (in
  `src/session/read_first_frame_bounded.hpp`, which an installed header cannot include), so the saturating step stays in
  one helper; the behaviour is the one E-7 states.
- [X] T105 [US4] `src/session/engine.cpp`: `enum class pre_session_reject` (data-model E-6) and `log_pre_session_reject` beside
  `log_role_loop_open_failure` (same logger resolution and trace context, warn, `cat::session`), each byte of the BeginString and CompIDs outside
  printable ASCII (0x20 to 0x7E) replaced by `?` before `ArgValue::from_inline` (research D8); one record per reject branch of
  T015's census; one gate per entry as a local of `run_accept_loop` (clock `engine_cfg.clock`, window `max(HeartBtInt or the
  default, 1 s)`), the default being `kDefaultHeartBtInt`, hoisted from `src/session/session.cpp`'s file scope into the
  shared `detail` header that holds the default-threshold helper (beside `default_test_request_threshold` in
  `read_first_frame_bounded.hpp`, T095), which `session.cpp` and `engine.cpp` both include: no second literal; the listener catch split into `catch (std::system_error const&)` (its `code().value()`) and `catch (...)`
  (0), each one error record; a listener-construction seam; both formats registered in `src/log/format_registry.cpp`.
  Done: `17a16832` (`log_pre_session_reject`, `log_listener_bind_failure`, `Engine::test_hook_listener_construction_fails_`
  set through `engine_test_access::set_listener_construction_fails`).
- [X] T106 [US4] [seam] `PreSessionLog105.ListenerConstructionThrowsNonSystemError_OneRecordErrorZero` in `tests/session/test_105_seams.cpp` (the seam throws
  `std::bad_alloc` in place of the construction; one record, `error=0`). Not readable on `$B`.
  Done: `17a16832`; RED under T107's MgenRecDel.
- [X] T107 [US4] Run T102 and T106 GREEN and `-L 105`; commit `H5`. Mutants in `$M`: the printable-ASCII replacement deleted (`ControlBytesInCompIds_WrittenAsQuestionMarks` RED); each listener catch arm's record deleted
  (`ListenerBindFails_OneRecord`, `ListenerConstructionThrowsNonSystemError_*` RED); the accept-loop gate bypassed
  (`Flood_BoundedWithSuppressedCount` RED). Record the mutant table; every quickstart section 5 row for this story is RED, or
  stop.
  Done: `17a16832` (H5); evidence file, Phase 7, T107 (the four mutants RED, each in its named cell only).

**Checkpoint**: US4 complete.

---

## Phase 8: User Story 5 — dead state is gone (Priority: P3) (research D7)

**Goal**: FR-013, SC-005.

**Independent Test**: a `-k 0` build under the project's warning flags with the markers removed, and a `git grep` showing no
remaining reference.

### Instrument first (US5)

- [X] T108 [US5] On `$B`, run the SC-005 instrument `git grep -n 'maybe_unused' -- src include` filtered to the #559 markers of
  sites 2 and 3, and T013's caller census; both must report their known positives there (the instrument can report non-zero).
  Done: evidence file, Phase 8, T108 (four markers, the three `ReconnectFsm` members and `parse_cn_from_dn_local`; the
  census's `drive_logout` test call).

### Implementation for US5

- [X] T109 [US5] `include/fixpp/session/reconnect_fsm.hpp` and `src/session/reconnect_fsm.cpp`: remove `attempt_index_`,
  `heartbeat_interval_`, `logout_disconnect_timeout_`, `heartbeat_timer_`, `test_request_timer_`, `logout_timer_`,
  `last_outbound_testreqid_`, `drive_logout`, every caller-less stub T013 confirms (`run_heartbeat_cadence`,
  `run_inbound_liveness_watch` with its timing comment, `validate_inbound_heartbeat_testreqid`,
  `process_inbound_sequence_reset`, `reply_to_inbound_resend_request`), the constructor's `heartbeat_interval` and
  `logout_disconnect_timeout` parameters and the false header comment; update `Session::Session` and every constructor call
  site of T013.
  Done: `05bb1b2f` (22 test construction sites, `Session::Session`; the census widened to `bindings bench examples tools
  docs` found no other).
- [X] T110 [US5] Remove `parse_cn_from_dn_local` from `src/session/session.cpp`.
  Done: `05bb1b2f`.
- [X] T111 [US5] Retire `DriveLogoutStub_*` and `Initiator_DriveLogout_*` in `tests/session/test_logout_timeout.cpp` with a note
  naming FR-013; remove their entry from `ci/red-arms/batch11-labels.txt`; correct the "triggers drive_logout path" comment in
  `tests/fuzz/fuzz_session_recovery_admin_parse.cpp` and every test comment naming a removed stub as a RED witness.
  Done: `05bb1b2f`, `2f478e50` (test_logout_timeout.cpp, batch11-labels.txt, the fuzz harness, test_heartbeat_cadence_8cell.cpp,
  test_recovery_admin_span_gapfill.cpp, test_recovery_store_horizon.cpp; `test_inbound_sequence_reset.cpp`'s past-tense
  history kept as written).
- [X] T112 [US5] Size pins: re-run T010's two greps at the head and update every `sizeof(Session)` / `sizeof(ReconnectFsm)` pin
  they report in `tests/` and `ci/`; the full `-k 0` build of T113 is the second instrument.
  Done: no pin to update (evidence file, Phase 8, T112).
- [X] T113 [US5] Full `-k 0` build on linux-clang-debug and linux-gcc-release; T108's two instruments now report nothing; commit
  `H6`; `codegraph sync`.
  Partial: linux-clang-debug full `-k 0` in `$M` at `05bb1b2f` rc 0, no compiler warning; the two instruments report
  nothing; H6 `e2202e44`. linux-gcc-release: the full `-k 0` was stopped (its regenerated codegen builders ran at a rate
  of hours) and the gcc-release build of every target compiling a C++ file the branch touches was run instead (rc 0
  after `e2202e44` braced five gtest-macro ifs gcc refused under `-Werror=dangling-else`). Owed: the full gcc-release
  `-k 0` (evidence file, Phase 8, T113).
  Done (orchestrator ruling 2026-11-24): the full linux-gcc-release `-k 0` is not re-run locally (its codegen builders
  run for hours). Discharged by the gcc-release build of every target compiling a touched C++ file (done, `e2202e44`),
  T136 at the end of Phase 11, and CI's linux-gcc-release lane.

**Checkpoint**: US5 complete.

---

## Phase 9: Polish & cross-cutting — C ABI 1.18 and the Python surface (research D9; contract C-9)

- [X] T114 [P] RED first, the cells committed before their base reading: C-driven witnesses in `tests/capi/recovery_liveness_capi_witness_test.cpp`, each with a positive
  control (the session shown live first, or a well-formed message afterwards reaching `cb`): `fixpp_session_is_established`
  true during an R-2 wait after Active and false after its end; `fixpp_session_send` refused during the wait;
  `fixpp_session_close` graceful and terminal during the wait (one close each); `fixpp_session_register_callback` and
  `fixpp_session_register_send_callback` delivery during the wait; ~~`fixpp_session_send` called from inside the
  application callback during the wait, refused with `session_invalid_state_for_send` and returning (contract C-9,
  reentrancy)~~ (dropped, below); the peer's EOF after an ending through the C ABI. RED on the
  base before the fix. Base reading (the Execution rules' overlay covers `tests/session/` only): overlay this file (and its
  support header, if this task edits it) on `$B` from the commit holding the cells, reconfigure (the reading counts only after
  it returned 0), build only its own target `capi_recovery_liveness_test`, and run it with the new cells filtered. Its labels
  (`capi`, `536`, `537`, `538`) are shared with entries not built there, so a label run is not a reading unless
  `ctest --preset <p> -N` with the chosen label, recorded first, lists this entry alone; otherwise run the binary with
  `--gtest_filter='<the new cells>'`, never `-R`. Record per cell its result and the reported count (0 fails the step). The witnesses name only C-ABI symbols present on the base (contract C-9: no symbol
  changes), so the file compiles there; a compile failure is not RED.
  Partial: `59fc1ba1`, suite `Capi105Wait` (7 cells: `IsEstablished_TrueDuringTheWait_FalseAtThePeersLogout`,
  `Send_RefusedDuringTheWait_ReturnsAtOnce`, `SendCallback_NotInvokedForASendDuringTheWait`,
  `ReceiveCallback_MessageAtNextNumIn_DeliveredDuringTheWait`, `Close_DuringTheWait_ReturnsAtThePeersLogout_OneClose`,
  `EngineDestroy_DuringTheWait_EndsItAtOnce_OneClose` (the C ABI's terminal close is `fixpp_engine_destroy`, W-14),
  `PeersLogout_EndsTheWait_PeerReadsEofAtOnce`); on `$B` 7 ran, 7 RED, rc 1, no abort (evidence file, Phase 9, T114).
  Not written, escalated: `fixpp_session_send` from inside the application callback during the wait. On the head the
  call blocks (`fut.get()` on the engine's worker, which runs the callback), as `session.h`'s FR-013a annotation and
  `send_recv_test.cpp`'s header state; contract C-9's reentrancy bullet said it returns.
  Done: reentrancy cell dropped: C-9 corrected to FR-013a (orchestrator ruling 2026-11-24). `session.h` and
  `version.h` carry no such claim (`git grep -n session_invalid_state_for_send -- include/fix/c_api` reports
  nothing); checklist `c-abi.md` CHK011's disposition annotated.
- [X] T115 `include/fix/c_api/version.h`: `FIXPP_C_ABI_VERSION_MINOR` 18 and the 1.18 history entry, marked **BREAKING** ([const §X.7]: the version comment carries
  what no declaration carries), the counterparty's side:
  every ending closes the connection; the unanswered TestRequest's Logout; the R-2 wait; the write limit on every ending
  Logout; the TestRequest at 1.2 × HeartBtInt; the TestReqID(112) rule. Never clang-format `include/fix/c_api/*.h`.
  Done: `aca84d06` (evidence file, Phase 9, T115).
- [X] T116 `include/fix/c_api/session.h`: a 1.18 clause on each observer of contract C-9, each clause marked **BREAKING** ([const §X.7]), stating what contract
  C-9 "What the 1.18 clauses say" gives it; the 1.11, 1.12 and 1.17 sentences this feature changes amended in place,
  "amended in C-ABI 1.18, 105". First run research D9's census of affected declarations at the head and record it: its
  known positives are the five observers; a hit on another declaration gets its own clause, and every sentence the
  census classes "a result this feature changes" is amended (then `git grep -n 'amended in C-ABI 1.18' -- include/fix/c_api`
  lists them for review).
  Done: `aca84d06`; the census and its classes in the evidence file, Phase 9, T116.
- [X] T117 Move every "pins the minor" row of T012 (the capi tests, `tests/capi/CMakeLists.txt`, the support headers, the CI
  scripts it lists) and regenerate `tools/capi_freeze.sha256` by the generator T012 found (the manifest's own `sha256sum` format over
  `include/fix/c_api.h` and every header under `include/fix/c_api/`, never edited by hand; contract C-9); then run
  `bash tools/check_capi_freeze.sh` and `tools/check_capi_occupancy.sh` (both pass) and `-L capi`.
  Done: `d3c505a4`; `check_capi_freeze.sh` and `check_capi_occupancy.sh` rc 0, `-L capi` 38/38 (evidence file, Phase 9, T117).
- [X] T118 Python (Article X §7, in-repo consumers): run T012's grep over `bindings/` again at the head, its seeded known
  positive first; move any pin it reports and record the result, a zero included. Then contract C-6.7's instrument, research
  D6's `grep -rn test_request_threshold bindings include/fix`, seeded first in `$M`: it must report nothing (no C-ABI setter,
  no Python surface).
  Done: no pin in `bindings/` and C-6.7's instrument reports nothing at the head; both seeded first in `$M` (evidence file, Phase 9, T118).
- [X] T119 Run T114 GREEN and `-L capi`; commit `H7`.
  Done: `Capi105Wait` 7/7 GREEN, `-L capi` 38/38; H7 is the commit that marks this task (evidence file, Phase 9, T119).

- [X] T119a [US3] Held TestRequest gets a fresh window (owner 2026-11-24). When a TestRequest is pending, the give-up
  instant is the later of (last inbound + 2T) and (that TestRequest's write instant + T); a TestRequest held behind a
  resend answer past last + 2T is written on release and the session gives up only T after that write if nothing
  arrives (FIX-SL §4.5.5), also when no TestRequest was pending before the hold; a frame (or the TestReqID answer, D-d)
  inside that window keeps the session. (a) spec.md Clarifications "Session 2026-11-24", data-model E-4, contract C-6.2
  and every place the give-up instant is specified, amended in place, dated, naming the ruling; (b) the code in
  `run_liveness_loop` / `detail::test_request_give_up_instant` (or its caller), saturating; (c) `ReplayGate588.R588g`
  re-pinned (the TestRequest written on release, no Logout before write + T, then R-1's Logout naming it) plus a
  positive arm (an answer within T after the release keeps the session Active), RED first on the head before the
  change, then a named mutant in `$M` (the max dropped) RED by those cells; (d) the comment rule; (e) `-L 105` and the
  538/replay-gate binary unfiltered, then the gate.
  Done: cells `040ba246` (RED on the head before the change: 3 ran, 3 RED, rc 1, no abort), the code `8546614b`
  (and `37de6150`), the records `33e4613a`, two give-up cells re-staged `555ec83a`; mutants in `$M` at `8546614b`:
  Mmax (the later instant dropped) RED by all three cells and `LivenessTiming105.FreshWindow_IsTheLaterInstant`, Mans
  (the no-frame-since term dropped) RED by both R588g cells, R588h an expected survivor (no TestRequest pending before
  its hold); `-L 105` 2/2, the 538 binary unfiltered 41/41, the 249-entry gate 249/249 (evidence file, Phase 9, T119a).
  Escalated: the no-frame-since eligibility is an implementation reading; its consequence for a hold that never ends
  was ruled by the owner (the cap at 3T) and is T119b.
- [X] T119b [US3] Held TestRequest capped at 3T (owner 2026-11-24, "cap at 3T"). If the TestRequest is still held (not
  yet written) at the last inbound frame + 3T, the session gives up then: the give-up is decided (final) and R-1's
  Logout goes through the existing R-1 budget and write limit (the budget starts at the decision, before the replay
  gate), then the connection closes. One released and written before the cap keeps its fresh window (the later of
  last + 2T and write + T). Saturating. (a) spec.md Clarifications "Session 2026-11-24" second bullet, data-model E-4,
  contract C-6.2 and C-3.1, research D6, amended in place, plus `version.h`'s 1.18 history and
  `fixpp_session_is_established`'s 1.18 clause (manifest regenerated); (b) RED first: the stuck-hold cell and a
  boundary cell; (c) the code; (d) a named mutant in `$M` (the cap dropped) RED by the stuck-hold cells; R588g, R588h
  and the FreshWindow guard GREEN.
  Done: cells `a500c401` (`HeldTestRequestCap105.*`, both roles: three stuck-hold cells, one step past the cap,
  parked in the TestRequest arm, parked in the Heartbeat arm; the boundary at exactly 3T; a release after the decision),
  on the head before the change (`1c9015c9`'s `src/`): 10 ran, the 8 capping cells RED, the 2 boundary cells GREEN by
  construction (the head never gives up there), rc 1, no abort. Code `d83ae13a` (`detail::held_test_request_cap`;
  the give-up also at the held cap; one gate wait for the Heartbeat and TestRequest arms, joined with the cap while a
  TestRequest is pending); [guard] `LivenessTiming105.HeldCap_IsThreeThresholdsAndTheGuard`. Mutants in `$M`
  (the range applied as a patch, `-DFIXPP_105_STATIC_GUARDS=OFF`): Mcap (the cap dropped) RED by the 8 capping cells;
  Mjoin (no join) RED by both parked cells; Mtronly (the Heartbeat arm's wait not joined) RED by the Heartbeat-parked
  cell; Mlate (the guard 1 s) RED by the 8 capping cells and the guard cell; Mearly (no guard) and Mtwo (two
  thresholds) RED by the boundary cells and the guard cell, Mtwo also by both R588g cells; every other cell GREEN
  under each, R588g, R588h and `FreshWindow_IsTheLaterInstant` included (evidence file, Phase 9, T119b).
  Escalated: the cap applies while a TestRequest is pending (the give-up names it); a hold with none pending before
  it is not capped, as on the base (L-105-f, #714): whether the cap applies there, and what its Text(58) names, is
  open (spec.md Clarifications "Session 2026-11-24", second bullet's implementation reading).

---

## Phase 10: Polish & cross-cutting — records, follow-ups and supersession (FR-016, FR-018, FR-019; research D11, D12)

- [X] T120 **(orchestrator)** File the three follow-up issues of plan "Follow-ups to file before Gate B" on fixpp, each
  labelled at creation from `gh label list`: bounding every non-ending write (L-105-f); the per-session timer slot under a
  direct driver (`system_clock_source`); the read pump's establishment race re-racing an absorbed exception. Record the issue
  numbers in the evidence file. Precedes T123.
  Done (orchestrator): issues filed 2026-11-24, labelled: #714 (L-105-f non-ending writes unbounded), #715 (direct-driver
  timer slot), #716 (run_read_pump establishment race spin), #717 (initiator TLS handshake unbounded); placed in parent
  issue-batches.md (B124, B125, B48, B123). L-105-f's row names #714.
- [X] T121 `spec/behaviors-and-limitations.md` (live file only): retire L-103-5 (its QuickFIX statement is not carried forward
  as written) and move it to `spec/behaviors-and-limitations-closed.md` under that file's cut rule (only self-declared-resolved
  entries), marked resolved by 105. After T122, any amended L-row whose amendment leaves nothing live moves the same way.
  Done: L-103-5 moved to `spec/behaviors-and-limitations-closed.md` (comment header, status resolved (105); its
  QuickFIX statement replaced by `B-105-2`'s). After T122 no amended L-row is left with nothing live: L-093-1 (the
  stall), L-092-1 (wontfix), L-093-6 (garbles not counted) and L-103-6 stay.
- [X] T122 Amend in place, dated, naming 105: B-537-2 (the "transport stays open" clause and its silent grace-check ending,
  B-537-2(b)), B-537-3 (#582), L-093-1 and L-092-1 (R-1's Logout and the close; the "or leaves a TestRequest unanswered"
  clause removed), L-093-6 (the rejects are logged; garbles of a connection with no Session still not counted), B-093-4,
  B-092-2 and B-092-5 (the grace window is 2T), B-103-10, B-104-6 and B-103-13 (the direct driver's precondition naming
  `run_ending_logout_bound_` and the close frame; `Lifetime105.*` its cells).
  Done: each amended in place, dated 2026-11-24 and naming 105, with the old text recorded: B-537-2, B-537-3, L-093-1
  (status disclosed: #534's half closed), L-092-1, L-093-6, B-093-4, B-092-2, B-092-5, B-103-10, B-104-6, B-103-13; by
  research D11's plan also B-103-7 (`Lo1.*`) and L-103-6 (`TooHighLogoutState103.Lo3_*`); B-103-1's witness
  citation now `CapiVersion.CApiVersionIsExactly_1_18_0`.
- [X] T123 New rows: B-105-1 (the close rule by ending class) to B-105-7, B-105-7 copying research D11's text, not re-deriving it
  (BREAKING C-ABI 1.18, the observers of contract C-9; the C++ layout changes, `sizeof(Session)` changing as FR-013 removes
  members and D1, D2 and D5 add them; BREAKING C++ for `ReconnectFsm`, its constructor losing two parameters and `drive_logout`
  and the other caller-less stubs going, with D11's migration line: drop the two arguments, which were never read; the stubs
  have no replacement; the new installed implementation header `include/fixpp/session/detail/log_rate_gate.hpp`, with the
  status of `detail/replay_gate.hpp`), with B-105-2 carrying research D11's disclosures; L-105-b to
  L-105-h from contract C-11 (L-105-a was withdrawn in Gate A round 1 and is not used), L-105-f written as narrowed by ruling
  D-e and naming T120's issue and the round-1 text it supersedes. The spec's and C-10's departure tables copied, not
  re-derived. Each row cites its cells by name.
  Done: a "105 — transport liveness" section in the live file: the spec's and C-10's departure tables copied; B-105-1
  to B-105-8 (B-105-8 by research D11's plan; B-105-7 with the `engine.hpp` test-hook member
  `test_hook_listener_construction_fails_`; B-105-3 states the fresh window and its 3T cap, T119a and T119b);
  L-105-b to L-105-h from contract C-11, L-105-f naming #714, the round-1 text it supersedes and the uncapped hold
  with no TestRequest pending (T119b's escalation); a release note. Also (orchestrator rulings): contract C-7.1 and
  data-model E-6 (over-budget and over-limit first frames both report `wire_frame_too_large`), data-model E-7
  (`admit_log_record` takes the window's end).
- [X] T124 [P] `.specify/536-537-538-recovery-liveness.md`: an erratum at its end naming 105 (R-6 superseded by R-1; D-537-5's
  C-3 disclosure by FR-004).
  Done: "Erratum — 105-transport-liveness (2026-11-24)" at the note's end: R-6's "Not chosen: … a Logout on the
  dead-peer ending" by R-1; D-537-5's C-3 disclosure by FR-004.
- [X] T125 [P] Brain pages, each citing source (research D11): `brain/components/session.md` (R-6; the grace-check deferral to
  B27), `brain/components/session-liveness-and-reconnect.md` (the `ReconnectFsm` correction block; the thresholds as a pointer
  to `run_liveness_loop`), `brain/components/engine-accept-path.md` (the first frame through the pump; the reject records),
  `brain/components/graceful-logout.md` if T009 hit a rule of it. Each page's superseded-docs list names 105's supersessions
  of 005, 013, 092, 093, 103 and 104 by name.
  Done: `brain/components/session.md` (refs; the R-6 and grace-check items of the #537 bullet marked superseded or
  decided by 105; a "Transport liveness (105)" section with D1's and D2's rejected alternatives and the
  supersessions of 005, 013, 092, 093, 103 and 104 by name), `session-liveness-and-reconnect.md` (the correction
  block: the stubs are gone, with the re-derivation grep; the thresholds as a pointer to the helpers
  `run_liveness_loop` uses), `engine-accept-path.md` (the first frame through the pump; the reject records;
  `L-093-6`'s no-log-site claim superseded); `engine-accept-path.md`'s WRONG table and `session-liveness-and-reconnect.md`'s correction block name 105's supersessions of 005, 013, 092, 093, 103 and 104 by name, as `session.md`'s section does. `graceful-logout.md`: no T009 hit (the evidence file's T009 census
  names it only through a B&L line), not edited.
- [X] T126 [P] T-DOC-1 (research D12): a `brain/failure-classes.md` entry in that page's form (trigger and procedure, no
  instance, no PR) for `<windows.h>` macro identifiers, its text sourced from PARENT `.claude/agents/phase-implementer.md`
  section "Cross-platform — `<windows.h>` macros rewrite common identifiers on MSVC": the trigger (a new identifier in code an
  MSVC lane compiles whose spelling a platform header defines as a macro: `near`, `far`, `min`, `max`, `small`, `ERROR`, `IN`,
  `OUT`), the procedure (a descriptive name; `(std::max)(a, b)`; build the touched targets on MSVC before the first labelled
  push; a Linux-only green is no evidence for the class); and one `brain/log.md` line dated at the PR, naming the class.
  Done: `brain/failure-classes.md` class 25 (trigger, procedure, scanning heuristic, sibling; no instance, no PR) and
  one `brain/log.md` line dated 2026-11-24.
- [X] T127 Brain checks: `python3 tools/check_brain.py --self-test`, `python3 tools/check_brain.py gate` and
  `python3 tools/check_brain.py census` (and whatever `.github/workflows/brain-freshness.yml` runs, derived from it), then the
  wrapper PARENT `research/G19-fix-fpml-iso20022/tools/brain-external-sweep.sh` (never a bare `check_brain.py sweep`); record
  its execution count.
  Done: `check_brain.py --self-test` 11/11, `gate` rc 0 (a seeded missing ref in `session.md` reported, then
  removed), `census` rc 0; the brain-freshness.yml steps `brain_link_decisions.py --self-test` 5/5,
  `check_workflows.py --self-test` 7/7 and `.` OK; `tools/brain-external-sweep.sh` PASS (self-test ok, seeded break
  detected, 121 external refs resolve) (evidence file, Phase 10).
- [X] T128 Supersession close (FR-019): re-run T009's census at the head; every living-surface hit now states the new rule or
  names 105; every FR-019 pin of T033, T072 and T099 and the retired probes of T111 assert the new rule; T124 and T125 name
  every ruling of FR-019's list; `git grep -nE 'Phase 6 US4|not a close|unanswered_tr_' -- src include` reports no stale claim.
  Record the classed table.
  Added by T009 (living surfaces with no task): `tests/capi/inbound_frame_dispositions_c7_witness_test.cpp` (the Row 5
  refused-Logon cells and the "Rows 3 and 5 in Disconnected" cells, which state the refused session's transport stays open
  until the establishment deadline) and `tests/session/unparseable_frame_disposition_test.cpp` (the comments stating D-1,
  D-2 and D-3 are "a state transition, not a close").
  The T035 ruling re-pinned in Phase 3 the "Rows 3 and 5 in Disconnected" cells and the Row 5 refused-Logon cells of the
  first file (renamed `Row3_In<Disconnected|AnInitiatorsDisconnected>_TheRefusalClosesTheConnectionAtOnce`,
  `Row5_<AnAcceptorWhoseLogonWasRefused|AnInitiatorWhoseLogonReplyWasRefused>IsClosedAtTheRefusal`); this task re-checks
  them. Added by the T035 census widening (living surfaces with no task): `spec/coverage-index.md` row 13 (TC-009's "step 2's
  wait with the transport open", which C-1 row 5 now closes after the acknowledgement).

  Done: T009's census re-run at the head; the living-surface hits of the old claims classed (evidence file, Phase 10,
  T128): two rewritten (`unparseable_frame_disposition_test.cpp`'s D-1/D-2 and D-3 "not a close" comments now
  name `close()` and 105's connection close; `spec/coverage-index.md` row 13 amended), the rest correct as written
  or not this rule; the `git grep` over `src include` reports only lines naming 105 or not this rule.
---

## Phase 11: Polish, verification and mandatory close-out

- [X] T129 `/simplify` over the diff; any change re-runs `-L 105`.
  Done: the orchestrator's deduplicated list of the four reviewers' findings, every item applied, each
  behaviour-neutral, in one commit per item or group (evidence file, Phase 11, T129): one `counter_join` hold (the
  three join names aliases) and one `wake_clearer`; `FIXPP_JOIN_COUNTERS` for the post joins of
  `drain_for_reconnect_` and `close()` (a macro, so no cancellation check is added where no counter is held; still a
  spin, the wake left as escalated); `ending_write_unwind_guard` and one block for the two bad-789 Logouts;
  `begin_connection_`; `resolve_ending_wait_`; `test_request_threshold_in_range`; the in-sequence Heartbeat through
  `answer_pending_test_request_`; `from_active()` derived; `detail::FirstFrameIds`, `reject_pre_session` and
  `detail::log_rate_window_end`; the ending-response route decided in `on_inbound_ending_response_` (no second
  `check_inbound` await in the Active arm, the phase byte tested first); the liveness loop's `answerable_now` and its
  ordered give-up, TestRequest, Heartbeat chain; `spawn_connection_close_` taking the transport; the test-access
  phase alias; `session_engine_access::ended` removed. No cell added (Polish): the before and after readings, the
  gate and a reach mutant of the moved ending-response route stand in for RED.
- [X] T130 clang-format on touched files only (never `specs/`, never `include/fix/c_api/*.h`):
  `git diff --name-only --diff-filter=d origin/main...HEAD -- src include tests bench | grep -E '\.(cpp|hpp|h)$' | grep -vE '^include/fix/c_api/' | xargs clang-format --dry-run --Werror`.
  Done: the literal command reports only `session_config.hpp` and `read_first_frame_bounded.hpp`, whose `origin/main`
  copies fail with the same counts; `--lines` over this branch's hunks clean (evidence file, Phase 11, T130).
- [X] T131 clang-tidy, cppcheck (the local 2.22 on PATH) and IWYU on the touched `src/` and `include/` files (Article IX §4); a
  translation unit cppcheck abandons is not clean.
  Done: `e046f055` (the clang-tidy, cppcheck and IWYU findings new against `$B` on the five touched TUs, each fixed or
  dispositioned; seeded controls reported in every TU; evidence file, Phase 11, T131).
- [X] T132 Comment-claim lint: `PARENT/.claude/scripts/check-comment-claims.py --root PARENT/research/G19-fix-fpml-iso20022/library --base origin/main`;
  read added runtime strings and inventories by hand.
  Done: 0 findings at `96d0e648` and after `3c5390dc`; added strings read by recipe (evidence file, Phase 11, T132).
- [X] T133 Line-citation gate: `python3 tools/check_line_citations.py --range origin/main..HEAD` and
  `--shift-audit origin/main..HEAD` (the feature's own `specs/` bundle is charged by the range gate).
  Done: range gate OK, shift audit GATE PASSED (evidence file, Phase 11, T133).
- [X] T134 Article X §7 predicate at the PR head: `gh release list --exclude-drafts` on the fixpp repo; empty keeps the MINOR
  bump of T115; a non-empty listing stops and goes to the orchestrator. Record the result.
  Done: no published release (empty listing, `releases` length 0; known positive `-R cli/cli`); T115's MINOR bump
  stands (evidence file, Phase 11, T134).
- [X] T135 Every buildless Tier 1 step run locally, derived from `.github/workflows/tier1.yml`: `ci-script-pins` (all its steps,
  including `check_co_spawn_lambda.py --self-test` and its gate), `check-layers`, `co-spawn-closure-audit` (it applies: the new
  spawns of T027 and T059, against T016's base list), and `ci/pump-census.sh` against `ci/expected-pump-sites.txt`.
  Done: every step rc 0 after `3c5390dc` (the mock-clock staging sweep's KIND dispositions for 105's escalation rows;
  rc 2 before it, rc 0 on `$B`); the co-spawn audit and its cross-check clean (evidence file, Phase 11, T135).
- [ ] T136 linux-gcc-release `-Werror` build of the touched targets (`session_105_tests`, every re-pinned entry, the library,
  the capi and config tests, `engine_send_bench`).
- [ ] T137 Sanitizer lanes linux-clang-asan, -ubsan and -tsan: `-L 105` and `-L session` (every existing close, stop and
  reconnect entry included), and the touched `capi` and `config` entries (`EngineStopRacesEndingClose_OneClose`, `OnLogoutCallsClose_OneClose` and the TLS cells on TSan; `Lifetime105.*` on ASan); a
  report is a defect until disproven by a reproduction or a client-path analysis. The baseline is the base on the same
  entries (spec SC-006). A lane's run whose TLS skip count is not zero is not a reading of that lane (research D10,
  "Lanes"); record each lane's test count and skip count.
- [ ] T138 Benchmarks (Article VIII §2, §3; ruling D-f; contract C-12): add the pump case to `bench/session/engine_send_bench.cpp`'s
  harness (inbound Heartbeat and application frames through `run_read_pump`, the unarmed path), and a pending-TestRequest
  Heartbeat case to `bench/session/on_inbound_frame_bench.cpp` (a Heartbeat arriving while a TestRequest is pending: C-5.4's
  TestReqID classification runs only under that condition, and the existing Heartbeat case has none pending); then paired base-vs-candidate
  A-B-A-B runs of `on_inbound_frame_bench`, `session_send_bench` and `engine_send_bench` with the new case, the base a release
  tree of `$B` with the bench file overlaid, its path padded to `$T`'s length (`cmp` the `.text`); the +5 % budget; results in
  the verify evidence.
- [ ] T139 Interop (research R4): the live QuickFIX interop cells of `tests/interop` (`ci/run-interop-live.py`'s selection):
  peers now see a Logout before EOF on the give-up and an R-2 wait.
- [ ] T140 The WHOLE pytest suite: `python3 -m pytest bindings/python/tests` (the behaviour reaches Python through the C ABI).
- [ ] T141 **(orchestrator)** windows-msvc-debug build (`-k 0`) and run of the touched targets and `-L 105` in
  `/mnt/c/temp/fixpp`, after taking `.sandbox-lock` by PARENT `research/G19-fix-fpml-iso20022/msvc-local-build-procedure.md`
  Step 0; release the lock after; never at the same time as a Linux build. The TLS cells run there too (research D10,
  "Lanes"): a TLS cell that skips on MSVC is recorded with its reason and is not counted as a pass, and the run's test count
  and skip count are recorded.
- [ ] T142 One full `ctest --preset linux-clang-debug --no-tests=error` (Article XVII §7), after reading the sandbox lock; record
  the count and any failure, and the result of every `tests/alloc_guard` entry by name (`ctest -N -L alloc_guard` lists them),
  `alloc_guard_093_pump_active_read` (`tests/alloc_guard/test_093_pump_active_read_alloc_guard.cpp`, the pump's Active read
  path, which 105's latch test runs on) among them.
- [ ] T143 Coverage of added lines (Article IX §1): fresh per-binary profraw, from every binary the `105` label runs (`session_105_seam_tests` included: the
  allocation-failure catches, the listener's generic arm and the bound's failure exit are reached only there), for the
  `session`, `log` and `config` modules and every other module whose `src/` the diff adds a reachable line to (`src/capi`
  if a witness needs a change there; `include/fixpp/core/error.hpp` and `include/fix/c_api/*.h` change comments only and
  add no line to measure); pre-assigned, each with its rationale in the verify record: the queued-shape Logout before
  Active has no branch of its own (the shared write point, covered by the after-Active cells; research D10, "The write
  limit"), a `Disconnected`-over-`Disconnected` write is I-5's census row (data-model E-2), and the 2³² window wrap is
  reached by the window function's runtime twins (T086); every
  uncovered added line or branch tested, waived with a one-line rationale, or escalated to a filed, labelled issue, in
  `.specify/decisions/105-transport-liveness-verify.md`.
- [ ] T144 **(orchestrator)** `/speckit-verify`; writes `.specify/decisions/105-transport-liveness-verify.md`. Re-run T130 to T135
  as the last actions before any labelled push.
- [ ] T149 **(orchestrator)** The PR body declares C-ABI 1.18 **BREAKING** ([const §X.7]): the observers of contract C-9 and
  the `version.h` history entry, with the B-105-7 row. It is written before the gate labels are applied and before CI runs
  (an edited body re-queues the matrix), in the final sign-off order (body, labels, wait, push last).
- [ ] T145 Remove the temporary worktrees `$B` and `$M` (`git worktree remove`, then `git worktree prune`) once every RED reading,
  mutant and bench run is recorded; never `fixpp-parallel`, never the MSVC sandbox.

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [ ] T146 **Catalogue close-out**: read `spec/feature-catalogue.md` for every OFFICIAL row whose Feature column names 105 or
  whose subject is a 105 issue (`grep -nE '105-transport|#534|#535|#559|#582|#687'`, then a read of the session rows by
  subject); flip each feature-owned row to `done` with the PR and evidence, with its `spec/coverage-index.md` entry, or record
  "none owned" after the check. Rows this feature amends but does not own get a dated "105 amends this row" note naming the
  B-105 rows; their state does not change.
- [ ] T147 **Feature-completeness audit (MUST be the FINAL task)**: against the final head, (i) every row of this file is
  `[X]` or carries a waiver rationale; (ii) every FR and SC maps to a landed test and a landed implementation (the
  traceability table below, re-read cell by cell), every W-row and E-3 row maps to a landed cell (the design table
  below), and every contract clause maps to a landed cell or its named instrument (the contract table below); (iii) every feature-owned OFFICIAL catalogue row is `done` with its `coverage-index.md` entry. Record the verdict,
  100 % or fully waived, in `.specify/decisions/105-transport-liveness-verify.md` under `## Completeness`.

---

## Traceability: FR/SC → tasks

| Requirement | Cells | Implementation | Mutants / instrument |
|---|---|---|---|
| FR-001 (every ending closes) | T018–T024, T042, T051 | T026–T029 | T036–T038; census T006 |
| FR-002 (orderly close via `close_async`; D-e, D-g exceptions) | T024, T034, T054, T070 | T027, T062 | T039, T041, T079 |
| FR-003 (R-1 Logout, record before the write, close, slot 74) | T023, T051, T070 | T031, T068 | T037, T039, T077 |
| FR-004, FR-004a (R-2 wait, overlay, absorption) | T042–T050, T053, T114 | T057–T066 | T074–T076; census T007, T008 |
| FR-004b (write limit, spawn failure, failure exit) | T051–T055, T070 | T059–T062, T067 | T077–T079 |
| FR-005 (close after reply and reset; reply limited) | T019, T051 | T029, T059 | T036, T077 |
| FR-006 (acceptor first frame) | T021 | T029 | T036 |
| FR-007 (silent pre-Active refusal at once, no timeout event) | T021, T049 | T029, T030 | T036, T040, T076 |
| FR-008 (store work not cut) | T019, T051, T050 | T027 (`inbound_depth_`), T062 | T036, T078 |
| FR-009 (#582, the 112 rule) | T081, T082, T086 | T084, T085 | T088; fuzz run T148 |
| FR-010 (T, 2T, sleep set, HeartBtInt ≤ 0, precedence) | T089–T091, T097 | T095 | T101 |
| FR-011 (validation) | T092, T093 | T096 | T101 |
| FR-012 (comments and surfaces) | — | T032, T098 | T128, T132 |
| FR-013 (dead state, size pins) | T108 (instrument) | T109–T112 | T113 |
| FR-014, FR-015 (records) | T102, T106 | T104, T105 | T107 |
| FR-016 (B&L) | — | T120–T123 | — |
| FR-017 (RED first, peer EOF, discriminating bounds) | every RED task, T025, T056, T083, T094, T103 | — | every mutant task |
| FR-018 (superseded comments) | — | T032, T071, T098, T111 | T128, T132 |
| FR-019 (supersession) | T033, T072, T099, T111 | T124, T125, T128 | census T009 |
| SC-001 | Ending-site table, T018–T024, T042–T055 | T026–T031, T057–T069 | T036–T041, T074–T080 |
| SC-002 | T089–T091 | T095 | T101 |
| SC-003 | T081 | T084 | T088 |
| SC-004 | T102 | T105 | T107 |
| SC-005 | T108 | T109, T110 | T113 |
| SC-006 | T137, T142 | — | — |
| Article VIII §3 (ruling D-f) | T138 | T138 (pump case) | census T014 |
| C-ABI 1.18 (D9) | T114 | T115–T118, T149 (PR body) | T012, T134 |
| Article VII §7 (the 112 parse fuzzed) | T086 | T085 | T148 |
| T-DOC-1 (D12) | — | T126 | T127 |

## Traceability: design elements → tasks

| Element | Cells | Implementation |
|---|---|---|
| W-1 (the peer's Logout, any MsgSeqNum; before Active, no reset) | T042, T049 | T066 |
| W-2 to W-5 (frames at NextNumIn) | T044, T049 (drained) | T066 (overlay), T060 |
| W-6, W-7 (above NextNumIn; gap deadline passed) | T044 | T064 |
| W-8 (absorbed by cause) | T045 | T063, T064 |
| W-9 (internal failure ends the wait; the bound's own failure) | T046, T053, T070 | T065, T061 |
| W-10, W-11 (send refused; liveness originates nothing) | T047 | T068 |
| W-12 (the bound; before Active capped) | T043, T049, T050 | T061, T062 |
| W-13, W-14 (graceful, terminal, stop, drain) | T048, T054 | T069 |
| W-15 (the Logout's own write cut at the bound) | T051, T052 | T059–T062 |
| E-3 rows 1, 2, 2a (enter `writing`; timeout 0; bound spawn fails) | T051, T048, T070 | T059 |
| E-3 row 3 (leaving `writing`; the wake on both branches) | T051, T052 | T060 |
| E-3 row 4 (write limit; `cut`) | T051, T052, T070 | T061, T062 |
| E-3 rows 5, 5a, 5b, 5c (closers and other roots during `writing`) | T048, T051, T055 | T067, T069 |
| E-3 rows 6, 9, 11 (before Active) | T049 | T060, T065, T066 |
| E-3 rows 7, 8, 10, 12 (after Active) | T045, T042, T043, T050, T046 | T063–T066, T061 |
| E-3 rows 13, 14, 15 (under `close()`, stop, drain) | T048, T054 | T069 |
| E-3 row 16 (failure exit, per phase) | T053, T070 | T061 |
| E-3 row 17 (next connection) | T020, T082 | T069, T085 |
| E-2 latch, setter, close frame, joins, I-4 | T018–T022, T034, T055 | T026–T029, T069 |
| E-4 sleep set, `liveness_may_originate_`, 112 window | T047, T082, T086, T089, T090 | T068, T085, T095 |
| E-5 `expire()` and the establishment hand-off | T021, T049 | T030, T058 |
| E-6, E-7, E-8 (records, gate) | T023, T102, T106 | T031, T104, T105 |
| E-9 (`ReconnectFsm`) | T108 | T109 |
| D-f benches (paired, plus the pump case) | — | T138 |
| Follow-ups before Gate B | — | T120 |

## Traceability: contract clauses → tasks

W-rows are in the design table above. "Statement" marks a clause no cell can observe; its instrument is named.

| Clause | Cells | Implementation / instrument |
|---|---|---|
| C-1 rows 1, 11 (silent endings after and before Active) | T018, T021 | T026, T029 |
| C-1 row 2 (an arm returns an error) | unchanged from `main`; W-9's error dispositions T046 | T029 (`!r` kept first) |
| C-1 rows 3, 5 (the peer's Logout; the too-high acknowledgement) | T019, T051 (`Reply`) | T029, T059 |
| C-1 rows 4, 13, 14 (class X) | T022 | T026 (the latch's `lifecycle` and drain terms) |
| C-1 rows 6, 12 (ending Logouts: C-2) | T042–T050 | T057–T066 |
| C-1 row 7 (R-1) | T023, T051, T070 | T031 |
| C-1 rows 8, 9, 10 (detached roots, application calls) | T020 | T027 |
| C-1.a (one effective close) | T022 | T026–T028; census row I-5 (T006) |
| C-1.b (store work not cut) | T019, T050, T051 | T027, T062 |
| C-1.c (Engine terminal; direct Session) | T020, T046, T048 | T027, T029 |
| C-1.d (`onLogout` once) | T043 | T060, T061 |
| C-2 "Which Logouts", "No wait is armed" | T042, T048, T049, T051, T052, T070 | T059, T060, T063 |
| C-2.1 (Active through the wait) | T042, T047, T114 | T063, T066 |
| C-2.2 (the peer's numbers) | T042, T044 | T066 |
| C-2.3, C-2.4 (numbering; nothing originated) | T047 | T068 |
| C-2.5 (one budget from the write's start) | T043, T051 | T059, T061 |
| C-2.6 (before Active; the establishment cap) | T049 | T058, T061 |
| C-2.7 (the write limit) | T051–T054, T070 | T059–T062, T067; E-3 row 5b (an arm's error close cuts a Logout in flight, as on `main`): no new behaviour, no cell, disclosed in L-105-f (T123) |
| C-2.8 (the pending too-high mark) | T048 | T063 |
| C-2.9 (the peer's Logout under the other checks) | W-1's cells (T042, T049) | T066 (the response test ahead of the PossDup arm; the 35=5 exemptions are `main`'s) |
| C-3.1, C-3.2, C-3.3 (R-1) | T023, T051, T070, T089 | T031, T068, T095 |
| C-3.4 (an answering or earlier 112) | T081, T082, T090 | T085 |
| C-4.1 to C-4.5 | T021 | T029, T030 |
| C-5.1 to C-5.3 (#582) | T081 | T084 |
| C-5.4 (the 112 rule) | T082, T086 | T085; fuzz run T148 |
| C-6.1 to C-6.5 (timing) | T089–T091, T097 | T095 |
| C-6.6 (validation) | T092, T093 | T096 |
| C-6.7 (no C-ABI setter, no Python surface) | statement | T118 (research D6's grep, seeded first) |
| C-7.1 to C-7.3 (records) | T102, T106 | T104, T105 |
| C-7.4 (no counter; garbles of a refused connection not counted) | statement | T105 adds none; L-093-6 amended in T122 |
| C-8.1, C-8.2 (through `close_async`) | T024 | T027; mutant T039 |
| C-8.3 (a `close()` waits for a close in flight) | T022 (`EngineStopRacesEndingClose_OneClose`), T055 (`Lifetime105.CloseFrame_SessionDestroyedAfterClose`) | T028, T069; mutant T078 |
| C-8.4 (graceful `close()` during the wait) | T054 | T061, T069 |
| C-8.5 (the write limit on TLS; L-105-g) | T054 | T062 |
| C-8.6 (the synchronous fallback; L-105-h) | T034, T070 | T027, T062 |
| C-8.7 (the outcome by row) | the cells of C-8.1 to C-8.6 | statement |
| C-9 (surfaces) | T114 | T104, T109, T115–T118 |
| C-10 (departures) | T023 (R-1), T042 (the response), T047 (send, liveness), T049 (pre-Active), T051 (write limit), T082 (112) | T123 (copied, not re-derived) |
| C-11 (L-105-b to L-105-h) | L-105-b T046, T048; L-105-c T023; L-105-d T102; L-105-e T047; L-105-f T051 (`TimeoutZero_WriteUnbounded`); L-105-g T054; L-105-h T034, T070 | T123 |
| C-12 (benchmarks) | — | T014, T138 |

## Dependencies and execution order

- **Before Phase 1 (orchestrator)**: `/speckit-analyze`, `/speckit-checklist` refresh of `checklists/requirements.md` (its items that
  waited for regenerated tasks) and `/speckit-checklist-audit` (`.specify/pipeline.md`); the audit blocks implementation.
- **Before T018 (owner)**: the user `/plan` sign-off ([const §X.6], the fourth Appendix A control), requested from the owner
  2026-11-21. It is a hard precondition of Phase 3 (US1 part A): no cell of T018 onward is written until it is recorded.
  Not given yet. It counts only as the owner's own statement, recorded in the evidence file with its date, its words and
  the bundle head it was given on (plan "Open items carried to the orchestrator" states its scope and when a later edit
  needs a fresh one).
- **After the checklist audit (orchestrator)**: if `/speckit-checklist-audit` SPEC-FIXED any item, `/speckit-analyze`
  is re-run before Phase 1 (`.specify/pipeline.md` step 9); that re-run is part of Article X §6's `/analyze` control.
- **Phases**: 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10 → 11. US1 to US3 all edit `src/session/session.cpp` and run in order,
  never in parallel.
- **US1's close mechanism is foundational.** T026 to T029 (the latch, `release_connection_end_`, `close_transport_`, the pump's
  consumer) are what every later close cell observes: US2's mismatch close (T081, T082), US3's give-up (`run_liveness_loop`'s
  R-1 exit, which T031 and T068 shape and T095 re-times) and the C-ABI cells (T114). US2 starts after T073; US3 after T087.
  T148 (the fuzz run) after T087, before T144.
  US3 moves the liveness instants, so every earlier cell that reaches one sets `test_request_threshold = HeartBtInt` and is
  driven by the wire reader (Execution rules).
- **Within US1**: T018–T024 → T025 → T026 → T027 → T028 → T029 → T030 → T031 → T032 → T033 → T034 → T035 → T036–T041; then
  T042–T055 → T056 → T057 → T058 → T059 → T060 → T061 → T062 → T063 → T064 → T065 → T066 → T067 → T068 → T069 → T070 → T071
  → T072 → T073 → T074–T080. The R-2-class pins are RED between T035 and T072 by design.
- **US4** depends on T015 only; **US5** on T010 and T013 only; both run after US3 because they share review order and the
  build, not code.
- **Phase 9** after US1 to US3 (its witnesses observe their rules). **Phase 10**: T120 before T123; T128 after T121–T127.
  **Phase 11**: T144 after T129–T143; T149 after T144 and before the gate labels; T145 before T146; **T146 → T147 last**.

### Parallel opportunities

- Phase 2: T006 to T017 are read-only and run in parallel.
- US1 part A: T018 (`test_105_connection_close.cpp`), T023 (`test_105_liveness_timing.cpp`) and T024 (`test_105_tls_close.cpp`);
  the other cells of each file follow their file's first task.
- US1 part B: T042 (`test_105_logout_wait.cpp`), T051 (`test_105_write_limit.cpp`) and T054 (`test_105_tls_close.cpp`).
- US2: T081 is alone in its file. US3: T089 and T092 (different files). US4: T102. Phase 9: T114.
- Phase 10: T124, T125 and T126 (different files); T121 to T123 share the B&L file and run in order.
- Phase 11: the read-only checks T132 to T134 in parallel; no two builds ever run at once.

### Parallel examples

```bash
# Phase 2 (read-only censuses):
Task: "T006 ending-site census and table"
Task: "T009 supersession census with its three known positives"
Task: "T012 C-ABI minor-pin census and the 103 bump's file list"
Task: "T017 establishment-cap condition re-derived in source"

# US1 part A RED cells (three files):
Task: "T018 PumpSilentEnding cells in tests/session/test_105_connection_close.cpp"
Task: "T023 R-1 give-up cells in tests/session/test_105_liveness_timing.cpp"
Task: "T024 real-TLS close_notify cells in tests/session/test_105_tls_close.cpp"

# US1 part B RED cells (three files):
Task: "T042 W-1 cells in tests/session/test_105_logout_wait.cpp"
Task: "T051 write-limit cells in tests/session/test_105_write_limit.cpp"
Task: "T054 TLS graceful-close and abortive write-limit cells in tests/session/test_105_tls_close.cpp"

# US2 / US3 / US4 (one file each):
Task: "T081 knob-off cells in tests/session/test_105_heartbeat_knob_off.cpp"
Task: "T089 default-timing cells in tests/session/test_105_liveness_timing.cpp"
Task: "T092 TOML threshold cells in tests/config/test_load_negative_battery.cpp"
Task: "T102 pre-session record cells in tests/session/test_105_presession_log.cpp"

# Phase 10 records:
Task: "T124 erratum in .specify/536-537-538-recovery-liveness.md"
Task: "T125 brain component pages"
Task: "T126 T-DOC-1 failure-classes entry and log line"
```

## Implementation strategy

- **MVP = US1** (Phases 1 to 4): every ending closes its connection, the ending Logout is limited, and the R-2 wait holds.
  It is the only peer-reachable defect of the batch (a held connection and acceptor slot). Part A alone (T018 to T041) is
  an intermediate checkpoint, not a shippable state: R-2-class endings close at once there, against R-2.
- **Then** US2 and US3 (P2), US4 and US5 (P3), the C-ABI surface, the records and the polish, in that order. One PR; no story
  ships alone.
- **Who executes.** A `phase-implementer` agent runs the implementer's tasks, one phase per invocation, each ending at its
  commit (`H1` to `H7`); the orchestrator runs the tasks marked (orchestrator), reviews every RED reading and mutant table
  before the next phase, and holds the MSVC sandbox lock.
- **Before any labelled push**: T130 to T135 re-run last, then the final sign-off order (body, labels, wait, push last).
