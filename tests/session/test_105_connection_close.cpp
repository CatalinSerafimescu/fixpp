// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_105_connection_close.cpp — 105-transport-liveness (tasks.md T003).
//
// The close of the connection at each ending class (research D1, D4; contract C-1).
//
// Created by T003 as a translation unit with no cells, so that the ctest entry configures
// before its cells exist; each cell task of tasks.md appends its cells here.

#include "session/test_105_support.hpp"  // compiled here, so a build of this entry proves it compiles
