// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/consumer/probe_capi_negative.cpp
//
// 086 T013 (US1, FR-006/FR-008) — the ❌ cell: `fixpp::capi` MUST NOT reach a C++
// engine header (contracts/include-interface.md §1 row 4).
//
// ⚠️ THIS FILE IS NOT A BUILD TARGET. It is the SOURCE argument of a
// configure-time try_compile() in tests/consumer/CMakeLists.txt, asserted FALSE.
// It cannot be a target: run_consumer_witness.cmake:96-104 raises FATAL_ERROR on
// ANY non-zero build exit, so a must-fail target would red the whole witness
// (contracts §4a — the reason the earlier "OBJECT library that must fail to
// build" wording was unimplementable).
//
// The probe header is chosen so the assertion cannot pass for the wrong reason
// (FR-008): <fixpp/wire/parser.hpp> is a shipped public engine header whose own
// disappearance from the package would itself be a defect. A probe naming a
// header that does not exist anywhere would return FALSE for a reason that has
// nothing to do with include isolation.
//
// C++ ONLY. A C compiler rejecting a C++ header proves nothing about isolation,
// which is why there is no C counterpart to this file.

#include <fixpp/wire/parser.hpp>

// No code: the #include either resolves or it does not, and that is the whole
// assertion. CMAKE_TRY_COMPILE_TARGET_TYPE is STATIC_LIBRARY for the duration,
// so no main() is required and no link stage runs (research.md R5/R9).
