// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/consumer/probe_recycler_guard.cpp
//
// fixpp#544 (B35; `.specify/544-hot-path-zero-alloc.md` §2.4, finding F-5): the TU of the
// installed recycler-guard arms. It includes an installed fixpp header that reaches asio, so
// the guard header (fixpp/core/detail/asio_recycler_config.hpp) is in its include closure.
//
//   twin      the OBJECT target probe_recycler_guard_twin, through fixpp::fixpp, must
//             compile: the imported target carries ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE;
//   negative  run_consumer_witness.cmake compiles this file against the staged install's
//             include directory and asio's, without the imported target, so without the
//             definition. That compile must fail on the guard's message.
//
// COMPILE-ONLY (OBJECT library, no main).

#include <fixpp/session/engine.hpp>
