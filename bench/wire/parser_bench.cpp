// SPDX-License-Identifier: AGPL-3.0-or-later
// bench/wire/parser_bench.cpp
//
// T048 — [2b §6.6] Parser<Iter/Index>::parse latency harness (seam #5).
//
// [2b §6.6] ceilings:
//   Parser<Iter>::parse_iter   20-tag message            ≤  80 ns
//   Parser<Index>::parse       20-tag message            ≤ 400 ns
//   Parser<Index>::parse       200-tag Instrument-heavy  ≤   4 µs
//
// Baseline seed: bench/baselines/wire/parser_bench.json (written on the first
// green CI run that includes this bench).
//
// hffix comparison: hffix is NOT wired into the build.
// D-14 says "measured-not-blocker"; see bench/REPORT.md.
// Validator bench omitted (US4 PAUSED — deferred with US4).

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
#include <fixpp/wire/parser.hpp>

// Concrete dict::table_view definition (seam #1 — test double).
// The real 2c table_view is forward-declared only in parser.hpp; the bench
// is not production code, so the test double is appropriate here.
#include "support/mock_dict_table.hpp"
// Test-only frame_view_factory (friend-of-frame_view).
#include "support/frame_view_factory.hpp"

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::frame_view;
using fixpp::wire::MessageView;
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

// ~20-tag NewOrderSingle-ish body.
const std::string k20TagBody =
    std::string("35=D\x01") + "34=1\x01" + "49=SENDER01\x01" + "56=TARGET01\x01"
    + "52=20260516-09:30:00.000\x01" + "11=ORD12345678\x01" + "55=AAPL\x01"
    + "54=1\x01" + "38=100\x01" + "40=2\x01" + "44=150.25\x01"
    + "59=0\x01" + "60=20260516-09:30:00.000\x01" + "1=ACC001\x01"
    + "21=1\x01" + "110=0\x01" + "111=0\x01" + "15=USD\x01"
    + "58=BenchOrder\x01" + "207=XNAS\x01";

// ~200-tag Instrument-heavy body: repeat many fields to exercise the hash
// overlay more heavily. Real 200-tag messages have many repeating-group
// entries; this approximates the allocation + hash insert cost.
static std::string build_200tag_body() {
    std::string body = std::string("35=W\x01") + "34=1\x01"
                       + "49=SENDER01\x01" + "56=TARGET01\x01"
                       + "52=20260516-09:30:00.000\x01" + "55=AAPL\x01";
    // NoMDEntries (268) = 48; then 48 × 4 fields ≈ 198 total fields.
    body += "268=48\x01";
    for (int i = 0; i < 48; ++i) {
        body += "269=0\x01";      // MDEntryType
        body += "270=150.25\x01"; // MDEntryPx
        body += "271=100\x01";    // MDEntrySize
        body += "272=20260516\x01"; // MDEntryDate
    }
    return body;
}

const std::vector<std::byte> k20TagFrame  = build_frame(k20TagBody);
const std::string             k200TagBody = build_200tag_body();
const std::vector<std::byte>  k200TagFrame = build_frame(k200TagBody);

// Mint frame_views via the test factory (friend of frame_view).
[[nodiscard]] frame_view make_fv(std::span<const std::byte> buf) {
    auto r = fixpp::wire::test::make_frame_view(buf);
    return r.value_or(frame_view{});
}

const frame_view k20TagFv  = make_fv(k20TagFrame);
const frame_view k200TagFv = make_fv(k200TagFrame);

}  // namespace

// ── BM_Parser_Iter_20tag ──────────────────────────────────────────────────────
// Parser<Iter>::parse_iter — 20-tag message.
// [2b §6.6] ceiling ≤ 80 ns.
static void BM_Parser_Iter_20tag(benchmark::State& state) {
    fixpp::dict::table_view tv{};
    Parser<access_mode::Iter> p{tv};
    for (auto _ : state) {
        auto r = p.parse_iter(k20TagFv);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Parser_Iter_20tag);

// ── BM_Parser_Index_20tag ─────────────────────────────────────────────────────
// Parser<Index>::parse — 20-tag message.
// [2b §6.6] ceiling ≤ 400 ns.
static void BM_Parser_Index_20tag(benchmark::State& state) {
    fixpp::dict::table_view tv{};
    Parser<access_mode::Index> p{tv};
    std::array<std::byte, 32 * 1024> arena_buf{};
    for (auto _ : state) {
        std::pmr::monotonic_buffer_resource arena{
            arena_buf.data(), arena_buf.size(),
            std::pmr::null_memory_resource()};
        auto r = p.parse(k20TagFv, &arena);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Parser_Index_20tag);

// ── BM_Parser_Index_200tag ────────────────────────────────────────────────────
// Parser<Index>::parse — 200-tag Instrument-heavy message.
// [2b §6.6] ceiling ≤ 4 µs.
static void BM_Parser_Index_200tag(benchmark::State& state) {
    fixpp::dict::table_view tv{};
    Parser<access_mode::Index> p{tv};
    std::array<std::byte, 256 * 1024> arena_buf{};
    for (auto _ : state) {
        std::pmr::monotonic_buffer_resource arena{
            arena_buf.data(), arena_buf.size(),
            std::pmr::null_memory_resource()};
        auto r = p.parse(k200TagFv, &arena);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Parser_Index_200tag);

BENCHMARK_MAIN();
