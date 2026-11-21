// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_105_heartbeat_knob_off.cpp — 105-transport-liveness (tasks.md T003).
//
// An out-of-sequence Heartbeat with sequence validation off, and the TestReqID rule (research D5).
//
// The ctest entry registers this file whether or not it holds cells yet, so it compiles on its
// own, on the base as at the head; each cell task of tasks.md appends its cells here.
