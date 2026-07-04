#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/wire_test_hooks.hpp — TEST/FUZZ-ONLY wire-layer test hooks.
//
// Gate B PR #166 round-1 Finding 2b: `detail::set_overlay_seed_for_testing`'s
// DECLARATION previously lived in the INSTALLED public header
// `include/fixpp/wire/offset_table.hpp`, exposing a test-only override hook
// on the shipped public surface. The DEFINITION stays in
// `src/wire/offset_table.cpp` (external linkage unchanged; the symbol still
// ships in libfixpp) — only the declaration moves here, into a non-installed
// test-support header, so production consumers never see it.

#include <cstdint>

namespace fixpp::wire::detail {

// TEST/FUZZ-ONLY: override the per-process overlay hash seed that mix() folds
// in (W-P3-2). Lets the collision witness craft a deterministic 128-collision
// set and the wire fuzzer stay reproducible WITHOUT compiling the library
// with a fuzz-only macro (which would fuzz a different binary). MUST be
// called before constructing the OffsetTable(s) under test. Never called on
// the production path; the seed is otherwise randomised once per process.
void set_overlay_seed_for_testing(std::uint32_t seed) noexcept;

}  // namespace fixpp::wire::detail
