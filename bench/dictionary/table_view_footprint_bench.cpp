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
// Reproduce:
//   cmake --build build/linux-clang-release -j2 --target table_view_footprint_bench
// clang-format off
//   ./build/linux-clang-release/bench/dictionary/table_view_footprint_bench --benchmark_repetitions=10 --benchmark_report_aggregates_only=true
// clang-format on
//
// Both `dictionary` objects are loaded ONCE outside the timed loop (XML parse cost
// is excluded — see xml_loader_bench.cpp for that figure separately); only
// `as_table_view()` itself is timed.
//
// ⚠️ No measured figure is written in this file. Numbers here go stale silently,
// because nothing ever re-runs a comment — and two of them did (a `sizeof` recorded
// as 336, then 392, while the type had moved on). The recorded results live in
// `bench/baselines/dictionary/table_view_footprint_bench.json`, which is re-measured
// paired against the merge-base whenever the footprint moves.
// ─────────────────────────────────────────────────────────────────────────

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/load_any.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fstream>
#include <iterator>
#include <memory_resource>
#include <string>
#include <string_view>

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
// A pre-change figure for this row WAS measured (Gate B round 1, PR #261 —
// see bench/baselines/dictionary/table_view_footprint_bench.json's
// `_pre_change_provenance` on this row): retroactively, via this exact
// back-ported-and-run technique against a throwaway `main` worktree, not at
// T002/T046 as FR-022(a) asked.
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
    // Computed ONCE: group_bits_words() runs a 10,000-iteration group_first_field
    // sweep, and the byte figure is just the word figure scaled.
    auto const words = group_bits_words(tv);
    state.counters["group_bits_words"] = static_cast<double>(words);
    state.counters["group_bits_B"] = static_cast<double>(words * sizeof(std::uint64_t));
}
BENCHMARK(BM_TableView_BuildFix42)->Unit(benchmark::kMicrosecond);

// fixpp#215 Gate B r1 C6 — copy-vs-walk. Item 1 turned the C++ Session,
// validation-ON path from "2 walks" into "1 walk + 1 table_view copy": the
// strict validator still holds its own table_view BY VALUE (SC-007, no
// virtual edge — L-215-2), so it is now copy-constructed from the view
// open() already resolved instead of re-derived from the Dictionary. These
// two benchmarks settle whether that copy is actually cheaper than the
// second as_table_view() walk it replaces, on the same two dictionaries and
// with the same ->Unit(kMicrosecond) as BM_TableView_BuildFix50SP2/Fix44
// above, so the numbers are directly comparable. See table_view.hpp's
// public copy ctor (defaulted; duplicates the owned unordered maps/vectors/
// strings/enum-domain table/context keys).
// -----------------------------------------------------------------------
static void BM_TableView_CopyFix50SP2(benchmark::State& state) {
    auto const path = dict_path("FIX50SP2.xml");
    std::array<std::byte, 4u * 1024u * 1024u> buffer{};
    std::pmr::monotonic_buffer_resource mr{buffer.data(), buffer.size()};
    auto const dictionary = fixpp::dict::XmlLoader{}.load(path, &mr);
    auto const tv = dictionary.as_table_view();  // built ONCE, outside the timed loop
    for (auto _ : state) {
        auto copy = tv;
        benchmark::DoNotOptimize(copy);
    }
}
BENCHMARK(BM_TableView_CopyFix50SP2)->Unit(benchmark::kMicrosecond);

static void BM_TableView_CopyFix44(benchmark::State& state) {
    auto const path = dict_path("FIX44.xml");
    std::array<std::byte, 4u * 1024u * 1024u> buffer{};
    std::pmr::monotonic_buffer_resource mr{buffer.data(), buffer.size()};
    auto const dictionary = fixpp::dict::XmlLoader{}.load(path, &mr);
    auto const tv = dictionary.as_table_view();  // built ONCE, outside the timed loop
    for (auto _ : state) {
        auto copy = tv;
        benchmark::DoNotOptimize(copy);
    }
}
BENCHMARK(BM_TableView_CopyFix44)->Unit(benchmark::kMicrosecond);

