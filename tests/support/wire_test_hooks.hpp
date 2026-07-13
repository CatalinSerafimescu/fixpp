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

#include <cstddef>
#include <cstdint>
#include <fixpp/wire/offset_table.hpp>

namespace fixpp::wire::detail {

// TEST/FUZZ-ONLY: override the per-process overlay hash seed that mix() folds
// in (W-P3-2). Lets the collision witness craft a deterministic 128-collision
// set and the wire fuzzer stay reproducible WITHOUT compiling the library
// with a fuzz-only macro (which would fuzz a different binary). MUST be
// called before constructing the OffsetTable(s) under test. Never called on
// the production path; the seed is otherwise randomised once per process.
void set_overlay_seed_for_testing(std::uint32_t seed) noexcept;

}  // namespace fixpp::wire::detail

namespace fixpp::wire {

// 073 T001 / gate-b/r1 FQ-2: TEST-ONLY nested_cache_ introspection (declared
// as a friend of OffsetTable in offset_table.hpp, gated behind
// FIXPP_TEST_HOOKS; see that header for the rationale). Given a ROOT table,
// resolves the sub-table already cached for `(slice_data, nested_no_tag)` in
// `nested_cache_`, or `nullptr` if no matching row exists (never requested,
// OR the row's build itself failed — research.md §D2 mode (a)). Does NOT
// trigger a build: the caller must already have invoked
// `nested_group_slices(slice_data, ..., nested_no_tag, ...)` once so the row
// exists. Never called from production code. The definition itself is gated
// behind FIXPP_TEST_HOOKS so it never attempts to access the (now ungranted)
// private OffsetTable::nested_cache_ member in a non-test-hooks build.
#ifdef FIXPP_TEST_HOOKS
struct nested_cache_access_for_testing {
    [[nodiscard]] static OffsetTable const* resolve(OffsetTable const& root,
                                                    std::byte const* slice_data,
                                                    std::uint16_t nested_no_tag) noexcept {
        for (auto const& row : root.nested_cache_) {
            if (row.slice_data == slice_data && row.nested_no_tag == nested_no_tag) {
                return row.table;
            }
        }
        return nullptr;
    }
};
#endif  // FIXPP_TEST_HOOKS

}  // namespace fixpp::wire
