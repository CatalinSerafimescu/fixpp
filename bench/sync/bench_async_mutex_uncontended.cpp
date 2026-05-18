// SPDX-License-Identifier: AGPL-3.0-or-later
// 006-async-mutex — seam #1 uncontended-acquire latency bench ([2f §6.3 row 1]:
// async_lock uncontended ≤ 20–25 ns; unlock uncontended ≤ 15 ns).
//
// Phase 1 placeholder — real single-CAS-fast-path + unlock workloads are
// authored in Phase 3 T031 against include/fixpp/core/sync/async_mutex.hpp.
#include <benchmark/benchmark.h>

static void BM_AsyncMutex_Uncontended_Placeholder(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(0);
  }
}
BENCHMARK(BM_AsyncMutex_Uncontended_Placeholder);

BENCHMARK_MAIN();
