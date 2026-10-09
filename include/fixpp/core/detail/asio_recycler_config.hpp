// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/core/detail/asio_recycler_config.hpp
//
// fixpp#544 (B35; `.specify/544-hot-path-zero-alloc.md` §2.4, owner ruling R-1′): the slot
// count fixpp builds asio's recycling allocator with, and the check that the including
// translation unit was compiled with the same count.
//
// ONE SOURCE. FIXPP_ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE below is where the value is written;
// build files and scripts read it from here rather than spelling the number (re-check with
// `git grep -n "ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE=[0-9]" -- . ':!*.md'`, which must print
// nothing). cmake/FixppAsioRecycler.cmake reads it from this file and attaches
// `ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE=<value>` as a PUBLIC compile definition of
// fixpp::core, so a CMake consumer of any fixpp:: target that publishes the `include/` root
// inherits it. A build that uses the installed headers without fixpp's CMake package must
// define it itself.
//
// THE OBLIGATION (ODR). The macro sizes `asio::detail::thread_info_base`, whose inline
// members every TU that includes asio compiles. So every TU in one process that includes
// asio must see the same value. A mismatch is undefined behaviour, and no linker reports it.
//
// WHY A static_assert. `<asio/detail/thread_info_base.hpp>` defines asio's default when no
// one else has, so after it a missing definition and a wrong one both fail the assertion,
// whichever of asio and fixpp the TU includes first. This header never defines the asio
// macro, which would hide a mismatch instead of reporting it. It is not `#error`, because
// preprocess-only gates (`-E`) include it and must not fail.
//
// The condition "every installed header that includes asio includes this one" is checked
// by the ctest `asio_recycler_guard_inclusion` (tools/check_asio_recycler_guard.py).
#pragma once

#include <asio/detail/thread_info_base.hpp>

#define FIXPP_ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE 16

static_assert(ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE == FIXPP_ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE,
              "FIXPP_ASIO_RECYCLER_ODR: every translation unit that includes asio must be "
              "compiled with ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE equal to "
              "FIXPP_ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE (link a fixpp:: CMake target, or "
              "define it; see include/fixpp/core/detail/asio_recycler_config.hpp)");
