// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/consumer/probe_service_negative.cpp
//
// 086 T021 (US6, FR-011b) — the ❌ cell for `fixpp::service`: it MUST NOT reach a
// C++ engine header (contracts/include-interface.md §1 row 4,
// `fixpp::service` column). [arch §8] — the service consumes the engine through
// the C ABI only.
//
// This cell has its OWN demonstrated-red obligation and its own revert: the
// $<INSTALL_INTERFACE:...> line in src/service/CMakeLists.txt ALONE -- cited by
// CONSTRUCT, not by line, because this is an INSTRUCTION a future verifier acts
// on and a stale number would have them revert a comment and observe nothing
// (FR-011e). The C-ABI revert reds this probe too -- fixpp_service links
// fixpp_capi -- so it cannot stand in for the service demonstration.
//
// ⚠️ AN ORDINARY OBJECT-LIBRARY TARGET THAT MUST COMPILE — the polarity is
// inverted on purpose (Gate B r1 P1 #2), and BUILDING it is the assertion.
// It was a configure-time try_compile() source until Gate B r3, when CI proved
// that form cannot resolve Conan's imported-target closure (contracts §4a r3).
// `__has_include` tests LOOKUP without parsing the forbidden header:
//   * not reachable (correct) -> #error skipped, compiles         -> PASS
//   * reachable (the defect)  -> #error fires with a UNIQUE token -> FAIL
//   * anything else broken    -> FAIL, but WITHOUT the token
// The old form asserted "did not compile", which made a syntax error or a
// missing third-party path indistinguishable from the isolation working.
// See probe_capi_negative.cpp for the full rationale.

#if __has_include(<fixpp/wire/parser.hpp>)
#error FIXPP_086_FORBIDDEN_HEADER_REACHABLE
#endif

int fixpp_086_service_engine_probe();
int fixpp_086_service_engine_probe() { return 0; }
