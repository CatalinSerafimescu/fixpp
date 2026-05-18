// SPDX-License-Identifier: AGPL-3.0-or-later
// 006-async-mutex — seam #2 contended-enqueue latency bench ([2f §6.3 rows
// 2–5]: async_lock contended ≤ 80 ns; #12 dispatch≈0 / post≈25 ns extra;
// drain rows bench-harness-soft).
//
// Phase 1 placeholder — real LIFO-push + cancellation-slot-bind + drain
// workloads are authored in Phase 3 T031 against
// include/fixpp/core/sync/async_mutex.hpp.
#include <benchmark/benchmark.h>

static void BM_AsyncMutex_Contended_Placeholder(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(0);
  }
}
BENCHMARK(BM_AsyncMutex_Contended_Placeholder);

BENCHMARK_MAIN();
