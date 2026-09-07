#pragma once

// Shared test-only helpers for the sync_* (async_mutex) seam suite.
// Hoisted from 15 byte-identical copies during the 006-async-mutex
// /simplify pass — test harness only, never linked into the primitive.

#include <asio/awaitable.hpp>

// (#289 batch 19) `yield_n` moved to tests/support/yield_n.hpp, because
// tests/support/pump_until_ready.hpp needs it and may not include this file. The
// `using` below keeps every `fixpp::sync::test::yield_n(n)` call site unchanged.
//
// ⚠️ RELATIVE, NOT `"support/yield_n.hpp"`, AND THAT IS A FIX. Five executables in
// tests/sync/CMakeLists.txt are declared with a bare `add_executable` rather than the
// `add_sync_test` helper, and only the helper puts `${CMAKE_SOURCE_DIR}/tests` on the
// include path -- so the rooted spelling compiles for most of the directory and fails
// for `test_async_mutex_aba_interleave`, `..._terminal_cas_recursive_unlock`,
// `..._acquire_livelock`, `..._chain_walk_cas_loss` and `test_am_p3_impossible_state_traps`.
// A quoted include resolves relative to THIS file first, so it needs no include path at
// all. Widening five CMake targets would work too and was declined: it is five edits to
// the build system to make one header reachable that already is.
// ⚠️ Found by a WHOLE-TREE build, not by the six targets the change was developed
// against -- and the first run of that build reported `exited with code 0` because its
// output was piped to `tail`. Check ninja's own status, not the pipeline's.
#include "../support/yield_n.hpp"

namespace fixpp::sync::test {

using fixpp::test_support::yield_n;

}  // namespace fixpp::sync::test
