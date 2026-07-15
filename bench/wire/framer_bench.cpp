// SPDX-License-Identifier: AGPL-3.0-or-later
// bench/wire/framer_bench.cpp
//
// T048 — [2b §6.6] Framer::feed latency harness (seam #5).
//
// [2b §6.6] ceiling:
//   Framer::feed   one frame, no carry, 80-byte body   ≤ 30 ns
//
// Baseline seed: bench/baselines/wire/framer_bench.json (written on the first
// green CI run that includes this bench).
//
// hffix comparison: hffix is NOT wired into the build — D-14 says
// "measured-not-blocker"; see bench/REPORT.md.

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <span>
#include <string>
#include <vector>

#include <fixpp/wire/framer.hpp>

namespace {

// Build a single well-formed FIX 4.4 frame (body ~80 bytes) with correct
// BodyLength and byte-sum-mod-256 CheckSum ([FIX50SP2 §3]).
[[nodiscard]] std::vector<std::byte> make_frame(std::string_view body) {
    std::string pre = std::string("8=FIX.4.4\x01") + "9="
                      + std::to_string(body.size()) + "\x01";
    pre.append(body);
    unsigned sum = 0;
    for (unsigned char c : pre) { sum += c; }
    sum %= 256U;
    char chk[8]{};
    std::snprintf(chk, sizeof(chk), "10=%03u\x01", sum);
    pre.append(chk);
    std::vector<std::byte> out(pre.size());
    std::memcpy(out.data(), pre.data(), pre.size());
    return out;
}

// A single-frame body of ~72 bytes (total frame ~80 byte body per §6.6).
// 35=D (NewOrderSingle) header fields enough to fill ~80 body bytes.
const std::string kBody80 =
    std::string("35=D\x01") + "34=1\x01" + "49=SENDER01\x01" + "56=TARGET01\x01"
    + "52=20260516-09:30:00.000\x01" + "11=ORD12345678\x01" + "55=AAPL\x01"
    + "54=1\x01" + "38=100\x01" + "40=2\x01" + "44=150.25\x01";

// Pre-built frame bytes and the frame buffer for the Framer output.
const std::vector<std::byte> kFrameBytes = make_frame(kBody80);

}  // namespace

// ── BM_Framer_Feed_NoCarry ────────────────────────────────────────────────────
// One complete 80-byte-body frame, no carry. [2b §6.6] ceiling ≤ 30 ns.
//
// Ceiling sourced from [2b §6.6]: "Framer::feed | one frame, no carry,
// 80-byte body | ≤ 30 ns". ±5% tolerance per [const §VIII.1]/[const §VIII.2].
// Note: this is the DESIGN ceiling from a release build. The bench is authored
// in the debug preset (linux-clang-debug) where no ceiling enforcement runs;
// the ±5% regression gate runs in CI on linux-clang-release.
static void BM_Framer_Feed_NoCarry(benchmark::State& state) {
    using fixpp::wire::Framer;
    using fixpp::wire::frame_view;
    using fixpp::wire::pmr_carry_buffer;

    // Carry arena (session lifetime — sized to frame + constant).
    std::array<std::byte, 512 * 1024> carry_arena_buf{};
    std::pmr::monotonic_buffer_resource carry_arena{
        carry_arena_buf.data(), carry_arena_buf.size(),
        std::pmr::null_memory_resource()};
    pmr_carry_buffer carry{fixpp::wire::default_max_frame_bytes, &carry_arena};

    // Output slots for emitted frame_views.
    std::array<frame_view, 4> out_views{};

    Framer framer;
    for (auto _ : state) {
        // Reset carry so each iteration is "no carry" (one complete frame).
        carry.clear();
        auto r = framer.feed(
            std::span<const std::byte>{kFrameBytes.data(), kFrameBytes.size()},
            carry,
            std::span<frame_view>{out_views.data(), out_views.size()});
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Framer_Feed_NoCarry);

BENCHMARK_MAIN();
