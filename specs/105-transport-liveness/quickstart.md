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

## 2. RED on the base, or a guard with its mutant

Every cell in section 3 is marked. **[RED]**: written first, run on `52aeed4e` (a scratch worktree at the base, on
the build volume), and must fail there for its stated reason. **[guard: <mutant>]**: a rule the base already keeps,
GREEN on the base by design; it must fail under the named mutant (section 5). A [RED] cell whose oracle the base
could meet for another reason (the session already `Disconnected`) carries the positive control named with it. The
mechanism mutants of section 5 must fail too.

```bash
cmake --build --preset linux-clang-debug --target session_105_tests
ctest --preset linux-clang-debug -L 105 --no-tests=error -V
```

## 3. Scenarios, by cell

Names are proposals; the implementer keeps the scenario, its assertion and its mark.

**Connections close (C-1, C-4, C-8)** — `test_105_connection_close.cpp`, both roles:
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
- [guard: the spawn's `catch` deleted] `ConnectionClose105.CloseFrameSpawnFails_CloseStillReturnsCounterZero` (an
  allocation-failure seam at the close frame's spawn; `close()` returns, the counter is 0, the peer sees EOF from
  `close()`).
- [RED] `TlsClose105.<each class>_PeerReceivesCloseNotify` — `test_105_tls_close.cpp` on the TLS loopback harness.

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
  `...PreActive_BoundCappedByDeadlineNoEvent` (Engine), `...PreActive_DirectSession_BoundAloneEndsWait`,
  `...PreActive_CloseDuringWait_OneClose`.
