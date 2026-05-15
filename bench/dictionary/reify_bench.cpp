// SPDX-License-Identifier: AGPL-3.0-or-later
// bench/dictionary/reify_bench.cpp
//
// T047 — NFR-003-3 dict::reify / reify_as latency harness (seam #5/#6).
//
// NFR-003-3 ceilings:
//   dict::reify_as<Msg>:  ≤ 1 µs (20-tag msg), ≤ 10 µs (200-tag msg)
//   dict::reify (dispatch): ≤ 1.2 µs (20-tag msg)
//
// Also exercises the codegen-table lookup arm (seam #5/#6 — the codegen-driven
// dispatch switch in _dispatch/reify_dispatch_application.hpp).
//
// Baseline seeds: bench/baselines/dictionary/reify_bench.json (written on
// the first green CI run that includes this bench).
//
// ─── R6 DEFERRED NOTICE ────────────────────────────────────────────────────
// FIXPP_R6_BENCH_DEFERRED: The vendored frozen wire stub
// (include/fixpp/wire/message_view_contract.hpp) carries NO frame state.
//
// dict::reify_as<Msg> is a free function template declared in
// include/fixpp/dict/reify.hpp but its BODY is R6-deferred — there is no
// implementation until 2b lands (the body requires OffsetTable-backed frame
// bytes to actually deep-copy). This bench therefore uses
// owning_<Msg>::from_view() directly (the generated Reify.hpp defines its
// body, so it links) to measure the allocation + dispatch path overhead.
//
// from_view() IS the implementation that reify_as<Msg> will delegate to;
// the bench correctly captures the same hot path.
//
// dict::reify() also returns field-absent in R6 scope (get<35>() on the stub
// always returns dict_xml_parse_failed). These benchmarks therefore measure
// dispatch and error-path overhead only; real NFR-003-3 numbers require the
// 2b wire feature. NFR-003-3 assertion gates MUST NOT be derived from stub
// timings. See spec.md §11 R6 / plan.md Tier-1 preset matrix.
// ───────────────────────────────────────────────────────────────────────────

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <memory_resource>

#include <fixpp/core/error.hpp>
#include <fixpp/dict/reify.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/wire/message_view_contract.hpp>

// Generated reify headers (build-tree only, AC-C4).
// from_view() is defined in the generated Reify.hpp (inline out-of-class body).
#include <fixpp/v44/Reify.hpp>
#include <fixpp/v50sp2/Reify.hpp>
#include <fixpp/vt11/Reify.hpp>

namespace {

using MV = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;

// Stack-local PMR monotonic buffer sized for a typical "20-tag" message.
constexpr std::size_t k20TagBufSz  = 4  * 1024;
// 64 KiB for the "200-tag" scenario.
constexpr std::size_t k200TagBufSz = 64 * 1024;

}  // namespace

// ── BM_ReifyAs_20tag ─────────────────────────────────────────────────────────
// owning_NewOrderSingle::from_view(view, mr) — proxy for reify_as<NOS> —
// NFR ceiling ≤ 1 µs (R6 deferred; from_view is the reify_as impl path).
// NewOrderSingle is the canonical 20-tag FIX44 message.
static void BM_ReifyAs_20tag(benchmark::State& state) {
    MV mv;
    std::array<std::byte, k20TagBufSz> buf{};
    for (auto _ : state) {
        std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
        auto r = fixpp::v44::owning_NewOrderSingle::from_view(mv, &arena);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_ReifyAs_20tag);

// ── BM_ReifyAs_200tag ────────────────────────────────────────────────────────
// owning_v50sp2::NewOrderSingle::from_view(view, mr) — NFR ceiling ≤ 10 µs.
// v50sp2::NewOrderSingle has a substantially larger field set (~200 tags).
// NFR-003-3 200-tag workload. (R6 deferred)
static void BM_ReifyAs_200tag(benchmark::State& state) {
    MV mv;
    std::array<std::byte, k200TagBufSz> buf{};
    for (auto _ : state) {
        std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
        auto r = fixpp::v50sp2::owning_NewOrderSingle::from_view(mv, &arena);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_ReifyAs_200tag);

// ── BM_Reify_Dispatch_20tag ──────────────────────────────────────────────────
// dict::reify(view, version_profile, mr) — NFR ceiling ≤ 1.2 µs (R6 deferred).
// Exercises the codegen-table lookup arm (seam #5/#6): runtime dispatch reads
// MsgType from the frozen stub (returns field-absent → error propagation).
// The dispatch infrastructure (error path, profile lookup) is exercised.
static void BM_Reify_Dispatch_20tag(benchmark::State& state) {
    MV mv;
    std::array<std::byte, k20TagBufSz> buf{};

    // A v44-default profile (no FIXT session-level default).
    fixpp::dict::version_profile profile{};
    profile.default_appl = fixpp::dict::application_version::v44;

    for (auto _ : state) {
        std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
        auto r = fixpp::dict::reify(mv, profile, &arena);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Reify_Dispatch_20tag);

BENCHMARK_MAIN();
