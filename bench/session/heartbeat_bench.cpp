// SPDX-License-Identifier: AGPL-3.0-or-later
//
// bench/session/heartbeat_bench.cpp
//
// 005-session-establishment-fsm T061 / Phase 8 — Tier-1 latency bench for the
// heartbeat-timer-fire emit decision.
//
// Ceiling (plan.md Technical-Context, [2e §6.6] / [2d §6.3]; warm cache,
// Linux/Clang/x86_64 release `-O2`):
//   BM_Session_HeartbeatTimerFire  ≤ 150 ns  (mock clock, no alloc) — DEFERRED
//
// Scope discipline (mirrors fsm_bench.cpp): the heartbeat-timer-fire decision
// path crosses the full liveness loop (Clock::sleep_until → fire → check
// idle-window → build_heartbeat → store-then-emit). The Session+mock_clock
// harness setup overhead dominates a single timer-fire measurement; per the
// bench/threading/bench_threading.cpp D-24 / RC#3 row-classification rule
// the end-to-end measurement is HARNESS-DOMINATED.
//
// The primitive cost dimensions already have benches:
//   - Clock::sleep_until                → bench/threading/bench_threading.cpp
//                                          BM_Threading_SteadyNow + family
//   - build_heartbeat()                 → BM_Session_OutboundAdminEmit
//                                          in bench/session/fsm_bench.cpp
//   - store_then_emit() / store()       → bench/session/bench_memory_store.cpp
//
// `BM_Session_HeartbeatTimerFire` as the wall-clock-of-the-full-fire-decision
// bench is the natural follow-up at the deferred-with-traceability
// session-recovery feature or `transport/` milestone, when the parts are
// stitched into one stable harness path.
//
// THIS BENCH FILE IS A SCOPE PLACEHOLDER — it compiles and registers a
// no-op-cost bench named `BM_Session_HeartbeatTimerFireScopePlaceholder` so
// the bench target builds and produces a baseline JSON entry. The placeholder
// bench measures `fixpp::session::seqnum_min` literal load — well below any
// ceiling — to keep the baselines/session/heartbeat_baseline.json file
// stable. Replace with the real wall-clock-of-fire-decision bench when the
// follow-up feature lands. ±5% regression gate per [const §VIII.2] does NOT
// apply to the placeholder.

#include <benchmark/benchmark.h>

#include <fixpp/session/seqnum.hpp>

namespace {

void BM_Session_HeartbeatTimerFireScopePlaceholder(benchmark::State& state) {
    // Placeholder: measures a trivial literal load so the bench harness
    // emits a stable baseline entry. See comment block for deferral context.
    for (auto _ : state) {
        auto x = fixpp::session::seqnum_min;
        benchmark::DoNotOptimize(x);
    }
}
BENCHMARK(BM_Session_HeartbeatTimerFireScopePlaceholder);

}  // namespace
