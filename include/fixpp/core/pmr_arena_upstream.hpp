#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/core/pmr_arena_upstream.hpp
//
// arena_upstream(): the upstream memory_resource for the fixed stack-arena
// monotonic_buffer_resources in the hot parse/build path, and for the Writer
// group-scratch and similar "bounded, no-heap-spill" sentinels.
//
// Release builds AND every non-MSVC toolchain (libstdc++ / libc++) get
// std::pmr::null_memory_resource() — a DELIBERATE fail-closed bound: these
// arenas must satisfy every allocation from their fixed stack buffer and must
// NOT spill to the heap; a spill is a sizing bug and null throws to surface it.
//
// MSVC's debug STL (_ITERATOR_DEBUG_LEVEL >= 1, i.e. the windows-msvc-debug and
// -asan lanes) heap-allocates a _Container_proxy for EVERY std::pmr container at
// construction, drawn from the container's allocator (= these arenas). That
// per-container overhead overflows the release-sized stack buffers and would
// reach the null upstream -> std::pmr::null_memory_resource throws bad_alloc ->
// the noexcept parse/build boundary calls std::terminate (a silent abort).
// Under MSVC debug ONLY we therefore permit a heap upstream to absorb the proxy
// overhead. Release and Linux behaviour is byte-identical (still null).
//
// See feedback_msvc_debug_container_proxy_null_memory_resource.

#include <memory_resource>

namespace fixpp::detail {

[[nodiscard]] inline std::pmr::memory_resource* arena_upstream() noexcept {
#if defined(_MSC_VER) && defined(_ITERATOR_DEBUG_LEVEL) && (_ITERATOR_DEBUG_LEVEL >= 1)
    return std::pmr::new_delete_resource();
#else
    return std::pmr::null_memory_resource();
#endif
}

}  // namespace fixpp::detail
