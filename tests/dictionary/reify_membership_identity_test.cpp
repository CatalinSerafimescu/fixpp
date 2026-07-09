// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/reify_membership_identity_test.cpp
//
// 066-dict-backed-inbound-parse T008 — reify-identity witness (FR-007/C4,
// mechanism (b)). Builds a REAL FIX44-dict-backed source MessageView over a
// group-bearing ExecutionReport(35=8) frame (NoLegs(555) x2 + trailing
// TransactTime(60), tests/support/fix44_group_frame_bodies.hpp), runs it
// through the runtime-dispatch entry point `dict::reify()`, DESTROYS the
// source (Dictionary, table_view, frame bytes, parse arena) BEFORE reading
// the resulting owning_message_handle, then reads the group off the
// handle's own re-framed view.
//
// MUST be observed RED before the reify-propagation edit (reify.cpp's
// `owning_message_handle::impl` / `owning_message_handle_from_frame`): the
// pre-066/T008 handle re-frames its view_cache_ via the dict-FREE 2-arg
// MessageView ctor, so its extent runs to end-of-message and ABSORBS tag 60.
// GREEN after T008 propagates the source view's membership into the
// handle's own owned table_view.
//
// Anchors: tasks.md T008; data-model.md "Reify owning handle owned
// table_view"; contracts/inbound-parse.md C4; research.md Decision 6.
#include <gtest/gtest.h>

#include <cstddef>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <fixpp/core/error.hpp>
#include <fixpp/dict/reify.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/wire/message_view_contract.hpp>

#include "support/fix44_dictionary.hpp"
#include "support/fix44_group_frame_bodies.hpp"

namespace {

using fixpp::dict::application_version;
using fixpp::dict::session_version;
using fixpp::dict::version_profile;

constexpr version_profile kProfileV44{session_version::v44, application_version::v44, false, 0};

TEST(ReifyMembershipIdentity, GroupMembershipSurvivesSourceDestruction) {
    // The handle's OWN memory resource — must OUTLIVE the inner scope below
    // (it backs the handle's deep-copied bytes_ AND, post-T008, its owned
    // membership copy).
    std::pmr::monotonic_buffer_resource handle_mr;
    std::optional<fixpp::dict::owning_message_handle> handle;

    {
        // Every SOURCE object (Dictionary, table_view, frame bytes, parse
        // arena, the source MessageView) is scoped to this block and
        // destroyed before `handle` is read below.
        auto dict = fixpp::test_support::make_fix44_dictionary();
        auto tv = dict->as_table_view();

        auto suffix = fixpp_test_support::execution_report_two_legs_trailing_suffix();
        auto frame_bytes =
            fixpp_test_support::make_execution_report_frame(suffix, /*seq=*/1, "SENDER", "TARGET");

        std::pmr::monotonic_buffer_resource parse_arena;
        fixpp::wire::pmr_carry_buffer carry{frame_bytes.size(), &parse_arena};
        fixpp::wire::Framer framer{};
        fixpp::wire::frame_view fvs[1]{};
        auto framed = framer.feed(
            std::span<const std::byte>{frame_bytes.data(), frame_bytes.size()}, carry,
            std::span<fixpp::wire::frame_view>{fvs, 1});
        ASSERT_TRUE(framed.has_value());
        ASSERT_FALSE(framed->empty());

        fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{tv};
        auto parsed = parser.parse(fvs[0], &parse_arena);
        ASSERT_TRUE(parsed.has_value()) << "dict-backed source parse must succeed";

        auto r = fixpp::dict::reify(*parsed, kProfileV44, &handle_mr);
        ASSERT_TRUE(r.has_value()) << "dict::reify() must dispatch v44 ExecutionReport(35=8)";
        handle.emplace(std::move(*r));
        // dict / tv / frame_bytes / parse_arena / parsed / framer are all
        // destroyed HERE, at the end of this block.
    }

    ASSERT_TRUE(handle.has_value());
    auto const& mv = handle->view();
    auto slices = mv.offsets().group_slices(555);
    ASSERT_EQ(slices.size(), 2U)
        << "reified handle must resolve NoLegs(555) as exactly 2 instances";

    // Each leg's own declared member (LegSymbol=600) reads OK on both instances.
    // token: MessageView::token() is protected (test code isn't a friend), and
    // the value only matters for a subsequent .bytes()/check_alive() call this
    // test never makes (only .has_value() is asserted) — a default-constructed
    // token is safe here, mirroring tests/fuzz/fuzz_wire_nested_slice.cpp.
    fixpp::wire::detail::generation_token const gen{};
    auto leg0_symbol =
        fixpp::wire::get(std::span<const std::byte>{slices[0].data, slices[0].len}, 600, gen);
    EXPECT_TRUE(leg0_symbol.has_value()) << "leg #1's own LegSymbol(600) must read OK";
    auto leg1_symbol =
        fixpp::wire::get(std::span<const std::byte>{slices[1].data, slices[1].len}, 600, gen);
    EXPECT_TRUE(leg1_symbol.has_value()) << "leg #2's own LegSymbol(600) must read OK";

    // DISCRIMINATING (RED pre-T008, GREEN post-T008): the trailing outer field
    // TransactTime(60) must be ABSENT from the LAST NoLegs(555) instance --
    // identical to the source (contracts/inbound-parse.md C4). A dict-free
    // re-frame absorbs tag 60 into the last instance's extent (has_value()
    // would be true), which is RED.
    auto trailing =
        fixpp::wire::get(std::span<const std::byte>{slices[1].data, slices[1].len}, 60, gen);
    EXPECT_FALSE(trailing.has_value())
        << "reified handle must read the trailing TransactTime(60) as absent from the "
           "last NoLegs(555) instance, identically to its (now-destroyed) source";
}

}  // namespace
