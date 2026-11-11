// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/test_100_seed_hook_status.cpp — 100-wire-group-read-integrity, tasks T039, D7 (ii)'s
// wire twin (FR-011; research D7 witness (ii), D1): a nested sub-table whose build exceeds D1's
// per-build probe budget, read through `OffsetTable::nested_group_slices`, reports
// `status == group_read_status::limit_exceeded` and `!is_group`, with no slices, on the first and
// the repeat read.
//
// It sets the process-global overlay seed, so it belongs to the standalone entry
// `wire_seed_hook_tests` (plan Structure Decision), and restores the prior seed through
// `test100::overlay_seed_guard`. The construction is tests/support/nested_probe_budget_100.hpp's.
// The cell asserts the root parse succeeds and the sub-build failed before it asserts the read's
// status. Before D1's budget the sub-build skips the inserts past the per-insert cap and reports
// success, so that precondition fails (tasks Stop rule exemption).

#include <gtest/gtest.h>

#include <cstddef>
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>

#include "support/frame_view_factory.hpp"
#include "support/nested_probe_budget_100.hpp"
#include "support/probe_cluster_100.hpp"
#include "support/wire_test_hooks.hpp"

namespace {

namespace t100 = fixpp::wire::test100;
namespace nb = fixpp::wire::test100::nested_budget;
using fixpp::wire::access_mode;
using fixpp::wire::group_read_status;
using fixpp::wire::Parser;

TEST(SeedHookStatus100, D7ii_NestedSubBuildOverProbeBudget_LimitExceededTwice) {
    t100::overlay_seed_guard const guard;
    auto const p = nb::find_plan();
    ASSERT_TRUE(p.has_value()) << "setup: no seed up to kSeedSearch gives the root/sub split";
    fixpp::wire::detail::set_overlay_seed_for_testing(p->seed);

    fixpp::dict::table_view const tv = nb::make_table(*p);
    auto const buf = nb::make_frame(*p);
    auto const fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{tv};
    auto const mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value()) << "D7 (ii) precondition: the root build is OK; error "
                                << static_cast<int>(mv.error());
    auto const outer = mv->offsets().group_slices_status(nb::kG);
    ASSERT_EQ(outer.status, group_read_status::ok) << "setup: the outer read";
    ASSERT_EQ(outer.slices.size(), 1U) << "setup: one G instance";
    auto const ctx = mv->offsets().group_context_for(nb::kG);
    auto const& inst = outer.slices[0];

    auto const r1 = mv->offsets().nested_group_slices(inst.data, inst.len, nb::kN, ctx);
    auto const* sub = fixpp::wire::offset_table_test_access::resolve(mv->offsets(), inst.data,
                                                                     mv->hooks(), nb::kN);
    ASSERT_NE(sub, nullptr) << "setup: the nested read cached its sub-table";
    ASSERT_FALSE(sub->build_status().has_value())
        << "D7 (ii) precondition: the sub-build fails on D1's per-build budget (precondition-"
           "blocked until the budget exists)";

    auto const r2 = mv->offsets().nested_group_slices(inst.data, inst.len, nb::kN, ctx);
    for (auto const& [r, which] : {std::pair{r1, "first"}, std::pair{r2, "repeat"}}) {
        EXPECT_EQ(r.status, group_read_status::limit_exceeded)
            << "D7 (ii) " << which << " read: status " << static_cast<int>(r.status);
        EXPECT_FALSE(r.is_group) << "D7 (ii) " << which << " read";
        EXPECT_TRUE(r.slices.empty()) << "D7 (ii) " << which << " read";
    }
}

}  // namespace
