#pragma once
// include/fixpp/core/decimal_alias.hpp
// Build-time engine-wide decimal alias + link-time mismatch guard.
// extract from 2a-decimal.md v0.3 §4.4

#include "fixpp/core/decimal.hpp"

#ifdef FIXPP_DECIMAL_USER_HEADER
#include FIXPP_DECIMAL_USER_HEADER
#endif

#ifndef FIXPP_DECIMAL_T
#define FIXPP_DECIMAL_T ::fixpp::core::pod_decimal
#endif

namespace fixpp {
using decimal_t = core::decimal<FIXPP_DECIMAL_T>;
}

// Link-time guard: every TU including this header references a template
// specialization whose mangled name encodes T. The library defines the
// specialization for its chosen FIXPP_DECIMAL_T in src/core/decimal.cpp.
// A consumer built with a different FIXPP_DECIMAL_T → unresolved symbol at link.
namespace fixpp::detail {
template <class T>
struct decimal_alias_sentinel {
    static char const tag;  // defined in src/core/decimal.cpp (one specialization only)
};
// Explicit specialization declaration — suppresses implicit instantiation so the
// definition in src/core/decimal.cpp can be the first (and only) definition.
// [C++17 [temp.expl.spec]p6]
template <>
char const decimal_alias_sentinel<FIXPP_DECIMAL_T>::tag;

// Production link-time guard (Gate B round 2 P1 fix). Two reinforcing
// mechanisms, because either alone is fragile:
//
//   1. `fixpp_decimal_alias_lock_value` is initialized from the *value* of
//      `decimal_alias_sentinel<T>::tag`. Across TUs the compiler cannot
//      constant-fold this read (the definition lives in src/core/decimal.cpp
//      only), so it emits a relocation against `tag` that the linker must
//      resolve. A mismatched FIXPP_DECIMAL_T leaves the relocation
//      unsatisfied → link error (AC-B3).
//
//   2. `[[gnu::used]]` (Clang/GCC, Tier 1) forces the compiler and linker to
//      retain the weak inline variable even when the consumer TU never
//      reads it. Without this, the previous `&tag`-into-pointer form was
//      elidable: the address-of-static folds to a known non-null value,
//      reads of the pointer fold to `true`, and the weak inline ends up
//      with no references and gets GC'd by the linker before the
//      relocation is checked. Tier 2 compilers (MSVC) ignore the unknown
//      attribute per [C++17 [dcl.attr.grammar]/6]; on those targets the
//      odr-use of `tag`'s value above is the load-bearing mechanism.
//
// `fixpp_decimal_alias_lock` preserves the original pointer type for any
// existing diagnostic readers; both variables share the same guarantee.
[[gnu::used]] inline char const fixpp_decimal_alias_lock_value =
    decimal_alias_sentinel<FIXPP_DECIMAL_T>::tag;
[[gnu::used]] inline char const* const fixpp_decimal_alias_lock =
    &fixpp_decimal_alias_lock_value;
}  // namespace fixpp::detail
