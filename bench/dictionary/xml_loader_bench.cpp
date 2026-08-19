// SPDX-License-Identifier: AGPL-3.0-or-later
// bench/dictionary/xml_loader_bench.cpp
//
// Google Benchmark harness for fixpp::dict::XmlLoader.
//
// NFR-002-1 (spec.md §6): `XmlLoader::load(FIX44.xml, mr)` ≤ **500 ms**
// wall-clock, Linux-native ext4, warm FS cache — "loaded once per session, not
// on the hot path". Absolute CI ceiling **1 s** on the slowest preset
// (FIX50SP2 / Debug / WSL2) per research.md D-18. Relative budget: ±5 % vs
// `bench/baselines/dictionary/xml_loader.json` per `[const §VIII.2]`.
//
// ⚠️ This comment previously read "parse latency ≤ 80 ms / 4 MiB PMR". Both
// figures were invented: before this correction the repository's sole "80 ms"
// occurrence was that comment itself, and NFR-002-1 says nothing whatsoever
// about memory. The misquote sent issue #263's investigation after a
// 6x-too-tight latency bar and a memory budget that does not exist. Cite
// spec.md §6 / research.md D-18 — do not restate the numbers here without them.
//
// ⚠️ The 4 MiB arena below is THIS HARNESS'S choice, not a product budget.
// NFR-002-1 sets no memory bound, and no shipped call site *constructs* a
// fixed-capacity arena: `src/capi/dictionary.cpp` passes
// `std::pmr::get_default_resource()`, while `load_any.cpp` and the codegen tool
// forward a caller-supplied `mr`. Note this is NOT the same as "a bounded arena
// can never reach the loader" — `src/config/selector_resolver.cpp` forwards
// `LoadOptions::resource`, which is host-supplied (it merely *defaults* to
// `get_default_resource()`), so an embedding application may legitimately hand
// the loader a bounded arena and must size it per the note below.
//
// ⚠️ 4 MiB does not hold FIX50SP2, and did not at `ae4c9a56` either — the
// loader's own introducing merge already needed ~9.41 MB, against ~10.9 MB on
// the revision these figures were taken from. A `monotonic_buffer_resource`
// falls back to its upstream SILENTLY once the initial buffer is exhausted, so
// `BM_XmlLoader_LoadFix50SP2` is timing allocator fallback in addition to the
// parse. Measured cost of that fallback: ~3 %, i.e. within the measurement
// noise — the reported time is still dominated by real parse work, but the
// harness is not measuring what its shape implies. That is the durable point;
// the absolute byte figures move with the loader and with `dictionaries/`, and
// are recorded with their conditions in #274 rather than maintained here.
//
// ⚠️ If you resize this arena: a `monotonic_buffer_resource` does not reclaim
// individual deallocations before `release()` or destruction, so it must cover
// the CUMULATIVE request (~21.8 MB for FIX50SP2 as measured), not the ~10.9 MB
// that survives the load. Sizing from the surviving footprint under-provisions
// by roughly 2x — a 16 MiB arena with a `null_memory_resource()` upstream makes
// the FIX50SP2 load throw `xml_oom_error`.
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
