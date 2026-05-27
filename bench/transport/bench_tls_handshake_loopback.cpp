// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// bench/transport/bench_tls_handshake_loopback.cpp
// T023 — [2h §6.3] row 4
//
// TLS 1.3 1-RTT loopback handshake latency.
//
// Target: ≤ 10 ms p99 (I/O-bound soft ceiling per [2h §6.3]).
// CI: flags > 2× regression.
//
// Benchmark conditions: self-signed cert, no verify_peer cost (loopback,
// known cert — measures the TLS 1.3 1-RTT overhead, not policy overhead).
//
// DEPENDENCY: asio_tls_transport (T026) + loopback TLS acceptor peer.
// Phase 3a: scaffold only. T029 wires this target after impl ships.

#include <benchmark/benchmark.h>

#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport_errors.hpp>

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// BM_TlsHandshakeLoopback
//
// Measures wall-clock time for a TLS 1.3 1-RTT handshake over 127.0.0.1:port.
// Both sides run in the same process; the acceptor runs as a background thread.
//
// Each iteration: destroy previous transport → mint fresh via factory →
// async_connect → async_handshake → measure.
//
// This matches the reconnect path: factory mints a fresh Transport per attempt.
//
// Counters: p50_ms, p95_ms, p99_ms.
// ─────────────────────────────────────────────────────────────────────────────

class TlsHandshakeFixture : public benchmark::Fixture {
public:
    void SetUp(const benchmark::State& /*state*/) override {
        // TODO (T029): construct asio_tls_transport_factory + loopback acceptor
        // thread. Load tests/tls/fixtures/leaf_rsa2048.pem + ca.pem.
    }

    void TearDown(const benchmark::State& /*state*/) override {
        // TODO (T029): stop acceptor thread; destroy factory.
    }
};

BENCHMARK_DEFINE_F(TlsHandshakeFixture, Handshake1Rtt)(benchmark::State& state) {
    // TODO (T029): replace with real handshake coroutine.
    // Each iteration:
    //   auto t = factory.make(exec, ssl_cfg, mr).value();
    //   co_await t->async_connect(loopback_ep);
    //   co_await static_cast<TlsTransport*>(t.get())->async_handshake(ssl_cfg);
    //   benchmark::DoNotOptimize(t.get());
    for (auto _ : state) {
        // Placeholder: no-op until T026 ships.
        benchmark::DoNotOptimize(state.iterations());
    }

    state.counters["p50_ms"] = 0.0;
    state.counters["p95_ms"] = 0.0;
    state.counters["p99_ms"] = 0.0;
}

// Run with fewer iterations (TLS handshake is I/O-bound and slow per session).
BENCHMARK_REGISTER_F(TlsHandshakeFixture, Handshake1Rtt)
    ->Iterations(1'000)
    ->Unit(benchmark::kMillisecond);

}  // namespace

BENCHMARK_MAIN();
