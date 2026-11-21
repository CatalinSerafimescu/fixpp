// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_105_seams.cpp — 105-transport-liveness (tasks.md T003).
//
// Every cell, or reach-proof twin, that names a symbol 105 adds (tasks.md, Execution rules,
// "Cells that cannot compile on the base"). Its [guard] static_asserts sit under
// #if FIXPP_105_STATIC_GUARDS (tasks.md, Execution rules, "Guard static_asserts").
//
// The ctest entry registers this file whether or not it holds cells yet, so it compiles on its
// own, on the base as at the head; each cell task of tasks.md appends its cells here.
