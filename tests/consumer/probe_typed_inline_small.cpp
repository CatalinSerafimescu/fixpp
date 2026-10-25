// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/consumer/probe_typed_inline_small.cpp — #613 C-2a (`.specify/613-builders-export.md` §4.1).
//
// The INSTALLED header-only inline mode, whole-TU form, for the two small versions:
// FIXPP_BUILDERS_HEADER_ONLY and FIXPP_VALIDATORS_HEADER_ONLY are defined by
// tests/consumer/CMakeLists.txt, and <fixpp/v42/all.hpp> / <fixpp/v44/all.hpp> reach
// each version's all.hpp, groups.hpp (through validators/traits.hpp),
// validators/traits.hpp and every .builder.inl / .validator.inl. Compiling is the
// assertion: a staged layout missing any of them fails here.
//
// ⚠️ A SEPARATE TARGET from C-2b, never one TU with it: the whole-TU macros carry the
// same name in every version, so in a shared TU they would also pull the large
// versions' validator closure (L-078-1's cost).
//
// COMPILE-ONLY (OBJECT library, never linked, so 078's same-message ODR rule cannot be
// violated by mixing this with the linked C-1).

#if !defined(FIXPP_BUILDERS_HEADER_ONLY) || !defined(FIXPP_VALIDATORS_HEADER_ONLY)
#error "FIXPP_613_C2A_MACROS_MISSING: C-2a must compile in whole-TU header-only mode"
#endif

#include <fixpp/v42/all.hpp>
#include <fixpp/v44/all.hpp>

int fixpp_613_typed_inline_small_probe();
int fixpp_613_typed_inline_small_probe() { return 0; }
