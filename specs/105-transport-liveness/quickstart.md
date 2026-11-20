# Quickstart: validating 105

Run from the library root of the tree that holds the branch. One build or test process at a time (machine rule).
The observable rules are [contracts/transport-liveness-105.md](contracts/transport-liveness-105.md); the decisions,
cells and census recipes are [research.md](research.md) D1 to D11. This file keeps commands and cell names only; it
does not copy the census keys.

## 1. Censuses first (before any fix)

Each census runs in a scratch copy, never committed. Results go in the evidence file, never in a code comment.

- **Ending sites and roots** (research D1, "The census, by execution context"). Run the write-site grep and the
  root grep from D1, walk each site to its roots with `codegraph_callers` (`projectPath` = this tree, after
  `codegraph sync`), and fill the tasks.md table. Known positives that must appear: the unanswered-TestRequest write
  under `run_liveness_loop`; Arm B under the pump; `send_body_`'s failure write under the send root.
- **R-2 callers** (research D2 item 1): each hit classed "initiates" or "replies"; `resend_gap_deadline_` initiates.
- **Supersession** (research D11): the grep there; its known positives must be hits.
- **Size pins** (research D7): the narrow grep, then the wide grep that must report `ref_shape_test.cpp`.
- **Moved timings** (research D10): the TestRequest grep, read for mock-clock advances across HeartBtInt.
- **C-ABI minor pins** (research D9): `git grep -nE 'MINOR 17|1\.17' -- include tests bindings tools ci`.
- **Article X §7 predicate** (research D9): `gh release list --exclude-drafts` at the PR head; empty keeps the MINOR
  bump, and the result goes in the evidence file.
- **Silent endings and the latch's setter** (research D2 item 4, D1): the silent-ending classification of the frame
  arms' write sites (known positive: the BeginString guard of `on_inbound_active_`), and the
  `owe_connection_end_(` call sites (known positive: the edge).
- **`ReconnectFsm` stubs** (research D7): the caller census; known positive `drive_logout`'s test call.

## 2. RED on the base, or a guard or seam cell with its mutant

The base is the branch's merge-base with `origin/main` (a scratch worktree at `git merge-base HEAD origin/main`, on the
build volume); its `src/` and `include/` are identical to `52aeed4e`, where the design was read, and tasks.md T001
checks that before any reading (spec clarification "Session 2026-11-21"). Every cell in section 3 is marked:
- **[RED]**: written first, run on the base, and must fail there by an assertion for its stated reason.
- **[guard: <mutant>]**: a rule the base already keeps, GREEN on the base by design; it must fail under the named mutant
  (section 5). A `constexpr` helper cell is a guard of this kind, in `test_105_seams.cpp`.
- **[seam: <mutant>]**: a behaviour cell that names a symbol the base lacks (an allocation-failure seam, the bound's
  in-body race seam, the listener-construction seam, a test-access read of a new member). It lives in
  `tests/session/test_105_seams.cpp`, ctest `session_105_seam_tests` (labels `session`, `105` and `105_seam`), which a base
  reading never builds; it must fail at the implementation head under the named mutant, which removes exactly its
  mechanism, and that reading stands in for the base reading.

A [RED] cell whose oracle the base could meet for another reason (the session already `Disconnected`) carries the
positive control named with it. The mechanism mutants of section 5 must fail too.

```bash
# on the base (the cell files overlaid): the seam entry is neither built nor run
cmake --build --preset linux-clang-debug --target session_105_tests
ctest --preset linux-clang-debug -L 105 -LE 105_seam --no-tests=error -V
# at the implementation head: both entries
cmake --build --preset linux-clang-debug --target session_105_tests session_105_seam_tests
ctest --preset linux-clang-debug -L 105 --no-tests=error -V
```

## 3. Scenarios, by cell

Names are proposals; the implementer keeps the scenario, its assertion and its mark.

**Connections close (C-1, C-4, C-8)** — `test_105_connection_close.cpp`, both roles (the [seam] cells in
`test_105_seams.cpp`):
- [RED] `ConnectionClose105.PumpSilentEnding_<site>_PeerSeesEofAtOnce` — one cell per class-P site of the census table
  (TestReqID mismatch, BeginString guard, CompID guard, MsgSeqNum 0, D-3); mock clock frozen; assert peer EOF.
