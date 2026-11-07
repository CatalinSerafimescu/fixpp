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
// Construction: a test-built `table_view` whose group G registers, in the read's context, the
// delimiter, a cluster of more than `kMaxBuildProbe` distinct member tags and a nested group N.
// The cluster is T007's window cluster against the sub-table's mask (the instance slice's own
// entry count), under a seed for which `test100::simulate_build` predicts the root build within
// its budget (the root carries filler fields outside the group, so its mask is wider) and the
// sub-build over it. The cell asserts the root parse succeeds and the sub-build failed before it
// asserts the nested getter's answer. Before D1's budget the sub-build skips the inserts past the
// per-insert cap and reports success, so that precondition fails (tasks Stop rule exemption).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <optional>
#include <string>
#include <vector>

#include "group_getters_100_support.hpp"
#include "support/frame_view_factory.hpp"
#include "support/probe_cluster_100.hpp"
#include "support/wire_test_hooks.hpp"

namespace {

namespace t100 = fixpp::wire::test100;
using fixpp::test100::get_group;
using fixpp::test100::get_nested;
using fixpp::test100::inbound_handle;
using fixpp::test100::rc_name;
using fixpp::wire::access_mode;
using fixpp::wire::Parser;

constexpr std::uint16_t kG = 9500;
constexpr std::uint16_t kDelim = 9501;
constexpr std::uint16_t kN = 9502;
constexpr std::uint16_t kNMember = 9503;
constexpr std::size_t kCluster = 400;
constexpr std::size_t kFiller = 200;
constexpr std::uint16_t kFirstFiller = 30000;
constexpr std::uint32_t kSeedSearch = 4096;

bool reserved(std::uint16_t t) {
    return (t >= kG && t <= kNMember) || (t >= kFirstFiller && t < kFirstFiller + 2U * kFiller) ||
           t == 35;
}

struct plan {
    std::uint32_t seed = 0;
    std::vector<std::uint16_t> cluster;
    std::vector<std::uint16_t> filler;
};

// The first seed whose cluster keeps the root within its budget and takes the sub-build over it.
std::optional<plan> find_plan() {
    std::size_t const sub_entries = kCluster + 3U;  // delimiter, cluster, N, N's member
    auto const sub_mask = static_cast<std::uint32_t>(t100::overlay_cap_ref(sub_entries) - 1U);
    for (std::uint32_t seed = 1; seed <= kSeedSearch; ++seed) {
        plan p;
        p.seed = seed;
        for (auto const t : t100::window_cluster(seed, sub_mask, kCluster + 16U)) {
            if (!reserved(t) && p.cluster.size() < kCluster) {
                p.cluster.push_back(t);
            }
        }
        for (std::uint16_t i = 0; i < kFiller; ++i) {
            p.filler.push_back(static_cast<std::uint16_t>(kFirstFiller + (2U * i)));
        }
        std::vector<std::uint16_t> sub{kDelim};
        sub.insert(sub.end(), p.cluster.begin(), p.cluster.end());
        sub.push_back(kN);
        sub.push_back(kNMember);
        std::vector<std::uint16_t> body{35};
        body.insert(body.end(), p.filler.begin(), p.filler.end());
        body.push_back(kG);
        body.insert(body.end(), sub.begin(), sub.end());
        auto const root_sim = t100::simulate_build(t100::frame_tag_sequence(body), seed);
        auto const sub_sim = t100::simulate_build(sub, seed);
        if (root_sim.total_probes <= root_sim.budget() && sub_sim.total_probes > sub_sim.budget()) {
            return p;
        }
    }
    return std::nullopt;
}

TEST(SeedHookCapi100, D7ii_NestedSubBuildOverProbeBudget_WireLimitExceededTwice) {
    t100::overlay_seed_guard const guard;
    auto const p = find_plan();
    ASSERT_TRUE(p.has_value()) << "setup: no seed up to kSeedSearch gives the root/sub split";
    fixpp::wire::detail::set_overlay_seed_for_testing(p->seed);

    std::uint16_t const root_path[] = {kG};
    fixpp::dict::table_view_builder b;
    b.add_valid("U", 35);
    b.add_valid("U", kG);
    b.set_group_first_ctx("U", {}, kG, kDelim);
    for (auto const t : p->cluster) {
        b.add_group_member_ctx("U", {}, kG, t);
    }
    b.add_group_member_ctx("U", {}, kG, kN);
    b.set_group_first_ctx("U", root_path, kN, kNMember);
    fixpp::dict::table_view const tv = std::move(b).build();

    std::string body = "35=U\x01";
    for (auto const t : p->filler) {
        body += std::to_string(t) + "=f\x01";
    }
    body += std::to_string(kG) + "=1\x01" + std::to_string(kDelim) + "=x\x01";
    for (auto const t : p->cluster) {
        body += std::to_string(t) + "=c\x01";
    }
    body += std::to_string(kN) + "=1\x01" + std::to_string(kNMember) + "=y\x01";
    auto const buf = t100::frame_with_body(body);
    auto const fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{tv};
    auto const mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value()) << "D7 (ii) precondition: the root build is OK; error "
                                << static_cast<int>(mv.error());
    inbound_handle const h{*mv};
    auto const outer = get_group(h.ptr(), kG);
    ASSERT_EQ(outer.rc, FIXPP_ERR_OK) << "setup: the outer read " << rc_name(outer.rc);
    ASSERT_EQ(outer.count, 1U);

    auto const r1 = get_nested(outer.g, 0, kN);
    auto const slices = mv->offsets().group_slices(kG);
    ASSERT_EQ(slices.size(), 1U);
    auto const* sub = fixpp::wire::offset_table_test_access::resolve(mv->offsets(), slices[0].data,
                                                                     mv->hooks(), kN);
    ASSERT_NE(sub, nullptr) << "setup: the nested read cached its sub-table";
    ASSERT_FALSE(sub->build_status().has_value())
        << "D7 (ii) precondition: the sub-build fails on D1's per-build budget (precondition-"
           "blocked until the budget exists)";

    auto const r2 = get_nested(outer.g, 0, kN);
    EXPECT_EQ(r1.rc, FIXPP_ERR_WIRE_LIMIT_EXCEEDED) << "D7 (ii) first read: " << rc_name(r1.rc);
    EXPECT_EQ(r2.rc, FIXPP_ERR_WIRE_LIMIT_EXCEEDED) << "D7 (ii) repeat read: " << rc_name(r2.rc);
}

}  // namespace
