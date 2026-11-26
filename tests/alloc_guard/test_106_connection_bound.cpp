// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/alloc_guard/test_106_connection_bound.cpp — 106-logon-guards-write-bounds (tasks.md T004).
//
// The connection bound's zero-allocation cell, alloc_guard_106_connection_bound_mallocnesia
// (research D9, the connection bound's zero-allocation row). tasks.md T108 writes its send half
// and its emit half. With FIXPP_106_ALLOC_BASE set to 1 the TU builds only the emit half with the
// limit off and names no symbol 106 adds, so the base tree can build it (tasks.md T004).
//
// The registration exists whether or not the cases are written yet, so this file compiles on its
// own, on the base as at the head.

#include <gtest/gtest.h>

#if !defined(FIXPP_106_ALLOC_BASE)
#error "FIXPP_106_ALLOC_BASE must be defined to 0 or 1 by tests/alloc_guard/CMakeLists.txt"
#endif
