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
// so on Windows they are inline no-op definitions: the markers compile and link,
// and the portable operator-new counter + sanitizer detection (__SANITIZE_ADDRESS__
// covers MSVC ASan) carry the zero-alloc gate on that platform.
//
// All four symbols are declared here regardless of which a given test uses; an
// unreferenced weak decl (POSIX) or unused inline (Windows) is harmless.
#pragma once

extern "C" {
#ifdef _WIN32
inline void alloc_guard_start() {}
inline void alloc_guard_end() {}
inline long alloc_guard_count() { return 0; }
inline long alloc_guard_global_count() { return 0; }
#else
__attribute__((weak)) void alloc_guard_start();
__attribute__((weak)) void alloc_guard_end();
__attribute__((weak)) long alloc_guard_count();
__attribute__((weak)) long alloc_guard_global_count();
#endif
}
