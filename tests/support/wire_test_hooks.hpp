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
// 389: `reserve_bound_access_for_testing` was DELETED here along with
// `OffsetTable::group_slices_reserve_bound()` itself. There is no reservation
// estimate left to read — each group allocates exactly its own slice count.
//
// Deleting this hook is PART of the instrument for the change, the same trick
// #384 used with the constructor defaults: every test that read the estimator
// becomes a COMPILE ERROR rather than something a grep has to find. It named
// three — W-10 probe 3, the hostile-input clamp cell, and an anti-vacuity guard
// on the bad_alloc degrade.
//
// ⚠️ AND IT WAS NOT THE WHOLE POPULATION, which is the more useful half.
// `WireOffsetTable.TwoTopLevelGroupsSpanStableAcrossReads` also depended on the
// estimator — its entire rationale and its named mutation were the reserve —
// but it never READ the hook, only the shared-vector SHAPE. A compile error
// cannot find a dependency on a shape. It was found by grep, and an earlier
// version of this comment claimed the compiler "named exactly three" while a
// fourth stood a directory away.
//
// The rule that generalises: removing a symbol enumerates the sites that NAME
// it, never the sites that depend on the STRUCTURE it was part of. Use both,
// and do not let the compiler's precision imply completeness. See B&L B-389-1.
#endif  // FIXPP_TEST_HOOKS

}  // namespace fixpp::wire