// -----------------------------------------------------------------------
// 101-inbound-reject-fidelity (research D8, "Footprint"): the defined-tag store's bytes,
// report-only. Not timed (Iterations(1), as BM_TableView_Sizeof), and named outside the
// BM_TableView_Build* / BM_TableView_Copy* rows, so the timed row set stays the base's.
//
// `defined_tags` counts the tags `tag_defined()` answers true for, over the whole uint16_t tag
// space; `defined_tags_B` is that count times sizeof(std::uint16_t): the store's element bytes,
// which is what a copy of the view allocates for it. Derived from the public accessor rather
// than from the private member, so the figure is reproducible from the header alone. Re-derive:
// run this binary with --benchmark_filter=DefinedTags.
//
// The high-tag overlay row adds one field at the top of the tag space to the FIX44 base and no
// message: its count is the FIX44 row's plus one, and its bytes grow by one element, where a
// tag-space bitset would grow to the top tag's word.
// -----------------------------------------------------------------------
namespace {

std::size_t defined_tag_count(fixpp::dict::table_view const& tv) {
    std::size_t n = 0;
    for (std::uint32_t t = 1; t <= 0xFFFFU; ++t) {
        if (tv.tag_defined(static_cast<std::uint16_t>(t))) {
            ++n;
        }
    }
    return n;
}

void report_defined_tags(benchmark::State& state, fixpp::dict::table_view const& tv) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(tv.tag_defined(35));
    }
    auto const n = defined_tag_count(tv);
    state.counters["defined_tags"] = static_cast<double>(n);
    state.counters["defined_tags_B"] = static_cast<double>(n * sizeof(std::uint16_t));
}

std::string slurp(std::filesystem::path const& p) {
    std::ifstream in{p, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

static void BM_TableView_DefinedTags_Fix44(benchmark::State& state) {
    static std::pmr::unsynchronized_pool_resource mr;
    auto const dictionary = fixpp::dict::XmlLoader{}.load(dict_path("FIX44.xml"), &mr);
    report_defined_tags(state, dictionary.as_table_view());
}
BENCHMARK(BM_TableView_DefinedTags_Fix44)->Iterations(1);

// A FIXT session's route table for FIX 5.0 SP2 (the as_table_view(fixt_framing) form).
static void BM_TableView_DefinedTags_FixtRouteFix50SP2(benchmark::State& state) {
    static std::pmr::unsynchronized_pool_resource mr;
    auto const dictionary = fixpp::dict::XmlLoader{}.load(dict_path("FIX50SP2.xml"), &mr);
    report_defined_tags(state, dictionary.as_table_view(fixpp::dict::fixt_framing));
}
BENCHMARK(BM_TableView_DefinedTags_FixtRouteFix50SP2)->Iterations(1);

static void BM_TableView_DefinedTags_FixLatest(benchmark::State& state) {
    static std::pmr::unsynchronized_pool_resource mr;
    auto const dictionary =
        fixpp::dict::load_any(dict_path("orchestra/OrchestraFIXLatest.xml"), &mr);
    report_defined_tags(state, dictionary.as_table_view());
}
BENCHMARK(BM_TableView_DefinedTags_FixLatest)->Iterations(1);

static void BM_TableView_DefinedTags_Fix44HighTagOverlay(benchmark::State& state) {
    static std::pmr::unsynchronized_pool_resource mr;
    auto const dictionary = fixpp::dict::XmlLoader{}.load_with_overlay_from_string(
        slurp(dict_path("FIX44.xml")),
        "<fix type='FIX' major='4' minor='4'><fields>"
        "<field number='65535' name='Footprint101High' type='STRING' />"
        "</fields></fix>",
        &mr);
    report_defined_tags(state, dictionary.as_table_view());
}
BENCHMARK(BM_TableView_DefinedTags_Fix44HighTagOverlay)->Iterations(1);

BENCHMARK_MAIN();
