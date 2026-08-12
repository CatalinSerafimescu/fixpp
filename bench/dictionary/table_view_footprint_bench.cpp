// SPDX-License-Identifier: AGPL-3.0-or-later
// bench/dictionary/table_view_footprint_bench.cpp
//
// 075-live-wire-enum-validation T011 — PRE-CHANGE baseline for SC-006:
// `Dictionary::as_table_view()` build time + `sizeof(fixpp::dict::table_view)`
// footprint, measured on FIX50SP2 (668 enum-backed fields / 5565 codes — the
// largest case) and FIX44 (245 enum-backed fields / 1708 codes — mid-size
// reference) as they stand BEFORE Phase 2 adds an owned enum-domain table to
// `table_view` (research R-10 / O-3; plan.md SC-006 row; data-model.md
// "Footprint" note). Measured, not asserted. Re-measured post-change by T032.
//
// Compile definition required:
//   FIXPP_DICT_DATA_DIR  — absolute path to the `dictionaries/` directory
//                          (set by bench/dictionary/CMakeLists.txt).
//
// ─── MEASURED BASELINE (pre-change, commit 4cb65c5d, linux-clang-release,
//     WSL2 host, 10 X 3593 MHz CPUs) ─────────────────────────────────────
// Reproduce:
//   cmake --build build/linux-clang-release -j2 --target table_view_footprint_bench
//   ./build/linux-clang-release/bench/dictionary/table_view_footprint_bench \
//       --benchmark_repetitions=10 --benchmark_report_aggregates_only=true
//
//   sizeof(fixpp::dict::table_view):            336 bytes  (compile-time
//                                                constant; single measurement)
//
//   BM_TableView_BuildFix50SP2  (10 repetitions; google-benchmark auto-picks
//                                 the inner iteration count per repetition
//                                 to hit its stable-measurement time budget):
//     mean:    274789 us  (~274.8 ms)
//     median:  273776 us  (~273.8 ms)
//     stddev:    3474 us  (cv 1.26%)
//
//   BM_TableView_BuildFix44     (10 repetitions, same methodology):
//     mean:      2567 us  (~2.57 ms)
//     median:    2550 us  (~2.55 ms)
//     stddev:    74.5 us  (cv 2.90%)
//
// Both `dictionary` objects are loaded ONCE outside the timed loop (XML
// parse cost is excluded — see xml_loader_bench.cpp for that figure
// separately); only `as_table_view()` itself is timed.
//
// ─── MEASURED POST-CHANGE (T032, commit 51b8644c + T030/T032/T033 wip,
//     linux-clang-release, WSL2 host, 10 X 3593 MHz CPUs) ─────────────────
//   sizeof(fixpp::dict::table_view):            392 bytes  (336 + the owned
//                                                enum-domain table, T017)
//
//   BM_TableView_BuildFix50SP2  (10 repetitions):
//     mean:    275081 us  (~275.1 ms)  — +0.11% vs the 274789 us baseline
//     median:  273860 us  (~273.9 ms)
//     stddev:    7422 us  (cv 2.70%)
//
//   BM_TableView_BuildFix44     (10 repetitions):
//     mean:      2697 us  (~2.70 ms)   — +5.06% vs the 2567 us baseline
//     median:    2700 us  (~2.70 ms)
//     stddev:    38.9 us  (cv 1.44%)
//
// Both deltas are single-digit-% or smaller — expected given the added enum-
// domain projection walks the store once at build time (T015). The FIX50SP2/
// FIX44 build-time ratio stays ~102x (was ~107x pre-change) — the superlinear
// build-time shape observed at T011 is pre-existing and unaffected by 075
// (T011's note: NOT 075's doing, not 075's to fix).
// ─────────────────────────────────────────────────────────────────────────

#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory_resource>
#include <string_view>

#include <benchmark/benchmark.h>

namespace {

// Helper: build the path to a named dictionary file (mirrors
// xml_loader_bench.cpp's dict_path()).
std::filesystem::path dict_path(std::string_view filename) {
    return std::filesystem::path{FIXPP_DICT_DATA_DIR} / filename;
}

}  // namespace

// -----------------------------------------------------------------------
// BM_TableView_Sizeof — sizeof(fixpp::dict::table_view), reported via a
// counter (compile-time constant; Iterations(1), mirrors the
// offset_table_footprint_bench.cpp FOOTPRINT_BENCH convention).
// -----------------------------------------------------------------------
static void BM_TableView_Sizeof(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(sizeof(fixpp::dict::table_view));
    }
    state.counters["sizeof_B"] = static_cast<double>(sizeof(fixpp::dict::table_view));
}
BENCHMARK(BM_TableView_Sizeof)->Iterations(1);

