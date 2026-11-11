// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/test_100_probe_budget.cpp — 100-wire-group-read-integrity, tasks T008 (US3, FR-008;
// research D1, D9 "#645 (D1)").
//
// The index build's probe bound is a per-build budget of `kMaxBuildProbe` x occurrences; a frame
// whose total probe work exceeds it fails `Parser::parse` with `wire_offset_table_full`, and a
// frame within it indexes every distinct tag.
//
// Every cell sets the process-global overlay seed, so this file belongs to the standalone entry
// `wire_seed_hook_tests` (plan Structure Decision), and each cell restores the prior seed through
// `test100::overlay_seed_guard`, a local of its body.
//
// Each cell first checks, with `test100::simulate_build` over the exact tag sequence of the frame
// it parses, the probe property the cell rests on (over the budget; within the budget with one
// insert at the per-insert cap; within both), so a cell cannot pass or fail on a frame that does
// not have the shape its name states.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <string>
#include <vector>

#include "support/frame_view_factory.hpp"
#include "support/probe_cluster_100.hpp"
#include "support/wire_test_hooks.hpp"

namespace {

using fixpp::core::error;
using fixpp::wire::access_mode;
using fixpp::wire::Parser;
namespace t100 = fixpp::wire::test100;

constexpr std::uint32_t kClusterSeed = 0x9E3779B9U;

// The body of a cluster frame: `35=D`, then each tag of `tags` as `<tag>=x`.
std::string cluster_body(std::vector<std::uint16_t> const& tags) {
    std::string body = "35=D\x01";
    for (auto const t : tags) {
        body += std::to_string(t);
        body += "=x\x01";
    }
    return body;
}

// US3.1: the crafted window cluster for a frame of `entries` occurrences (8, 9, 35, the
// cluster, 10), under `kClusterSeed`, fails the parse with `wire_offset_table_full`.
void run_crafted_cluster_cell(std::size_t entries) {
    t100::overlay_seed_guard const guard;
    fixpp::wire::detail::set_overlay_seed_for_testing(kClusterSeed);

    std::size_t const k = entries - 4U;
    auto const mask = static_cast<std::uint32_t>(t100::overlay_cap_ref(entries) - 1U);
    auto const cluster = t100::window_cluster(kClusterSeed, mask, k);
    ASSERT_EQ(cluster.size(), k) << "setup: not enough eligible tags for the cluster";

    std::vector<std::uint16_t> body_tags{35};
    body_tags.insert(body_tags.end(), cluster.begin(), cluster.end());
    auto const seq = t100::frame_tag_sequence(body_tags);
    ASSERT_EQ(seq.size(), entries) << "setup: the frame's entry count";
    auto const sim = t100::simulate_build(seq, kClusterSeed);
    ASSERT_GT(sim.total_probes, sim.budget())
        << "setup: the crafted cluster must exceed kMaxBuildProbe x occurrences; total "
        << sim.total_probes << ", budget " << sim.budget();

    auto const buf = fixpp::test100::frame44(cluster_body(cluster));
    auto const fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    ASSERT_LE(buf.size(), std::size_t{65536}) << "setup: within the default inbound byte limit";

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser;
    auto const r = parser.parse(*fv, &arena);
    std::size_t absent = 0;
    if (r.has_value()) {
        for (auto const t : cluster) {
            if (!r->offsets().find(t).has_value()) {
                ++absent;
            }
        }
    }
    ASSERT_FALSE(r.has_value())
        << "US3.1 / FR-008: a frame whose probe work exceeds the per-build budget must fail the "
           "parse; it parsed, with "
        << absent << " of " << cluster.size() << " cluster tags absent from its index";
    EXPECT_EQ(r.error(), error::wire_offset_table_full)
        << "US3.1: actual error " << static_cast<int>(r.error());
}

TEST(ProbeBudget100, US3_1_CraftedClusterFailsParse_512Entries) { run_crafted_cluster_cell(512); }
TEST(ProbeBudget100, US3_1_CraftedClusterFailsParse_1024Entries) { run_crafted_cluster_cell(1024); }
TEST(ProbeBudget100, US3_1_CraftedClusterFailsParse_2048Entries) { run_crafted_cluster_cell(2048); }
TEST(ProbeBudget100, US3_1_CraftedClusterFailsParse_4000Entries) { run_crafted_cluster_cell(4000); }

// A fixed set of distinct, non-consecutive body tags behind a session header, sized so the
// overlay's load is near its highest (research D1's measurement: such sets reach today's
// per-insert cap for some seeds while their total work stays within the per-build budget). The
// set is drawn by a fixed linear congruential sequence from 1..kNonCraftedRange, skipping a tag
// that is ineligible, already drawn, or adjacent to a drawn one. With the session header and the
// envelope, the frame's entry count is the largest that keeps the overlay at its capacity.
// Under `mix()` the slot of a tag depends on the seed only through the seed's low bits within the
// mask, so whether some seed reaches the cap is a property of the set; the setup asserts it.
constexpr std::size_t kNonCraftedBodyTags = 401;
constexpr std::uint32_t kNonCraftedRange = 9999;
constexpr std::uint32_t kNonCraftedLcgStart = 12346;

std::vector<std::uint16_t> non_crafted_tags() {
    std::vector<std::uint16_t> out;
    std::vector<bool> used(65536U, false);
    std::uint32_t lcg = kNonCraftedLcgStart;
    while (out.size() < kNonCraftedBodyTags) {
        lcg = (lcg * 1664525U) + 1013904223U;
        auto const t = static_cast<std::uint16_t>(1U + ((lcg >> 8U) % kNonCraftedRange));
        if (!t100::cluster_eligible(t) || used[t] || used[t - 1U] || used[t + 1U]) {
            continue;
        }
        used[t] = true;
        out.push_back(t);
    }
    return out;
}

constexpr char const* kSessionHeader =
    "35=D\x01"
    "49=S\x01"
    "56=T\x01"
    "34=1\x01"
    "52=20240101-00:00:00.000\x01";
std::vector<std::uint16_t> const kSessionHeaderTags{35, 49, 56, 34, 52};

// US3.2: the frame (c) finds, under the seed it finds, parses and every tag is found.
TEST(ProbeBudget100, US3_2_NonCraftedFrameAtPerInsertCapParsesAndIndexesEveryTag) {
    t100::overlay_seed_guard const guard;

    auto const tags = non_crafted_tags();
    std::vector<std::uint16_t> body_tags = kSessionHeaderTags;
    body_tags.insert(body_tags.end(), tags.begin(), tags.end());
    auto const seq = t100::frame_tag_sequence(body_tags);

    auto const seed = t100::find_seed_reaching_insert_cap(seq, t100::kSeedSearchBound);
    ASSERT_TRUE(seed.has_value())
        << "setup: no seed up to kSeedSearchBound makes one insert reach kMaxBuildProbe";
    auto const sim = t100::simulate_build(seq, *seed);
    ASSERT_GE(sim.max_insert_probes, t100::kMaxBuildProbe) << "setup: the per-insert cap";
    ASSERT_LE(sim.total_probes, sim.budget())
        << "setup: the frame must stay within the per-build budget; total " << sim.total_probes
        << ", budget " << sim.budget();

    fixpp::wire::detail::set_overlay_seed_for_testing(*seed);
    std::string body = kSessionHeader;
    for (auto const t : tags) {
        body += std::to_string(t) + "=x\x01";
    }
    auto const buf = fixpp::test100::frame44(body);
    auto const fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser;
    auto const r = parser.parse(*fv, &arena);
    ASSERT_TRUE(r.has_value()) << "US3.2: a frame within the per-build budget must parse; error "
                               << static_cast<int>(r.error());
    std::vector<std::uint16_t> missing;
    for (auto const t : seq) {
        if (!r->offsets().find(t).has_value()) {
            missing.push_back(t);
        }
    }
    std::string list;
    for (auto const t : missing) {
        list += std::to_string(t) + " ";
    }
    EXPECT_TRUE(missing.empty())
        << "US3.2 / FR-008: every distinct tag of a frame within the budget must be found; "
           "absent from the index (seed "
        << *seed << "): " << list;
}

// Control: a frame within both bounds parses and indexes every tag, under the cluster seed.
TEST(ProbeBudget100, Control_WithinBudgetFrameParsesAndIndexesEveryTag) {
    t100::overlay_seed_guard const guard;
    fixpp::wire::detail::set_overlay_seed_for_testing(kClusterSeed);

    std::vector<std::uint16_t> tags;
    for (std::uint16_t t = 5000; tags.size() < 30; t = static_cast<std::uint16_t>(t + 3)) {
        tags.push_back(t);
    }
    std::vector<std::uint16_t> body_tags = kSessionHeaderTags;
    body_tags.insert(body_tags.end(), tags.begin(), tags.end());
    auto const seq = t100::frame_tag_sequence(body_tags);
    auto const sim = t100::simulate_build(seq, kClusterSeed);
    ASSERT_LT(sim.max_insert_probes, t100::kMaxBuildProbe) << "setup: below the per-insert cap";
    ASSERT_LE(sim.total_probes, sim.budget()) << "setup: within the per-build budget";

    std::string body = kSessionHeader;
    for (auto const t : tags) {
        body += std::to_string(t) + "=x\x01";
    }
    auto const buf = fixpp::test100::frame44(body);
    auto const fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser;
    auto const r = parser.parse(*fv, &arena);
    ASSERT_TRUE(r.has_value()) << "control: error " << static_cast<int>(r.error());
    for (auto const t : seq) {
        EXPECT_TRUE(r->offsets().find(t).has_value()) << "control: tag " << t << " absent";
    }
}

}  // namespace
