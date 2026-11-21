// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_105_seams.cpp — 105-transport-liveness (tasks.md T003).
//
// Every cell, or reach-proof twin, that names a symbol 105 adds (tasks.md, Execution rules,
// "Cells that cannot compile on the base"). Its [guard] static_asserts sit under
// #if FIXPP_105_STATIC_GUARDS (tasks.md, Execution rules, "Guard static_asserts").
//
// Created by T003 as a translation unit with no cells, so that the ctest entry configures
// before its cells exist; each cell task of tasks.md appends its cells here.
