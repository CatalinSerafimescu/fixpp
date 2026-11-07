#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/probe_cluster_100.hpp — TEST-ONLY support for 100-wire-group-read-integrity
// (tasks T007; research D1 and D9 "#645 (D1)").
//
// Three tools for the cells that drive `OffsetTable::build`'s overlay probing under a known
// seed:
//   (a) `window_cluster`: the crafted window cluster research D9 describes. Under a known seed
//       and the mask of the frame's real entry count (envelope tags included), take the k
//       distinct eligible tags whose home slots are lowest, and order them highest-home first,
//       so each later insert walks past every earlier one.
//   (b) `overlay_seed_guard`: snapshots the process overlay seed on construction and restores
//       it on destruction. It never sets a seed itself: each cell calls
//       `set_overlay_seed_for_testing` in its own body, so a grep for that name finds every
//       seed-setting file (tasks T020's re-derivation).
//   (c) `find_seed_reaching_insert_cap`: for a fixed tag sequence in frame order, search seeds
//       up to a caller-named bound for one whose build makes a single insert reach
//       `kMaxBuildProbe` probes.
//
// `mix_ref`, `overlay_cap_ref` and `simulate_build` mirror `mix()`, `OffsetTable::overlay_cap_for`
// and the insert loop of `OffsetTable::build` (src/wire/offset_table.cpp), as
// tests/wire/hostile_input_hardening_test.cpp mirrors them. If those functions change, these
// mirrors change with them; `simulate_build`'s callers assert its prediction against the real
// build where the prediction is what the cell rests on.
//
// A probe is one step past an occupied slot (the `slot = (slot + 1U) & mask` advance of the
// insert loop). Inspecting the home slot is not a probe. A repeated tag walks to its first
// occurrence's slot, and those steps count (research D1).

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fixpp/core/length_data_pairs.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "support/frame_view_factory.hpp"
#include "support/wire_test_hooks.hpp"

