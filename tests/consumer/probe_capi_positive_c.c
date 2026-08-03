/* SPDX-License-Identifier: AGPL-3.0-or-later
 * tests/consumer/probe_capi_positive_c.c
 *
 * 086 T012 (US1, FR-002) — the same twelve C-ABI headers as
 * probe_capi_positive.cpp, compiled as C.
 *
 * This is the only place the INSTALLED C-ABI interface is exercised from a C
 * compiler. In-tree C-cleanliness is already pinned (tests/capi/CMakeLists.txt:13,
 * :23), but that says nothing about what the installed package delivers, which is
 * what US1 promises a "C or C++ integrator". Requiring it is why
 * tests/consumer/ moved from project(... CXX) to project(... C CXX) (contracts §2a).
 *
 * COMPILE-ONLY (OBJECT library, no main) — contracts §4 / research.md R5.
 */

#include <fix/c_api.h>

#include <fix/c_api/decimal.h>
#include <fix/c_api/dict.h>
#include <fix/c_api/engine.h>
#include <fix/c_api/error.h>
#include <fix/c_api/export.h>
#include <fix/c_api/handles.h>
#include <fix/c_api/log.h>
#include <fix/c_api/message.h>
#include <fix/c_api/otel.h>
#include <fix/c_api/session.h>
#include <fix/c_api/version.h>

/* Same rationale as the C++ probe: name a declaration so the TU is not purely a
 * preprocessor exercise — a header that resolved but declared nothing usable
 * would still compile if this file were empty of code. External linkage, so no
 * -Wunused-variable and no function-pointer-to-void* cast is needed. */
fixpp_version_t (*fixpp_probe_capi_c_entry)(void) = &fixpp_library_version;
