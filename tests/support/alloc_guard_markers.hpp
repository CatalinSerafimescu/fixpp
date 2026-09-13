// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/alloc_guard_markers.hpp
//
// Mallocnesia alloc-guard marker declarations, centralised so the POSIX/Windows
// split lives in one place instead of being copied into every zero-alloc test.
//
// On POSIX these are WEAK UNDEFINED symbols: when a test binary runs under
// `LD_PRELOAD=libmallocnesia.so` the preload defines them and the markers gate a
// global new/delete/malloc between alloc_guard_start() and alloc_guard_end()
// (alloc_guard_end exits(1) if any fired); without the preload they resolve to
// null and the `if (alloc_guard_start) alloc_guard_start();` call sites no-op so
// the test still runs (the portable TU-local operator-new counter carries the
// in-process witness).
//
// MSVC has no weak-symbol mechanism and there is no mallocnesia port for Windows,
// so on Windows they are null function pointers: every call site is null-checked,
// so the markers no-op, and the portable operator-new counter + sanitizer detection
// (__SANITIZE_ADDRESS__ covers MSVC ASan) carry the zero-alloc gate on that platform.
// They used to be inline no-op functions, which made the null check test a function
// name that is never null -- MSVC warning C4551, an error under FIXPP_WERROR (#417).
//
// All four symbols are declared here regardless of which a given test uses; an
// unreferenced weak decl (POSIX) or unused pointer (Windows) is harmless.
#pragma once

#ifdef _WIN32
inline constexpr void (*alloc_guard_start)() = nullptr;
inline constexpr void (*alloc_guard_end)() = nullptr;
inline constexpr long (*alloc_guard_count)() = nullptr;
inline constexpr long (*alloc_guard_global_count)() = nullptr;
#else
extern "C" {
__attribute__((weak)) void alloc_guard_start();
__attribute__((weak)) void alloc_guard_end();
__attribute__((weak)) long alloc_guard_count();
__attribute__((weak)) long alloc_guard_global_count();
}
#endif
