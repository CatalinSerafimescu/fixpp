#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/nested_probe_budget_100.hpp — TEST-ONLY support for 100-wire-group-read-integrity's
// D7 (ii) cells (tasks T012, T039, T040; research D7 witness (ii), D1): the one construction of
// tests/capi/test_100_seed_hook_capi.cpp's cell and of its wire-layer and typed twins.
//
// A test-built `table_view` whose group G registers, in the read's context, the delimiter, a
// cluster of more than `kMaxBuildProbe` distinct member tags and a nested group N. The cluster is
// T007's window cluster against the sub-table's mask (the instance slice's own entry count), under
// a seed for which `simulate_build` predicts the root build within its budget (the root carries
// filler fields outside the group, so its mask is wider) and the sub-build over it. Each cell sets
// the plan's seed in its own body (probe_cluster_100.hpp (b)); this header sets none.

#include <cstddef>
#include <cstdint>
#include <fixpp/dict/table_view.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "support/probe_cluster_100.hpp"

namespace fixpp::wire::test100::nested_budget {

inline constexpr std::uint16_t kG = 9500;
inline constexpr std::uint16_t kDelim = 9501;
inline constexpr std::uint16_t kN = 9502;
inline constexpr std::uint16_t kNMember = 9503;
inline constexpr std::size_t kCluster = 400;
inline constexpr std::size_t kFiller = 200;
inline constexpr std::uint16_t kFirstFiller = 30000;
inline constexpr std::uint32_t kSeedSearch = 4096;

[[nodiscard]] inline bool reserved(std::uint16_t t) {
    return (t >= kG && t <= kNMember) || (t >= kFirstFiller && t < kFirstFiller + 2U * kFiller) ||
           t == 35;
}

struct plan {
    std::uint32_t seed = 0;
    std::vector<std::uint16_t> cluster;
    std::vector<std::uint16_t> filler;
};

// The first seed whose cluster keeps the root within its budget and takes the sub-build over it.
[[nodiscard]] inline std::optional<plan> find_plan() {
    std::size_t const sub_entries = kCluster + 3U;  // delimiter, cluster, N, N's member
    auto const sub_mask = static_cast<std::uint32_t>(overlay_cap_ref(sub_entries) - 1U);
    for (std::uint32_t seed = 1; seed <= kSeedSearch; ++seed) {
        plan p;
        p.seed = seed;
        for (auto const t : window_cluster(seed, sub_mask, kCluster + 16U)) {
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
        auto const root_sim = simulate_build(frame_tag_sequence(body), seed);
        auto const sub_sim = simulate_build(sub, seed);
        if (root_sim.total_probes <= root_sim.budget() && sub_sim.total_probes > sub_sim.budget()) {
            return p;
        }
    }
    return std::nullopt;
}

// G at the root of MsgType "U", its members and the nested N registered under [G].
[[nodiscard]] inline fixpp::dict::table_view make_table(plan const& p) {
    std::uint16_t const root_path[] = {kG};
    fixpp::dict::table_view_builder b;
    b.add_valid("U", 35);
    b.add_valid("U", kG);
    b.set_group_first_ctx("U", {}, kG, kDelim);
    for (auto const t : p.cluster) {
        b.add_group_member_ctx("U", {}, kG, t);
    }
    b.add_group_member_ctx("U", {}, kG, kN);
    b.set_group_first_ctx("U", root_path, kN, kNMember);
    return std::move(b).build();
}

// The fillers, then one G instance: the delimiter, the cluster, and N with one instance.
[[nodiscard]] inline std::vector<std::byte> make_frame(plan const& p) {
    std::string body = "35=U\x01";
    for (auto const t : p.filler) {
        body += std::to_string(t) + "=f\x01";
    }
    body += std::to_string(kG) + "=1\x01" + std::to_string(kDelim) + "=x\x01";
    for (auto const t : p.cluster) {
        body += std::to_string(t) + "=c\x01";
    }
    body += std::to_string(kN) + "=1\x01" + std::to_string(kNMember) + "=y\x01";
    return ::fixpp::test100::frame44(body);
}

}  // namespace fixpp::wire::test100::nested_budget
