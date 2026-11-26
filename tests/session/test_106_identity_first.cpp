// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_106_identity_first.cpp — 106-logon-guards-write-bounds (tasks.md T003).
//
// The identity-first and NextNumIn cells (research D1, D2; contract C-1, C-2) that compile on the base.
//
// The ctest entry registers this file whether or not it holds cells yet, so it compiles on its
// own, on the base as at the head; each cell task of tasks.md appends its cells here.

#include <gtest/gtest.h>
