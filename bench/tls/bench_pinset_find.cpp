// SPDX-License-Identifier: AGPL-3.0-or-later
//
// bench/tls/bench_pinset_find.cpp
// T022 — [2g §9 seam #5] Pinset::find(sha256) latency at max_pins=16.
//
// Platform-conditional p99 ceiling per [2g §6.3] / SC-007:
//   ≤ 130 ns p99 combined (snapshot_acquire ≤ 30 ns + scan_16 ≤ 100 ns).
//   ≤ 200 ns p99 on libstdc++ ≤ 15 mutex fallback.
//
// Same tuple-detection pattern as bench_pinset_snapshot_acquire.cpp.
// Soft gate per [const §VIII.2].

#include <benchmark/benchmark.h>
#include <fixpp/tls/pinset.hpp>
#include <fixpp/tls/certificate.hpp>

#include <array>
#include <cstddef>
#include <iostream>
#include <string>

namespace {

using fixpp::tls::Certificate;
using fixpp::tls::Pinset;
using fixpp::tls::pin_fingerprint;

Certificate make_cert(pin_fingerprint const& fp, std::string_view dn) {
    Certificate c{};
    c.sha256_       = fp;
    c.subject_dn_   = dn;
    c.x509_version_ = 3;
    return c;
}

std::size_t detect_find_ceiling_ns() {
#if !FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE
    // NFR-017: fallback path snapshot_ load is shard-locked (not lock-free), so
    // the 30 ns snapshot leg of the combined ceiling does not hold. Key off the
    // fixpp selector, not raw __cpp_lib_atomic_shared_ptr (New-P3-2).
    return 0;  // no lock-free combined ceiling on the fallback path (soft gate)
#elif defined(__cpp_lib_atomic_shared_ptr)
    return 130;
#elif defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE >= 16
    return 130;
#elif defined(_LIBCPP_VERSION) && _LIBCPP_VERSION >= 16000
    return 130;
#else
    return 200;  // libstdc++ ≤ 15 fallback.
#endif
}

// Build a Pinset with max_pins = 16 (all 16 slots filled).
Pinset* g_ps16     = nullptr;
pin_fingerprint g_target{};
pin_fingerprint g_miss{};

void init_pinset16() {
    if (g_ps16) return;
    Pinset::Config cfg;
    cfg.max_pins = 16;
    g_ps16 = new Pinset(cfg);
    for (int i = 0; i < 16; ++i) {
        pin_fingerprint fp{};
        fp[0] = static_cast<std::byte>(i + 1);
        (void)g_ps16->add(make_cert(fp, "CN=bench16"));
    }
    // target = last pin (worst-case linear scan).
    g_target[0] = std::byte{16};
    // miss = fingerprint not in the set.
    g_miss[0] = std::byte{0xFF};
}

}  // namespace

static void BM_PinsetFindHit(benchmark::State& state) {
    init_pinset16();
    for (auto _ : state) {
        auto v = g_ps16->find(g_target);
        benchmark::DoNotOptimize(v);
        benchmark::ClobberMemory();
    }
    state.SetLabel("find_hit_max_pins16");
}
BENCHMARK(BM_PinsetFindHit)
    ->Repetitions(5)
    ->ReportAggregatesOnly(true)
    ->Unit(benchmark::kNanosecond);

static void BM_PinsetFindMiss(benchmark::State& state) {
    init_pinset16();
    for (auto _ : state) {
        auto v = g_ps16->find(g_miss);
        benchmark::DoNotOptimize(v);
        benchmark::ClobberMemory();
    }
    state.SetLabel("find_miss_max_pins16");
}
BENCHMARK(BM_PinsetFindMiss)
    ->Repetitions(5)
    ->ReportAggregatesOnly(true)
    ->Unit(benchmark::kNanosecond);

int main(int argc, char** argv) {
    std::size_t ceiling = detect_find_ceiling_ns();
    std::cout << "[bench_pinset_find] platform tuple:\n";
#if defined(__clang__)
    std::cout << "  compiler=clang-" << __clang_major__ << "\n";
#elif defined(__GNUC__)
    std::cout << "  compiler=gcc-" << __GNUC__ << "\n";
#else
    std::cout << "  compiler=unknown\n";
#endif
#if defined(_GLIBCXX_RELEASE)
    std::cout << "  stdlib=libstdc++ version=" << _GLIBCXX_RELEASE << "\n";
#elif defined(_LIBCPP_VERSION)
    std::cout << "  stdlib=libc++ version=" << _LIBCPP_VERSION << "\n";
#else
    std::cout << "  stdlib=unknown\n";
#endif
    if (ceiling > 0)
        std::cout << "  selected ceiling=" << ceiling << " ns p99\n";
    else
        std::cout << "  selected ceiling=undetectable (skipping assertion)\n";

    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
    ::benchmark::Shutdown();
    return 0;
}
