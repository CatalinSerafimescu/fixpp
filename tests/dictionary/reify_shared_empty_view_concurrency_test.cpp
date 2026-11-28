// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/reify_shared_empty_view_concurrency_test.cpp
//
// W1 D-688-2 (`.specify/688-707-559-wire-headroom-token.md` §1.4a, cell (e)): every null reify
// handle returns one shared, function-local empty MessageView from owning_message_handle::view(),
// on every thread. unknown_fields() writes nothing over an empty table, so two threads, each with
// its own null handle, may call it on that one view. Pass: both threads read an empty view and TSan
// reports nothing. Mutation: the first-unknown index written over an empty table, which TSan
// reports as a race on the index; on a lane without TSan the cell passes either way.
//
// Keep this binary to its one cell, in its own process: the index is written at most once, so the
// process's first unknown_fields() call on the shared view must be made inside the two threads. A
// call made first on the main thread would happen-before both threads and hide the race.
//
// Standalone (`[const §VII.8]`): genuine concurrency, a TSan target.

#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <fixpp/dict/reify.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <fixpp/wire/parser.hpp>
#include <functional>
#include <memory_resource>
#include <thread>
#include <utility>

#include "support/frame_view_factory.hpp"
#include "support/reify_test_frame.hpp"

namespace {

using fixpp::dict::application_version;
using fixpp::dict::session_version;
using fixpp::dict::version_profile;
using fixpp::wire::access_mode;

constexpr version_profile kProfileV44{.session = session_version::v44,
                                      .default_appl = application_version::v44,
                                      .has_per_message_override = false,
                                      ._reserved = 0};
constexpr int kIterations = 2000;

TEST(ReifySharedEmptyViewConcurrency, TwoThreadsCallUnknownFieldsOnTheSharedEmptyView) {
    auto const frame = fixpp::test_support::make_nos_frame();
    std::pmr::monotonic_buffer_resource parse_arena;
    auto const fv = fixpp::wire::test::make_frame_view(frame);
    ASSERT_TRUE(fv.has_value());
    fixpp::wire::Parser<access_mode::Index> parser{};
    auto src = parser.parse(*fv, &parse_arena);
    ASSERT_TRUE(src.has_value());

    std::pmr::monotonic_buffer_resource mr;
    auto ra = fixpp::dict::reify(*src, kProfileV44, &mr);
    auto rb = fixpp::dict::reify(*src, kProfileV44, &mr);
    ASSERT_TRUE(ra.has_value() && rb.has_value());
    // Moving each handle out leaves two null handles behind.
    fixpp::dict::owning_message_handle const keep_a{std::move(*ra)};
    fixpp::dict::owning_message_handle const keep_b{std::move(*rb)};
    fixpp::dict::owning_message_handle const& ha = *ra;
    fixpp::dict::owning_message_handle const& hb = *rb;
    // Only the addresses are read here, never unknown_fields().
    ASSERT_EQ(&ha.view(), &hb.view()) << "precondition: both null handles return one shared view";
    ASSERT_NE(&ha.view(), &keep_a.view());

    std::barrier start{2};
    std::atomic<int> failures{0};
    auto reader = [&](fixpp::dict::owning_message_handle const& h) {
        start.arrive_and_wait();
        for (int i = 0; i < kIterations; ++i) {
            if (!h.view().unknown_fields().empty()) {
                failures.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };
    std::thread ta{reader, std::cref(ha)};
    std::thread tb{reader, std::cref(hb)};
    ta.join();
    tb.join();
    EXPECT_EQ(failures.load(), 0);
}

}  // namespace
