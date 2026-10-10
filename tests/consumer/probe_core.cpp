// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/consumer/probe_core.cpp
//
// fixpp#544 (B35; `.specify/544-hot-path-zero-alloc.md` §2.4, "The consumer witness"): the
// carrier of the `probe_core` leg, a target that links ONLY fixpp::core. The leg reads the
// target's collected COMPILE_DEFINITIONS (tests/consumer/CMakeLists.txt) and requires
// ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE to arrive through fixpp::core alone.
//
// It includes a core header that reaches no asio header: fixpp::core gives a consumer no
// asio include path, so an asio-reaching header would fail this build for a reason that has
// nothing to do with the leg.
//
// COMPILE-ONLY (OBJECT library, no main).

#include <fixpp/core/decimal.hpp>