- [RED] `ConnectionClose105.PumpSilentEnding_NoApplication_PeerSeesEofAtOnce` — `EngineConfig::application` null (N-4).
- [RED] `ConnectionClose105.PeerLogoutInActive_EofAfterReplyAndReset` — `reset_on_logout` on; the peer reads the
  reply, then EOF; the store holds (1, 1) before EOF.
- [RED] `ConnectionClose105.TooHighLogoutAcknowledged_EofAfterReset` — the bound's route.
- [RED] `ConnectionClose105.LivenessToAdminThrows_EofAtOnce`, `...LivenessAssignFails_EofAtOnce`,
  `...LivenessStoreFails_EofAtOnce`.
- [RED] `ConnectionClose105.SendStoreFailure_EofAfterSendReturns`.
- [RED] `ConnectionClose105.AcceptorFirstFrameError_EofAtOnce`, `...AcceptorFirstFrameRefused_EofAtOnceNoTimeoutEvent`.
- [RED] `ConnectionClose105.PreActiveRefusal_<D-1|D-2|D-3|NotConnected|LogonSent>_EofBeforeDeadlineNoTimeoutEvent` — a
  short `logon_timeout_ms`; assert EOF with the clock frozen and no `session_event_establishment_timeout`.
- [guard: `expire()` closes without its event] `ConnectionClose105.SilentPeer_StillClosedAtEstablishmentDeadlineWithEvent`
  (B-093-4 kept).
- [RED] `ConnectionClose105.OnLogoutCallsClose_OneClose`, `...EngineStopRacesEndingClose_OneClose` (TSan lane).
- [RED] `ConnectionClose105.DirectInitiator_EndingThenDriveReconnect_Reconnects`.
- [guard: the latch's `lifecycle` term deleted] `ConnectionClose105.InsideClose_NoSecondClose`; [guard: the drain flag
  deleted] `...InsideReconnectDrain_DrainKeepsItsBound`.
