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
// Every TU including this header references this symbol; the library's decimal.cpp
// provides the one definition. A mismatched FIXPP_DECIMAL_T → link error (AC-B3).
inline char const* const fixpp_decimal_alias_lock = &decimal_alias_sentinel<FIXPP_DECIMAL_T>::tag;
}  // namespace fixpp::detail
