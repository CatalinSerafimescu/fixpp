// SPDX-License-Identifier: AGPL-3.0-or-later
// bench/dictionary/xml_loader_bench.cpp
//
// Google Benchmark harness for fixpp::dict::XmlLoader.
//
// NFR-002-1 (spec.md:178): `XmlLoader::load(FIX44.xml, mr)` ≤ **500 ms**
// wall-clock, Linux-native ext4, warm FS cache — "loaded once per session, not
// on the hot path". Absolute CI ceiling **1 s** on the slowest preset
// (FIX50SP2 / Debug / WSL2) per research.md D-18. Relative budget: ±5 % vs
// `bench/baselines/dictionary/xml_loader.json` per `[const §VIII.2]`.
//
// ⚠️ This comment previously read "parse latency ≤ 80 ms / 4 MiB PMR". Both
// figures were invented: "80 ms" appeared nowhere else in the repository, and
// NFR-002-1 says nothing whatsoever about memory. That misquote sent issue
// #263's investigation after a 6x-too-tight latency bar and a memory budget
// that does not exist. Cite spec.md:178 / research.md D-18 — do not restate
// the numbers here without them.
//
// ⚠️ The 4 MiB arena below is THIS HARNESS'S choice, not a product budget. No
// shipped caller passes a bounded arena: `src/capi/dictionary.cpp` passes
// `std::pmr::get_default_resource()`, and `load_any.cpp` / the codegen tool
// forward a caller-supplied `mr`.
//
// ⚠️ 4 MiB has NEVER held FIX50SP2 — it needed 9.41 MB at `ae4c9a56`, the
// loader's own introducing merge, and 10.9 MB today. A
// `monotonic_buffer_resource` falls back to its upstream SILENTLY when the
// initial buffer is exhausted, so `BM_XmlLoader_LoadFix50SP2` has always been
// timing ~30 MB of allocator fallback in addition to the parse. Measured cost
// of that fallback: ~3 % (within noise), so the reported time is still
// dominated by real parse work — but the harness is not measuring what its
// shape implies. Tracked as #274.
//
// ⚠️ If you resize this arena: a monotonic resource NEVER reclaims, so it must
// cover the CUMULATIVE request (21.8 MB for FIX50SP2 today), not the 10.9 MB
// that survives the load. Sizing from the surviving footprint under-provisions
// by ~2x — a 16 MiB arena with a `null_memory_resource()` upstream makes the
// FIX50SP2 load throw `xml_oom_error`.
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
