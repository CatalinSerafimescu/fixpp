// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_106_handshake.cpp — 106-logon-guards-write-bounds (tasks.md T003).
//
// The handshake-bound cells (research D6; contract C-5) that compile on the base, including
// the TOML cells that mint a TLS transport (tasks.md T003: config_044_tests defines no
// FIXPP_TLS_FIXTURE_DIR, so those cells live here).
//
// The ctest entry registers this file whether or not it holds cells yet, so it compiles on its
// own, on the base as at the head; each cell task of tasks.md appends its cells here.

#include <gtest/gtest.h>
