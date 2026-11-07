// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/test_100_seed_hook_capi.cpp — 100-wire-group-read-integrity, tasks T012, D7 (ii)
// (FR-011; research D7 witness (ii), D1): a nested sub-table whose build exceeds D1's per-build
// probe budget, read through `fixpp_group_get_nested_group`, answers
// FIXPP_ERR_WIRE_LIMIT_EXCEEDED on the first and the repeat read.
//
// It sets the process-global overlay seed, so it belongs to the standalone entry
// `wire_seed_hook_tests` (plan Structure Decision), and restores the prior seed through
// `test100::overlay_seed_guard`.
//
// Construction: tests/support/nested_probe_budget_100.hpp's, shared with the wire-layer and
// typed twins. The cell asserts the root parse succeeds and the sub-build failed before it
// asserts the nested getter's answer. Before D1's budget the sub-build skips the inserts past the
// per-insert cap and reports success, so that precondition fails (tasks Stop rule exemption).

#include <gtest/gtest.h>

#include <cstdint>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>

#include "group_getters_100_support.hpp"
#include "support/frame_view_factory.hpp"
#include "support/nested_probe_budget_100.hpp"
#include "support/probe_cluster_100.hpp"
#include "support/wire_test_hooks.hpp"

namespace {

namespace t100 = fixpp::wire::test100;
namespace nb = fixpp::wire::test100::nested_budget;
using fixpp::test100::get_group;
using fixpp::test100::get_nested;
using fixpp::test100::inbound_handle;
using fixpp::test100::rc_name;
using fixpp::wire::access_mode;
using fixpp::wire::Parser;

TEST(SeedHookCapi100, D7ii_NestedSubBuildOverProbeBudget_WireLimitExceededTwice) {
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
    inbound_handle const h{*mv};
    auto const outer = get_group(h.ptr(), nb::kG);
    ASSERT_EQ(outer.rc, FIXPP_ERR_OK) << "setup: the outer read " << rc_name(outer.rc);
    ASSERT_EQ(outer.count, 1U);

    auto const r1 = get_nested(outer.g, 0, nb::kN);
    auto const slices = mv->offsets().group_slices(nb::kG);
    ASSERT_EQ(slices.size(), 1U);
    auto const* sub = fixpp::wire::offset_table_test_access::resolve(mv->offsets(), slices[0].data,
                                                                     mv->hooks(), nb::kN);
    ASSERT_NE(sub, nullptr) << "setup: the nested read cached its sub-table";
    ASSERT_FALSE(sub->build_status().has_value())
        << "D7 (ii) precondition: the sub-build fails on D1's per-build budget (precondition-"
           "blocked until the budget exists)";

    auto const r2 = get_nested(outer.g, 0, nb::kN);
    EXPECT_EQ(r1.rc, FIXPP_ERR_WIRE_LIMIT_EXCEEDED) << "D7 (ii) first read: " << rc_name(r1.rc);
    EXPECT_EQ(r2.rc, FIXPP_ERR_WIRE_LIMIT_EXCEEDED) << "D7 (ii) repeat read: " << rc_name(r2.rc);
}

}  // namespace
