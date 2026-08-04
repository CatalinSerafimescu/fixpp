// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/consumer/probe_capi_negative_service.cpp
//
// 086 T014 (US1, FR-006) — a SECOND, DISTINCT ❌ cell for `fixpp::capi`: the
// service plugin header MUST NOT resolve either
// (contracts/include-interface.md §1 row 3, `fixpp::capi` column).
//
// Not covered by probe_capi_negative.cpp. <fixpp/service/control_plane_factory.hpp>
// is the one C++ header this feature deliberately republishes at a SECOND
// installed root (include/service-iface/), so a mis-wired `fixpp::capi` that
// picked up that root would leak this cell while the <fixpp/wire/parser.hpp>
// probe still passed. Measured FALSE at ISO=ON in research.md R4 row 4.
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

#if __has_include(<fixpp/service/control_plane_factory.hpp>)
#error FIXPP_086_FORBIDDEN_HEADER_REACHABLE
#endif

int fixpp_086_capi_service_probe();
int fixpp_086_capi_service_probe() { return 0; }
