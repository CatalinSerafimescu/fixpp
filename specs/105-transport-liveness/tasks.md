---
description: "Task list for 105-transport-liveness"
---

# Tasks: Transport and liveness — a session that ends closes its connection; TestRequest timing and pre-session rejects

> **Non-operative (Gate A round 1, 2026-11-20):** generated before Gate A; to be regenerated from the corrected bundle after Gate A converges, without carrying these task numbers forward.

**Input**: `specs/105-transport-liveness/`: spec.md (US1 to US5, FR-001 to FR-019, SC-001 to SC-006, Clarifications and owner rulings R-1 to R-4, Q1, Q2 and the 2026-11-20 rulings on OQ-1, SI-1, SI-2, L-105-b), plan.md ("Phases (for /speckit-tasks)", Project Structure, T-DOC-1), research.md (F-1 to F-10, D1 to D12, R1 to R4, "Owner rulings"), data-model.md (E-1 to E-9), contracts/transport-liveness-105.md (C-1 to C-11), quickstart.md. Where this file and the bundle differ, the bundle wins and the difference goes to the orchestrator.

**Preconditions not yet on record** (orchestrator): plan.md's Constitution Check schedules Gate A before `/speckit-tasks`; no `105-transport-liveness-gatea.md` exists under `.specify/decisions/` at the time of writing. `.specify/feature.json` `pinned_at` is a 105 spec commit, not the base; the base for every RED reading is `52aeed4e` (spec "Base", `git merge-base HEAD origin/main`).

**Tests**: required (`[const §VII.3]`, FR-017). In every story phase the RED cells come first as their own tasks, then the mechanism, then the mutant and spurious-hit arms.

## Format: `[ID] [P?] [Story] Description`

`[P]`: a different file from every other open task of its phase and no dependency on an incomplete task (appending only its own section of the evidence file does not count as a shared file). `[US1]` to `[US5]`: story phases only. Paths are relative to the library root unless marked PARENT (`/home/catalin/Work/Programming/Antreprenoriat/`). A task marked **(orchestrator)** is run or requested by the orchestrator; every other task is the implementer's (`[const §XVI.6]`).

## Execution rules (apply to every task)

- **Trees.** `$T` = the main checkout `PARENT/research/G19-fix-fpml-iso20022/library`, branch `105-transport-liveness`. `$B` = `/mnt/wsl/fixppbuild/fixpp-105-base`, a detached worktree at `52aeed4e` (RED readings only). `$M` = `/mnt/wsl/fixppbuild/fixpp-105-mutants`, detached (mutants, census scratch edits, known-positive seeds only). Never `git checkout`/`git switch` in `$T`, never `git stash`, never `--amend`. `fixpp-parallel` and the MSVC sandbox are never `$B` or `$M`.
- **Builds.** One build or ctest process at a time on the machine, under `flock /tmp/fixpp-build.lock`; a long one detached (`setsid nohup … ; touch <marker>.done`) with one background waiter on the marker, never a foreground poll, never `pgrep -f`. `$B`/`$M` and every sanitizer, release and bench tree live on `/mnt/wsl/fixppbuild` (`export CCACHE_DIR=/mnt/wsl/fixppbuild/ccache`). `$T`'s `build/linux-clang-debug` is built incrementally only. Read `/mnt/c/temp/fixpp/.sandbox-lock` before any full ctest and before any MSVC run; a lock held by another owner means ask the owner.
- **RED on the base.** Cells are committed in `$T` (commit `C`), overlaid on `$B` with `git -C "$B" restore --source=C -- <cell files> tests/session/CMakeLists.txt`, built and run there, and must fail by an assertion for the reason the task states. A compile failure, abort, crash, hang or ctest timeout is not RED. A cell expected RED that reads GREEN on the base stops its task; the reading goes to the orchestrator. A cell the task marks *pin* is read GREEN on the base and says why.
- **"At once" cells (SC-001).** The mock clock is held still, and the executor is pumped with the readiness-checked helpers of `tests/support/pump_until_ready.hpp` until the peer's read ends or a wall cap passes. The cap fails as an assertion. Each such cell names its competing real-time timers (the TLS handshake timeout, `read_first_frame_bounded`'s deadline) and either configures them longer than the cap or sets the cap below them.
- **Bounds from the competing timeout (FR-017).** Every bounded close is asserted on both sides of its bound on the mock clock (bound − 1 ms open, bound EOF), and every cell has a spurious-hit arm: the mechanism under test deleted in `$M` turns it RED.
- **Selection.** `ctest --preset <p> -L 105 --no-tests=error --output-on-failure`; one cell by the binary's `--gtest_filter='*<Suite>.<Cell>*'`. Every run records its reported test count; 0 fails the step. Never `ctest -R <cell>`.
- **Mutant protocol.** At commit `H` holding the mechanism: `$M` at `H` with a clean status; apply only the named edit; build; run the named cells and then the `105` label unfiltered; record RED per cell; revert with `git -C "$M" restore . && git -C "$M" clean -fd`. A mutant applies to a fresh binary (`strings`-check a marker).
- **Evidence.** Census outputs, RED readings and mutant tables go to `.specify/decisions/105-transport-liveness-evidence.md`, never into a code comment. Cite symbols, never line numbers.

---

## Phase 1: Setup (no production code)

- [ ] T001 Check `git -C $T rev-parse --abbrev-ref HEAD` is `105-transport-liveness`; create `$B` and `$M` with `git worktree add --detach` on `/mnt/wsl/fixppbuild` (`df -h /mnt/wsl/fixppbuild` first); run `codegraph sync` in `$T` and confirm `codegraph status --json` names `$T` with a nonzero `fileCount`.
- [ ] T002 Create `.specify/decisions/105-transport-liveness-evidence.md` with one section per census (T005 to T014), per RED reading and per mutant task.
- [ ] T003 Register ctest `session_105_tests` (label `105`) in `tests/session/CMakeLists.txt` after `add_session_104_bucket`'s shape, over `tests/session/test_105_connection_close.cpp`, `test_105_logout_wait.cpp`, `test_105_liveness_timing.cpp`, `test_105_heartbeat_knob_off.cpp`, `test_105_presession_log.cpp`, `test_105_tls_close.cpp`. Find every list a new entry must join with `git grep -nE 'session_104_tests|add_session_104_bucket' -- ci tools .github tests` (known positive: the CMakeLists entry) and read `ci/pump-census.sh` against `ci/expected-pump-sites.txt`: the new files join its population, so they use readiness-checked pumps, never `run_for(W); restart(); get()`. Before putting `test_105_tls_close.cpp` in the bucket, copy the registration form of an existing user of `loopback_tls_session_harness.hpp` (`git grep -l loopback_tls_session_harness tests` and its CMake entry): bucket or standalone, and whether it is listed in `ci/expected-preset-conditional-tests.txt`.
- [ ] T004 Write `tests/session/test_105_support.hpp`: a peer-side EOF / close_notify observer on `engine_loopback_harness.hpp`, `plain_engine_rig.hpp` and `loopback_tls_session_harness.hpp`; a frozen-clock "pump until the peer's read ends or the cap" helper whose cap fails as an assertion; a capturing log sink; a wire reader that advances the mock clock until a named MsgType is read (so cells are not tied to the TestRequest instant, which US3 moves), with a mock-clock cap that fails as an assertion; each cell names its cap, set above the latest instant any phase of this feature sends the awaited frame (above 2.4 × HeartBtInt plus the guard for the give-up; above 1.2 × HeartBtInt per cycle for US2), so a RED on the base terminates; an `INSTANTIATE_TEST_SUITE_P` over `session_role` as 537's cells use. No production symbol is read, so every cell compiles on `$B`.

