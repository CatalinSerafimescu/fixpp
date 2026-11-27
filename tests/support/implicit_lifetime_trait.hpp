#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/implicit_lifetime_trait.hpp
//
// W1 (`.specify/688-707-559-wire-headroom-token.md` §2.6): the implicit-lifetime query the
// passkey pins use, where the compiler offers one. A passkey is forgeable when it is an
// implicit-lifetime type and a non-friend can copy it, so the pins assert that the conjunction
// is false. FIXPP_TEST_IMPLICIT_LIFETIME(T) is defined only where clang's
// __builtin_is_implicit_lifetime or the library's std::is_implicit_lifetime_v exists, and
// implicit_lifetime::kHasTrait says which. A pin written inside
// `#if defined(FIXPP_TEST_IMPLICIT_LIFETIME)` is left out elsewhere; each TU that includes this
// header has a runtime cell that skips, naming the compiler, where kHasTrait is false, so which
// lanes evaluated the pins is read from the test log, not from the build.
//
// The `defined(__has_builtin)` test is nested on purpose: where __has_builtin is not a macro the
// inner `#if` sits in a skipped group and is never evaluated, while a one-level
// `#if __has_builtin(x)` is ill-formed there.

#include <type_traits>
#include <version>

#if defined(__has_builtin)
#if __has_builtin(__builtin_is_implicit_lifetime)
#define FIXPP_TEST_IMPLICIT_LIFETIME(T) __builtin_is_implicit_lifetime(T)
#endif
#endif
#if !defined(FIXPP_TEST_IMPLICIT_LIFETIME) && defined(__cpp_lib_is_implicit_lifetime)
#define FIXPP_TEST_IMPLICIT_LIFETIME(T) std::is_implicit_lifetime_v<T>
#endif

#define FIXPP_TEST_IL_STR2(x) #x
#define FIXPP_TEST_IL_STR(x) FIXPP_TEST_IL_STR2(x)

namespace fixpp::test_support::implicit_lifetime {

#if defined(FIXPP_TEST_IMPLICIT_LIFETIME)
inline constexpr bool kHasTrait = true;
#else
inline constexpr bool kHasTrait = false;
#endif

// The compiler and its version, for the skip message of a cell that needs the trait.
inline constexpr char const* kCompiler =
#if defined(__clang__)
    "clang " __clang_version__;
#elif defined(_MSC_FULL_VER)
    "MSVC " FIXPP_TEST_IL_STR(_MSC_FULL_VER);
#elif defined(__GNUC__)
    "gcc " __VERSION__;
#else
    "an unidentified compiler";
#endif

}  // namespace fixpp::test_support::implicit_lifetime
