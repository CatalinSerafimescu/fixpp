// SPDX-License-Identifier: AGPL-3.0-or-later
// bench/codegen/typed_accessor_bench.cpp
//
// T045 — NFR-003-1 typed-accessor latency harness (seam #3).
//
// Measures wall-clock time per call for the main accessor categories on a
// v44::NewOrderSingle flyweight over the frozen R6 wire stub:
//   • string/int/char accessors  → NFR ceiling ≤ 20 ns
//   • price(mr) decimal accessor → NFR ceiling ≤ 75 ns
//   • field_value(uint16_t)      → NFR ceiling ≤ 25 ns
//
// ─── R6 DEFERRED NOTICE ────────────────────────────────────────────────────
// FIXPP_R6_BENCH_DEFERRED: The vendored frozen wire stub
// (include/fixpp/wire/message_view_contract.hpp) carries NO frame state —
// every get<Tag>() returns field-absent unconditionally. These benchmarks
// therefore measure the overhead of the accessor dispatch path alone (null
// check + unexpected return); the real hot-path numbers (field-present, with
// OffsetTable lookup) require the 2b wire feature to land and replace the stub
// body. NFR-003-1 assertion gates MUST NOT be derived from these stub timings.
//
// This harness is wired into the Tier-1 release preset bench run so the build
// path is always exercised. Latency assertions (≤20 ns / ≤75 ns / ≤25 ns)
// are recorded here as documentation; they CANNOT be enforced until 2b.
// See spec.md §11 R6 / plan.md Tier-1 preset matrix.
// ───────────────────────────────────────────────────────────────────────────

#include <benchmark/benchmark.h>

#include <cstdint>
#include <fixpp/v44/Messages.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <memory_resource>

namespace {

// Shared fixture: a default-constructed (stub) MessageView and a monotonic
// arena for the decimal (price) accessor.
using MV = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;

}  // namespace

// ── BM_StringAccessor ────────────────────────────────────────────────────────
// cl_ord_id() → expected_t<string_view>  — NFR ceiling ≤ 20 ns (R6 deferred).
static void BM_StringAccessor(benchmark::State& state) {
    MV mv;
    fixpp::v44::NewOrderSingle nos(mv);
    for (auto _ : state) {
        auto r = nos.cl_ord_id();
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_StringAccessor);

// ── BM_CharAccessor ──────────────────────────────────────────────────────────
// side() → expected_t<char>  — NFR ceiling ≤ 20 ns (R6 deferred).
static void BM_CharAccessor(benchmark::State& state) {
    MV mv;
    fixpp::v44::NewOrderSingle nos(mv);
    for (auto _ : state) {
        auto r = nos.side();
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_CharAccessor);

// ── BM_IntAccessor ───────────────────────────────────────────────────────────
// msg_seq_num() → expected_t<int32_t>  — NFR ceiling ≤ 20 ns (R6 deferred).
static void BM_IntAccessor(benchmark::State& state) {
    MV mv;
    fixpp::v44::NewOrderSingle nos(mv);
    for (auto _ : state) {
        auto r = nos.msg_seq_num();
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_IntAccessor);

// ── BM_DecimalAccessor ───────────────────────────────────────────────────────
// price(mr) → expected_t<decimal_t>  — NFR ceiling ≤ 75 ns (R6 deferred).
// The PMR arena is reset each iteration to give a stable allocation baseline.
static void BM_DecimalAccessor(benchmark::State& state) {
    MV mv;
    fixpp::v44::NewOrderSingle nos(mv);
    std::pmr::monotonic_buffer_resource arena;
    for (auto _ : state) {
        auto r = nos.price(&arena);
        benchmark::DoNotOptimize(r);
        // Reset arena so each iteration starts clean (no growing footprint).
        arena.release();
    }
}
BENCHMARK(BM_DecimalAccessor);

// ── BM_FieldValueForwarder ────────────────────────────────────────────────────
// field_value(uint16_t tag) — NFR ceiling ≤ 25 ns (R6 deferred).
static void BM_FieldValueForwarder(benchmark::State& state) {
    MV mv;
    fixpp::v44::NewOrderSingle nos(mv);
    for (auto _ : state) {
        auto r = nos.field_value(11);  // ClOrdID tag
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_FieldValueForwarder);

BENCHMARK_MAIN();
