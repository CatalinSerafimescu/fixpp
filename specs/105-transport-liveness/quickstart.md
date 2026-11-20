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

## 2. RED on the base

Every cell below is written first and run on `52aeed4e` (a scratch worktree at the base, on the build volume),
and must fail for its stated reason. The mechanism mutants of section 5 must fail too.

```bash
cmake --build --preset linux-clang-debug --target session_105_tests
ctest --preset linux-clang-debug -L 105 --no-tests=error -V
```

## 3. Scenarios, by cell

Names are proposals; the implementer keeps the scenario and its assertion.

**Connections close (C-1, C-4, C-8)** — `test_105_connection_close.cpp`, both roles:
- `ConnectionClose105.PumpSilentEnding_<site>_PeerSeesEofAtOnce` — one cell per class-P site of the census table
  (TestReqID mismatch, BeginString guard, CompID guard, MsgSeqNum 0, D-3); mock clock frozen; assert peer EOF.
- `ConnectionClose105.PeerLogoutInActive_EofAfterReplyAndReset` — `reset_on_logout` on; the peer reads the reply,
  then EOF; the store holds (1, 1) before EOF.
- `ConnectionClose105.TooHighLogoutAcknowledged_EofAfterReset` — the bound's route.
- `ConnectionClose105.LivenessToAdminThrows_EofAtOnce`, `...LivenessAssignFails_EofAtOnce`,
  `...LivenessStoreFails_EofAtOnce`.
- `ConnectionClose105.SendStoreFailure_EofAfterSendReturns`.
- `ConnectionClose105.AcceptorFirstFrameError_EofAtOnce`, `...AcceptorFirstFrameRefused_EofAtOnceNoTimeoutEvent`.
- `ConnectionClose105.PreActiveRefusal_<D-1|D-2|D-3|NotConnected|LogonSent>_EofBeforeDeadlineNoTimeoutEvent` — a
  short `logon_timeout_ms`; assert EOF with the clock frozen and no `session_event_establishment_timeout`.
- `ConnectionClose105.SilentPeer_StillClosedAtEstablishmentDeadlineWithEvent` (B-093-4 kept).
- `ConnectionClose105.OnLogoutCallsClose_OneClose`, `...EngineStopRacesEndingClose_OneClose` (TSan lane).
- `ConnectionClose105.DirectInitiator_EndingThenDriveReconnect_Reconnects`.
- `ConnectionClose105.InsideClose_NoSecondClose`, `...InsideReconnectDrain_DrainKeepsItsBound`.
- `TlsClose105.<each class>_PeerReceivesCloseNotify` — `test_105_tls_close.cpp` on the TLS loopback harness.

**The R-2 wait (C-2)** — `test_105_logout_wait.cpp`, both roles:
- `LogoutWait105.<TooLow|GapDeadline|SendingTimeReject|OrigSendingTimeReject>_PeerLogoutEndsWait` — EOF after the
  peer's Logout, not before; with `reset_on_logout` the receipt resets.
- `LogoutWait105.Bound_EofAtExactlyTheBound` — open at bound − 1 ms, EOF at the bound.
- `LogoutWait105.Bound_DoesNotSlideWithAnAnswerSent` — a Heartbeat answering the peer's TestRequest mid-wait.
- `LogoutWait105.AppMessageDuringWait_ReachesFromAppAndAdvances`, `...TestRequestDuringWait_Answered`,
  `...ResendRequestDuringWait_Answered`, `...TooLowDuringWait_NoSecondLogout`, `...TooHighDuringWait_SequenceChecks`.
- `LogoutWait105.SecondEndingDuringWait_NoSecondLogoutBoundHolds`, `...SilentEndingDuringWait_BoundHolds`.
- `LogoutWait105.SendDuringWait_Refused`, `...LivenessDuringWait_SendsNothing`.
- `LogoutWait105.TimeoutZero_EofAfterWrite`, `...NoClock_EofAfterWrite`.
- `LogoutWait105.<CloseGraceful|CloseTerminal|EngineStop>DuringWait_OneClose`.
- `LogoutWait105.PreActiveTooLowLogon_PeerLogoutEndsWaitNoReset` (OQ-1), `...PreActive_BoundCappedByDeadlineNoEvent`.
- `LogoutWait105.OnLogoutFiresAtWaitEnd`.

