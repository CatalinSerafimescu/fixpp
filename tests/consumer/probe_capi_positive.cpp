// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/consumer/probe_capi_positive.cpp
//
// 086 T011 (US1, FR-002/FR-003) — the ✅ half of contracts/include-interface.md
// §1 row 1-2 for `fixpp::capi`, from C++.
//
// Every one of the TWELVE C-ABI headers is named explicitly. The umbrella
// <fix/c_api.h> pulls in only nine of the eleven sub-headers (log.h and otel.h
// are not among them, c_api.h:40-48), so including the umbrella alone would
// leave two of the twelve unwitnessed.
//
// COMPILE-ONLY, by construction: this TU has no main(), and its target is an
// OBJECT library. contracts §4 requires it — research.md R5 measured a false
// negative on the umbrella row caused by a LINK-stage failure while every
// #include resolved correctly.
//
// ⚠️ This probe on its own establishes NOTHING about isolation. Under the
// additive layout <fix/c_api.h> resolves from either root, so a green here is
// exactly as consistent with the defect being fully present as with it being
// fixed. Only the PAIR — this TU compiling AND the probe_capi_negative*
// try_compile calls returning FALSE, from the same configured consumer —
// discriminates (FR-008a).

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

// Use one declaration from the umbrella so the TU is not merely a preprocessor
// exercise: a header that resolved but declared nothing usable would still
// compile if this file were empty of code.
namespace {
[[maybe_unused]] fixpp_version_t probe_use() { return fixpp_library_version(); }
}  // namespace
