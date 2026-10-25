// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/consumer/probe_typed_inline_large.cpp — #613 C-2b (`.specify/613-builders-export.md` §4.1).
//
// The INSTALLED header-only inline mode, per-message form, for the large versions:
// only FIXPP_BUILDERS_HEADER_ONLY_ListStatus is defined (tests/consumer/CMakeLists.txt),
// so the v50sp2 (and, iff FIXPP_CODEGEN_FIX_LATEST, vlatest) ListStatus.builder.inl is
// reached. A builder .inl includes only its message header and
// <fixpp/wire/body_builder.hpp>, so no large groups.hpp is pulled.
//
// COMPILE-ONLY (OBJECT library, never linked).

#if !defined(FIXPP_BUILDERS_HEADER_ONLY_ListStatus) || defined(FIXPP_BUILDERS_HEADER_ONLY) || \
    defined(FIXPP_VALIDATORS_HEADER_ONLY)
#error "FIXPP_613_C2B_MACROS_WRONG: C-2b must define only FIXPP_BUILDERS_HEADER_ONLY_ListStatus"
#endif
#ifndef FIXPP_613_HAS_VLATEST
#error "FIXPP_613_HAS_VLATEST must be defined (0 or 1) by tests/consumer/CMakeLists.txt"
#endif

#include <fixpp/v50sp2/messages/ListStatus.hpp>
#if FIXPP_613_HAS_VLATEST
#include <fixpp/vlatest/messages/ListStatus.hpp>
#endif

int fixpp_613_typed_inline_large_probe();
int fixpp_613_typed_inline_large_probe() { return 0; }
