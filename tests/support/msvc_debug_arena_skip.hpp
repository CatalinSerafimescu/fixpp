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
#define FIXPP_SKIP_ON_MSVC_DEBUG_ARENA()                                                       \
    GTEST_SKIP() << "byte-exact OOM-injection arena is incompatible with MSVC debug STL's "    \
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
// proxy allocations (e.g. 8 for a top-level dict-backed read, 2 for a nested
// descent) even though the code under test draws only from its stack arena.
// The zero-global-heap DISCIPLINE is fully verified on windows-msvc-release
// (no debug iterators, no proxy) and on ALL Linux lanes
// (debug/asan/tsan/ubsan/libc++). Only the MSVC-debug-iterator interaction is
// skipped. See feedback_operator_new_witness_breaks_sanitizers /
// feedback_msvc_debug_container_proxy_null_memory_resource.
#if defined(_MSC_VER) && defined(_ITERATOR_DEBUG_LEVEL) && (_ITERATOR_DEBUG_LEVEL >= 1)
#define FIXPP_SKIP_ON_MSVC_DEBUG_GLOBAL_HEAP_GUARD()                                            \
    GTEST_SKIP() << "MSVC debug STL heap-allocates a hidden _Container_proxy per std::pmr "     \
                    "container via global operator new (_ITERATOR_DEBUG_LEVEL), so a "          \
                    "zero-global-heap read guard cannot hold; the discipline is verified on "   \
                    "windows-msvc-release + all Linux lanes (debug/asan/tsan/ubsan/libc++)"
#else
#define FIXPP_SKIP_ON_MSVC_DEBUG_GLOBAL_HEAP_GUARD() ((void)0)
#endif