- [RED] `LogoutWait105.PendingTooHighMark_GapDeadlineLogout_EofAfterWrite` (C-2.8, N-5).
- [RED] `LogoutWait105.OnLogoutFiresAtWaitEnd`.
- [guard: the bound spawn's `catch` deleted] `LogoutWait105.BoundSpawnFails_EndsAsTimeoutZero` — after Active and
  before Active alike: no wait armed, EOF after the write.
- [RED] `Lifetime105.<EndingLogoutBound|CloseFrame>_ClosedSessionDestroyedAfterExit` (ASan lane).

**The write limit (C-2.7, C-8.5)** — `test_105_write_limit.cpp`, both roles, on a transport whose write is held
until it is closed (the scripted transport's `write_hold`, or a seam in `test_105_support.hpp`); the TLS arm on a peer
that stops reading. Each: open at the bound − 1 ms, no complete Logout at the peer, EOF at the bound (plain) or the
close at the bound plus `tls_close_timeout` with no close_notify (TLS), `onLogout` once, no wait after it,
`logon_timeout_ms` set well above the bound:
- [RED] `WriteLimit105.<R1|R2AfterActive|R2BeforeActive|Reply>_LogoutBlocked_ClosedAtTheBound` (the Logout is the
  blocked write); for R-1 the slot-74 record present.
- [RED] `WriteLimit105.<R1|R2AfterActive>_LogoutQueuedBehindBlockedSend_ClosedAtTheBound` (the Logout waits at the
  write gate).
- [RED] `TlsWriteLimit105.R2AfterActive_PeerStopsReading_AbortiveAtBoundPlusTlsCloseTimeout`.
- [RED] `WriteLimit105.WriteReleasedBeforeTheBound_LogoutOnWire_WaitKeepsTheSameBudget` (the control: the Logout complete
  on the wire, EOF at the same bound).
- [guard: a 0 ms limit read as "close at once"] `WriteLimit105.TimeoutZero_WriteUnbounded`.
- [RED] `WriteLimit105.R1WriteHeld_PeerLogoutArrives_LogoutCompleteThenEof` — R-1's write held, the peer's
  in-sequence Logout delivered, the write released: the Logout complete on the wire, then EOF (the latch deferred to
  the writer; mutant: the pump's latch test without the `writing` deferral).
- [RED] `WriteLimit105.R1WriteHeld_LimitFiresInsideStoreCall_EofAtBoundOnLogoutAfterStore` — the limit fires while
  the Logout's store call is held: EOF at the bound, `onLogout` when the store returns.

**R-1 and timing (C-3, C-6)** — `test_105_liveness_timing.cpp`, both roles, H = 10 s on the mock clock:
- [RED] `LivenessTiming105.Default_NoTestRequestAt11s_OneAt12s` (the base sends at 10 s).
- [RED] `LivenessTiming105.Default_ActiveAt23900ms_EndedAt24sPlusGuard` (the base ends near 20 s).
- [RED] `LivenessTiming105.SetThreshold7s_TestRequestAt7s_GiveUpAt14sPlusGuard`.
- [RED] `LivenessTiming105.GuardAtExactly2T_EchoWins`, `...At2TPlus1ns_GivesUp`; a `constexpr` cell of the guard at
  the saturated instant.
- [RED] `LivenessTiming105.TrafficAfterTestRequestNoHeartbeat_NotGivenUpAt2T_GivenUpAt2TAfterLastFrame` (D-c).
- [RED] `LivenessTiming105.SilentPeer_NoHeartbeatWhileTestRequestPending` — positive control: the TestRequest at
  12 s and the session Active at 22 s; no Heartbeat at 22 s; the give-up just after 24 s.
- [RED] `LivenessTiming105.FrameAfterTestRequest_HeartbeatAt22s_TR2At25s_NoneAt35s_GiveUpAfter37s` (N-2).
- [RED] `LivenessTiming105.SecondTestRequest_LateAnswerToTheFirst_StaysActive` (D-d: TR2 stays pending; the give-up
  2T after that frame).
- [RED] `LivenessTiming105.GiveUp_LogoutTextPinned_ThenEof` — Text(58) spelled out in the cell.
- [RED] `LivenessTiming105.GiveUp_<LogoutDoesNotFit|ToAdminThrows|AssignFails|StoreFailsPersistent|StoreFailsVolatile|WriteFails>_StillEof`
  — each asserts the slot-74 record, the wire (no complete Logout, except `StoreFailsVolatile`) and EOF; each has
  its own mutant.
- [RED] `LivenessTiming105.GiveUp_OneLogRecordWithSlot74` — capturing sink; the record precedes the Logout's write.
- [RED] `LivenessTiming105.GapOpen_PeerSendsNonAdvancingFrame_DeadlineLogout`, `...GapOpen_PeerSilent_R1LogoutAt2T`.
- [guard: the loop's HeartBtInt test deleted] `LivenessTiming105.HeartBtIntZero_NoTestRequest`; [RED]
  `...HeartBtIntNegative_NoTestRequest` (mutant: the test reverted to `== 0`).
- [RED] `LivenessTiming105.ThresholdOverflow_Saturates`; `constexpr` cells of the default-threshold helper.
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
  `...NoTestRequestPending_NotCompared`.

**Records (C-7)** — `test_105_presession_log.cpp`, capturing sink:
- [RED] `PreSessionLog105.<NotTls|TlsHandshake|NoTimeLeft|FirstFrameTimeout|FirstFrameOverBudget|FirstFrameOverLimit|NoIds|NoSession>_OneRecord`.
- [RED] `PreSessionLog105.NoSession_RecordNamesCompIds`, `...LongCompIds_CutAt15Bytes`.
- [RED] `PreSessionLog105.Flood_BoundedWithSuppressedCount`.
- [RED] `PreSessionLog105.ListenerBindFails_OneRecord` (port in use).

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
- `expire()` without its `ended` test (the no-timeout-event cells);
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
- the latch consumers' `writing` deferral deleted (`R1WriteHeld_PeerLogoutArrives_*`);
- the slot-74 record moved after the write (`WriteLimit105.R1_*`'s record assertion);
- each `owe_connection_end_()` call site deleted;
- the give-up's eligibility ignoring inbound since the TestRequest (`TrafficAfterTestRequestNoHeartbeat_*`);
- the Heartbeat's eligibility ignoring inbound since the TestRequest (`FrameAfterTestRequest_HeartbeatAt22s_*`);
- the default threshold set back to H (the timing cells);
- the #582 predicate set back (`AheadPeerAnswersTenCycles_*`);
- each 112 site's plain comparison restored (`TestReqId105.*_EarlierId_*`, `SecondTestRequest_LateAnswerToTheFirst_*`).

## 6. Lanes and full runs

```bash
for p in linux-clang-debug linux-clang-asan linux-clang-ubsan linux-clang-tsan; do
  cmake --build --preset "$p" --target session_105_tests && ctest --preset "$p" -L 105 --no-tests=error
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
