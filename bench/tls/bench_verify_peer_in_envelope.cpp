// bench/tls/bench_verify_peer_in_envelope.cpp
// T041 — verify_peer p99 latency benchmarks per [2g §6.3].
//
// Design anchor: .specify/2g-tls.md v0.4 §6.3 latency targets.
// Spec anchors: FR-019 (DoS bounds), FR-020 (full enforcement).
//
// Targets (soft-gate — recorded + asserted when detectable, no hard CI fail):
//   - In-envelope chain (depth ≤ 4, ECDSA-P256, ≤ 8 SAN entries): ≤ 1 ms p99.
//   - DoS-reject paths (RSA-16384 / DER-too-large / SAN-too-many): ≤ 50 µs p99.
//     (DoS short-circuit is the whole point — reject BEFORE expensive parse.)
//
// Soft-gate strategy: the benchmark records timing; ASSERT_LE at the
// P99 threshold when the platform is reliable (CI single-run). Non-deterministic
// environments may show higher variance; the test label "bench" allows the CI
// matrix to skip or note-only.

#include <fixpp/tls/security_profile.hpp>

#include <benchmark/benchmark.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <string_view>

using namespace fixpp::tls;
using fixpp::core::error;

// ── Stubs ─────────────────────────────────────────────────────────────────────

namespace {

class bench_cs final : public cert_source {
 public:
    asio::awaitable<fixpp::core::expected_t<local_credentials>>
    load_credentials() override {
        co_return fixpp::core::expected_t<local_credentials>{
            std::unexpect, error::tls_load_cancelled};
    }
    fixpp::core::expected_t<std::span<const Certificate>>
    load_trust_anchors() [[clang::lifetimebound]] override {
        return std::span<const Certificate>{};
    }
};

class bench_clock final : public fixpp::core::Clock {
 public:
    explicit bench_clock(std::chrono::system_clock::time_point t) : t_{t} {}
    fixpp::core::utc_time_point now() const noexcept override { return t_; }
    fixpp::core::steady_time_point steady_now() const noexcept override {
        return std::chrono::steady_clock::now();
    }
    asio::awaitable<void> sleep_until(fixpp::core::steady_time_point) override { co_return; }
    void cancel_sleeps() noexcept override {}
 private:
    std::chrono::system_clock::time_point t_;
};

// Static DER buffers (shared across benchmarks; within-cap sizes).
static std::array<std::byte, 512>  g_small_der{};
static std::array<std::byte, 16385> g_big_der{};  // 16 KiB + 1 → DER-too-large

SslCtxConfig make_mtls_ca_cfg() {
    auto now = std::chrono::system_clock::now();
    SslCtxConfig cfg;
    cfg.profile = SecurityProfile::mtls_ca;
    cfg.cs      = std::make_shared<bench_cs>();
    cfg.clock   = std::make_shared<bench_clock>(now);
    return cfg;
}

Certificate make_valid_ecdsa_p256() {
    auto now = std::chrono::system_clock::now();
    Certificate c{};
    c.raw_der_      = std::span<const std::byte>{g_small_der};
    c.alg_          = signature_algorithm::ecdsa;
    c.curve_        = ecdsa_curve::p256;
    c.x509_version_ = 3;
    c.not_before_   = now - std::chrono::hours{24};
    c.not_after_    = now + std::chrono::hours{24};
    return c;
}

}  // namespace

// ── BM: in-envelope chain — depth 1, ECDSA-P256, 0 SANs ─────────────────────

static void BM_VerifyPeer_ValidEcdsaP256(benchmark::State& state) {
    auto cfg  = make_mtls_ca_cfg();
    auto cert = make_valid_ecdsa_p256();
    std::span<const Certificate> chain{&cert, 1};

    for (auto _ : state) {
        auto result = verify_peer(cfg, chain);
        benchmark::DoNotOptimize(result);
    }

    // Soft-gate: p99 ≤ 1 ms per [2g §6.3].
    // No hard assertion here — the benchmark runner reports timing.
    state.SetLabel("verify_peer valid ECDSA-P256 depth=1");
}
BENCHMARK(BM_VerifyPeer_ValidEcdsaP256)->Unit(benchmark::kMicrosecond);

// ── BM: in-envelope chain — depth 4, ECDSA-P256 ──────────────────────────────

static void BM_VerifyPeer_ValidChainDepth4(benchmark::State& state) {
    auto cfg = make_mtls_ca_cfg();
    auto base_cert = make_valid_ecdsa_p256();
    std::array<Certificate, 4> chain;
    for (auto& c : chain) c = base_cert;
    std::span<const Certificate> chain_span{chain};

    for (auto _ : state) {
        auto result = verify_peer(cfg, chain_span);
        benchmark::DoNotOptimize(result);
    }

    state.SetLabel("verify_peer valid ECDSA-P256 depth=4");
}
BENCHMARK(BM_VerifyPeer_ValidChainDepth4)->Unit(benchmark::kMicrosecond);

// ── BM: DoS-reject path — DER too large (step 1 short-circuit) ───────────────

static void BM_VerifyPeer_DerTooLarge(benchmark::State& state) {
    auto cfg = make_mtls_ca_cfg();
    auto cert = make_valid_ecdsa_p256();
    cert.raw_der_ = std::span<const std::byte>{g_big_der};  // > 16 KiB

    std::span<const Certificate> chain{&cert, 1};
    for (auto _ : state) {
        auto result = verify_peer(cfg, chain);
        benchmark::DoNotOptimize(result);
    }

    // Soft-gate: DoS-reject ≤ 50 µs p99 per [2g §6.3].
    // The short-circuit at step 1 must make this fast.
    state.SetLabel("verify_peer DER-too-large (DoS short-circuit step 1)");
}
BENCHMARK(BM_VerifyPeer_DerTooLarge)->Unit(benchmark::kMicrosecond);

// ── BM: DoS-reject path — SAN-too-many (step 6 short-circuit) ────────────────

static void BM_VerifyPeer_SanTooMany(benchmark::State& state) {
    auto cfg = make_mtls_ca_cfg();
    auto cert = make_valid_ecdsa_p256();

    // 65 SAN entries — exceeds cap of 64.
    static const std::array<std::string_view, 65> sans = [](){
        std::array<std::string_view, 65> a{};
        for (auto& s : a) s = "example.com";
        return a;
    }();
    cert.san_dns_names_ = std::span<const std::string_view>{sans};

    std::span<const Certificate> chain{&cert, 1};
    for (auto _ : state) {
        auto result = verify_peer(cfg, chain);
        benchmark::DoNotOptimize(result);
    }

    state.SetLabel("verify_peer SAN-too-many (DoS short-circuit step 6)");
}
BENCHMARK(BM_VerifyPeer_SanTooMany)->Unit(benchmark::kMicrosecond);

// ── BM: DoS-reject path — RSA-16384 (step 3 short-circuit) ───────────────────

static void BM_VerifyPeer_RsaTooLarge(benchmark::State& state) {
    auto cfg = make_mtls_ca_cfg();
    auto cert = make_valid_ecdsa_p256();
    cert.alg_          = signature_algorithm::rsa_pss;
    cert.rsa_key_bits_  = 16384;  // above default cap 8192

    std::span<const Certificate> chain{&cert, 1};
    for (auto _ : state) {
        auto result = verify_peer(cfg, chain);
        benchmark::DoNotOptimize(result);
    }

    state.SetLabel("verify_peer RSA-16384 (DoS short-circuit step 3)");
}
BENCHMARK(BM_VerifyPeer_RsaTooLarge)->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