- [seam: the fallback deleted] `ConnectionClose105.CloseFrameSpawnFails_<ReleaseAtLoopExit|WriteLimit|BoundExpiry>_PeerSeesEofWithNoLaterCall`
  (an allocation-failure seam at the close frame's spawn; an Engine session ended by a detached root while its pump
  is parked in a read; the peer sees EOF with no inbound frame and no API call, by the synchronous fallback; the
  counter is 0 after `close()`; on the TLS arm no close_notify, L-105-h; clarification D-g). Mutant: the fallback
  deleted (the latch left for a later consumer, round 2's rule).
- [RED] `TlsClose105.<each class>_PeerReceivesCloseNotify` — `test_105_tls_close.cpp`: the pump's classes on the
  Engine-over-TLS rig `engine_loopback_harness.hpp` (acceptor role, a `LoopbackTlsFixture::make_client` peer, a
  `mock_clock` in `EngineConfig::clock`), the classes that close at a root's exit also on
  `loopback_tls_session_harness.hpp` (a direct Session); research D10.

**The R-2 wait (C-2)** — `test_105_logout_wait.cpp`, both roles; one or more cells per row W-1 to W-15 of the
contract's table and per row of data-model E-3 (research D10 lists them):
- [RED] `LogoutWait105.TooLow_PeerLogoutBelowNextNumIn_EndsWaitAndResets`, `...GapDeadline_PeerLogoutAboveNextNumIn_EndsWaitAndResets`,
  `...SendingTimeReject_PeerLogoutInSequence_EndsWait`, `...OrigSendingTimeReject_PeerLogoutEndsWait` — the peer's
  Logout at the number a real peer uses (C-2.2); open before it (positive control), EOF after it; with
  `reset_on_logout` the receipt resets; NextNumIn advanced only for the in-sequence one.
- [RED] `LogoutWait105.Bound_EofAtExactlyTheBound` — open at bound − 1 ms, EOF at the bound.
- [RED] `LogoutWait105.Bound_DoesNotSlideWithAnAnswerSent` — a Heartbeat answering the peer's TestRequest mid-wait.
- [RED] `LogoutWait105.AppMessageDuringWait_ReachesFromAppAndAdvances`, `...TestRequestDuringWait_Answered`,
  `...ResendRequestDuringWait_Answered`, `...HeartbeatDuringWait_AnswersPendingTestRequest`,
  `...TooHighDuringWait_NotDeliveredResendRequestSent`, `...GapDeadlinePassedDuringWait_NoSecondLogout` — the
  in-sequence frames on the SendingTime-Reject route (C-2.2).
- [RED] `LogoutWait105.Absorbed_<TooLow|SecondSendingTimeReject|TestReqIdMismatch|CompIdMismatch|MsgSeqNumZero>_ThenAppMessageAndPeerLogout`
  — no second Logout, the application message reaches `fromApp`, the peer's Logout resets, `onLogout` once at the end;
  and `...TooLowRoute_PeerFramesBelowNextNumIn_Absorbed` (a real peer's frames after the too-low Logout).
- [RED] W-9, one per disposition, each EOF with the clock frozen, the bound joined, `onLogout` once, no second Logout:
  `LogoutWait105.StoreFailureDuringWait_EofAtOnce` (error, `Disconnected` written),
  `...PersistFailureDuringWait_EofAtOnce` (error, none written), `...TooHighDuringWait_ResendRequestAssignFails_EofAtOnce`
  (success, `Disconnected` written), `...SendHeldInStoreAcrossTheEnding_StoreFails_EofAtOnce` (a non-pump root);
  `...DirectSession_StoreFailureDuringWait_ClosesAtNextSend`.
- [RED] `LogoutWait105.SendDuringWait_Refused` — positive control: `fixpp_session_is_established` true at the send;
  [RED] `...SendHeldInStoreAcrossTheEnding_NumberedBeforeTheLogout`.
- [RED] `LogoutWait105.LivenessDuringWait_ArmedOneTickBeforeHeartbeat_SendsNothing`, `...ArmedOneTickBeforeGiveUp_SendsNothing`
  — positive control: `fsm_state() == Active` at the instant tested.
- [RED] `LogoutWait105.TimeoutZero_NoWaitEofAfterWrite`, `...NoClock_NoWaitEofAfterWrite`.
- [RED] `TlsClose105.CloseGracefulDuringWait_CloseNotifyAtTheBound` — TLS; `logout_disconnect_timeout_ms` set long
  (well above the wall cap; `close()`'s grace is the same knob), the mock clock advanced to the bound; asserts
  close_notify and the close at the bound's mock-clock instant.
- [RED] `LogoutWait105.<CloseTerminal|EngineStop|DriveReconnectDrain>DuringWait_OneClose`.
- [RED] `LogoutWait105.PreActive_BoundSurvivesTheRefusalsOwnEdge`, `...PreActive_FramesDrained`,
  `...PreActiveTooLowLogon_PeerLogoutEndsWaitNoReset` (OQ-1), `...PreActive_BoundExpiryCloses`,
  `...PreActive_WaitArmedAtEstablishmentDeadline_ClosedNoEvent` (Engine; no arm in flight, so the pump's own `expire()` closes at that instant: this is E-5's cell, not the cap's, research D10), `...PreActive_DirectSession_BoundAloneEndsWait`,
  `...PreActive_CloseDuringWait_OneClose`.
- [RED] `LogoutWait105.PendingTooHighMark_GapDeadlineLogout_EofAfterWrite` (C-2.8, N-5).
- [RED] `LogoutWait105.OnLogoutFiresAtWaitEnd`.
- [seam: the failure read as timeout 0] `LogoutWait105.BoundSpawnFails_<R2AfterActive|R2BeforeActive|Reply>_NoLogoutWritten_EofAtOnce` — an
  allocation-failure seam at the bound's spawn: no Logout bytes at the peer, no wait, `Disconnected` and `onLogout`
  once (after Active), EOF at once (clarification D-g). Mutant: the failure read as timeout 0 (a Logout on the
  wire).
- [seam: the failure exit routed through `release_connection_end_`] `LogoutWait105.EndingBound_RaceThrows_<Writing|Armed>_EofAtOnce` — an in-body seam throwing `std::bad_alloc`
  where the bound builds its race, before it awaits the `||` expression, never inside an arm (data-model E-3 row 16). `Writing`: the Logout's write held behind a peer that does not
  read, EOF at once with the clock frozen, no wait after it, `onLogout` once; `...Writing_StoreHeld_EofAtOnce`, the
  Logout's store call still held when EOF is asserted. `Armed`: after Active and before Active, the seam armed by the
  test only after the leaf has returned and the phase reads `armed` (the first race is built in `writing`), the phase
  the exit found asserted through test access as the reach proof. Mutant: the failure exit routed through
  `release_connection_end_` (no EOF while the write is held).
- [seam: the armed failure exit routed through `release_connection_end_`] `LogoutWait105.EndingBound_RaceThrows_Armed_ResendAnswerBlocked_EofAtOnce` — after Active on the
  SendingTime-Reject route, the peer's ResendRequest answered during the wait, the answer blocked in `async_write` behind
  a peer that stopped reading, the seam armed in `armed`: EOF at once with the clock frozen. Mutant: the armed failure
  exit routed through `release_connection_end_` (no EOF while the answer is blocked; Gate A loop 2 round 2, N-2).
- [RED] `LogoutWait105.EndingBound_DeadlineArmAbsorbsException_<Writing|Armed>_EofAtOnce` — the counting `Clock`
  decorator of `test_105_support.hpp` (a test-side instrument, so the cell builds on the base) as `clock_override`,
  throwing `std::bad_alloc` from `sleep_until` for the bound's instant only (for `Armed`, armed by the test after the
  leaf has returned, as above); EOF at once. Its reach-proof twins (the phase the exit found, read through test access)
  are [seam] cells. Mutant: the early completion read as a re-test (the decorator's count of the
  bound's sleeps exceeds one per wake).
- Both failure-exit cells drive the executor by a bounded number of handler runs (`run_one` × N, or `run_for`
  under a wall cap), so each RED arm terminates.
- [RED] `LogoutWait105.Bound_ExpiresWhileResendAnswerBlocked_EofAtTheBound` — SendingTime-Reject route; the peer's
  ResendRequest during the wait, its answer blocked in `async_write` (the peer stops reading); open at the bound −
  1 ms, EOF at the bound (N-1). Mutant: rows 10 and 11 routed through `release_connection_end_`'s `inbound_depth_`
  test.
- [RED] `LogoutWait105.PreActive_<LogoutBlockedInWrite|LogoutHeldInStore>_ClosedAtEstablishmentDeadlineNoEvent`
  (Engine; establishment time left below `logout_disconnect_timeout_ms`): open at the deadline − 1 ms, EOF at the
  deadline, no `session_event_establishment_timeout` and no record; `...PreActive_ClockOverride_CapOnEngineClock`
  (a second mock clock as `clock_override`; only the engine clock reaches the deadline). Mutant: the cap's arm
  deleted.
- [RED] `Lifetime105.CloseFrame_SpawnedByWriteLimitDuringGracefulClose` (ASan lane; the mock clock advanced to the
  bound well inside `close_grace`'s wall time: EOF at the bound, the Session destroyed after `close()` returns, the
  counter 0 then), RED on the base in its EOF-at-the-bound half; mutant: `close()`'s second join of the close frame's
  counter deleted. [guard: the bound's join deleted, and the close frame's join deleted]
  `Lifetime105.<EndingLogoutBound|CloseFrame>_SessionDestroyedAfterClose`.

**The write limit (C-2.7, C-8.5)** — `test_105_write_limit.cpp`, both roles, on a transport whose write is held
until it is closed (the scripted transport's `write_hold`, or a seam in `test_105_support.hpp`); the TLS arm on a peer
that stops reading. Each: open at the bound − 1 ms, no complete Logout at the peer, EOF at the bound (plain) or the
close at the bound plus `tls_close_timeout` with no close_notify (TLS), `onLogout` once, no wait after it,
`logon_timeout_ms` set well above the bound:
- [RED] `WriteLimit105.<R1|R2AfterActive|R2BeforeActive|Reply>_LogoutBlocked_ClosedAtTheBound` (the Logout is the
  blocked write); for R-1 the slot-74 record present.
- [RED] `WriteLimit105.<R1|R2AfterActive|Reply>_LogoutQueuedBehindBlockedSend_ClosedAtTheBound` (the Logout waits at
  the write gate; for `Reply` an application send blocked in `async_write` holding the gate, then the peer's Logout:
  the receipt arm resumes, `onLogout` once, the reset unit run as 104 orders it). No pre-Active queued cell: that
  shape is unreachable on an Engine session (research D10).
- [RED] `TlsWriteLimit105.R2AfterActive_PeerStopsReading_AbortiveAtBoundPlusTlsCloseTimeout`.
- [RED] `WriteLimit105.WriteReleasedBeforeTheBound_LogoutOnWire_WaitKeepsTheSameBudget` (the control: the Logout complete
  on the wire, EOF at the same bound).
- [guard: a 0 ms limit read as "close at once"] `WriteLimit105.TimeoutZero_WriteUnbounded`.
- [RED] `WriteLimit105.R1WriteHeld_PeerLogoutArrives_LogoutCompleteThenEof` — R-1's write held, the peer's
  in-sequence Logout delivered, the write released: the Logout complete on the wire, then EOF (the latch deferred to
  the writer; mutant: the pump's latch test without the `writing` deferral).
- [guard: "Leaving `writing`" ignores `cut`] `WriteLimit105.DirectSession_WriteCompletesAfterTheBoundFires_NoWaitArmed`, in
  `test_105_seams.cpp` (it reads `cut` through test access) — an R-2
  Logout's write on `test_105_support.hpp`'s deferred-completion transport (its `async_write` commits the bytes, then
  holds its success completion behind a second flag and returns success once released, even after a close). Schedule,
  pinned: advance the mock clock to the bound; `run_one` until the bound has taken row 4 (reach proof through
  `session_test_access`: `cut` set, or the close-frame counter at 1); release the completion; drain by a bounded drive.
  Positive control: the peer holds the whole Logout, and `cut` was set when the leaf returned. Then: no wait armed,
  `Disconnected` and `onLogout` once, the session not left Active, EOF with no further advance. GREEN on the base by
  design (no wait exists there); its RED is the mutant, under which the phase reads `armed` after the drain (asserted
  through test access too; data-model E-3, `ending_wait_.cut`; Gate A loop 2 round 2, N-1).
- [RED] `WriteLimit105.R1WriteHeld_LimitFiresInsideStoreCall_EofAtBoundOnLogoutAfterStore` — the limit fires while
  the Logout's store call is held: EOF at the bound, `onLogout` when the store returns.

**R-1 and timing (C-3, C-6)** — `test_105_liveness_timing.cpp`, both roles, H = 10 s on the mock clock:
- [RED] `LivenessTiming105.Default_NoTestRequestAt11s_OneAt12s` (the base sends at 10 s).
- [RED] `LivenessTiming105.Default_ActiveAt23900ms_EndedAt24sPlusGuard` (the base ends near 20 s).
- [RED] `LivenessTiming105.SetThreshold7s_TestRequestAt7s_GiveUpAt14sPlusGuard`.
- [RED] `LivenessTiming105.GuardAtExactly2T_EchoWins`, `...At2TPlus1ns_GivesUp`; [guard: the guard added by hand to a
  saturated instant] `LivenessTiming105.GuardAtTheSaturatedInstant`, a `constexpr` cell in `test_105_seams.cpp`.
- [RED] `LivenessTiming105.TrafficAfterTestRequestNoHeartbeat_NotGivenUpAt2T_GivenUpAt2TAfterLastFrame` (D-c).
- [RED] `LivenessTiming105.SilentPeer_NoHeartbeatWhileTestRequestPending` — positive control: the TestRequest at
  12 s and the session Active at 22 s; no Heartbeat at 22 s; the give-up just after 24 s.
- [RED] `LivenessTiming105.FrameAfterTestRequest_HeartbeatAt22s_TR2At25s_NoneAt35s_GiveUpAfter37s` (N-2; the
  construction proof of the sleep set, data-model E-4).
- [RED] `LivenessTiming105.FrameAfterHeartbeatInstant_HeartbeatAtTheStaleGiveUpWake` — a frame at 23 s; the Heartbeat
  just after 24 s, not before; positive control: none at 23 s.
- [RED] `LivenessTiming105.SilentAfterTestRequest_NoSpin` — a counting `Clock` decorator over `mock_clock` in
  `test_105_support.hpp` (`mock_clock` exposes no count), set as `clock_override`: one `sleep_until` per wake, a
  bounded number from TR1 to the give-up. The executor is driven by a bounded number of handler runs per mock-clock
  step (`run_one` × N), never run-until-idle, because under the mutant every sleep completes at once. Mutant: the
  passed, ineligible Heartbeat instant kept in the sleep set; its RED arm terminates as a failed count.
- [RED] `LivenessTiming105.SecondTestRequest_LateAnswerToTheFirst_StaysActive` (D-d: TR2 stays pending; the give-up
  2T after that frame).
- [RED] `LivenessTiming105.GiveUp_LogoutTextPinned_ThenEof` — Text(58) spelled out in the cell.
- [RED] `LivenessTiming105.GiveUp_<LogoutDoesNotFit|ToAdminThrows|AssignFails|StoreFailsPersistent|StoreFailsVolatile|WriteFails>_StillEof`
  — each asserts the slot-74 record, the wire (no complete Logout, except `StoreFailsVolatile`) and EOF; each has
  its own mutant. [seam: the bound's fail-closed `catch` replaced by round 2's] `LivenessTiming105.GiveUp_BoundSpawnFails_StillEof`
  (the slot-74 record, no Logout, EOF).
- [RED] `LivenessTiming105.GiveUp_OneLogRecordWithSlot74` — capturing sink; the record precedes the Logout's write.
- [RED] `LivenessTiming105.GapOpen_PeerSendsNonAdvancingFrame_DeadlineLogout`, `...GapOpen_PeerSilent_R1LogoutAt2T`.
- [guard: the loop's HeartBtInt test deleted] `LivenessTiming105.HeartBtIntZero_NoTestRequest`; [RED]
  `...HeartBtIntNegative_NoTestRequest` (mutant: the test reverted to `== 0`).
- [RED] `LivenessTiming105.ThresholdOverflow_Saturates`; [guard: the product formed unsaturated]
  `LivenessTiming105.DefaultThresholdHelper_Saturates`, `constexpr` cells of the default-threshold helper in
  `test_105_seams.cpp`.
- Config: [RED] `LoadNegativeBattery.TestRequestThreshold_Zero_OutOfRange`; [guard: the duration parser's
  leading-digit refusal deleted] `...TestRequestThreshold_Negative_MalformedValue`; [RED] `SessionOpen105.Threshold<Zero|Negative>_InvalidConfig`,
  `EngineRegister105.Threshold<Zero|Negative>_InvalidConfig`.

**#582 and the 112 rule (C-5)** — `test_105_heartbeat_knob_off.cpp`:
- [RED] `HeartbeatKnobOff105.AheadPeerAnswersTenCycles_StaysActive` (SC-003).
- [RED] `HeartbeatKnobOff105.MismatchedTestReqId_EndsAndCloses`; [RED] `...AbsentTestReqId_Answers`,
  `...BehindPeerAnswers`.
- [guard: the #582 predicate widened to the knob-on route] `HeartbeatKnobOff105.KnobOn_Unchanged` (B-537-3's cells
  stay GREEN unedited).
- [RED] `TestReqId105.<InSequenceArm|AnswerPendingRoute>_EarlierId_NeitherAnswersNorEnds`, each with the mutant that
  restores the plain comparison at that site; [RED] `...PreviousConnectionId_Mismatch` (across `drive_reconnect`);
  [guard: the canonical-form test deleted] `...LeadingZeroId_Mismatch`; [guard: the pending test deleted]
  `...NoTestRequestPending_NotCompared`; [guard: a non-modular comparison; the full-cycle case dropped]
  `TestReqIdWindow105.<AtUint32Max|AcrossTheWrap|OneFullCycle_NoValueEarlier>`, `constexpr` cells (with runtime twins)
  over the free window function the member calls, in `test_105_seams.cpp` (data-model E-4; Gate A round 3, Codex #7;
  tagged in loop 2 round 2, Codex #3).

**Records (C-7)** — `test_105_presession_log.cpp`, capturing sink:
- [RED] `PreSessionLog105.<NotTls|TlsHandshake|NoTimeLeft|FirstFrameTimeout|FirstFrameOverBudget|FirstFrameOverLimit|NoIds|NoSession>_OneRecord`.
- [RED] `PreSessionLog105.NoSession_RecordNamesCompIds`, `...LongCompIds_CutAt15Bytes`.
- [RED] `PreSessionLog105.Flood_BoundedWithSuppressedCount`.
- [RED] `PreSessionLog105.ListenerBindFails_OneRecord` (port in use; the `std::system_error` arm, the error's value).
- [seam: the generic catch arm's record deleted] `PreSessionLog105.ListenerConstructionThrowsNonSystemError_OneRecordErrorZero`,
  in `test_105_seams.cpp` — a listener-construction seam in `run_accept_loop` throwing `std::bad_alloc` (no
  configuration reaches the generic arm; research D8).

**C ABI (C-9)** — extend `tests/capi/recovery_liveness_capi_witness_test.cpp`: [RED] the 1.18 observers through the C
ABI, `fixpp_session_close` (graceful and terminal) and callback delivery during the wait included, with a positive
control (a well-formed message afterwards reaches `cb`, or the session is shown live first).

**Census rows, not cells**: data-model E-2 I-5 (a write of `Disconnected` over `Disconnected` owes nothing) has no
peer-visible observable, so round 1's `ConnectionClose105.SecondDisconnectedWrite_OwesNothing` and its mutant are
dropped; the census table records each such write with its synchronous chain.

## 4. Supersession pins that flip

Re-run each after the fix; each must now assert the new rule (renamed where its name states the old one):
`GapDeadlineOutOfSequenceSite.FrameAtTheBoundDrawsTheLogoutAndTheTransportStaysOpen`,
`Capi537Witness.Deadline_CompIdsLeaveNoRoomForTheLogout_EndsSilently`,
`InboundFrameDispositionsQ10.WithinCapMismatchInActive_DisconnectsWithoutALogout`, the #534-commented
`SecondFrameInFlight.*` and `ReconcileAfterTheTakeBack.*`, `EstablishmentTimeoutQ16.AcceptorAfterRefusedLogon_*`,
`ResendAnswerStoreFailureEngine.Acceptor789Path_SlotReadFails_ConnectionStaysOpenUntilTheEstablishmentDeadline`,
T018 `CellC_*_UnansweredTestRequestDisconnects`, the 537 control cell "D-537-6, N", any cell that pins a stale or
earlier TestReqID as an ending (`git grep -nE 'stale|different' tests/session` near `test_req_id`, read), and the
retired `DriveLogoutStub_*` / `Initiator_DriveLogout_*`.

## 5. Mechanism mutants (in a scratch copy, never the PR's worktree)

Each must turn its cells RED, run unfiltered:
- the pump's latch test deleted (class P and the first-frame cells);
- `release_connection_end_` made a no-op (class D and A cells);
- the owe refused whenever a wait is `armed` (round 1's predicate; the W-9 cells);
- the edge's owe moved below the no-application return (`PumpSilentEnding_NoApplication_*`);
- `expire()` without its `ended` test (the no-timeout-event cells, `PreActive_WaitArmedAtEstablishmentDeadline_ClosedNoEvent`
  included);
- the R-2 bound's deadline read from the last send, or from the write's completion, instead of the write's start
  (`Bound_DoesNotSlideWithAnAnswerSent`, `WriteReleasedBeforeTheBound_*`);
- the response test deleted, and moved below the too-high branch (the off-NextNumIn `*_EndsWaitAndResets` cells);
- `send_ending_logout_`'s absorb branch deleted, and `end_session_silently_` recording while armed (`Absorbed_*`);
- `inbound_resend_request_leaf_`'s assignment failure routed through `end_session_silently_` (absorption by return
  value; `TooHighDuringWait_ResendRequestAssignFails_*`);
- one post-suspension test of the liveness loop reverted to the state test alone (`LivenessDuringWait_*`);
- the bound's live predicate testing `closing_or_stopped_()` (`CloseGracefulDuringWait_*`);
- the bound exiting on any wake, and the arming after the refusal's own `Disconnected` write (`PreActive_BoundSurvives*`);
- the claim's result ignored, so a wait is armed (`PendingTooHighMark_*`);
- the write limit deleted (every `WriteLimit105.*` cell; the spurious-hit arm: the connection stays open past the bound);
- the write limit's close made through the latch consumer's `inbound_depth_` test (`WriteLimit105.R2*` and `Reply_*`);
- the bound's expiry close made through `release_connection_end_` (`Bound_ExpiresWhileResendAnswerBlocked_*`);
- the establishment cap's arm deleted (`PreActive_<LogoutBlockedInWrite|LogoutHeldInStore>_ClosedAtEstablishmentDeadlineNoEvent`,
  `PreActive_ClockOverride_CapOnEngineClock`);
- the bound's failure exit routed through `release_connection_end_` (`EndingBound_RaceThrows_Writing*`,
  `EndingBound_DeadlineArmAbsorbsException_Writing*`);
- the armed failure exit routed through `release_connection_end_` (`EndingBound_RaceThrows_Armed_ResendAnswerBlocked_EofAtOnce`);
- the bound's early deadline-arm completion read as a re-test (`EndingBound_DeadlineArmAbsorbsException_*`;
  terminates under the bounded drive);
- each spawn's fail-closed `catch` replaced by round 2's (`CloseFrameSpawnFails_*`, `BoundSpawnFails_*`);
- `close()`'s second join deleted (`Lifetime105.CloseFrame_SpawnedByWriteLimitDuringGracefulClose`); the bound's join
  deleted, and the close frame's first join deleted (the guards `Lifetime105.<EndingLogoutBound|CloseFrame>_SessionDestroyedAfterClose`);
- the passed, ineligible Heartbeat instant kept in the sleep set (`SilentAfterTestRequest_NoSpin`; terminates
  under the bounded drive);
- each listener catch arm's record deleted (`ListenerBindFails_OneRecord`,
  `ListenerConstructionThrowsNonSystemError_OneRecordErrorZero`);
- the latch consumers' `writing` deferral deleted (`R1WriteHeld_PeerLogoutArrives_*`);
- "Leaving `writing`" ignoring `cut` (`DirectSession_WriteCompletesAfterTheBoundFires_NoWaitArmed`);
- the slot-74 record moved after the write (`WriteLimit105.R1_*`'s record assertion);
- each `owe_connection_end_()` call site deleted;
- the give-up's eligibility ignoring inbound since the TestRequest (`TrafficAfterTestRequestNoHeartbeat_*`);
- the Heartbeat's eligibility ignoring inbound since the TestRequest (`FrameAfterTestRequest_HeartbeatAt22s_*`);
- the default threshold set back to H (the timing cells);
- the #582 predicate set back (`AheadPeerAnswersTenCycles_*`);
- each 112 site's plain comparison restored (`TestReqId105.*_EarlierId_*`, `SecondTestRequest_LateAnswerToTheFirst_*`);
- the window function's comparison made non-modular, and its full-cycle case dropped (`TestReqIdWindow105.*`);
- the default-threshold product formed unsaturated, and the 1 ns guard added by hand to a saturated instant
  (`LivenessTiming105.<DefaultThresholdHelper_Saturates|GuardAtTheSaturatedInstant>`).

## 6. Lanes and full runs

```bash
for p in linux-clang-debug linux-clang-asan linux-clang-ubsan linux-clang-tsan; do
  cmake --build --preset "$p" --target session_105_tests session_105_seam_tests && ctest --preset "$p" -L 105 --no-tests=error
done
ctest --preset linux-clang-debug -L session --no-tests=error
ctest --preset linux-clang-debug -L capi --no-tests=error
ctest --preset linux-clang-debug -L config --no-tests=error
python3 -m pytest bindings/python/tests
python3 tools/check_line_citations.py --range origin/main..HEAD
```
Then gcc-release of the touched targets, the MSVC build of the feature's targets (sandbox lock first), the paired
A-B-A-B benchmark runs of research D10 (`on_inbound_frame_bench`, `session_send_bench`, `engine_send_bench` with its
new pump case; contract C-12), and `/speckit-verify`.
