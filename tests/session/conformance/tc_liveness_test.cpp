// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/conformance/tc_liveness_test.cpp
//
// [FIX-TC] Liveness subset — DEFERRED to US4 (Phase 6).
//
// Phase 5 (T037–T042 / US3) cannot deliver the [FIX-TC] 4a/4b liveness
// conformance because the outbound transport path — Session→transport
// async_write for engine-internal Heartbeat / TestRequest frames — is wired
// in US4/Phase 6 via the Session transport member + Session::send().  Without
// it, no observable outbound frame exists on the wire.  The QFJ 4a/4b oracle
// scenarios both require asserting engine-emitted frames against the
// oracle's E= lines (E=Heartbeat(35=0), E=Heartbeat(112=<TestReqID>)) — that
// assertion is not honest without the transport seam.
//
// What ships in US3 (Phase 5):
//   * src/session/heartbeat.cpp run_liveness_loop coroutine (timer body)
//   * build_heartbeat / build_test_request admin-message builders (T040)
//   * Active-state inbound dispatch for 35=0 / 35=1 + unanswered-TR →
//     Disconnected (T041 / data-model.md Active row, error slot 74)
//   * Seam #5 unit tests in tests/session/heartbeat_testrequest_test.cpp
//     covering: builder shape (HB / TR / TestReqID echo / distinctness),
//     HeartBtInt=0 disables timers (multi-window assertion), unanswered-TR
//     disconnects, inbound HB keeps session alive across multiple windows.
//
// What is DEFERRED to US4 (T046 / seam #11 ordering follow-up):
//   * Outbound Heartbeat emission per HeartBtInt window (4a oracle's E=)
//   * Outbound Heartbeat echo on inbound TestRequest with the peer's
//     TestReqID(112) (4b oracle's E=)
//   * The tc_liveness conformance executable (this file's prior body) —
//     authored fresh once the transport member exists so that frames can
//     be captured at the transport double and matched against the
//     QuickFIX/J def-file oracle.
//
// Anchors: spec US3 AC1–AC5 (covered by seam #5); spec US4 transport seam;
// data-model.md Active row; research D-10 in-scope TC-004 subset; FR-006;
// SC-004 (Heartbeat emission cadence — requires transport).
//
// This file is intentionally NOT registered as a CMake test executable
// (see tests/session/conformance/CMakeLists.txt T038 row).  It is kept on
// disk so the deferral is discoverable from the conformance directory and
// the git history preserves the RED-phase author intent.
//
// Cross-refs:
//   * specs/005-session-establishment-fsm/tasks.md T038 (deferred note)
//   * specs/005-session-establishment-fsm/tasks.md T042 (deferred note)
//   * specs/005-session-establishment-fsm/spec.md US4 (transport seam)
