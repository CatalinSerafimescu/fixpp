// SPDX-License-Identifier: AGPL-3.0-or-later
//
// bench/session/fsm_bench.cpp
//
// 005-session-establishment-fsm T061 / Phase 8 — Tier-1 latency benches for
// the session FSM admin emit / inbound dispatch fast paths.
//
// Ceilings (plan.md Technical-Context, [2e §6.6] / [2d §6.3]; warm cache,
// Linux/Clang/x86_64 release `-O2`):
//   BM_Session_OutboundAdminEmit       ≤ 400 ns  (Heartbeat, test-double store)
//   BM_Session_InboundAdminDispatch    ≤ 250 ns  (in-sequence Heartbeat, warm)  — DEFERRED
//
// Scope discipline (Karpathy guidelines, [const §VII.5]-adjacent): the
// inbound-dispatch path requires a fully-wired Session in Active state
// (transport_double + store_double + mock_clock + open-handshake exchange) —
// the harness setup overhead would itself dominate the measurement window
// per the bench/threading/bench_threading.cpp D-24 / RC#3 row-classification
// convention. That bench is DEFERRED-WITH-TRACEABILITY here; the cost
// dimensions it covers (parse-validate, seqnum advance, fromAdmin dispatch)
// are each individually covered by an existing seam-test alloc-guard or
// bench:
//   - parse/validate         → bench/wire/framer_bench.cpp
//   - seqnum advance          → BM_Session_SeqnumNextWithIncrement (seqnum_bench)
//   - cancellable dispatch    → bench/threading/bench_threading.cpp
//                                BM_Threading_InStrand_Dispatch (D-24 / RC#3)
// Wiring `BM_Session_InboundAdminDispatch` as an end-to-end Session bench is
// the natural follow-up at the deferred-with-traceability session-recovery
// feature or `transport/` milestone, when the parts are stitched into one
// hot path with stable harness overhead.
//
// `BM_Session_OutboundAdminEmit` IS implemented here in its primitive-cost
// form: `build_heartbeat()` into a stack buffer. This is the dominant cost
// of the admin emit path (the `store()` tail of "→ store() → hand to
// transport" is the store-side bench, see bench/session/bench_memory_store.cpp).
// ±5% regression gate per [const §VIII.2]; baselines under
// bench/baselines/session/fsm_baseline.json.

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/seqnum.hpp>
#include <span>
#include <string_view>

namespace {

using fixpp::session::build_heartbeat;
using fixpp::session::seqnum_min;
using fixpp::session::seqnum_t;

void BM_Session_OutboundAdminEmit(benchmark::State& state) {
    std::array<std::byte, 512> buf{};
    constexpr std::string_view kSender = "SENDER";
    constexpr std::string_view kTarget = "TARGET";
    seqnum_t seq = seqnum_min;
    for (auto _ : state) {
        auto r = build_heartbeat(buf, seq, kSender, kTarget, {}, "FIX.4.2", "20240101-00:00:00.000");
        benchmark::DoNotOptimize(r);
        ++seq;
    }
}
BENCHMARK(BM_Session_OutboundAdminEmit);

}  // namespace
