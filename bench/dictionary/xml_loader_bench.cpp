// SPDX-License-Identifier: AGPL-3.0-or-later
// bench/dictionary/xml_loader_bench.cpp
//
// Google Benchmark harness for fixpp::dict::XmlLoader.
// NFR-002-1: parse latency ≤ 80 ms / 4 MiB PMR (single-threaded wall-clock).
//
// Compile definition required:
//   FIXPP_DICT_DATA_DIR  — absolute path to the `dictionaries/` directory
//                          (set by bench/dictionary/CMakeLists.txt).

#include <fixpp/dict/xml_loader.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory_resource>
#include <string>

#include <benchmark/benchmark.h>

namespace {

// Helper: build the path to a named dictionary file.
std::filesystem::path dict_path(std::string_view filename) {
    return std::filesystem::path{FIXPP_DICT_DATA_DIR} / filename;
}

// -----------------------------------------------------------------------
// BM_XmlLoader_LoadFix44
// -----------------------------------------------------------------------
void BM_XmlLoader_LoadFix44(benchmark::State& state) {
    const auto path = dict_path("FIX44.xml");
    for (auto _ : state) {
        state.PauseTiming();
        std::array<std::byte, 4u * 1024u * 1024u> buffer{};
        std::pmr::monotonic_buffer_resource mr{buffer.data(), buffer.size()};
        state.ResumeTiming();
        auto d = fixpp::dict::XmlLoader{}.load(path, &mr);
        benchmark::DoNotOptimize(d);
    }
    state.SetItemsProcessed(state.iterations());
}

// -----------------------------------------------------------------------
// BM_XmlLoader_LoadFix42
// -----------------------------------------------------------------------
void BM_XmlLoader_LoadFix42(benchmark::State& state) {
    const auto path = dict_path("FIX42.xml");
    for (auto _ : state) {
        state.PauseTiming();
        std::array<std::byte, 4u * 1024u * 1024u> buffer{};
        std::pmr::monotonic_buffer_resource mr{buffer.data(), buffer.size()};
        state.ResumeTiming();
        auto d = fixpp::dict::XmlLoader{}.load(path, &mr);
        benchmark::DoNotOptimize(d);
    }
    state.SetItemsProcessed(state.iterations());
}

// -----------------------------------------------------------------------
// BM_XmlLoader_LoadFix50SP2
// -----------------------------------------------------------------------
void BM_XmlLoader_LoadFix50SP2(benchmark::State& state) {
    const auto path = dict_path("FIX50SP2.xml");
    for (auto _ : state) {
        state.PauseTiming();
        std::array<std::byte, 4u * 1024u * 1024u> buffer{};
        std::pmr::monotonic_buffer_resource mr{buffer.data(), buffer.size()};
        state.ResumeTiming();
        auto d = fixpp::dict::XmlLoader{}.load(path, &mr);
        benchmark::DoNotOptimize(d);
    }
    state.SetItemsProcessed(state.iterations());
}

}  // namespace

BENCHMARK(BM_XmlLoader_LoadFix44)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_XmlLoader_LoadFix42)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_XmlLoader_LoadFix50SP2)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