**R-1 and timing (C-3, C-6)** — `test_105_liveness_timing.cpp`, both roles, H = 10 s on the mock clock:
- `LivenessTiming105.Default_NoTestRequestAt11s_OneAt12s` (RED: the base sends at 10 s).
- `LivenessTiming105.Default_ActiveAt23900ms_EndedAt24s` (RED: the base ends near 20 s).
- `LivenessTiming105.SetThreshold7s_TestRequestAt7s_GiveUpAt14s`.
- `LivenessTiming105.GuardAtExactly2T_EchoWins`, `...At2TPlus1ns_GivesUp`.
- `LivenessTiming105.GiveUp_LogoutTextPinned_ThenEof` — Text(58) spelled out in the cell.
- `LivenessTiming105.GiveUp_LogoutDoesNotFit_StillEof`, `...ToAdminThrows_StillEof`, `...StoreFails_StillEof`.
- `LivenessTiming105.GiveUp_OneLogRecordWithSlot74` — capturing sink.
- `LivenessTiming105.GapOpenDefaultT_DeadlineLogoutComesFirst`, `...ThresholdBelowH_GiveUpComesFirst`.
- `LivenessTiming105.HeartBtIntZero_NoTestRequest` (and negative, if SI-2 is accepted).
- `LivenessTiming105.ThresholdOverflow_Saturates`; `constexpr` cells of the default-threshold helper.
- Config: `LoadNegativeBattery.TestRequestThreshold_Zero_OutOfRange` (and negative), `SessionOpen105.ThresholdZero_InvalidConfig`,
  `EngineRegister105.ThresholdZero_InvalidConfig`.

**#582 (C-5)** — `test_105_heartbeat_knob_off.cpp`:
- `HeartbeatKnobOff105.AheadPeerAnswersTenCycles_StaysActive` (SC-003; RED on the base).
- `HeartbeatKnobOff105.MismatchedTestReqId_EndsAndCloses`, `...AbsentTestReqId_Answers`, `...BehindPeerAnswers`.
- `HeartbeatKnobOff105.KnobOn_Unchanged` (B-537-3's cells stay GREEN unedited).

**Records (C-7)** — `test_105_presession_log.cpp`, capturing sink:
- `PreSessionLog105.<NotTls|TlsHandshake|NoTimeLeft|FirstFrameTimeout|FirstFrameOverBudget|FirstFrameOverLimit|NoIds|NoSession>_OneRecord`.
- `PreSessionLog105.NoSession_RecordNamesCompIds`, `...LongCompIds_CutAt15Bytes`.
- `PreSessionLog105.Flood_BoundedWithSuppressedCount`.
- `PreSessionLog105.ListenerBindFails_OneRecord` (port in use).

**C ABI (C-9)** — extend `tests/capi/recovery_liveness_capi_witness_test.cpp`: the 1.18 observers through the C
ABI, with a positive control (a well-formed message afterwards reaches `cb`, or the session is shown live first).

## 4. Supersession pins that flip

Re-run each after the fix; each must now assert the new rule (renamed where its name states the old one):
`GapDeadlineOutOfSequenceSite.FrameAtTheBoundDrawsTheLogoutAndTheTransportStaysOpen`,
`Capi537Witness.Deadline_CompIdsLeaveNoRoomForTheLogout_EndsSilently`,
`InboundFrameDispositionsQ10.WithinCapMismatchInActive_DisconnectsWithoutALogout`, the #534-commented
`SecondFrameInFlight.*` and `ReconcileAfterTheTakeBack.*`, `EstablishmentTimeoutQ16.AcceptorAfterRefusedLogon_*`,
`ResendAnswerStoreFailureEngine.Acceptor789Path_SlotReadFails_ConnectionStaysOpenUntilTheEstablishmentDeadline`,
T018 `CellC_*_UnansweredTestRequestDisconnects`, the 537 control cell "D-537-6, N", and the retired
`DriveLogoutStub_*` / `Initiator_DriveLogout_*`.

## 5. Mechanism mutants (in a scratch copy, never the PR's worktree)

Each must turn its cells RED, run unfiltered:
- the pump's latch test deleted (class P and the first-frame cells);
- `release_connection_end_` made a no-op (class D and A cells);
- `expire()` without its `ended` test (the no-timeout-event cells);
- the R-2 bound's deadline read from the last send instead of the write (`Bound_DoesNotSlideWithAnAnswerSent`);
- `send_ending_logout_`'s absorb branch deleted (`SecondEndingDuringWait_*`);
- the default threshold set back to H (the timing cells);
- the #582 predicate set back (`AheadPeerAnswersTenCycles_*`).

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
Then gcc-release of the touched targets, the MSVC build of the feature's targets (sandbox lock first), the
paired benchmark of research D10, and `/speckit-verify`.