// -----------------------------------------------------------------------
// BM_TableView_BuildFix50SP2 — Dictionary::as_table_view() build time on
// FIX50SP2 (668 enum-backed fields / 5565 codes — the largest case). The
// Dictionary itself is loaded ONCE outside the timed loop (as_table_view()
// is a const, read-only method; XML parse cost is not part of this
// measurement — see xml_loader_bench.cpp for that figure separately).
// -----------------------------------------------------------------------
static void BM_TableView_BuildFix50SP2(benchmark::State& state) {
    auto const path = dict_path("FIX50SP2.xml");
    // Loaded ONCE, outside the timed loop — mirrors xml_loader_bench.cpp's
    // 4 MiB PMR arena sizing for this same file.
    std::array<std::byte, 4u * 1024u * 1024u> buffer{};
    std::pmr::monotonic_buffer_resource mr{buffer.data(), buffer.size()};
    auto const dictionary = fixpp::dict::XmlLoader{}.load(path, &mr);
    for (auto _ : state) {
        auto tv = dictionary.as_table_view();
        benchmark::DoNotOptimize(tv);
    }
}
BENCHMARK(BM_TableView_BuildFix50SP2)->Unit(benchmark::kMicrosecond);

// -----------------------------------------------------------------------
// BM_TableView_BuildFix44 — Dictionary::as_table_view() build time on FIX44
// (245 enum-backed fields / 1708 codes — mid-size reference point).
// -----------------------------------------------------------------------
static void BM_TableView_BuildFix44(benchmark::State& state) {
    auto const path = dict_path("FIX44.xml");
    std::array<std::byte, 4u * 1024u * 1024u> buffer{};
    std::pmr::monotonic_buffer_resource mr{buffer.data(), buffer.size()};
    auto const dictionary = fixpp::dict::XmlLoader{}.load(path, &mr);
    for (auto _ : state) {
        auto tv = dictionary.as_table_view();
        benchmark::DoNotOptimize(tv);
    }
}
BENCHMARK(BM_TableView_BuildFix44)->Unit(benchmark::kMicrosecond);

// -----------------------------------------------------------------------
// BM_TableView_BuildFix42 — 082-structural-group-detection T046 / FR-022(a).
//
// NEW ROW. FIX 4.2 was absent from this profile because it registered ZERO
// repeating groups (L-063-1: its `<group>` count fields are typed legacy XML
// `INT`, and detection used to key on the datatype), so `as_table_view()` did
// no group work at all for it. 082 makes detection structural, so FIX 4.2 now
// registers 18 groups and this row measures work that did not previously exist.
// It is therefore the one row on this profile with no pre-change counterpart —
// stated rather than silently baselined.
//
// `group_bits_words` is the heap-footprint half of FR-022(a): `table_view`'s
// `sizeof` is a compile-time constant and does NOT move (see
// BM_TableView_Sizeof), but `group_bits_` is a `std::vector<std::uint64_t>`
// that goes from EMPTY to `(max registered no_tag >> 6) + 1` words per
// `table_view` copy on FIX40/41/42. Derived here from the public accessor
// rather than from the private member, so the figure is reproducible.
// -----------------------------------------------------------------------
namespace {

// Highest registered group count-tag, via the public structural accessor.
std::uint16_t max_registered_group_tag(fixpp::dict::table_view const& tv) {
    std::uint16_t hi = 0;
    for (std::uint32_t t = 1; t <= 10000U; ++t) {
        if (tv.group_first_field(static_cast<std::uint16_t>(t)) != 0) {
            hi = static_cast<std::uint16_t>(t);
        }
    }
    return hi;
}

std::size_t group_bits_words(fixpp::dict::table_view const& tv) {
    std::uint16_t const hi = max_registered_group_tag(tv);
    return hi == 0 ? 0U : static_cast<std::size_t>(hi >> 6U) + 1U;
}

}  // namespace

static void BM_TableView_BuildFix42(benchmark::State& state) {
    auto const path = dict_path("FIX42.xml");
    std::array<std::byte, 4u * 1024u * 1024u> buffer{};
    std::pmr::monotonic_buffer_resource mr{buffer.data(), buffer.size()};
    auto const dictionary = fixpp::dict::XmlLoader{}.load(path, &mr);
    for (auto _ : state) {
        auto tv = dictionary.as_table_view();
        benchmark::DoNotOptimize(tv);
    }
    auto const tv = dictionary.as_table_view();
    state.counters["group_bits_words"] = static_cast<double>(group_bits_words(tv));
    state.counters["group_bits_B"] =
        static_cast<double>(group_bits_words(tv) * sizeof(std::uint64_t));
}
BENCHMARK(BM_TableView_BuildFix42)->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
