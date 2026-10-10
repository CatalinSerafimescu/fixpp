// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/sync/fixtures/asio_recycler_guard/negative_compile.cpp
//
// fixpp#544 (B35; `.specify/544-hot-path-zero-alloc.md` §2.4, "The mechanical guards"): the
// TU of the ctest asio_recycler_guard_negative_compile
// (tests/sync/run_asio_recycler_negative_compile.cmake). It includes one installed fixpp
// header that includes asio. The driver syntax-checks it with
// ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE omitted, with a wrong value and with the exported one.
#include <fixpp/core/clock.hpp>
