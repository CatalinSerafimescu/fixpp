// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/consumer/probe_capi_negative.cpp
//
// 086 T013 (US1, FR-006/FR-008) — the ❌ cell: `fixpp::capi` MUST NOT reach a C++
// engine header (contracts/include-interface.md §1 row 4).
//
// ⚠️ THIS FILE IS AN ORDINARY BUILD TARGET (an OBJECT library) AND MUST COMPILE.
// Building it IS the assertion: run_consumer_witness.cmake:107 raises
// FATAL_ERROR on ANY non-zero build exit, which is exactly the gate this probe
// needs, and the driver names the target in _required_targets so deleting it
// fails the build too.
//
// It was the SOURCE argument of a configure-time try_compile() until Gate B r3,
// when CI (windows-msvc-debug) proved that form cannot resolve Conan's
// CONAN_LIB::…_DEBUG closure inside CMake's scratch project — every Debug run
// failed for a reason unrelated to include reachability. See contracts §4a r3.
//
// ── THE POLARITY IS INVERTED ON PURPOSE — Gate B r1 P1 #2 ────────────────────
//
// This probe is asserted to COMPILE, not to fail. (That inversion is also what
// made an ordinary target possible: while a ❌ cell had to FAIL to compile it
// could not be a target at all, since a must-fail target reds the whole witness.)
//
// The earlier form was `#include <fixpp/wire/parser.hpp>` asserted NOT to
// compile, which made "did not compile" the whole assertion. That is a
// FALSE GREEN generator: a syntax error, a missing third-party include path, a
// language-standard mismatch, or a future missing transitive dependency all
// produce a failed compile and were indistinguishable from the isolation
// working. Adding `#error unrelated_failure` to the old probe left the witness
// green.
//
// `__has_include` tests LOOKUP without parsing the forbidden header, so:
//   * header NOT reachable (correct)  -> the #error is skipped, TU compiles -> PASS
//   * header IS reachable (defect)    -> the #error fires with a UNIQUE token -> FAIL
//   * anything else breaks            -> FAIL, but WITHOUT the token
// The build output is echoed verbatim by the driver, so an isolation breach and
// a broken probe are both red and are distinguishable in the log by whether the
// token appears. Re-measured under the target carrier (Gate B r3): appending
// `#error unrelated_failure_counter_test` reds the witness with the token
// appearing 0 times.

#if __has_include(<fixpp/wire/parser.hpp>)
#error FIXPP_086_FORBIDDEN_HEADER_REACHABLE
#endif

// The probe header is chosen so the assertion cannot pass for the wrong reason
// (FR-008): <fixpp/wire/parser.hpp> is a shipped public engine header whose own
// disappearance from the package would itself be a defect. A probe naming a
// header that does not exist anywhere would report "not reachable" for a reason
// that has nothing to do with include isolation — which is why the paired
// umbrella probe (probe_umbrella.cpp) includes this same header for real and
// would go red if it ever stopped shipping.

int fixpp_086_capi_engine_probe();
int fixpp_086_capi_engine_probe() { return 0; }
