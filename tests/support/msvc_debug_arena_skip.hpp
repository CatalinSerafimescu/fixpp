// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/msvc_debug_arena_skip.hpp
//
// FIXPP_SKIP_ON_MSVC_DEBUG_ARENA(): GTEST_SKIP a byte-exact arena-exhaustion
// OOM-injection test on the MSVC debug/asan lanes.
//
// These tests inject an allocation failure by handing the code under test a
// deliberately tiny std::pmr arena sized to the exact byte at which the failure
// must trip. MSVC's debug STL (_ITERATOR_DEBUG_LEVEL >= 1, i.e. the
// windows-msvc-debug and -asan lanes) heap-allocates a hidden _Container_proxy
// for EVERY std::pmr container at construction, drawn from that same arena. That
// per-container overhead exhausts the injection arena before the intended
// allocation, so bad_alloc is thrown during a container's member-initialisation
// — before any noexcept ctor body can catch and degrade — and escapes into
// std::terminate. There is no clean code fix (a function-try-block in a ctor
// re-throws; it cannot produce a valid degraded object), and adding debug
// headroom would shift WHERE the injected failure trips, defeating the test.
//
// The OOM-degradation BEHAVIOUR these tests verify is fully exercised on the
// windows-msvc-RELEASE lane (no debug iterators, no proxy) and on ALL Linux
// lanes (debug/asan/tsan/ubsan/libc++). Only the MSVC-debug-iterator interaction
// is skipped. User sign-off 2026-06-23.
// See feedback_msvc_debug_container_proxy_null_memory_resource.
#pragma once

#include <gtest/gtest.h>

#if defined(_MSC_VER) && defined(_ITERATOR_DEBUG_LEVEL) && (_ITERATOR_DEBUG_LEVEL >= 1)
#define FIXPP_SKIP_ON_MSVC_DEBUG_ARENA()                                                        \
    GTEST_SKIP() << "byte-exact OOM-injection arena is incompatible with MSVC debug STL's "     \
                    "per-container _Container_proxy allocation (the OOM-degradation behaviour " \
                    "is covered on the windows-msvc-release lane and all Linux lanes)"
#else
#define FIXPP_SKIP_ON_MSVC_DEBUG_ARENA() ((void)0)
#endif

// FIXPP_SKIP_ON_MSVC_DEBUG_GLOBAL_HEAP_GUARD(): GTEST_SKIP a zero-GLOBAL-heap
// assertion (a TU-local `operator new` counter that must read 0 across a
// parse+read window) on the MSVC debug/asan lanes. Same _Container_proxy root
// cause as the arena skip above: MSVC's debug STL (_ITERATOR_DEBUG_LEVEL >= 1)
// heap-allocates a hidden _Container_proxy per std::pmr container at
// construction THROUGH global operator new, so the counter observes those
// proxy allocations even though the code under test draws only from its stack
// arena. ⚠️ 389: this comment used to give the counts ("e.g. 8 for a top-level
// dict-backed read, 2 for a nested descent"). Those are RESULTS, and #389 moved
// them — it deleted `OffsetTable::group_slices_`, one std::pmr container per
// table, so every table now constructs one fewer. The numbers are dropped
// rather than re-derived: the CONDITION (one proxy per std::pmr container
// constructed inside the window) is what a reader needs, and it cannot rot.
// To re-derive a count, run the guarded cell on an MSVC debug lane and read the
// counter.
// The zero-global-heap DISCIPLINE is fully verified on windows-msvc-release
// (no debug iterators, no proxy) and on ALL Linux lanes
// (debug/asan/tsan/ubsan/libc++). Only the MSVC-debug-iterator interaction is
// skipped. See feedback_operator_new_witness_breaks_sanitizers /
// feedback_msvc_debug_container_proxy_null_memory_resource.
#if defined(_MSC_VER) && defined(_ITERATOR_DEBUG_LEVEL) && (_ITERATOR_DEBUG_LEVEL >= 1)
#define FIXPP_SKIP_ON_MSVC_DEBUG_GLOBAL_HEAP_GUARD()                                          \
    GTEST_SKIP() << "MSVC debug STL heap-allocates a hidden _Container_proxy per std::pmr "   \
                    "container via global operator new (_ITERATOR_DEBUG_LEVEL), so a "        \
                    "zero-global-heap read guard cannot hold; the discipline is verified on " \
                    "windows-msvc-release + all Linux lanes (debug/asan/tsan/ubsan/libc++)"
#else
#define FIXPP_SKIP_ON_MSVC_DEBUG_GLOBAL_HEAP_GUARD() ((void)0)
#endif

// FIXPP_SKIP_ON_MSVC_DEBUG_GLOBAL_NEW_SWEEP(): GTEST_SKIP a test that SWEEPS a
// global `operator new` override, failing the Nth allocation for every N in turn,
// on the MSVC debug/asan lanes.
//
// ⚠️ This is a THIRD mechanism, not a synonym for the two above, and it gets its
// own macro because a call site must not carry a sentence that is false of it:
//   - ..._ARENA is for a byte-exact *std::pmr arena* sized to the failing byte;
//     a sweep has no arena.
//   - ..._GLOBAL_HEAP_GUARD is for a counter asserted to read *zero*; a sweep
//     deliberately makes allocations fail and asserts the subject is unchanged.
// The ROOT CAUSE is the same as both: MSVC's debug STL (_ITERATOR_DEBUG_LEVEL >= 1)
// draws a hidden _Container_proxy per std::pmr container through GLOBAL operator
// new. A sweep counts those proxy allocations as if they belonged to the operation
// under test and duly fails one, so bad_alloc is thrown inside a container's
// member-initialisation — before any noexcept ctor body can catch and degrade —
// and escapes into std::terminate. The signature is a process death with NO gtest
// output at all (fixpp#426, Gate B r11: `dictionary_table_view_pair_oom_test`
// died this way on windows-msvc-debug while every Linux lane passed).
//
// ⚠️ Deliberately a runtime skip rather than widening the enclosing `#if` to
// `defined(__GLIBCXX__)` (the reify_membership_copy_oom_test precedent). That
// precedent exists because its ordinal is *calibrated* to libstdc++; a sweep is
// calibration-free and runs correctly on libc++, so compiling it out by STL would
// discard a lane where it genuinely works. The exception-safety behaviour remains
// verified on windows-msvc-RELEASE (no debug iterators, no proxy) and on ALL Linux
// lanes (debug/asan/tsan/ubsan/libc++); only the MSVC-debug-iterator interaction
// is skipped.
#if defined(_MSC_VER) && defined(_ITERATOR_DEBUG_LEVEL) && (_ITERATOR_DEBUG_LEVEL >= 1)
#define FIXPP_SKIP_ON_MSVC_DEBUG_GLOBAL_NEW_SWEEP()                                            \
    GTEST_SKIP() << "MSVC debug STL draws a hidden _Container_proxy per std::pmr container "   \
                    "through global operator new (_ITERATOR_DEBUG_LEVEL), so an allocation "   \
                    "sweep fails one of those instead of the operation's own and terminates "  \
                    "inside member-initialisation; the exception-safety behaviour is verified " \
                    "on windows-msvc-release + all Linux lanes (debug/asan/tsan/ubsan/libc++)"
#else
#define FIXPP_SKIP_ON_MSVC_DEBUG_GLOBAL_NEW_SWEEP() ((void)0)
#endif
