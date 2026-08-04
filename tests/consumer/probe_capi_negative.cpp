// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/consumer/probe_capi_negative.cpp
//
// 086 T013 (US1, FR-006/FR-008) — the ❌ cell: `fixpp::capi` MUST NOT reach a C++
// engine header (contracts/include-interface.md §1 row 4).
//
// ⚠️ THIS FILE IS NOT A BUILD TARGET. It is the SOURCE argument of a
// configure-time try_compile() in tests/consumer/CMakeLists.txt. It cannot be a
// target: run_consumer_witness.cmake:107 raises FATAL_ERROR on ANY non-zero
// build exit, so a must-fail target would red the whole witness (contracts §4a —
// the reason the earlier "OBJECT library that must fail to build" wording was
// unimplementable).
//
// ── THE POLARITY IS INVERTED ON PURPOSE — Gate B r1 P1 #2 ────────────────────
//
// This probe is asserted to COMPILE (try_compile TRUE), not to fail.
//
// The earlier form was `#include <fixpp/wire/parser.hpp>` with the try_compile
// asserted FALSE, which made "did not compile" the whole assertion. That is a
// FALSE GREEN generator: a syntax error, a missing third-party include path, a
// language-standard mismatch, or a future missing transitive dependency all
// produce FALSE and were indistinguishable from the isolation working. Adding
// `#error unrelated_failure` to the old probe left the witness green.
//
// `__has_include` tests LOOKUP without parsing the forbidden header, so:
//   * header NOT reachable (correct)  -> the #error is skipped, TU compiles -> TRUE
//   * header IS reachable (defect)    -> the #error fires with a UNIQUE token -> FALSE
//   * anything else breaks            -> FALSE, but WITHOUT the token
// The CMake side asserts TRUE and, on FALSE, classifies by whether the token
// appears — so an isolation breach and a broken probe are both red and are
// reported as different things.

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
