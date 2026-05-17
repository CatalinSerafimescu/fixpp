// SPDX-License-Identifier: AGPL-3.0-or-later
// bench/wire/check_alive_bench.cpp
//
// T052 — D-8 [2b §10 Q3] Micro-bench for View::check_alive() generation-counter
// overhead on a 200-tag read loop.
//
// Measures:
//   BM_CheckAlive_200tag — one check_alive() call per simulated field access,
//   200 calls per iteration. In debug builds (NDEBUG not defined) this does a
//   real thread-local generation slot lookup; in release (NDEBUG) it is a
//   constexpr no-op that the compiler optimises away.
//
//   BM_Bytes_200tag — a baseline of 200 bytes() calls on the same view WITHOUT
//   a check_alive() call (serves as the release-equivalent denominator to
//   estimate per-access overhead).
//
// Results recorded in .specify/decisions/004-wire-codec-verify.md §D-8.
// Verdict rule: if BM_CheckAlive_200tag > 2× BM_Bytes_200tag then the per-N-
// access sampling fallback described in [2b §10 Q3] is triggered.
//
// NOTE: This bench is compiled as part of the standard linux-clang-debug
// preset where NDEBUG is NOT set (i.e., check_alive() is ACTIVE and does the
// real generation-slot lookup). A release-mode run would show zero overhead
// because the method body is `constexpr void check_alive() const noexcept {}`
// — a literal no-op — so the "release" row in the D-8 table below is derived
// analytically rather than by a separate preset run.

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <span>

#include <fixpp/wire/view.hpp>

namespace {

// Expose the protected check_alive() for the bench via a minimal subclass.
// This is bench-only code; production code never calls check_alive() directly.
class ViewProbe : public fixpp::wire::View {
public:
    using View::check_alive;

    explicit ViewProbe(fixpp::wire::detail::generation_token gen) noexcept
        : View{nullptr, 0, gen} {}
};

}  // namespace

// ── BM_CheckAlive_200tag ──────────────────────────────────────────────────────
// 200 check_alive() calls per iteration (one per simulated field access on a
// 200-tag Instrument-heavy message). In debug builds this calls the real
// implementation (thread-local array lookup + generation comparison).
static void BM_CheckAlive_200tag(benchmark::State& state) {
    // pool_id = 1, gen = 0 → matches the live slot for a freshly-constructed
    // pool (pool_generation_slot(1) points to a thread_local 0 by default).
    fixpp::wire::detail::generation_token tok{.pool_id = 1, .gen = 0};
    ViewProbe probe{tok};

    for (auto _ : state) {
        for (int i = 0; i < 200; ++i) {
            probe.check_alive();
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * 200);
    // Report per-access time so the 2× verdict is easy to read.
    state.counters["ns_per_access"] =
        benchmark::Counter(static_cast<double>(state.iterations() * 200),
                           benchmark::Counter::kIsRate |
                               benchmark::Counter::kInvert);
}
BENCHMARK(BM_CheckAlive_200tag);

// ── BM_Bytes_200tag ───────────────────────────────────────────────────────────
// 200 bytes() calls per iteration WITHOUT check_alive() — this is the release-
// equivalent baseline (bytes() has no generation check; check_alive() in
// release is a constexpr no-op identical to this path).
static void BM_Bytes_200tag(benchmark::State& state) {
    fixpp::wire::detail::generation_token tok{.pool_id = 0, .gen = 0};
    ViewProbe probe{tok};

    for (auto _ : state) {
        for (int i = 0; i < 200; ++i) {
            auto b = probe.bytes();
            benchmark::DoNotOptimize(b);
        }
    }
    state.SetItemsProcessed(state.iterations() * 200);
    state.counters["ns_per_access"] =
        benchmark::Counter(static_cast<double>(state.iterations() * 200),
                           benchmark::Counter::kIsRate |
                               benchmark::Counter::kInvert);
}
BENCHMARK(BM_Bytes_200tag);

BENCHMARK_MAIN();
