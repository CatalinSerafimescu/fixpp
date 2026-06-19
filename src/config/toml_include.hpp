// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/config/toml_include.hpp
// 044-toml-session-config — T039: hardened toml++ include shim.
//
// PROBLEM (FR-012 / validation rule 9, fuzzer-found):
// tomlplusplus 3.4.0 routes internal invariant checks through
// TOML_ASSERT_ASSUME. On certain malformed input — e.g. the table header
// `[ [key]]` — parse_key() (parser.inl:3037) fires it on attacker-controlled
// data:
//   - non-NDEBUG: assert() -> abort(). A signal, NOT catchable by the
//     try/catch around toml::parse_file -> ESCAPES the noexcept boundary.
//   - NDEBUG/release: __builtin_assume(false) = UNDEFINED BEHAVIOUR.
// Either mode breaks the central guarantee that every byte sequence yields a
// ConfigBundle or a vector<LoadDiagnostic> and never terminates / triggers UB.
//
// Why a plain pre-define of TOML_ASSERT does NOT fix it: under NDEBUG toml++
// #undef's any pre-defined TOML_ASSERT (preprocessor.hpp:1175-1178) and routes
// TOML_ASSERT_ASSUME -> TOML_ASSUME -> __builtin_assume (TOML_ASSUME has no
// #ifndef guard, so it cannot be overridden without corrupting legitimate
// optimisation hints elsewhere).
//
// FIX: locally #undef NDEBUG across the toml++ include (save & restore) so the
// overridable TOML_ASSERT path stays active in ALL build modes, and define
// TOML_ASSERT to THROW a catchable exception. Throwing — not no-op — unwinds
// cleanly into load_toml_config's existing catch as a parse_error diagnostic
// and never continues past the violated invariant (a no-op would continue past
// it = a memory-safety question). The thrown std::logic_error is a
// std::exception, caught by the loader's `catch (const std::exception&)` arm.
//
// SAFE because only the (non-noexcept) parse path asserts are reachable on
// hostile input: post-parse code uses only safe accessors (value<>/get/
// as_table + nullcheck), which do not assert. Validated by a >=10-min ASan
// fuzz from the seed corpus, in both a non-NDEBUG and an NDEBUG/release build.
//
// ODR: #undef NDEBUG changes toml++ inline/template bodies (TOML_PURE /
// TOML_CONST attributes + TOML_ASSERT_ASSUME). EVERY config TU that
// instantiates toml++ bodies MUST include toml++ through THIS shim and never
// <toml++/toml.hpp> directly. Mixing shimmed and un-shimmed TUs is UB. Current
// include sites routed here: src/config/mappers.hpp,
// src/config/toml_config_loader.cpp.
#pragma once

// --- save & disable NDEBUG so toml++ keeps the overridable TOML_ASSERT path --
#ifdef NDEBUG
#define FIXPP_TOML_NDEBUG_WAS_DEFINED 1
#undef NDEBUG
#endif

// --- throwing assert: unwinds into the loader's parse_error catch ------------
#include <stdexcept>
#define TOML_ASSERT(expr)    \
    (static_cast<bool>(expr) \
         ? void(0)           \
         : throw ::std::logic_error("tomlplusplus internal invariant violated"))

// GCC -Wterminate: toml++ also uses TOML_ASSERT inside a handful of `noexcept`
// members, where a throw would call std::terminate. GCC flags every such throw
// statically (the warning is unconditional — it carries no reachability signal).
// This is SAFE here, and a strict improvement over toml++'s native behaviour:
//   * The noexcept asserts live in (a) the toml::path / path_component
//     navigation API — which this loader NEVER uses (verified: no `toml::path`
//     or `at_path` anywhere in src/config/), so those throws are dead code; and
//     (b) utf8_decoder::operator() on the parse path — whose internal-invariant
//     assert is not tripped by malformed bytes (3.1M arbitrary-byte fuzz
//     iterations across asan/Debug + release/NDEBUG, zero terminates).
//   * Where such a noexcept assert WOULD fire, the shim yields a clean
//     std::terminate instead of toml++'s native __builtin_assume (UB under
//     NDEBUG) / assert->abort (debug). terminate >= abort > UB.
//   * The actual defect this shim fixes is on the NON-noexcept parse path
//     (parse_key), where the throw is caught and converted to a diagnostic.
// Suppress GCC-only (clang has no -Wterminate and would emit
// -Wunknown-warning-option). See specs/044-.../tasks.md T039 + B&L L-044-*.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wterminate"
#endif

#include <toml++/toml.hpp>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// --- restore NDEBUG for the remainder of the translation unit -----------------
#ifdef FIXPP_TOML_NDEBUG_WAS_DEFINED
#define NDEBUG 1
#undef FIXPP_TOML_NDEBUG_WAS_DEFINED
#endif
