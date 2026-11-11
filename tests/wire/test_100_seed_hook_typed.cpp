// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/test_100_seed_hook_typed.cpp — 100-wire-group-read-integrity, tasks T040, D7 (ii)'s
// typed twin (FR-011; research D7 witness (ii), D1): a nested sub-table whose build exceeds D1's
// per-build probe budget, read through a typed nested accessor, reports `read_failed()` true and
// `alloc_failed()` false, with size 0, on the first and the repeat read.
//
// The group is a test-built dictionary's, and codegen emits accessors only for shipped
// dictionaries, so `outer_entry::nested()` below is written by hand in the shape
// tools/codegen/fixpp-codegen/emit_messages.cpp emits a nested accessor: the same
// `nested_group_slices` call on `ctx_`'s fields and the same `group_view` status construction.
// It witnesses that shape over this construction, not the generated text; a change to the
// emitter's literal is not reflected here.
//
// It sets the process-global overlay seed, so it belongs to the standalone entry
// `wire_seed_hook_tests` (plan Structure Decision), and restores the prior seed through
// `test100::overlay_seed_guard`. The construction is tests/support/nested_probe_budget_100.hpp's.
// The cell asserts the root parse succeeds and the sub-build failed before it asserts the read.
// Before D1's budget the sub-build skips the inserts past the per-insert cap and reports
// success, so that precondition fails (tasks Stop rule exemption).

#include <gtest/gtest.h>

#include <cstddef>
#include <fixpp/wire/group_view.hpp>
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
using fixpp::wire::Parser;

class nested_entry {
public:
    nested_entry() noexcept = default;
    explicit nested_entry(::fixpp::wire::entry_context ctx) noexcept : ctx_(ctx) {}

private:
    ::fixpp::wire::entry_context ctx_{};
};

class outer_entry {
public:
    outer_entry() noexcept = default;
    explicit outer_entry(::fixpp::wire::entry_context ctx) noexcept : ctx_(ctx) {}

    // The emitter's nested accessor shape, for N.
    [[nodiscard]] ::fixpp::wire::group_view<nested_entry> nested() const noexcept {
        if (ctx_.parent_cache_owner == nullptr) return {};
        auto const r = ctx_.parent_cache_owner->nested_group_slices(
            ctx_.outer_occurrence_id, ctx_.span.size(), nb::kN, ctx_.hooks, ctx_.gen,
            ctx_.group_ctx);
        ::fixpp::wire::entry_context child_ctx = ctx_;
        child_ctx.group_ctx = ctx_.group_ctx.pushed(nb::kN);
        return ::fixpp::wire::group_view<nested_entry>{r.slices, child_ctx, r.status};
    }

private:
    ::fixpp::wire::entry_context ctx_{};
};

TEST(SeedHookTyped100, D7ii_TypedNestedSubBuildOverProbeBudget_ReadFailedTwice) {
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
    auto const outer = mv->group<nb::kG, outer_entry>();
    ASSERT_FALSE(outer.read_failed()) << "setup: the outer read";
    ASSERT_EQ(outer.size(), 1U) << "setup: one G instance";
    outer_entry const e = outer[0];

    auto const first = e.nested();
    auto const slices = mv->offsets().group_slices(nb::kG);
    ASSERT_EQ(slices.size(), 1U);
    auto const* sub = fixpp::wire::offset_table_test_access::resolve(mv->offsets(), slices[0].data,
                                                                     mv->hooks(), nb::kN);
    ASSERT_NE(sub, nullptr) << "setup: the nested read cached its sub-table";
    ASSERT_FALSE(sub->build_status().has_value())
        << "D7 (ii) precondition: the sub-build fails on D1's per-build budget (precondition-"
           "blocked until the budget exists)";

    auto const repeat = e.nested();
    EXPECT_EQ(first.size(), 0U) << "D7 (ii) typed, first read";
    EXPECT_TRUE(first.read_failed()) << "D7 (ii) typed, first read / FR-011";
    EXPECT_FALSE(first.alloc_failed()) << "D7 (ii) typed, first read: a wire limit, not an "
                                          "allocation";
    EXPECT_EQ(repeat.size(), 0U) << "D7 (ii) typed, repeat read";
    EXPECT_TRUE(repeat.read_failed()) << "D7 (ii) typed, repeat read / FR-011";
    EXPECT_FALSE(repeat.alloc_failed()) << "D7 (ii) typed, repeat read";
}

}  // namespace
