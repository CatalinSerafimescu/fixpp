// SPDX-License-Identifier: AGPL-3.0-or-later
// bench/wire/offset_table_bench.cpp
//
// T048 — [2b §6.6] OffsetTable::find latency harness (seam #5).
//
// [2b §6.6] ceiling:
//   OffsetTable::find   after build, 32-slot hash   ≤ 15 ns
//
// Baseline seed: bench/baselines/wire/offset_table_bench.json.

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
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/parser.hpp>

// Concrete dict::table_view definition (seam #1 — test double).
#include "support/mock_dict_table.hpp"
#include "support/frame_view_factory.hpp"

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::frame_view;
using fixpp::wire::OffsetTable;
using fixpp::wire::Parser;

[[nodiscard]] std::vector<std::byte> build_frame(std::string_view body) {
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

// ~20-tag NewOrderSingle body (matches the [2b §6.6] "32-slot hash" scenario).
const std::string k20TagBody =
    std::string("35=D\x01") + "34=1\x01" + "49=SENDER01\x01" + "56=TARGET01\x01"
    + "52=20260516-09:30:00.000\x01" + "11=ORD12345678\x01" + "55=AAPL\x01"
    + "54=1\x01" + "38=100\x01" + "40=2\x01" + "44=150.25\x01"
    + "59=0\x01" + "60=20260516-09:30:00.000\x01" + "1=ACC001\x01"
    + "21=1\x01" + "110=0\x01" + "111=0\x01" + "15=USD\x01"
    + "58=BenchOrder\x01" + "207=XNAS\x01";

const std::vector<std::byte> k20TagFrame = build_frame(k20TagBody);

[[nodiscard]] frame_view make_fv(std::span<const std::byte> buf) {
    auto r = fixpp::wire::test::make_frame_view(buf);
    return r.value_or(frame_view{});
}

const frame_view k20TagFv = make_fv(k20TagFrame);

// A pre-built OffsetTable for the find() bench — building it inside the bench
// loop would conflate build cost with find cost. We rebuild per-state rather
// than caching across iterations to keep the arena fresh.
std::array<std::byte, 32 * 1024> g_arena_buf{};

}  // namespace

// ── BM_OffsetTable_Find_32slot ────────────────────────────────────────────────
// find() after a 20-tag parse (32-slot hash overlay). [2b §6.6] ceiling ≤ 15 ns.
// Measures one find() call on the already-built table (tag 55 = Symbol).
static void BM_OffsetTable_Find_32slot(benchmark::State& state) {
    std::pmr::monotonic_buffer_resource arena{
        g_arena_buf.data(), g_arena_buf.size(),
        std::pmr::null_memory_resource()};
    OffsetTable tbl{k20TagFv, &arena};

    for (auto _ : state) {
        auto e = tbl.find(55);  // Symbol tag
        benchmark::DoNotOptimize(e);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_OffsetTable_Find_32slot);

BENCHMARK_MAIN();
