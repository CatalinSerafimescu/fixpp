// bench/src/placeholder_bench.cpp
// Placeholder Google Benchmark target.
// BM_Noop returns immediately — its only purpose is to verify the benchmark
// infrastructure compiles and runs end-to-end.
// Real benchmarks for perf-sensitive modules land in Phase 4+.

#include <benchmark/benchmark.h>

static void BM_Noop(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(0);
    }
}
BENCHMARK(BM_Noop);
