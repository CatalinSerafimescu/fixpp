// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/reify_shared_table_concurrency_test.cpp
//
// fixpp#495 (`.specify/495-493-486-dict-reify-copy.md` §3.3, §10 T-17): two reify
// handles minted over ONE owned-route source share its membership table, and two
// threads read through them concurrently. The claim is that every accessor the
// hooks and the view call on a `table_view` is a read with no hidden write; this
// cell is its executed witness under TSan (a grep for `mutable` cannot stop a
// future lazy cache). Pass: correct reads on both threads and a clean TSan run.
// Mutation: a `mutable` lazily-filled cache written inside a `const` table_view
// accessor on this path — TSan reports a race.
//
// Standalone (`[const §VII.8]`): genuine concurrency, a TSan target.

#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <fixpp/dict/reify.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <memory>
#include <memory_resource>
#include <span>
#include <thread>
#include <vector>

#include "support/app_message_read_scaffold.hpp"
#include "support/fix44_dictionary.hpp"
#include "support/fix44_group_frame_bodies.hpp"

namespace {

using fixpp::dict::application_version;
using fixpp::dict::session_version;
using fixpp::dict::version_profile;
using fixpp::wire::access_mode;

constexpr version_profile kProfileV44{.session = session_version::v44,
                                      .default_appl = application_version::v44,
                                      .has_per_message_override = false,
                                      ._reserved = 0};
constexpr std::uint16_t kNoLegs = 555;
constexpr std::uint16_t kLegSymbol = 600;  // NoLegs' first field in FIX44
constexpr int kIterations = 2000;

TEST(ReifySharedTableConcurrency, TwoThreadsReadOneSharedTable) {
    auto const dict = fixpp::test_support::make_fix44_dictionary();
    std::shared_ptr<const fixpp::dict::table_view> const sp =
        std::make_shared<const fixpp::dict::table_view>(dict->as_table_view());

    auto const frame = fixpp_test_support::make_execution_report_frame(
        fixpp_test_support::execution_report_two_legs_trailing_suffix(), /*seq=*/3, "S", "T");
    std::pmr::monotonic_buffer_resource parse_arena;
    fixpp::wire::pmr_carry_buffer carry{frame.size(), &parse_arena};
    fixpp::wire::Framer framer{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = framer.feed(std::span<const std::byte>{frame.data(), frame.size()}, carry,
                              std::span<fixpp::wire::frame_view>{fvs, 1});
    ASSERT_TRUE(framed.has_value() && !framed->empty());
    fixpp::wire::Parser<access_mode::Index> parser{fixpp::wire::detail::owned_route_key{}, sp};
    auto src = parser.parse(fvs[0], &parse_arena);
    ASSERT_TRUE(src.has_value());

    std::pmr::monotonic_buffer_resource mr_a;
    std::pmr::monotonic_buffer_resource mr_b;
    auto ha = fixpp::dict::reify(*src, kProfileV44, &mr_a);
    auto hb = fixpp::dict::reify(*src, kProfileV44, &mr_b);
    ASSERT_TRUE(ha.has_value() && hb.has_value());
    ASSERT_EQ(ha->view().hooks().opaque_dict(), sp.get());
    ASSERT_EQ(hb->view().hooks().opaque_dict(), sp.get())
        << "precondition: both handles read the SAME shared table";

    std::barrier start{2};
    std::atomic<int> failures{0};
    auto reader = [&](fixpp::dict::owning_message_handle const& h) {
        start.arrive_and_wait();
        for (int i = 0; i < kIterations; ++i) {
            // Membership-bounded group read: the hooks' group_member / delimiter
            // callbacks on the shared table.
            if (h.view().offsets().group_slices(kNoLegs).size() != 2U) {
                failures.fetch_add(1, std::memory_order_relaxed);
            }
            // Field classification over every entry: the classify callback.
            std::size_t unknown = 0;
            for ([[maybe_unused]] auto const& kv : h.view().unknown_fields()) {
                ++unknown;
            }
            if (unknown > h.view().offsets().entries().size()) {
                failures.fetch_add(1, std::memory_order_relaxed);
            }
            // The view caches its group slices and unknown fields after the first
            // call, so every iteration also calls the shared table's callbacks
            // directly: classification, group membership and the delimiter.
            auto const& hooks = h.view().hooks();
            if (!hooks.classify_fn()(hooks.opaque_dict(), "8", kNoLegs)) {
                failures.fetch_add(1, std::memory_order_relaxed);
            }
            fixpp::wire::group_context const root{.msg_type = "8"};
            if (!hooks.group_member_fn()(hooks.opaque_dict(), root, kNoLegs, kLegSymbol)) {
                failures.fetch_add(1, std::memory_order_relaxed);
            }
            if (hooks.group_delim_fn()(hooks.opaque_dict(), root, kNoLegs) != kLegSymbol) {
                failures.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };
    std::thread ta{reader, std::cref(*ha)};
    std::thread tb{reader, std::cref(*hb)};
    ta.join();
    tb.join();
    EXPECT_EQ(failures.load(), 0);
}

}  // namespace