namespace fixpp::wire::test100 {

// Mirrors the constant of the same name in `OffsetTable::build`.
inline constexpr std::size_t kMaxBuildProbe = 128;

[[nodiscard]] inline std::uint32_t mix_ref(std::uint16_t tag, std::uint32_t seed) noexcept {
    std::uint32_t h = 2166136261U ^ seed;
    h = (h ^ (static_cast<std::uint32_t>(tag) & 0xFFU)) * 16777619U;
    h = (h ^ ((static_cast<std::uint32_t>(tag) >> 8U) & 0xFFU)) * 16777619U;
    return h;
}

[[nodiscard]] inline std::size_t overlay_cap_ref(std::size_t n) noexcept {
    std::size_t const want = ((n * 5U) / 4U) + 1U;
    std::size_t cap = 8U;
    while (cap < want) {
        cap <<= 1U;
    }
    return cap;
}

// Header and trailer tags of the FIX 4.x and FIXT 1.1 standard header and trailer, the envelope
// tags among them. The cluster excludes these and the standard Length/Data pair tags, so a
// cluster frame cannot fail on framing or on a Length/Data split instead of on its probing.
[[nodiscard]] inline bool is_session_header_or_trailer_tag(std::uint16_t tag) noexcept {
    switch (tag) {
        case 8:
        case 9:
        case 10:
        case 34:
        case 35:
        case 43:
        case 49:
        case 50:
        case 52:
        case 56:
        case 57:
        case 89:
        case 90:
        case 91:
        case 93:
        case 97:
        case 115:
        case 116:
        case 122:
        case 128:
        case 129:
        case 142:
        case 143:
        case 144:
        case 145:
        case 212:
        case 213:
        case 347:
        case 369:
        case 627:
        case 628:
        case 629:
        case 630:
        case 1128:
        case 1129:
        case 1137:
        case 1156:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] inline bool cluster_eligible(std::uint16_t tag) noexcept {
    return tag != 0 && !is_session_header_or_trailer_tag(tag) &&
           !core::detail::is_standard_pair_tag(tag);
}

// (a) The k eligible tags with the lowest home slots under `seed` and `mask`, ordered highest
// home first (ties: higher tag first). Fewer than k when fewer eligible tags exist.
[[nodiscard]] inline std::vector<std::uint16_t> window_cluster(std::uint32_t seed,
                                                               std::uint32_t mask, std::size_t k) {
    std::vector<std::pair<std::uint32_t, std::uint16_t>> by_home;
    by_home.reserve(65535U);
    for (std::uint32_t t = 1; t <= 65535U; ++t) {
        auto const tag = static_cast<std::uint16_t>(t);
        if (cluster_eligible(tag)) {
            by_home.emplace_back(mix_ref(tag, seed) & mask, tag);
        }
    }
    std::ranges::sort(by_home);
    if (by_home.size() > k) {
        by_home.resize(k);
    }
    std::vector<std::uint16_t> out;
    out.reserve(by_home.size());
    for (auto it = by_home.rbegin(); it != by_home.rend(); ++it) {
        out.push_back(it->second);
    }
    return out;
}

struct build_probe_sim {
    std::size_t entries = 0;            // occurrences, envelope included
    std::size_t total_probes = 0;       // across the whole insert loop, no per-insert cap
    std::size_t max_insert_probes = 0;  // the longest single insert
    std::size_t budget() const noexcept { return kMaxBuildProbe * entries; }
};

// Simulates the overlay inserts for `frame_tags` (every tag of the frame, in frame order) under
// `seed`, with no per-insert cap: each first occurrence is placed, and every probe is counted.
[[nodiscard]] inline build_probe_sim simulate_build(std::span<std::uint16_t const> frame_tags,
                                                    std::uint32_t seed) {
    build_probe_sim sim;
    sim.entries = frame_tags.size();
    std::size_t const cap = overlay_cap_ref(frame_tags.size());
    auto const mask = static_cast<std::uint32_t>(cap - 1U);
    // Slot value: 0 empty, else tag + 1 (tags are 16-bit, so no overflow in 32 bits).
    std::vector<std::uint32_t> overlay(cap, 0U);
    for (std::uint16_t const tag : frame_tags) {
        std::uint32_t slot = mix_ref(tag, seed) & mask;
        std::size_t probes = 0;
        bool duplicate = false;
        while (overlay[slot] != 0U) {
            if (overlay[slot] == static_cast<std::uint32_t>(tag) + 1U) {
                duplicate = true;
                break;
            }
            slot = (slot + 1U) & mask;
            ++probes;
        }
        if (!duplicate) {
            overlay[slot] = static_cast<std::uint32_t>(tag) + 1U;
        }
        sim.total_probes += probes;
        if (probes > sim.max_insert_probes) {
            sim.max_insert_probes = probes;
        }
    }
    return sim;
}

// The tag sequence of the frame `frame_with_body` builds, in frame order: `8`, `9`, the body's
// tags, `10`.
[[nodiscard]] inline std::vector<std::uint16_t> frame_tag_sequence(
    std::span<std::uint16_t const> body_tags) {
    std::vector<std::uint16_t> out;
    out.reserve(body_tags.size() + 3U);
    out.push_back(8);
    out.push_back(9);
    out.insert(out.end(), body_tags.begin(), body_tags.end());
    out.push_back(10);
    return out;
}

// A FIX.4.4 frame around `body` (8=, a correct 9=, the body, 10=000), as the wire tests build
// one; `make_frame_view` does not check the checksum value.
[[nodiscard]] inline std::vector<std::byte> frame_with_body(std::string const& body) {
    std::string full = "8=FIX.4.4\x01";
    full += "9=" + std::to_string(body.size()) + "\x01";
    full += body;
    full += "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// The overlay seed in force now, read from a table built under it.
[[nodiscard]] inline std::uint32_t current_overlay_seed() {
    auto const buf = frame_with_body("35=0\x01");
    auto const fv = test::make_frame_view(buf);
    if (!fv) {
        return 0U;
    }
    std::pmr::monotonic_buffer_resource arena;
    OffsetTable const table{*fv, &arena};
    return offset_table_test_access::seed_of(table);
}

// (b) Restores the overlay seed in force at construction when it is destroyed. A local object of
// each seed-setting cell's body, so the restore runs on every exit of the cell, a fatal
// assertion's return or an exception included.
class overlay_seed_guard {
public:
    overlay_seed_guard() : saved_{current_overlay_seed()} {}
    overlay_seed_guard(overlay_seed_guard const&) = delete;
    overlay_seed_guard& operator=(overlay_seed_guard const&) = delete;
    ~overlay_seed_guard() { detail::set_overlay_seed_for_testing(saved_); }
    [[nodiscard]] std::uint32_t saved() const noexcept { return saved_; }

private:
    std::uint32_t saved_;
};

// The bound (c)'s callers search up to.
inline constexpr std::uint32_t kSeedSearchBound = 1U << 20U;

// (c) The first seed in [1, bound] under which some single insert of `frame_tags` reaches
// `kMaxBuildProbe` probes, or nullopt when none does. The caller fails its setup on nullopt; it
// never skips.
[[nodiscard]] inline std::optional<std::uint32_t> find_seed_reaching_insert_cap(
    std::span<std::uint16_t const> frame_tags, std::uint32_t bound) {
    for (std::uint32_t seed = 1; seed <= bound; ++seed) {
        if (simulate_build(frame_tags, seed).max_insert_probes >= kMaxBuildProbe) {
            return seed;
        }
    }
    return std::nullopt;
}

}  // namespace fixpp::wire::test100