---

## Phase 2: Foundational — censuses (read-only; plan phase 1; quickstart section 1)

**Purpose**: fix the populations every story asserts over. Each census is run first on a seeded or known positive.

- [ ] T005 [P] Ending-site census (research D1, "The census, by execution context"): run its write-site grep and root grep over `src/session/session.cpp` and `src/session/engine.cpp`, walk each site with `codegraph_callers` (`projectPath` = `$T`) to every root, and regenerate the **Ending-site table** below (one row per site, roots, class P/D/A/W/X of data-model E-1, cell task). The walk must reproduce the three known positives (the unanswered-TestRequest write under `run_liveness_loop`; Arm B of `on_inbound_active_out_of_sequence_` under the pump; `send_body_`'s failure write under the send root). A site with no cell and no named exemption fails SC-001.
- [ ] T006 [P] R-2 caller census (research D2 item 1): run its grep; class every hit "initiates" or "replies"; `resend_gap_deadline_` must read "initiates", `on_inbound_active_logout_` "replies". Also run the receipt-arm grep of D2 item 4 (known positive `on_inbound_active_logout_`).
- [ ] T007 [P] Supersession census (FR-019, research D11): run D11's `git grep` over `specs/ .specify/ brain/ spec/ tests/ include/ src/ ci/`; it must report the three known positives (`tests/session/test_537_gap_deadline.cpp` "fixpp#534", `include/fixpp/core/error.hpp` "1×HeartBtInt", `src/session/session.cpp` "Phase 6 US4"). Class each hit: merged bundle (superseded by name, never edited), living surface (rewritten by a named task below), or not this rule. Every FR-019 known-positive pin must appear; a living-surface hit with no task is added to T080.
- [ ] T008 [P] Size-pin census (research D7): the narrow `sizeof\((Session|ReconnectFsm)` grep over `src include tests ci tools bench bindings`, then the wide grep that must report `tests/dictionary/ref_shape_test.cpp`'s `FieldRef` pin (instrument proof).
- [ ] T009 [P] Moved-timing census (research D10): the TestRequest grep over `tests/session tests/capi tests/conformance tests/interop`, read for mock-clock advances across HeartBtInt that expect a TestRequest or a give-up; list each cell for T057.
- [ ] T010 [P] C-ABI minor-pin census (research D9): `git grep -nE 'MINOR 17|1\.17' -- include tests bindings tools ci docs`, classed "pins the minor" (moved in T072) or "history" (unchanged); then the 103 bump itself: `git log --format=%h -S'FIXPP_C_ABI_VERSION_MINOR 17' -- include/fix/c_api/version.h` and `git show --name-only` on that commit, every file it touched (docs, changelog, bindings, tests) being a row. Known positives: `include/fix/c_api/version.h` (`FIXPP_C_ABI_VERSION_MINOR`) and `tests/capi/version_test.cpp`.
- [ ] T011 [P] `ReconnectFsm` census (research D7, SI-1): the stub caller grep (known positive: `drive_logout`'s test call) and the constructor call-site grep (known positive `tests/session/test_logout_timeout.cpp`).
- [ ] T012 [P] Bench census (research D10, Article VIII §3): `grep -rln 'Engine\b\|run_read_pump' bench`; read `bench/threading/bench_threading.cpp` and `bench/session/engine_send_bench.cpp`; record which bench drives `run_read_pump`, or that none does.
- [ ] T013 [P] Pre-session reject census (data-model E-6): every `(void)transport->close(); continue;` in `run_accept_loop` before `open()` maps to one row of E-6, plus the listener `catch (...)`; a branch with no row is a table defect for the orchestrator.
- [ ] T014 [P] `co_spawn` census: `grep -n 'co_spawn' src/session/session.cpp src/session/engine.cpp` at the base, kept for T107's audit of the new spawns.

**Checkpoint**: the Ending-site table is filled; every population has its known positive.

### Ending-site table (seed; T005 regenerates it at the implementation head by D1's recipe)

Function names only; a function may hold several writes and reach several roots. "multi" = reached from several roots; T005 splits it into one row per root.

| Write site (function) | Root(s) | Class | Cell task |
|---|---|---|---|
| `run_liveness_loop` | liveness loop | D (R-1: not W) | T019, T020 |
| `run_peer_logout_bound_` | too-high Logout bound | D | T018 |
| `resend_gap_deadline_` | pump | W | T038 |
| `on_inbound_active_out_of_sequence_` (Arm B; #582 mismatch) | pump | W; P | T038; T050 |
| `on_inbound_active_sending_time_reject_`, `on_inbound_active_poss_dup_` | pump | W | T038 |
| `on_inbound_active_`, `answer_pending_test_request_` | pump | P | T016 |
| `on_inbound_active_logout_`, `acknowledge_peer_logout_`, `inbound_logout_leaf_` | pump; bound; liveness | P / D | T017, T018, T019 |
| `dispose_unparseable_` (D-1, D-2, D-3) | pump | P | T016, T021 |
| `on_inbound_not_connected_`, `on_inbound_logon_sent_`, `refuse_logon_with_logout_`, `inbound_logon_sent_logout_leaf_` | pump (first frame included) | P; W | T021, T022, T040 |
| `inbound_acceptor_reply_logon_leaf_`, `inbound_heartbeat_reply_leaf_`, `inbound_resend_request_leaf_`, `inbound_reject_leaf_`, `inbound_reject_best_effort_leaf_`, `inbound_business_reject_leaf_`, `answer_resend_request_`, `apply_inbound_sequence_reset`, `consume_rejected_seqnum_`, `persist_inbound_advance_`, `ensure_hydrated_` | multi | T005 | T005 |
| `store_then_emit`, `emit_session_reject_`, `disconnect_unless_superseded_` | multi | T005 | T005 |
| `send_body_` | send root | A | T023 |
| `emit_initiator_logon_` | `open`, `drive_reconnect`, connect loop | A | T023 |
| `on_inbound_logout_sent_`, `run_logout_phase1`, `close` | `close()` | X | T024 |
| `drain_for_reconnect_` | `drive_reconnect` | X | T024 |

---

## Phase 3: User Story 1, part 1 — every ending closes its connection (Priority: P1) 🎯 MVP (D1, D3, D4)

**Goal**: FR-001 to FR-003 and FR-005 to FR-008: every Engine-managed ending closes the transport through
`close_async()`, and the unanswered TestRequest sends its Logout with Text and closes.

**Independent Test**: per ending class, the peer observes EOF within the class's bound with no further inbound frame and no local API call (SC-001).

### RED cells for US1 part 1 (write first; RED on the base `52aeed4e` before the fix)

- [ ] T015 [US1] Commit point: the cells T016 to T025 are written in their files and committed (`C1`) before T026.
- [ ] T016 [P] [US1] `tests/session/test_105_connection_close.cpp`, `ConnectionClose105.PumpSilentEnding_<site>_PeerSeesEofAtOnce`, both roles, one cell per class-P silent ending after Active of the Ending-site table (TestReqID mismatch, BeginString guard, CompID guard, MsgSeqNum 0, D-3, a too-low frame whose Logout does not fit): mock clock frozen, asserts the peer reads nothing more and then EOF; no event. RED on the base `52aeed4e` before the fix: the transport stays open.
- [ ] T017 [US1] Same file, `PeerLogoutInActive_EofAfterReplyAndReset` (C-1 row 3, FR-005), both roles, with `reset_on_logout` on: the peer reads the reply Logout, then EOF; the store reads (1, 1) before the EOF is observed; `onLogout` fired once. RED on the base `52aeed4e` before the fix: no EOF.
- [ ] T018 [US1] Same file, `TooHighLogoutAcknowledged_EofAfterReset` (C-1 row 5; the peer-Logout bound's route and the pump's at-once route): reply Logout, reset unit complete, then EOF. RED on the base `52aeed4e` before the fix.
- [ ] T019 [P] [US1] `tests/session/test_105_liveness_timing.cpp`, R-1 cells (C-3), both roles, driven past the give-up by T004's wire reader (not by a fixed instant): `GiveUp_LogoutTextPinned_ThenEof` (exactly one 35=5 with `58=TestRequest unanswered, TestReqID <id>`, `<id>` the 112 of the TestRequest read, then EOF with the clock frozen and no Logout sent by the peer); `GiveUp_OneLogRecordWithSlot74` (capturing sink: one warn record of C-3.3's format with `error` = the value of `error::session_test_request_unanswered`). RED on the base `52aeed4e` before the fix: no Logout, no record, no EOF.
- [ ] T020 [US1] Same file, `GiveUp_LogoutDoesNotFit_StillEof`, `GiveUp_ToAdminThrows_StillEof`, `GiveUp_StoreFails_StillEof` (C-3.2), and `ConnectionClose105.Liveness<ToAdminThrows|AssignFails|StoreFails>_EofAtOnce` in `test_105_connection_close.cpp` (C-1 row 8, on a Heartbeat or TestRequest emission): EOF after the loop exits. RED on the base `52aeed4e` before the fix.
- [ ] T021 [US1] `test_105_connection_close.cpp`, `PreActiveRefusal_<D-1|D-2|D-3|NotConnected|LogonSent|IdGuard|SeqNum0>_EofBeforeDeadlineNoTimeoutEvent` (C-4.1, C-4.2, FR-007), both roles where the refusal exists for the role: a short `logon_timeout_ms`, clock frozen; EOF, and no `session_event_establishment_timeout` and no `session not established within logon_timeout_ms` record. Plus the *pin* `SilentPeer_StillClosedAtEstablishmentDeadlineWithEvent` (C-4.3, B-093-4; GREEN on the base). RED on the base `52aeed4e` before the fix: closed only at the deadline, with the event.
- [ ] T022 [US1] Same file, acceptor first frame (C-4.4, C-4.5, FR-006): `AcceptorFirstFrameError_EofAtOnce`, `AcceptorFirstFrameRefused_EofAtOnceNoTimeoutEvent`, `AcceptorFirstFrameAcceptedLaterFrameRefused_EofAtOnce`; the TLS handshake and first-frame deadlines configured longer than the cap (named in the cell). RED on the base `52aeed4e` before the fix (the first frame's result is discarded).
- [ ] T023 [US1] Same file, application calls (C-1 rows 9, 10; C-1.c): `SendStoreFailure_EofAfterSendReturns`, `DirectInitiatorLogonEmitFails_EofAfterOpenReturns`, `DirectInitiatorLogonEmitFails_EofAfterDriveReconnectReturns`, `DirectInitiator_EndingThenDriveReconnect_Reconnects` (the transport closed, the Session `Disconnected` and not closed, `drive_reconnect` installs a new transport that logs on). RED on the base `52aeed4e` before the fix (no EOF; the reconnect cell's EOF half).
- [ ] T024 [US1] Same file, one close (C-1.a, C-1.b, class X): `OnLogoutCallsClose_OneClose`, `EngineStopRacesEndingClose_OneClose` (run on the TSan lane by T109), `InsideClose_NoSecondClose`, `InsideReconnectDrain_DrainKeepsItsBound` (B-103-9's bound). The first two are RED on the base `52aeed4e` before the fix only in their EOF-timing half; the last two are *pins* (GREEN on the base), recorded as such.
- [ ] T025 [P] [US1] `tests/session/test_105_tls_close.cpp` on `loopback_tls_session_harness.hpp`, the real-TLS cell (C-8, FR-002): `TlsClose105.GiveUpWhilePumpParkedInRead_PeerReceivesCloseNotify` — a detached-root ending (the R-1 give-up) while the pump is parked in a TLS read; the peer reads the Logout, then a close_notify, then EOF; and `TlsClose105.PumpSilentEnding_PeerReceivesCloseNotify`. The TLS handshake timeout is configured longer than the cap. RED on the base `52aeed4e` before the fix.
- [ ] T026 [US1] Read T016 to T025 on `$B` (Execution rules "RED on the base"); record per cell its RED reason or *pin* status, and the test count.

### Implementation for US1 part 1

- [ ] T027 [US1] D1 items 1 and 6 in `include/fixpp/session/session.hpp` and `src/session/session.cpp`: the latch `connection_end_owed_`, `reconnect_drain_in_flight_` (held only around `drain_for_reconnect_`'s call in `drive_reconnect`), `connection_close_counter_`; set at the `Disconnected` edge in `record_state_transition_` under data-model E-2's condition, before `onLogout`; one store, no suspension, no spawn (F-1). `close()` clears it at `closing` and joins the counter by post before its own `close_async()`; `drain_for_reconnect_` clears and joins; `~Session` asserts the counter is zero.
- [ ] T028 [US1] D1 items 4 and 5 in `src/session/session.cpp`: `release_connection_end_()` (acts only when `inbound_depth_ == 0`; spawns `close_connection_(conn_gen_)` on `exec_.underlying()`, counter incremented before the spawn; a throwing spawn leaves the latch set) and `close_connection_` (`close_async()` only, under `state_ == lifecycle::open && conn_gen_ == gen`, on its own chain, F-6). Call it at the exit of every class D and A root of research D1 item 7.
- [ ] T029 [US1] D1 items 2 and 3 in `src/session/engine.cpp` and `src/session/session_engine_access.hpp`: `take_connection_end` and `ended`; `run_read_pump` tests the latch after every `on_inbound_frame` return and runs `stop_pump()`; `run_read_pump` gains `first_frame`, delivered before the surplus without the `expired()` pre-check; `run_accept_loop` step 8 deleted; `run_connect_loop` passes an empty span.
- [ ] T030 [US1] D4 in `src/session/engine.cpp`: the pump's `expire()` calls `note_establishment_timeout` only when `!session_engine_access::ended(session)`, then `stop_pump()` either way. Its discriminating cell is `PreActive_DeadlineBeforeBound_EofAtDeadlineNoEvent` (T040); its mutant is in T048.
- [ ] T031 [US1] D3 in `src/session/session.cpp`, `src/log/format_registry.cpp`, `include/fixpp/session/session.hpp`: at the give-up, the replay-gate shared hold, `claim_peer_logout_none_`, the Logout through `inbound_logout_leaf_` with the C-3.1 Text built in a bounded stack buffer with a `static_assert` on its size, then `record_disconnected_unless_already_()`; the C-3.3 warn record (registered format) whether or not the Logout went out; `unanswered_tr_` and its writes deleted.
- [ ] T032 [US1] Header comments naming the new rule (issue #310, FR-018): `record_state_transition_` ("105 D1: the edge sets the connection-end latch; its consumers"), `run_read_pump`'s disposition list, `dispose_unparseable_` (092's "D-1 and D-2 are a state transition, not a close" and its D-3 twin rewritten to name 105 FR-007), the deleted "(Phase 6 US4 wires the Logout emission before Disconnected.)", `pending_test_req_id_`'s comment, and the `unanswered_tr_` declaration comment removed.
- [ ] T033 [US1] Re-pin the D1 flips (FR-019), renaming where the name states the old rule: `InboundFrameDispositionsQ10.WithinCapMismatchInActive_DisconnectsWithoutALogout` (open-transport half) and the #534-commented cells in `tests/session/inbound_frame_dispositions_test.cpp`; `SecondFrameInFlight.*` and `ReconcileAfterTheTakeBack.*` (#534-commented) in `tests/session/test_cancelled_outbound_store_engine.cpp`; `EstablishmentTimeoutQ16.AcceptorAfterRefusedLogon_*` in `tests/session/engine_establishment_timeout_test.cpp`; `ResendAnswerStoreFailureEngine.Acceptor789Path_SlotReadFails_ConnectionStaysOpenUntilTheEstablishmentDeadline` in `tests/session/test_538_resend_answer_store_failure_engine.cpp`; `Capi537Witness.Deadline_CompIdsLeaveNoRoomForTheLogout_EndsSilently` → "ends silently and closes at once" in `tests/capi/recovery_liveness_capi_witness_test.cpp`. Run each entry.
- [ ] T034 [US1] Run T016 to T025 GREEN, then `ctest -L 105`, `-L session` and `-L capi` on linux-clang-debug. **R-2-class pins are expected RED here** (`GapDeadlineOutOfSequenceSite.FrameAtTheBoundDrawsTheLogoutAndTheTransportStaysOpen` and the 537 control cell "D-537-6, N"): they flip at D2 (T046); do not re-pin them to D1's behaviour. Commit `H1`.

### Mutant and spurious-hit arms for US1 part 1 (in `$M` at `H1`)

- [ ] T035 [US1] The pump's latch test deleted: T016, T017, T021 and T022 RED (the frozen clock leaves no competing close, so a GREEN would be spurious). (The `expire()` arm is T048's: no Phase 3 cell reaches `expire()` on a session that has recorded its own ending.)
- [ ] T036 [US1] `release_connection_end_` a no-op: T018 (bound route), T019, T020, T023 RED.
- [ ] T037 [US1] `close_connection_` calls the synchronous `Transport::close()` instead of `close_async()`: T025's close_notify assertion RED (C-8.2). And the R-1 Logout write deleted (keep the close): T019's Text cell RED.

**Checkpoint**: every class P, D and A ending closes; R-2-class endings still close at once, pending part 2.

---

## Phase 4: User Story 1, part 2 — the R-2 wait (Priority: P1) (D2; contract C-2)

**Goal**: FR-004, FR-004a: after fixpp's own ending Logout (except R-1's) the session closes at the peer's Logout or at
`logout_disconnect_timeout_ms` from the write, whichever is first.

**Independent Test**: EOF follows the peer's Logout, or comes at exactly the bound and not before.

### RED cells for US1 part 2 (write first; RED on the base `52aeed4e` before the fix)

- [ ] T038 [US1] `tests/session/test_105_logout_wait.cpp`, both roles: `LogoutWait105.<TooLow|GapDeadline|SendingTimeReject|OrigSendingTimeReject>_PeerLogoutEndsWait` (EOF after the peer's Logout and not before it, with `reset_on_logout` the receipt resets, C-2.6) and `Bound_EofAtExactlyTheBound` (open at bound − 1 ms, EOF at the bound, `onLogout` fired then, C-2.7; the competing HeartBtInt liveness instants and the gap deadline configured well past the bound, named in the cell). RED on the base `52aeed4e` before the fix: the transport stays open past the bound.
- [ ] T039 [US1] Same file, frames during the wait (C-2.1 to C-2.5): `StaysEstablishedDuringWait`, `AppMessageDuringWait_ReachesFromAppAndAdvances`, `TestRequestDuringWait_Answered`, `ResendRequestDuringWait_Answered`, `TooLowDuringWait_NoSecondLogout`, `TooHighDuringWait_SequenceChecks`, `SecondEndingDuringWait_NoSecondLogoutBoundHolds`, `SilentEndingDuringWait_BoundHolds` (L-105-a), `SendDuringWait_Refused` (`session_invalid_state_for_send`), `LivenessDuringWait_SendsNothing`, `Bound_DoesNotSlideWithAnAnswerSent` (a Heartbeat answering a TestRequest mid-wait). RED on the base `52aeed4e` before the fix (the session is `Disconnected` at the Logout's write).
- [ ] T040 [US1] Same file, before Active (C-2.10, C-2.11, OQ-1): `PreActiveTooLowLogon_PeerLogoutEndsWaitNoReset`, `PreActive_BoundBeforeDeadline_EofAtBoundNoEvent`, `PreActive_DeadlineBeforeBound_EofAtDeadlineNoEvent`, and one cell per Logon refusal with a Logout of C-2's list (TestMessageIndicator posture mismatch, strict-reset refusal, LogonSent's Logout-with-Text). RED on the base `52aeed4e` before the fix (closed at the deadline with the event).
- [ ] T041 [US1] Same file, edges (C-2.8, C-2.9): `TimeoutZero_EofAfterWrite`, `NoClock_EofAfterWrite` (direct Session), `<CloseGraceful|CloseTerminal|EngineStop>DuringWait_OneClose`, `OnLogoutFiresAtWaitEnd`, `LogoutNotTransmitted_ClosesAtOnce` (the long-CompIDs Logout, C-2 "not transmitted"). RED on the base `52aeed4e` before the fix where the base differs; cells equal on the base are *pins*, recorded.
- [ ] T042 [US1] Commit the cells (`C2`) and read T038 to T041 on `$B`; record per cell.

### Implementation for US1 part 2

- [ ] T043 [US1] D2 items 1 and 5 in `src/session/session.cpp` and `include/fixpp/session/session.hpp`: `store_then_emit`'s private transmitted out-parameter, passed up by the leaves; `send_ending_logout_(text)` (arms only when transmitted; absorbs a second ending while armed); every "initiates" caller of T006 routed through it, no longer recording `Disconnected` after Active; the liveness loop's condition `fsm_state_ == Active && ending_wait_.phase != armed`.
- [ ] T044 [US1] D2 items 2 to 4 and 6 to 9: `ending_wait_` (data-model E-3), `run_ending_logout_bound_` in the shape of `run_peer_logout_bound_` (join counter joined by `close()` and `drain_for_reconnect_`, published wake cleared on every exit, deadline race, generation assert, fail-closed handler, `release_connection_end_()` at exit); the receipt arms set `resolved` at entry; `on_inbound_cold_`'s armed `Disconnected` row (fault-free 35=5 with MsgType third → `resolved` and latch; no reset); timeout 0 and null clock arm nothing; `resolved`/`none` asserted at `install_reconnected_transport` and `attach_accepted_transport`.
- [ ] T045 [US1] Header comments (issue #310): `resend_gap_deadline_` and Arm B of `on_inbound_active_out_of_sequence_` name 105 R-2 / FR-004; `store_then_emit`'s declaration comment in `session.hpp` names the transmitted bit.
- [ ] T046 [US1] Re-pin the D2 flips (FR-019): `GapDeadlineOutOfSequenceSite.FrameAtTheBoundDrawsTheLogoutAndTheTransportStaysOpen` and its helper `expect_logout_and_open_transport` (renamed; `state_after_the_bound_frame` now Active during the wait, EOF at the peer's Logout or the bound) and the 537 note's control cell "D-537-6, N" in `tests/session/test_537_gap_deadline.cpp`. Run T038 to T041 GREEN, then `-L 105`, `-L session`, `-L capi`. Commit `H2`.

### Mutant and spurious-hit arms for US1 part 2 (in `$M` at `H2`)

- [ ] T047 [US1] The bound's deadline read from the last send: `Bound_DoesNotSlideWithAnAnswerSent` RED. The absorb branch of `send_ending_logout_` deleted: `SecondEndingDuringWait_*` RED.
- [ ] T048 [US1] `run_ending_logout_bound_` never spawned (forced miss): `Bound_EofAtExactlyTheBound` RED; and the spurious-hit arm: the bound set to `logout_disconnect_timeout_ms` + 1 ms, `Bound_EofAtExactlyTheBound` RED at the bound instant (the cell is not satisfied by another timer). The armed `Disconnected` row deleted: `PreActiveTooLowLogon_PeerLogoutEndsWaitNoReset` RED. `expire()`'s `ended` test deleted (D4): `PreActive_DeadlineBeforeBound_EofAtDeadlineNoEvent` RED on its no-event assertion.

**Checkpoint**: US1 complete; SC-001 read from the Ending-site table (every row has a GREEN cell or a named exemption).

---

## Phase 5: User Story 2 — an out-of-sequence Heartbeat answers with validation off (Priority: P2) (D5; C-5)

**Goal**: FR-009. **Independent Test**: knob off, peer numbers ahead, ten TestRequest cycles answered, session Active.

### RED cells for US2 (write first; RED on the base `52aeed4e` before the fix)

- [ ] T049 [US2] `tests/session/test_105_heartbeat_knob_off.cpp`, both roles, every cycle driven by T004's wire reader (advance until a 35=1 is read, so US3's timing does not break it): `HeartbeatKnobOff105.AheadPeerAnswersTenCycles_StaysActive` (SC-003; NextNumIn unmoved, no `fromAdmin` for those Heartbeats), `AbsentTestReqId_Answers`, `BehindPeerAnswers`. RED on the base `52aeed4e` before the fix: the session ends at the grace check.
- [ ] T050 [US2] Same file, `MismatchedTestReqId_EndsAndCloses` (C-5.2: no Logout, peer EOF at once; RED on the base `52aeed4e` before the fix on its EOF half) and the *pin* `KnobOn_Unchanged` (B-537-3's cells run unedited). Commit (`C3`); read T049 and T050 on `$B`.

### Implementation for US2

- [ ] T051 [US2] D5 in `on_inbound_active_out_of_sequence_` (`src/session/session.cpp`): the Heartbeat branch calls `answer_pending_test_request_(hdr.test_req_id)` when a TestRequest is pending and (too high with a resend outstanding, or `!cfg_.validate_sequence_numbers`); nothing else in the branch changes. Run US2 cells and `-L 537` cells GREEN. Commit `H3`.
- [ ] T052 [US2] Mutant in `$M` at `H3`: the predicate set back: `AheadPeerAnswersTenCycles_StaysActive` RED.

---

## Phase 6: User Story 3 — TestRequest timing and the threshold (Priority: P2) (D6; C-6)

**Goal**: FR-010 to FR-012. **Independent Test**: TestRequest at T, give-up at 2T (default T = 1.2 × HeartBtInt), both roles, default and set-threshold cells, discriminating the old instants from the new.

### RED cells for US3 (write first; RED on the base `52aeed4e` before the fix)

- [ ] T053 [US3] `tests/session/test_105_liveness_timing.cpp`, both roles, H = 10 s on the mock clock: `LivenessTiming105.Default_NoTestRequestAt11s_OneAt12s` (RED on the base: it sends at 10 s), `Default_ActiveAt23900ms_EndedAt24s` (RED on the base: it ends near 20 s), `SetThreshold7s_TestRequestAt7s_GiveUpAt14s`, `GuardAtExactly2T_EchoWins` and `At2TPlus1ns_GivesUp`, `GapOpenDefaultT_DeadlineLogoutComesFirst`, `ThresholdBelowH_GiveUpComesFirst` (FR-010 precedence, both orders), `HeartBtIntNegative_NoTestRequest` (SI-2; C++ only) and the *pin* `HeartBtIntZero_NoTestRequest`, `ThresholdOverflow_Saturates`. RED on the base `52aeed4e` before the fix.
- [ ] T054 [US3] Config cells: `LoadNegativeBattery.TestRequestThreshold_Zero_OutOfRange` (and the negative case if `parse_duration_to_ms` can yield one; pinned either way) in `tests/config/test_load_negative_battery.cpp`; `SessionOpen105.ThresholdZero_InvalidConfig` and `EngineRegister105.ThresholdZero_InvalidConfig` in `test_105_liveness_timing.cpp`; `test_load_happy_path.cpp`'s `"1h"` stays a *pin*. RED on the base `52aeed4e` before the fix. Commit (`C4`); read T053 and T054 on `$B`.

### Implementation for US3

- [ ] T055 [US3] D6 in `run_liveness_loop` (`src/session/session.cpp`) and a saturating `default_test_request_threshold` helper beside `deadline_after` in `src/session/read_first_frame_bounded.hpp` (or a sibling detail header) with `constexpr` cells in `test_105_liveness_timing.cpp` (their RED source is T059's mutant): T computed once, TestRequest at `deadline_after(last_inbound_steady_, T)`, give-up at two saturating steps plus the `+1ns` guard re-derived, HeartBtInt `<= 0` disables (SI-2).
- [ ] T056 [US3] FR-011 validation: `Session::open()` and `Engine::register_session` return `invalid_session_config` for T ≤ 0; `src/config/scalar_mappers.cpp` records `out_of_range` at `session[i].test_request_threshold`.
- [ ] T057 [US3] Re-pin the moved timings (FR-017, FR-019): T018 `CellB_*` (made to discriminate 1× from 1.2×) and `CellC_*_UnansweredTestRequestDisconnects` (now 2T, Logout then EOF) in `tests/session/heartbeat_testrequest_test.cpp` and `tests/session/test_heartbeat_cadence_8cell.cpp`; `ci/red-arms/batch18-lost-advance.sh`'s cell name; every cell T009 listed.
- [ ] T058 [US3] FR-012 comments and surfaces: slot 74's comment in `include/fixpp/core/error.hpp` (D3's wording, no arithmetic), `SessionConfig::test_request_threshold`'s comment in `include/fixpp/session/session_config.hpp`, `run_liveness_loop`'s algorithm block and its declaration comment in `session.hpp` (the "1 × HeartBtInt" steps and "grace window (1×heartbt_int)"), the QuickFIX parity row in `tests/config/test_quickfix_parity_table.cpp` and its TOML documentation (found by T007). Run US3 cells, `-L config`, `-L session` GREEN. Commit `H4`.
- [ ] T059 [US3] Mutants in `$M` at `H4`: the default threshold set back to H: `Default_NoTestRequestAt11s_*`, `Default_ActiveAt23900ms_*` and the `constexpr` cells RED; the give-up at T instead of 2T: `SetThreshold7s_*` RED; the `+1ns` guard deleted: `GuardAtExactly2T_EchoWins` RED; validation deleted: T054's cells RED.

---

## Phase 7: User Story 4 — pre-session rejects are logged (Priority: P3) (D8; C-7)

**Goal**: FR-014, FR-015. **Independent Test**: capturing sink, one record per refused connection naming its reason.

### RED cells for US4 (write first; RED on the base `52aeed4e` before the fix)

- [ ] T060 [US4] `tests/session/test_105_presession_log.cpp`, capturing sink: `PreSessionLog105.<NotTls|TlsHandshake|NoTimeLeft|FirstFrameTimeout|FirstFrameOverBudget|FirstFrameOverLimit|NoIds|NoSession>_OneRecord` (exactly one C-7.1 record per connection, its `reason` name and `error`), `NoSession_RecordNamesCompIds`, `LongCompIds_CutAt15Bytes` (L-105-d), `Flood_BoundedWithSuppressedCount` (C-7.2 on the engine's mock clock: one record per window, the next carries the count), `ListenerBindFails_OneRecord` (port in use, C-7.3). Each reason cell's real timers are named and set. RED on the base `52aeed4e` before the fix: no record. Commit (`C5`); read on `$B`.

### Implementation for US4

- [ ] T061 [US4] Hoist the rate gate (D8, E-7): new installed `include/fixpp/session/detail/log_rate_gate.hpp` with `detail::log_rate_gate` (Session's fields, layout unchanged) and inline `admit_log_record`; `Session` keeps the name by a member alias and `admit_log_record_` forwards. The garble and reply-ceiling record cells run unedited GREEN. The new installed header joins every install manifest or packaging census that lists headers (find them with `git grep -n` on an existing `include/fixpp/session/detail/` header name as the known positive).
- [ ] T062 [US4] `log_pre_session_reject` in `src/session/engine.cpp` beside `log_role_loop_open_failure` (same logger and trace context), one call per E-6 branch; the accept loop's local gate (window `max(HeartBtInt or default, 1 s)`, `engine_cfg.clock`); the listener catch split into `std::system_error` and `...`; both formats registered in `src/log/format_registry.cpp`. Run US4 cells GREEN. Commit `H5`.
- [ ] T063 [US4] Mutants in `$M` at `H5`: the gate bypassed: `Flood_*` RED; the `no_session` call deleted: `NoSession_OneRecord` RED; the listener record deleted: `ListenerBindFails_OneRecord` RED.

---

## Phase 8: User Story 5 — dead state is gone (Priority: P3) (D7; E-9)

**Goal**: FR-013, SC-005. **Independent Test**: a warning-clean build with the markers removed, and a `git grep` that reports no remaining reference.

### RED (instrument) for US5 (first, on the base `52aeed4e`)

- [ ] T064 [US5] Record on `$B` the instrument's RED: `git grep -nwE 'attempt_index_|heartbeat_interval_|logout_disconnect_timeout_|heartbeat_timer_|test_request_timer_|logout_timer_|last_outbound_testreqid_|drive_logout|run_heartbeat_cadence|run_inbound_liveness_watch|validate_inbound_heartbeat_testreqid|process_inbound_sequence_reset|reply_to_inbound_resend_request|parse_cn_from_dn_local' -- include/fixpp/session/reconnect_fsm.hpp src/session/reconnect_fsm.cpp src/session/session.cpp tests` (`-w`, so `logout_disconnect_timeout_ms` and other live members do not match; known positive: a hit in `reconnect_fsm.hpp`) and `git grep -nE -B2 -A2 'maybe_unused' -- src include | grep -E '559'` (known positive: a `ReconnectFsm` member) both report hits there.

### Implementation for US5

- [ ] T065 [US5] Remove from `include/fixpp/session/reconnect_fsm.hpp` and `src/session/reconnect_fsm.cpp` E-9's members, `drive_logout`, the caller-less stubs of T011 (SI-1 ruling) with their comments (including `run_inbound_liveness_watch`'s timing comment), the false header comment, and the two constructor parameters; update `Session::Session` and every constructor call site of T011.
- [ ] T066 [US5] Remove `parse_cn_from_dn_local` from `src/session/session.cpp`.
- [ ] T067 [US5] Retire `DriveLogoutStub_*` and `Initiator_DriveLogout_*` in `tests/session/test_logout_timeout.cpp` with a note naming 105 FR-013; remove their entry from `ci/red-arms/batch11-labels.txt`; correct the "triggers drive_logout path" comment in `tests/fuzz/fuzz_session_recovery_admin_parse.cpp` and every test comment naming a removed stub as a RED witness.
- [ ] T068 [US5] Size pins (FR-013): re-run T008's two greps at the head and update every `sizeof(Session)` / `ReconnectFsm` pin found; then a full `-k 0` build of linux-clang-debug as the second instrument. T064's grep now reports no hit (SC-005).

---

## Phase 9: Surfaces — C-ABI 1.18 and Python (D9; C-9)

- [ ] T069 C-ABI witness cells first, in `tests/capi/recovery_liveness_capi_witness_test.cpp` (through `recovery_liveness_capi_support.hpp`), each with a positive control (the session shown live first, or a well-formed message afterwards reaching `cb`): `fixpp_session_is_established` true during an R-2 wait and false after the bound; `fixpp_session_send` refused during the wait; a knob-off answering peer stays established; the peer reads the R-1 Logout then EOF through a C-driven session. RED on the base `52aeed4e` before the fix.
- [ ] T070 `include/fix/c_api/session.h`: a 1.18 clause on `fixpp_session_is_established`, `fixpp_session_close`, `fixpp_session_send`, `fixpp_session_register_callback` and `fixpp_session_register_send_callback`, and the 1.11, 1.12 and 1.17 sentences these change amended in place, "amended in C-ABI 1.18, 105" (found by T010 and by the 103 bump's clause sites: `git grep -n 'C-ABI 1.17' include/fix/c_api/session.h`). Not clang-formatted.
- [ ] T071 `include/fix/c_api/version.h`: `FIXPP_C_ABI_VERSION_MINOR 18` and the 1.18 history entry (BREAKING, `[const §X.7]`; the counterparty's side: every ending closes the connection, the R-1 Logout, the R-2 wait, the TestRequest at 1.2 × HeartBtInt). Not clang-formatted.
- [ ] T072 Move every "pins the minor" hit of T010 and update every other file the 103 bump touched that T010 lists (`tests/capi/version_test.cpp`; docs and changelog entries; the Python binding's version expectations only where T010 found a hit under `bindings/`), then run `-L capi` and `python3 -m pytest bindings/python/tests`.

---

## Phase 10: Records and supersession (FR-016, FR-018, FR-019; D11, D12)

- [ ] T073 B&L, live file `spec/behaviors-and-limitations.md` (FR-016, D11): retire L-103-5 (its QuickFIX statement not carried forward as written) and move it to `spec/behaviors-and-limitations-closed.md` under that file's cut rule (only self-declared-resolved entries, marked "[RESOLVED in 105]").
- [ ] T074 B&L amend in place, dated, naming 105: B-537-2 (the "transport stays open" clause and B-537-2(b)'s silent grace-check ending), B-537-3, L-093-1, L-093-6, B-093-4, B-092-2, B-092-5, L-092-1, B-103-10 and B-104-6.
- [ ] T075 B&L new rows B-105-1 (close rule by ending class) to B-105-7 (BREAKING C-ABI 1.18 and the C++ layout of `Session` and `ReconnectFsm`), and L-105-a, L-105-b, L-105-c, L-105-d from contract C-11; the departures table of the spec and C-10 copied, not re-derived. Each row cites its cells by name.
- [ ] T076 [P] `.specify/536-537-538-recovery-liveness.md`: an erratum at its end naming 105 (R-6 superseded by R-1; D-537-5's C-3 disclosure by FR-004).
- [ ] T077 [P] Brain pages, each citing source (D11): `brain/components/session.md` (R-6; the grace-check deferral to B27), `brain/components/session-liveness-and-reconnect.md` (the `ReconnectFsm` correction block; the thresholds as a pointer to `run_liveness_loop`), `brain/components/engine-accept-path.md` (the first frame through the pump; the reject records), `brain/components/graceful-logout.md` if T007 hit a rule of it. Each page's superseded-docs list names 105's supersessions of 005, 013, 092, 093, 103 and 104 by name (merged bundles are not edited).
- [ ] T078 [P] T-DOC-1 (D12): a `brain/failure-classes.md` entry in that page's form (trigger and procedure, no instance, no PR) for `<windows.h>` macro identifiers, its text sourced from PARENT `.claude/agents/phase-implementer.md` section "Cross-platform — `<windows.h>` macros rewrite common identifiers on MSVC": the trigger (a new identifier in code an MSVC lane compiles whose spelling a platform header defines as a macro), the procedure (descriptive names; `(std::max)(a, b)`; build the touched targets on MSVC before the first labelled push; a Linux-only green is no evidence for the class); and one `brain/log.md` line dated at the PR.
- [ ] T079 Brain checks: `python3 tools/check_brain.py --self-test`, `python3 tools/check_brain.py gate` and `python3 tools/check_brain.py census` (and what `.github/workflows/brain-freshness.yml` runs, derived from it), then the WRAPPER PARENT `research/G19-fix-fpml-iso20022/tools/brain-external-sweep.sh` (never a bare `check_brain.py sweep`); record its execution count.
- [ ] T080 Supersession close (FR-019): re-run T007's census at the head; every living-surface hit now states the new rule or names 105; every FR-019 pin of T033, T046, T057 and T067 asserts the new rule; T076 and T077 name every ruling of FR-019's list. Record the classed table in the evidence file.

---

## Phase 11: Polish, verification and mandatory close-out

- [ ] T081 [P] `/simplify` over the diff; any change re-runs `-L 105`.
- [ ] T082 [P] Header-comment check (FR-018): `git grep -nE 'Phase 6 US4|not a close|unanswered_tr_' -- src include` reports no stale claim.
- [ ] T083 clang-format on touched files only (never `specs/`, never `include/fix/c_api/*.h`): `git diff --name-only --diff-filter=d origin/main...HEAD -- src include tests | grep -E '\.(cpp|hpp|h)$' | grep -vE '^include/fix/c_api/' | xargs clang-format --dry-run --Werror`.
- [ ] T084 clang-tidy, cppcheck (the local 2.22 on PATH) and IWYU on the touched `src/` and `include/` files (Article IX §4).
- [ ] T085 Comment-claim lint: `PARENT/.claude/scripts/check-comment-claims.py --root PARENT/research/G19-fix-fpml-iso20022/library --base origin/main`; read added runtime strings and inventories by hand.
- [ ] T086 Line-citation gate: `python3 tools/check_line_citations.py --range origin/main..HEAD` and its `--shift-audit origin/main..HEAD` (the bundle is charged by the range gate).
- [ ] T087 Every buildless Tier 1 step run locally, derived from `.github/workflows/tier1.yml`: `ci-script-pins` (all its steps, including `check_co_spawn_lambda.py --self-test` and the gate), `check-layers`, and `co-spawn-closure-audit` (unconditional: `close_connection_` and `run_ending_logout_bound_` add spawns, T014); `ci/pump-census.sh` against `ci/expected-pump-sites.txt`.
- [ ] T088 linux-gcc-release `-Werror` build of the touched targets (`session_105_tests`, the re-pinned entries, `fixpp_session`, the capi and config tests).
- [ ] T089 Sanitizer lanes: linux-clang-asan, -ubsan, -tsan, each `-L 105` and the touched `session`, `capi` and `config` entries (`EngineStopRacesEndingClose_OneClose` and the TLS cells on TSan); any report is a defect until disproven.
- [ ] T090 Interop (research R4): run the live QuickFIX interop cells of `tests/interop` (`ci/run-interop-live.py`'s selection): peers now see a Logout before EOF on the give-up and an R-2 wait.
- [ ] T091 Article VIII §3: a paired base-vs-candidate run of every bench T012 found driving `run_read_pump`, base at `$B` padded to `$T`'s path length (`cmp` the `.text`), or the recorded statement that none does.
- [ ] T092 **(orchestrator)** windows-msvc-debug build (`-k 0`) and run of the touched targets and `-L 105` in `/mnt/c/temp/fixpp`, after taking `.sandbox-lock` by PARENT `research/G19-fix-fpml-iso20022/msvc-local-build-procedure.md` Step 0; release the lock after. Never at the same time as a Linux build.
- [ ] T093 The WHOLE pytest suite: `python3 -m pytest bindings/python/tests` (the behaviour reaches Python through the C ABI).
- [ ] T094 One full `ctest --preset linux-clang-debug --no-tests=error` (Article XVII §7), after reading the sandbox lock; record the count and any failure.
- [ ] T095 Coverage of added lines (Article IX §1): fresh per-binary profraw, `session`, `log` and `config` modules; every uncovered added line or branch tested, waived with a one-line rationale, or escalated to a filed, labelled issue, in `.specify/decisions/105-transport-liveness-verify.md`.
- [ ] T096 **(orchestrator)** `/speckit-verify`; writes `.specify/decisions/105-transport-liveness-verify.md`. Re-run T083 to T087 as the last actions before any labelled push.

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [ ] T097 **Catalogue close-out**: read `spec/feature-catalogue.md` for every OFFICIAL row whose Feature column names 105 or whose subject is a 105 issue (`grep -nE '105-transport|#534|#535|#559|#582|#687'`); flip each such row to `done` with the PR and evidence, with its `spec/coverage-index.md` entry, or record "none owned" after the check. Rows this feature amends but does not own (S-003, S-004, S-015 by their subjects) get a dated "105 amends this row" note naming the B-105 rows; their state does not change.
- [ ] T098 **Feature-completeness audit (MUST be the FINAL task)**: against the final head, (i) every row of this file is `[X]` or carries a waiver rationale; (ii) every FR and SC maps to a landed test and a landed implementation (the Requirement map below, re-read cell by cell); (iii) every feature-owned OFFICIAL row is `done` with its `coverage-index.md` entry. Record the verdict, 100 % or fully waived, in `.specify/decisions/105-transport-liveness-verify.md` under `## Completeness`.

---

## Requirement map (FR/SC → tasks)

| Requirement | Cells | Implementation | Mutants / instrument |
|---|---|---|---|
| FR-001 (every ending closes) | T016–T024, T038 | T027–T029 | T035, T036; census T005 |
| FR-002 (orderly close via `close_async`) | T024, T025 | T028 | T037 |
| FR-003 (R-1 Logout, close, record, slot 74) | T019, T020, T025 | T031 | T036, T037 |
| FR-004, FR-004a (R-2 wait) | T038–T041, T069 | T043, T044 | T047, T048; census T006 |
| FR-005 (close after reply and reset) | T017, T018 | T027–T029 | T035, T036 |
| FR-006 (acceptor first frame) | T022 | T029 | T035 |
| FR-007 (silent pre-Active refusal at once) | T021, T022, T040 | T029, T030 | T035, T048 |
| FR-008 (store work not cut) | T017, T018, T024 | T028 (`inbound_depth_`) | T035 |
| FR-009 (#582) | T049, T050, T069 | T051 | T052 |
| FR-010 (T, 2T, HeartBtInt ≤ 0, precedence) | T053 | T055 | T059 |
| FR-011 (validation) | T054 | T056 | T059 |
| FR-012 (comments and surfaces) | — | T058 | T080 |
| FR-013 (dead state, size pins) | T064 (instrument) | T065–T068 | T064's grep, T068's build |
| FR-014, FR-015 (records) | T060 | T061, T062 | T063 |
| FR-016 (B&L) | — | T073–T075 | — |
| FR-017 (RED first, peer EOF, discriminating bounds) | every RED task, T026, T042 | — | every mutant task |
| FR-018 (superseded comments) | — | T032, T045, T058 | T082, T085 |
| FR-019 (supersession) | T033, T046, T057, T067 | T076, T077, T080 | census T007 |
| SC-001 | Ending-site table, T016–T025, T038–T041 | T027–T031, T043, T044 | T035–T037, T047, T048 |
| SC-002 | T053 | T055 | T059 |
| SC-003 | T049 | T051 | T052 |
| SC-004 | T060 | T062 | T063 |
| SC-005 | T064 | T065, T066 | T068 |
| SC-006 | T089, T094 | — | — |
| Article VIII §3 | T091 | — | — |
| C-ABI 1.18 (D9) | T069 | T070–T072 | T010 |

## Dependencies and execution order

- **Pipeline before Phase 1**: Gate A, `/speckit-analyze`, `/speckit-checklist` and `/speckit-checklist-audit` (`.specify/pipeline.md`); the audit blocks implementation.
- **Phases**: 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10 → 11. Every story but US4 and US5 edits `src/session/session.cpp`, so stories run in order, never in parallel.
- **US1's close mechanism is foundational**: T027 to T029 (the latch, `release_connection_end_`, `close_connection_`, the pump's consumer) are what US2's mismatch close (T050), US3's give-up path (`run_liveness_loop`'s R-1 exit, T053's ended-at cells) and the C-ABI cells (T069) observe. US3 must not start before T034. US3 changes the instants the R-1 cells reach, so T019, T020 and T049 are event-driven (T004).
- **Within US1**: T015 → T016–T025 → T026 → T027 → T028 → T029 → T030 → T031 → T032 → T033 → T034 → T035–T037; then T038–T041 → T042 → T043 → T044 → T045 → T046 → T047, T048. The R-2-class pins are RED between T034 and T046 by design.
- **US5** depends on T011 and T008 only; **US4** on T013 only; both after US3 because of the shared session strand files in review order, not by code dependency.
- **Records** (Phase 10) after every mechanism; T080 after T073–T079. **T096** after T081–T095; **T097 → T098** last.

### Parallel opportunities

- Phase 2: T005 to T014 are read-only and run in parallel.
- Phase 3: T016 (`test_105_connection_close.cpp`), T019 (`test_105_liveness_timing.cpp`) and T025 (`test_105_tls_close.cpp`) are different files; the other cells of each file follow their file's first task.
- Phase 4 and US2, US4: each story's cells share one file and run in order.
- Phase 10: T076, T077 and T078 are different files (T073 to T075 share the B&L file and run in order).
- Phase 11: T081 and T082 in parallel; no two builds ever run at once, so a parallel task never builds beside another.

### Parallel examples

```bash
# Phase 2 (read-only censuses):
Task: "T005 ending-site census and table"
Task: "T007 supersession census with known positives"
Task: "T010 C-ABI minor-pin census"
Task: "T013 pre-session reject branch census"

# US1 part 1 RED cells (three files):
Task: "T016 PumpSilentEnding cells in tests/session/test_105_connection_close.cpp"
Task: "T019 R-1 give-up cells in tests/session/test_105_liveness_timing.cpp"
Task: "T025 real-TLS close_notify cell in tests/session/test_105_tls_close.cpp"

# US3 / US5 (sequential within the story; shown for the hand-off order):
Task: "T053 then T054 (timing cells, then config cells)"
Task: "T065 then T066 then T067 (ReconnectFsm, parse_cn_from_dn_local, retired probes)"

# Phase 10 records:
Task: "T073 retire L-103-5 to the closed B&L file"
Task: "T077 brain component pages"
Task: "T078 T-DOC-1 failure-classes entry and log line"
```

## Implementation strategy

- **Who executes.** A `phase-implementer` agent runs the implementer's tasks; the orchestrator runs the **(orchestrator)** tasks and commits only its own records.
- **MVP = US1** (Phases 1 to 4): the only defect a peer can use to hold a connection and an acceptor slot open. STOP and VALIDATE at Phase 4's checkpoint (SC-001 from the Ending-site table). The feature still merges whole.
- **Incremental**: US2, then US3, then US4 and US5; each story GREEN with its mutants before the next starts; then surfaces, records, polish and the close-out pair.
- **Records before CI**: brain, B&L and T-DOC-1 edits go in the PR before its first labelled push (a late docs push re-runs the matrix).

## Notes (hard rules)

- Write recipes, not results: no pasted counts, no line numbers, no example values in code comments; cite symbols.
- A merged feature's bundle is never edited; it is superseded by name (FR-019).
- A gate result is scoped to the commits that existed when it ran.
